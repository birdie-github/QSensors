// SPDX-License-Identifier: GPL-3.0-only
#include "tray/traycontroller.h"
#include "tray/traysettingsdialog.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QIcon>
#include <QKeySequence>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QSystemTrayIcon>

#include <algorithm>
#include <cmath>

namespace qsensors {
namespace {

QString compactValue(const SensorDescriptor &descriptor, double value)
{
    QString text;
    switch (descriptor.kind) {
    case SensorKind::Temperature:
    case SensorKind::Humidity: text = QString::number(value, 'f', 0); break;
    case SensorKind::Fan: text = QString::number(value / 1000.0, 'f', 1); break;
    case SensorKind::Voltage: text = QString::number(value, 'f', 1); break;
    default: text = QString::number(value, 'g', 3); break;
    }
    if (text.size() > 5)
        text = QString::number(descriptor.kind == SensorKind::Fan ? value / 1000.0 : value, 'g', 2);
    return text;
}

QIcon numericIcon(const QString &text, const QColor &color, QFont font)
{
    QIcon icon;
    const QColor outline = color.lightness() > 128 ? QColor(Qt::black) : QColor(Qt::white);
    for (int size : {16, 22, 24, 32, 48, 64, 128}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        font.setPixelSize(size);
        font.setBold(true);
        QPainterPath path;
        path.addText(0, 0, font, text);
        const QRectF bounds = path.boundingRect();
        if (bounds.isEmpty())
            continue;
        const qreal scale = std::min((size * 0.90) / bounds.width(), (size * 0.82) / bounds.height());
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(size / 2.0, size / 2.0);
        painter.scale(scale, scale);
        painter.translate(-bounds.center());
        painter.setPen(QPen(outline, (size * 0.045) / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(color);
        painter.drawPath(path);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

TrayController::TrayController(QWidget *window, QObject *parent)
    : QObject(parent), m_window(window), m_menu(std::make_unique<QMenu>())
{
    m_showAction = m_menu->addAction(tr("Show Dashboard"));
    m_hideAction = m_menu->addAction(tr("Hide Dashboard"));
    m_menu->addSeparator();
    m_settingsAction = m_menu->addAction(tr("Settings…"));
    m_rescanAction = m_menu->addAction(tr("Rescan"));
    m_readingsMenu = m_menu->addMenu(tr("Selected Sensors"));
    m_menu->addSeparator();
    m_aboutAction = m_menu->addAction(tr("About QSensors"));
    m_quitAction = m_menu->addAction(tr("Quit"));
    m_showAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    m_rescanAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_showAction, &QAction::triggered, this, &TrayController::showRequested);
    connect(m_hideAction, &QAction::triggered, this, &TrayController::hideRequested);
    connect(m_settingsAction, &QAction::triggered, this, &TrayController::openSettings);
    connect(m_rescanAction, &QAction::triggered, this, &TrayController::rescanRequested);
    connect(m_quitAction, &QAction::triggered, this, &TrayController::quitRequested);
    connect(m_aboutAction, &QAction::triggered, this, [this] {
        emit showRequested();
        QMessageBox::about(m_window, tr("About QSensors"),
            tr("QSensors %1\nA Qt hardware-sensor monitor inspired by KSensors.\nLicensed under GPL version 3.")
                .arg(QCoreApplication::applicationVersion()));
    });
    m_selected = QSettings().value(QStringLiteral("Tray/sensors")).toStringList();
    m_selected.removeAll(QString());
    m_selected.removeDuplicates();
    std::sort(m_selected.begin(), m_selected.end());
    m_hostWatch.setInterval(3000);
    m_hostWatch.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_hostWatch, &QTimer::timeout, this, &TrayController::checkAvailability);
    qApp->installEventFilter(this);
}

TrayController::~TrayController()
{
    qApp->removeEventFilter(this);
    // Delete icons before their shared menu; QSystemTrayIcon does not own it.
    for (const auto &entry : m_entries)
        delete entry.icon;
    delete m_fallback;
    if (m_settings)
        delete m_settings.data();
}

void TrayController::start()
{
    m_available = QSystemTrayIcon::isSystemTrayAvailable();
    reconcileIcons();
    emit availabilityChanged(m_available);
    setDashboardVisible(m_dashboardVisible);
    m_hostWatch.start();
}

QSystemTrayIcon *TrayController::createIcon()
{
    auto *icon = new QSystemTrayIcon(this);
    icon->setContextMenu(m_menu.get());
    connect(icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            emit toggleRequested();
    });
    return icon;
}

void TrayController::checkAvailability()
{
    const bool available = QSystemTrayIcon::isSystemTrayAvailable();
    m_hideAction->setEnabled(available && m_dashboardVisible);
    if (available != m_available) {
        m_available = available;
        emit availabilityChanged(available);
        if (!available && !m_dashboardVisible)
            emit showRequested();
    }
}

void TrayController::setDashboardVisible(bool visible)
{
    m_dashboardVisible = visible;
    m_showAction->setEnabled(!visible);
    m_hideAction->setEnabled(visible && m_available);
}

void TrayController::setSelectedIds(QStringList ids)
{
    ids.removeAll(QString());
    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end());
    if (ids == m_selected)
        return;
    m_selected = ids;
    QSettings().setValue(QStringLiteral("Tray/sensors"), ids);
    reconcileIcons();
    emit selectionChanged(ids);
}

void TrayController::reconcileIcons()
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (m_selected.contains(it.key())) {
            ++it;
            continue;
        }
        delete it->icon;
        delete it->reading;
        it = m_entries.erase(it);
    }
    if (m_selected.isEmpty()) {
        if (!m_fallback) {
            m_fallback = createIcon();
            m_fallback->setIcon(QApplication::windowIcon());
            m_fallback->setToolTip(tr("QSensors — hardware sensor monitor"));
            m_fallback->show();
        }
    } else {
        delete m_fallback;
        m_fallback = nullptr;
        for (const auto &id : m_selected) {
            if (!m_entries.contains(id)) {
                Entry entry;
                entry.icon = createIcon();
                entry.reading = m_readingsMenu->addAction(id);
                entry.reading->setEnabled(false);
                m_entries.insert(id, entry);
            }
            auto &entry = m_entries[id];
            updateEntry(id, entry);
            entry.icon->show(); // Qt automatically registers with a later/restarted host.
        }
    }
    m_readingsMenu->menuAction()->setVisible(!m_selected.isEmpty());
}

void TrayController::applySnapshot(const SensorSnapshot &snapshot)
{
    if (snapshot.rescanned) {
        m_catalog = snapshot.descriptors;
        // Retain metadata only for selected missing channels, not every old device.
        QHash<QString, SensorDescriptor> descriptors;
        for (const auto &descriptor : m_catalog)
            descriptors.insert(descriptor.id, descriptor);
        for (const auto &id : m_selected) {
            if (!descriptors.contains(id) && m_descriptors.contains(id))
                descriptors.insert(id, m_descriptors.value(id));
        }
        m_descriptors = descriptors;
        if (m_settings)
            m_settings->updateCatalog(m_catalog);
    }
    m_samples.clear();
    for (const auto &sample : snapshot.samples) {
        if (m_selected.contains(sample.id))
            m_samples.insert(sample.id, sample);
    }
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        updateEntry(it.key(), it.value());
}

void TrayController::updateEntry(const QString &id, Entry &entry)
{
    const auto descriptor = m_descriptors.value(id);
    const auto sample = m_samples.value(id);
    const bool available = m_descriptors.contains(id) && sample.available && std::isfinite(sample.value) && sample.timestamp.isValid();
    const QString text = available ? compactValue(descriptor, sample.value) : QStringLiteral("—");
    const QString label = descriptor.label.isEmpty() ? id : descriptor.label;
    const QString value = available ? tr("%1 %2").arg(QLocale().toString(sample.value, 'g', 8), descriptor.unit)
                                   : tr("Unavailable");
    QString tooltip = tr("%1\n%2\n%3").arg(label, descriptor.device, value);
    if (!available)
        tooltip += QStringLiteral("\n") + (sample.error.isEmpty() ? tr("No current reading") : sample.error);
    if (tooltip != entry.tooltip) {
        entry.tooltip = tooltip;
        entry.icon->setToolTip(tooltip);
        entry.reading->setText(tr("%1 — %2: %3").arg(label, descriptor.device, value));
    }
    const QColor color = QApplication::palette().color(QPalette::Text);
    const QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QString key = text + QStringLiteral("|") + color.name(QColor::HexArgb) + font.toString();
    if (key != entry.renderKey) {
        entry.renderKey = key;
        entry.icon->setIcon(numericIcon(text, color, font));
    }
}

void TrayController::openSettings()
{
    emit showRequested();
    if (m_settings) {
        m_settings->show();
        m_settings->raise();
        m_settings->activateWindow();
        return;
    }
    m_settings = new TraySettingsDialog(m_catalog, m_selected, m_window);
    connect(m_settings.data(), &QDialog::accepted, this, [this] {
        if (m_settings)
            setSelectedIds(m_settings->selectedIds());
    });
    m_settings->show();
}

bool TrayController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::ApplicationFontChange) {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            updateEntry(it.key(), it.value());
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace qsensors

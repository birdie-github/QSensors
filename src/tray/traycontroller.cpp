// SPDX-License-Identifier: GPL-3.0-only
#include "tray/traycontroller.h"
#include "rendering/valueformat.h"
#include "settings/sensorpreferences.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QIcon>
#include <QIconEngine>
#include <QKeySequence>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSystemTrayIcon>

#include <algorithm>
#include <cmath>
#include <functional>

namespace qsensors {
namespace {

QString compactValue(const SensorDescriptor &descriptor, double value)
{
    if (descriptor.kind == SensorKind::Frequency) {
        // Frequency providers and calibration use canonical MHz. Include the
        // suffix in the four-character budget and promote rounding overflows.
        if (!std::isfinite(value) || value < 0.001 || value > 999.0 * 1000)
            return QStringLiteral("ERR");
        if (value < 1) {
            const double kilohertz = std::round(value * 1000);
            if (kilohertz < 1000)
                return QString::number(kilohertz, 'f', 0) + QStringLiteral("K");
        }
        const double megahertz = std::round(value);
        if (megahertz < 10000) return QString::number(megahertz, 'f', 0);
        return QString::number(std::round(value / 1000), 'f', 0) + QStringLiteral("G");
    }
    if (descriptor.kind == SensorKind::Memory) {
        const auto formatted = formatSensorValue(descriptor, value, true);
        return formatted.number + formatted.unit;
    }
    QString text;
    switch (descriptor.kind) {
    case SensorKind::Temperature:
    case SensorKind::Humidity:
    case SensorKind::Percentage: text = QString::number(value, 'f', 0); break;
    case SensorKind::Fan: text = QString::number(value / 1000.0, 'f', 1); break;
    case SensorKind::Voltage: text = QString::number(value, 'f', 1); break;
    default: text = QString::number(value, 'g', 3); break;
    }
    if (text.size() > 5)
        text = QString::number(descriptor.kind == SensorKind::Fan ? value / 1000.0 : value, 'g', 2);
    return text;
}

// Render at the requested device resolution: paths and painter transforms lose
// bitmap strikes and font hinting, particularly at small tray sizes.
QPixmap numericPixmap(const QSize &size, const QString &text, const QColor &color,
                      QFont font, const SensorPreferences &preferences)
{
    if (size.isEmpty()) return {};
    QPixmap pixmap(size);
    pixmap.fill(preferences.trayTransparent ? QColor(Qt::transparent)
        : preferences.trayBackground.isValid() ? preferences.trayBackground : QApplication::palette().color(QPalette::Base));

    font.setBold(false);
    font.setItalic(false);
    font.setStyleName(QString());
    font.setHintingPreference(QFont::PreferFullHinting);
    const bool bitmap = !QFontDatabase::isSmoothlyScalable(font.family());
    font.setStyleStrategy(QFont::StyleStrategy(QFont::NoSubpixelAntialias
        | (bitmap ? QFont::PreferBitmap | QFont::NoAntialias : QFont::PreferMatch)));

    const int edge = std::min(size.width(), size.height());
    const int shadow = preferences.trayShadow && edge >= 8 ? 1 : 0;
    const QSize available = size - QSize(shadow, shadow);
    // Fit ink bounds, which can be shorter than the font's nominal em size.
    // Select a font at this resolution; never scale painted glyphs.
    const int requested = edge * 2;
    QRect bounds;
    auto fits = [&] {
        bounds = QFontMetrics(font, &pixmap).tightBoundingRect(text);
        return !bounds.isEmpty() && bounds.width() <= available.width()
            && bounds.height() <= available.height();
    };
    auto fitFont = [&](bool nativeBitmap) {
        if (nativeBitmap) {
            auto sizes = QFontDatabase::smoothSizes(font.family(), QFontDatabase::styleString(font));
            std::sort(sizes.begin(), sizes.end(), std::greater<int>());
            for (int points : sizes) {
                font.setPointSize(points);
                if (fits()) return true;
            }
        } else {
            for (int pixels = requested; pixels >= 1; --pixels) {
                font.setPixelSize(pixels);
                if (fits()) return true;
            }
        }
        return false;
    };
    if (!fitFont(bitmap)) {
        // Some bitmap fonts have no strike small enough for a long value.
        // Preserve the complete reading without resampling bitmap glyphs.
        font = QFont(QStringLiteral("sans-serif"));
        font.setHintingPreference(QFont::PreferFullHinting);
        font.setStyleStrategy(QFont::StyleStrategy(QFont::PreferOutline | QFont::NoSubpixelAntialias));
        if (!fitFont(false)) return pixmap;
    }
    // Integral device-pixel baselines retain bitmap sharpness. Reserve room
    // for the shadow, then paint the foreground last without any outline.
    const QPoint baseline((available.width() - bounds.width()) / 2 - bounds.left(),
                          (available.height() - bounds.height()) / 2 - bounds.top());
    QPainter painter(&pixmap);
    painter.setFont(font);
    if (shadow) {
        painter.setPen(color.lightness() > 128 ? QColor(Qt::black) : QColor(Qt::white));
        painter.drawText(baseline + QPoint(shadow, shadow), text);
    }
    painter.setPen(color);
    painter.drawText(baseline, text);
    return pixmap;
}

class NumericIconEngine final : public QIconEngine {
public:
    NumericIconEngine(const QString &text, const QColor &color, const QFont &font,
                      const SensorPreferences &preferences)
        : m_text(text), m_color(color), m_font(font), m_preferences(preferences) {}

    QIconEngine *clone() const override { return new NumericIconEngine(*this); }
    bool isNull() override { return m_text.isEmpty(); }
    QPixmap pixmap(const QSize &size, QIcon::Mode, QIcon::State) override
    {
        return numericPixmap(size, m_text, m_color, m_font, m_preferences);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        // Qt 6.8+ passes logical size separately from the device-pixel ratio.
        QPixmap result = pixmap(size * scale, mode, state);
        result.setDevicePixelRatio(scale);
        return result;
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        painter->drawPixmap(rect.topLeft(), scaledPixmap(rect.size(), mode, state,
                                                       painter->device()->devicePixelRatioF()));
    }
    QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override
    {
        // StatusNotifier hosts request a list of raster images rather than
        // painting the QIcon. Supply common panel sizes without resampling.
        QList<QSize> sizes;
        for (int edge : {16, 20, 22, 24, 28, 32, 40, 48, 64, 96, 128})
            sizes.append(QSize(edge, edge));
        return sizes;
    }
private:
    QString m_text;
    QColor m_color;
    QFont m_font;
    SensorPreferences m_preferences;
};

QIcon numericIcon(const QString &text, const QColor &color, const QFont &font, const SensorPreferences &preferences)
{
    return QIcon(new NumericIconEngine(text, color, font, preferences));
}

} // namespace

TrayController::TrayController(QWidget *window, SensorPreferencesStore *preferences, QObject *parent)
    : QObject(parent), m_window(window), m_preferences(preferences), m_menu(std::make_unique<QMenu>())
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
    m_selected = preferences->trayIds();
    connect(preferences, &SensorPreferencesStore::changed, this, [this] {
        setSelectedIds(m_preferences->trayIds());
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
            updateEntry(it.key(), it.value());
    });
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
    ids.erase(std::remove_if(ids.begin(), ids.end(), [](const QString &id) { return !sensorSupportsTray(id); }), ids.end());
    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end());
    if (ids == m_selected)
        return;
    m_selected = ids;
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
        // Retain metadata only for selected missing channels, not every old device.
        QHash<QString, SensorDescriptor> descriptors;
        for (const auto &descriptor : snapshot.descriptors)
            descriptors.insert(descriptor.id, descriptor);
        for (const auto &id : m_selected) {
            if (!descriptors.contains(id) && m_descriptors.contains(id))
                descriptors.insert(id, m_descriptors.value(id));
        }
        m_descriptors = descriptors;
    }
    if (!snapshot.incremental) m_samples.clear();
    for (const auto &sample : snapshot.samples) {
        if (m_selected.contains(sample.id) && (!snapshot.incremental || snapshot.trayRefreshIds.contains(sample.id)))
            m_samples.insert(sample.id, sample);
    }
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (!snapshot.incremental || snapshot.trayRefreshIds.contains(it.key())) updateEntry(it.key(), it.value());
    }
}

void TrayController::updateEntry(const QString &id, Entry &entry)
{
    const auto descriptor = m_descriptors.value(id);
    const auto sample = m_samples.value(id);
    const bool available = m_descriptors.contains(id) && sample.available && std::isfinite(sample.value) && sample.timestamp.isValid();
    const QString text = available ? compactValue(descriptor, sample.value) : QStringLiteral("—");
    const auto preferences = m_preferences->value(id);
    const QString label = !preferences.label.isEmpty() ? preferences.label
        : descriptor.label.isEmpty() ? id : descriptor.label;
    const auto formatted = formatSensorValue(descriptor, sample.value);
    const QString value = available && descriptor.kind == SensorKind::Frequency
        ? QLocale(QLocale::English, QLocale::UnitedStates).toString(sample.value * 1000000, 'f', 0) + QStringLiteral(" Hz")
        : available ? tr("%1 %2").arg(formatted.number, formatted.unit)
                                   : tr("Unavailable");
    QString tooltip = tr("%1\n%2\n%3").arg(label, descriptor.device, value);
    if (available && !sample.detail.isEmpty())
        tooltip += QStringLiteral("\n") + sample.detail;
    if (!available)
        tooltip += QStringLiteral("\n") + (sample.error.isEmpty() ? tr("No current reading") : sample.error);
    if (tooltip != entry.tooltip) {
        entry.tooltip = tooltip;
        entry.icon->setToolTip(tooltip);
        entry.reading->setText(tr("%1 — %2: %3").arg(label, descriptor.device, value));
    }
    const QColor color = preferences.trayForeground.isValid() ? preferences.trayForeground
        : !preferences.trayTransparent && preferences.trayBackground.isValid()
            ? (preferences.trayBackground.lightness() > 128 ? QColor(Qt::black) : QColor(Qt::white))
            : QApplication::palette().color(QPalette::Text);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!preferences.trayFontFamily.isEmpty()) font.setFamily(preferences.trayFontFamily);
    const QString key = text + QStringLiteral("|") + color.name(QColor::HexArgb) + font.toString()
        + QStringLiteral("|%1|%2|%3").arg(preferences.trayShadow)
            .arg(preferences.trayTransparent).arg(preferences.trayTransparent ? QString() :
                (preferences.trayBackground.isValid() ? preferences.trayBackground : QApplication::palette().color(QPalette::Base)).name(QColor::HexArgb));
    if (key != entry.renderKey) {
        entry.renderKey = key;
        entry.icon->setIcon(numericIcon(text, color, font, preferences));
    }
}

void TrayController::openSettings()
{
    emit showRequested();
    emit settingsRequested();
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

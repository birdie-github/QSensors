// SPDX-License-Identifier: GPL-3.0-only
#include "tray/traycontroller.h"
#include "rendering/valueformat.h"
#include "rendering/digitaltext.h"
#include "settings/sensorpreferences.h"

#include <QAction>
#include <QApplication>
#include <QCache>
#include <QDataStream>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QIcon>
#include <QIconEngine>
#include <QIODevice>
#include <QKeySequence>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QSet>
#include <QScreen>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace qsensors {

// Shared by successive icons for one sensor, not by unrelated sensors. Keep
// only a bounded set of physical-resolution/font fits, without reading history.
struct TrayFontSizingState {
    QHash<QByteArray, int> pixelLimits;
    QHash<QByteArray, int> pointLimits;
    QStringList fitTemplates;
    quint64 generation = 0;
};

namespace {

QStringList trayFitTemplates(SensorKind kind)
{
    switch (kind) {
    case SensorKind::Frequency:
        return {QStringLiteral("####")};
    case SensorKind::Memory:
        return {QStringLiteral("###"), QStringLiteral("-##")};
    case SensorKind::Fan:
    case SensorKind::Voltage:
        return {QStringLiteral("##.#"), QStringLiteral("-##.#")};
    case SensorKind::Temperature:
    case SensorKind::Humidity:
    case SensorKind::Percentage:
        return {QStringLiteral("####"), QStringLiteral("-###")};
    default:
        return {QStringLiteral("####"), QStringLiteral("-##.#")};
    }
}

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

struct FontTraits {
    bool bitmap = false;
    QList<int> strikes;
};
struct FontFit {
    QFont font;
    QRect bounds;
};

// GUI-thread caches, bounded across all sensors and changing readings. Owned by
// QApplication so font resources are released before GUI shutdown.
class TrayFontCache final : public QObject {
public:
    TrayFontCache() : QObject(qApp)
    {
        connect(qApp, &QGuiApplication::fontDatabaseChanged, this, [this] {
            traits.clear();
            fits.clear();
            ++generation;
        });
    }
    QCache<QString, FontTraits> traits{16};
    QCache<QByteArray, FontFit> fits{128};
    quint64 generation = 0;
};

TrayFontCache &trayFontCache()
{
    static auto *cache = new TrayFontCache;
    return *cache;
}

FontTraits fontTraits(const QFont &font)
{
    auto &cache = trayFontCache();
    const QString key = font.toString();
    if (const auto *cached = cache.traits.object(key)) return *cached;
    FontTraits result;
    result.bitmap = !QFontDatabase::isSmoothlyScalable(font.family());
    if (result.bitmap) {
        result.strikes = QFontDatabase::smoothSizes(font.family(), QFontDatabase::styleString(font));
        std::sort(result.strikes.begin(), result.strikes.end(), std::greater<int>());
    }
    cache.traits.insert(key, new FontTraits(result));
    return result;
}

// Render at the requested device resolution: paths and painter transforms lose
// bitmap strikes and font hinting, particularly at small tray sizes.
QPixmap numericPixmap(const QSize &size, const QString &text, const QColor &color,
                      QFont font, const SensorPreferences &preferences, TrayFontSizingState &sizing)
{
    if (size.isEmpty()) return {};
    QPixmap pixmap(size);
    pixmap.fill(preferences.trayTransparent ? QColor(Qt::transparent)
        : preferences.trayBackground.isValid() ? preferences.trayBackground : QApplication::palette().color(QPalette::Base));

    if (preferences.trayDigital) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        const int shadow = preferences.trayShadow && std::min(size.width(), size.height()) >= 8 ? 1 : 0;
        const QRectF area(QPointF(0, 0), QSizeF(size - QSize(shadow, shadow)));
        if (shadow) {
            const QColor ink = color.lightness() > 128 ? QColor(Qt::black) : QColor(Qt::white);
            drawDigitalText(painter, area.translated(shadow, shadow), text, ink, false, true);
        }
        drawDigitalText(painter, area, text, color, false, true);
        return pixmap;
    }

    font.setBold(false);
    font.setItalic(false);
    font.setStyleName(QString());
    font.setHintingPreference(QFont::PreferFullHinting);
    const auto traits = fontTraits(font);
    const bool bitmap = traits.bitmap;
    font.setStyleStrategy(QFont::StyleStrategy(QFont::NoSubpixelAntialias
        | (bitmap ? QFont::PreferBitmap | QFont::NoAntialias : QFont::PreferMatch)));

    const int edge = std::min(size.width(), size.height());
    const int shadow = preferences.trayShadow && edge >= 8 ? 1 : 0;
    const QSize available = size - QSize(shadow, shadow);
    // Fit ink bounds, which can be shorter than the font's nominal em size.
    // Select a font at this resolution; never scale painted glyphs.
    int requested = edge * 2;
    QByteArray sizingKey;
    // Suffixes, scientific notation and status strings have independent fits.
    // An unusual display form must not shrink the normal numeric reading.
    QString formatClass;
    QStringList fitTemplates = sizing.fitTemplates;
    if (text == QStringLiteral("ERR") || text == QStringLiteral("—")) {
        formatClass = text;
        fitTemplates.clear();
    } else if (text.contains(QLatin1Char('e'))) {
        formatClass = QStringLiteral("scientific");
        QString pattern = text;
        for (auto &character : pattern) {
            if (character.isDigit()) character = QLatin1Char('#');
        }
        fitTemplates = {pattern};
    } else if (!text.isEmpty() && text.back().isLetter()) {
        formatClass = QString(text.back());
        // Frequency has a four-character compact budget: three digits plus
        // its suffix. Memory uses up to three numeric characters plus a unit.
        const bool signedNumeric = fitTemplates.contains(QStringLiteral("-##"));
        fitTemplates = {QStringLiteral("###") + formatClass};
        if (signedNumeric) fitTemplates.append(QStringLiteral("-##") + formatClass);
    }
    QDataStream sizingStream(&sizingKey, QIODevice::WriteOnly);
    sizingStream << font << available << pixmap.logicalDpiX() << pixmap.logicalDpiY() << formatClass;
    auto &cache = trayFontCache();
    if (sizing.generation != cache.generation) {
        sizing.pixelLimits.clear();
        sizing.pointLimits.clear();
        sizing.generation = cache.generation;
    }
    // Seed from the format envelope; retain the fit across fluctuating values.
    // Exceptional readings beyond the envelope may still shrink the font.
    const bool seedFit = !sizing.pixelLimits.contains(sizingKey) && !sizing.pointLimits.contains(sizingKey);
    requested = sizing.pixelLimits.value(sizingKey, requested);
    QByteArray fitKey;
    QDataStream keyStream(&fitKey, QIODevice::WriteOnly);
    keyStream << font << available << pixmap.logicalDpiX() << pixmap.logicalDpiY()
              << text << requested << sizing.pointLimits.value(sizingKey, 0) << seedFit << fitTemplates;
    FontFit fitted;
    if (const auto *cached = cache.fits.object(fitKey)) {
        fitted = *cached;
    } else {
        QRect bounds;
        auto fits = [&] {
            const QFontMetrics metrics(font, &pixmap);
            bounds = metrics.tightBoundingRect(text);
            if (bounds.isEmpty() || bounds.width() > available.width() || bounds.height() > available.height())
                return false;
            if (!seedFit || fitTemplates.isEmpty()) return true;
            // Reserve all digit widths before the first reading. Per-character
            // advances plus bearing margins avoid fitting just one lucky set of
            // narrow digits such as 1111. This work runs only when seeding a fit.
            int digitAdvance = 0;
            int digitLeft = 0, digitRight = 0;
            for (const QChar digit : QStringLiteral("0123456789")) {
                const QRect ink = metrics.tightBoundingRect(QString(digit));
                digitAdvance = std::max(digitAdvance, metrics.horizontalAdvance(digit));
                digitLeft = std::max(digitLeft, -ink.left());
                digitRight = std::max(digitRight, ink.left() + ink.width() - metrics.horizontalAdvance(digit));
            }
            for (const auto &pattern : fitTemplates) {
                int width = 0, left = digitLeft, right = digitRight;
                QString glyphs = QStringLiteral("0123456789");
                for (const QChar character : pattern) {
                    if (character == QLatin1Char('#')) {
                        width += digitAdvance;
                    } else {
                        const QRect ink = metrics.tightBoundingRect(QString(character));
                        const int advance = metrics.horizontalAdvance(character);
                        width += advance;
                        left = std::max(left, -ink.left());
                        right = std::max(right, ink.left() + ink.width() - advance);
                        glyphs.append(character);
                    }
                }
                if (width + left + right > available.width()
                    || metrics.tightBoundingRect(glyphs).height() > available.height()) return false;
            }
            return true;
        };
        auto fitFont = [&](bool nativeBitmap) {
            if (nativeBitmap) {
                // Only native strikes: never resample a bitmap font.
                const int limit = sizing.pointLimits.value(sizingKey, std::numeric_limits<int>::max());
                for (int points : traits.strikes) {
                    if (points > limit) continue;
                    font.setPointSize(points);
                    if (fits()) return true;
                }
                return false;
            }
            // Most value changes fit the retained size with one measurement.
            font.setPixelSize(requested);
            if (fits()) return true;
            // Estimate from one reference measurement, then bracket the actual
            // hinted ink bounds. At 128px this takes at most 13 size probes,
            // rather than a descending scan of up to 256 sizes.
            font.setPixelSize(32);
            const QRect reference = QFontMetrics(font, &pixmap).tightBoundingRect(text);
            if (reference.isEmpty()) return false;
            const double ratio = std::min(double(available.width()) / reference.width(),
                                          double(available.height()) / reference.height());
            int low = 1, high = requested;
            int pixels = std::clamp(int(std::floor(32 * ratio)), low, high);
            int best = 0;
            QRect bestBounds;
            while (low <= high) {
                font.setPixelSize(pixels);
                if (fits()) {
                    best = pixels;
                    bestBounds = bounds;
                    low = pixels + 1;
                } else {
                    high = pixels - 1;
                }
                pixels = low + (high - low) / 2;
            }
            if (!best) return false;
            // Hinting can perturb adjacent sizes. Check two neighbors above the
            // bracket result, retaining only bounds that actually fit.
            for (int next = best + 1, end = std::min(requested, best + 2); next <= end; ++next) {
                font.setPixelSize(next);
                if (fits()) { best = next; bestBounds = bounds; }
            }
            font.setPixelSize(best);
            bounds = bestBounds;
            return true;
        };
        const bool retainedOutline = bitmap && sizing.pixelLimits.contains(sizingKey);
        if (retainedOutline || !fitFont(bitmap)) {
            // Preserve the complete reading if no native bitmap strike fits.
            font = QFont(QStringLiteral("sans-serif"));
            font.setHintingPreference(QFont::PreferFullHinting);
            font.setStyleStrategy(QFont::StyleStrategy(QFont::PreferOutline | QFont::NoSubpixelAntialias));
            if (!fitFont(false)) return pixmap;
        }
        fitted = {font, bounds};
        cache.fits.insert(fitKey, new FontFit(fitted));
    }
    font = fitted.font;
    const QRect bounds = fitted.bounds;
    if (seedFit && sizing.pixelLimits.size() + sizing.pointLimits.size() >= 16) {
        sizing.pixelLimits.clear();
        sizing.pointLimits.clear();
    }
    if (font.pixelSize() > 0) {
        sizing.pointLimits.remove(sizingKey);
        sizing.pixelLimits.insert(sizingKey, font.pixelSize());
    } else if (font.pointSize() > 0) {
        sizing.pixelLimits.remove(sizingKey);
        sizing.pointLimits.insert(sizingKey, font.pointSize());
    }
    // Integral device-pixel baselines retain bitmap sharpness. Reserve room
    // for the shadow, then paint the foreground last without any outline.
    const QPoint baseline(available.width() - bounds.width() - bounds.left(),
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
                      const SensorPreferences &preferences, std::shared_ptr<TrayFontSizingState> sizing)
        : m_text(text), m_color(color), m_font(font), m_preferences(preferences), m_sizing(std::move(sizing)) {}

    QIconEngine *clone() const override { return new NumericIconEngine(*this); }
    bool isNull() override { return m_text.isEmpty(); }
    QPixmap pixmap(const QSize &size, QIcon::Mode, QIcon::State) override
    {
        if (size.isEmpty()) return {};
        const auto *screen = QGuiApplication::primaryScreen();
        const QSize dpi = screen ? QSize(qRound(screen->logicalDotsPerInchX()), qRound(screen->logicalDotsPerInchY())) : QSize();
        const quint64 generation = trayFontCache().generation;
        const QColor background = QApplication::palette().color(QPalette::Base);
        if (generation != m_fontGeneration || dpi != m_dpi || background != m_background) {
            m_pixmaps.clear();
            m_cachedPixels = 0;
            m_fontGeneration = generation;
            m_dpi = dpi;
            m_background = background;
        }
        for (int i = 0; i < m_pixmaps.size(); ++i) {
            if (m_pixmaps[i].size() == size) {
                const QPixmap result = m_pixmaps.takeAt(i);
                m_pixmaps.append(result);
                return result;
            }
        }
        QPixmap result = numericPixmap(size, m_text, m_color, m_font, m_preferences, *m_sizing);
        const qint64 pixels = qint64(size.width()) * size.height();
        // Per-current-value LRU: retain the eleven advertised resolutions within
        // 64K raster pixels (~256 KiB at 32bpp). Oversized requests aren't cached.
        constexpr qint64 maxPixels = 64 * 1024;
        if (!result.isNull() && pixels <= maxPixels) {
            while (!m_pixmaps.isEmpty() && (m_pixmaps.size() >= 11 || m_cachedPixels + pixels > maxPixels)) {
                const auto oldest = m_pixmaps.takeFirst();
                m_cachedPixels -= qint64(oldest.width()) * oldest.height();
            }
            m_pixmaps.append(result);
            m_cachedPixels += pixels;
        }
        return result;
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
    std::shared_ptr<TrayFontSizingState> m_sizing;
    QList<QPixmap> m_pixmaps;
    qint64 m_cachedPixels = 0;
    quint64 m_fontGeneration = 0;
    QSize m_dpi;
    QColor m_background;
};

QIcon numericIcon(const QString &text, const QColor &color, const QFont &font,
                  const SensorPreferences &preferences, const std::shared_ptr<TrayFontSizingState> &sizing)
{
    return QIcon(new NumericIconEngine(text, color, font, preferences, sizing));
}

} // namespace

TrayController::TrayController(QWidget *window, SensorPreferencesStore *preferences, QObject *parent)
    : QObject(parent), m_window(window), m_preferences(preferences), m_menu(std::make_unique<QMenu>())
{
    m_dashboardAction = m_menu->addAction(tr("Hide Dashboard"));
    // Separate actions remain available to the dashboard's File menu.
    m_showAction = new QAction(tr("Show Dashboard"), this);
    m_hideAction = new QAction(tr("Hide Dashboard"), this);
    m_menu->addSeparator();
    m_settingsAction = m_menu->addAction(tr("Settings…"));
    m_rescanAction = m_menu->addAction(tr("Rescan"));
    m_menu->addSeparator();
    m_aboutAction = m_menu->addAction(tr("About QSensors"));
    m_quitAction = m_menu->addAction(tr("Quit"));
    m_showAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    m_rescanAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_dashboardAction, &QAction::triggered, this, &TrayController::toggleRequested);
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
    // Install cache invalidation before redrawing icons on font changes.
    trayFontCache();
    connect(qApp, &QGuiApplication::fontDatabaseChanged, this, [this] {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            it->renderKey.clear();
            updateEntry(it.key(), it.value());
        }
    });
}

TrayController::~TrayController()
{
    qApp->removeEventFilter(this);
    // QSystemTrayIcon does not own its menu. Remove icons before menus.
    for (const auto &entry : m_entries) {
        delete entry.icon;
        delete entry.menu;
    }
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
    if (available != m_available) {
        m_available = available;
        emit availabilityChanged(available);
        if (!available && !m_dashboardVisible)
            emit showRequested();
    }
    setDashboardVisible(m_dashboardVisible);
}

void TrayController::setDashboardVisible(bool visible)
{
    m_dashboardVisible = visible;
    m_dashboardAction->setText(visible ? tr("Hide Dashboard") : tr("Show Dashboard"));
    m_dashboardAction->setEnabled(!visible || m_available);
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
        delete it->menu;
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
                entry.menu = new QMenu;
                // Reuse application actions while keeping the reading specific
                // to the icon whose context menu was opened.
                for (auto *action : m_menu->actions()) {
                    entry.menu->addAction(action);
                    if (action == m_rescanAction) entry.reading = entry.menu->addAction(id);
                }
                QFont readingFont = entry.menu->font();
                readingFont.setBold(true);
                entry.reading->setFont(readingFont);
                entry.menu->setDefaultAction(entry.reading);
                connect(entry.reading, &QAction::triggered, this, [this, id] {
                    emit showRequested();
                    emit sensorSettingsRequested(id);
                });
                entry.icon->setContextMenu(entry.menu);
                m_entries.insert(id, entry);
            }
            auto &entry = m_entries[id];
            updateEntry(id, entry);
            entry.icon->show(); // Qt automatically registers with a later/restarted host.
        }
    }
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
    const QSet<QString> due(snapshot.trayRefreshIds.cbegin(), snapshot.trayRefreshIds.cend());
    for (const auto &sample : snapshot.samples) {
        if (m_entries.contains(sample.id) && (!snapshot.incremental || due.contains(sample.id)))
            m_samples.insert(sample.id, sample);
    }
    if (snapshot.incremental) {
        for (const auto &id : due) {
            auto it = m_entries.find(id);
            if (it != m_entries.end()) updateEntry(id, it.value());
        }
    } else {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) updateEntry(it.key(), it.value());
    }
}

void TrayController::updateEntry(const QString &id, Entry &entry)
{
    const auto descriptor = m_descriptors.value(id);
    const auto sample = m_samples.value(id);
    const bool available = m_descriptors.contains(id) && sample.available && std::isfinite(sample.value) && sample.timestamp.isValid();
    auto preferences = m_preferences->value(id);
    preferences.trayDigital = preferences.trayDigital && descriptor.kind == SensorKind::Frequency;
    const QString text = !available ? QStringLiteral("—")
        : preferences.trayDigital
            ? (sample.value < 1 || sample.value > 9999 ? QStringLiteral("ERR")
                : QString::number(std::round(sample.value), 'f', 0))
            : compactValue(descriptor, sample.value);
    const QString label = !preferences.label.isEmpty() ? preferences.label
        : descriptor.label.isEmpty() ? id : descriptor.label;
    // Names are literal text, not menu mnemonics. Do not export a menu label
    // update on every value change; full readings remain in the icon tooltip.
    QString menuLabel = label;
    menuLabel.replace(QLatin1Char('&'), QStringLiteral("&&"));
    if (entry.reading->text() != menuLabel) entry.reading->setText(menuLabel);
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
    }
    const QColor color = preferences.trayForeground.isValid() ? preferences.trayForeground
        : !preferences.trayTransparent && preferences.trayBackground.isValid()
            ? (preferences.trayBackground.lightness() > 128 ? QColor(Qt::black) : QColor(Qt::white))
            : QApplication::palette().color(QPalette::Text);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!preferences.trayFontFamily.isEmpty()) font.setFamily(preferences.trayFontFamily);
    const QString key = QString::number(preferences.trayDigital) + QLatin1Char('|') + text + QStringLiteral("|") + color.name(QColor::HexArgb) + font.toString()
        + QStringLiteral("|%1|%2|%3").arg(preferences.trayShadow)
            .arg(preferences.trayTransparent).arg(preferences.trayTransparent ? QString() :
                (preferences.trayBackground.isValid() ? preferences.trayBackground : QApplication::palette().color(QPalette::Base)).name(QColor::HexArgb));
    const QString sizingKey = font.toString() + QStringLiteral("|%1|%2|%3")
        .arg(preferences.trayDigital).arg(preferences.trayShadow).arg(static_cast<int>(descriptor.kind));
    if (!entry.fontSizing || entry.fontSizingKey != sizingKey) {
        entry.renderKey.clear();
        entry.fontSizingKey = sizingKey;
        entry.fontSizing = std::make_shared<TrayFontSizingState>();
        entry.fontSizing->fitTemplates = trayFitTemplates(descriptor.kind);
    }
    if (key != entry.renderKey) {
        entry.renderKey = key;
        entry.icon->setIcon(numericIcon(text, color, font, preferences, entry.fontSizing));
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

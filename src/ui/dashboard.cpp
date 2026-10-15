// SPDX-License-Identifier: GPL-3.0-only
#include "ui/dashboard.h"
#include "settings/numberformat.h"
#include "ui/decimalspinbox.h"
#include "rendering/emojiicon.h"
#include "alarms/alarmcontroller.h"
#include "ui/sensortablemodel.h"
#include "ui/sensorgrid.h"
#include "settings/sensorpreferences.h"
#include "settings/autostart.h"

#include <QSet>
#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QInputDialog>
#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QHeaderView>
#include <QFontMetrics>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QResizeEvent>
#include <QSizeGrip>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <algorithm>

namespace qsensors {
namespace {
class SensorSortModel final : public QSortFilterProxyModel {
public:
    explicit SensorSortModel(QObject *parent) : QSortFilterProxyModel(parent) {}
protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        if (left.column() == 2) {
            const bool leftAvailable = sourceModel()->data(left, sortRole()).isValid();
            const bool rightAvailable = sourceModel()->data(right, sortRole()).isValid();
            // The proxy reverses comparisons for descending order. Compensate
            // here so unavailable readings remain last in either direction.
            if (leftAvailable != rightAvailable)
                return sortOrder() == Qt::AscendingOrder ? leftAvailable : rightAvailable;
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }
};
}

Dashboard::Dashboard(SensorTableModel *model, SensorPreferencesStore *preferences, QWidget *parent)
    : QMainWindow(parent), m_preferences(preferences), m_model(model)
{
    setWindowTitle(QCoreApplication::applicationName());
    resize(800, 500);
    auto *toolbar = addToolBar(tr("Sensors"));
    toolbar->setObjectName(QStringLiteral("sensorsToolbar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    createSettingsMenu();
    auto *view = menuBar()->addMenu(tr("&View"));
    auto *toolbarStyles = view->addMenu(tr("Toolbar Style"));
    auto *toolbarStyleGroup = new QActionGroup(toolbarStyles);
    const int savedStyle = QSettings().value(QStringLiteral("Dashboard/toolbarStyle"),
                                            int(Qt::ToolButtonTextBesideIcon)).toInt();
    const auto toolbarStyle = savedStyle >= int(Qt::ToolButtonIconOnly)
        && savedStyle <= int(Qt::ToolButtonTextUnderIcon)
        ? static_cast<Qt::ToolButtonStyle>(savedStyle) : Qt::ToolButtonTextBesideIcon;
    toolbar->setToolButtonStyle(toolbarStyle);
    const auto addToolbarStyle = [this, toolbar, toolbarStyles, toolbarStyleGroup](
                                     const QString &label, Qt::ToolButtonStyle style) {
        auto *action = toolbarStyles->addAction(label);
        action->setCheckable(true);
        toolbarStyleGroup->addAction(action);
        action->setChecked(toolbar->toolButtonStyle() == style);
        connect(action, &QAction::triggered, this, [toolbar, style] {
            toolbar->setToolButtonStyle(style);
            QSettings().setValue(QStringLiteral("Dashboard/toolbarStyle"), int(style));
        });
    };
    addToolbarStyle(tr("Icons Only"), Qt::ToolButtonIconOnly);
    addToolbarStyle(tr("Text Beside Icons"), Qt::ToolButtonTextBesideIcon);
    addToolbarStyle(tr("Text Below Icons"), Qt::ToolButtonTextUnderIcon);
    addToolbarStyle(tr("Text Only"), Qt::ToolButtonTextOnly);
    view->addSeparator();
    auto *presentationGroup = new QActionGroup(view);
    m_dashboardView = view->addAction(tr("Dashboard"));
    m_tableView = view->addAction(tr("Table"));
    for (auto *action : {m_dashboardView, m_tableView}) {
        action->setCheckable(true);
        presentationGroup->addAction(action);
    }
    const bool tableView = QSettings().value(QStringLiteral("Dashboard/tableView"), false).toBool();
    m_tableView->setChecked(tableView);
    m_dashboardView->setChecked(!tableView);
    m_controlsAction = view->addAction(tr("Compact Mode"));
    m_controlsAction->setCheckable(true);
    m_controlsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    addAction(m_controlsAction);
    connect(m_controlsAction, &QAction::toggled, this, [this](bool compact) { setControlsVisible(!compact); });
    view->addSeparator();
    m_grid = new SensorGrid(preferences, this);
    auto *paletteMenu = view->addMenu(tr("Palette"));
    auto *paletteGroup = new QActionGroup(paletteMenu);
    const auto addPalette = [this, paletteMenu, paletteGroup](const QString &label, DashboardPalette preset) {
        auto *action = paletteMenu->addAction(label);
        action->setCheckable(true);
        paletteGroup->addAction(action);
        action->setData(static_cast<int>(preset));
        action->setChecked(m_grid->palettePreset() == preset);
        connect(action, &QAction::triggered, m_grid, [this, preset] { m_grid->setPalettePreset(preset); });
    };
    addPalette(tr("System"), DashboardPalette::System);
    addPalette(tr("Classic KSensors"), DashboardPalette::Classic);
    addPalette(tr("Graphite"), DashboardPalette::Graphite);
    addPalette(tr("Custom"), DashboardPalette::Custom);
    paletteMenu->addSeparator();
    connect(paletteMenu->addAction(tr("Custom Colors…")), &QAction::triggered, this, [this, paletteGroup] {
        m_grid->customizeColors();
        // The editor activates Custom only on OK; Cancel retains the current action.
        for (auto *action : paletteGroup->actions())
            action->setChecked(action->data().toInt() == static_cast<int>(m_grid->palettePreset()));
    });
    auto *separatorMenu = view->addMenu(tr("Decimal Separator"));
    auto *separatorGroup = new QActionGroup(separatorMenu);
    const auto addSeparator = [separatorMenu, separatorGroup](const QString &label, DecimalSeparator separator) {
        auto *action = separatorMenu->addAction(label);
        action->setCheckable(true);
        action->setData(static_cast<int>(separator));
        separatorGroup->addAction(action);
        action->setChecked(NumberFormatSettings::instance().separator() == separator);
        QObject::connect(action, &QAction::triggered, &NumberFormatSettings::instance(), [separator] {
            NumberFormatSettings::instance().setSeparator(separator);
        });
    };
    addSeparator(tr("Dot (.)"), DecimalSeparator::Dot);
    addSeparator(tr("Comma (,)"), DecimalSeparator::Comma);
    addSeparator(tr("System Default"), DecimalSeparator::System);
    connect(&NumberFormatSettings::instance(), &NumberFormatSettings::changed, separatorGroup, [separatorGroup] {
        for (auto *action : separatorGroup->actions())
            action->setChecked(action->data().toInt() == static_cast<int>(NumberFormatSettings::instance().separator()));
    });
    auto *backgroundMenu = view->addMenu(tr("Background"));
    auto *backgroundGroup = new QActionGroup(backgroundMenu);
    const auto addBackground = [this, backgroundMenu, backgroundGroup](const QString &label, DashboardBackground pattern) {
        auto *action = backgroundMenu->addAction(label);
        action->setCheckable(true);
        backgroundGroup->addAction(action);
        action->setChecked(m_grid->backgroundPattern() == pattern);
        connect(action, &QAction::triggered, m_grid, [this, pattern] { m_grid->setBackgroundPattern(pattern); });
    };
    addBackground(tr("Solid"), DashboardBackground::Solid);
    addBackground(tr("Dots"), DashboardBackground::Dots);
    addBackground(tr("Grid"), DashboardBackground::Grid);
    view->addSeparator();
    connect(view->addAction(tr("Sensor Name Font…")), &QAction::triggered, m_grid, [this] {
        m_grid->chooseTileFont(true);
    });
    connect(view->addAction(tr("Sensor Data Font…")), &QAction::triggered, m_grid, [this] {
        m_grid->chooseTileFont(false);
    });
    connect(view->addAction(tr("Reset Tile Fonts")), &QAction::triggered, m_grid, &SensorGrid::resetTileFonts);
    auto *styleMenu = new QMenu(tr("Tile Style"), view);
    view->insertMenu(paletteMenu->menuAction(), styleMenu);
    auto *styleGroup = new QActionGroup(styleMenu);
    const auto addStyle = [this, styleMenu, styleGroup](const QString &label, DashboardTileShape shape) {
        auto *action = styleMenu->addAction(label);
        action->setCheckable(true);
        styleGroup->addAction(action);
        action->setChecked(m_grid->tileShape() == shape);
        connect(action, &QAction::triggered, m_grid, [this, shape] { m_grid->setTileShape(shape); });
    };
    addStyle(tr("Rounded Rectangular"), DashboardTileShape::RoundedRectangle);
    addStyle(tr("Square"), DashboardTileShape::Square);
    addStyle(tr("Circular"), DashboardTileShape::Circle);
    styleMenu->addSeparator();
    auto *shrinkMenu = styleMenu->addMenu(tr("Circular Content Shrink"));
    auto *shrinkGroup = new QActionGroup(shrinkMenu);
    for (int percent = 0; percent <= 30; percent += 5) {
        auto *action = shrinkMenu->addAction(tr("%1%").arg(percent));
        action->setCheckable(true);
        action->setData(percent);
        shrinkGroup->addAction(action);
        connect(action, &QAction::triggered, m_grid, [this, percent] { m_grid->setCircleShrinkPercent(percent); });
    }
    shrinkMenu->addSeparator();
    auto *customShrink = shrinkMenu->addAction(tr("Custom…"));
    customShrink->setCheckable(true);
    customShrink->setData(-1);
    shrinkGroup->addAction(customShrink);
    const auto updateShrinkActions = [this, shrinkGroup, customShrink] {
        const int percent = m_grid->circleShrinkPercent();
        bool preset = false;
        for (auto *action : shrinkGroup->actions()) {
            const bool matches = action->data().toInt() == percent;
            action->setChecked(matches);
            preset = preset || matches;
        }
        customShrink->setChecked(!preset);
        customShrink->setText(preset ? tr("Custom…") : tr("Custom (%1%)…").arg(percent));
    };
    connect(styleMenu, &QMenu::aboutToShow, this, [this, shrinkMenu] {
        shrinkMenu->setEnabled(m_grid->tileShape() == DashboardTileShape::Circle);
    });
    connect(shrinkMenu, &QMenu::aboutToShow, this, updateShrinkActions);
    connect(customShrink, &QAction::triggered, this, [this, updateShrinkActions] {
        bool accepted = false;
        const int percent = QInputDialog::getInt(this, tr("Circular Content Shrink"), tr("Shrink contents by (%):"),
                                                m_grid->circleShrinkPercent(), 0, 50, 1, &accepted);
        if (accepted) m_grid->setCircleShrinkPercent(percent);
        updateShrinkActions();
    });
    updateShrinkActions();
    // A bottom-right handle leaves the tile field available for moving the window.
    // QSizeGrip uses Qt's native resize support, including Wayland when available.
    m_resizeGrip = new QSizeGrip(this);
    m_resizeGrip->setToolTip(tr("Resize square tiles"));
    m_resizeGrip->hide();
    m_resizeSaveTimer.setSingleShot(true);
    m_resizeSaveTimer.setInterval(200);
    connect(&m_resizeSaveTimer, &QTimer::timeout, this, [this] {
        // Native resize operations do not expose a portable "resize finished"
        // event. Wait for both quiet geometry and released mouse buttons.
        if (QGuiApplication::mouseButtons() != Qt::NoButton) {
            m_resizeSaveTimer.start();
            return;
        }
        finishCompactResize();
    });
    connect(m_grid, &SensorGrid::controlsToggleRequested, this, [this] { m_controlsAction->toggle(); });
    connect(m_grid, &SensorGrid::fieldSizeChanged, this, &Dashboard::fitCompactView);
    connect(m_grid, &SensorGrid::configureRequested, this, &Dashboard::sensorConfigureRequested);
    connect(m_grid, &SensorGrid::layoutSaveFailed, this, [this](const QString &error) {
        QMessageBox::warning(this, tr("Save dashboard layout"), error);
    });
    connect(m_grid, &SensorGrid::deleteRequested, this, [this](const QString &id) {
        auto values = m_preferences->values();
        auto prefs = m_preferences->value(id);
        prefs.dashboard = false;
        values.insert(id, prefs);
        QString error;
        if (!m_preferences->apply(values, m_preferences->temperatureUnit(), error)) {
            QMessageBox::warning(this, tr("Delete tile"), error);
            return;
        }
        emit sensorDisabled(id);
    });
    auto *sizes = new QMenu(tr("Tile Size"), view);
    view->insertMenu(styleMenu->menuAction(), sizes);
    for (int size : {64, 96, 128, 192, 256, 384, 512, 1024}) {
        auto *preset = sizes->addAction(tr("%1 × %1").arg(size));
        connect(preset, &QAction::triggered, this, [this, size] {
            m_grid->setTileSize(size);
        });
    }
    sizes->addSeparator();
    m_tileSizeAction = sizes->addAction(emojiIcon(QStringLiteral("📏")), tr("Custom Tile Size…"));
    m_tileSizeAction->setIconText(tr("Tile Size"));
    m_tileSizeAction->setIconVisibleInMenu(true);
    m_tileSizeAction->setShortcut(QKeySequence(Qt::Key_F3));
    addAction(m_tileSizeAction);
    sizes->setIcon(m_tileSizeAction->icon());
    sizes->menuAction()->setIconVisibleInMenu(true);
    connect(m_tileSizeAction, &QAction::triggered, this, &Dashboard::chooseCustomTileSize);
    toolbar->addAction(m_intervalAction);
    toolbar->addAction(m_tileSizeAction);
    auto *stack = new QStackedWidget(this);
    m_stack = stack;
    auto *empty = new QLabel(tr("Scanning sensors…"), stack);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    empty->setMargin(16);
    stack->addWidget(empty);
    auto *table = new QTableView(stack);
    auto *sortModel = new SensorSortModel(table);
    sortModel->setSourceModel(model);
    sortModel->setDynamicSortFilter(true);
    sortModel->setSortRole(SensorTableModel::SortRole);
    sortModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    sortModel->setSortLocaleAware(true);
    table->setModel(sortModel);
    // Keep provider/catalog order until the user chooses a sort column.
    table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    table->setSortingEnabled(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(false);
    const int characterWidth = table->fontMetrics().horizontalAdvance(QLatin1Char('M'));
    table->setColumnWidth(0, 24 * characterWidth);
    table->setColumnWidth(1, 28 * characterWidth);
    table->setColumnWidth(2, 12 * characterWidth);
    table->setColumnWidth(3, 8 * characterWidth);
    stack->addWidget(m_grid);
    stack->addWidget(table);
    setCentralWidget(stack);
    connect(stack, &QStackedWidget::currentChanged, this, [this] { refreshPresentation(); });
    connect(m_grid, &SensorGrid::visibleIdsChanged, this, [this] { updateVisibleIds(); });
    connect(m_tableView, &QAction::toggled, this, [this](bool table) {
        QSettings().setValue(QStringLiteral("Dashboard/tableView"), table);
        if (table && m_compact) setControlsVisible(true);
        if (!m_compact) m_stack->setCurrentIndex(table ? 2 : 1);
        m_tileSizeAction->setEnabled(!table);
        updateVisibleIds();
    });
    m_tileSizeAction->setEnabled(!m_tableView->isChecked());
    connect(statusBar(), &QStatusBar::messageChanged, empty, &QLabel::setText);
    m_trayWarning = new QLabel(tr("No system tray host — closing the window exits."), this);
    m_trayWarning->setWordWrap(true);
    m_trayWarning->setToolTip(tr("A compatible system tray host is required for background operation."));
    statusBar()->addPermanentWidget(m_trayWarning);
    QSettings settings;
    m_controlsAction->setChecked(false);
    const bool controlsVisible = settings.value(QStringLiteral("Dashboard/controlsVisible"), true).toBool();
    if (settings.contains(QStringLiteral("Dashboard/width"))) {
        bool widthOk = false, heightOk = false;
        const int width = settings.value(QStringLiteral("Dashboard/width")).toInt(&widthOk);
        const int height = settings.value(QStringLiteral("Dashboard/height")).toInt(&heightOk);
        if (widthOk && heightOk && width > 0 && height > 0) resize(width, height);
        // Global positioning is supported on X11; Wayland chooses placement.
        if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
            bool xOk = false, yOk = false;
            const int x = settings.value(QStringLiteral("Dashboard/x")).toInt(&xOk);
            const int y = settings.value(QStringLiteral("Dashboard/y")).toInt(&yOk);
            if (xOk && yOk) {
                const QPoint position(x, y);
                for (const auto *display : QGuiApplication::screens()) {
                    if (display->availableGeometry().contains(position)) { move(position); break; }
                }
            }
        }
        if (settings.value(QStringLiteral("Dashboard/maximized"), false).toBool())
            setWindowState(windowState() | Qt::WindowMaximized);
    }
    if (const auto *currentScreen = screen()) {
        const QSize available = currentScreen->availableGeometry().size();
        resize(size().boundedTo(available));
    }
    if (!controlsVisible) QTimer::singleShot(0, this, [this] { setControlsVisible(false); });
}

void Dashboard::createSettingsMenu()
{
    m_settingsMenu = new QMenu(tr("&Settings"), this);
    auto *units = m_settingsMenu->addMenu(tr("Temperature Unit"));
    auto *unitGroup = new QActionGroup(units);
    const auto syncUnits = [this, unitGroup] {
        for (auto *action : unitGroup->actions())
            action->setChecked(action->data().toInt() == static_cast<int>(m_preferences->temperatureUnit()));
    };
    const auto addUnit = [this, units, unitGroup, syncUnits](const QString &label, TemperatureUnit unit) {
        auto *action = units->addAction(label);
        action->setCheckable(true);
        action->setData(static_cast<int>(unit));
        unitGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, unit, syncUnits] {
            QString error;
            if (!m_preferences->setTemperatureUnit(unit, error))
                QMessageBox::warning(this, tr("Temperature Unit"), error);
            syncUnits();
        });
    };
    addUnit(tr("Celsius (°C)"), TemperatureUnit::Celsius);
    addUnit(tr("Fahrenheit (°F)"), TemperatureUnit::Fahrenheit);
    addUnit(tr("Kelvin (K)"), TemperatureUnit::Kelvin);
    syncUnits();
    connect(m_preferences, &SensorPreferencesStore::temperatureUnitChanged, this, syncUnits);
    m_intervalAction = m_settingsMenu->addAction(emojiIcon(QStringLiteral("⏱️")), tr("Global Update Interval…"));
    m_intervalAction->setIconText(tr("Update Interval"));
    m_intervalAction->setIconVisibleInMenu(true);
    m_intervalAction->setShortcut(QKeySequence(Qt::Key_F2));
    addAction(m_intervalAction);
    connect(m_intervalAction, &QAction::triggered, this, [this] {
        const auto seconds = DecimalSpinBox::getValue(this, tr("Global Update Interval"), tr("Interval (seconds):"),
            m_intervalSeconds, minimumGlobalRefreshSeconds, maximumGlobalRefreshSeconds, 0.1);
        if (seconds && *seconds != m_intervalSeconds) {
            setIntervalSeconds(*seconds);
            emit intervalChanged(*seconds);
        }
    });
    m_settingsMenu->addSeparator();
    auto *autostart = m_settingsMenu->addAction(tr("Start at Login"));
    autostart->setCheckable(true);
    autostart->setChecked(Autostart::enabled());
    autostart->setToolTip(tr("Starts this executable: %1").arg(QCoreApplication::applicationFilePath()));
    connect(autostart, &QAction::triggered, this, [this, autostart](bool enabled) {
        QString error;
        if (!Autostart::setEnabled(enabled, error)) {
            autostart->setChecked(Autostart::enabled());
            QMessageBox::warning(this, tr("Start at Login"), error);
        }
    });
    connect(m_settingsMenu, &QMenu::aboutToShow, this, [autostart] {
        autostart->setChecked(Autostart::enabled());
    });
}

Dashboard::~Dashboard()
{
    finishCompactResize();
    saveWindowGeometry();
}

void Dashboard::setControlsVisible(bool visible)
{
    const QSignalBlocker blocker(m_controlsAction);
    m_controlsAction->setChecked(!visible);
    if (m_compact == !visible) return;
    if (!visible) m_dashboardView->setChecked(true);
    finishCompactResize();
    const QScopedValueRollback<bool> adjusting(m_adjustingCompact, true);
    const bool wasVisible = isVisible();
    if (!visible) {
        saveWindowGeometry();
        m_expandedGeometry = (isMaximized() || isMinimized()) ? normalGeometry() : geometry();
        m_expandedState = windowState() & ~Qt::WindowMinimized;
        m_expandedFlags = windowFlags();
        // Remove the stack so hidden table/placeholder minimum sizes cannot enlarge the field.
        takeCentralWidget();
        // Keep the inactive view owned while it is outside the main-window layout.
        m_stack->setParent(this);
        m_stack->hide();
        m_grid->setParent(this);
        setCentralWidget(m_grid);
        m_compact = true;
        m_grid->setCompactView(true);
        setWindowState(Qt::WindowNoState);
        setWindowFlags(m_expandedFlags | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    } else {
        m_compact = false;
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        takeCentralWidget();
        m_grid->setCompactView(false);
        m_stack->insertWidget(1, m_grid);
        setCentralWidget(m_stack);
        m_stack->setCurrentIndex(m_tableView->isChecked() ? 2 : 1);
        setWindowFlags(m_expandedFlags);
        setGeometry(m_expandedGeometry);
        setWindowState(m_expandedState);
    }
    menuBar()->setVisible(visible);
    if (auto *toolbar = findChild<QToolBar *>(QStringLiteral("sensorsToolbar"))) toolbar->setVisible(visible);
    statusBar()->setVisible(visible);
    QSettings().setValue(QStringLiteral("Dashboard/controlsVisible"), visible);
    // Reparenting explicitly hides widgets; showing the window does not undo it.
    centralWidget()->show();
    refreshPresentation();
    m_resizeGrip->setVisible(m_compact);
    fitCompactView();
    if (wasVisible) show();
    // Window flag changes and hidden toolbars settle through posted layout events.
    QTimer::singleShot(0, this, &Dashboard::fitCompactView);
    updateVisibleIds();
}

void Dashboard::fitCompactView()
{
    // Never change native drag geometry or its constraints mid-gesture.
    if (!m_compact || m_resizeSavePending) return;
    const QScopedValueRollback<bool> adjusting(m_adjustingCompact, true);
    // Flush the central-widget/chrome layout before deriving the outer size.
    layout()->invalidate();
    layout()->activate();
    const QSize chrome(std::max(0, width() - m_grid->width()),
                       std::max(0, height() - m_grid->height()));
    m_compactChrome = chrome;
    const QSize field = m_grid->fieldSize() + chrome;
    // Compact mode always contains the complete field, even beyond screen bounds.
    setMinimumSize(m_grid->fieldSizeForTileSize(minimumTileSize) + chrome);
    setMaximumSize(m_grid->fieldSizeForTileSize(maximumTileSize) + chrome);
    resize(field);
    layout()->activate();
    positionResizeGrip();
}

void Dashboard::positionResizeGrip()
{
    const QSize extent = m_resizeGrip->sizeHint();
    m_resizeGrip->setGeometry(width() - extent.width(), height() - extent.height(),
                              extent.width(), extent.height());
    m_resizeGrip->raise();
}

void Dashboard::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!m_compact || m_adjustingCompact) return;
    const QScopedValueRollback<bool> adjusting(m_adjustingCompact, true);
    if (!m_resizeSavePending) {
        m_resizeInitialTileSize = m_grid->tileSize();
        m_resizeSavePending = true;
    }
    m_grid->setTileSize(m_grid->tileSizeForFieldSize(event->size() - m_compactChrome), false);
    positionResizeGrip();
    m_resizeSaveTimer.start();
    // The window follows the native drag freely. Tiles stay square and fit
    // inside it; any spare space is removed only after the gesture finishes.
}

void Dashboard::finishCompactResize()
{
    m_resizeSaveTimer.stop();
    if (!m_resizeSavePending) return;
    m_resizeSavePending = false;
    fitCompactView();
    if (m_grid->tileSize() != m_resizeInitialTileSize) m_grid->saveTileSize();
}

void Dashboard::saveWindowGeometry()
{
    QSettings settings;
    const QRect normal = m_compact ? m_expandedGeometry
        : (isMaximized() || isMinimized()) ? normalGeometry() : geometry();
    if (!normal.isValid()) return;
    settings.setValue(QStringLiteral("Dashboard/width"), normal.width());
    settings.setValue(QStringLiteral("Dashboard/height"), normal.height());
    if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
        // move() on a top-level widget accepts the frame position.
        const QPoint position = (isMaximized() || isMinimized()) ? normal.topLeft() : pos();
        settings.setValue(QStringLiteral("Dashboard/x"), position.x());
        settings.setValue(QStringLiteral("Dashboard/y"), position.y());
    }
    if (m_compact ? bool(m_expandedState & Qt::WindowMaximized) : isMaximized()) settings.setValue(QStringLiteral("Dashboard/maximized"), true);
    else settings.remove(QStringLiteral("Dashboard/maximized"));
}

void Dashboard::chooseCustomTileSize()
{
    auto *dialog = new QInputDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Custom Tile Size"));
    dialog->setLabelText(tr("Tile size in logical pixels (%1–%2):").arg(minimumTileSize).arg(maximumTileSize));
    dialog->setInputMode(QInputDialog::IntInput);
    dialog->setIntRange(minimumTileSize, maximumTileSize);
    dialog->setIntStep(1);
    dialog->setIntValue(m_grid->tileSize());
    connect(dialog, &QInputDialog::intValueSelected, this, [this](int size) {
        m_grid->setTileSize(size);
    });
    dialog->open();
}

void Dashboard::applySnapshot(const SensorSnapshot &snapshot)
{
    QList<SensorDescriptor> selected;
    if (snapshot.rescanned) {
        m_catalogIds.clear();
        m_catalogSet.clear();
        for (const auto &descriptor : snapshot.descriptors) {
            if (m_preferences->value(descriptor.id).dashboard) {
                m_catalogIds.append(descriptor.id);
                m_catalogSet.insert(descriptor.id);
                selected.append(descriptor);
            }
        }
    }
    if (!snapshot.incremental) m_dashboardSamples.clear();
    const QSet<QString> due(snapshot.dashboardRefreshIds.cbegin(), snapshot.dashboardRefreshIds.cend());
    QList<SensorSample> samples;
    for (const auto &sample : snapshot.samples) {
        if (m_catalogSet.contains(sample.id) && (!snapshot.incremental || due.contains(sample.id))) {
            m_dashboardSamples.insert(sample.id, sample);
            samples.append(sample);
        }
    }
    if (snapshot.rescanned) {
        // Catalog/layout maintenance is infrequent and still reaches both views.
        // Install the cache first: grid visibility signals can request a batch.
        m_gridDirty = m_tableDirty = true;
        m_model->setDescriptors(selected);
        m_grid->setDescriptors(selected);
        if (!m_compact) m_stack->setCurrentIndex(snapshot.descriptors.isEmpty() ? 0 : m_tableView->isChecked() ? 2 : 1);
    }
    if (snapshot.rescanned) refreshPresentation();
    else if (!snapshot.incremental || !samples.isEmpty()) updatePresentation(samples, snapshot.incremental);
    if (snapshot.rescanned) updateVisibleIds();
}

void Dashboard::updatePresentation(const QList<SensorSample> &samples, bool incremental)
{
    if (!isVisible() || isMinimized()) {
        m_gridDirty = m_tableDirty = true;
        return;
    }
    if (m_compact || m_stack->currentIndex() == 1) {
        m_tableDirty = true;
        if (!incremental || m_gridDirty) m_grid->applySamples(m_dashboardSamples.values());
        else m_grid->applySamples(samples, true);
        m_gridDirty = false;
    } else if (m_stack->currentIndex() == 2) {
        m_gridDirty = true;
        if (!incremental || m_tableDirty) m_model->applySamples(m_dashboardSamples.values());
        else m_model->applySamples(samples, true);
        m_tableDirty = false;
    }
}

void Dashboard::refreshPresentation()
{
    // A switch catches up once from the latest dashboard-cadence cache, without
    // requesting extra readings or applying the hidden view's formatting work.
    if ((m_compact || m_stack->currentIndex() == 1) ? m_gridDirty : m_tableDirty)
        updatePresentation({}, true);
}

void Dashboard::setAlarmController(AlarmController *alarms)
{
    m_grid->setAlarmController(alarms);
    m_model->setAlarmController(alarms);
}

void Dashboard::clearReadings()
{
    m_dashboardSamples.clear();
    m_gridDirty = m_tableDirty = true;
    refreshPresentation();
}

void Dashboard::updateVisibleIds()
{
    emit visibleSensorIdsChanged(!isVisible() || isMinimized() ? QStringList()
        : !m_compact && m_tableView->isChecked() ? m_catalogIds : m_grid->visibleIds());
}

void Dashboard::setActions(QAction *hide, QAction *settings,
                           QAction *rescan, QAction *about, QAction *quit)
{
    auto *application = new QMenu(tr("&QSensors"), menuBar());
    menuBar()->insertMenu(menuBar()->actions().first(), application);
    application->addAction(settings);
    application->addAction(rescan);
    application->addSeparator();
    application->addAction(hide);
    application->addAction(quit);
    menuBar()->addMenu(m_settingsMenu);
    menuBar()->addMenu(tr("&Help"))->addAction(about);
    auto *toolbar = findChild<QToolBar *>(QStringLiteral("sensorsToolbar"));
    toolbar->insertAction(toolbar->actions().first(), rescan);
    toolbar->addAction(settings);
    // Keep shortcuts active when the toolbar and menu bar are hidden in compact mode.
    addAction(settings);
    addAction(rescan);
}

void Dashboard::setTrayAvailable(bool available)
{
    m_trayAvailable = available;
    m_trayWarning->setVisible(!available);
}

void Dashboard::closeEvent(QCloseEvent *event)
{
    saveWindowGeometry();
    // Recheck now rather than trusting the host watchdog's previous result.
    if (m_trayAvailable && QSystemTrayIcon::isSystemTrayAvailable()) {
        event->ignore();
        hide();
    } else {
        event->accept();
        emit quitRequested();
    }
}

void Dashboard::setIntervalSeconds(double seconds)
{
    m_intervalSeconds = seconds;
}

void Dashboard::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, this, &Dashboard::fitCompactView);
    updateVisibleIds();
    emit dashboardActiveChanged(!isMinimized());
}

void Dashboard::hideEvent(QHideEvent *event)
{
    finishCompactResize();
    QMainWindow::hideEvent(event);
    updateVisibleIds();
    emit dashboardActiveChanged(false);
}

void Dashboard::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateVisibleIds();
        emit dashboardActiveChanged(isVisible() && !isMinimized());
    }
}

} // namespace qsensors

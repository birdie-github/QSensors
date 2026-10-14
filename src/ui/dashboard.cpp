// SPDX-License-Identifier: GPL-3.0-only
#include "ui/dashboard.h"
#include "ui/sensortablemodel.h"
#include "ui/sensorgrid.h"
#include "settings/sensorpreferences.h"

#include <QAction>
#include <QCoreApplication>
#include <QComboBox>
#include <QIntValidator>
#include <QLineEdit>
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
#include <QSpinBox>
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
    : QMainWindow(parent), m_preferences(preferences)
{
    setWindowTitle(QCoreApplication::applicationName());
    resize(800, 500);
    auto *toolbar = addToolBar(tr("Sensors"));
    toolbar->setObjectName(QStringLiteral("sensorsToolbar"));
    toolbar->setMovable(false);
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Update interval: "), toolbar));
    m_interval = new QSpinBox(toolbar);
    m_interval->setRange(1, 3600);
    m_interval->setSuffix(tr(" s"));
    m_interval->setValue(5);
    m_interval->setKeyboardTracking(false);
    m_interval->setAccessibleName(tr("Update interval in seconds"));
    toolbar->addWidget(m_interval);
    connect(m_interval, &QSpinBox::valueChanged, this, &Dashboard::intervalChanged);

    auto *view = menuBar()->addMenu(tr("&View"));
    m_tableView = view->addAction(tr("Table View"));
    m_tableView->setCheckable(true);
    m_tableView->setChecked(QSettings().value(QStringLiteral("Dashboard/tableView"), false).toBool());
    m_grid = new SensorGrid(preferences, this);
    m_controlsAction = view->addAction(tr("Show Main Window Controls"));
    m_controlsAction->setCheckable(true);
    m_controlsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    addAction(m_controlsAction);
    connect(m_controlsAction, &QAction::toggled, this, &Dashboard::setControlsVisible);
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
    auto *gauges = view->addAction(tr("Show Gauges"));
    gauges->setCheckable(true);
    gauges->setChecked(m_grid->gaugesVisible());
    connect(gauges, &QAction::toggled, m_grid, &SensorGrid::setGaugesVisible);
    auto *sizes = view->addMenu(tr("Tile Size"));
    for (int size : {64, 96, 128, 192, 256, 512, 1024, 2048}) {
        auto *preset = sizes->addAction(tr("%1 × %1").arg(size));
        connect(preset, &QAction::triggered, this, [this, size] {
            m_tileSize->setEditText(QString::number(size));
        });
    }
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Tile size: "), toolbar));
    m_tileSize = new QComboBox(toolbar);
    m_tileSize->setEditable(true);
    m_tileSize->setInsertPolicy(QComboBox::NoInsert);
    m_tileSize->setValidator(new QIntValidator(48, 2048, m_tileSize));
    for (int size : {64, 96, 128, 192, 256, 512, 1024, 2048})
        m_tileSize->addItem(QString::number(size));
    m_tileSize->setCurrentText(QString::number(m_grid->tileSize()));
    m_tileSize->setMinimumContentsLength(4);
    m_tileSize->setToolTip(tr("Square tile size in logical pixels (48–2048)."));
    m_tileSize->setAccessibleName(tr("Tile size in logical pixels"));
    toolbar->addWidget(m_tileSize);
    connect(m_tileSize, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        bool valid = false;
        const int size = text.toInt(&valid);
        if (valid && size >= 48 && size <= 2048)
            m_grid->setTileSize(size);
    });
    connect(m_tileSize->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        const QSignalBlocker blocker(m_tileSize);
        m_tileSize->setCurrentText(QString::number(m_grid->tileSize()));
    });
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
    connect(m_grid, &SensorGrid::visibleIdsChanged, this, [this] { updateVisibleIds(); });
    connect(m_tableView, &QAction::toggled, this, [this](bool table) {
        QSettings().setValue(QStringLiteral("Dashboard/tableView"), table);
        if (!m_compact) m_stack->setCurrentIndex(table ? 2 : 1);
        m_tileSize->setEnabled(!table);
        updateVisibleIds();
    });
    m_tileSize->setEnabled(!m_tableView->isChecked());
    connect(statusBar(), &QStatusBar::messageChanged, empty, &QLabel::setText);
    m_trayWarning = new QLabel(tr("No system tray host — closing the window exits."), this);
    m_trayWarning->setWordWrap(true);
    m_trayWarning->setToolTip(tr("A compatible system tray host is required for background operation."));
    statusBar()->addPermanentWidget(m_trayWarning);
    QSettings settings;
    m_controlsAction->setChecked(true);
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

Dashboard::~Dashboard()
{
    saveWindowGeometry();
}

void Dashboard::setControlsVisible(bool visible)
{
    const QSignalBlocker blocker(m_controlsAction);
    m_controlsAction->setChecked(visible);
    if (m_compact == !visible) return;
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
    fitCompactView();
    if (wasVisible) show();
    // Window flag changes and hidden toolbars settle through posted layout events.
    QTimer::singleShot(0, this, &Dashboard::fitCompactView);
    updateVisibleIds();
}

void Dashboard::fitCompactView()
{
    if (!m_compact) return;
    // Flush the central-widget/chrome layout before deriving the outer size.
    layout()->invalidate();
    layout()->activate();
    const QSize chrome(std::max(0, width() - m_grid->width()),
                       std::max(0, height() - m_grid->height()));
    const QSize field = m_grid->fieldSize() + chrome;
    // Compact mode always contains the complete field, even beyond screen bounds.
    setFixedSize(field);
    layout()->activate();
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

void Dashboard::applySnapshot(const SensorSnapshot &snapshot)
{
    if (snapshot.rescanned) {
        m_catalogIds.clear();
        QList<SensorDescriptor> selected;
        for (const auto &descriptor : snapshot.descriptors) {
            if (m_preferences->value(descriptor.id).dashboard) {
                m_catalogIds.append(descriptor.id);
                selected.append(descriptor);
            }
        }
        m_grid->setDescriptors(selected);
        if (!m_compact) m_stack->setCurrentIndex(snapshot.descriptors.isEmpty() ? 0 : m_tableView->isChecked() ? 2 : 1);
    }
    QList<SensorSample> samples;
    for (const auto &sample : snapshot.samples) {
        if (!snapshot.incremental || snapshot.dashboardRefreshIds.contains(sample.id)) samples.append(sample);
    }
    m_grid->applySamples(samples, snapshot.incremental);
    updateVisibleIds();
}

void Dashboard::clearReadings()
{
    m_grid->applySamples({});
}

void Dashboard::updateVisibleIds()
{
    emit visibleSensorIdsChanged(!isVisible() || isMinimized() ? QStringList()
        : !m_compact && m_tableView->isChecked() ? m_catalogIds : m_grid->visibleIds());
}

void Dashboard::setActions(QAction *show, QAction *hide, QAction *settings,
                           QAction *rescan, QAction *about, QAction *quit)
{
    auto *file = new QMenu(tr("&File"), menuBar());
    menuBar()->insertMenu(menuBar()->actions().first(), file);
    file->addAction(show);
    file->addAction(hide);
    file->addSeparator();
    file->addAction(settings);
    file->addSeparator();
    file->addAction(quit);
    auto *sensors = menuBar()->addMenu(tr("&Sensors"));
    sensors->addAction(rescan);
    menuBar()->addMenu(tr("&Help"))->addAction(about);
    auto *toolbar = findChild<QToolBar *>(QStringLiteral("sensorsToolbar"));
    toolbar->insertAction(toolbar->actions().first(), rescan);
    toolbar->addSeparator();
    toolbar->addAction(settings);
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

void Dashboard::setIntervalSeconds(int seconds)
{
    const QSignalBlocker blocker(m_interval);
    m_interval->setValue(seconds);
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

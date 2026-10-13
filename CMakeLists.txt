# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.21)
project(QSensors VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "QSensors currently targets Linux")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Sensors REQUIRED)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(qsensors
    src/app/main.cpp
    src/app/singleinstance.cpp
    src/app/singleinstance.h
    src/tray/traycontroller.cpp
    src/tray/traycontroller.h
    src/settings/sensorpreferences.cpp
    src/settings/sensorpreferences.h
    src/settings/settingsdialog.cpp
    src/settings/settingsdialog.h
    src/settings/autostart.cpp
    src/settings/autostart.h
    src/sensors/sensorpresentation.cpp
    src/sensors/sensorpresentation.h
    src/system/systemmetrics.cpp
    src/system/systemmetrics.h
    src/system/systemsensorprovider.cpp
    src/system/systemsensorprovider.h
    src/sensors/sensorprovider.h
    src/sensors/sensortypes.h
    src/sensors/lmsensorsprovider.cpp
    src/sensors/lmsensorsprovider.h
    src/sensors/sensormonitor.cpp
    src/sensors/sensormonitor.h
    src/ui/dashboard.cpp
    src/ui/dashboard.h
    src/ui/sensorgrid.cpp
    src/ui/sensorgrid.h
    src/rendering/sensortilerenderer.cpp
    src/rendering/sensortilerenderer.h
    src/rendering/valueformat.cpp
    src/rendering/valueformat.h
    src/ui/sensortablemodel.cpp
    src/ui/sensortablemodel.h
)
target_include_directories(qsensors PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(qsensors PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Sensors::Sensors)
target_compile_definitions(qsensors PRIVATE QSENSORS_VERSION="${PROJECT_VERSION}")

qt_add_resources(qsensors app_icons
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES resources/icons/qsensors-64.png resources/icons/qsensors-256.png
)

install(TARGETS qsensors RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES resources/io.github.birdie_github.QSensors.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
foreach(size IN ITEMS 64 256)
    install(FILES "resources/icons/qsensors-${size}.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
        RENAME io.github.birdie_github.QSensors.png)
endforeach()
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/QSensors")

# Parser fixtures are opt-in and require only the existing Qt Core dependency.
option(QSENSORS_BUILD_TESTS "Build system parser fixtures" OFF)
if(QSENSORS_BUILD_TESTS)
    enable_testing()
    add_executable(qsensors-systemmetrics-test
        tests/systemmetrics_test.cpp
        src/system/systemmetrics.cpp
        src/rendering/valueformat.cpp
    )
    target_include_directories(qsensors-systemmetrics-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(qsensors-systemmetrics-test PRIVATE Qt6::Core)
    add_test(NAME systemmetrics COMMAND qsensors-systemmetrics-test)
endif()

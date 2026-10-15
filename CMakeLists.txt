# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.21)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/project.json" PROJECT_METADATA)
string(JSON APP_NAME GET "${PROJECT_METADATA}" identity name)
string(JSON APP_EXECUTABLE GET "${PROJECT_METADATA}" identity executable)
string(JSON APP_NAMESPACE GET "${PROJECT_METADATA}" identity bundle_namespace)
string(JSON APP_ORGANIZATION GET "${PROJECT_METADATA}" identity organization)
string(JSON APP_DOMAIN GET "${PROJECT_METADATA}" identity organization_domain)
string(JSON APP_VERSION GET "${PROJECT_METADATA}" release version)
string(JSON APP_RELEASE_DATE GET "${PROJECT_METADATA}" release date)
string(JSON APP_DESCRIPTION GET "${PROJECT_METADATA}" project description)
string(JSON APP_REPOSITORY GET "${PROJECT_METADATA}" project repository)
string(JSON APP_LICENSE GET "${PROJECT_METADATA}" project license)
string(JSON APP_OS GET "${PROJECT_METADATA}" platform os)
string(JSON APP_MINIMUM_QT GET "${PROJECT_METADATA}" platform minimum_qt)
string(JSON APP_CPP_STANDARD GET "${PROJECT_METADATA}" platform cpp_standard)
project(${APP_NAME} VERSION ${APP_VERSION} LANGUAGES CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS project.json)
set(APP_DESKTOP_ID "${APP_NAMESPACE}.${APP_NAME}")
configure_file(resources/projectmetadata.h.in generated/projectmetadata.h @ONLY)
configure_file(resources/qsensors.desktop.in "${APP_DESKTOP_ID}.desktop" @ONLY)

if(NOT CMAKE_SYSTEM_NAME STREQUAL APP_OS)
    message(FATAL_ERROR "QSensors currently targets Linux")
endif()

set(CMAKE_CXX_STANDARD ${APP_CPP_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
find_package(Qt6 ${APP_MINIMUM_QT} REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Sensors REQUIRED)
qt_standard_project_setup(REQUIRES ${APP_MINIMUM_QT})

qt_add_executable(qsensors
    src/alarms/alarmsettings.cpp
    src/alarms/alarmsettings.h
    src/alarms/alarmcontroller.cpp
    src/alarms/alarmcontroller.h
    src/settings/alarmeditor.cpp
    src/settings/alarmeditor.h
    src/app/main.cpp
    src/app/singleinstance.cpp
    src/app/singleinstance.h
    src/tray/traycontroller.cpp
    src/tray/traycontroller.h
    src/settings/numberformat.cpp
    src/settings/numberformat.h
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
    src/system/disksensorprovider.cpp
    src/system/disksensorprovider.h
    src/sensors/sensorprovider.h
    src/sensors/sensortypes.h
    src/sensors/lmsensorsprovider.cpp
    src/sensors/lmsensorsprovider.h
    src/sensors/sensormonitor.cpp
    src/sensors/sensormonitor.h
    src/ui/decimalspinbox.cpp
    src/ui/decimalspinbox.h
    src/ui/dashboard.cpp
    src/ui/dashboard.h
    src/ui/dashboardcolorsdialog.cpp
    src/ui/dashboardcolorsdialog.h
    src/ui/sensorgrid.cpp
    src/ui/sensorgrid.h
    src/rendering/sensortilerenderer.cpp
    src/rendering/sensortilerenderer.h
    src/rendering/dashboardappearance.cpp
    src/rendering/dashboardappearance.h
    src/rendering/valueformat.cpp
    src/rendering/valueformat.h
    src/rendering/digitaltext.cpp
    src/rendering/digitaltext.h
    src/rendering/emojiicon.cpp
    src/rendering/emojiicon.h
    src/ui/sensortablemodel.cpp
    src/ui/sensortablemodel.h
)
target_include_directories(qsensors PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_target_properties(qsensors PROPERTIES OUTPUT_NAME "${APP_EXECUTABLE}")
target_link_libraries(qsensors PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Sensors::Sensors)

qt_add_resources(qsensors app_icons
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES resources/icons/qsensors-64.png resources/icons/qsensors-256.png
)

install(TARGETS qsensors RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${APP_DESKTOP_ID}.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
foreach(size IN ITEMS 64 256)
    install(FILES "resources/icons/qsensors-${size}.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
        RENAME "${APP_DESKTOP_ID}.png")
endforeach()
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${APP_NAME}")
install(FILES project.json DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}")

# Parser and settings fixtures are opt-in and use existing Qt dependencies.
option(QSENSORS_BUILD_TESTS "Build parser and settings fixtures" OFF)
if(QSENSORS_BUILD_TESTS)
    enable_testing()
    add_executable(qsensors-systemmetrics-test
        tests/systemmetrics_test.cpp
        src/system/systemmetrics.cpp
        src/rendering/valueformat.cpp
        src/settings/numberformat.cpp
        src/settings/numberformat.h
    )
    target_include_directories(qsensors-systemmetrics-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(qsensors-systemmetrics-test PRIVATE Qt6::Core)
    add_test(NAME systemmetrics COMMAND qsensors-systemmetrics-test)
    add_executable(qsensors-alarmsettings-test
        tests/alarmsettings_test.cpp
        src/alarms/alarmsettings.cpp
    )
    target_include_directories(qsensors-alarmsettings-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(qsensors-alarmsettings-test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME alarmsettings COMMAND qsensors-alarmsettings-test)
endif()

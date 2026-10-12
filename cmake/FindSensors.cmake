# SPDX-License-Identifier: GPL-3.0-only
# libsensors installations need not provide a pkg-config or CMake package.
find_path(Sensors_INCLUDE_DIR NAMES sensors/sensors.h)
find_library(Sensors_LIBRARY NAMES sensors)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Sensors REQUIRED_VARS Sensors_INCLUDE_DIR Sensors_LIBRARY)
mark_as_advanced(Sensors_INCLUDE_DIR Sensors_LIBRARY)

if(Sensors_FOUND AND NOT TARGET Sensors::Sensors)
    add_library(Sensors::Sensors UNKNOWN IMPORTED)
    set_target_properties(Sensors::Sensors PROPERTIES
        IMPORTED_LOCATION "${Sensors_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Sensors_INCLUDE_DIR}"
    )
endif()

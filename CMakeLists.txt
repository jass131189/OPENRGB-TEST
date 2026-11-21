cmake_minimum_required(VERSION 3.16)
project(OpenRGBZotac5070TiPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# OpenRGB 1.0 / 1.1 uses Qt 6; OpenRGB 0.9 uses Qt 5
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    set(QT_WIDGETS_TARGET Qt6::Widgets)
else()
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    set(QT_WIDGETS_TARGET Qt5::Widgets)
endif()

# Point OPENRGB_SRC_DIR to your local clone of https://gitlab.com/CalcProgrammer1/OpenRGB
set(OPENRGB_SRC_DIR "${CMAKE_SOURCE_DIR}/../OpenRGB" CACHE PATH "Path to OpenRGB source tree")

# Detect OPENRGB_PLUGIN_API_VERSION from OpenRGBPluginInterface.h so MetaData matches the host build
set(DETECTED_PLUGIN_API_VERSION 5)
if(EXISTS "${OPENRGB_SRC_DIR}/OpenRGBPluginInterface/OpenRGBPluginInterface.h")
    file(READ "${OPENRGB_SRC_DIR}/OpenRGBPluginInterface/OpenRGBPluginInterface.h" _OPENRGB_IFACE_H)
    if(_OPENRGB_IFACE_H MATCHES "#define[ \t]+OPENRGB_PLUGIN_API_VERSION[ \t]+([0-9]+)")
        set(DETECTED_PLUGIN_API_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

# Always ensure OpenRGBZotac5070TiPlugin.json exists next to OpenRGBZotac5070TiPlugin.h for moc Q_PLUGIN_METADATA
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/OpenRGBZotac5070TiPlugin.json"
"{
    \"OpenRGBPluginAPIVersion\": ${DETECTED_PLUGIN_API_VERSION},
    \"Id\": \"org.spectrabridge.zotac5070ti\",
    \"Name\": \"ZOTAC RTX 5070 Ti SPECTRA Bridge\",
    \"Description\": \"Controls ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC (0x19DA:0x2765) inside OpenRGB 1.1 over NVAPI/I2C.\",
    \"VersionStr\": \"1.0.0\",
    \"Commit\": \"1.1\",
    \"Url\": \"https://gitlab.com/CalcProgrammer1/OpenRGB\"
}
")

add_library(OpenRGBZotac5070TiPlugin SHARED
    OpenRGBZotac5070TiPlugin.h
    OpenRGBZotac5070TiPlugin.cpp
    RGBController_Zotac5070Ti.h
    RGBController_Zotac5070Ti.cpp
    ${OPENRGB_SRC_DIR}/RGBController/RGBController.cpp
    ${OPENRGB_SRC_DIR}/i2c_smbus/i2c_smbus.cpp
)

target_compile_definitions(OpenRGBZotac5070TiPlugin PRIVATE QT_PLUGIN)

target_include_directories(OpenRGBZotac5070TiPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OPENRGB_SRC_DIR}
    ${OPENRGB_SRC_DIR}/qt
    ${OPENRGB_SRC_DIR}/serial_port
    ${OPENRGB_SRC_DIR}/dependencies
    ${OPENRGB_SRC_DIR}/dependencies/json/include
    ${OPENRGB_SRC_DIR}/dependencies/nlohmann
    ${OPENRGB_SRC_DIR}/OpenRGBPluginInterface
    ${OPENRGB_SRC_DIR}/RGBController
    ${OPENRGB_SRC_DIR}/i2c_smbus
    ${OPENRGB_SRC_DIR}/ResourceManager
)

target_link_libraries(OpenRGBZotac5070TiPlugin PRIVATE ${QT_WIDGETS_TARGET})

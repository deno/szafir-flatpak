cmake_minimum_required(VERSION 3.25)

set(APP_VERSION "0.0.0" CACHE STRING "SzafirHostProxy application version")
project(szafir-host-proxy VERSION ${APP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ----- Build-time configuration ---------------------------------------------
set(APP_ID "pl.deno.kir.szafirhostproxy" CACHE STRING "Flatpak application ID")
option(BUNDLED_HOST "Bundle SzafirHost inside the Flatpak instead of delegating to a separate one" OFF)
option(ENABLE_FLATPAK_HOST_ICONS_LOOKUP "Add host icons to path" ON)
option(SZAFIR_HOST_PROXY_SOURCE_PACKAGE_ONLY "Configure only source packaging (skip build deps/targets)" OFF)
set(CHROME_EXTENSION_URL "https://chromewebstore.google.com/detail/szafir-sdk-web/gjalhnomhafafofonpdihihjnbafkipc" CACHE STRING "URL to Chrome extension")
set(FIREFOX_EXTENSION_URL "https://www.elektronicznypodpis.pl/download/webmodule/firefox/szafir_sdk_web-current.xpi" CACHE STRING "URL to Firefox extension")

# Derive D-Bus service/interface ID from APP_ID by dropping the final
# hyphenated suffix, e.g. "...-fh" -> "...".
string(REGEX REPLACE "-[^-]+$" "" DBUS_APP_ID "${APP_ID}")
# Derive the D-Bus object-path segment from DBUS_APP_ID (dots → slashes).
string(REPLACE "." "/" DBUS_OBJECT_PATH_SEGMENT "${DBUS_APP_ID}")
# Internal wrapper template version. Bump when the wrapper format changes
set(WRAPPER_TEMPLATE_VERSION 1)

configure_file(config.h.in config.h)
configure_file(flatpak-host-wrapper.in flatpak-host-wrapper @ONLY)
configure_file(szafir-host-proxy.service.in ${DBUS_APP_ID}.service @ONLY)
configure_file(szafir-host-proxy.desktop.in ${APP_ID}.desktop @ONLY)
configure_file(szafirhost-integration.xml.in szafirhost-integration.xml @ONLY)

set(SZAFIR_HOST_PROXY_REQUIRED_LOCAL_FILES
    LICENSE
    szafir_license_en.md
    szafir_license_pl.md
)

if(BUNDLED_HOST)
    list(APPEND SZAFIR_HOST_PROXY_REQUIRED_LOCAL_FILES components.json)
endif()

foreach(required_file IN LISTS SZAFIR_HOST_PROXY_REQUIRED_LOCAL_FILES)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${required_file}")
        message(FATAL_ERROR "Missing required local source file: ${required_file}. Keep proxy build inputs in szafir-host-proxy/.")
    endif()
endforeach()

# -----------------------------------------------------------------------------

if(NOT SZAFIR_HOST_PROXY_SOURCE_PACKAGE_ONLY)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Network Widgets Qml Quick QuickControls2)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6DBusAddons REQUIRED)
find_package(KF6I18n REQUIRED)
find_package(KF6Kirigami REQUIRED)
find_package(KF6StatusNotifierItem REQUIRED)


qt_standard_project_setup(REQUIRES 6.8)

set(SZAFIR_HOST_PROXY_SOURCES
    main.cpp
    AppSettings.h
    Component.h
    ComponentInfo.h
    ComponentInfo.cpp
    LandlockEnv.h
    LandlockSandbox.h
    LandlockSandbox.cpp
    ScalingController.h
    ScalingController.cpp
    SetupController.h
    SetupController.cpp
    NativeHostIntegrator.h
    NativeHostIntegrator.cpp
    NativeMessagingService.h
    NativeMessagingService.cpp
    HostRuntimeController.h
    HostRuntimeController.cpp
    MainWindow.h
    MainWindow.cpp
)
if(BUNDLED_HOST)
    list(APPEND SZAFIR_HOST_PROXY_SOURCES
        ComponentDownloader.h
        ComponentDownloader.cpp
    )
endif()

add_executable(szafir-host-proxy ${SZAFIR_HOST_PROXY_SOURCES})

qt_add_qml_module(szafir-host-proxy
    URI SzafirHostProxy
    VERSION 1.0
    QML_FILES
        qml/MainWindow.qml
        qml/WelcomePage.qml
        qml/DownloadPage.qml
        qml/LicensePage.qml
        qml/MissingHostPage.qml
        qml/StatusPage.qml
        qml/SzafirHostAboutPage.qml
        qml/AnimatedStateButton.qml
)

qt_add_resources(szafir-host-proxy "szafir-host-proxy-static-assets"
    PREFIX "/"
    FILES
    szafir_license_en.md
    szafir_license_pl.md
)
if(BUNDLED_HOST)
    qt_add_resources(szafir-host-proxy "download-manifest"
        PREFIX "/szafir-host-proxy"
        FILES
        components.json
        system_components.json
    )
endif()

target_include_directories(szafir-host-proxy PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(szafir-host-proxy PRIVATE
    $<$<BOOL:${BUNDLED_HOST}>:BUNDLED_HOST>
    $<$<BOOL:${ENABLE_FLATPAK_HOST_ICONS_LOOKUP}>:ENABLE_FLATPAK_HOST_ICONS_LOOKUP>
    DBUS_APP_ID="${DBUS_APP_ID}"
)

target_link_libraries(szafir-host-proxy
    PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::DBusAddons
    KF6::I18n
    KF6::I18nQml
    KF6::Kirigami
    KF6::StatusNotifierItem
)
if(BUNDLED_HOST)
    target_link_libraries(szafir-host-proxy PRIVATE Qt6::Network)
endif()



install(TARGETS szafir-host-proxy DESTINATION bin)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/flatpak-host-wrapper DESTINATION share/szafir-host-proxy)
install(FILES flatpak-host-wrapper.d/50-icon-example.sh DESTINATION share/szafir-host-proxy/flatpak-host-wrapper.d)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${DBUS_APP_ID}.service DESTINATION share/dbus-1/services)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.desktop DESTINATION share/applications)
install(FILES LICENSE DESTINATION share/licenses/${APP_ID}/szafir-host-proxy)
include(GNUInstallDirs)
set(KDE_INSTALL_LOCALEDIR "${CMAKE_INSTALL_DATAROOTDIR}/locale")
ki18n_install(translations)
endif()

set(CPACK_PACKAGE_NAME "szafir-host-proxy")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "szafir-host-proxy-${PROJECT_VERSION}-source")
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_SOURCE_IGNORE_FILES
    "/\\.git/"
    "/build/"
    "/build-.*/"
    "/_CPack_Packages/"
    "/\\.vscode/"
    "/\\.idea/"
    "~$"
    "\\\\.swp$"
)

include(CPack)

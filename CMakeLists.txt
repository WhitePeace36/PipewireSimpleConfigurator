cmake_minimum_required(VERSION 3.16)

project(PipewireConfigurator VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
find_package(Qt6 6.2 QUIET COMPONENTS Test)

include(GNUInstallDirs)

# Config handling and live state, shared by the application and the tests.
add_library(psc_core STATIC
    app/SpaJson.cpp app/SpaJson.h
    app/Schema.cpp app/Schema.h
    app/ConfigFiles.cpp app/ConfigFiles.h
    app/ConfigModel.cpp app/ConfigModel.h
    app/LiveState.cpp app/LiveState.h
    app/Services.cpp app/Services.h
    app/Checks.cpp app/Checks.h
)
target_include_directories(psc_core PUBLIC app)
target_link_libraries(psc_core PUBLIC Qt6::Core)
qt_add_resources(psc_core "schema" PREFIX "/" BASE resources FILES resources/schema.json)

add_library(psc_ui STATIC
    app/MainWindow.cpp app/MainWindow.h
    app/SettingEditor.cpp app/SettingEditor.h
    app/SettingsPage.cpp app/SettingsPage.h
)
target_link_libraries(psc_ui PUBLIC psc_core Qt6::Widgets)
qt_add_resources(psc_ui "icons" PREFIX "/" BASE resources FILES resources/icons/SimplePipewireConfigurator.png)

add_executable(SimplePipewireConfigurator app/main.cpp)
target_link_libraries(SimplePipewireConfigurator PRIVATE psc_ui)
target_compile_definitions(SimplePipewireConfigurator PRIVATE APP_VERSION="${PROJECT_VERSION}")

install(TARGETS SimplePipewireConfigurator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES SimplePipewireConfigurator.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icons/SimplePipewireConfigurator.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES resources/icons/SimplePipewireConfigurator.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
# Copyright file in the format Debian requires (the license itself is in the system's common licenses).
install(FILES packaging/copyright DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/pipewire-configurator-simple)
# Compressed changelog, as Debian expects it for a native package.
# (gzip -9n: maximum compression and no timestamp, so the package is reproducible.)
find_program(GZIP_EXECUTABLE gzip)
if(NOT GZIP_EXECUTABLE)
    message(FATAL_ERROR "gzip is needed to build the changelog")
endif()
execute_process(COMMAND ${GZIP_EXECUTABLE} -9nc ${CMAKE_CURRENT_SOURCE_DIR}/packaging/changelog
                OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/changelog.gz RESULT_VARIABLE GZIP_RESULT)
if(NOT GZIP_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not compress packaging/changelog")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS packaging/changelog)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/changelog.gz DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/pipewire-configurator-simple)

if(Qt6Test_FOUND)
    enable_testing()
    add_executable(tst_core tests/tst_core.cpp)
    target_link_libraries(tst_core PRIVATE psc_core Qt6::Test)
    target_compile_definitions(tst_core PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME tst_core COMMAND tst_core)

    add_executable(tst_ui tests/tst_ui.cpp)
    target_link_libraries(tst_ui PRIVATE psc_ui Qt6::Test)
    target_compile_definitions(tst_ui PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME tst_ui COMMAND tst_ui)
    set_tests_properties(tst_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# --- Packaging ---
#
#   cpack            builds the .deb (Debian, Ubuntu, ...) and .rpm (Fedora, openSUSE, ...) packages
#   makepkg -si      builds and installs the Arch package from the PKGBUILD in this directory
#
# Build the .deb on the Debian/Ubuntu release it is meant for and the .rpm on Fedora: the library
# dependencies are taken from the system the package is built on.

set(CPACK_PACKAGE_NAME "pipewire-configurator-simple")
set(CPACK_PACKAGE_VENDOR "Timon")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Timon <timon2201@gmail.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/WhitePeace36/PipewireSimpleConfigurator")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GUI tool for PipeWire and WirePlumber settings")
set(CPACK_PACKAGE_DESCRIPTION
    "Pipewire Simple Configurator sets the sample rates and buffer sizes of the\nPipeWire graph, resampling and channel mixing, per-device and Bluetooth\nsettings and per-application rules. It only writes its own drop-in files and\nleaves the rest of the PipeWire and WirePlumber configuration untouched.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
set(CPACK_GENERATOR "DEB;RPM")

# Debian / Ubuntu. Library dependencies are computed by dpkg-shlibdeps (package dpkg-dev).
# pipewire-bin provides pw-dump and pw-metadata; qt6-qpa-plugins provides the X11 platform plugin.
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "pipewire, pipewire-bin, wireplumber, qt6-qpa-plugins")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pipewire-pulse, qt6-wayland")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")

# Fedora / openSUSE. Library dependencies are added automatically by rpmbuild.
# pipewire-utils provides pw-dump and pw-metadata.
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-2.0-only")
set(CPACK_RPM_PACKAGE_RELEASE 1)
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_RPM_PACKAGE_REQUIRES "pipewire, pipewire-utils, wireplumber, qt6-qtbase-gui")
set(CPACK_RPM_PACKAGE_SUGGESTS "pipewire-pulseaudio")
# These directories belong to the system (filesystem, hicolor-icon-theme); the package must not own them.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications
    /usr/share/icons
    /usr/share/icons/hicolor
    /usr/share/icons/hicolor/scalable
    /usr/share/icons/hicolor/scalable/apps
    /usr/share/icons/hicolor/256x256
    /usr/share/icons/hicolor/256x256/apps
    /usr/share/doc)

include(CPack)

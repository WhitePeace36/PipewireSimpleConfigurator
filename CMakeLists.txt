cmake_minimum_required(VERSION 3.16)

project(PipewireConfigurator VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_QML_GENERATE_QMLLS_INI ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(SimplePipewireConfigurator
    main.cpp
)

qt_add_qml_module(SimplePipewireConfigurator
    URI PipewireConfigurator
    QML_FILES
        Main.qml
        SOURCES
        ConfigManager.h
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(SimplePipewireConfigurator PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(SimplePipewireConfigurator
    PRIVATE Qt6::Quick
)

install(TARGETS SimplePipewireConfigurator
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES SimplePipewireConfigurator.desktop DESTINATION share/applications)

# --- Packaging Logic ---

# 1. Basic Metadata
set(CPACK_PACKAGE_NAME "pipewire-configurator-simple")
set(CPACK_PACKAGE_VENDOR "Timon")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A GUI tool for simple Pipewire configuration")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "timon2201@gmail.com")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE") # Ensure you have a LICENSE file

# 2. Specify Generators
set(CPACK_GENERATOR "DEB;RPM;TGZ")

# 3. Debian Specific (APT)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6quick6, libqt6qml6, pipewire") # Basic Qt6 runtime deps
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")

# 4. Fedora Specific (DNF)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, pipewire")

# 5. Arch Linux Specific (Pacman)
# Note: CMake's "External" generator or a simple CPackZST can be used,
# but most Arch users prefer a PKGBUILD.
# For a quick ZST (pacman compatible format):
set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/CPackPacman.cmake")

set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")

include(CPack)

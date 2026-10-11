cmake_minimum_required(VERSION 3.20)

project(netpulse
    VERSION 0.1.0
    DESCRIPTION "NetPulse — Network Monitoring Dashboard (GTK4, C17)"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings: strict but practical for GTK/GLib code.
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wformat=2 -Wformat-security
    -Wnull-dereference -Wdouble-promotion
    -Wshadow -Wimplicit-fallthrough
    -fno-omit-frame-pointer
)
add_compile_definitions(_POSIX_C_SOURCE=200809L _DEFAULT_SOURCE=1)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.66 gio-2.0>=2.66 gobject-2.0)
pkg_check_modules(GTK4 gtk4>=4.6)
pkg_check_modules(ADWAITA libadwaita-1>=1.2)

if(NOT GTK4_FOUND)
    message(WARNING "GTK4 dev files not found (libgtk-4-dev missing). "
        "Core library + unit tests will still build. "
        "Install with: sudo apt-get install -y libgtk-4-dev "
        "then re-run cmake. The 'netpulse' GUI target will be skipped until then.")
endif()

# ---------------------------------------------------------------- core library
# Monitoring / persistence / utils: GLib+GIO only, no GTK dependency.
set(NETPULSE_CORE_SOURCES
    src/core/network.c
    src/core/interface_monitor.c
    src/core/connectivity.c
    src/core/traffic_monitor.c
    src/core/diagnostics.c
    src/services/settings.c
    src/services/exporter.c
    src/utils/formatters.c
    src/utils/error_utils.c
)

add_library(netpulse_core STATIC ${NETPULSE_CORE_SOURCES})
target_include_directories(netpulse_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${GLIB_INCLUDE_DIRS}
)
target_link_directories(netpulse_core PUBLIC ${GLIB_LIBRARY_DIRS})
target_link_libraries(netpulse_core PUBLIC ${GLIB_LIBRARIES} m)
target_compile_options(netpulse_core PUBLIC ${GLIB_CFLAGS_OTHER})

# ------------------------------------------------------------- GUI executable
if(GTK4_FOUND)
    set(NETPULSE_UI_SOURCES
        src/main.c
        src/app.c
        src/ui/main_window.c
        src/ui/sidebar.c
        src/ui/overview_page.c
        src/ui/interfaces_page.c
        src/ui/iface_model.c
        src/ui/connectivity_page.c
        src/ui/traffic_page.c
        src/ui/logs_page.c
        src/ui/settings_page.c
        src/ui/chart.c
        src/ui/widgets.c
    )

    add_executable(netpulse ${NETPULSE_UI_SOURCES})
    # Third-party include dirs are marked SYSTEM so our strict warning set
    # (-Wpedantic, -Wdouble-promotion, ...) doesn't fire on GTK/glib headers.
    # SYSTEM must come before the visibility keyword; putting it after the
    # item list makes CMake read it as a directory named "SYSTEM".
    target_include_directories(netpulse PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_include_directories(netpulse SYSTEM PRIVATE
        ${GLIB_INCLUDE_DIRS} ${GTK4_INCLUDE_DIRS}
    )
    target_link_directories(netpulse PRIVATE ${GLIB_LIBRARY_DIRS} ${GTK4_LIBRARY_DIRS})
    target_link_libraries(netpulse PRIVATE netpulse_core ${GLIB_LIBRARIES} ${GTK4_LIBRARIES} m)

    if(ADWAITA_FOUND)
        message(STATUS "libadwaita found — enabling HAVE_ADWAITA")
        target_compile_definitions(netpulse PRIVATE HAVE_ADWAITA=1)
        target_include_directories(netpulse SYSTEM PRIVATE ${ADWAITA_INCLUDE_DIRS})
        target_link_libraries(netpulse PRIVATE ${ADWAITA_LIBRARIES})
    endif()

    # GResource bundling (optional at build time; app also loads loose files)
    find_program(GLIB_COMPILE_RESOURCES glib-compile-resources)
    if(GLIB_COMPILE_RESOURCES)
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/netpulse_resources.c
            COMMAND ${GLIB_COMPILE_RESOURCES}
                --target=${CMAKE_BINARY_DIR}/netpulse_resources.c
                --generate-source
                ${CMAKE_SOURCE_DIR}/resources/netpulse.gresource.xml
            DEPENDS ${CMAKE_SOURCE_DIR}/resources/netpulse.gresource.xml
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/resources
            COMMENT "Compiling GResources"
        )
        target_sources(netpulse PRIVATE ${CMAKE_BINARY_DIR}/netpulse_resources.c)
        target_compile_definitions(netpulse PRIVATE HAVE_NETPULSE_RESOURCES=1)
        # Machine-generated: it embeds the PNG payloads as long string literals,
        # which trips -Woverlength-strings. Not our code to fix.
        set_source_files_properties(${CMAKE_BINARY_DIR}/netpulse_resources.c
            PROPERTIES COMPILE_OPTIONS "-w")
    endif()

    install(TARGETS netpulse RUNTIME DESTINATION bin)

    # App icon: install into the hicolor theme so the desktop environment's
    # launcher, taskbar and window manager pick up the right mark.
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/16x16/apps/org.netpulse.Monitor.png
            DESTINATION share/icons/hicolor/16x16/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/32x32/apps/org.netpulse.Monitor.png
            DESTINATION share/icons/hicolor/32x32/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/48x48/apps/org.netpulse.Monitor.png
            DESTINATION share/icons/hicolor/48x48/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/64x64/apps/org.netpulse.Monitor.png
            DESTINATION share/icons/hicolor/64x64/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/128x128/apps/org.netpulse.Monitor.png
            DESTINATION share/icons/hicolor/128x128/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/256x256/apps/org.netpulse.Monitor.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/netpulse-icon.svg
            RENAME org.netpulse.Monitor.svg
            DESTINATION share/icons/hicolor/scalable/apps)

    # Interfaces table model tests need GTK (GtkSorter/GtkFilter), so they sit
    # beside the GUI target rather than in the GLib-only suite above.
    add_executable(test_iface_model tests/test_iface_model.c src/ui/iface_model.c)
    target_include_directories(test_iface_model PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_include_directories(test_iface_model SYSTEM PRIVATE
        ${GLIB_INCLUDE_DIRS} ${GTK4_INCLUDE_DIRS}
    )
    target_link_directories(test_iface_model PRIVATE ${GLIB_LIBRARY_DIRS} ${GTK4_LIBRARY_DIRS})
    target_link_libraries(test_iface_model PRIVATE
        netpulse_core ${GLIB_LIBRARIES} ${GTK4_LIBRARIES})
    add_test(NAME test_iface_model COMMAND test_iface_model)
endif()

# ------------------------------------------------------------------- testing
enable_testing()
add_subdirectory(tests)

pkg_check_modules(GLIB_TEST REQUIRED glib-2.0>=2.66 gio-2.0>=2.66 gobject-2.0)

set(NP_TESTS
    test_formatters
    test_traffic_monitor
    test_connectivity
    test_network
    test_settings_export
    test_sidebar_state
)

foreach(t IN LISTS NP_TESTS)
    add_executable(${t} ${t}.c)
    target_include_directories(${t} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${GLIB_TEST_INCLUDE_DIRS}
    )
    target_link_directories(${t} PRIVATE ${GLIB_TEST_LIBRARY_DIRS})
    target_link_libraries(${t} PRIVATE netpulse_core ${GLIB_TEST_LIBRARIES})
    target_compile_options(${t} PRIVATE ${GLIB_TEST_CFLAGS_OTHER})
    add_test(NAME ${t} COMMAND ${t})
endforeach()

#include <glib.h>
#include "netpulse/connectivity.h"

static void
test_bad_hostname_empty(void)
{
    GError *e = NULL;
    g_assert_false(np_validate_hostname("", &e));
    g_assert_nonnull(e);
    g_clear_error(&e);
    g_assert_false(np_validate_hostname(NULL, &e));
    g_clear_error(&e);
}

static void
test_bad_hostname_shell(void)
{
    GError *e = NULL;
    g_assert_false(np_validate_hostname("host; rm -rf /", &e));
    g_clear_error(&e);
    g_assert_false(np_validate_hostname("a$(whoami)", &e));
    g_clear_error(&e);
    g_assert_false(np_validate_hostname("x|y", &e));
    g_clear_error(&e);
}

static void
test_good_hostnames(void)
{
    GError *e = NULL;
    g_assert_true(np_validate_hostname("8.8.8.8", &e));
    g_assert_true(np_validate_hostname("example.com", &e));
    g_assert_true(np_validate_hostname("2001:4860:4860::8888", &e));
    g_assert_no_error(e);
}

static void
test_ports(void)
{
    GError *e = NULL;
    g_assert_false(np_validate_port(0, &e));
    g_clear_error(&e);
    g_assert_false(np_validate_port(70000, &e));
    g_clear_error(&e);
    g_assert_true(np_validate_port(53, &e));
    g_assert_true(np_validate_port(443, &e));
}

static void
test_timeouts(void)
{
    GError *e = NULL;
    g_assert_false(np_validate_timeout_ms(50, &e));
    g_clear_error(&e);
    g_assert_false(np_validate_timeout_ms(999999, &e));
    g_clear_error(&e);
    g_assert_true(np_validate_timeout_ms(3000, &e));
}

static void
test_invalid_host_check(void)
{
    /* Must fail gracefully (no crash, no success), never fabricate success. */
    NpConnectivityResult *r = np_connectivity_check("", NP_TEST_DNS, 0, 1000);
    g_assert_nonnull(r);
    g_assert_false(r->success);
    g_assert_cmpfloat(r->latency_ms, ==, -1.0);
    np_connectivity_result_free(r);
}

static void
test_type_strings(void)
{
    NpTestType t;
    g_assert_true(np_test_type_from_string("dns", &t));
    g_assert_cmpint(t, ==, NP_TEST_DNS);
    g_assert_true(np_test_type_from_string("TCP", &t));
    g_assert_cmpint(t, ==, NP_TEST_TCP);
    g_assert_false(np_test_type_from_string("bogus", &t));
    g_assert_cmpstr(np_test_type_to_string(NP_TEST_PING), ==, "PING");
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/connectivity/hostname-empty", test_bad_hostname_empty);
    g_test_add_func("/connectivity/hostname-shell", test_bad_hostname_shell);
    g_test_add_func("/connectivity/hostname-good", test_good_hostnames);
    g_test_add_func("/connectivity/ports", test_ports);
    g_test_add_func("/connectivity/timeouts", test_timeouts);
    g_test_add_func("/connectivity/invalid-check", test_invalid_host_check);
    g_test_add_func("/connectivity/types", test_type_strings);
    return g_test_run();
}

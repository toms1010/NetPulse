#include <glib.h>
#include <stdio.h>
#include <string.h>
#include "netpulse/settings.h"
#include "netpulse/exporter.h"
#include "netpulse/diagnostics.h"
#include "netpulse/network.h"

static void
test_settings_defaults_valid(void)
{
    NpSettings s;
    np_settings_init_defaults(&s);
    GError *e = NULL;
    g_assert_true(np_settings_validate(&s, &e));
    g_assert_no_error(e);
    g_assert_cmpint(s.monitor_interval_ms, ==, 2000);
    np_settings_clear(&s);
}

static void
test_settings_rejects_bad(void)
{
    NpSettings s;
    np_settings_init_defaults(&s);
    s.monitor_interval_ms = 10; /* too fast */
    GError *e = NULL;
    g_assert_false(np_settings_validate(&s, &e));
    g_assert_nonnull(e);
    g_clear_error(&e);
    s.monitor_interval_ms = 2000;
    g_free(s.target_host);
    s.target_host = g_strdup("bad;host");
    g_assert_false(np_settings_validate(&s, &e));
    g_clear_error(&e);
    np_settings_clear(&s);
}

static void
test_csv_escape(void)
{
    g_autofree char *a = np_csv_escape("plain");
    g_assert_cmpstr(a, ==, "plain");
    g_autofree char *b = np_csv_escape("a,b");
    g_assert_cmpstr(b, ==, "\"a,b\"");
    g_autofree char *c = np_csv_escape("say \"hi\"");
    g_assert_cmpstr(c, ==, "\"say \"\"hi\"\"\"");
}

static void
test_export_logs_header(void)
{
    NpDiagnostics *d = np_diagnostics_new();
    np_diagnostics_log(d, NP_SEV_INFO, "connectivity", "test message");
    g_autofree char *path = g_build_filename(g_get_tmp_dir(), "np-test-logs.csv", NULL);
    GError *e = NULL;
    g_assert_true(np_export_logs_csv(d, path, &e));
    g_assert_no_error(e);
    g_autofree char *contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, &e));
    g_assert_no_error(e);
    g_assert_true(g_str_has_prefix(contents, "timestamp,severity,category,message\n"));
    g_assert_nonnull(strstr(contents, "test message"));
    (void)remove(path);
    np_diagnostics_free(d);
}

static void
test_export_interfaces_header(void)
{
    GError *e = NULL;
    GList *ifaces = np_network_get_interfaces(&e);
    g_assert_no_error(e);
    g_autofree char *path = g_build_filename(g_get_tmp_dir(), "np-test-ifaces.csv", NULL);
    g_assert_true(np_export_interfaces_csv(ifaces, path, &e));
    g_assert_no_error(e);
    g_autofree char *contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, &e));
    g_assert_true(g_str_has_prefix(contents, "name,type,operstate,"));
    (void)remove(path);
    np_interface_list_free(ifaces);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/settings/defaults", test_settings_defaults_valid);
    g_test_add_func("/settings/rejects-bad", test_settings_rejects_bad);
    g_test_add_func("/export/csv-escape", test_csv_escape);
    g_test_add_func("/export/logs", test_export_logs_header);
    g_test_add_func("/export/interfaces", test_export_interfaces_header);
    return g_test_run();
}

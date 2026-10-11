#include <glib.h>
#include "netpulse/formatters.h"

static void
test_bytes(void)
{
    g_autofree char *a = np_format_bytes(0);
    g_assert_cmpstr(a, ==, "0 B");
    g_autofree char *b = np_format_bytes(999);
    g_assert_cmpstr(b, ==, "999 B");
    g_autofree char *c = np_format_bytes(1024);
    g_assert_cmpstr(c, ==, "1.0 KB");
    g_autofree char *d = np_format_bytes(1536);
    g_assert_cmpstr(d, ==, "1.5 KB");
    g_autofree char *e = np_format_bytes(5ULL * 1024 * 1024);
    g_assert_cmpstr(e, ==, "5.0 MB");
}

static void
test_rate(void)
{
    g_autofree char *a = np_format_rate(0.0);
    g_assert_cmpstr(a, ==, "0 B/s");
    g_autofree char *b = np_format_rate(1536.0);
    g_assert_cmpstr(b, ==, "1.5 KB/s");
    g_autofree char *c = np_format_rate(2.5 * 1024 * 1024);
    g_assert_cmpstr(c, ==, "2.5 MB/s");
    g_autofree char *n = np_format_rate(-1.0);
    g_assert_cmpstr(n, ==, "n/a");
}

static void
test_latency(void)
{
    g_autofree char *a = np_format_latency(3.25);
    g_assert_cmpstr(a, ==, "3.2 ms");
    g_autofree char *b = np_format_latency(123.0);
    g_assert_cmpstr(b, ==, "123 ms");
    g_autofree char *n = np_format_latency(-1.0);
    g_assert_cmpstr(n, ==, "n/a");
}

static void
test_timestamp(void)
{
    g_autofree char *t = np_format_timestamp(g_get_real_time());
    g_assert_nonnull(t);
    g_assert_cmpint((int)strlen(t), >=, 10);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/formatters/bytes", test_bytes);
    g_test_add_func("/formatters/rate", test_rate);
    g_test_add_func("/formatters/latency", test_latency);
    g_test_add_func("/formatters/timestamp", test_timestamp);
    return g_test_run();
}

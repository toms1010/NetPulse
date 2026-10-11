#include <glib.h>
#include "netpulse/interface_monitor.h"
#include "netpulse/traffic_monitor.h"

static void
test_basic_rate(void)
{
    double rx = -1, tx = -1;
    np_rates_from_counters(1000, 2000, 3000, 5000, 2.0, &rx, &tx);
    g_assert_cmpfloat_with_epsilon(rx, 1000.0, 0.001);
    g_assert_cmpfloat_with_epsilon(tx, 1500.0, 0.001);
}

static void
test_zero_dt(void)
{
    double rx = 5, tx = 5;
    np_rates_from_counters(100, 100, 200, 200, 0.0, &rx, &tx);
    g_assert_cmpfloat(rx, ==, 0.0);
    g_assert_cmpfloat(tx, ==, 0.0);
}

static void
test_counter_reset(void)
{
    /* Interface recreated: new counter smaller than old. Must not go
     * negative or spike to 2^64/dt. */
    double rx = -1, tx = -1;
    np_rates_from_counters(1000000, 2000000, 500, 800, 1.0, &rx, &tx);
    g_assert_cmpfloat(rx, ==, 500.0);
    g_assert_cmpfloat(tx, ==, 800.0);
    g_assert_cmpfloat(rx, <, 1e9);
}

static void
test_wraparound_32bit(void)
{
    guint64 prev = G_MAXUINT32 - 100ULL;
    guint64 cur = 50ULL; /* wrapped */
    double rx = -1, tx = -1;
    np_rates_from_counters(prev, prev, cur, cur, 1.0, &rx, &tx);
    g_assert_cmpfloat(rx, >=, 0.0);
    g_assert_cmpfloat(rx, <, 1e9);
    (void)tx;
}

static void
test_monitor_first_sample_zero(void)
{
    NpInterfaceMonitor *m = np_interface_monitor_new();
    double rx = -1, tx = -1;
    np_interface_monitor_sample(m, "eth0", 1000, 2000, 1.0, &rx, &tx);
    g_assert_cmpfloat(rx, ==, 0.0);
    g_assert_cmpfloat(tx, ==, 0.0);
    np_interface_monitor_sample(m, "eth0", 3000, 5000, 3.0, &rx, &tx);
    g_assert_cmpfloat_with_epsilon(rx, 1000.0, 0.001);
    g_assert_cmpfloat_with_epsilon(tx, 1500.0, 0.001);
    np_interface_monitor_free(m);
}

static void
test_traffic_window(void)
{
    NpTrafficMonitor *tm = np_traffic_monitor_new("eth0", 10.0);
    for (int i = 0; i < 20; i++)
        np_traffic_monitor_push(tm, (double)i, 100.0 * i, 50.0 * i);
    /* Window is 10s ending at t=19 -> keeps t=9..19 = 11 samples. */
    g_assert_cmpuint(np_traffic_monitor_get_count(tm), ==, 11);
    double rx, tx;
    np_traffic_monitor_get_current(tm, &rx, &tx);
    g_assert_cmpfloat(rx, ==, 1900.0);
    double prx, ptx;
    np_traffic_monitor_get_peak(tm, &prx, &ptx);
    g_assert_cmpfloat(prx, ==, 1900.0);
    np_traffic_monitor_clear(tm);
    g_assert_cmpuint(np_traffic_monitor_get_count(tm), ==, 0);
    np_traffic_monitor_set_paused(tm, TRUE);
    np_traffic_monitor_push(tm, 100.0, 999.0, 999.0);
    g_assert_cmpuint(np_traffic_monitor_get_count(tm), ==, 0);
    np_traffic_monitor_free(tm);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/traffic/basic-rate", test_basic_rate);
    g_test_add_func("/traffic/zero-dt", test_zero_dt);
    g_test_add_func("/traffic/counter-reset", test_counter_reset);
    g_test_add_func("/traffic/wraparound", test_wraparound_32bit);
    g_test_add_func("/traffic/monitor-first-sample", test_monitor_first_sample_zero);
    g_test_add_func("/traffic/window", test_traffic_window);
    return g_test_run();
}

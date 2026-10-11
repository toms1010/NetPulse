#include "netpulse/traffic_monitor.h"

NpTrafficMonitor *
np_traffic_monitor_new(const char *ifname, double history_sec)
{
    NpTrafficMonitor *tm = g_new0(NpTrafficMonitor, 1);
    tm->ifname = g_strdup(ifname ? ifname : "");
    tm->samples = g_array_new(FALSE, TRUE, sizeof(NpTrafficSample));
    tm->history_sec = history_sec > 0 ? history_sec : 120.0;
    return tm;
}

void
np_traffic_monitor_free(NpTrafficMonitor *tm)
{
    if (!tm)
        return;
    g_free(tm->ifname);
    g_array_free(tm->samples, TRUE);
    g_free(tm);
}

void
np_traffic_monitor_set_interface(NpTrafficMonitor *tm, const char *ifname)
{
    g_return_if_fail(tm != NULL);
    if (g_strcmp0(tm->ifname, ifname) != 0) {
        g_free(tm->ifname);
        tm->ifname = g_strdup(ifname ? ifname : "");
        np_traffic_monitor_clear(tm);
    }
}

void
np_traffic_monitor_set_history(NpTrafficMonitor *tm, double history_sec)
{
    g_return_if_fail(tm != NULL);
    if (history_sec > 0)
        tm->history_sec = history_sec;
}

void
np_traffic_monitor_set_paused(NpTrafficMonitor *tm, gboolean paused)
{
    g_return_if_fail(tm != NULL);
    tm->paused = paused;
}

void
np_traffic_monitor_clear(NpTrafficMonitor *tm)
{
    g_return_if_fail(tm != NULL);
    g_array_set_size(tm->samples, 0);
    tm->peak_rx = 0.0;
    tm->peak_tx = 0.0;
}

void
np_traffic_monitor_push(NpTrafficMonitor *tm, double t,
                        double rx_rate, double tx_rate)
{
    g_return_if_fail(tm != NULL);
    if (tm->paused)
        return;
    NpTrafficSample s = { .t = t, .rx_rate = rx_rate, .tx_rate = tx_rate };
    g_array_append_val(tm->samples, s);
    if (rx_rate > tm->peak_rx)
        tm->peak_rx = rx_rate;
    if (tx_rate > tm->peak_tx)
        tm->peak_tx = tx_rate;
    /* Evict samples older than the rolling window. */
    double cutoff = t - tm->history_sec;
    guint drop = 0;
    while (drop < tm->samples->len &&
           g_array_index(tm->samples, NpTrafficSample, drop).t < cutoff)
        drop++;
    if (drop > 0)
        g_array_remove_range(tm->samples, 0, drop);
}

void
np_traffic_monitor_get_current(NpTrafficMonitor *tm,
                               double *out_rx, double *out_tx)
{
    g_return_if_fail(tm != NULL);
    double rx = 0.0, tx = 0.0;
    if (tm->samples->len > 0) {
        NpTrafficSample *last =
            &g_array_index(tm->samples, NpTrafficSample, tm->samples->len - 1);
        rx = last->rx_rate;
        tx = last->tx_rate;
    }
    if (out_rx)
        *out_rx = rx;
    if (out_tx)
        *out_tx = tx;
}

void
np_traffic_monitor_get_peak(NpTrafficMonitor *tm,
                            double *out_peak_rx, double *out_peak_tx)
{
    g_return_if_fail(tm != NULL);
    if (out_peak_rx)
        *out_peak_rx = tm->peak_rx;
    if (out_peak_tx)
        *out_peak_tx = tm->peak_tx;
}

guint
np_traffic_monitor_get_count(NpTrafficMonitor *tm)
{
    g_return_val_if_fail(tm != NULL, 0);
    return tm->samples->len;
}

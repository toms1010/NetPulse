#include "netpulse/interface_monitor.h"

static NpIfaceSampler *
sampler_new(const char *ifname)
{
    NpIfaceSampler *s = g_new0(NpIfaceSampler, 1);
    s->ifname = g_strdup(ifname);
    return s;
}

static void
sampler_free(NpIfaceSampler *s)
{
    if (!s)
        return;
    g_free(s->ifname);
    g_free(s);
}

void
np_rates_from_counters(guint64 prev_rx, guint64 prev_tx,
                       guint64 cur_rx, guint64 cur_tx,
                       double dt_sec,
                       double *out_rx_rate, double *out_tx_rate)
{
    double rx = 0.0, tx = 0.0;
    if (dt_sec > 0.0) {
        /* Counter reset / wraparound / interface recreation: never emit a
         * negative rate or a giant 2^64 spike. Treat the new counter as the
         * baseline delta. */
        guint64 drx = (cur_rx >= prev_rx) ? (cur_rx - prev_rx) : cur_rx;
        guint64 dtx = (cur_tx >= prev_tx) ? (cur_tx - prev_tx) : cur_tx;
        rx = (double)drx / dt_sec;
        tx = (double)dtx / dt_sec;
    }
    if (out_rx_rate)
        *out_rx_rate = rx;
    if (out_tx_rate)
        *out_tx_rate = tx;
}

NpInterfaceMonitor *
np_interface_monitor_new(void)
{
    NpInterfaceMonitor *mon = g_new0(NpInterfaceMonitor, 1);
    mon->samplers = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, (GDestroyNotify)sampler_free);
    return mon;
}

void
np_interface_monitor_free(NpInterfaceMonitor *mon)
{
    if (!mon)
        return;
    g_hash_table_destroy(mon->samplers);
    g_free(mon);
}

void
np_interface_monitor_sample(NpInterfaceMonitor *mon,
                            const char *ifname,
                            guint64 rx_bytes, guint64 tx_bytes,
                            double now_t,
                            double *out_rx_rate, double *out_tx_rate)
{
    g_return_if_fail(mon != NULL && ifname != NULL);
    NpIfaceSampler *s = g_hash_table_lookup(mon->samplers, ifname);
    if (!s) {
        s = sampler_new(ifname);
        g_hash_table_insert(mon->samplers, g_strdup(ifname), s);
    }
    double rx = 0.0, tx = 0.0;
    if (s->has_prev)
        np_rates_from_counters(s->prev_rx, s->prev_tx, rx_bytes, tx_bytes,
                               now_t - s->prev_t, &rx, &tx);
    s->prev_rx = rx_bytes;
    s->prev_tx = tx_bytes;
    s->prev_t = now_t;
    s->has_prev = TRUE;
    s->rx_rate = rx;
    s->tx_rate = tx;
    if (out_rx_rate)
        *out_rx_rate = rx;
    if (out_tx_rate)
        *out_tx_rate = tx;
}

void
np_interface_monitor_prune(NpInterfaceMonitor *mon, GList *current_ifaces)
{
    g_return_if_fail(mon != NULL);
    GHashTable *alive = g_hash_table_new(g_str_hash, g_str_equal);
    for (GList *l = current_ifaces; l; l = l->next) {
        NpInterface *iface = l->data;
        g_hash_table_add(alive, iface->name);
    }
    GList *dead = NULL;
    GHashTableIter iter;
    gpointer k, v;
    g_hash_table_iter_init(&iter, mon->samplers);
    while (g_hash_table_iter_next(&iter, &k, &v)) {
        if (!g_hash_table_contains(alive, k))
            dead = g_list_prepend(dead, k);
    }
    for (GList *l = dead; l; l = l->next)
        g_hash_table_remove(mon->samplers, l->data);
    g_list_free(dead);
    g_hash_table_destroy(alive);
}

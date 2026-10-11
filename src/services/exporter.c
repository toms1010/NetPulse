#include "netpulse/exporter.h"
#include "netpulse/formatters.h"
#include "netpulse/error_utils.h"
#include <stdio.h>

char *
np_csv_escape(const char *field)
{
    if (!field)
        return g_strdup("");
    gboolean need_quotes = strpbrk(field, ",\"\n\r") != NULL;
    if (!need_quotes)
        return g_strdup(field);
    GString *s = g_string_new("\"");
    for (const char *p = field; *p; p++) {
        if (*p == '"')
            g_string_append(s, "\"\"");
        else
            g_string_append_c(s, *p);
    }
    g_string_append_c(s, '"');
    return g_string_free(s, FALSE);
}

static gboolean
write_all(FILE *f, const char *path, GError **error, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    g_autofree char *line = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    if (fputs(line, f) == EOF) {
        np_set_error(error, NP_ERR_IO, "Write failed for '%s'", path);
        return FALSE;
    }
    return TRUE;
}

gboolean
np_export_interfaces_csv(GList *ifaces, const char *path, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);
    FILE *f = fopen(path, "w");
    if (!f) {
        np_set_error(error, NP_ERR_IO, "Cannot open '%s' for writing", path);
        return FALSE;
    }
    gboolean ok = write_all(f, path, error,
        "name,type,operstate,is_up,ipv4,ipv6,rx_bytes,tx_bytes,rx_packets,tx_packets\n");
    for (GList *l = ifaces; ok && l; l = l->next) {
        NpInterface *i = l->data;
        g_autofree char *name = np_csv_escape(i->name);
        g_autofree char *type = np_csv_escape(i->type ? i->type : "unknown");
        g_autofree char *state = np_csv_escape(i->operstate ? i->operstate : "unknown");
        g_autofree char *v4 = np_csv_escape(i->ipv4 ? i->ipv4 : "");
        g_autofree char *v6 = np_csv_escape(i->ipv6 ? i->ipv6 : "");
        ok = write_all(f, path, error,
            "%s,%s,%s,%d,%s,%s,%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT
            ",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT "\n",
            name, type, state, i->is_up ? 1 : 0, v4, v6,
            i->rx_bytes, i->tx_bytes, i->rx_packets, i->tx_packets);
    }
    if (fclose(f) != 0 && ok)
        np_set_error(error, NP_ERR_IO, "Failed to close '%s'", path), (ok = FALSE);
    return ok;
}

gboolean
np_export_traffic_csv(NpTrafficMonitor *tm, const char *path, GError **error)
{
    g_return_val_if_fail(tm != NULL && path != NULL, FALSE);
    FILE *f = fopen(path, "w");
    if (!f) {
        np_set_error(error, NP_ERR_IO, "Cannot open '%s' for writing", path);
        return FALSE;
    }
    gboolean ok = write_all(f, path, error,
        "timestamp,interface,rx_bytes_per_sec,tx_bytes_per_sec,units\n");
    g_autofree char *iface = np_csv_escape(tm->ifname);
    for (guint i = 0; ok && i < tm->samples->len; i++) {
        NpTrafficSample *s = &g_array_index(tm->samples, NpTrafficSample, i);
        /* Monotonic sample time (seconds); header documents the epoch basis.
         * See docs/ui-design.md defect #2 for the wall-clock join plan. */
        ok = write_all(f, path, error, "%.3f,%s,%.1f,%.1f,B/s\n",
                       s->t, iface, s->rx_rate, s->tx_rate);
    }
    if (fclose(f) != 0 && ok)
        np_set_error(error, NP_ERR_IO, "Failed to close '%s'", path), (ok = FALSE);
    return ok;
}

gboolean
np_export_logs_csv(NpDiagnostics *diag, const char *path, GError **error)
{
    g_return_val_if_fail(diag != NULL && path != NULL, FALSE);
    FILE *f = fopen(path, "w");
    if (!f) {
        np_set_error(error, NP_ERR_IO, "Cannot open '%s' for writing", path);
        return FALSE;
    }
    gboolean ok = write_all(f, path, error, "timestamp,severity,category,message\n");
    g_autoptr(GPtrArray) rows = np_diagnostics_query(diag, NP_SEV_DEBUG, NULL, NULL, 0);
    for (guint i = 0; ok && i < rows->len; i++) {
        NpLogEntry *e = rows->pdata[i];
        g_autofree char *ts = np_format_timestamp(e->timestamp_us);
        g_autofree char *sev = np_csv_escape(np_severity_to_string(e->severity));
        g_autofree char *cat = np_csv_escape(e->category);
        g_autofree char *msg = np_csv_escape(e->message);
        ok = write_all(f, path, error, "%s,%s,%s,%s\n", ts, sev, cat, msg);
    }
    if (fclose(f) != 0 && ok)
        np_set_error(error, NP_ERR_IO, "Failed to close '%s'", path), (ok = FALSE);
    return ok;
}

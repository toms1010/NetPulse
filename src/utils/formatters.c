#include "netpulse/formatters.h"
#include <math.h>
#include <stdio.h>

char *
np_format_bytes(guint64 bytes)
{
    const char *units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        u++;
    }
    if (u == 0)
        return g_strdup_printf("%" G_GUINT64_FORMAT " B", bytes);
    return g_strdup_printf("%.1f %s", v, units[u]);
}

char *
np_format_rate(double bytes_per_sec)
{
    if (bytes_per_sec < 0.0 || !isfinite(bytes_per_sec))
        return g_strdup("n/a");
    const char *units[] = { "B/s", "KB/s", "MB/s", "GB/s" };
    double v = bytes_per_sec;
    int u = 0;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        u++;
    }
    if (u == 0)
        return g_strdup_printf("%.0f B/s", v);
    return g_strdup_printf("%.1f %s", v, units[u]);
}

char *
np_format_latency(double ms)
{
    if (ms < 0.0 || !isfinite(ms))
        return g_strdup("n/a");
    if (ms < 10.0)
        return g_strdup_printf("%.1f ms", ms);
    return g_strdup_printf("%.0f ms", ms);
}

char *
np_format_timestamp(gint64 timestamp_us)
{
    GDateTime *dt = g_date_time_new_from_unix_local(timestamp_us / 1000000);
    char *s;
    if (!dt)
        return g_strdup("n/a");
    s = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
    g_date_time_unref(dt);
    return s ? s : g_strdup("n/a");
}

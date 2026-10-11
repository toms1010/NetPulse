#include "netpulse/diagnostics.h"
#include <stdarg.h>

struct _NpDiagnostics {
    GMutex      mutex;
    GPtrArray  *entries; /* NpLogEntry*, owned */
};

const char *
np_severity_to_string(NpSeverity s)
{
    switch (s) {
    case NP_SEV_DEBUG:   return "DEBUG";
    case NP_SEV_INFO:    return "INFO";
    case NP_SEV_WARNING: return "WARNING";
    case NP_SEV_ERROR:   return "ERROR";
    default:             return "INFO";
    }
}

NpSeverity
np_severity_from_string(const char *s)
{
    if (!s)
        return NP_SEV_INFO;
    if (g_ascii_strcasecmp(s, "debug") == 0)
        return NP_SEV_DEBUG;
    if (g_ascii_strcasecmp(s, "warning") == 0 || g_ascii_strcasecmp(s, "warn") == 0)
        return NP_SEV_WARNING;
    if (g_ascii_strcasecmp(s, "error") == 0 || g_ascii_strcasecmp(s, "err") == 0)
        return NP_SEV_ERROR;
    return NP_SEV_INFO;
}

void
np_log_entry_free(NpLogEntry *e)
{
    if (!e)
        return;
    g_free(e->category);
    g_free(e->message);
    g_free(e);
}

NpDiagnostics *
np_diagnostics_new(void)
{
    NpDiagnostics *d = g_new0(NpDiagnostics, 1);
    g_mutex_init(&d->mutex);
    d->entries = g_ptr_array_new_with_free_func((GDestroyNotify)np_log_entry_free);
    return d;
}

void
np_diagnostics_free(NpDiagnostics *d)
{
    if (!d)
        return;
    g_mutex_clear(&d->mutex);
    g_ptr_array_free(d->entries, TRUE);
    g_free(d);
}

void
np_diagnostics_log(NpDiagnostics *d, NpSeverity sev,
                   const char *category, const char *fmt, ...)
{
    g_return_if_fail(d != NULL && fmt != NULL);
    va_list ap;
    va_start(ap, fmt);
    char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    NpLogEntry *e = g_new0(NpLogEntry, 1);
    e->timestamp_us = g_get_real_time();
    e->severity = sev;
    e->category = g_strdup(category ? category : "app");
    e->message = msg;

    g_mutex_lock(&d->mutex);
    g_ptr_array_add(d->entries, e);
    /* Cap in-memory history to keep the app bounded. */
    if (d->entries->len > 5000)
        g_ptr_array_remove_index(d->entries, 0);
    g_mutex_unlock(&d->mutex);
}

guint
np_diagnostics_get_count(NpDiagnostics *d)
{
    g_return_val_if_fail(d != NULL, 0);
    g_mutex_lock(&d->mutex);
    guint n = d->entries->len;
    g_mutex_unlock(&d->mutex);
    return n;
}

GPtrArray *
np_diagnostics_query(NpDiagnostics *d,
                     NpSeverity min_sev,
                     const char *category_substr,
                     const char *text_substr,
                     gint64 since_us)
{
    g_return_val_if_fail(d != NULL, NULL);
    GPtrArray *out = g_ptr_array_new();
    g_mutex_lock(&d->mutex);
    for (guint i = 0; i < d->entries->len; i++) {
        NpLogEntry *e = d->entries->pdata[i];
        if (e->severity < min_sev)
            continue;
        if (since_us > 0 && e->timestamp_us < since_us)
            continue;
        if (category_substr && *category_substr &&
            !strstr(e->category, category_substr))
            continue;
        if (text_substr && *text_substr && !strstr(e->message, text_substr))
            continue;
        g_ptr_array_add(out, e);
    }
    g_mutex_unlock(&d->mutex);
    return out;
}

void
np_diagnostics_clear(NpDiagnostics *d)
{
    g_return_if_fail(d != NULL);
    g_mutex_lock(&d->mutex);
    g_ptr_array_set_size(d->entries, 0);
    g_mutex_unlock(&d->mutex);
}

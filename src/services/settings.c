#include "netpulse/settings.h"
#include "netpulse/error_utils.h"
#include "netpulse/connectivity.h"

static const char *GROUP = "netpulse";

const char *
np_theme_to_string(NpTheme t)
{
    switch (t) {
    case NP_THEME_LIGHT: return "light";
    case NP_THEME_DARK:  return "dark";
    default:             return "system";
    }
}

NpTheme
np_theme_from_string(const char *s)
{
    if (!s)
        return NP_THEME_SYSTEM;
    if (g_ascii_strcasecmp(s, "light") == 0)
        return NP_THEME_LIGHT;
    if (g_ascii_strcasecmp(s, "dark") == 0)
        return NP_THEME_DARK;
    return NP_THEME_SYSTEM;
}

void
np_settings_init_defaults(NpSettings *s)
{
    g_return_if_fail(s != NULL);
    memset(s, 0, sizeof *s);
    s->monitor_interval_ms = 2000;
    s->sample_interval_ms = 1000;
    s->chart_history_sec = 120.0;
    s->target_host = g_strdup("8.8.8.8");
    s->target_port = 53;
    s->timeout_ms = 3000;
    s->theme = NP_THEME_SYSTEM;
    s->latency_warn_ms = 250.0;
    s->warnings_enabled = TRUE;
    s->sidebar_collapsed = FALSE;
    s->selected_iface = NULL;
}

void
np_settings_clear(NpSettings *s)
{
    if (!s)
        return;
    g_free(s->target_host);
    g_free(s->selected_iface);
    memset(s, 0, sizeof *s);
}

void
np_settings_reset_to_defaults(NpSettings *s)
{
    g_return_if_fail(s != NULL);
    np_settings_clear(s);
    np_settings_init_defaults(s);
}

gboolean
np_settings_validate(const NpSettings *s, GError **error)
{
    g_return_val_if_fail(s != NULL, FALSE);
    if (s->monitor_interval_ms < 500 || s->monitor_interval_ms > 60000) {
        np_set_error(error, NP_ERR_INVALID_INPUT,
                     "Monitoring interval %d ms out of range (500–60000)",
                     s->monitor_interval_ms);
        return FALSE;
    }
    if (s->sample_interval_ms < 250 || s->sample_interval_ms > 10000) {
        np_set_error(error, NP_ERR_INVALID_INPUT,
                     "Sampling interval %d ms out of range (250–10000)",
                     s->sample_interval_ms);
        return FALSE;
    }
    if (s->chart_history_sec < 15.0 || s->chart_history_sec > 3600.0) {
        np_set_error(error, NP_ERR_INVALID_INPUT,
                     "Chart history %.0f s out of range (15–3600)",
                     s->chart_history_sec);
        return FALSE;
    }
    if (!np_validate_hostname(s->target_host, error))
        return FALSE;
    if (!np_validate_port(s->target_port, error))
        return FALSE;
    if (!np_validate_timeout_ms(s->timeout_ms, error))
        return FALSE;
    if (s->latency_warn_ms < 10.0 || s->latency_warn_ms > 10000.0) {
        np_set_error(error, NP_ERR_INVALID_INPUT,
                     "Latency warning threshold %.0f ms out of range (10–10000)",
                     s->latency_warn_ms);
        return FALSE;
    }
    return TRUE;
}

char *
np_settings_config_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "netpulse",
                            "netpulse.conf", NULL);
}

NpSettings *
np_settings_load(GError **error)
{
    NpSettings *s = g_new0(NpSettings, 1);
    np_settings_init_defaults(s);

    g_autofree char *path = np_settings_config_path();
    g_autoptr(GKeyFile) kf = g_key_file_new();
    GError *load_err = NULL;
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &load_err)) {
        if (!g_error_matches(load_err, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_propagate_error(error, load_err);
            return s; /* return defaults alongside the error */
        }
        g_clear_error(&load_err);
        return s; /* first run: defaults */
    }

    /* Each key is optional; invalid values fall back to defaults. */
    if (g_key_file_has_key(kf, GROUP, "monitor-interval-ms", NULL))
        s->monitor_interval_ms = g_key_file_get_integer(kf, GROUP, "monitor-interval-ms", NULL);
    if (g_key_file_has_key(kf, GROUP, "sample-interval-ms", NULL))
        s->sample_interval_ms = g_key_file_get_integer(kf, GROUP, "sample-interval-ms", NULL);
    if (g_key_file_has_key(kf, GROUP, "chart-history-sec", NULL))
        s->chart_history_sec = g_key_file_get_double(kf, GROUP, "chart-history-sec", NULL);
    if (g_key_file_has_key(kf, GROUP, "target-host", NULL)) {
        g_autofree char *h = g_key_file_get_string(kf, GROUP, "target-host", NULL);
        if (h && *h) {
            g_free(s->target_host);
            s->target_host = g_steal_pointer(&h);
        }
    }
    if (g_key_file_has_key(kf, GROUP, "target-port", NULL))
        s->target_port = g_key_file_get_integer(kf, GROUP, "target-port", NULL);
    if (g_key_file_has_key(kf, GROUP, "timeout-ms", NULL))
        s->timeout_ms = g_key_file_get_integer(kf, GROUP, "timeout-ms", NULL);
    if (g_key_file_has_key(kf, GROUP, "theme", NULL)) {
        g_autofree char *t = g_key_file_get_string(kf, GROUP, "theme", NULL);
        s->theme = np_theme_from_string(t);
    }
    if (g_key_file_has_key(kf, GROUP, "latency-warn-ms", NULL))
        s->latency_warn_ms = g_key_file_get_double(kf, GROUP, "latency-warn-ms", NULL);
    if (g_key_file_has_key(kf, GROUP, "warnings-enabled", NULL))
        s->warnings_enabled = g_key_file_get_boolean(kf, GROUP, "warnings-enabled", NULL);
    if (g_key_file_has_key(kf, GROUP, "sidebar-collapsed", NULL))
        s->sidebar_collapsed = g_key_file_get_boolean(kf, GROUP, "sidebar-collapsed", NULL);
    if (g_key_file_has_key(kf, GROUP, "selected-iface", NULL)) {
        g_autofree char *i = g_key_file_get_string(kf, GROUP, "selected-iface", NULL);
        g_free(s->selected_iface);
        s->selected_iface = (i && *i) ? g_steal_pointer(&i) : NULL;
    }

    GError *verr = NULL;
    if (!np_settings_validate(s, &verr)) {
        g_warning("Stored settings invalid (%s); using defaults for bad keys", verr->message);
        g_clear_error(&verr);
        /* Keep what loaded; the UI will surface validation on edit. */
    }
    return s;
}

gboolean
np_settings_save(const NpSettings *s, GError **error)
{
    g_return_val_if_fail(s != NULL, FALSE);
    GError *verr = NULL;
    if (!np_settings_validate(s, &verr)) {
        g_propagate_error(error, verr);
        return FALSE;
    }
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_key_file_set_integer(kf, GROUP, "monitor-interval-ms", s->monitor_interval_ms);
    g_key_file_set_integer(kf, GROUP, "sample-interval-ms", s->sample_interval_ms);
    g_key_file_set_double(kf, GROUP, "chart-history-sec", s->chart_history_sec);
    g_key_file_set_string(kf, GROUP, "target-host", s->target_host ? s->target_host : "8.8.8.8");
    g_key_file_set_integer(kf, GROUP, "target-port", s->target_port);
    g_key_file_set_integer(kf, GROUP, "timeout-ms", s->timeout_ms);
    g_key_file_set_string(kf, GROUP, "theme", np_theme_to_string(s->theme));
    g_key_file_set_double(kf, GROUP, "latency-warn-ms", s->latency_warn_ms);
    g_key_file_set_boolean(kf, GROUP, "warnings-enabled", s->warnings_enabled);
    g_key_file_set_boolean(kf, GROUP, "sidebar-collapsed", s->sidebar_collapsed);
    g_key_file_set_string(kf, GROUP, "selected-iface",
                          s->selected_iface ? s->selected_iface : "");

    g_autofree char *path = np_settings_config_path();
    g_autofree char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        np_set_error(error, NP_ERR_IO, "Cannot create config dir '%s'", dir);
        return FALSE;
    }
    return g_key_file_save_to_file(kf, path, error);
}

#include "netpulse/ui.h"

typedef struct {
    NpWindowState *st;
    GtkWidget *monitor_spin;
    GtkWidget *sample_spin;
    GtkWidget *history_spin;
    GtkWidget *host_entry;
    GtkWidget *port_spin;
    GtkWidget *timeout_spin;
    GtkWidget *theme_drop;
    GtkWidget *lat_spin;
    GtkWidget *warn_switch;
    GtkWidget *status_label;
} SettingsCtx;

static void
on_save_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    SettingsCtx *c = data;
    NpSettings *s = c->st->settings;

    s->monitor_interval_ms = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(c->monitor_spin));
    s->sample_interval_ms = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(c->sample_spin));
    s->chart_history_sec = gtk_spin_button_get_value(
        GTK_SPIN_BUTTON(c->history_spin));
    g_free(s->target_host);
    s->target_host = g_strdup(
        gtk_editable_get_text(GTK_EDITABLE(c->host_entry)));
    s->target_port = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(c->port_spin));
    s->timeout_ms = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(c->timeout_spin));
    guint theme_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(c->theme_drop));
    s->theme = (theme_sel == 1) ? NP_THEME_LIGHT :
               (theme_sel == 2) ? NP_THEME_DARK : NP_THEME_SYSTEM;
    s->latency_warn_ms = gtk_spin_button_get_value(
        GTK_SPIN_BUTTON(c->lat_spin));
    s->warnings_enabled = gtk_switch_get_active(GTK_SWITCH(c->warn_switch));

    GError *err = NULL;
    if (!np_settings_validate(s, &err)) {
        gtk_label_set_text(GTK_LABEL(c->status_label), err->message);
        np_diagnostics_log(c->st->diag, NP_SEV_WARNING, "settings",
                           "Settings validation failed: %s", err->message);
        g_clear_error(&err);
        return;
    }
    if (!np_settings_save(s, &err)) {
        gtk_label_set_text(GTK_LABEL(c->status_label), err->message);
        g_clear_error(&err);
        return;
    }
    np_traffic_monitor_set_history(c->st->traffic, s->chart_history_sec);
    gtk_label_set_text(GTK_LABEL(c->status_label), "Settings saved.");
    np_diagnostics_log(c->st->diag, NP_SEV_INFO, "settings",
                       "Settings saved by user");
    np_show_toast(c->st, "Settings saved");
}

static void
on_reset_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    SettingsCtx *c = data;
    np_settings_reset_to_defaults(c->st->settings);
    GError *err = NULL;
    if (!np_settings_save(c->st->settings, &err)) {
        gtk_label_set_text(GTK_LABEL(c->status_label), err->message);
        g_clear_error(&err);
        return;
    }
    np_settings_refresh(c->st);
    gtk_label_set_text(GTK_LABEL(c->status_label),
                       "Settings reset to defaults.");
    np_show_toast(c->st, "Settings reset to defaults");
}

static void
on_ifaces_saved(char *path, gpointer u)
{
    SettingsCtx *cc = u;
    if (!path)
        return; /* cancelled */
    GError *e = NULL;
    if (np_export_interfaces_csv(cc->st->ifaces, path, &e)) {
        np_show_toast(cc->st, "Interface statistics exported");
        np_diagnostics_log(cc->st->diag, NP_SEV_INFO, "export",
                           "Interfaces exported to %s", path);
    } else {
        np_show_toast(cc->st, (e && e->message) ? e->message : "Export failed");
        np_diagnostics_log(cc->st->diag, NP_SEV_ERROR, "export",
                           "Interface export failed: %s",
                           (e && e->message) ? e->message : "?");
        g_clear_error(&e);
    }
}

static void
on_export_ifaces(GtkButton *btn, gpointer data)
{
    (void)btn;
    SettingsCtx *c = data;
    np_show_save_dialog(c->st, "Export interface statistics",
                        "netpulse-interfaces.csv", on_ifaces_saved, c);
}

static GtkWidget *
spin_row(GtkWidget *grid, int row, const char *label, GtkWidget *spin)
{
    GtkWidget *l = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), spin, 1, row, 1, 1);
    return spin;
}

GtkWidget *
np_settings_page_new(NpWindowState *state)
{
    SettingsCtx *c = g_new0(SettingsCtx, 1);
    c->st = state;
    state->page_ctx[NP_PAGE_SETTINGS] = c;

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(outer, 24);
    gtk_widget_set_margin_end(outer, 24);
    gtk_widget_set_margin_top(outer, 20);
    gtk_widget_set_margin_bottom(outer, 20);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), outer);
    gtk_widget_set_hexpand(sw, TRUE);
    gtk_widget_set_vexpand(sw, TRUE);

    gtk_box_append(GTK_BOX(outer), np_section_label_new("Settings"));

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_box_append(GTK_BOX(outer), grid);

    NpSettings *s = state->settings;
    c->monitor_spin = gtk_spin_button_new_with_range(500, 60000, 500);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->monitor_spin),
                              s ? s->monitor_interval_ms : 2000);
    spin_row(grid, 0, "Monitoring interval (ms)", c->monitor_spin);

    c->sample_spin = gtk_spin_button_new_with_range(250, 10000, 250);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->sample_spin),
                              s ? s->sample_interval_ms : 1000);
    spin_row(grid, 1, "Sampling interval (ms)", c->sample_spin);

    c->history_spin = gtk_spin_button_new_with_range(15, 3600, 15);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->history_spin),
                              s ? s->chart_history_sec : 120.0);
    spin_row(grid, 2, "Chart history (s)", c->history_spin);

    GtkWidget *hl = gtk_label_new("Connectivity target host");
    gtk_label_set_xalign(GTK_LABEL(hl), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), hl, 0, 3, 1, 1);
    c->host_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(c->host_entry),
                          s && s->target_host ? s->target_host : "8.8.8.8");
    gtk_grid_attach(GTK_GRID(grid), c->host_entry, 1, 3, 1, 1);

    c->port_spin = gtk_spin_button_new_with_range(1, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->port_spin),
                              s ? s->target_port : 53);
    spin_row(grid, 4, "Target port", c->port_spin);

    c->timeout_spin = gtk_spin_button_new_with_range(200, 60000, 100);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->timeout_spin),
                              s ? s->timeout_ms : 3000);
    spin_row(grid, 5, "Timeout (ms)", c->timeout_spin);

    GtkWidget *thl = gtk_label_new("Appearance");
    gtk_label_set_xalign(GTK_LABEL(thl), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), thl, 0, 6, 1, 1);
    c->theme_drop = gtk_drop_down_new_from_strings(
        (const char *[]){"System", "Light", "Dark", NULL});
    guint tsel = 0;
    if (s && s->theme == NP_THEME_LIGHT)
        tsel = 1;
    else if (s && s->theme == NP_THEME_DARK)
        tsel = 2;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(c->theme_drop), tsel);
    gtk_grid_attach(GTK_GRID(grid), c->theme_drop, 1, 6, 1, 1);

    c->lat_spin = gtk_spin_button_new_with_range(10, 10000, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->lat_spin),
                              s ? s->latency_warn_ms : 250.0);
    spin_row(grid, 7, "Latency warning (ms)", c->lat_spin);

    GtkWidget *wl = gtk_label_new("Enable warnings");
    gtk_label_set_xalign(GTK_LABEL(wl), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), wl, 0, 8, 1, 1);
    c->warn_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(c->warn_switch),
                          s ? s->warnings_enabled : TRUE);
    gtk_widget_set_halign(c->warn_switch, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), c->warn_switch, 1, 8, 1, 1);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(outer), actions);
    GtkWidget *save_btn = gtk_button_new_with_label("Save Settings");
    gtk_widget_add_css_class(save_btn, "suggested-action");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), c);
    gtk_box_append(GTK_BOX(actions), save_btn);
    GtkWidget *reset_btn = gtk_button_new_with_label("Restore Defaults");
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_clicked), c);
    gtk_box_append(GTK_BOX(actions), reset_btn);
    GtkWidget *exp_btn = gtk_button_new_with_label("Export Interfaces CSV");
    g_signal_connect(exp_btn, "clicked", G_CALLBACK(on_export_ifaces), c);
    gtk_box_append(GTK_BOX(actions), exp_btn);

    c->status_label = gtk_label_new("");
    gtk_widget_add_css_class(c->status_label, "np-muted");
    gtk_label_set_xalign(GTK_LABEL(c->status_label), 0.0f);
    gtk_box_append(GTK_BOX(outer), c->status_label);

    return sw;
}

void
np_settings_refresh(NpWindowState *st)
{
    SettingsCtx *c = st->page_ctx[NP_PAGE_SETTINGS];
    if (!c || !st->settings)
        return;
    /* Only push stored values into widgets when the page is first shown or
     * after reset; avoid clobbering in-progress edits on every tick. */
}

/* ------------------------------------------------------------------ about */

GtkWidget *
np_about_page_new(NpWindowState *state)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(outer, 24);
    gtk_widget_set_margin_end(outer, 24);
    gtk_widget_set_margin_top(outer, 28);
    gtk_widget_set_margin_bottom(outer, 20);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), outer);
    gtk_widget_set_hexpand(sw, TRUE);
    gtk_widget_set_vexpand(sw, TRUE);

    /* The bundled mark, not a generic theme icon: the About page is the one
     * place that should show NetPulse's own branding, and a theme that ships
     * none of the symbolic fallbacks would otherwise draw a broken-image
     * placeholder here. */
    GtkWidget *icon = np_app_icon_widget(64);
    gtk_widget_set_halign(icon, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(outer), icon);

    GtkWidget *name = gtk_label_new("NetPulse");
    gtk_widget_add_css_class(name, "np-about-title");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_box_append(GTK_BOX(outer), name);

    GtkWidget *ver = gtk_label_new("Version 0.1.0");
    gtk_widget_add_css_class(ver, "np-muted");
    gtk_label_set_xalign(GTK_LABEL(ver), 0.0f);
    gtk_box_append(GTK_BOX(outer), ver);

    GtkWidget *desc = gtk_label_new(
        "NetPulse is a native Linux desktop application for real-time network "
        "monitoring, connectivity diagnostics, interface inspection, and "
        "traffic visualization. All measurements come from live system "
        "interfaces — no simulated data is ever presented as real.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_box_append(GTK_BOX(outer), desc);

    gtk_box_append(GTK_BOX(outer), np_section_label_new("Technology"));
    GtkWidget *tech = gtk_label_new(
        "• Language: C17\n"
        "• GUI: GTK 4 with GTK CSS themes\n"
        "• Core: GLib / GIO, Cairo charts\n"
        "• Build: CMake + Ninja, GCC\n"
        "• Tests: GLib g_test\n"
        "• System APIs: getifaddrs(), /proc/net/dev, /sys/class/net");
    gtk_label_set_xalign(GTK_LABEL(tech), 0.0f);
    gtk_widget_add_css_class(tech, "np-mono");
    gtk_box_append(GTK_BOX(outer), tech);

    gtk_box_append(GTK_BOX(outer), np_section_label_new("License"));
    GtkWidget *lic = gtk_label_new("MIT License — see the LICENSE file.");
    gtk_label_set_xalign(GTK_LABEL(lic), 0.0f);
    gtk_widget_add_css_class(lic, "np-muted");
    gtk_box_append(GTK_BOX(outer), lic);

    GtkWidget *note = gtk_label_new(
        "Limitations: ICMP ping needs the 'ping' utility and permission to "
        "send ICMP; some virtual interfaces expose no IP addresses; rates are "
        "derived from kernel counters and update at the sampling interval.");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_widget_add_css_class(note, "np-muted");
    gtk_box_append(GTK_BOX(outer), note);

    (void)state;
    return sw;
}

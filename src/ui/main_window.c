#include "netpulse/ui.h"
#include <stdlib.h>

static const char *kPageNames[NP_N_PAGES] = {
    "overview", "interfaces", "connectivity", "traffic", "logs", "settings", "about",
};
static const char *kPageTitles[NP_N_PAGES] = {
    "Overview", "Network Interfaces", "Connectivity Tests", "Traffic Monitor",
    "Diagnostics & Logs", "Settings", "About NetPulse",
};

static void
free_state(NpWindowState *s)
{
    if (!s)
        return;
    if (s->timer_id)
        g_source_remove(s->timer_id);
    g_list_free_full(g_steal_pointer(&s->ifaces),
                     (GDestroyNotify)np_interface_free);
    g_free(s->selected_iface);
    g_free(s->default_iface);
    g_clear_pointer(&s->iface_mon, np_interface_monitor_free);
    g_clear_pointer(&s->traffic, np_traffic_monitor_free);
    g_free(s);
}

static gboolean
tick(gpointer data)
{
    NpWindowState *st = data;
    if (st->paused) {
        np_status_pill_set(st->status_pill, "Paused", "muted");
        return G_SOURCE_CONTINUE;
    }

    GError *err = NULL;
    GList *fresh = np_network_get_interfaces(&err);
    if (!fresh) {
        np_status_pill_set(st->status_pill, "Unknown", "muted");
        np_diagnostics_log(st->diag, NP_SEV_WARNING, "interfaces",
                           "Interface scan failed: %s",
                           err ? err->message : "unknown error");
        g_clear_error(&err);
    } else {
        g_list_free_full(g_steal_pointer(&st->ifaces),
                         (GDestroyNotify)np_interface_free);
        st->ifaces = fresh;
        np_interface_monitor_prune(st->iface_mon, st->ifaces);
        g_free(st->default_iface);
        st->default_iface = np_network_get_default_iface();

        double now = g_get_monotonic_time() / 1000000.0;
        double sel_rx = 0.0, sel_tx = 0.0;
        gboolean have_sel = FALSE;

        /* Pick effective interface: user selection if still present,
         * else default route, else first non-loopback up, else first. */
        const char *want = st->selected_iface;
        NpInterface *sel = NULL;
        if (want) {
            for (GList *l = st->ifaces; l; l = l->next) {
                if (g_strcmp0(((NpInterface *)l->data)->name, want) == 0) {
                    sel = l->data;
                    break;
                }
            }
        }
        if (!sel && st->default_iface) {
            for (GList *l = st->ifaces; l; l = l->next) {
                if (g_strcmp0(((NpInterface *)l->data)->name,
                              st->default_iface) == 0) {
                    sel = l->data;
                    break;
                }
            }
        }
        if (!sel) {
            for (GList *l = st->ifaces; l; l = l->next) {
                NpInterface *i = l->data;
                if (!i->is_loopback && i->is_up) {
                    sel = i;
                    break;
                }
            }
        }
        if (!sel && st->ifaces)
            sel = st->ifaces->data;

        for (GList *l = st->ifaces; l; l = l->next) {
            NpInterface *i = l->data;
            double rx = 0.0, tx = 0.0;
            np_interface_monitor_sample(st->iface_mon, i->name,
                                        i->rx_bytes, i->tx_bytes,
                                        now, &rx, &tx);
            if (sel && i == sel) {
                sel_rx = rx;
                sel_tx = tx;
                have_sel = TRUE;
            }
        }
        if (have_sel) {
            st->last_rx_rate = sel_rx;
            st->last_tx_rate = sel_tx;
            if (sel && g_strcmp0(st->traffic->ifname, sel->name) != 0)
                np_traffic_monitor_set_interface(st->traffic, sel->name);
            np_traffic_monitor_push(st->traffic, now, sel_rx, sel_tx);
        }

        /* Overall status: any non-loopback interface up => Connected. */
        gboolean any_up = FALSE;
        for (GList *l = st->ifaces; l; l = l->next) {
            NpInterface *i = l->data;
            if (!i->is_loopback && i->is_up) {
                any_up = TRUE;
                break;
            }
        }
        st->last_connected = any_up;
        if (any_up)
            np_status_pill_set(st->status_pill, "Connected", "ok");
        else
            np_status_pill_set(st->status_pill, "Disconnected", "err");
    }

    np_window_refresh_all(st);
    return G_SOURCE_CONTINUE;
}

void
np_window_request_refresh(NpWindowState *st)
{
    if (st)
        tick(st);
}

void
np_window_refresh_all(NpWindowState *st)
{
    if (!st)
        return;
    np_overview_refresh(st);
    np_interfaces_refresh(st);
    np_connectivity_refresh(st);
    np_traffic_refresh(st);
    np_logs_refresh(st);
    np_settings_refresh(st);
}

void
np_window_set_paused(NpWindowState *st, gboolean paused)
{
    if (!st)
        return;
    st->paused = paused;
    np_traffic_monitor_set_paused(st->traffic, paused);
    gtk_button_set_label(GTK_BUTTON(st->pause_btn),
                         paused ? "Resume" : "Pause");
    np_diagnostics_log(st->diag, NP_SEV_INFO, "app",
                       paused ? "Monitoring paused" : "Monitoring resumed");
    if (paused)
        np_status_pill_set(st->status_pill, "Paused", "muted");
}

void
np_window_select_page(NpWindowState *st, int index)
{
    if (!st || index < 0 || index >= NP_N_PAGES)
        return;
    if (st->stack && GTK_IS_STACK(st->stack))
        gtk_stack_set_visible_child_name(GTK_STACK(st->stack),
                                         kPageNames[index]);
    if (st->title_label)
        gtk_label_set_text(GTK_LABEL(st->title_label), kPageTitles[index]);
    if (st->sidebar)
        np_sidebar_set_active(st->sidebar, index);
}

static void
on_sidebar_select(int index, gpointer user_data)
{
    np_window_select_page(user_data, index);
}

static void
on_refresh_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    NpWindowState *st = data;
    np_diagnostics_log(st->diag, NP_SEV_INFO, "app", "Manual refresh requested");
    tick(st);
    np_show_toast(st, "Refreshed interface statistics");
}

static void
on_pause_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    NpWindowState *st = data;
    np_window_set_paused(st, !st->paused);
}

static void
on_theme_selected(GtkDropDown *dd, GParamSpec *ps, gpointer data)
{
    (void)ps;
    NpWindowState *st = data;
    guint sel = gtk_drop_down_get_selected(dd);
    NpTheme t = (sel == 1) ? NP_THEME_LIGHT : (sel == 2) ? NP_THEME_DARK : NP_THEME_SYSTEM;
    st->settings->theme = t;

    GtkSettings *gs = gtk_settings_get_default();
    if (gs) {
        /* Only an explicit choice overrides the desktop; "System" must not
         * force light, or it would pin the app to the light palette while the
         * rest of the desktop is dark. */
        if (t == NP_THEME_DARK)
            g_object_set(gs, "gtk-application-prefer-dark-theme", TRUE, NULL);
        else if (t == NP_THEME_LIGHT)
            g_object_set(gs, "gtk-application-prefer-dark-theme", FALSE, NULL);
        /* NP_THEME_SYSTEM: leave the desktop preference in charge. */
    }

    GError *err = NULL;
    if (!np_settings_save(st->settings, &err)) {
        np_show_toast(st, err ? err->message : "Failed to save theme");
        g_clear_error(&err);
    }
}

/* Enable animated sidebar transitions once the window is on screen. */
static void
on_sidebar_map(GtkWidget *sidebar, gpointer data)
{
    (void)sidebar;
    NpWindowState *st = data;
    np_sidebar_set_mapped(st->sidebar, TRUE);
}

static void
on_settings_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    np_window_select_page(data, NP_PAGE_SETTINGS);
}

static void
update_sidebar_toggle_icon(NpWindowState *st)
{
    gboolean collapsed = np_sidebar_get_collapsed(st->sidebar);
    GtkWidget *img = gtk_button_get_child(GTK_BUTTON(st->sidebar_toggle));
    if (!GTK_IS_IMAGE(img))
        return;
    const char *name = collapsed
        ? np_resolve_icon("sidebar-show-symbolic", "view-list-symbolic",
                          "openmenu-symbolic")
        : np_resolve_icon("sidebar-hide-symbolic", "sidebar-show-symbolic",
                          "openmenu-symbolic");
    gtk_image_set_from_icon_name(GTK_IMAGE(img), name);
    gtk_widget_set_tooltip_text(st->sidebar_toggle,
                                collapsed ? "Expand navigation sidebar"
                                          : "Collapse navigation sidebar");
    gtk_accessible_update_property(GTK_ACCESSIBLE(st->sidebar_toggle),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   collapsed ? "Expand navigation sidebar"
                                             : "Collapse navigation sidebar",
                                   -1);
}

static void
on_toggle_sidebar(GtkButton *btn, gpointer data)
{
    (void)btn;
    NpWindowState *st = data;
    /* Presentation-only change: the active page and monitoring are untouched. */
    np_sidebar_toggle(st->sidebar);
    update_sidebar_toggle_icon(st);
    np_diagnostics_log(st->diag, NP_SEV_DEBUG, "app",
                       "Sidebar %s",
                       np_sidebar_get_collapsed(st->sidebar) ? "collapsed"
                                                              : "expanded");
}

static void
on_window_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    NpWindowState *st = data;
    GError *err = NULL;
    if (st->settings && st->selected_iface) {
        g_free(st->settings->selected_iface);
        st->settings->selected_iface = g_strdup(st->selected_iface);
    }
    /* Persist the sidebar state chosen in this session. */
    if (st->settings)
        st->settings->sidebar_collapsed = np_sidebar_get_collapsed(st->sidebar);
    if (st->settings && !np_settings_save(st->settings, &err)) {
        g_warning("Save settings on exit: %s", err ? err->message : "?");
        g_clear_error(&err);
    }
    /* free_state() runs via the window's qdata destroy notify. */
}

GtkWidget *
np_main_window_new(NetPulseApp *app)
{
    NpWindowState *st = g_new0(NpWindowState, 1);
    st->app = app;
    st->settings = netpulse_app_get_settings(app);
    st->diag = netpulse_app_get_diagnostics(app);
    /* -1 = not measured yet; the UI shows "n/a" instead of a fake 0 ms. */
    st->last_latency_ms = -1.0;
    st->iface_mon = np_interface_monitor_new();
    st->traffic = np_traffic_monitor_new(
        (st->settings && st->settings->selected_iface)
            ? st->settings->selected_iface : "",
        st->settings ? st->settings->chart_history_sec : 120.0);
    if (st->settings && st->settings->selected_iface)
        st->selected_iface = g_strdup(st->settings->selected_iface);

    GtkWidget *win = gtk_application_window_new(GTK_APPLICATION(app));
    st->window = win;
    g_object_set_data_full(G_OBJECT(win), "np-state", st, (GDestroyNotify)free_state);
    gtk_window_set_title(GTK_WINDOW(win), "NetPulse — Network Monitoring Dashboard");
    gtk_window_set_default_size(GTK_WINDOW(win), 1440, 900);
    np_set_app_icon();

    /* Header bar */
    GtkWidget *header = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(win), header);

    GtkWidget *title_holder = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *toggle = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(toggle),
                         np_icon("sidebar-show-symbolic", "view-list-symbolic",
                                 "openmenu-symbolic", 16));
    gtk_widget_add_css_class(toggle, "flat");
    gtk_widget_set_tooltip_text(toggle, "Collapse navigation sidebar");
    gtk_accessible_update_property(GTK_ACCESSIBLE(toggle),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Toggle navigation sidebar", -1);
    g_signal_connect(toggle, "clicked", G_CALLBACK(on_toggle_sidebar), st);
    st->sidebar_toggle = toggle;
    gtk_box_append(GTK_BOX(title_holder), toggle);
    st->title_label = gtk_label_new("Overview");
    gtk_widget_add_css_class(st->title_label, "np-header-title");
    gtk_box_append(GTK_BOX(title_holder), st->title_label);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_holder);

    st->status_pill = np_status_pill_new();
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), st->status_pill);
    gtk_widget_set_tooltip_text(st->status_pill, "Overall monitoring status");

    GtkWidget *refresh = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(refresh),
                         np_icon("view-refresh-symbolic", "view-refresh",
                                 "object-revert", 16));
    gtk_widget_set_tooltip_text(refresh, "Refresh now");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_refresh_clicked), st);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), refresh);

    st->pause_btn = gtk_button_new_with_label("Pause");
    gtk_widget_set_tooltip_text(st->pause_btn, "Pause or resume monitoring");
    g_signal_connect(st->pause_btn, "clicked", G_CALLBACK(on_pause_clicked), st);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), st->pause_btn);

    const char *themes[] = { "System", "Light", "Dark", NULL };
    GtkWidget *theme_dd = gtk_drop_down_new_from_strings(themes);
    gtk_widget_set_tooltip_text(theme_dd, "Appearance theme");
    guint initial = 0;
    if (st->settings) {
        if (st->settings->theme == NP_THEME_LIGHT)
            initial = 1;
        else if (st->settings->theme == NP_THEME_DARK)
            initial = 2;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(theme_dd), initial);
    g_signal_connect(theme_dd, "notify::selected",
                     G_CALLBACK(on_theme_selected), st);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), theme_dd);

    GtkWidget *settings_btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(settings_btn),
                         np_icon("emblem-system-symbolic",
                                 "preferences-system-symbolic",
                                 "emblem-preferences-symbolic", 16));
    gtk_widget_set_tooltip_text(settings_btn, "Open Settings");
    g_signal_connect(settings_btn, "clicked",
                     G_CALLBACK(on_settings_clicked), st);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), settings_btn);

    /* Body: sidebar + stack. A mapped handler flips the sidebar's animation
     * gate so the first toggle animates from the real width. */
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), body);

    /* The sidebar applies its persisted state immediately; when the window is
     * mapped we allow animated transitions from then on. */
    st->sidebar = np_sidebar_new(st->settings, on_sidebar_select, st);
    g_signal_connect_after(st->sidebar, "map",
                           G_CALLBACK(on_sidebar_map), st);
    gtk_box_append(GTK_BOX(body), st->sidebar);
    gtk_widget_set_vexpand(st->sidebar, TRUE);
    update_sidebar_toggle_icon(st);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(body), sep);

    /* Overlay for toasts over the stack. */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_box_append(GTK_BOX(body), overlay);

    st->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(st->stack),
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_widget_set_hexpand(st->stack, TRUE);
    gtk_widget_set_vexpand(st->stack, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), st->stack);

    GtkWidget *pages[NP_N_PAGES] = {
        np_overview_page_new(st),
        np_interfaces_page_new(st),
        np_connectivity_page_new(st),
        np_traffic_page_new(st),
        np_logs_page_new(st),
        np_settings_page_new(st),
        np_about_page_new(st),
    };
    for (int i = 0; i < NP_N_PAGES; i++)
        gtk_stack_add_named(GTK_STACK(st->stack), pages[i], kPageNames[i]);

    /* Toast revealer anchored at bottom center. */
    GtkWidget *revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_widget_add_css_class(revealer, "np-toast");
    gtk_widget_set_halign(revealer, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(revealer, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(revealer, 18);
    GtkWidget *toast = gtk_label_new("");
    gtk_revealer_set_child(GTK_REVEALER(revealer), toast);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), revealer);
    st->toast_label = toast;
    st->toast_revealer = revealer;

    g_signal_connect(win, "destroy", G_CALLBACK(on_window_destroy), st);

    /* Initial scan + periodic sampling. */
    int interval = (st->settings ? st->settings->sample_interval_ms : 1000);
    tick(st);
    st->timer_id = g_timeout_add((guint)MAX(interval, 250), tick, st);
    {
        const char *np_page = g_getenv("NETPULSE_PAGE");
        int start_page = (np_page && *np_page) ? atoi(np_page) : NP_PAGE_OVERVIEW;
        np_window_select_page(st, start_page);
    }
    return win;
}

#include "netpulse/ui.h"

typedef struct {
    NpWindowState *st;
    GtkWidget *iface_drop;
    GtkWidget *interval_spin;
    GtkWidget *down_label;
    GtkWidget *up_label;
    GtkWidget *peak_label;
    GtkWidget *chart;
    GtkWidget *pause_btn;
    gboolean   rebuilding; /* guard drop-down rebuild feedback */
} TrafficCtx;

static void
on_iface_chosen(GtkDropDown *dd, GParamSpec *ps, gpointer data)
{
    (void)ps;
    TrafficCtx *c = data;
    if (c->rebuilding)
        return;
    guint sel = gtk_drop_down_get_selected(dd);
    if (!c->st->ifaces)
        return;
    GList *node = g_list_nth(c->st->ifaces, (int)sel);
    if (!node)
        return;
    const char *name = ((NpInterface *)node->data)->name;
    g_free(c->st->selected_iface);
    c->st->selected_iface = g_strdup(name);
    np_traffic_monitor_set_interface(c->st->traffic, name);
    np_diagnostics_log(c->st->diag, NP_SEV_INFO, "traffic",
                       "Traffic monitor switched to %s", name);
}

static void
on_interval_changed(GtkSpinButton *spin, gpointer data)
{
    TrafficCtx *c = data;
    int ms = gtk_spin_button_get_value_as_int(spin);
    if (c->st->settings)
        c->st->settings->sample_interval_ms = ms;
}

static void
on_pause_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    TrafficCtx *c = data;
    np_window_set_paused(c->st, !c->st->paused);
    gtk_button_set_label(GTK_BUTTON(c->pause_btn),
                         c->st->paused ? "Resume" : "Pause");
}

static void
on_clear_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    TrafficCtx *c = data;
    np_traffic_monitor_clear(c->st->traffic);
    np_diagnostics_log(c->st->diag, NP_SEV_INFO, "traffic",
                       "Traffic history cleared");
    np_show_toast(c->st, "Traffic history cleared");
}

GtkWidget *
np_traffic_page_new(NpWindowState *state)
{
    TrafficCtx *c = g_new0(TrafficCtx, 1);
    c->st = state;
    state->page_ctx[NP_PAGE_TRAFFIC] = c;

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(outer, 24);
    gtk_widget_set_margin_end(outer, 24);
    gtk_widget_set_margin_top(outer, 20);
    gtk_widget_set_margin_bottom(outer, 20);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), outer);
    gtk_widget_set_hexpand(sw, TRUE);
    gtk_widget_set_vexpand(sw, TRUE);

    gtk_box_append(GTK_BOX(outer), np_section_label_new("Traffic Monitor"));

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(outer), controls);
    GtkWidget *if_lbl = gtk_label_new("Interface:");
    gtk_box_append(GTK_BOX(controls), if_lbl);
    c->iface_drop = gtk_drop_down_new(NULL, NULL);
    gtk_widget_set_hexpand(c->iface_drop, TRUE);
    g_signal_connect(c->iface_drop, "notify::selected",
                     G_CALLBACK(on_iface_chosen), c);
    gtk_box_append(GTK_BOX(controls), c->iface_drop);

    GtkWidget *in_lbl = gtk_label_new("Sample (ms):");
    gtk_box_append(GTK_BOX(controls), in_lbl);
    c->interval_spin = gtk_spin_button_new_with_range(250, 10000, 250);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->interval_spin),
                              state->settings ? state->settings->sample_interval_ms : 1000);
    g_signal_connect(c->interval_spin, "value-changed",
                     G_CALLBACK(on_interval_changed), c);
    gtk_box_append(GTK_BOX(controls), c->interval_spin);

    c->pause_btn = gtk_button_new_with_label("Pause");
    g_signal_connect(c->pause_btn, "clicked", G_CALLBACK(on_pause_clicked), c);
    gtk_box_append(GTK_BOX(controls), c->pause_btn);
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear");
    gtk_widget_set_tooltip_text(clear_btn, "Clear chart history");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_clicked), c);
    gtk_box_append(GTK_BOX(controls), clear_btn);

    /* Prominent current rates */
    GtkWidget *rates = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_hexpand(rates, TRUE);
    gtk_box_append(GTK_BOX(outer), rates);
    GtkWidget *down_card = np_metric_card_new("Download", "—", "Current inbound");
    GtkWidget *up_card = np_metric_card_new("Upload", "—", "Current outbound");
    gtk_widget_set_hexpand(down_card, TRUE);
    gtk_widget_set_hexpand(up_card, TRUE);
    gtk_box_append(GTK_BOX(rates), down_card);
    gtk_box_append(GTK_BOX(rates), up_card);
    c->down_label = down_card;
    c->up_label = up_card;

    c->chart = np_chart_new();
    gtk_widget_set_size_request(c->chart, -1, 300);
    gtk_box_append(GTK_BOX(outer), c->chart);
    GtkWidget *legend = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(legend),
        "<span foreground='#33b359'>Download (inbound)</span>"
        "        <span foreground='#408cF2'>Upload (outbound)</span>"
        "        Units: B/s, KB/s, MB/s");
    gtk_widget_add_css_class(legend, "np-muted");
    gtk_label_set_xalign(GTK_LABEL(legend), 0.0f);
    gtk_box_append(GTK_BOX(outer), legend);

    c->peak_label = gtk_label_new("Peak: —");
    gtk_widget_add_css_class(c->peak_label, "np-muted");
    gtk_label_set_xalign(GTK_LABEL(c->peak_label), 0.0f);
    gtk_box_append(GTK_BOX(outer), c->peak_label);

    return sw;
}

void
np_traffic_refresh(NpWindowState *st)
{
    TrafficCtx *c = st->page_ctx[NP_PAGE_TRAFFIC];
    if (!c)
        return;

    /* Rebuild interface drop-down when membership changes. */
    c->rebuilding = TRUE;
    GtkStringList *sl = gtk_string_list_new(NULL);
    guint sel_idx = 0;
    guint i = 0;
    for (GList *l = st->ifaces; l; l = l->next, i++) {
        NpInterface *iface = l->data;
        gtk_string_list_append(sl, iface->name);
        if (st->selected_iface && g_strcmp0(st->selected_iface, iface->name) == 0)
            sel_idx = i;
        else if (!st->selected_iface && g_strcmp0(st->traffic->ifname, iface->name) == 0)
            sel_idx = i;
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(c->iface_drop), G_LIST_MODEL(sl));
    g_object_unref(sl);
    if (st->ifaces)
        gtk_drop_down_set_selected(GTK_DROP_DOWN(c->iface_drop), sel_idx);
    c->rebuilding = FALSE;

    g_autofree char *down = np_format_rate(st->last_rx_rate);
    g_autofree char *up = np_format_rate(st->last_tx_rate);
    np_metric_card_set_value(c->down_label, down);
    np_metric_card_set_value(c->up_label, up);

    double prx = 0.0, ptx = 0.0;
    np_traffic_monitor_get_peak(st->traffic, &prx, &ptx);
    g_autofree char *sprx = np_format_rate(prx);
    g_autofree char *sptx = np_format_rate(ptx);
    g_autofree char *peak = g_strdup_printf(
        "Peak download %s   •   Peak upload %s   •   Samples: %u   •   Interface: %s%s",
        sprx, sptx, np_traffic_monitor_get_count(st->traffic),
        st->traffic->ifname, st->paused ? "   •   PAUSED" : "");
    gtk_label_set_text(GTK_LABEL(c->peak_label), peak);

    np_chart_set_series(c->chart, st->traffic->samples,
                        st->settings ? st->settings->chart_history_sec : 120.0);
    gtk_button_set_label(GTK_BUTTON(c->pause_btn),
                         st->paused ? "Resume" : "Pause");
}

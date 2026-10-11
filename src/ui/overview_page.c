#include "netpulse/ui.h"

typedef struct {
    NpWindowState *st;
    GtkWidget *card_status;
    GtkWidget *card_iface;
    GtkWidget *card_down;
    GtkWidget *card_up;
    GtkWidget *card_lat;
    GtkWidget *card_loss;
    GtkWidget *chart;
    GtkWidget *iface_summary;
    GtkWidget *events_list;
    GtkWidget *last_check;
} OverviewCtx;

static GtkWidget *
wrap_scroll(GtkWidget *child)
{
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), child);
    gtk_widget_set_hexpand(sw, TRUE);
    gtk_widget_set_vexpand(sw, TRUE);
    return sw;
}

static GtkWidget *
page_container(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    return box;
}

GtkWidget *
np_overview_page_new(NpWindowState *state)
{
    OverviewCtx *c = g_new0(OverviewCtx, 1);
    c->st = state;
    state->page_ctx[NP_PAGE_OVERVIEW] = c;

    GtkWidget *outer = page_container();
    GtkWidget *sw = wrap_scroll(outer);

    /* Summary cards grid: 3 columns x 2 rows. */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 16);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_box_append(GTK_BOX(outer), grid);

    c->card_status = np_metric_card_new("Connectivity", "Unknown", "Checking…");
    c->card_iface = np_metric_card_new("Active Interface", "—", "Default route");
    c->card_down = np_metric_card_new("Download", "—", "Current rate");
    c->card_up = np_metric_card_new("Upload", "—", "Current rate");
    c->card_lat = np_metric_card_new("Latency", "n/a", "Last successful check");
    c->card_loss = np_metric_card_new("Packet Loss", "n/a", "Not measured by current tests");

    gtk_grid_attach(GTK_GRID(grid), c->card_status, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), c->card_iface, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), c->card_lat, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), c->card_down, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), c->card_up, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), c->card_loss, 2, 1, 1, 1);
    for (int i = 0; i < 6; i++) {
        GtkWidget *ch = gtk_grid_get_child_at(GTK_GRID(grid), i % 3, i / 3);
        gtk_widget_set_hexpand(ch, TRUE);
    }

    /* Live chart */
    GtkWidget *chart_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(chart_frame, "np-card");
    GtkWidget *chart_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(chart_box, 12);
    gtk_widget_set_margin_end(chart_box, 12);
    gtk_widget_set_margin_top(chart_box, 10);
    gtk_widget_set_margin_bottom(chart_box, 10);
    gtk_frame_set_child(GTK_FRAME(chart_frame), chart_box);
    GtkWidget *chart_title = np_section_label_new("Live Network Traffic");
    gtk_box_append(GTK_BOX(chart_box), chart_title);
    c->chart = np_chart_new();
    gtk_widget_set_size_request(c->chart, -1, 220);
    gtk_box_append(GTK_BOX(chart_box), c->chart);
    GtkWidget *legend = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(legend),
        "<span foreground='#33b359'>Download (inbound)</span>"
        "        <span foreground='#408cF2'>Upload (outbound)</span>");
    gtk_widget_add_css_class(legend, "np-muted");
    gtk_label_set_xalign(GTK_LABEL(legend), 0.0f);
    gtk_box_append(GTK_BOX(chart_box), legend);
    gtk_widget_set_hexpand(chart_frame, TRUE);
    gtk_box_append(GTK_BOX(outer), chart_frame);

    /* Two-column bottom row */
    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_hexpand(bottom, TRUE);
    gtk_box_append(GTK_BOX(outer), bottom);

    GtkWidget *iface_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(iface_frame, "np-card");
    gtk_widget_set_hexpand(iface_frame, TRUE);
    GtkWidget *iface_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(iface_box, 12);
    gtk_widget_set_margin_end(iface_box, 12);
    gtk_widget_set_margin_top(iface_box, 10);
    gtk_widget_set_margin_bottom(iface_box, 10);
    gtk_frame_set_child(GTK_FRAME(iface_frame), iface_box);
    gtk_box_append(GTK_BOX(iface_box), np_section_label_new("Network Interfaces"));
    c->iface_summary = gtk_label_new("Scanning…");
    gtk_label_set_xalign(GTK_LABEL(c->iface_summary), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(c->iface_summary), TRUE);
    gtk_widget_add_css_class(c->iface_summary, "np-mono");
    gtk_box_append(GTK_BOX(iface_box), c->iface_summary);
    gtk_box_append(GTK_BOX(bottom), iface_frame);

    GtkWidget *ev_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(ev_frame, "np-card");
    gtk_widget_set_hexpand(ev_frame, TRUE);
    GtkWidget *ev_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(ev_box, 12);
    gtk_widget_set_margin_end(ev_box, 12);
    gtk_widget_set_margin_top(ev_box, 10);
    gtk_widget_set_margin_bottom(ev_box, 10);
    gtk_frame_set_child(GTK_FRAME(ev_frame), ev_box);
    gtk_box_append(GTK_BOX(ev_box), np_section_label_new("Recent Events"));
    c->events_list = gtk_label_new("No events yet.");
    gtk_label_set_xalign(GTK_LABEL(c->events_list), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(c->events_list), TRUE);
    gtk_box_append(GTK_BOX(ev_box), c->events_list);
    c->last_check = gtk_label_new("Last check: never");
    gtk_widget_add_css_class(c->last_check, "np-muted");
    gtk_label_set_xalign(GTK_LABEL(c->last_check), 0.0f);
    gtk_box_append(GTK_BOX(ev_box), c->last_check);
    gtk_box_append(GTK_BOX(bottom), ev_frame);

    return sw;
}

void
np_overview_refresh(NpWindowState *st)
{
    OverviewCtx *c = st->page_ctx[NP_PAGE_OVERVIEW];
    if (!c)
        return;

    if (st->paused) {
        np_metric_card_set_value(c->card_status, "Paused");
        np_metric_card_set_subtitle(c->card_status, "Monitoring paused by user");
        np_metric_card_set_state(c->card_status, NULL);
    } else if (!st->ifaces) {
        np_metric_card_set_value(c->card_status, "Unknown");
        np_metric_card_set_subtitle(c->card_status, "No interface data yet");
        np_metric_card_set_state(c->card_status, NULL);
    } else if (st->last_connected) {
        np_metric_card_set_value(c->card_status, "Connected");
        np_metric_card_set_subtitle(c->card_status, "Local link up");
        np_metric_card_set_state(c->card_status, "ok");
    } else {
        np_metric_card_set_value(c->card_status, "Disconnected");
        np_metric_card_set_subtitle(c->card_status, "No active link detected");
        np_metric_card_set_state(c->card_status, "err");
    }

    /* Active interface */
    const char *active = st->traffic->ifname;
    if (!active || !*active)
        active = st->default_iface ? st->default_iface : "—";
    np_metric_card_set_value(c->card_iface, active);
    np_metric_card_set_subtitle(c->card_iface,
                                st->default_iface ? "Default route" : "Selected interface");

    g_autofree char *down = np_format_rate(st->last_rx_rate);
    g_autofree char *up = np_format_rate(st->last_tx_rate);
    np_metric_card_set_value(c->card_down, down);
    np_metric_card_set_value(c->card_up, up);

    if (st->last_latency_ms >= 0) {
        g_autofree char *lat = np_format_latency(st->last_latency_ms);
        np_metric_card_set_value(c->card_lat, lat);
        np_metric_card_set_subtitle(c->card_lat, "Last successful check");
    } else {
        np_metric_card_set_value(c->card_lat, "n/a");
        np_metric_card_set_subtitle(c->card_lat, "No successful check yet");
    }
    /* Packet loss: only shown when a supporting test measured it; current
     * DNS/TCP/PING checks do not compute loss, so we honestly show n/a. */
    np_metric_card_set_value(c->card_loss, "n/a");

    np_chart_set_series(c->chart, st->traffic->samples,
                        st->settings ? st->settings->chart_history_sec : 120.0);

    /* Interface summary */
    if (!st->ifaces) {
        gtk_label_set_text(GTK_LABEL(c->iface_summary), "No interfaces found.");
    } else {
        GString *s = g_string_new(NULL);
        int shown = 0;
        for (GList *l = st->ifaces; l && shown < 5; l = l->next, shown++) {
            NpInterface *i = l->data;
            g_string_append_printf(s, "%s  %s  %s\n", i->name,
                                   i->is_up ? "up" : "down",
                                   i->ipv4 ? i->ipv4 : (i->ipv6 ? i->ipv6 : "no IP"));
        }
        gtk_label_set_text(GTK_LABEL(c->iface_summary), s->str);
        g_string_free(s, TRUE);
    }

    /* Recent events (last 4). */
    g_autoptr(GPtrArray) rows =
        np_diagnostics_query(st->diag, NP_SEV_DEBUG, NULL, NULL, 0);
    if (!rows || rows->len == 0) {
        gtk_label_set_text(GTK_LABEL(c->events_list), "No events yet.");
    } else {
        GString *s = g_string_new(NULL);
        guint start = rows->len > 4 ? rows->len - 4 : 0;
        for (guint i = start; i < rows->len; i++) {
            NpLogEntry *e = rows->pdata[i];
            g_autofree char *ts = np_format_timestamp(e->timestamp_us);
            g_string_append_printf(s, "%s  [%s] %s\n", ts,
                                   np_severity_to_string(e->severity), e->message);
        }
        gtk_label_set_text(GTK_LABEL(c->events_list), s->str);
        g_string_free(s, TRUE);
    }

    if (st->last_check_us > 0) {
        g_autofree char *ts = np_format_timestamp(st->last_check_us);
        g_autofree char *msg = g_strdup_printf("Last check: %s", ts);
        gtk_label_set_text(GTK_LABEL(c->last_check), msg);
    } else {
        gtk_label_set_text(GTK_LABEL(c->last_check), "Last check: never");
    }
}

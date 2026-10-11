#include "netpulse/ui.h"

typedef struct {
    NpWindowState *st;
    GtkWidget *target_entry;
    GtkWidget *type_drop;
    GtkWidget *port_spin;
    GtkWidget *timeout_spin;
    GtkWidget *start_btn;
    GtkWidget *spinner;
    GtkWidget *results_box; /* rows prepended here */
    GtkWidget *status_label;
    gboolean   running;
} ConnCtx;

static void
on_result_main(NpConnectivityResult *res, gpointer user_data)
{
    ConnCtx *c = user_data;
    c->running = FALSE;
    gtk_widget_set_sensitive(c->start_btn, TRUE);
    gtk_spinner_stop(GTK_SPINNER(c->spinner));

    c->st->last_latency_ms = res->success ? res->latency_ms : c->st->last_latency_ms;
    if (res->success) {
        c->st->last_check_us = res->timestamp_us;
        np_diagnostics_log(c->st->diag, NP_SEV_INFO, "connectivity",
                           "%s %s: %s", np_test_type_to_string(res->type),
                           res->target, res->message);
    } else {
        np_diagnostics_log(c->st->diag, NP_SEV_WARNING, "connectivity",
                           "%s %s failed: %s",
                           np_test_type_to_string(res->type),
                           res->target, res->message);
    }

    g_autofree char *ts = np_format_timestamp(res->timestamp_us);
    g_autofree char *row_text = NULL;
    if (res->type == NP_TEST_TCP) {
        g_autofree char *lat = np_format_latency(res->latency_ms);
        row_text = g_strdup_printf("%s  %s  %s:%d  %s  %s", ts,
                                   np_test_type_to_string(res->type),
                                   res->target, res->port,
                                   res->success ? "OK" : "FAIL",
                                   res->success ? lat : res->message);
    } else {
        g_autofree char *lat = np_format_latency(res->latency_ms);
        row_text = g_strdup_printf("%s  %s  %s  %s  %s", ts,
                                   np_test_type_to_string(res->type),
                                   res->target,
                                   res->success ? "OK" : "FAIL",
                                   res->success ? lat : res->message);
    }
    GtkWidget *row = gtk_label_new(row_text);
    gtk_label_set_xalign(GTK_LABEL(row), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(row), TRUE);
    gtk_widget_add_css_class(row, "np-mono");
    gtk_widget_add_css_class(row, res->success ? "np-row-ok" : "np-row-err");
    gtk_box_prepend(GTK_BOX(c->results_box), row);

    /* Cap visible history to 50 rows. */
    int n = 0;
    for (GtkWidget *ch = gtk_widget_get_first_child(c->results_box);
         ch; ch = gtk_widget_get_next_sibling(ch))
        n++;
    if (n > 50) {
        GtkWidget *last = gtk_widget_get_last_child(c->results_box);
        if (last)
            gtk_box_remove(GTK_BOX(c->results_box), last);
    }

    gtk_label_set_text(GTK_LABEL(c->status_label),
                       res->success ? "Last test: success" : "Last test: failed — see details above");
    np_show_toast(c->st, res->success ? "Connectivity test succeeded"
                                      : "Connectivity test failed");
    np_window_refresh_all(c->st);
}

static void
on_start_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    ConnCtx *c = data;
    if (c->running)
        return;

    const char *target = gtk_editable_get_text(GTK_EDITABLE(c->target_entry));
    guint type_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(c->type_drop));
    NpTestType type = (type_sel == 1) ? NP_TEST_TCP :
                      (type_sel == 2) ? NP_TEST_PING : NP_TEST_DNS;
    int port = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(c->port_spin));
    int timeout = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(c->timeout_spin));

    GError *err = NULL;
    if (!np_validate_hostname(target, &err) ||
        (type == NP_TEST_TCP && !np_validate_port(port, &err)) ||
        !np_validate_timeout_ms(timeout, &err)) {
        gtk_label_set_text(GTK_LABEL(c->status_label), err->message);
        np_diagnostics_log(c->st->diag, NP_SEV_WARNING, "connectivity",
                           "Invalid test input: %s", err->message);
        g_clear_error(&err);
        return;
    }

    c->running = TRUE;
    gtk_widget_set_sensitive(c->start_btn, FALSE);
    gtk_spinner_start(GTK_SPINNER(c->spinner));
    gtk_label_set_text(GTK_LABEL(c->status_label), "Running test…");

    np_connectivity_check_async(target, type, port, timeout,
                                on_result_main, c, NULL);
}

static void
on_type_changed(GtkDropDown *dd, GParamSpec *ps, gpointer data)
{
    (void)ps;
    ConnCtx *c = data;
    guint sel = gtk_drop_down_get_selected(dd);
    gtk_widget_set_sensitive(c->port_spin, sel == 1);
}

GtkWidget *
np_connectivity_page_new(NpWindowState *state)
{
    ConnCtx *c = g_new0(ConnCtx, 1);
    c->st = state;
    state->page_ctx[NP_PAGE_CONNECTIVITY] = c;

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(outer, 24);
    gtk_widget_set_margin_end(outer, 24);
    gtk_widget_set_margin_top(outer, 20);
    gtk_widget_set_margin_bottom(outer, 20);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), outer);
    gtk_widget_set_hexpand(sw, TRUE);
    gtk_widget_set_vexpand(sw, TRUE);

    gtk_box_append(GTK_BOX(outer), np_section_label_new("Connectivity Tests"));

    GtkWidget *form = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(form), 12);
    gtk_grid_set_row_spacing(GTK_GRID(form), 8);
    gtk_box_append(GTK_BOX(outer), form);

    GtkWidget *t_lbl = gtk_label_new("Target host / IP");
    gtk_label_set_xalign(GTK_LABEL(t_lbl), 0.0f);
    gtk_grid_attach(GTK_GRID(form), t_lbl, 0, 0, 1, 1);
    c->target_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(c->target_entry), "e.g. 8.8.8.8 or example.com");
    if (state->settings && state->settings->target_host)
        gtk_editable_set_text(GTK_EDITABLE(c->target_entry),
                              state->settings->target_host);
    gtk_widget_set_hexpand(c->target_entry, TRUE);
    gtk_grid_attach(GTK_GRID(form), c->target_entry, 1, 0, 1, 1);

    GtkWidget *ty_lbl = gtk_label_new("Test type");
    gtk_label_set_xalign(GTK_LABEL(ty_lbl), 0.0f);
    gtk_grid_attach(GTK_GRID(form), ty_lbl, 0, 1, 1, 1);
    c->type_drop = gtk_drop_down_new_from_strings(
        (const char *[]){"DNS resolution", "TCP connection", "ICMP ping", NULL});
    gtk_grid_attach(GTK_GRID(form), c->type_drop, 1, 1, 1, 1);
    g_signal_connect(c->type_drop, "notify::selected",
                     G_CALLBACK(on_type_changed), c);

    GtkWidget *p_lbl = gtk_label_new("Port (TCP only)");
    gtk_label_set_xalign(GTK_LABEL(p_lbl), 0.0f);
    gtk_grid_attach(GTK_GRID(form), p_lbl, 0, 2, 1, 1);
    c->port_spin = gtk_spin_button_new_with_range(1, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->port_spin),
                              state->settings ? state->settings->target_port : 53);
    gtk_widget_set_sensitive(c->port_spin, FALSE);
    gtk_grid_attach(GTK_GRID(form), c->port_spin, 1, 2, 1, 1);

    GtkWidget *to_lbl = gtk_label_new("Timeout (ms)");
    gtk_label_set_xalign(GTK_LABEL(to_lbl), 0.0f);
    gtk_grid_attach(GTK_GRID(form), to_lbl, 0, 3, 1, 1);
    c->timeout_spin = gtk_spin_button_new_with_range(200, 60000, 100);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->timeout_spin),
                              state->settings ? state->settings->timeout_ms : 3000);
    gtk_grid_attach(GTK_GRID(form), c->timeout_spin, 1, 3, 1, 1);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(outer), actions);
    c->start_btn = gtk_button_new_with_label("Start Test");
    gtk_widget_add_css_class(c->start_btn, "suggested-action");
    gtk_widget_set_tooltip_text(c->start_btn, "Run the selected connectivity test");
    g_signal_connect(c->start_btn, "clicked", G_CALLBACK(on_start_clicked), c);
    gtk_box_append(GTK_BOX(actions), c->start_btn);
    c->spinner = gtk_spinner_new();
    gtk_box_append(GTK_BOX(actions), c->spinner);
    c->status_label = gtk_label_new("Idle — no test running.");
    gtk_widget_add_css_class(c->status_label, "np-muted");
    gtk_label_set_xalign(GTK_LABEL(c->status_label), 0.0f);
    gtk_box_append(GTK_BOX(outer), c->status_label);

    gtk_box_append(GTK_BOX(outer), np_section_label_new("Test History"));
    GtkWidget *hist_sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(hist_sw, TRUE);
    gtk_widget_set_size_request(hist_sw, -1, 220);
    c->results_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(c->results_box, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(hist_sw), c->results_box);
    gtk_box_append(GTK_BOX(outer), hist_sw);

    GtkWidget *hint = gtk_label_new(
        "Results are genuine measurements. DNS uses the system resolver, "
        "TCP opens a real socket, and ping requires the 'ping' utility plus "
        "permission to send ICMP.");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_widget_add_css_class(hint, "np-muted");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_append(GTK_BOX(outer), hint);

    return sw;
}

void
np_connectivity_refresh(NpWindowState *st)
{
    (void)st;
    /* History rows are event-driven (async callbacks); nothing periodic. */
}

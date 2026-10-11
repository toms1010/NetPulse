#include "netpulse/ui.h"

typedef struct {
    NpWindowState *st;
    GtkWidget *search_entry;
    GtkWidget *sev_drop;   /* 0 all, 1 info+, 2 warning+, 3 error */
    GtkWidget *cat_entry;
    GtkWidget *range_drop; /* 0 all time, 1 5m, 2 15m, 3 1h, 4 24h */
    GtkWidget *rows_box;
    GtkWidget *count_label;
} LogsCtx;

/* Time-range presets in seconds; 0 means "all time". */
static const gint64 kRangeSeconds[] = { 0, 5 * 60, 15 * 60, 60 * 60, 24 * 60 * 60 };

static NpSeverity
min_sev_of(guint sel)
{
    switch (sel) {
    case 1: return NP_SEV_INFO;
    case 2: return NP_SEV_WARNING;
    case 3: return NP_SEV_ERROR;
    default: return NP_SEV_DEBUG;
    }
}

static gint64
since_us_for(guint sel)
{
    if (sel >= G_N_ELEMENTS(kRangeSeconds))
        return 0;
    gint64 window = kRangeSeconds[sel];
    if (window <= 0)
        return 0; /* all time */
    return g_get_real_time() - window * G_USEC_PER_SEC;
}

static void
rebuild_rows(LogsCtx *c)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(c->rows_box)))
        gtk_box_remove(GTK_BOX(c->rows_box), child);

    const char *needle = gtk_editable_get_text(GTK_EDITABLE(c->search_entry));
    const char *cat = gtk_editable_get_text(GTK_EDITABLE(c->cat_entry));
    guint sev_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(c->sev_drop));
    guint range_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(c->range_drop));

    g_autoptr(GPtrArray) rows = np_diagnostics_query(
        c->st->diag, min_sev_of(sev_sel), (cat && *cat) ? cat : NULL,
        (needle && *needle) ? needle : NULL, since_us_for(range_sel));

    g_autofree char *count = g_strdup_printf("%u event(s)", rows ? rows->len : 0);
    gtk_label_set_text(GTK_LABEL(c->count_label), count);

    if (!rows || rows->len == 0) {
        GtkWidget *empty = gtk_label_new(
            "No log entries match the current filters.");
        gtk_widget_add_css_class(empty, "np-muted");
        gtk_box_append(GTK_BOX(c->rows_box), empty);
        return;
    }
    /* Newest first, cap at 200 visible rows. */
    guint shown = 0;
    for (guint idx = rows->len; idx > 0 && shown < 200; idx--, shown++) {
        NpLogEntry *e = rows->pdata[idx - 1];
        g_autofree char *ts = np_format_timestamp(e->timestamp_us);
        g_autofree char *line = g_strdup_printf(
            "%s  %-7s  %-12s  %s", ts,
            np_severity_to_string(e->severity), e->category, e->message);
        GtkWidget *row = gtk_label_new(line);
        gtk_label_set_xalign(GTK_LABEL(row), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(row), TRUE);
        gtk_widget_add_css_class(row, "np-mono");
        gtk_widget_add_css_class(row, "np-log-row");
        if (e->severity == NP_SEV_ERROR)
            gtk_widget_add_css_class(row, "np-row-err");
        else if (e->severity == NP_SEV_WARNING)
            gtk_widget_add_css_class(row, "np-row-warn");
        gtk_box_append(GTK_BOX(c->rows_box), row);
    }
}

static void
on_filter_changed(GtkSearchEntry *w, gpointer data)
{
    (void)w;
    rebuild_rows(data);
}

static void
on_filter_notify(GObject *w, GParamSpec *ps, gpointer data)
{
    (void)w;
    (void)ps;
    rebuild_rows(data);
}

static void
on_filter_text_changed(GtkEditable *w, gpointer data)
{
    (void)w;
    rebuild_rows(data);
}

static void
on_export_saved(char *path, gpointer u)
{
    LogsCtx *cc = u;
    if (!path)
        return; /* user cancelled */
    GError *err = NULL;
    if (np_export_logs_csv(cc->st->diag, path, &err)) {
        np_show_toast(cc->st, "Logs exported to CSV");
        np_diagnostics_log(cc->st->diag, NP_SEV_INFO, "export",
                           "Logs exported to %s", path);
    } else {
        np_show_toast(cc->st, (err && err->message) ? err->message : "Export failed");
        np_diagnostics_log(cc->st->diag, NP_SEV_ERROR, "export",
                           "Log export failed: %s",
                           (err && err->message) ? err->message : "?");
        g_clear_error(&err);
    }
}

static void
on_clear_confirmed(gboolean confirmed, gpointer u)
{
    LogsCtx *cc = u;
    if (!confirmed)
        return;
    np_diagnostics_clear(cc->st->diag);
    np_diagnostics_log(cc->st->diag, NP_SEV_INFO, "app",
                       "Diagnostic log cleared by user");
    rebuild_rows(cc);
    np_show_toast(cc->st, "Diagnostic logs cleared");
}

static void
on_export_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    LogsCtx *c = data;
    np_show_save_dialog(c->st, "Export logs to CSV", "netpulse-logs.csv",
                        on_export_saved, c);
}

static void
on_clear_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    LogsCtx *c = data;
    np_show_confirm(c->st, "Clear all diagnostic logs? This cannot be undone.",
                    "Clear Logs", on_clear_confirmed, c);
}

GtkWidget *
np_logs_page_new(NpWindowState *state)
{
    LogsCtx *c = g_new0(LogsCtx, 1);
    c->st = state;
    state->page_ctx[NP_PAGE_LOGS] = c;

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(outer, 24);
    gtk_widget_set_margin_end(outer, 24);
    gtk_widget_set_margin_top(outer, 20);
    gtk_widget_set_margin_bottom(outer, 20);

    gtk_box_append(GTK_BOX(outer), np_section_label_new("Diagnostics & Logs"));

    GtkWidget *filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(outer), filters);
    c->search_entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(c->search_entry),
                                          "Search messages…");
    gtk_widget_set_hexpand(c->search_entry, TRUE);
    g_signal_connect(c->search_entry, "search-changed",
                     G_CALLBACK(on_filter_changed), c);
    gtk_box_append(GTK_BOX(filters), c->search_entry);

    c->sev_drop = gtk_drop_down_new_from_strings(
        (const char *[]){"All severities", "Info and above",
                         "Warnings and errors", "Errors only", NULL});
    g_signal_connect(c->sev_drop, "notify::selected",
                     G_CALLBACK(on_filter_notify), c);
    gtk_box_append(GTK_BOX(filters), c->sev_drop);

    c->range_drop = gtk_drop_down_new_from_strings(
        (const char *[]){"All time", "Last 5 minutes", "Last 15 minutes",
                         "Last hour", "Last 24 hours", NULL});
    gtk_widget_set_tooltip_text(c->range_drop, "Filter events by age");
    g_signal_connect(c->range_drop, "notify::selected",
                     G_CALLBACK(on_filter_notify), c);
    gtk_box_append(GTK_BOX(filters), c->range_drop);

    c->cat_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(c->cat_entry), "Category filter…");
    gtk_widget_set_size_request(c->cat_entry, 150, -1);
    g_signal_connect(c->cat_entry, "changed",
                     G_CALLBACK(on_filter_text_changed), c);
    gtk_box_append(GTK_BOX(filters), c->cat_entry);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(outer), actions);
    GtkWidget *export_btn = gtk_button_new_with_label("Export CSV");
    g_signal_connect(export_btn, "clicked", G_CALLBACK(on_export_clicked), c);
    gtk_box_append(GTK_BOX(actions), export_btn);
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear Logs");
    gtk_widget_add_css_class(clear_btn, "destructive-action");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_clicked), c);
    gtk_box_append(GTK_BOX(actions), clear_btn);
    c->count_label = gtk_label_new("");
    gtk_widget_add_css_class(c->count_label, "np-muted");
    gtk_box_append(GTK_BOX(actions), c->count_label);

    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_widget_set_size_request(sw, -1, 300);
    c->rows_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(c->rows_box, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), c->rows_box);
    gtk_box_append(GTK_BOX(outer), sw);

    GtkWidget *wrap = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(wrap), outer);
    gtk_widget_set_hexpand(wrap, TRUE);
    gtk_widget_set_vexpand(wrap, TRUE);
    rebuild_rows(c);
    return wrap;
}

void
np_logs_refresh(NpWindowState *st)
{
    LogsCtx *c = st->page_ctx[NP_PAGE_LOGS];
    if (!c)
        return;
    /* Rebuild only if the page is visible to avoid churn; cheap enough
     * to always rebuild given the 200-row cap. */
    rebuild_rows(c);
}

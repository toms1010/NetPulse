/*
 * NetPulse "Network Interfaces" page.
 *
 * Model chain, outermost first:
 *
 *   GtkSingleSelection -> GtkSortListModel -> GtkFilterListModel -> GListStore
 *
 * GtkColumnView owns only the presentation half. Each sampling tick reconciles
 * the store by interface name (add / update in place / remove) rather than
 * rebuilding it, which is what keeps the sort order, the search query and the
 * selected row stable while counters climb.
 */

#include "netpulse/ui.h"
#include "netpulse/iface_model.h"

typedef struct {
    NpWindowState     *st;
    GListStore        *store;
    NpIfaceFilter     *filter;
    GtkFilterListModel *filter_model;
    GtkSingleSelection *selection;

    GtkWidget         *view;
    GtkWidget         *search;
    GtkWidget         *details;
    GtkWidget         *empty_label;
    GtkWidget         *count_label;

    /* Name of the row the user is inspecting; selection is restored from this
     * after the search filter hides or reveals rows. */
    char              *selected;
    gboolean           syncing;
} IfacesCtx;

/* Columns are appended in NpSortKey order, so the sort key doubles as the
 * column identity for the shared cell factory. */
enum { COL_COUNT = NP_SORT_TX_BYTES + 1 };

static void
update_details(IfacesCtx *c);

/* ------------------------------------------------------------- factories */

/* One factory serves every text column; each cell re-reads its property so
 * in-place row updates repaint without re-binding the model. */
static void
cell_setup(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer data)
{
    (void)factory;
    (void)data;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    /* Inset the cell content. Without it the right-aligned numeric cells in
     * the last column end flush against the frame border and get clipped by
     * its rounded edge. */
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_add_css_class(label, "np-mono");
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_item_set_child(list_item, box);
}

static void
cell_bind(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer data)
{
    (void)factory;
    GtkWidget *box = gtk_list_item_get_child(list_item);
    GtkWidget *label = gtk_widget_get_first_child(box);
    NpIfaceRow *row = gtk_list_item_get_item(list_item);
    gint col = GPOINTER_TO_INT(data);

    /* The cell is recycled, so any state class from a previous row must go. */
    gtk_widget_remove_css_class(label, "np-tbl-ok");
    gtk_widget_remove_css_class(label, "np-tbl-down");

    const char *text = "";
    switch (col) {
    case NP_SORT_NAME:     text = row->name; break;
    case NP_SORT_TYPE:     text = row->type; break;
    case NP_SORT_STATE:    text = row->state; break;
    case NP_SORT_IP:       text = row->ipv4; break;
    case NP_SORT_DOWN:     text = row->rx_rate; break;
    case NP_SORT_UP:       text = row->tx_rate; break;
    case NP_SORT_RX_BYTES: text = row->rx_bytes; break;
    case NP_SORT_TX_BYTES: text = row->tx_bytes; break;
    default: break;
    }

    gtk_label_set_text(GTK_LABEL(label), text ? text : "");
    gtk_label_set_xalign(GTK_LABEL(label), col >= NP_SORT_DOWN ? 1.0f : 0.0f);
    if (col == NP_SORT_DOWN)
        gtk_widget_add_css_class(label, "np-tbl-ok");
    else if (col == NP_SORT_STATE && !row->is_up)
        gtk_widget_add_css_class(label, "np-tbl-down");
}

static GtkColumnViewColumn *
add_column(IfacesCtx *c, const char *title, NpSortKey key, gboolean expand)
{
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(cell_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(cell_bind), GINT_TO_POINTER(key));

    GtkColumnViewColumn *column = gtk_column_view_column_new(title, factory);
    gtk_column_view_column_set_resizable(column, TRUE);
    gtk_column_view_column_set_expand(column, expand);

    /* Attaching a sorter is what makes the column sortable: GtkColumnView owns
     * the header button, the direction toggle and the sort indicator. Reaching
     * into the column for its internal button is not possible anyway --
     * GtkColumnViewColumn is a GObject, not a widget. */
    NpIfaceSorter *sorter = np_iface_sorter_new_for_key(key);
    gtk_column_view_column_set_sorter(column, GTK_SORTER(sorter));
    g_object_unref(sorter);

    gtk_column_view_append_column(GTK_COLUMN_VIEW(c->view), column);
    return column;
}

/* ---------------------------------------------------------------- sorting */

/* Sorting itself is delegated to GtkColumnView via per-column sorters; see
 * add_column(). Nothing here needs to touch column internals. */

/* -------------------------------------------------------------- selection */

static void
on_selection_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    IfacesCtx *c = data;
    (void)pspec;

    if (c->syncing)
        return;

    gpointer item =
        gtk_single_selection_get_selected_item(GTK_SINGLE_SELECTION(obj));
    if (!item || !NP_IS_IFACE_ROW(item)) {
        /* A search query can hide the selected row. Keep the last inspected
         * interface in the details panel rather than blanking it. */
        return;
    }

    NpIfaceRow *row = NP_IFACE_ROW(item);
    g_free(c->selected);
    c->selected = g_strdup(row->name);

    g_free(c->st->selected_iface);
    c->st->selected_iface = g_strdup(row->name);
    np_traffic_monitor_set_interface(c->st->traffic, row->name);

    update_details(c);
}

/* Restore selection by name. With nothing selected yet, default to the
 * interface that carries the default route so the details panel is never blank
 * on first open. */
static void
restore_selection(IfacesCtx *c)
{
    const char *want = c->selected;
    if (!want) {
        want = c->st->selected_iface ? c->st->selected_iface
                                     : c->st->default_iface;
    }

    guint n = g_list_model_get_n_items(G_LIST_MODEL(c->selection));
    for (guint i = 0; i < n; i++) {
        g_autoptr(GObject) item =
            g_list_model_get_item(G_LIST_MODEL(c->selection), i);
        if (!NP_IS_IFACE_ROW(item))
            continue;
        NpIfaceRow *row = NP_IFACE_ROW(item);
        if (want && g_strcmp0(row->name, want) == 0) {
            c->syncing = TRUE;
            gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(c->selection), i);
            c->syncing = FALSE;
            if (!c->selected)
                c->selected = g_strdup(row->name);
            return;
        }
    }
}

/* ----------------------------------------------------------------- search */

static void
on_search_changed(GtkSearchEntry *entry, gpointer data)
{
    IfacesCtx *c = data;
    np_iface_filter_set_query(c->filter, gtk_editable_get_text(GTK_EDITABLE(entry)));
    restore_selection(c);
}

static void
on_refresh_clicked(GtkButton *btn, gpointer data)
{
    IfacesCtx *c = data;
    (void)btn;
    np_window_request_refresh(c->st);
    np_show_toast(c->st, "Interface list refreshed");
}

/* --------------------------------------------------------------- details */

static void
update_details(IfacesCtx *c)
{
    if (!c->details)
        return;

    NpIfaceRow *row = NULL;
    guint n = g_list_model_get_n_items(G_LIST_MODEL(c->selection));
    for (guint i = 0; i < n; i++) {
        g_autoptr(GObject) item =
            g_list_model_get_item(G_LIST_MODEL(c->selection), i);
        if (NP_IS_IFACE_ROW(item) &&
            g_strcmp0(((NpIfaceRow *)item)->name, c->selected) == 0) {
            row = NP_IFACE_ROW(item);
            break;
        }
    }

    if (!row) {
        gtk_label_set_text(GTK_LABEL(c->details),
                           c->selected ? "Selected interface is not currently visible."
                                       : "Select an interface to see its details.");
        return;
    }

    g_autofree char *txt = g_strdup_printf(
        "Name: %s\n"
        "Type: %s\n"
        "State: %s\n"
        "MAC: %s\n"
        "IPv4: %s\n"
        "IPv6: %s\n\n"
        "Received: %s\n"
        "Transmitted: %s\n"
        "Download: %s\n"
        "Upload: %s",
        row->name, row->type, row->state,
        (row->mac && *row->mac) ? row->mac : "\xe2\x80\x94",
        row->ipv4, row->ipv6,
        row->rx_bytes, row->tx_bytes, row->rx_rate, row->tx_rate);
    gtk_label_set_text(GTK_LABEL(c->details), txt);
}

/* ----------------------------------------------------------- reconciliation */

static NpIfaceRow *
find_row(IfacesCtx *c, const char *name)
{
    guint n = g_list_model_get_n_items(G_LIST_MODEL(c->store));
    for (guint i = 0; i < n; i++) {
        g_autoptr(GObject) item = g_list_model_get_item(G_LIST_MODEL(c->store), i);
        if (NP_IS_IFACE_ROW(item) &&
            g_strcmp0(((NpIfaceRow *)item)->name, name) == 0)
            return NP_IFACE_ROW(item);
    }
    return NULL;
}

static void
remove_missing(IfacesCtx *c, GHashTable *seen)
{
    guint n = g_list_model_get_n_items(G_LIST_MODEL(c->store));
    for (guint i = n; i > 0; i--) {
        g_autoptr(GObject) item = g_list_model_get_item(G_LIST_MODEL(c->store), i - 1);
        if (!NP_IS_IFACE_ROW(item))
            continue;
        NpIfaceRow *row = NP_IFACE_ROW(item);
        if (!g_hash_table_contains(seen, row->name))
            g_list_store_remove(c->store, i - 1);
    }
}

static void
sync_store(IfacesCtx *c)
{
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);

    for (GList *l = c->st->ifaces; l; l = l->next) {
        NpInterface *i = l->data;
        if (!i->name)
            continue;
        g_hash_table_add(seen, i->name);

        double rx = -1.0, tx = -1.0;
        if (c->st->iface_mon) {
            NpIfaceSampler *samp =
                g_hash_table_lookup(c->st->iface_mon->samplers, i->name);
            if (samp) {
                rx = samp->rx_rate;
                tx = samp->tx_rate;
            }
        }

        NpIfaceRow *row = find_row(c, i->name);
        if (row) {
            np_iface_row_update(row, i, rx, tx);
        } else {
            row = g_object_new(NP_TYPE_IFACE_ROW, NULL);
            np_iface_row_update(row, i, rx, tx);
            g_list_store_append(c->store, row);
            g_object_unref(row);
        }
    }

    remove_missing(c, seen);
    g_hash_table_destroy(seen);
}

static void
update_empty_state(IfacesCtx *c)
{
    gboolean no_ifaces = (c->st->ifaces == NULL);
    gboolean no_rows = (g_list_model_get_n_items(G_LIST_MODEL(c->selection)) == 0);

    if (no_ifaces) {
        gtk_label_set_text(GTK_LABEL(c->empty_label),
                           "No interfaces found. Check permissions or virtual devices.");
    } else if (no_rows) {
        gtk_label_set_text(GTK_LABEL(c->empty_label),
                           "No interfaces match the current search.");
    } else {
        gtk_label_set_text(GTK_LABEL(c->empty_label), "");
    }

    gtk_widget_set_visible(c->empty_label, no_ifaces || no_rows);

    g_autofree char *count = g_strdup_printf("%u interface%s",
        g_list_model_get_n_items(G_LIST_MODEL(c->store)),
        g_list_model_get_n_items(G_LIST_MODEL(c->store)) == 1 ? "" : "s");
    gtk_label_set_text(GTK_LABEL(c->count_label), count);
}

/* ----------------------------------------------------------------- build */

GtkWidget *
np_interfaces_page_new(NpWindowState *state)
{
    IfacesCtx *c = g_new0(IfacesCtx, 1);
    c->st = state;
    state->page_ctx[NP_PAGE_INTERFACES] = c;

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(outer, 24);
    gtk_widget_set_margin_end(outer, 24);
    gtk_widget_set_margin_top(outer, 20);
    gtk_widget_set_margin_bottom(outer, 20);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), outer);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);

    /* Title row: heading, live count, search field, refresh action. */
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_hexpand(head, TRUE);
    gtk_box_append(GTK_BOX(outer), head);

    GtkWidget *heading = np_section_label_new("Network Interfaces");
    gtk_widget_set_hexpand(heading, TRUE);
    gtk_box_append(GTK_BOX(head), heading);

    c->count_label = gtk_label_new("");
    gtk_widget_add_css_class(c->count_label, "np-muted");
    gtk_box_append(GTK_BOX(head), c->count_label);

    c->search = gtk_search_entry_new();
    gtk_widget_set_size_request(c->search, 260, -1);
    gtk_widget_set_tooltip_text(c->search, "Filter by name, type or address");
    g_signal_connect(c->search, "search-changed", G_CALLBACK(on_search_changed), c);
    gtk_box_append(GTK_BOX(head), c->search);

    GtkWidget *refresh = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(refresh, "Refresh interface list");
    gtk_widget_add_css_class(refresh, "flat");
    gtk_accessible_update_property(GTK_ACCESSIBLE(refresh),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Refresh interface list", -1);
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_refresh_clicked), c);
    gtk_box_append(GTK_BOX(head), refresh);

    c->empty_label = gtk_label_new("");
    gtk_widget_add_css_class(c->empty_label, "np-muted");
    gtk_box_append(GTK_BOX(outer), c->empty_label);

    /* Table. Name expands so it absorbs width at small window sizes while the
     * numeric columns keep their intrinsic width. */
    c->store = g_list_store_new(NP_TYPE_IFACE_ROW);
    c->filter = g_object_new(NP_TYPE_IFACE_FILTER, NULL);
    c->filter_model = gtk_filter_list_model_new(G_LIST_MODEL(c->store),
                                                GTK_FILTER(c->filter));
    c->selection = gtk_single_selection_new(G_LIST_MODEL(c->filter_model));
    gtk_single_selection_set_autoselect(GTK_SINGLE_SELECTION(c->selection), FALSE);
    gtk_single_selection_set_can_unselect(GTK_SINGLE_SELECTION(c->selection), TRUE);
    g_signal_connect(c->selection, "notify::selected-item",
                     G_CALLBACK(on_selection_changed), c);

    c->view = gtk_column_view_new(GTK_SELECTION_MODEL(c->selection));
    gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(c->view), TRUE);
    gtk_widget_add_css_class(c->view, "np-table");
    gtk_widget_set_vexpand(c->view, TRUE);

    /* Name and address absorb the window's slack so the numeric block stays
     * readable and right-aligned at any width; everything else keeps its
     * natural size and long values ellipsize instead of stretching a column. */
    add_column(c, "Interface", NP_SORT_NAME, TRUE);
    add_column(c, "Type", NP_SORT_TYPE, FALSE);
    add_column(c, "State", NP_SORT_STATE, FALSE);
    add_column(c, "IP Address", NP_SORT_IP, TRUE);
    add_column(c, "Download", NP_SORT_DOWN, FALSE);
    add_column(c, "Upload", NP_SORT_UP, FALSE);
    add_column(c, "Received", NP_SORT_RX_BYTES, FALSE);
    add_column(c, "Transmitted", NP_SORT_TX_BYTES, FALSE);

    GtkWidget *table_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(table_frame, "np-card");
    gtk_widget_set_vexpand(table_frame, TRUE);
    gtk_frame_set_child(GTK_FRAME(table_frame), c->view);
    gtk_box_append(GTK_BOX(outer), table_frame);

    /* Detail panel keeps the fields the table has no column for (MAC, IPv6). */
    GtkWidget *details_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(details_frame, "np-card");
    GtkWidget *dbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(dbox, 14);
    gtk_widget_set_margin_end(dbox, 14);
    gtk_widget_set_margin_top(dbox, 12);
    gtk_widget_set_margin_bottom(dbox, 12);
    gtk_frame_set_child(GTK_FRAME(details_frame), dbox);
    gtk_box_append(GTK_BOX(dbox), np_section_label_new("Interface Details"));
    c->details = gtk_label_new("Select an interface to see its details.");
    gtk_label_set_xalign(GTK_LABEL(c->details), 0.0f);
    gtk_label_set_yalign(GTK_LABEL(c->details), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(c->details), TRUE);
    gtk_widget_add_css_class(c->details, "np-mono");
    gtk_box_append(GTK_BOX(dbox), c->details);
    gtk_box_append(GTK_BOX(outer), details_frame);

    return scroller;
}

void
np_interfaces_refresh(NpWindowState *st)
{
    IfacesCtx *c = st->page_ctx[NP_PAGE_INTERFACES];
    if (!c)
        return;

    sync_store(c);
    restore_selection(c);
    update_empty_state(c);
    update_details(c);
}
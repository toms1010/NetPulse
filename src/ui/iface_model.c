/*
 * NetPulse interfaces table model.
 *
 * The page used to be a GtkListBox with two visible columns, which forced the
 * rest of the per-interface data (type, state, IPs, rates, counters) into a
 * text blob in the side panel. Spec section 6 asks for a real table, so this
 * module owns the data side of a GtkColumnView:
 *
 *   NpIfaceRow     one interface, formatted once per tick so cells stay cheap
 *   NpIfaceStore   GListStore of NpIfaceRow, reconciled by name on each tick
 *   filter/sort    GtkFilterListModel + GtkSortListModel drive order and search
 *
 * Rows are updated in place rather than re-added, which is what keeps the
 * user's sort order and row selection alive across the 1 Hz sampling tick.
 */

#include "netpulse/ui.h"
#include "netpulse/iface_model.h"

#include <string.h>

/* ------------------------------------------------------------- row object */

G_DEFINE_FINAL_TYPE(NpIfaceRow, np_iface_row, G_TYPE_OBJECT)

/* Map a plain int comparison onto GtkOrdering. */
static GtkOrdering
ordering_from_cmp(int cmp)
{
    if (cmp == 0)
        return GTK_ORDERING_EQUAL;
    return (cmp < 0) ? GTK_ORDERING_SMALLER : GTK_ORDERING_LARGER;
}

static int
cmp_double(double a, double b)
{
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

static int
cmp_u64(guint64 a, guint64 b)
{
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

enum {
    PROP_0,
    PROP_NAME,
    PROP_TYPE,
    PROP_STATE,
    PROP_IPV4,
    PROP_IPV6,
    PROP_RX_RATE,
    PROP_TX_RATE,
    PROP_RX_BYTES,
    PROP_TX_BYTES,
    PROP_DOWN,
    PROP_UP,
    N_PROP,
};

static GParamSpec *props[N_PROP];

static void
np_iface_row_finalize(GObject *obj)
{
    NpIfaceRow *self = NP_IFACE_ROW(obj);
    g_clear_pointer(&self->name, g_free);
    g_clear_pointer(&self->type, g_free);
    g_clear_pointer(&self->state, g_free);
    g_clear_pointer(&self->ipv4, g_free);
    g_clear_pointer(&self->ipv6, g_free);
    g_clear_pointer(&self->mac, g_free);
    g_clear_pointer(&self->rx_rate, g_free);
    g_clear_pointer(&self->tx_rate, g_free);
    g_clear_pointer(&self->rx_bytes, g_free);
    g_clear_pointer(&self->tx_bytes, g_free);
    g_clear_pointer(&self->search_blob, g_free);
    G_OBJECT_CLASS(np_iface_row_parent_class)->finalize(obj);
}

static void
np_iface_row_get_property(GObject *obj, guint id, GValue *value, GParamSpec *pspec)
{
    NpIfaceRow *self = NP_IFACE_ROW(obj);
    switch (id) {
    case PROP_NAME:     g_value_set_string(value, self->name); break;
    case PROP_TYPE:     g_value_set_string(value, self->type); break;
    case PROP_STATE:    g_value_set_string(value, self->state); break;
    case PROP_IPV4:     g_value_set_string(value, self->ipv4); break;
    case PROP_IPV6:     g_value_set_string(value, self->ipv6); break;
    case PROP_RX_RATE:  g_value_set_string(value, self->rx_rate); break;
    case PROP_TX_RATE:  g_value_set_string(value, self->tx_rate); break;
    case PROP_RX_BYTES: g_value_set_string(value, self->rx_bytes); break;
    case PROP_TX_BYTES: g_value_set_string(value, self->tx_bytes); break;
    case PROP_DOWN:     g_value_set_double(value, self->down); break;
    case PROP_UP:       g_value_set_double(value, self->up); break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
    }
}

static void
np_iface_row_class_init(NpIfaceRowClass *klass)
{
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->finalize = np_iface_row_finalize;
    oc->get_property = np_iface_row_get_property;

    props[PROP_NAME] = g_param_spec_string("name", NULL, NULL, NULL,
                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    props[PROP_TYPE] = g_param_spec_string("type", NULL, NULL, NULL,
                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    props[PROP_STATE] = g_param_spec_string("state", NULL, NULL, NULL,
                                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    props[PROP_IPV4] = g_param_spec_string("ipv4", NULL, NULL, NULL,
                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    props[PROP_IPV6] = g_param_spec_string("ipv6", NULL, NULL, NULL,
                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    props[PROP_RX_RATE] = g_param_spec_string("rx-rate", NULL, NULL, NULL,
                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    props[PROP_TX_RATE] = g_param_spec_string("tx-rate", NULL, NULL, NULL,
                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    props[PROP_RX_BYTES] = g_param_spec_string("rx-bytes", NULL, NULL, NULL,
                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    props[PROP_TX_BYTES] = g_param_spec_string("tx-bytes", NULL, NULL, NULL,
                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    /* Raw numerics back the sorter so ordering never parses formatted text. */
    props[PROP_DOWN] = g_param_spec_double("down", NULL, NULL, 0.0, G_MAXDOUBLE,
                                           0.0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    props[PROP_UP] = g_param_spec_double("up", NULL, NULL, 0.0, G_MAXDOUBLE,
                                         0.0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(oc, N_PROP, props);
}

static void
np_iface_row_init(NpIfaceRow *self)
{
    (void)self;
}

static void
replace_str(char **slot, const char *value)
{
    g_free(*slot);
    *slot = g_strdup(value ? value : "");
}

/**
 * Refresh a row from a fresh scan result.
 *
 * Formatted strings are recomputed here rather than in the cell factories so
 * that sorting and filtering stay cheap and comparisons use real numbers.
 * @rx_rate/@tx_rate come from the sampler; a negative value is rendered "n/a"
 * rather than a fabricated zero.
 */
void
np_iface_row_update(NpIfaceRow *self, const NpInterface *iface,
                    double rx_rate, double tx_rate)
{
    g_return_if_fail(NP_IS_IFACE_ROW(self));
    g_return_if_fail(iface != NULL);

    replace_str(&self->name, iface->name);
    replace_str(&self->type, (iface->type && *iface->type) ? iface->type : "unknown");
    replace_str(&self->state, iface->is_up ? "Up" : "Down");
    replace_str(&self->ipv4, (iface->ipv4 && *iface->ipv4) ? iface->ipv4 : "\xe2\x80\x94");
    replace_str(&self->ipv6, (iface->ipv6 && *iface->ipv6) ? iface->ipv6 : "\xe2\x80\x94");
    replace_str(&self->mac, iface->mac);

    self->is_up = iface->is_up;
    self->is_loopback = iface->is_loopback;

    self->down = rx_rate;
    self->up = tx_rate;
    g_free(self->rx_rate);
    self->rx_rate = np_format_rate(rx_rate);
    g_free(self->tx_rate);
    self->tx_rate = np_format_rate(tx_rate);

    self->rx_bytes_num = iface->rx_bytes;
    self->tx_bytes_num = iface->tx_bytes;
    g_free(self->rx_bytes);
    self->rx_bytes = np_format_bytes(iface->rx_bytes);
    g_free(self->tx_bytes);
    self->tx_bytes = np_format_bytes(iface->tx_bytes);

    /* One lowercase haystack so the search filter needs no per-row folding. */
    g_free(self->search_blob);
    self->search_blob = g_utf8_casefold(
        g_strdup_printf("%s %s %s %s %s", self->name, self->type,
                        self->ipv4, self->ipv6, self->state), -1);

    /* Notify every column so visible cells re-read their values. */
    for (guint i = PROP_NAME; i < N_PROP; i++)
        g_object_notify_by_pspec(G_OBJECT(self), props[i]);
}

/* ---------------------------------------------------------------- sorter */

G_DEFINE_FINAL_TYPE(NpIfaceSorter, np_iface_sorter, GTK_TYPE_SORTER)

static GtkOrdering
np_iface_sorter_compare(GtkSorter *sorter, gpointer item1, gpointer item2)
{
    NpIfaceSorter *self = NP_IFACE_SORTER(sorter);
    NpIfaceRow *ra = item1;
    NpIfaceRow *rb = item2;
    if (!ra || !rb)
        return GTK_ORDERING_EQUAL;

    switch (self->key) {
    case NP_SORT_TYPE:
        return ordering_from_cmp(g_strcmp0(ra->type, rb->type));
    case NP_SORT_STATE:
        /* Up before Down, so operational state reads as the primary signal
         * rather than an alphabetical accident. */
        if (ra->is_up != rb->is_up)
            return ordering_from_cmp(ra->is_up ? -1 : 1);
        return GTK_ORDERING_EQUAL;
    case NP_SORT_IP:
        return ordering_from_cmp(g_strcmp0(ra->ipv4, rb->ipv4));
    case NP_SORT_DOWN:
        return ordering_from_cmp(cmp_double(ra->down, rb->down));
    case NP_SORT_UP:
        return ordering_from_cmp(cmp_double(ra->up, rb->up));
    case NP_SORT_RX_BYTES:
        return ordering_from_cmp(cmp_u64(ra->rx_bytes_num, rb->rx_bytes_num));
    case NP_SORT_TX_BYTES:
        return ordering_from_cmp(cmp_u64(ra->tx_bytes_num, rb->tx_bytes_num));
    case NP_SORT_NAME:
    default:
        return ordering_from_cmp(g_utf8_collate(ra->name, rb->name));
    }
}

static GtkSorterOrder
np_iface_sorter_get_order(GtkSorter *sorter)
{
    (void)sorter;
    return GTK_SORTER_ORDER_TOTAL;
}

static void
np_iface_sorter_class_init(NpIfaceSorterClass *klass)
{
    GTK_SORTER_CLASS(klass)->compare = np_iface_sorter_compare;
    GTK_SORTER_CLASS(klass)->get_order = np_iface_sorter_get_order;
}

static void
np_iface_sorter_init(NpIfaceSorter *self)
{
    self->key = NP_SORT_NAME;
}

/** Sorter bound to one column. */
NpIfaceSorter *
np_iface_sorter_new_for_key(NpSortKey key)
{
    NpIfaceSorter *self = g_object_new(NP_TYPE_IFACE_SORTER, NULL);
    self->key = key;
    return self;
}

NpSortKey
np_iface_sorter_get_key(NpIfaceSorter *self)
{
    return self->key;
}

/* --------------------------------------------------------------- filter */

G_DEFINE_FINAL_TYPE(NpIfaceFilter, np_iface_filter, GTK_TYPE_FILTER)

static gboolean
np_iface_filter_match(GtkFilter *filter, gpointer item)
{
    NpIfaceFilter *self = NP_IFACE_FILTER(filter);
    NpIfaceRow *row = item;
    if (!self->query || !*self->query)
        return TRUE;
    if (!row)
        return FALSE;
    /* Rows carry a pre-folded "name type ip4 ip6 state" haystack, so a
     * keystroke never re-folds every row. */
    return row->search_blob && strstr(row->search_blob, self->query) != NULL;
}

static GtkFilterMatch
np_iface_filter_get_strictness(GtkFilter *filter)
{
    NpIfaceFilter *self = NP_IFACE_FILTER(filter);
    if (!self->query || !*self->query)
        return GTK_FILTER_MATCH_ALL;
    return GTK_FILTER_MATCH_SOME;
}

static void
np_iface_filter_finalize(GObject *obj)
{
    g_clear_pointer(&NP_IFACE_FILTER(obj)->query, g_free);
    G_OBJECT_CLASS(np_iface_filter_parent_class)->finalize(obj);
}

static void
np_iface_filter_class_init(NpIfaceFilterClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = np_iface_filter_finalize;
    GTK_FILTER_CLASS(klass)->match = np_iface_filter_match;
    GTK_FILTER_CLASS(klass)->get_strictness = np_iface_filter_get_strictness;
}

static void
np_iface_filter_init(NpIfaceFilter *self)
{
    (void)self;
}

/** Case-insensitive substring match on interface name, type and addresses. */
void
np_iface_filter_set_query(NpIfaceFilter *self, const char *query)
{
    g_return_if_fail(NP_IS_IFACE_FILTER(self));

    /* g_utf8_casefold does not take ownership of its input. */
    gchar *folded = (query && *query) ? g_utf8_casefold(query, -1) : NULL;
    if (g_strcmp0(folded, self->query) == 0) {
        g_free(folded);
        return; /* unchanged: avoid a pointless refilter on every keystroke */
    }
    g_free(self->query);
    self->query = folded;
    /* DIFFERENT is the honest report here: a different predicate and a
     * different ordering are both in effect, and both need a full re-sort. */
    gtk_filter_changed(GTK_FILTER(self), GTK_FILTER_CHANGE_DIFFERENT);
}
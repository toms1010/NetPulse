/*
 * Interfaces table model tests.
 *
 * These live outside the GLib-only suite because the model is built on
 * GtkSorter/GtkFilter. They cover the parts that are easy to get subtly wrong
 * and that a screenshot cannot confirm: column ordering (numeric vs text,
 * "up before down") and case-insensitive search matching.
 */

#include <gtk/gtk.h>
#include "netpulse/iface_model.h"
#include "netpulse/network.h"

/* Minimal interface fixture; the model only reads these fields. */
static NpInterface
make_iface(const char *name, const char *type, const char *ipv4,
           gboolean is_up, guint64 rx, guint64 tx)
{
    NpInterface i = { 0 };
    i.name = (char *)name;
    i.type = (char *)type;
    i.ipv4 = (char *)ipv4;
    i.ipv6 = NULL;
    i.operstate = (char *)(is_up ? "up" : "down");
    i.is_up = is_up;
    i.is_loopback = (g_strcmp0(name, "lo") == 0);
    i.rx_bytes = rx;
    i.tx_bytes = tx;
    i.rx_packets = 0;
    i.tx_packets = 0;
    return i;
}

static GtkOrdering
cmp(NpIfaceRow *a, NpIfaceRow *b, NpSortKey key)
{
    g_autoptr(NpIfaceSorter) sorter = np_iface_sorter_new_for_key(key);
    return gtk_sorter_compare(GTK_SORTER(sorter), a, b);
}

static void
test_sorter_orders_names(void)
{
    NpInterface ia = make_iface("eth0", "ethernet", "10.0.0.1", TRUE, 0, 0);
    NpInterface ib = make_iface("wlan0", "wifi", "10.0.0.2", TRUE, 0, 0);
    g_autoptr(NpIfaceRow) a = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    g_autoptr(NpIfaceRow) b = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    np_iface_row_update(a, &ia, 0.0, 0.0);
    np_iface_row_update(b, &ib, 0.0, 0.0);

    g_assert_cmpint(cmp(a, b, NP_SORT_NAME), ==, GTK_ORDERING_SMALLER);
    g_assert_cmpint(cmp(b, a, NP_SORT_NAME), ==, GTK_ORDERING_LARGER);
    g_assert_cmpint(cmp(a, a, NP_SORT_NAME), ==, GTK_ORDERING_EQUAL);
}

static void
test_sorter_puts_up_before_down(void)
{
    NpInterface up = make_iface("zzz", "ethernet", "10.0.0.9", TRUE, 0, 0);
    NpInterface down = make_iface("aaa", "ethernet", "10.0.0.1", FALSE, 0, 0);
    g_autoptr(NpIfaceRow) u = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    g_autoptr(NpIfaceRow) d = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    np_iface_row_update(u, &up, 0.0, 0.0);
    np_iface_row_update(d, &down, 0.0, 0.0);

    /* Alphabetically "aaa" < "zzz", but operational state must win. */
    g_assert_cmpint(cmp(u, d, NP_SORT_STATE), ==, GTK_ORDERING_SMALLER);
    g_assert_cmpint(cmp(d, u, NP_SORT_STATE), ==, GTK_ORDERING_LARGER);
}

/* Byte formatting would sort "9 B" above "10 B"; the raw values must not. */
static void
test_sorter_orders_byte_counters_numerically(void)
{
    NpInterface small = make_iface("eth0", "ethernet", "10.0.0.1", TRUE,
                                   9, 0);
    NpInterface big = make_iface("eth1", "ethernet", "10.0.0.2", TRUE,
                                 1024 * 1024, 0);
    g_autoptr(NpIfaceRow) s = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    g_autoptr(NpIfaceRow) b = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    np_iface_row_update(s, &small, 0.0, 0.0);
    np_iface_row_update(b, &big, 0.0, 0.0);

    g_assert_cmpint(cmp(s, b, NP_SORT_RX_BYTES), ==, GTK_ORDERING_SMALLER);
    g_assert_cmpint(cmp(b, s, NP_SORT_RX_BYTES), ==, GTK_ORDERING_LARGER);
}

static void
test_sorter_orders_rates_numerically(void)
{
    NpInterface ia = make_iface("eth0", "ethernet", "10.0.0.1", TRUE, 0, 0);
    NpInterface ib = make_iface("eth1", "ethernet", "10.0.0.2", TRUE, 0, 0);
    g_autoptr(NpIfaceRow) slow = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    g_autoptr(NpIfaceRow) fast = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    np_iface_row_update(slow, &ia, 1500.0, 0.0);
    np_iface_row_update(fast, &ib, 9.0e6, 0.0);

    g_assert_cmpint(cmp(slow, fast, NP_SORT_DOWN), ==, GTK_ORDERING_SMALLER);
    g_assert_cmpint(cmp(fast, slow, NP_SORT_DOWN), ==, GTK_ORDERING_LARGER);
}

/* An unmeasured rate must read as "n/a", never as a fabricated 0 B/s. */
static void
test_unmeasured_rate_is_not_faked(void)
{
    NpInterface i = make_iface("eth0", "ethernet", "10.0.0.1", TRUE, 0, 0);
    g_autoptr(NpIfaceRow) row = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    np_iface_row_update(row, &i, -1.0, -1.0);

    g_assert_cmpstr(row->rx_rate, ==, "n/a");
    g_assert_cmpstr(row->tx_rate, ==, "n/a");
}

static void
test_missing_address_is_an_em_dash(void)
{
    NpInterface i = make_iface("eth0", "ethernet", NULL, TRUE, 0, 0);
    g_autoptr(NpIfaceRow) row = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    np_iface_row_update(row, &i, 0.0, 0.0);

    g_assert_cmpstr(row->ipv4, ==, "\xe2\x80\x94");
    g_assert_cmpstr(row->type, ==, "ethernet");
}

static gboolean
matches(NpIfaceRow *row, const char *query)
{
    g_autoptr(NpIfaceFilter) filter = g_object_new(NP_TYPE_IFACE_FILTER, NULL);
    np_iface_filter_set_query(filter, query);
    return gtk_filter_match(GTK_FILTER(filter), row);
}

static void
test_filter_matches_name_type_and_address(void)
{
    NpInterface i = make_iface("wlp61s0", "wifi", "192.168.254.106", TRUE, 0, 0);
    g_autoptr(NpIfaceRow) row = g_object_new(NP_TYPE_IFACE_ROW, NULL);
    np_iface_row_update(row, &i, 0.0, 0.0);

    g_assert_true(matches(row, "wlp"));
    g_assert_true(matches(row, "WIFI"));      /* case-insensitive */
    g_assert_true(matches(row, "192.168"));   /* address substring */
    g_assert_true(matches(row, ""));          /* empty = show all */
    g_assert_true(matches(row, NULL));
    g_assert_false(matches(row, "eth0"));
}

static void
test_filter_row_updates_search_blob(void)
{
    NpInterface a = make_iface("eth0", "ethernet", "10.0.0.1", TRUE, 0, 0);
    NpInterface b = make_iface("wlan9", "wifi", "10.0.0.9", TRUE, 0, 0);
    g_autoptr(NpIfaceRow) row = g_object_new(NP_TYPE_IFACE_ROW, NULL);

    np_iface_row_update(row, &a, 0.0, 0.0);
    g_assert_true(matches(row, "eth0"));

    /* The same object is reused when an interface's data changes; the search
     * index has to follow, not keep the stale name. */
    np_iface_row_update(row, &b, 0.0, 0.0);
    g_assert_false(matches(row, "eth0"));
    g_assert_true(matches(row, "wlan9"));
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/sorter/name", test_sorter_orders_names);
    g_test_add_func("/sorter/state", test_sorter_puts_up_before_down);
    g_test_add_func("/sorter/bytes", test_sorter_orders_byte_counters_numerically);
    g_test_add_func("/sorter/rates", test_sorter_orders_rates_numerically);
    g_test_add_func("/row/unmeasured-rate", test_unmeasured_rate_is_not_faked);
    g_test_add_func("/row/missing-address", test_missing_address_is_an_em_dash);
    g_test_add_func("/filter/fields", test_filter_matches_name_type_and_address);
    g_test_add_func("/filter/reindex", test_filter_row_updates_search_blob);

    return g_test_run();
}
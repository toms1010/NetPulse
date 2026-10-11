#include <glib.h>
#include "netpulse/network.h"

static void
test_discovery_real(void)
{
    /* Integration: must reflect the real machine. CI runners always have `lo`. */
    GError *e = NULL;
    GList *ifaces = np_network_get_interfaces(&e);
    g_assert_no_error(e);
    g_assert_nonnull(ifaces);

    gboolean found_lo = FALSE;
    for (GList *l = ifaces; l; l = l->next) {
        NpInterface *i = l->data;
        g_assert_nonnull(i->name);
        g_assert_nonnull(i->type);
        if (g_strcmp0(i->name, "lo") == 0) {
            found_lo = TRUE;
            g_assert_true(i->is_loopback);
        }
    }
    g_assert_true(found_lo);
    np_interface_list_free(ifaces);
}

static void
test_missing_iface(void)
{
    GError *e = NULL;
    guint64 rx = 0, tx = 0;
    gboolean ok = np_network_read_counters("np-nonexistent0", &rx, &tx, &e);
    g_assert_false(ok);
    g_assert_nonnull(e);
    np_interface_list_free(NULL);
    g_clear_error(&e);
}

static void
test_classify(void)
{
    g_autofree char *a = np_network_classify_type("lo", TRUE);
    g_assert_cmpstr(a, ==, "loopback");
    g_autofree char *b = np_network_classify_type("wlp61s0", FALSE);
    g_assert_cmpstr(b, ==, "wifi");
    g_autofree char *c = np_network_classify_type("eth0", FALSE);
    g_assert_cmpstr(c, ==, "ethernet");
    g_autofree char *d = np_network_classify_type("docker0", FALSE);
    g_assert_cmpstr(d, ==, "virtual");
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/network/discovery", test_discovery_real);
    g_test_add_func("/network/missing-iface", test_missing_iface);
    g_test_add_func("/network/classify", test_classify);
    return g_test_run();
}

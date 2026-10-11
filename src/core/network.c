#include "netpulse/network.h"
#include "netpulse/error_utils.h"

#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

void
np_interface_free(NpInterface *iface)
{
    if (!iface)
        return;
    g_free(iface->name);
    g_free(iface->type);
    g_free(iface->operstate);
    g_free(iface->ipv4);
    g_free(iface->ipv6);
    g_free(iface->mac);
    g_free(iface);
}

void
np_interface_list_free(GList *list)
{
    g_list_free_full(list, (GDestroyNotify)np_interface_free);
}

static char *
read_sys_file(const char *ifname, const char *leaf)
{
    g_autofree char *path = g_strdup_printf("/sys/class/net/%s/%s", ifname, leaf);
    g_autofree char *contents = NULL;
    if (!g_file_get_contents(path, &contents, NULL, NULL))
        return NULL;
    g_strstrip(contents);
    if (*contents == '\0')
        return NULL;
    return g_steal_pointer(&contents);
}

char *
np_network_classify_type(const char *ifname, gboolean is_loopback)
{
    if (is_loopback || g_strcmp0(ifname, "lo") == 0)
        return g_strdup("loopback");
    if (g_str_has_prefix(ifname, "wl") || g_str_has_prefix(ifname, "wifi") ||
        g_str_has_prefix(ifname, "wlan"))
        return g_strdup("wifi");
    if (g_str_has_prefix(ifname, "en") || g_str_has_prefix(ifname, "eth") ||
        g_str_has_prefix(ifname, "eno") || g_str_has_prefix(ifname, "ens") ||
        g_str_has_prefix(ifname, "enp"))
        return g_strdup("ethernet");
    if (g_str_has_prefix(ifname, "br-") || g_str_has_prefix(ifname, "br") ||
        g_str_has_prefix(ifname, "docker") || g_str_has_prefix(ifname, "virbr") ||
        g_str_has_prefix(ifname, "veth"))
        return g_strdup("virtual");
    if (g_str_has_prefix(ifname, "tun") || g_str_has_prefix(ifname, "tap") ||
        g_str_has_prefix(ifname, "wg") || g_str_has_prefix(ifname, "ppp"))
        return g_strdup("tunnel");
    return g_strdup("unknown");
}

gboolean
np_network_read_counters(const char *ifname,
                         guint64 *rx_bytes, guint64 *tx_bytes,
                         GError **error)
{
    g_return_val_if_fail(ifname != NULL, FALSE);
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) {
        np_set_error(error, NP_ERR_IO, "Cannot open /proc/net/dev: %s",
                     g_strerror(errno));
        return FALSE;
    }
    char line[512];
    /* Skip two header lines. */
    if (!fgets(line, sizeof line, f) || !fgets(line, sizeof line, f)) {
        fclose(f);
        np_set_error(error, NP_ERR_IO, "Unexpected /proc/net/dev header");
        return FALSE;
    }
    gboolean found = FALSE;
    guint64 rx = 0, tx = 0;
    while (fgets(line, sizeof line, f)) {
        char *colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = '\0';
        char *name = g_strstrip(line);
        if (g_strcmp0(name, ifname) != 0)
            continue;
        /* /proc/net/dev columns: rx bytes packets errs ... | tx bytes packets ... */
        unsigned long long r = 0, t = 0;
        if (sscanf(colon + 1, "%llu %*u %*u %*u %*u %*u %*u %*u %llu",
                   &r, &t) == 2) {
            rx = (guint64)r;
            tx = (guint64)t;
            found = TRUE;
        }
        break;
    }
    fclose(f);
    if (!found) {
        np_set_error(error, NP_ERR_NO_INTERFACE,
                     "Interface '%s' not found in /proc/net/dev", ifname);
        return FALSE;
    }
    if (rx_bytes)
        *rx_bytes = rx;
    if (tx_bytes)
        *tx_bytes = tx;
    return TRUE;
}

char *
np_network_get_default_iface(void)
{
    FILE *f = fopen("/proc/net/route", "r");
    if (!f)
        return NULL;
    char line[512];
    if (!fgets(line, sizeof line, f)) {
        fclose(f);
        return NULL;
    }
    char *result = NULL;
    while (fgets(line, sizeof line, f)) {
        char ifname[64] = { 0 };
        char dest[16] = { 0 };
        if (sscanf(line, "%63s %15s", ifname, dest) == 2 &&
            g_strcmp0(dest, "00000000") == 0) {
            result = g_strdup(ifname);
            break;
        }
    }
    fclose(f);
    return result;
}

static int
np_iface_name_cmp(const NpInterface *a, const NpInterface *b)
{
    return g_strcmp0(a->name, b->name);
}

GList *
np_network_get_interfaces(GError **error)
{
    struct ifaddrs *addrs = NULL;
    if (getifaddrs(&addrs) != 0) {
        np_set_error(error, NP_ERR_IO, "getifaddrs() failed: %s",
                     g_strerror(errno));
        return NULL;
    }

    GHashTable *by_name = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                g_free, NULL);

    for (struct ifaddrs *a = addrs; a != NULL; a = a->ifa_next) {
        if (!a->ifa_name || !a->ifa_addr)
            continue;
        NpInterface *iface = g_hash_table_lookup(by_name, a->ifa_name);
        if (!iface) {
            iface = g_new0(NpInterface, 1);
            iface->name = g_strdup(a->ifa_name);
            iface->is_loopback = (a->ifa_flags & IFF_LOOPBACK) != 0;
            iface->is_up = (a->ifa_flags & IFF_UP) != 0;
            iface->type = np_network_classify_type(a->ifa_name, iface->is_loopback);
            iface->operstate = read_sys_file(a->ifa_name, "operstate");
            iface->mac = read_sys_file(a->ifa_name, "address");
            g_hash_table_insert(by_name, g_strdup(a->ifa_name), iface);
        } else {
            if (a->ifa_flags & IFF_UP)
                iface->is_up = TRUE;
        }

        int family = a->ifa_addr->sa_family;
        char buf[INET6_ADDRSTRLEN] = { 0 };
        if (family == AF_INET) {
            struct sockaddr_in *s4 = (struct sockaddr_in *)a->ifa_addr;
            if (inet_ntop(AF_INET, &s4->sin_addr, buf, sizeof buf) && !iface->ipv4) {
                /* Skip link-local 169.254.x.x unless nothing else exists. */
                if (!g_str_has_prefix(buf, "169.254."))
                    iface->ipv4 = g_strdup(buf);
                else if (!iface->ipv4)
                    iface->ipv4 = g_strdup(buf);
            }
        } else if (family == AF_INET6) {
            struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)a->ifa_addr;
            if (inet_ntop(AF_INET6, &s6->sin6_addr, buf, sizeof buf)) {
                if (g_str_has_prefix(buf, "fe80:") || g_strcmp0(buf, "::1") == 0)
                    continue; /* skip link-local/loopback for display */
                if (!iface->ipv6)
                    iface->ipv6 = g_strdup(buf);
            }
        }
    }
    freeifaddrs(addrs);

    /* Attach counters from /proc/net/dev. Interfaces missing there keep
     * counters_valid = FALSE (e.g. transient devices). */
    GList *result = NULL;
    GHashTableIter iter;
    gpointer k, v;
    g_hash_table_iter_init(&iter, by_name);
    while (g_hash_table_iter_next(&iter, &k, &v)) {
        NpInterface *iface = v;
        guint64 rx = 0, tx = 0;
        /* Packet counters best-effort: parse full line. */
        FILE *f = fopen("/proc/net/dev", "r");
        if (f) {
            char line[512];
            /* Skip the two header lines; a short file simply yields no match. */
            if (fgets(line, sizeof line, f) && fgets(line, sizeof line, f)) {
                while (fgets(line, sizeof line, f)) {
                    char *colon = strchr(line, ':');
                    if (!colon)
                        continue;
                    *colon = '\0';
                    if (g_strcmp0(g_strstrip(line), iface->name) != 0)
                        continue;
                    unsigned long long rb = 0, rp = 0, tb = 0, tp = 0;
                    if (sscanf(colon + 1,
                               "%llu %llu %*u %*u %*u %*u %*u %*u %llu %llu",
                               &rb, &rp, &tb, &tp) >= 4) {
                        rx = (guint64)rb;
                        tx = (guint64)tb;
                        iface->rx_packets = (guint64)rp;
                        iface->tx_packets = (guint64)tp;
                        iface->counters_valid = TRUE;
                    }
                    break;
                }
            }
            fclose(f);
        }
        iface->rx_bytes = rx;
        iface->tx_bytes = tx;
        /* Operstate refines is_up when sysfs agrees. */
        if (iface->operstate && g_strcmp0(iface->operstate, "up") == 0)
            iface->is_up = TRUE;
        else if (iface->operstate &&
                 (g_strcmp0(iface->operstate, "down") == 0 ||
                  g_strcmp0(iface->operstate, "dormant") == 0)) {
            /* keep IFF flag; dormant (wifi powersave) still counts as up */
        }
        result = g_list_prepend(result, iface);
    }
    g_hash_table_destroy(by_name);

    result = g_list_sort(result, (GCompareFunc)np_iface_name_cmp);

    if (!result)
        np_set_error(error, NP_ERR_NO_INTERFACE, "No network interfaces found");
    return result;
}

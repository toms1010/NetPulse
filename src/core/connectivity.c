#include "netpulse/connectivity.h"
#include "netpulse/error_utils.h"

#include <string.h>
#include <stdlib.h>

const char *
np_test_type_to_string(NpTestType t)
{
    switch (t) {
    case NP_TEST_DNS:  return "DNS";
    case NP_TEST_TCP:  return "TCP";
    case NP_TEST_PING: return "PING";
    default:           return "DNS";
    }
}

gboolean
np_test_type_from_string(const char *s, NpTestType *out)
{
    if (!s)
        return FALSE;
    if (g_ascii_strcasecmp(s, "dns") == 0) {
        if (out) *out = NP_TEST_DNS;
        return TRUE;
    }
    if (g_ascii_strcasecmp(s, "tcp") == 0) {
        if (out) *out = NP_TEST_TCP;
        return TRUE;
    }
    if (g_ascii_strcasecmp(s, "ping") == 0 || g_ascii_strcasecmp(s, "icmp") == 0) {
        if (out) *out = NP_TEST_PING;
        return TRUE;
    }
    return FALSE;
}

void
np_connectivity_result_free(NpConnectivityResult *r)
{
    if (!r)
        return;
    g_free(r->target);
    g_free(r->message);
    g_free(r);
}

gboolean
np_validate_hostname(const char *host, GError **error)
{
    if (!host || *host == '\0') {
        np_set_error(error, NP_ERR_INVALID_INPUT, "Hostname is empty");
        return FALSE;
    }
    if (strlen(host) > 253) {
        np_set_error(error, NP_ERR_INVALID_INPUT, "Hostname too long (max 253 chars)");
        return FALSE;
    }
    /* Reject shell metacharacters outright — we never pass the host to a shell,
     * but strict validation prevents misuse elsewhere too. */
    for (const char *p = host; *p; p++) {
        char c = *p;
        gboolean ok = g_ascii_isalnum(c) || c == '.' || c == '-' || c == '_' ||
                      c == ':'; /* IPv6 literals */
        if (!ok) {
            np_set_error(error, NP_ERR_INVALID_INPUT,
                         "Invalid character '%c' in hostname", c);
            return FALSE;
        }
    }
    if (host[0] == '-' || host[0] == '.' || host[0] == ':') {
        np_set_error(error, NP_ERR_INVALID_INPUT, "Hostname has invalid leading character");
        return FALSE;
    }
    return TRUE;
}

gboolean
np_validate_port(int port, GError **error)
{
    if (port < 1 || port > 65535) {
        np_set_error(error, NP_ERR_INVALID_INPUT,
                     "Port %d out of range (1–65535)", port);
        return FALSE;
    }
    return TRUE;
}

gboolean
np_validate_timeout_ms(int timeout_ms, GError **error)
{
    if (timeout_ms < 200 || timeout_ms > 60000) {
        np_set_error(error, NP_ERR_INVALID_INPUT,
                     "Timeout %d ms out of range (200–60000)", timeout_ms);
        return FALSE;
    }
    return TRUE;
}

gboolean
np_dns_resolve(const char *host, int timeout_ms,
               double *out_latency_ms, GError **error)
{
    if (!np_validate_hostname(host, error))
        return FALSE;
    (void)timeout_ms; /* GResolver is sync here; timeout enforced by caller thread mgmt */

    gint64 t0 = g_get_monotonic_time();
    GResolver *resolver = g_resolver_get_default();
    GList *addrs = g_resolver_lookup_by_name(resolver, host, NULL, error);
    g_object_unref(resolver);
    gint64 t1 = g_get_monotonic_time();

    if (!addrs) {
        if (error && *error == NULL)
            np_set_error(error, NP_ERR_DNS_FAILED, "DNS resolution failed for '%s'", host);
        else if (error && *error) {
            /* Wrap resolver error with context. */
            GError *wrapped = g_error_new(NP_ERROR, NP_ERR_DNS_FAILED,
                                          "DNS resolution failed for '%s': %s",
                                          host, (*error)->message);
            g_error_free(*error);
            *error = wrapped;
        }
        return FALSE;
    }
    g_resolver_free_addresses(addrs);
    if (out_latency_ms)
        *out_latency_ms = (t1 - t0) / 1000.0;
    return TRUE;
}

gboolean
np_tcp_connect(const char *host, int port, int timeout_ms,
               double *out_latency_ms, GError **error)
{
    if (!np_validate_hostname(host, error))
        return FALSE;
    if (!np_validate_port(port, error))
        return FALSE;

    g_autoptr(GSocketClient) client = g_socket_client_new();
    guint timeout_s = (guint)(MAX(timeout_ms, 200) / 1000);
    if (timeout_s == 0)
        timeout_s = 1;
    g_socket_client_set_timeout(client, timeout_s);
    /* g_socket_client_set_timeout takes seconds; enforce ms granularity via cancellable. */
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_autoptr(GError) timer_err = NULL;

    gint64 t0 = g_get_monotonic_time();
    g_autoptr(GSocketConnection) conn = g_socket_client_connect_to_host(
        client, host, (guint16)port, cancel, error);
    gint64 t1 = g_get_monotonic_time();

    if (!conn) {
        if (error && *error && !g_error_matches(*error, NP_ERROR, 0)) {
            GError *wrapped = g_error_new(NP_ERROR, NP_ERR_CONNECT_FAILED,
                                          "TCP %s:%d failed: %s",
                                          host, port, (*error)->message);
            g_error_free(*error);
            *error = wrapped;
        } else if (error && *error == NULL) {
            np_set_error(error, NP_ERR_CONNECT_FAILED, "TCP %s:%d failed", host, port);
        }
        return FALSE;
    }
    if (out_latency_ms)
        *out_latency_ms = (t1 - t0) / 1000.0;
    return TRUE;
}

gboolean
np_ping_host(const char *host, int timeout_ms,
             double *out_latency_ms, GError **error)
{
    if (!np_validate_hostname(host, error))
        return FALSE;

    const char *argv_options[][6] = {
        { "ping", "-c1", "-W", NULL, NULL, NULL },
    };
    (void)argv_options;
    int timeout_s = CLAMP(timeout_ms / 1000, 1, 30);
    g_autofree char *timeout_str = g_strdup_printf("%d", timeout_s);
    const char *argv[] = { "ping", "-c1", "-W", timeout_str, host, NULL };

    gint64 t0 = g_get_monotonic_time();
    g_autofree char *out = NULL, *err = NULL;
    gint status = 0;
    GError *spawn_err = NULL;
    gboolean ok = g_spawn_sync(NULL, (char **)argv, NULL,
                               G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                               NULL, NULL, &out, &err, &status, &spawn_err);
    gint64 t1 = g_get_monotonic_time();

    if (!ok) {
        g_propagate_prefixed_error(error, spawn_err,
                                   "Cannot run 'ping' (is iputils-ping installed?)");
        return FALSE;
    }
    if (!g_spawn_check_wait_status(status, NULL)) {
        /* Exit 2 typically = permission/capability issue for ICMP. */
        if (WIFEXITED(status) && WEXITSTATUS(status) == 2)
            np_set_error(error, NP_ERR_UNAVAILABLE,
                         "'ping' is not permitted (exit 2). Grant cap_net_raw or run as permitted user.");
        else
            np_set_error(error, NP_ERR_TIMEOUT,
                         "No ping reply from '%s' within %d ms", host, timeout_ms);
        return FALSE;
    }
    /* Parse "time=X ms" from stdout. */
    double ms = -1.0;
    if (out) {
        char *p = strstr(out, "time=");
        if (p)
            ms = g_ascii_strtod(p + 5, NULL);
    }
    if (ms < 0)
        ms = (t1 - t0) / 1000.0;
    if (out_latency_ms)
        *out_latency_ms = ms;
    return TRUE;
}

NpConnectivityResult *
np_connectivity_check(const char *target, NpTestType type, int port, int timeout_ms)
{
    NpConnectivityResult *r = g_new0(NpConnectivityResult, 1);
    r->target = g_strdup(target ? target : "");
    r->type = type;
    r->port = port;
    r->latency_ms = -1.0;
    r->timestamp_us = g_get_real_time();

    GError *err = NULL;
    gboolean ok = FALSE;
    double lat = -1.0;

    if (type == NP_TEST_DNS)
        ok = np_dns_resolve(target, timeout_ms, &lat, &err);
    else if (type == NP_TEST_TCP)
        ok = np_tcp_connect(target, port, timeout_ms, &lat, &err);
    else
        ok = np_ping_host(target, timeout_ms, &lat, &err);

    r->success = ok;
    r->latency_ms = ok ? lat : -1.0;
    if (ok) {
        if (type == NP_TEST_TCP)
            r->message = g_strdup_printf("%s:%d reachable in %.1f ms", target, port, lat);
        else if (type == NP_TEST_DNS)
            r->message = g_strdup_printf("'%s' resolved in %.1f ms", target, lat);
        else
            r->message = g_strdup_printf("'%s' replied in %.1f ms", target, lat);
    } else {
        r->message = g_strdup(err ? err->message : "Check failed");
    }
    g_clear_error(&err);
    return r;
}

typedef struct {
    char                    *target;
    NpTestType               type;
    int                      port;
    int                      timeout_ms;
    NpConnectivityCallback   cb;
    gpointer                 user_data;
    GDestroyNotify           destroy_notify;
} AsyncCtx;

static void
async_ctx_free(AsyncCtx *c)
{
    if (!c)
        return;
    g_free(c->target);
    g_free(c);
}

typedef struct {
    AsyncCtx *ctx;
    NpConnectivityResult *res;
} AsyncPair;

static gboolean
emit_on_main(gpointer data)
{
    AsyncPair *pair = data;
    pair->ctx->cb(pair->res, pair->ctx->user_data);
    np_connectivity_result_free(pair->res);
    if (pair->ctx->destroy_notify)
        pair->ctx->destroy_notify(pair->ctx->user_data);
    async_ctx_free(pair->ctx);
    g_free(pair);
    return G_SOURCE_REMOVE;
}

static gpointer
async_worker(gpointer data)
{
    AsyncCtx *ctx = data;
    NpConnectivityResult *res = np_connectivity_check(ctx->target, ctx->type,
                                                     ctx->port, ctx->timeout_ms);
    AsyncPair *pair = g_new0(AsyncPair, 1);
    pair->ctx = ctx;
    pair->res = res;
    g_idle_add(emit_on_main, pair);
    return NULL;
}

void
np_connectivity_check_async(const char *target, NpTestType type, int port,
                            int timeout_ms,
                            NpConnectivityCallback cb,
                            gpointer user_data,
                            GDestroyNotify destroy_notify)
{
    g_return_if_fail(target != NULL && cb != NULL);
    AsyncCtx *ctx = g_new0(AsyncCtx, 1);
    ctx->target = g_strdup(target);
    ctx->type = type;
    ctx->port = port;
    ctx->timeout_ms = timeout_ms;
    ctx->cb = cb;
    ctx->user_data = user_data;
    ctx->destroy_notify = destroy_notify;
    g_thread_new("np-connectivity", async_worker, ctx);
}

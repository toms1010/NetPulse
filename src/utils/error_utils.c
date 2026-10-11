#include "netpulse/error_utils.h"
#include <stdarg.h>

GQuark
np_error_quark(void)
{
    return g_quark_from_static_string("netpulse-error");
}

void
np_set_error(GError **error, NpErrorCode code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    g_set_error_literal(error, NP_ERROR, code, msg);
    g_free(msg);
}

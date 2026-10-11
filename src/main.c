#include "netpulse/app.h"

int
main(int argc, char **argv)
{
    g_autoptr(NetPulseApp) app = netpulse_app_new();
    return g_application_run(G_APPLICATION(app), argc, argv);
}

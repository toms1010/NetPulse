#include "netpulse/app.h"
#include "netpulse/ui.h"
#include "netpulse/settings.h"
#include "netpulse/diagnostics.h"

struct _NetPulseApp {
    GtkApplication  parent;
    NpSettings     *settings;
    NpDiagnostics  *diag;
};

G_DEFINE_TYPE(NetPulseApp, netpulse_app, GTK_TYPE_APPLICATION)

static void
netpulse_app_dispose(GObject *obj)
{
    NetPulseApp *self = NETPULSE_APP(obj);
    if (self->settings) {
        np_settings_clear(self->settings);
        g_free(self->settings);
        self->settings = NULL;
    }
    g_clear_pointer(&self->diag, np_diagnostics_free);
    G_OBJECT_CLASS(netpulse_app_parent_class)->dispose(obj);
}

static void
apply_theme(NetPulseApp *self)
{
    NpTheme t = self->settings ? self->settings->theme : NP_THEME_SYSTEM;

    /* Only an explicit choice touches the GTK setting. "System" must leave it
     * alone so the desktop's own light/dark preference keeps working. */
    if (t == NP_THEME_LIGHT || t == NP_THEME_DARK) {
        GtkSettings *gs = gtk_settings_get_default();
        if (gs)
            g_object_set(gs, "gtk-application-prefer-dark-theme",
                         (t == NP_THEME_DARK) ? TRUE : FALSE, NULL);
    }
}

#ifdef HAVE_NETPULSE_RESOURCES
/* Emitted by glib-compile-resources; only exists when the bundle is built. */
extern GResource *netpulse_get_resource(void);
#endif

static void
on_css_parsing_error(GtkCssProvider *provider, GtkCssSection *section,
                     GError *error, gpointer data)
{
    (void)provider;
    (void)section;
    (void)data;
    g_warning("stylesheet: %s", error ? error->message : "unknown parse error");
}

/*
 * Load the single stylesheet.
 *
 * Theming is handled entirely by GTK's theme-aware named colours plus
 * gtk-application-prefer-dark-theme (see apply_theme). There is no second
 * "dark.css"/"light.css" override sheet: GTK resolves @define-color when a
 * sheet is parsed, so a later sheet re-declaring a colour name invalidates the
 * references the first sheet already resolved and silently drops those rules.
 * The cards visibly lost their borders when the variant sheets were layered on
 * top, so they are gone rather than kept as dead weight.
 *
 * Preferred source is the compiled-in bundle, so an installed binary styles
 * itself regardless of the working directory.
 */
static void
load_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    gboolean loaded = FALSE;

    /* gtk_css_provider_load_from_data has no error out-parameter; problems
     * surface through the parsing-error signal instead. */
    g_signal_connect(provider, "parsing-error",
                     G_CALLBACK(on_css_parsing_error), NULL);

#ifdef HAVE_NETPULSE_RESOURCES
    g_resources_register(netpulse_get_resource());
    {
        g_autoptr(GBytes) bytes = g_resources_lookup_data(
            "/org/netpulse/Monitor/styles/main.css",
            G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
        if (bytes) {
            gsize size = 0;
            const char *data = g_bytes_get_data(bytes, &size);
            g_autofree char *text = g_strndup(data, size);
            gtk_css_provider_load_from_string(provider, text);
            loaded = TRUE;
        }
    }
#endif

    /* Developer fallback: run from a source checkout. */
    if (!loaded) {
        static const char *candidates[] = {
            "resources/styles/main.css",
            "../resources/styles/main.css",
            "/usr/share/netpulse/styles/main.css",
            NULL,
        };
        for (int i = 0; candidates[i] && !loaded; i++) {
            if (!g_file_test(candidates[i], G_FILE_TEST_EXISTS))
                continue;
            gtk_css_provider_load_from_path(provider, candidates[i]);
            loaded = TRUE;
        }
    }

    if (loaded) {
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    } else {
        g_message("No stylesheet found; running with the default GTK theme");
    }
    g_object_unref(provider);
}

static void
netpulse_app_activate(GApplication *gapp)
{
    NetPulseApp *self = NETPULSE_APP(gapp);
    apply_theme(self);
    load_css();
    GtkWidget *win = np_main_window_new(self);
    gtk_window_present(GTK_WINDOW(win));
}

static void
netpulse_app_startup(GApplication *gapp)
{
    G_APPLICATION_CLASS(netpulse_app_parent_class)->startup(gapp);

    NetPulseApp *self = NETPULSE_APP(gapp);
    GError *err = NULL;
    self->settings = np_settings_load(&err);
    if (err) {
        g_warning("Settings load: %s", err->message);
        g_clear_error(&err);
    }
    self->diag = np_diagnostics_new();
    np_diagnostics_log(self->diag, NP_SEV_INFO, "app",
                       "NetPulse started (v%s)", "0.1.0");
}

static void
netpulse_app_class_init(NetPulseAppClass *klass)
{
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->dispose = netpulse_app_dispose;
    GApplicationClass *ac = G_APPLICATION_CLASS(klass);
    ac->startup = netpulse_app_startup;
    ac->activate = netpulse_app_activate;
}

static void
netpulse_app_init(NetPulseApp *self)
{
    (void)self;
}

NetPulseApp *
netpulse_app_new(void)
{
    return g_object_new(NETPULSE_TYPE_APP,
                        "application-id", "org.netpulse.Monitor",
                        "flags", G_APPLICATION_DEFAULT_FLAGS,
                        NULL);
}

NpSettings *
netpulse_app_get_settings(NetPulseApp *app)
{
    g_return_val_if_fail(NETPULSE_IS_APP(app), NULL);
    return app->settings;
}

NpDiagnostics *
netpulse_app_get_diagnostics(NetPulseApp *app)
{
    g_return_val_if_fail(NETPULSE_IS_APP(app), NULL);
    return app->diag;
}

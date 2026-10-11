#include "netpulse/ui.h"

/* ------------------------------------------------------------------ pills */

GtkWidget *
np_status_pill_new(void)
{
    GtkWidget *label = gtk_label_new("Unknown");
    gtk_widget_add_css_class(label, "np-pill");
    gtk_widget_add_css_class(label, "muted");
    return label;
}

void
np_status_pill_set(GtkWidget *pill, const char *text, const char *css_class)
{
    g_return_if_fail(GTK_IS_LABEL(pill));
    gtk_label_set_text(GTK_LABEL(pill), text ? text : "");
    gtk_widget_remove_css_class(pill, "ok");
    gtk_widget_remove_css_class(pill, "warn");
    gtk_widget_remove_css_class(pill, "err");
    gtk_widget_remove_css_class(pill, "muted");
    gtk_widget_add_css_class(pill, css_class ? css_class : "muted");
}

/* ------------------------------------------------------------ metric cards */

typedef struct {
    GtkWidget *value;
    GtkWidget *subtitle;
} CardParts;

GtkWidget *
np_metric_card_new(const char *title, const char *initial_value,
                   const char *subtitle)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(frame, "np-card");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_frame_set_child(GTK_FRAME(frame), box);

    GtkWidget *t = gtk_label_new(title ? title : "");
    gtk_widget_add_css_class(t, "np-card-title");
    gtk_label_set_xalign(GTK_LABEL(t), 0.0f);
    gtk_box_append(GTK_BOX(box), t);

    GtkWidget *v = gtk_label_new(initial_value ? initial_value : "—");
    gtk_widget_add_css_class(v, "np-card-value");
    gtk_label_set_xalign(GTK_LABEL(v), 0.0f);
    gtk_box_append(GTK_BOX(box), v);

    GtkWidget *s = gtk_label_new(subtitle ? subtitle : "");
    gtk_widget_add_css_class(s, "np-card-subtitle");
    gtk_label_set_xalign(GTK_LABEL(s), 0.0f);
    gtk_box_append(GTK_BOX(box), s);

    CardParts *parts = g_new0(CardParts, 1);
    parts->value = v;
    parts->subtitle = s;
    g_object_set_data_full(G_OBJECT(frame), "np-parts", parts, g_free);
    return frame;
}

static CardParts *
card_parts(GtkWidget *card)
{
    return g_object_get_data(G_OBJECT(card), "np-parts");
}

void
np_metric_card_set_value(GtkWidget *card, const char *value)
{
    CardParts *p = card_parts(card);
    if (p)
        gtk_label_set_text(GTK_LABEL(p->value), value ? value : "—");
}

void
np_metric_card_set_subtitle(GtkWidget *card, const char *subtitle)
{
    CardParts *p = card_parts(card);
    if (p)
        gtk_label_set_text(GTK_LABEL(p->subtitle), subtitle ? subtitle : "");
}

void
np_metric_card_set_state(GtkWidget *card, const char *css_class)
{
    const char *classes[] = { "ok", "warn", "err", NULL };
    for (int i = 0; classes[i]; i++)
        gtk_widget_remove_css_class(card, classes[i]);
    if (css_class)
        gtk_widget_add_css_class(card, css_class);
}

/* ------------------------------------------------------------------ toast */

static void
hide_toast_once(gpointer data)
{
    NpWindowState *st = data;
    if (st && st->toast_revealer)
        gtk_revealer_set_reveal_child(GTK_REVEALER(st->toast_revealer), FALSE);
}

void
np_show_toast(NpWindowState *state, const char *message)
{
    if (!state || !state->toast_label || !state->toast_revealer)
        return;
    gtk_label_set_text(GTK_LABEL(state->toast_label), message);
    gtk_revealer_set_reveal_child(GTK_REVEALER(state->toast_revealer), TRUE);
    g_timeout_add_once(3000, hide_toast_once, state);
}

GtkWidget *
np_section_label_new(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "np-section");
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    return l;
}

/*
 * Icon helper: desktop themes vary in which symbolic icons they ship, so we
 * walk a fallback list and pick the first name the active GtkIconTheme can
 * actually resolve. Falls back to the first candidate otherwise, which keeps
 * GTK's own "missing image" behaviour.
 */
const char *
np_resolve_icon(const char *primary, const char *alt1, const char *alt2)
{
    const char *names[3] = { primary, alt1, alt2 };
    const char *chosen = primary;

    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
        for (int i = 0; i < 3 && names[i]; i++) {
            if (gtk_icon_theme_has_icon(theme, names[i])) {
                chosen = names[i];
                break;
            }
        }
    }
    return chosen;
}

GtkWidget *
np_icon(const char *primary, const char *alt1, const char *alt2,
        gint pixel_size)
{
    GtkWidget *img = gtk_image_new_from_icon_name(
        np_resolve_icon(primary, alt1, alt2));
    gtk_image_set_pixel_size(GTK_IMAGE(img), pixel_size);
    return img;
}

/* --------------------------------------------------------------- app mark */

#define NP_ICON_RES "/org/netpulse/Monitor/icons/%dx%d/apps/org.netpulse.Monitor.png"

/**
 * The NetPulse mark, loaded from the compiled-in GResource.
 *
 * Preferring the bundle means the branding is identical whether the app runs
 * from a source tree or an installed copy, and needs no icon-theme lookup.
 * Returns NULL when the resource is unavailable, so callers can fall back.
 */
GdkTexture *
np_app_icon_texture(int size)
{
    static const int sizes[] = { 128, 64, 48, 32, 16 };
    for (guint i = 0; i < G_N_ELEMENTS(sizes); i++) {
        if (sizes[i] > size)
            continue;
        g_autofree char *path = g_strdup_printf(NP_ICON_RES, sizes[i], sizes[i]);
        GdkTexture *tex = gdk_texture_new_from_resource(path);
        if (tex)
            return tex;
    }
    return NULL;
}

GtkWidget *
np_app_icon_widget(int pixel_size)
{
    GdkTexture *tex = np_app_icon_texture(pixel_size * 2);
    if (!tex)
        return np_icon("network-workgroup-symbolic", "computer-symbolic",
                       "network-server-symbolic", pixel_size);

    /* GtkImage scales a paintable down to its allocation, so the texture is
     * handed over at full resolution rather than pre-resampled here. */
    GtkWidget *img = gtk_image_new_from_paintable(GDK_PAINTABLE(tex));
    gtk_image_set_pixel_size(GTK_IMAGE(img), pixel_size);
    return img;
}

/**
 * Apply the app icon to every window.
 *
 * The icon-theme name is the primary path -- it is what the taskbar, launcher
 * and window manager display. GTK 4 exposes no way to set a window icon from a
 * paintable, so when running uninstalled from a checkout we point the icon
 * theme at the bundled resource directory; the same name then resolves without
 * the icons having to be copied into /usr/share.
 */
void np_set_app_icon(void)
{
    const char *name = "org.netpulse.Monitor";

    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
        if (!gtk_icon_theme_has_icon(theme, name)) {
            /* <size>x<size>/apps/<name>.png is the unthemed layout the icon
             * theme looks for, so the resource dir is the search path. */
            gtk_icon_theme_add_search_path(
                theme, "resource:///org/netpulse/Monitor/icons");
        }
        gtk_icon_theme_has_icon(theme, name);
    }
    gtk_window_set_default_icon_name(name);
}

/* ------------------------------------------------- native save + confirm */

typedef struct {
    NpSaveCallback cb;
    gpointer user_data;
} SaveCtx;

#if GTK_CHECK_VERSION(4, 10, 0)
static void
on_file_dialog_done(GObject *source, GAsyncResult *res, gpointer data)
{
    SaveCtx *ctx = data; /* owned: freed at the end of this callback */
    g_autoptr(GError) err = NULL;
    GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, &err);
    if (f) {
        g_autofree char *path = g_file_get_path(f);
        if (ctx->cb)
            ctx->cb(path, ctx->user_data);
        g_object_unref(f);
    }
    g_free(ctx);
}

void
np_show_save_dialog(NpWindowState *state, const char *title,
                    const char *suggested_name,
                    NpSaveCallback cb, gpointer user_data)
{
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, title ? title : "Export");
    if (suggested_name) {
        char *p = g_build_filename(g_get_home_dir(), suggested_name, NULL);
        g_autoptr(GFile) f = g_file_new_for_path(p);
        g_free(p);
        gtk_file_dialog_set_initial_file(dlg, f);
    }
    SaveCtx *ctx = g_new0(SaveCtx, 1);
    ctx->cb = cb;
    ctx->user_data = user_data;
    gtk_file_dialog_save(dlg, state ? GTK_WINDOW(state->window) : NULL,
                         NULL, on_file_dialog_done, ctx);
    g_object_unref(dlg);
}
#else /* GTK < 4.10 */
typedef struct {
    NpSaveCallback cb;
    gpointer user_data;
    char *suggested;
} SaveCtxLegacy;

static void
on_native_response(GtkNativeDialog *d, int r, gpointer data)
{
    SaveCtxLegacy *ctx = data; /* freed below */
    if (r == GTK_RESPONSE_ACCEPT) {
        g_autoptr(GFile) f = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(d));
        g_autofree char *path = g_file_get_path(f);
        if (ctx->cb && path)
            ctx->cb(path, ctx->user_data);
    }
    g_free(ctx->suggested);
    g_free(ctx);
    g_object_unref(d);
}

void
np_show_save_dialog(NpWindowState *state, const char *title,
                    const char *suggested_name,
                    NpSaveCallback cb, gpointer user_data)
{
    GtkFileChooserNative *dlg = gtk_file_chooser_native_new(
        title ? title : "Export",
        state ? GTK_WINDOW(state->window) : NULL,
        GTK_FILE_CHOOSER_ACTION_SAVE, "Export", "Cancel");
    if (suggested_name)
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), suggested_name);
    SaveCtxLegacy *ctx = g_new0(SaveCtxLegacy, 1);
    ctx->cb = cb;
    ctx->user_data = user_data;
    ctx->suggested = g_strdup(suggested_name);
    g_signal_connect(dlg, "response", G_CALLBACK(on_native_response), ctx);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dlg));
}
#endif

typedef struct {
    NpConfirmCallback cb;
    gpointer user_data;
} ConfirmCtx;

#if GTK_CHECK_VERSION(4, 10, 0)
static void
on_alert_chosen(GObject *source, GAsyncResult *res, gpointer data)
{
    ConfirmCtx *ctx = data; /* owned: freed at the end of this callback */
    g_autoptr(GError) err = NULL;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source),
                                                res, &err);
    /* choice >= 0 selects a button (0 = confirm); -1 = dismissed/cancelled */
    if (ctx->cb)
        ctx->cb(choice == 0, ctx->user_data);
    g_free(ctx);
    g_object_unref(source);
}

void
np_show_confirm(NpWindowState *state, const char *message,
                const char *confirm_label,
                NpConfirmCallback cb, gpointer user_data)
{
    GtkAlertDialog *dlg = gtk_alert_dialog_new(
        "%s", message ? message : "Are you sure?");
    gtk_alert_dialog_set_modal(dlg, TRUE);
    const char *buttons[] = {
        confirm_label ? confirm_label : "Confirm", "cancel", NULL
    };
    gtk_alert_dialog_set_buttons(dlg, buttons);
    gtk_alert_dialog_set_cancel_button(dlg, 1);
    gtk_alert_dialog_set_default_button(dlg, 1);

    ConfirmCtx *ctx = g_new0(ConfirmCtx, 1);
    ctx->cb = cb;
    ctx->user_data = user_data;
    gtk_alert_dialog_choose(dlg, state ? GTK_WINDOW(state->window) : NULL,
                            NULL, on_alert_chosen, ctx);
}
#else /* GTK < 4.10 */
typedef struct {
    NpConfirmCallback cb;
    gpointer user_data;
    GtkWidget *dlg;
} ConfirmCtxLegacy;

static void
on_message_response(GtkDialog *d, int r, gpointer data)
{
    ConfirmCtxLegacy *ctx = data; /* freed below */
    if (ctx->cb)
        ctx->cb(r == GTK_RESPONSE_OK, ctx->user_data);
    g_free(ctx);
    gtk_window_destroy(GTK_WINDOW(d));
}

void
np_show_confirm(NpWindowState *state, const char *message,
                const char *confirm_label,
                NpConfirmCallback cb, gpointer user_data)
{
    GtkWidget *dlg = gtk_message_dialog_new(
        state ? GTK_WINDOW(state->window) : NULL, GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "%s",
        message ? message : "Are you sure?");
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dlg), confirm_label ? confirm_label : "Confirm",
                          GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_CANCEL);
    ConfirmCtxLegacy *ctx = g_new0(ConfirmCtxLegacy, 1);
    ctx->cb = cb;
    ctx->user_data = user_data;
    ctx->dlg = dlg;
    g_signal_connect(dlg, "response", G_CALLBACK(on_message_response), ctx);
    gtk_window_present(GTK_WINDOW(dlg));
}
#endif

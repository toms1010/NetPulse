/*
 * NetPulse sidebar: logo + 7 navigation rows inside a vertical GtkBox.
 *
 * Responsibilities are deliberately narrow — the sidebar owns *presentation*
 * (width, label visibility, tooltips, selection highlight) and reports user
 * intent through the on_select callback. Page state lives in the main window,
 * so collapsing or expanding can never reset the active page.
 *
 * Implementation note: the widget is a plain GtkBox with a private state
 * struct attached via qdata, rather than a GObject subclass. GtkWidget
 * subclassing in GTK 4 means overriding vfuncs for something we can express
 * more directly with a size-request animation, and the box API stays simple.
 *
 * The expand/collapse transition animates the width size-request from a
 * main-loop tick. It never blocks: each tick does one frame's worth of work
 * and returns immediately, so monitoring timers keep firing during animation.
 * With reduced-motion preferences the transition is applied in one step.
 */

#include "netpulse/sidebar.h"
#include "netpulse/ui.h"

typedef struct {
    GtkWidget *box;

    GtkWidget *list;
    GtkWidget *brand_label;
    GtkWidget *brand_box;
    GtkWidget *rows[NP_SIDEBAR_ITEM_COUNT];
    GtkWidget *row_boxes[NP_SIDEBAR_ITEM_COUNT];
    GtkWidget *row_icons[NP_SIDEBAR_ITEM_COUNT];
    GtkWidget *row_labels[NP_SIDEBAR_ITEM_COUNT];

    gboolean   collapsed;
    gboolean   suppress_select;
    gboolean   mapped;       /* gate: no animation before the window is shown */

    double     anim_from;
    double     anim_to;
    gint64     anim_start_us;
    guint      anim_id;

    NpSettings *settings;         /* borrowed */
    void      (*on_select)(int index, gpointer user_data);
    gpointer   user_data;
} NpSidebar;

static const struct {
    const char *label;
    const char *icon;
    const char *alt1;
    const char *alt2;
} kItems[NP_SIDEBAR_ITEM_COUNT] = {
    { "Overview",           "view-grid-symbolic",                 "view-list-symbolic",             "computer-symbolic" },
    { "Network Interfaces", "network-wired-symbolic",            "network-workgroup-symbolic",    "network-server-symbolic" },
    { "Connectivity Tests", "network-transmit-receive-symbolic", "network-wired-symbolic",        "dialog-information-symbolic" },
    { "Traffic Monitor",    "utilities-system-monitor-symbolic", "content-loading-symbolic",       "media-playback-start-symbolic" },
    { "Diagnostics & Logs", "dialog-information-symbolic",       "utilities-system-monitor-symbolic", "view-list-symbolic" },
    { "Settings",           "emblem-system-symbolic",            "preferences-system-symbolic",   "emblem-preferences-symbolic" },
    { "About NetPulse",     "help-about-symbolic",               "dialog-information-symbolic",    "help-browser-symbolic" },
};

/* ------------------------------------------------------------------ utils */

static NpSidebar *
sidebar_of(GtkWidget *w)
{
    return w ? g_object_get_data(G_OBJECT(w), "np-sidebar") : NULL;
}

/* Honour the desktop "reduce motion" preference when the schema is present. */
static gboolean
animations_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        GSettings *gs = g_settings_new("org.gnome.desktop.interface");
        if (gs) {
            cached = g_settings_get_boolean(gs, "enable-animations") ? 1 : 0;
            g_object_unref(gs);
        } else {
            cached = 1;
        }
    }
    return cached == 1;
}

static void
apply_width(NpSidebar *self, double width)
{
    int w = (int)(width + 0.5);
    if (w < NP_SIDEBAR_WIDTH_COLLAPSED)
        w = NP_SIDEBAR_WIDTH_COLLAPSED;
    if (w > NP_SIDEBAR_WIDTH_EXPANDED)
        w = NP_SIDEBAR_WIDTH_EXPANDED;
    gtk_widget_set_size_request(self->box, w, -1);
}

/* Show/hide labels and re-centre icons for the target state. */
static void
apply_collapsed_content(NpSidebar *self, gboolean collapsed)
{
    gtk_widget_set_visible(self->brand_label, !collapsed);
    gtk_widget_set_margin_start(self->brand_box, collapsed ? 0 : 14);
    gtk_widget_set_margin_end(self->brand_box, collapsed ? 0 : 14);
    /* GtkBox packs children at their natural size from the start, so setting
     * halign on the icon alone does nothing here. Centring the *box* is what
     * actually centres the mark once the wordmark is hidden. */
    gtk_widget_set_halign(self->brand_box,
                          collapsed ? GTK_ALIGN_CENTER : GTK_ALIGN_FILL);

    for (int i = 0; i < NP_SIDEBAR_ITEM_COUNT; i++) {
        GtkWidget *box = self->row_boxes[i];
        GtkWidget *lbl = self->row_labels[i];
        GtkWidget *icon = self->row_icons[i];

        /* Hiding labels is what collapses a row; the row itself, its tooltip
         * and its accessible name are untouched, so collapsed rows stay
         * discoverable and reachable by keyboard/screen reader. */
        gtk_widget_set_visible(lbl, !collapsed);
        gtk_widget_set_halign(box,
                              collapsed ? GTK_ALIGN_CENTER : GTK_ALIGN_FILL);
        gtk_widget_set_margin_start(box, collapsed ? 0 : 12);
        gtk_widget_set_margin_end(box, collapsed ? 0 : 12);
        gtk_widget_set_margin_start(icon, collapsed ? 0 : 6);
        gtk_widget_set_margin_end(icon, collapsed ? 0 : 6);
        gtk_widget_set_size_request(icon, 16, 16);
    }
}

static void
stop_animation(NpSidebar *self)
{
    if (self->anim_id) {
        g_source_remove(self->anim_id);
        self->anim_id = 0;
    }
}

static gboolean
anim_tick(gpointer data)
{
    NpSidebar *self = data;

    double total = (double)NP_SIDEBAR_ANIM_MS * 1000.0;
    double elapsed = (double)(g_get_monotonic_time() - self->anim_start_us);
    double t = total > 0.0 ? elapsed / total : 1.0;
    if (t > 1.0)
        t = 1.0;

    /* Ease-out cubic: quick start, gentle settle. */
    double eased = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    apply_width(self, self->anim_from + (self->anim_to - self->anim_from) * eased);

    if (t >= 1.0) {
        self->anim_id = 0;
        apply_collapsed_content(self, self->collapsed);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void
set_target_width(NpSidebar *self, gboolean collapsed, gboolean animate)
{
    double target = collapsed ? NP_SIDEBAR_WIDTH_COLLAPSED
                              : NP_SIDEBAR_WIDTH_EXPANDED;

    stop_animation(self);

    /* No animation before the window is mapped (nothing to animate) or when
     * reduced motion is requested: apply the final state in one step. This is
     * a deliberate stable fallback rather than a fragile partial animation. */
    if (!animate || !self->mapped || !animations_enabled()) {
        apply_width(self, target);
        apply_collapsed_content(self, collapsed);
        return;
    }

    int current = 0;
    gtk_widget_get_size_request(self->box, &current, NULL);
    self->anim_from = current > 0 ? current : target;
    self->anim_to = target;
    self->anim_start_us = g_get_monotonic_time();
    self->anim_id = g_timeout_add(16, anim_tick, self);
}

/* ------------------------------------------------------------- selection */

static void
on_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
    (void)box;
    NpSidebar *self = data;
    if (self->suppress_select || !row || !self->on_select)
        return;
    int idx = gtk_list_box_row_get_index(row);
    if (idx >= 0 && idx < NP_SIDEBAR_ITEM_COUNT)
        self->on_select(idx, self->user_data);
}

static GtkWidget *
build_row(NpSidebar *self, int i)
{
    GtkWidget *row = gtk_list_box_row_new();
    gtk_widget_add_css_class(row, "np-nav-row");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    GtkWidget *icon = np_icon(kItems[i].icon, kItems[i].alt1,
                              kItems[i].alt2, 16);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *label = gtk_label_new(kItems[i].label);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    /* Deliberately no hexpand on the labels: a container whose child expands
     * is itself treated as wanting room by the parent GtkBox, which made the
     * whole sidebar absorb the leftover space instead of the content pane.
     * The sidebar's width is pinned by apply_width() on the root box, so the
     * labels only ever need their natural size. */
    gtk_box_append(GTK_BOX(box), label);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

    /* Tooltips stay in place in both states, so icon-only (collapsed) rows
     * remain discoverable; the visible label doubles as the accessible name. */
    gtk_widget_set_tooltip_text(row, kItems[i].label);
    gtk_accessible_update_property(GTK_ACCESSIBLE(row),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   kItems[i].label, -1);

    self->rows[i] = row;
    self->row_boxes[i] = box;
    self->row_icons[i] = icon;
    self->row_labels[i] = label;
    return row;
}

static void
state_free(gpointer data)
{
    NpSidebar *self = data;
    stop_animation(self);
    g_free(self);
}

/* ------------------------------------------------------------ public API */

GtkWidget *
np_sidebar_new(NpSettings *settings,
               void (*on_select)(int index, gpointer user_data),
               gpointer user_data)
{
    NpSidebar *self = g_new0(NpSidebar, 1);
    self->settings = settings;
    self->on_select = on_select;
    self->user_data = user_data;

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    self->box = box;
    gtk_widget_add_css_class(box, "np-sidebar");
    gtk_widget_set_vexpand(box, TRUE);
    apply_width(self, NP_SIDEBAR_WIDTH_EXPANDED);

    /* Brand: logo always visible; wordmark hides when collapsed. */
    self->brand_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(self->brand_box, 14);
    gtk_widget_set_margin_end(self->brand_box, 14);
    gtk_widget_set_margin_top(self->brand_box, 14);
    gtk_widget_set_margin_bottom(self->brand_box, 10);
    GtkWidget *logo = np_app_icon_widget(28);
    gtk_widget_set_valign(logo, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(self->brand_box), logo);
    self->brand_label = gtk_label_new("NetPulse");
    gtk_widget_add_css_class(self->brand_label, "np-brand");
    gtk_label_set_xalign(GTK_LABEL(self->brand_label), 0.0f);
    gtk_box_append(GTK_BOX(self->brand_box), self->brand_label);
    gtk_box_append(GTK_BOX(box), self->brand_box);

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    self->list = gtk_list_box_new();
    gtk_widget_add_css_class(self->list, "np-nav");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(self->list),
                                    GTK_SELECTION_SINGLE);
    gtk_widget_set_vexpand(self->list, TRUE);
    for (int i = 0; i < NP_SIDEBAR_ITEM_COUNT; i++)
        gtk_list_box_append(GTK_LIST_BOX(self->list), build_row(self, i));
    gtk_box_append(GTK_BOX(box), self->list);

    g_signal_connect(self->list, "row-selected",
                     G_CALLBACK(on_row_selected), self);

    /* Default active row without firing the callback; the window selects the
     * Overview page explicitly after all pages are built. */
    self->suppress_select = TRUE;
    gtk_list_box_select_row(GTK_LIST_BOX(self->list),
                            GTK_LIST_BOX_ROW(self->rows[0]));
    self->suppress_select = FALSE;

    /* Freed with the widget; animation timer is stopped there too. */
    g_object_set_data_full(G_OBJECT(box), "np-sidebar", self, state_free);

    np_sidebar_apply_settings(box, settings);
    return box;
}

void
np_sidebar_set_active(GtkWidget *sidebar, int index)
{
    NpSidebar *self = sidebar_of(sidebar);
    if (!self || index < 0 || index >= NP_SIDEBAR_ITEM_COUNT)
        return;

    GtkListBoxRow *cur = gtk_list_box_get_selected_row(GTK_LIST_BOX(self->list));
    if (cur && gtk_list_box_row_get_index(cur) == index)
        return; /* already active: no signal, no loop */

    gboolean prev = self->suppress_select;
    self->suppress_select = TRUE;
    gtk_list_box_select_row(GTK_LIST_BOX(self->list),
                            GTK_LIST_BOX_ROW(self->rows[index]));
    self->suppress_select = prev;
}

void
np_sidebar_set_collapsed(GtkWidget *sidebar, gboolean collapsed)
{
    NpSidebar *self = sidebar_of(sidebar);
    if (!self)
        return;
    self->collapsed = collapsed;
    set_target_width(self, collapsed, FALSE);
}

void
np_sidebar_set_mapped(GtkWidget *sidebar, gboolean mapped)
{
    NpSidebar *self = sidebar_of(sidebar);
    if (self)
        self->mapped = mapped;
}

gboolean
np_sidebar_get_collapsed(GtkWidget *sidebar)
{
    NpSidebar *self = sidebar_of(sidebar);
    return self ? self->collapsed : FALSE;
}

void
np_sidebar_toggle(GtkWidget *sidebar)
{
    NpSidebar *self = sidebar_of(sidebar);
    if (!self)
        return;
    self->collapsed = !self->collapsed;
    set_target_width(self, self->collapsed, TRUE);
    np_sidebar_save_state(sidebar);
}

void
np_sidebar_apply_settings(GtkWidget *sidebar, const NpSettings *settings)
{
    NpSidebar *self = sidebar_of(sidebar);
    if (!self)
        return;
    if (settings)
        self->settings = (NpSettings *)settings;
    /* A missing or malformed key leaves sidebar_collapsed FALSE (expanded),
     * which np_settings_load() already guarantees by defaulting. */
    self->collapsed = (self->settings && self->settings->sidebar_collapsed);
    set_target_width(self, self->collapsed, FALSE);
}

gboolean
np_sidebar_save_state(GtkWidget *sidebar)
{
    NpSidebar *self = sidebar_of(sidebar);
    if (!self || !self->settings)
        return FALSE;
    self->settings->sidebar_collapsed = self->collapsed;
    GError *err = NULL;
    if (!np_settings_save(self->settings, &err)) {
        g_clear_error(&err);
        return FALSE;
    }
    return TRUE;
}
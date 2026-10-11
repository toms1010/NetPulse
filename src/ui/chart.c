#include "netpulse/ui.h"
#include "netpulse/traffic_monitor.h"
#include <cairo.h>
#include <math.h>

typedef struct {
    GArray *copy; /* NpTrafficSample copy for paint */
    double  history_sec;
    double  max_rate;
} ChartData;

static void
chart_data_free(ChartData *d)
{
    if (!d)
        return;
    if (d->copy)
        g_array_free(d->copy, TRUE);
    g_free(d);
}

static ChartData *
chart_data(GtkWidget *chart)
{
    return g_object_get_data(G_OBJECT(chart), "np-chart");
}

static void
np_chart_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
              gpointer user_data)
{
    (void)user_data;
    ChartData *d = chart_data(GTK_WIDGET(area));

    /* Background */
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); /* transparent-ish; CSS provides bg */
    cairo_paint(cr);

    GtkStyleContext *ctx = NULL;
    (void)ctx;
    GdkRGBA fg = { 0.55f, 0.55f, 0.60f, 1.0f }, grid, dl, ul;
    gtk_widget_get_color(GTK_WIDGET(area), &fg);
    grid = fg; grid.alpha = 0.12;
    /* Download: green/blue; Upload: amber/teal. Readable in both themes. */
    dl = (GdkRGBA){ 0.20, 0.70, 0.35, 1.0 };
    ul = (GdkRGBA){ 0.25, 0.55, 0.95, 1.0 };

    const double pad_l = 60.0, pad_r = 10.0, pad_t = 10.0, pad_b = 22.0;
    double pw = width - pad_l - pad_r;
    double ph = height - pad_t - pad_b;
    if (pw < 10 || ph < 10)
        return;

    double max_rate = 1024.0;
    if (d && d->max_rate > max_rate)
        max_rate = d->max_rate;
    if (d && d->copy) {
        for (guint i = 0; i < d->copy->len; i++) {
            NpTrafficSample *s = &g_array_index(d->copy, NpTrafficSample, i);
            max_rate = MAX(max_rate, MAX(s->rx_rate, s->tx_rate));
        }
    }
    max_rate *= 1.15; /* headroom */

    /* Grid: 4 horizontal lines with rate labels. */
    cairo_set_source_rgba(cr, grid.red, grid.green, grid.blue, grid.alpha);
    cairo_set_line_width(cr, 1.0);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.0);
    for (int i = 0; i <= 4; i++) {
        double y = pad_t + ph - (ph * i / 4.0);
        cairo_move_to(cr, pad_l, y);
        cairo_line_to(cr, pad_l + pw, y);
        cairo_stroke(cr);
        g_autofree char *lbl = np_format_rate(max_rate * i / 4.0);
        cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.65);
        /* Right-align the tick label against the axis. Drawing it from a fixed
         * x would let a wide label ("1.4 MB/s") run past pad_l and collide with
         * the gridline it belongs to. */
        cairo_text_extents_t ext;
        cairo_text_extents(cr, lbl ? lbl : "", &ext);
        double tx = pad_l - 6.0 - ext.width;
        if (tx < 2.0)
            tx = 2.0;
        cairo_move_to(cr, tx, y + 3.0);
        cairo_show_text(cr, lbl ? lbl : "");
        cairo_set_source_rgba(cr, grid.red, grid.green, grid.blue, grid.alpha);
    }

    if (!d || !d->copy || d->copy->len < 1) {
        cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.55);
        cairo_set_font_size(cr, 12.0);
        const char *msg = "No data yet — monitoring will populate this chart.";
        cairo_text_extents_t ext;
        cairo_text_extents(cr, msg, &ext);
        cairo_move_to(cr, pad_l + (pw - ext.width) / 2.0,
                      pad_t + ph / 2.0);
        cairo_show_text(cr, msg);
        return;
    }

    double t_end = g_array_index(d->copy, NpTrafficSample, d->copy->len - 1).t;
    double t_start = t_end - d->history_sec;

#define X_OF(t) (pad_l + pw * ((t) - t_start) / d->history_sec)

    /* Download series (green). */
    cairo_set_source_rgba(cr, dl.red, dl.green, dl.blue, 1.0);
    cairo_set_line_width(cr, 1.6);
    gboolean started = FALSE;
    for (guint i = 0; i < d->copy->len; i++) {
        NpTrafficSample *s = &g_array_index(d->copy, NpTrafficSample, i);
        double x = X_OF(s->t);
        double y = pad_t + ph - (ph * s->rx_rate / max_rate);
        if (!started) {
            cairo_move_to(cr, x, y);
            started = TRUE;
        } else {
            cairo_line_to(cr, x, y);
        }
    }
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, ul.red, ul.green, ul.blue, 1.0);
    started = FALSE;
    for (guint i = 0; i < d->copy->len; i++) {
        NpTrafficSample *s = &g_array_index(d->copy, NpTrafficSample, i);
        double x = X_OF(s->t);
        double y = pad_t + ph - (ph * s->tx_rate / max_rate);
        if (!started) {
            cairo_move_to(cr, x, y);
            started = TRUE;
        } else {
            cairo_line_to(cr, x, y);
        }
    }
    cairo_stroke(cr);

    /* X axis: time labels. */
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.65);
    cairo_set_font_size(cr, 10.0);
    for (int i = 0; i <= 4; i++) {
        double t = t_start + d->history_sec * i / 4.0;
        double x = X_OF(t);
        g_autofree char *lbl = g_strdup_printf("-%ds", (int)(t_end - t));
        /* Centre each tick under its position, then clamp into the plot area so
         * the end labels never spill under the y-axis ticks or past the edge. */
        cairo_text_extents_t xext;
        cairo_text_extents(cr, lbl, &xext);
        double tx = x - xext.width / 2.0;
        if (tx < pad_l)
            tx = pad_l;
        if (tx + xext.width > width - 2.0)
            tx = width - 2.0 - xext.width;
        cairo_move_to(cr, tx, height - 6.0);
        cairo_show_text(cr, lbl);
    }
#undef X_OF
}

GtkWidget *
np_chart_new(void)
{
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_add_css_class(area, "np-chart");
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 400);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 220);
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), np_chart_draw,
                                   NULL, NULL);
    return area;
}

void
np_chart_set_series(GtkWidget *chart, GArray *samples, double history_sec)
{
    g_return_if_fail(GTK_IS_DRAWING_AREA(chart));
    ChartData *d = g_new0(ChartData, 1);
    d->history_sec = history_sec > 0 ? history_sec : 120.0;
    if (samples && samples->len > 0) {
        d->copy = g_array_sized_new(FALSE, TRUE, sizeof(NpTrafficSample),
                                    samples->len);
        g_array_append_vals(d->copy, samples->data, samples->len);
        for (guint i = 0; i < d->copy->len; i++) {
            NpTrafficSample *s = &g_array_index(d->copy, NpTrafficSample, i);
            d->max_rate = MAX(d->max_rate, MAX(s->rx_rate, s->tx_rate));
        }
    }
    g_object_set_data_full(G_OBJECT(chart), "np-chart", d,
                           (GDestroyNotify)chart_data_free);
    gtk_widget_queue_draw(chart);
}

void
np_chart_queue_redraw(GtkWidget *chart)
{
    if (chart)
        gtk_widget_queue_draw(chart);
}

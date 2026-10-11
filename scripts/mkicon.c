/*
 * Generates the NetPulse app icon PNGs from the same geometry as
 * resources/icons/netpulse-icon.svg.
 *
 * librsvg's gdk-pixbuf loader is not available in every environment, and GTK's
 * icon theme wants real PNGs on disk, so the mark is drawn with Cairo and
 * written through gdk-pixbuf (whose PNG loader is always present). Keeping the
 * numbers identical to the SVG means both stay visually in sync.
 *
 * Not part of the application build; a maintenance tool.
 *   cc -o mkicon mkicon.c $(pkg-config --cflags --libs cairo gdk-pixbuf-2.0)
 */

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <stdlib.h>

/* Design grid is 32x32 units; every value below is in those units. */
#define GRID 32.0

static const double BG_R      = 8.0;   /* corner radius      */
#define BORDER_W   1.5            /* inner hairline      */
#define WAVE_W     2.0            /* waveform stroke     */
#define BORDER_ALPHA 0.55

static void
rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -G_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0,     G_PI_2);
    cairo_arc(cr, x + r,     y + h - r, r, G_PI_2, G_PI);
    cairo_arc(cr, x + r,     y +     r, r, G_PI,   3 * G_PI_2);
    cairo_close_path(cr);
}

static void
draw_mark(cairo_t *cr, double size)
{
    const double s = size / GRID;
    cairo_save(cr);
    cairo_scale(cr, s, s);

    /* Badge surface. */
    rounded_rect(cr, 1, 1, GRID - 2, GRID - 2, BG_R);
    cairo_set_source_rgb(cr, 15 / 255.0, 23 / 255.0, 42 / 255.0);
    cairo_fill_preserve(cr);

    /* Emerald hairline, inset by half its width so it stays inside the badge. */
    cairo_set_source_rgba(cr, 16 / 255.0, 185 / 255.0, 129 / 255.0, BORDER_ALPHA);
    cairo_set_line_width(cr, BORDER_W);
    rounded_rect(cr, 1.75, 1.75, GRID - 3.5, GRID - 3.5, BG_R - 0.75);
    cairo_stroke(cr);

    /* Traffic waveform: flat baseline with one up- and one down-spike. */
    cairo_set_source_rgb(cr, 16 / 255.0, 185 / 255.0, 129 / 255.0);
    cairo_set_line_width(cr, WAVE_W);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    const double pts[][2] = {
        { 6.0, 16.0 }, { 10.2, 16.0 }, { 12.8, 9.6 }, { 16.2, 22.4 },
        { 18.8, 16.0 }, { 26.0, 16.0 },
    };
    for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
        if (i == 0)
            cairo_move_to(cr, pts[i][0], pts[i][1]);
        else
            cairo_line_to(cr, pts[i][0], pts[i][1]);
    }
    cairo_stroke(cr);
    cairo_restore(cr);
}

static int
write_png(const char *dir, const char *name, int size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);

    cairo_surface_t *surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *cr = cairo_create(surf);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);   /* transparent backdrop */
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_mark(cr, size);

    cairo_surface_write_to_png(surf, path);
    int err = cairo_surface_status(surf);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    if (err != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "%s: %s\n", path, cairo_status_to_string(err));
        return 1;
    }
    printf("wrote %s (%dx%d)\n", path, size, size);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <out-dir> <size> [size...]\n", argv[0]);
        return 2;
    }
    int rc = 0;
    for (int i = 2; i < argc; i++) {
        int size = atoi(argv[i]);
        char name[64];
        /* Icon-theme naming convention: <size>x<size>/apps/<id>.png */
        snprintf(name, sizeof name, "%dx%d/apps/org.netpulse.Monitor.png",
                 size, size);
        rc |= write_png(argv[1], name, size);
    }
    return rc;
}
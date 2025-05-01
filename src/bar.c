#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_icccm.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bar.h"
#include "config.h"

static char*        workspaceNames[9];

xcb_window_t*       barWindows   = NULL;
int                 barVisible   = 1;
bar_draw_context_t* bar_contexts = NULL;

static uint32_t     barBgColor;
static uint32_t     barFgColor;
static uint32_t     barBorderPixel;
static uint32_t     barActiveWsPixel;
static uint32_t     barUrgentWsPixel;

static char         barActiveTextColor_str[16];
static char         barInactiveTextColor_str[16];
static char         barUrgentTextColor_str[16];
static char         barStatusTextColor_str[16];

static xcb_atom_t   WM_NAME;
static xcb_atom_t   NET_WM_STRUT;
static xcb_atom_t   NET_WM_STRUT_PARTIAL;

static char         statusText[MAX_STATUS_LENGTH] = "";

static int          initialized = 0;

static void         initWorkspaceNames(void) {
    for (int i = 0; i < workspaceCount; i++) {
        workspaceNames[i] = malloc(8);
        if (workspaceNames[i])
            snprintf(workspaceNames[i], 8, "%d", i + 1);
    }
}

static void freeWorkspaceNames(void) {
    for (int i = 0; i < workspaceCount; i++) {
        free(workspaceNames[i]);
        workspaceNames[i] = NULL;
    }
}

static uint32_t hex_to_rgb(const char* hex) {
    unsigned int r, g, b;
    if (sscanf(hex, "#%02x%02x%02x", &r, &g, &b) != 3)
        return 0;
    return (r << 16) | (g << 8) | b;
}

static void initColors(void) {
    strncpy(barActiveTextColor_str, barActiveTextColor, sizeof(barActiveTextColor_str) - 1);
    strncpy(barInactiveTextColor_str, barInactiveTextColor, sizeof(barInactiveTextColor_str) - 1);
    strncpy(barUrgentTextColor_str, barUrgentTextColor, sizeof(barUrgentTextColor_str) - 1);
    strncpy(barStatusTextColor_str, barStatusTextColor, sizeof(barStatusTextColor_str) - 1);

    barBgColor       = hex_to_rgb(barBackgroundColor);
    barFgColor       = hex_to_rgb(barForegroundColor);
    barBorderPixel   = hex_to_rgb(barBorderColor);
    barActiveWsPixel = hex_to_rgb(barActiveWsColor);
    barUrgentWsPixel = hex_to_rgb(barUrgentWsColor);

    if (barBgColor == 0)
        barBgColor = 0x000000;
    if (barFgColor == 0)
        barFgColor = 0xFFFFFF;
    if (barBorderPixel == 0)
        barBorderPixel = 0x000000;
    if (barActiveWsPixel == 0)
        barActiveWsPixel = barFgColor;
    if (barUrgentWsPixel == 0)
        barUrgentWsPixel = 0xFF0000;
}

static int initFont(void) {
    return 1;
}

static int getTextWidth(const char* text) {
    if (!text || !*text)
        return 0;

    PangoLayout*     layout  = NULL;
    cairo_t*         cr      = NULL;
    cairo_surface_t* surface = NULL;
    int              width   = 0;

    if (bar_contexts && bar_contexts[0].layout) {
        pango_layout_set_text(bar_contexts[0].layout, text, -1);
        pango_layout_get_pixel_size(bar_contexts[0].layout, &width, NULL);
    } else {
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
        cr      = cairo_create(surface);
        layout  = pango_cairo_create_layout(cr);

        if (layout) {
            PangoFontDescription* font_desc = pango_font_description_from_string(barFont);
            if (font_desc) {
                pango_layout_set_font_description(layout, font_desc);
                pango_font_description_free(font_desc);
            }

            pango_layout_set_text(layout, text, -1);
            pango_layout_get_pixel_size(layout, &width, NULL);

            g_object_unref(layout);
        }

        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }

    return width;
}

static void drawText(int monitorIndex, const char* text, int x, int y, const char* color, int isCenter) {
    if (!text || !text[0] || !bar_contexts || !bar_contexts[monitorIndex].cr)
        return;

    cairo_t*     cr     = bar_contexts[monitorIndex].cr;
    PangoLayout* layout = bar_contexts[monitorIndex].layout;

    pango_layout_set_text(layout, text, -1);

    int width, height;
    pango_layout_get_pixel_size(layout, &width, &height);

    if (isCenter)
        x = x - (width / 2);

    unsigned int r, g, b;
    if (sscanf(color, "#%02x%02x%02x", &r, &g, &b) == 3)
        cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
    else
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

    cairo_move_to(cr, x, y + (barHeight - height) / 2);
    pango_cairo_show_layout(cr, layout);
}

void showHideBars(int show) {
    if (!barWindows)
        return;

    barVisible = show;

    for (int i = 0; i < numMonitors; i++) {
        if (barWindows[i]) {
            if (show)
                xcb_map_window(connection, barWindows[i]);
            else
                xcb_unmap_window(connection, barWindows[i]);
        }
    }
    xcb_flush(connection);
}

void resetBarResources(void) {
    if (bar_contexts) {
        for (int i = 0; i < numMonitors; i++) {
            if (bar_contexts[i].layout)
                g_object_unref(bar_contexts[i].layout);
            if (bar_contexts[i].font_desc)
                pango_font_description_free(bar_contexts[i].font_desc);
            if (bar_contexts[i].cr)
                cairo_destroy(bar_contexts[i].cr);
            if (bar_contexts[i].surface)
                cairo_surface_destroy(bar_contexts[i].surface);
        }
        free(bar_contexts);
        bar_contexts = NULL;
    }

    initialized = 0;
}

void createBars(void) {
    if (barWindows) {
        for (int i = 0; i < numMonitors; i++) {
            if (barWindows[i] != 0)
                xcb_destroy_window(connection, barWindows[i]);
        }
        free(barWindows);
    }

    if (bar_contexts) {
        for (int i = 0; i < numMonitors; i++) {
            if (bar_contexts[i].layout)
                g_object_unref(bar_contexts[i].layout);
            if (bar_contexts[i].font_desc)
                pango_font_description_free(bar_contexts[i].font_desc);
            if (bar_contexts[i].cr)
                cairo_destroy(bar_contexts[i].cr);
            if (bar_contexts[i].surface)
                cairo_surface_destroy(bar_contexts[i].surface);
        }
        free(bar_contexts);
    }

    barWindows   = calloc(numMonitors, sizeof(xcb_window_t));
    bar_contexts = calloc(numMonitors, sizeof(bar_draw_context_t));

    if (!barWindows || !bar_contexts) {
        fprintf(stderr, "Failed to allocate memory for bars\n");
        return;
    }

    if (!initialized) {
        initColors();

        WM_NAME = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, strlen("WM_NAME"), "WM_NAME"), NULL)->atom;

        NET_WM_STRUT = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, strlen("_NET_WM_STRUT"), "_NET_WM_STRUT"), NULL)->atom;

        NET_WM_STRUT_PARTIAL = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, strlen("_NET_WM_STRUT_PARTIAL"), "_NET_WM_STRUT_PARTIAL"), NULL)->atom;

        initWorkspaceNames();

        barVisible  = showBar;
        initialized = 1;
    }

    uint32_t values[3];
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK;

    values[0] = barBgColor;
    values[1] = barBorderPixel;
    values[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS;

    for (int i = 0; i < numMonitors; i++) {
        int barX     = monitors[i].x + barStrutsLeft;
        int barY     = monitors[i].y + barStrutsTop;
        int barWidth = monitors[i].width - barStrutsLeft - barStrutsRight;

        barWindows[i] = xcb_generate_id(connection);
        xcb_create_window(connection, XCB_COPY_FROM_PARENT, barWindows[i], root, barX, barY, barWidth, barHeight, barBorderWidth, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          screen->root_visual, mask, values);

        uint32_t or_val = 1;
        xcb_change_window_attributes(connection, barWindows[i], XCB_CW_OVERRIDE_REDIRECT, &or_val);

        xcb_visualtype_t* visual_type = xcb_aux_find_visual_by_id(screen, screen->root_visual);
        bar_contexts[i].surface       = cairo_xcb_surface_create(connection, barWindows[i], visual_type, barWidth, barHeight);
        bar_contexts[i].cr            = cairo_create(bar_contexts[i].surface);

        bar_contexts[i].font_desc = pango_font_description_from_string(barFont);
        bar_contexts[i].layout    = pango_cairo_create_layout(bar_contexts[i].cr);
        pango_layout_set_font_description(bar_contexts[i].layout, bar_contexts[i].font_desc);

        uint32_t struts[12] = {0};

        struts[2] = barY + barHeight + barBorderWidth * 2;

        struts[4] = monitors[i].x;
        struts[5] = monitors[i].x + monitors[i].width - 1;

        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, barWindows[i], NET_WM_STRUT, XCB_ATOM_CARDINAL, 32, 4, struts);

        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, barWindows[i], NET_WM_STRUT_PARTIAL, XCB_ATOM_CARDINAL, 32, 12, struts);

        if (barVisible)
            xcb_map_window(connection, barWindows[i]);
    }

    xcb_flush(connection);
    updateBars();
}

void updateStatus(void) {
    xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, root, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, MAX_STATUS_LENGTH);
    xcb_get_property_reply_t* reply  = xcb_get_property_reply(connection, cookie, NULL);

    if (reply) {
        int len = xcb_get_property_value_length(reply);
        if (len > 0) {
            char* value = (char*)xcb_get_property_value(reply);
            strncpy(statusText, value, sizeof(statusText) - 1);
            statusText[sizeof(statusText) - 1] = '\0';
        }
        free(reply);
    }

    updateBars();
}

static int workspaceHasUrgentWindow(int monitor, int workspace) {
    SClient* client = clients;
    while (client) {
        if (client->monitor == monitor && client->workspace == workspace && client->isUrgent)
            return 1;
        client = client->next;
    }
    return 0;
}

static int workspaceHasClients(int monitor, int workspace) {
    SClient* client = clients;
    while (client) {
        if (client->monitor == monitor && client->workspace == workspace)
            return 1;
        client = client->next;
    }
    return 0;
}

void updateBars(void) {
    if (!barWindows || !barVisible)
        return;

    for (int i = 0; i < numMonitors; i++) {
        if (!barWindows[i] || !bar_contexts[i].cr)
            continue;

        cairo_t* cr = bar_contexts[i].cr;

        double   bg_r = ((barBgColor >> 16) & 0xff) / 255.0;
        double   bg_g = ((barBgColor >> 8) & 0xff) / 255.0;
        double   bg_b = (barBgColor & 0xff) / 255.0;

        cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
        cairo_paint(cr);

        int x = 0;

        int maxTextWidth = 0;
        for (int w = 0; w < workspaceCount; w++) {
            pango_layout_set_text(bar_contexts[i].layout, workspaceNames[w], -1);
            int width, height;
            pango_layout_get_pixel_size(bar_contexts[i].layout, &width, &height);
            if (width > maxTextWidth)
                maxTextWidth = width;
        }

        int wsWidth = maxTextWidth + 16;

        for (int w = 0; w < workspaceCount; w++) {
            if (showOnlyActiveWorkspaces && monitors[i].currentWorkspace != w && !workspaceHasClients(i, w) && !workspaceHasUrgentWindow(i, w))
                continue;

            int         hasUrgent = workspaceHasUrgentWindow(i, w);
            double      r, g, b;
            const char* textColor;

            if (monitors[i].currentWorkspace == w) {
                r         = ((barActiveWsPixel >> 16) & 0xff) / 255.0;
                g         = ((barActiveWsPixel >> 8) & 0xff) / 255.0;
                b         = (barActiveWsPixel & 0xff) / 255.0;
                textColor = barActiveTextColor_str;
            } else if (hasUrgent) {
                r         = ((barUrgentWsPixel >> 16) & 0xff) / 255.0;
                g         = ((barUrgentWsPixel >> 8) & 0xff) / 255.0;
                b         = (barUrgentWsPixel & 0xff) / 255.0;
                textColor = barUrgentTextColor_str;
            } else {
                r         = bg_r;
                g         = bg_g;
                b         = bg_b;
                textColor = barInactiveTextColor_str;
            }

            cairo_set_source_rgb(cr, r, g, b);
            cairo_rectangle(cr, x, 0, wsWidth, barHeight);
            cairo_fill(cr);

            char wsLabel[32];
            snprintf(wsLabel, sizeof(wsLabel), " %s ", workspaceNames[w]);

            pango_layout_set_text(bar_contexts[i].layout, wsLabel, -1);

            int width, height;
            pango_layout_get_pixel_size(bar_contexts[i].layout, &width, &height);

            unsigned int tr, tg, tb;
            if (sscanf(textColor, "#%02x%02x%02x", &tr, &tg, &tb) == 3)
                cairo_set_source_rgb(cr, tr / 255.0, tg / 255.0, tb / 255.0);
            else
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

            int textX = x + (wsWidth - width) / 2;
            int textY = (barHeight - height) / 2;

            cairo_move_to(cr, textX, textY);
            pango_cairo_show_layout(cr, bar_contexts[i].layout);

            x += wsWidth;
        }

        if (statusText[0] != '\0') {
            pango_layout_set_text(bar_contexts[i].layout, statusText, -1);

            int width, height;
            pango_layout_get_pixel_size(bar_contexts[i].layout, &width, &height);

            int          barWidth = monitors[i].width - barStrutsLeft - barStrutsRight;

            unsigned int tr, tg, tb;
            if (sscanf(barStatusTextColor_str, "#%02x%02x%02x", &tr, &tg, &tb) == 3)
                cairo_set_source_rgb(cr, tr / 255.0, tg / 255.0, tb / 255.0);
            else
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

            cairo_move_to(cr, barWidth - width - 5, (barHeight - height) / 2);
            pango_cairo_show_layout(cr, bar_contexts[i].layout);
        }

        cairo_surface_flush(bar_contexts[i].surface);
    }

    xcb_flush(connection);
}

void raiseBars(void) {
    if (!barWindows)
        return;

    for (int i = 0; i < numMonitors; i++) {
        if (barWindows[i]) {
            const uint32_t values[] = {XCB_STACK_MODE_ABOVE};
            xcb_configure_window(connection, barWindows[i], XCB_CONFIG_WINDOW_STACK_MODE, values);
        }
    }
    xcb_flush(connection);
}

void cleanupBars(void) {
    if (barWindows) {
        for (int i = 0; i < numMonitors; i++) {
            if (barWindows[i])
                xcb_destroy_window(connection, barWindows[i]);
        }
        free(barWindows);
        barWindows = NULL;
    }

    if (bar_contexts) {
        for (int i = 0; i < numMonitors; i++) {
            if (bar_contexts[i].layout)
                g_object_unref(bar_contexts[i].layout);
            if (bar_contexts[i].font_desc)
                pango_font_description_free(bar_contexts[i].font_desc);
            if (bar_contexts[i].cr)
                cairo_destroy(bar_contexts[i].cr);
            if (bar_contexts[i].surface)
                cairo_surface_destroy(bar_contexts[i].surface);
        }
        free(bar_contexts);
        bar_contexts = NULL;
    }

    freeWorkspaceNames();
}

void handleBarExpose(xcb_generic_event_t* event) {
    xcb_expose_event_t* ev = (xcb_expose_event_t*)event;

    for (int i = 0; i < numMonitors; i++) {
        if (barWindows[i] == ev->window) {
            if (ev->count == 0)
                updateBars();
            break;
        }
    }
}

void handleBarClick(xcb_generic_event_t* event) {
    xcb_button_press_event_t* ev = (xcb_button_press_event_t*)event;

    if (ev->detail != XCB_BUTTON_INDEX_1)
        return;

    for (int i = 0; i < numMonitors; i++) {
        if (ev->event == barWindows[i]) {
            int x = 0;

            int maxTextWidth = 0;
            for (int w = 0; w < workspaceCount; w++) {
                pango_layout_set_text(bar_contexts[i].layout, workspaceNames[w], -1);
                int width, height;
                pango_layout_get_pixel_size(bar_contexts[i].layout, &width, &height);
                if (width > maxTextWidth)
                    maxTextWidth = width;
            }

            int wsWidth = maxTextWidth + 16;

            if (showOnlyActiveWorkspaces) {
                int clickPos = 0;
                for (int w = 0; w < workspaceCount; w++) {
                    if (monitors[i].currentWorkspace != w && !workspaceHasClients(i, w) && !workspaceHasUrgentWindow(i, w))
                        continue;

                    if (ev->event_x >= clickPos && ev->event_x < clickPos + wsWidth) {
                        monitors[i].currentWorkspace = w;
                        updateClientVisibility();
                        updateBars();
                        break;
                    }
                    clickPos += wsWidth;
                }
            } else {
                for (int w = 0; w < workspaceCount; w++) {
                    if (ev->event_x >= x && ev->event_x < x + wsWidth) {
                        monitors[i].currentWorkspace = w;
                        updateClientVisibility();
                        updateBars();
                        break;
                    }
                    x += wsWidth;
                }
            }

            break;
        }
    }
}

void updateClientPositionsForBar(void) {
    SClient* client = clients;

    while (client) {
        if (!client->isFloating && !client->isFullscreen) {
            SMonitor* m = &monitors[client->monitor];
            if (barVisible) {
                int barBottom = m->y + barStrutsTop + barHeight + barBorderWidth * 2;
                if (client->y < barBottom + outerGap) {
                    client->y = barBottom + outerGap;
                    xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_Y, (const uint32_t[]){client->y});
                }
            } else if (!barVisible && client->y == m->y + barStrutsTop + barHeight + barBorderWidth * 2 + outerGap) {
                client->y = m->y + outerGap;
                xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_Y, (const uint32_t[]){client->y});
            }
        }
        client = client->next;
    }
    xcb_flush(connection);
}
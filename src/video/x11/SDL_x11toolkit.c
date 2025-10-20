/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_X11

#include "../../SDL_list.h"
#include "SDL_x11video.h"
#ifdef SDL_USE_LIBDBUS
#include "../../core/linux/SDL_system_theme.h"
#endif
#ifdef HAVE_FRIBIDI_H
#include "../../core/unix/SDL_fribidi.h"
#endif
#include "SDL_x11dyn.h"
#include "SDL_x11modes.h"
#include "SDL_x11settings.h"
#include "SDL_x11toolkit.h"
#include "xsettings-client.h"
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <locale.h>

#define SDL_SET_LOCALE 1
#define SDL_GRAB       1

typedef struct SDL_ToolkitIconControlX11
{
    SDL_ToolkitControlX11 parent;

    /* Icon type */
    SDL_MessageBoxFlags flags;
    char icon_char;

    /* Font */
    XFontStruct *icon_char_font;
    int icon_char_x;
    int icon_char_y;
    int icon_char_a;
    int icon_char_h;

    /* Colors */
    XColor xcolor_black;
    XColor xcolor_red;
    XColor xcolor_red_darker;
    XColor xcolor_white;
    XColor xcolor_yellow;
    XColor xcolor_blue;
    XColor xcolor_bg_shadow;
} SDL_ToolkitIconControlX11;

typedef struct SDL_ToolkitButtonControlX11
{
    SDL_ToolkitControlX11 parent;

    /* Data */
    const SDL_MessageBoxButtonData *data;

    /* Icon */
    SDL_ToolkitIconX11 icon;

    /* Text */
    SDL_Rect text_rect;
    int text_a;
    int text_d;
    int str_sz;
#ifdef HAVE_FRIBIDI_H
    char *text;
    bool free_text;
#endif

    /* Callback */
    void *cb_data;
    void (*cb)(struct SDL_ToolkitControlX11 *, void *);
} SDL_ToolkitButtonControlX11;

typedef struct SDL_ToolkitLabelControlX11
{
    SDL_ToolkitControlX11 parent;

    char **lines;
    int *y;
    size_t *szs;
    size_t sz;
#ifdef HAVE_FRIBIDI_H
    int *x;
    int *w;
    bool *free_lines;
    FriBidiParType *par_types;
#endif
} SDL_ToolkitLabelControlX11;

typedef struct SDL_ToolkitMenuBarControlX11
{
    SDL_ToolkitControlX11 parent;

    SDL_ListNode *menu_items;
} SDL_ToolkitMenuBarControlX11;

typedef struct SDL_ToolkitMenuControlX11
{
    SDL_ToolkitControlX11 parent;

    SDL_ListNode *menu_items;
    XColor xcolor_check_bg;
} SDL_ToolkitMenuControlX11;

typedef struct SDL_ToolkitSliderControlX11
{
    SDL_ToolkitControlX11 parent;

    bool horiz;
    Pixmap bg;
    SDL_ToolkitControlStateX11 handle_state;
    SDL_Rect handle_rect;
    void *user_data;
    void (*callback)(SDL_ToolkitControlX11 *, void *, int, int, int);
} SDL_ToolkitSliderControlX11;

typedef struct SDL_ToolkitListControlX11
{
    SDL_ToolkitControlX11 parent;

    /* Header */
    const char *header;
    size_t header_sz;
    SDL_Rect header_rect;
    SDL_Rect header_text_rect;

    /* Items */
    SDL_ListNode *items;
    Pixmap item_area;
    SDL_Rect item_area_rect;
    SDL_Rect item_area_reserved_rect;
    bool item_area_rendered;

    /* Colors */
    XColor xcolor_black;
    XColor xcolor_green;
    XColor xcolor_white;
    XColor xcolor_cream;
} SDL_ToolkitListControlX11;

typedef enum SDL_ToolkitEntrySelectionDir
{
    SDL_TOOLKIT_ENTRY_SELECTION_NONE,
    SDL_TOOLKIT_ENTRY_SELECTION_LEFT,
    SDL_TOOLKIT_ENTRY_SELECTION_RIGHT,
} SDL_ToolkitEntrySelectionDir;

typedef struct SDL_ToolkitEntryControlX11
{
    SDL_ToolkitControlX11 parent;

    /* Text */
    char *buffer;
    size_t sz;
    size_t old_sz;
    int text_x;
    int text_y;
    int text_reserved_w;
    int text_a;

    /* Cursor */
    size_t cur;
    size_t old_cur;
    int cur_x;
    int cur_draw_y1;
    int cur_draw_y2;

    /* Cursor blink */
    bool cur_blink;
    SDL_TimerID cur_blink_timer;

    /* Selection */
    SDL_ToolkitEntrySelectionDir sel_dir;
    size_t sel;
    size_t sel_end;
    int sel_x;
    int sel_w;
    int sel_h;
    bool sel_held;
    int sel_held_x;
    size_t sel_held_i;

    /* Paging */
    int start_offset;  // x offset from 0
    int clip_offset;   // x offset from clip_start
    size_t clip_start; // clipped region start
    size_t clip_end;   // clipped region end

    /* Clipboard */
    Atom atom_clip, atom_prop, atom_targets, atom_type;
    char *clip;
    size_t clip_sz;
} SDL_ToolkitEntryControlX11;

/* Font for icon control */
static const char *g_IconFont = "-*-*-bold-r-normal-*-%d-*-*-*-*-*-iso8859-1[33 88 105]";
#define G_ICONFONT_SIZE 22

/* General UI font */
static const char g_ToolkitFontLatin1[] =
    "-*-*-medium-r-normal--0-%d-*-*-p-0-iso8859-1";
static const char g_ToolkitFontLatin1Fallback[] =
    "-*-*-*-*-*--*-*-*-*-*-*-iso8859-1";

static const char *g_ToolkitFont[] = {
    "-*-*-medium-r-normal--*-%d-*-*-*-*-iso10646-1,*", // explicitly unicode (iso10646-1)
    "-*-*-medium-r-*--*-%d-*-*-*-*-iso10646-1,*",      // explicitly unicode (iso10646-1)
    "-misc-*-*-*-*--*-*-*-*-*-*-iso10646-1,*",         // misc unicode (fix for some systems)
    "-*-*-*-*-*--*-*-*-*-*-*-iso10646-1,*",            // just give me anything Unicode.
    "-*-*-medium-r-normal--*-%d-*-*-*-*-iso8859-1,*",  // explicitly latin1, in case low-ASCII works out.
    "-*-*-medium-r-*--*-%d-*-*-*-*-iso8859-1,*",       // explicitly latin1, in case low-ASCII works out.
    "-misc-*-*-*-*--*-*-*-*-*-*-iso8859-1,*",          // misc latin1 (fix for some systems)
    "-*-*-*-*-*--*-*-*-*-*-*-iso8859-1,*",             // just give me anything latin1.
    NULL
};
#define G_TOOLKITFONT_SIZE 140

static const SDL_MessageBoxColor g_default_colors[SDL_MESSAGEBOX_COLOR_COUNT] = {
    { 191, 184, 191 }, // SDL_MESSAGEBOX_COLOR_BACKGROUND,
    { 0, 0, 0 },       // SDL_MESSAGEBOX_COLOR_TEXT,
    { 127, 120, 127 }, // SDL_MESSAGEBOX_COLOR_BUTTON_BORDER,
    { 191, 184, 191 }, // SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND,
    { 235, 235, 235 }, // SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED,
};

#ifdef SDL_USE_LIBDBUS
static const SDL_MessageBoxColor g_default_colors_dark[SDL_MESSAGEBOX_COLOR_COUNT] = {
    { 20, 20, 20 },    // SDL_MESSAGEBOX_COLOR_BACKGROUND,
    { 192, 192, 192 }, // SDL_MESSAGEBOX_COLOR_TEXT,
    { 12, 12, 12 },    // SDL_MESSAGEBOX_COLOR_BUTTON_BORDER,
    { 20, 20, 20 },    // SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND,
    { 36, 36, 36 },    // SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED,
};

#if 0
static const SDL_MessageBoxColor g_default_colors_dark_high_contrast[SDL_MESSAGEBOX_COLOR_COUNT] = {
    { 0, 0, 0 },    // SDL_MESSAGEBOX_COLOR_BACKGROUND,
    { 255, 255, 255 }, // SDL_MESSAGEBOX_COLOR_TEXT,
    { 20, 235, 255 }, // SDL_MESSAGEBOX_COLOR_BUTTON_BORDER,
    { 0, 0, 0 },  // SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND,
    { 125, 5, 125 },  // SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED,
};

static const SDL_MessageBoxColor g_default_colors_light_high_contrast[SDL_MESSAGEBOX_COLOR_COUNT] = {
    { 255, 255, 255 },    // SDL_MESSAGEBOX_COLOR_BACKGROUND,
    { 0, 0, 0 }, // SDL_MESSAGEBOX_COLOR_TEXT,
    { 0, 0, 0 }, // SDL_MESSAGEBOX_COLOR_BUTTON_BORDER,
    { 255, 255, 255 },  // SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND,
    { 20, 230, 255 },  // SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED,
};
#endif

#endif

static int g_shm_error;
static int (*g_old_error_handler)(Display *, XErrorEvent *) = NULL;

static int X11Toolkit_SharedMemoryErrorHandler(Display *d, XErrorEvent *e)
{
    if (e->error_code == BadAccess || e->error_code == BadRequest) {
        g_shm_error = True;
        return 0;
    }
    return g_old_error_handler(d, e);
}

int X11Toolkit_SettingsGetInt(XSettingsClient *client, const char *key, int fallback_value)
{
    XSettingsSetting *setting = NULL;
    int res = fallback_value;

    if (client) {
        if (xsettings_client_get_setting(client, key, &setting) != XSETTINGS_SUCCESS) {
            goto no_key;
        }

        if (setting->type != XSETTINGS_TYPE_INT) {
            goto no_key;
        }

        res = setting->data.v_int;
    }

no_key:
    if (setting) {
        xsettings_setting_free(setting);
    }

    return res;
}

static float X11Toolkit_GetUIScale(XSettingsClient *client, Display *display)
{
    double scale_factor = 0.0;

    // First use the forced scaling factor specified by the app/user
    const char *hint = SDL_GetHint(SDL_HINT_VIDEO_X11_SCALING_FACTOR);
    if (hint && *hint) {
        double value = SDL_atof(hint);
        if (value >= 1.0f && value <= 10.0f) {
            scale_factor = value;
        }
    }

    // If that failed, try "Xft.dpi" from the XResourcesDatabase...
    // We attempt to read this directly to get the live value, XResourceManagerString
    // is cached per display connection.
    if (scale_factor <= 0.0) {
        int status, real_format;
        Atom real_type;
        Atom res_mgr;
        unsigned long items_read, items_left;
        char *resource_manager;
        bool owns_resource_manager = false;

        X11_XrmInitialize();
        res_mgr = X11_XInternAtom(display, "RESOURCE_MANAGER", False);
        status = X11_XGetWindowProperty(display, RootWindow(display, DefaultScreen(display)),
                                        res_mgr, 0L, 8192L, False, XA_STRING,
                                        &real_type, &real_format, &items_read, &items_left,
                                        (unsigned char **)&resource_manager);

        if (status == Success && resource_manager) {
            owns_resource_manager = true;
        } else {
            // Fall back to XResourceManagerString. This will not be updated if the
            // dpi value is later changed but should allow getting the initial value.
            resource_manager = X11_XResourceManagerString(display);
        }

        if (resource_manager) {
            XrmDatabase db;
            XrmValue value;
            char *type;

            db = X11_XrmGetStringDatabase(resource_manager);

            // Get the value of Xft.dpi from the Database
            if (X11_XrmGetResource(db, "Xft.dpi", "String", &type, &value)) {
                if (value.addr && type && SDL_strcmp(type, "String") == 0) {
                    int dpi = SDL_atoi(value.addr);
                    scale_factor = dpi / 96.0;
                }
            }
            X11_XrmDestroyDatabase(db);

            if (owns_resource_manager) {
                X11_XFree(resource_manager);
            }
        }
    }

    // If that failed, try the XSETTINGS keys...
    if (scale_factor <= 0.0) {
        scale_factor = X11Toolkit_SettingsGetInt(client, "Gdk/WindowScalingFactor", -1);

        // The Xft/DPI key is stored in increments of 1024th
        if (scale_factor <= 0.0) {
            int dpi = X11Toolkit_SettingsGetInt(client, "Xft/DPI", -1);
            if (dpi > 0) {
                scale_factor = (double)dpi / 1024.0;
                scale_factor /= 96.0;
            }
        }
    }

    // If that failed, try the GDK_SCALE envvar...
    if (scale_factor <= 0.0) {
        const char *scale_str = SDL_getenv("GDK_SCALE");
        if (scale_str) {
            scale_factor = SDL_atoi(scale_str);
        }
    }

    // Nothing or a bad value, just fall back to 1.0
    if (scale_factor <= 0.0) {
        scale_factor = 1.0;
    }

    return (float)scale_factor;
}

static void X11Toolkit_InitWindowPixmap(SDL_ToolkitWindowX11 *data)
{
    if (data->pixmap) {
#ifndef NO_SHARED_MEMORY
        if (!data->shm_pixmap) {
            data->drawable = X11_XCreatePixmap(data->display, data->window, data->pixmap_width, data->pixmap_height, data->depth);
        }
#else
        data->drawable = X11_XCreatePixmap(data->display, data->window, data->pixmap_width, data->pixmap_height, data->depth);
#endif
#ifndef NO_SHARED_MEMORY
        if (data->shm) {
            data->image = X11_XShmCreateImage(data->display, data->visual, data->depth, ZPixmap, NULL, &data->shm_info, data->pixmap_width, data->pixmap_height);
            if (data->image) {
                data->shm_bytes_per_line = data->image->bytes_per_line;

                data->shm_info.shmid = shmget(IPC_PRIVATE, data->image->bytes_per_line * data->image->height, IPC_CREAT | 0777);
                if (data->shm_info.shmid < 0) {
                    XDestroyImage(data->image);
                    data->image = NULL;
                    data->shm = false;
                }

                data->shm_info.readOnly = False;
                data->shm_info.shmaddr = data->image->data = (char *)shmat(data->shm_info.shmid, 0, 0);
                if (((signed char *)data->shm_info.shmaddr) == (signed char *)-1) {
                    XDestroyImage(data->image);
                    data->shm = false;
                    data->image = NULL;
                }

                g_shm_error = False;
                g_old_error_handler = X11_XSetErrorHandler(X11Toolkit_SharedMemoryErrorHandler);
                X11_XShmAttach(data->display, &data->shm_info);
                X11_XSync(data->display, False);
                X11_XSetErrorHandler(g_old_error_handler);
                if (g_shm_error) {
                    XDestroyImage(data->image);
                    shmdt(data->shm_info.shmaddr);
                    shmctl(data->shm_info.shmid, IPC_RMID, 0);
                    data->image = NULL;
                    data->shm = false;
                }

                if (data->shm_pixmap) {
                    data->drawable = X11_XShmCreatePixmap(data->display, data->window, data->shm_info.shmaddr, &data->shm_info, data->pixmap_width, data->pixmap_height, data->depth);
                    if (data->drawable == None) {
                        data->shm_pixmap = False;
                    } else {
                        XDestroyImage(data->image);
                        data->image = NULL;
                    }
                }

                shmctl(data->shm_info.shmid, IPC_RMID, 0);
            } else {
                data->shm = false;
            }
        }
#endif
    }
}

static void X11Toolkit_InitWindowFonts(SDL_ToolkitWindowX11 *window)
{
#ifdef X_HAVE_UTF8_STRING
    window->utf8 = true;
    window->font_set = NULL;
    if (SDL_X11_HAVE_UTF8) {
        char **missing = NULL;
        int num_missing = 0;
        int i_font;
        window->font_struct = NULL;
        for (i_font = 0; g_ToolkitFont[i_font]; ++i_font) {
            char *font;

            if (SDL_strstr(g_ToolkitFont[i_font], "%d")) {
            try_load_font:
                SDL_asprintf(&font, g_ToolkitFont[i_font], G_TOOLKITFONT_SIZE * window->iscale);
                window->font_set = X11_XCreateFontSet(window->display, font, &missing, &num_missing, NULL);
                SDL_free(font);

                if (!window->font_set) {
                    if (window->scale != 0 && window->iscale > 0) {
                        window->iscale = (int)SDL_ceilf(window->scale);
                        window->scale = 0;
                    } else {
                        window->iscale--;
                    }
                    goto try_load_font;
                }
            } else {
                window->font_set = X11_XCreateFontSet(window->display, g_ToolkitFont[i_font], &missing, &num_missing, NULL);
            }

            if (missing) {
                X11_XFreeStringList(missing);
            }

            if (window->font_set) {
                break;
            }
        }

        if (!window->font_set) {
            goto load_font_traditional;
        } else {
#ifdef HAVE_FRIBIDI_H
            window->do_shaping = !X11_XContextDependentDrawing(window->font_set);
#endif
        }
    } else
#endif
    {
        char *font;

    load_font_traditional:
        window->utf8 = false;
        SDL_asprintf(&font, g_ToolkitFontLatin1, G_TOOLKITFONT_SIZE * window->iscale);
        window->font_struct = X11_XLoadQueryFont(window->display, font);
        SDL_free(font);
        if (!window->font_struct) {
            if (window->iscale > 0) {
                if (window->scale != 0) {
                    window->iscale = (int)SDL_ceilf(window->scale);
                    window->scale = 0;
                } else {
                    window->iscale--;
                }
                goto load_font_traditional;
            } else {
                window->font_struct = X11_XLoadQueryFont(window->display, g_ToolkitFontLatin1Fallback);
            }
        }
    }
}

static void X11Toolkit_SettingsNotify(const char *name, XSettingsAction action, XSettingsSetting *setting, void *data)
{
    SDL_ToolkitWindowX11 *window;
    int i;

    window = data;

    if (window->xsettings_first_time) {
        return;
    }

    if (SDL_strcmp(name, SDL_XSETTINGS_GDK_WINDOW_SCALING_FACTOR) == 0 ||
        SDL_strcmp(name, SDL_XSETTINGS_GDK_UNSCALED_DPI) == 0 ||
        SDL_strcmp(name, SDL_XSETTINGS_XFT_DPI) == 0) {
        bool dbe_already_setup = false;
        bool pixmap_already_setup = false;

        if (window->pixmap) {
            pixmap_already_setup = true;
        } else {
            dbe_already_setup = true;
        }

        /* set scale vars */
        window->scale = X11Toolkit_GetUIScale(window->xsettings, window->display);
        window->iscale = (int)SDL_ceilf(window->scale);
        if (SDL_roundf(window->scale) == window->scale) {
            window->scale = 0;
        }

        /* setup fonts */
#ifdef X_HAVE_UTF8_STRING
        if (window->font_set) {
            X11_XFreeFontSet(window->display, window->font_set);
        }
#endif
        if (window->font_struct) {
            X11_XFreeFont(window->display, window->font_struct);
        }

        X11Toolkit_InitWindowFonts(window);

        /* set up window */
        if (window->scale != 0) {
            window->window_width = SDL_lroundf((window->window_width / window->iscale) * window->scale);
            window->window_height = SDL_lroundf((window->window_height / window->iscale) * window->scale);
            window->pixmap_width = window->window_width;
            window->pixmap_height = window->window_height;
            window->pixmap = true;
        } else {
            window->pixmap = false;
        }

        if (window->pixmap) {
            if (!pixmap_already_setup) {
#ifdef SDL_VIDEO_DRIVER_X11_XDBE
                if (SDL_X11_HAVE_XDBE && window->xdbe) {
                    X11_XdbeDeallocateBackBufferName(window->display, window->buf);
                }
#endif
            }
            X11_XFreePixmap(window->display, window->drawable);
            X11Toolkit_InitWindowPixmap(window);
        } else {
            if (!dbe_already_setup) {
                X11_XFreePixmap(window->display, window->drawable);
#ifndef NO_SHARED_MEMORY
                if (window->image) {
                    XDestroyImage(window->image);
                    window->image = NULL;
                }
#endif
#ifdef SDL_VIDEO_DRIVER_X11_XDBE
                if (SDL_X11_HAVE_XDBE && window->xdbe) {
                    window->buf = X11_XdbeAllocateBackBufferName(window->display, window->window, XdbeUndefined);
                    window->drawable = window->buf;
                }
#endif
            }
        }

        /* notify controls */
        for (i = 0; i < window->controls_sz; i++) {
            window->controls[i]->do_size = true;

            if (window->controls[i]->func_on_scale_change) {
                window->controls[i]->func_on_scale_change(window->controls[i]);
            }

            if (window->controls[i]->func_calc_size) {
                window->controls[i]->func_calc_size(window->controls[i]);
            }

            window->controls[i]->do_size = false;
        }

        /* notify cb */
        if (window->cb_on_scale_change) {
            window->cb_on_scale_change(window, window->cb_data);
        }

        /* update ev scales */
        if (!window->pixmap) {
            window->ev_scale = window->ev_iscale = 1;
        } else {
            window->ev_scale = window->scale;
            window->ev_iscale = window->iscale;
        }
    }
}

static void X11Toolkit_GetTextWidthHeightForFont(XFontStruct *font, const char *str, int nbytes, int *pwidth, int *pheight, int *ascent)
{
    XCharStruct text_structure;
    int font_direction, font_ascent, font_descent;
    X11_XTextExtents(font, str, nbytes,
                     &font_direction, &font_ascent, &font_descent,
                     &text_structure);
    *pwidth = text_structure.width;
    *pheight = text_structure.ascent + text_structure.descent;
    *ascent = text_structure.ascent;
}

static void X11Toolkit_GetTextWidthHeight(SDL_ToolkitWindowX11 *data, const char *str, int nbytes, int *pwidth, int *pheight, int *ascent, int *descent, int *font_height)
{
#ifdef X_HAVE_UTF8_STRING
    if (data->utf8) {
        if (str) {
            XRectangle overall_ink, overall_logical;

            X11_Xutf8TextExtents(data->font_set, str, nbytes, &overall_ink, &overall_logical);
            *pwidth = overall_logical.width;
            *pheight = overall_logical.height;
            *ascent = -overall_logical.y;
            *descent = overall_logical.height - *ascent;
        }

        if (font_height) {
            XFontSetExtents *extents;

            extents = X11_XExtentsOfFontSet(data->font_set);
            *font_height = extents->max_logical_extent.height;
        }
    } else
#endif
    {
        int font_ascent, font_descent;

        if (str) {
            XCharStruct text_structure;
            int font_direction;
            X11_XTextExtents(data->font_struct, str, nbytes,
                             &font_direction, &font_ascent, &font_descent,
                             &text_structure);
            *pwidth = text_structure.width;
            *pheight = text_structure.ascent + text_structure.descent;
            *ascent = text_structure.ascent;
            *descent = text_structure.descent;
        } else {
            font_ascent = data->font_struct->ascent;
            font_descent = data->font_struct->descent;
        }

        if (font_height) {
            *font_height = font_ascent + font_descent;
        }
    }
}

SDL_ToolkitWindowX11 *X11Toolkit_CreateWindowStruct(SDL_Window *parent, SDL_ToolkitWindowX11 *tkparent, SDL_ToolkitWindowModeX11 mode, const SDL_MessageBoxColor *colorhints, bool create_new_display)
{
    SDL_ToolkitWindowX11 *window;
    int i;
#ifdef SDL_USE_LIBDBUS
    SDL_SystemTheme theme;
#endif
#define ErrorFreeRetNull(x, y) \
    SDL_SetError(x);           \
    SDL_free(y);               \
    return NULL;
#define ErrorCloseFreeRetNull(x, y, z) \
    X11_XCloseDisplay(z->display);     \
    SDL_SetError(x, y);                \
    SDL_free(z);                       \
    return NULL;

    if (!SDL_X11_LoadSymbols()) {
        return NULL;
    }

    // This code could get called from multiple threads maybe?
    X11_XInitThreads();

    window = (SDL_ToolkitWindowX11 *)SDL_malloc(sizeof(SDL_ToolkitWindowX11));
    if (!window) {
        SDL_SetError("Unable to allocate toolkit window structure");
        return NULL;
    }

    window->mode = mode;
    window->tk_parent = tkparent;

#if SDL_SET_LOCALE
    if (mode == SDL_TOOLKIT_WINDOW_MODE_X11_DIALOG) {
        window->origlocale = setlocale(LC_ALL, NULL);
        if (window->origlocale) {
            window->origlocale = SDL_strdup(window->origlocale);
            if (!window->origlocale) {
                return NULL;
            }
            (void)setlocale(LC_ALL, "");
        }
    }
#endif

    window->parent_device = NULL;
    if (create_new_display) {
        window->display = X11_XOpenDisplay(NULL);
        window->display_close = true;
        if (!window->display) {
            ErrorFreeRetNull("Couldn't open X11 display", window);
        }
    } else {
        if (parent) {
            window->parent_device = SDL_GetVideoDevice();
            window->display = window->parent_device->internal->display;
            window->display_close = false;
        } else if (tkparent) {
            window->display = tkparent->display;
            window->display_close = false;
        } else {
            window->display = X11_XOpenDisplay(NULL);
            window->display_close = true;
            if (!window->display) {
                ErrorFreeRetNull("Couldn't open X11 display", window);
            }
        }
    }

#ifdef SDL_VIDEO_DRIVER_X11_XRANDR
    int xrandr_event_base, xrandr_error_base;
    window->xrandr = X11_XRRQueryExtension(window->display, &xrandr_event_base, &xrandr_error_base);
#endif

#ifndef NO_SHARED_MEMORY
    window->shm_pixmap = False;
    window->shm = X11_XShmQueryExtension(window->display) ? SDL_X11_HAVE_SHM : false;
    if (window->shm) {
        int major;
        int minor;

        X11_XShmQueryVersion(window->display, &major, &minor, &window->shm_pixmap);
        if (window->shm_pixmap) {
            if (X11_XShmPixmapFormat(window->display) != ZPixmap) {
                window->shm_pixmap = False;
            }
        }
    }
#endif

    /* Scale/Xsettings */
    window->pixmap = false;
    window->xsettings_first_time = true;
    window->xsettings = xsettings_client_new(window->display, DefaultScreen(window->display), X11Toolkit_SettingsNotify, NULL, window);
    window->xsettings_first_time = false;
    window->scale = X11Toolkit_GetUIScale(window->xsettings, window->display);
    window->iscale = (int)SDL_ceilf(window->scale);
    if (SDL_roundf(window->scale) == window->scale) {
        window->scale = 0;
    }

    /* Fonts */
    X11Toolkit_InitWindowFonts(window);

    /* Color hints */
#ifdef SDL_USE_LIBDBUS
    theme = SDL_SYSTEM_THEME_LIGHT;
    if (SDL_SystemTheme_Init()) {
        theme = SDL_SystemTheme_Get();
    }
#endif

    if (!colorhints) {
#ifdef SDL_USE_LIBDBUS
        switch (theme) {
        case SDL_SYSTEM_THEME_DARK:
            colorhints = g_default_colors_dark;
            break;
#if 0
            case SDL_SYSTEM_THEME_LIGHT_HIGH_CONTRAST:
                colorhints = g_default_colors_light_high_contrast;
                break;
            case SDL_SYSTEM_THEME_DARK_HIGH_CONTRAST:
                colorhints = g_default_colors_dark_high_contrast;
                break;
#endif
        default:
            colorhints = g_default_colors;
        }
#else
        colorhints = g_default_colors;
#endif
    }
    window->color_hints = colorhints;

    /* Convert colors to 16 bpc XColor format */
    for (i = 0; i < SDL_MESSAGEBOX_COLOR_COUNT; i++) {
        window->xcolor[i].flags = DoRed | DoGreen | DoBlue;
        window->xcolor[i].red = colorhints[i].r * 257;
        window->xcolor[i].green = colorhints[i].g * 257;
        window->xcolor[i].blue = colorhints[i].b * 257;
    }

    /* Generate bevel and pressed colors */
    window->xcolor_bevel_l1.flags = DoRed | DoGreen | DoBlue;
    window->xcolor_bevel_l1.red = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].red + 12500, 0, 65535);
    window->xcolor_bevel_l1.green = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].green + 12500, 0, 65535);
    window->xcolor_bevel_l1.blue = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].blue + 12500, 0, 65535);

    window->xcolor_bevel_l2.flags = DoRed | DoGreen | DoBlue;
    window->xcolor_bevel_l2.red = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].red + 32500, 0, 65535);
    window->xcolor_bevel_l2.green = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].green + 32500, 0, 65535);
    window->xcolor_bevel_l2.blue = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].blue + 32500, 0, 65535);

    window->xcolor_bevel_d.flags = DoRed | DoGreen | DoBlue;
    window->xcolor_bevel_d.red = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].red - 22500, 0, 65535);
    window->xcolor_bevel_d.green = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].green - 22500, 0, 65535);
    window->xcolor_bevel_d.blue = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].blue - 22500, 0, 65535);

    window->xcolor_pressed.flags = DoRed | DoGreen | DoBlue;
    window->xcolor_pressed.red = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND].red - 12500, 0, 65535);
    window->xcolor_pressed.green = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND].green - 12500, 0, 65535);
    window->xcolor_pressed.blue = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND].blue - 12500, 0, 65535);

    window->xcolor_disabled_text.flags = DoRed | DoGreen | DoBlue;
    window->xcolor_disabled_text.red = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].red + 19500, 0, 65535);
    window->xcolor_disabled_text.green = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].green + 19500, 0, 65535);
    window->xcolor_disabled_text.blue = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].blue + 19500, 0, 65535);

    window->xcolor_light_control_bg.flags = DoRed | DoGreen | DoBlue;
    window->xcolor_light_control_bg.red = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].red + 19500, 0, 65535);
    window->xcolor_light_control_bg.green = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].green + 19500, 0, 65535);
    window->xcolor_light_control_bg.blue = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].blue + 19500, 0, 65535);

    window->xcolor_light_control_selection.flags = DoRed | DoGreen | DoBlue;
    window->xcolor_light_control_selection.blue = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].blue + 22050, 0, 65535);
    window->xcolor_light_control_selection.green = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].green - 22050, 0, 65535);
    window->xcolor_light_control_selection.red = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].red - 22050, 0, 65535);

    window->xcolor_light_control_selection_text.flags = DoRed | DoGreen | DoBlue;
    window->xcolor_light_control_selection_text.red = SDL_clamp(65535 - window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].red, 0, 65535);
    window->xcolor_light_control_selection_text.green = SDL_clamp(65535 - window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].green, 0, 65535);
    window->xcolor_light_control_selection_text.blue = SDL_clamp(65535 - window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].blue, 0, 65535);

    /* Screen */
    window->parent = parent;
    if (parent) {
        SDL_DisplayData *displaydata = SDL_GetDisplayDriverDataForWindow(parent);
        window->screen = displaydata->screen;
    } else {
        window->screen = DefaultScreen(window->display);
    }

    /* Visuals */
    if (mode == SDL_TOOLKIT_WINDOW_MODE_X11_CHILD) {
        window->visual = parent->internal->visual;
        window->cmap = parent->internal->colormap;
        X11_GetVisualInfoFromVisual(window->display, window->visual, &window->vi);
        window->depth = window->vi.depth;
    } else {
        window->visual = DefaultVisual(window->display, window->screen);
        window->cmap = DefaultColormap(window->display, window->screen);
        window->depth = DefaultDepth(window->display, window->screen);
        X11_GetVisualInfoFromVisual(window->display, window->visual, &window->vi);
    }

    /* Allocate colors */
    for (i = 0; i < SDL_MESSAGEBOX_COLOR_COUNT; i++) {
        X11_XAllocColor(window->display, window->cmap, &window->xcolor[i]);
    }
    X11_XAllocColor(window->display, window->cmap, &window->xcolor_bevel_l1);
    X11_XAllocColor(window->display, window->cmap, &window->xcolor_bevel_l2);
    X11_XAllocColor(window->display, window->cmap, &window->xcolor_bevel_d);
    X11_XAllocColor(window->display, window->cmap, &window->xcolor_pressed);
    X11_XAllocColor(window->display, window->cmap, &window->xcolor_disabled_text);
    X11_XAllocColor(window->display, window->cmap, &window->xcolor_light_control_bg);
    X11_XAllocColor(window->display, window->cmap, &window->xcolor_light_control_selection);
    X11_XAllocColor(window->display, window->cmap, &window->xcolor_light_control_selection_text);

    /* Control list */
    window->has_focus = false;
    window->controls = NULL;
    window->controls_sz = 0;
    window->dyn_controls_sz = 0;
    window->fiddled_control = NULL;
    window->dyn_controls = NULL;
    window->focused_control = NULL;
    window->dyn_controls_non_capturing_sz = 0;
    window->dyn_controls_non_capturing = NULL;

    /* Menu windows */
    window->popup_windows = NULL;

    /* BIDI Engine*/
#ifdef HAVE_FRIBIDI_H
    window->fribidi = SDL_FriBidi_Create();
#endif

    /* XIM */
#ifdef X_HAVE_UTF8_STRING
    if (window->utf8) {
        char *prev_xmods;

        if (mode != SDL_TOOLKIT_WINDOW_MODE_X11_DIALOG) {
            window->origlocale = setlocale(LC_ALL, NULL);

            if (window->origlocale) {
                window->origlocale = SDL_strdup(window->origlocale);
            }

            (void)setlocale(LC_ALL, "");
        }

        prev_xmods = X11_XSetLocaleModifiers(NULL);
        if (prev_xmods) {
            prev_xmods = SDL_strdup(prev_xmods);
        }
        X11_XSetLocaleModifiers("");

        window->im = X11_XOpenIM(window->display, NULL, NULL, NULL);

        if (mode != SDL_TOOLKIT_WINDOW_MODE_X11_DIALOG) {
            (void)setlocale(LC_ALL, window->origlocale);
            if (window->origlocale) {
                SDL_free(window->origlocale);
            }
        }

        X11_XSetLocaleModifiers(prev_xmods);
        if (prev_xmods) {
            SDL_free(prev_xmods);
        }
    }
#endif

    /* Cursors */
#ifdef SDL_VIDEO_DRIVER_X11_XCURSOR
    if (SDL_X11_HAVE_XCURSOR) {
        window->cursor_normal = X11_XcursorLibraryLoadCursor(window->display, "default");
        window->cursor_text_edit = X11_XcursorLibraryLoadCursor(window->display, "text");
    } else
#endif
    {
        window->cursor_normal = X11_XCreateFontCursor(window->display, XC_left_ptr);
        window->cursor_text_edit = X11_XCreateFontCursor(window->display, XC_xterm);
    }

    return window;
}

static void X11Toolkit_AddControlToWindow(SDL_ToolkitWindowX11 *window, SDL_ToolkitControlX11 *control)
{
    /* Add to controls list */
    window->controls_sz++;
    if (window->controls_sz == 1) {
        window->controls = (struct SDL_ToolkitControlX11 **)SDL_malloc(sizeof(struct SDL_ToolkitControlX11 *));
    } else {
        window->controls = (struct SDL_ToolkitControlX11 **)SDL_realloc(window->controls, sizeof(struct SDL_ToolkitControlX11 *) * window->controls_sz);
    }
    window->controls[window->controls_sz - 1] = control;

    /* If dynamic, add it to the dynamic controls list too */
    if (control->dynamic) {
        window->dyn_controls_sz++;
        if (window->dyn_controls_sz == 1) {
            window->dyn_controls = (struct SDL_ToolkitControlX11 **)SDL_malloc(sizeof(struct SDL_ToolkitControlX11 *));
        } else {
            window->dyn_controls = (struct SDL_ToolkitControlX11 **)SDL_realloc(window->dyn_controls, sizeof(struct SDL_ToolkitControlX11 *) * window->dyn_controls_sz);
        }
        window->dyn_controls[window->dyn_controls_sz - 1] = control;
    }

    if (control->dynamic && !control->captures_lr_arrows) {
        window->dyn_controls_non_capturing_sz++;
        if (window->dyn_controls_non_capturing_sz == 1) {
            window->dyn_controls_non_capturing = (struct SDL_ToolkitControlX11 **)SDL_malloc(sizeof(struct SDL_ToolkitControlX11 *));
        } else {
            window->dyn_controls_non_capturing = (struct SDL_ToolkitControlX11 **)SDL_realloc(window->dyn_controls_non_capturing, sizeof(struct SDL_ToolkitControlX11 *) * window->dyn_controls_non_capturing_sz);
        }
        window->dyn_controls_non_capturing[window->dyn_controls_non_capturing_sz - 1] = control;
    }

    /* If selected, set currently focused control to it */
    if (control->selected) {
        window->focused_control = control;
    }
}

bool X11Toolkit_CreateWindowRes(SDL_ToolkitWindowX11 *data, int w, int h, int cx, int cy, char *title)
{
    int x, y;
    XSizeHints *sizehints;
    XSetWindowAttributes wnd_attr;
    Atom _NET_WM_WINDOW_TYPE, _NET_WM_WINDOW_TYPE_DIALOG, _NET_WM_WINDOW_TYPE_DROPDOWN_MENU, _NET_WM_WINDOW_TYPE_TOOLTIP;
    SDL_WindowData *windowdata = NULL;
    Display *display = data->display;
    XGCValues ctx_vals;
    Window root_win;
    Window parent_win;
    unsigned long gcflags = GCForeground | GCBackground;
    unsigned long valuemask;
#ifdef SDL_VIDEO_DRIVER_X11_XRANDR
#ifdef XRANDR_DISABLED_BY_DEFAULT
    const bool use_xrandr_by_default = false;
#else
    const bool use_xrandr_by_default = true;
#endif
#endif

    if (data->scale == 0) {
        data->window_width = w;
        data->window_height = h;
    } else {
        data->window_width = SDL_lroundf((w / data->iscale) * data->scale);
        data->window_height = SDL_lroundf((h / data->iscale) * data->scale);
        data->pixmap_width = w;
        data->pixmap_height = h;
        data->pixmap = true;
    }

    if (data->parent) {
        windowdata = data->parent->internal;
    }

    valuemask = CWEventMask | CWColormap;
    data->event_mask = ExposureMask |
                       ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask |
                       StructureNotifyMask | FocusChangeMask | PointerMotionMask;
    wnd_attr.event_mask = data->event_mask;
    wnd_attr.colormap = data->cmap;
    if (data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_MENU || data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_TOOLTIP) {
        valuemask |= CWOverrideRedirect | CWSaveUnder;
        wnd_attr.save_under = True;
        wnd_attr.override_redirect = True;
    }
    root_win = RootWindow(display, data->screen);
    if (data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_CHILD) {
        parent_win = windowdata->xwindow;
    } else {
        parent_win = root_win;
    }

    data->window = X11_XCreateWindow(
        display, parent_win,
        0, 0,
        data->window_width, data->window_height,
        0, data->depth, InputOutput, data->visual,
        valuemask, &wnd_attr);
    if (data->window == None) {
        return SDL_SetError("Couldn't create X window");
    }

    X11_XDefineCursor(display, data->window, data->cursor_normal);

#ifdef X_HAVE_UTF8_STRING
    if (data->utf8 && data->im) {
        unsigned long ic_mask;

        data->ic = X11_XCreateIC(data->im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, data->window, XNFocusWindow, data->window, NULL);
        X11_XGetICValues(data->ic, XNFilterEvents, &ic_mask, NULL);
        X11_XSelectInput(display, data->window, ic_mask | data->event_mask);
        X11_XSetICFocus(data->ic);
    }
#endif

    if (windowdata && data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_DIALOG) {
        Atom _NET_WM_STATE = X11_XInternAtom(display, "_NET_WM_STATE", False);
        Atom stateatoms[16];
        size_t statecount = 0;
        // Set some message-boxy window states when attached to a parent window...
        // we skip the taskbar since this will pop to the front when the parent window is clicked in the taskbar, etc
        stateatoms[statecount++] = X11_XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
        stateatoms[statecount++] = X11_XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False);
        stateatoms[statecount++] = X11_XInternAtom(display, "_NET_WM_STATE_FOCUSED", False);
        stateatoms[statecount++] = X11_XInternAtom(display, "_NET_WM_STATE_MODAL", False);
        SDL_assert(statecount <= SDL_arraysize(stateatoms));
        X11_XChangeProperty(display, data->window, _NET_WM_STATE, XA_ATOM, 32,
                            PropModeReplace, (unsigned char *)stateatoms, statecount);
    }

    if (windowdata && data->mode != SDL_TOOLKIT_WINDOW_MODE_X11_CHILD) {
        X11_XSetTransientForHint(display, data->window, windowdata->xwindow);
    }

    if (data->tk_parent) {
        X11_XSetTransientForHint(display, data->window, data->tk_parent->window);
    }

    SDL_X11_SetWindowTitle(display, data->window, title);

    // Let the window manager the type of the window
    if (data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_DIALOG) {
        _NET_WM_WINDOW_TYPE = X11_XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
        _NET_WM_WINDOW_TYPE_DIALOG = X11_XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
        X11_XChangeProperty(display, data->window, _NET_WM_WINDOW_TYPE, XA_ATOM, 32,
                            PropModeReplace,
                            (unsigned char *)&_NET_WM_WINDOW_TYPE_DIALOG, 1);
    } else if (data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_MENU) {
        _NET_WM_WINDOW_TYPE = X11_XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
        _NET_WM_WINDOW_TYPE_DROPDOWN_MENU = X11_XInternAtom(display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False);
        X11_XChangeProperty(display, data->window, _NET_WM_WINDOW_TYPE, XA_ATOM, 32,
                            PropModeReplace,
                            (unsigned char *)&_NET_WM_WINDOW_TYPE_DROPDOWN_MENU, 1);
    } else if (data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_TOOLTIP) {
        _NET_WM_WINDOW_TYPE = X11_XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
        _NET_WM_WINDOW_TYPE_TOOLTIP = X11_XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
        X11_XChangeProperty(display, data->window, _NET_WM_WINDOW_TYPE, XA_ATOM, 32,
                            PropModeReplace,
                            (unsigned char *)&_NET_WM_WINDOW_TYPE_TOOLTIP, 1);
    }

    // Allow the window to be deleted by the window manager
    data->wm_delete_message = X11_XInternAtom(display, "WM_DELETE_WINDOW", False);
    X11_XSetWMProtocols(display, data->window, &data->wm_delete_message, 1);
    data->wm_protocols = X11_XInternAtom(display, "WM_PROTOCOLS", False);

    if (data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_MENU || data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_TOOLTIP) {
        x = cx;
        y = cy;
        goto MOVEWINDOW;
    }
    if (windowdata) {
        XWindowAttributes attrib;
        Window dummy;

        X11_XGetWindowAttributes(display, windowdata->xwindow, &attrib);
        x = attrib.x + (attrib.width - data->window_width) / 2;
        y = attrib.y + (attrib.height - data->window_height) / 3;
        X11_XTranslateCoordinates(display, windowdata->xwindow, RootWindow(display, data->screen), x, y, &x, &y, &dummy);
    } else {
        const SDL_VideoDevice *dev = SDL_GetVideoDevice();
        if (dev && dev->displays && dev->num_displays > 0) {
            const SDL_VideoDisplay *dpy = dev->displays[0];
            const SDL_DisplayData *dpydata = dpy->internal;
            x = dpydata->x + ((dpy->current_mode->w - data->window_width) / 2);
            y = dpydata->y + ((dpy->current_mode->h - data->window_height) / 3);
        }
#ifdef SDL_VIDEO_DRIVER_X11_XRANDR
        else if (SDL_GetHintBoolean(SDL_HINT_VIDEO_X11_XRANDR, use_xrandr_by_default) && data->xrandr) {
            XRRScreenResources *screen_res;
            XRRCrtcInfo *crtc_info;
            RROutput default_out;

            screen_res = X11_XRRGetScreenResourcesCurrent(display, root_win);
            if (!screen_res) {
                goto NOXRANDR;
            }

            default_out = X11_XRRGetOutputPrimary(display, root_win);
            if (default_out != None) {
                XRROutputInfo *out_info;

                out_info = X11_XRRGetOutputInfo(display, screen_res, default_out);
                if (out_info->connection != RR_Connected) {
                    X11_XRRFreeOutputInfo(out_info);
                    goto FIRSTOUTPUTXRANDR;
                }

                if (out_info->crtc != None) {
                    crtc_info = X11_XRRGetCrtcInfo(display, screen_res, out_info->crtc);
                } else if (out_info->ncrtc > 0) {
                    crtc_info = X11_XRRGetCrtcInfo(display, screen_res, out_info->crtcs[0]);
                } else {
                    crtc_info = NULL;
                }

                if (crtc_info) {
                    x = (crtc_info->width - data->window_width) / 2;
                    y = (crtc_info->height - data->window_height) / 3;
                    X11_XRRFreeOutputInfo(out_info);
                    X11_XRRFreeCrtcInfo(crtc_info);
                    X11_XRRFreeScreenResources(screen_res);
                } else {
                    X11_XRRFreeOutputInfo(out_info);
                    goto NOXRANDR;
                }
            } else {
            FIRSTOUTPUTXRANDR:
                if (screen_res->noutput > 0) {
                    XRROutputInfo *out_info;

                    out_info = X11_XRRGetOutputInfo(display, screen_res, screen_res->outputs[0]);
                    if (!out_info) {
                        goto FIRSTCRTCXRANDR;
                    }

                    if (out_info->crtc != None) {
                        crtc_info = X11_XRRGetCrtcInfo(display, screen_res, out_info->crtc);
                    } else if (out_info->ncrtc > 0) {
                        crtc_info = X11_XRRGetCrtcInfo(display, screen_res, out_info->crtcs[0]);
                    } else {
                        crtc_info = NULL;
                    }

                    if (!crtc_info) {
                        X11_XRRFreeOutputInfo(out_info);
                        goto FIRSTCRTCXRANDR;
                    }

                    x = (crtc_info->width - data->window_width) / 2;
                    y = (crtc_info->height - data->window_height) / 3;
                    X11_XRRFreeOutputInfo(out_info);
                    X11_XRRFreeCrtcInfo(crtc_info);
                    X11_XRRFreeScreenResources(screen_res);
                    goto MOVEWINDOW;
                }

            FIRSTCRTCXRANDR:
                if (!screen_res->ncrtc) {
                    X11_XRRFreeScreenResources(screen_res);
                    goto NOXRANDR;
                }

                crtc_info = X11_XRRGetCrtcInfo(display, screen_res, screen_res->crtcs[0]);
                if (crtc_info) {
                    x = (crtc_info->width - data->window_width) / 2;
                    y = (crtc_info->height - data->window_height) / 3;
                    X11_XRRFreeCrtcInfo(crtc_info);
                    X11_XRRFreeScreenResources(screen_res);
                } else {
                    X11_XRRFreeScreenResources(screen_res);
                    goto NOXRANDR;
                }
            }
        }
#endif
        else {
        // oh well. This will misposition on a multi-head setup. Init first next time.
        NOXRANDR:
            x = (DisplayWidth(display, data->screen) - data->window_width) / 2;
            y = (DisplayHeight(display, data->screen) - data->window_height) / 3;
        }
    }
MOVEWINDOW:
    X11_XMoveWindow(display, data->window, x, y);
    data->window_x = x;
    data->window_y = y;

    sizehints = X11_XAllocSizeHints();
    if (sizehints) {
        sizehints->flags = USPosition | USSize | PMaxSize | PMinSize;
        sizehints->x = x;
        sizehints->y = y;
        sizehints->width = data->window_width;
        sizehints->height = data->window_height;

        sizehints->min_width = sizehints->max_width = data->window_width;
        sizehints->min_height = sizehints->max_height = data->window_height;

        X11_XSetWMNormalHints(display, data->window, sizehints);

        X11_XFree(sizehints);
    }

    X11_XMapRaised(display, data->window);

    data->drawable = data->window;
#ifdef SDL_VIDEO_DRIVER_X11_XDBE
    // Initialise a back buffer for double buffering
    if (SDL_X11_HAVE_XDBE && !data->pixmap) {
        int xdbe_major, xdbe_minor;
        if (X11_XdbeQueryExtension(display, &xdbe_major, &xdbe_minor) != 0) {
            data->xdbe = true;
            data->buf = X11_XdbeAllocateBackBufferName(display, data->window, XdbeUndefined);
            data->drawable = data->buf;
        } else {
            data->xdbe = false;
        }
    }
#endif

    X11Toolkit_InitWindowPixmap(data);

    SDL_zero(ctx_vals);
    ctx_vals.foreground = data->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].pixel;
    ctx_vals.background = data->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].pixel;
    if (!data->utf8) {
        gcflags |= GCFont;
        ctx_vals.font = data->font_struct->fid;
    }
    data->ctx = X11_XCreateGC(data->display, data->drawable, gcflags, &ctx_vals);
    if (data->ctx == None) {
        return SDL_SetError("Couldn't create graphics context");
    }

    data->close = false;
    data->key_control_esc = data->key_control_enter = NULL;
    if (!data->pixmap) {
        data->ev_scale = data->ev_iscale = 1;
    } else {
        data->ev_scale = data->scale;
        data->ev_iscale = data->iscale;
    }

#if SDL_GRAB
    if (data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_MENU || data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_TOOLTIP) {
        X11_XGrabPointer(display, data->window, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        X11_XGrabKeyboard(display, data->window, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    }
#endif

    return true;
}

static void X11Toolkit_DrawWindow(SDL_ToolkitWindowX11 *data)
{
    SDL_Rect rect;
    int i;

#ifdef SDL_VIDEO_DRIVER_X11_XDBE
    if (SDL_X11_HAVE_XDBE && data->xdbe && !data->pixmap) {
        X11_XdbeBeginIdiom(data->display);
    }
#endif

    X11_XSetForeground(data->display, data->ctx, data->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].pixel);
    if (data->pixmap) {
        X11_XFillRectangle(data->display, data->drawable, data->ctx, 0, 0, data->pixmap_width, data->pixmap_height);
    } else {
        X11_XFillRectangle(data->display, data->drawable, data->ctx, 0, 0, data->window_width, data->window_height);
    }

    for (i = 0; i < data->controls_sz; i++) {
        SDL_ToolkitControlX11 *control;

        control = data->controls[i];
        if (control) {
            if (control->func_draw) {
                control->func_draw(control);
            }
        }
    }

#ifdef SDL_VIDEO_DRIVER_X11_XDBE
    if (SDL_X11_HAVE_XDBE && data->xdbe && !data->pixmap) {
        XdbeSwapInfo swap_info;
        swap_info.swap_window = data->window;
        swap_info.swap_action = XdbeUndefined;
        X11_XdbeSwapBuffers(data->display, &swap_info, 1);
        X11_XdbeEndIdiom(data->display);
    }
#endif

    if (data->pixmap) {
        SDL_Surface *scale_surface;

        rect.x = rect.y = 0;
        rect.w = data->window_width;
        rect.h = data->window_height;
#ifndef NO_SHARED_MEMORY
        if (data->shm) {
            if (data->shm_pixmap) {
                X11_XFlush(data->display);
                X11_XSync(data->display, false);
                scale_surface = SDL_CreateSurfaceFrom(data->pixmap_width, data->pixmap_height, X11_GetPixelFormatFromVisualInfo(data->display, &data->vi), data->shm_info.shmaddr, data->shm_bytes_per_line);
                SDL_BlitSurfaceScaled(scale_surface, NULL, scale_surface, &rect, SDL_SCALEMODE_LINEAR);
                SDL_DestroySurface(scale_surface);
                X11_XCopyArea(data->display, data->drawable, data->window, data->ctx, 0, 0, data->window_width, data->window_height, 0, 0);
            } else {
                X11_XShmGetImage(data->display, data->drawable, data->image, 0, 0, AllPlanes);
                scale_surface = SDL_CreateSurfaceFrom(data->pixmap_width, data->pixmap_height, X11_GetPixelFormatFromVisualInfo(data->display, &data->vi), data->image->data, data->image->bytes_per_line);
                SDL_BlitSurfaceScaled(scale_surface, NULL, scale_surface, &rect, SDL_SCALEMODE_LINEAR);
                X11_XShmPutImage(data->display, data->window, data->ctx, data->image, 0, 0, 0, 0, data->window_width, data->window_height, False);
            }
        } else
#endif
        {
            XImage *image;

            image = X11_XGetImage(data->display, data->drawable, 0, 0, data->pixmap_width, data->pixmap_height, AllPlanes, ZPixmap);
            scale_surface = SDL_CreateSurfaceFrom(data->pixmap_width, data->pixmap_height, X11_GetPixelFormatFromVisualInfo(data->display, &data->vi), image->data, image->bytes_per_line);
            SDL_BlitSurfaceScaled(scale_surface, NULL, scale_surface, &rect, SDL_SCALEMODE_LINEAR);
            X11_XPutImage(data->display, data->window, data->ctx, image, 0, 0, 0, 0, data->window_width, data->window_height);

            XDestroyImage(image);
            SDL_DestroySurface(scale_surface);
        }
    }

    X11_XFlush(data->display);
}

static SDL_ToolkitControlX11 *X11Toolkit_GetControlMouseIsOn(SDL_ToolkitWindowX11 *data, int x, int y)
{
    int i;

    for (i = 0; i < data->dyn_controls_sz; i++) {
        SDL_Rect *rect = &data->dyn_controls[i]->rect;
        if ((x >= rect->x) &&
            (x <= (rect->x + rect->w)) &&
            (y >= rect->y) &&
            (y <= (rect->y + rect->h))) {
            return data->dyn_controls[i];
        }
    }

    return NULL;
}

// NOLINTNEXTLINE(readability-non-const-parameter): cannot make XPointer a const pointer due to typedef
static Bool X11Toolkit_EventTest(Display *display, XEvent *event, XPointer arg)
{
    SDL_ToolkitWindowX11 *data = (SDL_ToolkitWindowX11 *)arg;

    if (event->xany.display != data->display) {
        return False;
    }

    if (event->xany.window == data->window) {
        return True;
    }

    return False;
}

void X11Toolkit_ProcessWindowEvents(SDL_ToolkitWindowX11 *data, XEvent *e)
{
    /* If X11_XFilterEvent returns True, then some input method has filtered the
        event, and the client should discard the event. */
    if ((e->type != Expose) && X11_XFilterEvent(e, None)) {
        return;
    }

    data->draw = false;
    data->e = e;

    if (data->focused_control) {
        if (data->focused_control->func_process_event) {
            if (data->focused_control->func_process_event(data->focused_control)) {
                return;
            }
        }
    }

    switch (e->type) {
    case Expose:
        data->draw = true;
        break;
    case MappingNotify:
        X11_XRefreshKeyboardMapping(&e->xmapping);
        break;
    case ClientMessage:
        if (e->xclient.message_type == data->wm_protocols &&
            e->xclient.format == 32 &&
            e->xclient.data.l[0] == data->wm_delete_message) {
            data->close = true;
        }
        break;
    case FocusIn:
        data->has_focus = true;
#ifdef X_HAVE_UTF8_STRING
        if (data->utf8) {
            X11_XSetICFocus(data->ic);
        }
#endif
        break;
    case FocusOut:
        data->has_focus = false;
#ifdef X_HAVE_UTF8_STRING
        if (data->utf8) {
            X11_XUnsetICFocus(data->ic);
        }
#endif
        if (data->fiddled_control) {
            data->fiddled_control->selected = false;
        }
        data->fiddled_control = NULL;
        for (data->ev_i = 0; data->ev_i < data->controls_sz; data->ev_i++) {
            data->controls[data->ev_i]->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
        }
        break;
    case MotionNotify:
        if (data->has_focus) {
            if (data->focused_control) {
                if (data->focused_control->func_on_state_change) {
                    data->focused_control->func_on_state_change(data->focused_control);
                }
            }

            data->previous_control = data->fiddled_control;
            data->fiddled_control = X11Toolkit_GetControlMouseIsOn(data, SDL_lroundf((e->xbutton.x / data->ev_scale) * data->ev_iscale), SDL_lroundf((e->xbutton.y / data->ev_scale) * data->ev_iscale));
            if (data->previous_control) {
                data->previous_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
                if (data->previous_control->func_on_state_change) {
                    data->previous_control->func_on_state_change(data->previous_control);
                }
                data->draw = true;
            }
            if (data->fiddled_control) {
                if (data->fiddled_control->dynamic) {
                    data->fiddled_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_HOVER;
                    if (data->fiddled_control->func_on_state_change) {
                        data->fiddled_control->func_on_state_change(data->fiddled_control);
                    }
                    data->draw = true;
                } else {
                    data->fiddled_control = NULL;
                }
            }
        }
        break;
    case ButtonPress:
        data->previous_control = data->fiddled_control;
        if (data->previous_control) {
            data->previous_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
            if (data->previous_control->func_on_state_change) {
                data->previous_control->func_on_state_change(data->previous_control);
            }
            data->draw = true;
        }
        if (e->xbutton.button == Button1) {
            data->fiddled_control = X11Toolkit_GetControlMouseIsOn(data, SDL_lroundf((e->xbutton.x / data->ev_scale) * data->ev_iscale), SDL_lroundf((e->xbutton.y / data->ev_scale) * data->ev_iscale));
            if (data->fiddled_control) {
                data->fiddled_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED_HELD;
                if (data->fiddled_control->func_on_state_change) {
                    data->fiddled_control->func_on_state_change(data->fiddled_control);
                }
                data->draw = true;
            }
        }
        break;
    case ButtonRelease:
        if (data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_MENU || data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_TOOLTIP) {
            int cx;
            int cy;

            cx = e->xbutton.x;
            cy = e->xbutton.y;

            if (cy < 0 || cx < 0) {
                data->close = true;
            }

            if (cy > data->window_height || cx > data->window_width) {
                data->close = true;
            }
        }

        if (e->xbutton.button == Button1) {
            SDL_ToolkitControlX11 *control;

            data->draw = true;
            control = X11Toolkit_GetControlMouseIsOn(data, SDL_lroundf((e->xbutton.x / data->ev_scale) * data->ev_iscale), SDL_lroundf((e->xbutton.y / data->ev_scale) * data->ev_iscale));
            if (control) {
                if (control->special_focus) {
                    if (data->focused_control) {
                        data->focused_control->selected = false;
                    }
                    data->focused_control = control;
                    data->focused_control->selected = true;
                }
            } else {
                if (data->focused_control) {
                    if (data->focused_control->special_focus) {
                        data->focused_control->selected = false;
                        data->focused_control = NULL;
                    }
                }
            }

            if (data->fiddled_control == control && data->fiddled_control) {
                data->fiddled_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED;
                if (data->fiddled_control->func_on_state_change) {
                    data->fiddled_control->func_on_state_change(data->fiddled_control);
                }
                data->fiddled_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
            }
        }
        break;
    case KeyPress:
        data->last_key_pressed = X11_XLookupKeysym(&e->xkey, 0);

        if (data->last_key_pressed == XK_Escape) {
            for (data->ev_i = 0; data->ev_i < data->controls_sz; data->ev_i++) {
                if (data->controls[data->ev_i]->is_default_esc) {
                    data->controls[data->ev_i]->state = SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED_HELD;
                    data->draw = true;
                    data->key_control_esc = data->controls[data->ev_i];
                    if (data->key_control_esc->func_on_state_change) {
                        data->key_control_esc->func_on_state_change(data->key_control_esc);
                    }
                }
            }
        } else if ((data->last_key_pressed == XK_Return) || (data->last_key_pressed == XK_KP_Enter)) {
            for (data->ev_i = 0; data->ev_i < data->controls_sz; data->ev_i++) {
                if (data->controls[data->ev_i]->selected) {
                    data->controls[data->ev_i]->state = SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED_HELD;
                    data->draw = true;
                    data->key_control_enter = data->controls[data->ev_i];
                    if (data->key_control_enter->func_on_state_change) {
                        data->key_control_enter->func_on_state_change(data->key_control_enter);
                    }
                }
            }
        }
        break;
    case KeyRelease:
    {
        KeySym key = X11_XLookupKeysym(&e->xkey, 0);

        // If this is a key release for something we didn't get the key down for, then bail.
        if (key != data->last_key_pressed) {
            break;
        }

        if (key == XK_Escape) {
            if (data->key_control_esc) {
                data->key_control_esc->state = SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED;
                if (data->key_control_esc->func_on_state_change) {
                    data->key_control_esc->func_on_state_change(data->key_control_esc);
                }
            }
        } else if ((key == XK_Return) || (key == XK_KP_Enter)) {
            if (data->key_control_enter) {
                data->key_control_enter->state = SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED;
                if (data->key_control_enter->func_on_state_change) {
                    data->key_control_enter->func_on_state_change(data->key_control_enter);
                }
            }
        } else if (key == XK_Tab) {
            if (data->focused_control) {
                data->focused_control->selected = false;
            }
            data->draw = true;
            for (data->ev_i = 0; data->ev_i < data->dyn_controls_sz; data->ev_i++) {
                if (data->dyn_controls[data->ev_i] == data->focused_control) {
                    int next_index;

                    next_index = data->ev_i + 1;
                    if ((next_index >= data->dyn_controls_sz) || (next_index < 0)) {
                        next_index = 0;
                    }
                    data->focused_control = data->dyn_controls[next_index];
                    data->focused_control->selected = true;
                    break;
                }
            }
            if (!data->focused_control && data->dyn_controls_sz > 0) {
                data->focused_control = data->dyn_controls[0];
                data->focused_control->selected = true;
            }
        } else if (key == XK_Left || key == XK_Right) {
            if (data->focused_control) {
                data->focused_control->selected = false;
            }
            data->draw = true;
            for (data->ev_i = 0; data->ev_i < data->dyn_controls_non_capturing_sz; data->ev_i++) {
                if (data->dyn_controls_non_capturing[data->ev_i] == data->focused_control) {
                    int next_index;

                    if (key == XK_Left) {
                        next_index = data->ev_i - 1;
                    } else {
                        next_index = data->ev_i + 1;
                    }
                    if ((next_index >= data->dyn_controls_non_capturing_sz) || (next_index < 0)) {
                        if (key == XK_Right || key == XK_Left) {
                            next_index = data->ev_i;
                        } else {
                            next_index = 0;
                        }
                    }
                    data->focused_control = data->dyn_controls_non_capturing[next_index];
                    data->focused_control->selected = true;
                    break;
                }
            }
        }
        break;
    }
    }

    if (data->draw) {
        X11Toolkit_DrawWindow(data);
    }
}

void X11Toolkit_DoWindowEventLoop(SDL_ToolkitWindowX11 *data)
{
    while (!data->close) {
        XEvent e;

        /* Process settings events */
        X11_XPeekEvent(data->display, &e);
        if (data->xsettings) {
            xsettings_client_process_event(data->xsettings, &e);
        }

        /* Do actual event loop */
        X11_XNextEvent(data->display, &e);
        if (X11Toolkit_EventTest(data->display, &e, (XPointer)data)) {
            X11Toolkit_ProcessWindowEvents(data, &e);
        }
    }
}

void X11Toolkit_ResizeWindow(SDL_ToolkitWindowX11 *data, int w, int h)
{
    if (!data->pixmap) {
        data->window_width = w;
        data->window_height = h;
    } else {
        data->window_width = SDL_lroundf((w / data->iscale) * data->scale);
        data->window_height = SDL_lroundf((h / data->iscale) * data->scale);
        data->pixmap_width = w;
        data->pixmap_height = h;
        X11_XFreePixmap(data->display, data->drawable);
        X11Toolkit_InitWindowPixmap(data);
    }

    X11_XResizeWindow(data->display, data->window, data->window_width, data->window_height);
}

static void X11Toolkit_DestroyIconControl(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitIconControlX11 *icon_control;

    icon_control = (SDL_ToolkitIconControlX11 *)control;
    X11_XFreeFont(control->window->display, icon_control->icon_char_font);
    SDL_free(control);
}

static void X11Toolkit_DrawIconControl(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitIconControlX11 *icon_control;

    icon_control = (SDL_ToolkitIconControlX11 *)control;
    control->rect.w -= 2 * control->window->iscale;
    control->rect.h -= 2 * control->window->iscale;
    X11_XSetForeground(control->window->display, control->window->ctx, icon_control->xcolor_bg_shadow.pixel);
    X11_XFillArc(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (2 * control->window->iscale), control->rect.y + (2 * control->window->iscale), control->rect.w, control->rect.h, 0, 360 * 64);

    switch (icon_control->flags & (SDL_MESSAGEBOX_ERROR | SDL_MESSAGEBOX_WARNING | SDL_MESSAGEBOX_INFORMATION)) {
    case SDL_MESSAGEBOX_ERROR:
        X11_XSetForeground(control->window->display, control->window->ctx, icon_control->xcolor_red_darker.pixel);
        X11_XFillArc(control->window->display, control->window->drawable, control->window->ctx, control->rect.x, control->rect.y, control->rect.w, control->rect.h, 0, 360 * 64);
        X11_XSetForeground(control->window->display, control->window->ctx, icon_control->xcolor_red.pixel);
        X11_XFillArc(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (1 * control->window->iscale), control->rect.y + (1 * control->window->iscale), control->rect.w - (2 * control->window->iscale), control->rect.h - (2 * control->window->iscale), 0, 360 * 64);
        X11_XSetForeground(control->window->display, control->window->ctx, icon_control->xcolor_white.pixel);
        break;
    case SDL_MESSAGEBOX_WARNING:
        X11_XSetForeground(control->window->display, control->window->ctx, icon_control->xcolor_black.pixel);
        X11_XFillArc(control->window->display, control->window->drawable, control->window->ctx, control->rect.x, control->rect.y, control->rect.w, control->rect.h, 0, 360 * 64);
        X11_XSetForeground(control->window->display, control->window->ctx, icon_control->xcolor_yellow.pixel);
        X11_XFillArc(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (1 * control->window->iscale), control->rect.y + (1 * control->window->iscale), control->rect.w - (2 * control->window->iscale), control->rect.h - (2 * control->window->iscale), 0, 360 * 64);
        X11_XSetForeground(control->window->display, control->window->ctx, icon_control->xcolor_black.pixel);
        break;
    case SDL_MESSAGEBOX_INFORMATION:
        X11_XSetForeground(control->window->display, control->window->ctx, icon_control->xcolor_white.pixel);
        X11_XFillArc(control->window->display, control->window->drawable, control->window->ctx, control->rect.x, control->rect.y, control->rect.w, control->rect.h, 0, 360 * 64);
        X11_XSetForeground(control->window->display, control->window->ctx, icon_control->xcolor_blue.pixel);
        X11_XFillArc(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (1 * control->window->iscale), control->rect.y + (1 * control->window->iscale), control->rect.w - (2 * control->window->iscale), control->rect.h - (2 * control->window->iscale), 0, 360 * 64);
        X11_XSetForeground(control->window->display, control->window->ctx, icon_control->xcolor_white.pixel);
        break;
    }
    X11_XSetFont(control->window->display, control->window->ctx, icon_control->icon_char_font->fid);
    X11_XDrawString(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + icon_control->icon_char_x, control->rect.y + icon_control->icon_char_y, &icon_control->icon_char, 1);
    if (!control->window->utf8) {
        X11_XSetFont(control->window->display, control->window->ctx, control->window->font_struct->fid);
    }

    control->rect.w += 2 * control->window->iscale;
    control->rect.h += 2 * control->window->iscale;
}

static void X11Toolkit_CalculateIconControl(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitIconControlX11 *control;
    int icon_char_w;
    int icon_wh;

    control = (SDL_ToolkitIconControlX11 *)base_control;
    X11Toolkit_GetTextWidthHeightForFont(control->icon_char_font, &control->icon_char, 1, &icon_char_w, &control->icon_char_h, &control->icon_char_a);
    base_control->rect.w = icon_char_w;
    base_control->rect.h = control->icon_char_h;
    icon_wh = SDL_max(icon_char_w, control->icon_char_h) + SDL_TOOLKIT_X11_ELEMENT_PADDING * 2 * base_control->window->iscale;
    base_control->rect.w = icon_wh;
    base_control->rect.h = icon_wh;
    base_control->rect.y = 0;
    base_control->rect.x = 0;
    control->icon_char_y = control->icon_char_a + (base_control->rect.h - control->icon_char_h) / 2;
    control->icon_char_x = (base_control->rect.w - icon_char_w) / 2;
    base_control->rect.w += 2 * base_control->window->iscale;
    base_control->rect.h += 2 * base_control->window->iscale;
}

static void X11Toolkit_OnIconControlScaleChange(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitIconControlX11 *control;
    char *font;

    control = (SDL_ToolkitIconControlX11 *)base_control;
    X11_XFreeFont(base_control->window->display, control->icon_char_font);
    SDL_asprintf(&font, g_IconFont, G_ICONFONT_SIZE * base_control->window->iscale);
    control->icon_char_font = X11_XLoadQueryFont(base_control->window->display, font);
    SDL_free(font);
    if (!control->icon_char_font) {
        SDL_asprintf(&font, g_ToolkitFontLatin1, G_TOOLKITFONT_SIZE * base_control->window->iscale);
        control->icon_char_font = X11_XLoadQueryFont(base_control->window->display, font);
        SDL_free(font);
    }
}

SDL_ToolkitControlX11 *X11Toolkit_CreateIconControl(SDL_ToolkitWindowX11 *window, SDL_MessageBoxFlags flags)
{
    SDL_ToolkitIconControlX11 *control;
    SDL_ToolkitControlX11 *base_control;
    char *font;

    /* Create control struct */
    control = (SDL_ToolkitIconControlX11 *)SDL_malloc(sizeof(SDL_ToolkitIconControlX11));
    base_control = (SDL_ToolkitControlX11 *)control;
    if (!control) {
        SDL_SetError("Unable to allocate icon control structure");
        return NULL;
    }

    /* Fill out struct */
    base_control->window = window;
    base_control->func_draw = X11Toolkit_DrawIconControl;
    base_control->func_free = X11Toolkit_DestroyIconControl;
    base_control->func_on_state_change = NULL;
    base_control->func_process_event = NULL;
    base_control->func_calc_size = X11Toolkit_CalculateIconControl;
    base_control->func_on_scale_change = X11Toolkit_OnIconControlScaleChange;
    base_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
    base_control->selected = false;
    base_control->dynamic = false;
    base_control->is_default_enter = false;
    base_control->is_default_esc = false;
    base_control->special_focus = false;
    base_control->captures_lr_arrows = false;
    control->flags = flags;

    /* Load font */
    SDL_asprintf(&font, g_IconFont, G_ICONFONT_SIZE * window->iscale);
    control->icon_char_font = X11_XLoadQueryFont(window->display, font);
    SDL_free(font);
    if (!control->icon_char_font) {
        SDL_asprintf(&font, g_ToolkitFontLatin1, G_TOOLKITFONT_SIZE * window->iscale);
        control->icon_char_font = X11_XLoadQueryFont(window->display, font);
        SDL_free(font);
        if (!control->icon_char_font) {
            SDL_free(control);
            return NULL;
        }
    }

    /* Set colors */
    switch (flags & (SDL_MESSAGEBOX_ERROR | SDL_MESSAGEBOX_WARNING | SDL_MESSAGEBOX_INFORMATION)) {
    case SDL_MESSAGEBOX_ERROR:
        control->icon_char = 'X';
        control->xcolor_white.flags = DoRed | DoGreen | DoBlue;
        control->xcolor_white.red = 65535;
        control->xcolor_white.green = 65535;
        control->xcolor_white.blue = 65535;
        control->xcolor_red.flags = DoRed | DoGreen | DoBlue;
        control->xcolor_red.red = 65535;
        control->xcolor_red.green = 0;
        control->xcolor_red.blue = 0;
        control->xcolor_red_darker.flags = DoRed | DoGreen | DoBlue;
        control->xcolor_red_darker.red = 40535;
        control->xcolor_red_darker.green = 0;
        control->xcolor_red_darker.blue = 0;
        X11_XAllocColor(window->display, window->cmap, &control->xcolor_white);
        X11_XAllocColor(window->display, window->cmap, &control->xcolor_red);
        X11_XAllocColor(window->display, window->cmap, &control->xcolor_red_darker);
        break;
    case SDL_MESSAGEBOX_WARNING:
        control->icon_char = '!';
        control->xcolor_black.flags = DoRed | DoGreen | DoBlue;
        control->xcolor_black.red = 0;
        control->xcolor_black.green = 0;
        control->xcolor_black.blue = 0;
        control->xcolor_yellow.flags = DoRed | DoGreen | DoBlue;
        control->xcolor_yellow.red = 65535;
        control->xcolor_yellow.green = 65535;
        control->xcolor_yellow.blue = 0;
        X11_XAllocColor(window->display, window->cmap, &control->xcolor_black);
        X11_XAllocColor(window->display, window->cmap, &control->xcolor_yellow);
        break;
    case SDL_MESSAGEBOX_INFORMATION:
        control->icon_char = 'i';
        control->xcolor_white.flags = DoRed | DoGreen | DoBlue;
        control->xcolor_white.red = 65535;
        control->xcolor_white.green = 65535;
        control->xcolor_white.blue = 65535;
        control->xcolor_blue.flags = DoRed | DoGreen | DoBlue;
        control->xcolor_blue.red = 0;
        control->xcolor_blue.green = 0;
        control->xcolor_blue.blue = 65535;
        X11_XAllocColor(window->display, window->cmap, &control->xcolor_white);
        X11_XAllocColor(window->display, window->cmap, &control->xcolor_blue);
        break;
    default:
        X11_XFreeFont(window->display, control->icon_char_font);
        SDL_free(control);
        return NULL;
    }
    control->xcolor_bg_shadow.flags = DoRed | DoGreen | DoBlue;
    if (window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].red > 32896) {
        control->xcolor_bg_shadow.red = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].red - 12500, 0, 65535);
    } else if (window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].red == 0) {
        control->xcolor_bg_shadow.red = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].red + 9000, 0, 65535);
    } else {
        control->xcolor_bg_shadow.red = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].red - 3000, 0, 65535);
    }

    if (window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].green > 32896) {
        control->xcolor_bg_shadow.green = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].green - 12500, 0, 65535);
    } else if (window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].green == 0) {
        control->xcolor_bg_shadow.green = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].green + 9000, 0, 65535);
    } else {
        control->xcolor_bg_shadow.green = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].green - 3000, 0, 65535);
    }

    if (window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].blue > 32896) {
        control->xcolor_bg_shadow.blue = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].blue - 12500, 0, 65535);
    } else if (window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].blue == 0) {
        control->xcolor_bg_shadow.blue = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].blue + 9000, 0, 65535);
    } else {
        control->xcolor_bg_shadow.blue = SDL_clamp(window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].blue - 3000, 0, 65535);
    }
    X11_XAllocColor(window->display, window->cmap, &control->xcolor_bg_shadow);

    /* Sizing and positioning */
    X11Toolkit_CalculateIconControl(base_control);

    X11Toolkit_AddControlToWindow(window, base_control);
    return base_control;
}

bool X11Toolkit_NotifyControlOfSizeChange(SDL_ToolkitControlX11 *control)
{
    if (control->func_calc_size) {
        control->func_calc_size(control);
        return true;
    } else {
        return false;
    }
}

static void X11Toolkit_CalculateButtonControl(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitButtonControlX11 *button_control;
    int text_d;

    button_control = (SDL_ToolkitButtonControlX11 *)control;
    button_control->text_rect.x = button_control->text_rect.y = 0;

    if (button_control->data) {
        X11Toolkit_GetTextWidthHeight(control->window, button_control->data->text, button_control->str_sz, &button_control->text_rect.w, &button_control->text_rect.h, &button_control->text_a, &text_d, NULL);
    } else {
        switch (button_control->icon) {
        case SDL_TOOLKIT_ICON_X11_UP_ARROW:
        case SDL_TOOLKIT_ICON_X11_DOWN_ARROW:
            button_control->text_rect.w = 7 * control->window->iscale;
            button_control->text_rect.h = 4 * control->window->iscale;
            break;
        case SDL_TOOLKIT_ICON_X11_LEFT_ARROW:
        case SDL_TOOLKIT_ICON_X11_RIGHT_ARROW:
            button_control->text_rect.w = 4 * control->window->iscale;
            button_control->text_rect.h = 7 * control->window->iscale;
            break;
        default:
            button_control->text_rect.w = button_control->text_rect.h = 0;
        }
    }

    if (control->do_size) {
        if (!button_control->data) {
            control->rect.w = SDL_TOOLKIT_X11_ELEMENT_PADDING * 2 * control->window->iscale + button_control->text_rect.w;
            control->rect.h = SDL_TOOLKIT_X11_ELEMENT_PADDING * 2 * control->window->iscale + button_control->text_rect.h;
            control->rect.w = control->rect.h = SDL_max(control->rect.w, control->rect.h);
        } else {
            control->rect.w = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * 2 * control->window->iscale + button_control->text_rect.w;
            control->rect.h = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * 2 * control->window->iscale + button_control->text_rect.h;
        }
    }

    button_control->text_rect.x = (control->rect.w - button_control->text_rect.w) / 2;
    button_control->text_rect.y = (control->rect.h - button_control->text_rect.h) / 2;
    if (button_control->data) {
        button_control->text_rect.y += button_control->text_a;
    }
}

static void X11Toolkit_DrawButtonControl(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitButtonControlX11 *button_control;
    char *text;

    button_control = (SDL_ToolkitButtonControlX11 *)control;

#ifdef HAVE_FRIBIDI_H
    text = button_control->text;
#else
    text = (char *)button_control->data->text;
#endif

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].pixel);
    /* Draw bevel */
    if (control->state == SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED || control->state == SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED_HELD) {
        X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_d.pixel);
        X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                           control->rect.x, control->rect.y,
                           control->rect.w, control->rect.h);

        X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_l2.pixel);
        X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                           control->rect.x, control->rect.y,
                           control->rect.w - (1 * control->window->iscale), control->rect.h - (1 * control->window->iscale));

        X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_l1.pixel);
        X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                           control->rect.x + 1 * control->window->iscale, control->rect.y + 1 * control->window->iscale,
                           control->rect.w - 3 * control->window->iscale, control->rect.h - 2 * control->window->iscale);

        X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].pixel);
        X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                           control->rect.x + 1 * control->window->iscale, control->rect.y + 1 * control->window->iscale,
                           control->rect.w - 3 * control->window->iscale, control->rect.h - 3 * control->window->iscale);

        X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_pressed.pixel);
        X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                           control->rect.x + 2 * control->window->iscale, control->rect.y + 2 * control->window->iscale,
                           control->rect.w - 4 * control->window->iscale, control->rect.h - 4 * control->window->iscale);
    } else {
        if (control->selected) {
            X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_d.pixel);
            X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                               control->rect.x, control->rect.y,
                               control->rect.w, control->rect.h);

            X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_l2.pixel);
            X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                               control->rect.x + 1 * control->window->iscale, control->rect.y + 1 * control->window->iscale,
                               control->rect.w - 3 * control->window->iscale, control->rect.h - 3 * control->window->iscale);

            X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].pixel);
            X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                               control->rect.x + 2 * control->window->iscale, control->rect.y + 2 * control->window->iscale,
                               control->rect.w - 4 * control->window->iscale, control->rect.h - 4 * control->window->iscale);

            X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_l1.pixel);
            X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                               control->rect.x + 2 * control->window->iscale, control->rect.y + 2 * control->window->iscale,
                               control->rect.w - 5 * control->window->iscale, control->rect.h - 5 * control->window->iscale);

            X11_XSetForeground(control->window->display, control->window->ctx, (control->state == SDL_TOOLKIT_CONTROL_STATE_X11_HOVER) ? control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED].pixel : control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND].pixel);
            X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                               control->rect.x + 3 * control->window->iscale, control->rect.y + 3 * control->window->iscale,
                               control->rect.w - 6 * control->window->iscale, control->rect.h - 6 * control->window->iscale);
        } else {
            X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_d.pixel);
            X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                               control->rect.x, control->rect.y,
                               control->rect.w, control->rect.h);

            X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_l2.pixel);
            X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                               control->rect.x, control->rect.y,
                               control->rect.w - 1 * control->window->iscale, control->rect.h - 1 * control->window->iscale);

            X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].pixel);
            X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                               control->rect.x + 1 * control->window->iscale, control->rect.y + 1 * control->window->iscale,
                               control->rect.w - 2 * control->window->iscale, control->rect.h - 2 * control->window->iscale);

            X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_l1.pixel);
            X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                               control->rect.x + 1 * control->window->iscale, control->rect.y + 1 * control->window->iscale,
                               control->rect.w - 3 * control->window->iscale, control->rect.h - 3 * control->window->iscale);

            X11_XSetForeground(control->window->display, control->window->ctx, (control->state == SDL_TOOLKIT_CONTROL_STATE_X11_HOVER) ? control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED].pixel : control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND].pixel);
            X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx,
                               control->rect.x + 2 * control->window->iscale, control->rect.y + 2 * control->window->iscale,
                               control->rect.w - 4 * control->window->iscale, control->rect.h - 4 * control->window->iscale);
        }
    }

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].pixel);
    if (button_control->data) {
#ifdef X_HAVE_UTF8_STRING
        if (control->window->utf8) {
            X11_Xutf8DrawString(control->window->display, control->window->drawable, control->window->font_set, control->window->ctx,
                                control->rect.x + button_control->text_rect.x,
                                control->rect.y + button_control->text_rect.y,
                                text, button_control->str_sz);
        } else
#endif
        {
            X11_XDrawString(control->window->display, control->window->drawable, control->window->ctx,
                            control->rect.x + button_control->text_rect.x, control->rect.y + button_control->text_rect.y,
                            text, button_control->str_sz);
        }
    } else {
        XPoint points[3];

        switch (button_control->icon) {
        case SDL_TOOLKIT_ICON_X11_UP_ARROW:
            points[0].x = control->rect.x + button_control->text_rect.x + button_control->text_rect.w / 2;
            points[0].y = control->rect.y + button_control->text_rect.y;
            points[1].x = control->rect.x + button_control->text_rect.x;
            points[1].y = control->rect.y + button_control->text_rect.y + button_control->text_rect.h;
            points[2].x = control->rect.x + button_control->text_rect.x + button_control->text_rect.w;
            points[2].y = control->rect.y + button_control->text_rect.y + button_control->text_rect.h;
            break;
        case SDL_TOOLKIT_ICON_X11_DOWN_ARROW:
            points[0].x = control->rect.x + button_control->text_rect.x + button_control->text_rect.w / 2;
            points[0].y = control->rect.y + button_control->text_rect.y + button_control->text_rect.h;
            points[1].x = control->rect.x + button_control->text_rect.x;
            points[1].y = control->rect.y + button_control->text_rect.y;
            points[2].x = control->rect.x + button_control->text_rect.x + button_control->text_rect.w;
            points[2].y = control->rect.y + button_control->text_rect.y;
            break;
        case SDL_TOOLKIT_ICON_X11_LEFT_ARROW:
            points[0].x = control->rect.x + button_control->text_rect.x;
            points[0].y = control->rect.y + button_control->text_rect.y + button_control->text_rect.h / 2;
            points[1].x = control->rect.x + button_control->text_rect.x + button_control->text_rect.w;
            points[1].y = control->rect.y + button_control->text_rect.y;
            points[2].x = control->rect.x + button_control->text_rect.x + button_control->text_rect.w;
            points[2].y = control->rect.y + button_control->text_rect.y + button_control->text_rect.h;
            break;
        case SDL_TOOLKIT_ICON_X11_RIGHT_ARROW:
            points[0].x = control->rect.x + button_control->text_rect.x;
            points[0].y = control->rect.y + button_control->text_rect.y;
            points[1].x = control->rect.x + button_control->text_rect.x;
            points[1].y = control->rect.y + button_control->text_rect.y + button_control->text_rect.h;
            points[2].x = control->rect.x + button_control->text_rect.x + button_control->text_rect.w;
            points[2].y = control->rect.y + button_control->text_rect.y + button_control->text_rect.h / 2;
            break;
        default:
            points[0].x = points[0].y = 0;
            points[1].x = points[1].y = 0;
            points[2].x = points[2].y = 0;
        }

        X11_XFillPolygon(control->window->display, control->window->drawable, control->window->ctx, points, 3, Convex, CoordModeOrigin);
    }
}

static void X11Toolkit_OnButtonControlStateChange(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitButtonControlX11 *button_control;

    button_control = (SDL_ToolkitButtonControlX11 *)control;
    if (button_control->cb && control->state == SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED) {
        button_control->cb(control, button_control->cb_data);
    }
}

static void X11Toolkit_DestroyGenericControl(SDL_ToolkitControlX11 *control)
{
    SDL_free(control);
}

static void X11Toolkit_DestroyButtonControl(SDL_ToolkitControlX11 *control)
{
#ifdef HAVE_FRIBIDI_H
    SDL_ToolkitButtonControlX11 *button_control;

    button_control = (SDL_ToolkitButtonControlX11 *)control;
    if (button_control->free_text) {
        SDL_free(button_control->text);
    }
#endif
    SDL_free(control);
}

SDL_ToolkitControlX11 *X11Toolkit_CreateCommonButtonControl(SDL_ToolkitWindowX11 *window, const SDL_MessageBoxButtonData *data, SDL_ToolkitIconX11 icon)
{
    SDL_ToolkitButtonControlX11 *control;
    SDL_ToolkitControlX11 *base_control;

    control = (SDL_ToolkitButtonControlX11 *)SDL_malloc(sizeof(SDL_ToolkitButtonControlX11));
    base_control = (SDL_ToolkitControlX11 *)control;
    if (!control) {
        SDL_SetError("Unable to allocate button control structure");
        return NULL;
    }

    base_control->window = window;
    base_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
    base_control->func_calc_size = X11Toolkit_CalculateButtonControl;
    base_control->func_draw = X11Toolkit_DrawButtonControl;
    base_control->func_on_state_change = X11Toolkit_OnButtonControlStateChange;
    base_control->func_free = X11Toolkit_DestroyButtonControl;
    base_control->func_on_scale_change = NULL;
    base_control->func_process_event = NULL;
    base_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
    base_control->selected = false;
    base_control->dynamic = true;
    base_control->is_default_enter = false;
    base_control->captures_lr_arrows = false;
    base_control->is_default_esc = false;
    base_control->special_focus = false;

    if (data) {
        if (data->flags & SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT) {
            base_control->is_default_esc = true;
        }
        if (data->flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) {
            base_control->is_default_enter = true;
            base_control->selected = true;
        }
        control->str_sz = SDL_strlen(data->text);
        control->data = data;
    } else {
        control->data = NULL;
    }
    control->icon = icon;
    control->cb = NULL;

#ifdef HAVE_FRIBIDI_H
    if (data) {
        if (base_control->window->fribidi) {
            control->text = SDL_FriBidi_Process(base_control->window->fribidi, (char *)control->data->text, control->str_sz, base_control->window->do_shaping, NULL);
            if (control->text) {
                control->free_text = true;
                control->str_sz = SDL_strlen(control->text);
            } else {
                control->text = (char *)control->data->text;
                control->free_text = false;
            }
        } else {
            control->text = (char *)control->data->text;
            control->free_text = false;
        }
    } else {
        control->text = NULL;
        control->free_text = false;
    }
#endif

    base_control->do_size = true;
    X11Toolkit_CalculateButtonControl(base_control);
    base_control->do_size = false;

    X11Toolkit_AddControlToWindow(window, base_control);
    return base_control;
}

SDL_ToolkitControlX11 *X11Toolkit_CreateButtonControl(SDL_ToolkitWindowX11 *window, const SDL_MessageBoxButtonData *data)
{
    return X11Toolkit_CreateCommonButtonControl(window, data, SDL_TOOLKIT_ICON_X11_NONE);
}

SDL_ToolkitControlX11 *X11Toolkit_CreateIconButtonControl(SDL_ToolkitWindowX11 *window, SDL_ToolkitIconX11 icon)
{
    return X11Toolkit_CreateCommonButtonControl(window, NULL, icon);
}

void X11Toolkit_RegisterCallbackForButtonControl(SDL_ToolkitControlX11 *control, void *data, void (*cb)(struct SDL_ToolkitControlX11 *, void *))
{
    SDL_ToolkitButtonControlX11 *button_control;

    button_control = (SDL_ToolkitButtonControlX11 *)control;
    button_control->cb_data = data;
    button_control->cb = cb;
}

const SDL_MessageBoxButtonData *X11Toolkit_GetButtonControlData(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitButtonControlX11 *button_control;

    button_control = (SDL_ToolkitButtonControlX11 *)control;
    return button_control->data;
}

void X11Toolkit_DestroyWindow(SDL_ToolkitWindowX11 *data)
{
    int i;

    if (!data) {
        return;
    }

#if SDL_GRAB
    if (data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_MENU || data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_TOOLTIP) {
        X11_XUngrabPointer(data->display, CurrentTime);
        X11_XUngrabKeyboard(data->display, CurrentTime);
    }
#endif

    for (i = 0; i < data->controls_sz; i++) {
        if (data->controls[i]->func_free) {
            data->controls[i]->func_free(data->controls[i]);
        }
    }
    if (data->controls) {
        SDL_free(data->controls);
    }
    if (data->dyn_controls) {
        SDL_free(data->dyn_controls);
    }
    if (data->dyn_controls_non_capturing) {
        SDL_free(data->dyn_controls_non_capturing);
    }

    if (data->popup_windows) {
        SDL_ListClear(&data->popup_windows);
    }

    if (data->pixmap) {
        X11_XFreePixmap(data->display, data->drawable);
    }

#ifndef NO_SHARED_MEMORY
    if (data->pixmap && data->shm) {
        X11_XShmDetach(data->display, &data->shm_info);
        if (!data->shm_pixmap) {
            XDestroyImage(data->image);
        }
        shmdt(data->shm_info.shmaddr);
    }
#endif

#ifdef X_HAVE_UTF8_STRING
    if (data->font_set) {
        X11_XFreeFontSet(data->display, data->font_set);
        data->font_set = NULL;
    }

    if (data->utf8 && data->im) {
        X11_XDestroyIC(data->ic);
        X11_XCloseIM(data->im);
    }
#endif

    if (data->font_struct) {
        X11_XFreeFont(data->display, data->font_struct);
        data->font_struct = NULL;
    }

#ifdef SDL_VIDEO_DRIVER_X11_XDBE
    if (SDL_X11_HAVE_XDBE && data->xdbe && !data->pixmap) {
        X11_XdbeDeallocateBackBufferName(data->display, data->buf);
    }
#endif

    if (data->xsettings) {
        xsettings_client_destroy(data->xsettings);
    }

    X11_XFreeGC(data->display, data->ctx);

    X11_XFreeCursor(data->display, data->cursor_normal);
    X11_XFreeCursor(data->display, data->cursor_text_edit);

    if (data->display) {
        if (data->window != None) {
            X11_XWithdrawWindow(data->display, data->window, data->screen);
            X11_XDestroyWindow(data->display, data->window);
            data->window = None;
        }

        if (data->display_close) {
            X11_XCloseDisplay(data->display);
        }
        data->display = NULL;
    }

#if SDL_SET_LOCALE
    if (data->origlocale && (data->mode == SDL_TOOLKIT_WINDOW_MODE_X11_DIALOG)) {
        (void)setlocale(LC_ALL, data->origlocale);
        SDL_free(data->origlocale);
    }
#endif

#ifdef HAVE_FRIBIDI_H
    SDL_FriBidi_Destroy(data->fribidi);
#endif

    SDL_free(data);
}

static int X11Toolkit_CountLinesOfText(const char *text)
{
    int result = 0;
    while (text && *text) {
        const char *lf = SDL_strchr(text, '\n');
        result++; // even without an endline, this counts as a line.
        text = lf ? lf + 1 : NULL;
    }
    return result;
}

static void X11Toolkit_DrawLabelControl(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitLabelControlX11 *label_control;
    int i;
    int x;

    label_control = (SDL_ToolkitLabelControlX11 *)control;
    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].pixel);
    for (i = 0; i < label_control->sz; i++) {
        x = control->rect.x;
#ifdef HAVE_FRIBIDI_H
        if (control->window->fribidi) {
            x += label_control->x[i];
        }
#endif
#ifdef X_HAVE_UTF8_STRING
        if (control->window->utf8) {
            X11_Xutf8DrawString(control->window->display, control->window->drawable, control->window->font_set, control->window->ctx,
                                x, control->rect.y + label_control->y[i],
                                label_control->lines[i], label_control->szs[i]);
        } else
#endif
        {
            X11_XDrawString(control->window->display, control->window->drawable, control->window->ctx,
                            x, control->rect.y + label_control->y[i],
                            label_control->lines[i], label_control->szs[i]);
        }
    }
}

static void X11Toolkit_DestroyLabelControl(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitLabelControlX11 *label_control;

    label_control = (SDL_ToolkitLabelControlX11 *)control;
#ifdef HAVE_FRIBIDI_H
    if (control->window->fribidi) {
        int i;

        for (i = 0; i < label_control->sz; i++) {
            if (label_control->free_lines[i]) {
                SDL_free(label_control->lines[i]);
            }
        }
        SDL_free(label_control->x);
        SDL_free(label_control->free_lines);
        SDL_free(label_control->w);
        SDL_free(label_control->par_types);
    }
#endif
    SDL_free(label_control->lines);
    SDL_free(label_control->szs);
    SDL_free(label_control->y);
    SDL_free(label_control);
}

static void X11Toolkit_CalculateLabelControl(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitLabelControlX11 *control;
    int last_h;
    int ascent;
    int descent;
    int font_h;
    int w;
    int h;
    int i;
#ifdef HAVE_FRIBIDI_H
    FriBidiParType first_ndn_dir;
    int last_ndn;
#endif

    last_h = 0;
    control = (SDL_ToolkitLabelControlX11 *)base_control;
    for (i = 0; i < control->sz; i++) {
        X11Toolkit_GetTextWidthHeight(base_control->window, control->lines[i], control->szs[i], &w, &h, &ascent, &descent, &font_h);
        base_control->rect.w = SDL_max(base_control->rect.w, w);

        if (i > 0) {
            control->y[i] = font_h + control->y[i - 1];
        } else {
            control->y[i] = ascent;
        }

        last_h = h;
    }
    base_control->rect.h = control->y[control->sz - 1] + last_h;

#ifdef HAVE_FRIBIDI_H
    if (base_control->window->fribidi) {
        first_ndn_dir = FRIBIDI_PAR_LTR;
        for (i = 0; i < control->sz; i++) {
            if (control->par_types[i] != FRIBIDI_PAR_ON) {
                first_ndn_dir = control->par_types[i];
            }
        }

        last_ndn = -1;
        for (i = 0; i < control->sz; i++) {
            switch (control->par_types[i]) {
            case FRIBIDI_PAR_LTR:
                control->x[i] = 0;
                last_ndn = i;
                break;
            case FRIBIDI_PAR_RTL:
                control->x[i] = base_control->rect.w - control->w[i];
                last_ndn = i;
                break;
            default:
                if (last_ndn != -1) {
                    if (control->par_types[last_ndn] == FRIBIDI_PAR_RTL) {
                        control->x[i] = base_control->rect.w - control->w[i];
                    } else {
                        control->x[i] = 0;
                    }
                } else {
                    if (first_ndn_dir == FRIBIDI_PAR_RTL) {
                        control->x[i] = base_control->rect.w - control->w[i];
                    } else {
                        control->x[i] = 0;
                    }
                }
            }
        }
    }
#endif
}

SDL_ToolkitControlX11 *X11Toolkit_CreateLabelControl(SDL_ToolkitWindowX11 *window, char *utf8)
{
    SDL_ToolkitLabelControlX11 *control;
    SDL_ToolkitControlX11 *base_control;
#ifdef HAVE_FRIBIDI_H
    FriBidiParType first_ndn_dir;
    int last_ndn;
#endif
    int font_h;
    int last_h;
    int ascent;
    int descent;
    int i;

    if (!utf8) {
        return NULL;
    }
    control = (SDL_ToolkitLabelControlX11 *)SDL_malloc(sizeof(SDL_ToolkitLabelControlX11));
    base_control = (SDL_ToolkitControlX11 *)control;
    if (!control) {
        SDL_SetError("Unable to allocate label control structure");
        return NULL;
    }
    base_control->window = window;
    base_control->func_draw = X11Toolkit_DrawLabelControl;
    base_control->func_on_state_change = NULL;
    base_control->func_process_event = NULL;
    base_control->func_calc_size = X11Toolkit_CalculateLabelControl;
    base_control->func_free = X11Toolkit_DestroyLabelControl;
    base_control->func_on_scale_change = NULL;
    base_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
    base_control->selected = false;
    base_control->dynamic = false;
    base_control->special_focus = false;
    base_control->rect.w = 0;
    base_control->rect.h = 0;
    base_control->is_default_enter = false;
    base_control->is_default_esc = false;
    base_control->captures_lr_arrows = false;
    control->sz = X11Toolkit_CountLinesOfText(utf8);
    control->lines = (char **)SDL_malloc(sizeof(char *) * control->sz);
    control->y = (int *)SDL_calloc(control->sz, sizeof(int));
    control->szs = (size_t *)SDL_calloc(control->sz, sizeof(size_t));
#ifdef HAVE_FRIBIDI_H
    if (base_control->window->fribidi) {
        control->x = (int *)SDL_calloc(control->sz, sizeof(int));
        control->free_lines = (bool *)SDL_calloc(control->sz, sizeof(bool));
        control->par_types = (FriBidiParType *)SDL_calloc(control->sz, sizeof(FriBidiParType));
        control->w = (int *)SDL_calloc(control->sz, sizeof(int));
    }
#endif
    last_h = 0;
    for (i = 0; i < control->sz; i++) {
        const char *lf = SDL_strchr(utf8, '\n');
        const int length = lf ? (lf - utf8) : SDL_strlen(utf8);
        int w;
        int h;

#ifdef HAVE_FRIBIDI_H
        if (base_control->window->fribidi) {
            control->lines[i] = SDL_FriBidi_Process(base_control->window->fribidi, utf8, length, base_control->window->do_shaping, &control->par_types[i]);
            control->szs[i] = SDL_strlen(control->lines[i]);
            control->free_lines[i] = true;
        } else
#endif
        {
            control->lines[i] = utf8;
            control->szs[i] = length;
#ifdef HAVE_FRIBIDI_H
            control->free_lines[i] = false;
#endif
        }
        X11Toolkit_GetTextWidthHeight(window, control->lines[i], control->szs[i], &w, &h, &ascent, &descent, &font_h);
#ifdef HAVE_FRIBIDI_H
        if (base_control->window->fribidi) {
            control->w[i] = w;
        }
#endif
        base_control->rect.w = SDL_max(base_control->rect.w, w);

        if (lf && (lf > control->lines[i]) && (lf[-1] == '\r')) {
            control->szs[i]--;
        }

        if (i > 0) {
            control->y[i] = font_h + control->y[i - 1];
        } else {
            control->y[i] = ascent;
        }
        last_h = h;
        utf8 += length + 1;

        if (!lf) {
            break;
        }
    }
    base_control->rect.h = control->y[control->sz - 1] + last_h;
#ifdef HAVE_FRIBIDI_H
    if (base_control->window->fribidi) {
        first_ndn_dir = FRIBIDI_PAR_LTR;
        for (i = 0; i < control->sz; i++) {
            if (control->par_types[i] != FRIBIDI_PAR_ON) {
                first_ndn_dir = control->par_types[i];
            }
        }

        last_ndn = -1;
        for (i = 0; i < control->sz; i++) {
            switch (control->par_types[i]) {
            case FRIBIDI_PAR_LTR:
                control->x[i] = 0;
                last_ndn = i;
                break;
            case FRIBIDI_PAR_RTL:
                control->x[i] = base_control->rect.w - control->w[i];
                last_ndn = i;
                break;
            default:
                if (last_ndn != -1) {
                    if (control->par_types[last_ndn] == FRIBIDI_PAR_RTL) {
                        control->x[i] = base_control->rect.w - control->w[i];
                    } else {
                        control->x[i] = 0;
                    }
                } else {
                    if (first_ndn_dir == FRIBIDI_PAR_RTL) {
                        control->x[i] = base_control->rect.w - control->w[i];
                    } else {
                        control->x[i] = 0;
                    }
                }
            }
        }
    }
#endif

    X11Toolkit_AddControlToWindow(window, base_control);

    return base_control;
}

int X11Toolkit_GetIconControlCharY(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitIconControlX11 *icon_control;

    icon_control = (SDL_ToolkitIconControlX11 *)control;
    return icon_control->icon_char_y - icon_control->icon_char_a + icon_control->icon_char_h / 4;
}

void X11Toolkit_SignalWindowClose(SDL_ToolkitWindowX11 *data)
{
    data->close = true;
}

static size_t X11Tookit_FindEntryControlNextCodePointSize(SDL_ToolkitEntryControlX11 *entry_control, size_t offset)
{
    size_t sz;
    sz = 1;

#ifdef X_HAVE_UTF8_STRING
    if (!entry_control->parent.window->utf8) {
        return sz;
    }

    if (++offset >= entry_control->sz) {
        return sz;
    }

    while (((entry_control->buffer[offset]) & (1 << (7))) && !((entry_control->buffer[offset]) & (1 << (6)))) {
        if (++sz > 4) {
            return 0;
        }

        if (++offset >= entry_control->sz) {
            return sz;
        }
    }
#endif

    return sz;
}

static size_t X11Tookit_FindEntryControlPrevCodePointSize(SDL_ToolkitEntryControlX11 *entry_control, size_t offset)
{
    size_t sz;
    sz = 1;

#ifdef X_HAVE_UTF8_STRING
    if (!entry_control->parent.window->utf8) {
        return sz;
    }

    if (offset-- == 0) {
        return sz;
    }

    while (((entry_control->buffer[offset]) & (1 << (7))) &&
           !((entry_control->buffer[offset]) & (1 << (6)))) {
        if (++sz > 4) {
            return 0;
        }

        if (offset-- == 0) {
            return sz;
        }
    }

#endif

    return sz;
}

static size_t X11Toolkit_InsertIntoEntryControlBuffer(SDL_ToolkitEntryControlX11 *entry_control, const char *str, size_t offset, size_t sz)
{
    if (sz == 0) {
        return 0;
    }

    // don't allow inserting with offset out of bounds
    if (offset > entry_control->sz) {
        return 0;
    }

    if (entry_control->buffer) {
        size_t total_sz;

        total_sz = entry_control->sz + sz;

        entry_control->buffer = SDL_realloc(entry_control->buffer, total_sz);
        SDL_memmove(&entry_control->buffer[offset + sz], &entry_control->buffer[offset], entry_control->sz - offset);
        SDL_memmove(&entry_control->buffer[offset], str, sz);

        entry_control->sz = total_sz;
    } else {
        entry_control->sz = sz;
        entry_control->buffer = SDL_malloc(entry_control->sz);
        SDL_memcpy(entry_control->buffer, str, entry_control->sz);
    }

    return sz;
}

static size_t X11Toolkit_EraseFromEntryControlBuffer(SDL_ToolkitEntryControlX11 *entry_control, size_t offset, size_t sz)
{
    size_t end, left_sz;

    end = offset + sz;
    left_sz = entry_control->sz - end;

    if (offset >= entry_control->sz || end > entry_control->sz) {
        return 0;
    }

    SDL_memmove(&entry_control->buffer[offset], &entry_control->buffer[end], left_sz);
    entry_control->sz -= sz;
    return sz;
}

static void X11Toolkit_ClearEntryControlSelection(SDL_ToolkitEntryControlX11 *entry_control)
{
    entry_control->sel_dir = SDL_TOOLKIT_ENTRY_SELECTION_NONE;
    entry_control->sel = 0;
    entry_control->sel_end = 0;
}

static void X11Toolkit_EraseEntryControlSelection(SDL_ToolkitEntryControlX11 *entry_control)
{
    size_t erase_sz;
    erase_sz = X11Toolkit_EraseFromEntryControlBuffer(entry_control, entry_control->sel, entry_control->sel_end - entry_control->sel);

    // move the cursor if its located after the selection's start
    if (entry_control->cur > entry_control->sel) {
        entry_control->cur -= erase_sz;
    }

    X11Toolkit_ClearEntryControlSelection(entry_control);
}

static void X11Toolkit_CalculateEntryControl(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitEntryControlX11 *entry_control;

    entry_control = (SDL_ToolkitEntryControlX11 *)control;
    X11Toolkit_GetTextWidthHeight(control->window, NULL, 0, NULL, NULL, NULL, NULL, &entry_control->cur_draw_y2);

    control->rect.h = entry_control->cur_draw_y2 + SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * 2 * control->window->iscale;
    entry_control->text_x = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * control->window->iscale;
    entry_control->text_y = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * control->window->iscale;
    entry_control->cur_draw_y1 = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * control->window->iscale;
    entry_control->cur_draw_y2 += entry_control->cur_draw_y1;
    entry_control->text_reserved_w = control->rect.w - SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * 2 * control->window->iscale;
}

static void X11Toolkit_DrawEntryControl(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitEntryControlX11 *entry_control;
    int ascent;

    entry_control = (SDL_ToolkitEntryControlX11 *)control;

    /* Draw bevel */
    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_l2.pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x, control->rect.y, control->rect.w, control->rect.h);

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x, control->rect.y, control->rect.w - (1 * control->window->iscale), control->rect.h - (1 * control->window->iscale));

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_l1.pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (1 * control->window->iscale), control->rect.y + (1 * control->window->iscale), control->rect.w - (2 * control->window->iscale), control->rect.h - (2 * control->window->iscale));

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_d.pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (1 * control->window->iscale), control->rect.y + (1 * control->window->iscale), control->rect.w - (3 * control->window->iscale), control->rect.h - (3 * control->window->iscale));

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_light_control_bg.pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (2 * control->window->iscale), control->rect.y + (2 * control->window->iscale), control->rect.w - (4 * control->window->iscale), control->rect.h - (4 * control->window->iscale));

    /* Selection */
    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_light_control_selection.pixel);
    if (control->selected && entry_control->sel_dir != SDL_TOOLKIT_ENTRY_SELECTION_NONE) {
        int selection_x;

        selection_x = control->rect.x + entry_control->text_x + entry_control->sel_x;

        X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, selection_x, control->rect.y + entry_control->cur_draw_y1, entry_control->sel_w, entry_control->sel_h);
    }

    /* Cursor */
    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].pixel);
    if (control->selected && entry_control->cur_blink) {
        int cursor_x;

        cursor_x = control->rect.x + entry_control->text_x + entry_control->cur_x;
        X11_XDrawLine(control->window->display, control->window->drawable, control->window->ctx, cursor_x, control->rect.y + entry_control->cur_draw_y1, cursor_x, control->rect.y + entry_control->cur_draw_y2);
    }

    /* Draw text */
    ascent = 0;
    if (entry_control->buffer) {
        int width;
        int height;
        int descent;

        X11Toolkit_GetTextWidthHeight(control->window, entry_control->buffer, entry_control->sz, &width, &height, &ascent, &descent, NULL);
    }

    if (entry_control->buffer == NULL) {
        return;
    }

#ifdef X_HAVE_UTF8_STRING
    if (control->window->utf8) {
        X11_Xutf8DrawString(control->window->display, control->window->drawable, control->window->font_set, control->window->ctx,
                            control->rect.x + entry_control->text_x + entry_control->clip_offset,
                            control->rect.y + entry_control->text_y + ascent,
                            &entry_control->buffer[entry_control->clip_start], entry_control->clip_end - entry_control->clip_start);
    } else
#endif
    {
        X11_XDrawString(control->window->display, control->window->drawable, control->window->ctx,
                        control->rect.x + entry_control->text_x + entry_control->clip_offset,
                        control->rect.y + entry_control->text_y + ascent,
                        &entry_control->buffer[entry_control->clip_start], entry_control->clip_end - entry_control->clip_start);
    }
}

static void X11Toolkit_ScrollEntryControl(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitEntryControlX11 *entry_control;
    int width;
    int height;
    int ascent;
    int descent;
    int i;
    size_t start_sz, end_sz;

    entry_control = (SDL_ToolkitEntryControlX11 *)control;

    /* Ensure text is always right-justified (if it exceeds width) */
    X11Toolkit_GetTextWidthHeight(control->window, entry_control->buffer, entry_control->sz, &width, &height, &ascent, &descent, NULL);
    if (width > entry_control->text_reserved_w && width - entry_control->start_offset < entry_control->text_reserved_w) {
        entry_control->start_offset = width - entry_control->text_reserved_w;
    } else if (width < entry_control->text_reserved_w) {
        /* Otherwise left align */
        entry_control->start_offset = 0;
    }

    /* Position cursor */
    X11Toolkit_GetTextWidthHeight(control->window, entry_control->buffer, entry_control->cur, &entry_control->cur_x, &height, &ascent, &descent, NULL);

    /* Scroll page to cursor */
    if ((entry_control->cur_x - entry_control->start_offset) > entry_control->text_reserved_w) {
        /* Move by offset */
        entry_control->start_offset = entry_control->cur_x - entry_control->text_reserved_w;
    } else if ((entry_control->cur_x - entry_control->start_offset) < 0) {
        entry_control->start_offset = entry_control->cur_x;
    }

    /* Apply clipping, any characters below 0 or above max width are not displayed */
    entry_control->clip_end = entry_control->sz;
    for (i = 0; i < entry_control->sz; i++) {
        X11Toolkit_GetTextWidthHeight(control->window, entry_control->buffer, i, &width, &height, &ascent, &descent, NULL);
        if (width >= (entry_control->text_reserved_w + entry_control->start_offset)) {
            entry_control->clip_end = i;
            break;
        }
    }

    for (i = 0; i < entry_control->sz; i++) {
        X11Toolkit_GetTextWidthHeight(control->window, entry_control->buffer, i, &width, &height, &ascent, &descent, NULL);
        entry_control->clip_start = i;
        if ((width - entry_control->start_offset) >= 0) {
            break;
        }
    }

    entry_control->cur_x -= entry_control->start_offset; /* cursor position - page start position */
    entry_control->clip_offset = 0;

    /* Position & Size of selection */
    if (entry_control->sel_dir != SDL_TOOLKIT_ENTRY_SELECTION_NONE) {
        start_sz = entry_control->sel - entry_control->clip_start;
        if (start_sz < 0) {
            start_sz = 0;
        }

        end_sz = entry_control->sel_end - entry_control->clip_start;
        if (end_sz < 0) {
            end_sz = 0;
        }

        X11Toolkit_GetTextWidthHeight(control->window, &entry_control->buffer[entry_control->clip_start], start_sz, &entry_control->sel_x, &height, &ascent, &descent, NULL);
        X11Toolkit_GetTextWidthHeight(control->window, &entry_control->buffer[entry_control->clip_start], end_sz, &entry_control->sel_w, &entry_control->sel_h, &ascent, &descent, NULL);

        // Ensure width stays in bbox
        entry_control->sel_w -= entry_control->sel_x;
        entry_control->sel_w = SDL_clamp(entry_control->sel_w, 0, entry_control->text_reserved_w - entry_control->sel_x);
    }

#ifdef X_HAVE_UTF8_STRING
    if (control->window->utf8 && control->window->im) {
        XVaNestedList preedit_attr;
        XPoint spot;

        spot.x = entry_control->cur_x;
        spot.y = entry_control->cur_draw_y1;
        preedit_attr = X11_XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
        X11_XSetICValues(control->window->ic, XNPreeditAttributes, preedit_attr, NULL);
        X11_XFree(preedit_attr);
    }
#endif
}

static void X11Toolkit_ProecssEntryControlSelection(SDL_ToolkitEntryControlX11 *entry_control, unsigned int keystate, SDL_ToolkitEntrySelectionDir dir)
{
    // If we hold shift, start selection, otherwise reset
    if (!(keystate & ShiftMask) && !(keystate & ControlMask)) {
        X11Toolkit_ClearEntryControlSelection(entry_control);
        return;
    }

    if (entry_control->cur == entry_control->old_cur) {
        return;
    }

    // Start new selection
    if (entry_control->sel_dir == SDL_TOOLKIT_ENTRY_SELECTION_NONE) {
        entry_control->sel_dir = dir;

        if (dir == SDL_TOOLKIT_ENTRY_SELECTION_LEFT) {
            entry_control->sel_end = entry_control->old_cur;
        } else {
            entry_control->sel = entry_control->old_cur;
        }
    }

    // Move existing selection
    if (entry_control->sel_dir == SDL_TOOLKIT_ENTRY_SELECTION_LEFT) {
        if (dir == SDL_TOOLKIT_ENTRY_SELECTION_LEFT) {
            entry_control->sel = entry_control->cur;
        } else {
            entry_control->sel += X11Tookit_FindEntryControlNextCodePointSize(entry_control, entry_control->sel);
        }

    } else {
        if (dir == SDL_TOOLKIT_ENTRY_SELECTION_RIGHT) {
            entry_control->sel_end = entry_control->cur;
        } else {
            entry_control->sel_end -= X11Tookit_FindEntryControlPrevCodePointSize(entry_control, entry_control->sel_end);
        }
    }

    // reset if start == end
    if (entry_control->sel_dir != dir && entry_control->sel == entry_control->sel_end) {
        X11Toolkit_ClearEntryControlSelection(entry_control);
    }
}

static bool X11Toolkit_ProcessEntryControlKeyPress(SDL_ToolkitControlX11 *control, SDL_ToolkitEntryControlX11 *entry_control)
{
    XKeyEvent *xkey;
    KeySym keysym;
    unsigned int keystate;
    size_t str_sz, sz;
    char *str;

    xkey = &control->window->e->xkey;
    keysym = X11_XLookupKeysym(xkey, 0);
    keystate = xkey->state;

    switch (keysym) {

    case XK_Left:
    {
        if (entry_control->cur > 0) {
            entry_control->cur -= X11Tookit_FindEntryControlPrevCodePointSize(entry_control, entry_control->cur);
        }

        X11Toolkit_ProecssEntryControlSelection(entry_control, keystate, SDL_TOOLKIT_ENTRY_SELECTION_LEFT);
        return true;
    }

    case XK_Right:
    {
        if (entry_control->cur < entry_control->sz) {
            entry_control->cur += X11Tookit_FindEntryControlNextCodePointSize(entry_control, entry_control->cur);
        }

        X11Toolkit_ProecssEntryControlSelection(entry_control, keystate, SDL_TOOLKIT_ENTRY_SELECTION_RIGHT);
        return true;
    }

    case XK_BackSpace:
    {
        size_t sz_sub;

        if (entry_control->sz == 0) {
            return true;
        }

        // erase the selection if its present
        if (entry_control->sel_dir != SDL_TOOLKIT_ENTRY_SELECTION_NONE) {
            X11Toolkit_EraseEntryControlSelection(entry_control);
            return true;
        }

        sz_sub = X11Tookit_FindEntryControlPrevCodePointSize(entry_control, entry_control->cur);

        if (entry_control->cur < sz_sub) {
            return true;
        }

        entry_control->cur -= sz_sub;

        X11Toolkit_EraseFromEntryControlBuffer(entry_control, entry_control->cur, sz_sub);
        X11Toolkit_ClearEntryControlSelection(entry_control);
        return true;
    }

    case XK_Delete:
    {
        size_t sz_sub;

        if (entry_control->sz == 0) {
            return true;
        }

        // erase the selection if its present
        if (entry_control->sel_dir != SDL_TOOLKIT_ENTRY_SELECTION_NONE) {
            X11Toolkit_EraseEntryControlSelection(entry_control);
            return true;
        }

        sz_sub = X11Tookit_FindEntryControlNextCodePointSize(entry_control, entry_control->cur);

        if (entry_control->cur + sz_sub > entry_control->sz) {
            return true;
        }

        X11Toolkit_EraseFromEntryControlBuffer(entry_control, entry_control->cur, sz_sub);
        X11Toolkit_ClearEntryControlSelection(entry_control);
        return true;
    }

    // Ctrl + A
    case XK_a:
    {
        if (!(keystate & ControlMask)) {
            break;
        }

        entry_control->sel_dir = SDL_TOOLKIT_ENTRY_SELECTION_RIGHT;
        entry_control->sel = 0;
        entry_control->sel_end = entry_control->sz;
        entry_control->cur = entry_control->sz;
        entry_control->old_cur = entry_control->sz;
        return true;
    }

    // Ctrl + X / Ctrl + C
    case XK_x:
    case XK_c:
    {
        size_t sel_sz;

        if (!(keystate & ControlMask)) {
            break;
        }

        if (entry_control->sel_dir == SDL_TOOLKIT_ENTRY_SELECTION_NONE) {
            break;
        }

        if (entry_control->clip) {
            SDL_free(entry_control->clip);
        }

        sel_sz = entry_control->sel_end - entry_control->sel;

        // copy clipboard text
        entry_control->clip = SDL_malloc(sel_sz);
        entry_control->clip_sz = sel_sz;
        SDL_memcpy(entry_control->clip, &entry_control->buffer[entry_control->sel], sel_sz);

        // send request, response handeled in X11Toolkit_ProcessSetEntryControlClipboard
        X11_XSetSelectionOwner(control->window->display, entry_control->atom_clip, control->window->window, CurrentTime);

        // only erase for Ctrl + X
        if (keysym == XK_x) {
            X11Toolkit_EraseEntryControlSelection(entry_control);
        }

        return true;
    }

    // Ctrl + V
    case XK_v:
    {
        if (!(keystate & ControlMask)) {
            break;
        }

        // send request, response handeled in X11Toolkit_ProcessGetEntryControlClipboard
        X11_XConvertSelection(control->window->display, entry_control->atom_clip, entry_control->atom_type, entry_control->atom_prop, control->window->window, CurrentTime);
        return true;
    }

    case XK_Tab:
    case XK_Return:
    case XK_Escape:
    case XK_Control_L:
    case XK_Control_R:
    case XK_Shift_L:
    case XK_Shift_R:
    case XK_Up:
    case XK_Down:
        /* Just ignore these keys for now. */
        return false;

    default:
        break;
    }

    if (keystate & ControlMask) {
        return false;
    }

    str_sz = 256;
    str = SDL_malloc(str_sz);

#ifdef X_HAVE_UTF8_STRING
    if (control->window->utf8) {
        if (control->window->im) {
            Status status;

            sz = X11_Xutf8LookupString(control->window->ic, xkey, str, str_sz - 1, &keysym, &status);
            if (status == XBufferOverflow) {
                str = SDL_realloc(str, sz + 1);
                sz = X11_Xutf8LookupString(control->window->ic, xkey, str, sz, &keysym, &status);
            }
        } else {
            sz = X11_XLookupStringAsUTF8(xkey, str, sizeof(str), NULL, NULL);
        }
    } else
#endif
    {
        sz = X11_XLookupString(xkey, str, sizeof(str), NULL, NULL);
    }

    // erase the selection if its present
    if (entry_control->sel_dir != SDL_TOOLKIT_ENTRY_SELECTION_NONE) {
        X11Toolkit_EraseEntryControlSelection(entry_control);
    }

    entry_control->cur += X11Toolkit_InsertIntoEntryControlBuffer(entry_control, str, entry_control->cur, sz);
    SDL_free(str);

    X11Toolkit_ClearEntryControlSelection(entry_control);
    return true;
}

static bool X11Toolkit_ProcessEntryControlMouseEvent(SDL_ToolkitControlX11 *control, SDL_ToolkitEntryControlX11 *entry_control)
{
    switch (control->window->e->type) {
    case ButtonPress:
    {
        XButtonEvent *xbutton;
        int x, xx1;
        int width, height, ascent, descent;
        size_t i;

        xbutton = &control->window->e->xbutton;
        x = xbutton->x;

        if (xbutton->button != Button1) {
            entry_control->sel_held = false;
            return false;
        }

        i = entry_control->clip_start;
        while (i < entry_control->clip_end) {
            size_t sz;

            X11Toolkit_GetTextWidthHeight(control->window, &entry_control->buffer[entry_control->clip_start], i - entry_control->clip_start, &width, &height, &ascent, &descent, NULL);
            xx1 = control->rect.x + entry_control->text_x + width;

            // cursor's on that character, return
            if (x <= xx1) {
                break;
            }

            sz = X11Tookit_FindEntryControlNextCodePointSize(entry_control, i);

            // codepoint not found
            if (!sz) {
                break;
            }

            i += sz;
        }

        // If shift is held, we instead do quick-select
        if (xbutton->state & ShiftMask) {
            if (entry_control->cur <= i) {
                entry_control->sel_dir = SDL_TOOLKIT_ENTRY_SELECTION_RIGHT;
                entry_control->sel = entry_control->cur;
                entry_control->sel_end = i;
            } else {

                entry_control->sel_dir = SDL_TOOLKIT_ENTRY_SELECTION_LEFT;
                entry_control->sel_end = entry_control->cur;
                entry_control->sel = i;
            }
        } else {
            entry_control->cur = i;
            X11Toolkit_ClearEntryControlSelection(entry_control);
        }

        entry_control->sel_held = true;
        entry_control->sel_held_x = x;
        entry_control->sel_held_i = entry_control->cur;
        break;
    }

    case ButtonRelease:
    {
        XButtonEvent *xbutton;
        xbutton = &control->window->e->xbutton;

        if (xbutton->button != Button1) {
            entry_control->sel_held = false;
            return false;
        }

        entry_control->sel_held = false;
        break;
    }

    case MotionNotify:
    {
        XMotionEvent *xmotion;
        int x, i, xx1;
        int width, height, ascent, descent;

        xmotion = &control->window->e->xmotion;
        x = xmotion->x;

        if (!(xmotion->state & Button1Mask)) {
            entry_control->sel_held = false;
            return false;
        }

        i = entry_control->clip_start;
        while (i < entry_control->clip_end) {
            size_t sz;

            X11Toolkit_GetTextWidthHeight(control->window, &entry_control->buffer[entry_control->clip_start], i - entry_control->clip_start, &width, &height, &ascent, &descent, NULL);
            xx1 = control->rect.x + entry_control->text_x + width;

            // cursor's on that character, return
            if (x <= xx1) {
                break;
            }

            sz = X11Tookit_FindEntryControlNextCodePointSize(entry_control, i);

            // codepoint not found
            if (!sz) {
                break;
            }

            i += sz;
        }

        // set the direction relative to start of selection
        entry_control->sel_dir = entry_control->sel_held_x <= x
                                     ? SDL_TOOLKIT_ENTRY_SELECTION_LEFT
                                     : SDL_TOOLKIT_ENTRY_SELECTION_RIGHT;

        if (entry_control->sel_dir == SDL_TOOLKIT_ENTRY_SELECTION_LEFT) {
            entry_control->sel = entry_control->sel_held_i;
            entry_control->sel_end = i;
        } else {
            entry_control->sel = i;
            entry_control->sel_end = entry_control->sel_held_i;
        }
        break;
    }
    }

    return true;
}

static bool X11Toolkit_ProcessEntryControlClipboard(SDL_ToolkitControlX11 *control, SDL_ToolkitEntryControlX11 *entry_control)
{
    if (control->window->e->type == SelectionRequest) { // Copying
        const Atom types[2] = { XA_STRING, entry_control->atom_type };
        const XSelectionRequestEvent *request;
        size_t types_sz;
        XSelectionEvent event;

        request = &control->window->e->xselectionrequest;
        types_sz = 1;

#ifdef X_HAVE_UTF8_STRING
        types_sz += (size_t)control->window->utf8;
#endif

        SDL_zero(event);
        event.type = SelectionNotify;
        event.display = request->display;
        event.requestor = request->requestor;
        event.selection = request->selection;
        event.time = request->time;
        event.target = request->target;
        event.property = request->property;

        // Send the type atoms that we support
        if (request->target == entry_control->atom_targets) {
            X11_XChangeProperty(control->window->display, request->requestor, request->property, XA_ATOM, 32,
                                PropModeReplace, (unsigned char *)types, types_sz);
        }

        // Send the clipboard contents (Freeing is handled elsewhere)
        else if (request->target == entry_control->atom_type || request->target == XA_STRING) {
            X11_XChangeProperty(control->window->display, request->requestor, request->property, request->target, 8,
                                PropModeReplace, (unsigned char *)entry_control->clip, entry_control->clip_sz);
        }

        X11_XSendEvent(control->window->display, request->requestor, False, 0, (XEvent *)&event);
        X11_XFlush(control->window->display);

    } else { // Pasting
        Atom atom_type;
        int format;
        unsigned long sz, after_sz;
        unsigned char *value = NULL;

        // Try to fetch clipboard text
        X11_XGetWindowProperty(control->window->display, control->window->window, entry_control->atom_prop, 0, -1, False,
                               AnyPropertyType, &atom_type, &format,
                               &sz, &after_sz, &value);

        if (!value) {
            return false;
        }

        if (entry_control->sel_dir != SDL_TOOLKIT_ENTRY_SELECTION_NONE) {
            X11Toolkit_EraseEntryControlSelection(entry_control);
        }

        entry_control->cur += X11Toolkit_InsertIntoEntryControlBuffer(entry_control, (const char *)value, entry_control->cur, sz);
        X11_XFree(value);
    }

    return true;
}

static bool X11Toolkit_ProcessEntryControlEvent(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitEntryControlX11 *entry_control;

    entry_control = (SDL_ToolkitEntryControlX11 *)control;
    entry_control->old_sz = entry_control->sz;
    entry_control->old_cur = entry_control->cur;

    switch (control->window->e->type) {
    case KeyPress:
        if (!X11Toolkit_ProcessEntryControlKeyPress(control, entry_control)) {
            return false;
        }
        break;

    /* Mouse events */
    case ButtonPress:
    case ButtonRelease:
    case MotionNotify:
        if (!X11Toolkit_ProcessEntryControlMouseEvent(control, entry_control)) {
            return false;
        }
        break;

    /* Copy / Paste */
    case SelectionRequest:
    case SelectionNotify:
        if (!X11Toolkit_ProcessEntryControlClipboard(control, entry_control)) {
            return false;
        }
        break;

    default:
        return false; /* Don't update the control and ignore the event */
    }

    X11Toolkit_ScrollEntryControl(control);
    X11Toolkit_DrawWindow(control->window);
    return true;
}

static Uint32 X11Toolkit_BlinkEntryControlCursor(void *userdata, SDL_TimerID timerID, Uint32 interval)
{
    SDL_ToolkitEntryControlX11 *entry_control;
    SDL_ToolkitControlX11 *base_control;
    XExposeEvent event;

    entry_control = (SDL_ToolkitEntryControlX11 *)userdata;
    base_control = (SDL_ToolkitControlX11 *)userdata;

    entry_control->cur_blink = !entry_control->cur_blink;

    SDL_memset(&event, 0, sizeof(XExposeEvent));
    event.display = base_control->window->display;
    event.window = base_control->window->window;
    event.type = Expose;
    event.count = 1;
    X11_XSendEvent(base_control->window->display, base_control->window->window, False, ExposureMask, (XEvent *)&event);
    X11_XFlush(base_control->window->display);

    return interval;
}

static void X11Toolkit_OnEntryControlStateChange(SDL_ToolkitControlX11 *base_control)
{
    switch (base_control->state) {
    case SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL:
        X11_XDefineCursor(base_control->window->display, base_control->window->window, base_control->window->cursor_normal);
        break;

    case SDL_TOOLKIT_CONTROL_STATE_X11_HOVER:
    case SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED_HELD:
        X11_XDefineCursor(base_control->window->display, base_control->window->window, base_control->window->cursor_text_edit);
        break;

    case SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED:
        X11_XDefineCursor(base_control->window->display, base_control->window->window, base_control->window->cursor_text_edit);
        if (base_control->window->focused_control) {
            base_control->window->focused_control->selected = false;
        }
        base_control->selected = true;
        base_control->window->focused_control = base_control;
        break;

    default:
        break;
    }
}

char *X11Toolkit_GetEntryControlText(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitEntryControlX11 *entry_control;
    char *str;

    entry_control = (SDL_ToolkitEntryControlX11 *)control;

    str = SDL_calloc(entry_control->sz + 1, sizeof(char));
    SDL_memcpy(str, entry_control->buffer, entry_control->sz);
    return str;
}

void X11Toolkit_SetEntryControlText(SDL_ToolkitControlX11 *control, const char *str)
{
    SDL_ToolkitEntryControlX11 *entry_control;
    size_t sz;

    entry_control = (SDL_ToolkitEntryControlX11 *)control;
    sz = SDL_strlen(str);

    if (entry_control->buffer) {
        SDL_free(entry_control->buffer);
    }

    entry_control->buffer = SDL_malloc(sz);
    SDL_memcpy(entry_control->buffer, str, sz);

    entry_control->sz = sz;
    entry_control->cur = 0;
    X11Toolkit_ScrollEntryControl(control);
    X11Toolkit_DrawWindow(control->window);
}

static void X11Toolkit_DestroyEntryControl(SDL_ToolkitControlX11 *control)
{
    SDL_ToolkitEntryControlX11 *entry_control;

    entry_control = (SDL_ToolkitEntryControlX11 *)control;
    SDL_RemoveTimer(entry_control->cur_blink_timer);

    if (entry_control->buffer) {
        SDL_free(entry_control->buffer);
    }

    if (entry_control->clip) {
        SDL_free(entry_control->clip);
    }

    SDL_free(entry_control);
}

/* Special thanks to m-doescode on Github for helping me with the entry control */
/* TODO: Move cursor with mouse, selections (both with mouse and shift+arrow keys), clipboard */
SDL_ToolkitControlX11 *X11Toolkit_CreateEntryControl(SDL_ToolkitWindowX11 *window)
{
    SDL_ToolkitEntryControlX11 *control;
    SDL_ToolkitControlX11 *base_control;

    control = (SDL_ToolkitEntryControlX11 *)SDL_malloc(sizeof(SDL_ToolkitEntryControlX11));
    base_control = (SDL_ToolkitControlX11 *)control;
    if (!control) {
        SDL_SetError("Unable to allocate entry control structure");
        return NULL;
    }

    base_control->window = window;
    base_control->func_calc_size = X11Toolkit_CalculateEntryControl;
    base_control->func_draw = X11Toolkit_DrawEntryControl;
    base_control->func_on_state_change = X11Toolkit_OnEntryControlStateChange;
    base_control->func_free = X11Toolkit_DestroyEntryControl;
    base_control->func_on_scale_change = NULL;
    base_control->func_process_event = X11Toolkit_ProcessEntryControlEvent;
    base_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
    base_control->selected = true;
    base_control->dynamic = true;
    base_control->is_default_enter = false;
    base_control->is_default_esc = false;
    base_control->captures_lr_arrows = true;
    base_control->special_focus = true;
    control->buffer = NULL;
    control->sz = 0;
    control->cur = 0;
    control->old_cur = 0;
    control->cur_x = 0;
    control->cur_blink = true;
    control->cur_blink_timer = SDL_AddTimer(500, X11Toolkit_BlinkEntryControlCursor, control);

    /* selection */
    control->sel_dir = SDL_TOOLKIT_ENTRY_SELECTION_NONE;
    control->sel = 0;
    control->sel_end = 0;
    control->sel_x = 0;
    control->sel_w = 0;
    control->sel_h = 0;
    control->sel_held = false;
    control->sel_held_x = 0;
    control->sel_held_i = 0;

    /* paging */
    control->old_sz = control->sz;
    control->start_offset = 0;
    control->clip_offset = 0;
    control->clip_start = 0;
    control->clip_end = 0;

    /* clipboard */
    control->atom_clip = X11_XInternAtom(base_control->window->display, "CLIPBOARD", False);
    control->atom_prop = X11_XInternAtom(base_control->window->display, "XSEL_DATA", False);
    control->atom_targets = X11_XInternAtom(base_control->window->display, "TARGETS", False);
    control->atom_type = XA_STRING;
    control->clip = NULL;
    control->clip_sz = 0;

    // request utf8 strings, if supported
#ifdef X_HAVE_UTF8_STRING
    if (base_control->window->utf8) {
        control->atom_type = X11_XInternAtom(base_control->window->display, "UTF8_STRING", False);
    }
#endif

    X11Toolkit_CalculateEntryControl(base_control);
    X11Toolkit_AddControlToWindow(window, base_control);
    return base_control;
}

static void X11Toolkit_DrawSliderControl(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitSliderControlX11 *control;

    control = (SDL_ToolkitSliderControlX11 *)base_control;

    /* background */
    X11_XSetTile(base_control->window->display, base_control->window->ctx, control->bg);
    X11_XSetFillStyle(base_control->window->display, base_control->window->ctx, FillTiled);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x, base_control->rect.y, base_control->rect.w, base_control->rect.h);
    X11_XSetFillStyle(base_control->window->display, base_control->window->ctx, FillSolid);

    /* slider */
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor_bevel_d.pixel);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->handle_rect.x, base_control->rect.y + control->handle_rect.y, control->handle_rect.w, control->handle_rect.h);
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor_bevel_l2.pixel);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->handle_rect.x, base_control->rect.y + control->handle_rect.y, control->handle_rect.w - 1 * base_control->window->iscale, control->handle_rect.h - 1 * base_control->window->iscale);
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].pixel);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->handle_rect.x + 1 * base_control->window->iscale, base_control->rect.y + control->handle_rect.y + 1 * base_control->window->iscale, control->handle_rect.w - 2 * base_control->window->iscale, control->handle_rect.h - 2 * base_control->window->iscale);
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor_bevel_l1.pixel);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->handle_rect.x + 1 * base_control->window->iscale, base_control->rect.y + control->handle_rect.y + 1 * base_control->window->iscale, control->handle_rect.w - 3 * base_control->window->iscale, control->handle_rect.h - 3 * base_control->window->iscale);
    switch (control->handle_state) {
    case SDL_TOOLKIT_CONTROL_STATE_X11_HOVER:
        X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED].pixel);
        break;
    case SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED_HELD:
    case SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED:
        X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor_pressed.pixel);
        break;
    default:
        X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND].pixel);
        break;
    }
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->handle_rect.x + 2 * base_control->window->iscale, base_control->rect.y + control->handle_rect.y + 2 * base_control->window->iscale, control->handle_rect.w - 4 * base_control->window->iscale, control->handle_rect.h - 4 * base_control->window->iscale);
}

static void X11Toolkit_CalculateSliderControl(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitSliderControlX11 *control;

    control = (SDL_ToolkitSliderControlX11 *)base_control;
    if (!control->horiz) {
        if (base_control->do_size) {
            base_control->rect.x = base_control->rect.y = 0;
            base_control->rect.w = 15 * base_control->window->iscale;
            base_control->rect.h = 50 * base_control->window->iscale;
        }
        control->handle_rect.w = base_control->rect.w;
        control->handle_rect.h = 15 * base_control->window->iscale;
        control->handle_rect.x = 0;
        control->handle_rect.y = 30;
    } else {
        if (base_control->do_size) {
            base_control->rect.x = base_control->rect.y = 0;
            base_control->rect.w = 50 * base_control->window->iscale;
            base_control->rect.h = 15 * base_control->window->iscale;
        }
        control->handle_rect.w = 15 * base_control->window->iscale;
        control->handle_rect.h = base_control->rect.h;
        control->handle_rect.x = 30;
        control->handle_rect.y = 0;
    }
}

void X11Toolkit_SetSliderControlSize(SDL_ToolkitControlX11 *base_control, int real, int reserved)
{
    SDL_ToolkitSliderControlX11 *control;
    double ratio;

    control = (SDL_ToolkitSliderControlX11 *)base_control;
    control->handle_rect.x = 0;
    control->handle_rect.y = 0;
    if (control->horiz) {
        ratio = (double)base_control->rect.w / (double)real;
        control->handle_rect.w = SDL_max(SDL_lround((double)reserved * ratio), 3 * base_control->window->iscale);
        if (control->handle_rect.w > base_control->rect.w) {
            control->handle_rect.w = base_control->rect.w;
        }
    } else {
        ratio = (double)base_control->rect.h / (double)real;
        control->handle_rect.h = SDL_max(SDL_lround((double)reserved * ratio), 3 * base_control->window->iscale);
        if (control->handle_rect.h > base_control->rect.h) {
            control->handle_rect.h = base_control->rect.h;
        }
    }
}

void X11Toolkit_RegisterSliderControlCallback(SDL_ToolkitControlX11 *control, void *data, void (*cb)(SDL_ToolkitControlX11 *, void *data, int real, int reserved, int offset))
{
    SDL_ToolkitSliderControlX11 *slider_control;

    slider_control = (SDL_ToolkitSliderControlX11 *)control;
    slider_control->user_data = data;
    slider_control->callback = cb;
}

static void X11Toolkit_OnSliderControlScaleChange(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitSliderControlX11 *control;
    GC ctx;
    XGCValues ctx_vals;

    control = (SDL_ToolkitSliderControlX11 *)base_control;
    if (control->bg != None) {
        X11_XFreePixmap(base_control->window->display, control->bg);
    }
    control->bg = X11_XCreatePixmap(base_control->window->display, RootWindow(base_control->window->display, base_control->window->screen), 2 * base_control->window->iscale, 2 * base_control->window->iscale, base_control->window->depth);
    SDL_zero(ctx_vals);
    ctx_vals.foreground = base_control->window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].pixel;
    ctx = X11_XCreateGC(base_control->window->display, control->bg, GCForeground, &ctx_vals);
    X11_XFillRectangle(base_control->window->display, control->bg, ctx, 0, 0, 1 * base_control->window->iscale, 1 * base_control->window->iscale);
    X11_XFillRectangle(base_control->window->display, control->bg, ctx, 1 * base_control->window->iscale, 1 * base_control->window->iscale, 1 * base_control->window->iscale, 1 * base_control->window->iscale);
    X11_XSetForeground(base_control->window->display, ctx, base_control->window->xcolor_light_control_bg.pixel);
    X11_XFillRectangle(base_control->window->display, control->bg, ctx, 1 * base_control->window->iscale, 0, 1 * base_control->window->iscale, 1 * base_control->window->iscale);
    X11_XFillRectangle(base_control->window->display, control->bg, ctx, 0, 1 * base_control->window->iscale, 1 * base_control->window->iscale, 1 * base_control->window->iscale);
    X11_XFreeGC(base_control->window->display, ctx);
}

static void X11Toolkit_OnSliderControlStateChange(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitSliderControlX11 *control;
    int x;
    int y;
    bool on_handle;

    control = (SDL_ToolkitSliderControlX11 *)base_control;
    x = SDL_lroundf((base_control->window->e->xbutton.x / base_control->window->ev_scale) * base_control->window->ev_iscale);
    y = SDL_lroundf((base_control->window->e->xbutton.y / base_control->window->ev_scale) * base_control->window->ev_iscale);
    if ((x >= base_control->rect.x + control->handle_rect.x) && (x <= (base_control->rect.x + control->handle_rect.x + control->handle_rect.w)) && (y >= base_control->rect.y + control->handle_rect.y) && (y <= (base_control->rect.y + control->handle_rect.y + control->handle_rect.h))) {
        on_handle = true;
    } else {
        on_handle = false;
    }

    x -= base_control->rect.x;
    y -= base_control->rect.y;
    switch (base_control->state) {
    case SDL_TOOLKIT_CONTROL_STATE_X11_HOVER:
    case SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED_HELD:
        if (on_handle) {
            control->handle_state = base_control->state;
        }
        break;
    case SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED:
        if (!on_handle) {
            if (control->horiz) {
                control->handle_rect.x = x;
                if (control->handle_rect.x + control->handle_rect.w > base_control->rect.w) {
                    control->handle_rect.x = base_control->rect.w - control->handle_rect.w;
                } else if (control->handle_rect.x < 0) {
                    control->handle_rect.x = 0;
                }
                if (control->callback) {
                    control->callback(base_control, control->user_data, base_control->rect.w, control->handle_rect.w, control->handle_rect.x);
                }
            } else {
                control->handle_rect.y = y;
                if (control->handle_rect.y + control->handle_rect.h > base_control->rect.h) {
                    control->handle_rect.y = base_control->rect.h - control->handle_rect.h;
                } else if (control->handle_rect.y < 0) {
                    control->handle_rect.y = 0;
                }
                if (control->callback) {
                    control->callback(base_control, control->user_data, base_control->rect.h, control->handle_rect.h, control->handle_rect.y);
                }
            }
        } else {
            control->handle_state = SDL_TOOLKIT_CONTROL_STATE_X11_PRESSED;
        }
        break;
    default:
        control->handle_state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
        break;
    }
}

static void X11Toolkit_DestroySliderControl(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitSliderControlX11 *control;

    control = (SDL_ToolkitSliderControlX11 *)base_control;
    X11_XFreePixmap(base_control->window->display, control->bg);
    SDL_free(base_control);
}

SDL_ToolkitControlX11 *X11Toolkit_CreateSliderControl(SDL_ToolkitWindowX11 *window, bool horiz)
{
    SDL_ToolkitSliderControlX11 *control;
    SDL_ToolkitControlX11 *base_control;

    control = (SDL_ToolkitSliderControlX11 *)SDL_malloc(sizeof(SDL_ToolkitSliderControlX11));
    base_control = (SDL_ToolkitControlX11 *)control;
    if (!control) {
        SDL_SetError("Unable to allocate slider control structure");
        return NULL;
    }

    base_control->window = window;
    base_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
    base_control->func_calc_size = X11Toolkit_CalculateSliderControl;
    base_control->func_draw = X11Toolkit_DrawSliderControl;
    base_control->func_on_state_change = X11Toolkit_OnSliderControlStateChange;
    base_control->func_free = X11Toolkit_DestroySliderControl;
    base_control->func_on_scale_change = X11Toolkit_OnSliderControlScaleChange;
    base_control->func_process_event = NULL;
    base_control->selected = false;
    base_control->dynamic = true;
    base_control->is_default_enter = false;
    base_control->is_default_esc = false;
    base_control->captures_lr_arrows = false;
    base_control->special_focus = false;

    control->horiz = horiz;
    control->handle_state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
    control->bg = None;
    control->callback = NULL;
    control->user_data = NULL;
    X11Toolkit_OnSliderControlScaleChange(base_control);
    base_control->do_size = true;
    X11Toolkit_CalculateSliderControl(base_control);
    base_control->do_size = false;

    X11Toolkit_AddControlToWindow(window, base_control);
    return base_control;
}

void X11Toolkit_ElevateSliderControl(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitSliderControlX11 *control;

    control = (SDL_ToolkitSliderControlX11 *)base_control;
    if (!control->horiz) {
        control->handle_rect.y -= 10 * base_control->window->iscale;
        if (control->handle_rect.y < 0) {
            control->handle_rect.y = 0;
        }
        if (control->callback) {
            control->callback(base_control, control->user_data, base_control->rect.h, control->handle_rect.h, control->handle_rect.y);
        }
    } else {
        control->handle_rect.x += 10 * base_control->window->iscale;
        if (control->handle_rect.x + control->handle_rect.w > base_control->rect.w) {
            control->handle_rect.x = base_control->rect.w - control->handle_rect.w;
        }
        if (control->callback) {
            control->callback(base_control, control->user_data, base_control->rect.w, control->handle_rect.w, control->handle_rect.x);
        }
    }
}

void X11Toolkit_DropSliderControl(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitSliderControlX11 *control;

    control = (SDL_ToolkitSliderControlX11 *)base_control;
    if (!control->horiz) {
        control->handle_rect.y += 10 * base_control->window->iscale;
        if (control->handle_rect.y + control->handle_rect.h > base_control->rect.h) {
            control->handle_rect.y = base_control->rect.h - control->handle_rect.h;
        }
        if (control->callback) {
            control->callback(base_control, control->user_data, base_control->rect.h, control->handle_rect.h, control->handle_rect.y);
        }
    } else {
        control->handle_rect.x -= 10 * base_control->window->iscale;
        if (control->handle_rect.x < 0) {
            control->handle_rect.x = 0;
        }
        if (control->callback) {
            control->callback(base_control, control->user_data, base_control->rect.w, control->handle_rect.w, control->handle_rect.x);
        }
    }
}

static void X11Toolkit_DrawPanControl(SDL_ToolkitControlX11 *control)
{
    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_l2.pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x, control->rect.y, control->rect.w, control->rect.h);

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x, control->rect.y, control->rect.w - (1 * control->window->iscale), control->rect.h - (1 * control->window->iscale));

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_l1.pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (1 * control->window->iscale), control->rect.y + (1 * control->window->iscale), control->rect.w - (2 * control->window->iscale), control->rect.h - (2 * control->window->iscale));

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_d.pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (1 * control->window->iscale), control->rect.y + (1 * control->window->iscale), control->rect.w - (3 * control->window->iscale), control->rect.h - (3 * control->window->iscale));

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_light_control_bg.pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (2 * control->window->iscale), control->rect.y + (2 * control->window->iscale), control->rect.w - (4 * control->window->iscale), control->rect.h - (4 * control->window->iscale));
}

extern void X11Toolkit_GetPanControlInnerArea(SDL_ToolkitControlX11 *control, SDL_Rect *rect)
{
    rect->x = control->rect.x + (2 * control->window->iscale);
    rect->y = control->rect.y + (2 * control->window->iscale);
    rect->w = control->rect.w - (4 * control->window->iscale);
    rect->h = control->rect.h - (4 * control->window->iscale);
}

SDL_ToolkitControlX11 *X11Toolkit_CreatePanControl(SDL_ToolkitWindowX11 *window)
{
    SDL_ToolkitControlX11 *base_control;

    base_control = (SDL_ToolkitControlX11 *)SDL_malloc(sizeof(SDL_ToolkitControlX11));
    if (!base_control) {
        SDL_SetError("Unable to allocate pan control structure");
        return NULL;
    }

    base_control->window = window;
    base_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
    base_control->func_calc_size = NULL;
    base_control->func_draw = X11Toolkit_DrawPanControl;
    base_control->func_on_state_change = NULL;
    base_control->func_free = X11Toolkit_DestroyGenericControl;
    base_control->func_on_scale_change = NULL;
    base_control->func_process_event = NULL;
    base_control->selected = false;
    base_control->dynamic = false;
    base_control->is_default_enter = false;
    base_control->is_default_esc = false;
    base_control->captures_lr_arrows = false;
    base_control->special_focus = false;

    X11Toolkit_AddControlToWindow(window, base_control);
    return base_control;
}

static void X11Toolkit_DrawBlockControl(SDL_ToolkitControlX11 *control)
{
    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor_bevel_d.pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x, control->rect.y, control->rect.w, control->rect.h);

    X11_XSetForeground(control->window->display, control->window->ctx, control->window->xcolor[SDL_MESSAGEBOX_COLOR_BACKGROUND].pixel);
    X11_XFillRectangle(control->window->display, control->window->drawable, control->window->ctx, control->rect.x + (1 * control->window->iscale), control->rect.y + (1 * control->window->iscale), control->rect.w - (1 * control->window->iscale), control->rect.h - (1 * control->window->iscale));
}

SDL_ToolkitControlX11 *X11Toolkit_CreateBlockControl(SDL_ToolkitWindowX11 *window)
{
    SDL_ToolkitControlX11 *base_control;

    base_control = (SDL_ToolkitControlX11 *)SDL_malloc(sizeof(SDL_ToolkitControlX11));
    if (!base_control) {
        SDL_SetError("Unable to allocate pan control structure");
        return NULL;
    }

    base_control->window = window;
    base_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
    base_control->func_calc_size = NULL;
    base_control->func_draw = X11Toolkit_DrawBlockControl;
    base_control->func_on_state_change = NULL;
    base_control->func_free = X11Toolkit_DestroyGenericControl;
    base_control->func_on_scale_change = NULL;
    base_control->func_process_event = NULL;
    base_control->selected = false;
    base_control->dynamic = false;
    base_control->is_default_enter = false;
    base_control->is_default_esc = false;
    base_control->captures_lr_arrows = false;
    base_control->special_focus = false;

    X11Toolkit_AddControlToWindow(window, base_control);
    return base_control;
}

static void X11Toolkit_CalculateListControl(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitListControlX11 *control;
    int ascent;
    int descent;

    control = (SDL_ToolkitListControlX11 *)base_control;
    X11Toolkit_GetTextWidthHeight(base_control->window, control->header, control->header_sz, &control->header_text_rect.w, &control->header_text_rect.h, &ascent, &descent, NULL);
    control->header_rect.x = 0;
    control->header_rect.y = 0;
    control->header_rect.w = control->header_text_rect.w + SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * 2 * base_control->window->iscale;
    control->header_rect.h = control->header_text_rect.h + SDL_TOOLKIT_X11_ELEMENT_PADDING * 2 * base_control->window->iscale;
    control->header_text_rect.y = (control->header_rect.h - control->header_text_rect.h) / 2 + ascent;
    control->header_text_rect.x = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * base_control->window->iscale;
    if (base_control->do_size) {
        base_control->rect.w = control->header_rect.w;
        base_control->rect.h = control->header_rect.h;
    } else {
        control->header_rect.w = SDL_max(control->header_rect.w, base_control->rect.w);
    }

    control->item_area_rect.y = control->header_rect.y + control->header_rect.h;
    control->item_area_reserved_rect.w = base_control->rect.w;
    control->item_area_reserved_rect.h = base_control->rect.h - control->header_rect.h;
    control->item_area_rect.w = 2;
    control->item_area_rect.h = 2;
    control->item_area_rect.x = 0;
    control->item_area_reserved_rect.x = 0;
    control->item_area_reserved_rect.y = 0;
    if (control->items) {
        SDL_ListNode *cursor;
        SDL_ToolkitListItemX11 *last_item;

        last_item = NULL;
        cursor = control->items;
        control->item_area_rect.h = 0;
        while (cursor) {
            SDL_ToolkitListItemX11 *item;
            int pad;

            item = cursor->entry;

            item->utf8_len = SDL_strlen(item->utf8);
            X11Toolkit_GetTextWidthHeight(base_control->window, item->utf8, item->utf8_len, &item->text_rect.w, &item->text_rect.h, &ascent, &descent, NULL);
            switch (item->icon) {
            case SDL_TOOLKIT_ICON_X11_UP_ARROW:
                if (!control->xcolor_green.flags) {
                    control->xcolor_green.flags = DoRed | DoGreen | DoBlue;
                    X11_XAllocColor(base_control->window->display, base_control->window->cmap, &control->xcolor_green);
                }
                if (!control->xcolor_black.flags) {
                    control->xcolor_black.flags = DoRed | DoGreen | DoBlue;
                    X11_XAllocColor(base_control->window->display, base_control->window->cmap, &control->xcolor_black);
                }
                item->icon_rect.w = item->icon_rect.h = 16 * base_control->window->iscale;
                pad = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * base_control->window->iscale;
                break;
            case SDL_TOOLKIT_ICON_X11_FILE:
                if (!control->xcolor_white.flags) {
                    control->xcolor_white.flags = DoRed | DoGreen | DoBlue;
                    X11_XAllocColor(base_control->window->display, base_control->window->cmap, &control->xcolor_white);
                }
                if (!control->xcolor_black.flags) {
                    control->xcolor_black.flags = DoRed | DoGreen | DoBlue;
                    X11_XAllocColor(base_control->window->display, base_control->window->cmap, &control->xcolor_black);
                }
                item->icon_rect.w = item->icon_rect.h = 16 * base_control->window->iscale;
                pad = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * base_control->window->iscale;
                break;
            case SDL_TOOLKIT_ICON_X11_FOLDER:
                if (!control->xcolor_cream.flags) {
                    control->xcolor_cream.flags = DoRed | DoGreen | DoBlue;
                    X11_XAllocColor(base_control->window->display, base_control->window->cmap, &control->xcolor_cream);
                }
                if (!control->xcolor_black.flags) {
                    control->xcolor_black.flags = DoRed | DoGreen | DoBlue;
                    X11_XAllocColor(base_control->window->display, base_control->window->cmap, &control->xcolor_black);
                }
                item->icon_rect.w = item->icon_rect.h = 16 * base_control->window->iscale;
                pad = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * base_control->window->iscale;
                break;
            default:
                pad = 0;
                item->icon_rect.w = item->icon_rect.h = 0;
            }

            item->rect.x = 0;
            item->rect.w = item->text_rect.w + SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * 2 * base_control->window->iscale + item->icon_rect.w + pad;
            item->rect.h = SDL_max(item->text_rect.h, item->icon_rect.h) + SDL_TOOLKIT_X11_ELEMENT_PADDING * 2 * base_control->window->iscale;
            item->text_rect.y = (item->rect.h - item->text_rect.h) / 2 + ascent;
            item->text_rect.x = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * base_control->window->iscale + item->icon_rect.w + pad;
            if (item->icon_rect.w) {
                item->icon_rect.x = SDL_TOOLKIT_X11_ELEMENT_PADDING_3 * base_control->window->iscale;
                item->icon_rect.y = (item->rect.h - item->icon_rect.h) / 2;
            }
            if (last_item) {
                item->rect.y = last_item->rect.y + item->rect.h;
            } else {
                item->rect.y = 0;
            }
            control->item_area_rect.h += item->rect.h;
            control->item_area_rect.w = SDL_max(control->item_area_rect.w, item->rect.w);

            last_item = item;
            cursor = cursor->next;
        }
    }

    if (control->item_area != None) {
        X11_XFreePixmap(base_control->window->display, control->item_area);
    }
    control->item_area = X11_XCreatePixmap(base_control->window->display, RootWindow(base_control->window->display, base_control->window->screen), control->item_area_rect.w, control->item_area_rect.h, base_control->window->depth);
    control->item_area_rendered = false;
}

static void X11Toolkit_DrawListControl(SDL_ToolkitControlX11 *base_control)
{
    SDL_ToolkitListControlX11 *control;

    control = (SDL_ToolkitListControlX11 *)base_control;

    /* background */
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor_light_control_bg.pixel);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x, base_control->rect.y, base_control->rect.w, base_control->rect.h);

    /* header */
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor_bevel_d.pixel);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->header_rect.x, base_control->rect.y + control->header_rect.y, control->header_rect.w, control->header_rect.h);
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor_bevel_l2.pixel);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->header_rect.x, base_control->rect.y + control->header_rect.y, control->header_rect.w - 1 * base_control->window->iscale, control->header_rect.h - 1 * base_control->window->iscale);
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER].pixel);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->header_rect.x + 1 * base_control->window->iscale, base_control->rect.y + control->header_rect.y + 1 * base_control->window->iscale, control->header_rect.w - 2 * base_control->window->iscale, control->header_rect.h - 2 * base_control->window->iscale);
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor_bevel_l1.pixel);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->header_rect.x + 1 * base_control->window->iscale, base_control->rect.y + control->header_rect.y + 1 * base_control->window->iscale, control->header_rect.w - 3 * base_control->window->iscale, control->header_rect.h - 3 * base_control->window->iscale);
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor[SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND].pixel);
    X11_XFillRectangle(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->header_rect.x + 2 * base_control->window->iscale, base_control->rect.y + control->header_rect.y + 2 * base_control->window->iscale, control->header_rect.w - 4 * base_control->window->iscale, control->header_rect.h - 4 * base_control->window->iscale);
    X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].pixel);
#ifdef X_HAVE_UTF8_STRING
    if (base_control->window->utf8) {
        X11_Xutf8DrawString(base_control->window->display, base_control->window->drawable, base_control->window->font_set, base_control->window->ctx, base_control->rect.x + control->header_text_rect.x, base_control->rect.y + control->header_text_rect.y, control->header, control->header_sz);
    } else
#endif
    {
        X11_XDrawString(base_control->window->display, base_control->window->drawable, base_control->window->ctx, base_control->rect.x + control->header_text_rect.x, base_control->rect.y + control->header_text_rect.y, control->header, control->header_sz);
    }

    /* items */
    if (!control->item_area_rendered && control->items) {
        SDL_ListNode *cursor;

        X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor_light_control_bg.pixel);
        X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, 0, 0, control->item_area_rect.w, control->item_area_rect.h);

        cursor = control->items;
        while (cursor) {
            SDL_ToolkitListItemX11 *item;

            item = cursor->entry;

            switch (item->icon) {
            case SDL_TOOLKIT_ICON_X11_UP_ARROW:
            {
                XPoint points[3];

                X11_XSetForeground(base_control->window->display, base_control->window->ctx, control->xcolor_black.pixel);
                points[0].x = item->rect.x + item->icon_rect.x + item->icon_rect.w / 2;
                points[0].y = item->rect.y + item->icon_rect.y;
                points[1].x = item->rect.x + item->icon_rect.x;
                points[1].y = item->rect.y + item->icon_rect.y + item->icon_rect.h;
                points[2].x = item->rect.x + item->icon_rect.x + item->icon_rect.w;
                points[2].y = item->rect.y + item->icon_rect.y + item->icon_rect.h;
                X11_XFillPolygon(base_control->window->display, control->item_area, base_control->window->ctx, points, 3, Convex, CoordModeOrigin);

                X11_XSetForeground(base_control->window->display, base_control->window->ctx, control->xcolor_green.pixel);
                points[0].x = item->rect.x + item->icon_rect.x + item->icon_rect.w / 2;
                points[0].y = item->rect.y + item->icon_rect.y + 2 * base_control->window->iscale;
                points[1].x = item->rect.x + item->icon_rect.x + 2 * base_control->window->iscale;
                points[1].y = item->rect.y + item->icon_rect.y + item->icon_rect.h - 1 * base_control->window->iscale;
                points[2].x = item->rect.x + item->icon_rect.x + item->icon_rect.w - 2 * base_control->window->iscale;
                points[2].y = item->rect.y + item->icon_rect.y + item->icon_rect.h - 1 * base_control->window->iscale;

                X11_XFillPolygon(base_control->window->display, control->item_area, base_control->window->ctx, points, 3, Convex, CoordModeOrigin);
            } break;
            case SDL_TOOLKIT_ICON_X11_FILE:
                X11_XSetForeground(base_control->window->display, base_control->window->ctx, control->xcolor_black.pixel);
                X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->icon_rect.x, item->rect.y + item->icon_rect.y, item->icon_rect.w, item->icon_rect.h);

                X11_XSetForeground(base_control->window->display, base_control->window->ctx, control->xcolor_white.pixel);
                X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->icon_rect.x + 1 * base_control->window->iscale, item->rect.y + item->icon_rect.y + 1 * base_control->window->iscale, item->icon_rect.w - 2 * base_control->window->iscale, item->icon_rect.h - 2 * base_control->window->iscale);

                X11_XSetForeground(base_control->window->display, base_control->window->ctx, control->xcolor_black.pixel);
                X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->icon_rect.x + 3 * base_control->window->iscale, item->rect.y + item->icon_rect.y + 3 * base_control->window->iscale, item->icon_rect.w - 6 * base_control->window->iscale, 1 * base_control->window->iscale);
                X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->icon_rect.x + 3 * base_control->window->iscale, item->rect.y + item->icon_rect.y + 6 * base_control->window->iscale, item->icon_rect.w - 6 * base_control->window->iscale, 1 * base_control->window->iscale);
                X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->icon_rect.x + 3 * base_control->window->iscale, item->rect.y + item->icon_rect.y + 9 * base_control->window->iscale, item->icon_rect.w - 6 * base_control->window->iscale, 1 * base_control->window->iscale);
                X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->icon_rect.x + 3 * base_control->window->iscale, item->rect.y + item->icon_rect.y + 12 * base_control->window->iscale, item->icon_rect.w - 6 * base_control->window->iscale, 1 * base_control->window->iscale);
                break;
            case SDL_TOOLKIT_ICON_X11_FOLDER:
                X11_XSetForeground(base_control->window->display, base_control->window->ctx, control->xcolor_black.pixel);
                X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->icon_rect.x, item->rect.y + item->icon_rect.y + 2 * base_control->window->iscale, item->icon_rect.w, item->icon_rect.h - 2 * base_control->window->iscale);
                X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->icon_rect.x + 1 * base_control->window->iscale, item->rect.y + item->icon_rect.y, 7 * base_control->window->iscale, 2 * base_control->window->iscale);

                X11_XSetForeground(base_control->window->display, base_control->window->ctx, control->xcolor_cream.pixel);
                X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->icon_rect.x + 1 * base_control->window->iscale, item->rect.y + item->icon_rect.y + 3 * base_control->window->iscale, item->icon_rect.w - 2 * base_control->window->iscale, item->icon_rect.h - 4 * base_control->window->iscale);
                X11_XFillRectangle(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->icon_rect.x + 2 * base_control->window->iscale, item->rect.y + item->icon_rect.y + 1 * base_control->window->iscale, 5 * base_control->window->iscale, 1 * base_control->window->iscale);
                break;
            default:
                break;
            }

            X11_XSetForeground(base_control->window->display, base_control->window->ctx, base_control->window->xcolor[SDL_MESSAGEBOX_COLOR_TEXT].pixel);
#ifdef X_HAVE_UTF8_STRING
            if (base_control->window->utf8) {
                X11_Xutf8DrawString(base_control->window->display, control->item_area, base_control->window->font_set, base_control->window->ctx, item->rect.x + item->text_rect.x, item->rect.y + item->text_rect.y, item->utf8, item->utf8_len);
            } else
#endif
            {
                X11_XDrawString(base_control->window->display, control->item_area, base_control->window->ctx, item->rect.x + item->text_rect.x, item->rect.y + item->text_rect.y, item->utf8, item->utf8_len);
            }

            cursor = cursor->next;
        }

        control->item_area_rendered = true;
    }

    /* copy items pixmap to drawable */
    if (control->item_area_rendered) {
        X11_XCopyArea(base_control->window->display, control->item_area, base_control->window->drawable, base_control->window->ctx, control->item_area_reserved_rect.x, control->item_area_reserved_rect.y, SDL_min(control->item_area_reserved_rect.w, control->item_area_rect.w), SDL_min(control->item_area_reserved_rect.h, control->item_area_rect.h), base_control->rect.x + control->item_area_rect.x, base_control->rect.y + control->item_area_rect.y);
    }
}

extern SDL_ToolkitControlX11 *X11Toolkit_CreateListControl(SDL_ToolkitWindowX11 *window, const char *header, SDL_ListNode *items)
{
    SDL_ToolkitListControlX11 *control;
    SDL_ToolkitControlX11 *base_control;

    control = (SDL_ToolkitListControlX11 *)SDL_malloc(sizeof(SDL_ToolkitListControlX11));
    base_control = (SDL_ToolkitControlX11 *)control;
    if (!base_control) {
        SDL_SetError("Unable to allocate list control structure");
        return NULL;
    }

    /* base control */
    base_control->window = window;
    base_control->state = SDL_TOOLKIT_CONTROL_STATE_X11_NORMAL;
    base_control->func_calc_size = X11Toolkit_CalculateListControl;
    base_control->func_draw = X11Toolkit_DrawListControl;
    base_control->func_on_state_change = NULL;
    base_control->func_free = X11Toolkit_DestroyGenericControl;
    base_control->func_on_scale_change = NULL;
    base_control->func_process_event = NULL;
    base_control->selected = false;
    base_control->dynamic = false;
    base_control->is_default_enter = false;
    base_control->is_default_esc = false;
    base_control->captures_lr_arrows = false;
    base_control->special_focus = false;

    /* control */
    control->header = header;
    control->header_sz = SDL_strlen(header);
    control->items = items;
    control->item_area = None;

    /* colors */
    control->xcolor_green.flags = 0;
    control->xcolor_green.red = 0;
    control->xcolor_green.green = 51400;
    control->xcolor_green.blue = 0;
    control->xcolor_black.flags = 0;
    control->xcolor_black.red = 0;
    control->xcolor_black.green = 0;
    control->xcolor_black.blue = 0;
    control->xcolor_white.flags = 0;
    control->xcolor_white.red = 65535;
    control->xcolor_white.green = 65535;
    control->xcolor_white.blue = 65535;
    control->xcolor_cream.flags = 0;
    control->xcolor_cream.red = 61680;
    control->xcolor_cream.green = 53713;
    control->xcolor_cream.blue = 0;

    /* size */
    base_control->do_size = true;
    X11Toolkit_CalculateListControl(base_control);
    base_control->do_size = false;

    X11Toolkit_AddControlToWindow(window, base_control);
    return base_control;
}

void X11Toolkit_GetListControlAreaSize(SDL_ToolkitControlX11 *base_control, int *real_w, int *real_h, int *reserved_w, int *reserved_h)
{
    SDL_ToolkitListControlX11 *control;

    control = (SDL_ToolkitListControlX11 *)base_control;
    *real_w = control->item_area_rect.w;
    *real_h = control->item_area_rect.h;
    *reserved_w = control->item_area_reserved_rect.w;
    *reserved_h = control->item_area_reserved_rect.h;
}

void X11Toolkit_UpdateListControlAreaOffsets(SDL_ToolkitControlX11 *base_control, int x, int y)
{
    SDL_ToolkitListControlX11 *control;

    control = (SDL_ToolkitListControlX11 *)base_control;
    if (x >= 0) {
        control->item_area_reserved_rect.x = x;
    }
    if (y >= 0) {
        control->item_area_reserved_rect.y = y;
    }
    X11Toolkit_DrawWindow(base_control->window);
}

#endif // SDL_VIDEO_DRIVER_X11

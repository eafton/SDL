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

#ifndef SDL_fcft_h_
#define SDL_fcft_h_

#ifdef HAVE_LIBFCFT_H
#include <fcft/fcft.h>

/* fcft symbols */
typedef bool (*SDL_FcftInit)(enum fcft_log_colorize, bool, enum fcft_log_class);
typedef void (*SDL_FcftFini)(void);
typedef struct fcft_font *(*SDL_FcftFromName)(size_t count, const char *names[static count], const char *);
typedef void (*SDL_FcftDestroy)(struct fcft_font *);
typedef enum fcft_capabilities (*SDL_FcftCaps)(void);
typedef bool (*SDL_FcftKern)(struct fcft_font *, uint32_t, uint32_t, long *restrict, long *restrict);
typedef struct fcft_glyph *(*SDL_FcftRastChr)(struct fcft_font *, uint32_t, enum fcft_subpixel);
typedef struct fcft_text_run  *(*SDL_FcftRastRun)(struct fcft_font *, size_t len, const uint32_t text[static len], enum fcft_subpixel);
typedef void (*SDL_FcftDestroyRun)(struct fcft_text_run *);

/* pixman symbols */
typedef pixman_bool_t (*SDL_PixmanImgUnref)(pixman_image_t *);
typedef pixman_image_t *(*SDL_PixmanImgColFill)(const pixman_color_t *);
typedef pixman_image_t *(*SDL_PixmanImgCreate)(pixman_format_code_t, int, int, uint32_t *, int);
typedef pixman_image_t *(*SDL_PixmanImgComposite)(pixman_op_t, pixman_image_t *, pixman_image_t *, pixman_image_t *, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t , int32_t, int32_t);
typedef pixman_format_code_t (*SDL_PixmanImgGetFmt)(pixman_image_t *);

typedef struct SDL_Fcft {
	bool debug;
	bool do_fini;
	
    SDL_SharedObject *lib;
	
	SDL_FcftInit init;
	SDL_FcftFini fini;
	SDL_FcftFromName from_name;
	SDL_FcftDestroy destroy;
	SDL_FcftCaps capabilities;
	SDL_FcftKern kerning;
	SDL_FcftRastChr rasterize_char_utf32;
	SDL_FcftRastRun rasterize_text_run_utf32;
	SDL_FcftDestroyRun text_run_destroy;

	struct SDL_Pixman {
		SDL_PixmanImgUnref image_unref;
		SDL_PixmanImgColFill image_create_solid_fill;
		SDL_PixmanImgCreate image_create_bits_no_clear;
		SDL_PixmanImgComposite image_composite32;
		SDL_PixmanImgGetFmt image_get_format;
	} pixman;
} SDL_Fcft;

extern SDL_Fcft *SDL_Fcft_Create(void);
extern SDL_Surface *SDL_Fcft_Render(SDL_Fcft *fcft, struct fcft_font *font, pixman_color_t *foreground, pixman_color_t *background, char *utf8);
extern void SDL_Fcft_Destroy();

#endif // HAVE_LIBFCFT_H

#endif // SDL_fcft_h_

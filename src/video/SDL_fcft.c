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

#ifdef HAVE_LIBFCFT_H

#include "SDL_fcft.h"
#include "SDL_surface_c.h"

#ifdef SDL_LIBFCFT_DYNAMIC
SDL_ELF_NOTE_DLOPEN(
    "Fcft",
    "Modern text rendering for toolkit functionality",
    SDL_ELF_NOTE_DLOPEN_PRIORITY_SUGGESTED,
    SDL_LIBFCFT_DYNAMIC
);
#endif

SDL_Fcft *SDL_Fcft_Create(void)
{
    SDL_Fcft *fcft;
	
	if (!SDL_GetHintBoolean(SDL_HINT_ALLOW_FCFT, true)) {
		return NULL;
	}
	
    fcft = (SDL_Fcft *)SDL_malloc(sizeof(SDL_Fcft));
    if (!fcft) {
        return NULL;
    }

#ifdef SDL_LIBFCFT_DYNAMIC
    #define SDL_LIBFCFT_LOAD_SYM(a, x, n, t) x = ((t)SDL_LoadFunction(a->lib, n)); if (!x) { SDL_UnloadObject(a->lib); SDL_free(a); return NULL; }

    fcft->lib = SDL_LoadObject(SDL_LIBFCFT_DYNAMIC);
    if (!fcft->lib) {
        SDL_free(fcft);
        return NULL;
    }

	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->init, "fcft_init", SDL_FcftInit);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->fini, "fcft_fini", SDL_FcftFini);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->from_name, "fcft_from_name", SDL_FcftFromName);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->destroy, "fcft_destroy", SDL_FcftDestroy);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->capabilities, "fcft_capabilities", SDL_FcftCaps);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->kerning, "fcft_kerning", SDL_FcftKern);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->rasterize_char_utf32, "fcft_rasterize_char_utf32", SDL_FcftRastChr);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->rasterize_text_run_utf32, "fcft_rasterize_text_run_utf32", SDL_FcftRastRun);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->text_run_destroy, "fcft_text_run_destroy", SDL_FcftDestroyRun);
	
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->pixman.image_unref, "pixman_image_unref", SDL_PixmanImgUnref);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->pixman.image_create_solid_fill, "pixman_image_create_solid_fill", SDL_PixmanImgColFill);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->pixman.image_create_bits_no_clear, "pixman_image_create_bits_no_clear", SDL_PixmanImgCreate);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->pixman.image_composite32, "pixman_image_composite32", SDL_PixmanImgComposite);
	SDL_LIBFCFT_LOAD_SYM(fcft, fcft->pixman.image_get_format, "pixman_image_get_format", SDL_PixmanImgGetFmt);
#else
	fcft->init = fcft_init;
	fcft->fini = fcft_fini;
	fcft->from_name, fcft_from_name;
	fcft->destroy, fcft_destroy;
	fcft->capabilities, fcft_capabilities;
	fcft->kerning, fcft_kerning;
	fcft->rasterize_char_utf32, fcft_rasterize_char_utf32;
	fcft->rasterize_text_run_utf32, fcft_rasterize_text_run_utf32;
	fcft->text_run_destroy, fcft_text_run_destroy;
	
	fcft->pixman.image_unref = pixman_image_unref;
	fcft->pixman.image_create_solid_fill = pixman_image_create_solid_fill;
	fcft->pixman.image_create_bits_no_clear = pixman_image_create_bits_no_clear;
	fcft->pixman.image_composite32 = pixman_image_composite32;
	fcft->pixman.image_get_format = pixman_image_get_format;
	fcft->pixman.image_get_format = pixman_image_get_format;
#endif

	fcft->do_fini = false;
	fcft->debug = true;
	if (fcft->debug) {
		fcft->init(FCFT_LOG_COLORIZE_AUTO, false, FCFT_LOG_CLASS_DEBUG);
	} else {
		fcft->init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_NONE);
	}
	
    return fcft;
}

SDL_Surface *SDL_Fcft_Render(SDL_Fcft *fcft, struct fcft_font *font, pixman_color_t *foreground, pixman_color_t *background, char *utf8) {
	SDL_Surface *surface;
	pixman_image_t *image;
	pixman_image_t *foreground_fill;
	pixman_image_t *background_fill;
	Uint32 *utf32;
	SDL_Rect rect;
	enum fcft_capabilities caps;
	size_t sz;
	size_t i;

	/* Init vars */
	surface = NULL;
	image = NULL;
	rect.x = 0;
	rect.y = 0;
	rect.h = 0;
	rect.w = 0;
	
	/* Fills */
	if (background) {
		background_fill = fcft->pixman.image_create_solid_fill(background);
	}
	if (foreground) {
		foreground_fill = fcft->pixman.image_create_solid_fill(foreground);
	} else {
		pixman_color_t foreground_fallback;
		
		foreground_fallback.red = 0xffff,
		foreground_fallback.green = 0xffff,
		foreground_fallback.blue = 0xffff,
		foreground_fallback.alpha = 0xffff,
		foreground_fill = fcft->pixman.image_create_solid_fill(&foreground_fallback);
	}

	/* Convert to UTF-32 */
	utf32 = SDL_iconv_utf8_ucs4(utf8);
	sz = SDL_utf8strlen(utf8);
	for (i = 0; i < sz; i++) {
		utf32[i] = SDL_Swap32BE(utf32[i]);
	}

	/* Render */
	/* TODO: Grapheme and char */
	caps = fcft->capabilities();
	if (caps & FCFT_CAPABILITY_TEXT_RUN_SHAPING) {
		struct fcft_text_run *run;
		
		run = fcft->rasterize_text_run_utf32(font, sz, utf32, FCFT_SUBPIXEL_DEFAULT);
		
		/* Calculate extents */
		for (i = 0; i < run->count; i++) {
			const struct fcft_glyph *glyph;

			glyph = run->glyphs[i];
			
			if (i) {
				long x_kern;

				x_kern = 0;
				fcft->kerning(font, utf32[i - 1], utf32[i], &x_kern, NULL);
				rect.w += x_kern;
			}

			rect.w += glyph->advance.x;
			rect.h = SDL_max(rect.h, (font->ascent - glyph->y + glyph->height));
		}	
		
		/* Create target surface */
		surface = SDL_CreateSurface(rect.w, rect.h, SDL_PIXELFORMAT_ARGB8888);
		image = fcft->pixman.image_create_bits_no_clear(PIXMAN_a8r8g8b8, rect.w, rect.h, surface->pixels, surface->pitch);

		/* Background */
		if (background) {
			fcft->pixman.image_composite32(PIXMAN_OP_OVER, background_fill, NULL, image, 0, 0, 0, 0, 0, 0, rect.w, rect.h);
		}
		
		/* Blit glyphs to target */
		for (i = 0; i < run->count; i++) {
			const struct fcft_glyph *glyph;

			glyph = run->glyphs[i];

			if (i) {
				long x_kern;

				x_kern = 0;
				fcft->kerning(font, utf32[i - 1], utf32[i], &x_kern, NULL);
				rect.x += x_kern;
			}

			if (fcft->pixman.image_get_format(glyph->pix) == PIXMAN_a8r8g8b8) {
				fcft->pixman.image_composite32(PIXMAN_OP_OVER, glyph->pix, NULL, image, 0, 0, 0, 0,  rect.x + glyph->x, font->ascent - glyph->y, glyph->width, glyph->height);
			} else {
				fcft->pixman.image_composite32(PIXMAN_OP_OVER, foreground_fill, glyph->pix, image, 0, 0, 0, 0, rect.x + glyph->x, font->ascent - glyph->y, glyph->width, glyph->height);
			}

			rect.x += glyph->advance.x;
		}
	}
	
	/* Cleanup */
	SDL_free(utf32);
	fcft->pixman.image_unref(foreground_fill);
	if (background) { 
		fcft->pixman.image_unref(background_fill);
	}
	if (image) {
		fcft->pixman.image_unref(image);
	}
	
	return surface;
}

void SDL_Fcft_Destroy(SDL_Fcft *fcft)
{
    if (!fcft) {
        return;
    }

	if (fcft->do_fini) {
		fcft->fini();
	}
	
#ifdef SDL_LIBFCFT_DYNAMIC
    SDL_UnloadObject(fcft->lib);
#endif
	
    SDL_free(fcft);
}

#endif

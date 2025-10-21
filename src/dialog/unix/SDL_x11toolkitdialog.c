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

#include "../SDL_dialog_utils.h"
#include "../x11/SDL_x11toolkit.h"
#include "./SDL_x11toolkitdialog.h"

void drop(SDL_ToolkitControlX11 *control, void *data) {
	SDL_ToolkitControlX11 *slider;
	
	slider = data;
	X11Toolkit_DropSliderControl(slider);
}

void elevate(SDL_ToolkitControlX11 *control, void *data) {
	SDL_ToolkitControlX11 *slider;
	
	slider = data;
	X11Toolkit_ElevateSliderControl(slider);
}

void move_v(SDL_ToolkitControlX11 *control, void *data, int real, int reserved, int offset) {
	SDL_ToolkitControlX11 *list;
	int real_w, real_h, reserved_w, reserved_h;
	double ratio;
	
	list = (SDL_ToolkitControlX11 *)data;
 	X11Toolkit_GetListControlAreaSize(list, &real_w, &real_h, &reserved_w, &reserved_h);
	ratio = (double)real_h/(double)real;
	X11Toolkit_UpdateListControlAreaOffsets(list, -1, SDL_lround((double)offset * ratio));
}

void move_x(SDL_ToolkitControlX11 *control, void *data, int real, int reserved, int offset) {
	SDL_ToolkitControlX11 *list;
	int real_w, real_h, reserved_w, reserved_h;
	double ratio;
	
	list = (SDL_ToolkitControlX11 *)data;
 	X11Toolkit_GetListControlAreaSize(list, &real_w, &real_h, &reserved_w, &reserved_h);
	ratio = (double)real_w/(double)real;
	X11Toolkit_UpdateListControlAreaOffsets(list, SDL_lround((double)offset * ratio), -1);
}

void rand_str(char *dest, size_t length) {
    char charset[] = "0123456789"
                     "abcdefghijklmnopqrstuvwxyz"
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    while (length-- > 0) {
        size_t index = (double) rand() / RAND_MAX * (sizeof charset - 1);
        *dest++ = charset[index];
    }
    *dest = '\0';
}

void SDL_X11Toolkit_ShowFileDialogWithProperties(SDL_FileDialogType type, SDL_DialogFileCallback callback, void *userdata, SDL_PropertiesID props) {
	SDL_ToolkitWindowX11 *window;	
	SDL_ToolkitControlX11 *entry;
	SDL_ToolkitControlX11 *button;
	SDL_ToolkitControlX11 *slider;
	SDL_ToolkitControlX11 *sliderv;
	SDL_ToolkitControlX11 *pan;
	SDL_ToolkitControlX11 *list;
	SDL_ListNode *list_items_list;
	SDL_ToolkitListItemX11 *list_items;
	SDL_Window *parent_window;
	const char *title;
	SDL_Rect pan_inner_area;
	int real_w;
	int real_h;
	int reserved_w;
	int reserved_h;
	
	parent_window = SDL_GetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, NULL);
	title = SDL_GetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, NULL); 
	
    window = X11Toolkit_CreateWindowStruct(parent_window, NULL, SDL_TOOLKIT_WINDOW_MODE_X11_DIALOG, NULL, false);
    
	entry = X11Toolkit_CreateEntryControl(window);
    entry->rect.x = entry->rect.y = 32;
    entry->rect.w = 100;
    X11Toolkit_NotifyControlOfSizeChange(entry);
    
    pan = X11Toolkit_CreatePanControl(window);
	pan->rect.x = 200;
	pan->rect.y = 32;
	pan->rect.w = 300;
 	pan->rect.h = 300;
	X11Toolkit_GetPanControlInnerArea(pan, &pan_inner_area);
    
    sliderv = X11Toolkit_CreateSliderControl(window, false);
    sliderv->rect.x = 483;
	sliderv->rect.y = 49;
	sliderv->rect.h = 251;
	sliderv->rect.w = 15;
	X11Toolkit_NotifyControlOfSizeChange(sliderv);
	
	button = X11Toolkit_CreateIconButtonControl(window, SDL_TOOLKIT_ICON_X11_DOWN_ARROW);
    button->rect.x = 483;
    button->rect.y = 300;
    button->rect.w = 15;
    button->rect.h = 15;
 	X11Toolkit_NotifyControlOfSizeChange(button);
	X11Toolkit_RegisterCallbackForButtonControl(button, (void*)sliderv, drop);
 	
    button = X11Toolkit_CreateIconButtonControl(window, SDL_TOOLKIT_ICON_X11_UP_ARROW);
    button->rect.x = 483;
    button->rect.y = 34;
    button->rect.w = 15;
    button->rect.h = 15;
 	X11Toolkit_NotifyControlOfSizeChange(button);
 	X11Toolkit_RegisterCallbackForButtonControl(button, (void*)sliderv, elevate);

	pan = X11Toolkit_CreateBlockControl(window);
	pan->rect.x = 483;
	pan->rect.y = 315;
	pan->rect.w = 15;
 	pan->rect.h = 15;

    slider = X11Toolkit_CreateSliderControl(window, true);
    slider->rect.x = 217;
	slider->rect.y = 315;
	slider->rect.h = 15;
	slider->rect.w = 251;
	X11Toolkit_NotifyControlOfSizeChange(slider);

	button = X11Toolkit_CreateIconButtonControl(window, SDL_TOOLKIT_ICON_X11_LEFT_ARROW);
    button->rect.x = 202;
    button->rect.y = 315;
    button->rect.w = 15;
    button->rect.h = 15;
 	X11Toolkit_NotifyControlOfSizeChange(button);
	X11Toolkit_RegisterCallbackForButtonControl(button, (void*)slider, drop);

	button = X11Toolkit_CreateIconButtonControl(window, SDL_TOOLKIT_ICON_X11_RIGHT_ARROW);
    button->rect.x = 468;
    button->rect.y = 315;
    button->rect.w = 15;
    button->rect.h = 15;
 	X11Toolkit_NotifyControlOfSizeChange(button);
	X11Toolkit_RegisterCallbackForButtonControl(button, (void*)slider, elevate);

	list_items_list = NULL;
	list_items = SDL_calloc(99, sizeof(SDL_ToolkitListItemX11));
	list_items[0].utf8 = "Last one :)";
	list_items[0].icon = SDL_TOOLKIT_ICON_X11_UP_ARROW;
	SDL_ListAdd(&list_items_list, &list_items[0]);
	for (int i = 1; i < 98; i++) {
		list_items[i].utf8 = SDL_malloc(100);
		rand_str(list_items[i].utf8, 100);
		puts(list_items[i].utf8);
		list_items[i].icon = SDL_TOOLKIT_ICON_X11_FILE;
		SDL_ListAdd(&list_items_list, &list_items[i]);
	}

	list = X11Toolkit_CreateListControl(window, NULL, list_items_list);
    list->rect.x = 202;
    list->rect.y = 34;
    list->rect.w = 281;
    list->rect.h = 281;
 	X11Toolkit_NotifyControlOfSizeChange(list);
 	X11Toolkit_GetListControlAreaSize(list, &real_w, &real_h, &reserved_w, &reserved_h);
 	X11Toolkit_SetSliderControlSize(sliderv, real_h, reserved_h);
 	X11Toolkit_SetSliderControlSize(slider, real_w, reserved_w);
	X11Toolkit_RegisterSliderControlCallback(slider, list, move_x);
	X11Toolkit_RegisterSliderControlCallback(sliderv, list, move_v);

	X11Toolkit_CreateWindowRes(window, 640, 480, 0, 0, (char *)title);
    X11Toolkit_DoWindowEventLoop(window);
    X11Toolkit_DestroyWindow(window);
}

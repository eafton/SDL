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

void SDL_X11Toolkit_ShowFileDialogWithProperties(SDL_FileDialogType type, SDL_DialogFileCallback callback, void *userdata, SDL_PropertiesID props) {
	SDL_ToolkitWindowX11 *window;	
	SDL_ToolkitControlX11 *entry;
	SDL_ToolkitControlX11 *button;
	SDL_ToolkitControlX11 *slider;
	SDL_ToolkitControlX11 *pan;
	SDL_ToolkitControlX11 *list;
	SDL_ListNode *list_items;
	SDL_ToolkitListItemX11 a;
	SDL_ToolkitListItemX11 b;
	SDL_ToolkitListItemX11 c;
	SDL_ToolkitListItemX11 go_up;
	SDL_Window *parent_window;
	const char *title;
	SDL_Rect pan_inner_area;
	
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
    
    slider = X11Toolkit_CreateSliderControl(window, false);
    slider->rect.x = 483;
	slider->rect.y = 49;
	slider->rect.h = 251;
	slider->rect.w = 15;
	X11Toolkit_NotifyControlOfSizeChange(slider);
	
	button = X11Toolkit_CreateIconButtonControl(window, SDL_TOOLKIT_ICON_X11_DOWN_ARROW);
    button->rect.x = 483;
    button->rect.y = 300;
    button->rect.w = 15;
    button->rect.h = 15;
 	X11Toolkit_NotifyControlOfSizeChange(button);
	X11Toolkit_RegisterCallbackForButtonControl(button, (void*)slider, drop);
 	
    button = X11Toolkit_CreateIconButtonControl(window, SDL_TOOLKIT_ICON_X11_UP_ARROW);
    button->rect.x = 483;
    button->rect.y = 34;
    button->rect.w = 15;
    button->rect.h = 15;
 	X11Toolkit_NotifyControlOfSizeChange(button);
 	X11Toolkit_RegisterCallbackForButtonControl(button, (void*)slider, elevate);

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

	list_items = NULL;
	go_up.utf8 = "Go Up";
	go_up.icon = SDL_TOOLKIT_ICON_X11_UP_ARROW;
	a.utf8 = "Super Mario 64";
	a.icon = SDL_TOOLKIT_ICON_X11_FILE;
	b.utf8 = "Star Fox 64";
	b.icon = SDL_TOOLKIT_ICON_X11_FOLDER;
	c.utf8 = "F-ZERO X";
	c.icon = SDL_TOOLKIT_ICON_X11_NONE;
	SDL_ListAdd(&list_items, &c);
	SDL_ListAdd(&list_items, &b);
	SDL_ListAdd(&list_items, &a);
	SDL_ListAdd(&list_items, &go_up);
	
	list = X11Toolkit_CreateListControl(window, "Files", list_items);
    list->rect.x = 202;
    list->rect.y = 34;
    list->rect.w = 281;
    list->rect.h = 281;
 	X11Toolkit_NotifyControlOfSizeChange(list);
 	
	X11Toolkit_CreateWindowRes(window, 640, 480, 0, 0, (char *)title);
    X11Toolkit_DoWindowEventLoop(window);
    X11Toolkit_DestroyWindow(window);
}

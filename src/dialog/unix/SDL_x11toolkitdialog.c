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

void SDL_X11Toolkit_ShowFileDialogWithProperties(SDL_FileDialogType type, SDL_DialogFileCallback callback, void *userdata, SDL_PropertiesID props) {
	SDL_ToolkitWindowX11 *window;	
	SDL_ToolkitControlX11 *control;
	SDL_Window *parent_window;
	const char *title;
	
	parent_window = SDL_GetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, NULL);
	title = SDL_GetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, NULL); 
	
    window = X11Toolkit_CreateWindowStruct(parent_window, NULL, SDL_TOOLKIT_WINDOW_MODE_X11_DIALOG, NULL, false);
    control = X11Toolkit_CreateEntryControl(window);
    control->rect.x = control->rect.y = 32;
    control->rect.w = 100;
    X11Toolkit_NotifyControlOfSizeChange(control);
	X11Toolkit_CreateWindowRes(window, 320, 240, 0, 0, (char *)title);
    X11Toolkit_DoWindowEventLoop(window);
    X11Toolkit_DestroyWindow(window);
}

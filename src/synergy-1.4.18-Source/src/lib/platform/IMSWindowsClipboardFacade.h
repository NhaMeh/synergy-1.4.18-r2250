/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012 Bolton Software Ltd.
 * Copyright (C) 2002 Chris Schoeneman
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IMWINDOWSCLIPBOARDFACADE
#define IMWINDOWSCLIPBOARDFACADE

#include "common/IInterface.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

class IMSWindowsClipboardConverter;

class IMSWindowsClipboardFacade : public IInterface
{
public:
	virtual void write(HANDLE win32Data, UINT win32Format) = 0;
	virtual ~IMSWindowsClipboardFacade() { }
};

#endif
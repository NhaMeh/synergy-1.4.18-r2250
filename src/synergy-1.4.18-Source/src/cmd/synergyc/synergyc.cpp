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

#include "synergy/ClientApp.h"
#include "arch/Arch.h"
#include "base/Log.h"
#include "base/EventQueue.h"

#if WINAPI_MSWINDOWS
#include "synergyc/MSWindowsClientTaskBarReceiver.h"
#elif WINAPI_XWINDOWS
#include "synergyc/XWindowsClientTaskBarReceiver.h"
#elif WINAPI_CARBON
#include "synergyc/OSXClientTaskBarReceiver.h"
#else
#error Platform not supported.
#endif

int
main(int argc, char** argv) 
{
#if SYSAPI_WIN32
	// record window instance for tray icon, etc
	CArchMiscWindows::setInstanceWin32(GetModuleHandle(NULL));
#endif
	
	CArch arch;
	arch.init();

	CLog log;
	CEventQueue events;

	CClientApp app(&events, createTaskBarReceiver);
	return app.run(argc, argv);
}

/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012 Bolton Software Ltd.
 * Copyright (C) 2012 Nick Bolton
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
 
#pragma once

#include "arch/Arch.h"
#include "ipc/IpcServer.h"

#include <string>

class CEvent;
class CIpcLogOutputter;

#if SYSAPI_WIN32
class CMSWindowsWatchdog;
#endif

class CDaemonApp {

public:
	CDaemonApp();
	virtual ~CDaemonApp();
	int run(int argc, char** argv);
	void mainLoop(bool logToFile);

private:
	void daemonize();
	void foregroundError(const char* message);
	std::string logPath();
	void				handleIpcMessage(const CEvent&, void*);

public:
	static CDaemonApp* s_instance;

#if SYSAPI_WIN32
	CMSWindowsWatchdog*	m_watchdog;
#endif

private:
	CIpcServer*			m_ipcServer;
	CIpcLogOutputter*	m_ipcLogOutputter;
	IEventQueue*		m_events;
};

#define LOG_FILENAME "synergyd.log"

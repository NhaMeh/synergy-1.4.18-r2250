/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012 Bolton Software Ltd.
 * Copyright (C) 2003 Chris Schoeneman
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

#include "synergy/ServerTaskBarReceiver.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

class CBufferedLogOutputter;
class IEventQueue;

//! Implementation of CServerTaskBarReceiver for Microsoft Windows
class CMSWindowsServerTaskBarReceiver : public CServerTaskBarReceiver {
public:
	CMSWindowsServerTaskBarReceiver(HINSTANCE, const CBufferedLogOutputter*, IEventQueue* events);
	virtual ~CMSWindowsServerTaskBarReceiver();

	// IArchTaskBarReceiver overrides
	virtual void		showStatus();
	virtual void		runMenu(int x, int y);
	virtual void		primaryAction();
	virtual const Icon	getIcon() const;
	void cleanup();

protected:
	void				copyLog() const;

	// CServerTaskBarReceiver overrides
	virtual void		onStatusChanged();

private:
	HICON				loadIcon(UINT);
	void				deleteIcon(HICON);
	void				createWindow();
	void				destroyWindow();

	BOOL				dlgProc(HWND hwnd,
							UINT msg, WPARAM wParam, LPARAM lParam);
	static BOOL CALLBACK
						staticDlgProc(HWND hwnd,
							UINT msg, WPARAM wParam, LPARAM lParam);

private:
	HINSTANCE			m_appInstance;
	HWND				m_window;
	HMENU				m_menu;
	HICON				m_icon[kMaxState];
	const CBufferedLogOutputter*	m_logBuffer;
	IEventQueue*		m_events;

	static const UINT	s_stateToIconID[];
};

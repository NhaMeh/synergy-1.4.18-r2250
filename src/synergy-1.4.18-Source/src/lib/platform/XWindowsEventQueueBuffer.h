/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012 Bolton Software Ltd.
 * Copyright (C) 2004 Chris Schoeneman
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

#include "mt/Mutex.h"
#include "base/IEventQueueBuffer.h"
#include "common/stdvector.h"

#if X_DISPLAY_MISSING
#	error X11 is required to build synergy
#else
#	include <X11/Xlib.h>
#endif

class IEventQueue;

//! Event queue buffer for X11
class CXWindowsEventQueueBuffer : public IEventQueueBuffer {
public:
	CXWindowsEventQueueBuffer(Display*, Window, IEventQueue* events);
	virtual ~CXWindowsEventQueueBuffer();

	// IEventQueueBuffer overrides
	virtual	void		init() { }
	virtual void		waitForEvent(double timeout);
	virtual Type		getEvent(CEvent& event, UInt32& dataID);
	virtual bool		addEvent(UInt32 dataID);
	virtual bool		isEmpty() const;
	virtual CEventQueueTimer*
						newTimer(double duration, bool oneShot) const;
	virtual void		deleteTimer(CEventQueueTimer*) const;

private:
	void				flush();

private:
	typedef std::vector<XEvent> CEventList;

	CMutex				m_mutex;
	Display*			m_display;
	Window				m_window;
	Atom				m_userEvent;
	XEvent				m_event;
	CEventList			m_postedEvents;
	bool				m_waiting;
	int					m_pipefd[2];
	IEventQueue*		m_events;
};

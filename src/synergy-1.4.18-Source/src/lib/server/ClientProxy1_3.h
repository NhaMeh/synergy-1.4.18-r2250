/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012 Bolton Software Ltd.
 * Copyright (C) 2006 Chris Schoeneman
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

#include "server/ClientProxy1_2.h"

//! Proxy for client implementing protocol version 1.3
class CClientProxy1_3 : public CClientProxy1_2 {
public:
	CClientProxy1_3(const CString& name, synergy::IStream* adoptedStream, IEventQueue* events);
	~CClientProxy1_3();

	// IClient overrides
	virtual void		mouseWheel(SInt32 xDelta, SInt32 yDelta);

protected:
	// CClientProxy overrides
	virtual bool		parseMessage(const UInt8* code);
	virtual void		resetHeartbeatRate();
	virtual void		setHeartbeatRate(double rate, double alarm);
	virtual void		resetHeartbeatTimer();
	virtual void		addHeartbeatTimer();
	virtual void		removeHeartbeatTimer();
	virtual void		keepAlive();

private:
	void				handleKeepAlive(const CEvent&, void*);

private:
	double				m_keepAliveRate;
	CEventQueueTimer*	m_keepAliveTimer;
	IEventQueue*		m_events;
};

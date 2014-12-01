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

#pragma once

#include "net/IListenSocket.h"
#include "arch/IArchNetwork.h"

class CMutex;
class ISocketMultiplexerJob;
class IEventQueue;
class CSocketMultiplexer;

//! TCP listen socket
/*!
A listen socket using TCP.
*/
class CTCPListenSocket : public IListenSocket {
public:
	CTCPListenSocket(IEventQueue* events, CSocketMultiplexer* socketMultiplexer);
	~CTCPListenSocket();

	// ISocket overrides
	virtual void		bind(const CNetworkAddress&);
	virtual void		close();
	virtual void*		getEventTarget() const;

	// IListenSocket overrides
	virtual IDataSocket*	accept();

private:
	ISocketMultiplexerJob*
						serviceListening(ISocketMultiplexerJob*,
							bool, bool, bool);

private:
	CArchSocket			m_socket;
	CMutex*				m_mutex;
	IEventQueue*		m_events;
	CSocketMultiplexer* m_socketMultiplexer;
};

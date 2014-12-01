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

#include "server/Server.h"
#include "synergy/ServerApp.h"
#include "arch/IArchTaskBarReceiver.h"
#include "base/EventTypes.h"
#include "base/String.h"
#include "base/Event.h"
#include "common/stdvector.h"

class IEventQueue;

//! Implementation of IArchTaskBarReceiver for the synergy server
class CServerTaskBarReceiver : public IArchTaskBarReceiver {
public:
	CServerTaskBarReceiver(IEventQueue* events);
	virtual ~CServerTaskBarReceiver();

	//! @name manipulators
	//@{

	//! Update status
	/*!
	Determine the status and query required information from the server.
	*/
	void				updateStatus(CServer*, const CString& errorMsg);

	void updateStatus(INode* n, const CString& errorMsg) { updateStatus((CServer*)n, errorMsg); }

	//@}

	// IArchTaskBarReceiver overrides
	virtual void		showStatus() = 0;
	virtual void		runMenu(int x, int y) = 0;
	virtual void		primaryAction() = 0;
	virtual void		lock() const;
	virtual void		unlock() const;
	virtual const Icon	getIcon() const = 0;
	virtual std::string	getToolTip() const;

protected:
	typedef std::vector<CString> CClients;
	enum EState {
		kNotRunning,
		kNotWorking,
		kNotConnected,
		kConnected,
		kMaxState
	};

	//! Get status
	EState				getStatus() const;

	//! Get error message
	const CString&		getErrorMessage() const;

	//! Get connected clients
	const CClients&		getClients() const;

	//! Quit app
	/*!
	Causes the application to quit gracefully
	*/
	void				quit();

	//! Status change notification
	/*!
	Called when status changes.  The default implementation does
	nothing.
	*/
	virtual void		onStatusChanged(CServer* server);

private:
	EState				m_state;
	CString				m_errorMessage;
	CClients			m_clients;
	IEventQueue*		m_events;
};

IArchTaskBarReceiver* createTaskBarReceiver(const CBufferedLogOutputter* logBuffer, IEventQueue* events);

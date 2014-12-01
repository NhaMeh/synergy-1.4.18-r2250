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

#include "synergy/ServerTaskBarReceiver.h"
#include "server/Server.h"
#include "mt/Lock.h"
#include "base/String.h"
#include "base/IEventQueue.h"
#include "arch/Arch.h"
#include "common/Version.h"

//
// CServerTaskBarReceiver
//

CServerTaskBarReceiver::CServerTaskBarReceiver(IEventQueue* events) :
	m_state(kNotRunning),
	m_events(events)
{
	// do nothing
}

CServerTaskBarReceiver::~CServerTaskBarReceiver()
{
	// do nothing
}

void
CServerTaskBarReceiver::updateStatus(CServer* server, const CString& errorMsg)
{
	{
		// update our status
		m_errorMessage = errorMsg;
		if (server == NULL) {
			if (m_errorMessage.empty()) {
				m_state = kNotRunning;
			}
			else {
				m_state = kNotWorking;
			}
		}
		else {
			m_clients.clear();
			server->getClients(m_clients);
			if (m_clients.size() <= 1) {
				m_state = kNotConnected;
			}
			else {
				m_state = kConnected;
			}
		}

		// let subclasses have a go
		onStatusChanged(server);
	}

	// tell task bar
	ARCH->updateReceiver(this);
}

CServerTaskBarReceiver::EState
CServerTaskBarReceiver::getStatus() const
{
	return m_state;
}

const CString&
CServerTaskBarReceiver::getErrorMessage() const
{
	return m_errorMessage;
}

const CServerTaskBarReceiver::CClients&
CServerTaskBarReceiver::getClients() const
{
	return m_clients;
}

void
CServerTaskBarReceiver::quit()
{
	m_events->addEvent(CEvent(CEvent::kQuit));
}

void
CServerTaskBarReceiver::onStatusChanged(CServer*)
{
	// do nothing
}

void
CServerTaskBarReceiver::lock() const
{
	// do nothing
}

void
CServerTaskBarReceiver::unlock() const
{
	// do nothing
}

std::string
CServerTaskBarReceiver::getToolTip() const
{
	switch (m_state) {
	case kNotRunning:
		return synergy::string::sprintf("%s:  Not running", kAppVersion);

	case kNotWorking:
		return synergy::string::sprintf("%s:  %s",
								kAppVersion, m_errorMessage.c_str());
						
	case kNotConnected:
		return synergy::string::sprintf("%s:  Waiting for clients", kAppVersion);

	case kConnected:
		return synergy::string::sprintf("%s:  Connected", kAppVersion);

	default:
		return "";
	}
}

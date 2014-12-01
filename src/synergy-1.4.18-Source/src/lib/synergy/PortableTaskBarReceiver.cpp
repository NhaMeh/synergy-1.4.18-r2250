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

#include "synergy/PortableTaskBarReceiver.h"
#include "mt/Lock.h"
#include "base/String.h"
#include "base/IEventQueue.h"
#include "arch/Arch.h"
#include "common/Version.h"

//
// CPortableTaskBarReceiver
//

CPortableTaskBarReceiver::CPortableTaskBarReceiver(IEventQueue* events) :
	m_state(kNotRunning),
	m_events(events)
{
	// do nothing
}

CPortableTaskBarReceiver::~CPortableTaskBarReceiver()
{
	// do nothing
}

void
CPortableTaskBarReceiver::updateStatus(INode* node, const CString& errorMsg)
{
	{
		// update our status
		m_errorMessage = errorMsg;
		if (node == NULL) {
			if (m_errorMessage.empty()) {
				m_state = kNotRunning;
			}
			else {
				m_state = kNotWorking;
			}
		}
		else {
			m_state = kNotConnected;
		}

		// let subclasses have a go
		onStatusChanged(node);
	}

	// tell task bar
	ARCH->updateReceiver(this);
}

CPortableTaskBarReceiver::EState
CPortableTaskBarReceiver::getStatus() const
{
	return m_state;
}

const CString&
CPortableTaskBarReceiver::getErrorMessage() const
{
	return m_errorMessage;
}

void
CPortableTaskBarReceiver::quit()
{
	m_events->addEvent(CEvent(CEvent::kQuit));
}

void
CPortableTaskBarReceiver::onStatusChanged(INode*)
{
	// do nothing
}

void
CPortableTaskBarReceiver::lock() const
{
	// do nothing
}

void
CPortableTaskBarReceiver::unlock() const
{
	// do nothing
}

std::string
CPortableTaskBarReceiver::getToolTip() const
{
	switch (m_state) {
	case kNotRunning:
		return synergy::string::sprintf("%s:  Not running", kAppVersion);

	case kNotWorking:
		return synergy::string::sprintf("%s:  %s",
								kAppVersion, m_errorMessage.c_str());
						
	case kNotConnected:
		return synergy::string::sprintf("%s:  Unknown", kAppVersion);

	default:
		return "";
	}
}

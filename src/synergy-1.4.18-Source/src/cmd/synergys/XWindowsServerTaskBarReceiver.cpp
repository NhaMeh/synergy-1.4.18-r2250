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

#include "synergys/XWindowsServerTaskBarReceiver.h"
#include "arch/Arch.h"

//
// CXWindowsServerTaskBarReceiver
//

CXWindowsServerTaskBarReceiver::CXWindowsServerTaskBarReceiver(
		const CBufferedLogOutputter*, IEventQueue* events) :
	CServerTaskBarReceiver(events)
{
	// add ourself to the task bar
	ARCH->addReceiver(this);
}

CXWindowsServerTaskBarReceiver::~CXWindowsServerTaskBarReceiver()
{
	ARCH->removeReceiver(this);
}

void
CXWindowsServerTaskBarReceiver::showStatus()
{
	// do nothing
}

void
CXWindowsServerTaskBarReceiver::runMenu(int, int)
{
	// do nothing
}

void
CXWindowsServerTaskBarReceiver::primaryAction()
{
	// do nothing
}

const IArchTaskBarReceiver::Icon
CXWindowsServerTaskBarReceiver::getIcon() const
{
	return NULL;
}

IArchTaskBarReceiver*
createTaskBarReceiver(const CBufferedLogOutputter* logBuffer, IEventQueue* events)
{
	return new CXWindowsServerTaskBarReceiver(logBuffer, events);
}

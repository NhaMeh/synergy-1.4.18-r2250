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

#include "ipc/IpcMessage.h"
#include "ipc/Ipc.h"

CIpcMessage::CIpcMessage(UInt8 type) :
	m_type(type)
{
}

CIpcMessage::~CIpcMessage()
{
}

CIpcHelloMessage::CIpcHelloMessage(EIpcClientType clientType) :
	CIpcMessage(kIpcHello),
	m_clientType(clientType)
{
}

CIpcHelloMessage::~CIpcHelloMessage()
{
}

CIpcShutdownMessage::CIpcShutdownMessage() :
CIpcMessage(kIpcShutdown)
{
}

CIpcShutdownMessage::~CIpcShutdownMessage()
{
}

CIpcLogLineMessage::CIpcLogLineMessage(const CString& logLine) :
CIpcMessage(kIpcLogLine),
m_logLine(logLine)
{
}

CIpcLogLineMessage::~CIpcLogLineMessage()
{
}

CIpcCommandMessage::CIpcCommandMessage(const CString& command, bool elevate) :
CIpcMessage(kIpcCommand),
m_command(command),
m_elevate(elevate)
{
}

CIpcCommandMessage::~CIpcCommandMessage()
{
}

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

#include "platform/OSXEventQueueBuffer.h"

#include "base/Event.h"
#include "base/IEventQueue.h"

//
// CEventQueueTimer
//

class CEventQueueTimer { };

//
// COSXEventQueueBuffer
//

COSXEventQueueBuffer::COSXEventQueueBuffer(IEventQueue* events) :
	m_event(NULL),
	m_eventQueue(events),
	m_carbonEventQueue(NULL)
{
	// do nothing
}

COSXEventQueueBuffer::~COSXEventQueueBuffer()
{
	// release the last event
	if (m_event != NULL) {
		ReleaseEvent(m_event);
	}
}

void
COSXEventQueueBuffer::init()
{
	m_carbonEventQueue = GetCurrentEventQueue();
}

void
COSXEventQueueBuffer::waitForEvent(double timeout)
{
	EventRef event;
	ReceiveNextEvent(0, NULL, timeout, false, &event);
}

IEventQueueBuffer::Type
COSXEventQueueBuffer::getEvent(CEvent& event, UInt32& dataID)
{
	// release the previous event
	if (m_event != NULL) {
		ReleaseEvent(m_event);
		m_event = NULL;
	}

	// get the next event
	OSStatus error = ReceiveNextEvent(0, NULL, 0.0, true, &m_event);

	// handle the event
	if (error == eventLoopQuitErr) {
		event = CEvent(CEvent::kQuit);
		return kSystem;
	}
	else if (error != noErr) {
		return kNone;
	}
	else {
		UInt32 eventClass = GetEventClass(m_event);
		switch (eventClass) {
		case 'Syne': 
			dataID = GetEventKind(m_event);
			return kUser;

		default: 
			event = CEvent(CEvent::kSystem,
						m_eventQueue->getSystemTarget(), &m_event);
			return kSystem;
		}
	}
}

bool
COSXEventQueueBuffer::addEvent(UInt32 dataID)
{
	EventRef event;
	OSStatus error = CreateEvent(
							kCFAllocatorDefault,
							'Syne', 
							dataID,
							0,
							kEventAttributeNone,
							&event);

	if (error == noErr) {
	
		assert(m_carbonEventQueue != NULL);
		
		error = PostEventToQueue(
			m_carbonEventQueue,
			event,
			kEventPriorityStandard);
		
		ReleaseEvent(event);
	}
	
	return (error == noErr);
}

bool
COSXEventQueueBuffer::isEmpty() const
{
	EventRef event;
	OSStatus status = ReceiveNextEvent(0, NULL, 0.0, false, &event);
	return (status == eventLoopTimedOutErr);
}

CEventQueueTimer*
COSXEventQueueBuffer::newTimer(double, bool) const
{
	return new CEventQueueTimer;
}

void
COSXEventQueueBuffer::deleteTimer(CEventQueueTimer* timer) const
{
	delete timer;
}

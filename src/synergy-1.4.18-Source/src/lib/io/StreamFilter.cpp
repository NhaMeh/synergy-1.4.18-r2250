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

#include "io/StreamFilter.h"
#include "base/IEventQueue.h"
#include "base/TMethodEventJob.h"

//
// CStreamFilter
//

CStreamFilter::CStreamFilter(IEventQueue* events, synergy::IStream* stream, bool adoptStream) :
	m_stream(stream),
	m_adopted(adoptStream),
	m_events(events)
{
	// replace handlers for m_stream
	m_events->removeHandlers(m_stream->getEventTarget());
	m_events->adoptHandler(CEvent::kUnknown, m_stream->getEventTarget(),
							new TMethodEventJob<CStreamFilter>(this,
								&CStreamFilter::handleUpstreamEvent));
}

CStreamFilter::~CStreamFilter()
{
	m_events->removeHandler(CEvent::kUnknown, m_stream->getEventTarget());
	if (m_adopted) {
		delete m_stream;
	}
}

void
CStreamFilter::close()
{
	getStream()->close();
}

UInt32
CStreamFilter::read(void* buffer, UInt32 n)
{
	return getStream()->read(buffer, n);
}

void
CStreamFilter::write(const void* buffer, UInt32 n)
{
	getStream()->write(buffer, n);
}

void
CStreamFilter::flush()
{
	getStream()->flush();
}

void
CStreamFilter::shutdownInput()
{
	getStream()->shutdownInput();
}

void
CStreamFilter::shutdownOutput()
{
	getStream()->shutdownOutput();
}

void*
CStreamFilter::getEventTarget() const
{
	return const_cast<void*>(reinterpret_cast<const void*>(this));
}

bool
CStreamFilter::isReady() const
{
	return getStream()->isReady();
}

UInt32
CStreamFilter::getSize() const
{
	return getStream()->getSize();
}

synergy::IStream*
CStreamFilter::getStream() const
{
	return m_stream;
}

void
CStreamFilter::filterEvent(const CEvent& event)
{
	m_events->dispatchEvent(CEvent(event.getType(),
						getEventTarget(), event.getData()));
}

void
CStreamFilter::handleUpstreamEvent(const CEvent& event, void*)
{
	filterEvent(event);
}

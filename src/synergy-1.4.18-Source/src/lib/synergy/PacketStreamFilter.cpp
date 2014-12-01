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

#include "synergy/PacketStreamFilter.h"
#include "base/IEventQueue.h"
#include "mt/Lock.h"
#include "base/TMethodEventJob.h"

#include <cstring>
#include <memory>

//
// CPacketStreamFilter
//

CPacketStreamFilter::CPacketStreamFilter(IEventQueue* events, synergy::IStream* stream, bool adoptStream) :
	CStreamFilter(events, stream, adoptStream),
	m_size(0),
	m_inputShutdown(false),
	m_events(events)
{
	// do nothing
}

CPacketStreamFilter::~CPacketStreamFilter()
{
	// do nothing
}

void
CPacketStreamFilter::close()
{
	CLock lock(&m_mutex);
	m_size = 0;
	m_buffer.pop(m_buffer.getSize());
	CStreamFilter::close();
}

UInt32
CPacketStreamFilter::read(void* buffer, UInt32 n)
{
	if (n == 0) {
		return 0;
	}

	CLock lock(&m_mutex);

	// if not enough data yet then give up
	if (!isReadyNoLock()) {
		return 0;
	}

	// read no more than what's left in the buffered packet
	if (n > m_size) {
		n = m_size;
	}

	// read it
	if (buffer != NULL) {
		memcpy(buffer, m_buffer.peek(n), n);
	}
	m_buffer.pop(n);
	m_size -= n;

	// get next packet's size if we've finished with this packet and
	// there's enough data to do so.
	readPacketSize();

	if (m_inputShutdown && m_size == 0) {
		m_events->addEvent(CEvent(m_events->forIStream().inputShutdown(),
						getEventTarget(), NULL));
	}

	return n;
}

void
CPacketStreamFilter::write(const void* buffer, UInt32 count)
{
	// write the length of the payload
	UInt8 length[4];
	length[0] = (UInt8)((count >> 24) & 0xff);
	length[1] = (UInt8)((count >> 16) & 0xff);
	length[2] = (UInt8)((count >>  8) & 0xff);
	length[3] = (UInt8)( count        & 0xff);
	getStream()->write(length, sizeof(length));

	// write the payload
	getStream()->write(buffer, count);
}

void
CPacketStreamFilter::shutdownInput()
{
	CLock lock(&m_mutex);
	m_size = 0;
	m_buffer.pop(m_buffer.getSize());
	CStreamFilter::shutdownInput();
}

bool
CPacketStreamFilter::isReady() const
{
	CLock lock(&m_mutex);
	return isReadyNoLock();
}

UInt32
CPacketStreamFilter::getSize() const
{
	CLock lock(&m_mutex);
	return isReadyNoLock() ? m_size : 0;
}

bool
CPacketStreamFilter::isReadyNoLock() const
{
	return (m_size != 0 && m_buffer.getSize() >= m_size);
}

void
CPacketStreamFilter::readPacketSize()
{
	// note -- m_mutex must be locked on entry

	if (m_size == 0 && m_buffer.getSize() >= 4) {
		UInt8 buffer[4];
		memcpy(buffer, m_buffer.peek(sizeof(buffer)), sizeof(buffer));
		m_buffer.pop(sizeof(buffer));
		m_size = ((UInt32)buffer[0] << 24) |
				 ((UInt32)buffer[1] << 16) |
				 ((UInt32)buffer[2] <<  8) |
				  (UInt32)buffer[3];
	}
}

bool
CPacketStreamFilter::readMore()
{
	// note if we have whole packet
	bool wasReady = isReadyNoLock();

	// read more data
	char buffer[4096];
	UInt32 n = getStream()->read(buffer, sizeof(buffer));
	while (n > 0) {
		m_buffer.write(buffer, n);
		n = getStream()->read(buffer, sizeof(buffer));
	}

	// if we don't yet have the next packet size then get it,
	// if possible.
	readPacketSize();

	// note if we now have a whole packet
	bool isReady = isReadyNoLock();

	// if we weren't ready before but now we are then send a
	// input ready event apparently from the filtered stream.
	return (wasReady != isReady);
}

void
CPacketStreamFilter::filterEvent(const CEvent& event)
{
	if (event.getType() == m_events->forIStream().inputReady()) {
		CLock lock(&m_mutex);
		if (!readMore()) {
			return;
		}
	}
	else if (event.getType() == m_events->forIStream().inputShutdown()) {
		// discard this if we have buffered data
		CLock lock(&m_mutex);
		m_inputShutdown = true;
		if (m_size != 0) {
			return;
		}
	}

	// pass event
	CStreamFilter::filterEvent(event);
}

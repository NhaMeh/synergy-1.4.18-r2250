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

#pragma once

#include "net/ISocketMultiplexerJob.h"
#include "arch/Arch.h"

//! Use a method as a socket multiplexer job
/*!
A socket multiplexer job class that invokes a member function.
*/
template <class T>
class TSocketMultiplexerMethodJob : public ISocketMultiplexerJob {
public:
	typedef ISocketMultiplexerJob*
				(T::*Method)(ISocketMultiplexerJob*, bool, bool, bool);

	//! run() invokes \c object->method(arg)
	TSocketMultiplexerMethodJob(T* object, Method method,
							CArchSocket socket, bool readable, bool writeable);
	virtual ~TSocketMultiplexerMethodJob();

	// IJob overrides
	virtual ISocketMultiplexerJob*
						run(bool readable, bool writable, bool error);
	virtual CArchSocket	getSocket() const;
	virtual bool		isReadable() const;
	virtual bool		isWritable() const;

private:
	T*					m_object;
	Method				m_method;
	CArchSocket			m_socket;
	bool				m_readable;
	bool				m_writable;
	void*				m_arg;
};

template <class T>
inline
TSocketMultiplexerMethodJob<T>::TSocketMultiplexerMethodJob(T* object,
				Method method, CArchSocket socket,
				bool readable, bool writable) :
	m_object(object),
	m_method(method),
	m_socket(ARCH->copySocket(socket)),
	m_readable(readable),
	m_writable(writable)
{
	// do nothing
}

template <class T>
inline
TSocketMultiplexerMethodJob<T>::~TSocketMultiplexerMethodJob()
{
	ARCH->closeSocket(m_socket);
}

template <class T>
inline
ISocketMultiplexerJob*
TSocketMultiplexerMethodJob<T>::run(bool read, bool write, bool error)
{
	if (m_object != NULL) {
		return (m_object->*m_method)(this, read, write, error);
	}
	return NULL;
}

template <class T>
inline
CArchSocket
TSocketMultiplexerMethodJob<T>::getSocket() const
{
	return m_socket;
}

template <class T>
inline
bool
TSocketMultiplexerMethodJob<T>::isReadable() const
{
	return m_readable;
}

template <class T>
inline
bool
TSocketMultiplexerMethodJob<T>::isWritable() const
{
	return m_writable;
}

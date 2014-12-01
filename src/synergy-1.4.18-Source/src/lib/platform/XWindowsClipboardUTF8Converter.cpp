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

#include "platform/XWindowsClipboardUTF8Converter.h"

//
// CXWindowsClipboardUTF8Converter
//

CXWindowsClipboardUTF8Converter::CXWindowsClipboardUTF8Converter(
				Display* display, const char* name) :
	m_atom(XInternAtom(display, name, False))
{
	// do nothing
}

CXWindowsClipboardUTF8Converter::~CXWindowsClipboardUTF8Converter()
{
	// do nothing
}

IClipboard::EFormat
CXWindowsClipboardUTF8Converter::getFormat() const
{
	return IClipboard::kText;
}

Atom
CXWindowsClipboardUTF8Converter::getAtom() const
{
	return m_atom;
}

int
CXWindowsClipboardUTF8Converter::getDataSize() const
{
	return 8;
}

CString
CXWindowsClipboardUTF8Converter::fromIClipboard(const CString& data) const
{
	return data;
}

CString
CXWindowsClipboardUTF8Converter::toIClipboard(const CString& data) const
{
	return data;
}

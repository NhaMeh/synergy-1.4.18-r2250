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

#include "platform/MSWindowsClipboard.h"

#include "platform/MSWindowsClipboardTextConverter.h"
#include "platform/MSWindowsClipboardUTF16Converter.h"
#include "platform/MSWindowsClipboardBitmapConverter.h"
#include "platform/MSWindowsClipboardHTMLConverter.h"
#include "platform/MSWindowsClipboardFacade.h"
#include "arch/win32/ArchMiscWindows.h"
#include "base/Log.h"

//
// CMSWindowsClipboard
//

UINT					CMSWindowsClipboard::s_ownershipFormat = 0;

CMSWindowsClipboard::CMSWindowsClipboard(HWND window) :
	m_window(window),
	m_time(0),
	m_facade(new CMSWindowsClipboardFacade()),
	m_deleteFacade(true)
{
	// add converters, most desired first
	m_converters.push_back(new CMSWindowsClipboardUTF16Converter);
	m_converters.push_back(new CMSWindowsClipboardBitmapConverter);
	m_converters.push_back(new CMSWindowsClipboardHTMLConverter);
}

CMSWindowsClipboard::~CMSWindowsClipboard()
{
	clearConverters();

	// dependency injection causes confusion over ownership, so we need
	// logic to decide whether or not we delete the facade. there must
	// be a more elegant way of doing this.
	if (m_deleteFacade)
		delete m_facade;
}

void
CMSWindowsClipboard::setFacade(IMSWindowsClipboardFacade& facade)
{
	delete m_facade;
	m_facade = &facade;
	m_deleteFacade = false;
}

bool
CMSWindowsClipboard::emptyUnowned()
{
	LOG((CLOG_DEBUG "empty clipboard"));

	// empty the clipboard (and take ownership)
	if (!EmptyClipboard()) {
		// unable to cause this in integ tests, but this error has never 
		// actually been reported by users.
		LOG((CLOG_DEBUG "failed to grab clipboard"));
		return false;
	}

	return true;
}

bool
CMSWindowsClipboard::empty()
{
	if (!emptyUnowned()) {
		return false;
	}

	// mark clipboard as being owned by synergy
	HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 1);
	SetClipboardData(getOwnershipFormat(), data);

	return true;
}

void
CMSWindowsClipboard::add(EFormat format, const CString& data)
{
	LOG((CLOG_DEBUG "add %d bytes to clipboard format: %d", data.size(), format));

	// convert data to win32 form
	for (ConverterList::const_iterator index = m_converters.begin();
								index != m_converters.end(); ++index) {
		IMSWindowsClipboardConverter* converter = *index;

		// skip converters for other formats
		if (converter->getFormat() == format) {
			HANDLE win32Data = converter->fromIClipboard(data);
			if (win32Data != NULL) {
				UINT win32Format = converter->getWin32Format();
				m_facade->write(win32Data, win32Format);
			}
		}
	}
}

bool
CMSWindowsClipboard::open(Time time) const
{
	LOG((CLOG_DEBUG "open clipboard"));

	if (!OpenClipboard(m_window)) {
		// unable to cause this in integ tests; but this can happen!
		// * http://synergy-project.org/pm/issues/86
		// * http://synergy-project.org/pm/issues/1256
		// logging improved to see if we can catch more info next time.
		LOG((CLOG_WARN "failed to open clipboard: %d", GetLastError()));
		return false;
	}

	m_time = time;

	return true;
}

void
CMSWindowsClipboard::close() const
{
	LOG((CLOG_DEBUG "close clipboard"));
	CloseClipboard();
}

IClipboard::Time
CMSWindowsClipboard::getTime() const
{
	return m_time;
}

bool
CMSWindowsClipboard::has(EFormat format) const
{
	for (ConverterList::const_iterator index = m_converters.begin();
								index != m_converters.end(); ++index) {
		IMSWindowsClipboardConverter* converter = *index;
		if (converter->getFormat() == format) {
			if (IsClipboardFormatAvailable(converter->getWin32Format())) {
				return true;
			}
		}
	}
	return false;
}

CString
CMSWindowsClipboard::get(EFormat format) const
{
	// find the converter for the first clipboard format we can handle
	IMSWindowsClipboardConverter* converter = NULL;
	UINT win32Format = EnumClipboardFormats(0);
	while (converter == NULL && win32Format != 0) {
		for (ConverterList::const_iterator index = m_converters.begin();
								index != m_converters.end(); ++index) {
			converter = *index;
			if (converter->getWin32Format() == win32Format &&
				converter->getFormat()      == format) {
				break;
			}
			converter = NULL;
		}
		win32Format = EnumClipboardFormats(win32Format);
	}

	// if no converter then we don't recognize any formats
	if (converter == NULL) {
		return CString();
	}

	// get a handle to the clipboard data
	HANDLE win32Data = GetClipboardData(converter->getWin32Format());
	if (win32Data == NULL) {
		// nb: can't cause this using integ tests; this is only caused when
		// the selected converter returns an invalid format -- which you
		// cannot cause using public functions.
		return CString();
	}

	// convert
	return converter->toIClipboard(win32Data);
}

void
CMSWindowsClipboard::clearConverters()
{
	for (ConverterList::iterator index = m_converters.begin();
								index != m_converters.end(); ++index) {
		delete *index;
	}
	m_converters.clear();
}

bool
CMSWindowsClipboard::isOwnedBySynergy()
{
	// create ownership format if we haven't yet
	if (s_ownershipFormat == 0) {
		s_ownershipFormat = RegisterClipboardFormat(TEXT("SynergyOwnership"));
	}
	return (IsClipboardFormatAvailable(getOwnershipFormat()) != 0);
}

UINT
CMSWindowsClipboard::getOwnershipFormat()
{
	// create ownership format if we haven't yet
	if (s_ownershipFormat == 0) {
		s_ownershipFormat = RegisterClipboardFormat(TEXT("SynergyOwnership"));
	}

	// return the format
	return s_ownershipFormat;
}

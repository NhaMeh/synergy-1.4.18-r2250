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

#include "platform/OSXClipboardAnyTextConverter.h"

//! Convert to/from UTF-16 encoding
class COSXClipboardUTF16Converter : public COSXClipboardAnyTextConverter {
public:
	COSXClipboardUTF16Converter();
	virtual ~COSXClipboardUTF16Converter();

	// IOSXClipboardAnyTextConverter overrides
	virtual CFStringRef
						getOSXFormat() const;

protected:
	// COSXClipboardAnyTextConverter overrides
	virtual CString		doFromIClipboard(const CString&) const;
	virtual CString		doToIClipboard(const CString&) const;
};

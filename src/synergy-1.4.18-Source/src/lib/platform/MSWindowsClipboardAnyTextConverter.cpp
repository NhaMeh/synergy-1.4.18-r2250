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

#include "platform/MSWindowsClipboardAnyTextConverter.h"

//
// CMSWindowsClipboardAnyTextConverter
//

CMSWindowsClipboardAnyTextConverter::CMSWindowsClipboardAnyTextConverter()
{
	// do nothing
}

CMSWindowsClipboardAnyTextConverter::~CMSWindowsClipboardAnyTextConverter()
{
	// do nothing
}

IClipboard::EFormat
CMSWindowsClipboardAnyTextConverter::getFormat() const
{
	return IClipboard::kText;
}

HANDLE
CMSWindowsClipboardAnyTextConverter::fromIClipboard(const CString& data) const
{
	// convert linefeeds and then convert to desired encoding
	CString text = doFromIClipboard(convertLinefeedToWin32(data));
	UInt32 size  = (UInt32)text.size();

	// copy to memory handle
	HGLOBAL gData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size);
	if (gData != NULL) {
		// get a pointer to the allocated memory
		char* dst = (char*)GlobalLock(gData);
		if (dst != NULL) {
			memcpy(dst, text.data(), size);
			GlobalUnlock(gData);
		}
		else {
			GlobalFree(gData);
			gData = NULL;
		}
	}

	return gData;
}

CString
CMSWindowsClipboardAnyTextConverter::toIClipboard(HANDLE data) const
{
	// get datator
	const char* src = (const char*)GlobalLock(data);
	UInt32 srcSize = (UInt32)GlobalSize(data);
	if (src == NULL || srcSize <= 1) {
		return CString();
	}

	// convert text
	CString text = doToIClipboard(CString(src, srcSize));

	// release handle
	GlobalUnlock(data);

	// convert newlines
	return convertLinefeedToUnix(text);
}

CString
CMSWindowsClipboardAnyTextConverter::convertLinefeedToWin32(
				const CString& src) const
{
	// note -- we assume src is a valid UTF-8 string

	// count newlines in string
	UInt32 numNewlines = 0;
	UInt32 n = (UInt32)src.size();
	for (const char* scan = src.c_str(); n > 0; ++scan, --n) {
		if (*scan == '\n') {
			++numNewlines;
		}
	}
	if (numNewlines == 0) {
		return src;
	}

	// allocate new string
	CString dst;
	dst.reserve(src.size() + numNewlines);

	// copy string, converting newlines
	n = (UInt32)src.size();
	for (const char* scan = src.c_str(); n > 0; ++scan, --n) {
		if (scan[0] == '\n') {
			dst += '\r';
		}
		dst += scan[0];
	}

	return dst;
}

CString
CMSWindowsClipboardAnyTextConverter::convertLinefeedToUnix(
				const CString& src) const
{
	// count newlines in string
	UInt32 numNewlines = 0;
	UInt32 n = (UInt32)src.size();
	for (const char* scan = src.c_str(); n > 0; ++scan, --n) {
		if (scan[0] == '\r' && scan[1] == '\n') {
			++numNewlines;
		}
	}
	if (numNewlines == 0) {
		return src;
	}

	// allocate new string
	CString dst;
	dst.reserve(src.size());

	// copy string, converting newlines
	n = (UInt32)src.size();
	for (const char* scan = src.c_str(); n > 0; ++scan, --n) {
		if (scan[0] != '\r' || scan[1] != '\n') {
			dst += scan[0];
		}
	}

	return dst;
}

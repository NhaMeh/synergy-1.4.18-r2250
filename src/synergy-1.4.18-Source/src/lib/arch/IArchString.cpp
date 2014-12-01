/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012 Bolton Software Ltd.
 * Copyright (C) 2011 Chris Schoeneman
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

#include "arch/IArchString.h"
#include "arch/Arch.h"
#include "common/common.h"

#include <climits>
#include <cstring>
#include <cstdlib>

static CArchMutex              s_mutex = NULL;

//
// use C library non-reentrant multibyte conversion with mutex
//

IArchString::~IArchString()
{
	if (s_mutex != NULL) {
		ARCH->closeMutex(s_mutex);
		s_mutex = NULL;
	}
}

int
IArchString::convStringWCToMB(char* dst,
				const wchar_t* src, UInt32 n, bool* errors)
{
	int len = 0;

	bool dummyErrors;
	if (errors == NULL) {
		errors = &dummyErrors;
	}

	if (s_mutex == NULL) {
		s_mutex = ARCH->newMutex();
	}

	ARCH->lockMutex(s_mutex);

	if (dst == NULL) {
		char dummy[MB_LEN_MAX];
		for (const wchar_t* scan = src; n > 0; ++scan, --n) {
			int mblen = wctomb(dummy, *scan);
			if (mblen == -1) {
				*errors = true;
				mblen   = 1;
			}
			len += mblen;
		}
		int mblen = wctomb(dummy, L'\0');
		if (mblen != -1) {
			len += mblen - 1;
		}
	}
	else {
		char* dst0 = dst;
		for (const wchar_t* scan = src; n > 0; ++scan, --n) {
			int mblen = wctomb(dst, *scan);
			if (mblen == -1) {
				*errors = true;
				*dst++  = '?';
			}
			else {
				dst    += mblen;
			}
		}
		int mblen = wctomb(dst, L'\0');
		if (mblen != -1) {
			// don't include nul terminator
			dst += mblen - 1;
		}
		len = (int)(dst - dst0);
	}
	ARCH->unlockMutex(s_mutex);

	return len;
}

int
IArchString::convStringMBToWC(wchar_t* dst,
				const char* src, UInt32 n, bool* errors)
{
	int len = 0;
	wchar_t dummy;

	bool dummyErrors;
	if (errors == NULL) {
		errors = &dummyErrors;
	}

	if (s_mutex == NULL) {
		s_mutex = ARCH->newMutex();
	}

	ARCH->lockMutex(s_mutex);

	if (dst == NULL) {
		for (const char* scan = src; n > 0; ) {
			int mblen = mbtowc(&dummy, scan, n);
			switch (mblen) {
			case -2:
				// incomplete last character.  convert to unknown character.
				*errors = true;
				len    += 1;
				n       = 0;
				break;

			case -1:
				// invalid character.  count one unknown character and
				// start at the next byte.
				*errors = true;
				len    += 1;
				scan   += 1;
				n      -= 1;
				break;

			case 0:
				len    += 1;
				scan   += 1;
				n      -= 1;
				break;

			default:
				// normal character
				len    += 1;
				scan   += mblen;
				n      -= mblen;
				break;
			}
		}
	}
	else {
		wchar_t* dst0 = dst;
		for (const char* scan = src; n > 0; ++dst) {
			int mblen = mbtowc(dst, scan, n);
			switch (mblen) {
			case -2:
				// incomplete character.  convert to unknown character.
				*errors = true;
				*dst    = (wchar_t)0xfffd;
				n       = 0;
				break;

			case -1:
				// invalid character.  count one unknown character and
				// start at the next byte.
				*errors = true;
				*dst    = (wchar_t)0xfffd;
				scan   += 1;
				n      -= 1;
				break;

			case 0:
				*dst    = (wchar_t)0x0000;
				scan   += 1;
				n      -= 1;
				break;

			default:
				// normal character
				scan   += mblen;
				n      -= mblen;
				break;
			}
		}
		len = (int)(dst - dst0);
	}
	ARCH->unlockMutex(s_mutex);

	return len;
}

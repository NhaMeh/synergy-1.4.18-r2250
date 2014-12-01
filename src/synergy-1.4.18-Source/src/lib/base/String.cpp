/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2014 Bolton Software Ltd.
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

#include "arch/Arch.h"
#include "base/String.h"
#include "common/common.h"
#include "common/stdvector.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <stdio.h>
#include <cstdarg>

namespace synergy {
namespace string {

CString
format(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	CString result = vformat(fmt, args);
	va_end(args);
	return result;
}

CString
vformat(const char* fmt, va_list args)
{
	// find highest indexed substitution and the locations of substitutions
	std::vector<size_t> pos;
	std::vector<size_t> width;
	std::vector<int> index;
	int maxIndex = 0;
	for (const char* scan = fmt; *scan != '\0'; ++scan) {
		if (*scan == '%') {
			++scan;
			if (*scan == '\0') {
				break;
			}
			else if (*scan == '%') {
				// literal
				index.push_back(0);
				pos.push_back(static_cast<int>(scan - 1 - fmt));
				width.push_back(2);
			}
			else if (*scan == '{') {
				// get argument index
				char* end;
				int i = static_cast<int>(strtol(scan + 1, &end, 10));
				if (*end != '}') {
					// invalid index -- ignore
					scan = end - 1;
				}
				else {
					index.push_back(i);
					pos.push_back(static_cast<int>(scan - 1 - fmt));
					width.push_back(static_cast<int>(end - scan + 2));
					if (i > maxIndex) {
						maxIndex = i;
					}
					scan = end;
				}
			}
			else {
				// improper escape -- ignore
			}
		}
	}

	// get args
	std::vector<const char*> value;
	std::vector<size_t> length;
	value.push_back("%");
	length.push_back(1);
	for (int i = 0; i < maxIndex; ++i) {
		const char* arg = va_arg(args, const char*);
		size_t len = strlen(arg);
		value.push_back(arg);
		length.push_back(len);
	}

	// compute final length
	size_t resultLength = strlen(fmt);
	const int n = static_cast<int>(pos.size());
	for (int i = 0; i < n; ++i) {
		resultLength -= width[i];
		resultLength += length[index[i]];
	}

	// substitute
	CString result;
	result.reserve(resultLength);
	size_t src = 0;
	for (int i = 0; i < n; ++i) {
		result.append(fmt + src, pos[i] - src);
		result.append(value[index[i]]);
		src = pos[i] + width[i];
	}
	result.append(fmt + src);

	return result;
}

CString
sprintf(const char* fmt, ...)
{
	char tmp[1024];
	char* buffer = tmp;
	int len      = (int)(sizeof(tmp) / sizeof(tmp[0]));
	CString result;
	while (buffer != NULL) {
		// try printing into the buffer
		va_list args;
		va_start(args, fmt);
		int n = ARCH->vsnprintf(buffer, len, fmt, args);
		va_end(args);

		// if the buffer wasn't big enough then make it bigger and try again
		if (n < 0 || n > len) {
			if (buffer != tmp) {
				delete[] buffer;
			}
			len   *= 2;
			buffer = new char[len];
		}

		// if it was big enough then save the string and don't try again
		else {
			result = buffer;
			if (buffer != tmp) {
				delete[] buffer;
			}
			buffer = NULL;
		}
	}

	return result;
}

void
findReplaceAll(
	CString& subject,
	const CString& find,
	const CString& replace)
{
	size_t pos = 0;
	while ((pos = subject.find(find, pos)) != CString::npos) {
		 subject.replace(pos, find.length(), replace);
		 pos += replace.length();
	}
}


//
// CaselessCmp
//

bool
CaselessCmp::cmpEqual(
	const CString::value_type& a,
	const CString::value_type& b)
{
	// should use std::tolower but not in all versions of libstdc++ have it
	return tolower(a) == tolower(b);
}

bool
CaselessCmp::cmpLess(
	const CString::value_type& a,
	const CString::value_type& b)
{
	// should use std::tolower but not in all versions of libstdc++ have it
	return tolower(a) < tolower(b);
}

bool
CaselessCmp::less(const CString& a, const CString& b)
{
	return std::lexicographical_compare(
		a.begin(), a.end(),
		b.begin(), b.end(),
		&synergy::string::CaselessCmp::cmpLess);
}

bool
CaselessCmp::equal(const CString& a, const CString& b)
{
	return !(less(a, b) || less(b, a));
}

bool
CaselessCmp::operator()(const CString& a, const CString& b) const
{
	return less(a, b);
}

}
}

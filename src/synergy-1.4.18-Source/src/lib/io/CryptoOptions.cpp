/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2013 Bolton Software Ltd.
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

#include "io/CryptoOptions.h"
#include "io/XIO.h"

CCryptoOptions::CCryptoOptions(
		const CString& modeString,
		const CString& pass) :
	m_pass(pass),
	m_mode(parseMode(modeString))
{
}

void
CCryptoOptions::setMode(CString modeString)
{
	m_modeString = modeString;
	m_mode = parseMode(modeString);
}

ECryptoMode
CCryptoOptions::parseMode(CString modeString)
{
	if (modeString == "cfb") {
		return kCfb;
	}
	else {
		throw XIOBadCryptoMode();
	}
}

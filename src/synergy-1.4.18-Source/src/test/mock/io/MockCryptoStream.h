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

#pragma once

#include "io/CryptoStream.h"
#include "io/CryptoOptions.h"

#include "test/global/gmock.h"

class CMockCryptoStream : public CCryptoStream
{
public:
	CMockCryptoStream(IEventQueue* eventQueue, IStream* stream) :
		CCryptoStream(eventQueue, stream, CCryptoOptions("gcm", "stub"), false) { }
	MOCK_METHOD2(read, UInt32(void*, UInt32));
	MOCK_METHOD2(write, void(const void*, UInt32));
};

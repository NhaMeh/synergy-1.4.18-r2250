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

#pragma once

#include "common/IInterface.h"

using namespace synergy;
namespace synergy { class IStream; }

//! Stream filter factory interface
/*!
This interface provides factory methods to create stream filters.
*/
class IStreamFilterFactory : public IInterface {
public:
	//! Create filter
	/*!
	Create and return a stream filter on \p stream.  The caller must
	delete the returned object.
	*/
	virtual synergy::IStream*	create(IStream* stream, bool adoptStream) = 0;
};

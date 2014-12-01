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

#include "synergy/unix/AppUtilUnix.h"
#include "synergy/ArgsBase.h"

CAppUtilUnix::CAppUtilUnix(IEventQueue* events)
{
}

CAppUtilUnix::~CAppUtilUnix()
{
}

bool 
CAppUtilUnix::parseArg(const int& argc, const char* const* argv, int& i)
{
#if WINAPI_XWINDOWS
	if (app().isArg(i, argc, argv, "-display", "--display", 1)) {
		// use alternative display
		app().argsBase().m_display = argv[++i];
	}

	else if (app().isArg(i, argc, argv, NULL, "--no-xinitthreads")) {
		app().argsBase().m_disableXInitThreads = true;
	}

	else {
		// option not supported here
		return false;
	}

	return true;
#else
	// no options for carbon
	return false;
#endif
}

int
standardStartupStatic(int argc, char** argv)
{
	return CAppUtil::instance().app().standardStartup(argc, argv);
}

int
CAppUtilUnix::run(int argc, char** argv)
{
	return app().runInner(argc, argv, NULL, &standardStartupStatic);
}

void
CAppUtilUnix::startNode()
{
	app().startNode();
}

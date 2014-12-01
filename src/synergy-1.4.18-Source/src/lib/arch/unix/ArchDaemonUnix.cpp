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

#include "arch/unix/ArchDaemonUnix.h"

#include "arch/unix/XArchUnix.h"
#include "base/Log.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <cstdlib>

//
// CArchDaemonUnix
//

CArchDaemonUnix::CArchDaemonUnix()
{
	// do nothing
}

CArchDaemonUnix::~CArchDaemonUnix()
{
	// do nothing
}


#ifdef __APPLE__

// In Mac OS X, fork()'d child processes can't use most APIs (the frameworks
// that Synergy uses in fact prevent it and make the process just up and die),
// so need to exec a copy of the program that doesn't fork so isn't limited.
int
execSelfNonDaemonized()
{
	extern char** NXArgv;
	char** selfArgv = NXArgv;
	
	setenv("_SYNERGY_DAEMONIZED", "", 1);
	
	execvp(selfArgv[0], selfArgv);
	return 0;
}

bool alreadyDaemonized() {
	return getenv("_SYNERGY_DAEMONIZED") != NULL;
}

#endif

int
CArchDaemonUnix::daemonize(const char* name, DaemonFunc func)
{
#ifdef __APPLE__
	if (alreadyDaemonized())
		return func(1, &name);
#endif
	
	// fork so shell thinks we're done and so we're not a process
	// group leader
	switch (fork()) {
	case -1:
		// failed
		throw XArchDaemonFailed(new XArchEvalUnix(errno));

	case 0:
		// child
		break;

	default:
		// parent exits
		exit(0);
	}

	// become leader of a new session
	setsid();
	
#ifndef __APPLE__
	// NB: don't run chdir on apple; causes strange behaviour.
	// chdir to root so we don't keep mounted filesystems points busy
	// TODO: this is a bit of a hack - can we find a better solution?
	int chdirErr = chdir("/");
	if (chdirErr)
		// NB: file logging actually isn't working at this point!
		LOG((CLOG_ERR "chdir error: %i", chdirErr));
#endif

	// mask off permissions for any but owner
	umask(077);

	// close open files.  we only expect stdin, stdout, stderr to be open.
	close(0);
	close(1);
	close(2);

	// attach file descriptors 0, 1, 2 to /dev/null so inadvertent use
	// of standard I/O safely goes in the bit bucket.
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_RDWR);
	
	int dupErr = dup(1);
	if (dupErr)
		// NB: file logging actually isn't working at this point!
		LOG((CLOG_ERR "dup error: %i", dupErr));
	
#ifdef __APPLE__
	return execSelfNonDaemonized();
#endif
	
	// invoke function
	return func(1, &name);
}

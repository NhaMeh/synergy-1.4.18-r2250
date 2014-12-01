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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	If not, see <http://www.gnu.org/licenses/>.
 */

#include "arch/Arch.h"
#include "arch/XArch.h"
#include "base/Log.h"
#include "base/String.h"
#include "base/log_outputters.h"
#include "common/Version.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <ctime> 

// names of priorities
static const char*		g_priority[] = {
	"FATAL",
	"ERROR",
	"WARNING",
	"NOTE",
	"INFO",
	"DEBUG",
	"DEBUG1",
	"DEBUG2",
	"DEBUG3",
	"DEBUG4",
	"DEBUG5"
};

// number of priorities
static const int g_numPriority = (int)(sizeof(g_priority) / sizeof(g_priority[0]));

// the default priority
#ifndef NDEBUG
static const int		g_defaultMaxPriority = kDEBUG;
#else
static const int		g_defaultMaxPriority = kINFO;
#endif

//
// CLog
//

CLog*				 CLog::s_log = NULL;

CLog::CLog()
{
	assert(s_log == NULL);

	// create mutex for multithread safe operation
	m_mutex = ARCH->newMutex();

	// other initalization
	m_maxPriority = g_defaultMaxPriority;
	m_maxNewlineLength = 0;
	insert(new CConsoleLogOutputter);

	s_log = this;
}

CLog::~CLog()
{
	// clean up
	for (COutputterList::iterator index	= m_outputters.begin();
									index != m_outputters.end(); ++index) {
		delete *index;
	}
	for (COutputterList::iterator index	= m_alwaysOutputters.begin();
									index != m_alwaysOutputters.end(); ++index) {
		delete *index;
	}
	ARCH->closeMutex(m_mutex);
}

CLog*
CLog::getInstance()
{
	assert(s_log != NULL);
	return s_log;
}

const char*
CLog::getFilterName() const
{
	return getFilterName(getFilter());
}

const char*
CLog::getFilterName(int level) const
{
	if (level < 0) {
		return "Message";
	}
	return g_priority[level];
}

void
CLog::print(const char* file, int line, const char* fmt, ...)
{
	// check if fmt begins with a priority argument
	ELevel priority = kINFO;
	if ((strlen(fmt) > 2) && (fmt[0] == '%' && fmt[1] == 'z')) {

		// 060 in octal is 0 (48 in decimal), so subtracting this converts ascii
		// number it a true number. we could use atoi instead, but this is how
		// it was done originally.
		priority = (ELevel)(fmt[2] - '\060');

		// move the pointer on past the debug priority char
		fmt += 3;
	}

	// done if below priority threshold
	if (priority > getFilter()) {
		return;
	}

	// compute prefix padding length
	char stack[1024];

	// compute suffix padding length
	int sPad = m_maxNewlineLength;

	// print to buffer, leaving space for a newline at the end and prefix
	// at the beginning.
	char* buffer = stack;
	int len			= (int)(sizeof(stack) / sizeof(stack[0]));
	while (true) {
		// try printing into the buffer
		va_list args;
		va_start(args, fmt);
		int n = ARCH->vsnprintf(buffer, len	- sPad, fmt, args);
		va_end(args);

		// if the buffer wasn't big enough then make it bigger and try again
		if (n < 0 || n > (int)len) {
			if (buffer != stack) {
				delete[] buffer;
			}
			len	 *= 2;
			buffer = new char[len];
		}

		// if the buffer was big enough then continue
		else {
			break;
		}
	}

	// print the prefix to the buffer.	leave space for priority label.
	// do not prefix time and file for kPRINT (CLOG_PRINT)
	if (priority != kPRINT) {

		char message[kLogMessageLength];

#ifndef NDEBUG
		struct tm *tm;
		char tmp[220];
		time_t t;
		time(&t);
		tm = localtime(&t);
		sprintf(tmp, "%04i-%02i-%02iT%02i:%02i:%02i", tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
		sprintf(message, "%s %s: %s\n\t%s,%d", tmp, g_priority[priority], buffer, file, line);
#else
		sprintf(message, "%s: %s", g_priority[priority], buffer);
#endif

		output(priority, message);
	} else {
		output(priority, buffer);
	}

	// clean up
	if (buffer != stack) {
		delete[] buffer;
	}
}

void
CLog::insert(ILogOutputter* outputter, bool alwaysAtHead)
{
	assert(outputter != NULL);

	CArchMutexLock lock(m_mutex);
	if (alwaysAtHead) {
		m_alwaysOutputters.push_front(outputter);
	}
	else {
		m_outputters.push_front(outputter);
	}

	outputter->open(kAppVersion);

	// Issue 41
	// don't show log unless user requests it, as some users find this
	// feature irritating (i.e. when they lose network connectivity).
	// in windows the log window can be displayed by selecting "show log"
	// from the synergy system tray icon.
	// if this causes problems for other architectures, then a different
	// work around should be attempted.
	//outputter->show(false);
}

void
CLog::remove(ILogOutputter* outputter)
{
	CArchMutexLock lock(m_mutex);
	m_outputters.remove(outputter);
	m_alwaysOutputters.remove(outputter);
}

void
CLog::pop_front(bool alwaysAtHead)
{
	CArchMutexLock lock(m_mutex);
	COutputterList* list = alwaysAtHead ? &m_alwaysOutputters : &m_outputters;
	if (!list->empty()) {
		delete list->front();
		list->pop_front();
	}
}

bool
CLog::setFilter(const char* maxPriority)
{
	if (maxPriority != NULL) {
		for (int i = 0; i < g_numPriority; ++i) {
			if (strcmp(maxPriority, g_priority[i]) == 0) {
				setFilter(i);
				return true;
			}
		}
		return false;
	}
	return true;
}

void
CLog::setFilter(int maxPriority)
{
	CArchMutexLock lock(m_mutex);
	m_maxPriority = maxPriority;
}

int
CLog::getFilter() const
{
	CArchMutexLock lock(m_mutex);
	return m_maxPriority;
}

void
CLog::output(ELevel priority, char* msg)
{
	assert(priority >= -1 && priority < g_numPriority);
	assert(msg != NULL);
	if (!msg) return;

	CArchMutexLock lock(m_mutex);

	COutputterList::const_iterator i;

	for (i = m_alwaysOutputters.begin(); i != m_alwaysOutputters.end(); ++i) {

		// write to outputter
		(*i)->write(priority, msg);
	}

	for (i = m_outputters.begin(); i != m_outputters.end(); ++i) {

		// write to outputter and break out of loop if it returns false
		if (!(*i)->write(priority, msg)) {
			break;
		}
	}
}

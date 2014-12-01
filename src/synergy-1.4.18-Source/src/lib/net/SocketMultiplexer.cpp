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

#include "net/SocketMultiplexer.h"

#include "net/ISocketMultiplexerJob.h"
#include "mt/CondVar.h"
#include "mt/Lock.h"
#include "mt/Mutex.h"
#include "mt/Thread.h"
#include "arch/Arch.h"
#include "arch/XArch.h"
#include "base/Log.h"
#include "base/TMethodJob.h"
#include "common/stdvector.h"

//
// CSocketMultiplexer
//

CSocketMultiplexer::CSocketMultiplexer() :
	m_mutex(new CMutex),
	m_thread(NULL),
	m_update(false),
	m_jobsReady(new CCondVar<bool>(m_mutex, false)),
	m_jobListLock(new CCondVar<bool>(m_mutex, false)),
	m_jobListLockLocked(new CCondVar<bool>(m_mutex, false)),
	m_jobListLocker(NULL),
	m_jobListLockLocker(NULL)
{
	// this pointer just has to be unique and not NULL.  it will
	// never be dereferenced.  it's used to identify cursor nodes
	// in the jobs list.
	m_cursorMark = reinterpret_cast<ISocketMultiplexerJob*>(this);

	// start thread
	m_thread = new CThread(new TMethodJob<CSocketMultiplexer>(
								this, &CSocketMultiplexer::serviceThread));
}

CSocketMultiplexer::~CSocketMultiplexer()
{
	m_thread->cancel();
	m_thread->unblockPollSocket();
	m_thread->wait();
	delete m_thread;
	delete m_jobsReady;
	delete m_jobListLock;
	delete m_jobListLockLocked;
	delete m_jobListLocker;
	delete m_jobListLockLocker;
	delete m_mutex;

	// clean up jobs
	for (CSocketJobMap::iterator i = m_socketJobMap.begin();
						i != m_socketJobMap.end(); ++i) {
		delete *(i->second);
	}
}

void
CSocketMultiplexer::addSocket(ISocket* socket, ISocketMultiplexerJob* job)
{
	assert(socket != NULL);
	assert(job    != NULL);

	// prevent other threads from locking the job list
	lockJobListLock();

	// break thread out of poll
	m_thread->unblockPollSocket();

	// lock the job list
	lockJobList();

	// insert/replace job
	CSocketJobMap::iterator i = m_socketJobMap.find(socket);
	if (i == m_socketJobMap.end()) {
		// we *must* put the job at the end so the order of jobs in
		// the list continue to match the order of jobs in pfds in
		// serviceThread().
		CJobCursor j = m_socketJobs.insert(m_socketJobs.end(), job);
		m_update     = true;
		m_socketJobMap.insert(std::make_pair(socket, j));
	}
	else {
		CJobCursor j = i->second;
		if (*j != job) {
			delete *j;
			*j = job;
		}
		m_update = true;
	}

	// unlock the job list
	unlockJobList();
}

void
CSocketMultiplexer::removeSocket(ISocket* socket)
{
	assert(socket != NULL);

	// prevent other threads from locking the job list
	lockJobListLock();

	// break thread out of poll
	m_thread->unblockPollSocket();

	// lock the job list
	lockJobList();

	// remove job.  rather than removing it from the map we put NULL
	// in the list instead so the order of jobs in the list continues
	// to match the order of jobs in pfds in serviceThread().
	CSocketJobMap::iterator i = m_socketJobMap.find(socket);
	if (i != m_socketJobMap.end()) {
		if (*(i->second) != NULL) {
			delete *(i->second);
			*(i->second) = NULL;
			m_update     = true;
		}
	}

	// unlock the job list
	unlockJobList();
}

void
CSocketMultiplexer::serviceThread(void*)
{
	std::vector<IArchNetwork::CPollEntry> pfds;
	IArchNetwork::CPollEntry pfd;

	// service the connections
	for (;;) {
		CThread::testCancel();

		// wait until there are jobs to handle
		{
			CLock lock(m_mutex);
			while (!(bool)*m_jobsReady) {
				m_jobsReady->wait();
			}
		}

		// lock the job list
		lockJobListLock();
		lockJobList();

		// collect poll entries
		if (m_update) {
			m_update = false;
			pfds.clear();
			pfds.reserve(m_socketJobMap.size());

			CJobCursor cursor    = newCursor();
			CJobCursor jobCursor = nextCursor(cursor);
			while (jobCursor != m_socketJobs.end()) {
				ISocketMultiplexerJob* job = *jobCursor;
				if (job != NULL) {
					pfd.m_socket = job->getSocket();
					pfd.m_events = 0;
					if (job->isReadable()) {
						pfd.m_events |= IArchNetwork::kPOLLIN;
					}
					if (job->isWritable()) {
						pfd.m_events |= IArchNetwork::kPOLLOUT;
					}
					pfds.push_back(pfd);
				}				
				jobCursor = nextCursor(cursor);
			}
			deleteCursor(cursor);
		}

		int status;
		try {
			// check for status
			if (!pfds.empty()) {
				status = ARCH->pollSocket(&pfds[0], (int)pfds.size(), -1);
			}
			else {
				status = 0;
			}
		}
		catch (XArchNetwork& e) {
			LOG((CLOG_WARN "error in socket multiplexer: %s", e.what()));
			status = 0;
		}

		if (status != 0) {
			// iterate over socket jobs, invoking each and saving the
			// new job.
			UInt32 i             = 0;
			CJobCursor cursor    = newCursor();
			CJobCursor jobCursor = nextCursor(cursor);
			while (i < pfds.size() && jobCursor != m_socketJobs.end()) {
				if (*jobCursor != NULL) {
					// get poll state
					unsigned short revents = pfds[i].m_revents;
					bool read  = ((revents & IArchNetwork::kPOLLIN) != 0);
					bool write = ((revents & IArchNetwork::kPOLLOUT) != 0);
					bool error = ((revents & (IArchNetwork::kPOLLERR |
											  IArchNetwork::kPOLLNVAL)) != 0);

					// run job
					ISocketMultiplexerJob* job    = *jobCursor;
					ISocketMultiplexerJob* newJob = job->run(read, write, error);

					// save job, if different
					if (newJob != job) {
						CLock lock(m_mutex);
						delete job;
						*jobCursor = newJob;
						m_update   = true;
					}
					++i;
				}

				// next job
				jobCursor = nextCursor(cursor);
			}
			deleteCursor(cursor);
		}

		// delete any removed socket jobs
		for (CSocketJobMap::iterator i = m_socketJobMap.begin();
							i != m_socketJobMap.end();) {
			if (*(i->second) == NULL) {
				m_socketJobMap.erase(i++);
				m_update = true;
			}
			else {
				++i;
			}
		}

		// unlock the job list
		unlockJobList();
	}
}

CSocketMultiplexer::CJobCursor
CSocketMultiplexer::newCursor()
{
	CLock lock(m_mutex);
	return m_socketJobs.insert(m_socketJobs.begin(), m_cursorMark);
}

CSocketMultiplexer::CJobCursor
CSocketMultiplexer::nextCursor(CJobCursor cursor)
{
	CLock lock(m_mutex);
	CJobCursor j = m_socketJobs.end();
	CJobCursor i = cursor;
	while (++i != m_socketJobs.end()) {
		if (*i != m_cursorMark) {
			// found a real job (as opposed to a cursor)
			j = i;

			// move our cursor just past the job
			m_socketJobs.splice(++i, m_socketJobs, cursor);
			break;
		}
	}
	return j;
}

void
CSocketMultiplexer::deleteCursor(CJobCursor cursor)
{
	CLock lock(m_mutex);
	m_socketJobs.erase(cursor);
}

void
CSocketMultiplexer::lockJobListLock()
{
	CLock lock(m_mutex);

	// wait for the lock on the lock
	while (*m_jobListLockLocked) {
		m_jobListLockLocked->wait();
	}

	// take ownership of the lock on the lock
	*m_jobListLockLocked = true;
	m_jobListLockLocker  = new CThread(CThread::getCurrentThread());
}

void
CSocketMultiplexer::lockJobList()
{
	CLock lock(m_mutex);

	// make sure we're the one that called lockJobListLock()
	assert(*m_jobListLockLocker == CThread::getCurrentThread());

	// wait for the job list lock
	while (*m_jobListLock) {
		m_jobListLock->wait();
	}

	// take ownership of the lock
	*m_jobListLock      = true;
	m_jobListLocker     = m_jobListLockLocker;
	m_jobListLockLocker = NULL;

	// release the lock on the lock
	*m_jobListLockLocked = false;
	m_jobListLockLocked->broadcast();
}

void
CSocketMultiplexer::unlockJobList()
{
	CLock lock(m_mutex);

	// make sure we're the one that called lockJobList()
	assert(*m_jobListLocker == CThread::getCurrentThread());

	// release the lock
	delete m_jobListLocker;
	m_jobListLocker = NULL;
	*m_jobListLock  = false;
	m_jobListLock->signal();

	// set new jobs ready state
	bool isReady = !m_socketJobMap.empty();
	if (*m_jobsReady != isReady) {
		*m_jobsReady = isReady;
		m_jobsReady->signal();
	}
}

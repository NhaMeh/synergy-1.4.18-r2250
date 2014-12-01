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

#include "arch/IArchMultithread.h"
#include "common/stdlist.h"

#include <pthread.h>

#define ARCH_MULTITHREAD CArchMultithreadPosix

class CArchCondImpl {
public:
	pthread_cond_t		m_cond;
};

class CArchMutexImpl {
public:
	pthread_mutex_t		m_mutex;
};

//! Posix implementation of IArchMultithread
class CArchMultithreadPosix : public IArchMultithread {
public:
	CArchMultithreadPosix();
	virtual ~CArchMultithreadPosix();

	//! @name manipulators
	//@{

	void				setNetworkDataForCurrentThread(void*);

	//@}
	//! @name accessors
	//@{

	void*				getNetworkDataForThread(CArchThread);

	static CArchMultithreadPosix*	getInstance();

	//@}

	// IArchMultithread overrides
	virtual CArchCond	newCondVar();
	virtual void		closeCondVar(CArchCond);
	virtual void		signalCondVar(CArchCond);
	virtual void		broadcastCondVar(CArchCond);
	virtual bool		waitCondVar(CArchCond, CArchMutex, double timeout);
	virtual CArchMutex	newMutex();
	virtual void		closeMutex(CArchMutex);
	virtual void		lockMutex(CArchMutex);
	virtual void		unlockMutex(CArchMutex);
	virtual CArchThread	newThread(ThreadFunc, void*);
	virtual CArchThread	newCurrentThread();
	virtual CArchThread	copyThread(CArchThread);
	virtual void		closeThread(CArchThread);
	virtual void		cancelThread(CArchThread);
	virtual void		setPriorityOfThread(CArchThread, int n);
	virtual void		testCancelThread();
	virtual bool		wait(CArchThread, double timeout);
	virtual bool		isSameThread(CArchThread, CArchThread);
	virtual bool		isExitedThread(CArchThread);
	virtual void*		getResultOfThread(CArchThread);
	virtual ThreadID	getIDOfThread(CArchThread);
	virtual void		setSignalHandler(ESignal, SignalFunc, void*);
	virtual void		raiseSignal(ESignal);

private:
	void				startSignalHandler();

	CArchThreadImpl*	find(pthread_t thread);
	CArchThreadImpl*	findNoRef(pthread_t thread);
	void				insert(CArchThreadImpl* thread);
	void				erase(CArchThreadImpl* thread);

	void				refThread(CArchThreadImpl* rep);
	void				testCancelThreadImpl(CArchThreadImpl* rep);

	void				doThreadFunc(CArchThread thread);
	static void*		threadFunc(void* vrep);
	static void			threadCancel(int);
	static void*		threadSignalHandler(void* vrep);

private:
	typedef std::list<CArchThread> CThreadList;

	static CArchMultithreadPosix*	s_instance;

	bool				m_newThreadCalled;

	CArchMutex			m_threadMutex;
	CArchThread			m_mainThread;
	CThreadList			m_threadList;
	ThreadID			m_nextID;

	pthread_t			m_signalThread;
	SignalFunc			m_signalFunc[kNUM_SIGNALS];
	void*				m_signalUserData[kNUM_SIGNALS];
};

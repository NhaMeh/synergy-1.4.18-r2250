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

#include "synergy/App.h"
#include "synergy/ArgsBase.h"

class CScreen;
class CEvent;
class CClient;
class CNetworkAddress;
class CThread;

class CClientApp : public CApp {
public:
	class CArgs : public CArgsBase {
	public:
		CArgs();
		~CArgs();

	public:
		int m_yscroll;
		CNetworkAddress* m_serverAddress;
	};

	CClientApp(IEventQueue* events, CreateTaskBarReceiverFunc createTaskBarReceiver);
	virtual ~CClientApp();

	// Parse client specific command line arguments.
	void parseArgs(int argc, const char* const* argv);

	// Prints help specific to client.
	void help();

	// Returns arguments that are common and for client.
	CArgs& args() const { return (CArgs&)argsBase(); }

	const char* daemonName() const;
	const char* daemonInfo() const;

	// TODO: move to server only (not supported on client)
	void loadConfig() { }
	bool loadConfig(const CString& pathname) { return false; }

	int foregroundStartup(int argc, char** argv);
	int standardStartup(int argc, char** argv);
	int runInner(int argc, char** argv, ILogOutputter* outputter, StartupFunc startup);
	CScreen* createScreen();
	void updateStatus();
	void updateStatus(const CString& msg);
	void resetRestartTimeout();
	double nextRestartTimeout();
	void handleScreenError(const CEvent&, void*);
	CScreen* openClientScreen();
	void closeClientScreen(CScreen* screen);
	void handleClientRestart(const CEvent&, void* vtimer);
	void scheduleClientRestart(double retryTime);
	void handleClientConnected(const CEvent&, void*);
	void handleClientFailed(const CEvent& e, void*);
	void handleClientDisconnected(const CEvent&, void*);
	CClient* openClient(const CString& name, const CNetworkAddress& address, CScreen* screen, const CCryptoOptions& crypto);
	void closeClient(CClient* client);
	bool startClient();
	void stopClient();
	int mainLoop();
	void startNode();

	static CClientApp& instance() { return (CClientApp&)CApp::instance(); }

	CClient* getClientPtr() { return m_client; }
	
private:
	virtual bool parseArg(const int& argc, const char* const* argv, int& i);

private:
	CClient*			m_client;
	CScreen*			m_clientScreen;
};

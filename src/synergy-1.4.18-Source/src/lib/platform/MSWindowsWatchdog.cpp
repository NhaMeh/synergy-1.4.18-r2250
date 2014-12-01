/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012 Bolton Software Ltd.
 * Copyright (C) 2009 Chris Schoeneman
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

#include "platform/MSWindowsWatchdog.h"

#include "ipc/IpcLogOutputter.h"
#include "ipc/IpcServer.h"
#include "ipc/IpcMessage.h"
#include "ipc/Ipc.h"
#include "synergy/App.h"
#include "synergy/ArgsBase.h"
#include "mt/Thread.h"
#include "arch/win32/ArchDaemonWindows.h"
#include "arch/win32/XArchWindows.h"
#include "arch/Arch.h"
#include "base/TMethodJob.h"
#include "base/Log.h"
#include "common/Version.h"

#include <sstream>
#include <UserEnv.h>
#include <Shellapi.h>

enum {
	kOutputBufferSize = 4096
};

typedef VOID (WINAPI *SendSas)(BOOL asUser);

CMSWindowsWatchdog::CMSWindowsWatchdog(
	bool autoDetectCommand,
	CIpcServer& ipcServer,
	CIpcLogOutputter& ipcLogOutputter) :
	m_thread(NULL),
	m_autoDetectCommand(autoDetectCommand),
	m_monitoring(true),
	m_commandChanged(false),
	m_stdOutWrite(NULL),
	m_stdOutRead(NULL),
	m_ipcServer(ipcServer),
	m_ipcLogOutputter(ipcLogOutputter),
	m_elevateProcess(false),
	m_processFailures(0),
	m_processRunning(false)
{
}

CMSWindowsWatchdog::~CMSWindowsWatchdog()
{
}

void 
CMSWindowsWatchdog::startAsync()
{
	m_thread = new CThread(new TMethodJob<CMSWindowsWatchdog>(
		this, &CMSWindowsWatchdog::mainLoop, nullptr));

	m_outputThread = new CThread(new TMethodJob<CMSWindowsWatchdog>(
		this, &CMSWindowsWatchdog::outputLoop, nullptr));
}

void
CMSWindowsWatchdog::stop()
{
	m_monitoring = false;
	
	m_thread->wait(5);
	delete m_thread;

	m_outputThread->wait(5);
	delete m_outputThread;
}

HANDLE
CMSWindowsWatchdog::duplicateProcessToken(HANDLE process, LPSECURITY_ATTRIBUTES security)
{
	HANDLE sourceToken;

	BOOL tokenRet = OpenProcessToken(
		process,
		TOKEN_ASSIGN_PRIMARY | TOKEN_ALL_ACCESS,
		&sourceToken);

	if (!tokenRet) {
		LOG((CLOG_ERR "could not open token, process handle: %d", process));
		throw XArch(new XArchEvalWindows());
	}
	
	LOG((CLOG_DEBUG "got token %i, duplicating", sourceToken));

	HANDLE newToken;
	BOOL duplicateRet = DuplicateTokenEx(
		sourceToken, TOKEN_ASSIGN_PRIMARY | TOKEN_ALL_ACCESS, security,
		SecurityImpersonation, TokenPrimary, &newToken);

	if (!duplicateRet) {
		LOG((CLOG_ERR "could not duplicate token %i", sourceToken));
		throw XArch(new XArchEvalWindows());
	}
	
	LOG((CLOG_DEBUG "duplicated, new token: %i", newToken));
	return newToken;
}

HANDLE 
CMSWindowsWatchdog::getUserToken(LPSECURITY_ATTRIBUTES security)
{
	// always elevate if we are at the vista/7 login screen. we could also 
	// elevate for the uac dialog (consent.exe) but this would be pointless,
	// since synergy would re-launch as non-elevated after the desk switch,
	// and so would be unusable with the new elevated process taking focus.
	if (m_elevateProcess || m_session.isProcessInSession("logonui.exe", NULL)) {
		
		LOG((CLOG_DEBUG "getting elevated token, %s",
			(m_elevateProcess ? "elevation required" : "at login screen")));
		
		HANDLE process;
		if (!m_session.isProcessInSession("winlogon.exe", &process)) {
			throw XMSWindowsWatchdogError("cannot get user token without winlogon.exe");
		}

		return duplicateProcessToken(process, security);
	}
	else {
		LOG((CLOG_DEBUG "getting non-elevated token"));
		return m_session.getUserToken(security);
	}
}

void
CMSWindowsWatchdog::mainLoop(void*)
{
	shutdownExistingProcesses();

	SendSas sendSasFunc = NULL;
	HINSTANCE sasLib = LoadLibrary("sas.dll");
	if (sasLib) {
		LOG((CLOG_DEBUG "found sas.dll"));
		sendSasFunc = (SendSas)GetProcAddress(sasLib, "SendSAS");
	}

	SECURITY_ATTRIBUTES saAttr; 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL; 

	if (!CreatePipe(&m_stdOutRead, &m_stdOutWrite, &saAttr, 0)) {
		throw XArch(new XArchEvalWindows());
	}

	ZeroMemory(&m_processInfo, sizeof(PROCESS_INFORMATION));

	while (m_monitoring) {
		try {

			if (m_processRunning && getCommand().empty()) {
				LOG((CLOG_INFO "process started but command is empty, shutting down"));
				shutdownExistingProcesses();
				m_processRunning = false;
				continue;
			}

			if (m_processFailures != 0) {
				// increasing backoff period, maximum of 10 seconds.
				int timeout = (m_processFailures * 2) < 10 ? (m_processFailures * 2) : 10;
				LOG((CLOG_INFO "backing off, wait=%ds, failures=%d", timeout, m_processFailures));
				ARCH->sleep(timeout);
			}
		
			if (!getCommand().empty() && ((m_processFailures != 0) || m_session.hasChanged() || m_commandChanged)) {
				startProcess();
			}

			if (m_processRunning && !isProcessActive()) {

				m_processFailures++;
				m_processRunning = false;
			
				LOG((CLOG_WARN "detected application not running, pid=%d",
					m_processInfo.dwProcessId));
			}

			if (sendSasFunc != NULL) {

				HANDLE sendSasEvent = CreateEvent(NULL, FALSE, FALSE, "Global\\SendSAS");
				if (sendSasEvent != NULL) {

					// use SendSAS event to wait for next session (timeout 1 second).
					if (WaitForSingleObject(sendSasEvent, 1000) == WAIT_OBJECT_0) {
						LOG((CLOG_DEBUG "calling SendSAS"));
						sendSasFunc(FALSE);
					}

					CloseHandle(sendSasEvent);
					continue;
				}
			}

			// if the sas event failed, wait by sleeping.
			ARCH->sleep(1);
		
		}
		catch (std::exception& e) {
			LOG((CLOG_ERR "failed to launch, error: %s", e.what()));
			m_processFailures++;
			m_processRunning = false;
			continue;
		}
		catch (...) {
			LOG((CLOG_ERR "failed to launch, unknown error."));
			m_processFailures++;
			m_processRunning = false;
			continue;
		}
	}

	if (m_processRunning) {
		LOG((CLOG_DEBUG "terminated running process on exit"));
		shutdownProcess(m_processInfo.hProcess, m_processInfo.dwProcessId, 20);
	}
	
	LOG((CLOG_DEBUG "watchdog main thread finished"));
}

bool
CMSWindowsWatchdog::isProcessActive()
{
	DWORD exitCode;
	GetExitCodeProcess(m_processInfo.hProcess, &exitCode);
	return exitCode == STILL_ACTIVE;
}

void
CMSWindowsWatchdog::startProcess()
{
	if (m_command.empty()) {
		throw XMSWindowsWatchdogError("cannot start process, command is empty");
	}

	m_commandChanged = false;

	if (m_processRunning) {
		LOG((CLOG_DEBUG "closing existing process to make way for new one"));
		shutdownProcess(m_processInfo.hProcess, m_processInfo.dwProcessId, 20);
		m_processRunning = false;
	}

	m_session.updateActiveSession();

	SECURITY_ATTRIBUTES sa;
	ZeroMemory(&sa, sizeof(SECURITY_ATTRIBUTES));

	HANDLE userToken = getUserToken(&sa);

	// patch by Jack Zhou and Henry Tung
	// set UIAccess to fix Windows 8 GUI interaction
	// http://synergy-project.org/spit/issues/details/3338/#c70
	DWORD uiAccess = 1;
	SetTokenInformation(userToken, TokenUIAccess, &uiAccess, sizeof(DWORD));

	// clear, as we're reusing process info struct
	ZeroMemory(&m_processInfo, sizeof(PROCESS_INFORMATION));

	STARTUPINFO si;
	ZeroMemory(&si, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	si.lpDesktop = "winsta0\\Default"; // TODO: maybe this should be \winlogon if we have logonui.exe?
	si.hStdError = m_stdOutWrite;
	si.hStdOutput = m_stdOutWrite;
	si.dwFlags |= STARTF_USESTDHANDLES;

	LPVOID environment;
	BOOL blockRet = CreateEnvironmentBlock(&environment, userToken, FALSE);
	if (!blockRet) {
		LOG((CLOG_ERR "could not create environment block"));
		throw XArch(new XArchEvalWindows);
	}

	DWORD creationFlags = 
		NORMAL_PRIORITY_CLASS |
		CREATE_NO_WINDOW |
		CREATE_UNICODE_ENVIRONMENT;

	// re-launch in current active user session
	LOG((CLOG_INFO "starting new process"));
	BOOL createRet = CreateProcessAsUser(
		userToken, NULL, LPSTR(m_command.c_str()),
		&sa, NULL, TRUE, creationFlags,
		environment, NULL, &si, &m_processInfo);

	DestroyEnvironmentBlock(environment);
	CloseHandle(userToken);

	if (!createRet) {
		LOG((CLOG_ERR "could not launch"));
		throw XArch(new XArchEvalWindows);
	}
	else {
		// wait for program to fail.
		ARCH->sleep(1);
		if (!isProcessActive()) {
			throw XMSWindowsWatchdogError("process immediately stopped");
		}

		m_processRunning = true;
		m_processFailures = 0;

		LOG((CLOG_DEBUG "started process, session=%i, command=%s",
			m_session.getActiveSessionId(), m_command.c_str()));
	}
}

void
CMSWindowsWatchdog::setCommand(const std::string& command, bool elevate)
{
	LOG((CLOG_INFO "service command updated"));
	m_command = command;
	m_elevateProcess = elevate;
	m_commandChanged = true;
	m_processFailures = 0;
}

std::string
CMSWindowsWatchdog::getCommand() const
{
	if (!m_autoDetectCommand) {
		return m_command;
	}

	// seems like a fairly convoluted way to get the process name
	const char* launchName = CApp::instance().argsBase().m_pname;
	std::string args = ARCH->commandLine();

	// build up a full command line
	std::stringstream cmdTemp;
	cmdTemp << launchName << args;

	std::string cmd = cmdTemp.str();

	size_t i;
	std::string find = "--relaunch";
	while ((i = cmd.find(find)) != std::string::npos) {
		cmd.replace(i, find.length(), "");
	}

	return cmd;
}

void
CMSWindowsWatchdog::outputLoop(void*)
{
	// +1 char for \0
	CHAR buffer[kOutputBufferSize + 1];

	while (m_monitoring) {
		
		DWORD bytesRead;
		BOOL success = ReadFile(m_stdOutRead, buffer, kOutputBufferSize, &bytesRead, NULL);

		// assume the process has gone away? slow down
		// the reads until another one turns up.
		if (!success || bytesRead == 0) {
			ARCH->sleep(1);
		}
		else {
			buffer[bytesRead] = '\0';

			// send process output over IPC to GUI, and force it to be sent
			// which bypasses the ipc logging anti-recursion mechanism.
			m_ipcLogOutputter.write(kINFO, buffer, true);
		}
			
	}
}

void
CMSWindowsWatchdog::shutdownProcess(HANDLE handle, DWORD pid, int timeout)
{
	DWORD exitCode;
	GetExitCodeProcess(handle, &exitCode);
	if (exitCode != STILL_ACTIVE) {
		return;
	}

	CIpcShutdownMessage shutdown;
	m_ipcServer.send(shutdown, kIpcClientNode);

	// wait for process to exit gracefully.
	double start = ARCH->time();
	while (true) {

		GetExitCodeProcess(handle, &exitCode);
		if (exitCode != STILL_ACTIVE) {
			// yay, we got a graceful shutdown. there should be no hook in use errors!
			LOG((CLOG_INFO "process %d was shutdown gracefully", pid));
			break;
		}
		else {
			
			double elapsed = (ARCH->time() - start);
			if (elapsed > timeout) {
				// if timeout reached, kill forcefully.
				// calling TerminateProcess on synergy is very bad!
				// it causes the hook DLL to stay loaded in some apps,
				// making it impossible to start synergy again.
				LOG((CLOG_WARN "shutdown timed out after %d secs, forcefully terminating", (int)elapsed));
				TerminateProcess(handle, kExitSuccess);
				break;
			}

			ARCH->sleep(1);
		}
	}
}

void
CMSWindowsWatchdog::shutdownExistingProcesses()
{
	// first we need to take a snapshot of the running processes
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		LOG((CLOG_ERR "could not get process snapshot"));
		throw XArch(new XArchEvalWindows);
	}

	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	// get the first process, and if we can't do that then it's 
	// unlikely we can go any further
	BOOL gotEntry = Process32First(snapshot, &entry);
	if (!gotEntry) {
		LOG((CLOG_ERR "could not get first process entry"));
		throw XArch(new XArchEvalWindows);
	}

	// now just iterate until we can find winlogon.exe pid
	DWORD pid = 0;
	while (gotEntry) {

		// make sure we're not checking the system process
		if (entry.th32ProcessID != 0) {

			if (_stricmp(entry.szExeFile, "synergyc.exe") == 0 ||
				_stricmp(entry.szExeFile, "synergys.exe") == 0) {
				
				HANDLE handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);
				shutdownProcess(handle, entry.th32ProcessID, 10);
			}
		}

		// now move on to the next entry (if we're not at the end)
		gotEntry = Process32Next(snapshot, &entry);
		if (!gotEntry) {

			DWORD err = GetLastError();
			if (err != ERROR_NO_MORE_FILES) {

				// only worry about error if it's not the end of the snapshot
				LOG((CLOG_ERR "could not get subsiquent process entry"));
				throw XArch(new XArchEvalWindows);
			}
		}
	}

	CloseHandle(snapshot);
	m_processRunning = false;
}

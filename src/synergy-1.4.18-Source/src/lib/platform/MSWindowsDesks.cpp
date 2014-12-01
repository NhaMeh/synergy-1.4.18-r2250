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

#include "platform/MSWindowsDesks.h"

#include "synwinhk/synwinhk.h"
#include "platform/MSWindowsScreen.h"
#include "synergy/IScreenSaver.h"
#include "synergy/XScreen.h"
#include "mt/Lock.h"
#include "mt/Thread.h"
#include "arch/win32/ArchMiscWindows.h"
#include "base/Log.h"
#include "base/IEventQueue.h"
#include "base/IJob.h"
#include "base/TMethodEventJob.h"
#include "base/TMethodJob.h"
#include "base/IEventQueue.h"

#include <malloc.h>

// these are only defined when WINVER >= 0x0500
#if !defined(SPI_GETMOUSESPEED)
#define SPI_GETMOUSESPEED 112
#endif
#if !defined(SPI_SETMOUSESPEED)
#define SPI_SETMOUSESPEED 113
#endif
#if !defined(SPI_GETSCREENSAVERRUNNING)
#define SPI_GETSCREENSAVERRUNNING 114
#endif

// X button stuff
#if !defined(WM_XBUTTONDOWN)
#define WM_XBUTTONDOWN		0x020B
#define WM_XBUTTONUP		0x020C
#define WM_XBUTTONDBLCLK	0x020D
#define WM_NCXBUTTONDOWN	0x00AB
#define WM_NCXBUTTONUP		0x00AC
#define WM_NCXBUTTONDBLCLK	0x00AD
#define MOUSEEVENTF_XDOWN	0x0080
#define MOUSEEVENTF_XUP		0x0100
#define XBUTTON1			0x0001
#define XBUTTON2			0x0002
#endif
#if !defined(VK_XBUTTON1)
#define VK_XBUTTON1			0x05
#define VK_XBUTTON2			0x06
#endif

// <unused>; <unused>
#define SYNERGY_MSG_SWITCH			SYNERGY_HOOK_LAST_MSG + 1
// <unused>; <unused>
#define SYNERGY_MSG_ENTER			SYNERGY_HOOK_LAST_MSG + 2
// <unused>; <unused>
#define SYNERGY_MSG_LEAVE			SYNERGY_HOOK_LAST_MSG + 3
// wParam = flags, HIBYTE(lParam) = virtual key, LOBYTE(lParam) = scan code
#define SYNERGY_MSG_FAKE_KEY		SYNERGY_HOOK_LAST_MSG + 4
 // flags, XBUTTON id
#define SYNERGY_MSG_FAKE_BUTTON		SYNERGY_HOOK_LAST_MSG + 5
// x; y
#define SYNERGY_MSG_FAKE_MOVE		SYNERGY_HOOK_LAST_MSG + 6
// xDelta; yDelta
#define SYNERGY_MSG_FAKE_WHEEL		SYNERGY_HOOK_LAST_MSG + 7
// POINT*; <unused>
#define SYNERGY_MSG_CURSOR_POS		SYNERGY_HOOK_LAST_MSG + 8
// IKeyState*; <unused>
#define SYNERGY_MSG_SYNC_KEYS		SYNERGY_HOOK_LAST_MSG + 9
// install; <unused>
#define SYNERGY_MSG_SCREENSAVER		SYNERGY_HOOK_LAST_MSG + 10
// dx; dy
#define SYNERGY_MSG_FAKE_REL_MOVE	SYNERGY_HOOK_LAST_MSG + 11
// enable; <unused>
#define SYNERGY_MSG_FAKE_INPUT		SYNERGY_HOOK_LAST_MSG + 12

//
// CMSWindowsDesks
//

CMSWindowsDesks::CMSWindowsDesks(
		bool isPrimary, bool noHooks, HINSTANCE hookLibrary,
		const IScreenSaver* screensaver, IEventQueue* events,
		IJob* updateKeys, bool stopOnDeskSwitch) :
	m_isPrimary(isPrimary),
	m_noHooks(noHooks),
	m_isOnScreen(m_isPrimary),
	m_x(0), m_y(0),
	m_w(0), m_h(0),
	m_xCenter(0), m_yCenter(0),
	m_multimon(false),
	m_timer(NULL),
	m_screensaver(screensaver),
	m_screensaverNotify(false),
	m_activeDesk(NULL),
	m_activeDeskName(),
	m_mutex(),
	m_deskReady(&m_mutex, false),
	m_updateKeys(updateKeys),
	m_events(events),
	m_stopOnDeskSwitch(stopOnDeskSwitch)
{
	if (hookLibrary != NULL)
		queryHookLibrary(hookLibrary);

	m_cursor    = createBlankCursor();
	m_deskClass = createDeskWindowClass(m_isPrimary);
	m_keyLayout = GetKeyboardLayout(GetCurrentThreadId());
	resetOptions();
}

CMSWindowsDesks::~CMSWindowsDesks()
{
	disable();
	destroyClass(m_deskClass);
	destroyCursor(m_cursor);
	delete m_updateKeys;
}

void
CMSWindowsDesks::enable()
{
	m_threadID = GetCurrentThreadId();

	// set the active desk and (re)install the hooks
	checkDesk();

	// install the desk timer.  this timer periodically checks
	// which desk is active and reinstalls the hooks as necessary.
	// we wouldn't need this if windows notified us of a desktop
	// change but as far as i can tell it doesn't.
	m_timer = m_events->newTimer(0.2, NULL);
	m_events->adoptHandler(CEvent::kTimer, m_timer,
							new TMethodEventJob<CMSWindowsDesks>(
								this, &CMSWindowsDesks::handleCheckDesk));

	updateKeys();
}

void
CMSWindowsDesks::disable()
{
	// remove timer
	if (m_timer != NULL) {
		m_events->removeHandler(CEvent::kTimer, m_timer);
		m_events->deleteTimer(m_timer);
		m_timer = NULL;
	}

	// destroy desks
	removeDesks();

	m_isOnScreen = m_isPrimary;
}

void
CMSWindowsDesks::enter()
{
	sendMessage(SYNERGY_MSG_ENTER, 0, 0);
}

void
CMSWindowsDesks::leave(HKL keyLayout)
{
	sendMessage(SYNERGY_MSG_LEAVE, (WPARAM)keyLayout, 0);
}

void
CMSWindowsDesks::resetOptions()
{
	m_leaveForegroundOption = false;
}

void
CMSWindowsDesks::setOptions(const COptionsList& options)
{
	for (UInt32 i = 0, n = (UInt32)options.size(); i < n; i += 2) {
		if (options[i] == kOptionWin32KeepForeground) {
			m_leaveForegroundOption = (options[i + 1] != 0);
			LOG((CLOG_DEBUG1 "%s the foreground window", m_leaveForegroundOption ? "don\'t grab" : "grab"));
		}
	}
}

void
CMSWindowsDesks::updateKeys()
{
	sendMessage(SYNERGY_MSG_SYNC_KEYS, 0, 0);
}

void
CMSWindowsDesks::setShape(SInt32 x, SInt32 y,
				SInt32 width, SInt32 height,
				SInt32 xCenter, SInt32 yCenter, bool isMultimon)
{
	m_x        = x;
	m_y        = y;
	m_w        = width;
	m_h        = height;
	m_xCenter  = xCenter;
	m_yCenter  = yCenter;
	m_multimon = isMultimon;
}

void
CMSWindowsDesks::installScreensaverHooks(bool install)
{
	if (m_isPrimary && m_screensaverNotify != install) {
		m_screensaverNotify = install;
		sendMessage(SYNERGY_MSG_SCREENSAVER, install, 0);
	}
}

void
CMSWindowsDesks::fakeInputBegin()
{
	sendMessage(SYNERGY_MSG_FAKE_INPUT, 1, 0);
}

void
CMSWindowsDesks::fakeInputEnd()
{
	sendMessage(SYNERGY_MSG_FAKE_INPUT, 0, 0);
}

void
CMSWindowsDesks::getCursorPos(SInt32& x, SInt32& y) const
{
	POINT pos;
	sendMessage(SYNERGY_MSG_CURSOR_POS, reinterpret_cast<WPARAM>(&pos), 0);
	x = pos.x;
	y = pos.y;
}

void
CMSWindowsDesks::fakeKeyEvent(
				KeyButton button, UINT virtualKey,
				bool press, bool /*isAutoRepeat*/) const
{
	// synthesize event
	DWORD flags = 0;
	if (((button & 0x100u) != 0)) {
		flags |= KEYEVENTF_EXTENDEDKEY;
	}
	if (!press) {
		flags |= KEYEVENTF_KEYUP;
	}
	sendMessage(SYNERGY_MSG_FAKE_KEY, flags,
							MAKEWORD(static_cast<BYTE>(button & 0xffu),
								static_cast<BYTE>(virtualKey & 0xffu)));
}

void
CMSWindowsDesks::fakeMouseButton(ButtonID button, bool press)
{
	// the system will swap the meaning of left/right for us if
	// the user has configured a left-handed mouse but we don't
	// want it to swap since we want the handedness of the
	// server's mouse.  so pre-swap for a left-handed mouse.
	if (GetSystemMetrics(SM_SWAPBUTTON)) {
		switch (button) {
		case kButtonLeft:
			button = kButtonRight;
			break;

		case kButtonRight:
			button = kButtonLeft;
			break;
		}
	}

	// map button id to button flag and button data
	DWORD data = 0;
	DWORD flags;
	switch (button) {
	case kButtonLeft:
		flags = press ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
		break;

	case kButtonMiddle:
		flags = press ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
		break;

	case kButtonRight:
		flags = press ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
		break;

	case kButtonExtra0 + 0:
		data = XBUTTON1;
		flags = press ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
		break;

	case kButtonExtra0 + 1:
		data = XBUTTON2;
		flags = press ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
		break;

	default:
		return;
	}

	// do it
	sendMessage(SYNERGY_MSG_FAKE_BUTTON, flags, data);
}

void
CMSWindowsDesks::fakeMouseMove(SInt32 x, SInt32 y) const
{
	sendMessage(SYNERGY_MSG_FAKE_MOVE,
							static_cast<WPARAM>(x),
							static_cast<LPARAM>(y));
}

void
CMSWindowsDesks::fakeMouseRelativeMove(SInt32 dx, SInt32 dy) const
{
	sendMessage(SYNERGY_MSG_FAKE_REL_MOVE,
							static_cast<WPARAM>(dx),
							static_cast<LPARAM>(dy));
}

void
CMSWindowsDesks::fakeMouseWheel(SInt32 xDelta, SInt32 yDelta) const
{
	sendMessage(SYNERGY_MSG_FAKE_WHEEL, xDelta, yDelta);
}

void
CMSWindowsDesks::sendMessage(UINT msg, WPARAM wParam, LPARAM lParam) const
{
	if (m_activeDesk != NULL && m_activeDesk->m_window != NULL) {
		PostThreadMessage(m_activeDesk->m_threadID, msg, wParam, lParam);
		waitForDesk();
	}
}

void
CMSWindowsDesks::queryHookLibrary(HINSTANCE hookLibrary)
{
	// look up functions
	if (m_isPrimary && !m_noHooks) {
		m_install   = (InstallFunc)GetProcAddress(hookLibrary, "install");
		m_uninstall = (UninstallFunc)GetProcAddress(hookLibrary, "uninstall");
		m_installScreensaver   =
				  (InstallScreenSaverFunc)GetProcAddress(
								hookLibrary, "installScreenSaver");
		m_uninstallScreensaver =
				  (UninstallScreenSaverFunc)GetProcAddress(
								hookLibrary, "uninstallScreenSaver");
		if (m_install              == NULL ||
			m_uninstall            == NULL ||
			m_installScreensaver   == NULL ||
			m_uninstallScreensaver == NULL) {
			LOG((CLOG_ERR "Invalid hook library"));
			throw XScreenOpenFailure();
		}
	}
	else {
		m_install              = NULL;
		m_uninstall            = NULL;
		m_installScreensaver   = NULL;
		m_uninstallScreensaver = NULL;
	}
}

HCURSOR
CMSWindowsDesks::createBlankCursor() const
{
	// create a transparent cursor
	int cw = GetSystemMetrics(SM_CXCURSOR);
	int ch = GetSystemMetrics(SM_CYCURSOR);
	UInt8* cursorAND = new UInt8[ch * ((cw + 31) >> 2)];
	UInt8* cursorXOR = new UInt8[ch * ((cw + 31) >> 2)];
	memset(cursorAND, 0xff, ch * ((cw + 31) >> 2));
	memset(cursorXOR, 0x00, ch * ((cw + 31) >> 2));
	HCURSOR c = CreateCursor(CMSWindowsScreen::getWindowInstance(),
							0, 0, cw, ch, cursorAND, cursorXOR);
	delete[] cursorXOR;
	delete[] cursorAND;
	return c;
}

void
CMSWindowsDesks::destroyCursor(HCURSOR cursor) const
{
	if (cursor != NULL) {
		DestroyCursor(cursor);
	}
}

ATOM
CMSWindowsDesks::createDeskWindowClass(bool isPrimary) const
{
	WNDCLASSEX classInfo;
	classInfo.cbSize        = sizeof(classInfo);
	classInfo.style         = CS_DBLCLKS | CS_NOCLOSE;
	classInfo.lpfnWndProc   = isPrimary ?
								&CMSWindowsDesks::primaryDeskProc :
								&CMSWindowsDesks::secondaryDeskProc;
	classInfo.cbClsExtra    = 0;
	classInfo.cbWndExtra    = 0;
	classInfo.hInstance     = CMSWindowsScreen::getWindowInstance();
	classInfo.hIcon         = NULL;
	classInfo.hCursor       = m_cursor;
	classInfo.hbrBackground = NULL;
	classInfo.lpszMenuName  = NULL;
	classInfo.lpszClassName = "SynergyDesk";
	classInfo.hIconSm       = NULL;
	return RegisterClassEx(&classInfo);
}

void
CMSWindowsDesks::destroyClass(ATOM windowClass) const
{
	if (windowClass != 0) {
		UnregisterClass(reinterpret_cast<LPCTSTR>(windowClass),
							CMSWindowsScreen::getWindowInstance());
	}
}

HWND
CMSWindowsDesks::createWindow(ATOM windowClass, const char* name) const
{
	HWND window = CreateWindowEx(WS_EX_TOPMOST |
									WS_EX_TRANSPARENT |
									WS_EX_TOOLWINDOW,
								reinterpret_cast<LPCTSTR>(windowClass),
								name,
								WS_POPUP,
								0, 0, 1, 1,
								NULL, NULL,
								CMSWindowsScreen::getWindowInstance(),
								NULL);
	if (window == NULL) {
		LOG((CLOG_ERR "failed to create window: %d", GetLastError()));
		throw XScreenOpenFailure();
	}
	return window;
}

void
CMSWindowsDesks::destroyWindow(HWND hwnd) const
{
	if (hwnd != NULL) {
		DestroyWindow(hwnd);
	}
}

LRESULT CALLBACK
CMSWindowsDesks::primaryDeskProc(
				HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK
CMSWindowsDesks::secondaryDeskProc(
				HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// would like to detect any local user input and hide the hider
	// window but for now we just detect mouse motion.
	bool hide = false;
	switch (msg) {
	case WM_MOUSEMOVE:
		if (LOWORD(lParam) != 0 || HIWORD(lParam) != 0) {
			hide = true;
		}
		break;
	}

	if (hide && IsWindowVisible(hwnd)) {
		ReleaseCapture();
		SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
							SWP_NOMOVE | SWP_NOSIZE |
							SWP_NOACTIVATE | SWP_HIDEWINDOW);
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void
CMSWindowsDesks::deskMouseMove(SInt32 x, SInt32 y) const
{
	// when using absolute positioning with mouse_event(),
	// the normalized device coordinates range over only
	// the primary screen.
	SInt32 w = GetSystemMetrics(SM_CXSCREEN);
	SInt32 h = GetSystemMetrics(SM_CYSCREEN);
	mouse_event(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE,
							(DWORD)((65535.0f * x) / (w - 1) + 0.5f),
							(DWORD)((65535.0f * y) / (h - 1) + 0.5f),
							0, 0);
}

void
CMSWindowsDesks::deskMouseRelativeMove(SInt32 dx, SInt32 dy) const
{
	// relative moves are subject to cursor acceleration which we don't
	// want.so we disable acceleration, do the relative move, then
	// restore acceleration.  there's a slight chance we'll end up in
	// the wrong place if the user moves the cursor using this system's
	// mouse while simultaneously moving the mouse on the server
	// system.  that defeats the purpose of synergy so we'll assume
	// that won't happen.  even if it does, the next mouse move will
	// correct the position.

	// save mouse speed & acceleration
	int oldSpeed[4];
	bool accelChanged =
				SystemParametersInfo(SPI_GETMOUSE,0, oldSpeed, 0) &&
				SystemParametersInfo(SPI_GETMOUSESPEED, 0, oldSpeed + 3, 0);

	// use 1:1 motion
	if (accelChanged) {
		int newSpeed[4] = { 0, 0, 0, 1 };
		accelChanged =
				SystemParametersInfo(SPI_SETMOUSE, 0, newSpeed, 0) ||
				SystemParametersInfo(SPI_SETMOUSESPEED, 0, newSpeed + 3, 0);
	}

	// move relative to mouse position
	mouse_event(MOUSEEVENTF_MOVE, dx, dy, 0, 0);

	// restore mouse speed & acceleration
	if (accelChanged) {
		SystemParametersInfo(SPI_SETMOUSE, 0, oldSpeed, 0);
		SystemParametersInfo(SPI_SETMOUSESPEED, 0, oldSpeed + 3, 0);
	}
}

void
CMSWindowsDesks::deskEnter(CDesk* desk)
{
	if (!m_isPrimary) {
		ReleaseCapture();
	}
	ShowCursor(TRUE);
	SetWindowPos(desk->m_window, HWND_BOTTOM, 0, 0, 0, 0,
							SWP_NOMOVE | SWP_NOSIZE |
							SWP_NOACTIVATE | SWP_HIDEWINDOW);

	// restore the foreground window
	// XXX -- this raises the window to the top of the Z-order.  we
	// want it to stay wherever it was to properly support X-mouse
	// (mouse over activation) but i've no idea how to do that.
	// the obvious workaround of using SetWindowPos() to move it back
	// after being raised doesn't work.
	DWORD thisThread =
		GetWindowThreadProcessId(desk->m_window, NULL);
	DWORD thatThread =
		GetWindowThreadProcessId(desk->m_foregroundWindow, NULL);
	AttachThreadInput(thatThread, thisThread, TRUE);
	SetForegroundWindow(desk->m_foregroundWindow);
	AttachThreadInput(thatThread, thisThread, FALSE);
	EnableWindow(desk->m_window, desk->m_lowLevel ? FALSE : TRUE);
	desk->m_foregroundWindow = NULL;
}

void
CMSWindowsDesks::deskLeave(CDesk* desk, HKL keyLayout)
{
	ShowCursor(FALSE);
	if (m_isPrimary) {
		// map a window to hide the cursor and to use whatever keyboard
		// layout we choose rather than the keyboard layout of the last
		// active window.
		int x, y, w, h;
		if (desk->m_lowLevel) {
			// with a low level hook the cursor will never budge so
			// just a 1x1 window is sufficient.
			x = m_xCenter;
			y = m_yCenter;
			w = 1;
			h = 1;
		}
		else {
			// with regular hooks the cursor will jitter as it's moved
			// by the user then back to the center by us.  to be sure
			// we never lose it, cover all the monitors with the window.
			x = m_x;
			y = m_y;
			w = m_w;
			h = m_h;
		}
		SetWindowPos(desk->m_window, HWND_TOPMOST, x, y, w, h,
							SWP_NOACTIVATE | SWP_SHOWWINDOW);

		// if not using low-level hooks we have to also activate the
		// window to ensure we don't lose keyboard focus.
		// FIXME -- see if this can be avoided.  if so then always
		// disable the window (see handling of SYNERGY_MSG_SWITCH).
		if (!desk->m_lowLevel) {
			SetActiveWindow(desk->m_window);
		}

		// if using low-level hooks then disable the foreground window
		// so it can't mess up any of our keyboard events.  the console
		// program, for example, will cause characters to be reported as
		// unshifted, regardless of the shift key state.  interestingly
		// we do see the shift key go down and up.
		//
		// note that we must enable the window to activate it and we
		// need to disable the window on deskEnter.
		else {
			desk->m_foregroundWindow = getForegroundWindow();
			if (desk->m_foregroundWindow != NULL) {
				EnableWindow(desk->m_window, TRUE);
				SetActiveWindow(desk->m_window);
				DWORD thisThread =
					GetWindowThreadProcessId(desk->m_window, NULL);
				DWORD thatThread =
					GetWindowThreadProcessId(desk->m_foregroundWindow, NULL);
				AttachThreadInput(thatThread, thisThread, TRUE);
				SetForegroundWindow(desk->m_window);
				AttachThreadInput(thatThread, thisThread, FALSE);
			}
		}

		// switch to requested keyboard layout
		ActivateKeyboardLayout(keyLayout, 0);
	}
	else {
		// move hider window under the cursor center, raise, and show it
		SetWindowPos(desk->m_window, HWND_TOPMOST,
							m_xCenter, m_yCenter, 1, 1,
							SWP_NOACTIVATE | SWP_SHOWWINDOW);

		// watch for mouse motion.  if we see any then we hide the
		// hider window so the user can use the physically attached
		// mouse if desired.  we'd rather not capture the mouse but
		// we aren't notified when the mouse leaves our window.
		SetCapture(desk->m_window);

		// warp the mouse to the cursor center
		LOG((CLOG_DEBUG2 "warping cursor to center: %+d,%+d", m_xCenter, m_yCenter));
		deskMouseMove(m_xCenter, m_yCenter);
	}
}

void
CMSWindowsDesks::deskThread(void* vdesk)
{
	MSG msg;

	// use given desktop for this thread
	CDesk* desk              = reinterpret_cast<CDesk*>(vdesk);
	desk->m_threadID         = GetCurrentThreadId();
	desk->m_window           = NULL;
	desk->m_foregroundWindow = NULL;
	if (desk->m_desk != NULL && SetThreadDesktop(desk->m_desk) != 0) {
		// create a message queue
		PeekMessage(&msg, NULL, 0,0, PM_NOREMOVE);

		// create a window.  we use this window to hide the cursor.
		try {
			desk->m_window = createWindow(m_deskClass, "SynergyDesk");
			LOG((CLOG_DEBUG "desk %s window is 0x%08x", desk->m_name.c_str(), desk->m_window));
		}
		catch (...) {
			// ignore
			LOG((CLOG_DEBUG "can't create desk window for %s", desk->m_name.c_str()));
		}
	}

	// tell main thread that we're ready
	{
		CLock lock(&m_mutex);
		m_deskReady = true;
		m_deskReady.broadcast();
	}

	while (GetMessage(&msg, NULL, 0, 0)) {
		switch (msg.message) {
		default:
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;

		case SYNERGY_MSG_SWITCH:
			if (m_isPrimary && !m_noHooks) {
				m_uninstall();
				if (m_screensaverNotify) {
					m_uninstallScreensaver();
					m_installScreensaver();
				}
				switch (m_install()) {
				case kHOOK_FAILED:
					// we won't work on this desk
					desk->m_lowLevel = false;
					break;

				case kHOOK_OKAY:
					desk->m_lowLevel = false;
					break;

				case kHOOK_OKAY_LL:
					desk->m_lowLevel = true;
					break;
				}

				// a window on the primary screen with low-level hooks
				// should never activate.
				if (desk->m_window)
					EnableWindow(desk->m_window, desk->m_lowLevel ? FALSE : TRUE);
			}
			break;

		case SYNERGY_MSG_ENTER:
			m_isOnScreen = true;
			deskEnter(desk);
			break;

		case SYNERGY_MSG_LEAVE:
			m_isOnScreen = false;
			m_keyLayout  = (HKL)msg.wParam;
			deskLeave(desk, m_keyLayout);
			break;

		case SYNERGY_MSG_FAKE_KEY:
			keybd_event(HIBYTE(msg.lParam), LOBYTE(msg.lParam), (DWORD)msg.wParam, 0);
			break;

		case SYNERGY_MSG_FAKE_BUTTON:
			if (msg.wParam != 0) {
				mouse_event((DWORD)msg.wParam, 0, 0, (DWORD)msg.lParam, 0);
			}
			break;

		case SYNERGY_MSG_FAKE_MOVE:
			deskMouseMove(static_cast<SInt32>(msg.wParam),
							static_cast<SInt32>(msg.lParam));
			break;

		case SYNERGY_MSG_FAKE_REL_MOVE:
			deskMouseRelativeMove(static_cast<SInt32>(msg.wParam),
							static_cast<SInt32>(msg.lParam));
			break;

		case SYNERGY_MSG_FAKE_WHEEL:
			// XXX -- add support for x-axis scrolling
			if (msg.lParam != 0) {
				mouse_event(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)msg.lParam, 0);
			}
			break;

		case SYNERGY_MSG_CURSOR_POS: {
			POINT* pos = reinterpret_cast<POINT*>(msg.wParam);
			if (!GetCursorPos(pos)) {
				pos->x = m_xCenter;
				pos->y = m_yCenter;
			}
			break;
		}

		case SYNERGY_MSG_SYNC_KEYS:
			m_updateKeys->run();
			break;

		case SYNERGY_MSG_SCREENSAVER:
			if (!m_noHooks) {
				if (msg.wParam != 0) {
					m_installScreensaver();
				}
				else {
					m_uninstallScreensaver();
				}
			}
			break;

		case SYNERGY_MSG_FAKE_INPUT:
			keybd_event(SYNERGY_HOOK_FAKE_INPUT_VIRTUAL_KEY,
								SYNERGY_HOOK_FAKE_INPUT_SCANCODE,
								msg.wParam ? 0 : KEYEVENTF_KEYUP, 0);
			break;
		}

		// notify that message was processed
		CLock lock(&m_mutex);
		m_deskReady = true;
		m_deskReady.broadcast();
	}

	// clean up
	deskEnter(desk);
	if (desk->m_window != NULL) {
		DestroyWindow(desk->m_window);
	}
	if (desk->m_desk != NULL) {
		closeDesktop(desk->m_desk);
	}
}

CMSWindowsDesks::CDesk*
CMSWindowsDesks::addDesk(const CString& name, HDESK hdesk)
{
	CDesk* desk      = new CDesk;
	desk->m_name     = name;
	desk->m_desk     = hdesk;
	desk->m_targetID = GetCurrentThreadId();
	desk->m_thread   = new CThread(new TMethodJob<CMSWindowsDesks>(
						this, &CMSWindowsDesks::deskThread, desk));
	waitForDesk();
	m_desks.insert(std::make_pair(name, desk));
	return desk;
}

void
CMSWindowsDesks::removeDesks()
{
	for (CDesks::iterator index = m_desks.begin();
							index != m_desks.end(); ++index) {
		CDesk* desk = index->second;
		PostThreadMessage(desk->m_threadID, WM_QUIT, 0, 0);
		desk->m_thread->wait();
		delete desk->m_thread;
		delete desk;
	}
	m_desks.clear();
	m_activeDesk     = NULL;
	m_activeDeskName = "";
}

void
CMSWindowsDesks::checkDesk()
{
	// get current desktop.  if we already know about it then return.
	CDesk* desk;
	HDESK hdesk  = openInputDesktop();
	CString name = getDesktopName(hdesk);
	CDesks::const_iterator index = m_desks.find(name);
	if (index == m_desks.end()) {
		desk = addDesk(name, hdesk);
		// hold on to hdesk until thread exits so the desk can't
		// be removed by the system
	}
	else {
		closeDesktop(hdesk);
		desk = index->second;
	}

	// if we are told to shut down on desk switch, and this is not the 
	// first switch, then shut down.
	if (m_stopOnDeskSwitch && m_activeDesk != NULL && name != m_activeDeskName) {
		LOG((CLOG_DEBUG "shutting down because of desk switch to \"%s\"", name.c_str()));
		m_events->addEvent(CEvent(CEvent::kQuit));
		return;
	}

	// if active desktop changed then tell the old and new desk threads
	// about the change.  don't switch desktops when the screensaver is
	// active becaue we'd most likely switch to the screensaver desktop
	// which would have the side effect of forcing the screensaver to
	// stop.
	if (name != m_activeDeskName && !m_screensaver->isActive()) {
		// show cursor on previous desk
		bool wasOnScreen = m_isOnScreen;
		if (!wasOnScreen) {
			sendMessage(SYNERGY_MSG_ENTER, 0, 0);
		}

		// check for desk accessibility change.  we don't get events
		// from an inaccessible desktop so when we switch from an
		// inaccessible desktop to an accessible one we have to
		// update the keyboard state.
		LOG((CLOG_DEBUG "switched to desk \"%s\"", name.c_str()));
		bool syncKeys = false;
		bool isAccessible = isDeskAccessible(desk);
		if (isDeskAccessible(m_activeDesk) != isAccessible) {
			if (isAccessible) {
				LOG((CLOG_DEBUG "desktop is now accessible"));
				syncKeys = true;
			}
			else {
				LOG((CLOG_DEBUG "desktop is now inaccessible"));
			}
		}

		// switch desk
		m_activeDesk     = desk;
		m_activeDeskName = name;
		sendMessage(SYNERGY_MSG_SWITCH, 0, 0);

		// hide cursor on new desk
		if (!wasOnScreen) {
			sendMessage(SYNERGY_MSG_LEAVE, (WPARAM)m_keyLayout, 0);
		}

		// update keys if necessary
		if (syncKeys) {
			updateKeys();
		}
	}
	else if (name != m_activeDeskName) {
		// screen saver might have started
		PostThreadMessage(m_threadID, SYNERGY_MSG_SCREEN_SAVER, TRUE, 0);
	}
}

bool
CMSWindowsDesks::isDeskAccessible(const CDesk* desk) const
{
	return (desk != NULL && desk->m_desk != NULL);
}

void
CMSWindowsDesks::waitForDesk() const
{
	CMSWindowsDesks* self = const_cast<CMSWindowsDesks*>(this);

	CLock lock(&m_mutex);
	while (!(bool)m_deskReady) {
		m_deskReady.wait();
	}
	self->m_deskReady = false;
}

void
CMSWindowsDesks::handleCheckDesk(const CEvent&, void*)
{
	checkDesk();

	// also check if screen saver is running if on a modern OS and
	// this is the primary screen.
	if (m_isPrimary) {
		BOOL running;
		SystemParametersInfo(SPI_GETSCREENSAVERRUNNING, 0, &running, FALSE);
		PostThreadMessage(m_threadID, SYNERGY_MSG_SCREEN_SAVER, running, 0);
	}
}

HDESK
CMSWindowsDesks::openInputDesktop()
{
	return OpenInputDesktop(
		DF_ALLOWOTHERACCOUNTHOOK, TRUE,
		DESKTOP_CREATEWINDOW | DESKTOP_HOOKCONTROL | GENERIC_WRITE);
}

void
CMSWindowsDesks::closeDesktop(HDESK desk)
{
	if (desk != NULL) {
		CloseDesktop(desk);
	}
}

CString
CMSWindowsDesks::getDesktopName(HDESK desk)
{
	if (desk == NULL) {
		return CString();
	}
	else {
		DWORD size;
		GetUserObjectInformation(desk, UOI_NAME, NULL, 0, &size);
		TCHAR* name = (TCHAR*)alloca(size + sizeof(TCHAR));
		GetUserObjectInformation(desk, UOI_NAME, name, size, &size);
		CString result(name);
		return result;
	}
}

HWND
CMSWindowsDesks::getForegroundWindow() const
{
	// Ideally we'd return NULL as much as possible, only returning
	// the actual foreground window when we know it's going to mess
	// up our keyboard input.  For now we'll just let the user
	// decide.
	if (m_leaveForegroundOption) {
		return NULL;
	}
	return GetForegroundWindow();
}

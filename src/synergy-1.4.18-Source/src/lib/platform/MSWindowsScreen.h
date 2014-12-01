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

#include "platform/MSWindowsHook.h"
#include "synergy/PlatformScreen.h"
#include "synergy/DragInformation.h"
#include "synwinhk/synwinhk.h"
#include "mt/CondVar.h"
#include "mt/Mutex.h"
#include "base/String.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

class CEventQueueTimer;
class CMSWindowsDesks;
class CMSWindowsKeyState;
class CMSWindowsScreenSaver;
class CThread;
class CMSWindowsDropTarget;

//! Implementation of IPlatformScreen for Microsoft Windows
class CMSWindowsScreen : public CPlatformScreen {
public:
	CMSWindowsScreen(
		bool isPrimary,
		bool noHooks,
		bool stopOnDeskSwitch,
		IEventQueue* events);
	virtual ~CMSWindowsScreen();

	//! @name manipulators
	//@{

	//! Initialize
	/*!
	Saves the application's HINSTANCE.  This \b must be called by
	WinMain with the HINSTANCE it was passed.
	*/
	static void			init(HINSTANCE);

	//@}
	//! @name accessors
	//@{

	//! Get instance
	/*!
	Returns the application instance handle passed to init().
	*/
	static HINSTANCE	getWindowInstance();

	//@}

	// IScreen overrides
	virtual void*		getEventTarget() const;
	virtual bool		getClipboard(ClipboardID id, IClipboard*) const;
	virtual void		getShape(SInt32& x, SInt32& y,
							SInt32& width, SInt32& height) const;
	virtual void		getCursorPos(SInt32& x, SInt32& y) const;

	// IPrimaryScreen overrides
	virtual void		reconfigure(UInt32 activeSides);
	virtual void		warpCursor(SInt32 x, SInt32 y);
	virtual UInt32		registerHotKey(KeyID key,
							KeyModifierMask mask);
	virtual void		unregisterHotKey(UInt32 id);
	virtual void		fakeInputBegin();
	virtual void		fakeInputEnd();
	virtual SInt32		getJumpZoneSize() const;
	virtual bool		isAnyMouseButtonDown(UInt32& buttonID) const;
	virtual void		getCursorCenter(SInt32& x, SInt32& y) const;

	// ISecondaryScreen overrides
	virtual void		fakeMouseButton(ButtonID id, bool press);
	virtual void		fakeMouseMove(SInt32 x, SInt32 y);
	virtual void		fakeMouseRelativeMove(SInt32 dx, SInt32 dy) const;
	virtual void		fakeMouseWheel(SInt32 xDelta, SInt32 yDelta) const;

	// IKeyState overrides
	virtual void		updateKeys();
	virtual void		fakeKeyDown(KeyID id, KeyModifierMask mask,
							KeyButton button);
	virtual bool		fakeKeyRepeat(KeyID id, KeyModifierMask mask,
							SInt32 count, KeyButton button);
	virtual bool		fakeKeyUp(KeyButton button);
	virtual void		fakeAllKeysUp();

	// IPlatformScreen overrides
	virtual void		enable();
	virtual void		disable();
	virtual void		enter();
	virtual bool		leave();
	virtual bool		setClipboard(ClipboardID, const IClipboard*);
	virtual void		checkClipboards();
	virtual void		openScreensaver(bool notify);
	virtual void		closeScreensaver();
	virtual void		screensaver(bool activate);
	virtual void		resetOptions();
	virtual void		setOptions(const COptionsList& options);
	virtual void		setSequenceNumber(UInt32);
	virtual bool		isPrimary() const;
	virtual void		fakeDraggingFiles(CDragFileList fileList);
	virtual CString&	getDraggingFilename();
	virtual const CString&	
						getDropTarget() const;

protected:
	// IPlatformScreen overrides
	virtual void		handleSystemEvent(const CEvent&, void*);
	virtual void		updateButtons();
	virtual IKeyState*	getKeyState() const;

private:
	// initialization and shutdown operations
	HCURSOR				createBlankCursor() const;
	void				destroyCursor(HCURSOR cursor) const;
	ATOM				createWindowClass() const;
	ATOM				createDeskWindowClass(bool isPrimary) const;
	void				destroyClass(ATOM windowClass) const;
	HWND				createWindow(ATOM windowClass, const char* name) const;
	HWND				createDropWindow(ATOM windowClass, const char* name) const;
	void				destroyWindow(HWND) const;

	// convenience function to send events
public: // HACK
	void				sendEvent(CEvent::Type type, void* = NULL);
private: // HACK
	void				sendClipboardEvent(CEvent::Type type, ClipboardID id);

	// handle message before it gets dispatched.  returns true iff
	// the message should not be dispatched.
	bool				onPreDispatch(HWND, UINT, WPARAM, LPARAM);

	// handle message before it gets dispatched.  returns true iff
	// the message should not be dispatched.
	bool				onPreDispatchPrimary(HWND, UINT, WPARAM, LPARAM);

	// handle message.  returns true iff handled and optionally sets
	// \c *result (which defaults to 0).
	bool				onEvent(HWND, UINT, WPARAM, LPARAM, LRESULT* result);

	// message handlers
	bool				onMark(UInt32 mark);
	bool				onKey(WPARAM, LPARAM);
	bool				onHotKey(WPARAM, LPARAM);
	bool				onMouseButton(WPARAM, LPARAM);
	bool				onMouseMove(SInt32 x, SInt32 y);
	bool				onMouseWheel(SInt32 xDelta, SInt32 yDelta);
	bool				onScreensaver(bool activated);
	bool				onDisplayChange();
	bool				onClipboardChange();

	// warp cursor without discarding queued events
	void				warpCursorNoFlush(SInt32 x, SInt32 y);

	// discard posted messages
	void				nextMark();

	// test if event should be ignored
	bool				ignore() const;

	// update screen size cache
	void				updateScreenShape();

	// fix timer callback
	void				handleFixes(const CEvent&, void*);

	// fix the clipboard viewer chain
	void				fixClipboardViewer();

	// enable/disable special key combinations so we can catch/pass them
	void				enableSpecialKeys(bool) const;

	// map a button event to a button ID
	ButtonID			mapButtonFromEvent(WPARAM msg, LPARAM button) const;

	// map a button event to a press (true) or release (false)
	bool				mapPressFromEvent(WPARAM msg, LPARAM button) const;

	// job to update the key state
	void				updateKeysCB(void*);

	// determine whether the mouse is hidden by the system and force
	// it to be displayed if user has entered this secondary screen.
	void				forceShowCursor();

	// forceShowCursor uses MouseKeys to show the cursor.  since we
	// don't actually want MouseKeys behavior we have to make sure
	// it applies when NumLock is in whatever state it's not in now.
	// this method does that.
	void				updateForceShowCursor();

	// our window proc
	static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
	
	// save last position of mouse to compute next delta movement
	void saveMousePosition(SInt32 x, SInt32 y);
	
	// check if it is a modifier key repeating message
	bool				isModifierRepeat(KeyModifierMask oldState, 
							KeyModifierMask state, WPARAM wParam) const;

	// send drag info and data back to server
	void				sendDragThread(void*);

private:
	struct CHotKeyItem {
	public:
		CHotKeyItem(UINT vk, UINT modifiers);

		UINT			getVirtualKey() const;

		bool			operator<(const CHotKeyItem&) const;

	private:
		UINT			m_keycode;
		UINT			m_mask;
	};
	typedef std::map<UInt32, CHotKeyItem> HotKeyMap;
	typedef std::vector<UInt32> HotKeyIDList;
	typedef std::map<CHotKeyItem, UInt32> HotKeyToIDMap;

	static HINSTANCE	s_windowInstance;

	// true if screen is being used as a primary screen, false otherwise
	bool				m_isPrimary;

	// true if hooks are not to be installed (useful for debugging)
	bool				m_noHooks;

	// true if mouse has entered the screen
	bool				m_isOnScreen;

	// our resources
	ATOM				m_class;

	// screen shape stuff
	SInt32				m_x, m_y;
	SInt32				m_w, m_h;
	SInt32				m_xCenter, m_yCenter;

	// true if system appears to have multiple monitors
	bool				m_multimon;

	// last mouse position
	SInt32				m_xCursor, m_yCursor;

	// last clipboard
	UInt32				m_sequenceNumber;

	// used to discard queued messages that are no longer needed
	UInt32				m_mark;
	UInt32				m_markReceived;

	// the main loop's thread id
	DWORD				m_threadID;

	// timer for periodically checking stuff that requires polling
	CEventQueueTimer*	m_fixTimer;

	// the keyboard layout to use when off primary screen
	HKL					m_keyLayout;

	// screen saver stuff
	CMSWindowsScreenSaver*	m_screensaver;
	bool					m_screensaverNotify;
	bool					m_screensaverActive;

	// clipboard stuff.  our window is used mainly as a clipboard
	// owner and as a link in the clipboard viewer chain.
	HWND				m_window;
	HWND				m_nextClipboardWindow;
	bool				m_ownClipboard;

	// one desk per desktop and a cond var to communicate with it
	CMSWindowsDesks*	m_desks;

	// keyboard stuff
	CMSWindowsKeyState*	m_keyState;

	// hot key stuff
	HotKeyMap			m_hotKeys;
	HotKeyIDList		m_oldHotKeyIDs;
	HotKeyToIDMap		m_hotKeyToIDMap;

	// map of button state
	bool				m_buttons[1 + kButtonExtra0 + 1];

	// the system shows the mouse cursor when an internal display count
	// is >= 0.  this count is maintained per application but there's
	// apparently a system wide count added to the application's count.
	// this system count is 0 if there's a mouse attached to the system
	// and -1 otherwise.  the MouseKeys accessibility feature can modify
	// this system count by making the system appear to have a mouse.
	//
	// m_hasMouse is true iff there's a mouse attached to the system or
	// MouseKeys is simulating one.  we track this so we can force the
	// cursor to be displayed when the user has entered this screen.
	// m_showingMouse is true when we're doing that.
	bool				m_hasMouse;
	bool				m_showingMouse;
	bool				m_gotOldMouseKeys;
	MOUSEKEYS			m_mouseKeys;
	MOUSEKEYS			m_oldMouseKeys;
	
	CMSWindowsHook		m_hook;

	static CMSWindowsScreen*
						s_screen;
	
	IEventQueue*		m_events;

	CString				m_desktopPath;

	CMSWindowsDropTarget*
						m_dropTarget;
	HWND				m_dropWindow;
	const int			m_dropWindowSize;

	CThread*			m_sendDragThread;
};

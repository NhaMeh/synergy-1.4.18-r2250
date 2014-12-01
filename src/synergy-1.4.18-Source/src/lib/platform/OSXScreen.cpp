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

#include "platform/OSXScreen.h"

#include "base/EventQueue.h"
#include "client/Client.h"
#include "platform/OSXClipboard.h"
#include "platform/OSXEventQueueBuffer.h"
#include "platform/OSXKeyState.h"
#include "platform/OSXScreenSaver.h"
#include "platform/OSXDragSimulator.h"
#include "platform/OSXPasteboardPeeker.h"
#include "synergy/Clipboard.h"
#include "synergy/KeyMap.h"
#include "synergy/ClientApp.h"
#include "mt/CondVar.h"
#include "mt/Lock.h"
#include "mt/Mutex.h"
#include "mt/Thread.h"
#include "arch/XArch.h"
#include "base/Log.h"
#include "base/IEventQueue.h"
#include "base/TMethodEventJob.h"
#include "base/TMethodJob.h"

#include <math.h>
#include <mach-o/dyld.h>
#include <AvailabilityMacros.h>
#include <IOKit/hidsystem/event_status_driver.h>

// Set some enums for fast user switching if we're building with an SDK
// from before such support was added.
#if !defined(MAC_OS_X_VERSION_10_3) || \
	(MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_3)
enum {
	kEventClassSystem				  = 'macs',
	kEventSystemUserSessionActivated   = 10,
	kEventSystemUserSessionDeactivated = 11
};
#endif

// This isn't in any Apple SDK that I know of as of yet.
enum {
	kSynergyEventMouseScroll = 11,
	kSynergyMouseScrollAxisX = 'saxx',
	kSynergyMouseScrollAxisY = 'saxy'
};

// TODO: upgrade deprecated function usage in these functions.
void setZeroSuppressionInterval();
void avoidSupression();
void logCursorVisibility();
void avoidHesitatingCursor();

//
// COSXScreen
//

bool					COSXScreen::s_testedForGHOM = false;
bool					COSXScreen::s_hasGHOM	    = false;

COSXScreen::COSXScreen(IEventQueue* events, bool isPrimary, bool autoShowHideCursor) :
	CPlatformScreen(events),
	m_isPrimary(isPrimary),
	m_isOnScreen(m_isPrimary),
	m_cursorPosValid(false),
	MouseButtonEventMap(NumButtonIDs),
	m_cursorHidden(false),
	m_dragNumButtonsDown(0),
	m_dragTimer(NULL),
	m_keyState(NULL),
	m_sequenceNumber(0),
	m_screensaver(NULL),
	m_screensaverNotify(false),
	m_ownClipboard(false),
	m_clipboardTimer(NULL),
	m_hiddenWindow(NULL),
	m_userInputWindow(NULL),
	m_switchEventHandlerRef(0),
	m_pmMutex(new CMutex),
	m_pmWatchThread(NULL),
	m_pmThreadReady(new CCondVar<bool>(m_pmMutex, false)),
	m_pmRootPort(0),
	m_activeModifierHotKey(0),
	m_activeModifierHotKeyMask(0),
	m_eventTapPort(nullptr),
	m_eventTapRLSR(nullptr),
	m_lastSingleClick(0),
	m_lastDoubleClick(0),
	m_lastSingleClickXCursor(0),
	m_lastSingleClickYCursor(0),
	m_autoShowHideCursor(autoShowHideCursor),
	m_events(events),
	m_getDropTargetThread(NULL)
{
	try {
		m_displayID   = CGMainDisplayID();
		updateScreenShape(m_displayID, 0);
		m_screensaver = new COSXScreenSaver(m_events, getEventTarget());
		m_keyState	  = new COSXKeyState(m_events);
		
		// only needed when running as a server.
		if (m_isPrimary) {
		
#if defined(MAC_OS_X_VERSION_10_9)
			// we can't pass options to show the dialog, this must be done by the gui.
			if (!AXIsProcessTrusted()) {
				throw XArch("assistive devices does not trust this process, allow it in system settings.");
			}
#else
			// now deprecated in mavericks.
			if (!AXAPIEnabled()) {
				throw XArch("assistive devices is not enabled, enable it in system settings.");
			}
#endif
		}
		
		// install display manager notification handler
#if defined(MAC_OS_X_VERSION_10_5)
		CGDisplayRegisterReconfigurationCallback(displayReconfigurationCallback, this);
#else
		m_displayManagerNotificationUPP =
			NewDMExtendedNotificationUPP(displayManagerCallback);
		OSStatus err = GetCurrentProcess(&m_PSN);
		err = DMRegisterExtendedNotifyProc(m_displayManagerNotificationUPP,
							this, 0, &m_PSN);
#endif

		// install fast user switching event handler
		EventTypeSpec switchEventTypes[2];
		switchEventTypes[0].eventClass = kEventClassSystem;
		switchEventTypes[0].eventKind  = kEventSystemUserSessionDeactivated;
		switchEventTypes[1].eventClass = kEventClassSystem;
		switchEventTypes[1].eventKind  = kEventSystemUserSessionActivated;
		EventHandlerUPP switchEventHandler =
			NewEventHandlerUPP(userSwitchCallback);
		InstallApplicationEventHandler(switchEventHandler, 2, switchEventTypes,
									   this, &m_switchEventHandlerRef);
		DisposeEventHandlerUPP(switchEventHandler);

		constructMouseButtonEventMap();

		// watch for requests to sleep
		m_events->adoptHandler(m_events->forCOSXScreen().confirmSleep(),
								getEventTarget(),
								new TMethodEventJob<COSXScreen>(this,
									&COSXScreen::handleConfirmSleep));

		// create thread for monitoring system power state.
		*m_pmThreadReady = false;
#if defined(MAC_OS_X_VERSION_10_7)
		m_carbonLoopMutex = new CMutex();
		m_carbonLoopReady = new CCondVar<bool>(m_carbonLoopMutex, false);
#endif
		LOG((CLOG_DEBUG "starting watchSystemPowerThread"));
		m_pmWatchThread = new CThread(new TMethodJob<COSXScreen>
								(this, &COSXScreen::watchSystemPowerThread));
	}
	catch (...) {
		m_events->removeHandler(m_events->forCOSXScreen().confirmSleep(),
								getEventTarget());
		if (m_switchEventHandlerRef != 0) {
			RemoveEventHandler(m_switchEventHandlerRef);
		}
#if defined(MAC_OS_X_VERSION_10_5)
		CGDisplayRemoveReconfigurationCallback(displayReconfigurationCallback, this);
#else
		if (m_displayManagerNotificationUPP != NULL) {
			DMRemoveExtendedNotifyProc(m_displayManagerNotificationUPP,
							NULL, &m_PSN, 0);
		}

		if (m_hiddenWindow) {
			ReleaseWindow(m_hiddenWindow);
			m_hiddenWindow = NULL;
		}

		if (m_userInputWindow) {
			ReleaseWindow(m_userInputWindow);
			m_userInputWindow = NULL;
		}
#endif

		delete m_keyState;
		delete m_screensaver;
		throw;
	}

	// install event handlers
	m_events->adoptHandler(CEvent::kSystem, m_events->getSystemTarget(),
							new TMethodEventJob<COSXScreen>(this,
								&COSXScreen::handleSystemEvent));

	// install the platform event queue
	m_events->adoptBuffer(new COSXEventQueueBuffer(m_events));
}

COSXScreen::~COSXScreen()
{
	disable();
	m_events->adoptBuffer(NULL);
	m_events->removeHandler(CEvent::kSystem, m_events->getSystemTarget());

	if (m_pmWatchThread) {
		// make sure the thread has setup the runloop.
		{
			CLock lock(m_pmMutex);
			while (!(bool)*m_pmThreadReady) {
				m_pmThreadReady->wait();
			}
		}

		// now exit the thread's runloop and wait for it to exit
		LOG((CLOG_DEBUG "stopping watchSystemPowerThread"));
		CFRunLoopStop(m_pmRunloop);
		m_pmWatchThread->wait();
		delete m_pmWatchThread;
		m_pmWatchThread = NULL;
	}
	delete m_pmThreadReady;
	delete m_pmMutex;

	m_events->removeHandler(m_events->forCOSXScreen().confirmSleep(),
								getEventTarget());

	RemoveEventHandler(m_switchEventHandlerRef);

#if defined(MAC_OS_X_VERSION_10_5)
	CGDisplayRemoveReconfigurationCallback(displayReconfigurationCallback, this);
#else
	DMRemoveExtendedNotifyProc(m_displayManagerNotificationUPP,
							NULL, &m_PSN, 0);

	if (m_hiddenWindow) {
		ReleaseWindow(m_hiddenWindow);
		m_hiddenWindow = NULL;
	}

	if (m_userInputWindow) {
		ReleaseWindow(m_userInputWindow);
		m_userInputWindow = NULL;
	}
#endif

	delete m_keyState;
	delete m_screensaver;
	
#if defined(MAC_OS_X_VERSION_10_7)
	delete m_carbonLoopMutex;
	delete m_carbonLoopReady;
#endif
}

void*
COSXScreen::getEventTarget() const
{
	return const_cast<COSXScreen*>(this);
}

bool
COSXScreen::getClipboard(ClipboardID, IClipboard* dst) const
{
	CClipboard::copy(dst, &m_pasteboard);
	return true;
}

void
COSXScreen::getShape(SInt32& x, SInt32& y, SInt32& w, SInt32& h) const
{
	x = m_x;
	y = m_y;
	w = m_w;
	h = m_h;
}

void
COSXScreen::getCursorPos(SInt32& x, SInt32& y) const
{
	Point mouse;
	GetGlobalMouse(&mouse);
	x                = mouse.h;
	y                = mouse.v;
	m_cursorPosValid = true;
	m_xCursor        = x;
	m_yCursor        = y;
}

void
COSXScreen::reconfigure(UInt32)
{
	// do nothing
}

void
COSXScreen::warpCursor(SInt32 x, SInt32 y)
{
	// move cursor without generating events
	CGPoint pos;
	pos.x = x;
	pos.y = y;
	CGWarpMouseCursorPosition(pos);
	
	// save new cursor position
	m_xCursor        = x;
	m_yCursor        = y;
	m_cursorPosValid = true;
}

void
COSXScreen::fakeInputBegin()
{
	// FIXME -- not implemented
}

void
COSXScreen::fakeInputEnd()
{
	// FIXME -- not implemented
}

SInt32
COSXScreen::getJumpZoneSize() const
{
	return 1;
}

bool
COSXScreen::isAnyMouseButtonDown(UInt32& buttonID) const
{
	if (m_buttonState.test(0)) {
		buttonID = kButtonLeft;
		return true;
	}

	return (GetCurrentButtonState() != 0);
}

void
COSXScreen::getCursorCenter(SInt32& x, SInt32& y) const
{
	x = m_xCenter;
	y = m_yCenter;
}

UInt32
COSXScreen::registerHotKey(KeyID key, KeyModifierMask mask)
{
	// get mac virtual key and modifier mask matching synergy key and mask
	UInt32 macKey, macMask;
	if (!m_keyState->mapSynergyHotKeyToMac(key, mask, macKey, macMask)) {
		LOG((CLOG_DEBUG "could not map hotkey id=%04x mask=%04x", key, mask));
		return 0;
	}
	
	// choose hotkey id
	UInt32 id;
	if (!m_oldHotKeyIDs.empty()) {
		id = m_oldHotKeyIDs.back();
		m_oldHotKeyIDs.pop_back();
	}
	else {
		id = m_hotKeys.size() + 1;
	}

	// if this hot key has modifiers only then we'll handle it specially
	EventHotKeyRef ref = NULL;
	bool okay;
	if (key == kKeyNone) {
		if (m_modifierHotKeys.count(mask) > 0) {
			// already registered
			okay = false;
		}
		else {
			m_modifierHotKeys[mask] = id;
			okay = true;
		}
	}
	else {
		EventHotKeyID hkid = { 'SNRG', (UInt32)id };
		OSStatus status = RegisterEventHotKey(macKey, macMask, hkid, 
								GetApplicationEventTarget(), 0,
								&ref);
		okay = (status == noErr);
		m_hotKeyToIDMap[CHotKeyItem(macKey, macMask)] = id;
	}

	if (!okay) {
		m_oldHotKeyIDs.push_back(id);
		m_hotKeyToIDMap.erase(CHotKeyItem(macKey, macMask));
		LOG((CLOG_WARN "failed to register hotkey %s (id=%04x mask=%04x)", CKeyMap::formatKey(key, mask).c_str(), key, mask));
		return 0;
	}

	m_hotKeys.insert(std::make_pair(id, CHotKeyItem(ref, macKey, macMask)));
	
	LOG((CLOG_DEBUG "registered hotkey %s (id=%04x mask=%04x) as id=%d", CKeyMap::formatKey(key, mask).c_str(), key, mask, id));
	return id;
}

void
COSXScreen::unregisterHotKey(UInt32 id)
{
	// look up hotkey
	HotKeyMap::iterator i = m_hotKeys.find(id);
	if (i == m_hotKeys.end()) {
		return;
	}

	// unregister with OS
	bool okay;
	if (i->second.getRef() != NULL) {
		okay = (UnregisterEventHotKey(i->second.getRef()) == noErr);
	}
	else {
		okay = false;
		// XXX -- this is inefficient
		for (ModifierHotKeyMap::iterator j = m_modifierHotKeys.begin();
								j != m_modifierHotKeys.end(); ++j) {
			if (j->second == id) {
				m_modifierHotKeys.erase(j);
				okay = true;
				break;
			}
		}
	}
	if (!okay) {
		LOG((CLOG_WARN "failed to unregister hotkey id=%d", id));
	}
	else {
		LOG((CLOG_DEBUG "unregistered hotkey id=%d", id));
	}

	// discard hot key from map and record old id for reuse
	m_hotKeyToIDMap.erase(i->second);
	m_hotKeys.erase(i);
	m_oldHotKeyIDs.push_back(id);
	if (m_activeModifierHotKey == id) {
		m_activeModifierHotKey     = 0;
		m_activeModifierHotKeyMask = 0;
	}
}

void
COSXScreen::constructMouseButtonEventMap()
{
	const CGEventType source[NumButtonIDs][3] = {
		{kCGEventLeftMouseUp, kCGEventLeftMouseDragged, kCGEventLeftMouseDown},
		{kCGEventOtherMouseUp, kCGEventOtherMouseDragged, kCGEventOtherMouseDown},
		{kCGEventRightMouseUp, kCGEventRightMouseDragged, kCGEventRightMouseDown},
		{kCGEventOtherMouseUp, kCGEventOtherMouseDragged, kCGEventOtherMouseDown},
		{kCGEventOtherMouseUp, kCGEventOtherMouseDragged, kCGEventOtherMouseDown}
	};

	for (UInt16 button = 0; button < NumButtonIDs; button++) {
		MouseButtonEventMapType new_map;
		for (UInt16 state = (UInt32) kMouseButtonUp; state < kMouseButtonStateMax; state++) {
			CGEventType curEvent = source[button][state];
			new_map[state] = curEvent;
		}
		MouseButtonEventMap[button] = new_map;
	}
}

void
COSXScreen::postMouseEvent(CGPoint& pos) const
{
	// check if cursor position is valid on the client display configuration
	// stkamp@users.sourceforge.net
	CGDisplayCount displayCount = 0;
	CGGetDisplaysWithPoint(pos, 0, NULL, &displayCount);
	if (displayCount == 0) {
		// cursor position invalid - clamp to bounds of last valid display.
		// find the last valid display using the last cursor position.
		displayCount = 0;
		CGDirectDisplayID displayID;
		CGGetDisplaysWithPoint(CGPointMake(m_xCursor, m_yCursor), 1,
								&displayID, &displayCount);
		if (displayCount != 0) {
			CGRect displayRect = CGDisplayBounds(displayID);
			if (pos.x < displayRect.origin.x) {
				pos.x = displayRect.origin.x;
			}
			else if (pos.x > displayRect.origin.x +
								displayRect.size.width - 1) {
				pos.x = displayRect.origin.x + displayRect.size.width - 1;
			}
			if (pos.y < displayRect.origin.y) {
				pos.y = displayRect.origin.y;
			}
			else if (pos.y > displayRect.origin.y +
								displayRect.size.height - 1) {
				pos.y = displayRect.origin.y + displayRect.size.height - 1;
			}
		}
	}
	
	CGEventType type = kCGEventMouseMoved;

	SInt8 button = m_buttonState.getFirstButtonDown();
	if (button != -1) {
		MouseButtonEventMapType thisButtonType = MouseButtonEventMap[button];
		type = thisButtonType[kMouseButtonDragged];
	}

	CGEventRef event = CGEventCreateMouseEvent(NULL, type, pos, button);
	CGEventPost(kCGHIDEventTap, event);
	
	CFRelease(event);
}

void
COSXScreen::fakeMouseButton(ButtonID id, bool press)
{
	NXEventHandle handle = NXOpenEventStatus();
	double clickTime = NXClickTime(handle);
	
	if ((ARCH->time() - m_lastDoubleClick) <= clickTime) {
		// drop all down and up fakes immedately after a double click.
		// TODO: perhaps there is a better way to do this, usually in
		// finder, if you tripple click a folder, it will open it and
		// then select a folder under the cursor -- and perhaps other
		// strange behaviour might happen?
		LOG((CLOG_DEBUG1 "dropping mouse button %s",
			press ? "press" : "release"));
		return;
	}
	
	// Buttons are indexed from one, but the button down array is indexed from zero
	UInt32 index = id - kButtonLeft;
	if (index >= NumButtonIDs) {
		return;
	}
	
	CGPoint pos;
	if (!m_cursorPosValid) {
		SInt32 x, y;
		getCursorPos(x, y);
	}
	pos.x = m_xCursor;
	pos.y = m_yCursor;

	// variable used to detect mouse coordinate differences between
	// old & new mouse clicks. Used in double click detection.
	SInt32 xDiff = m_xCursor - m_lastSingleClickXCursor;
	SInt32 yDiff = m_yCursor - m_lastSingleClickYCursor;
	double diff = sqrt(xDiff * xDiff + yDiff * yDiff);
	// max sqrt(x^2 + y^2) difference allowed to double click
	// since we don't have double click distance in NX APIs
	// we define our own defaults.
	const double maxDiff = sqrt(2) + 0.0001;

	if (press && (id == kButtonLeft) &&
		((ARCH->time() - m_lastSingleClick) <= clickTime) &&
		diff <= maxDiff) {

		LOG((CLOG_DEBUG1 "faking mouse left double click"));
		
		// finder does not seem to detect double clicks from two separate
		// CGEventCreateMouseEvent calls. so, if we detect a double click we
		// use CGEventSetIntegerValueField to tell the OS.
		// 
		// the caveat here is that findor will see this as a single click 
		// followed by a double click (even though there should be only a
		// double click). this may cause weird behaviour in other apps.
		//
		// for some reason using the old CGPostMouseEvent function, doesn't
		// cause double clicks (though i'm sure it did work at some point).
		
		CGEventRef event = CGEventCreateMouseEvent(
			NULL, kCGEventLeftMouseDown, pos, kCGMouseButtonLeft);
		
		CGEventSetIntegerValueField(event, kCGMouseEventClickState, 2);
		m_buttonState.set(index, kMouseButtonDown);
		CGEventPost(kCGHIDEventTap, event);
		
		CGEventSetType(event, kCGEventLeftMouseUp);
		m_buttonState.set(index, kMouseButtonUp);
		CGEventPost(kCGHIDEventTap, event);
		
		CFRelease(event);
	
		m_lastDoubleClick = ARCH->time();
	}
	else {
		
		// ... otherwise, perform a single press or release as normal.
		
		MouseButtonState state = press ? kMouseButtonDown : kMouseButtonUp;
		
		LOG((CLOG_DEBUG1 "faking mouse button id: %d press: %s", id, press ? "pressed" : "released"));
		
		MouseButtonEventMapType thisButtonMap = MouseButtonEventMap[index];
		CGEventType type = thisButtonMap[state];
		
		CGEventRef event = CGEventCreateMouseEvent(NULL, type, pos, index);
	
		m_buttonState.set(index, state);
		CGEventPost(kCGHIDEventTap, event);
		
		CFRelease(event);
	
		m_lastSingleClick = ARCH->time();
		m_lastSingleClickXCursor = m_xCursor;
		m_lastSingleClickYCursor = m_yCursor;
	}
	if (!press && (id == kButtonLeft)) {
		if (m_fakeDraggingStarted) {
			m_getDropTargetThread = new CThread(new TMethodJob<COSXScreen>(
				this, &COSXScreen::getDropTargetThread));
		}
		
		m_draggingStarted = false;
	}
}

void
COSXScreen::getDropTargetThread(void*)
{
#if defined(MAC_OS_X_VERSION_10_7)
	char* cstr = NULL;
	
	// wait for 5 secs for the drop destinaiton string to be filled.
	UInt32 timeout = ARCH->time() + 5;
	
	while (ARCH->time() < timeout) {
		CFStringRef cfstr = getCocoaDropTarget();
		cstr = CFStringRefToUTF8String(cfstr);
		CFRelease(cfstr);
		
		if (cstr != NULL) {
			break;
		}
		ARCH->sleep(.1f);
	}
	
	if (cstr != NULL) {
		LOG((CLOG_DEBUG "drop target: %s", cstr));
		m_dropTarget = cstr;
	}
	else {
		LOG((CLOG_ERR "failed to get drop target"));
		m_dropTarget.clear();
	}
#else
	LOG((CLOG_WARN "drag drop not supported"));
#endif
	m_fakeDraggingStarted = false;
}

void
COSXScreen::fakeMouseMove(SInt32 x, SInt32 y)
{
	if (m_fakeDraggingStarted) {
		m_buttonState.set(0, kMouseButtonDown);
	}
	
	// index 0 means left mouse button
	if (m_buttonState.test(0)) {
		m_draggingStarted = true;
	}
	
	// synthesize event
	CGPoint pos;
	pos.x = x;
	pos.y = y;
	postMouseEvent(pos);

	// save new cursor position
	m_xCursor        = static_cast<SInt32>(pos.x);
	m_yCursor        = static_cast<SInt32>(pos.y);
	m_cursorPosValid = true;
}

void
COSXScreen::fakeMouseRelativeMove(SInt32 dx, SInt32 dy) const
{
	// OS X does not appear to have a fake relative mouse move function.
	// simulate it by getting the current mouse position and adding to
	// that.  this can yield the wrong answer but there's not much else
	// we can do.

	// get current position
	Point oldPos;
	GetGlobalMouse(&oldPos);

	// synthesize event
	CGPoint pos;
	m_xCursor = static_cast<SInt32>(oldPos.h);
	m_yCursor = static_cast<SInt32>(oldPos.v);
	pos.x     = oldPos.h + dx;
	pos.y     = oldPos.v + dy;
	postMouseEvent(pos);

	// we now assume we don't know the current cursor position
	m_cursorPosValid = false;
}

void
COSXScreen::fakeMouseWheel(SInt32 xDelta, SInt32 yDelta) const
{
	if (xDelta != 0 || yDelta != 0) {
#if defined(MAC_OS_X_VERSION_10_5)
		// create a scroll event, post it and release it.  not sure if kCGScrollEventUnitLine
		// is the right choice here over kCGScrollEventUnitPixel
		CGEventRef scrollEvent = CGEventCreateScrollWheelEvent(
			NULL, kCGScrollEventUnitLine, 2,
			mapScrollWheelFromSynergy(yDelta),
			-mapScrollWheelFromSynergy(xDelta));
		
		CGEventPost(kCGHIDEventTap, scrollEvent);
		CFRelease(scrollEvent);
#else

		CGPostScrollWheelEvent(
			2, mapScrollWheelFromSynergy(yDelta),
			-mapScrollWheelFromSynergy(xDelta));
#endif
	}
}

void
COSXScreen::showCursor()
{
	LOG((CLOG_DEBUG "showing cursor"));

	CFStringRef propertyString = CFStringCreateWithCString(
		NULL, "SetsCursorInBackground", kCFStringEncodingMacRoman);

	CGSSetConnectionProperty(
		_CGSDefaultConnection(), _CGSDefaultConnection(),
		propertyString, kCFBooleanTrue);

	CFRelease(propertyString);

	CGError error = CGDisplayShowCursor(m_displayID);
	if (error != kCGErrorSuccess) {
		LOG((CLOG_ERR "failed to show cursor, error=%d", error));
	}

	// appears to fix "mouse randomly not showing" bug
	CGAssociateMouseAndMouseCursorPosition(true);

	logCursorVisibility();

	m_cursorHidden = false;
}

void
COSXScreen::hideCursor()
{
	LOG((CLOG_DEBUG "hiding cursor"));

	CFStringRef propertyString = CFStringCreateWithCString(
		NULL, "SetsCursorInBackground", kCFStringEncodingMacRoman);

	CGSSetConnectionProperty(
		_CGSDefaultConnection(), _CGSDefaultConnection(),
		propertyString, kCFBooleanTrue);

	CFRelease(propertyString);

	CGError error = CGDisplayHideCursor(m_displayID);
	if (error != kCGErrorSuccess) {
		LOG((CLOG_ERR "failed to hide cursor, error=%d", error));
	}

	// appears to fix "mouse randomly not hiding" bug
	CGAssociateMouseAndMouseCursorPosition(true);

	logCursorVisibility();

	m_cursorHidden = true;
}

void
COSXScreen::enable()
{
	// watch the clipboard
	m_clipboardTimer = m_events->newTimer(1.0, NULL);
	m_events->adoptHandler(CEvent::kTimer, m_clipboardTimer,
							new TMethodEventJob<COSXScreen>(this,
								&COSXScreen::handleClipboardCheck));

	if (m_isPrimary) {
		// FIXME -- start watching jump zones
		
		// kCGEventTapOptionDefault = 0x00000000 (Missing in 10.4, so specified literally)
		m_eventTapPort = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap, 0,
										kCGEventMaskForAllEvents, 
										handleCGInputEvent, 
										this);
	}
	else {
		// FIXME -- prevent system from entering power save mode

		if (m_autoShowHideCursor) {
			hideCursor();
		}

		// warp the mouse to the cursor center
		fakeMouseMove(m_xCenter, m_yCenter);

                // there may be a better way to do this, but we register an event handler even if we're
                // not on the primary display (acting as a client). This way, if a local event comes in
                // (either keyboard or mouse), we can make sure to show the cursor if we've hidden it. 
		m_eventTapPort = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap, 0,
										kCGEventMaskForAllEvents, 
										handleCGInputEventSecondary, 
										this);
	}

	if (!m_eventTapPort) {
		LOG((CLOG_ERR "failed to create quartz event tap"));
	}

	m_eventTapRLSR = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_eventTapPort, 0);
	if (!m_eventTapRLSR) {
		LOG((CLOG_ERR "failed to create a CFRunLoopSourceRef for the quartz event tap"));
	}

	CFRunLoopAddSource(CFRunLoopGetCurrent(), m_eventTapRLSR, kCFRunLoopDefaultMode);
}

void
COSXScreen::disable()
{
	if (m_autoShowHideCursor) {
		showCursor();
	}
    
	// FIXME -- stop watching jump zones, stop capturing input
	
	if (m_eventTapRLSR) {
		CFRelease(m_eventTapRLSR);
		m_eventTapRLSR = nullptr;
	}

	if (m_eventTapPort) {
		CFRelease(m_eventTapPort);
		m_eventTapPort = nullptr;
	}
	// FIXME -- allow system to enter power saving mode

	// disable drag handling
	m_dragNumButtonsDown = 0;
	enableDragTimer(false);

	// uninstall clipboard timer
	if (m_clipboardTimer != NULL) {
		m_events->removeHandler(CEvent::kTimer, m_clipboardTimer);
		m_events->deleteTimer(m_clipboardTimer);
		m_clipboardTimer = NULL;
	}

	m_isOnScreen = m_isPrimary;
}

void
COSXScreen::enter()
{
	showCursor();

	if (m_isPrimary) {
		setZeroSuppressionInterval();
	}
	else {
		// reset buttons
		m_buttonState.reset();

		// patch by Yutaka Tsutano
		// wakes the client screen
		// http://synergy-project.org/spit/issues/details/3287#c12
		io_registry_entry_t entry = IORegistryEntryFromPath(
			kIOMasterPortDefault,
			"IOService:/IOResources/IODisplayWrangler");
		
		if (entry != MACH_PORT_NULL) {
			IORegistryEntrySetCFProperty(entry, CFSTR("IORequestIdle"), kCFBooleanFalse);
			IOObjectRelease(entry);
		}

		avoidSupression();
	}

	// now on screen
	m_isOnScreen = true;
}

bool
COSXScreen::leave()
{
    hideCursor();
    
	if (isDraggingStarted()) {
		CString& fileList = getDraggingFilename();
		
		if (!m_isPrimary) {
			if (fileList.empty() == false) {
				CClientApp& app = CClientApp::instance();
				CClient* client = app.getClientPtr();
				
				CDragInformation di;
				di.setFilename(fileList);
				CDragFileList dragFileList;
				dragFileList.push_back(di);
				CString info;
				UInt32 fileCount = CDragInformation::setupDragInfo(
					dragFileList, info);
				client->sendDragInfo(fileCount, info, info.size());
				LOG((CLOG_DEBUG "send dragging file to server"));
				
				// TODO: what to do with multiple file or even
				// a folder
				client->sendFileToServer(fileList.c_str());
			}
		}
		m_draggingStarted = false;
	}
	
	if (m_isPrimary) {
		avoidHesitatingCursor();

	}

	// now off screen
	m_isOnScreen = false;

	return true;
}

bool
COSXScreen::setClipboard(ClipboardID, const IClipboard* src)
{
	if(src != NULL) {
		LOG((CLOG_DEBUG "setting clipboard"));
		CClipboard::copy(&m_pasteboard, src);	
	}	
	return true;
}

void
COSXScreen::checkClipboards()
{
	LOG((CLOG_DEBUG2 "checking clipboard"));
	if (m_pasteboard.synchronize()) {
		LOG((CLOG_DEBUG "clipboard changed"));
		sendClipboardEvent(m_events->forIScreen().clipboardGrabbed(), kClipboardClipboard);
		sendClipboardEvent(m_events->forIScreen().clipboardGrabbed(), kClipboardSelection);
	}
}

void
COSXScreen::openScreensaver(bool notify)
{
	m_screensaverNotify = notify;
	if (!m_screensaverNotify) {
		m_screensaver->disable();
	}
}

void
COSXScreen::closeScreensaver()
{
	if (!m_screensaverNotify) {
		m_screensaver->enable();
	}
}

void
COSXScreen::screensaver(bool activate)
{
	if (activate) {
		m_screensaver->activate();
	}
	else {
		m_screensaver->deactivate();
	}
}

void
COSXScreen::resetOptions()
{
	// no options
}

void
COSXScreen::setOptions(const COptionsList&)
{
	// no options
}

void
COSXScreen::setSequenceNumber(UInt32 seqNum)
{
	m_sequenceNumber = seqNum;
}

bool
COSXScreen::isPrimary() const
{
	return m_isPrimary;
}

void
COSXScreen::sendEvent(CEvent::Type type, void* data) const
{
	m_events->addEvent(CEvent(type, getEventTarget(), data));
}

void
COSXScreen::sendClipboardEvent(CEvent::Type type, ClipboardID id) const
{
	CClipboardInfo* info   = (CClipboardInfo*)malloc(sizeof(CClipboardInfo));
	info->m_id             = id;
	info->m_sequenceNumber = m_sequenceNumber;
	sendEvent(type, info);
}

void
COSXScreen::handleSystemEvent(const CEvent& event, void*)
{
	EventRef* carbonEvent = reinterpret_cast<EventRef*>(event.getData());
	assert(carbonEvent != NULL);

	UInt32 eventClass = GetEventClass(*carbonEvent);

	switch (eventClass) {
	case kEventClassMouse:
		switch (GetEventKind(*carbonEvent)) {
		case kSynergyEventMouseScroll:
		{
			OSStatus r;
			long xScroll;
			long yScroll;

			// get scroll amount
			r = GetEventParameter(*carbonEvent,
					kSynergyMouseScrollAxisX,
					typeLongInteger,
					NULL,
					sizeof(xScroll),
					NULL,
					&xScroll);
			if (r != noErr) {
				xScroll = 0;
			}
			r = GetEventParameter(*carbonEvent,
					kSynergyMouseScrollAxisY,
					typeLongInteger,
					NULL,
					sizeof(yScroll),
					NULL,
					&yScroll);
			if (r != noErr) {
				yScroll = 0;
			}

			if (xScroll != 0 || yScroll != 0) {
				onMouseWheel(-mapScrollWheelToSynergy(xScroll),
								mapScrollWheelToSynergy(yScroll));
			}
		}
		}
		break;

	case kEventClassKeyboard: 
			switch (GetEventKind(*carbonEvent)) {
				case kEventHotKeyPressed:
				case kEventHotKeyReleased:
					onHotKey(*carbonEvent);
					break;
			}
			
			break;
			
	case kEventClassWindow:
		SendEventToWindow(*carbonEvent, m_userInputWindow);
		switch (GetEventKind(*carbonEvent)) {
		case kEventWindowActivated:
			LOG((CLOG_DEBUG1 "window activated"));
			break;

		case kEventWindowDeactivated:
			LOG((CLOG_DEBUG1 "window deactivated"));
			break;

		case kEventWindowFocusAcquired:
			LOG((CLOG_DEBUG1 "focus acquired"));
			break;

		case kEventWindowFocusRelinquish:
			LOG((CLOG_DEBUG1 "focus released"));
			break;
		}
		break;

	default:
		SendEventToEventTarget(*carbonEvent, GetEventDispatcherTarget());
		break;
	}
}

bool 
COSXScreen::onMouseMove(SInt32 mx, SInt32 my)
{
	LOG((CLOG_DEBUG2 "mouse move %+d,%+d", mx, my));

	SInt32 x = mx - m_xCursor;
	SInt32 y = my - m_yCursor;

	if ((x == 0 && y == 0) || (mx == m_xCenter && mx == m_yCenter)) {
		return true;
	}

	// save position to compute delta of next motion
	m_xCursor = mx;
	m_yCursor = my;

	if (m_isOnScreen) {
		// motion on primary screen
		sendEvent(m_events->forIPrimaryScreen().motionOnPrimary(),
							CMotionInfo::alloc(m_xCursor, m_yCursor));
		if (m_buttonState.test(0)) {
			m_draggingStarted = true;
		}
	}
	else {
		// motion on secondary screen.  warp mouse back to
		// center.
		warpCursor(m_xCenter, m_yCenter);

		// examine the motion.  if it's about the distance
		// from the center of the screen to an edge then
		// it's probably a bogus motion that we want to
		// ignore (see warpCursorNoFlush() for a further
		// description).
		static SInt32 bogusZoneSize = 10;
		if (-x + bogusZoneSize > m_xCenter - m_x ||
			 x + bogusZoneSize > m_x + m_w - m_xCenter ||
			-y + bogusZoneSize > m_yCenter - m_y ||
			 y + bogusZoneSize > m_y + m_h - m_yCenter) {
			LOG((CLOG_DEBUG "dropped bogus motion %+d,%+d", x, y));
		}
		else {
			// send motion
			sendEvent(m_events->forIPrimaryScreen().motionOnSecondary(), CMotionInfo::alloc(x, y));
		}
	}

	return true;
}

bool				
COSXScreen::onMouseButton(bool pressed, UInt16 macButton)
{
	// Buttons 2 and 3 are inverted on the mac
	ButtonID button = mapMacButtonToSynergy(macButton);

	if (pressed) {
		LOG((CLOG_DEBUG1 "event: button press button=%d", button));
		if (button != kButtonNone) {
			KeyModifierMask mask = m_keyState->getActiveModifiers();
			sendEvent(m_events->forIPrimaryScreen().buttonDown(), CButtonInfo::alloc(button, mask));
		}
	}
	else {
		LOG((CLOG_DEBUG1 "event: button release button=%d", button));
		if (button != kButtonNone) {
			KeyModifierMask mask = m_keyState->getActiveModifiers();
			sendEvent(m_events->forIPrimaryScreen().buttonUp(), CButtonInfo::alloc(button, mask));
		}
	}

	// handle drags with any button other than button 1 or 2
	if (macButton > 2) {
		if (pressed) {
			// one more button
			if (m_dragNumButtonsDown++ == 0) {
				enableDragTimer(true);
			}
		}
		else {
			// one less button
			if (--m_dragNumButtonsDown == 0) {
				enableDragTimer(false);
			}
		}
	}
	
	if (macButton == kButtonLeft) {
		MouseButtonState state = pressed ? kMouseButtonDown : kMouseButtonUp;
		m_buttonState.set(kButtonLeft - 1, state);
		if (pressed) {
			m_draggingFilename.clear();
			LOG((CLOG_DEBUG2 "dragging file directory is cleared"));
		}
		else {
			if (m_fakeDraggingStarted) {
				m_getDropTargetThread = new CThread(new TMethodJob<COSXScreen>(
																			   this, &COSXScreen::getDropTargetThread));
			}
			
			m_draggingStarted = false;
		}
	}

	return true;
}

bool
COSXScreen::onMouseWheel(SInt32 xDelta, SInt32 yDelta) const
{
	LOG((CLOG_DEBUG1 "event: button wheel delta=%+d,%+d", xDelta, yDelta));
	sendEvent(m_events->forIPrimaryScreen().wheel(), CWheelInfo::alloc(xDelta, yDelta));
	return true;
}

void
COSXScreen::handleClipboardCheck(const CEvent&, void*)
{
	checkClipboards();
}

#if !defined(MAC_OS_X_VERSION_10_5)
pascal void 
COSXScreen::displayManagerCallback(void* inUserData, SInt16 inMessage, void*)
{
	COSXScreen* screen = (COSXScreen*)inUserData;

	if (inMessage == kDMNotifyEvent) {
		screen->onDisplayChange();
	}
}

bool
COSXScreen::onDisplayChange()
{
	// screen resolution may have changed.  save old shape.
	SInt32 xOld = m_x, yOld = m_y, wOld = m_w, hOld = m_h;

	// update shape
	updateScreenShape();

	// do nothing if resolution hasn't changed
	if (xOld != m_x || yOld != m_y || wOld != m_w || hOld != m_h) {
		if (m_isPrimary) {
			// warp mouse to center if off screen
			if (!m_isOnScreen) {
				warpCursor(m_xCenter, m_yCenter);
			}
		}

		// send new screen info
		sendEvent(m_events->forIPrimaryScreen().shapeChanged());
	}

	return true;
}
#else
void 
COSXScreen::displayReconfigurationCallback(CGDirectDisplayID displayID, CGDisplayChangeSummaryFlags flags, void* inUserData)
{
	COSXScreen* screen = (COSXScreen*)inUserData;

	// Closing or opening the lid when an external monitor is
    // connected causes an kCGDisplayBeginConfigurationFlag event
	CGDisplayChangeSummaryFlags mask = kCGDisplayBeginConfigurationFlag | kCGDisplayMovedFlag | 
		kCGDisplaySetModeFlag | kCGDisplayAddFlag | kCGDisplayRemoveFlag | 
		kCGDisplayEnabledFlag | kCGDisplayDisabledFlag | 
		kCGDisplayMirrorFlag | kCGDisplayUnMirrorFlag | 
		kCGDisplayDesktopShapeChangedFlag;
 
	LOG((CLOG_DEBUG1 "event: display was reconfigured: %x %x %x", flags, mask, flags & mask));

	if (flags & mask) { /* Something actually did change */
		
		LOG((CLOG_DEBUG1 "event: screen changed shape; refreshing dimensions"));
		screen->updateScreenShape(displayID, flags);
	}
}
#endif

bool
COSXScreen::onKey(CGEventRef event)
{
	CGEventType eventKind = CGEventGetType(event);

	// get the key and active modifiers
	UInt32 virtualKey = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
	CGEventFlags macMask = CGEventGetFlags(event);
	LOG((CLOG_DEBUG1 "event: Key event kind: %d, keycode=%d", eventKind, virtualKey));

	// Special handling to track state of modifiers
	if (eventKind == kCGEventFlagsChanged) {
		// get old and new modifier state
		KeyModifierMask oldMask = getActiveModifiers();
		KeyModifierMask newMask = m_keyState->mapModifiersFromOSX(macMask);
		m_keyState->handleModifierKeys(getEventTarget(), oldMask, newMask);

		// if the current set of modifiers exactly matches a modifiers-only
		// hot key then generate a hot key down event.
		if (m_activeModifierHotKey == 0) {
			if (m_modifierHotKeys.count(newMask) > 0) {
				m_activeModifierHotKey     = m_modifierHotKeys[newMask];
				m_activeModifierHotKeyMask = newMask;
				m_events->addEvent(CEvent(m_events->forIPrimaryScreen().hotKeyDown(),
								getEventTarget(),
								CHotKeyInfo::alloc(m_activeModifierHotKey)));
			}
		}

		// if a modifiers-only hot key is active and should no longer be
		// then generate a hot key up event.
		else if (m_activeModifierHotKey != 0) {
			KeyModifierMask mask = (newMask & m_activeModifierHotKeyMask);
			if (mask != m_activeModifierHotKeyMask) {
				m_events->addEvent(CEvent(m_events->forIPrimaryScreen().hotKeyUp(),
								getEventTarget(),
								CHotKeyInfo::alloc(m_activeModifierHotKey)));
				m_activeModifierHotKey     = 0;
				m_activeModifierHotKeyMask = 0;
			}
		}
			
		return true;
	}

	// check for hot key.  when we're on a secondary screen we disable
	// all hotkeys so we can capture the OS defined hot keys as regular
	// keystrokes but that means we don't get our own hot keys either.
	// so we check for a key/modifier match in our hot key map.
	if (!m_isOnScreen) {
		HotKeyToIDMap::const_iterator i =
			m_hotKeyToIDMap.find(CHotKeyItem(virtualKey, 
											 m_keyState->mapModifiersToCarbon(macMask) 
											 & 0xff00u));
		if (i != m_hotKeyToIDMap.end()) {
			UInt32 id = i->second;
	
			// determine event type
			CEvent::Type type;
			//UInt32 eventKind = GetEventKind(event);
			if (eventKind == kCGEventKeyDown) {
				type = m_events->forIPrimaryScreen().hotKeyDown();
			}
			else if (eventKind == kCGEventKeyUp) {
				type = m_events->forIPrimaryScreen().hotKeyUp();
			}
			else {
				return false;
			}
	
			m_events->addEvent(CEvent(type, getEventTarget(),
										CHotKeyInfo::alloc(id)));
		
			return true;
		}
	}

	// decode event type
	bool down	  = (eventKind == kCGEventKeyDown);
	bool up		  = (eventKind == kCGEventKeyUp);
	bool isRepeat = (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) == 1);

	// map event to keys
	KeyModifierMask mask;
	COSXKeyState::CKeyIDs keys;
	KeyButton button = m_keyState->mapKeyFromEvent(keys, &mask, event);
	if (button == 0) {
		return false;
	}

	// check for AltGr in mask.  if set we send neither the AltGr nor
	// the super modifiers to clients then remove AltGr before passing
	// the modifiers to onKey.
	KeyModifierMask sendMask = (mask & ~KeyModifierAltGr);
	if ((mask & KeyModifierAltGr) != 0) {
		sendMask &= ~KeyModifierSuper;
	}
	mask &= ~KeyModifierAltGr;

	// update button state
	if (down) {
		m_keyState->onKey(button, true, mask);
	}
	else if (up) {
		if (!m_keyState->isKeyDown(button)) {
			// up event for a dead key.  throw it away.
			return false;
		}
		m_keyState->onKey(button, false, mask);
	}

	// send key events
	for (COSXKeyState::CKeyIDs::const_iterator i = keys.begin();
							i != keys.end(); ++i) {
		m_keyState->sendKeyEvent(getEventTarget(), down, isRepeat,
							*i, sendMask, 1, button);
	}

	return true;
}

bool
COSXScreen::onHotKey(EventRef event) const
{
	// get the hotkey id
	EventHotKeyID hkid;
	GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
							NULL, sizeof(EventHotKeyID), NULL, &hkid);
	UInt32 id = hkid.id;

	// determine event type
	CEvent::Type type;
	UInt32 eventKind = GetEventKind(event);
	if (eventKind == kEventHotKeyPressed) {
		type = m_events->forIPrimaryScreen().hotKeyDown();
	}
	else if (eventKind == kEventHotKeyReleased) {
		type = m_events->forIPrimaryScreen().hotKeyUp();
	}
	else {
		return false;
	}

	m_events->addEvent(CEvent(type, getEventTarget(),
								CHotKeyInfo::alloc(id)));

	return true;
}

ButtonID 
COSXScreen::mapMacButtonToSynergy(UInt16 macButton) const
{
	switch (macButton) {
	case 1:
		return kButtonLeft;

	case 2:
		return kButtonRight;

	case 3:
		return kButtonMiddle;
	}
	
	return static_cast<ButtonID>(macButton);
}

SInt32
COSXScreen::mapScrollWheelToSynergy(SInt32 x) const
{
	// return accelerated scrolling but not exponentially scaled as it is
	// on the mac.
	double d = (1.0 + getScrollSpeed()) * x / getScrollSpeedFactor();
	return static_cast<SInt32>(120.0 * d);
}

SInt32
COSXScreen::mapScrollWheelFromSynergy(SInt32 x) const
{
	// use server's acceleration with a little boost since other platforms
	// take one wheel step as a larger step than the mac does.
	return static_cast<SInt32>(3.0 * x / 120.0);
}

double
COSXScreen::getScrollSpeed() const
{
	double scaling = 0.0;

	CFPropertyListRef pref = ::CFPreferencesCopyValue(
							CFSTR("com.apple.scrollwheel.scaling") , 
							kCFPreferencesAnyApplication, 
							kCFPreferencesCurrentUser,
							kCFPreferencesAnyHost);
	if (pref != NULL) {
		CFTypeID id = CFGetTypeID(pref);
		if (id == CFNumberGetTypeID()) {
			CFNumberRef value = static_cast<CFNumberRef>(pref);
			if (CFNumberGetValue(value, kCFNumberDoubleType, &scaling)) {
				if (scaling < 0.0) {
					scaling = 0.0;
				}
			}
		}
		CFRelease(pref);
	}

	return scaling;
}

double
COSXScreen::getScrollSpeedFactor() const
{
	return pow(10.0, getScrollSpeed());
}

void
COSXScreen::enableDragTimer(bool enable)
{
	UInt32 modifiers;
	MouseTrackingResult res;

	if (enable && m_dragTimer == NULL) {
		m_dragTimer = m_events->newTimer(0.01, NULL);
		m_events->adoptHandler(CEvent::kTimer, m_dragTimer,
							new TMethodEventJob<COSXScreen>(this,
								&COSXScreen::handleDrag));
		TrackMouseLocationWithOptions(NULL, 0, 0, &m_dragLastPoint, &modifiers, &res);
	}
	else if (!enable && m_dragTimer != NULL) {
		m_events->removeHandler(CEvent::kTimer, m_dragTimer);
		m_events->deleteTimer(m_dragTimer);
		m_dragTimer = NULL;
	}
}

void
COSXScreen::handleDrag(const CEvent&, void*)
{
	Point p;
	UInt32 modifiers;
	MouseTrackingResult res; 

	TrackMouseLocationWithOptions(NULL, 0, 0, &p, &modifiers, &res);

	if (res != kMouseTrackingTimedOut && (p.h != m_dragLastPoint.h || p.v != m_dragLastPoint.v)) {
		m_dragLastPoint = p;
		onMouseMove((SInt32)p.h, (SInt32)p.v);
	}
}

void
COSXScreen::updateButtons()
{
	UInt32 buttons = GetCurrentButtonState();

	m_buttonState.overwrite(buttons);
}

IKeyState*
COSXScreen::getKeyState() const
{
	return m_keyState;
}

void
COSXScreen::updateScreenShape(const CGDirectDisplayID, const CGDisplayChangeSummaryFlags flags)
{
	updateScreenShape();
}

void
COSXScreen::updateScreenShape()
{
	// get info for each display
	CGDisplayCount displayCount = 0;

	if (CGGetActiveDisplayList(0, NULL, &displayCount) != CGDisplayNoErr) {
		return;
	}
	
	if (displayCount == 0) {
		return;
	}

	CGDirectDisplayID* displays = new CGDirectDisplayID[displayCount];
	if (displays == NULL) {
		return;
	}

	if (CGGetActiveDisplayList(displayCount,
							displays, &displayCount) != CGDisplayNoErr) {
		delete[] displays;
		return;
	}

	// get smallest rect enclosing all display rects
	CGRect totalBounds = CGRectZero;
	for (CGDisplayCount i = 0; i < displayCount; ++i) {
		CGRect bounds = CGDisplayBounds(displays[i]);
		totalBounds   = CGRectUnion(totalBounds, bounds);
	}

	// get shape of default screen
	m_x = (SInt32)totalBounds.origin.x;
	m_y = (SInt32)totalBounds.origin.y;
	m_w = (SInt32)totalBounds.size.width;
	m_h = (SInt32)totalBounds.size.height;

	// get center of default screen
  CGDirectDisplayID main = CGMainDisplayID();
  const CGRect rect = CGDisplayBounds(main);
  m_xCenter = (rect.origin.x + rect.size.width) / 2;
  m_yCenter = (rect.origin.y + rect.size.height) / 2;

	delete[] displays;
	// We want to notify the peer screen whether we are primary screen or not
	sendEvent(m_events->forIScreen().shapeChanged());

	LOG((CLOG_DEBUG "screen shape: center=%d,%d size=%dx%d on %u %s",
         m_x, m_y, m_w, m_h, displayCount,
         (displayCount == 1) ? "display" : "displays"));
}

#pragma mark - 

//
// FAST USER SWITCH NOTIFICATION SUPPORT
//
// COSXScreen::userSwitchCallback(void*)
// 
// gets called if a fast user switch occurs
//

pascal OSStatus
COSXScreen::userSwitchCallback(EventHandlerCallRef nextHandler,
								EventRef theEvent,
								void* inUserData)
{
	COSXScreen* screen = (COSXScreen*)inUserData;
	UInt32 kind        = GetEventKind(theEvent);
	IEventQueue* events = screen->getEvents();

	if (kind == kEventSystemUserSessionDeactivated) {
		LOG((CLOG_DEBUG "user session deactivated"));
		events->addEvent(CEvent(events->forIScreen().suspend(),
									screen->getEventTarget()));
	}
	else if (kind == kEventSystemUserSessionActivated) {
		LOG((CLOG_DEBUG "user session activated"));
		events->addEvent(CEvent(events->forIScreen().resume(),
									screen->getEventTarget()));
	}
	return (CallNextEventHandler(nextHandler, theEvent));
}

#pragma mark - 

//
// SLEEP/WAKEUP NOTIFICATION SUPPORT
//
// COSXScreen::watchSystemPowerThread(void*)
// 
// main of thread monitoring system power (sleep/wakup) using a CFRunLoop
//

void
COSXScreen::watchSystemPowerThread(void*)
{
	io_object_t				notifier;
	IONotificationPortRef	notificationPortRef;
	CFRunLoopSourceRef		runloopSourceRef = 0;

	m_pmRunloop = CFRunLoopGetCurrent();
	// install system power change callback
	m_pmRootPort = IORegisterForSystemPower(this, &notificationPortRef,
											powerChangeCallback, &notifier);
	if (m_pmRootPort == 0) {
		LOG((CLOG_WARN "IORegisterForSystemPower failed"));
	}
	else {
		runloopSourceRef =
			IONotificationPortGetRunLoopSource(notificationPortRef);
		CFRunLoopAddSource(m_pmRunloop, runloopSourceRef,
								kCFRunLoopCommonModes);
	}
	
	// thread is ready
	{
		CLock lock(m_pmMutex);
		*m_pmThreadReady = true;
		m_pmThreadReady->signal();
	}

	// if we were unable to initialize then exit.  we must do this after
	// setting m_pmThreadReady to true otherwise the parent thread will
	// block waiting for it.
	if (m_pmRootPort == 0) {
		return;
	}

	LOG((CLOG_DEBUG "started watchSystemPowerThread"));

	m_events->waitForReady();
    
#if defined(MAC_OS_X_VERSION_10_7)
	if (*m_carbonLoopReady == false) {
		*m_carbonLoopReady = true;
		m_carbonLoopReady->broadcast();
	}
#endif
	
	// start the run loop
	LOG((CLOG_DEBUG "starting carbon loop"));
	CFRunLoopRun();
	
	// cleanup
	if (notificationPortRef) {
		CFRunLoopRemoveSource(m_pmRunloop,
								runloopSourceRef, kCFRunLoopDefaultMode);
		CFRunLoopSourceInvalidate(runloopSourceRef);
		CFRelease(runloopSourceRef);
	}

	CLock lock(m_pmMutex);
	IODeregisterForSystemPower(&notifier);
	m_pmRootPort = 0;
	LOG((CLOG_DEBUG "stopped watchSystemPowerThread"));
}

void
COSXScreen::powerChangeCallback(void* refcon, io_service_t service,
						  natural_t messageType, void* messageArg)
{
	((COSXScreen*)refcon)->handlePowerChangeRequest(messageType, messageArg);
}

void
COSXScreen::handlePowerChangeRequest(natural_t messageType, void* messageArg)
{
	// we've received a power change notification
	switch (messageType) {
	case kIOMessageSystemWillSleep:
		// COSXScreen has to handle this in the main thread so we have to
		// queue a confirm sleep event here.  we actually don't allow the
		// system to sleep until the event is handled.
		m_events->addEvent(CEvent(m_events->forCOSXScreen().confirmSleep(),
								getEventTarget(), messageArg,
								CEvent::kDontFreeData));
		return;
			
	case kIOMessageSystemHasPoweredOn:
		LOG((CLOG_DEBUG "system wakeup"));
		m_events->addEvent(CEvent(m_events->forIScreen().resume(),
								getEventTarget()));
		break;

	default:
		break;
	}

	CLock lock(m_pmMutex);
	if (m_pmRootPort != 0) {
		IOAllowPowerChange(m_pmRootPort, (long)messageArg);
	}
}

void
COSXScreen::handleConfirmSleep(const CEvent& event, void*)
{
	long messageArg = (long)event.getData();
	if (messageArg != 0) {
		CLock lock(m_pmMutex);
		if (m_pmRootPort != 0) {
			// deliver suspend event immediately.
			m_events->addEvent(CEvent(m_events->forIScreen().suspend(),
									getEventTarget(), NULL, 
									CEvent::kDeliverImmediately));
	
			LOG((CLOG_DEBUG "system will sleep"));
			IOAllowPowerChange(m_pmRootPort, messageArg);
		}
	}
}

#pragma mark - 

//
// GLOBAL HOTKEY OPERATING MODE SUPPORT (10.3)
//
// CoreGraphics private API (OSX 10.3)
// Source: http://ichiro.nnip.org/osx/Cocoa/GlobalHotkey.html
//
// We load the functions dynamically because they're not available in
// older SDKs.  We don't use weak linking because we want users of
// older SDKs to build an app that works on newer systems and older
// SDKs will not provide the symbols.
//
// FIXME: This is hosed as of OS 10.5; patches to repair this are
// a good thing.
//
#if 0

#ifdef	__cplusplus
extern "C" {
#endif

typedef int CGSConnection;
typedef enum {
	CGSGlobalHotKeyEnable = 0,
	CGSGlobalHotKeyDisable = 1,
} CGSGlobalHotKeyOperatingMode;

extern CGSConnection _CGSDefaultConnection(void) WEAK_IMPORT_ATTRIBUTE;
extern CGError CGSGetGlobalHotKeyOperatingMode(CGSConnection connection, CGSGlobalHotKeyOperatingMode *mode) WEAK_IMPORT_ATTRIBUTE;
extern CGError CGSSetGlobalHotKeyOperatingMode(CGSConnection connection, CGSGlobalHotKeyOperatingMode mode) WEAK_IMPORT_ATTRIBUTE;

typedef CGSConnection (*_CGSDefaultConnection_t)(void);
typedef CGError (*CGSGetGlobalHotKeyOperatingMode_t)(CGSConnection connection, CGSGlobalHotKeyOperatingMode *mode);
typedef CGError (*CGSSetGlobalHotKeyOperatingMode_t)(CGSConnection connection, CGSGlobalHotKeyOperatingMode mode);

static _CGSDefaultConnection_t				s__CGSDefaultConnection;
static CGSGetGlobalHotKeyOperatingMode_t	s_CGSGetGlobalHotKeyOperatingMode;
static CGSSetGlobalHotKeyOperatingMode_t	s_CGSSetGlobalHotKeyOperatingMode;

#ifdef	__cplusplus
}
#endif

#define LOOKUP(name_)													\
	s_ ## name_ = NULL;													\
	if (NSIsSymbolNameDefinedWithHint("_" #name_, "CoreGraphics")) {	\
		s_ ## name_ = (name_ ## _t)NSAddressOfSymbol(					\
							NSLookupAndBindSymbolWithHint(				\
								"_" #name_, "CoreGraphics"));			\
	}

bool
COSXScreen::isGlobalHotKeyOperatingModeAvailable()
{
	if (!s_testedForGHOM) {
		s_testedForGHOM = true;
		LOOKUP(_CGSDefaultConnection);
		LOOKUP(CGSGetGlobalHotKeyOperatingMode);
		LOOKUP(CGSSetGlobalHotKeyOperatingMode);
		s_hasGHOM = (s__CGSDefaultConnection != NULL &&
					s_CGSGetGlobalHotKeyOperatingMode != NULL &&
					s_CGSSetGlobalHotKeyOperatingMode != NULL);
	}
	return s_hasGHOM;
}

void
COSXScreen::setGlobalHotKeysEnabled(bool enabled)
{
	if (isGlobalHotKeyOperatingModeAvailable()) {
		CGSConnection conn = s__CGSDefaultConnection();

		CGSGlobalHotKeyOperatingMode mode;
		s_CGSGetGlobalHotKeyOperatingMode(conn, &mode);

		if (enabled && mode == CGSGlobalHotKeyDisable) {
			s_CGSSetGlobalHotKeyOperatingMode(conn, CGSGlobalHotKeyEnable);
		}
		else if (!enabled && mode == CGSGlobalHotKeyEnable) {
			s_CGSSetGlobalHotKeyOperatingMode(conn, CGSGlobalHotKeyDisable);
		}
	}
}

bool
COSXScreen::getGlobalHotKeysEnabled()
{
	CGSGlobalHotKeyOperatingMode mode;
	if (isGlobalHotKeyOperatingModeAvailable()) {
		CGSConnection conn = s__CGSDefaultConnection();
		s_CGSGetGlobalHotKeyOperatingMode(conn, &mode);
	}
	else {
		mode = CGSGlobalHotKeyEnable;
	}
	return (mode == CGSGlobalHotKeyEnable);
}

#endif

//
// COSXScreen::CHotKeyItem
//

COSXScreen::CHotKeyItem::CHotKeyItem(UInt32 keycode, UInt32 mask) :
	m_ref(NULL),
	m_keycode(keycode),
	m_mask(mask)
{
	// do nothing
}

COSXScreen::CHotKeyItem::CHotKeyItem(EventHotKeyRef ref,
				UInt32 keycode, UInt32 mask) :
	m_ref(ref),
	m_keycode(keycode),
	m_mask(mask)
{
	// do nothing
}

EventHotKeyRef
COSXScreen::CHotKeyItem::getRef() const
{
	return m_ref;
}

bool
COSXScreen::CHotKeyItem::operator<(const CHotKeyItem& x) const
{
	return (m_keycode < x.m_keycode ||
			(m_keycode == x.m_keycode && m_mask < x.m_mask));
}

// Quartz event tap support for the secondary display. This makes sure that we
// will show the cursor if a local event comes in while synergy has the cursor
// off the screen.
CGEventRef
COSXScreen::handleCGInputEventSecondary(
	CGEventTapProxy proxy,
	CGEventType type,
	CGEventRef event,
	void* refcon)
{
	// this fix is really screwing with the correct show/hide behavior. it
	// should be tested better before reintroducing.
	return event;

	COSXScreen* screen = (COSXScreen*)refcon;
	if (screen->m_cursorHidden && type == kCGEventMouseMoved) {

		CGPoint pos = CGEventGetLocation(event);
		if (pos.x != screen->m_xCenter || pos.y != screen->m_yCenter) {

			LOG((CLOG_DEBUG "show cursor on secondary, type=%d pos=%d,%d",
					type, pos.x, pos.y));
			screen->showCursor();
		}
	}
	return event;
}

// Quartz event tap support
CGEventRef
COSXScreen::handleCGInputEvent(CGEventTapProxy proxy,
							   CGEventType type,
							   CGEventRef event,
							   void* refcon)
{
	COSXScreen* screen = (COSXScreen*)refcon;
	CGPoint pos;

	switch(type) {
		case kCGEventLeftMouseDown:
		case kCGEventRightMouseDown:
		case kCGEventOtherMouseDown:
			screen->onMouseButton(true, CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber) + 1);
			break;
		case kCGEventLeftMouseUp:
		case kCGEventRightMouseUp:
		case kCGEventOtherMouseUp:
			screen->onMouseButton(false, CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber) + 1);
			break;
		case kCGEventLeftMouseDragged:
		case kCGEventRightMouseDragged:
		case kCGEventOtherMouseDragged:
		case kCGEventMouseMoved:
			pos = CGEventGetLocation(event);
			screen->onMouseMove(pos.x, pos.y);
			
			// The system ignores our cursor-centering calls if
			// we don't return the event. This should be harmless,
			// but might register as slight movement to other apps
			// on the system. It hasn't been a problem before, though.
			return event;
			break;
		case kCGEventScrollWheel:
			screen->onMouseWheel(screen->mapScrollWheelToSynergy(
								 CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2)),
								 screen->mapScrollWheelToSynergy(
								 CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1)));
			break;
		case kCGEventKeyDown:
		case kCGEventKeyUp:
		case kCGEventFlagsChanged:
			screen->onKey(event);
			break;
		case kCGEventTapDisabledByTimeout:
			// Re-enable our event-tap
			CGEventTapEnable(screen->m_eventTapPort, true);
			LOG((CLOG_NOTE "quartz event tap was disabled by timeout, re-enabling"));
			break;
		case kCGEventTapDisabledByUserInput:
			LOG((CLOG_ERR "quartz event tap was disabled by user input"));
			break;
		case NX_NULLEVENT:
			break;
		case NX_SYSDEFINED:
			// Unknown, forward it
			return event;
			break;
		case NX_NUMPROCS:
			break;
		default:
			LOG((CLOG_NOTE "unknown quartz event type: 0x%02x", type));
	}
	
	if(screen->m_isOnScreen) {
		return event;
	} else {
		return NULL;
	}
}

void
COSXScreen::CMouseButtonState::set(UInt32 button, MouseButtonState state) 
{
	bool newState = (state == kMouseButtonDown);
	m_buttons.set(button, newState);
}

bool
COSXScreen::CMouseButtonState::any() 
{
	return m_buttons.any();
}

void
COSXScreen::CMouseButtonState::reset() 
{
	m_buttons.reset();
}

void
COSXScreen::CMouseButtonState::overwrite(UInt32 buttons) 
{
	m_buttons = std::bitset<NumButtonIDs>(buttons);
}

bool
COSXScreen::CMouseButtonState::test(UInt32 button) const 
{
	return m_buttons.test(button);
}

SInt8
COSXScreen::CMouseButtonState::getFirstButtonDown() const 
{
	if (m_buttons.any()) {
		for (unsigned short button = 0; button < m_buttons.size(); button++) {
			if (m_buttons.test(button)) {
				return button;
			}
		}
	}
	return -1;
}

char*
COSXScreen::CFStringRefToUTF8String(CFStringRef aString)
{
	if (aString == NULL) {
		return NULL;
	}
	
	CFIndex length = CFStringGetLength(aString);
	CFIndex maxSize = CFStringGetMaximumSizeForEncoding(
		length,
		kCFStringEncodingUTF8);
	char* buffer = (char*)malloc(maxSize);
	if (CFStringGetCString(aString, buffer, maxSize, kCFStringEncodingUTF8)) {
		return buffer;
	}
	return NULL;
}

void
COSXScreen::fakeDraggingFiles(CDragFileList fileList)
{
	m_fakeDraggingStarted = true;
	CString fileExt;
	if (fileList.size() == 1) {
		fileExt = CDragInformation::getDragFileExtension(
			fileList.at(0).getFilename());
	}

#if defined(MAC_OS_X_VERSION_10_7)
	fakeDragging(fileExt.c_str(), m_xCursor, m_yCursor);
#else
	LOG((CLOG_WARN "drag drop not supported"));
#endif
}

CString&
COSXScreen::getDraggingFilename()
{
	if (m_draggingStarted) {
		CFStringRef dragInfo = getDraggedFileURL();
		char* info = NULL;
		info = CFStringRefToUTF8String(dragInfo);
		if (info == NULL) {
			m_draggingFilename.clear();
		}
		else {
			LOG((CLOG_DEBUG "drag info: %s", info));
			CFRelease(dragInfo);
			CString fileList(info);
			m_draggingFilename = fileList;
		}

		// fake a escape key down and up then left mouse button up
		fakeKeyDown(kKeyEscape, 8192, 1);
		fakeKeyUp(1);
		fakeMouseButton(kButtonLeft, false);
	}
	return m_draggingFilename;
}

void
COSXScreen::waitForCarbonLoop() const
{
#if defined(MAC_OS_X_VERSION_10_7)
	double timeout = ARCH->time() + 5;
	m_carbonLoopReady->lock();
	
	while (!m_carbonLoopReady->wait()) {
		if(ARCH->time() > timeout) {
			throw std::runtime_error("carbon loop is not ready within 5 sec");
		}
	}
	
	m_carbonLoopReady->unlock();
#endif
}

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

void
setZeroSuppressionInterval()
{
	CGSetLocalEventsSuppressionInterval(0.0);
}

void
avoidSupression()
{
	// avoid suppression of local hardware events
	// stkamp@users.sourceforge.net
	CGSetLocalEventsFilterDuringSupressionState(
							kCGEventFilterMaskPermitAllEvents,
							kCGEventSupressionStateSupressionInterval);
	CGSetLocalEventsFilterDuringSupressionState(
							(kCGEventFilterMaskPermitLocalKeyboardEvents |
							kCGEventFilterMaskPermitSystemDefinedEvents),
							kCGEventSupressionStateRemoteMouseDrag);
}

void
logCursorVisibility()
{
	// CGCursorIsVisible is probably deprecated because its unreliable.
	if (!CGCursorIsVisible()) {
		LOG((CLOG_WARN "cursor may not be visible"));
	}
}

void
avoidHesitatingCursor()
{
	// This used to be necessary to get smooth mouse motion on other screens,
	// but now is just to avoid a hesitating cursor when transitioning to
	// the primary (this) screen.
	CGSetLocalEventsSuppressionInterval(0.0001);
}

#pragma GCC diagnostic error "-Wdeprecated-declarations"

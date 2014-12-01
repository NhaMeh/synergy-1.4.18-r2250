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

#include "server/Config.h"
#include "synergy/clipboard_types.h"
#include "synergy/Clipboard.h"
#include "synergy/key_types.h"
#include "synergy/mouse_types.h"
#include "synergy/INode.h"
#include "synergy/DragInformation.h"
#include "base/Event.h"
#include "base/Stopwatch.h"
#include "base/EventTypes.h"
#include "common/stdmap.h"
#include "common/stdset.h"
#include "common/stdvector.h"

class CBaseClientProxy;
class CEventQueueTimer;
class CPrimaryClient;
class CInputFilter;
class CScreen;
class IEventQueue;
class CThread;

//! Synergy server
/*!
This class implements the top-level server algorithms for synergy.
*/
class CServer : public INode {
public:
	//! Lock cursor to screen data
	class CLockCursorToScreenInfo {
	public:
		enum State { kOff, kOn, kToggle };

		static CLockCursorToScreenInfo* alloc(State state = kToggle);

	public:
		State			m_state;
	};

	//! Switch to screen data
	class CSwitchToScreenInfo {
	public:
		static CSwitchToScreenInfo* alloc(const CString& screen);

	public:
		// this is a C-string;  this type is a variable size structure
		char			m_screen[1];
	};

	//! Switch in direction data
	class CSwitchInDirectionInfo {
	public:
		static CSwitchInDirectionInfo* alloc(EDirection direction);

	public:
		EDirection		m_direction;
	};

	//! Screen connected data
	class CScreenConnectedInfo {
	public:
		CScreenConnectedInfo(CString screen) : m_screen(screen) { }

	public:
		CString			m_screen; // was char[1]
	};

	//! Keyboard broadcast data
	class CKeyboardBroadcastInfo {
	public:
		enum State { kOff, kOn, kToggle };

		static CKeyboardBroadcastInfo* alloc(State state = kToggle);
		static CKeyboardBroadcastInfo* alloc(State state,
											const CString& screens);

	public:
		State			m_state;
		char			m_screens[1];
	};

	/*!
	Start the server with the configuration \p config and the primary
	client (local screen) \p primaryClient.  The client retains
	ownership of \p primaryClient.
	*/
	CServer(CConfig& config, CPrimaryClient* primaryClient, CScreen* screen, IEventQueue* events, bool enableDragDrop);
	~CServer();

#ifdef TEST_ENV
	CServer() : m_mock(true), m_config(NULL) { }
	void setActive(CBaseClientProxy* active) {	m_active = active; }
#endif

	//! @name manipulators
	//@{

	//! Set configuration
	/*!
	Change the server's configuration.  Returns true iff the new
	configuration was accepted (it must include the server's name).
	This will disconnect any clients no longer in the configuration.
	*/
	bool				setConfig(const CConfig&);

	//! Add a client
	/*!
	Adds \p client to the server.  The client is adopted and will be
	destroyed when the client disconnects or is disconnected.
	*/
	void				adoptClient(CBaseClientProxy* client);

	//! Disconnect clients
	/*!
	Disconnect clients.  This tells them to disconnect but does not wait
	for them to actually do so.  The server sends the disconnected event
	when they're all disconnected (or immediately if none are connected).
	The caller can also just destroy this object to force the disconnection.
	*/
	void				disconnect();

	//! Clears the file buffer
	void				clearReceivedFileData();

	//! Set the expected size of receiving file
	void				setExpectedFileSize(CString data);
	
	//! Received a chunk of file data
	void				fileChunkReceived(CString data);

	//! Create a new thread and use it to send file to client
	void				sendFileToClient(const char* filename);

	//! Received dragging information from client
	void				dragInfoReceived(UInt32 fileNum, CString content);
	
	//@}
	//! @name accessors
	//@{

	//! Get number of connected clients
	/*!
	Returns the number of connected clients, including the server itself.
	*/
	UInt32				getNumClients() const;

	//! Get the list of connected clients
	/*!
	Set the \c list to the names of the currently connected clients.
	*/
	void				getClients(std::vector<CString>& list) const;
	
	//! Return true if recieved file size is valid
	bool				isReceivedFileSizeValid();

	//! Return expected file size
	size_t				getExpectedFileSize() { return m_expectedFileSize; }

	//@}

private:
	// get canonical name of client
	CString				getName(const CBaseClientProxy*) const;

	// get the sides of the primary screen that have neighbors
	UInt32				getActivePrimarySides() const;

	// returns true iff mouse should be locked to the current screen
	// according to this object only, ignoring what the primary client
	// says.
	bool				isLockedToScreenServer() const;

	// returns true iff mouse should be locked to the current screen
	// according to this object or the primary client.
	bool				isLockedToScreen() const;

	// returns the jump zone of the client
	SInt32				getJumpZoneSize(CBaseClientProxy*) const;

	// change the active screen
	void				switchScreen(CBaseClientProxy*,
							SInt32 x, SInt32 y, bool forScreenSaver);

	// jump to screen
	void				jumpToScreen(CBaseClientProxy*);

	// convert pixel position to fraction, using x or y depending on the
	// direction.
	float				mapToFraction(CBaseClientProxy*, EDirection,
							SInt32 x, SInt32 y) const;

	// convert fraction to pixel position, writing only x or y depending
	// on the direction.
	void				mapToPixel(CBaseClientProxy*, EDirection, float f,
							SInt32& x, SInt32& y) const;

	// returns true if the client has a neighbor anywhere along the edge
	// indicated by the direction.
	bool				hasAnyNeighbor(CBaseClientProxy*, EDirection) const;

	// lookup neighboring screen, mapping the coordinate independent of
	// the direction to the neighbor's coordinate space.
	CBaseClientProxy*	getNeighbor(CBaseClientProxy*, EDirection,
							SInt32& x, SInt32& y) const;

	// lookup neighboring screen.  given a position relative to the
	// source screen, find the screen we should move onto and where.
	// if the position is sufficiently far from the source then we
	// cross multiple screens.  if there is no suitable screen then
	// return NULL and x,y are not modified.
	CBaseClientProxy*	mapToNeighbor(CBaseClientProxy*, EDirection,
							SInt32& x, SInt32& y) const;

	// adjusts x and y or neither to avoid ending up in a jump zone
	// after entering the client in the given direction.
	void				avoidJumpZone(CBaseClientProxy*, EDirection,
							SInt32& x, SInt32& y) const;

	// test if a switch is permitted.  this includes testing user
	// options like switch delay and tracking any state required to
	// implement them.  returns true iff a switch is permitted.
	bool				isSwitchOkay(CBaseClientProxy* dst, EDirection,
							SInt32 x, SInt32 y, SInt32 xActive, SInt32 yActive);

	// update switch state due to a mouse move at \p x, \p y that
	// doesn't switch screens.
	void				noSwitch(SInt32 x, SInt32 y);

	// stop switch timers
	void				stopSwitch();

	// start two tap switch timer
	void				startSwitchTwoTap();

	// arm the two tap switch timer if \p x, \p y is outside the tap zone
	void				armSwitchTwoTap(SInt32 x, SInt32 y);

	// stop the two tap switch timer
	void				stopSwitchTwoTap();

	// returns true iff the two tap switch timer is started
	bool				isSwitchTwoTapStarted() const;

	// returns true iff should switch because of two tap
	bool				shouldSwitchTwoTap() const;

	// start delay switch timer
	void				startSwitchWait(SInt32 x, SInt32 y);

	// stop delay switch timer
	void				stopSwitchWait();

	// returns true iff the delay switch timer is started
	bool				isSwitchWaitStarted() const;

	// returns the corner (EScreenSwitchCornerMasks) where x,y is on the
	// given client.  corners have the given size.
	UInt32				getCorner(CBaseClientProxy*,
							SInt32 x, SInt32 y, SInt32 size) const;

	// stop relative mouse moves
	void				stopRelativeMoves();

	// send screen options to \c client
	void				sendOptions(CBaseClientProxy* client) const;

	// process options from configuration
	void				processOptions();

	// event handlers
	void				handleShapeChanged(const CEvent&, void*);
	void				handleClipboardGrabbed(const CEvent&, void*);
	void				handleClipboardChanged(const CEvent&, void*);
	void				handleKeyDownEvent(const CEvent&, void*);
	void				handleKeyUpEvent(const CEvent&, void*);
	void				handleKeyRepeatEvent(const CEvent&, void*);
	void				handleButtonDownEvent(const CEvent&, void*);
	void				handleButtonUpEvent(const CEvent&, void*);
	void				handleMotionPrimaryEvent(const CEvent&, void*);
	void				handleMotionSecondaryEvent(const CEvent&, void*);
	void				handleWheelEvent(const CEvent&, void*);
	void				handleScreensaverActivatedEvent(const CEvent&, void*);
	void				handleScreensaverDeactivatedEvent(const CEvent&, void*);
	void				handleSwitchWaitTimeout(const CEvent&, void*);
	void				handleClientDisconnected(const CEvent&, void*);
	void				handleClientCloseTimeout(const CEvent&, void*);
	void				handleSwitchToScreenEvent(const CEvent&, void*);
	void				handleSwitchInDirectionEvent(const CEvent&, void*);
	void				handleKeyboardBroadcastEvent(const CEvent&,void*);
	void				handleLockCursorToScreenEvent(const CEvent&, void*);
	void				handleFakeInputBeginEvent(const CEvent&, void*);
	void				handleFakeInputEndEvent(const CEvent&, void*);
	void				handleFileChunkSendingEvent(const CEvent&, void*);
	void				handleFileRecieveCompletedEvent(const CEvent&, void*);

	// event processing
	void				onClipboardChanged(CBaseClientProxy* sender,
							ClipboardID id, UInt32 seqNum);
	void				onScreensaver(bool activated);
	void				onKeyDown(KeyID, KeyModifierMask, KeyButton,
							const char* screens);
	void				onKeyUp(KeyID, KeyModifierMask, KeyButton,
							const char* screens);
	void				onKeyRepeat(KeyID, KeyModifierMask, SInt32, KeyButton);
	void				onMouseDown(ButtonID);
	void				onMouseUp(ButtonID);
	bool				onMouseMovePrimary(SInt32 x, SInt32 y);
	void				onMouseMoveSecondary(SInt32 dx, SInt32 dy);
	void				onMouseWheel(SInt32 xDelta, SInt32 yDelta);
	void				onFileChunkSending(const void* data);
	void				onFileRecieveCompleted();

	// add client to list and attach event handlers for client
	bool				addClient(CBaseClientProxy*);

	// remove client from list and detach event handlers for client
	bool				removeClient(CBaseClientProxy*);

	// close a client
	void				closeClient(CBaseClientProxy*, const char* msg);

	// close clients not in \p config
	void				closeClients(const CConfig& config);

	// close all clients whether they've completed the handshake or not,
	// except the primary client
	void				closeAllClients();

	// remove clients from internal state
	void				removeActiveClient(CBaseClientProxy*);
	void				removeOldClient(CBaseClientProxy*);

	// force the cursor off of \p client
	void				forceLeaveClient(CBaseClientProxy* client);
	
	// thread funciton for sending file
	void				sendFileThread(void*);
	
	// thread function for writing file to drop directory
	void				writeToDropDirThread(void*);

	// thread function for getting drag filename
	void				getDragInfoThread(void*);

	// send drag info to new client screen
	void				sendDragInfo(CBaseClientProxy* newScreen);

public:
	bool				m_mock;

private:
	class CClipboardInfo {
	public:
		CClipboardInfo();

	public:
		CClipboard		m_clipboard;
		CString			m_clipboardData;
		CString			m_clipboardOwner;
		UInt32			m_clipboardSeqNum;
	};

	// the primary screen client
	CPrimaryClient*		m_primaryClient;

	// all clients (including the primary client) indexed by name
	typedef std::map<CString, CBaseClientProxy*> CClientList;
	typedef std::set<CBaseClientProxy*> CClientSet;
	CClientList			m_clients;
	CClientSet			m_clientSet;

	// all old connections that we're waiting to hangup
	typedef std::map<CBaseClientProxy*, CEventQueueTimer*> COldClients;
	COldClients			m_oldClients;

	// the client with focus
	CBaseClientProxy*	m_active;

	// the sequence number of enter messages
	UInt32				m_seqNum;

	// current mouse position (in absolute screen coordinates) on
	// whichever screen is active
	SInt32				m_x, m_y;

	// last mouse deltas.  this is needed to smooth out double tap
	// on win32 which reports bogus mouse motion at the edge of
	// the screen when using low level hooks, synthesizing motion
	// in the opposite direction the mouse actually moved.
	SInt32				m_xDelta, m_yDelta;
	SInt32				m_xDelta2, m_yDelta2;

	// current configuration
	CConfig*			m_config;

	// input filter (from m_config);
	CInputFilter*		m_inputFilter;

	// clipboard cache
	CClipboardInfo		m_clipboards[kClipboardEnd];

	// state saved when screen saver activates
	CBaseClientProxy*	m_activeSaver;
	SInt32				m_xSaver, m_ySaver;

	// common state for screen switch tests.  all tests are always
	// trying to reach the same screen in the same direction.
	EDirection			m_switchDir;
	CBaseClientProxy*	m_switchScreen;

	// state for delayed screen switching
	double				m_switchWaitDelay;
	CEventQueueTimer*	m_switchWaitTimer;
	SInt32				m_switchWaitX, m_switchWaitY;

	// state for double-tap screen switching
	double				m_switchTwoTapDelay;
	CStopwatch			m_switchTwoTapTimer;
	bool				m_switchTwoTapEngaged;
	bool				m_switchTwoTapArmed;
	SInt32				m_switchTwoTapZone;

	// modifiers needed before switching
	bool				m_switchNeedsShift;
	bool				m_switchNeedsControl;
	bool				m_switchNeedsAlt;
	
	// relative mouse move option
	bool				m_relativeMoves;

	// flag whether or not we have broadcasting enabled and the screens to
	// which we should send broadcasted keys.
	bool				m_keyboardBroadcasting;
	CString				m_keyboardBroadcastingScreens;

	// screen locking (former scroll lock)
	bool				m_lockedToScreen;

	// server screen
	CScreen*			m_screen;

	IEventQueue*		m_events;

	// file transfer
	size_t				m_expectedFileSize;
	CString				m_receivedFileData;
	CDragFileList		m_dragFileList;
	CThread*			m_sendFileThread;
	CThread*			m_writeToDropDirThread;
	CString				m_dragFileExt;
	bool				m_ignoreFileTransfer;
	bool				m_enableDragDrop;

	CThread*			m_getDragInfoThread;
	bool				m_waitDragInfoThread;
};

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

#include "client/ServerProxy.h"

#include "client/Client.h"
#include "synergy/Clipboard.h"
#include "synergy/ProtocolUtil.h"
#include "synergy/option_types.h"
#include "synergy/protocol_types.h"
#include "io/IStream.h"
#include "io/CryptoStream.h"
#include "base/Log.h"
#include "base/IEventQueue.h"
#include "base/TMethodEventJob.h"
#include "base/XBase.h"

#include <memory>

//
// CServerProxy
//

const UInt16 CServerProxy::m_intervalThreshold = 1;

CServerProxy::CServerProxy(CClient* client, synergy::IStream* stream, IEventQueue* events) :
	m_client(client),
	m_stream(stream),
	m_seqNum(0),
	m_compressMouse(false),
	m_compressMouseRelative(false),
	m_xMouse(0),
	m_yMouse(0),
	m_dxMouse(0),
	m_dyMouse(0),
	m_ignoreMouse(false),
	m_keepAliveAlarm(0.0),
	m_keepAliveAlarmTimer(NULL),
	m_parser(&CServerProxy::parseHandshakeMessage),
	m_events(events),
	m_stopwatch(true),
	m_elapsedTime(0),
	m_receivedDataSize(0)
{
	assert(m_client != NULL);
	assert(m_stream != NULL);

	// initialize modifier translation table
	for (KeyModifierID id = 0; id < kKeyModifierIDLast; ++id)
		m_modifierTranslationTable[id] = id;

	// handle data on stream
	m_events->adoptHandler(m_events->forIStream().inputReady(),
							m_stream->getEventTarget(),
							new TMethodEventJob<CServerProxy>(this,
								&CServerProxy::handleData));

	// send heartbeat
	setKeepAliveRate(kKeepAliveRate);
}

CServerProxy::~CServerProxy()
{
	setKeepAliveRate(-1.0);
	m_events->removeHandler(m_events->forIStream().inputReady(),
							m_stream->getEventTarget());
}

void
CServerProxy::resetKeepAliveAlarm()
{
	if (m_keepAliveAlarmTimer != NULL) {
		m_events->removeHandler(CEvent::kTimer, m_keepAliveAlarmTimer);
		m_events->deleteTimer(m_keepAliveAlarmTimer);
		m_keepAliveAlarmTimer = NULL;
	}
	if (m_keepAliveAlarm > 0.0) {
		m_keepAliveAlarmTimer =
			m_events->newOneShotTimer(m_keepAliveAlarm, NULL);
		m_events->adoptHandler(CEvent::kTimer, m_keepAliveAlarmTimer,
							new TMethodEventJob<CServerProxy>(this,
								&CServerProxy::handleKeepAliveAlarm));
	}
}

void
CServerProxy::setKeepAliveRate(double rate)
{
	m_keepAliveAlarm = rate * kKeepAlivesUntilDeath;
	resetKeepAliveAlarm();
}

void
CServerProxy::handleData(const CEvent&, void*)
{
	// handle messages until there are no more.  first read message code.
	UInt8 code[4];
	UInt32 n = m_stream->read(code, 4);
	while (n != 0) {
		// verify we got an entire code
		if (n != 4) {
			LOG((CLOG_ERR "incomplete message from server: %d bytes", n));
			m_client->disconnect("incomplete message from server");
			return;
		}

		// parse message
		LOG((CLOG_DEBUG2 "msg from server: %c%c%c%c", code[0], code[1], code[2], code[3]));
		switch ((this->*m_parser)(code)) {
		case kOkay:
			break;

		case kUnknown:
			LOG((CLOG_ERR "invalid message from server: %c%c%c%c", code[0], code[1], code[2], code[3]));
			m_client->disconnect("invalid message from server");
			return;

		case kDisconnect:
			return;
		}

		// next message
		n = m_stream->read(code, 4);
	}

	flushCompressedMouse();
}

CServerProxy::EResult
CServerProxy::parseHandshakeMessage(const UInt8* code)
{
	if (memcmp(code, kMsgQInfo, 4) == 0) {
		queryInfo();
	}

	else if (memcmp(code, kMsgCInfoAck, 4) == 0) {
		infoAcknowledgment();
	}

	else if (memcmp(code, kMsgDSetOptions, 4) == 0) {
		setOptions();

		// handshake is complete
		m_parser = &CServerProxy::parseMessage;
		m_client->handshakeComplete();
	}

	else if (memcmp(code, kMsgCResetOptions, 4) == 0) {
		resetOptions();
	}

	else if (memcmp(code, kMsgCKeepAlive, 4) == 0) {
		// echo keep alives and reset alarm
		CProtocolUtil::writef(m_stream, kMsgCKeepAlive);
		resetKeepAliveAlarm();
	}

	else if (memcmp(code, kMsgCNoop, 4) == 0) {
		// accept and discard no-op
	}

	else if (memcmp(code, kMsgCClose, 4) == 0) {
		// server wants us to hangup
		LOG((CLOG_DEBUG1 "recv close"));
		m_client->disconnect(NULL);
		return kDisconnect;
	}

	else if (memcmp(code, kMsgEIncompatible, 4) == 0) {
		SInt32 major, minor;
		CProtocolUtil::readf(m_stream,
						kMsgEIncompatible + 4, &major, &minor);
		LOG((CLOG_ERR "server has incompatible version %d.%d", major, minor));
		m_client->disconnect("server has incompatible version");
		return kDisconnect;
	}

	else if (memcmp(code, kMsgEBusy, 4) == 0) {
		LOG((CLOG_ERR "server already has a connected client with name \"%s\"", m_client->getName().c_str()));
		m_client->disconnect("server already has a connected client with our name");
		return kDisconnect;
	}

	else if (memcmp(code, kMsgEUnknown, 4) == 0) {
		LOG((CLOG_ERR "server refused client with name \"%s\"", m_client->getName().c_str()));
		m_client->disconnect("server refused client with our name");
		return kDisconnect;
	}

	else if (memcmp(code, kMsgEBad, 4) == 0) {
		LOG((CLOG_ERR "server disconnected due to a protocol error"));
		m_client->disconnect("server reported a protocol error");
		return kDisconnect;
	}
	else {
		return kUnknown;
	}

	return kOkay;
}

CServerProxy::EResult
CServerProxy::parseMessage(const UInt8* code)
{
	if (memcmp(code, kMsgDMouseMove, 4) == 0) {
		mouseMove();
	}

	else if (memcmp(code, kMsgDMouseRelMove, 4) == 0) {
		mouseRelativeMove();
	}

	else if (memcmp(code, kMsgDMouseWheel, 4) == 0) {
		mouseWheel();
	}

	else if (memcmp(code, kMsgDKeyDown, 4) == 0) {
		keyDown();
	}

	else if (memcmp(code, kMsgDKeyUp, 4) == 0) {
		keyUp();
	}

	else if (memcmp(code, kMsgDMouseDown, 4) == 0) {
		mouseDown();
	}

	else if (memcmp(code, kMsgDMouseUp, 4) == 0) {
		mouseUp();
	}

	else if (memcmp(code, kMsgDKeyRepeat, 4) == 0) {
		keyRepeat();
	}

	else if (memcmp(code, kMsgCKeepAlive, 4) == 0) {
		// echo keep alives and reset alarm
		CProtocolUtil::writef(m_stream, kMsgCKeepAlive);
		resetKeepAliveAlarm();
	}

	else if (memcmp(code, kMsgCNoop, 4) == 0) {
		// accept and discard no-op
	}

	else if (memcmp(code, kMsgCEnter, 4) == 0) {
		enter();
	}

	else if (memcmp(code, kMsgCLeave, 4) == 0) {
		leave();
	}

	else if (memcmp(code, kMsgCClipboard, 4) == 0) {
		grabClipboard();
	}

	else if (memcmp(code, kMsgCScreenSaver, 4) == 0) {
		screensaver();
	}

	else if (memcmp(code, kMsgQInfo, 4) == 0) {
		queryInfo();
	}

	else if (memcmp(code, kMsgCInfoAck, 4) == 0) {
		infoAcknowledgment();
	}

	else if (memcmp(code, kMsgDClipboard, 4) == 0) {
		setClipboard();
	}

	else if (memcmp(code, kMsgCResetOptions, 4) == 0) {
		resetOptions();
	}

	else if (memcmp(code, kMsgDSetOptions, 4) == 0) {
		setOptions();
	}

	else if (memcmp(code, kMsgDCryptoIv, 4) == 0) {
		cryptoIv();
	}

	else if (memcmp(code, kMsgDFileTransfer, 4) == 0) {
		fileChunkReceived();
	}
	else if (memcmp(code, kMsgDDragInfo, 4) == 0) {
		dragInfoReceived();
	}

	else if (memcmp(code, kMsgCClose, 4) == 0) {
		// server wants us to hangup
		LOG((CLOG_DEBUG1 "recv close"));
		m_client->disconnect(NULL);
		return kDisconnect;
	}
	else if (memcmp(code, kMsgEBad, 4) == 0) {
		LOG((CLOG_ERR "server disconnected due to a protocol error"));
		m_client->disconnect("server reported a protocol error");
		return kDisconnect;
	}
	else {
		return kUnknown;
	}

	// send a reply.  this is intended to work around a delay when
	// running a linux server and an OS X (any BSD?) client.  the
	// client waits to send an ACK (if the system control flag
	// net.inet.tcp.delayed_ack is 1) in hopes of piggybacking it
	// on a data packet.  we provide that packet here.  i don't
	// know why a delayed ACK should cause the server to wait since
	// TCP_NODELAY is enabled.
	CProtocolUtil::writef(m_stream, kMsgCNoop);

	return kOkay;
}

void
CServerProxy::handleKeepAliveAlarm(const CEvent&, void*)
{
	LOG((CLOG_NOTE "server is dead"));
	m_client->disconnect("server is not responding");
}

void
CServerProxy::onInfoChanged()
{
	// ignore mouse motion until we receive acknowledgment of our info
	// change message.
	m_ignoreMouse = true;

	// send info update
	queryInfo();
}

bool
CServerProxy::onGrabClipboard(ClipboardID id)
{
	LOG((CLOG_DEBUG1 "sending clipboard %d changed", id));
	CProtocolUtil::writef(m_stream, kMsgCClipboard, id, m_seqNum);
	return true;
}

void
CServerProxy::onClipboardChanged(ClipboardID id, const IClipboard* clipboard)
{
	CString data = IClipboard::marshall(clipboard);
	LOG((CLOG_DEBUG1 "sending clipboard %d seqnum=%d, size=%d", id, m_seqNum, data.size()));
	CProtocolUtil::writef(m_stream, kMsgDClipboard, id, m_seqNum, &data);
}

void
CServerProxy::flushCompressedMouse()
{
	if (m_compressMouse) {
		m_compressMouse = false;
		m_client->mouseMove(m_xMouse, m_yMouse);
	}
	if (m_compressMouseRelative) {
		m_compressMouseRelative = false;
		m_client->mouseRelativeMove(m_dxMouse, m_dyMouse);
		m_dxMouse = 0;
		m_dyMouse = 0;
	}
}

void
CServerProxy::sendInfo(const CClientInfo& info)
{
	LOG((CLOG_DEBUG1 "sending info shape=%d,%d %dx%d", info.m_x, info.m_y, info.m_w, info.m_h));
	CProtocolUtil::writef(m_stream, kMsgDInfo,
								info.m_x, info.m_y,
								info.m_w, info.m_h, 0,
								info.m_mx, info.m_my);
}

KeyID
CServerProxy::translateKey(KeyID id) const
{
	static const KeyID s_translationTable[kKeyModifierIDLast][2] = {
		{ kKeyNone,      kKeyNone },
		{ kKeyShift_L,   kKeyShift_R },
		{ kKeyControl_L, kKeyControl_R },
		{ kKeyAlt_L,     kKeyAlt_R },
		{ kKeyMeta_L,    kKeyMeta_R },
		{ kKeySuper_L,   kKeySuper_R },
		{ kKeyAltGr,     kKeyAltGr}
	};

	KeyModifierID id2 = kKeyModifierIDNull;
	UInt32 side      = 0;
	switch (id) {
	case kKeyShift_L:
		id2  = kKeyModifierIDShift;
		side = 0;
		break;

	case kKeyShift_R:
		id2  = kKeyModifierIDShift;
		side = 1;
		break;

	case kKeyControl_L:
		id2  = kKeyModifierIDControl;
		side = 0;
		break;

	case kKeyControl_R:
		id2  = kKeyModifierIDControl;
		side = 1;
		break;

	case kKeyAlt_L:
		id2  = kKeyModifierIDAlt;
		side = 0;
		break;

	case kKeyAlt_R:
		id2  = kKeyModifierIDAlt;
		side = 1;
		break;

	case kKeyAltGr:
		id2 = kKeyModifierIDAltGr;
		side = 1; // there is only one alt gr key on the right side
		break;

	case kKeyMeta_L:
		id2  = kKeyModifierIDMeta;
		side = 0;
		break;

	case kKeyMeta_R:
		id2  = kKeyModifierIDMeta;
		side = 1;
		break;

	case kKeySuper_L:
		id2  = kKeyModifierIDSuper;
		side = 0;
		break;

	case kKeySuper_R:
		id2  = kKeyModifierIDSuper;
		side = 1;
		break;
	}

	if (id2 != kKeyModifierIDNull) {
		return s_translationTable[m_modifierTranslationTable[id2]][side];
	}
	else {
		return id;
	}
}

KeyModifierMask
CServerProxy::translateModifierMask(KeyModifierMask mask) const
{
	static const KeyModifierMask s_masks[kKeyModifierIDLast] = {
		0x0000,
		KeyModifierShift,
		KeyModifierControl,
		KeyModifierAlt,
		KeyModifierMeta,
		KeyModifierSuper,
		KeyModifierAltGr
	};

	KeyModifierMask newMask = mask & ~(KeyModifierShift |
										KeyModifierControl |
										KeyModifierAlt |
										KeyModifierMeta |
										KeyModifierSuper |
										KeyModifierAltGr );
	if ((mask & KeyModifierShift) != 0) {
		newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDShift]];
	}
	if ((mask & KeyModifierControl) != 0) {
		newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDControl]];
	}
	if ((mask & KeyModifierAlt) != 0) {
		newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDAlt]];
	}
	if ((mask & KeyModifierAltGr) != 0) {
		newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDAltGr]];
	}
	if ((mask & KeyModifierMeta) != 0) {
		newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDMeta]];
	}
	if ((mask & KeyModifierSuper) != 0) {
		newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDSuper]];
	}
	return newMask;
}

void
CServerProxy::enter()
{
	// parse
	SInt16 x, y;
	UInt16 mask;
	UInt32 seqNum;
	CProtocolUtil::readf(m_stream, kMsgCEnter + 4, &x, &y, &seqNum, &mask);
	LOG((CLOG_DEBUG1 "recv enter, %d,%d %d %04x", x, y, seqNum, mask));

	// discard old compressed mouse motion, if any
	m_compressMouse         = false;
	m_compressMouseRelative = false;
	m_dxMouse               = 0;
	m_dyMouse               = 0;
	m_seqNum                = seqNum;

	// forward
	m_client->enter(x, y, seqNum, static_cast<KeyModifierMask>(mask), false);
}

void
CServerProxy::leave()
{
	// parse
	LOG((CLOG_DEBUG1 "recv leave"));

	// send last mouse motion
	flushCompressedMouse();

	// forward
	m_client->leave();
}

void
CServerProxy::setClipboard()
{
	// parse
	ClipboardID id;
	UInt32 seqNum;
	CString data;
	CProtocolUtil::readf(m_stream, kMsgDClipboard + 4, &id, &seqNum, &data);
	LOG((CLOG_DEBUG "recv clipboard %d size=%d", id, data.size()));

	// validate
	if (id >= kClipboardEnd) {
		return;
	}

	// forward
	CClipboard clipboard;
	clipboard.unmarshall(data, 0);
	m_client->setClipboard(id, &clipboard);
}

void
CServerProxy::grabClipboard()
{
	// parse
	ClipboardID id;
	UInt32 seqNum;
	CProtocolUtil::readf(m_stream, kMsgCClipboard + 4, &id, &seqNum);
	LOG((CLOG_DEBUG "recv grab clipboard %d", id));

	// validate
	if (id >= kClipboardEnd) {
		return;
	}

	// forward
	m_client->grabClipboard(id);
}

void
CServerProxy::keyDown()
{
	// get mouse up to date
	flushCompressedMouse();

	// parse
	UInt16 id, mask, button;
	CProtocolUtil::readf(m_stream, kMsgDKeyDown + 4, &id, &mask, &button);
	LOG((CLOG_DEBUG1 "recv key down id=0x%08x, mask=0x%04x, button=0x%04x", id, mask, button));

	// translate
	KeyID id2             = translateKey(static_cast<KeyID>(id));
	KeyModifierMask mask2 = translateModifierMask(
								static_cast<KeyModifierMask>(mask));
	if (id2   != static_cast<KeyID>(id) ||
		mask2 != static_cast<KeyModifierMask>(mask))
		LOG((CLOG_DEBUG1 "key down translated to id=0x%08x, mask=0x%04x", id2, mask2));

	// forward
	m_client->keyDown(id2, mask2, button);
}

void
CServerProxy::keyRepeat()
{
	// get mouse up to date
	flushCompressedMouse();

	// parse
	UInt16 id, mask, count, button;
	CProtocolUtil::readf(m_stream, kMsgDKeyRepeat + 4,
								&id, &mask, &count, &button);
	LOG((CLOG_DEBUG1 "recv key repeat id=0x%08x, mask=0x%04x, count=%d, button=0x%04x", id, mask, count, button));

	// translate
	KeyID id2             = translateKey(static_cast<KeyID>(id));
	KeyModifierMask mask2 = translateModifierMask(
								static_cast<KeyModifierMask>(mask));
	if (id2   != static_cast<KeyID>(id) ||
		mask2 != static_cast<KeyModifierMask>(mask))
		LOG((CLOG_DEBUG1 "key repeat translated to id=0x%08x, mask=0x%04x", id2, mask2));

	// forward
	m_client->keyRepeat(id2, mask2, count, button);
}

void
CServerProxy::keyUp()
{
	// get mouse up to date
	flushCompressedMouse();

	// parse
	UInt16 id, mask, button;
	CProtocolUtil::readf(m_stream, kMsgDKeyUp + 4, &id, &mask, &button);
	LOG((CLOG_DEBUG1 "recv key up id=0x%08x, mask=0x%04x, button=0x%04x", id, mask, button));

	// translate
	KeyID id2             = translateKey(static_cast<KeyID>(id));
	KeyModifierMask mask2 = translateModifierMask(
								static_cast<KeyModifierMask>(mask));
	if (id2   != static_cast<KeyID>(id) ||
		mask2 != static_cast<KeyModifierMask>(mask))
		LOG((CLOG_DEBUG1 "key up translated to id=0x%08x, mask=0x%04x", id2, mask2));

	// forward
	m_client->keyUp(id2, mask2, button);
}

void
CServerProxy::mouseDown()
{
	// get mouse up to date
	flushCompressedMouse();

	// parse
	SInt8 id;
	CProtocolUtil::readf(m_stream, kMsgDMouseDown + 4, &id);
	LOG((CLOG_DEBUG1 "recv mouse down id=%d", id));

	// forward
	m_client->mouseDown(static_cast<ButtonID>(id));
}

void
CServerProxy::mouseUp()
{
	// get mouse up to date
	flushCompressedMouse();

	// parse
	SInt8 id;
	CProtocolUtil::readf(m_stream, kMsgDMouseUp + 4, &id);
	LOG((CLOG_DEBUG1 "recv mouse up id=%d", id));

	// forward
	m_client->mouseUp(static_cast<ButtonID>(id));
}

void
CServerProxy::mouseMove()
{
	// parse
	bool ignore;
	SInt16 x, y;
	CProtocolUtil::readf(m_stream, kMsgDMouseMove + 4, &x, &y);

	// note if we should ignore the move
	ignore = m_ignoreMouse;

	// compress mouse motion events if more input follows
	if (!ignore && !m_compressMouse && m_stream->isReady()) {
		m_compressMouse = true;
	}

	// if compressing then ignore the motion but record it
	if (m_compressMouse) {
		m_compressMouseRelative = false;
		ignore    = true;
		m_xMouse  = x;
		m_yMouse  = y;
		m_dxMouse = 0;
		m_dyMouse = 0;
	}
	LOG((CLOG_DEBUG2 "recv mouse move %d,%d", x, y));

	// forward
	if (!ignore) {
		m_client->mouseMove(x, y);
	}
}

void
CServerProxy::mouseRelativeMove()
{
	// parse
	bool ignore;
	SInt16 dx, dy;
	CProtocolUtil::readf(m_stream, kMsgDMouseRelMove + 4, &dx, &dy);

	// note if we should ignore the move
	ignore = m_ignoreMouse;

	// compress mouse motion events if more input follows
	if (!ignore && !m_compressMouseRelative && m_stream->isReady()) {
		m_compressMouseRelative = true;
	}

	// if compressing then ignore the motion but record it
	if (m_compressMouseRelative) {
		ignore     = true;
		m_dxMouse += dx;
		m_dyMouse += dy;
	}
	LOG((CLOG_DEBUG2 "recv mouse relative move %d,%d", dx, dy));

	// forward
	if (!ignore) {
		m_client->mouseRelativeMove(dx, dy);
	}
}

void
CServerProxy::mouseWheel()
{
	// get mouse up to date
	flushCompressedMouse();

	// parse
	SInt16 xDelta, yDelta;
	CProtocolUtil::readf(m_stream, kMsgDMouseWheel + 4, &xDelta, &yDelta);
	LOG((CLOG_DEBUG2 "recv mouse wheel %+d,%+d", xDelta, yDelta));

	// forward
	m_client->mouseWheel(xDelta, yDelta);
}

void
CServerProxy::cryptoIv()
{
	// parse
	CString s;
	CProtocolUtil::readf(m_stream, kMsgDCryptoIv + 4, &s);
	LOG((CLOG_DEBUG2 "recv crypto iv size=%i", s.size()));

	// forward
	m_client->setDecryptIv(reinterpret_cast<const UInt8*>(s.c_str()));
}

void
CServerProxy::screensaver()
{
	// parse
	SInt8 on;
	CProtocolUtil::readf(m_stream, kMsgCScreenSaver + 4, &on);
	LOG((CLOG_DEBUG1 "recv screen saver on=%d", on));

	// forward
	m_client->screensaver(on != 0);
}

void
CServerProxy::resetOptions()
{
	// parse
	LOG((CLOG_DEBUG1 "recv reset options"));

	// forward
	m_client->resetOptions();

	// reset keep alive
	setKeepAliveRate(kKeepAliveRate);

	// reset modifier translation table
	for (KeyModifierID id = 0; id < kKeyModifierIDLast; ++id) {
		m_modifierTranslationTable[id] = id;
	}
}

void
CServerProxy::setOptions()
{
	// parse
	COptionsList options;
	CProtocolUtil::readf(m_stream, kMsgDSetOptions + 4, &options);
	LOG((CLOG_DEBUG1 "recv set options size=%d", options.size()));

	// forward
	m_client->setOptions(options);

	// update modifier table
	for (UInt32 i = 0, n = (UInt32)options.size(); i < n; i += 2) {
		KeyModifierID id = kKeyModifierIDNull;
		if (options[i] == kOptionModifierMapForShift) {
			id = kKeyModifierIDShift;
		}
		else if (options[i] == kOptionModifierMapForControl) {
			id = kKeyModifierIDControl;
		}
		else if (options[i] == kOptionModifierMapForAlt) {
			id = kKeyModifierIDAlt;
		}
		else if (options[i] == kOptionModifierMapForAltGr) {
			id = kKeyModifierIDAltGr;
		}
		else if (options[i] == kOptionModifierMapForMeta) {
			id = kKeyModifierIDMeta;
		}
		else if (options[i] == kOptionModifierMapForSuper) {
			id = kKeyModifierIDSuper;
		}
		else if (options[i] == kOptionHeartbeat) {
			// update keep alive
			setKeepAliveRate(1.0e-3 * static_cast<double>(options[i + 1]));
		}
		if (id != kKeyModifierIDNull) {
			m_modifierTranslationTable[id] =
				static_cast<KeyModifierID>(options[i + 1]);
			LOG((CLOG_DEBUG1 "modifier %d mapped to %d", id, m_modifierTranslationTable[id]));
		}
	}
}

void
CServerProxy::queryInfo()
{
	CClientInfo info;
	m_client->getShape(info.m_x, info.m_y, info.m_w, info.m_h);
	m_client->getCursorPos(info.m_mx, info.m_my);
	sendInfo(info);
}

void
CServerProxy::infoAcknowledgment()
{
	LOG((CLOG_DEBUG1 "recv info acknowledgment"));
	m_ignoreMouse = false;
}

void
CServerProxy::fileChunkReceived()
{
	// parse
	UInt8 mark = 0;
	CString content;
	CProtocolUtil::readf(m_stream, kMsgDFileTransfer + 4, &mark, &content);

	switch (mark) {
	case kFileStart:
		m_client->clearReceivedFileData();
		m_client->setExpectedFileSize(content);
		if (CLOG->getFilter() >= kDEBUG2) {
			LOG((CLOG_DEBUG2 "recv file data from server: size=%s", content.c_str()));
			m_stopwatch.start();
		}
		break;

	case kFileChunk:
		m_client->fileChunkReceived(content);
		if (CLOG->getFilter() >= kDEBUG2) {
			LOG((CLOG_DEBUG2 "recv file data from server: size=%i", content.size()));
			double interval = m_stopwatch.getTime();
			LOG((CLOG_DEBUG2 "recv file data from server: interval=%f s", interval));
			m_receivedDataSize += content.size();
			if (interval >= m_intervalThreshold) {
				double averageSpeed = m_receivedDataSize / interval / 1000;
				LOG((CLOG_DEBUG2 "recv file data from server: average speed=%f kb/s", averageSpeed));

				m_receivedDataSize = 0;
				m_elapsedTime += interval;
				m_stopwatch.reset();
			}
		}
		break;

	case kFileEnd:
		m_events->addEvent(CEvent(m_events->forIScreen().fileRecieveCompleted(), m_client));
		if (CLOG->getFilter() >= kDEBUG2) {
			LOG((CLOG_DEBUG2 "file data transfer finished"));
			m_elapsedTime += m_stopwatch.getTime();
			double averageSpeed = m_client->getExpectedFileSize() / m_elapsedTime / 1000;
			LOG((CLOG_DEBUG2 "file data transfer finished: total time consumed=%f s", m_elapsedTime));
			LOG((CLOG_DEBUG2 "file data transfer finished: total data received=%i kb", m_client->getExpectedFileSize() / 1000));
			LOG((CLOG_DEBUG2 "file data transfer finished: total average speed=%f kb/s", averageSpeed));
		}
		break;
	}
}

void
CServerProxy::dragInfoReceived()
{
	// parse
	UInt32 fileNum = 0;
	CString content;
	CProtocolUtil::readf(m_stream, kMsgDDragInfo + 4, &fileNum, &content);

	m_client->dragInfoReceived(fileNum, content);
}

void
CServerProxy::fileChunkSending(UInt8 mark, char* data, size_t dataSize)
{
	CString chunk(data, dataSize);

	switch (mark) {
	case kFileStart:
		LOG((CLOG_DEBUG2 "file sending start: size=%s", data));
		break;

	case kFileChunk:
		LOG((CLOG_DEBUG2 "file chunk sending: size=%i", chunk.size()));
		break;

	case kFileEnd:
		LOG((CLOG_DEBUG2 "file sending finished"));
		break;
	}

	CProtocolUtil::writef(m_stream, kMsgDFileTransfer, mark, &chunk);
}

void
CServerProxy::sendDragInfo(UInt32 fileCount, const char* info, size_t size)
{
	CString data(info, size);
	CProtocolUtil::writef(m_stream, kMsgDDragInfo, fileCount, &data);
}

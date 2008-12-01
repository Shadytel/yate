/**
 * session.cpp
 * Yet Another Jingle Stack
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yatejingle.h>

using namespace TelEngine;

static XMPPNamespace s_ns;
static XMPPError s_err;

// Utility: add session content(s) to an already created stanza's jingle child
static void addJingleContents(XMLElement* xml, const ObjList& contents, bool minimum,
    bool addDesc, bool addTrans, bool addCandidates, bool addAuth = true)
{
    if (!xml)
	return;
    XMLElement* jingle = xml->findFirstChild(XMLElement::Jingle);
    if (!jingle)
	return;
    for (ObjList* o = contents.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
	jingle->addChild(c->toXml(minimum,addDesc,addTrans,addCandidates,addAuth));
    }
    TelEngine::destruct(jingle);
}

// Utility: add xml element child to an already created stanza's jingle child
static void addJingleChild(XMLElement* xml, XMLElement* child)
{
    if (!(xml && child))
	return;
    XMLElement* jingle = xml->findFirstChild(XMLElement::Jingle);
    if (!jingle)
	return;
    jingle->addChild(child);
    TelEngine::destruct(jingle);
}


/**
 * JGRtpMedia
 */
XMLElement* JGRtpMedia::toXML() const
{
    XMLElement* p = new XMLElement(XMLElement::PayloadType);
    p->setAttribute("id",m_id);
    p->setAttributeValid("name",m_name);
    p->setAttributeValid("clockrate",m_clockrate);
    p->setAttributeValid("channels",m_channels);
    unsigned int n = m_params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* s = m_params.getParam(i);
	if (!s)
	    continue;
        XMLElement* param = new XMLElement(XMLElement::Parameter);
	param->setAttributeValid("name",s->name());
	param->setAttributeValid("value",*s);
	p->addChild(param);
    }
    return p;
}

void JGRtpMedia::fromXML(XMLElement* xml)
{
    if (!xml) {
	set("","","","","");
	return;
    }
    set(xml->getAttribute("id"),xml->getAttribute("name"),
	xml->getAttribute("clockrate"),xml->getAttribute("channels"),"");
    XMLElement* param = xml->findFirstChild(XMLElement::Parameter);
    for (; param; param = xml->findNextChild(param,XMLElement::Parameter))
	m_params.addParam(param->getAttribute("name"),param->getAttribute("value"));
}


/**
 * JGCrypto
 */

XMLElement* JGCrypto::toXML() const
{
    XMLElement* xml = new XMLElement(XMLElement::Crypto);
    xml->setAttributeValid("crypto-suite",m_suite);
    xml->setAttributeValid("key-params",m_keyParams);
    xml->setAttributeValid("session-params",m_sessionParams);
    xml->setAttributeValid("tag",toString());
    return xml;
}

void JGCrypto::fromXML(const XMLElement* xml)
{
    if (!xml)
	return;
    m_suite = xml->getAttribute("crypto-suite");
    m_keyParams = xml->getAttribute("key-params");
    m_sessionParams = xml->getAttribute("session-params");
    assign(xml->getAttribute("tag"));
}


/**
 * JGRtpMediaList
 */

TokenDict JGRtpMediaList::s_media[] = {
    {"audio",     Audio},
    {0,0}
};

// Find a data payload by its id
JGRtpMedia* JGRtpMediaList::findMedia(const String& id)
{
    ObjList* obj = find(id);
    return obj ? static_cast<JGRtpMedia*>(obj->get()) : 0;
}

// Find a data payload by its synonym
JGRtpMedia* JGRtpMediaList::findSynonym(const String& value) const
{
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGRtpMedia* a = static_cast<JGRtpMedia*>(o->get());
	if (value == a->m_synonym)
	    return a;
    }
    return 0;
}

// Create a 'description' element and add payload children to it
XMLElement* JGRtpMediaList::toXML(bool telEvent) const
{
    if (m_media != Audio)
	return 0;
    XMLElement* desc = XMPPUtils::createElement(XMLElement::Description,
	XMPPNamespace::JingleAppsRtp);
    desc->setAttributeValid("media",lookup(m_media,s_media));
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGRtpMedia* a = static_cast<JGRtpMedia*>(o->get());
	desc->addChild(a->toXML());
    }
    if (telEvent) {
	JGRtpMedia* te = new JGRtpMedia("106","telephone-event","8000","","");
	desc->addChild(te->toXML());
	TelEngine::destruct(te);
    }
    ObjList* c = m_cryptoLocal.skipNull();
    if (c) {
	if (m_cryptoMandatory)
	    desc->addChild(new XMLElement(XMLElement::CryptoRequired));
	for (; c; c = c->skipNext())
	    desc->addChild((static_cast<JGCrypto*>(c->get()))->toXML());
    }
    return desc;
}

// Fill this list from an XML element's children. Clear before attempting to fill
void JGRtpMediaList::fromXML(XMLElement* xml)
{
    clear();
    m_cryptoMandatory = false;
    m_cryptoRemote.clear();
    if (!xml)
	return;
    m_media = (Media)lookup(xml->getAttribute("media"),s_media,MediaUnknown);
    XMLElement* m = xml->findFirstChild(XMLElement::PayloadType);
    for (; m; m = xml->findNextChild(m,XMLElement::PayloadType))
	ObjList::append(new JGRtpMedia(m));
    // Check crypto
    XMLElement* c = xml->findFirstChild(XMLElement::Crypto);
    if (c) {
	XMLElement* mandatory = xml->findFirstChild(XMLElement::CryptoRequired);
	if (mandatory) {
	    m_cryptoMandatory = true;
	    TelEngine::destruct(mandatory);
	}
	for (; c; c = xml->findNextChild(c,XMLElement::Crypto))
	    m_cryptoRemote.append(new JGCrypto(c));
    }
}

// Create a list from data payloads
bool JGRtpMediaList::createList(String& dest, bool synonym, const char* sep)
{
    dest = "";
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGRtpMedia* a = static_cast<JGRtpMedia*>(o->get());
	dest.append(synonym ? a->m_synonym : a->m_name,sep);
    }
    return (0 != dest.length());
}


/**
 * JGRtpCandidate
 */

// Create a 'candidate' element from this object
XMLElement* JGRtpCandidate::toXml(const JGRtpCandidates& container) const
{
    if (container.m_type == JGRtpCandidates::Unknown)
	return 0;

    XMLElement* xml = new XMLElement(XMLElement::Candidate);

    xml->setAttributeValid("component",m_component);
    xml->setAttributeValid("generation",m_generation);

    if (container.m_type == JGRtpCandidates::RtpIceUdp)
	xml->setAttributeValid("foundation",toString());
    else if (container.m_type == JGRtpCandidates::RtpRawUdp)
	xml->setAttributeValid("id",toString());

    xml->setAttributeValid("ip",m_address);
    xml->setAttributeValid("port",m_port);

    if (container.m_type == JGRtpCandidates::RtpIceUdp) {
	xml->setAttributeValid("network",m_network);
	xml->setAttributeValid("priority",m_priority);
	xml->setAttributeValid("protocol",m_protocol);
	xml->setAttributeValid("type",m_type);
    }
    return xml;
}

// Fill this object from a candidate element
void JGRtpCandidate::fromXml(XMLElement* xml, const JGRtpCandidates& container)
{
    if (!xml || container.m_type == JGRtpCandidates::Unknown)
	return;

    if (container.m_type == JGRtpCandidates::RtpIceUdp)
	assign(xml->getAttribute("foundation"));
    else if (container.m_type == JGRtpCandidates::RtpRawUdp)
	assign(xml->getAttribute("id"));

    m_component = xml->getAttribute("component");
    m_generation = xml->getAttribute("generation");
    m_address = xml->getAttribute("ip");
    m_port = xml->getAttribute("port");
    if (container.m_type == JGRtpCandidates::RtpIceUdp) {
	m_network = xml->getAttribute("network");
	m_priority = xml->getAttribute("priority");
	m_protocol = xml->getAttribute("protocol");
	m_type = xml->getAttribute("type");
    }
}


/**
 * JGRtpCandidates
 */

TokenDict JGRtpCandidates::s_type[] = {
    {"ice-udp", RtpIceUdp},
    {"raw-udp", RtpRawUdp},
    {0,0},
};

// Create a 'transport' element from this object. Add 
XMLElement* JGRtpCandidates::toXML(bool addCandidates, bool addAuth) const
{
    XMPPNamespace::Type ns;
    if (m_type == RtpIceUdp)
	ns = XMPPNamespace::JingleTransportIceUdp;
    else if (m_type == RtpRawUdp)
	ns = XMPPNamespace::JingleTransportRawUdp;
    else
	return 0;
    XMLElement* trans = XMPPUtils::createElement(XMLElement::Transport,ns);
    if (addAuth && m_type == RtpIceUdp) {
	trans->setAttributeValid("pwd",m_password);
	trans->setAttributeValid("ufrag",m_ufrag);
    }
    if (addCandidates)
	for (ObjList* o = skipNull(); o; o = o->skipNext())
	    trans->addChild((static_cast<JGRtpCandidate*>(o->get()))->toXml(*this));
    return trans;
}

// Fill this object from a given element
void JGRtpCandidates::fromXML(XMLElement* element)
{
    clear();
    m_type = Unknown;
    m_password = "";
    m_ufrag = "";
    if (!element)
	return;
    // Set transport data
    if (XMPPUtils::hasXmlns(*element,XMPPNamespace::JingleTransportIceUdp))
	m_type = RtpIceUdp;
    else if (XMPPUtils::hasXmlns(*element,XMPPNamespace::JingleTransportRawUdp))
	m_type = RtpRawUdp;
    else
	return;
    m_password = element->getAttribute("pwd");
    m_ufrag = element->getAttribute("ufrag");
    // Get candidates
    XMLElement* c = element->findFirstChild(XMLElement::Candidate);
    for (; c; c = element->findNextChild(c,XMLElement::Candidate))
	append(new JGRtpCandidate(c,*this));
}

// Find a candidate by its component value
JGRtpCandidate* JGRtpCandidates::findByComponent(unsigned int component)
{
    String tmp = component;
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGRtpCandidate* c = static_cast<JGRtpCandidate*>(o->get());
	if (c->m_component == tmp)
	    return c;
    }
    return 0;
}

// Generate a random password to be used with ICE-UDP transport
// Maximum number of characters. The maxmimum value is 256.
// The minimum value is 22 for password and 4 for username
void JGRtpCandidates::generateIceToken(String& dest, bool pwd, unsigned int max)
{
    if (pwd) {
	if (max < 22)
	    max = 22;
    }
    else if (max < 4)
	max = 4;
    if (max > 256)
	max = 256;
    dest = "";
    while (dest.length() < max)
 	dest << (int)random();
    dest = dest.substr(0,max);
}


/**
 * JGSessionContent
 */

// The list containing the text values for Senders enumeration
TokenDict JGSessionContent::s_senders[] = {
    {"both",       SendBoth},
    {"initiator",  SendInitiator},
    {"responder",  SendResponder},
    {0,0}
};

// The list containing the text values for Creator enumeration
TokenDict JGSessionContent::s_creator[] = {
    {"initiator",  CreatorInitiator},
    {"responder",  CreatorResponder},
    {0,0}
};

// Constructor
JGSessionContent::JGSessionContent(const char* name, Senders senders,
    Creator creator, const char* disposition)
    : m_name(name), m_senders(senders), m_creator(creator),
    m_disposition(disposition)
{
}

// Build a 'content' XML element from this object
XMLElement* JGSessionContent::toXml(bool minimum, bool addDesc,
    bool addTrans, bool addCandidates, bool addAuth) const
{
    XMLElement* xml = new XMLElement(XMLElement::Content);
    xml->setAttributeValid("name",m_name);
    xml->setAttributeValid("creator",lookup(m_creator,s_creator));
    if (!minimum) {
	xml->setAttributeValid("senders",lookup(m_senders,s_senders));
	xml->setAttributeValid("disposition",m_disposition);
    }
    // Add description and transport
    if (addDesc)
	xml->addChild(m_rtpMedia.toXML());
    if (addTrans)
	xml->addChild(m_rtpLocalCandidates.toXML(addCandidates,addAuth));
    return xml;
}

// Build a content object from an XML element
JGSessionContent* JGSessionContent::fromXml(XMLElement* xml, XMPPError::Type& err,
	String& error)
{
    static const char* errAttr = "Required attribute is missing: ";
    static const char* errAttrValue = "Invalid attribute value: ";

    if (!xml) {
	err = XMPPError::SInternal;
	return 0;
    }

    err = XMPPError::SNotAcceptable;

    const char* name = xml->getAttribute("name");
    if (!(name && *name)) {
	error << errAttr << "name";
	return 0;
    }
    // Creator (default: initiator)
    Creator creator = CreatorInitiator;
    const char* tmp = xml->getAttribute("creator");
    if (tmp)
	creator = (Creator)lookup(tmp,s_creator,CreatorUnknown);
    if (creator == CreatorUnknown) {
	error << errAttrValue << "creator";
	return 0;
    }
    // Senders (default: both)
    Senders senders = SendBoth;
    tmp = xml->getAttribute("senders");
    if (tmp)
	senders = (Senders)lookup(tmp,s_senders,SendUnknown);
    if (senders == SendUnknown) {
	error << errAttrValue << "senders";
	return 0;
    }

    JGSessionContent* content = new JGSessionContent(name,senders,creator,
	xml->getAttribute("disposition"));
    XMLElement* desc = 0;
    XMLElement* trans = 0;
    err = XMPPError::NoError;
    // Use a while() to go to end and cleanup data
    while (true) {
	// Check description
	desc = xml->findFirstChild(XMLElement::Description);
	if (desc) {
	    if (XMPPUtils::hasXmlns(*desc,XMPPNamespace::JingleAppsRtp))
		content->m_rtpMedia.fromXML(desc);
	    else
		content->m_rtpMedia.m_media = JGRtpMediaList::MediaUnknown;
	}
	else
	    content->m_rtpMedia.m_media = JGRtpMediaList::MediaMissing;

	// Check transport
	trans = xml->findFirstChild(XMLElement::Transport);
	if (trans)
	    content->m_rtpRemoteCandidates.fromXML(trans);
	else
	    content->m_rtpRemoteCandidates.m_type = JGRtpCandidates::Unknown;

	break;
    }

    TelEngine::destruct(desc);
    TelEngine::destruct(trans);
    if (err == XMPPError::NoError)
	return content;
    TelEngine::destruct(content);
    return 0;
}


/**
 * JGSession
 */

TokenDict JGSession::s_states[] = {
    {"Idle",     Idle},
    {"Pending",  Pending},
    {"Active",   Active},
    {"Ending",   Ending},
    {"Destroy",  Destroy},
    {0,0}
};

TokenDict JGSession::s_actions[] = {
    {"session-accept",        ActAccept},
    {"session-initiate",      ActInitiate},
    {"session-terminate",     ActTerminate},
    {"session-info",          ActInfo},
    {"transport-info",        ActTransportInfo},
    {"transport-accept",      ActTransportAccept},
    {"transport-reject",      ActTransportReject},
    {"transport-replace",     ActTransportReplace},
    {"content-accept",        ActContentAccept},
    {"content-add",           ActContentAdd},
    {"content-modify",        ActContentModify},
    {"content-reject",        ActContentReject},
    {"content-remove",        ActContentRemove},
    {"session-transfer",      ActTransfer},
    {"DTMF",                  ActDtmf},
    {"ringing",               ActRinging},
    {"trying",                ActTrying},
    {"received",              ActReceived},
    {"hold",                  ActHold},
    {"active",                ActActive},
    {"mute",                  ActMute},
    {0,0}
};

TokenDict JGSession::s_reasons[] = {
    {"busy",                     ReasonBusy},
    {"decline",                  ReasonDecline},
    {"connectivity-error",       ReasonConn},
    {"media-error",              ReasonMedia},
    {"unsupported-transports",   ReasonTransport},
    {"no-error",                 ReasonNoError},
    {"success",                  ReasonOk},
    {"unsupported-applications", ReasonNoApp},
    {"alternative-session",      ReasonAltSess},
    {"general-error",            ReasonUnknown},
    {"transferred",              ReasonTransfer},
    {0,0}
};

// Create an outgoing session
JGSession::JGSession(JGEngine* engine, JBStream* stream,
	const String& callerJID, const String& calledJID,
	const ObjList& contents, XMLElement* extra, const char* msg)
    : Mutex(true),
    m_state(Idle),
    m_engine(engine),
    m_stream(0),
    m_outgoing(true),
    m_localJID(callerJID),
    m_remoteJID(calledJID),
    m_lastEvent(0),
    m_private(0),
    m_stanzaId(1)
{
    if (stream && stream->ref())
	m_stream = stream;
    m_engine->createSessionId(m_localSid);
    m_sid = m_localSid;
    Debug(m_engine,DebugAll,"Call(%s). Outgoing msg=%s [%p]",m_sid.c_str(),msg,this);
    if (msg)
	sendMessage(msg);
    XMLElement* xml = createJingle(ActInitiate);
    addJingleContents(xml,contents,false,true,true,true);
    addJingleChild(xml,extra);
    if (sendStanza(xml))
	changeState(Pending);
    else
	changeState(Destroy);
}

// Create an incoming session
JGSession::JGSession(JGEngine* engine, JBEvent* event, const String& id)
    : Mutex(true),
    m_state(Idle),
    m_engine(engine),
    m_stream(0),
    m_outgoing(false),
    m_sid(id),
    m_lastEvent(0),
    m_private(0),
    m_stanzaId(1)
{
    if (event->stream() && event->stream()->ref())
	m_stream = event->stream();
    m_events.append(event);
    m_engine->createSessionId(m_localSid);
    Debug(m_engine,DebugAll,"Call(%s). Incoming [%p]",m_sid.c_str(),this);
}

// Destructor: hangup, cleanup, remove from engine's list
JGSession::~JGSession()
{
    XDebug(m_engine,DebugAll,"JGSession::~JGSession() [%p]",this);
}

// Release this session and its memory
void JGSession::destroyed()
{
    lock();
    // Cancel pending outgoing. Hangup. Cleanup
    if (m_stream) {
	m_stream->removePending(m_localSid,false);
	hangup(ReasonUnknown);
	TelEngine::destruct(m_stream);
    }
    m_events.clear();
    unlock();
    // Remove from engine
    Lock lock(m_engine);
    m_engine->m_sessions.remove(this,false);
    lock.drop();
    DDebug(m_engine,DebugInfo,"Call(%s). Destroyed [%p]",m_sid.c_str(),this);
}

// Ask this session to accept an event
bool JGSession::acceptEvent(JBEvent* event, const String& sid)
{
    if (!event)
	return false;

    // Requests must match the session id
    // Responses' id must start with session's local id (this is the way we generate the stanza id)
    if (sid) {
	if (sid != m_sid)
	    return false;
    }
    else if (!event->id().startsWith(m_localSid))
	return false;
    // Check to/from
    if (m_localJID != event->to() || m_remoteJID != event->from())
	return false;

    // Ok: keep a referenced event
    if (event->ref())
	enqueue(event);
    return true;
}

// Accept a Pending incoming session
bool JGSession::accept(const ObjList& contents, String* stanzaId)
{
    Lock lock(this);
    if (outgoing() || state() != Pending)
	return false;
    XMLElement* xml = createJingle(ActAccept);
    addJingleContents(xml,contents,false,true,true,true,true);
    if (!sendStanza(xml,stanzaId))
	return false;
    changeState(Active);
    return true;
}

// Close a Pending or Active session
bool JGSession::hangup(int reason, const char* msg)
{
    Lock lock(this);
    if (state() != Pending && state() != Active)
	return false;
    DDebug(m_engine,DebugAll,"Call(%s). Hangup('%s') [%p]",m_sid.c_str(),msg,this);
    // Clear sent stanzas list. We will wait for this element to be confirmed
    m_sentStanza.clear();
    const char* tmp = lookupReason(reason);
    XMLElement* res = 0;
    if (tmp || msg) {
	res = new XMLElement(XMLElement::Reason);
	if (tmp)
	    res->addChild(new XMLElement(tmp));
	if (msg)
	    res->addChild(new XMLElement(XMLElement::Text,0,msg));
    }
    XMLElement* xml = createJingle(ActTerminate,res);
    bool ok = sendStanza(xml);
    changeState(Ending);
    return ok;
}

// Send a stanza with session content(s)
bool JGSession::sendContent(Action action, const ObjList& contents, String* stanzaId)
{
    Lock lock(this);
    if (state() != Pending && state() != Active)
	return false;
    // XEP-0176 5.2: add ICE auth only for content-add, transport-replace, transport-info
    bool addIceAuth = false;
    bool addCandidates = false;
    bool minimal = false;
    bool addDesc = true;
    bool addTrans = true;
    switch (action) {
	case ActContentAdd:
	    addCandidates = true;
	    addIceAuth = true;
	    break;
	case ActTransportInfo:
	    addCandidates = true;
	    addIceAuth = true;
	    addDesc = false;
	    break;
	case ActTransportReplace:
	    addIceAuth = true;
	    break;
	case ActTransportAccept:
	case ActTransportReject:
	case ActContentAccept:
	case ActContentModify:
	    break;
	case ActContentReject:
	case ActContentRemove:
	    minimal = true;
	    addDesc = false;
	    addTrans = false;
	    break;
	default:
	    return false;
    };
    // Make sure we dont't terminate the session on failure
    String tmp;
    if (!stanzaId) {
	tmp = "Content" + String(Time::secNow());
	stanzaId = &tmp;
    }
    XMLElement* xml = createJingle(action);
    addJingleContents(xml,contents,minimal,addDesc,addTrans,addCandidates,addIceAuth);
    return sendStanza(xml,stanzaId);
}

// Confirm a received element. If the error is NoError a result stanza will be sent
// Otherwise, an error stanza will be created and sent
bool JGSession::confirm(XMLElement* xml, XMPPError::Type error,
	const char* text, XMPPError::ErrorType type)
{
    if (!xml)
	return false;
    XMLElement* iq = 0;
    if (error == XMPPError::NoError) {
	String id = xml->getAttribute("id");
	iq = XMPPUtils::createIq(XMPPUtils::IqResult,m_localJID,m_remoteJID,id);
	// The receiver will detect which stanza is confirmed by id
	// If missing, make a copy of the received element and attach it to the error
	if (!id) {
	    XMLElement* copy = new XMLElement(*xml);
	    iq->addChild(copy);
	}
    }
    else
	iq = XMPPUtils::createError(xml,type,error,text);
    return sendStanza(iq,0,false);
}

// Send a dtmf string to remote peer
bool JGSession::sendDtmf(const char* dtmf, unsigned int msDuration, String* stanzaId)
{
    if (!(dtmf && *dtmf))
	return false;

    XMLElement* iq = createJingle(ActInfo);
    XMLElement* sess = iq->findFirstChild();
    if (!sess) {
	TelEngine::destruct(iq);
	return false;
    }
    char s[2] = {0,0};
    while (*dtmf) {
	s[0] = *dtmf++;
	XMLElement* xml = XMPPUtils::createElement(XMLElement::Dtmf,XMPPNamespace::Dtmf);
	xml->setAttribute("code",s);
	if (msDuration)
	    xml->setAttribute("duration",String(msDuration));
	sess->addChild(xml);
    }
    TelEngine::destruct(sess);
    return sendStanza(iq,stanzaId);
}

// Send a session info element to the remote peer
bool JGSession::sendInfo(XMLElement* xml, String* stanzaId)
{
    if (!xml)
	return false;
    // Make sure we dont't terminate the session if info fails
    String tmp;
    if (!stanzaId) {
	tmp = "Info" + String(Time::secNow());
	stanzaId = &tmp;
    }
    return sendStanza(createJingle(ActInfo,xml),stanzaId);
}

// Check if the remote party supports a given feature
bool JGSession::hasFeature(XMPPNamespace::Type feature)
{
    if (!m_stream)
	return false;
    JBClientStream* cStream = static_cast<JBClientStream*>(m_stream->getObject("JBClientStream"));
    if (cStream) {
	XMPPUser* user = cStream->getRemote(remote());
	if (!user)
	    return false;
	bool ok = false;
	user->lock();
	JIDResource* res = user->remoteRes().get(remote().resource());
	ok = res && res->features().get(feature);
	user->unlock();
	TelEngine::destruct(user);
	return ok;
    }
    return false;
}

// Build a transfer element
XMLElement* JGSession::buildTransfer(const String& transferTo,
    const String& transferFrom, const String& sid)
{
    XMLElement* transfer = XMPPUtils::createElement(XMLElement::Transfer,
	XMPPNamespace::JingleTransfer);
    transfer->setAttributeValid("from",transferFrom);
    transfer->setAttributeValid("to",transferTo);
    transfer->setAttributeValid("sid",sid);
    return transfer;
}

// Enqueue a Jabber engine event
void JGSession::enqueue(JBEvent* event)
{
    Lock lock(this);
    if (event->type() == JBEvent::Terminated || event->type() == JBEvent::Destroy)
	m_events.insert(event);
    else
	m_events.append(event);
    DDebug(m_engine,DebugAll,"Call(%s). Accepted event (%p,%s) [%p]",
	m_sid.c_str(),event,event->name(),this);
}

// Process received events. Generate Jingle events
JGEvent* JGSession::getEvent(u_int64_t time)
{
    Lock lock(this);
    if (m_lastEvent)
	return 0;
    if (state() == Destroy)
	return 0;
    // Deque and process event(s)
    // Loop until a jingle event is generated or no more events in queue
    JBEvent* jbev = 0;
    while (true) {
	TelEngine::destruct(jbev);
	jbev = static_cast<JBEvent*>(m_events.remove(false));
	if (!jbev)
	    break;

	DDebug(m_engine,DebugAll,
	    "Call(%s). Dequeued Jabber event (%p,%s) in state %s [%p]",
	    m_sid.c_str(),jbev,jbev->name(),lookupState(state()),this);

	// Process Jingle 'set' stanzas
	if (jbev->type() == JBEvent::IqJingleSet) {
	    // Filter some conditions in which we can't accept any jingle stanza
	    // Outgoing idle sessions are waiting for the user to initiate them
	    if (state() == Idle && outgoing()) {
		confirm(jbev->releaseXML(),XMPPError::SRequest);
		continue;
	    }

	    m_lastEvent = decodeJingle(jbev);

	    if (!m_lastEvent) {
		// Destroy incoming session if session initiate stanza contains errors
		if (!outgoing() && state() == Idle) {
		    m_lastEvent = new JGEvent(JGEvent::Destroy,this,0,"failure");
		    // TODO: hangup
		    break;
		}
		continue;
	    }

	    // ActInfo: empty session info
	    if (m_lastEvent->action() == ActInfo) {
	        XDebug(m_engine,DebugAll,"Call(%s). Received empty '%s' (ping) [%p]",
		    m_sid.c_str(),lookup(m_lastEvent->action(),s_actions),this);
		confirm(m_lastEvent->element());
		delete m_lastEvent;
		m_lastEvent = 0;
		continue;
	    }

	    DDebug(m_engine,DebugInfo,
		"Call(%s). Processing action (%u,'%s') state=%s [%p]",
		m_sid.c_str(),m_lastEvent->action(),
		lookup(m_lastEvent->action(),s_actions),lookupState(state()),this);

	    // Check for termination events
	    if (m_lastEvent->final())
		break;

	    bool error = false;
	    bool fatal = false;
	    switch (state()) {
		case Active:
		    error = m_lastEvent->action() == ActAccept ||
			m_lastEvent->action() == ActInitiate ||
			m_lastEvent->action() == ActRinging;
		    break;
		case Pending:
		    // Accept session-accept, transport, content and ringing stanzas
		    switch (m_lastEvent->action()) {
			case ActAccept:
			    if (outgoing()) {
				// XEP-0166 7.2.6: responder may be overridden
				if (m_lastEvent->jingle()) {
				    JabberID rsp = m_lastEvent->jingle()->getAttribute("responder");
				    if (!rsp.null() && m_remoteJID != rsp) {
					m_remoteJID.set(rsp);
					Debug(m_engine,DebugInfo,
					    "Call(%s). Remote jid changed to '%s' [%p]",
					    m_sid.c_str(),rsp.c_str(),this);
				    }
				}
				changeState(Active);
			    }
			    else
				error = true;
			    break;
			case ActTransportInfo:
			case ActTransportAccept:
			case ActTransportReject:
			case ActTransportReplace:
			case ActContentAccept:
			case ActContentAdd:
			case ActContentModify:
			case ActContentReject:
			case ActContentRemove:
			case ActInfo:
			case ActRinging:
			case ActTrying:
			case ActReceived:
			    break;
			default:
			    error = true;
		    }
		    break;
		case Idle:
		    // Update data. Terminate if not a session initiating event
		    if (m_lastEvent->action() == ActInitiate) {
			m_localJID.set(jbev->to());
			m_remoteJID.set(jbev->from());
			changeState(Pending);
		    }
		    else
			error = fatal = true;
		    break;
		default:
		    error = true;
	    }

	    if (!error) {
		// Don't confirm actions that need session user's interaction
		switch (m_lastEvent->action()) {
		    case ActInitiate:
		    case ActTransportInfo:
		    case ActTransportAccept:
		    case ActTransportReject:
		    case ActTransportReplace:
		    case ActContentAccept:
		    case ActContentAdd:
		    case ActContentModify:
		    case ActContentReject:
		    case ActContentRemove:
		    case ActTransfer:
		    case ActRinging:
		    case ActHold:
		    case ActActive:
		    case ActMute:
		    case ActTrying:
		    case ActReceived:
			break;
		    default:
			confirm(m_lastEvent->element());
		}
	    }
	    else {
		confirm(m_lastEvent->releaseXML(),XMPPError::SRequest);
		delete m_lastEvent;
		m_lastEvent = 0;
		if (fatal)
		    m_lastEvent = new JGEvent(JGEvent::Destroy,this);
		else
		    continue;
	    }
	    break;
	}

	// Check for responses or failures
	bool response = jbev->type() == JBEvent::IqJingleRes ||
			jbev->type() == JBEvent::IqJingleErr ||
			jbev->type() == JBEvent::IqResult ||
			jbev->type() == JBEvent::IqError ||
			jbev->type() == JBEvent::WriteFail;
	while (response) {
	    JGSentStanza* sent = 0;
	    // Find a sent stanza to match the event's id
	    for (ObjList* o = m_sentStanza.skipNull(); o; o = o->skipNext()) {
		sent = static_cast<JGSentStanza*>(o->get());
		if (jbev->id() == *sent)
		    break;
		sent = 0;
	    }
	    if (!sent)
		break;

	    // Check termination conditions
	    // Always terminate when receiving responses in Ending state
	    bool terminateEnding = (state() == Ending);
	    // Terminate pending outgoing if no notification required
	    // (Initial session request is sent without notification required)
	    bool terminatePending = false;
	    if (state() == Pending && outgoing() &&
		(jbev->type() == JBEvent::IqJingleErr || jbev->type() == JBEvent::WriteFail))
		terminatePending = !sent->notify();
	    // Write fail: Terminate if failed stanza is a Jingle one and the sender
	    //  didn't requested notification
	    bool terminateFail = false;
	    if (!(terminateEnding || terminatePending) && jbev->type() == JBEvent::WriteFail)
		terminateFail = !sent->notify();

	    // Generate event
	    if (terminateEnding)
		m_lastEvent = new JGEvent(JGEvent::Destroy,this);
	    else if (terminatePending || terminateFail)
		m_lastEvent = new JGEvent(JGEvent::Terminated,this,
		    jbev->type() != JBEvent::WriteFail ? jbev->releaseXML() : 0,
		    jbev->text() ? jbev->text().c_str() : "failure");
	    else if (sent->notify())
		switch (jbev->type()) {
		    case JBEvent::IqJingleRes:
		    case JBEvent::IqResult:
			m_lastEvent = new JGEvent(JGEvent::ResultOk,this,
			    jbev->releaseXML());
			break;
		    case JBEvent::IqJingleErr:
		    case JBEvent::IqError:
			m_lastEvent = new JGEvent(JGEvent::ResultError,this,
			    jbev->releaseXML(),jbev->text());
			break;
		    case JBEvent::WriteFail:
			m_lastEvent = new JGEvent(JGEvent::ResultWriteFail,this,
			    jbev->releaseXML(),jbev->text());
			break;
		    default:
			DDebug(m_engine,DebugStub,
			    "Call(%s). Unhandled response event (%p,%u,%s) [%p]",
			    m_sid.c_str(),jbev,jbev->type(),jbev->name(),this);
		}
	    if (m_lastEvent && !m_lastEvent->m_id)
		m_lastEvent->m_id = *sent;
	    m_sentStanza.remove(sent,true);

	    String error;
#ifdef DEBUG
	    if (jbev->type() == JBEvent::IqJingleErr && jbev->text())
		error << " (error='" << jbev->text() << "')";
#endif
	    bool terminate = (m_lastEvent && m_lastEvent->final());
	    Debug(m_engine,DebugAll,
		"Call(%s). Sent element with id=%s confirmed by event=%s%s%s [%p]",
		m_sid.c_str(),jbev->id().c_str(),jbev->name(),error.safe(),
		terminate ? ". Terminating": "",this);

	    // Gracefully terminate
	    if (terminate && state() != Ending)
		hangup(ReasonUnknown);

	    break;
	}
	if (response)
	    if (!m_lastEvent)
		continue;
	    else
		break;

	// Silently ignore temporary stream down
	if (jbev->type() == JBEvent::Terminated) {
	    DDebug(m_engine,DebugInfo,
		"Call(%s). Stream disconnected in state %s [%p]",
		m_sid.c_str(),lookupState(state()),this);
	    continue;
	}

	// Terminate on stream destroy
	if (jbev->type() == JBEvent::Destroy) {
	    Debug(m_engine,DebugInfo,
		"Call(%s). Stream destroyed in state %s [%p]",
		m_sid.c_str(),lookupState(state()),this);
	    m_lastEvent = new JGEvent(JGEvent::Terminated,this,0,"noconn");
	    break;
	}

	Debug(m_engine,DebugStub,"Call(%s). Unhandled event type %u '%s' [%p]",
	    m_sid.c_str(),jbev->type(),jbev->name(),this);
	continue;
    }
    TelEngine::destruct(jbev);

    // No event: check first sent stanza's timeout
    if (!m_lastEvent) {
	ObjList* o = m_sentStanza.skipNull();
	JGSentStanza* tmp = o ? static_cast<JGSentStanza*>(o->get()) : 0;
	while (tmp && tmp->timeout(time)) {
	    Debug(m_engine,DebugNote,"Call(%s). Sent stanza ('%s') timed out [%p]",
		m_sid.c_str(),tmp->c_str(),this);
	    // Don't terminate if the sender requested to be notified
	    m_lastEvent = new JGEvent(tmp->notify() ? JGEvent::ResultTimeout : JGEvent::Terminated,
		this,0,"timeout");
	    m_lastEvent->m_id = *tmp;
	    o->remove();
	    if (m_lastEvent->final())
		hangup(false,"Timeout");
	    break;
	}
    }

    if (m_lastEvent) {
	// Deref the session for final events
	if (m_lastEvent->final()) {
	    changeState(Destroy);
	    deref();
	}
	DDebug(m_engine,DebugAll,
	    "Call(%s). Raising event (%p,%u) action=%s final=%s [%p]",
	    m_sid.c_str(),m_lastEvent,m_lastEvent->type(),
	    m_lastEvent->actionName(),String::boolText(m_lastEvent->final()),this);
	return m_lastEvent;
    }

    return 0;
}

// Send a stanza to the remote peer
bool JGSession::sendStanza(XMLElement* stanza, String* stanzaId, bool confirmation)
{
    Lock lock(this);
    if (!(state() != Ending && state() != Destroy && stanza && m_stream)) {
#ifdef DEBUG
	Debug(m_engine,DebugNote,
	    "Call(%s). Can't send stanza (%p,'%s') in state %s [%p]",
	    m_sid.c_str(),stanza,stanza->name(),lookupState(m_state),this);
#endif
	TelEngine::destruct(stanza);
	return false;
    }
    DDebug(m_engine,DebugAll,"Call(%s). Sending stanza (%p,'%s') id=%s [%p]",
	m_sid.c_str(),stanza,stanza->name(),String::boolText(stanzaId != 0),this);
    const char* senderId = m_localSid;
    // Check if the stanza should be added to the list of stanzas requiring confirmation
    if (confirmation && stanza->type() == XMLElement::Iq) {
	String id = m_localSid;
	id << "_" << (unsigned int)m_stanzaId++;
	JGSentStanza* sent = new JGSentStanza(id,
	    m_engine->stanzaTimeout() + Time::msecNow(),stanzaId != 0);
	stanza->setAttribute("id",*sent);
	senderId = *sent;
	if (stanzaId)
	    *stanzaId = *sent;
	m_sentStanza.append(sent);
    }
    // Send. If it fails leave it in the sent items to timeout
    JBStream::Error res = m_stream->sendStanza(stanza,senderId);
    if (res == JBStream::ErrorNoSocket || res == JBStream::ErrorContext)
	return false;
    return true;
}

// Decode a jingle stanza
JGEvent* JGSession::decodeJingle(JBEvent* jbev)
{
    XMLElement* jingle = jbev->child();
    if (!jingle) {
	confirm(jbev->releaseXML(),XMPPError::SBadRequest);
	return 0;
    }

    Action act = (Action)lookup(jingle->getAttribute("type"),s_actions,ActCount);
    if (act == ActCount) {
	confirm(jbev->releaseXML(),XMPPError::SServiceUnavailable,
	    "Unknown session action");
	return 0;
    }

    // *** Check some actions without any processing
    if (act == ActTransfer)
	return new JGEvent(act,this,jbev->releaseXML());

    // *** ActTerminate
    if (act == ActTerminate) {
	// Confirm here: this is a final event, 
	//  stanza won't be confirmed in getEvent()
	confirm(jbev->element());
	const char* reason = 0;
	const char* text = 0;
	XMLElement* res = jingle->findFirstChild(XMLElement::Reason);
	if (res) {
	    XMLElement* tmp = res->findFirstChild();
	    if (tmp && tmp->type() != XMLElement::Text)
		reason = tmp->name();
	    TelEngine::destruct(tmp);
	    tmp = res->findFirstChild(XMLElement::Text);
	    if (tmp)
		text = tmp->getText();
	    TelEngine::destruct(tmp);
	    TelEngine::destruct(res);
	}
	if (!reason)
	    reason = act==ActTerminate ? "hangup" : "rejected";
	return new JGEvent(JGEvent::Terminated,this,jbev->releaseXML(),reason,text);
    }

    // *** ActInfo
    if (act == ActInfo) {
        // Check info element
	// Return ActInfo event to signal ping (XEP-0166 6.8)
	XMLElement* child = jingle->findFirstChild();
	if (!child)
	    return new JGEvent(ActInfo,this,jbev->releaseXML());

	JGEvent* event = 0;
	Action a = ActCount;
	XMPPNamespace::Type ns = XMPPNamespace::Count;
	// Check namespace and build event
	switch (child->type()) {
	    case XMLElement::Dtmf:
		a = ActDtmf;
		ns = XMPPNamespace::Dtmf;
		break;
	    case XMLElement::Transfer:
		a = ActTransfer;
		ns = XMPPNamespace::JingleTransfer;
		break;
	    case XMLElement::Hold:
		a = ActHold;
		ns = XMPPNamespace::JingleAppsRtpInfo;
		break;
	    case XMLElement::Active:
		a = ActActive;
		ns = XMPPNamespace::JingleAppsRtpInfo;
		break;
	    case XMLElement::Ringing:
		a = ActRinging;
		ns = XMPPNamespace::JingleAppsRtpInfo;
		break;
	    case XMLElement::Trying:
		a = ActTrying;
		ns = XMPPNamespace::JingleTransportRawUdpInfo;
		break;
	    case XMLElement::Received:
		a = ActReceived;
		ns = XMPPNamespace::JingleTransportRawUdpInfo;
		break;
	    case XMLElement::Mute:
		a = ActMute;
		ns = XMPPNamespace::JingleAppsRtpInfo;
		break;
	    default: ;
	}
	if (a != ActCount && XMPPUtils::hasXmlns(*child,ns)) {
	    String text;
	    // Add Dtmf
	    if (a == ActDtmf) {
		// Expect more then 1 'dtmf' child
		for (; child; child = jingle->findNextChild(child,XMLElement::Dtmf))
		    text << child->getAttribute("code");
		if (!text) {
		    confirm(jbev->releaseXML(),XMPPError::SBadRequest,"Empty dtmf(s)");
		    return 0;
		}
	    }
	    event = new JGEvent(a,this,jbev->releaseXML(),"",text);
	}
	else
	    confirm(jbev->releaseXML(),XMPPError::SFeatureNotImpl);
        TelEngine::destruct(child);
	return event;
    }

    // *** Elements carrying contents
    switch (act) {
	case ActTransportInfo:
	case ActTransportAccept:
	case ActTransportReject:
	case ActTransportReplace:
	case ActContentAccept:
	case ActContentAdd:
	case ActContentModify:
	case ActContentReject:
	case ActContentRemove:
	case ActInitiate:
	case ActAccept:
	    break;
	default:
	    confirm(jbev->releaseXML(),XMPPError::SServiceUnavailable);
	    return 0;
    }

    JGEvent* event = new JGEvent(act,this,jbev->releaseXML());
    jingle = event->jingle();
    if (!jingle) {
	confirm(event->releaseXML(),XMPPError::SInternal);
	delete event;
	return 0;
    }
    XMPPError::Type err = XMPPError::NoError;
    String text;
    XMLElement* c = jingle->findFirstChild(XMLElement::Content);
    for (; c; c = jingle->findNextChild(c,XMLElement::Content)) {
	JGSessionContent* content = JGSessionContent::fromXml(c,err,text);
	if (content) {
	    DDebug(m_engine,DebugAll,
		"Call(%s). Found content='%s' in '%s' stanza [%p]",
		m_sid.c_str(),content->toString().c_str(),event->actionName(),this);
	    event->m_contents.append(content);
	    continue;
	}
	if (err == XMPPError::NoError) {
	    DDebug(m_engine,DebugAll,
		"Call(%s). Ignoring content='%s' in '%s' stanza [%p]",
		m_sid.c_str(),c->getAttribute("name"),event->actionName(),this);
	    continue;
	}
	// Error
	TelEngine::destruct(c);
	confirm(event->releaseXML(),err,text);
	delete event;
	return 0;
    }
    return event;
}

// Create an 'iq' stanza with a 'jingle' child
XMLElement* JGSession::createJingle(Action action, XMLElement* element1,
    XMLElement* element2, XMLElement* element3)
{
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,m_localJID,m_remoteJID,0);
    XMLElement* jingle = XMPPUtils::createElement(XMLElement::Jingle,
	XMPPNamespace::Jingle);
    if (action < ActCount)
	jingle->setAttribute("type",lookup(action,s_actions));
    jingle->setAttribute("initiator",outgoing() ? m_localJID : m_remoteJID);
    jingle->setAttribute("responder",outgoing() ? m_remoteJID : m_localJID);
    jingle->setAttribute("sid",m_sid);
    jingle->addChild(element1);
    jingle->addChild(element2);
    jingle->addChild(element3);
    iq->addChild(jingle);
    return iq;
}

// Event termination notification
void JGSession::eventTerminated(JGEvent* event)
{
    lock();
    if (event == m_lastEvent) {
	DDebug(m_engine,DebugAll,"Call(%s). Event (%p,%u) terminated [%p]",
	    m_sid.c_str(),event,event->type(),this);
	m_lastEvent = 0;
    }
    else if (m_lastEvent)
	Debug(m_engine,DebugNote,
	    "Call(%s). Event (%p,%u) replaced while processed [%p]",
	    m_sid.c_str(),event,event->type(),this);
    unlock();
}

// Change session state
void JGSession::changeState(State newState)
{
    if (m_state == newState)
	return;
    Debug(m_engine,DebugInfo,"Call(%s). Changing state from %s to %s [%p]",
	m_sid.c_str(),lookup(m_state,s_states),lookup(newState,s_states),this);
    m_state = newState;
}

/* vi: set ts=8 sw=4 sts=4 noet: */

#ifndef _ALLJOYN_INTERNAL_H
#define _ALLJOYN_INTERNAL_H
/**
 * @file
 *
 * This file defines internal state for a BusAttachment
 */

/******************************************************************************
 * Copyright (c) 2009-2011, AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

#ifndef __cplusplus
#error Only include BusInternal.h in C++ code.
#endif

#include <qcc/platform.h>
#include <qcc/String.h>
#include <qcc/Event.h>
#include <qcc/atomic.h>
#include <qcc/ManagedObj.h>
#include <qcc/IODispatch.h>

#include <alljoyn/BusAttachment.h>
#include <alljoyn/InterfaceDescription.h>

#include "AuthManager.h"
#include "ClientRouter.h"
#include "KeyStore.h"
#include "PeerState.h"
#include "Transport.h"
#include "TransportList.h"
#include "CompressionRules.h"

#include <alljoyn/Status.h>

namespace ajn {

class BusAttachment::Internal : public MessageReceiver, public JoinSessionAsyncCB {
    friend class BusAttachment;

  public:

    /**
     * Get a reference to the internal store object.
     *
     * @return A reference to the bus's key store.
     */
    KeyStore& GetKeyStore() { return keyStore; }

    /**
     * Return the next available serial number. Note 0 is an invalid serial number.
     *
     * @return   The next available serial number.
     */
    uint32_t NextSerial() {
        uint32_t sn = (uint32_t) qcc::IncrementAndFetch(&msgSerial);
        return sn ? sn : NextSerial();
    }

    /**
     * Return most recently allocated serial number
     */
    uint32_t PrevSerial() { return msgSerial ? msgSerial : -1; }

    /**
     * Get a reference to the authentication manager object.
     *
     * @return A pointer to the bus's authentication manager
     */
    AuthManager& GetAuthManager() { return authManager; }

    /**
     * Get a reference to the internal transport list.
     *
     * @return A reference to the bus's transport list.
     */
    TransportList& GetTransportList() { return transportList; }

    /**
     * Get a pointer to the internal peer state table.
     *
     * @return  The peer state table.
     */
    PeerStateTable* GetPeerStateTable() { return &peerStateTable; }

    /**
     * Get the global GUID for this bus.
     *
     * @return Global GUID
     */
    const qcc::GUID128& GetGlobalGUID(void) const { return globalGuid; }

    /**
     * Return the local endpoint for this bus.
     *
     * @return  Returns the local endpoint.
     */
    LocalEndpoint& GetLocalEndpoint() { return localEndpoint; }

    /**
     * Get the router.
     *
     * @return  The router
     */
    Router& GetRouter(void) { return *router; }

    /**
     * Get the router.
     *
     * @return  The router
     */
    const Router& GetRouter(void) const { return *router; }

    /**
     * Get the iodispatch.
     *
     * @return  The iodispatch
     */
    qcc::IODispatch& GetIODispatch(void) { return m_ioDispatch; }
    /**
     * Get the header compression rules
     *
     * @return The header compression rules.
     */
    CompressionRules& GetCompressionRules() { return compressionRules; };

    /**
     * Override the compressions rules for this bus attachment.
     */
    void OverrideCompressionRules(CompressionRules& newRules) { compressionRules = newRules; }

    /**
     * Constructor called by BusAttachment.
     */
    Internal(const char* appName,
             BusAttachment& bus,
             TransportFactoryContainer& factories,
             Router* router,
             bool allowRemoteMessages,
             const char* listenAddresses,
             uint32_t concurrency);

    /*
     * Destructor also called by BusAttachment
     */
    ~Internal();

    /**
     * Filter out authentication mechanisms not present in the list.
     *
     * @param list  The list of authentication mechanisms to filter on.
     */
    size_t FilterAuthMechanisms(const qcc::String& list) { return authManager.FilterMechanisms(list); }

    /**
     * A generic signal handler for AllJoyn signals
     */
    void AllJoynSignalHandler(const InterfaceDescription::Member* member, const char* srcPath, Message& message);

    /**
     * Indicate whether endpoints of this attachment are allowed to receive messages
     * from remote devices.
     */
    bool AllowRemoteMessages() const { return allowRemoteMessages; }

    /**
     * Get the bus addresses that this daemon uses to listen on.
     * For clients, this list is empty since clients dont listen.
     *
     * @return The bus addresses that this bus instance uses to listen on.
     */
    const qcc::String& GetListenAddresses() const { return listenAddresses; }

    /**
     * Inform BusListeners of incoming JoinSession attempt.
     *
     * @param sessionPort    SessionPort specified in join request.
     * @param joiner         Unique name of potential joiner.
     * @param opts           Session options requested by joiner.
     * @return   Return true if JoinSession request is accepted. false if rejected.
     */
    bool CallAcceptListeners(SessionPort sessionPort, const char* joiner, const SessionOpts& opts);

    /**
     * Inform BusListeners of a successful JoinSession.
     *
     * @param sessionPort    SessionPort specified by joiner.
     * @param id             Session id.
     * @param joiner         Unique name of sucessful joiner.
     */
    void CallJoinedListeners(SessionPort sessionPort, SessionId id, const char* joiner);

    /**
     * Set the SessionListener for an existing session id.
     *
     * @param sessionId  Existing session Id.
     * @param listener   SessionListener to associate with sessionId.
     * @return  ER_OK if successful.
     */
    QStatus SetSessionListener(SessionId id, SessionListener* listener);

    /**
     * Called if the bus attachment become disconnected from the bus.
     */
    void NonLocalEndpointDisconnected();

    /**
     * JoinSession implementation.
     */
    QStatus JoinSession(const char* sessionHost, SessionPort sessionPort, SessionListener* listener,
                        SessionId& sessionId, SessionOpts& opts);

    /**
     * JoinSessionAsync method_reply handler.
     */
    void JoinSessionAsyncCB(Message& message, void* context);

    /**
     * SetLinkTimeoutAsync method_reply handler.
     */
    void SetLinkTimeoutAsyncCB(Message& message, void* context);

    /**
     * Push a message into the local endpoint
     *
     * @param msg  The message to push
     * @return  ER_OK if successful.
     */
    QStatus PushToLocalEndpoint(Message& msg) {
        BusEndpoint busEndpoint = BusEndpoint::cast(localEndpoint);
        return router->PushMessage(msg, busEndpoint);
    }

  private:

    /**
     * Copy constructor.
     * Internal may not be copy constructed.
     *
     * @param other   sink being copied.
     */
    Internal(const BusAttachment::Internal& other);

    /**
     * Assignment operator.
     * Internal may not be assigned.
     *
     * @param other   RHS of assignment.
     */
    Internal& operator=(const BusAttachment::Internal& other);

    /**
     * @internal
     * JoinSessionAsync callback used by JoinSession
     */
    void JoinSessionCB(QStatus status, SessionId sessionId, const SessionOpts& opts, void* context);

    qcc::String application;              /* Name of the that owns the BusAttachment application */
    BusAttachment& bus;                   /* Reference back to the bus attachment that owns this state */


    qcc::Mutex listenersLock;             /* Mutex that protects BusListeners container (set) */
    typedef qcc::ManagedObj<BusListener*> ProtectedBusListener;
    typedef std::set<ProtectedBusListener> ListenerSet;
    ListenerSet listeners;               /* List of registered BusListeners */
    qcc::IODispatch m_ioDispatch;         /* iodispatch for this bus */
    TransportList transportList;          /* List of active transports */
    KeyStore keyStore;                    /* The key store for the bus attachment */
    AuthManager authManager;              /* The authentication manager for the bus attachment */
    qcc::GUID128 globalGuid;              /* Global GUID for this BusAttachment */
    int32_t msgSerial;                    /* Serial number is updated for every message sent by this bus */
    Router* router;                       /* Message bus router */
    PeerStateTable peerStateTable;        /* Table that maintains state information about remote peers */
    LocalEndpoint localEndpoint;          /* The local endpoint */
    CompressionRules compressionRules;    /* Rules for compresssing and decompressing headers */
    std::map<qcc::StringMapKey, InterfaceDescription> ifaceDescriptions;

    bool allowRemoteMessages;             /* true iff endpoints of this attachment can receive messages from remote devices */
    qcc::String listenAddresses;          /* The set of bus addresses that this bus can listen on. (empty for clients) */
    qcc::Mutex stopLock;                  /* Protects BusAttachement::Stop from being reentered */
    int32_t stopCount;                    /* Number of caller's blocked in BusAttachment::Stop() */

    typedef qcc::ManagedObj<SessionPortListener*> ProtectedSessionPortListener;
    typedef std::map<SessionPort, ProtectedSessionPortListener> SessionPortListenerMap;
    SessionPortListenerMap sessionPortListeners;  /* Lookup SessionPortListener by session port */

    typedef qcc::ManagedObj<SessionListener*> ProtectedSessionListener;
    typedef std::map<SessionId, ProtectedSessionListener> SessionListenerMap;
    SessionListenerMap sessionListeners;   /* Lookup SessionListener by session id */

    qcc::Mutex sessionListenersLock;       /* Lock protecting sessionListners maps */

    struct JoinContext {
        QStatus status;
        SessionId sessionId;
        SessionOpts opts;
    };

    std::map<qcc::Thread*, JoinContext> joinThreads;  /* List of threads waiting to join */
    qcc::Mutex joinLock;                              /* Mutex that protects joinThreads */
};

}

#endif

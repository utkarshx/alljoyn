#ifndef _ALLJOYN_BUSENDPOINT_H
#define _ALLJOYN_BUSENDPOINT_H
/**
 * @file This file defines the class for handling the client and server
 * endpoints for the message bus wire protocol
 */

/******************************************************************************
 * Copyright (c) 2009-2012, AllSeen Alliance. All rights reserved.
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

#include <qcc/platform.h>

#include <qcc/GUID.h>
#include <qcc/Mutex.h>
#include <qcc/String.h>
#include <qcc/ManagedObj.h>

#include <alljoyn/Message.h>
#include <alljoyn/MessageSink.h>

#include <alljoyn/Status.h>

namespace ajn {

class _BusEndpoint;

typedef qcc::ManagedObj<_BusEndpoint> BusEndpoint;

/**
 * BusEndpoint type.
 */
typedef enum {
    ENDPOINT_TYPE_INVALID, /**< An uninitialized endpoint */
    ENDPOINT_TYPE_NULL,    /**< Endpoint type used by the bundled daemon */
    ENDPOINT_TYPE_LOCAL,   /**< The local endpoint */
    ENDPOINT_TYPE_REMOTE,  /**< A remote endpoint */
    ENDPOINT_TYPE_BUS2BUS, /**< An endpoint connecting two busses */
    ENDPOINT_TYPE_VIRTUAL  /**< Represents an endpoint on another bus */
} EndpointType;

/**
 * Base class for all types of Bus endpoints
 */
class _BusEndpoint : public MessageSink {
  public:

    /**
     * Default constructor initializes an invalid endpoint
     */
    _BusEndpoint() : endpointType(ENDPOINT_TYPE_INVALID), isValid(false), disconnectStatus(ER_OK) { }

    /**
     * Constructor.
     *
     * @param type    BusEndpoint type.
     */
    _BusEndpoint(EndpointType type) : endpointType(type), isValid(type != ENDPOINT_TYPE_INVALID), disconnectStatus(ER_OK)  { }

    /**
     * Virtual destructor for derivable class.
     */
    virtual ~_BusEndpoint() { }

    /**
     * Check if an endpoint is valid
     */
    bool IsValid() const { return isValid; }

    /**
     * Invalidate a bus endpoint
     */
    void Invalidate();

    /**
     * Push a message into the endpoint
     *
     * @param msg   Message to send.
     *
     * @return ER_OK if successful
     */
    virtual QStatus PushMessage(Message& msg) { return ER_NOT_IMPLEMENTED; }

    /**
     * Get the endpoint's unique name.
     *
     * @return  Unique name for endpoint.
     */
    virtual const qcc::String& GetUniqueName() const { return qcc::String::Empty; }

    /**
     * Get the unique name of the endpoint's local controller object.
     *
     * @return  Unique name for endpoint's controller.
     */
    qcc::String GetControllerUniqueName() const;

    /**
     * Return the user id of the endpoint.
     *
     * @return  User ID number.
     */
    virtual uint32_t GetUserId() const { return -1; }

    /**
     * Return the group id of the endpoint.
     *
     * @return  Group ID number.
     */
    virtual uint32_t GetGroupId() const { return -1; }

    /**
     * Return the process id of the endpoint.
     *
     * @return  Process ID number.
     */
    virtual uint32_t GetProcessId() const { return -1; }

    /**
     * Indicates if the endpoint supports reporting UNIX style user, group, and process IDs.
     *
     * @return  'true' if UNIX IDs supported, 'false' if not supported.
     */
    virtual bool SupportsUnixIDs() const { return false; }

    /**
     * Get endpoint type.
     *
     * @return EndpointType
     */
    EndpointType GetEndpointType() const { return endpointType; }

    /**
     * Return true if this endpoint is allowed to receive messages from remote (bus-to-bus) endpoints.
     *
     * @return  true iff endpoint is allowed to receive messages from remote (bus-to-bus) endpoints.
     */
    virtual bool AllowRemoteMessages() { return false; }

    /**
     * Return true if the endpoint was disconnected due to an error rather than a clean shutdown.
     */
    bool SurpriseDisconnect() const { return disconnectStatus != ER_OK; }

    /**
     * Return true if the endpoint was disconnected due to an error rather than a clean shutdown.
     */
    QStatus GetDisconnectStatus() const { return disconnectStatus; }

    /**
     * Bus endpoints are only equal if they are the same object
     */
    bool operator ==(const _BusEndpoint& other) const { return this == &other; }

    /**
     * Bus endpoints are only equal if they are the same object
     */
    bool operator !=(const _BusEndpoint& other) const { return this != &other; }

    /*
     * Less than operator to allow endpoints to be put in sorted containers
     */
    bool operator <(const _BusEndpoint& other) const { return reinterpret_cast<ptrdiff_t>(this) < reinterpret_cast<ptrdiff_t>(&other); }

  protected:

    EndpointType endpointType;   /**< Type of endpoint */
    bool isValid;                /**< Is endpoint currently valid */
    QStatus disconnectStatus;    /**< Reason for the disconnect */
};


}

#endif

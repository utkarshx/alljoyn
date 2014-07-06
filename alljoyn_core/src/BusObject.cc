/**
 * @file
 *
 * This file implements the BusObject class
 */

/******************************************************************************
 * Copyright (c) 2009-2013, AllSeen Alliance. All rights reserved.
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

#include <assert.h>

#include <map>
#include <vector>

#include <qcc/Debug.h>
#include <qcc/Util.h>
#include <qcc/String.h>
#include <qcc/Mutex.h>
#include <alljoyn/DBusStd.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/BusObject.h>

#include <alljoyn/Status.h>
#include "Router.h"
#include "LocalTransport.h"
#include "AllJoynPeerObj.h"
#include "MethodTable.h"
#include "BusInternal.h"


#define QCC_MODULE "ALLJOYN"

using namespace qcc;
using namespace std;

namespace ajn {

/** Type definition for a METHOD message handler */
typedef struct {
    const InterfaceDescription::Member* member;   /**< Pointer to method's member */
    MessageReceiver::MethodHandler handler;       /**< Method implementation */
    void* context;
} MethodContext;

struct BusObject::Components {
    /** The interfaces this object implements */
    vector<const InterfaceDescription*> ifaces;
    /** The method handlers for this object */
    vector<MethodContext> methodContexts;
    /** Child objects of this object */
    vector<BusObject*> children;

    /** lock to prevent inUseCounter from being modified by two threads at the same time.*/
    qcc::Mutex counterLock;

    /** counter to prevent this BusObject being deleted if it is being used by another thread. */
    int32_t inUseCounter;
};


static inline bool SecurityApplies(const BusObject* obj, const InterfaceDescription* ifc)
{
    InterfaceSecurityPolicy ifcSec = ifc->GetSecurityPolicy();
    if (ifcSec == AJ_IFC_SECURITY_REQUIRED) {
        return true;
    } else {
        return obj->IsSecure() && (ifcSec != AJ_IFC_SECURITY_OFF);
    }
}

/*
 * Helper function to lookup an interface. Because we don't expect objects to implement more than a
 * small number of interfaces we just use a simple linear search.
 */
static const InterfaceDescription* LookupInterface(vector<const InterfaceDescription*>& ifaces, const char* ifName)
{
    vector<const InterfaceDescription*>::const_iterator it = ifaces.begin();
    while (it != ifaces.end()) {
        if (0 == strcmp((*it)->GetName(), ifName)) {
            return *it;
        } else {
            ++it;
        }
    }
    return NULL;
}

bool BusObject::ImplementsInterface(const char* ifName)
{
    return LookupInterface(components->ifaces, ifName) != NULL;
}

qcc::String BusObject::GetName()
{
    if (!path.empty()) {
        qcc::String name = path;
        size_t pos = name.find_last_of('/');
        if (pos == 0) {
            if (name.length() > 1) {
                name.erase(0, 1);
            }
        } else {
            name.erase(0, pos + 1);
        }
        return name;
    } else {
        return qcc::String("<anonymous>");
    }
}

qcc::String BusObject::GenerateIntrospection(bool deep, size_t indent) const
{
    qcc::String in(indent, ' ');
    qcc::String xml;

    /* Iterate over child nodes */
    vector<BusObject*>::const_iterator iter = components->children.begin();
    while (iter != components->children.end()) {
        BusObject* child = *iter++;
        xml += in + "<node name=\"" + child->GetName() + "\"";
        if (deep) {
            xml += "\n" + child->GenerateIntrospection(deep, indent + 2) + in + "</node>\n";
        } else {
            xml += "/>\n";
        }
    }
    if (deep || !isPlaceholder) {
        /* Iterate over interfaces */
        vector<const InterfaceDescription*>::const_iterator itIf = components->ifaces.begin();
        while (itIf != components->ifaces.end()) {
            xml += (*itIf++)->Introspect(indent);
        }
    }
    return xml;
}

void BusObject::GetProp(const InterfaceDescription::Member* member, Message& msg)
{
    QStatus status;
    const MsgArg* iface = msg->GetArg(0);
    const MsgArg* property = msg->GetArg(1);
    MsgArg val = MsgArg();

    /* Check property exists on this interface and is readable */
    const InterfaceDescription* ifc = LookupInterface(components->ifaces, iface->v_string.str);
    if (ifc) {
        /*
         * If the object or interface is secure the message must be encrypted
         */
        if (!msg->IsEncrypted() && SecurityApplies(this, ifc)) {
            status = ER_BUS_MESSAGE_NOT_ENCRYPTED;
            QCC_LogError(status, ("Attempt to get a property from a secure %s", isSecure ? "object" : "interface"));
        } else {
            const InterfaceDescription::Property* prop = ifc->GetProperty(property->v_string.str);
            if (prop) {
                if (prop->access & PROP_ACCESS_READ) {
                    status = Get(iface->v_string.str, property->v_string.str, val);
                } else {
                    QCC_DbgPrintf(("No read access on property %s", property->v_string.str));
                    status = ER_BUS_PROPERTY_ACCESS_DENIED;
                }
            } else {
                status = ER_BUS_NO_SUCH_PROPERTY;
            }
        }
    } else {
        status = ER_BUS_UNKNOWN_INTERFACE;
    }
    QCC_DbgPrintf(("Properties.Get %s", QCC_StatusText(status)));
    if (status == ER_OK) {
        /* Properties are returned as variants */
        MsgArg arg = MsgArg(ALLJOYN_VARIANT);
        arg.v_variant.val = &val;
        MethodReply(msg, &arg, 1);
        /* Prevent destructor from attempting to free val */
        arg.v_variant.val = NULL;
    } else {
        MethodReply(msg, status);
    }
}

void BusObject::EmitPropChanged(const char* ifcName, const char* propName, MsgArg& val, SessionId id)
{
    assert(bus);
    const InterfaceDescription* ifc = bus->GetInterface(ifcName);

    qcc::String emitsChanged;
    if (ifc && ifc->GetPropertyAnnotation(propName, org::freedesktop::DBus::AnnotateEmitsChanged, emitsChanged)) {
        if (emitsChanged == "true") {
            const InterfaceDescription* bus_ifc = bus->GetInterface(org::freedesktop::DBus::InterfaceName);
            const InterfaceDescription::Member* propChanged = (bus_ifc ? bus_ifc->GetMember("PropertiesChanged") : NULL);

            if (NULL != propChanged) {
                MsgArg args[3];
                args[0].Set("s", ifcName);
                MsgArg str("{sv}", propName, &val);
                args[1].Set("a{sv}", 1, &str);
                args[2].Set("as", 0, NULL);
                Signal(NULL, id, *propChanged, args, ArraySize(args));
            }
        } else if (emitsChanged == "invalidates") {
            const InterfaceDescription* bus_ifc = bus->GetInterface(org::freedesktop::DBus::InterfaceName);
            const InterfaceDescription::Member* propChanged = (bus_ifc ? bus_ifc->GetMember("PropertiesChanged") : NULL);

            if (NULL != propChanged) {
                // EMPTY array, followed by array of strings
                MsgArg args[3];
                args[0].Set("s", ifcName);
                args[1].Set("a{sv}", 0, NULL);
                args[2].Set("as", 1, &propName);
                Signal(NULL, id, *propChanged, args, ArraySize(args));
            }
        }
    }
}


void BusObject::SetProp(const InterfaceDescription::Member* member, Message& msg)
{
    QStatus status = ER_BUS_NO_SUCH_PROPERTY;
    const MsgArg* iface = msg->GetArg(0);
    const MsgArg* property = msg->GetArg(1);
    const MsgArg* val = msg->GetArg(2);

    /* Check property exists on this interface has correct signature and is writeable */
    const InterfaceDescription* ifc = LookupInterface(components->ifaces, iface->v_string.str);
    if (ifc) {
        /*
         * If the object or interface is secure the message must be encrypted
         */
        if (!msg->IsEncrypted() && SecurityApplies(this, ifc)) {
            status = ER_BUS_MESSAGE_NOT_ENCRYPTED;
            QCC_LogError(status, ("Attempt to set a property on a secure %s", isSecure ? "object" : "interface"));
        } else {
            const InterfaceDescription::Property* prop = ifc->GetProperty(property->v_string.str);
            if (prop) {
                if (!val->v_variant.val->HasSignature(prop->signature.c_str())) {
                    QCC_DbgPrintf(("Property value for %s has wrong type %s", property->v_string.str, prop->signature.c_str()));
                    status = ER_BUS_SET_WRONG_SIGNATURE;
                } else if (prop->access & PROP_ACCESS_WRITE) {
                    // set the value, then inform bus listeners via Signal
                    status = Set(iface->v_string.str, property->v_string.str, *(val->v_variant.val));
                    // notify all session members that this property has changed
                    const SessionId id = msg->hdrFields.field[ALLJOYN_HDR_FIELD_SESSION_ID].v_uint32;
                    EmitPropChanged(iface->v_string.str, property->v_string.str, *(val->v_variant.val), id);
                } else {
                    QCC_DbgPrintf(("No write access on property %s", property->v_string.str));
                    status = ER_BUS_PROPERTY_ACCESS_DENIED;
                }
            } else {
                status = ER_BUS_NO_SUCH_PROPERTY;
            }
        }
    } else {
        status = ER_BUS_UNKNOWN_INTERFACE;
    }
    QCC_DbgPrintf(("Properties.Set %s", QCC_StatusText(status)));
    MethodReply(msg, status);
}

void BusObject::GetAllProps(const InterfaceDescription::Member* member, Message& msg)
{
    QStatus status = ER_OK;
    const MsgArg* iface = msg->GetArg(0);
    MsgArg vals;
    const InterfaceDescription::Property** props = NULL;

    /* Check interface exists and has properties */
    const InterfaceDescription* ifc = LookupInterface(components->ifaces, iface->v_string.str);
    if (ifc) {
        /*
         * If the object or interface is secure the message must be encrypted
         */
        if (!msg->IsEncrypted() && SecurityApplies(this, ifc)) {
            status = ER_BUS_MESSAGE_NOT_ENCRYPTED;
            QCC_LogError(status, ("Attempt to get properties from a secure %s", isSecure ? "object" : "interface"));
        } else {
            size_t numProps = ifc->GetProperties();
            props = new const InterfaceDescription::Property *[numProps];
            ifc->GetProperties(props, numProps);
            size_t readable = 0;
            /* Count readable properties */
            for (size_t i = 0; i < numProps; i++) {
                if (props[i]->access & PROP_ACCESS_READ) {
                    readable++;
                }
            }
            MsgArg* dict = new MsgArg[readable];
            MsgArg* entry = dict;
            /* Get readable properties */
            for (size_t i = 0; i < numProps; i++) {
                if (props[i]->access & PROP_ACCESS_READ) {
                    MsgArg* val = new MsgArg();
                    status = Get(iface->v_string.str, props[i]->name.c_str(), *val);
                    if (status != ER_OK) {
                        delete val;
                        break;
                    }
                    entry->Set("{sv}", props[i]->name.c_str(), val);
                    entry++;
                }
            }
            vals.Set("a{sv}", readable, dict);
            /*
             * Set ownership of the MsgArgs so they will be automatically freed.
             */
            vals.SetOwnershipFlags(MsgArg::OwnsArgs, true /*deep*/);
        }
    } else {
        status = ER_BUS_UNKNOWN_INTERFACE;
    }
    QCC_DbgPrintf(("Properties.GetAll %s", QCC_StatusText(status)));
    if (status == ER_OK) {
        MethodReply(msg, &vals, 1);
    } else {
        MethodReply(msg, status);
    }
    delete [] props;
}

void BusObject::Introspect(const InterfaceDescription::Member* member, Message& msg)
{
    qcc::String xml = org::freedesktop::DBus::Introspectable::IntrospectDocType;
    xml += "<node>\n";
    if (isSecure) {
        xml += "  <annotation name=\"org.alljoyn.Bus.Secure\" value=\"true\"/>\n";
    }
    xml += GenerateIntrospection(false, 2);
    xml += "</node>\n";
    MsgArg arg("s", xml.c_str());
    QStatus status = MethodReply(msg, &arg, 1);
    if (status != ER_OK) {
        QCC_DbgPrintf(("Introspect %s", QCC_StatusText(status)));
    }
}

QStatus BusObject::AddMethodHandler(const InterfaceDescription::Member* member, MessageReceiver::MethodHandler handler, void* handlerContext)
{
    if (!member) {
        return ER_BAD_ARG_1;
    }
    if (!handler) {
        return ER_BAD_ARG_2;
    }
    QStatus status = ER_OK;
    if (isRegistered) {
        status = ER_BUS_CANNOT_ADD_HANDLER;
        QCC_LogError(status, ("Cannot add method handler to an object that is already registered"));
    } else if (ImplementsInterface(member->iface->GetName())) {
        MethodContext ctx = { member, handler, handlerContext };
        components->methodContexts.push_back(ctx);
    } else {
        status = ER_BUS_NO_SUCH_INTERFACE;
        QCC_LogError(status, ("Cannot add method handler for unknown interface"));
    }
    return status;
}

QStatus BusObject::AddMethodHandlers(const MethodEntry* entries, size_t numEntries)
{
    if (!entries) {
        return ER_BAD_ARG_1;
    }
    QStatus status = ER_OK;
    for (size_t i = 0; i < numEntries; ++i) {
        status = AddMethodHandler(entries[i].member, entries[i].handler);
        if (ER_OK != status) {
            QCC_LogError(status, ("Failed to add method handler for %s.%s", entries[i].member->iface->GetName(), entries[i].member->name.c_str()));
            break;
        }
    }
    return status;
}

void BusObject::InstallMethods(MethodTable& methodTable)
{
    vector<MethodContext>::iterator iter;
    for (iter = components->methodContexts.begin(); iter != components->methodContexts.end(); iter++) {
        const MethodContext methodContext = *iter;
        methodTable.Add(this, methodContext.handler, methodContext.member, methodContext.context);
    }
}

QStatus BusObject::AddInterface(const InterfaceDescription& iface)
{
    QStatus status = ER_OK;
    if (isRegistered) {
        status = ER_BUS_CANNOT_ADD_INTERFACE;
        QCC_LogError(status, ("Cannot add an interface to an object that is already registered"));
        goto ExitAddInterface;
    }
    /* The Peer interface is implicit on all objects so cannot be explicitly added */
    if (strcmp(iface.GetName(), org::freedesktop::DBus::Peer::InterfaceName) == 0) {
        status = ER_BUS_IFACE_ALREADY_EXISTS;
        QCC_LogError(status, ("%s is implicit on all objects and cannot be added manually", iface.GetName()));
        goto ExitAddInterface;
    }
    /* The Properties interface is automatically added when needed so cannot be explicitly added */
    if (strcmp(iface.GetName(), org::freedesktop::DBus::Properties::InterfaceName) == 0) {
        status = ER_BUS_IFACE_ALREADY_EXISTS;
        QCC_LogError(status, ("%s is automatically added if needed and cannot be added manually", iface.GetName()));
        goto ExitAddInterface;
    }
    /* Check interface has not already been added */
    if (ImplementsInterface(iface.GetName())) {
        status = ER_BUS_IFACE_ALREADY_EXISTS;
        QCC_LogError(status, ("%s already added to this object", iface.GetName()));
        goto ExitAddInterface;
    }

    /* Add the new interface */
    components->ifaces.push_back(&iface);


ExitAddInterface:

    return status;
}

QStatus BusObject::DoRegistration(BusAttachment& busAttachment)
{
    QStatus status;

    // Set the BusAttachment as part of the object registration. This will
    // overwrite the one from the (deprecated) constructor.
    bus = &busAttachment;

    /* Add the standard DBus interfaces */
    const InterfaceDescription* introspectable = bus->GetInterface(org::freedesktop::DBus::Introspectable::InterfaceName);
    assert(introspectable);
    components->ifaces.push_back(introspectable);

    /* Add the standard method handlers */
    const MethodEntry methodEntries[] = {
        { introspectable->GetMember("Introspect"),    static_cast<MessageReceiver::MethodHandler>(&BusObject::Introspect) }
    };

    /* If any of the interfaces has properties make sure the Properties interface and its method handlers are registered. */
    for (size_t i = 0; i < components->ifaces.size(); ++i) {
        const InterfaceDescription* iface = components->ifaces[i];
        if (iface->HasProperties() && !ImplementsInterface(org::freedesktop::DBus::Properties::InterfaceName)) {

            /* Add the org::freedesktop::DBus::Properties interface to this list of interfaces implemented by this obj */
            const InterfaceDescription* propIntf = bus->GetInterface(org::freedesktop::DBus::Properties::InterfaceName);
            assert(propIntf);
            components->ifaces.push_back(propIntf);

            /* Attach the handlers */
            const MethodEntry propHandlerList[] = {
                { propIntf->GetMember("Get"),    static_cast<MessageReceiver::MethodHandler>(&BusObject::GetProp) },
                { propIntf->GetMember("Set"),    static_cast<MessageReceiver::MethodHandler>(&BusObject::SetProp) },
                { propIntf->GetMember("GetAll"), static_cast<MessageReceiver::MethodHandler>(&BusObject::GetAllProps) }
            };
            status = AddMethodHandlers(propHandlerList, ArraySize(propHandlerList));
            if (ER_OK != status) {
                QCC_LogError(status, ("Failed to add property getter/setter message receivers for %s", GetPath()));
                return status;
            }
            break;
        }
    }
    status = AddMethodHandlers(methodEntries, ArraySize(methodEntries));
    return status;
}

QStatus BusObject::Signal(const char* destination,
                          SessionId sessionId,
                          const InterfaceDescription::Member& signalMember,
                          const MsgArg* args,
                          size_t numArgs,
                          uint16_t timeToLive,
                          uint8_t flags,
                          Message* outMsg)
{
    /* Protect against calling Signal before object is registered */
    if (!bus) {
        return ER_BUS_OBJECT_NOT_REGISTERED;
    }

    QStatus status;
    Message msg(*bus);

    /*
     * If the object or interface is secure or encryption is explicitly requested the signal must be encrypted.
     */
    if (SecurityApplies(this, signalMember.iface)) {
        flags |= ALLJOYN_FLAG_ENCRYPTED;
    }
    if ((flags & ALLJOYN_FLAG_ENCRYPTED) && !bus->IsPeerSecurityEnabled()) {
        return ER_BUS_SECURITY_NOT_ENABLED;
    }
    status = msg->SignalMsg(signalMember.signature,
                            destination,
                            sessionId,
                            path,
                            signalMember.iface->GetName(),
                            signalMember.name,
                            args,
                            numArgs,
                            flags,
                            timeToLive);
    if (status == ER_OK) {
        BusEndpoint bep = BusEndpoint::cast(bus->GetInternal().GetLocalEndpoint());
        status = bus->GetInternal().GetRouter().PushMessage(msg, bep);
        if ((status == ER_OK) && outMsg) {
            *outMsg = msg;
        }
    }
    return status;
}

QStatus BusObject::CancelSessionlessMessage(uint32_t serialNum)
{
    if (!bus) {
        return ER_BUS_OBJECT_NOT_REGISTERED;
    }

    Message reply(*bus);
    MsgArg arg("u", serialNum);
    const ProxyBusObject& alljoynObj = bus->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "CancelSessionlessMessage", &arg, 1, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_CANCELSESSIONLESS_REPLY_SUCCESS:
                break;

            case ALLJOYN_CANCELSESSIONLESS_REPLY_NO_SUCH_MSG:
                status = ER_BUS_NO_SUCH_MESSAGE;
                break;

            case ALLJOYN_CANCELSESSIONLESS_REPLY_NOT_ALLOWED:
                status = ER_BUS_NOT_ALLOWED;
                break;

            case ALLJOYN_CANCELSESSIONLESS_REPLY_FAILED:
                status = ER_FAIL;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    }
    return status;
}

QStatus BusObject::MethodReply(const Message& msg, const MsgArg* args, size_t numArgs)
{
    QStatus status;

    /* Protect against calling before object is registered */
    if (!bus) {
        return ER_BUS_OBJECT_NOT_REGISTERED;
    }

    if (msg->GetType() != MESSAGE_METHOD_CALL) {
        status = ER_BUS_NO_CALL_FOR_REPLY;
    } else {
        Message reply(*bus);
        status = reply->ReplyMsg(msg, args, numArgs);
        if (status == ER_OK) {
            BusEndpoint bep = BusEndpoint::cast(bus->GetInternal().GetLocalEndpoint());
            status = bus->GetInternal().GetRouter().PushMessage(reply, bep);
        }
    }
    return status;
}

QStatus BusObject::MethodReply(const Message& msg, const char* errorName, const char* errorMessage)
{
    QStatus status;

    /* Protect against calling before object is registered */
    if (!bus) {
        return ER_BUS_OBJECT_NOT_REGISTERED;
    }

    if (msg->GetType() != MESSAGE_METHOD_CALL) {
        status = ER_BUS_NO_CALL_FOR_REPLY;
        return status;
    } else {
        Message error(*bus);
        status = error->ErrorMsg(msg, errorName, errorMessage ? errorMessage : "");
        if (status == ER_OK) {
            BusEndpoint bep = BusEndpoint::cast(bus->GetInternal().GetLocalEndpoint());
            status = bus->GetInternal().GetRouter().PushMessage(error, bep);
        }
    }
    return status;
}

QStatus BusObject::MethodReply(const Message& msg, QStatus status)
{
    /* Protect against calling before object is registered */
    if (!bus) {
        return ER_BUS_OBJECT_NOT_REGISTERED;
    }

    if (status == ER_OK) {
        return MethodReply(msg);
    } else {
        if (msg->GetType() != MESSAGE_METHOD_CALL) {
            return ER_BUS_NO_CALL_FOR_REPLY;
        } else {
            Message error(*bus);
            error->ErrorMsg(msg, status);
            BusEndpoint bep = BusEndpoint::cast(bus->GetInternal().GetLocalEndpoint());
            return bus->GetInternal().GetRouter().PushMessage(error, bep);
        }
    }
}

void BusObject::AddChild(BusObject& child)
{
    QCC_DbgPrintf(("AddChild %s to object with path = \"%s\"", child.GetPath(), GetPath()));
    child.parent = this;
    components->children.push_back(&child);
}

QStatus BusObject::RemoveChild(BusObject& child)
{
    QStatus status = ER_BUS_NO_SUCH_OBJECT;
    vector<BusObject*>::iterator it = find(components->children.begin(), components->children.end(), &child);
    if (it != components->children.end()) {
        assert(*it == &child);
        child.parent = NULL;
        QCC_DbgPrintf(("RemoveChild %s from object with path = \"%s\"", child.GetPath(), GetPath()));
        components->children.erase(it);
        status = ER_OK;
    }
    return status;
}

BusObject* BusObject::RemoveChild()
{
    size_t sz = components->children.size();
    if (sz > 0) {
        BusObject* child = components->children[sz - 1];
        components->children.pop_back();
        QCC_DbgPrintf(("RemoveChild %s from object with path = \"%s\"", child->GetPath(), GetPath()));
        child->parent = NULL;
        return child;
    } else {
        return NULL;
    }
}

void BusObject::Replace(BusObject& object)
{
    QCC_DbgPrintf(("Replacing object with path = \"%s\"", GetPath()));
    object.components->children = components->children;
    vector<BusObject*>::iterator it = object.components->children.begin();
    while (it != object.components->children.end()) {
        (*it++)->parent = &object;
    }
    if (parent) {
        vector<BusObject*>::iterator pit = parent->components->children.begin();
        while (pit != parent->components->children.end()) {
            if ((*pit) == this) {
                parent->components->children.erase(pit);
                break;
            }
            ++pit;
        }
    }
    components->children.clear();
}

void BusObject::InUseIncrement() {
    components->counterLock.Lock(MUTEX_CONTEXT);
    qcc::IncrementAndFetch(&(components->inUseCounter));
    components->counterLock.Unlock(MUTEX_CONTEXT);
}

void BusObject::InUseDecrement() {
    components->counterLock.Lock(MUTEX_CONTEXT);
    qcc::DecrementAndFetch(&(components->inUseCounter));
    components->counterLock.Unlock(MUTEX_CONTEXT);
}

BusObject::BusObject(BusAttachment& bus, const char* path, bool isPlaceholder) :
    bus(&bus),
    components(new Components),
    path(path),
    parent(NULL),
    isRegistered(false),
    isPlaceholder(isPlaceholder),
    isSecure(false)
{
    components->inUseCounter = 0;
}

BusObject::BusObject(const char* path, bool isPlaceholder) :
    bus(0),
    components(new Components),
    path(path),
    parent(NULL),
    isRegistered(false),
    isPlaceholder(isPlaceholder),
    isSecure(false)
{
    components->inUseCounter = 0;
}

BusObject::~BusObject()
{
    components->counterLock.Lock(MUTEX_CONTEXT);
    while (components->inUseCounter != 0) {
        components->counterLock.Unlock(MUTEX_CONTEXT);
        qcc::Sleep(5);
        components->counterLock.Lock(MUTEX_CONTEXT);
    }
    components->counterLock.Unlock(MUTEX_CONTEXT);

    QCC_DbgPrintf(("BusObject destructor for object with path = \"%s\"", GetPath()));
    /*
     * If this object has a parent it has not been unregistered so do so now.
     */
    if (bus && parent) {
        bus->GetInternal().GetLocalEndpoint()->UnregisterBusObject(*this);
    }
    delete components;
}

}

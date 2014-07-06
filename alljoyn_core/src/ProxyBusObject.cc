/**
 * @file
 *
 * This file implements the ProxyBusObject class.
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
#include <vector>
#include <map>

#include <qcc/Debug.h>
#include <qcc/String.h>
#include <qcc/StringSource.h>
#include <qcc/XmlElement.h>
#include <qcc/Util.h>
#include <qcc/Event.h>
#include <qcc/Mutex.h>
#include <qcc/ManagedObj.h>

#include <alljoyn/BusAttachment.h>
#include <alljoyn/DBusStd.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/Message.h>
#include <alljoyn/ProxyBusObject.h>
#include <alljoyn/InterfaceDescription.h>

#include "Router.h"
#include "LocalTransport.h"
#include "AllJoynPeerObj.h"
#include "BusInternal.h"
#include "XmlHelper.h"

#include <alljoyn/Status.h>

#define QCC_MODULE "ALLJOYN"

#define SYNC_METHOD_ALERTCODE_OK     0
#define SYNC_METHOD_ALERTCODE_ABORT  1

using namespace qcc;
using namespace std;

namespace ajn {

struct ProxyBusObject::Components {

    /** The interfaces this object implements */
    map<qcc::StringMapKey, const InterfaceDescription*> ifaces;

    /** Names of child objects of this object */
    vector<_ProxyBusObject> children;

    /** List of threads that are waiting in sync method calls */
    vector<Thread*> waitingThreads;
};

template <typename _cbType> struct CBContext {
    CBContext(ProxyBusObject* obj, ProxyBusObject::Listener* listener, _cbType callback, void* context)
        : obj(obj), listener(listener), callback(callback), context(context) { }

    ProxyBusObject* obj;
    ProxyBusObject::Listener* listener;
    _cbType callback;
    void* context;
};

static inline bool SecurityApplies(const ProxyBusObject* obj, const InterfaceDescription* ifc)
{
    InterfaceSecurityPolicy ifcSec = ifc->GetSecurityPolicy();
    if (ifcSec == AJ_IFC_SECURITY_REQUIRED) {
        return true;
    } else {
        return obj->IsSecure() && (ifcSec != AJ_IFC_SECURITY_OFF);
    }
}

QStatus ProxyBusObject::GetAllProperties(const char* iface, MsgArg& value, uint32_t timeout) const
{
    QStatus status;
    const InterfaceDescription* valueIface = bus->GetInterface(iface);
    if (!valueIface) {
        status = ER_BUS_OBJECT_NO_SUCH_INTERFACE;
    } else {
        uint8_t flags = 0;
        /*
         * If the object or the property interface is secure method call must be encrypted.
         */
        if (SecurityApplies(this, valueIface)) {
            flags |= ALLJOYN_FLAG_ENCRYPTED;
        }
        Message reply(*bus);
        MsgArg arg = MsgArg("s", iface);
        const InterfaceDescription* propIface = bus->GetInterface(org::freedesktop::DBus::Properties::InterfaceName);
        if (propIface == NULL) {
            status = ER_BUS_NO_SUCH_INTERFACE;
        } else {
            status = MethodCall(*(propIface->GetMember("GetAll")), &arg, 1, reply, timeout, flags);
            if (ER_OK == status) {
                value = *(reply->GetArg(0));
            }
        }
    }
    return status;
}

void ProxyBusObject::GetAllPropsMethodCB(Message& message, void* context)
{
    CBContext<Listener::GetAllPropertiesCB>* ctx = reinterpret_cast<CBContext<Listener::GetAllPropertiesCB>*>(context);

    if (message->GetType() == MESSAGE_METHOD_RET) {
        (ctx->listener->*ctx->callback)(ER_OK, ctx->obj, *message->GetArg(0), ctx->context);
    } else {
        const MsgArg noVal;
        QStatus status = ER_BUS_NO_SUCH_PROPERTY;
        if (::strcmp(message->GetErrorName(), org::alljoyn::Bus::ErrorName) == 0) {
            const char* err;
            uint16_t rawStatus;
            if (message->GetArgs("sq", &err, &rawStatus) == ER_OK) {
                status = static_cast<QStatus>(rawStatus);
                QCC_DbgPrintf(("Asynch GetAllProperties call returned %s", err));
            }
        }
        (ctx->listener->*ctx->callback)(status, ctx->obj, noVal, ctx->context);
    }
    delete ctx;
}

QStatus ProxyBusObject::GetAllPropertiesAsync(const char* iface,
                                              ProxyBusObject::Listener* listener,
                                              ProxyBusObject::Listener::GetPropertyCB callback,
                                              void* context,
                                              uint32_t timeout)
{
    QStatus status;
    const InterfaceDescription* valueIface = bus->GetInterface(iface);
    if (!valueIface) {
        status = ER_BUS_OBJECT_NO_SUCH_INTERFACE;
    } else {
        uint8_t flags = 0;
        /*
         * If the object or the property interface is secure method call must be encrypted.
         */
        if (SecurityApplies(this, valueIface)) {
            flags |= ALLJOYN_FLAG_ENCRYPTED;
        }
        MsgArg arg = MsgArg("s", iface);
        const InterfaceDescription* propIface = bus->GetInterface(org::freedesktop::DBus::Properties::InterfaceName);
        if (propIface == NULL) {
            status = ER_BUS_NO_SUCH_INTERFACE;
        } else {
            CBContext<Listener::GetAllPropertiesCB>* ctx = new CBContext<Listener::GetAllPropertiesCB>(this, listener, callback, context);
            status = MethodCallAsync(*(propIface->GetMember("GetAll")),
                                     this,
                                     static_cast<MessageReceiver::ReplyHandler>(&ProxyBusObject::GetAllPropsMethodCB),
                                     &arg,
                                     1,
                                     reinterpret_cast<void*>(ctx),
                                     timeout,
                                     flags);
            if (status != ER_OK) {
                delete ctx;
            }
        }
    }
    return status;
}

QStatus ProxyBusObject::GetProperty(const char* iface, const char* property, MsgArg& value, uint32_t timeout) const
{
    QStatus status;
    const InterfaceDescription* valueIface = bus->GetInterface(iface);
    if (!valueIface) {
        status = ER_BUS_OBJECT_NO_SUCH_INTERFACE;
    } else {
        uint8_t flags = 0;
        /*
         * If the object or the property interface is secure method call must be encrypted.
         */
        if (SecurityApplies(this, valueIface)) {
            flags |= ALLJOYN_FLAG_ENCRYPTED;
        }
        Message reply(*bus);
        MsgArg inArgs[2];
        size_t numArgs = ArraySize(inArgs);
        MsgArg::Set(inArgs, numArgs, "ss", iface, property);
        const InterfaceDescription* propIface = bus->GetInterface(org::freedesktop::DBus::Properties::InterfaceName);
        if (propIface == NULL) {
            status = ER_BUS_NO_SUCH_INTERFACE;
        } else {
            status = MethodCall(*(propIface->GetMember("Get")), inArgs, numArgs, reply, timeout, flags);
            if (ER_OK == status) {
                value = *(reply->GetArg(0));
            }
        }
    }
    return status;
}

void ProxyBusObject::GetPropMethodCB(Message& message, void* context)
{
    CBContext<Listener::GetPropertyCB>* ctx = reinterpret_cast<CBContext<Listener::GetPropertyCB>*>(context);

    if (message->GetType() == MESSAGE_METHOD_RET) {
        (ctx->listener->*ctx->callback)(ER_OK, ctx->obj, *message->GetArg(0), ctx->context);
    } else {
        const MsgArg noVal;
        QStatus status = ER_BUS_NO_SUCH_PROPERTY;
        if (::strcmp(message->GetErrorName(), org::alljoyn::Bus::ErrorName) == 0) {
            const char* err;
            uint16_t rawStatus;
            if (message->GetArgs("sq", &err, &rawStatus) == ER_OK) {
                status = static_cast<QStatus>(rawStatus);
                QCC_DbgPrintf(("Asynch GetProperty call returned %s", err));
            }
        }
        (ctx->listener->*ctx->callback)(status, ctx->obj, noVal, ctx->context);
    }
    delete ctx;
}

QStatus ProxyBusObject::GetPropertyAsync(const char* iface,
                                         const char* property,
                                         ProxyBusObject::Listener* listener,
                                         ProxyBusObject::Listener::GetPropertyCB callback,
                                         void* context,
                                         uint32_t timeout)
{
    QStatus status;
    const InterfaceDescription* valueIface = bus->GetInterface(iface);
    if (!valueIface) {
        status = ER_BUS_OBJECT_NO_SUCH_INTERFACE;
    } else {
        uint8_t flags = 0;
        if (SecurityApplies(this, valueIface)) {
            flags |= ALLJOYN_FLAG_ENCRYPTED;
        }
        MsgArg inArgs[2];
        size_t numArgs = ArraySize(inArgs);
        MsgArg::Set(inArgs, numArgs, "ss", iface, property);
        const InterfaceDescription* propIface = bus->GetInterface(org::freedesktop::DBus::Properties::InterfaceName);
        if (propIface == NULL) {
            status = ER_BUS_NO_SUCH_INTERFACE;
        } else {
            CBContext<Listener::GetPropertyCB>* ctx = new CBContext<Listener::GetPropertyCB>(this, listener, callback, context);
            status = MethodCallAsync(*(propIface->GetMember("Get")),
                                     this,
                                     static_cast<MessageReceiver::ReplyHandler>(&ProxyBusObject::GetPropMethodCB),
                                     inArgs,
                                     numArgs,
                                     reinterpret_cast<void*>(ctx),
                                     timeout,
                                     flags);
            if (status != ER_OK) {
                delete ctx;
            }
        }
    }
    return status;
}

QStatus ProxyBusObject::SetProperty(const char* iface, const char* property, MsgArg& value, uint32_t timeout) const
{
    QStatus status;
    const InterfaceDescription* valueIface = bus->GetInterface(iface);
    if (!valueIface) {
        status = ER_BUS_OBJECT_NO_SUCH_INTERFACE;
    } else {
        uint8_t flags = 0;
        /*
         * If the object or the property interface is secure method call must be encrypted.
         */
        if (SecurityApplies(this, valueIface)) {
            flags |= ALLJOYN_FLAG_ENCRYPTED;
        }
        Message reply(*bus);
        MsgArg inArgs[3];
        size_t numArgs = ArraySize(inArgs);
        MsgArg::Set(inArgs, numArgs, "ssv", iface, property, &value);
        const InterfaceDescription* propIface = bus->GetInterface(org::freedesktop::DBus::Properties::InterfaceName);
        if (propIface == NULL) {
            status = ER_BUS_NO_SUCH_INTERFACE;
        } else {
            status = MethodCall(*(propIface->GetMember("Set")),
                                inArgs,
                                numArgs,
                                reply,
                                timeout,
                                flags);
        }
    }
    return status;
}

void ProxyBusObject::SetPropMethodCB(Message& message, void* context)
{
    QStatus status = ER_OK;
    CBContext<Listener::SetPropertyCB>* ctx = reinterpret_cast<CBContext<Listener::SetPropertyCB>*>(context);

    if (message->GetType() != MESSAGE_METHOD_RET) {
        status = ER_BUS_NO_SUCH_PROPERTY;
        if (::strcmp(message->GetErrorName(), org::alljoyn::Bus::ErrorName) == 0) {
            const char* err;
            uint16_t rawStatus;
            if (message->GetArgs("sq", &err, &rawStatus) == ER_OK) {
                status = static_cast<QStatus>(rawStatus);
                QCC_DbgPrintf(("Asynch SetProperty call returned %s", err));
            }
        }
    }
    (ctx->listener->*ctx->callback)(status, ctx->obj, ctx->context);
    delete ctx;
}

QStatus ProxyBusObject::SetPropertyAsync(const char* iface,
                                         const char* property,
                                         MsgArg& value,
                                         ProxyBusObject::Listener* listener,
                                         ProxyBusObject::Listener::SetPropertyCB callback,
                                         void* context,
                                         uint32_t timeout)
{
    QStatus status;
    const InterfaceDescription* valueIface = bus->GetInterface(iface);
    if (!valueIface) {
        status = ER_BUS_OBJECT_NO_SUCH_INTERFACE;
    } else {
        uint8_t flags = 0;
        if (SecurityApplies(this, valueIface)) {
            flags |= ALLJOYN_FLAG_ENCRYPTED;
        }
        MsgArg inArgs[3];
        size_t numArgs = ArraySize(inArgs);
        MsgArg::Set(inArgs, numArgs, "ssv", iface, property, &value);
        const InterfaceDescription* propIface = bus->GetInterface(org::freedesktop::DBus::Properties::InterfaceName);
        if (propIface == NULL) {
            status = ER_BUS_NO_SUCH_INTERFACE;
        } else {
            CBContext<Listener::SetPropertyCB>* ctx = new CBContext<Listener::SetPropertyCB>(this, listener, callback, context);
            status = MethodCallAsync(*(propIface->GetMember("Set")),
                                     this,
                                     static_cast<MessageReceiver::ReplyHandler>(&ProxyBusObject::SetPropMethodCB),
                                     inArgs,
                                     numArgs,
                                     reinterpret_cast<void*>(ctx),
                                     timeout,
                                     flags);
            if (status != ER_OK) {
                delete ctx;
            }
        }
    }
    return status;
}

size_t ProxyBusObject::GetInterfaces(const InterfaceDescription** ifaces, size_t numIfaces) const
{
    lock->Lock(MUTEX_CONTEXT);
    size_t count = components->ifaces.size();
    if (ifaces) {
        count = min(count, numIfaces);
        map<qcc::StringMapKey, const InterfaceDescription*>::const_iterator it = components->ifaces.begin();
        for (size_t i = 0; i < count; i++, it++) {
            ifaces[i] = it->second;
        }
    }
    lock->Unlock(MUTEX_CONTEXT);
    return count;
}

const InterfaceDescription* ProxyBusObject::GetInterface(const char* ifaceName) const
{
    StringMapKey key = ifaceName;
    lock->Lock(MUTEX_CONTEXT);
    map<StringMapKey, const InterfaceDescription*>::const_iterator it = components->ifaces.find(key);
    const InterfaceDescription* ret = (it == components->ifaces.end()) ? NULL : it->second;
    lock->Unlock(MUTEX_CONTEXT);
    return ret;
}


QStatus ProxyBusObject::AddInterface(const InterfaceDescription& iface) {
    StringMapKey key = iface.GetName();
    pair<StringMapKey, const InterfaceDescription*> item(key, &iface);
    lock->Lock(MUTEX_CONTEXT);
    if (!hasProperties && iface == *(bus->GetInterface(::ajn::org::freedesktop::DBus::Properties::InterfaceName))) {
        hasProperties = true;
    }
    pair<map<StringMapKey, const InterfaceDescription*>::const_iterator, bool> ret = components->ifaces.insert(item);
    QStatus status = ret.second ? ER_OK : ER_BUS_IFACE_ALREADY_EXISTS;

    /* Add org.freedesktop.DBus.Properties interface implicitly if iface specified properties */
    if ((status == ER_OK) && !hasProperties && (iface.GetProperties() > 0)) {
        const InterfaceDescription* propIntf = bus->GetInterface(::ajn::org::freedesktop::DBus::Properties::InterfaceName);
        if (propIntf) {
            hasProperties = true;
            StringMapKey propKey = ::ajn::org::freedesktop::DBus::Properties::InterfaceName;
            pair<StringMapKey, const InterfaceDescription*> propItem(propKey, propIntf);
            pair<map<StringMapKey, const InterfaceDescription*>::const_iterator, bool> ret = components->ifaces.insert(propItem);
            status = ret.second ? ER_OK : ER_BUS_IFACE_ALREADY_EXISTS;
        } else {
            status = ER_BUS_NO_SUCH_INTERFACE;
        }
    }
    lock->Unlock(MUTEX_CONTEXT);
    return status;
}


QStatus ProxyBusObject::AddInterface(const char* ifaceName)
{
    const InterfaceDescription* iface = bus->GetInterface(ifaceName);
    if (!iface) {
        return ER_BUS_NO_SUCH_INTERFACE;
    } else {
        return AddInterface(*iface);
    }
}

size_t ProxyBusObject::GetChildren(ProxyBusObject** children, size_t numChildren)
{
    lock->Lock(MUTEX_CONTEXT);
    size_t count = components->children.size();
    if (children) {
        count = min(count, numChildren);
        for (size_t i = 0; i < count; i++) {
            _ProxyBusObject pbo = (components->children)[i];
            children[i] = &(*pbo);
        }
    }
    lock->Unlock(MUTEX_CONTEXT);
    return count;
}

size_t ProxyBusObject::GetManagedChildren(void* children, size_t numChildren)
{
    _ProxyBusObject** pboChildren = reinterpret_cast<_ProxyBusObject**>(children);
    lock->Lock(MUTEX_CONTEXT);
    size_t count = components->children.size();
    if (pboChildren) {
        count = min(count, numChildren);
        for (size_t i = 0; i < count; i++) {
            pboChildren[i] = new _ProxyBusObject((components->children)[i]);
        }
    }
    lock->Unlock(MUTEX_CONTEXT);
    return count;
}

ProxyBusObject* ProxyBusObject::GetChild(const char* inPath)
{
    /* Add a trailing slash to this path */
    qcc::String pathSlash = (path == "/") ? path : path + '/';

    /* Create absolute version of inPath */
    qcc::String inPathStr = ('/' == inPath[0]) ? inPath : pathSlash + inPath;

    /* Sanity check to make sure path is possible */
    if ((0 != inPathStr.find(pathSlash)) || (inPathStr[inPathStr.length() - 1] == '/')) {
        return NULL;
    }

    /* Find each path element as a child within the parent's vector of children */
    size_t idx = path.size() + 1;
    ProxyBusObject* cur = this;
    lock->Lock(MUTEX_CONTEXT);
    while (idx != qcc::String::npos) {
        size_t end = inPathStr.find_first_of('/', idx);
        qcc::String item = inPathStr.substr(0, end);
        vector<_ProxyBusObject>& ch = cur->components->children;
        vector<_ProxyBusObject>::iterator it = ch.begin();
        while (it != ch.end()) {
            if ((*it)->GetPath() == item) {
                cur = &(*(*it));
                break;
            }
            ++it;
        }
        if (it == ch.end()) {
            lock->Unlock(MUTEX_CONTEXT);
            return NULL;
        }
        idx = ((qcc::String::npos == end) || ((end + 1) == inPathStr.size())) ? qcc::String::npos : end + 1;
    }
    lock->Unlock(MUTEX_CONTEXT);
    return cur;
}

void* ProxyBusObject::GetManagedChild(const char* inPath)
{
    /* Add a trailing slash to this path */
    qcc::String pathSlash = (path == "/") ? path : path + '/';

    /* Create absolute version of inPath */
    qcc::String inPathStr = ('/' == inPath[0]) ? inPath : pathSlash + inPath;

    /* Sanity check to make sure path is possible */
    if ((0 != inPathStr.find(pathSlash)) || (inPathStr[inPathStr.length() - 1] == '/')) {
        return NULL;
    }

    /* Find each path element as a child within the parent's vector of children */
    size_t idx = path.size() + 1;
    ProxyBusObject* cur = this;
    _ProxyBusObject mcur;
    lock->Lock(MUTEX_CONTEXT);
    while (idx != qcc::String::npos) {
        size_t end = inPathStr.find_first_of('/', idx);
        qcc::String item = inPathStr.substr(0, end);
        vector<_ProxyBusObject>& ch = cur->components->children;
        vector<_ProxyBusObject>::iterator it = ch.begin();
        while (it != ch.end()) {
            if ((*it)->GetPath() == item) {
                cur = &(*(*it));
                mcur = *(*it);
                break;
            }
            ++it;
        }
        if (it == ch.end()) {
            lock->Unlock(MUTEX_CONTEXT);
            return NULL;
        }
        idx = ((qcc::String::npos == end) || ((end + 1) == inPathStr.size())) ? qcc::String::npos : end + 1;
    }
    lock->Unlock(MUTEX_CONTEXT);
    if (NULL != cur) {
        return new _ProxyBusObject(mcur);
    }
    return NULL;
}

QStatus ProxyBusObject::AddChild(const ProxyBusObject& child)
{
    qcc::String childPath = child.GetPath();

    /* Sanity check to make sure path is possible */
    if (((path.size() > 1) && (0 != childPath.find(path + '/'))) ||
        ((path.size() == 1) && (childPath[0] != '/')) ||
        (childPath[childPath.length() - 1] == '/')) {
        return ER_BUS_BAD_CHILD_PATH;
    }

    /* Find each path element as a child within the parent's vector of children */
    /* Add new children as necessary */
    size_t idx = path.size() + 1;
    ProxyBusObject* cur = this;
    lock->Lock(MUTEX_CONTEXT);
    while (idx != qcc::String::npos) {
        size_t end = childPath.find_first_of('/', idx);
        qcc::String item = childPath.substr(0, end);
        vector<_ProxyBusObject>& ch = cur->components->children;
        vector<_ProxyBusObject>::iterator it = ch.begin();
        while (it != ch.end()) {
            if ((*it)->GetPath() == item) {
                cur = &(*(*it));
                break;
            }
            ++it;
        }
        if (it == ch.end()) {
            if (childPath == item) {
                ch.push_back(child);
                lock->Unlock(MUTEX_CONTEXT);
                return ER_OK;
            } else {
                const char* tempServiceName = serviceName.c_str();
                const char* tempPath = item.c_str();
                _ProxyBusObject ro(*bus, tempServiceName, tempPath, sessionId);
                ch.push_back(ro);
                cur = ch.empty() ? NULL : &(*(ch.back()));
            }
        }
        idx = ((qcc::String::npos == end) || ((end + 1) == childPath.size())) ? qcc::String::npos : end + 1;
    }
    lock->Unlock(MUTEX_CONTEXT);
    return ER_BUS_OBJ_ALREADY_EXISTS;
}

QStatus ProxyBusObject::RemoveChild(const char* inPath)
{
    QStatus status;

    /* Add a trailing slash to this path */
    qcc::String pathSlash = (path == "/") ? path : path + '/';

    /* Create absolute version of inPath */
    qcc::String childPath = ('/' == inPath[0]) ? inPath : pathSlash + inPath;

    /* Sanity check to make sure path is possible */
    if ((0 != childPath.find(pathSlash)) || (childPath[childPath.length() - 1] == '/')) {
        return ER_BUS_BAD_CHILD_PATH;
    }

    /* Navigate to child and remove it */
    size_t idx = path.size() + 1;
    ProxyBusObject* cur = this;
    lock->Lock(MUTEX_CONTEXT);
    while (idx != qcc::String::npos) {
        size_t end = childPath.find_first_of('/', idx);
        qcc::String item = childPath.substr(0, end);
        vector<_ProxyBusObject>& ch = cur->components->children;
        vector<_ProxyBusObject>::iterator it = ch.begin();
        while (it != ch.end()) {
            if ((*it)->GetPath() == item) {
                if (end == qcc::String::npos) {
                    ch.erase(it);
                    lock->Unlock(MUTEX_CONTEXT);
                    return ER_OK;
                } else {
                    cur = &(*(*it));
                    break;
                }
            }
            ++it;
        }
        if (it == ch.end()) {
            status = ER_BUS_OBJ_NOT_FOUND;
            lock->Unlock(MUTEX_CONTEXT);
            QCC_LogError(status, ("Cannot find object path %s", item.c_str()));
            return status;
        }
        idx = ((qcc::String::npos == end) || ((end + 1) == childPath.size())) ? qcc::String::npos : end + 1;
    }
    /* Shouldn't get here */
    lock->Unlock(MUTEX_CONTEXT);
    return ER_FAIL;
}



QStatus ProxyBusObject::MethodCallAsync(const InterfaceDescription::Member& method,
                                        MessageReceiver* receiver,
                                        MessageReceiver::ReplyHandler replyHandler,
                                        const MsgArg* args,
                                        size_t numArgs,
                                        void* context,
                                        uint32_t timeout,
                                        uint8_t flags) const
{

    QStatus status;
    Message msg(*bus);
    LocalEndpoint localEndpoint = bus->GetInternal().GetLocalEndpoint();
    if (!localEndpoint->IsValid()) {
        return ER_BUS_ENDPOINT_CLOSING;
    }
    /*
     * This object must implement the interface for this method
     */
    if (!ImplementsInterface(method.iface->GetName())) {
        status = ER_BUS_OBJECT_NO_SUCH_INTERFACE;
        QCC_LogError(status, ("Object %s does not implement %s", path.c_str(), method.iface->GetName()));
        return status;
    }
    if (!replyHandler) {
        flags |= ALLJOYN_FLAG_NO_REPLY_EXPECTED;
    }
    /*
     * If the interface is secure or encryption is explicitly rerquested the method call must be encrypted.
     */
    if (SecurityApplies(this, method.iface)) {
        flags |= ALLJOYN_FLAG_ENCRYPTED;
    }
    if ((flags & ALLJOYN_FLAG_ENCRYPTED) && !bus->IsPeerSecurityEnabled()) {
        return ER_BUS_SECURITY_NOT_ENABLED;
    }
    status = msg->CallMsg(method.signature, serviceName, sessionId, path, method.iface->GetName(), method.name, args, numArgs, flags);
    if (status == ER_OK) {
        if (!(flags & ALLJOYN_FLAG_NO_REPLY_EXPECTED)) {
            status = localEndpoint->RegisterReplyHandler(receiver, replyHandler, method, msg, context, timeout);
        }
        if (status == ER_OK) {
            if (b2bEp->IsValid()) {
                status = b2bEp->PushMessage(msg);
            } else {
                BusEndpoint busEndpoint = BusEndpoint::cast(localEndpoint);
                status = bus->GetInternal().GetRouter().PushMessage(msg, busEndpoint);
            }
            if (status != ER_OK) {
                bool unregistered = localEndpoint->UnregisterReplyHandler(msg);
                if (!unregistered) {
                    /*
                     * Unregister failed, so the reply handler must have already been called.
                     *
                     * The contract of this function is that the reply handler will be called iff
                     * the status is ER_OK, so set the status to ER_OK to indicate that the reply
                     * handler was called.
                     */
                    status = ER_OK;
                }
            }
        }
    }
    return status;
}

QStatus ProxyBusObject::MethodCallAsync(const char* ifaceName,
                                        const char* methodName,
                                        MessageReceiver* receiver,
                                        MessageReceiver::ReplyHandler replyHandler,
                                        const MsgArg* args,
                                        size_t numArgs,
                                        void* context,
                                        uint32_t timeout,
                                        uint8_t flags) const
{
    lock->Lock(MUTEX_CONTEXT);
    map<StringMapKey, const InterfaceDescription*>::const_iterator it = components->ifaces.find(StringMapKey(ifaceName));
    if (it == components->ifaces.end()) {
        lock->Unlock(MUTEX_CONTEXT);
        return ER_BUS_NO_SUCH_INTERFACE;
    }
    const InterfaceDescription::Member* member = it->second->GetMember(methodName);
    lock->Unlock(MUTEX_CONTEXT);
    if (NULL == member) {
        return ER_BUS_INTERFACE_NO_SUCH_MEMBER;
    }
    return MethodCallAsync(*member, receiver, replyHandler, args, numArgs, context, timeout, flags);
}

/**
 * Internal context structure used between synchronous method_call and method_return
 */
class SyncReplyContext {
  public:
    SyncReplyContext(BusAttachment& bus) : replyMsg(bus) { }
    Message replyMsg;
    Event event;
};


QStatus ProxyBusObject::MethodCall(const InterfaceDescription::Member& method,
                                   const MsgArg* args,
                                   size_t numArgs,
                                   Message& replyMsg,
                                   uint32_t timeout,
                                   uint8_t flags) const
{
    QStatus status;
    Message msg(*bus);
    LocalEndpoint localEndpoint = bus->GetInternal().GetLocalEndpoint();
    if (!localEndpoint->IsValid()) {
        return ER_BUS_ENDPOINT_CLOSING;
    }
    /*
     * if we're being called from the LocalEndpoint (callback) thread, do not allow
     * blocking calls unless BusAttachment::EnableConcurrentCallbacks has been called first
     */
    if (localEndpoint->IsReentrantCall()) {
        status = ER_BUS_BLOCKING_CALL_NOT_ALLOWED;
        goto MethodCallExit;
    }
    /*
     * This object must implement the interface for this method
     */
    if (!ImplementsInterface(method.iface->GetName())) {
        status = ER_BUS_OBJECT_NO_SUCH_INTERFACE;
        QCC_LogError(status, ("Object %s does not implement %s", path.c_str(), method.iface->GetName()));
        goto MethodCallExit;
    }
    /*
     * If the object or interface is secure or encryption is explicitly requested the method call must be encrypted.
     */
    if (SecurityApplies(this, method.iface)) {
        flags |= ALLJOYN_FLAG_ENCRYPTED;
    }
    if ((flags & ALLJOYN_FLAG_ENCRYPTED) && !bus->IsPeerSecurityEnabled()) {
        status = ER_BUS_SECURITY_NOT_ENABLED;
        goto MethodCallExit;
    }
    status = msg->CallMsg(method.signature, serviceName, sessionId, path, method.iface->GetName(), method.name, args, numArgs, flags);
    if (status != ER_OK) {
        goto MethodCallExit;
    }
    if (flags & ALLJOYN_FLAG_NO_REPLY_EXPECTED) {
        /*
         * Push the message to the router and we are done
         */
        if (b2bEp->IsValid()) {
            status = b2bEp->PushMessage(msg);
        } else {
            BusEndpoint busEndpoint = BusEndpoint::cast(localEndpoint);
            status = bus->GetInternal().GetRouter().PushMessage(msg, busEndpoint);
        }
    } else {
        ManagedObj<SyncReplyContext> ctxt(*bus);
        /*
         * Synchronous calls are really asynchronous calls that block waiting for a builtin
         * reply handler to be called.
         */
        ManagedObj<SyncReplyContext>* heapCtx = new ManagedObj<SyncReplyContext>(ctxt);
        status = localEndpoint->RegisterReplyHandler(const_cast<MessageReceiver*>(static_cast<const MessageReceiver* const>(this)),
                                                     static_cast<MessageReceiver::ReplyHandler>(&ProxyBusObject::SyncReplyHandler),
                                                     method,
                                                     msg,
                                                     heapCtx,
                                                     timeout);
        if (status == ER_OK) {
            if (b2bEp->IsValid()) {
                status = b2bEp->PushMessage(msg);
            } else {
                BusEndpoint busEndpoint = BusEndpoint::cast(localEndpoint);
                status = bus->GetInternal().GetRouter().PushMessage(msg, busEndpoint);
            }
        } else {
            delete heapCtx;
            heapCtx = NULL;
            goto MethodCallExit;
        }

        Thread* thisThread = Thread::GetThread();
        if (status == ER_OK) {
            lock->Lock(MUTEX_CONTEXT);
            if (!isExiting) {
                components->waitingThreads.push_back(thisThread);
                lock->Unlock(MUTEX_CONTEXT);
                /* In case of a timeout, the SyncReplyHandler will be called by the
                 * LocalEndpoint replyTimer. So wait forever to be signalled by the
                 * SyncReplyHandler or ProxyBusObject::DestructComponents(in case the
                 * ProxyBusObject is being destroyed) or this thread is stopped.
                 */
                status = Event::Wait(ctxt->event);
                lock->Lock(MUTEX_CONTEXT);

                std::vector<Thread*>::iterator it = std::find(components->waitingThreads.begin(), components->waitingThreads.end(), thisThread);
                if (it != components->waitingThreads.end()) {
                    components->waitingThreads.erase(it);
                }
            } else {
                status = ER_BUS_STOPPING;
            }
            lock->Unlock(MUTEX_CONTEXT);
        }

        if (status == ER_OK) {
            replyMsg = ctxt->replyMsg;
        } else if ((status == ER_ALERTED_THREAD) && (SYNC_METHOD_ALERTCODE_ABORT == thisThread->GetAlertCode())) {
            /*
             * We can't touch anything in this case since the external thread that was waiting
             * can't know whether this object still exists.
             */
            status = ER_BUS_METHOD_CALL_ABORTED;
        } else if (localEndpoint->UnregisterReplyHandler(msg)) {
            /*
             * The handler was deregistered so we need to delete the context here.
             */
            delete heapCtx;
        }
    }

MethodCallExit:
    /*
     * Let caller know that the method call reply was an error message
     */
    if (status == ER_OK) {
        if (replyMsg->GetType() == MESSAGE_ERROR) {
            status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
        } else if (replyMsg->GetType() == MESSAGE_INVALID) {
            status = ER_FAIL;
        }
    } else {
        replyMsg->ErrorMsg(status, 0);
    }
    return status;
}

QStatus ProxyBusObject::MethodCall(const char* ifaceName,
                                   const char* methodName,
                                   const MsgArg* args,
                                   size_t numArgs,
                                   Message& replyMsg,
                                   uint32_t timeout,
                                   uint8_t flags) const
{
    lock->Lock(MUTEX_CONTEXT);
    map<StringMapKey, const InterfaceDescription*>::const_iterator it = components->ifaces.find(StringMapKey(ifaceName));
    if (it == components->ifaces.end()) {
        lock->Unlock(MUTEX_CONTEXT);
        return ER_BUS_NO_SUCH_INTERFACE;
    }
    const InterfaceDescription::Member* member = it->second->GetMember(methodName);
    lock->Unlock(MUTEX_CONTEXT);
    if (NULL == member) {
        return ER_BUS_INTERFACE_NO_SUCH_MEMBER;
    }
    return MethodCall(*member, args, numArgs, replyMsg, timeout, flags);
}

void ProxyBusObject::SyncReplyHandler(Message& msg, void* context)
{
    ManagedObj<SyncReplyContext>* ctx = reinterpret_cast<ManagedObj<SyncReplyContext>*> (context);

    /* Set the reply message */
    (*ctx)->replyMsg = msg;

    /* Wake up sync method_call thread */
    QStatus status = (*ctx)->event.SetEvent();
    if (ER_OK != status) {
        QCC_LogError(status, ("SetEvent failed"));
    }
    delete ctx;
}

QStatus ProxyBusObject::SecureConnection(bool forceAuth)
{
    if (!bus->IsPeerSecurityEnabled()) {
        return ER_BUS_SECURITY_NOT_ENABLED;
    }
    LocalEndpoint localEndpoint = bus->GetInternal().GetLocalEndpoint();
    if (!localEndpoint->IsValid()) {
        return ER_BUS_ENDPOINT_CLOSING;
    } else {
        AllJoynPeerObj* peerObj = localEndpoint->GetPeerObj();
        if (forceAuth) {
            peerObj->ForceAuthentication(serviceName);
        }
        return peerObj->AuthenticatePeer(MESSAGE_METHOD_CALL, serviceName);
    }
}

QStatus ProxyBusObject::SecureConnectionAsync(bool forceAuth)
{
    if (!bus->IsPeerSecurityEnabled()) {
        return ER_BUS_SECURITY_NOT_ENABLED;
    }
    LocalEndpoint localEndpoint = bus->GetInternal().GetLocalEndpoint();
    if (!localEndpoint->IsValid()) {
        return ER_BUS_ENDPOINT_CLOSING;
    } else {
        AllJoynPeerObj* peerObj =  localEndpoint->GetPeerObj();
        if (forceAuth) {
            peerObj->ForceAuthentication(serviceName);
        }
        return peerObj->AuthenticatePeerAsync(serviceName);
    }
}

QStatus ProxyBusObject::IntrospectRemoteObject(uint32_t timeout)
{
    /* Need to have introspectable interface in order to call Introspect */
    const InterfaceDescription* introIntf = GetInterface(org::freedesktop::DBus::Introspectable::InterfaceName);
    if (!introIntf) {
        introIntf = bus->GetInterface(org::freedesktop::DBus::Introspectable::InterfaceName);
        assert(introIntf);
        AddInterface(*introIntf);
    }

    /* Attempt to retrieve introspection from the remote object using sync call */
    Message reply(*bus);
    const InterfaceDescription::Member* introMember = introIntf->GetMember("Introspect");
    assert(introMember);
    QStatus status = MethodCall(*introMember, NULL, 0, reply, timeout);

    /* Parse the XML reply */
    if (ER_OK == status) {
        QCC_DbgPrintf(("Introspection XML: %s\n", reply->GetArg(0)->v_string.str));
        qcc::String ident = reply->GetSender();
        ident += " : ";
        ident += reply->GetObjectPath();
        status = ParseXml(reply->GetArg(0)->v_string.str, ident.c_str());
    }
    return status;
}

QStatus ProxyBusObject::IntrospectRemoteObjectAsync(ProxyBusObject::Listener* listener,
                                                    ProxyBusObject::Listener::IntrospectCB callback,
                                                    void* context,
                                                    uint32_t timeout)
{
    /* Need to have introspectable interface in order to call Introspect */
    const InterfaceDescription* introIntf = GetInterface(org::freedesktop::DBus::Introspectable::InterfaceName);
    if (!introIntf) {
        introIntf = bus->GetInterface(org::freedesktop::DBus::Introspectable::InterfaceName);
        assert(introIntf);
        AddInterface(*introIntf);
    }

    /* Attempt to retrieve introspection from the remote object using async call */
    const InterfaceDescription::Member* introMember = introIntf->GetMember("Introspect");
    assert(introMember);
    CBContext<Listener::IntrospectCB>* ctx = new CBContext<Listener::IntrospectCB>(this, listener, callback, context);
    QStatus status = MethodCallAsync(*introMember,
                                     this,
                                     static_cast<MessageReceiver::ReplyHandler>(&ProxyBusObject::IntrospectMethodCB),
                                     NULL,
                                     0,
                                     reinterpret_cast<void*>(ctx),
                                     timeout);
    if (ER_OK != status) {
        delete ctx;
    }
    return status;
}

void ProxyBusObject::IntrospectMethodCB(Message& msg, void* context)
{
    QStatus status;
    if (NULL != msg->GetArg(0)) {
        QCC_DbgPrintf(("Introspection XML: %s", msg->GetArg(0)->v_string.str));
    }
    CBContext<Listener::IntrospectCB>* ctx = reinterpret_cast<CBContext<Listener::IntrospectCB>*>(context);

    if (msg->GetType() == MESSAGE_METHOD_RET) {
        /* Parse the XML reply to update this ProxyBusObject instance (plus any new interfaces) */
        qcc::String ident = msg->GetSender();
        ident += " : ";
        ident += msg->GetObjectPath();
        status = ParseXml(msg->GetArg(0)->v_string.str, ident.c_str());
    } else if (::strcmp("org.freedesktop.DBus.Error.ServiceUnknown", msg->GetErrorName()) == 0) {
        status = ER_BUS_NO_SUCH_SERVICE;
    } else {
        status = ER_FAIL;
    }

    /* Call the callback */
    (ctx->listener->*ctx->callback)(status, ctx->obj, ctx->context);
    delete ctx;
}

QStatus ProxyBusObject::ParseXml(const char* xml, const char* ident)
{
    StringSource source(xml);

    /* Parse the XML to update this ProxyBusObject instance (plus any new children and interfaces) */
    XmlParseContext pc(source);
    QStatus status = XmlElement::Parse(pc);
    if (status == ER_OK) {
        XmlHelper xmlHelper(bus, ident ? ident : path.c_str());
        status = xmlHelper.AddProxyObjects(*this, pc.GetRoot());
    }
    return status;
}

ProxyBusObject::~ProxyBusObject()
{
    DestructComponents();
    if (lock) {
        delete lock;
        lock = NULL;
    }
}

void ProxyBusObject::DestructComponents()
{
    if (lock && components) {
        lock->Lock(MUTEX_CONTEXT);
        isExiting = true;
        vector<Thread*>::iterator it = components->waitingThreads.begin();
        while (it != components->waitingThreads.end()) {
            (*it++)->Alert(SYNC_METHOD_ALERTCODE_ABORT);
        }

        if (bus) {
            bus->UnregisterAllHandlers(this);
        }

        /* Wait for any waiting threads to exit this object's members */
        while (components->waitingThreads.size() > 0) {
            lock->Unlock(MUTEX_CONTEXT);
            qcc::Sleep(5);
            lock->Lock(MUTEX_CONTEXT);
        }
        delete components;
        components = NULL;
        lock->Unlock(MUTEX_CONTEXT);
    }
}

ProxyBusObject::ProxyBusObject(BusAttachment& bus, const char* service, const char* path, SessionId sessionId, bool isSecure) :
    bus(&bus),
    components(new Components),
    path(path),
    serviceName(service),
    sessionId(sessionId),
    hasProperties(false),
    lock(new Mutex),
    isExiting(false),
    isSecure(isSecure)
{
    /* The Peer interface is implicitly defined for all objects */
    AddInterface(org::freedesktop::DBus::Peer::InterfaceName);
}

ProxyBusObject::ProxyBusObject() :
    bus(NULL),
    components(NULL),
    sessionId(0),
    hasProperties(false),
    lock(NULL),
    isExiting(false),
    isSecure(false)
{
}

ProxyBusObject::ProxyBusObject(const ProxyBusObject& other) :
    bus(other.bus),
    components(new Components),
    path(other.path),
    serviceName(other.serviceName),
    sessionId(other.sessionId),
    hasProperties(other.hasProperties),
    b2bEp(other.b2bEp),
    lock(new Mutex),
    isExiting(false),
    isSecure(other.isSecure)
{
    *components = *other.components;
}

ProxyBusObject& ProxyBusObject::operator=(const ProxyBusObject& other)
{
    if (this != &other) {
        DestructComponents();
        if (other.components) {
            components = new Components();
            *components = *other.components;
            if (!lock) {
                lock = new Mutex();
            }
        } else {
            components = NULL;
            if (lock) {
                delete lock;
                lock = NULL;
            }
        }
        bus = other.bus;
        path = other.path;
        serviceName = other.serviceName;
        sessionId = other.sessionId;
        hasProperties = other.hasProperties;
        b2bEp = other.b2bEp;
        isExiting = false;
        isSecure = other.isSecure;
    }
    return *this;
}

void ProxyBusObject::SetB2BEndpoint(RemoteEndpoint& b2bEp)
{
    this->b2bEp = b2bEp;
}

}

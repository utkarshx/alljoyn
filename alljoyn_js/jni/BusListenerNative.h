/*
 * Copyright (c) 2011-2012, AllSeen Alliance. All rights reserved.
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
 */
#ifndef _BUSLISTENERNATIVE_H
#define _BUSLISTENERNATIVE_H

#include "BusAttachmentHost.h"
#include "NativeObject.h"
#include <alljoyn/BusListener.h>
#include <qcc/String.h>

class BusListenerNative : public NativeObject {
  public:
    BusListenerNative(Plugin& plugin, NPObject* objectValue);
    virtual ~BusListenerNative();

    void onRegistered(BusAttachmentHost& busAttachment);
    void onUnregistered();
    void onFoundAdvertisedName(const qcc::String& name, ajn::TransportMask transport, const qcc::String& namePrefix);
    void onLostAdvertisedName(const qcc::String& name, ajn::TransportMask transport, const qcc::String& namePrefix);
    void onNameOwnerChanged(const qcc::String& busName, const qcc::String& previousOwner, const qcc::String& newOwner);
    void onPropertyChanged(const qcc::String& propName, const ajn::MsgArg* propValue);
    void onStopping();
    void onDisconnected();
};

#endif // _BUSLISTENERNATIVE_H

/******************************************************************************
 * Copyright (c) 2013, AllSeen Alliance. All rights reserved.
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

#ifndef ABOUTCLIENTSESSIONJOINER_H_
#define ABOUTCLIENTSESSIONJOINER_H_

#include <alljoyn/about/AboutClient.h>
#include <alljoyn/BusAttachment.h>

typedef void (*SessionJoinedCallback)(qcc::String const& busName, ajn::SessionId id);

class AboutClientSessionJoiner : public ajn::BusAttachment::JoinSessionAsyncCB {

  public:

    AboutClientSessionJoiner(const char* name, SessionJoinedCallback callback = 0);

    virtual ~AboutClientSessionJoiner();

    void JoinSessionCB(QStatus status, ajn::SessionId id, const ajn::SessionOpts& opts, void* context);

  private:

    qcc::String m_Busname;

    SessionJoinedCallback m_Callback;
};

#endif /* ABOUTCLIENTSESSIONJOINER_H_ */

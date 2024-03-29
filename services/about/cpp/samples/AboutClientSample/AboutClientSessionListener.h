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

#ifndef ABOUTCLIENTSESSIONLISTENER_H_
#define ABOUTCLIENTSESSIONLISTENER_H_

#include <alljoyn/SessionListener.h>
#include <qcc/String.h>

class AboutClientSessionListener : public ajn::SessionListener {
  public:
    AboutClientSessionListener(qcc::String const& inServiceNAme);

    virtual ~AboutClientSessionListener();

    void SessionLost(ajn::SessionId sessionId);

  private:

    ajn::SessionId mySessionID;

    qcc::String serviceName;

};

#endif /* ABOUTCLIENTSESSIONLISTENER_H_ */



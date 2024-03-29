/**
 * @file
 * This file implements functions to read or write the Persistent GUID from
 * the file PersistentGUID in the System Home Directory
 */

/******************************************************************************
 * Copyright (c) 2012, AllSeen Alliance. All rights reserved.
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

#ifndef _PERSISTGUID_H
#define _PERSISTGUID_H

#include <qcc/platform.h>

#include <qcc/String.h>
#include <qcc/FileStream.h>
#include <qcc/GUID.h>

#include <alljoyn/Status.h>

namespace ajn {

const qcc::String GUIDFileName = qcc::String("/PersistentGUID");

/* Retrieve the Persistent GUID from the file PersistentGUID in the System Home Directory */
QStatus GetPersistentGUID(qcc::GUID128& guid);

/* Set the Persistent GUID in the file PersistentGUID in the System Home Directory */
QStatus SetPersistentGUID(qcc::GUID128 guid);

}

#endif

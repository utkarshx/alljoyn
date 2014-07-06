/**
 * @file
 * MsgRouter is responsible for taking inbound messages and routing them
 * to an appropriate set of endpoints.
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
#include <qcc/platform.h>

#include <cstring>

#include "RuleTable.h"

#include <qcc/Debug.h>
#include <qcc/String.h>
#include <alljoyn/Message.h>

#define QCC_MODULE "ALLJOYN"

using namespace std;
using namespace qcc;

namespace ajn {

Rule::Rule(const char* ruleSpec, QStatus* outStatus) : type(MESSAGE_INVALID), sessionless(SESSIONLESS_NOT_SPECIFIED)
{
    QStatus status = ER_OK;
    const char* pos = ruleSpec;
    const char* finalPos = pos + strlen(ruleSpec);

    while (pos < finalPos) {
        const char* endPos = strchr(pos, ',');
        if (NULL == endPos) {
            endPos = finalPos;
        }
        const char* eqPos = strchr(pos, '=');
        if ((NULL == eqPos) || (eqPos >= endPos)) {
            status = ER_FAIL;
            QCC_LogError(status, ("Premature end of ruleSpec \"%s\"", ruleSpec));
            break;
        }
        ++eqPos;
        const char* begQuotePos = strchr(eqPos, '\'');
        const char* endQuotePos = NULL;
        if (begQuotePos && (++begQuotePos < finalPos)) {
            endQuotePos = strchr(begQuotePos, '\'');
        }
        if (!endQuotePos) {
            status = ER_FAIL;
            QCC_LogError(status, ("Quote mismatch in ruleSpec \"%s\"", ruleSpec));
            break;
        }
        if (0 == strncmp("type", pos, 4)) {
            if (0 == strncmp("signal", begQuotePos, endQuotePos - begQuotePos)) {
                type = MESSAGE_SIGNAL;
            } else if (0 == strncmp("method_call", begQuotePos, endQuotePos - begQuotePos)) {
                type = MESSAGE_METHOD_CALL;
            } else if (0 == strncmp("method_return", begQuotePos, endQuotePos - begQuotePos)) {
                type = MESSAGE_METHOD_RET;
            } else if (0 == strncmp("error", begQuotePos, endQuotePos - begQuotePos)) {
                type = MESSAGE_ERROR;
            } else {
                status = ER_FAIL;
                QCC_LogError(status, ("Invalid type value in ruleSpec \"%s\"", ruleSpec));
                break;
            }
        } else if (0 == strncmp("sender", pos, 6)) {
            sender = qcc::String(begQuotePos, endQuotePos - begQuotePos);
        } else if (0 == strncmp("interface", pos, 9)) {
            iface = qcc::String(begQuotePos, endQuotePos - begQuotePos);
        } else if (0 == strncmp("member", pos, 6)) {
            member = qcc::String(begQuotePos, endQuotePos - begQuotePos);
        } else if (0 == strncmp("path", pos, 4)) {
            path = qcc::String(begQuotePos, endQuotePos - begQuotePos);
        } else if (0 == strncmp("destination", pos, 11)) {
            destination = qcc::String(begQuotePos, endQuotePos - begQuotePos);
        } else if (0 == strncmp("sessionless", pos, 11)) {
            sessionless = ((begQuotePos[0] == 't') || (begQuotePos[0] == 'T')) ? SESSIONLESS_TRUE : SESSIONLESS_FALSE;
        } else if (0 == strncmp("arg", pos, 3)) {
            status = ER_NOT_IMPLEMENTED;
            QCC_LogError(status, ("arg keys are not supported in ruleSpec \"%s\"", ruleSpec));
            break;
        } else {
            status = ER_FAIL;
            QCC_LogError(status, ("Invalid key in ruleSpec \"%s\"", ruleSpec));
            break;
        }
        pos = endPos + 1;
    }
    if (outStatus) {
        *outStatus = status;
    }
}

bool Rule::IsMatch(const Message& msg)
{
    /* The fields of a rule (if specified) are logically anded together */
    if ((type != MESSAGE_INVALID) && (type != msg->GetType())) {
        return false;
    }
    if (!sender.empty() && (0 != strcmp(sender.c_str(), msg->GetSender()))) {
        return false;
    }
    if (!iface.empty() && (0 != strcmp(iface.c_str(), msg->GetInterface()))) {
        return false;
    }
    if (!member.empty() && (0 != strcmp(member.c_str(), msg->GetMemberName()))) {
        return false;
    }
    if (!path.empty() && (0 != strcmp(path.c_str(), msg->GetObjectPath()))) {
        return false;
    }
    if (!destination.empty() && (0 != strcmp(destination.c_str(), msg->GetDestination()))) {
        return false;
    }
    if (((sessionless == SESSIONLESS_TRUE) && !msg->IsSessionless()) ||
        ((sessionless == SESSIONLESS_FALSE) && msg->IsSessionless())) {
        return false;
    }

    // @@ TODO Arg matches are not handled
    return true;
}

qcc::String Rule::ToString() const
{
    return "s:" + sender + " i:" + iface + " m:" + member + " p:" + path + " d:" + destination;
}

QStatus RuleTable::AddRule(BusEndpoint& endpoint, const Rule& rule)
{
    QCC_DbgPrintf(("AddRule for endpoint %s\n  %s", endpoint->GetUniqueName().c_str(), rule.ToString().c_str()));
    Lock();
    rules.insert(std::pair<BusEndpoint, Rule>(endpoint, rule));
    Unlock();
    return ER_OK;
}

QStatus RuleTable::RemoveRule(BusEndpoint& endpoint, Rule& rule)
{
    Lock();

    //std::pair<std::multimap<BusEndpoint, Rule>::const_iterator, RuleIterator> range = rules.equal_range(endpoint);
    std::pair<RuleIterator, RuleIterator> range = rules.equal_range(endpoint);
    while (range.first != range.second) {
        if (range.first->second == rule) {
            rules.erase(range.first);
            break;
        }
        ++range.first;
    }
    Unlock();
    return ER_OK;
}

QStatus RuleTable::RemoveAllRules(BusEndpoint& endpoint)
{
    Lock();
    std::pair<RuleIterator, RuleIterator> range = rules.equal_range(endpoint);
    if (range.first != rules.end()) {
        rules.erase(range.first, range.second);
    }
    Unlock();
    return ER_OK;
}

}

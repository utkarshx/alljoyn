/**
 * @file
 * @brief  Sample implementation of an AllJoyn client. That has an implmentation
 * a secure client that is using a shared keystore file.
 */

/******************************************************************************
 *
 *
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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <alljoyn_c/BusAttachment.h>
#include <alljoyn_c/version.h>
#include <Status.h>

/** Static top level message bus object */
static alljoyn_busattachment g_msgBus = NULL;

/** Static bus listener */
static alljoyn_buslistener g_busListener;

/** Static auth listener */
static alljoyn_authlistener g_authListener;

/*constants*/
static const char* INTERFACE_NAME = "org.alljoyn.bus.samples.secure.SecureInterface";
static const char* OBJECT_NAME = "org.alljoyn.bus.samples.secure";
static const char* OBJECT_PATH = "/SecureService";
static const alljoyn_sessionport SERVICE_PORT = 42;

static QCC_BOOL s_joinComplete = QCC_FALSE;
static alljoyn_sessionid s_sessionId = 0;

static volatile sig_atomic_t g_interrupt = QCC_FALSE;

static void SigIntHandler(int sig)
{
    g_interrupt = QCC_TRUE;
}

/*
 * get a line of input from the the file pointer (most likely stdin).
 * This will capture the the num-1 characters or till a newline character is
 * entered.
 *
 * @param[out] str a pointer to a character array that will hold the user input
 * @param[in]  num the size of the character array 'str'
 * @param[in]  fp  the file pointer the sting will be read from. (most likely stdin)
 *
 * @return returns the same string as 'str' if there has been a read error a null
 *                 pointer will be returned and 'str' will remain unchanged.
 */
char*get_line(char*str, size_t num, FILE*fp)
{
    char*p = fgets(str, num, fp);

    // fgets will capture the '\n' character if the string entered is shorter than
    // num. Remove the '\n' from the end of the line and replace it with nul '\0'.
    if (p != NULL) {
        size_t last = strlen(str) - 1;
        if (str[last] == '\n') {
            str[last] = '\0';
        }
    }
    return p;
}

/* FoundAdvertisedName callback */
void found_advertised_name(const void* context, const char* name, alljoyn_transportmask transport, const char* namePrefix)
{
    printf("found_advertised_name(name=%s, prefix=%s)\n", name, namePrefix);
    if (0 == strcmp(name, OBJECT_NAME)) {
        QStatus status;
        /* We found a remote bus that is advertising basic service's  well-known name so connect to it */
        alljoyn_sessionopts opts = alljoyn_sessionopts_create(ALLJOYN_TRAFFIC_TYPE_MESSAGES, QCC_FALSE, ALLJOYN_PROXIMITY_ANY, ALLJOYN_TRANSPORT_ANY);
        alljoyn_busattachment_enableconcurrentcallbacks(g_msgBus);
        status = alljoyn_busattachment_joinsession(g_msgBus, name, SERVICE_PORT, NULL, &s_sessionId, opts);

        if (ER_OK != status) {
            printf("alljoyn_busattachment_joinsession failed (status=%s)\n", QCC_StatusText(status));
        } else {
            printf("alljoyn_busattachment_joinsession SUCCESS (Session id=%d)\n", s_sessionId);
        }
        alljoyn_sessionopts_destroy(opts);
    }
    s_joinComplete = QCC_TRUE;
}

/* NameOwnerChanged callback */
void name_owner_changed(const void* context, const char* busName, const char* previousOwner, const char* newOwner)
{
    if (newOwner && (0 == strcmp(busName, OBJECT_NAME))) {
        printf("name_owner_changed: name=%s, oldOwner=%s, newOwner=%s\n",
               busName,
               previousOwner ? previousOwner : "<none>",
               newOwner ? newOwner : "<none>");
    }
}

/*
 * This is the local implementation of the an AuthListener.  SrpKeyXListener is
 * designed to only handle SRP Key Exchange Authentication requests.
 *
 * When a Password request (CRED_PASSWORD) comes in using ALLJOYN_SRP_KEYX the
 * code will ask the user to enter the pin code that was generated by the
 * service. The pin code must match the service's pin code for Authentication to
 * be successful.
 *
 * If any other authMechanism is used other than SRP Key Exchange authentication
 * will fail.
 */
QCC_BOOL request_credentials(const void* context, const char* authMechanism, const char* authPeer, uint16_t authCount,
                             const char* userName, uint16_t credMask, alljoyn_credentials credentials)
{
    printf("request_credentials for authenticating %s using mechanism %s\n", authPeer, authMechanism);
    if (strcmp(authMechanism, "ALLJOYN_SRP_KEYX") == 0) {
        if (credMask & ALLJOYN_CRED_PASSWORD) {
            if (authCount <= 3) {
                const int bufSize = 7;
                char buf[7];
                /* Take input from stdin and send it as a chat messages */
                printf("Please enter one time password : ");
                get_line(buf, bufSize, stdin);
                alljoyn_credentials_setpassword(credentials, buf);
                return QCC_TRUE;
            } else {
                return QCC_FALSE;
            }
        }
    }
    return QCC_FALSE;
}

void authentication_complete(const void* context, const char* authMechanism, const char* peerName, QCC_BOOL success)
{
    printf("authentication_complete %s %s\n", authMechanism, success == QCC_TRUE ? "successful" : "failed");
}

/** Main entry point */
int main(int argc, char** argv, char** envArg)
{
    QStatus status = ER_OK;
    alljoyn_interfacedescription testIntf = NULL;
    char* connectArgs = "unix:abstract=alljoyn";
    alljoyn_buslistener_callbacks callbacks = {
        NULL,
        NULL,
        &found_advertised_name,
        NULL,
        &name_owner_changed,
        NULL,
        NULL,
        NULL
    };
    unsigned int count = 0;

    printf("AllJoyn Library version: %s\n", alljoyn_getversion());
    printf("AllJoyn Library build info: %s\n", alljoyn_getbuildinfo());

    /* Install SIGINT handler */
    signal(SIGINT, SigIntHandler);

    /* Create message bus */
    g_msgBus = alljoyn_busattachment_create("SRPSecurityClientC", QCC_TRUE);

    /* Add org.alljoyn.bus.samples.secure.SecureInterface interface */
    status = alljoyn_busattachment_createinterface_secure(g_msgBus, INTERFACE_NAME, &testIntf, AJ_IFC_SECURITY_REQUIRED);
    if (status == ER_OK) {
        alljoyn_interfacedescription_addmember(testIntf, ALLJOYN_MESSAGE_METHOD_CALL, "Ping", "s",  "s", "inStr1,outStr", 0);
        alljoyn_interfacedescription_activate(testIntf);
    } else {
        printf("Failed to create interface %s\n", INTERFACE_NAME);
    }

    /* Start the msg bus */
    if (ER_OK == status) {
        status = alljoyn_busattachment_start(g_msgBus);
        if (ER_OK != status) {
            printf("alljoyn_busattachment_start failed\n");
        } else {
            printf("alljoyn_busattachment started.\n");
        }
    }

    /*
     * enable security
     * note the location of the keystore file has been specified and the
     * isShared parameter is being set to true. So this keystore file can
     * be used by multiple applications
     */
    if (ER_OK == status) {
        alljoyn_authlistener_callbacks callbacks = {
            request_credentials,
            NULL,
            NULL,
            authentication_complete
        };
        g_authListener = alljoyn_authlistener_create(&callbacks, NULL);

        /*
         * alljoyn_busattachment_enablepeersecurity function is called by
         * applications that want to use authentication and encryption. This
         * function call must be made after alljoyn_busattachment_start and
         * before calling alljoyn_busattachment_connect.
         *
         * In most situations a per-application keystore file is generated.
         * However, this code specifies the location of the keystore file and
         * the isShared parameter is being set to QCC_TRUE. The resulting
         * keystore file can be used by multiple applications.
         */
        status = alljoyn_busattachment_enablepeersecurity(g_msgBus, "ALLJOYN_SRP_KEYX", g_authListener,
                                                          "/.alljoyn_keystore/central.ks", QCC_TRUE);
        if (ER_OK != status) {
            printf("alljoyn_busattachment_enablepeersecurity failed (%s)\n", QCC_StatusText(status));
        } else {
            printf("alljoyn_busattachment_enablepeersecurity Successful\n");
        }
    }

    /* Connect to the bus */
    if (ER_OK == status) {
        status = alljoyn_busattachment_connect(g_msgBus, connectArgs);
        if (ER_OK != status) {
            printf("alljoyn_busattachment_connect(\"%s\") failed\n", connectArgs);
        } else {
            printf("alljoyn_busattachment connected to \"%s\"\n", alljoyn_busattachment_getconnectspec(g_msgBus));
        }
    }

    /* Create a bus listener */
    g_busListener = alljoyn_buslistener_create(&callbacks, NULL);

    /* Register a bus listener in order to get discovery indications */
    if (ER_OK == status) {
        alljoyn_busattachment_registerbuslistener(g_msgBus, g_busListener);
        printf("alljoyn_buslistener Registered.\n");
    }

    /* Begin discovery on the well-known name of the service to be called */
    if (ER_OK == status) {
        status = alljoyn_busattachment_findadvertisedname(g_msgBus, OBJECT_NAME);
        if (status != ER_OK) {
            printf("alljoyn_busattachment_findadvertisednamee failed (%s))\n", QCC_StatusText(status));
        }
    }

    /* Wait for join session to complete */
    while (!s_joinComplete && !g_interrupt) {
        if (0 == (count++ % 10)) {
            printf("Waited %u seconds for alljoyn_busattachment_joinsession completion.\n", count / 10);
        }
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100 * 1000);
#endif
    }

    if (status == ER_OK && g_interrupt == QCC_FALSE) {
        alljoyn_message reply;
        alljoyn_msgarg inputs;
        size_t numArgs;

        alljoyn_proxybusobject remoteObj = alljoyn_proxybusobject_create(g_msgBus, OBJECT_NAME, OBJECT_PATH, s_sessionId);
        const alljoyn_interfacedescription alljoynTestIntf = alljoyn_busattachment_getinterface(g_msgBus, INTERFACE_NAME);
        assert(alljoynTestIntf);
        alljoyn_proxybusobject_addinterface(remoteObj, alljoynTestIntf);

        /*
         * Although AllJoyn will automatically try and establish a secure connection
         * when a method call is made.  It will only allow the user the amount of time
         * specified in the methodcall timeout parameter to enter user input.  For the
         * "Ping" method that is 5 seconds.  This is not a reasonable amount of time
         * for a user to see the security password and enter it.
         *
         * By calling alljoyn_proxybusobject_secureconnection the user will have
         * as much time as they need to to enter the security password to secure
         * the connection.
         */
        status = alljoyn_proxybusobject_secureconnection(remoteObj, QCC_TRUE);
        if (ER_OK == status) {
            reply = alljoyn_message_create(g_msgBus);
            inputs = alljoyn_msgarg_array_create(1);
            numArgs = 1;
            status = alljoyn_msgarg_array_set(inputs, &numArgs, "s", "ClientC says Hello AllJoyn!");

            status = alljoyn_proxybusobject_methodcall(remoteObj, INTERFACE_NAME, "Ping", inputs, 1, reply, 5000, 0);
            if (ER_OK == status) {
                char* ping_str;
                status = alljoyn_msgarg_get(alljoyn_message_getarg(reply, 0), "s", &ping_str);
                printf("%s.%s ( path=%s) returned \"%s\"\n", INTERFACE_NAME, "Ping",
                       OBJECT_PATH, ping_str);
            } else {
                printf("alljoyn_proxybusobject_methodcall on %s.%s failed\n", INTERFACE_NAME, "Ping");
            }
            alljoyn_msgarg_destroy(inputs);
            alljoyn_message_destroy(reply);
        }
        alljoyn_proxybusobject_destroy(remoteObj);
    }

    /* Deallocate bus */
    if (g_msgBus) {
        alljoyn_busattachment deleteMe = g_msgBus;
        g_msgBus = NULL;
        alljoyn_busattachment_destroy(deleteMe);
    }

    /* Deallocate bus listener */
    alljoyn_buslistener_destroy(g_busListener);

    /* Deallocate auth listener */
    alljoyn_authlistener_destroy(g_authListener);

    printf("exiting with status %d (%s)\n", status, QCC_StatusText(status));

    return (int) status;
}

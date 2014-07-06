/**
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
 */

package org.alljoyn.bus;

import junit.framework.TestCase;

import org.alljoyn.bus.ifaces.DBusProxyObj;
import org.alljoyn.bus.ifaces.Introspectable;

public class AnnotatedInterfaceTest extends TestCase {

    public AnnotatedInterfaceTest(String name) {
        super(name);
    }

    static {
        System.loadLibrary("alljoyn_java");
    }

    public class AnnotatedService implements InterfaceWithAnnotations, BusObject {
        public String Ping(String inStr) throws BusException {
            return inStr;
        }

        public String Pong(String inStr) throws BusException {
            return inStr;
        }

        public void Pong2(String inStr) throws BusException {
        }

        public void Signal() throws BusException {
        }
    }


    private BusAttachment bus;

    @Override
    public void setUp() throws Exception {
        bus = new BusAttachment(getClass().getName());

        Status status = bus.connect();
        assertEquals(Status.OK, status);

        DBusProxyObj control = bus.getDBusProxyObj();
        DBusProxyObj.RequestNameResult res = control.RequestName("org.alljoyn.bus.AnnotatedInterfaceTest",
                DBusProxyObj.REQUEST_NAME_NO_FLAGS);
        assertEquals(DBusProxyObj.RequestNameResult.PrimaryOwner, res);
    }


    @Override
    public void tearDown() throws Exception {
        DBusProxyObj control = bus.getDBusProxyObj();
        DBusProxyObj.ReleaseNameResult res = control.ReleaseName("org.alljoyn.bus.AnnotatedInterfaceTest");
        assertEquals(DBusProxyObj.ReleaseNameResult.Released, res);

        bus.disconnect();
        bus = null;
    }

    public void testAnnotatedInterface() throws Exception {

        AnnotatedService service = new AnnotatedService();
        assertEquals(Status.OK, bus.registerBusObject(service, "/annotation"));

        ProxyBusObject proxyObj = bus.getProxyBusObject("org.alljoyn.bus.AnnotatedInterfaceTest",
                "/annotation",
                BusAttachment.SESSION_ID_ANY,
                new Class[] { Introspectable.class });

        Introspectable introspectable = proxyObj.getInterface(Introspectable.class);
        String actual = introspectable.Introspect();

        String expected =
                "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n" +
                        "\"http://standards.freedesktop.org/dbus/introspect-1.0.dtd\">\n" +
                        "<node>\n" +
                        "  <interface name=\"org.alljoyn.bus.InterfaceWithAnnotations\">\n" +
                        "    <method name=\"Ping\">\n" +
                        "      <arg type=\"s\" direction=\"in\"/>\n" +
                        "      <arg type=\"s\" direction=\"out\"/>\n" +
                        "      <annotation name=\"name\" value=\"value\"/>\n" +
                        "      <annotation name=\"name2\" value=\"value2\"/>\n" +
                        "    </method>\n" +
                        "    <method name=\"Pong\">\n" +
                        "      <arg type=\"s\" direction=\"in\"/>\n" +
                        "      <arg type=\"s\" direction=\"out\"/>\n" +
                        "    </method>\n" +
                        "    <method name=\"Pong2\">\n" +
                        "      <arg type=\"s\" direction=\"in\"/>\n" +
                        "      <annotation name=\"org.freedesktop.DBus.Deprecated\" value=\"true\"/>\n" +
                        "      <annotation name=\"org.freedesktop.DBus.Method.NoReply\" value=\"true\"/>\n" +
                        "    </method>\n" +
                        "    <signal name=\"Signal\">\n" +
                        "      <annotation name=\"org.freedesktop.DBus.Deprecated\" value=\"true\"/>\n" +
                        "    </signal>\n" +
                        "    <annotation name=\"org.freedesktop.DBus.Deprecated\" value=\"true\"/>\n" +
                        "  </interface>\n" +
                        "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n" +
                        "    <method name=\"Introspect\">\n" +
                        "      <arg name=\"data\" type=\"s\" direction=\"out\"/>\n" +
                        "    </method>\n" +
                        "  </interface>\n" +
                        "</node>\n";
        assertEquals(expected, actual);
        bus.unregisterBusObject(service);
    }
}

/*
 * Copyright (c) 2009-2011, 2013 AllSeen Alliance. All rights reserved.
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

import org.alljoyn.bus.BusException;
import org.alljoyn.bus.annotation.BusInterface;
import org.alljoyn.bus.annotation.BusMethod;
import org.alljoyn.bus.annotation.BusProperty;

/** 
 * PropsInterface is an example of an interface that is published onto
 * alljoyn by org.alljoyn.bus.samples.props.Service and is subscribed
 * to by org.alljoyn.bus.samples.props.Client.  The interface
 * contains two read/write properties: 'StringProp' and 'IntProp'.
 */
@BusInterface
public interface PropsInterface {
    @BusMethod
    public String Ping(String str) throws BusException;
    /**
     * Get the property named 'StringProp'.
     *
     * @return The property value.
     */
    @BusProperty
    public String getStringProp() throws BusException;
    
    /**
     * Set the property named 'StringProp' to the value.
     * 
     * @param value The new value of 'StringProp'.
     */
    @BusProperty
    public void setStringProp(String value) throws BusException;
    
    /**
     * Get the property named 'IntProp'.
     *
     * @return The property value.
     */
    @BusProperty
    public int getIntProp() throws BusException;
    
    /**
     * Set the property named 'IntProp' to the value.
     * 
     * @param value The new value of 'IntProp'.
     */
    @BusProperty
    public void setIntProp(int value) throws BusException;
}


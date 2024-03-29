/*
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

import org.alljoyn.bus.BusException;
import org.alljoyn.bus.annotation.BusInterface;
import org.alljoyn.bus.annotation.BusMethod;
import org.alljoyn.bus.annotation.Secure;

import java.util.Map;

/** 
 * AddressBookInterface is an example of an AllJoyn interface that uses complex 
 * data types (Struct, Dictionary, etc.) to publish a simple addressbook.
 */
@BusInterface
@Secure
public interface AddressBookInterface {

    /**
     * Add or replace a contact in the address book.
     * Only one contact per last name is allowed.
     *
     * @param contact  The contact to add/replace.
     */
    @BusMethod(signature="r")
    public void setContact(Contact contact) throws BusException;

    /**
     * Find a contact in the address book based on last name.
     *
     * @param lastName   Last name of contact.
     * @return Matching contact or null if no such contact.
     */
    @BusMethod(signature="s", replySignature="r")
    public Contact getContact(String lastName) throws BusException;

    /**
     * Return all contacts whose last names match.
     *
     * @param lastNames   Array of last names to find in address book.
     * @return An array of contacts with last name as keys.
     */
    @BusMethod(signature="as", replySignature="ar")
    public Contact[] getContacts(String[] lastNames) throws BusException;
}


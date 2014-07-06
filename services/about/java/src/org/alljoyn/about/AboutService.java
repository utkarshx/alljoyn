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

package org.alljoyn.about;

import java.util.List;

import org.alljoyn.about.client.AboutClient;
import org.alljoyn.about.icon.AboutIconClient;
import org.alljoyn.about.transport.AboutTransport;
import org.alljoyn.bus.BusAttachment;
import org.alljoyn.bus.BusException;
import org.alljoyn.services.common.AnnouncementHandler;
import org.alljoyn.services.common.BusObjectDescription;
import org.alljoyn.services.common.ServiceAvailabilityListener;
import org.alljoyn.services.common.ServiceCommon;
import org.alljoyn.services.common.PropertyStore;

/**
 * An interface for both About client (consumer) and server (producer). 
 * An application may want to implement both, but still use one bus, so for convenience both functionalities are encapsulated here.
 */
public interface AboutService extends ServiceCommon 
{
	/**
	 * The About protocol version.
	 */
	public static final int PROTOCOL_VERSION = 1;
	
	/**
	 * The About AllJoyn BusInterface name
	 */
	public static final String ANNOUNCE_IFNAME   = AboutTransport.INTERFACE_NAME;
	
	/**
	 * The Announcement signal name
	 */
	public static final String SIGNAL_NAME       = "Announce";

	/**
	 * Starts the {@link AboutService} in client mode.
	 * @param bus the AllJoyn bus attachment
	 * @throws Exception Is thrown if a failure has occurred while starting the client
	 */
	public void startAboutClient(BusAttachment bus) throws Exception; 
	
	/**
	 * Creates an About client for a peer to receive Announcement signals and to call remote methods. 
	 * @param peerName The bus unique name of the remote About server
	 * @param serviceAvailabilityListener listener for connection loss
	 * @param port the peer's bound port of the About server 
	 * @return AboutClient to create a session with the peer
	 * @throws Exception
	 */
	public AboutClient createAboutClient(String peerName, ServiceAvailabilityListener serviceAvailabilityListener, short port) throws Exception;

	/**
	 * Create an Icon client for a peer. 
	 * @param peerName The bus unique name of the remote About server
	 * @param serviceAvailabilityListener listener for connection loss
	 * @param port the peer's bound port of the About server  
	 * @return AboutIconClient to create a session with the peer
	 * @throws Exception
	 */
	public AboutIconClient createAboutIconClient(String peerName, ServiceAvailabilityListener serviceAvailabilityListener, short port) throws BusException;
	
	/**
	 * Stops client mode. Disconnect all sessions.
	 * @throws Exception
	 */
	public void stopAboutClient() throws Exception;
	
	/**
	 * Register a handler for Announcements.
	 * @param handler
	 * @see AboutTransport#Announce(short, short, BusObjectDescription[], java.util.Map)
	 */
	public void addAnnouncementHandler(AnnouncementHandler handler);
	
	/**
	 * Unregister a handler for Announcements.
	 * @param handler
	 */
	public void removeAnnouncementHandler(AnnouncementHandler handler);

	/**
	 * Start server mode.  The application creates the BusAttachment
	 * @param port the bound AllJoyn session port. The application binds the port, and the about server announces it.
	 * @param propertyStore a container of device/application properties. 
	 * @param m_bus the AllJoyn bus attachment.
	 * @throws Exception
	 * @see AboutKeys
	 */
	public void startAboutServer(short port, PropertyStore propertyStore, BusAttachment m_bus) throws Exception;
	
	/**
	 * Stop server mode.
	 * @throws Exception
	 */
	public void stopAboutServer() throws Exception;

	/**
	 * Add a BusObject and the BusInterfaces that it implements, to the server's Announcement.
	 * @param objPath the path of the BusObject
	 * @param interfaces the BusInterfaces that the BusObject implements.
	 * @see BusObjectDescription
	 * @see AboutTransport#Announce(short, short, BusObjectDescription[], java.util.Map)
	 */
	public void addObjectDescription(String objPath, String [] interfaces);
	
	/**
	 * Remove the BusInterfaces that the given BusObject implements, from the server's Announcement
	 * @param objPath the path of the BusObject
	 * @param interfaces the interfaces to remove. Note: only the passed interfaces will be removed.
	 */
	public void removeObjectDescription(String objPath, String [] interfaces);
	
	/**
	 * A plural call of addBusObjectDescription
	 * @param addBusObjectDescriptions
	 * @see #addObjectDescription(String, String[])
	 */
	public void addObjectDescriptions(List<BusObjectDescription> addBusObjectDescriptions);

	/**
	 * A plural call of removeBusObjectDescription
	 * @param removeBusObjectDescriptions
	 * @see #removeObjectDescription(String, String[])
	 */
	public void removeObjectDescriptions(List<BusObjectDescription> removeBusObjectDescriptions);
	
	/**
	 * Make the server send an Announcement
	 */
	public void announce();

	/**
	 * Register the application icon with the server
	 */
	public void registerIcon(String mimetype, String url, byte[] content) throws Exception;

	/**
	 * Unregister the application icon
	 */
	public void unregisterIcon() throws Exception;
}

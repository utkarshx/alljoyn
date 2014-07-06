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

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.Vector;

import org.alljoyn.about.client.AboutClient;
import org.alljoyn.about.client.AboutClientImpl;
import org.alljoyn.about.icon.AboutIconClient;
import org.alljoyn.about.icon.AboutIconClientImpl;
import org.alljoyn.about.transport.AboutTransport;
import org.alljoyn.about.transport.IconTransport;
import org.alljoyn.bus.BusAttachment;
import org.alljoyn.bus.BusException;
import org.alljoyn.bus.BusListener;
import org.alljoyn.bus.BusObject;
import org.alljoyn.bus.SignalEmitter;
import org.alljoyn.bus.Status;
import org.alljoyn.bus.Variant;
import org.alljoyn.bus.annotation.BusSignalHandler;
import org.alljoyn.services.common.AnnouncementHandler;
import org.alljoyn.services.common.BusObjectDescription;
import org.alljoyn.services.common.LanguageNotSupportedException;
import org.alljoyn.services.common.PropertyStore;
import org.alljoyn.services.common.PropertyStore.Filter;
import org.alljoyn.services.common.PropertyStoreException;
import org.alljoyn.services.common.ServiceAvailabilityListener;
import org.alljoyn.services.common.ServiceCommonImpl;
import org.alljoyn.services.common.utils.TransportUtil;

/**
 * An implementation of the AboutService interface 
 */
public class AboutServiceImpl extends ServiceCommonImpl implements AboutService
{

	/********* Client *********/	
	// the AnnouncmentReceiver will specify the interface and signal names.
	private static final String ANNOUNCE_MATCH_RULE	= "interface='" + ANNOUNCE_IFNAME + "'";


	private static AboutServiceImpl m_instance      = new AboutServiceImpl();
	private Vector<AnnouncementHandler> m_announcementHandlers;
	private BusListener m_busListeners;

	private AnnouncmentReceiver m_announcmentReceiver;

	/********* Sender *********/

	private PropertyStore m_propertyStore;
	private AboutTransport m_aboutInterface;
	private IconTransport m_iconInterface;
	private Announcer m_announcer;

	private AboutTransport m_announcementEmitter;
	private Set<BusObjectDescription> m_ObjectDescriptions = new HashSet<BusObjectDescription>();

	private short m_servicesPort;


	private String m_iconMimeType;
	private String m_iconUrl;
	private byte[] m_iconContent;


	/********* General *********/
	
	/**
	 * @return {@link AboutService} instance
	 */
	public static AboutService getInstance()
	{
		return m_instance;
	}

	/**
	 * Constructor
	 */
	private AboutServiceImpl() 
	{
		super();
		TAG = "ioe" + AboutServiceImpl.class.getSimpleName();
		m_announcementHandlers = new Vector<AnnouncementHandler>();
	}


	//	======================
	/********* Client *********/

	/**
	 * @see org.alljoyn.about.AboutService#startAboutClient(org.alljoyn.bus.BusAttachment)
	 */
	@Override
	public void startAboutClient(BusAttachment bus) throws Exception 
	{
		super.startClient();
		setBus(bus);
		registerDeviceListener();
		registerAnnouncementReceiver();
	}

	/**
	 * @see org.alljoyn.about.AboutService#addAnnouncementHandler(org.alljoyn.services.common.AnnouncementHandler)
	 */
	@Override
	public void addAnnouncementHandler(AnnouncementHandler handler)
	{
		if ( handler == null ) {
			throw new IllegalArgumentException("The AnnouncementHandler can't be null");
		}
		
		m_announcementHandlers.add(handler);
	}

	/**
	 * @see org.alljoyn.about.AboutService#removeAnnouncementHandler(org.alljoyn.services.common.AnnouncementHandler)
	 */
	@Override
	public void removeAnnouncementHandler(AnnouncementHandler handler)
	{
		if ( handler == null ) {
			throw new IllegalArgumentException("The AnnouncementHandler can't be null");
		}
		
		m_announcementHandlers.remove(handler);
	}

	/**
	 * Register the {@link BusListener} and call to findAdvertisedName
	 */
	private void registerDeviceListener()
	{
		m_busListeners = new BusListener() 
		{
			@Override
			public void foundAdvertisedName(final String name, short transport, String namePrefix) 
			{
				getLogger().debug(TAG, "MyBusListener.foundAdvertisedName: '" + name + "'");
				// Do nothing. devices are discovered by Announcements
			}

			public void nameOwnerChanged(String busName, String previousOwner, String newOwner)
			{
				getLogger().debug(TAG,String.format("MyBusListener.nameOwnerChanged(%s, %s, %s)", busName, previousOwner, newOwner));
			}

			public void lostAdvertisedName(String name, short transport, String namePrefix)
			{
				getBus().enableConcurrentCallbacks();
				
				getLogger().debug(TAG, String.format("MyBusListener.lostAdvertisedName(%s, 0x%04x, %s)", name, transport, namePrefix));
				for (int i = 0; i < m_announcementHandlers.size(); i++)
				{
					AnnouncementHandler current = m_announcementHandlers.elementAt(i);
					current.onDeviceLost(name);
				}
			}
		};
		getBus().registerBusListener(m_busListeners);
		
		// By convention, servers use their unique bus name (:Yd_derf) as their well known name. 
		Status status = getBus().findAdvertisedName(":");
		if ( status != Status.OK ) {
			getLogger().error(TAG, "Failed to call findAdvertisedName, Status: '" + status + "'");
			throw new AboutServiceException("Failed to call findAdvertisedName, Status: '" + status + "'");
		}
		
	}//registerDeviceListener

	/**
	 * Register AnnouncementReceiver to receive Announcement signals
	 */
	private void registerAnnouncementReceiver()
	{
		BusAttachment bus = getBus(); 
		
		// for announce
		m_announcmentReceiver = new AnnouncmentReceiver();
		Status status = bus.registerBusObject(m_announcmentReceiver,  AboutTransport.OBJ_PATH);
		getLogger().info(TAG, "BusAttachment.registerBusObject(AnnouncmentReceiver) status = '" + status + "', ObjPath: '" + AboutTransport.OBJ_PATH + "'");
		if ( status != Status.OK ) {
			throw new AboutServiceException("Register BusObject of Announcement receiver has failed, Status: '" + status + "'");
		}
		
		status = bus.registerSignalHandlers(m_announcmentReceiver);
		getLogger().info(TAG, "BusAttachment.registerSignalHandlers(AnnouncmentReceiver) status = '" + status + "'");
		if ( status != Status.OK ) {
			throw new AboutServiceException("Register Announcement signal handler has failed, Status: '" + status + "'");
		}
		
		status = bus.addMatch(ANNOUNCE_MATCH_RULE);
		getLogger().info(TAG, "BusAttachment.addMatch() status = " + status);
		if ( status != Status.OK ) {
			throw new AboutServiceException("Failed to call AddMatch for the rule: '" + ANNOUNCE_MATCH_RULE + "', Status: '" + status + "'");
		}
		
	}//registerAnnouncementReceiver

	/**
	 * The class is an Announcement signal receiver.
	 * The class implements {@link AboutTransport} interface that extends {@link BusObject}
	 */
	private class AnnouncmentReceiver implements AboutTransport
	{
		/**
		 * Signal handler
		 * @see org.alljoyn.about.transport.AboutTransport#Announce(short, short, org.alljoyn.services.common.BusObjectDescription[], java.util.Map)
		 */
		@Override
		@BusSignalHandler(iface = ANNOUNCE_IFNAME, signal = SIGNAL_NAME)
		public void Announce(short version, short port, BusObjectDescription[] objectDescriptions, Map<String, Variant> aboutData) 
		{
			BusAttachment bus = getBus();
			bus.enableConcurrentCallbacks();
			
			String sender = bus.getMessageContext().sender;

			for (int i = 0; i < m_announcementHandlers.size(); i++)
			{
				AnnouncementHandler current = m_announcementHandlers.elementAt(i);
				current.onAnnouncement(sender, port, objectDescriptions, aboutData);
			}

		}//Announce

		/**                                                                                                                          
		 * Intentionally empty implementation. Since class is only used as a signal handler, it will never be called directly.
		 */
		@Override
		public short getVersion() throws BusException
		{
			return 0;
		}

		@Override
		public Map<String, Variant> GetAboutData(String languageTag)
				throws BusException {
			return null;
		}

		@Override
		public BusObjectDescription[] GetObjectDescription()
				throws BusException {
			return null;
		}
	}//AnnouncmentReceiver

	/**
	 * @see org.alljoyn.about.AboutService#stopAboutClient()
	 */
	@Override
	public void stopAboutClient() throws Exception 
	{
		BusAttachment bus = getBus();
		
		if (bus != null)
		{
			if (m_announcmentReceiver != null)
			{
				bus.unregisterSignalHandlers(m_announcmentReceiver);
				bus.unregisterBusObject(m_announcmentReceiver);
			}
			
			if ( m_busListeners != null ) {
				bus.unregisterBusListener(m_busListeners);
			}
			
			m_announcementHandlers.removeAllElements();
			m_announcmentReceiver = null;
			m_busListeners        = null;
		}
		
		super.stopClient();
	}//stopAboutClient

	/**
	 * @see org.alljoyn.about.AboutService#createAboutClient(java.lang.String, org.alljoyn.services.common.ServiceAvailabilityListener, short)
	 */
	@Override
	public AboutClient createAboutClient(String peerName, ServiceAvailabilityListener serviceAvailabilityListener, short port) throws Exception
	{
		return  new AboutClientImpl(peerName, getBus(), serviceAvailabilityListener, port);
	}

	/**
	 * @see org.alljoyn.about.AboutService#createAboutIconClient(java.lang.String, org.alljoyn.services.common.ServiceAvailabilityListener, short)
	 */
	@Override
	public AboutIconClient createAboutIconClient(String peerName,ServiceAvailabilityListener serviceAvailabilityListener, short port)
			throws BusException {

		return  new AboutIconClientImpl(peerName, getBus(), serviceAvailabilityListener, port);
	}
	
	// ======================================  //
	
	
	// **************** Server *************** // 

	
	/**
	 * @see org.alljoyn.about.AboutService#startAboutServer(short, org.alljoyn.services.common.PropertyStore, org.alljoyn.bus.BusAttachment)
	 */
	@Override
	public void startAboutServer(short port, PropertyStore propertyStore, BusAttachment bus) throws Exception {

		super.startServer();
		
		if ( propertyStore != null ) {
			m_propertyStore = propertyStore;
		}
		else {
			throw new IllegalArgumentException("PropertyStore can't be NULL");
		}
		
		m_servicesPort  = port;
		setBus(bus);

		registerAboutInterface();

		addObjectDescription(AboutTransport.OBJ_PATH, new String [] {AboutTransport.INTERFACE_NAME});
	}

	/**
	 * @see org.alljoyn.about.AboutService#registerIcon(java.lang.String, java.lang.String, byte[])
	 */
	@Override
	public void registerIcon(String mimetype, String url, byte[] content) throws Exception {
		
		super.startServer();
		//Init the image
		m_iconMimeType = mimetype;
		m_iconUrl      = url;
		m_iconContent  = content;		
		
		registerAboutIconInterface();
		addObjectDescription(IconTransport.OBJ_PATH, new String [] {IconTransport.INTERFACE_NAME});
	}
	
	/**
	 * Creates the Announcer object which is responsible for sending sessionless Announce signal 
	 * @throws Exception
	 */
	private void createAnnouncer() throws Exception
	{
		if (m_announcer == null)
		{
			
			if ( !isServerRunning() ) {   // Its a problem, the AboutServer had to be started prior to this moment
				throw new AboutServiceException("The AboutServer hasn't been started, can't continue working"); 
			}
			
			m_announcer = new Announcer()
			{

				private boolean m_isAnnouncing = true;

				/**
				 * Announce the about table. Whoever called addMatch("sessionless='t',interface='org.alljoyn.About'") will be notified
				 */
				@Override
				public void announce()
				{
					if (m_announcementEmitter != null && isAnnouncing())
					{
						BusObjectDescription[] objectDescriptionArray = m_ObjectDescriptions.toArray(new BusObjectDescription[] {});
						Map<String, Object> persistedAnnounceMap      = new HashMap<String, Object>();
						
						try {
							m_propertyStore.readAll("", Filter.ANNOUNCE, persistedAnnounceMap);
						} catch (PropertyStoreException pse) {
							throw new AboutServiceException("Failed to read announcable properties from the PropertyStore, Error: '" + pse.getMessage() + "'");
						}
						
						Map<String, Variant>announceMap = TransportUtil.toVariantMap(persistedAnnounceMap);
						m_announcementEmitter.Announce((short)PROTOCOL_VERSION, m_servicesPort, objectDescriptionArray, announceMap);
					}
					
				}//announce

				@Override
				public boolean isAnnouncing()
				{
					return m_isAnnouncing;
				}

				@Override
				public void setAnnouncing(boolean enable)
				{
					m_isAnnouncing = enable;
				}

				@Override
				public void addBusObjectAnnouncements(List<BusObjectDescription> descriptions)
				{
					m_ObjectDescriptions.addAll(descriptions);
				}

				@Override
				public void removeBusObjectAnnouncements(List<BusObjectDescription> descriptions)
				{

					for (BusObjectDescription removeMe : descriptions)
					{

						BusObjectDescription match = null;
						for (BusObjectDescription boDescription : m_ObjectDescriptions)
						{
							if (removeMe.getPath().equalsIgnoreCase(
									boDescription.getPath()))
							{
								if (removeMe.getInterfaces().length == 0)
								{
									// remove all interfaces => remove busobject
									match = boDescription;
									break;
								} else
								{
									if (Arrays.equals(removeMe.getInterfaces(),
											boDescription.getInterfaces()))
									{
										// remove all interfaces => remove busobject
										match = boDescription;
										break;
									} else
									{
										// keep bus object, purge interfaces
										List<String> newInterfaces = new ArrayList<String>(
												Arrays.asList(boDescription.interfaces));
										newInterfaces.removeAll(Arrays
												.asList(removeMe
														.getInterfaces()));
										boDescription
										.setInterfaces(newInterfaces
												.toArray(new String[] {}));
									}
								}
							}
						}

						if (match != null)
						{
							m_ObjectDescriptions.remove(match);
						}
					}

				}//removeBusObjectAnnouncements
			};

			SignalEmitter emitter = new SignalEmitter(m_aboutInterface,
					SignalEmitter.GlobalBroadcast.Off);
			emitter.setSessionlessFlag(true);
			emitter.setTimeToLive(0);
			m_announcementEmitter = emitter.getInterface(AboutTransport.class);
		}//if :: announcer == null		

	}//createAnnouncer

	/**
	 * Registers the {@link AboutInterface} that implements the {@link BusObject} interface
	 * @throws Exception
	 */
	private void registerAboutInterface() throws Exception
	{
		m_aboutInterface = new AboutInterface();

		Status status = getBus().registerBusObject(m_aboutInterface, AboutTransport.OBJ_PATH);
		getLogger().debug(TAG, String.format("BusAttachment.registerBusObject(m_aboutConfigInterface): %s", status));
		if (status != Status.OK) {
			throw new AboutServiceException("Failed to register the AboutInterface on the bus, Status: '" + status + "'");
		}
	}
	
	private void registerAboutIconInterface() throws Exception
	{
		m_iconInterface = new IconInterface();

		Status status = getBus().registerBusObject(m_iconInterface, IconTransport.OBJ_PATH);
		getLogger().debug(TAG, String.format("BusAttachment.registerBusObject(m_IconInterface): %s", status));
		if (status != Status.OK) {
			return;
		}
	}
	
	/**
	 * @see org.alljoyn.about.AboutService#addObjectDescription(java.lang.String, java.lang.String[])
	 */
	@Override
	public void addObjectDescription(String objPath, String [] interfaces)
	{
		if ( objPath == null ) {
			throw new IllegalArgumentException("ObjectPath can't be NULL");
		}
		
		if ( interfaces == null ) {
			throw new IllegalArgumentException("The interfaces array can't be NULL");
		}
		
		List<BusObjectDescription> addBusObjectDescriptions = new ArrayList<BusObjectDescription>(2);
		BusObjectDescription ajAboutBusObjectDescription    = new BusObjectDescription();
		ajAboutBusObjectDescription.setPath(objPath);
		ajAboutBusObjectDescription.setInterfaces(interfaces);
		addBusObjectDescriptions.add(ajAboutBusObjectDescription);
		
		Announcer announcer = getAnnouncer();
		if ( announcer == null ) {
			throw new AboutServiceException("AboutServer has not been initialized, no announcer");
		}
		
		announcer.addBusObjectAnnouncements(addBusObjectDescriptions);
	}

	/**
	 * @see org.alljoyn.about.AboutService#addObjectDescriptions(java.util.List)
	 */
	@Override
	public void addObjectDescriptions(List<BusObjectDescription> addBusObjectDescriptions)
	{
		Announcer announcer = getAnnouncer();
		if ( announcer == null ) {
			throw new AboutServiceException("AboutServer has not been initialized, no announcer");
		}
		
		if ( addBusObjectDescriptions == null ) {
			throw new IllegalArgumentException("addBusObjectDescriptions can't be NULL");
		}
		
		announcer.addBusObjectAnnouncements(addBusObjectDescriptions);
	}

	@Override
	public void removeObjectDescription(String objPath, String[] interfaces) {

		List<BusObjectDescription> addBusObjectDescriptions = new ArrayList<BusObjectDescription>(2);
		BusObjectDescription busObjectDescription = new BusObjectDescription();
		busObjectDescription.setPath(objPath);
		busObjectDescription.setInterfaces(interfaces);
		addBusObjectDescriptions.add(busObjectDescription);

		Announcer announcer = getAnnouncer();
		if ( announcer == null ) {
			throw new AboutServiceException("AboutServer has not been initialized, no announcer");
		}
		
		announcer.removeBusObjectAnnouncements(addBusObjectDescriptions);
	}

	@Override
	public void removeObjectDescriptions(List<BusObjectDescription> removeBusObjectDescriptions)
	{
		Announcer announcer = getAnnouncer();
		if ( announcer == null ) {
			throw new AboutServiceException("AboutServer has not been initialized, no announcer");
		}
		
		announcer.removeBusObjectAnnouncements(removeBusObjectDescriptions);
	}

	@Override
	public void stopAboutServer() throws Exception 
	{
		if(getBus() != null)
		{
			if (m_aboutInterface != null)
			{
				getBus().unregisterBusObject(m_aboutInterface);
			}
			m_ObjectDescriptions.clear();
			
			m_announcementEmitter = null;
			m_announcer = null;
		}
		
		super.stopServer();
	}

	@Override
	public void unregisterIcon() throws Exception {
		
		if(getBus() != null){
			if (m_iconInterface != null){
				getBus().unregisterBusObject(m_iconInterface);
			}
			removeObjectDescription(IconTransport.OBJ_PATH, new String [] {IconTransport.INTERFACE_NAME});
		}
	}

	/**
	 * @return The handle for triggering announcements, or NULL on failure
	 */
	public Announcer getAnnouncer()
	{
		try {
			createAnnouncer();
		}
		catch (Exception e) {
			getLogger().error(TAG, "Fail to create Announcer, Error: '" + e.getMessage() + "'");
			e.printStackTrace();
			return null;
		} // will create new only if null.
		
		return m_announcer;
	}

	/**
	 * The AllJoyn BusObject that exposes the About interface of this device over the Bus.
	 */
	private class AboutInterface implements BusObject, AboutTransport 
	{

		@Override
		public Map<String, Variant> GetAboutData(String languageTag) throws BusException
		{
			
			Map<String, Object> persistedAboutMap = new HashMap<String, Object>();
			try {
				m_propertyStore.readAll(languageTag, Filter.READ, persistedAboutMap);
			} catch (PropertyStoreException e) {
				if (e.getReason() == PropertyStoreException.UNSUPPORTED_LANGUAGE)
				{
					throw new LanguageNotSupportedException();
				}
				else
				{
					e.printStackTrace();
				}
			}
			Map<String,Variant> aboutMap = TransportUtil.toVariantMap(persistedAboutMap);
			return aboutMap;
		}

		@Override
		public BusObjectDescription[] GetObjectDescription()
				throws BusException
				{
			return m_ObjectDescriptions.toArray(new BusObjectDescription[]{});
				}

		@Override
		public short getVersion() throws BusException
		{
			return PROTOCOL_VERSION;
		}

		/**                                                                                                                          
		 * Intentionally empty implementation of ServiceAnnouncement method.  
		 * Since this method is only used as a signal emitter, it will never be called directly.
		 */

		@Override
		public void Announce(short version, short port, BusObjectDescription[] objectDescriptions,
				Map<String, Variant> aboutData)
		{}

	}


	@Override
	public List<BusObjectDescription> getBusObjectDescriptions() {
		// add self. announcer isn't ready yet, so no announcement will go
		List<BusObjectDescription> addBusObjectDescriptions = new ArrayList<BusObjectDescription>(2);
		BusObjectDescription ajAboutBusObjectDescription = new BusObjectDescription();
		ajAboutBusObjectDescription.setPath(AboutTransport.OBJ_PATH);
		ajAboutBusObjectDescription.setInterfaces(new String[]{AboutTransport.INTERFACE_NAME});
		addBusObjectDescriptions.add(ajAboutBusObjectDescription);
		return addBusObjectDescriptions;
	}

	@Override
	public void announce() {
		Announcer announcer = getAnnouncer();
		if ( announcer == null ) {
			throw new AboutServiceException("AboutServer has not been initialized, no announcer");
		}
		
		announcer.announce();
	}

	/**
	 * The AllJoyn BusObject that exposes the application icon
	 */
	private class IconInterface implements BusObject, IconTransport 
	{

		@Override
		public short getVersion() throws BusException {
			return PROTOCOL_VERSION;
		}

		@Override
		public String getMimeType() throws BusException {
			return m_iconMimeType;
		}

		@Override
		public int getSize() throws BusException {
			if(m_iconContent != null)
				return m_iconContent.length;
			else
				return 0;
		}

		@Override
		public String GetUrl() throws BusException {
			return m_iconUrl;
		}

		@Override
		public byte[] GetContent() throws BusException {
			return m_iconContent;
		}
		
	}

}

/**
 * @file
 * Singleton for the AllJoyn IP Name Service
 */

/******************************************************************************
 * Copyright (c) 2012, 2014, AllSeen Alliance. All rights reserved.
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

#ifndef _IP_NAME_SERVICE_H
#define _IP_NAME_SERVICE_H

#ifndef __cplusplus
#error Only include IpNameService.h in C++ code.
#endif

#include <qcc/String.h>
#include <qcc/IPAddress.h>
#include <qcc/IfConfig.h>
#include <alljoyn/TransportMask.h>

#include <alljoyn/Status.h>
#include <Callback.h>

namespace ajn {

class IpNameServiceImpl;

/**
 * @brief API to provide an implementation dependent IP (Layer 3) Name Service
 * singleton for AllJoyn.
 *
 * The IpNameService is implemented as a Meyers singleton, so a static method is
 * requred to get a reference to the sinle instance of the singleton.  The
 * underlying object will be constructed the first time this method is called.
 *
 * We expect that there may be zero to N transports running under control of a
 * given daemon that will need the name service.  Unfortunately, since we may
 * have a bundled daemon running, we are going to have to admit the possibility
 * of the ++ static initialization order fiasco.
 *
 * The BundledRouter is a static and so the destruction order of the two objects
 * (him and us) depends on whatever the linker decides the order should be.
 *
 * We use a Meyers Singleton, and therefore we defer construction of the
 * underlying object to the time of first use, which is going to be when the
 * transports are created, well after main() has started.  We want to have all
 * of the tear-down of the threads performed before main() ends, so we need to
 * have knowledge of when the singleton is no longer required.  We reference
 * count instances of transports that register with the IpNameService to
 * accomplish this.
 *
 * Whenever a transport comes up and wants to interact with the IpNameService it
 * calls our static Instance() method to get a reference to the underlying name
 * service object.  This accomplishes the construction on first use idiom.  This
 * is a very lightweight operation that does almost nothing.  The first thing
 * that a transport must do is to Acquire() the instance of the name service,
 * which is going to bump a reference count and do the hard work of starting the
 * IpNameService.  The last thing a transport must do is to Release() the
 * instance of the name service.  This will do the work of stopping and joining
 * the name service threads when the last reference is released.  Since this
 * operation may block waiting for the name service thread to exit, this should
 * only be done in the transport's Join() method.
 */
class IpNameService {
  public:

    /**
     * @brief Return a reference to the IpNameService singleton.
     */
    static IpNameService& Instance()
    {
        static IpNameService ipNameService;
        return ipNameService;
    }

    /**
     * @brief Notify the singleton that there is a transport coming up that will
     * be using the IP name service.
     *
     * Whenever a transport comes up and wants to interact with the
     * IpNameService it calls our static Instance() method to get a reference to
     * the underlying name service object.  This accomplishes the construction
     * on first use idiom.  This is a very lightweight operation that does
     * almost nothing.  The first thing that a transport must do is to Acquire()
     * the instance of the name service, which is going to bump a reference
     * count and do the hard work of starting the IpNameService.  A transport
     * author can think of this call as performin a reference-counted Start()
     *
     * @param guid A string containing the GUID assigned to the daemon which is
     *     hosting the name service.
     * @param loopback If true, receive our own advertisements.
     *     Typically used for test programs to listen to themselves talk.
     */
    void Acquire(const qcc::String& guid, bool loopback = false);

    /**
     * @brief Notify the singleton that there a transport is going down and will no
     * longer be using the IP name service.
     *
     * The last thing a transport must do is to Release() the instance of the
     * name service.  This will do the work of stopping and joining the name
     * service threads when the last reference is released.  Since this
     * operation may block waiting for the name service thread to exit, this
     * should only be done in the transport's Join() method.
     *
     * @param guid A string containing the GUID assigned to the daemon which is
     *     hosting the name service.
     * @param loopback If true, receive our own advertisements.
     *     Typically used for test programs to listen to themselves talk.
     */
    void Release();

    /**
     * @brief Determine if the IpNameService singleton has been started.
     *
     * Basically, this determines if the reference count is strictly positive.
     *
     * @return True if the singleton has been started, false otherwise.
     */
    bool Started();

    /**
     * @brief Set the callback function that is called to notify a transport about
     *     found and lost well-known names.
     *
     * @param transportMask A bitmask containing the transport handling the specified
     *     endpoints.  This allows the found advertisements to be demultiplexed into
     *     the interested transports.
     * @param cb The callback method on the transport that will be called to notify
     *     a transport about found and lost well-known names.
     */
    void SetCallback(TransportMask transportMask,
                     Callback<void, const qcc::String&, const qcc::String&, std::vector<qcc::String>&, uint8_t>* cb);

    /**
     * @brief Creat a virtual network interface. In normal cases WiFi-Direct
     * creates a soft-AP for a temporary network. In some OSs like WinRT, there is
     * no API to detect the presence of the soft-AP. Thus we need to manually create
     * a virtual network interface for it.
     *
     * @param entry Contains the network interface parameters
     *
     * @return Status of the operation.  Returns ER_OK on success.
     *
     * @see qcc::IfConfig()
     * @see qcc::IfConfigEntry
     */
    QStatus CreateVirtualInterface(const qcc::IfConfigEntry& entry);

    /**
     * @brief Delete a virtual network interface. In normal cases WiFi-Direct
     * creates a soft-AP for a temporary network. Once the P2P keep-alive connection is
     * terminated, we delete the virual network interface that represents the
     * soft-AP.
     *
     * @param ifceName The virtual network interface name
     *
     * @return Status of the operation.  Returns ER_OK on success.
     *
     */
    QStatus DeleteVirtualInterface(const qcc::String& ifceName);

    /**
     * @brief Enable the name service to advertise over the provided network interface
     *     on behalf of the specified transport.
     *
     * @param transportMask A bitmask containing the transport requesting the
     *     advertisements.
     * @param name The name of the network interface (cf. eth0) over which
     *     advertisements will be sent.
     */
    QStatus OpenInterface(TransportMask transportMask, const qcc::String& name);

    /**
     * @brief Enable the name service to advertise over the network interface
     *     having the specified IP address on behalf of the specified transport.
     *
     * @param transportMask A bitmask containing the transport requesting the
     *     advertisements.
     * @param address The IP address of the network interface (cf. 192.168.1.101)
     *     over which advertisements will be sent.
     */
    QStatus OpenInterface(TransportMask transportMask, const qcc::IPAddress& address);

    /**
     * @brief Disable the name service from advertising over the provided network
     *     interface on behalf of the specified transport.
     *
     * @param transportMask A bitmask containing the transport requesting the
     *     advertisements.
     * @param name The name of the network interface (cf. eth0) over which
     *     advertisements will no longer be sent.
     */
    QStatus CloseInterface(TransportMask transportMask, const qcc::String& name);

    /**
     * @brief Disable the name service from advertising over the network interface
     *     having the specified IP address on behalf of the specified transport.
     *
     * @param transportMask A bitmask containing the transport requesting the
     *     advertisements.
     * @param address The IP address of the network interface (cf. 192.168.1.101)
     *     over which advertisements will no longer be sent.
     */
    QStatus CloseInterface(TransportMask transportMask, const qcc::IPAddress& address);

    /**
     * @brief Notify the name service that that there is or is not a listener on
     *     the specified endpoints.
     *
     * The IpNameService is shared among several transports.  In order to
     * advertise the presence of a network endpoint managed by a transport, the
     * transports need to advise us of the IP addresses and ports on which it
     * can be contacted.  Each transport may use a different set of addresses
     * and ports, and so each transport must identify itself to the name
     * service.  This is done using the TransportMask.  There is a bit in the
     * TransportMask for each transport in the system.
     *
     * A transport is defined as a module that provides reliable and/or
     * unreliable data transfer over IPv4 and/or IPv6.  Support for reliable and
     * unreliable modes is optional, as is support for IPv4 and IPv6.  Port
     * numbers for reliable, unreliable, IPv4 and IPv6 may also differ.  An
     * enable bit is conceptually required since support for each endpoint type
     * is optional.  This makes for thirteen parameters.
     *
     *     transportMask,
     *     enableReliableIPv4, reliableIPv4Addr, reliableIPv4Port
     *     enableReliableIPv6, reliableIPv6Addr, reliableIPv6Port
     *     enableUnreliableIPv4, unreliableIPv4Addr, unreliableIPv4Port
     *     enableUnReliableIPv6, unreliableIPv6Addr, unreliableIPv6Port
     *
     * It turns out that since AllJoyn lives in a mobile environment, the
     * transport must listen on the "any" address.  This is because an IP
     * address assigned to a given network interface cannot generally be
     * predicted in advance.
     *
     * The mechanism used to "control" which network interfaces can accept
     * incoming connections, is the presence of outgoing advertisements on those
     * interfaces.  If a transport desires to accept connections over an
     * interface, it performs an OpenInterface() on that interface which begins
     * the process of sending advertisements.  The IP (V4 and or V6) address of
     * the interface is added to advertisements and sent out over the network.
     * This which provides the receiver of the advertisements with an IP address
     * and port to connect to.  The advertising transport, since it is listening
     * on the "any" address, will respond to all connect requests coming from
     * clients on the associated network.  If the transport wants to filter
     * further on source IP addresses, it certainly can; but this is not a
     * concern of the name service.
     *
     * This all means that we do not need to specify the IP addresses on which
     * the reliable and unreliable protocols are listening.  Additionally, it
     * turns out that TCP and UDP port numbers zero are reserved by IANA and so
     * we can use a port number of zero to indicate a non-enabled condition.
     * This means that the enabled parameters are not required.  This means that
     * there are five required parameters to this call instead of thirteen:
     *
     *     transportMask,
     *     reliableIPv4Port, reliableIPv6Port,
     *     unreliableIPv4Port, unreliableIPv6Port,
     *     enableReliableIPv4, enableReliableIPv6,
     *     enableUnreliableIPv4, enableUnreliableIPv6
     *
     * In many cases, the transports will not support all combinations.  For
     * example, the tcp transport currently only supports reliable IPv4
     * connections, and so the call for this transport might be:
     *
     *     SetEndpointsForTransport(TRANSPORT_WLAN, 9955, 0, 0, 0, true, false, false, false);
     *
     * The Android Compatibility Test Suite demands that an Android phone may
     * not hold an open socket in the quiescent state.  Since we provide a
     * native daemon that always runs on the phone, and the daemon has
     * transports that want to listen on sockets, there must be a way to close
     * those sockets (and the multicast sockets in the name service) and not
     * use them at all when there are no advertisements or discovery operations
     * in progress.  This also helps with power consumption.
     *
     * Enable() communicates the fact that there is or is not a listener for the
     * specified transport port and this, in turn, implies whether or not
     * advertisements should be sent and received over the interfaces opened by
     * the given transport.
     *
     * @param transportMask A bitmask containing the transport handling the specified
     *     endpoints.
     * @param reliableIPv4Port Indicates the port number of a server listening for
     *     connections if enableReliableIPv4 is true
     * @param reliableIPv6Port Indicates the port number of a server listening for
     *     connections if enableUnreliableIPv4 is true
     * @param unreliableIPv4Port Indicates the port number of a server listening for
     *     connections if enableReliableIPv6 is true
     * @param unreliableIPv6Port Indicates the port number of a server listening for
     *     connections if enableUnreliableIPv6 is true.
     * @param enableReliableIPv4
     *     - true indicates this protocol is enabled.
     *     - false indicates this protocol is not enabled.
     * @param enableReliableIPv6
     *     - true indicates this protocol is enabled.
     *     - false indicates this protocol is not enabled.
     * @param enableUnreliableIPv4
     *     - true indicates this protocol is enabled.
     *     - false indicates this protocol is not enabled.
     * @param enableUnreliableIPv4
     *     - true indicates this protocol is enabled.
     *     - false indicates this protocol is not enabled.
     */
    QStatus Enable(TransportMask transportMask,
                   uint16_t reliableIPv4Port, uint16_t reliableIPv6Port,
                   uint16_t unreliableIPv4Port, uint16_t unreliableIPv6Port,
                   bool enableReliableIPv4, bool enableReliableIPv6,
                   bool enableUnreliableIPv4, bool enableUnreliableIPv6);

    /**
     * @brief Ask the name service whether or not it thinks there is or is not a
     *     listener on the specified ports for the given transport.
     *
     * @param transportMask A bitmask containing the transport handling the specified
     *     endpoints.
     * @param reliableIPv4Port If zero, indicates this protocol is not enabled.  If
     *     non-zero, indicates the port number of a server listening for connections.
     * @param reliableIPv6Port If zero, indicates this protocol is not enabled.  If
     *     non-zero, indicates the port number of a server listening for connections.
     * @param unreliableIPv4Port If zero, indicates this protocol is not enabled.  If
     *     non-zero, indicates the port number of a server listening for connections.
     * @param unreliableIPv6Port If zero, indicates this protocol is not enabled.  If
     *     non-zero, indicates the port number of a server listening for connections.
     */
    QStatus Enabled(TransportMask transportMask,
                    uint16_t& reliableIPv4Port, uint16_t& reliableIPv6Port,
                    uint16_t& unreliableIPv4Port, uint16_t& unreliableIPv6Port);

    /**
     * @brief Discover well-known names starting with the specified prefix over
     * the network interfaces opened by the specified transport.
     *
     * @param transportMask A bitmask containing the transport requesting the
     *     discovery operation.
     * @param prefix The well-known name prefix to find.
     */
    QStatus FindAdvertisedName(TransportMask transportMask, const qcc::String& prefix);

    /**
     * @brief Stop discovering well-known names starting with the specified
     * prefix over the network interfaces opened by the specified transport.
     *
     * @param transportMask A bitmask containing the transport requesting the
     *     discovery operation.
     * @param prefix The well-known name prefix to stop finding.
     */
    QStatus CancelFindAdvertisedName(TransportMask transportMask, const qcc::String& prefix);

    /**
     * @brief Advertise a well-known name over the network interfaces opened by the
     * specified transport.
     *
     * @param transportMask A bitmask containing the transport requesting the
     *     advertisement.
     * @param wkn The well-known name to advertise.
     * @param quietly The quietly parameter, if true, specifies to not do
     *     gratuitous advertisements (send periodic is-at messages indicating we
     *     have the provided name avialable) but do respond to who-has requests
     *     for the name.
     */
    QStatus AdvertiseName(TransportMask transportMask, const qcc::String& wkn, bool quietly = false);

    /**
     * @brief Stop advertising a well-known name over the network interfaces
     * opened by the specified transport.
     *
     * @param transportMask A bitmask containing the transport requesting the
     *     advertisement be canceled.
     * @param wkn The well-known name to stop advertising.
     */
    QStatus CancelAdvertiseName(TransportMask transportMask, const qcc::String& wkn);

    /**
     * @brief Handle the suspending event of the process. Release exclusive socket file descriptor and port.
     */
    QStatus OnProcSuspend();

    /**
     * @brief Handle the resuming event of the process. Re-acquire exclusive socket file descriptor and port.
     */
    QStatus OnProcResume();

  private:
    /**
     * This is a singleton so the constructor is marked private to prevent
     * construction of an IpNameService instance in any other sneaky way than
     * the Meyers singleton mechanism.
     */
    IpNameService();

    /**
     * This is a singleton so the destructor is marked private to prevent
     * destruction of an IpNameService instance in any other sneaky way than the
     * Meyers singleton mechanism.
     */
    virtual ~IpNameService();

    /**
     * This is a singleton so the copy constructor is marked private to prevent
     * destruction of an IpNameService instance in any other sneaky way than the
     * Meyers singleton mechanism.
     */
    IpNameService(const IpNameService& other);

    /**
     * This is a singleton so the assignment constructor is marked private to
     * prevent destruction of an IpNameService instance in any other sneaky way
     * than the Meyers singleton mechanism.
     */
    IpNameService& operator =(const IpNameService& other);

    /**
     * @brief Start the IpNameService singleton.
     *
     * Since the IpNameService is shared among transports, the responsibility
     * for starting, stopping and joining the name service should not reside
     * with any single transport.  We provide a reference counting mechanism to
     * deal with this and so the actual Start() method is private and called
     * from the public Acquire().
     *
     * @return ER_OK if the start operation completed successfully, or an error code
     *     if not.
     */
    QStatus Start();

    /**
     * @brief Stop the IpNameService singleton.
     *
     * Since the IpNameService is shared among transports, the responsibility
     * for starting, stopping and joining the name service should not reside
     * with any single transport.  We provide a reference counting mechanism to
     * deal with this and so the actual Stop() method is private and called from
     * the public Release().
     *
     * @return ER_OK if the stop operation completed successfully, or an error code
     *     if not.
     */
    QStatus Stop();

    /**
     * @brief Join the IpNameService singleton.
     *
     * Since the IpNameService is shared among transports, the responsibility
     * for starting, stopping and joining the name service should not reside
     * with any single transport.  We provide a reference counting mechanism to
     * deal with this and so the actual Join() method is private and called from
     * the public Release().
     *
     * @return ER_OK if the join operation completed successfully, or an error code
     *     if not.
     */
    QStatus Join();

    /**
     * @brief Initialize the IpNameService singleton.
     *
     * Since the IpNameService is shared among transports, the responsibility for
     * initializing the shared name service should not reside with any single
     * transport.  We provide a reference counting mechanism to deal with this and
     * so the actual Init() method is private and called from the public Acquire().
     * The first transport to Acquire() provides the GUID, which must be unchanging
     * across transports since they are all managed by a single daemon.
     *
     * @param guid A string containing the GUID assigned to the daemon which is
     *     hosting the name service.
     * @param loopback If true, receive our own advertisements.
     *     Typically used for test programs to listen to themselves talk.
     */
    QStatus Init(const qcc::String& guid, bool loopback = false);

    bool m_constructed;          /**< State variable indicating the singleton has been constructed */
    bool m_destroyed;            /**< State variable indicating the singleton has been destroyed */
    int32_t m_refCount;          /**< The number of transports that have registered as users of the singleton */
    IpNameServiceImpl* m_pimpl;  /**< A pointer to the private implementation of the name service */
};

} // namespace ajn

#endif // _IP_NAME_SERVICE_H

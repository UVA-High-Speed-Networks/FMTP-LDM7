/**
 * This file declares the remote-procedure-call API for the multicast LDM.
 *
 *        File: MldmRpc.h
 *  Created on: Feb 7, 2018
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_C___MLDMRPC_H_
#define MCAST_LIB_C___MLDMRPC_H_

#include "ldm.h"

#include <netinet/in.h>

/******************************************************************************
 * C API:
 ******************************************************************************/

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Returns a new multicast LDM RPC client.
 * @param[in] port  Port number of the multicast LDM RPC server in host
 *                  byte-order
 * @retval NULL     Failure
 * @return          Pointer to multicast LDM RPC client. Caller should call
 *                  `mldmClnt_delete()` when it's no longer needed.
 * @see mldmClnt_delete()
 */
void* mldmClnt_new(const in_port_t port);

/**
 * Reserves an IP address for a remote FMTP layer to use for its TCP endpoint
 * for recovering missed data-blocks.
 * @param[in]  mldmClnt  Multicast LDM RPC client
 * @param[out] fmtpAddr  Reserved IP address
 * @retval LDM7_OK       Success. `*fmtpAddr` is set.
 * @retval LDM7_SYSTEM   System failure. `log_add()` called.
 */
Ldm7Status mldmClnt_reserve(
        void*      mldmClnt,
        in_addr_t* fmtpAddr);

/**
 * Releases a resered IP address for subsequent reuse.
 * @param[in] mldmClnt  Multicast LDM RPC client
 * @param[in] fmtpAddr  IP address to release
 * @retval LDM7_OK      Success
 * @retval LDM7_NOENT   `fmtpAddr` wasn't previously reserved. `log_add()`
 *                      called.
 * @retval LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status mldmClnt_release(
        void*           mldmClnt,
        const in_addr_t fmtpAddr);

/**
 * Destroys an allocated multicast LDM RPC client and deallocates it.
 * @param[in] mldmClnt  Multicast LDM RPC client
 * @see mldmClnt_new()
 */
void mldmClnt_delete(void* mldmClnt);

/**
 * Creates.
 * @param[in] networkPrefix      Network prefix in network byte-order
 * @param[in] prefixLen          Number of bits in network prefix
 * @retval NULL                  Failure. `log_add()` called.
 */
void* inAddrPool_new(
        const in_addr_t networkPrefix,
        const unsigned  prefixLen);

/**
 * Indicates if an IP address has been previously reserved.
 * @param[in]        IP address pool
 * @param[in] addr   IP address to check
 * @retval `true`    IP address has been previously reserved
 * @retval `false`   IP address has not been previously reserved
 */
bool inAddrPool_isReserved(
        void*           inAddrPool,
        const in_addr_t addr);

void inAddrPool_delete(void* inAddrPool);

/**
 * Constructs. Creates a listening server-socket and a file that contains a
 * secret that can be shared by other processes belonging to the same user.
 * @param[in] inAddrPool     Pool of available IP addresses
 */
void* mldmSrvr_new(void* inAddrPool);

/**
 * Returns the port number of the multicast LDM RPC server.
 * @param[in] mldmSrvr  Multicast LDM RPC server
 * @return              Port number on which the server is listening in host
 *                      byte-order
 */
in_port_t mldmSrvr_getPort(void* mldmSrvr);

/**
 * Starts the multicast LDM RPC server. Doesn't return unless a fatal error
 * occurs.
 * @param[in] mldmSrvr     Multicast LDM RPC server
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @threadsafety           Unsafe
 */
Ldm7Status mldmSrvr_run(void* mldmSrvr);

/**
 * Destroys an allocated multicast LDM RPC server and deallocates it.
 * @param[in] mldmClnt  Multicast LDM RPC server
 * @see mldmSrvr_new()
 */
void mldmSrvr_delete(void* mldmSrvr);

#ifdef __cplusplus
} // `extern "C"`

/******************************************************************************
 * C++ API:
 ******************************************************************************/

#include <memory>

/// Multicast LDM RPC actions
typedef enum MldmRpcAct
{
    /// Reserve an IP address
    RESERVE_ADDR,
    /// Release a previously-reserved IP address
    RELEASE_ADDR,
    /// Close the connection
    CLOSE_CONNECTION
} MldmRpcAct;

/**
 * Multicast LDM RPC client.
 */
class MldmClnt final
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    /**
     * Constructs.
     * @param[in] port  Port number of multicast LDM RPC server in host
     *                  byte-order
     */
    MldmClnt(const in_port_t port);

    /**
     * Reserves an IP address for a remote FMTP layer to use as its TCP endpoint
     * for recovering missed data-blocks.
     * @return  Reserved IP address
     */
    in_addr_t reserve() const;

    /**
     * Releases a reserved IP address for subsequent reuse.
     * @param[in] fmtpAddr  IP address to be released
     */
    void release(const in_addr_t fmtpAddr) const;
};

/**
 * Thread-safe pool of available IP addresses.
 */
class InAddrPool final
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    /**
     * Constructs.
     * @param[in] networkPrefix      Network prefix in network byte-order
     * @param[in] prefixLen          Number of bits in network prefix
     * @throw std::invalid_argument  `prefixLen >= 31`
     * @throw std::invalid_argument  `networkPrefix` and `prefixLen` are
     *                               incompatible
     */
    InAddrPool(
            const in_addr_t networkPrefix,
            const unsigned  prefixLen);

    /**
     * Reserves an address.
     * @return                    Reserved address in network byte-order
     * @throw std::out_of_range   No address is available
     * @threadsafety              Safe
     * @exceptionsafety           Strong guarantee
     */
    in_addr_t reserve() const;

    /**
     * Indicates if an IP address has been previously reserved.
     * @param[in] addr   IP address to check
     * @retval `true`    IP address has been previously reserved
     * @retval `false`   IP address has not been previously reserved
     * @threadsafety     Safe
     * @exceptionsafety  Nothrow
     */
    bool isReserved(const in_addr_t addr) const noexcept;

    /**
     * Releases an address so that it can be subsequently reserved.
     * @param[in] addr          Reserved address to be released in network
     *                          byte-order
     * @throw std::logic_error  `addr` wasn't previously reserved
     * @threadsafety            Safe
     * @exceptionsafety         Strong guarantee
     */
    void release(const in_addr_t addr) const;
}; // class InAddrPool

/**
 * Multicast LDM RPC server.
 */
class MldmSrvr final
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    /**
     * Constructs. Creates a listening server-socket and a file that contains a
     * secret.
     * @param[in] inAddrPool     Pool of available IP addresses
     */
    MldmSrvr(InAddrPool& inAddrPool);

    /**
     * Returns the port number of the multicast LDM RPC server.
     * @return Port number of multicast LDM RPC server in host byte-order
     */
    in_port_t getPort() const noexcept;

    /**
     * Runs the server. Doesn't return unless a fatal exception is thrown.
     */
    void operator()() const;
};

#endif // `#ifdef __cplusplus`

#endif /* MCAST_LIB_C___MLDMRPC_H_ */

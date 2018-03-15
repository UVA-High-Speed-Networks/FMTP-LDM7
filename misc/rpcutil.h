#ifndef RPCUTIL_H
#define RPCUTIL_H

#include <rpc/rpc.h>
#include <stddef.h>

#ifdef __cplusplus
    extern "C" {
#endif

char *clnt_errmsg(CLIENT* clnt);
int local_portmapper_running();

/**
 * Returns an identifier of the remote client.
 *
 * @param[in] rqstp  Client-request object.
 */
const char*
rpc_getClientId(
    struct svc_req* const rqstp);

#ifdef __cplusplus
    }
#endif

#endif

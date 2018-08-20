/*
 * Proxy for an LDM.
 *
 * See file ../COPYRIGHT for copying and redistribution conditions.
 */

#include "config.h"
#include "error.h"
#include "ldm.h"
#include "ldm_clnt_misc.h"
#include "ldmprint.h"
#include "log.h"
#include "prod_class.h"
#include "rpc/rpc.h"
#include "rpcutil.h"
#include "LdmProxy.h"

#include <stddef.h>
#include <string.h>

struct ldmProxy {
    LdmProxyStatus      (*hiya)(LdmProxy* proxy, prod_class_t* offer, 
                            prod_class_t** want);
    LdmProxyStatus      (*send)(LdmProxy* proxy, product* product);
    LdmProxyStatus      (*flush)(LdmProxy* proxy);
    char*               host;
    CLIENT*             clnt;
    struct timeval      rpcTimeout;
    size_t              max_hereis;
    int                 version;
};

static struct timeval   rpcTimeout = {25, 0}; /* usual RPC default */
/* Added to prevent SIGSEGV on 64-bit RHEL 6 */
static char             buf[2048];

/*
 * Returns the LDM proxy status for a given client failure. Logs the failure.
 *
 * Arguments:
 *      proxy           The LDM proxy data-structure.
 *      name            The name of the failed message or NULL.
 *      info            Metadata on the data-product that couldn't be sent or
 *                      NULL.
 * Returns:
 *      0               Success.
 *      LP_TIMEDOUT     The failure was due to an RPC timeout. "log_add()"
 *                      called iff "name" isn't NULL.
 *      LP_RPC_ERROR    RPC error. "log_add()" called iff "name" isn't NULL.
 */
static LdmProxyStatus
getStatus(
    LdmProxy* const     proxy,
    const char* const   name,
    prod_info* const    info)
{
    LdmProxyStatus      status;
    struct rpc_err      rpcErr;

    clnt_geterr(proxy->clnt, &rpcErr);

    if (0 == rpcErr.re_status) {
        status = 0;
    }
    else {
        if (NULL != name) {
            log_add("%s failure to host \"%s\": %s", name, proxy->host,
                    clnt_errmsg(proxy->clnt));

            if (NULL != info) {
                log_add("Couldn't send product: %s", s_prod_info(NULL, 0,
                            info, log_is_enabled_debug));
            }
        }

        status = RPC_TIMEDOUT == rpcErr.re_status
            ? LP_TIMEDOUT
            : LP_RPC_ERROR;
    }

    return status;
}

/*
 * Does nothing because LDM-5 messages are synchronous.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 * Returns:
 *      0
 */
static LdmProxyStatus
my_flush_5(
    LdmProxy* const     proxy)
{
    return 0;
}

/**
 * Notifies the LDM of the class of data-products that will be sent via LDM-5
 * protocols.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      offer           Pointer to the data-product class structure which will
 *                      be offered to the LDM.
 *      want            Pointer to a pointer to a data-product class structure.
 *                      "*want" will be set the data-product class structure
 *                      that the LDM wants. The client should call
 *                      "free_prod_class(*want)" when the product-class is no
 *                      longer needed.
 * Returns:
 *      0               Success. "*clsspp" might be modified.
 *      LP_TIMEDOUT     The RPC call timed-out. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
static LdmProxyStatus
my_hiya_5(
    LdmProxy* const             proxy,
    prod_class_t* const         offer,
    prod_class_t** const        want)
{
    static ldm_replyt   reply;
    enum clnt_stat      rpc_stat;
    CLIENT* const       clnt = proxy->clnt;

    memset(&reply, 0, sizeof(ldm_replyt));

    rpc_stat = clnt_call(clnt, HIYA, xdr_prod_class, (caddr_t)offer,
            xdr_ldm_replyt, (caddr_t)&reply, proxy->rpcTimeout);

    if (rpc_stat != RPC_SUCCESS) {
        return getStatus(proxy, "HIYA_5", NULL);
    }

    switch (reply.code) {
        case OK:
            *want = (prod_class_t*)PQ_CLASS_ALL;
            break;
        case SHUTTING_DOWN:
            log_add("%s is shutting down", proxy->host);
            return LP_LDM_ERROR;
        case DONT_SEND:
        case RESTART:
        case REDIRECT: /* TODO */
        default:
            log_add("%s: Unexpected reply from LDM: %s", proxy->host,
                    s_ldm_errt(reply.code));
            return LP_LDM_ERROR;
        case RECLASS:
            *want = reply.ldm_replyt_u.newclssp;
            clss_regcomp(*want);
            /* N.B. we use the downstream patterns */
            /* Use of "buf" added to prevent SIGSEGV on 64-bit RHEL 6 */
            (void)s_prod_class(buf, sizeof(buf)-1, *want);
            log_notice_q("%s: reclass: %s", proxy->host, buf);
            break;
    }

    return 0;
}

/**
 * Notifies the LDM of the class of data-products that will be sent via LDM-6
 * protocols.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      offer           Pointer to the data-product class structure which will
 *                      be offered to the LDM.
 *      want            Pointer to a pointer to a data-product class structure.
 *                      "*want" will be set the data-product class structure
 *                      that the LDM wants. The client should call
 *                      "free_prod_class(*want)" when the product-class is no
 *                      longer needed.
 * Returns:
 *      0               Success. "*clsspp" might be modified.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
static LdmProxyStatus
my_hiya_6(
    LdmProxy* const             proxy,
    prod_class_t* const         offer,
    prod_class_t** const        want)
{
    CLIENT* const       clnt = proxy->clnt;
    hiya_reply_t*       reply;
    LdmProxyStatus      status;
    
    reply = hiya_6(offer, clnt);

    if (NULL == reply) {
        status = getStatus(proxy, "HIYA_6", NULL);
    }
    else {
        switch (reply->code) {
            case OK:
                *want = (prod_class_t*)PQ_CLASS_ALL;
                proxy->max_hereis = reply->hiya_reply_t_u.max_hereis;
                status = 0;
                break;

            case SHUTTING_DOWN:
                log_add("%s: LDM shutting down", proxy->host);
                status = LP_LDM_ERROR;
                break;

            case BADPATTERN:
                log_add("%s: Bad product-class pattern", proxy->host);
                status = LP_LDM_ERROR;
                break;

            case DONT_SEND:
                log_add("%s: LDM says don't send", proxy->host);
                status = LP_LDM_ERROR;
                break;

            case RESEND:
                log_add("%s: LDM says resend (ain't gonna happen)",
                        proxy->host);
                status = LP_LDM_ERROR;
                break;

            case RESTART:
                log_add("%s: LDM says restart (ain't gonna happen)",
                        proxy->host);
                status = LP_LDM_ERROR;
                break;

            case REDIRECT:
                log_add("%s: LDM says redirect (ain't gonna happen)",
                        proxy->host);
                status = LP_LDM_ERROR;
                break;

            case RECLASS:
                *want = reply->hiya_reply_t_u.feedPar.prod_class;
                proxy->max_hereis = reply->hiya_reply_t_u.feedPar.max_hereis;
                clss_regcomp(*want);
                /* N.B. we use the downstream patterns */
                /* Use of "buf" added to prevent SIGSEGV on 64-bit RHEL 6 */
                (void)s_prod_class(buf, sizeof(buf)-1, *want);
                log_notice_q("%s: reclass: %s", proxy->host, buf);
                status = 0;
                break;
        }

        if (LP_OK != status)
            log_debug("max_hereis = %u", proxy->max_hereis);
    }

    return status;
}

/*
 * Sends an LDM-5 COMINGSOON message.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      infop           The data-product's metadata.
 *      pktsz           The canonical size of each BLOCKDATA message.
 *      replyp          The response from the LDM.
 * Returns:
 *      0               OK
 *      LP_TIMEDOUT     The RPC call timed-out. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
static LdmProxyStatus
my_comingsoon_5(
    LdmProxy* const     proxy,
    prod_info* const    infop,
    const u_int         pktsz,
    ldm_replyt* const   replyp)
{
    CLIENT* const       clnt = proxy->clnt;
    comingsoon_args     arg;
    
    arg.infop = infop;
    arg.pktsz = pktsz;

    memset(replyp, 0, sizeof(ldm_replyt));

    clnt_call(clnt, COMINGSOON, xdr_comingsoon_args, (caddr_t)&arg,
                xdr_ldm_replyt, (caddr_t)replyp, proxy->rpcTimeout);

    return getStatus(proxy, "COMINGSOON_5", infop);
}


/*
 * Sends an LDM-5 BLOCKDATA message.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      dpkp            The data packet.
 *      replyp          The response from the LDM.
 * Returns:
 *      0               OK
 *      LP_TIMEDOUT     The RPC call timed-out.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
static LdmProxyStatus
my_blkdata_5(
    LdmProxy* const     proxy,
    datapkt*            dpkp,
    ldm_replyt*         replyp)
{
    CLIENT* const       clnt = proxy->clnt;

    memset(replyp, 0, sizeof(ldm_replyt));

    clnt_call(clnt, BLKDATA, xdr_datapkt, (caddr_t)dpkp, xdr_ldm_replyt,
            (caddr_t)replyp, rpcTimeout);

    return getStatus(proxy, NULL, NULL);
}

/*
 * Sends a data-product to an LDM using the LDM-5 messages COMINGSOON and
 * BLOCKDATA.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      info            Pointer to the data-product's metadata.
 *      datap           Pointer to the data-product's data.
 * Returns:
 *      0               Success.
 *      LP_UNWANTED     Data-product was unwanted.
 *      LP_TIMEDOUT     The RPC call timed-out. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
static LdmProxyStatus
my_csbd_5(
    LdmProxy* const     proxy,
    product* const      product)
{
    CLIENT* const       clnt = proxy->clnt;
    LdmProxyStatus      status = 0;     /* success */
    ldm_replyt          reply;
    prod_info* const    info = &product->info;

    memset(&reply, 0, sizeof(ldm_replyt));

    status = my_comingsoon_5(proxy, info, DBUFMAX, &reply);

    if (0 == status) {
        if (reply.code != OK) {
            if (reply.code == DONT_SEND) {
               status = LP_UNWANTED;
            }
            else {
               log_add("send_5: %s: %s", info->ident,
                       s_ldm_errt(reply.code));
               status = LP_LDM_ERROR;
            }
        }
        else {
            size_t      unsent = info->sz;
            char*       data = product->data;
            datapkt     pkt;

            pkt.signaturep = &info->signature;
            pkt.pktnum = 0;

            while (unsent > 0) {
                size_t  nsend = DBUFMAX < unsent
                    ? DBUFMAX
                    : unsent;
                pkt.data.dbuf_len = (u_int)nsend;
                pkt.data.dbuf_val = data;
                status = my_blkdata_5(proxy, &pkt, &reply);
                if (0 != status) {
                    getStatus(proxy, "BLOCKDATA_5", info);
                    break;
                }
                else if (reply.code != OK) {
                    log_add("Unexpected reply from LDM: %s",
                            s_ldm_errt(reply.code));
                    status = LP_LDM_ERROR;
                    break;
                }
                pkt.pktnum++;
                data += nsend;
                unsent -= nsend;
            }
        }                                       /* reply.code == OK */
    }                                           /* OK COMINGSOON */

    return status;
}

/*
 * Send a product to an LDM using the LDM-5 message HEREIS.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      info            Pointer to the data-product's metadata.
 *      datap           Pointer to the data-product's data.
 * Returns:
 *      0               Success.
 *      LP_UNWANTED     Data-product was unwanted.
 *      LP_TIMEDOUT     The RPC call timed-out. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
static LdmProxyStatus
my_hereis_5(
    LdmProxy* const     proxy,
    product* const      product)
{
    CLIENT* const       clnt = proxy->clnt;
    LdmProxyStatus      status = 0;     /* success */
    ldm_replyt          reply;
    enum clnt_stat      rpc_stat;

    memset(&reply, 0, sizeof(ldm_replyt));

    rpc_stat = clnt_call(clnt, HEREIS, xdr_product, (caddr_t)product,
            xdr_ldm_replyt, (caddr_t)&reply, proxy->rpcTimeout);

    if (rpc_stat != RPC_SUCCESS) {
        status = getStatus(proxy, "HEREIS_5", &product->info);
    }
    else if (reply.code != OK) {
        if (reply.code == DONT_SEND) {
            status = LP_UNWANTED;
        }
        else {
            log_add("Unexpected reply from LDM: %s", s_ldm_errt(reply.code));
            status = LP_LDM_ERROR;
        }
    }

    return status;
}

/*
 * Sends a product to an LDM using LDM-5 protocol.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      product         Pointer to the data-product.
 * Returns:
 *      0               Success.
 *      LP_UNWANTED     Data-product was unwanted.
 *      LP_TIMEDOUT     The RPC call timed-out. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
static LdmProxyStatus
my_send_5(
    LdmProxy* const     proxy,
    product* const      product)
{
    return DBUFMAX < product->info.sz
        ? my_csbd_5(proxy, product)
        : my_hereis_5(proxy, product);
}

/*
 * Sends a data-product to an LDM using the LDM-6 COMINGSOON and BLOCKDATA
 * messages.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      product         Pointer to the data-product to be sent.
 * Return:
 *      0               Success.
 *      LP_UNWANTED     Data-product was unwanted.
 *      LP_TIMEDOUT     The RPC call timed-out. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
static LdmProxyStatus
my_csbd_6(
    LdmProxy* const     proxy,
    product* const      product)
{
    LdmProxyStatus      status = 0;     /* success */
    CLIENT* const       clnt = proxy->clnt;
    prod_info* const    info = &product->info;
    const unsigned      size = info->sz;
    comingsoon_reply_t* reply;
    comingsoon_args     soonArg;

    log_debug("Sending file via COMINGSOON_6/BLKDATA_6");

    soonArg.infop = info;
    soonArg.pktsz = size;
    
    reply = comingsoon_6(&soonArg, clnt);

    if (NULL == reply) {
        status = getStatus(proxy, "COMINGSOON_6", &product->info);
    }
    else {
        if (DONT_SEND == *reply) {
            status = LP_UNWANTED;
        }
        else if (0 != *reply) {
            log_add("Unexpected reply from LDM: %s", s_ldm_errt(*reply));
            status = LP_LDM_ERROR;
        }
        else {
            datapkt packet;

            packet.signaturep = (signaturet*)&info->signature;
            packet.pktnum = 0;
            packet.data.dbuf_len = size;
            packet.data.dbuf_val = product->data;

            (void)blkdata_6(&packet, clnt);
            /*
             * The status will be RPC_TIMEDOUT unless an error occurs because
             * the RPC call uses asynchronous message-passing.
             */
            status = (clnt_stat(clnt) == RPC_TIMEDOUT)
                ? 0
                : getStatus(proxy, "BLKDATA_6", &product->info);
        }
    }

    return status;
}

/*
 * Send a data-product to an LDM using the LDM-6 HEREIS message.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      product         Pointer to the data-product to be sent.
 * Return:
 *      0               Success.
 *      LP_TIMEDOUT     RPC timeout. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
static LdmProxyStatus
my_hereis_6(
    LdmProxy* const     proxy,
    product* const      product)
{
    LdmProxyStatus      status = 0;     /* success */

    log_debug("Sending file via HEREIS_6");

    (void)hereis_6(product, proxy->clnt);
    /*
     * The status will be RPC_TIMEDOUT unless an error occurs because the RPC
     * call uses asynchronous message-passing.
     */
    status = (clnt_stat(proxy->clnt) == RPC_TIMEDOUT)
        ? 0
        : getStatus(proxy, "HEREIS_6", &product->info);

    return status;
}

/*
 * Send a data-product to an LDM using LDM-6 protocol.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      product         Pointer to the data-product to be sent.
 * Return:
 *      0               Success.
 *      LP_UNWANTED     Data-product was unwanted.
 *      LP_TIMEDOUT     The RPC call timed-out. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
static LdmProxyStatus
my_send_6(
    LdmProxy* const     proxy,
    product* const      product)
{
    return (product->info.sz <= proxy->max_hereis)
        ? my_hereis_6(proxy, product)
        : my_csbd_6(proxy, product);
}

/*
 * Flushes the connection to an LDM-6.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 * Returns:
 *      0               Success.
 *      LP_TIMEDOUT     An RPC call timed-out.
 *      LP_RPC_ERROR    Other RPC error.
 */
static LdmProxyStatus
my_flush_6(
    LdmProxy* const     proxy)
{
    nullproc_6(NULL, proxy->clnt);

    return getStatus(proxy, "NULLPROC_6", NULL);
}

/*
 * Returns the LdmProxy status corresponding to an ldm_clnttcp_create_vers()
 * status.
 *
 * Arguments:
 *      error   Error object returned by ldm_clnttcp_create_vers(). May be NULL.
 * Returns:
 *      0               Success. "error" is NULL.
 *      LP_SYSTEM       System error.
 *      LP_TIMEDOUT     Connection attempt timed-out.
 *      LP_HOSTUNREACH  Host is unreachable.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */      
static LdmProxyStatus
convertStatus(
    ErrorObj*   error)
{
    if (NULL == error) {
        return 0;
    }
    else {
        int     errCode = err_code(error);

        switch (errCode) {
            case LDM_CLNT_UNKNOWN_HOST: return LP_HOSTUNREACH;
            case LDM_CLNT_TIMED_OUT:    return LP_TIMEDOUT;
            case LDM_CLNT_NO_CONNECT:   return LP_RPC_ERROR;
            case LDM_CLNT_BAD_VERSION:  return LP_LDM_ERROR;
            default:                    return LP_SYSTEM;
        }
    }
}

/*******************************************************************************
 * Public API:
 ******************************************************************************/

/*
 * Sets the RPC timeout used by all subsequently-create LDM proxies.
 *
 * Arguments:
 *      timeout         The RPC timeout in seconds.
 */
void
lp_setRpcTimeout(
    const unsigned      timeout)
{
    rpcTimeout.tv_sec = timeout;
}

/*
 * Returns a new instance of an LDM proxy. Can take a while because it
 * establishes a connection to the LDM.
 *
 * Arguments:
 *      host            Identifier of the host on which an LDM server is
 *                      running.
 *      instance        Pointer to a pointer to the new instance. "*instance"
 *                      is set upon successful return.
 * Returns:
 *      0               Success. "*instance" is set.
 *      LP_SYSTEM       System error. "log_add()" called.
 *      LP_TIMEDOUT     Connection attempt timed-out. "log_add()" called.
 *      LP_HOSTUNREACH  Host is unreachable. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
LdmProxyStatus
lp_new(
    const char* const   host,
    LdmProxy** const    instance)
{
    LdmProxyStatus      status = 0;     /* success */
    size_t              nbytes = sizeof(LdmProxy);
    LdmProxy*           proxy = (LdmProxy*)malloc(nbytes);

    if (NULL == proxy) {
        log_syserr_q("Couldn't allocate %lu bytes for new LdmProxy", nbytes);
        status = LP_SYSTEM;
    }
    else {
        proxy->host = strdup(host);

        if (NULL == proxy->host) {
            log_syserr_q("Couldn't duplicate string \"%s\"", host);
            status = LP_SYSTEM;
        }
        else {
            CLIENT*         clnt = NULL;
            ErrorObj*       error = ldm_clnttcp_create_vers(host, LDM_PORT, 6,
                    &clnt, NULL, NULL);

            if (!error) {
                proxy->version = 6;
                proxy->hiya = my_hiya_6;
                proxy->send = my_send_6;
                proxy->flush = my_flush_6;
            }
            else if (LDM_CLNT_BAD_VERSION == err_code(error)) {
                /* Couldn't connect due to protocol version. */
                err_free(error);

                error = ldm_clnttcp_create_vers(host, LDM_PORT, 5,
                        &clnt, NULL, NULL);

                if (!error) {
                    proxy->version = 5;
                    proxy->hiya = my_hiya_5;
                    proxy->send = my_send_5;
                    proxy->flush = my_flush_5;
                }
            }

            if (error) {
                log_add("%s", err_message(error));
                free(proxy->host);
                status = convertStatus(error);
                err_free(error);
            }
            else {
                proxy->clnt = clnt;
                proxy->rpcTimeout = rpcTimeout;
            }
        }                                       /* "proxy->host" allocated */

        if (LP_OK == status) {
            *instance = proxy;
        }
        else {
            free(proxy);
        }
    }                                           /* "proxy" allocated */

    return status;
}

/*
 * Frees an instance.
 *
 * Arguments:
 *      proxy           Pointer to the instance to be freed or NULL.
 */
void
lp_free(
    LdmProxy* const     proxy)
{
    if (NULL != proxy) {
        clnt_destroy(proxy->clnt);
        free(proxy->host);
        free(proxy);
    }
}

/*
 * Returns the identifier of the host.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 * Returns:
 *      The identifier of the host.
 */
const char*
lp_host(
    const LdmProxy* const       proxy)
{
    return proxy->host;
}

/*
 * Returns the protocol version.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 * Returns:
 *      The protocol version.
 */
unsigned int
lp_version(
    const LdmProxy* const       proxy)
{
    return proxy->version;
}

/*
 * Notifies the LDM of the class of data-products that will be sent.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy structure.
 *      offer           Pointer to the data-product class structure which will
 *                      be offered to the LDM.
 *      want            Pointer to a pointer to a data-product class structure.
 *                      "*want" will be set to the data-product class structure
 *                      that the LDM wants. The client should call
 *                      "free_prod_class(*want)" when the product-class is no
 *                      longer needed.
 * Returns:
 *      0               Success. "*want" is set.
 *      LP_TIMEDOUT     The RPC call timed-out. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
LdmProxyStatus
lp_hiya(
    LdmProxy* const             proxy,
    prod_class_t* const         offer,
    prod_class_t** const        want)
{
    return proxy->hiya(proxy, offer, want);
}

/*
 * Sends a data-product to the LDM.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 *      product         Pointer to the data-product to be sent.
 * Returns:
 *      0               Success.
 *      LP_UNWANTED     The data-product wasn't wanted by the LDM.
 *      LP_TIMEDOUT     The RPC call timed-out. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 *      LP_LDM_ERROR    LDM error. "log_add()" called.
 */
LdmProxyStatus
lp_send(
    LdmProxy*           proxy,
    product* const      product)
{
    return proxy->send(proxy, product);
}

/*
 * Flushes the connection to the LDM.
 *
 * Arguments:
 *      proxy           Pointer to the LDM proxy data-structure.
 * Returns:
 *      0               Success.
 *      LP_TIMEDOUT     The RPC call timed-out. "log_add()" called.
 *      LP_RPC_ERROR    RPC error. "log_add()" called.
 */
LdmProxyStatus
lp_flush(
    LdmProxy*           proxy)
{
    return proxy->flush(proxy);
}

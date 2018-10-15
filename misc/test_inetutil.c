/*
 * Copyright 2011 University Corporation for Atmospheric Research.
 *
 * See file COPYRIGHT in the top-level source-directory for copying and
 * redistribution conditions.
 */
#include "config.h"

#include "inetutil.h"
#include "ldmprint.h"
#include "log.h"

#include <arpa/inet.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <libgen.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>

static int
setup(void)
{
    return 0;
}

static int
teardown(void)
{
    return 0;
}

static void
test_getDottedDecimal(void)
{
    int status;
    const char* const localDottedDecimal = "127.0.0.1";
    const char* const localName = "localhost";
    const char* const zeroName = "zero.unidata.ucar.edu";
    const char* const zeroDottedDecimal = "128.117.140.56";
    char              buf[INET_ADDRSTRLEN];

    status = getDottedDecimal(localDottedDecimal, buf);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_STRING_EQUAL(buf, localDottedDecimal);

    status = getDottedDecimal(localName, buf);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_STRING_EQUAL(buf, localDottedDecimal);

    status = getDottedDecimal(zeroName, buf);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_STRING_EQUAL(buf, zeroDottedDecimal);

    status = getDottedDecimal(zeroDottedDecimal, buf);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_STRING_EQUAL(buf, zeroDottedDecimal);
}


#if WANT_MULTICAST
static void
test_sa_getInetSockAddr(void)
{
    static const char* const    IP_ADDR = "127.0.0.1";
    static const unsigned short PORT = 1;

    ServiceAddr* serviceAddr;
    int          status = sa_new(&serviceAddr, IP_ADDR, PORT);

    CU_ASSERT_EQUAL_FATAL(status, 0);

    struct sockaddr_in inetSockAddr;
    socklen_t          sockLen;

    status = sa_getInetSockAddr(serviceAddr, AF_INET, true,
            (struct sockaddr*)&inetSockAddr, &sockLen);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL(inetSockAddr.sin_family, AF_INET);
    CU_ASSERT_EQUAL(inetSockAddr.sin_addr.s_addr, inet_addr(IP_ADDR));
    CU_ASSERT_EQUAL(inetSockAddr.sin_port, htons(PORT));
    CU_ASSERT_EQUAL(sockLen, sizeof(struct sockaddr_in));
}

static void
test_sa_getInet6SockAddr(void)
{
    static const char* const    IP_ADDR = "::1";
    static const unsigned short PORT = 1;

    ServiceAddr* serviceAddr;
    int          status = sa_new(&serviceAddr, IP_ADDR, PORT);

    CU_ASSERT_EQUAL_FATAL(status, 0);

    struct sockaddr inetSockAddr;
    socklen_t       sockLen;

    status = sa_getInetSockAddr(serviceAddr, AF_INET6, true, &inetSockAddr,
            &sockLen);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL(((struct sockaddr_in6*)&inetSockAddr)->sin6_family,
            AF_INET6);
    unsigned char buf[16];
    CU_ASSERT_EQUAL(inet_pton(AF_INET6, IP_ADDR, buf), 1);
    CU_ASSERT_TRUE(memcmp(&((struct sockaddr_in6*)&inetSockAddr)->sin6_addr,
            buf, sizeof(buf)) == 0);
    CU_ASSERT_EQUAL(((struct sockaddr_in6*)&inetSockAddr)->sin6_port,
            htons(PORT));
    CU_ASSERT_EQUAL(sockLen, sizeof(struct sockaddr_in6));
}

static void
sa_parse_test(
    const char* const inetId,
    const unsigned short port)
{
    ServiceAddr* sa;
    char*        buf = ldm_format(80, strchr(inetId, ':') ? "[%s]:%d" : "%s:%d",
            inetId, port);
    int          status;

    CU_ASSERT_PTR_NOT_NULL_FATAL(buf);
    status = sa_parse(&sa, buf);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_STRING_EQUAL(sa_getInetId(sa), inetId);
    CU_ASSERT_EQUAL(sa_getPort(sa), port);
    sa_free(sa);
}

static void
test_sa_parse(void)
{
    sa_parse_test("zero.unidata.ucar.edu", 1);
    sa_parse_test("128.117.140.56", 2);
    sa_parse_test("A0:12::F3", 3);
}

static void
test_sa_parseWithDefaults(void)
{
    ServiceAddr* sa;
    int          status;
    const char*  hostId = "uni14.unidata.ucar.edu";
    const short  port = 388;

    status = sa_parseWithDefaults(&sa, hostId, NULL, 388);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_STRING_EQUAL(sa_getInetId(sa), hostId);
    CU_ASSERT_EQUAL(sa_getPort(sa), port);
    sa_free(sa);
}

#endif // WANT_MULTICAST

int
main(
    const int     argc,
    char* const*  argv)
{
    int         exitCode = EXIT_FAILURE;

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite*       testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            CU_ADD_TEST(testSuite, test_getDottedDecimal);
#           if WANT_MULTICAST
                CU_ADD_TEST(testSuite, test_sa_getInetSockAddr);
                CU_ADD_TEST(testSuite, test_sa_getInet6SockAddr);
                CU_ADD_TEST(testSuite, test_sa_parse);
                CU_ADD_TEST(testSuite, test_sa_parseWithDefaults);
#           endif

            if (log_init(argv[0])) {
                (void)fprintf(stderr, "Couldn't open logging system\n");
            }
            else {
                if (CU_basic_run_tests() == CUE_SUCCESS) {
                    if (0 == CU_get_number_of_failures())
                        exitCode = EXIT_SUCCESS;
                }
            }
        }

        CU_cleanup_registry();
    }                           /* CUnit registery allocated */

    return exitCode;
}

/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file fmtp_c_api_test.cpp
 *
 * This file performs a unit-test of the fmtp_c_api module.
 *
 * @author: Steven R. Emmerson
 */


#include "config.h"

#include "fmtp.h"
#include "FMTPReceiver_stub.hpp"
#include "log.h"
#include "mldm_receiver.h"

#include <errno.h>
#include <opmock.h>
#include <stdbool.h>
#include <stdlib.h>

#ifndef __BASE_FILE__
    #define __BASE_FILE__ "BASE_FILE_REPLACEMENT" // needed by OpMock
#endif

int bof_func(void* obj, void* file_entry)
{
    return 0;
}
int eof_func(void* obj, const void* file_entry)
{
    return 0;
}
void missed_file_func(void* obj, const FmtpProdIndex iProd)
{
}

void test_fmtpReceiver_new()
{
    int                         status;
    FmtpReceiver*              receiver;
    const char*                 addr = "224.0.0.1";
    const unsigned short        port = 1;
    const char* const           tcpAddr = "127.0.0.1";
    const unsigned short        tcpPort = 38800;

    status = mcastReceiver_new(NULL, tcpAddr, tcpPort, bof_func, eof_func,
            missed_file_func, addr, port, NULL);
    OP_ASSERT_EQUAL_INT(EINVAL, status);
    log_list_clear();
    status = mcastReceiver_new(&receiver, tcpAddr, tcpPort, NULL, eof_func,
            missed_file_func, addr, port, NULL);
    OP_ASSERT_EQUAL_INT(EINVAL, status);
    log_list_clear();
    status = mcastReceiver_new(&receiver, tcpAddr, tcpPort, bof_func, NULL,
            missed_file_func, addr, port, NULL);
    OP_ASSERT_EQUAL_INT(EINVAL, status);
    log_list_clear();
    status = mcastReceiver_new(&receiver, tcpAddr, tcpPort, bof_func, eof_func,
            NULL, addr, port, NULL);
    OP_ASSERT_EQUAL_INT(EINVAL, status);
    log_list_clear();
    status = mcastReceiver_new(&receiver, tcpAddr, tcpPort, bof_func, eof_func,
            missed_file_func, NULL, port, NULL);
    OP_ASSERT_EQUAL_INT(EINVAL, status);
    log_list_clear();

    status = mcastReceiver_new(&receiver, tcpAddr, tcpPort, bof_func, eof_func,
            missed_file_func, addr, port, NULL);
    OP_ASSERT_EQUAL_INT(0, status);
    log_list_clear();

    OP_VERIFY();
}

int main(
    int		argc,
    char**	argv)
{
    opmock_test_suite_reset();
    opmock_register_test(test_fmtpReceiver_new, "test_fmtpReceiver_new");
    opmock_test_suite_run();
    return opmock_get_number_of_errors() != 0;
}

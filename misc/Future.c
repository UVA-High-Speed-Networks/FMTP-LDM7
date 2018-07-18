/**
 * This file defines the future of an asynchronous task.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Future.c
 *  Created on: May 6, 2018
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "Future.h"
#include "log.h"
#include "Thread.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>

/******************************************************************************
 * Future of asynchronous task:
 ******************************************************************************/

typedef enum {
    STATE_INITIALIZED,///< Future initialized but not running
    STATE_RUNNING,    ///< Future running
    STATE_COMPLETED,  ///< Future completed (might have been canceled)
} JobState;

typedef uint64_t   Magic;
static const Magic MAGIC = 0x2acf8f2d19b89ded; ///< Random number

struct future {
    pthread_mutex_t mutex;       ///< For protecting state changes
    pthread_cond_t  cond;        ///< For signaling change in state
    pthread_t       thread;      ///< Thread on which future is executing
    Job*            job;         ///< Associated job
    void*           result;      ///< Result of run function
    Magic           magic;       ///< For detecting invalid future
    int             runStatus;   ///< Return-value of run function
    JobState        state;       ///< State of job
    bool            haveResults; ///< Have results?
    bool            wasCanceled; ///< Future was canceled?
};

inline static void
future_assertValid(const Future* const future)
{
    log_assert(future && future->magic == MAGIC);
}

inline static void
future_assertLocked(Future* const future)
{
#ifndef NDEBUG
    int status = pthread_mutex_trylock(&future->mutex);
    log_assert(status != 0);
#endif
}

/**
 * Locks a future.
 *
 * @param[in] future  Future
 * @threadsafety      Safe
 */
inline static void
future_lock(Future* const future)
{
    int status = pthread_mutex_lock(&future->mutex);
    log_assert(status == 0);
}

/**
 * Unlocks a future.
 *
 * @param[in] future  Future
 * @threadsafety      Safe
 */
inline static void
future_unlock(Future* const future)
{
    int status = pthread_mutex_unlock(&future->mutex);
    log_assert(status == 0);
}

/**
 * Sets the state of a future and signals its condition-variable.
 *
 * @pre                     future is locked
 * @param[in,out] future    Future
 * @param[in]     newState  New state for future
 * @retval        0         Success
 * @return                  Error code. `log_add()` called.
 * @post                    future is locked
 */
static int
future_setState(
        Future* const future,
        const JobState   newState)
{
    future_assertLocked(future);

    future->state = newState;

    int status = pthread_cond_broadcast(&future->cond);

    if (status)
        log_add_errno(status, "Couldn't signal future's condition-variable");

    return status;
}

/**
 * Compares and sets the state of a future.
 *
 * @pre                     Future is unlocked
 * @param[in,out] future    Future
 * @param[in]     expect    Expected state of future
 * @param[in]     newState  New state for future if current state equals
 *                          expected state
 * @retval        `true`    Future was in expected state
 * @retval        `false`   Future was not in expected state
 * @post                    Future is unlocked
 * @threadsafety            Safe
 */
static bool
future_cas(
        Future* const future,
        const JobState   expect,
        const JobState   newState)
{
    future_lock(future);
        const bool wasExpected = future->state == expect;

        if (wasExpected)
            (void)future_setState(future, newState);
    future_unlock(future);

    return wasExpected;
}

/**
 * Initializes a future object.
 *
 * @param[out] future  Future to be initialized
 * @param[in]  obj     Object to be executed
 * @param[in]  run     Function to execute
 * @param[in]  halt    Function to stop execution
 * @retval     EAGAIN  System lacked necessary resources. `log_add()` called.
 * @retval     ENOMEM  Out of memory. `log_add()` called.
 */
static int
future_init(Future* const future)
{
    int status;

    status = mutex_init(&future->mutex, PTHREAD_MUTEX_ERRORCHECK, true);

    if (status == 0) {
        status = pthread_cond_init(&future->cond, NULL);

        if (status) {
            log_add_errno(status, "Couldn't initialize condition variable");
            (void)pthread_mutex_destroy(&future->mutex);
        }
        else {
            (void)memset(&future->thread, 0, sizeof(future->thread));

            future->job = NULL;
            future->haveResults = false;
            future->wasCanceled = false;
            future->runStatus = 0;
            future->result = NULL;
            future->magic = MAGIC;
        }
    } // `future->mutex` initialized

    return status;
}

/**
 * Destroys a future.
 *
 * @param[in,out] future  Future to destroy
 */
static void
future_destroy(Future* const future)
{
    future->magic = ~MAGIC;
    (void)pthread_cond_destroy(&future->cond);

    int status = pthread_mutex_destroy(&future->mutex);
    log_assert(status == 0);
}

/**
 * Waits until a future's job has completed -- either normally or by being
 * canceled.
 *
 * @pre                        future is locked
 * @param[in] future           Future
 * @retval    0                Success
 * @retval    ENOTRECOVERABLE  State protected by mutex is not recoverable.
 *                             `log_add()` called.
 * @post                       future is locked
 */
static int
future_wait(Future* const future)
{
    future_assertLocked(future);

    int status = 0;

    while (!future->haveResults) {
        status = pthread_cond_wait(&future->cond, &future->mutex);

        if (status) {
            log_add_errno(status, "Couldn't wait on condition-variable");
            break;
        }
    }

    return status;
}

Future*
future_new()
{
    Future* future = log_malloc(sizeof(Future), "future of asynchronous task");

    if (future) {
        int status = future_init(future);

        if (status) {
            log_add("Couldn't initialize future");
            free(future);
            future = NULL;
        }
    }

    return future;
}

int
future_free(Future* future)
{
    int status;

    if (future == NULL) {
        status = 0;
    }
    else {
        future_assertValid(future);

        future_lock(future);
            if (future->haveResults) {
                status = 0;
            }
            else {
                log_add("Future's job is being executed");
                status = EINVAL;
            }
        future_unlock(future);

        if (status == 0) {
            future_destroy(future);
            free(future);
        }
    }

    return status;
}

void
future_setJob(
        Future* const future,
        Job* const    job)
{
    future_lock(future);
        future_assertValid(future);
        log_assert(future->job == NULL);

        future->job = job;
    future_unlock(future);
}

// Forward declaration
int job_cancel(Job* job);

int
future_cancel(Future* const future)
{
    future_assertValid(future);

    return job_cancel(future->job); // Asynchronous
}

void
future_setResult(
        Future* const restrict future,
        int                    runStatus,
        void* const restrict   result,
        const bool             wasCanceled)
{
    future_assertValid(future);

    future_lock(future);
        future->runStatus = runStatus;
        future->result = result;
        future->wasCanceled = wasCanceled;
        future->haveResults = true;

        int status = pthread_cond_broadcast(&future->cond);
        log_assert(status == 0);
    future_unlock(future);
}

int
future_getResult(
        Future* const restrict future,
        void** const restrict  result)
{
    // NB: This function can be called before `future_run()` due to asynchrony
    // in thread creation
    int status;

    future_assertValid(future);

    future_lock(future);
        status = future_wait(future);

        if (status == 0) {
            if (future->wasCanceled) {
                status = ECANCELED;
            }
            else if (future->runStatus) {
                status = EPERM;
            }
            else if (result) {
                *result = future->result;
            }
        }
    future_unlock(future);

    return status;
}

int
future_getAndFree(
        Future* const restrict future,
        void** const restrict  result)
{
    // NB: This function can be called before `future_run()` due to asynchrony
    // in thread creation

    int status = future_getResult(future, result);

    (void)future_free(future); // Doesn't free future's job

    return status;
}

int
future_getRunStatus(Future* const future)
{
    future_assertValid(future);

    mutex_lock(&future->mutex);
        int status = future->runStatus;
    mutex_unlock(&future->mutex);

    return status;
}

bool
future_areEqual(
        const Future* future1,
        const Future* future2)
{
    future_assertValid(future1);
    future_assertValid(future2);

    return future1 == future2;
}

/**
 * Prints the status of an LDM product-queue.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

/* 
 *  Monitor a product queue by reporting some of its vital statistics.
 */

#include <config.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <regex.h>
#include "ldm.h"
#include "atofeedt.h"
#include "globals.h"
#include "remote.h"
#include "ldmprint.h"
#include "timestamp.h"
#include "log.h"
#include "pq.h"
#include "md5.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

/* default "one trip" */
#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL 0
#endif

#ifndef DEFAULT_FEEDTYPE
#define DEFAULT_FEEDTYPE ANY
#endif

static volatile sig_atomic_t    intr = 0;
static int                      printSizePar = 0;

static void
usage(const char *av0) /*  id string */
{
        (void)fprintf(stderr,
"Usage: %s [options] [outputfile]\n\tOptions:\n", av0);
        (void)fprintf(stderr,
"\t-l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t             (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
        (void)fprintf(stderr,
"\t-q pqfname   (default \"%s\")\n", getDefaultQueuePath());
        (void)fprintf(stderr,
"\t-i interval  Poll queue after \"interval\" secs (default %d)\n",
                DEFAULT_INTERVAL);
        (void)fprintf(stderr,
"\t             (\"interval\" of 0 means exit at end of queue)\n");
        (void)fprintf(stderr,
"Output defaults to standard output\n");
        exit(1);
}


static void
cleanup(void)
{
        if (!printSizePar)
            log_notice_q("Exiting");

        if(!intr)
        {
                if(pq != NULL)  
                        (void)pq_close(pq);
        }
        log_fini();
}


static void
signal_handler(int sig)
{
    switch(sig) {
    case SIGINT :
        intr = 1;
        exit(0);
    case SIGTERM :
        done = 1;
        return;
    case SIGUSR1 :
        log_refresh();
        return;
    case SIGUSR2 :
        log_roll_level();
        return;
    }
}


/*
 * register the signal_handler
 */
static void
set_sigactions(void)
{
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /* Ignore the following */
    sigact.sa_handler = SIG_IGN;
    (void) sigaction(SIGPIPE, &sigact, NULL);
    (void) sigaction(SIGALRM, &sigact, NULL);
    (void) sigaction(SIGCHLD, &sigact, NULL);
    (void) sigaction(SIGCONT, &sigact, NULL); /* so won't be woken up for every product */

    /* Handle the following */
    sigact.sa_handler = signal_handler;

    /* Don't restart the following */
    (void) sigaction(SIGINT, &sigact, NULL);

    /* Restart the following */
    sigact.sa_flags |= SA_RESTART;
    (void) sigaction(SIGTERM, &sigact, NULL);
    (void) sigaction(SIGUSR1, &sigact, NULL);
    (void) sigaction(SIGUSR2, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGPIPE);
    (void)sigaddset(&sigset, SIGALRM);
    (void)sigaddset(&sigset, SIGCHLD);
    (void)sigaddset(&sigset, SIGCONT);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}


static void
hndlr_noop(int sig)
{
#ifndef NDEBUG
        switch(sig) {
        case SIGALRM :
                log_debug("SIGALRM") ;
                return ;
        }
        log_debug("hndlr_noop: unhandled signal: %d", sig) ;
#endif
        /* nothing to do, just wake up */
        return;
}


/*
 * Suspend yourself (sleep) until
 * one of the following events occurs:
 *   You receive a signal that you handle.
 *   "maxsleep" seconds elapse.
 *   If "maxsleep" is zero, you could sleep forever. 
 */
static int
xsuspend(unsigned int maxsleep)
{
        
        struct sigaction sigact, asavact;
        sigset_t mask, savmask;

        /* block ALRM while we set up */
        sigemptyset(&mask);
        if(maxsleep)
                sigaddset(&mask, SIGALRM);
        (void) sigprocmask(SIG_BLOCK, &mask, &savmask);

        /*
         * Set up handlers for ALRM, stashing old
         */
        sigemptyset(&sigact.sa_mask);
        sigact.sa_flags = 0;
        sigact.sa_handler = hndlr_noop;
        if(maxsleep)
        {
                /* set the alarm */
                (void) sigaction(SIGALRM, &sigact, &asavact);
                (void) alarm(maxsleep);
        }
        
        /*
         * Unblock the signals.
         */
        mask = savmask;
        if(maxsleep)
                sigdelset(&mask, SIGALRM);

        /* Nighty night... */
        (void) sigsuspend(&mask);

        /* Now we are back, restore state */
        if(maxsleep)
        {
                (void)alarm(0);
                (void) sigaction(SIGALRM, &asavact, NULL );
        }
        (void) sigprocmask(SIG_SETMASK, &savmask, NULL);

        return 0;
}



int
main(int ac, char *av[])
{
    const char* pqfname;
    const char* progname = basename(av[0]);
    int         status = 0;
    int         interval = DEFAULT_INTERVAL;
    int         list_extents = 0;
    int         extended = 0;

    /*
     * Set up default logging before calling anything that might log.
     */
    if (log_init(av[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }

    {
        extern int optind;
        extern int opterr;
        extern char *optarg;
        int ch;

        opterr = 1;

        while ((ch = getopt(ac, av, "Sevxl:q:o:i:")) != EOF)
            switch (ch) {
            case 'v':
                if (!log_is_enabled_info)
                    (void)log_set_level(LOG_LEVEL_INFO);
                break;
            case 'x':
                (void)log_set_level(LOG_LEVEL_DEBUG);
                list_extents = 1;
                break;
            case 'l':
                if (log_set_destination(optarg)) {
                    log_syserr("Couldn't set logging destination to \"%s\"",
                            optarg);
                    usage(progname);
                }
                break;
            case 'q':
                setQueuePath(optarg);
                break;
            case 'i':
                interval = atoi(optarg);
                if(interval == 0 && *optarg != '0')
                {
                    fprintf(stderr, "%s: invalid interval %s",
                        progname, optarg);
                    usage(progname);
                }
                break;
            case 'e': {
                extended = 1;
                break;
            }
            case 'S': {
                printSizePar = 1;
                break;
            }
            case '?':
                usage(progname);
                break;
            }

        /* last arg, outputfname, is optional */
        if(ac - optind > 0)
        {
            const char *const outputfname = av[optind];
            if(freopen(outputfname, "a+b", stdout) == NULL)
            {
                status = errno;
                fprintf(stderr, "%s: Couldn't open \"%s\": %s\n",
                    progname, outputfname, strerror(status));
            }
        }
    }

    pqfname = getQueuePath();           /* this might log */
    if (NULL == pqfname) {
        log_flush_error();
        exit(1);
    }

    /*
     * Set up error logging.
     */
    if (!printSizePar)
        log_notice_q("Starting Up (%d)", getpgrp());

    /*
     * register exit handler
     */
    if(atexit(cleanup) != 0)
    {
        log_syserr("atexit");
        exit(1);
    }

    /*
     * set up signal handlers
     */
    set_sigactions();


    /*
     * Open the product que
     */
    status = pq_open(pqfname, PQ_READONLY, &pq);
    if(status)
    {
        if (PQ_CORRUPT == status) {
            log_error_q("The product-queue \"%s\" is inconsistent\n",
                pqfname);
        }
        else {
            log_error_q("pq_open failed: %s: %s\n",
                pqfname, strerror(status));
        }
        exit(1);
    }

    if (!printSizePar) {
        if (extended) {
            log_notice_q("nprods nfree  nempty      nbytes  maxprods  maxfree  "
                "minempty    maxext    age    maxbytes");
        }
        else {
            log_notice_q("nprods nfree  nempty      nbytes  maxprods  maxfree  "
                "minempty    maxext  age");
        }
    }

    while(exitIfDone(1))
    {
        if (printSizePar) {
            size_t          nprods;
            size_t          nfree;
            size_t          nempty;
            size_t          nbytes;
            size_t          maxprods;
            size_t          maxfree;
            size_t          minempty;
            size_t          maxbytes;
            double          age_oldest;
            size_t          maxextent;
            double          age_youngest;
            long            minReside;
            off_t           mvrtSize;
            size_t          mvrtSlots;
            int             isFull;

            status = pq_stats(pq, &nprods,   &nfree,   &nempty, &nbytes,
                              &maxprods, &maxfree, &minempty, &maxbytes, 
                              &age_oldest, &maxextent);

            if (status) {
                log_error_q("pq_stats() failed: %s (errno = %d)", strerror(status),
                    status);
                exit(1);
            }

            if ((status = pq_isFull(pq, &isFull))) {
                log_error_q("pq_isFull() failed: %s (errno = %d)", strerror(status),
                    status);
                exit(1);
            }

            if (0 == nprods) {
                age_youngest = -1;
                minReside = -1;
                mvrtSize = -1;
                mvrtSlots = 0;
            }
            else {
                timestampt      now;
                timestampt      mostRecent;
                timestampt      minResidenceTime;

                if ((status = pq_getMostRecent(pq, &mostRecent))) {
                    log_error_q("pq_getMostRecent() failed: %s (errno = %d)",
                        strerror(status), status);
                    exit(1);
                }

                age_youngest = (0 == set_timestamp(&now))
                    ? d_diff_timestamp(&now, &mostRecent)
                    : -1;

                if ((status = pq_getMinVirtResTimeMetrics(pq,
                            &minResidenceTime, &mvrtSize, &mvrtSlots))) {
                    log_error_q("pq_getMinResidency() failed: %s (errno = %d)",
                        strerror(status), status);
                    exit(1);
                }

                minReside = (long)minResidenceTime.tv_sec;
            }

            printf("%d %lu %lu %lu %lu %lu %lu %.0f %.0f %ld %ld %lu\n", isFull,
                (unsigned long)pq_getDataSize(pq), (unsigned long)maxbytes,
                (unsigned long)nbytes,
                (unsigned long)pq_getSlotCount(pq), (unsigned long)maxprods,
                (unsigned long)nprods, age_oldest, age_youngest, minReside,
                (long)mvrtSize, (unsigned long)mvrtSlots);
        }
        else {
            size_t nprods;
            size_t nfree;
            size_t nempty;
            size_t nbytes;
            size_t maxprods;
            size_t maxfree;
            size_t minempty;
            size_t maxbytes;
            double age_oldest;
            size_t maxextent;

            status = pq_stats(pq, &nprods,   &nfree,   &nempty, &nbytes,
                              &maxprods, &maxfree, &minempty, &maxbytes, 
                              &age_oldest, &maxextent);

            if (status) {
                log_error_q("pq_stats() failed: %s (errno = %d)",
                   strerror(status), status);
                exit(1);
            }

            if (extended) {
                log_notice_q("%6ld %5lu %7lu %11lu %9lu %8lu %9lu %9lu %.0f %11lu",
                    nprods,   nfree,   nempty, nbytes,
                    maxprods, maxfree, minempty, maxextent, age_oldest,
                    maxbytes);
            }
            else {
                log_notice_q("%6ld %5lu %7lu %11lu %9lu %8lu %9lu %9lu %.0f",
                    nprods,   nfree,   nempty, nbytes,
                    maxprods, maxfree, minempty, maxextent, age_oldest);
            }
            if(list_extents) {
                status = pq_fext_dump(pq);
            }
            if (status) {
                log_error_q("pq_fext_dump failed: %s (errno = %d)",
                   strerror(status), status);
                exit(1);
            }
        }
        
        if(interval == 0)
            break;
        xsuspend(interval);
    }

    exit(0);
    /*NOTREACHED*/
}

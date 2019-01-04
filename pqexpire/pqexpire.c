/**
 * Delets old data-products from an LDM product-queue.
 *
 * **THIS PROGRAM IS DEPRECATED**
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <regex.h>
#include "log.h"
#include "ldm.h"
#include "ldmprint.h"
#include "atofeedt.h"
#include "globals.h"
#include "remote.h"
#include "pq.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL 300 /* .08333 of an hour */
#endif
#ifndef DEFAULT_AGE
#define DEFAULT_AGE (1. + (double)(DEFAULT_INTERVAL)/3600)
#endif
#ifndef DEFAULT_FEEDTYPE
#define DEFAULT_FEEDTYPE ANY
#endif
#ifndef DEFAULT_PATTERN
#define DEFAULT_PATTERN ".*"
#endif

struct expirestats {
        timestampt starttime;   /* stats start time */
        timestampt firsthit;    /* arrival time of first product deleted */
        timestampt lasthit;     /* arrival time of lastproduct deleted */
        unsigned nprods;        /* total number of products expired */
        unsigned nbytes;        /* total number of bytes recycled */
};
typedef struct expirestats expirestats;


static void
minstats(const expirestats *stp)
{
        
        static unsigned lastnprods = 0;

        if(pq != NULL && log_is_enabled_info)
        {
                off_t highwater = -1;
                size_t maxregions = 0;
                (void) pq_highwater(pq, &highwater, &maxregions);
                log_info_q("> Queue usage (bytes):%8ld",
                                        (long)highwater);
                log_info_q(">          (nregions):%8ld",
                                        (long)maxregions);
        }

        if(stp->nprods != 0 && stp->nprods != lastnprods)
        {
                double elapsed = d_diff_timestamp(&stp->lasthit,
                         &stp->firsthit);
                elapsed /= 3600;
                log_notice_q("> Recycled %10.3f kb/hr (%10.3f prods per hour)",
                        ((double)stp->nbytes)/(1024 * elapsed),
                        ((double)stp->nprods)/elapsed
                );
                lastnprods = stp->nprods;
        }
}

static void
dump_stats(const expirestats *stp)
{
        char cp[32];

        sprint_timestampt(cp, sizeof(cp), &stp->starttime);
        log_notice_q("> Up since:      %s", cp);

        if(pq != NULL)
        {
                off_t highwater = -1;
                size_t maxregions = 0;
                (void) pq_highwater(pq, &highwater, &maxregions);
                log_notice_q("> Queue usage (bytes):%8ld",
                                        (long)highwater);
                log_notice_q(">          (nregions):%8ld",
                                        (long)maxregions);
        }

        if(stp->nprods != 0)
        {
                double elapsed = d_diff_timestamp(&stp->lasthit,
                         &stp->firsthit);
                
                elapsed /= 3600;
                log_notice_q("> nbytes recycle:   %10u (%10.3f kb/hr)",
                        stp->nbytes, ((double)stp->nbytes)/(1024 * elapsed));
                log_notice_q("> nprods deleted:   %10u (%10.3f per hour)",
                        stp->nprods, ((double)stp->nprods)/elapsed);

                sprint_timestampt(cp, sizeof(cp), &stp->firsthit);
                log_notice_q("> First deleted: %s", cp);
        
                sprint_timestampt(cp, sizeof(cp), &stp->lasthit);
                log_notice_q("> Last  deleted: %s", cp);
        }
        else
        {
                log_notice_q("> nprods deleted 0");
        }
}


static expirestats stats;


static void
usage(const char *av0) /*  id string */
{
        (void)fprintf(stderr,
"Usage: %s [options]\nOptions:\n", av0);
        (void)fprintf(stderr,
"\t-v           Verbose, report each notification\n");
        (void)fprintf(stderr,
"\t-x           Debug mode\n");
        (void)fprintf(stderr,
"\t-w           Wait on region locks\n");
        (void)fprintf(stderr,
"\t-l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t             (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
        (void)fprintf(stderr,
"\t-q queue     default \"%s\"\n", getDefaultQueuePath());
        (void)fprintf(stderr,
"\t-a age       Protect products younger than \"age\" hours (default %.4f)\n",
                DEFAULT_AGE);
        (void)fprintf(stderr,
"\t-i interval  loop, restart each \"interval\" seconds (default %d)\n",
                DEFAULT_INTERVAL);
        (void)fprintf(stderr,
"\t             interval of 0 means exit after one pass\n");
        (void)fprintf(stderr,
"\t-f feedtype  Delete products from feed \"feedtype\" (default %s)\n",
                s_feedtypet(DEFAULT_FEEDTYPE));
        (void)fprintf(stderr,
"\t-p pattern   Delete products matching \"pattern\" (default \"%s\")\n",
                DEFAULT_PATTERN);
        exit(1);
}


static void
cleanup(void)
{
        log_notice_q("Exiting");

        dump_stats(&stats);

        if(pq != NULL)  
        {
                (void)pq_close(pq);
                pq = NULL;
        }

        log_fini();
}

static volatile sig_atomic_t stats_req = 0;

static void
signal_handler(int sig)
{
    switch(sig) {
    case SIGINT :
        exit(0);
    case SIGTERM :
        done = 1;
        sleep(0); /* redundant on many systems, needed on others */
        return;
    case SIGUSR1 :
        log_refresh();
        stats_req = 1;
        return;
    case SIGUSR2 :
        log_roll_level();
        return;
    }
}


static void
set_sigactions(void)
{
    struct sigaction sigact;
    (void) sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /* Handle the following */
    sigact.sa_handler = signal_handler;

    /* Don't restart the following */
    (void) sigaction(SIGINT, &sigact, NULL);

    /* Restart the following */
    sigact.sa_flags |= SA_RESTART;
    (void) sigaction(SIGUSR1, &sigact, NULL);
    (void) sigaction(SIGUSR2, &sigact, NULL);
    (void) sigaction(SIGTERM, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}


int main(ac,av)
int ac;
char *av[];
{
        int status;
        double age = DEFAULT_AGE;
        prod_class_t clss;
        prod_spec spec;
        int wait = 0;  /* flag to indicate whether to wait on locks */
        size_t nr; /* number of bytes recycled this seq call */
        timestampt ts;
        timestampt cursor;
        double diff; 
        double max_latency = 0; /* reset on each pass */

        /*
         * initialize logger
         */
        (void)log_init(av[0]);

        (void) set_timestamp(&stats.starttime);
        stats.firsthit = TS_ENDT;
        stats.lasthit = TS_ZERO;

        clss.from = TS_ZERO;
        clss.to = stats.starttime;
        clss.psa.psa_len = 1;
        clss.psa.psa_val = &spec;
        spec.feedtype = DEFAULT_FEEDTYPE;
        spec.pattern = DEFAULT_PATTERN;

        {
        extern int optind;
        extern int opterr;
        extern char *optarg;
        int ch;
        int fterr;

        opterr = 1;

        while ((ch = getopt(ac, av, "wvxl:p:f:q:a:i:")) != EOF)
                switch (ch) {
                case 'w':
                        wait = 1;
                        break;
                case 'v':
                        if (!log_is_enabled_info)
                            (void)log_set_level(LOG_LEVEL_INFO);
                        break;
                case 'x':
                        (void)log_set_level(LOG_LEVEL_DEBUG);
                        break;
                case 'l':
                        (void)log_set_destination(optarg);
                        break;
                case 'a':
                        age = atof(optarg);
                        if(age < 0)
                        {
                            (void) fprintf(stderr,
                                        "age (%s) must be non negative\n",
                                        optarg);
                                usage(av[0]);   
                        }
                        break;
                case 'p':
                        spec.pattern = optarg;
                        /* compiled below */
                        break;
                case 'f':
                        fterr = strfeedtypet(optarg, &spec.feedtype);
                        if(fterr != FEEDTYPE_OK)
                        {
                                (void) fprintf(stderr,
                                        "Bad feedtype \"%s\", %s\n",
                                        optarg, strfeederr(fterr));
                                usage(av[0]);   
                        }
                        break;
                case 'q':
                        setQueuePath(optarg);
                        break;
                case 'i':
                        interval = atoi(optarg);
                        if(interval == 0 && *optarg != '0')
                        {
                                (void) fprintf(stderr,
                                        "%s: invalid interval %s\n",
                                         av[0], optarg);
                                usage(av[0]);   
                        }
                        break;
                case '?':
                        usage(av[0]);
                        break;
                }
        if(ac - optind != 0)
                usage(av[0]);   

        status = regcomp(&spec.rgx,
                spec.pattern,
                REG_EXTENDED|REG_NOSUB);
        if(status != 0)
        {
                fprintf(stderr, "Bad regular expression \"%s\"\n",
                        spec.pattern);
                usage(av[0]);
        }

        age *= 3600;
        clss.to.tv_sec -= age;
        }

        log_notice_q("Starting Up");

        /*
         * Open the product queue
         */
        const char* const pqfname = getQueuePath();
        status = pq_open(pqfname, PQ_DEFAULT, &pq);
        if(status)
        {
                if (PQ_CORRUPT == status) {
                    log_error_q("The product-queue \"%s\" is inconsistent\n",
                            pqfname);
                }
                else {
                    log_error_q("pq_open failed: %s: %s",
                            pqfname, strerror(status));
                }
                exit(1);
        }

        /*
         * register exit handler
         */
        if(atexit(cleanup) != 0)
        {
                log_syserr_q("atexit");
                exit(1);
        }

        /*
         * set up signal handlers
         */
        set_sigactions();


        /*
         * Main loop
         */
        pq_cset(pq, &TS_ZERO);
        while(exitIfDone(1))
        {
                if(stats_req)
                {
                        dump_stats(&stats);
                        stats_req = 0;
                }

                nr = 0;
                status = pq_seqdel(pq, TV_GT, &clss, wait, &nr, &ts);

                (void)exitIfDone(1);

                switch(status) {
                case ENOERR:
                        /*
                         * No error occurred.  The product-queue cursor was
                         * advanced to the next data-product, which might or
                         * might not have been removed, depending on whether or
                         * not it matched the product-class specification.
                         */
                        pq_ctimestamp(pq, &cursor);
                        diff = d_diff_timestamp(&cursor, &ts);
                        if(diff > max_latency)
                        {
                                max_latency = diff;
                                log_debug("max_latency %.3f", max_latency);
                        }

                        if(nr != 0)
                        {
                                /*
                                 * The data-product was removed.
                                 */
                                stats.nprods ++;
                                stats.nbytes += nr;
                                if(d_diff_timestamp(&stats.firsthit,
                                                 &ts) > 0)
                                {
                                        stats.firsthit = ts;
                                }
                                if(d_diff_timestamp(&ts,
                                                 &stats.lasthit) > 0)
                                {
                                        stats.lasthit = ts;
                                }
                        }
                        else if(interval != 0)
                        {
                                /*
                                 * The data-product was not removed and the
                                 * product-queue is periodically scanned.
                                 */
                                diff = d_diff_timestamp(&cursor, &clss.to);
                                log_debug("diff %.3f", diff);
                                if(diff > interval + max_latency)
                                {
                                        log_debug("heuristic depth break");
                                        break;
                                }
                        }
                        continue; /* N.B., other cases sleep */
                case PQUEUE_END:
                        log_debug("End of Queue");
                        break;
                case EAGAIN:
                case EACCES:
                        /*
                         * The next data-product was locked.  The product-queue
                         * cursor was not advanced to it.
                         */
                        log_debug("Hit a lock");
                        /* N.B.: peculiar logic ahead */
                        if(interval != 0)
                        {
                                /*
                                 * The product-queue is periodically scanned.
                                 */
                                pq_ctimestamp(pq, &cursor);
                                diff = d_diff_timestamp(&cursor, &clss.to);
                        }
                        if(interval == 0
                                        /* one trip */
                                        || diff < 0)
                                        /*
                                         * OR
                                         * We are still in the rang
                                         * of things which might need to
                                         * be expired.
                                         */
                        {
                                /* Tunnel */
                                /* Could be a race here. */
                                status = pq_sequence(pq, TV_GT,
                                         NULL, NULL, NULL);

                                (void)exitIfDone(1);

                                if(status == ENOERR)
                                {
                                        /*
                                         * The product-queue cursor was advanced
                                         * to the next data-product.
                                         */
                                        continue;
                                }
                                /* else */
                                if(status != PQUEUE_END)
                                        log_error_q("pq_sequence failed: %s",
                                                        strerror(status));
                                break;
                        }
                        /* else, give up */
                        break;
#if defined(EDEADLOCK) && EDEADLOCK != EDEADLK
                case EDEADLOCK:
#endif
                case EDEADLK:
                        log_errno_q(status, NULL);
                        break;
                default:
                        log_errno_q(status, "pq_seqdel failed");
                        exit(1);
                        break;
                }

                (void)exitIfDone(1);

                if(interval == 0)
                        break;
                /*
                 * interval => wait awhile & try again
                 */
                minstats(&stats);
                (void) sleep(interval);
                (void)exitIfDone(1);
                (void) set_timestamp(&clss.to);
                clss.to.tv_sec -= age;
                /* Each pass, go thru whole inventory from old to the new */
                pq_cset(pq, &TS_ZERO);
                max_latency = 0;
        }

        exit(0);
}

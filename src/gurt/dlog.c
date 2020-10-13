/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * Portions of this file are based on The Self-* Storage System Project
 * Copyright (c) 2004-2011, Carnegie Mellon University.
 * All rights reserved.
 * http://www.pdl.cmu.edu/  (Parallel Data Lab at Carnegie Mellon)
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to reproduce, use, and prepare derivative works of this
 * software is granted provided the copyright and "No Warranty" statements
 * are included with all reproductions and derivative works and associated
 * documentation. This software may also be redistributed without charge
 * provided that the copyright and "No Warranty" statements are included
 * in all redistributions.
 *
 * NO WARRANTY. THIS SOFTWARE IS FURNISHED ON AN "AS IS" BASIS.
 * CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER
 * EXPRESSED OR IMPLIED AS TO THE MATTER INCLUDING, BUT NOT LIMITED
 * TO: WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY
 * OF RESULTS OR RESULTS OBTAINED FROM USE OF THIS SOFTWARE. CARNEGIE
 * MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT
 * TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
 * COPYRIGHT HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE
 * OR DOCUMENTATION.
 */
/**
 * This file is part of CaRT. It implements message logging system.
 */

#define DLOG_MUTEX

#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifdef DLOG_MUTEX
#include <pthread.h>
#endif

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <sys/stat.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <gurt/dlog.h>
#include <gurt/common.h>
#include <gurt/list.h>

/* extra tag bytes to alloc for a pid */
#define DLOG_TAGPAD 16

enum {
	/** minimum log file size is 1MB */
	LOG_SIZE_MIN	= (1ULL << 20),
	/** default log file size is 1GB */
	LOG_SIZE_DEF	= (1ULL << 30),
};

/**
 * internal global state
 */
struct d_log_state {
	/** logfile name [malloced] */
	char		*log_file;
	/** backup file name */
	char		*log_old;
	/** buffer to cache log messages */
	char		*log_buf;
	/** Number Of Bytes in the buffer */
	off_t		 log_buf_nob;
	/** fd of the open logfile */
	int		 log_fd;
	/** fd of the backup logfile, only for log_sync on assertion failure */
	int		 log_old_fd;
	/** current size of log file */
	uint64_t	 log_size;
	/** max size of log file */
	uint64_t	 log_size_max;
	/* note: tag, dlog_facs, and fac_cnt are in xstate now */
	int def_mask;		/* default facility mask value */
	int stderr_mask;	/* mask above which we send to stderr  */
	int oflags;		/* open flags */
	int fac_alloc;		/* # of slots in facs[] (>=fac_cnt) */
	struct utsname uts;	/* for hostname, from uname(3) */
	int stdout_isatty;	/* non-zero if stdout is a tty */
	int stderr_isatty;	/* non-zero if stderr is a tty */
#ifdef DLOG_MUTEX
	pthread_mutex_t clogmux;	/* protect clog in threaded env */
#endif
};

struct cache_entry {
	int		*ce_cache;
	d_list_t	 ce_link;
	int		 ce_nr;
};

/*
 * global data. Zero initialization means the log is not open.
 * this is global so clog_filter() in dlog.h can get at it.
 */
struct d_log_xstate d_log_xst;

static struct d_log_state mst;
static d_list_t	d_log_caches;


/* default name for facility 0 */
static const char *default_fac0name = "CLOG";

#ifdef DLOG_MUTEX
#define clog_lock()		D_MUTEX_LOCK(&mst.clogmux)
#define clog_unlock()		D_MUTEX_UNLOCK(&mst.clogmux)
#else
#define clog_lock()
#define clog_unlock()
#endif

static int d_log_write(char *buf, int len, bool flush);
static const char *clog_pristr(int);
static int clog_setnfac(int);

/* static arrays for converting between pri's and strings */
static const char * const norm[] = { "DBUG", "INFO", "NOTE", "WARN", "ERR ",
				     "CRIT", "ALRT", "EMRG"};
/**
 * clog_pristr: convert priority to 4 byte symbolic name.
 *
 * \param pri [IN]		the priority to convert to a string
 *
 * \return			the string (symbolic name) of the priority
 */
static const char *clog_pristr(int pri)
{
	int s;

	pri = pri & DLOG_PRIMASK;
	s = (pri >> DLOG_PRISHIFT) & 7;
	return norm[s];
}

/**
 * clog_setnfac: set the number of facilities allocated (including default
 * to a given value).   clog must be open for this to do anything.
 * we set the default name for facility 0 here.
 * caller must hold clog_lock.
 *
 * \param n [IN]	the number of facilities to allocate space for now.
 *
 * \return		zero on success, -1 on error.
 */
static int clog_setnfac(int n)
{
	int try, lcv;
	struct dlog_fac *nfacs;

	/*
	 * no need to check d_log_xst.tag to see if clog is open or not,
	 * since caller holds clog_lock already it must be ok.
	 */

	/* hmm, already done */
	if (n <= d_log_xst.fac_cnt)
		return 0;

	/* can we expand in place? */
	if (n <= mst.fac_alloc) {
		d_log_xst.fac_cnt = n;
		return 0;
	}
	/* must grow the array */
	try = (n < 1024) ? (n + 32) : n; /* pad a bit for small values of n */
	nfacs = calloc(1, try * sizeof(*nfacs));
	if (!nfacs)
		return -1;

	/* over the hump, setup the new array */
	lcv = 0;
	if (d_log_xst.dlog_facs && d_log_xst.fac_cnt) {	/* copy old? */
		for (/*null */ ; lcv < d_log_xst.fac_cnt; lcv++)
			nfacs[lcv] = d_log_xst.dlog_facs[lcv];
	}
	for (/*null */ ; lcv < try; lcv++) {	/* init the new */
		nfacs[lcv].fac_mask = mst.def_mask;
		nfacs[lcv].fac_aname =
		    (lcv == 0) ? (char *) default_fac0name : NULL;
		nfacs[lcv].fac_lname = NULL;
		nfacs[lcv].is_enabled = true; /* enable all facs by default */
	}
	/* install */
	if (d_log_xst.dlog_facs)
		free(d_log_xst.dlog_facs);
	d_log_xst.dlog_facs = nfacs;
	d_log_xst.fac_cnt = n;
	mst.fac_alloc = try;
	return 0;
}

/**
 * clog_bput: copy a string to a buffer, counting the bytes
 *
 * @param bpp pointer to output pointer (we advance it)
 * @param skippy pointer to bytes to skip
 * @param residp pointer to length of buffer remaining
 * @param totcp pointer to total bytes moved counter
 * @param str the string to copy in (null to just add a \0)
 */
static void clog_bput(char **bpp, int *skippy, int *residp, int *totcp,
		      const char *str)
{
	static const char *nullsrc = "X\0\0"; /* 'X' is a non-null dummy char */
	const char *sp;

	if (str == NULL) /* trick to allow a null insert */
		str = nullsrc;
	for (sp = str; *sp; sp++) {
		if (sp == nullsrc)
			sp++;	/* skip over 'X' to null */
		if (totcp)
			(*totcp)++;	/* update the total */
		if (skippy && *skippy > 0) {
			(*skippy)--;	/* honor skip */
			continue;
		}
		if (*residp > 0 && *bpp != NULL) {
			/* copyout if buffer w/space */
			**bpp = *sp;
			(*bpp)++;
			(*residp)--;
		}
	}
}

static void
reset_caches(bool lock_held)
{
	struct cache_entry	*ce;
	int			 i;

	if (!lock_held)
		clog_lock();
	d_list_for_each_entry(ce, &d_log_caches, ce_link) {
		for (i = 0; i < ce->ce_nr; i++)
			ce->ce_cache[i] = DLOG_UNINIT;
	}
	if (!lock_held)
		clog_unlock();
}

void
d_log_add_cache(int *cache, int nr)
{
	struct cache_entry	*ce;

	/* Can't use D_ALLOC yet */
	ce = malloc(sizeof(*ce));
	if (ce == NULL)
		return;

	clog_lock();
	ce->ce_cache = cache;
	ce->ce_nr = nr;
	d_list_add(&ce->ce_link, &d_log_caches);
	clog_unlock();
}


/**
 * dlog_cleanout: release previously allocated resources (e.g. from a
 * close or during a failed open).  this function assumes the clogmux
 * has been allocated (caller must ensure that this is true or we'll
 * die when attempting a clog_lock()).  we will dispose of clogmux.
 * (XXX: might want to switch over to a PTHREAD_MUTEX_INITIALIZER for
 * clogmux at some point?).
 *
 * the caller handles cleanout of d_log_xst.tag (not us).
 */
static void dlog_cleanout(void)
{
	struct cache_entry	*ce;
	int			 lcv;

	clog_lock();
	if (mst.log_file) {
		if (mst.log_fd >= 0) {
			d_log_write(NULL, 0, true);
			close(mst.log_fd);
		}
		mst.log_fd = -1;
		free(mst.log_file);
		mst.log_file = NULL;
	}

	if (mst.log_old) {
		if (mst.log_old_fd >= 0)
			close(mst.log_old_fd);
		mst.log_old_fd = -1;
		free(mst.log_old);
		mst.log_old = NULL;
	}

	if (mst.log_buf) {
		free(mst.log_buf);
		mst.log_buf = NULL;
		mst.log_buf_nob = 0;
	}

	if (d_log_xst.dlog_facs) {
		/*
		 * free malloced facility names, being careful not to free
		 * the static default_fac0name....
		 */
		for (lcv = 0; lcv < mst.fac_alloc; lcv++) {
			if (d_log_xst.dlog_facs[lcv].fac_aname &&
			    d_log_xst.dlog_facs[lcv].fac_aname !=
			    default_fac0name)
				free(d_log_xst.dlog_facs[lcv].fac_aname);
			if (d_log_xst.dlog_facs[lcv].fac_lname)
				free(d_log_xst.dlog_facs[lcv].fac_lname);
		}
		free(d_log_xst.dlog_facs);
		d_log_xst.dlog_facs = NULL;
		d_log_xst.fac_cnt = mst.fac_alloc = 0;
	}

	reset_caches(true); /* Log is going away, reset cached masks */
	while ((ce = d_list_pop_entry(&d_log_caches,
				      struct cache_entry, ce_link)))
		free(ce);
	clog_unlock();
#ifdef DLOG_MUTEX
	/* XXX
	 * do not destroy mutex to allow correct execution of dlog_sync()
	 * which as been registered using atexit() and to be run upon exit()
	 */
	 /* D_MUTEX_DESTROY(&mst.clogmux); */
#endif
}

static __thread int	 pre_err;
static __thread int	 pre_err_line;
static __thread uint64_t pre_err_time;

#define dlog_print_err(err, fmt, ...)				\
do {								\
	struct timeval	_tv;					\
								\
	gettimeofday(&_tv, NULL);				\
	if (pre_err_line == __LINE__ &&				\
	    pre_err == (err) &&	_tv.tv_sec <= pre_err_time)	\
		break;						\
	pre_err_time = _tv.tv_sec;				\
	pre_err_line = __LINE__;				\
	pre_err	     = err;					\
	fprintf(stderr, "%s: %d: err=%d " fmt,			\
		__func__, __LINE__, err,  ## __VA_ARGS__);	\
} while (0)

#define LOG_BUF_SIZE	(16 << 10)

/**
 * This function can do a few things:
 * - copy log message @msg to log buffer
 * - if the log buffer is full, write it to log file
 * - if the log file is too large (exceeds to threshold), rename it to
 *   original_name.old, and create a new log file.
 *
 * If @flush is true, it writes log buffer to log file immediately.
 */
static int
d_log_write(char *msg, int len, bool flush)
{
	int	 rc;

	if (mst.log_fd < 0)
		return 0;

	if (len >= LOG_BUF_SIZE) {
		dlog_print_err(0, "message='%.64s' is too long, len=%d\n",
			       msg, len);
		return 0;
	}

	if (!mst.log_buf) {
		mst.log_buf = malloc(LOG_BUF_SIZE);
		if (!mst.log_buf) {
			dlog_print_err(ENOMEM, "failed to alloc log buffer\n");
			return -1;
		}
	}
	D_ASSERT(!msg || len);
 again:
	if (msg && (len <= LOG_BUF_SIZE - mst.log_buf_nob)) {
		/* the current buffer is not full */
		strncpy(&mst.log_buf[mst.log_buf_nob], msg, len);
		mst.log_buf_nob += len;
		if (!flush)
			return 0; /* short path done */

		msg = NULL; /* already copied into log buffer */
		len = 0;
	}
	/* write log buffer to log file */

	if (mst.log_buf_nob == 0)
		return 0; /* nothing to write */

	if (mst.log_size + mst.log_buf_nob >= mst.log_size_max) {
		/* exceeds the size threshold, rename the current log file
		 * as backup, create a new log file.
		 */
		if (!mst.log_old) {
			rc = asprintf(&mst.log_old, "%s.old", mst.log_file);
			if (rc < 0) {
				dlog_print_err(errno, "failed to alloc name\n");
				return -1;
			}
		}

		if (mst.log_old_fd >= 0) {
			close(mst.log_old_fd);
			mst.log_old_fd = -1;
		}

		/* remove the backup log file */
		rc = unlink(mst.log_old);
		if (rc && errno != ENOENT) {
			dlog_print_err(errno, "failed to unlink old file\n");
			return -1;
		}

		/* rename the current log file as a backup */
		rc = rename(mst.log_file, mst.log_old);
		if (rc) {
			dlog_print_err(errno, "failed to rename log file\n");
			return -1;
		}
		mst.log_old_fd = mst.log_fd;

		/* create a new log file */
		mst.log_fd = open(mst.log_file,  O_RDWR | O_CREAT, 0666);
		if (mst.log_fd < 0) {
			dlog_print_err(errno, "failed to recreate log file\n");
			return -1;
		}
		mst.log_size = 0;
	}

	/* flush the cached log messages */
	rc = write(mst.log_fd, mst.log_buf, mst.log_buf_nob);
	if (rc < 0) {
		dlog_print_err(errno, "failed to write log\n");
		return -1;
	}
	mst.log_size += mst.log_buf_nob;
	mst.log_buf_nob = 0;
	if (msg) /* the current message is not processed yet */
		goto again;

	return 0;
}

void
d_log_sync(void)
{
	int	rc;

	clog_lock();
	if (mst.log_buf_nob > 0) /* write back the inflight buffer */
		d_log_write(NULL, 0, true);

	if (mst.log_fd >= 0) {
		rc = fsync(mst.log_fd);
		if (rc < 0)
			dlog_print_err(errno, "failed to sync log file\n");
	}

	if (mst.log_old_fd >= 0) {
		rc = fsync(mst.log_old_fd);
		if (rc < 0)
			dlog_print_err(errno, "failed to sync log backup\n");

		close(mst.log_old_fd);
		mst.log_old_fd = -1; /* nobody is going to write it again */
	}
	clog_unlock();
}

/**
 * d_vlog: core log function, front-ended by d_log
 * we vsnprintf the message into a holding buffer to format it.  then we
 * send it to all target output logs.  the holding buffer is set to
 * DLOG_TBSIZ, if the message is too long it will be silently truncated.
 * caller should not hold clog_lock, d_vlog will grab it as needed.
 *
 * @param flags returned by d_log_check
 * @param fmt the printf(3) format to use
 * @param ap the stdargs va_list to use for the printf format
 */
void d_vlog(int flags, const char *fmt, va_list ap)
{
#define DLOG_TBSIZ    1024	/* bigger than any line should be */
	static __thread char b[DLOG_TBSIZ];
	static uint64_t	last_flush;

	int fac, lvl;
	bool flush;
	char *b_nopt1hdr;
	char facstore[16], *facstr;
	struct timeval tv;
	struct tm *tm;
	unsigned int hlen_pt1, hlen, mlen, tlen;
	/*
	 * since we ignore any potential errors in CLOG let's always re-set
	 * errno to its original value
	 */
	int save_errno = errno;
	int rc;

	if (flags == 0)
		return;

	fac = flags & DLOG_FACMASK;
	lvl = flags & DLOG_PRIMASK;

	/* Check the facility so we don't crash.   We will just log the message
	 * in this case but it really is indicative of a usage error as user
	 * didn't pass sanitized flags to this routine
	 */
	if (fac >= d_log_xst.fac_cnt)
		fac = 0;

	/* Assumes stderr mask isn't used for debug messages */
	if (mst.stderr_mask != 0 && lvl >= mst.stderr_mask)
		flags |= DLOG_STDERR;

	/*
	 * we must log it, start computing the parts of the log we'll need.
	 */
	clog_lock();		/* lock out other threads */
	if (d_log_xst.dlog_facs[fac].fac_aname) {
		facstr = d_log_xst.dlog_facs[fac].fac_aname;
	} else {
		snprintf(facstore, sizeof(facstore), "%d", fac);
		facstr = facstore;
	}
	(void) gettimeofday(&tv, 0);
	tm = localtime(&tv.tv_sec);
	if (tm == NULL) {
		dlog_print_err(errno, "localtime returned NULL\n");
		clog_unlock();
		return;
	}

	/*
	 * ok, first, put the header into b[]
	 */
	hlen = 0;
	if (mst.oflags & DLOG_FLV_YEAR)
		hlen = snprintf(b, sizeof(b), "%04d/", tm->tm_year + 1900);

	hlen += snprintf(b + hlen, sizeof(b) - hlen,
			 "%02d/%02d-%02d:%02d:%02d.%02ld %s ",
			 tm->tm_mon + 1, tm->tm_mday,
			 tm->tm_hour, tm->tm_min, tm->tm_sec,
			 (long int) tv.tv_usec / 10000, mst.uts.nodename);

	if (mst.oflags & DLOG_FLV_TAG) {
		if (mst.oflags & DLOG_FLV_LOGPID) {
			static __thread pid_t tid = -1;

			if (tid == -1)
				tid = (pid_t)syscall(SYS_gettid);

			hlen += snprintf(b + hlen, sizeof(b) - hlen, "%s/%d] ",
					 d_log_xst.tag, tid);
		} else {
			hlen += snprintf(b + hlen, sizeof(b) - hlen, "%s ",
					 d_log_xst.tag);
		}
	}

	hlen_pt1 = hlen;	/* save part 1 length */
	if (hlen < sizeof(b)) {
		if (mst.oflags & DLOG_FLV_FAC)
			hlen += snprintf(b + hlen, sizeof(b) - hlen,
					 "%-4s ", facstr);

		hlen += snprintf(b + hlen, sizeof(b) - hlen, "%s ",
				 clog_pristr(lvl));
	}
	/*
	 * we expect there is still room (i.e. at least one byte) for a
	 * message, so this overflow check should never happen, but let's
	 * check for it anyway.
	 */
	if (hlen + 1 >= sizeof(b)) {
		clog_unlock();	/* drop lock, this is the only early exit */
		dlog_print_err(E2BIG,
			       "header overflowed %zd byte buffer (%d)\n",
			       sizeof(b), hlen + 1);
		errno = save_errno;
		return;
	}
	/*
	 * now slap in the user's data at the end of the buffer
	 */
	mlen = vsnprintf(b + hlen, sizeof(b) - hlen, fmt, ap);
	/*
	 * compute total length, check for overflows...  make sure the string
	 * ends in a newline.
	 */
	tlen = hlen + mlen;
	/* if overflow or totally full without newline at end ... */
	if (tlen >= sizeof(b) ||
	    (tlen == sizeof(b) - 1 && b[sizeof(b) - 2] != '\n')) {
		tlen = sizeof(b) - 1;	/* truncate, counting final null */
		/*
		 * could overwrite the end of b with "[truncated...]" or
		 * something like that if we wanted to note the problem.
		 */
		b[sizeof(b) - 2] = '\n';	/* jam a \n at the end */
	} else {
		/* it fit, make sure it ends in newline */
		if (b[tlen - 1] != '\n') {
			D_ASSERT(tlen < DLOG_TBSIZ-1);
			b[tlen++] = '\n';
			b[tlen] = 0;
		}
	}
	b_nopt1hdr = b + hlen_pt1;
	if (mst.oflags & DLOG_FLV_STDOUT)
		flags |= DLOG_STDOUT;

	if (mst.oflags & DLOG_FLV_STDERR)
		flags |= DLOG_STDERR;

	/* log message is ready to be dispatched, write to log file.
	 * NB: flush to logfile if the message is important (warning/error...)
	 * or the last flush was 1+ second ago.
	 */
	flush = (lvl >= DLOG_WARN) || (tv.tv_sec > last_flush);
	if (flush)
		last_flush = tv.tv_sec;

	rc = d_log_write(b, tlen, flush);
	if (rc < 0)
		errno = save_errno;

	clog_unlock();		/* drop lock here */
	/*
	 * log it to stderr and/or stdout.  skip part one of the header
	 * if the output channel is a tty
	 */
	if (flags & DLOG_STDERR) {
		if (mst.stderr_isatty)
			fprintf(stderr, "%s", b_nopt1hdr);
		else
			fprintf(stderr, "%s", b);
	}
	if (flags & DLOG_STDOUT) {
		if (mst.stderr_isatty)
			printf("%s", b_nopt1hdr);
		else
			printf("%s", b);
		fflush(stdout);
	}
	/* done! */
	errno = save_errno;
}

/*
 * d_log_str2pri: convert a priority string to an int pri value to allow
 * for more user-friendly programs.  returns -1 (an invalid pri) on error.
 * does not access clog global state.
 *
 * len should be the number of characters to evaluate, not including any
 * termination character.
 */
static int d_log_str2pri(const char *pstr, size_t len)
{
	int lcv;

	/* make sure we have a valid input */
	if (len == 0 || len > 7) {
		return -1;
	}

	/*
	 * handle some quirks
	 */

	if (strncasecmp(pstr, "ERR", len) == 0)
		/* has trailing space in the array */
		return DLOG_ERR;
	if (strncasecmp(pstr, "DEBUG", len) == 0 || \
		strncasecmp(pstr, "DBUG", len) == 0) {
		/* check to see is debug mask bits are set */
		return d_dbglog_data.dd_mask != 0 ?
		       d_dbglog_data.dd_mask : DLOG_DBG;
	}

	/*
	 * handle non-debug case
	 */
	for (lcv = 1; lcv <= 7; lcv++)
		if (strncasecmp(pstr, norm[lcv], len) == 0)
			return lcv << DLOG_PRISHIFT;
	/* bogus! */
	return -1;
}

/*
 * d_getenv_size: interpret a string as a size which can
 * contain a unit symbol.
 */
static uint64_t
d_getenv_size(char *env)
{
	char *end;
	uint64_t size;

	size = strtoull(env, &end, 0);

	switch (*end) {
	default:
		break;
	case 'k':
		size *= 1000;
		break;
	case 'm':
		size *= 1000 * 1000;
		break;
	case 'g':
		size *= 1000 * 1000 * 1000;
		break;
	case 'K':
		size *= 1024;
		break;
	case 'M':
		size *= 1024 * 1024;
		break;
	case 'G':
		size *= 1024 * 1024 * 1024;
		break;
	}

	return size;
}

/*
 * d_log_open: open a clog (uses malloc, inits global state).  you
 * can only have one clog open at a time, but you can use multiple
 * facilities.
 *
 * if an clog is already open, then this call will fail.  if you use
 * the message buffer, you prob want it to be 1K or larger.
 *
 * return 0 on success, -1 on error.
 */
int
d_log_open(char *tag, int maxfac_hint, int default_mask, int stderr_mask,
	      char *logfile, int flags)
{
	int	tagblen;
	char	*newtag = NULL, *cp;
	int	truncate = 0, rc;
	char	*env;
	char	*buffer = NULL;
	uint64_t log_size = LOG_SIZE_DEF;

	env = getenv(D_LOG_TRUNCATE_ENV);
	if (env != NULL && atoi(env) > 0)
		truncate = 1;

	env = getenv(D_LOG_SIZE_ENV);
	if (env != NULL) {
		log_size = d_getenv_size(env);
		if (log_size < LOG_SIZE_MIN)
			log_size = LOG_SIZE_MIN;
	}

	env = getenv(D_LOG_FILE_APPEND_PID_ENV);
	if (logfile != NULL && env != NULL) {
		if (strcmp(env, "0") != 0) {
			/* Append pid/tgid to log file name */
			rc = asprintf(&buffer, "%s.%d", logfile, getpid());
			if (buffer != NULL && rc != -1)
				logfile = buffer;
			else
				D_PRINT_ERR("Failed to append pid to DAOS debug log name, continuing.\n");
		}
	}

	/* quick sanity check (mst.tag is non-null if already open) */
	if (d_log_xst.tag || !tag ||
	    (maxfac_hint < 0) || (default_mask & ~DLOG_PRIMASK) ||
	    (stderr_mask & ~DLOG_PRIMASK)) {
		fprintf(stderr, "d_log_open invalid parameter.\n");
		goto early_error;
	}
	/* init working area so we can use dlog_cleanout to bail out */
	memset(&mst, 0, sizeof(mst));
	mst.log_fd = -1;
	mst.log_old_fd = -1;
	/* start filling it in */
	tagblen = strlen(tag) + DLOG_TAGPAD;	/* add a bit for pid */
	newtag = calloc(1, tagblen);
	if (!newtag) {
		fprintf(stderr, "d_log_open calloc failed.\n");
		goto early_error;
	}
#ifdef DLOG_MUTEX		/* create lock */
	if (D_MUTEX_INIT(&mst.clogmux, NULL) != 0) {
		/* XXX: consider cvt to PTHREAD_MUTEX_INITIALIZER */
		fprintf(stderr, "d_log_open D_MUTEX_INIT failed.\n");
		goto early_error;
	}
#endif
	/* it is now safe to use dlog_cleanout() for error handling */

	clog_lock();		/* now locked */

	D_INIT_LIST_HEAD(&d_log_caches);

	if (flags & DLOG_FLV_LOGPID)
		snprintf(newtag, tagblen, "%s[%d", tag, getpid());
	else
		snprintf(newtag, tagblen, "%s", tag);
	mst.def_mask = default_mask;
	mst.stderr_mask = stderr_mask;
	if (logfile) {
		int log_flags = O_RDWR | O_CREAT;
		bool	merge = false;
		struct stat st;

		env = getenv(D_LOG_STDERR_IN_LOG_ENV);
		if (env != NULL && atoi(env) > 0)
			merge = true;

		if (!truncate)
			log_flags |= O_APPEND;

		mst.log_file = strdup(logfile);
		if (!mst.log_file) {
			fprintf(stderr, "strdup failed.\n");
			goto error;
		}
		mst.log_fd = open(mst.log_file, log_flags, 0666);
		/* merge stderr into log file, to aggregate and order with
		 * messages from Mercury/libfabric
		 */
		if (merge) {
			if (freopen(mst.log_file, truncate ? "w" : "a",
				    stderr) == NULL) {
				fprintf(stderr, "d_log_open: cannot open %s: %s\n",
					mst.log_file, strerror(errno));
				goto error;
			}
			/* set per-line buffering to limit jumbling */
			setlinebuf(stderr);
			mst.log_fd = fileno(stderr);
		} else {
			mst.log_fd = open(mst.log_file, log_flags, 0644);
		}

		if (mst.log_fd < 0) {
			fprintf(stderr, "d_log_open: cannot open %s: %s\n",
				mst.log_file, strerror(errno));
			goto error;
		}
		if (!truncate) {
			rc = fstat(mst.log_fd, &st);
			if (rc)
				goto error;

			mst.log_size = st.st_size;
		}
		mst.log_size_max = log_size;
	}
	mst.oflags = flags;

	/* maxfac_hint should include default fac. */
	if (clog_setnfac((maxfac_hint < 1) ? 1 : maxfac_hint) < 0) {
		fprintf(stderr, "clog_setnfac failed.\n");
		goto error;
	}
	(void) uname(&mst.uts);
	d_log_xst.nodename = mst.uts.nodename;	/* expose this */
	/* chop off the domainname */
	if ((flags & DLOG_FLV_FQDN) == 0) {
		for (cp = mst.uts.nodename; *cp && *cp != '.'; cp++)
			;
		*cp = 0;
	}
	/* cache value of isatty() to avoid extra system calls */
	mst.stdout_isatty = isatty(fileno(stdout));
	mst.stderr_isatty = isatty(fileno(stderr));
	d_log_xst.tag = newtag;
	clog_unlock();

	/* ensure buffer+log flush upon exit in case fini routine not
	 * being called
	 */
	rc = atexit(d_log_sync);
	if (rc != 0)
		fprintf(stderr, "unable to register flush of log, potential risk to miss last lines if fini method not invoked upon exit\n");

	if (buffer)
		free(buffer);
	return 0;
error:
	/*
	 * we failed.  dlog_cleanout can handle the cleanup for us.
	 */
	clog_unlock();
	dlog_cleanout();
early_error:
	if (buffer)
		free(buffer);
	if (newtag)
		free(newtag);		/* was never installed */
	return -1;
}

/*
 * d_log_close: close off an clog and release any allocated resources
 * (e.g. as part of an orderly shutdown, after all worker threads have
 * been collected). if already closed, this function is a noop.
 */
void d_log_close(void)
{
	if (!d_log_xst.tag)
		return;		/* return if already closed */

	free(d_log_xst.tag);
	d_log_xst.tag = NULL;	/* marks us as down */
	dlog_cleanout();
}

/*
 * fac_is_enabled: parse DD_SUBSYS to see if facility name that has been called
 * to be registered is present in the environment variable, if present return
 * true, false otherwise.
 * DD_SUBSYS="all" will enable all facilities
 * if DD_SUBSYS env does not exist, then treat it as "all" (enable all facs)
 */
bool d_logfac_is_enabled(const char *fac_name)
{
	char *ddsubsys_env;
	char *ddsubsys_fac;
	int len = strlen(fac_name);

	/* read env DD_SUBSYS to enable corresponding facilities */
	ddsubsys_env = getenv(DD_FAC_ENV);
	if (ddsubsys_env == NULL)
		return true; /* enable all facilities by default */

	if (strncasecmp(ddsubsys_env, DD_FAC_ALL, strlen(DD_FAC_ALL)) == 0)
		return true; /* enable all facilities with DD_SUBSYS=all */

	ddsubsys_fac = strcasestr(ddsubsys_env, fac_name);
	if (ddsubsys_fac == NULL)
		return false;

	if (ddsubsys_fac[len] != '\0' && ddsubsys_fac[len] != ',')
		return false;

	return true;
}

/*
 * d_log_namefacility: assign a name to a facility
 * return 0 on success, -1 on error (malloc problem).
 */
int d_log_namefacility(int facility, const char *aname, const char *lname)
{
	int rv;
	char *n, *nl;

	/* not open? */
	if (!d_log_xst.tag)
		return -1;

	rv = -1;	/* assume error */
	clog_lock();
	/* need to allocate facility? */
	if (facility >= d_log_xst.fac_cnt)
		if (clog_setnfac(facility + 1) < 0)
			goto done;
	n = 0;
	nl = 0;
	if (aname) {
		n = strdup(aname);
		if (!n)
			goto done;
		if (lname) {
			nl = strdup(lname);
			if (nl == NULL) {
				free(n);
				goto done;
			}
		}
	}
	if (d_log_xst.dlog_facs[facility].fac_aname &&
	    d_log_xst.dlog_facs[facility].fac_aname != default_fac0name)
		free(d_log_xst.dlog_facs[facility].fac_aname);
	if (d_log_xst.dlog_facs[facility].fac_lname)
		free(d_log_xst.dlog_facs[facility].fac_lname);
	d_log_xst.dlog_facs[facility].fac_aname = n;
	d_log_xst.dlog_facs[facility].fac_lname = nl;
	/* is facility enabled? */
	if (!d_logfac_is_enabled(aname) && !d_logfac_is_enabled(lname))
		d_log_xst.dlog_facs[facility].is_enabled = false;
	else
		d_log_xst.dlog_facs[facility].is_enabled = true;


	rv = 0;		/* now we have success */
done:
	clog_unlock();
	return rv;
}

/*
 * d_log_allocfacility: allocate a new facility with the given name.
 * return new facility number on success, -1 on error (malloc problem).
 */
int d_log_allocfacility(const char *aname, const char *lname)
{
	int newfac;
	/* not open? */
	if (!d_log_xst.tag)
		return -1;

	clog_lock();
	newfac = d_log_xst.fac_cnt;
	if (clog_setnfac(newfac + 1) < 0)
		newfac = -1;
	clog_unlock();
	if (newfac == -1 || d_log_namefacility(newfac, aname, lname) < 0)
		return -1;
	return newfac;
}

/*
 * d_log_setlogmask: set the logmask for a given facility.  if the user
 * uses a new facility, we ensure that our facility array covers it
 * (expanding as needed).  return oldmask on success, -1 on error.  cannot
 * fail if facility array was preallocated.
 */
int d_log_setlogmask(int facility, int mask)
{
	int oldmask;
	/* not open? */
	if (!d_log_xst.tag)
		return -1;
	clog_lock();
	/* need to allocate facility? */
	if (facility >= d_log_xst.fac_cnt && clog_setnfac(facility + 1) < 0) {
		oldmask = -1;	/* error */
	} else {
		/* swap it in, masking out any naughty bits */
		oldmask = d_log_xst.dlog_facs[facility].fac_mask;
		d_log_xst.dlog_facs[facility].fac_mask =
			(mask & DLOG_PRIMASK);
	}
	clog_unlock();

	return oldmask;
}

/*
 * d_log_setmasks: set the clog masks for a set of facilities to a given
 * level.   the input string should look: PREFIX1=LEVEL1,PREFIX2=LEVEL2,...
 * if the "PREFIX=" part is omitted, then the level applies to all defined
 * facilities (e.g. d_log_setmasks("WARN") sets everything to WARN).
 */
int d_log_setmasks(char *mstr, int mlen0)
{
	char *m, *current, *fac, *pri;
	int mlen, facno, clen, elen, prino, rv, tmp;
	unsigned int faclen, prilen;
	int log_flags;

	/* not open? */
	if (!d_log_xst.tag)
		return -1;
	m = mstr;
	mlen = mlen0;
	if (mlen < 0)
		mlen = strlen(mstr);
	while (mlen > 0 && (*m == ' ' || *m == '\t')) {
		/* remove leading space */
		m++;
		mlen--;
	}
	if (mlen <= 0)
		return -1;		/* nothing doing */
	facno = 0;		/* make sure it gets init'd */
	rv = 0;
	tmp = 0;
	reset_caches(false);
	while (m) {
		/* note current chunk, and advance m to the next one */
		current = m;
		for (clen = 0; clen < mlen && m[clen] != ','; clen++)
			;
		if (clen < mlen) {
			m = m + clen + 1;	/* skip the comma too */
			mlen = mlen - (clen + 1);
		} else {
			m = NULL;
			mlen = 0;
		}
		if (clen == 0)
			continue;	/* null entry, just skip it */
		for (elen = 0; elen < clen && current[elen] != '='; elen++)
			;
		if (elen < clen) {	/* has a facility prefix? */
			fac = current;
			faclen = elen;
			pri = current + elen + 1;
			prilen = clen - (elen + 1);
		} else {
			fac = NULL;	/* means we apply to all facs */
			faclen = 0;
			pri = current;
			prilen = clen;
		}
		if (m == NULL)
			/* remove trailing white space from count */
			while (prilen > 0 && (pri[prilen - 1] == '\n' ||
					      pri[prilen - 1] == ' ' ||
					      pri[prilen - 1] == '\t'))
				prilen--;
		/* parse complete! */
		/* process priority */
		prino = d_log_str2pri(pri, prilen);
		if (prino == -1) {
			log_flags = d_log_check(DLOG_ERR);

			if (log_flags)
				d_log(log_flags, "d_log_setmasks: %.*s: "
				      "unknown priority %.*s",
				      faclen, fac, prilen, pri);
			continue;
		}
		/* process facility */
		if (fac) {
			clog_lock();
			for (facno = 0; facno < d_log_xst.fac_cnt; facno++) {
				if (d_log_xst.dlog_facs[facno].fac_aname &&
				    strlen(d_log_xst.dlog_facs[facno].
					   fac_aname) == faclen
				    && strncasecmp(d_log_xst.dlog_facs[facno].
						   fac_aname, fac,
						   faclen) == 0) {
					break;
				}
				if (d_log_xst.dlog_facs[facno].fac_lname &&
				    strlen(d_log_xst.dlog_facs[facno].
					   fac_lname) == faclen
				    && strncasecmp(d_log_xst.dlog_facs[facno].
						   fac_lname, fac,
						   faclen) == 0) {
					break;
				}
			}
			clog_unlock();
			if (facno >= d_log_xst.fac_cnt) {
				/* Sometimes a user wants to allocate a facility
				 * and then reset the masks using the same
				 * envirable.   In this case, a facility may be
				 * unknown.   As such, this should not be logged
				 * as an error.  To see these messages, either
				 * use DEBUG or CLOG=DEBUG
				 */
				log_flags = d_log_check(DLOG_DBG);

				if (log_flags)
					d_log(log_flags, "d_log_setmasks: "
					      "unknown facility %.*s",
					      faclen, fac);
				rv = -1;
				continue;
			}
			/* apply only to this fac */
			tmp = d_log_setlogmask(facno, prino);
			if (rv != -1)
				rv = tmp;

		} /* end if(fac) */
		else {
			/* apply to all facilities */
			for (facno = 0; facno < d_log_xst.fac_cnt; facno++) {
				tmp = d_log_setlogmask(facno, prino);
				if (rv != -1)
					rv = tmp;

			}
		}
	}
	return rv;
}

/* d_log_getmasks: get current masks levels */
int d_log_getmasks(char *buf, int discard, int len, int unterm)
{
	char *bp, *myname;
	const char *p;
	int skipcnt, resid, total, facno;
	char store[64];		/* fac unlikely to overflow this */
	/* not open? */
	if (!d_log_xst.tag)
		return 0;

	bp = buf;
	skipcnt = discard;
	resid = len;
	total = 0;
	clog_lock();
	for (facno = 0; facno < d_log_xst.fac_cnt; facno++) {
		if (facno)
			clog_bput(&bp, &skipcnt, &resid, &total, ",");
		if (d_log_xst.dlog_facs[facno].fac_lname != NULL)
			myname = d_log_xst.dlog_facs[facno].fac_lname;
		else
			myname = d_log_xst.dlog_facs[facno].fac_aname;
		if (myname == NULL) {
			snprintf(store, sizeof(store), "%d", facno);
			clog_bput(&bp, &skipcnt, &resid, &total, store);
		} else {
			clog_bput(&bp, &skipcnt, &resid, &total, myname);
		}
		clog_bput(&bp, &skipcnt, &resid, &total, "=");
		p = clog_pristr(d_log_xst.dlog_facs[facno].fac_mask);
		store[1] = 0;
		while (*p && *p != ' ' && *p != '-') {
			store[0] = *p;
			p++;
			clog_bput(&bp, &skipcnt, &resid, &total, store);
		}
	}
	clog_unlock();
	strncpy(store, "\n", sizeof(store));
	clog_bput(&bp, &skipcnt, &resid, &total, store);
	if (unterm == 0)
		clog_bput(&bp, &skipcnt, &resid, &total, NULL);
	/* buf == NULL means probe for length ... */
	return ((buf == NULL) ? total : len - resid);
}

/* Copyright (C) 2016-2017 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

#define CLOG_MUTEX

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

#ifdef CLOG_MUTEX
#include <pthread.h>
#endif

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <crt_util/clog.h>

/* extra tag bytes to alloc for a pid */
#define CLOG_TAGPAD 16

/**
 * internal global state
 */
struct clog_state {
	/* note: tag, clog_facs, and fac_cnt are in xstate now */
	int def_mask;		/* default facility mask value */
	int stderr_mask;	/* mask above which we send to stderr  */
	char *logfile;		/* logfile name [malloced] */
	int logfd;		/* fd of the open logfile */
	int oflags;		/* open flags */
	int fac_alloc;		/* # of slots in facs[] (>=fac_cnt) */
	struct utsname uts;	/* for hostname, from uname(3) */
	int stdout_isatty;	/* non-zero if stdout is a tty */
	int stderr_isatty;	/* non-zero if stderr is a tty */
#ifdef CLOG_MUTEX
	pthread_mutex_t clogmux;	/* protect clog in threaded env */
#endif
};

/*
 * global data.  this sets crt_log_xst.tag to 0, meaning the log is not open.
 * this is global so clog_filter() in clog.h can get at it.
 */
struct crt_log_xstate crt_log_xst = { 0, 0, 0, 0 };

static struct clog_state mst;

/* default name for facility 0 */
static const char *default_fac0name = "CLOG";

#ifdef CLOG_MUTEX
#define clog_lock()		pthread_mutex_lock(&mst.clogmux)
#define clog_unlock()		pthread_mutex_unlock(&mst.clogmux)
#else
#define clog_lock()
#define clog_unlock()
#endif

static const char *clog_pristr(int);
static int clog_setnfac(int);

/* static arrays for converting between pri's and strings */
static const char * const norm[] = { "DBUG", "INFO", "NOTE", "WARN", "ERR ",
				     "CRIT", "ALRT", "EMRG"};

static const char * const dbg[] = { "D0", "D1", "D2", "D3",
				   "D4", "D5", "D6", "D7",
				   "D8", "D9", "D10", "D11",
				   "D12", "D13", "D14", "D15"};

static const char *dbg_bitfield = "D0x";
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

	pri = pri & CLOG_PRIMASK;
	s = (pri >> CLOG_PRISHIFT) & 7;
	return norm[s];
}

/**
 * clog_setnfac: set the number of facilites allocated (including default
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
	struct clog_fac *nfacs;

	/*
	 * no need to check crt_log_xst.tag to see if clog is open or not,
	 * since caller holds clog_lock already it must be ok.
	 */

	/* hmm, already done */
	if (n <= crt_log_xst.fac_cnt)
		return 0;

	/* can we expand in place? */
	if (n <= mst.fac_alloc) {
		crt_log_xst.fac_cnt = n;
		return 0;
	}
	/* must grow the array */
	try = (n < 1024) ? (n + 32) : n; /* pad a bit for small values of n */
	nfacs = calloc(1, try * sizeof(*nfacs));
	if (!nfacs)
		return -1;

	/* over the hump, setup the new array */
	lcv = 0;
	if (crt_log_xst.clog_facs && crt_log_xst.fac_cnt) {	/* copy old? */
		for (/*null */ ; lcv < crt_log_xst.fac_cnt; lcv++)
			nfacs[lcv] = crt_log_xst.clog_facs[lcv];
	}
	for (/*null */ ; lcv < try; lcv++) {	/* init the new */
		nfacs[lcv].fac_mask = mst.def_mask;
		nfacs[lcv].fac_aname =
		    (lcv == 0) ? (char *) default_fac0name : NULL;
		nfacs[lcv].fac_lname = NULL;
	}
	/* install */
	if (crt_log_xst.clog_facs)
		free(crt_log_xst.clog_facs);
	crt_log_xst.clog_facs = nfacs;
	crt_log_xst.fac_cnt = n;
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

/**
 * clog_cleanout: release previously allocated resources (e.g. from a
 * close or during a failed open).  this function assumes the clogmux
 * has been allocated (caller must ensure that this is true or we'll
 * die when attempting a clog_lock()).  we will dispose of clogmux.
 * (XXX: might want to switch over to a PTHREAD_MUTEX_INITIALIZER for
 * clogmux at some point?).
 *
 * the caller handles cleanout of crt_log_xst.tag (not us).
 */
static void clog_cleanout(void)
{
	int lcv;

	clog_lock();
	if (mst.logfile) {
		if (mst.logfd >= 0)
			close(mst.logfd);
		mst.logfd = -1;
		free(mst.logfile);
		mst.logfile = NULL;
	}
	if (crt_log_xst.clog_facs) {
		/*
		 * free malloced facility names, being careful not to free
		 * the static default_fac0name....
		 */
		for (lcv = 0; lcv < mst.fac_alloc; lcv++) {
			if (crt_log_xst.clog_facs[lcv].fac_aname &&
			    crt_log_xst.clog_facs[lcv].fac_aname !=
			    default_fac0name)
				free(crt_log_xst.clog_facs[lcv].fac_aname);
			if (crt_log_xst.clog_facs[lcv].fac_lname)
				free(crt_log_xst.clog_facs[lcv].fac_lname);
		}
		free(crt_log_xst.clog_facs);
		crt_log_xst.clog_facs = NULL;
		crt_log_xst.fac_cnt = mst.fac_alloc = 0;
	}
	clog_unlock();
#ifdef CLOG_MUTEX
	pthread_mutex_destroy(&mst.clogmux);
#endif
}

/**
 * crt_vlog: core log function, front-ended by crt_log
 * we vsnprintf the message into a holding buffer to format it.  then we
 * send it to all target output logs.  the holding buffer is set to
 * CLOG_TBSIZ, if the message is too long it will be silently truncated.
 * caller should not hold clog_lock, crt_vlog will grab it as needed.
 *
 * @param flags returned by crt_log_check
 * @param fmt the printf(3) format to use
 * @param ap the stdargs va_list to use for the printf format
 */
void crt_vlog(int flags, const char *fmt, va_list ap)
{
#define CLOG_TBSIZ    1024	/* bigger than any line should be */
	int fac, lvl;
	char b[CLOG_TBSIZ], *b_nopt1hdr;
	char facstore[16], *facstr;
	struct timeval tv;
	struct tm *tm;
	unsigned int hlen_pt1, hlen, mlen, tlen;
	/*
	 * since we ignore any potential errors in CLOG let's always re-set
	 * errno to its orginal value
	 */
	int save_errno = errno;

	if (flags == 0)
		return;

	fac = flags & CLOG_FACMASK;
	lvl = flags & CLOG_PRIMASK;

	/* Check the facility so we don't crash.   We will just log the message
	 * in this case but it really is indicative of a usage error as user
	 * didn't pass sanitized flags to this routine
	 */
	if (fac >= crt_log_xst.fac_cnt)
		fac = 0;

	/* Assumes stderr mask isn't used for debug messages */
	if (mst.stderr_mask != 0 && lvl >= mst.stderr_mask)
		flags |= CLOG_STDERR;

	/*
	 * we must log it, start computing the parts of the log we'll need.
	 */
	clog_lock();		/* lock out other threads */
	if (crt_log_xst.clog_facs[fac].fac_aname) {
		facstr = crt_log_xst.clog_facs[fac].fac_aname;
	} else {
		snprintf(facstore, sizeof(facstore), "%d", fac);
		facstr = facstore;
	}
	(void) gettimeofday(&tv, 0);
	tm = localtime(&tv.tv_sec);

	/*
	 * ok, first, put the header into b[]
	 */
	hlen = 0;
	if (mst.oflags & CLOG_FLV_YEAR)
		hlen = snprintf(b, sizeof(b), "%04d/", tm->tm_year + 1900);

	hlen += snprintf(b + hlen, sizeof(b) - hlen,
			 "%02d/%02d-%02d:%02d:%02d.%02ld %s ",
			 tm->tm_mon + 1, tm->tm_mday,
			 tm->tm_hour, tm->tm_min, tm->tm_sec,
			 (long int) tv.tv_usec / 10000, mst.uts.nodename);

	if (mst.oflags & CLOG_FLV_TAG)
		hlen += snprintf(b + hlen, sizeof(b) - hlen,
				 "%s ", crt_log_xst.tag);

	hlen_pt1 = hlen;	/* save part 1 length */
	if (hlen < sizeof(b)) {
		if (mst.oflags & CLOG_FLV_FAC)
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
		fprintf(stderr,
			"clog: header overflowed %zd byte buffer (%d)\n",
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
			b[tlen++] = '\n';
			b[tlen] = 0;
		}
	}
	b_nopt1hdr = b + hlen_pt1;
	/*
	 * clog message is now ready to be dispatched.
	 */
	/*
	 * log it to the log file
	 */
	if (mst.logfd >= 0)
		if (write(mst.logfd, b, tlen) < 0) {
			fprintf(stderr, "%s:%d, write failed %d(%s).\n",
				__func__, __LINE__, errno, strerror(errno));
			errno = save_errno;
		}

	if (mst.oflags & CLOG_FLV_STDOUT)
		flags |= CLOG_STDOUT;

	if (mst.oflags & CLOG_FLV_STDERR)
		flags |= CLOG_STDERR;

	clog_unlock();		/* drop lock here */
	/*
	 * log it to stderr and/or stdout.  skip part one of the header
	 * if the output channel is a tty
	 */
	if (flags & CLOG_STDERR) {
		if (mst.stderr_isatty)
			fprintf(stderr, "%s", b_nopt1hdr);
		else
			fprintf(stderr, "%s", b);
	}
	if (flags & CLOG_STDOUT) {
		if (mst.stderr_isatty)
			printf("%s", b_nopt1hdr);
		else
			printf("%s", b);
		fflush(stdout);
	}
	/*
	 * done!
	 */
	errno = save_errno;
}

/*
 * crt_log_str2pri: convert a priority string to an int pri value to allow
 * for more user-friendly programs.  returns -1 (an invalid pri) on error.
 * does not access clog global state.
 */
int crt_log_str2pri(const char *pstr)
{
	char ptmp[11];
	int lcv;

	/* make sure we have a valid input */
	if (strlen(pstr) > 7) {
		printf("wrong size! ");
		return -1;
	}
	strncpy(ptmp, pstr, 11);  /* because we may overwrite parts of it */
	/*
	 * handle some quirks
	 */

	/* this case allows the user to pass in a bitmask for debug levels */
	if (strncasecmp(ptmp, dbg_bitfield, 3) == 0)
		return (strtoul((ptmp + 3), NULL, 16)) << CLOG_DPRISHIFT;

	if (strcasecmp(ptmp, "ERR") == 0)
		/* has trailing space in the array */
		return CLOG_ERR;
	if (strcasecmp(ptmp, "DEBUG") == 0) /* 5 char alternative to 'DBUG' */
		return CLOG_DBG;

	/*
	 * do non-debug first, then debug
	 */
	for (lcv = 1; lcv <= 7; lcv++)
		if (strcasecmp(ptmp, norm[lcv]) == 0)
			return lcv << CLOG_PRISHIFT;
	for (lcv = 0; lcv < 16; lcv++)
		if (strcasecmp(ptmp, dbg[lcv]) == 0)
			return 1 << (CLOG_DPRISHIFT + lcv);
	/* bogus! */
	return -1;
}

/*
 * crt_log_open: open a clog (uses malloc, inits global state).  you
 * can only have one clog open at a time, but you can use multiple
 * facilities.
 *
 * if an clog is already open, then this call will fail.  if you use
 * the message buffer, you prob want it to be 1K or larger.
 *
 * return 0 on success, -1 on error.
 */
int
crt_log_open(char *tag, int maxfac_hint, int default_mask, int stderr_mask,
	      char *logfile, int flags)
{
	int	tagblen;
	char	*newtag, *cp;

	/* quick sanity check (mst.tag is non-null if already open) */
	if (crt_log_xst.tag || !tag ||
	    (maxfac_hint < 0) || (default_mask & ~CLOG_PRIMASK) ||
	    (stderr_mask & ~CLOG_PRIMASK)) {
		fprintf(stderr, "crt_log_open invalid parameter.\n");
		return -1;
	}
	/* init working area so we can use clog_cleanout to bail out */
	memset(&mst, 0, sizeof(mst));
	mst.logfd = -1;
	/* start filling it in */
	tagblen = strlen(tag) + CLOG_TAGPAD;	/* add a bit for pid */
	newtag = calloc(1, tagblen);
	if (!newtag) {
		fprintf(stderr, "crt_log_open calloc failed.\n");
		return -1;
	}
#ifdef CLOG_MUTEX		/* create lock */
	if (pthread_mutex_init(&mst.clogmux, NULL) != 0) {
		/* XXX: consider cvt to PTHREAD_MUTEX_INITIALIZER */
		free(newtag);
		fprintf(stderr, "crt_log_open pthread_mutex_init failed.\n");
		return -1;
	}
#endif
	/* it is now safe to use clog_cleanout() for error handling */

	clog_lock();		/* now locked */
	if (flags & CLOG_FLV_LOGPID)
		snprintf(newtag, tagblen, "%s[%d]", tag, getpid());
	else
		snprintf(newtag, tagblen, "%s", tag);
	mst.def_mask = default_mask;
	mst.stderr_mask = stderr_mask;
	if (logfile) {
		mst.logfile = strdup(logfile);
		if (!mst.logfile) {
			fprintf(stderr, "strdup failed.\n");
			goto error;
		}
		mst.logfd =
		    open(mst.logfile, O_RDWR | O_APPEND | O_CREAT, 0666);
		if (mst.logfd < 0) {
			fprintf(stderr, "crt_log_open: cannot open %s: %s\n",
				mst.logfile, strerror(errno));
			goto error;
		}
	}
	mst.oflags = flags;
	/* maxfac_hint should include default fac. */
	if (clog_setnfac((maxfac_hint < 1) ? 1 : maxfac_hint) < 0) {
		fprintf(stderr, "clog_setnfac failed.\n");
		goto error;
	}
	(void) uname(&mst.uts);
	crt_log_xst.nodename = mst.uts.nodename;	/* expose this */
	/* chop off the domainname */
	if ((flags & CLOG_FLV_FQDN) == 0) {
		for (cp = mst.uts.nodename; *cp && *cp != '.'; cp++)
			;
		*cp = 0;
	}
	/* cache value of isatty() to avoid extra system calls */
	mst.stdout_isatty = isatty(fileno(stdout));
	mst.stderr_isatty = isatty(fileno(stderr));
	crt_log_xst.tag = newtag;
	clog_unlock();
	return 0;
error:
	/*
	 * we failed.  clog_cleanout can handle the cleanup for us.
	 */
	free(newtag);		/* was never installed */
	clog_unlock();
	clog_cleanout();
	return -1;
}

/*
 * crt_log_close: close off an clog and release any allocated resources
 * (e.g. as part of an orderly shutdown, after all worker threads have
 * been collected). if already closed, this function is a noop.
 */
void crt_log_close(void)
{
	if (!crt_log_xst.tag)
		return;		/* return if already closed */

	free(crt_log_xst.tag);
	crt_log_xst.tag = NULL;	/* marks us as down */
	clog_cleanout();
}

/*
 * crt_log_namefacility: assign a name to a facility
 * return 0 on success, -1 on error (malloc problem).
 */
int crt_log_namefacility(int facility, const char *aname, const char *lname)
{
	int rv;
	char *n, *nl;
	/* not open? */
	if (!crt_log_xst.tag)
		return -1;

	rv = -1;	/* assume error */
	clog_lock();
	/* need to allocate facility? */
	if (facility >= crt_log_xst.fac_cnt)
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
	if (crt_log_xst.clog_facs[facility].fac_aname &&
	    crt_log_xst.clog_facs[facility].fac_aname != default_fac0name)
		free(crt_log_xst.clog_facs[facility].fac_aname);
	if (crt_log_xst.clog_facs[facility].fac_lname)
		free(crt_log_xst.clog_facs[facility].fac_lname);
	crt_log_xst.clog_facs[facility].fac_aname = n;
	crt_log_xst.clog_facs[facility].fac_lname = nl;
	rv = 0;		/* now we have success */
done:
	clog_unlock();
	return rv;
}

/*
 * crt_log_allocfacility: allocate a new facility with the given name.
 * return new facility number on success, -1 on error (malloc problem).
 */
int crt_log_allocfacility(const char *aname, const char *lname)
{
	int newfac;
	/* not open? */
	if (!crt_log_xst.tag)
		return -1;

	clog_lock();
	newfac = crt_log_xst.fac_cnt;
	if (clog_setnfac(newfac + 1) < 0)
		newfac = -1;
	clog_unlock();
	if (newfac == -1 || crt_log_namefacility(newfac, aname, lname) < 0)
		return -1;
	return newfac;
}

/*
 * crt_log_setlogmask: set the logmask for a given facility.  if the user
 * uses a new facility, we ensure that our facility array covers it
 * (expanding as needed).  return oldmask on success, -1 on error.  cannot
 * fail if facility array was preallocated.
 */
int crt_log_setlogmask(int facility, int mask)
{
	int oldmask;
	/* not open? */
	if (!crt_log_xst.tag)
		return -1;
	clog_lock();
	/* need to allocate facility? */
	if (facility >= crt_log_xst.fac_cnt && clog_setnfac(facility + 1) < 0) {
		oldmask = -1;	/* error */
	} else {
		/* swap it in, masking out any naughty bits */
		oldmask = crt_log_xst.clog_facs[facility].fac_mask;
		crt_log_xst.clog_facs[facility].fac_mask =
			(mask & CLOG_PRIMASK);
	}
	clog_unlock();

	return oldmask;
}

/*
 * crt_log_setmasks: set the clog masks for a set of facilities to a given
 * level.   the input string should look: PREFIX1=LEVEL1,PREFIX2=LEVEL2,...
 * if the "PREFIX=" part is omitted, then the level applies to all defined
 * facilities (e.g. crt_log_setmasks("WARN") sets everything to WARN).
 */
int crt_log_setmasks(char *mstr, int mlen0)
{
	char *m, *current, *fac, *pri, pbuf[8];
	int mlen, facno, clen, elen, prilen, prino, rv, tmp;
	unsigned int faclen;
	/* not open? */
	if (!crt_log_xst.tag)
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
		if (prilen > 7) { /* we know it can't be longer than this */
			prino = -1;
		} else {
			memset(pbuf, 0, sizeof(pbuf));
			strncpy(pbuf, pri, prilen);
			prino = crt_log_str2pri(pbuf);
		}
		if (prino == -1) {
			crt_log(CLOG_ERR,
			     "crt_log_setmasks: %.*s: unknown priority %.*s",
			     faclen, fac, prilen, pri);
			continue;
		}
		/* process facility */
		if (fac) {
			clog_lock();
			for (facno = 0; facno < crt_log_xst.fac_cnt; facno++) {
				if (crt_log_xst.clog_facs[facno].fac_aname &&
				    strlen(crt_log_xst.clog_facs[facno].
					   fac_aname) == faclen
				    && strncasecmp(crt_log_xst.clog_facs[facno].
						   fac_aname, fac,
						   faclen) == 0) {
					break;
				}
				if (crt_log_xst.clog_facs[facno].fac_lname &&
				    strlen(crt_log_xst.clog_facs[facno].
					   fac_lname) == faclen
				    && strncasecmp(crt_log_xst.clog_facs[facno].
						   fac_lname, fac,
						   faclen) == 0) {
					break;
				}
			}
			clog_unlock();
			if (facno >= crt_log_xst.fac_cnt) {
				/* Sometimes a user wants to allocate a facility
				 * and then reset the masks using the same
				 * envirable.   In this case, a facility may be
				 * unknown.   As such, this should not be logged
				 * as an error.  To see these messages, either
				 * use DEBUG or CLOG=DEBUG
				 */
				crt_log(CLOG_DBG,
				     "crt_log_setmasks: unknown facility %.*s",
				     faclen, fac);
				rv = -1;
				continue;
			}
			/* apply only to this fac */
			tmp = crt_log_setlogmask(facno, prino);
			if (rv != -1)
				rv = tmp;

		} /* end if(fac) */
		else {
			/* apply to all facilities */
			for (facno = 0; facno < crt_log_xst.fac_cnt; facno++) {
				tmp = crt_log_setlogmask(facno, prino);
				if (rv != -1)
					rv = tmp;

			}
		}
	}
	return rv;
}

/* crt_log_getmasks: get current masks levels */
int crt_log_getmasks(char *buf, int discard, int len, int unterm)
{
	char *bp, *myname;
	const char *p;
	int skipcnt, resid, total, facno;
	char store[64];		/* fac unlikely to overflow this */
	/* not open? */
	if (!crt_log_xst.tag)
		return 0;

	bp = buf;
	skipcnt = discard;
	resid = len;
	total = 0;
	clog_lock();
	for (facno = 0; facno < crt_log_xst.fac_cnt; facno++) {
		if (facno)
			clog_bput(&bp, &skipcnt, &resid, &total, ",");
		if (crt_log_xst.clog_facs[facno].fac_lname != NULL)
			myname = crt_log_xst.clog_facs[facno].fac_lname;
		else
			myname = crt_log_xst.clog_facs[facno].fac_aname;
		if (myname == NULL) {
			snprintf(store, sizeof(store), "%d", facno);
			clog_bput(&bp, &skipcnt, &resid, &total, store);
		} else {
			clog_bput(&bp, &skipcnt, &resid, &total, myname);
		}
		clog_bput(&bp, &skipcnt, &resid, &total, "=");
		p = clog_pristr(crt_log_xst.clog_facs[facno].fac_mask);
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

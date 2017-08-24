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

/* dlog.h  define API for message logging system */

#ifndef _DLOG_H_
#define _DLOG_H_
#include <stdarg.h>

/* clog open flavor */
#define DLOG_FLV_LOGPID	(1 << 0)	/* include pid in log tag */
#define DLOG_FLV_FQDN	(1 << 1)	/* log fully quallified domain name */
#define DLOG_FLV_FAC	(1 << 2)	/* log facility name */
#define DLOG_FLV_YEAR	(1 << 3)	/* log year */
#define DLOG_FLV_TAG	(1 << 4)	/* log tag */
#define DLOG_FLV_STDOUT	(1 << 5)	/* always log to stdout */
#define DLOG_FLV_STDERR	(1 << 6)	/* always log to stderr */

/* per-message log flag values */
#define DLOG_STDERR     0x20000000	/* always log to stderr */
#define DLOG_STDOUT     0x10000000	/* always log to stdout */

#define DLOG_PRIMASK    0x07ffff00	/* priority mask */
#define DLOG_EMERG      0x07000000	/* emergency */
#define DLOG_ALERT      0x06000000	/* alert */
#define DLOG_CRIT       0x05000000	/* critical */
#define DLOG_ERR        0x04000000	/* error */
#define DLOG_WARN       0x03000000	/* warning */
#define DLOG_NOTE       0x02000000	/* notice */
#define DLOG_INFO       0x01000000	/* info */

#define DLOG_PRISHIFT   24		/* to get non-debug level */
#define DLOG_DPRISHIFT  8		/* to get debug level */
#define DLOG_DBG        0x00ffff00	/* all debug streams */
#define DLOG_FACMASK    0x000000ff	/* facility mask */

/* dlog_fac: facility name and mask info */
struct dlog_fac {
	int fac_mask;  /* log level for this facility */
	char *fac_aname;  /* abbreviated name of this facility */
	char *fac_lname;  /* optional long name of this facility */
};

/* clog global state */
struct d_log_xstate {
	char			*tag; /* tag string */
	/* note that tag is NULL if clog is not open/inited */
	struct dlog_fac		*dlog_facs; /* array of facility */
	int			 fac_cnt; /* # of facilities */
	char			*nodename; /* pointer to our utsname */
};

#if defined(__cplusplus)
extern "C" {
#endif

extern struct d_log_xstate d_log_xst;

/**
 * d_log_check: clog a message using stdarg list without checking filtering
 *
 * \param flags [IN]		facility+level+misc flags
 * @return flags to pass to d_vlog, 0 indicates no log
 */
static inline int d_log_check(int flags)
{
	int fac = flags & DLOG_FACMASK;
	int lvl = flags & DLOG_PRIMASK;
	int msk;

	if (!d_log_xst.tag) /* Log isn't open */
		return 0;

	/* Use default facility if it is malformed */
	if (fac >= d_log_xst.fac_cnt)
		fac = 0;

	/*
	 * first, see if we can ignore the log messages because it is
	 * masked out.  if debug messages are masked out, then we just
	 * directly compare levels.  if debug messages are not masked,
	 * then we allow all non-debug messages and for debug messages we
	 * check to make sure the proper bit is on.  [apps that don't use
	 * the debug bits just log with DLOG_DBG which has them all set]
	 */
	msk = d_log_xst.dlog_facs[fac].fac_mask;
	if (lvl >= DLOG_INFO) {
		if (lvl < msk)
			return 0; /* Skip it */
	} else { /* debug clog message */
		/*
		 * note: if (msk >= DLOG_INFO), then all the mask's debug bits
		 * are zero (meaning debugging messages are masked out).  thus,
		 * for messages with the debug level we only have to do a bit
		 * test.
		 */
		if ((lvl & msk) == 0)	/* do we want this type of debug msg? */
			return 0; /* Skip it */
	}

	return lvl | fac;
}

/**
 * d_vlog: log a message using stdarg list without checking flags
 *
 * A log line cannot be larger than DLOG_TBSZ (4096), if it is larger it will be
 * (silently) truncated].
 *
 * \param flags [IN]		flags returned from d_log_check
 * \param fmt [IN]		printf-style format string
 * @param ap [IN]		stdarg list
 */
void d_vlog(int flags, const char *fmt, va_list ap);

/**
 * d_log: clog a message if type specified by flags is enabled
 *
 * A log line cannot be larger than DLOG_TBSZ (4096), if it is larger it will be
 * (silently) truncated].
 *
 * \param flags [IN]		facility+level+misc flags
 * \param fmt [IN]		printf-style format string
 * @param ap [IN]		stdarg list
 */
static inline void d_log(int flags, const char *fmt, ...)
	__attribute__ ((__format__(__printf__, 2, 3)));
static inline void d_log(int flags, const char *fmt, ...)
{
	va_list ap;

	flags = d_log_check(flags);

	if (flags == 0)
		return;

	va_start(ap, fmt);
	d_vlog(flags, fmt, ap);
	va_end(ap);
}

/**
 * d_log_allocfacility: allocate a new facility with the given name
 *
 * \param aname [IN]		the abbr. name for the facility
 *				can be null for no name
 * \param lname [IN]		the long name for the new facility
 *				can be null for no name
 *
 * \return			new facility number on success
 *				-1 on error - malloc problem.
 */
int d_log_allocfacility(const char *aname, const char *lname);

/**
 * Ensure default cart log is initialized.  This routine calls
 * d_log_open the first time based on CRT_LOG_MASK and CRT_LOG_FILE
 * environment variables.   It keeps a reference count so d_log_fini
 * must be called by all callers to release the call d_log_close()
 *
 * Without this mechanism, it is difficult to use the cart logging
 * mechanism from another library because clog doesn't allow multiple
 * log files.   It's better for things in the same process to share
 * anyway.
 */
int d_log_init(void);

/**
 * Advanced version of clog initialing function. User can specify log tag,
 * output log file, the default log mask and the mask for output errors.
 */
int d_log_init_adv(char *log_tag, char *log_file, unsigned int flavor,
		     uint64_t def_mask, uint64_t err_mask);

/**
 * Remove a reference on the default cart log.  Calls d_log_close
 * if the reference count is 0.
 */
void d_log_fini(void);

/**
 * d_log_close: close off an clog and release any allocated resources.
 */
void d_log_close(void);

/** Reapplies the masks set in CRT_LOG_MASK.   Can be called after adding new
 *  log facilities to ensure that the mask is set appropriately for the
 *  previously unknown facilities.
 */
void d_log_sync_mask(void);

/**
 * d_log_open: open a clog.
 *
 * \param tag [IN]		string we tag each line with
 * \param maxfac_hint [IN]	hint as to largest user fac value will be used
 * \param default_mask [IN]	the default mask to use for each facility
 * \param stderr_mask [IN]	messages with a mask above this go to stderr.
 *				if pass in 0, then output goes to stderr only if
 *				DLOG_STDERR is used (either in d_log_open or
 *				in d_log).
 * \param logfile [IN]		log file name, or null if no log file
 * \param flags [IN]		STDERR, LOGPID
 *
 * \return			0 on success, -1 on error.
 */
int d_log_open(char *tag, int maxfac_hint, int default_mask,
	      int stderr_mask, char *logfile, int flags);

/**
 * d_log_setlogmask: set the logmask for a given facility.
 *
 * \param facility [IN]		Facility number
 * \param mask [IN]		The new mask for the facility
 */
int d_log_setlogmask(int facility, int mask);

/**
 * d_log_setmasks: set clog masks for a set of facilities to a given level.
 * the input string should look like: PREFIX1=LEVEL1,PREFIX2=LEVEL2,...
 * where the "PREFIX" is the facility name defined with d_log_namefacility().
 *
 * \param mstr [IN]		settings to use (doesn't have to be null term'd
 *				if mstr >= 0)
 * \param mlen [IN]		length of mstr (if < 0, assume null terminated,
 *				use strlen)
 */
int d_log_setmasks(char *mstr, int mlen);

/**
 * d_log_getmasks: get current mask level as a string (not null terminated).
 * if the buffer is null, we probe for length rather than fill.
 *
 * \param buf [OUT]		the buffer to put the results in
 *				(NULL == probe for length)
 * \param discard [IN]		bytes to discard before starting to fill buf
 * \param len [IN]		length of the buffer
 * \param unterm [IN]		if non-zero do not include a trailing null
 *
 * \return bytes returned (may be trunced and non-null terminated if == len)
 */
int d_log_getmasks(char *buf, int discard, int len, int unterm);

int d_log_str2pri(const char *pstr);

#if defined(__cplusplus)
}
#endif

#endif /* _DLOG_H_ */

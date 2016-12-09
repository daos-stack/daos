/* Copyright (C) 2016 Intel Corporation
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

/* clog.h  define API for message logging system */

#ifndef _CLOG_H_
#define _CLOG_H_

/* clog open flavor */
#define CLOG_FLV_LOGPID	(1 << 0)	/* include pid in log tag */
#define CLOG_FLV_FQDN	(1 << 1)	/* log fully quallified domain name */
#define CLOG_FLV_FAC	(1 << 2)	/* log facility name */
#define CLOG_FLV_YEAR	(1 << 3)	/* log year */
#define CLOG_FLV_TAG	(1 << 4)	/* log tag */
#define CLOG_FLV_STDOUT	(1 << 5)	/* always log to stdout */
#define CLOG_FLV_STDERR	(1 << 6)	/* always log to stderr */

/* per-message log flag values */
#define CLOG_STDERR     0x20000000	/* always log to stderr */
#define CLOG_STDOUT     0x10000000	/* always log to stdout */

#define CLOG_PRIMASK    0x07ffff00	/* priority mask */
#define CLOG_EMERG      0x07000000	/* emergency */
#define CLOG_ALERT      0x06000000	/* alert */
#define CLOG_CRIT       0x05000000	/* critical */
#define CLOG_ERR        0x04000000	/* error */
#define CLOG_WARN       0x03000000	/* warning */
#define CLOG_NOTE       0x02000000	/* notice */
#define CLOG_INFO       0x01000000	/* info */

#define CLOG_PRISHIFT   24		/* to get non-debug level */
#define CLOG_DPRISHIFT  8		/* to get debug level */
#define CLOG_DBG        0x00ffff00	/* all debug streams */
#define CLOG_FACMASK    0x000000ff	/* facility mask */

/* clog_fac: facility name and mask info */
struct clog_fac {
	int fac_mask;  /* log level for this facility */
	char *fac_aname;  /* abbreviated name of this facility */
	char *fac_lname;  /* optional long name of this facility */
};

/* clog global state */
struct crt_log_xstate {
	char			*tag; /* tag string */
	/* note that tag is NULL if clog is not open/inited */
	struct clog_fac		*clog_facs; /* array of facility */
	int			 fac_cnt; /* # of facilities */
	char			*nodename; /* pointer to our utsname */
};

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * crt_log: clog a message.
 *
 * A log line cannot be larger than CLOG_TBSZ (4096), if it is larger it will be
 * (silently) truncated].
 *
 * \param flags [IN]		facility+level+misc flags
 * \param fmt [IN]		printf-style format string
 * @param ... [IN]		printf-style args
 */
void crt_log(int flags, const char *fmt, ...)
	__attribute__ ((__format__(__printf__, 2, 3)));

/**
 * crt_log_allocfacility: allocate a new facility with the given name
 *
 * \param aname [IN]		the abbr. name for the facility
 *				can be null for no name
 * \param lname [IN]		the long name for the new facility
 *				can be null for no name
 *
 * \return			new facility number on success
 *				-1 on error - malloc problem.
 */
int crt_log_allocfacility(const char *aname, const char *lname);

/**
 * Ensure default cart log is initialized.  This routine calls
 * crt_log_open the first time based on CRT_LOG_MASK and CRT_LOG_FILE
 * environment variables.   It keeps a reference count so crt_log_fini
 * must be called by all callers to release the call crt_log_close()
 *
 * Without this mechanism, it is difficult to use the cart logging
 * mechanism from another library because clog doesn't allow multiple
 * log files.   It's better for things in the same process to share
 * anyway.
 */
int crt_log_init(void);

/**
 * Advanced version of clog initialing function. User can specify log tag,
 * output log file, the default log mask and the mask for output errors.
 */
int crt_log_init_adv(char *log_tag, char *log_file, unsigned int flavor,
		     uint64_t def_mask, uint64_t err_mask);

/**
 * Remove a reference on the default cart log.  Calls crt_log_close
 * if the reference count is 0.
 */
void crt_log_fini(void);

/**
 * crt_log_close: close off an clog and release any allocated resources.
 */
void crt_log_close(void);

/**
 * crt_log_open: open a clog.
 *
 * \param tag [IN]		string we tag each line with
 * \param maxfac_hint [IN]	hint as to largest user fac value will be used
 * \param default_mask [IN]	the default mask to use for each facility
 * \param stderr_mask [IN]	messages with a mask above this go to stderr.
 *				if pass in 0, then output goes to stderr only if
 *				CLOG_STDERR is used (either in crt_log_open or
 *				in crt_log).
 * \param logfile [IN]		log file name, or null if no log file
 * \param flags [IN]		STDERR, LOGPID
 *
 * \return			0 on success, -1 on error.
 */
int crt_log_open(char *tag, int maxfac_hint, int default_mask,
	      int stderr_mask, char *logfile, int flags);

/**
 * crt_log_setlogmask: set the logmask for a given facility.
 *
 * \param facility [IN]		Facility number
 * \param mask [IN]		The new mask for the facility
 */
int crt_log_setlogmask(int facility, int mask);

/**
 * crt_log_setmasks: set clog masks for a set of facilities to a given level.
 * the input string should look like: PREFIX1=LEVEL1,PREFIX2=LEVEL2,...
 * where the "PREFIX" is the facility name defined with crt_log_namefacility().
 *
 * \param mstr [IN]		settings to use (doesn't have to be null term'd
 *				if mstr >= 0)
 * \param mlen [IN]		length of mstr (if < 0, assume null terminated,
 *				use strlen)
 */
void crt_log_setmasks(char *mstr, int mlen);

/**
 * crt_log_getmasks: get current mask level as a string (not null terminated).
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
int crt_log_getmasks(char *buf, int discard, int len, int unterm);

#if defined(__cplusplus)
}
#endif

#endif /* _CLOG_H_ */

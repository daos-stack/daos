/**
 * (C) Copyright 2015, 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/* clog.h  define API for message logging system */

#ifndef _CLOG_H_
#define _CLOG_H_

/* clog flag values */
#define CLOG_STDERR     0x80000000	/* always log to stderr */
#define CLOG_LOGPID     0x40000000	/* include pid in log tag */
#define CLOG_FQDN       0x20000000	/* log fully quallified domain name */
#define CLOG_STDOUT     0x02000000	/* always log to stdout */

#define CLOG_PRIMASK    0x007f0000	/* priority mask */
#define CLOG_EMERG      0x00700000	/* emergency */
#define CLOG_ALERT      0x00600000	/* alert */
#define CLOG_CRIT       0x00500000	/* critical */
#define CLOG_ERR        0x00400000	/* error */
#define CLOG_WARN       0x00300000	/* warning */
#define CLOG_NOTE       0x00200000	/* notice */
#define CLOG_INFO       0x00100000	/* info */
#define CLOG_PRISHIFT   20		/* to get non-debug level */
#define CLOG_DPRISHIFT  16		/* to get debug level */
#define CLOG_DBG        0x000f0000	/* all debug streams */
#define CLOG_FACMASK    0x0000ffff	/* facility mask */

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
int crt_log_allocfacility(char *aname, char *lname);

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

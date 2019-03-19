/* Copyright (C) 2016-2018 Intel Corporation
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
 */

/**
 * \file
 *
 * File containing GAH manipulation headers.
 *
 * Datatypes and client/server code for allocating and handling Global Access
 * Handles.
 */

#ifndef __IOF_GAH_H__
#define __IOF_GAH_H__

#include <inttypes.h>
#include <stdbool.h>

#include <gurt/list.h>
#include <gurt/errno.h>
#include <gurt/common.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
/**
 * Global Access Handle (GAH).
 *
 * 128 bit GAH, used to uniquely identify server handles on clients.  Allocated
 * by a single client rank and shared between all clients that access the same
 * inode/file/directory
 */
struct ios_gah {
	uint64_t revision:48;	/**< 0-based revision NO of the fid */
	uint8_t root;		/**< The rank where the GAH was allocated */
	uint8_t base;		/**< Rank who owns the first byte of the file */
	/**
	 * The version of the protocol
	 *
	 * This will be constant for the duration on the a application run
	 * but allows for differing software versions across clients and
	 * servers.
	 */
	uint8_t version;
	uint64_t fid:24;	/**< The file id of the file */
	uint64_t reserved:24;	/**< Reserved for future use */
	/** CRC bit.  Used to verify struct contents after wire transfer */
	uint8_t crc;
};
#pragma GCC diagnostic pop

/**
 * Server side datatype for tracking allocation.
 *
 * Server has a number of these, one per fid which it uses to track in-use
 * handles and create new ones.
 */
struct ios_gah_ent {
	/** User pointer.  If fid is valid then this contains a user pointer */
	void		*arg;
	d_list_t	list;
	uint64_t	revision;	/**< The latest used revision number */
	uint64_t	fid;		/**< The ID of this entity */
	bool		in_use;		/**< Is this fid currently in-use */
};

/**
 * Structure with dynamically-sized storage to keep the file metadata.
 *
 * This is used on the server only, and is used for allocating
 */
struct ios_gah_store {
	/** number of fids currently in used */
	int size;
	/** total number of fids, whether used and unused */
	int capacity;
	/** local rank */
	d_rank_t rank;
	/** storage for the actual file entries */
	struct ios_gah_ent *data;
	/** array of pointers to file entries */
	struct ios_gah_ent **ptr_array;
	/** list of available file entries */
	d_list_t free_list;
};

/**
 * Allocate new GAH store to creating GAHs
 *
 * \param[in] rank		Rank of the calling process.
 * \returns			Pointer to ios_gah_store struct.
 */
struct ios_gah_store *ios_gah_init(d_rank_t rank);

/**
 * \param[in] ios_gah_store	Global access handle data structure.
 *
 * \retval -DER_SUCCESS		success
 */
int ios_gah_destroy(struct ios_gah_store *ios_gah_store);

/**
 * Allocates a new Global Access Handle
 *
 * \param[in] ios_gah_store	Global access handle data structure.
 * \param[out] gah		On return, *gah contains a global access handle.
 * \param[in] arg		User pointer.  This an be retrieved by
 *				ios_get_get_info() later on.
 *
 * \retval -DER_SUCCESS		success
 */
int ios_gah_allocate(struct ios_gah_store *ios_gah_store,
		     struct ios_gah *gah, void *arg);

/**
 * Allocates a new Global Access Handle with custom base.
 *
 * \param[in] ios_gah_store	Global access handle data structure.
 * \param[out] gah		On return, *gah contains a global access handle.
 * \param[in] base		Rank of the process which serves the first byte
 *				of the file.
 * \param[in] arg		User pointer.  This an be retrieved by
 *				ios_get_get_info() later on.
 *
 * \retval -DER_SUCCESS		success
 */
int ios_gah_allocate_base(struct ios_gah_store *ios_gah_store,
			  struct ios_gah *gah, d_rank_t base, void *arg);


/**
 * Deallocates a global access handle.
 *
 * \param[in] ios_gah_store	Global access handle data structure.
 * \param[in,out] gah		On exit, the fid contained in GAH is marked
 *				available again
 *
 * \retval -DER_SUCCESS		success
 */
int ios_gah_deallocate(struct ios_gah_store *ios_gah_store,
		       struct ios_gah *gah);

/**
 * Retrieve opaque data structure corresponding to a given global access handle.
 *
 * \param[in] gah_store		Global access handle data structure
 * \param[in] gah		Global access handle
 * \param[out] arg		On success, *arg contains the opaque
 *				data struture associated with gah. On
 *				failure, equals NULL
 *
 * \retval -DER_SUCCESS		success
 */
int ios_gah_get_info(struct ios_gah_store *gah_store,
		     struct ios_gah *gah, void **arg);

/**
 * Validates if the crc in *gah is correct.
 *
 * \param[in] gah		On entry, *gah contains a global access handle.
 * \retval -DER_SUCCESS		success
 * \retval -DER_NO_HDL		CRC is incorrect
 * \retval -DER_INVAL		Null input
 */
int ios_gah_check_crc(struct ios_gah *gah);

/**
 * Validates if the version in *gah and the and the version of the protocol in
 * use match.
 *
 * \param[in] gah		On entry, *gah contains a global access handle
 *
 * \retval -DER_SUCCESS		version match
 * \retval -DER_MISMATCH	version mistatch
 * \retval -DER_INVAL		NULL input

 */
int ios_gah_check_version(struct ios_gah *gah);

#define GAH_PRINT_STR "Gah(%" PRIu8 ".%" PRIu32 ".%" PRIu64 ")"
#define GAH_PRINT_VAL(P) (P).root, (P).fid, (uint64_t)(P).revision

#define GAH_PRINT_FULL_STR GAH_PRINT_STR " revision: %" PRIu64 " root: %" \
	PRIu8 " base: %" PRIu8 " version: %" PRIu8 " fid: %" PRIu32

#define GAH_PRINT_FULL_VAL(P) GAH_PRINT_VAL(P), (uint64_t)(P).revision, \
		(P).root, (P).base, (P).version, (P).fid

#endif

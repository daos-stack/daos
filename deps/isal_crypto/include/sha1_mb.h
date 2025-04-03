/**********************************************************************
  Copyright(c) 2011-2016 Intel Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/

#ifndef _SHA1_MB_H_
#define _SHA1_MB_H_

/**
 *  @file sha1_mb.h
 *  @brief Multi-buffer CTX API SHA1 function prototypes and structures
 *
 * Interface for multi-buffer SHA1 functions
 *
 * <b> Multi-buffer SHA1  Entire or First-Update..Update-Last </b>
 *
 * The interface to this multi-buffer hashing code is carried out through the
 * context-level (CTX) init, submit and flush functions and the SHA1_HASH_CTX_MGR and
 * SHA1_HASH_CTX objects. Numerous SHA1_HASH_CTX objects may be instantiated by the
 * application for use with a single SHA1_HASH_CTX_MGR.
 *
 * The CTX interface functions carry out the initialization and padding of the jobs
 * entered by the user and add them to the multi-buffer manager. The lower level "scheduler"
 * layer then processes the jobs in an out-of-order manner. The scheduler layer functions
 * are internal and are not intended to be invoked directly. Jobs can be submitted
 * to a CTX as a complete buffer to be hashed, using the HASH_ENTIRE flag, or as partial
 * jobs which can be started using the HASH_FIRST flag, and later resumed or finished
 * using the HASH_UPDATE and HASH_LAST flags respectively.
 *
 * <b>Note:</b> The submit function does not require data buffers to be block sized.
 *
 * The SHA1 CTX interface functions are available for 4 architectures: SSE, AVX, AVX2 and
 * AVX512. In addition, a multibinary interface is provided, which selects the appropriate
 * architecture-specific function at runtime.
 *
 * <b>Usage:</b> The application creates a SHA1_HASH_CTX_MGR object and initializes it
 * with a call to sha1_ctx_mgr_init*() function, where henceforth "*" stands for the
 * relevant suffix for each architecture; _sse, _avx, _avx2, _avx512(or no suffix for the
 * multibinary version). The SHA1_HASH_CTX_MGR object will be used to schedule processor
 * resources, with up to 4 SHA1_HASH_CTX objects (or 8 in the AVX2 case, 16 in the AVX512)
 * being processed at a time.
 *
 * Each SHA1_HASH_CTX must be initialized before first use by the hash_ctx_init macro
 * defined in multi_buffer.h. After initialization, the application may begin computing
 * a hash by giving the SHA1_HASH_CTX to a SHA1_HASH_CTX_MGR using the submit functions
 * sha1_ctx_mgr_submit*() with the HASH_FIRST flag set. When the SHA1_HASH_CTX is
 * returned to the application (via this or a later call to sha1_ctx_mgr_submit*() or
 * sha1_ctx_mgr_flush*()), the application can then re-submit it with another call to
 * sha1_ctx_mgr_submit*(), but without the HASH_FIRST flag set.
 *
 * Ideally, on the last buffer for that hash, sha1_ctx_mgr_submit_sse is called with
 * HASH_LAST, although it is also possible to submit the hash with HASH_LAST and a zero
 * length if necessary. When a SHA1_HASH_CTX is returned after having been submitted with
 * HASH_LAST, it will contain a valid hash. The SHA1_HASH_CTX can be reused immediately
 * by submitting with HASH_FIRST.
 *
 * For example, you would submit hashes with the following flags for the following numbers
 * of buffers:
 * <ul>
 *  <li> one buffer: HASH_FIRST | HASH_LAST  (or, equivalently, HASH_ENTIRE)
 *  <li> two buffers: HASH_FIRST, HASH_LAST
 *  <li> three buffers: HASH_FIRST, HASH_UPDATE, HASH_LAST
 * etc.
 * </ul>
 *
 * The order in which SHA1_CTX objects are returned is in general different from the order
 * in which they are submitted.
 *
 * A few possible error conditions exist:
 * <ul>
 *  <li> Submitting flags other than the allowed entire/first/update/last values
 *  <li> Submitting a context that is currently being managed by a SHA1_HASH_CTX_MGR.
 *  <li> Submitting a context after HASH_LAST is used but before HASH_FIRST is set.
 * </ul>
 *
 *  These error conditions are reported by returning the SHA1_HASH_CTX immediately after
 *  a submit with its error member set to a non-zero error code (defined in
 *  multi_buffer.h). No changes are made to the SHA1_HASH_CTX_MGR in the case of an
 *  error; no processing is done for other hashes.
 *
 */

#include <stdint.h>
#include "multi_buffer.h"
#include "types.h"

#ifndef _MSC_VER
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Hash Constants and Typedefs
#define SHA1_DIGEST_NWORDS		5
#define SHA1_MAX_LANES			16
#define SHA1_X8_LANES			8
#define SHA1_MIN_LANES			4
#define SHA1_BLOCK_SIZE			64
#define SHA1_LOG2_BLOCK_SIZE		6
#define SHA1_PADLENGTHFIELD_SIZE	8
#define SHA1_INITIAL_DIGEST		\
	0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0

typedef uint32_t sha1_digest_array[SHA1_DIGEST_NWORDS][SHA1_MAX_LANES];
typedef uint32_t SHA1_WORD_T;

/** @brief Scheduler layer - Holds info describing a single SHA1 job for the multi-buffer manager */

typedef struct {
	uint8_t*  buffer;	//!< pointer to data buffer for this job
	uint32_t  len;		//!< length of buffer for this job in blocks.
	DECLARE_ALIGNED(uint32_t result_digest[SHA1_DIGEST_NWORDS],64);
	JOB_STS status;		//!< output job status
	void*   user_data;	//!< pointer for user's job-related data
} SHA1_JOB;

/** @brief Scheduler layer -  Holds arguments for submitted SHA1 job */

typedef struct {
	sha1_digest_array digest;
	uint8_t*	data_ptr[SHA1_MAX_LANES];
} SHA1_MB_ARGS_X16;

/** @brief Scheduler layer - Lane data */

typedef struct {
	SHA1_JOB *job_in_lane;
} SHA1_LANE_DATA;

/** @brief Scheduler layer - Holds state for multi-buffer SHA1 jobs */

typedef struct {
	SHA1_MB_ARGS_X16 args;
	uint32_t lens[SHA1_MAX_LANES];
	uint64_t unused_lanes; //!< each nibble is index (0...3 or 0...7 or 0...15) of unused lanes, nibble 4 or 8 is set to F as a flag
	SHA1_LANE_DATA ldata[SHA1_MAX_LANES];
	uint32_t num_lanes_inuse;
} SHA1_MB_JOB_MGR;

/** @brief Context layer - Holds state for multi-buffer SHA1 jobs */

typedef struct {
	SHA1_MB_JOB_MGR mgr;
} SHA1_HASH_CTX_MGR;

/** @brief Context layer - Holds info describing a single SHA1 job for the multi-buffer CTX manager */

typedef struct {
	SHA1_JOB       job;		// Must be at struct offset 0.
	HASH_CTX_STS   status;		//!< Context status flag
	HASH_CTX_ERROR error;		//!< Context error flag
	uint64_t       total_length;	//!< Running counter of length processed for this CTX's job
	const void*    incoming_buffer; //!< pointer to data input buffer for this CTX's job
	uint32_t       incoming_buffer_length; //!< length of buffer for this job in bytes.
	uint8_t        partial_block_buffer[SHA1_BLOCK_SIZE * 2]; //!< CTX partial blocks
	uint32_t       partial_block_buffer_length;
	void*          user_data;	//!< pointer for user to keep any job-related data
} SHA1_HASH_CTX;

/******************** multibinary function prototypes **********************/

/**
 * @brief Initialize the SHA1 multi-buffer manager structure.
 * @requires SSE4.1 or AVX or AVX2 or AVX512
 *
 * @param mgr Structure holding context level state info
 * @returns void
 */
void      sha1_ctx_mgr_init (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief  Submit a new SHA1 job to the multi-buffer manager.
 * @requires SSE4.1 or AVX or AVX2 or AVX512
 *
 * @param  mgr Structure holding context level state info
 * @param  ctx Structure holding ctx job info
 * @param  buffer Pointer to buffer to be processed
 * @param  len Length of buffer (in bytes) to be processed
 * @param  flags Input flag specifying job type (first, update, last or entire)
 * @returns NULL if no jobs complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_submit (SHA1_HASH_CTX_MGR* mgr, SHA1_HASH_CTX* ctx,
				const void* buffer, uint32_t len, HASH_CTX_FLAG flags);

/**
 * @brief Finish all submitted SHA1 jobs and return when complete.
 * @requires SSE4.1 or AVX or AVX2 or AVX512
 *
 * @param mgr	Structure holding context level state info
 * @returns NULL if no jobs to complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_flush (SHA1_HASH_CTX_MGR* mgr);


/*******************************************************************
 * Context level API function prototypes
 ******************************************************************/

/**
 * @brief Initialize the context level SHA1 multi-buffer manager structure.
 * @requires SSE4.1
 *
 * @param mgr Structure holding context level state info
 * @returns void
 */
void      sha1_ctx_mgr_init_sse (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief  Submit a new SHA1 job to the context level multi-buffer manager.
 * @requires SSE4.1
 *
 * @param  mgr Structure holding context level state info
 * @param  ctx Structure holding ctx job info
 * @param  buffer Pointer to buffer to be processed
 * @param  len Length of buffer (in bytes) to be processed
 * @param  flags Input flag specifying job type (first, update, last or entire)
 * @returns NULL if no jobs complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_submit_sse (SHA1_HASH_CTX_MGR* mgr, SHA1_HASH_CTX* ctx,
				const void* buffer, uint32_t len, HASH_CTX_FLAG flags);

/**
 * @brief Finish all submitted SHA1 jobs and return when complete.
 * @requires SSE4.1
 *
 * @param mgr	Structure holding context level state info
 * @returns NULL if no jobs to complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_flush_sse (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief Initialize the context level SHA1 multi-buffer manager structure.
 * @requires SSE4.1 and SHANI
 *
 * @param mgr Structure holding context level state info
 * @returns void
 */
void      sha1_ctx_mgr_init_sse_ni (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief  Submit a new SHA1 job to the context level multi-buffer manager.
 * @requires SSE4.1 and SHANI
 *
 * @param  mgr Structure holding context level state info
 * @param  ctx Structure holding ctx job info
 * @param  buffer Pointer to buffer to be processed
 * @param  len Length of buffer (in bytes) to be processed
 * @param  flags Input flag specifying job type (first, update, last or entire)
 * @returns NULL if no jobs complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_submit_sse_ni (SHA1_HASH_CTX_MGR* mgr, SHA1_HASH_CTX* ctx,
				const void* buffer, uint32_t len, HASH_CTX_FLAG flags);

/**
 * @brief Finish all submitted SHA1 jobs and return when complete.
 * @requires SSE4.1 and SHANI
 *
 * @param mgr	Structure holding context level state info
 * @returns NULL if no jobs to complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_flush_sse_ni (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief Initialize the SHA1 multi-buffer manager structure.
 * @requires AVX
 *
 * @param mgr Structure holding context level state info
 * @returns void
 */
void      sha1_ctx_mgr_init_avx (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief  Submit a new SHA1 job to the multi-buffer manager.
 * @requires AVX
 *
 * @param  mgr Structure holding context level state info
 * @param  ctx Structure holding ctx job info
 * @param  buffer Pointer to buffer to be processed
 * @param  len Length of buffer (in bytes) to be processed
 * @param  flags Input flag specifying job type (first, update, last or entire)
 * @returns NULL if no jobs complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_submit_avx (SHA1_HASH_CTX_MGR* mgr, SHA1_HASH_CTX* ctx,
				const void* buffer, uint32_t len, HASH_CTX_FLAG flags);

/**
 * @brief Finish all submitted SHA1 jobs and return when complete.
 * @requires AVX
 *
 * @param mgr	Structure holding context level state info
 * @returns NULL if no jobs to complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_flush_avx (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief Initialize the SHA1 multi-buffer manager structure.
 * @requires AVX2
 *
 * @param mgr	Structure holding context level state info
 * @returns void
 */
void      sha1_ctx_mgr_init_avx2 (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief  Submit a new SHA1 job to the multi-buffer manager.
 * @requires AVX2
 *
 * @param  mgr Structure holding context level state info
 * @param  ctx Structure holding ctx job info
 * @param  buffer Pointer to buffer to be processed
 * @param  len Length of buffer (in bytes) to be processed
 * @param  flags Input flag specifying job type (first, update, last or entire)
 * @returns NULL if no jobs complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_submit_avx2 (SHA1_HASH_CTX_MGR* mgr, SHA1_HASH_CTX* ctx,
				const void* buffer, uint32_t len, HASH_CTX_FLAG flags);

/**
 * @brief Finish all submitted SHA1 jobs and return when complete.
 * @requires AVX2
 *
 * @param mgr	Structure holding context level state info
 * @returns NULL if no jobs to complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_flush_avx2 (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief Initialize the SHA1 multi-buffer manager structure.
 * @requires AVX512
 *
 * @param mgr	Structure holding context level state info
 * @returns void
 */
void      sha1_ctx_mgr_init_avx512 (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief  Submit a new SHA1 job to the multi-buffer manager.
 * @requires AVX512
 *
 * @param  mgr Structure holding context level state info
 * @param  ctx Structure holding ctx job info
 * @param  buffer Pointer to buffer to be processed
 * @param  len Length of buffer (in bytes) to be processed
 * @param  flags Input flag specifying job type (first, update, last or entire)
 * @returns NULL if no jobs complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_submit_avx512 (SHA1_HASH_CTX_MGR* mgr, SHA1_HASH_CTX* ctx,
				const void* buffer, uint32_t len, HASH_CTX_FLAG flags);

/**
 * @brief Finish all submitted SHA1 jobs and return when complete.
 * @requires AVX512
 *
 * @param mgr	Structure holding context level state info
 * @returns NULL if no jobs to complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_flush_avx512 (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief Initialize the SHA1 multi-buffer manager structure.
 * @requires AVX512 and SHANI
 *
 * @param mgr	Structure holding context level state info
 * @returns void
 */
void      sha1_ctx_mgr_init_avx512_ni (SHA1_HASH_CTX_MGR* mgr);

/**
 * @brief  Submit a new SHA1 job to the multi-buffer manager.
 * @requires AVX512 and SHANI
 *
 * @param  mgr Structure holding context level state info
 * @param  ctx Structure holding ctx job info
 * @param  buffer Pointer to buffer to be processed
 * @param  len Length of buffer (in bytes) to be processed
 * @param  flags Input flag specifying job type (first, update, last or entire)
 * @returns NULL if no jobs complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_submit_avx512_ni (SHA1_HASH_CTX_MGR* mgr, SHA1_HASH_CTX* ctx,
				const void* buffer, uint32_t len, HASH_CTX_FLAG flags);

/**
 * @brief Finish all submitted SHA1 jobs and return when complete.
 * @requires AVX512 and SHANI
 *
 * @param mgr	Structure holding context level state info
 * @returns NULL if no jobs to complete or pointer to jobs structure.
 */
SHA1_HASH_CTX* sha1_ctx_mgr_flush_avx512_ni (SHA1_HASH_CTX_MGR* mgr);


/*******************************************************************
 * Scheduler (internal) level out-of-order function prototypes
 ******************************************************************/

void      sha1_mb_mgr_init_sse    (SHA1_MB_JOB_MGR *state);
SHA1_JOB* sha1_mb_mgr_submit_sse  (SHA1_MB_JOB_MGR *state, SHA1_JOB* job);
SHA1_JOB* sha1_mb_mgr_flush_sse   (SHA1_MB_JOB_MGR *state);

#define   sha1_mb_mgr_init_avx    sha1_mb_mgr_init_sse
SHA1_JOB* sha1_mb_mgr_submit_avx  (SHA1_MB_JOB_MGR *state, SHA1_JOB* job);
SHA1_JOB* sha1_mb_mgr_flush_avx   (SHA1_MB_JOB_MGR *state);

void      sha1_mb_mgr_init_avx2   (SHA1_MB_JOB_MGR *state);
SHA1_JOB* sha1_mb_mgr_submit_avx2 (SHA1_MB_JOB_MGR *state, SHA1_JOB* job);
SHA1_JOB* sha1_mb_mgr_flush_avx2  (SHA1_MB_JOB_MGR *state);

void      sha1_mb_mgr_init_avx512   (SHA1_MB_JOB_MGR *state);
SHA1_JOB* sha1_mb_mgr_submit_avx512 (SHA1_MB_JOB_MGR *state, SHA1_JOB* job);
SHA1_JOB* sha1_mb_mgr_flush_avx512  (SHA1_MB_JOB_MGR *state);

void      sha1_mb_mgr_init_sse_ni    (SHA1_MB_JOB_MGR *state);
SHA1_JOB* sha1_mb_mgr_submit_sse_ni  (SHA1_MB_JOB_MGR *state, SHA1_JOB* job);
SHA1_JOB* sha1_mb_mgr_flush_sse_ni   (SHA1_MB_JOB_MGR *state);

void      sha1_mb_mgr_init_avx512_ni    (SHA1_MB_JOB_MGR *state);
SHA1_JOB* sha1_mb_mgr_submit_avx512_ni  (SHA1_MB_JOB_MGR *state, SHA1_JOB* job);
SHA1_JOB* sha1_mb_mgr_flush_avx512_ni   (SHA1_MB_JOB_MGR *state);

#ifdef __cplusplus
}
#endif

#endif // _SHA1_MB_H_

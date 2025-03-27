/**********************************************************************
  Copyright(c) 2011-2020 Intel Corporation All rights reserved.

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

#ifndef _SM3_MB_H_
#define _SM3_MB_H_


/**
 *  @file sm3_mb.h
 *  @brief Multi-buffer CTX API SM3 function prototypes and structures
 *
 *  \warning Experimental interface with only base functions available at this
 *           time.
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

#define SM3_DIGEST_NWORDS		8	/* Word in SM3 is 32-bit */
#define SM3_MAX_LANES			16
#define SM3_BLOCK_SIZE			64
#define SM3_LOG2_BLOCK_SIZE			6
#define SM3_PADLENGTHFIELD_SIZE		8
#define SM3_INITIAL_DIGEST		\
	0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600, \
	0xa96f30bc, 0x163138aa, 0xe38dee4d, 0xb0fb0e4e

typedef uint32_t sm3_digest_array[SM3_DIGEST_NWORDS][SM3_MAX_LANES];
typedef uint32_t SM3_WORD_T;

/** @brief Scheduler layer - Holds info describing a single SM3 job for the multi-buffer manager */

typedef struct {
	uint8_t *buffer;	//!< pointer to data buffer for this job
	uint64_t len;	//!< length of buffer for this job in blocks.
	DECLARE_ALIGNED(uint32_t result_digest[SM3_DIGEST_NWORDS], 64);
	JOB_STS status;	//!< output job status
	void *user_data;	//!< pointer for user's job-related data
} SM3_JOB;

/** @brief Scheduler layer -  Holds arguments for submitted SM3 job */

typedef struct {
	sm3_digest_array digest;
	uint8_t *data_ptr[SM3_MAX_LANES];
} SM3_MB_ARGS_X16;

/** @brief Scheduler layer - Lane data */

typedef struct {
	SM3_JOB *job_in_lane;
} SM3_LANE_DATA;

/** @brief Scheduler layer - Holds state for multi-buffer SM3 jobs */

typedef struct {
	SM3_MB_ARGS_X16 args;
	uint32_t lens[SM3_MAX_LANES];
	uint64_t unused_lanes;	//!< each nibble is index (0...3 or 0...7) of unused lanes, nibble 4 or 8 is set to F as a flag
	SM3_LANE_DATA ldata[SM3_MAX_LANES];
	uint32_t num_lanes_inuse;
} SM3_MB_JOB_MGR;

/** @brief Context layer - Holds state for multi-buffer SM3 jobs */

typedef struct {
	SM3_MB_JOB_MGR mgr;
} SM3_HASH_CTX_MGR;

/** @brief Context layer - Holds info describing a single SM3 job for the multi-buffer CTX manager */

typedef struct {
	SM3_JOB job;	// Must be at struct offset 0.
	HASH_CTX_STS status;	//!< Context status flag
	HASH_CTX_ERROR error;	//!< Context error flag
	uint64_t total_length;	//!< Running counter of length processed for this CTX's job
	const void *incoming_buffer;	//!< pointer to data input buffer for this CTX's job
	uint32_t incoming_buffer_length;	//!< length of buffer for this job in bytes.
	uint8_t partial_block_buffer[SM3_BLOCK_SIZE * 2];	//!< CTX partial blocks
	uint32_t partial_block_buffer_length;
	void *user_data;	//!< pointer for user to keep any job-related data
} SM3_HASH_CTX;

/******************** multibinary function prototypes **********************/

/**
* @brief Initialize the SM3 multi-buffer manager structure.
*
* @param mgr	Structure holding context level state info
* @returns void
*/
void sm3_ctx_mgr_init(SM3_HASH_CTX_MGR * mgr);

/**
* @brief  Submit a new SM3 job to the multi-buffer manager.
*
* @param  mgr Structure holding context level state info
* @param  ctx Structure holding ctx job info
* @param  buffer Pointer to buffer to be processed
* @param  len Length of buffer (in bytes) to be processed
* @param  flags Input flag specifying job type (first, update, last or entire)
* @returns NULL if no jobs complete or pointer to jobs structure.
*/
SM3_HASH_CTX *sm3_ctx_mgr_submit(SM3_HASH_CTX_MGR * mgr, SM3_HASH_CTX * ctx,
				 const void *buffer, uint32_t len,
				 HASH_CTX_FLAG flags);

/**
* @brief Finish all submitted SM3 jobs and return when complete.
*
* @param mgr	Structure holding context level state info
* @returns NULL if no jobs to complete or pointer to jobs structure.
*/
SM3_HASH_CTX *sm3_ctx_mgr_flush(SM3_HASH_CTX_MGR * mgr);

#ifdef __cplusplus
}
#endif
#endif

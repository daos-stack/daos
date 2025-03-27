/**********************************************************************
  Copyright(c) 2011-2017 Intel Corporation All rights reserved.

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

/**
 *  @file  rolling_hashx.h
 *  @brief Fingerprint functions based on rolling hash
 *
 *  rolling_hash2 - checks hash in a sliding window based on random 64-bit hash.
 */

#ifndef _ROLLING_HASHX_H_
#define _ROLLING_HASHX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 *@brief rolling hash return values
 */
enum {
	FINGERPRINT_RET_HIT = 0, //!< Fingerprint trigger hit
	FINGERPRINT_RET_MAX,     //!< Fingerprint max length reached before hit
	FINGERPRINT_RET_OTHER    //!< Fingerprint function error returned
};

#define FINGERPRINT_MAX_WINDOW 48

/**
 * @brief Context for rolling_hash2 functions
 */
struct rh_state2 {
	uint8_t history[FINGERPRINT_MAX_WINDOW];
	uint64_t table1[256];
	uint64_t table2[256];
	uint64_t hash;
	uint32_t w;
};

/**
 * @brief Initialize state object for rolling hash2
 *
 * @param state Structure holding state info on current rolling hash
 * @param w     Window width (1 <= w <= 32)
 * @returns 0 - success, -1 - failure
 */
int rolling_hash2_init(struct rh_state2 *state, uint32_t w);

/**
 * @brief Reset the hash state history
 *
 * @param state Structure holding state info on current rolling hash
 * @param init_bytes Optional window size buffer to pre-init hash
 * @returns none
 */
void rolling_hash2_reset(struct rh_state2 *state, uint8_t * init_bytes);

/**
 * @brief Run rolling hash function until trigger met or max length reached
 *
 * Checks for trigger based on a random hash in a sliding window.
 * @param state   Structure holding state info on current rolling hash
 * @param buffer  Pointer to input buffer to run windowed hash on
 * @param max_len Max length to run over input
 * @param mask    Mask bits ORed with hash before test with trigger
 * @param trigger Match value to compare with windowed hash at each input byte
 * @param offset  Offset from buffer to match, set if match found
 * @returns FINGERPRINT_RET_HIT - match found, FINGERPRINT_RET_MAX - exceeded max length
 */
int rolling_hash2_run(struct rh_state2 *state, uint8_t * buffer, uint32_t max_len,
		      uint32_t mask, uint32_t trigger, uint32_t * offset);

/**
 * @brief Generate an appropriate mask to target mean hit rate
 *
 * @param mean  Target chunk size in bytes
 * @param shift Bits to rotate result to get independent masks
 * @returns 32-bit mask value
 */
uint32_t rolling_hashx_mask_gen(long mean, int shift);

#ifdef __cplusplus
}
#endif

#endif // _ROLLING_HASHX_H_

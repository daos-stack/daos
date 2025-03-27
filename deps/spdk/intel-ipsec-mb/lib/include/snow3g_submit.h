/*******************************************************************************
  Copyright (c) 2012-2021, Intel Corporation

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
      * Neither the name of Intel Corporation nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#ifndef SNOW3G_SUBMIT_H
#define SNOW3G_SUBMIT_H

#include "intel-ipsec-mb.h"

static inline IMB_JOB *def_submit_snow3g_uea2_job(IMB_MGR *state, IMB_JOB *job)
{
        const snow3g_key_schedule_t *key = job->enc_keys;
        const uint32_t bitlen = (uint32_t) job->msg_len_to_cipher_in_bits;
        const uint32_t bitoff = (uint32_t) job->cipher_start_offset_in_bits;

        /* Use bit length API if
         * - msg length is not a multiple of bytes
         * - bit offset is not a multiple of bytes
         */
        if ((bitlen & 0x07) || (bitoff & 0x07)) {
                IMB_SNOW3G_F8_1_BUFFER_BIT(state, key, job->iv, job->src,
                                           job->dst, bitlen, bitoff);
        } else {
                const uint32_t bytelen = bitlen >> 3;
                const uint32_t byteoff = bitoff >> 3;
                const void *src = job->src + byteoff;
                void *dst = job->dst + byteoff;

                IMB_SNOW3G_F8_1_BUFFER(state, key, job->iv, src, dst, bytelen);
        }

        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

static inline IMB_JOB *def_flush_snow3g_uea2_job(IMB_MGR *state)
{
        (void) state;
        return NULL;
}

#endif /* SNOW3G_SUBMIT_H */

/*******************************************************************************
  Copyright (c) 2019-2021, Intel Corporation

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

#define SSE
#define SNOW3G_F8_1_BUFFER_BIT snow3g_f8_1_buffer_bit_sse
#define SNOW3G_F8_1_BUFFER snow3g_f8_1_buffer_sse
#define SNOW3G_F8_2_BUFFER snow3g_f8_2_buffer_sse
#define SNOW3G_F8_4_BUFFER snow3g_f8_4_buffer_sse
#define SNOW3G_F8_8_BUFFER snow3g_f8_8_buffer_sse
#define SNOW3G_F8_N_BUFFER snow3g_f8_n_buffer_sse
#define SNOW3G_F8_8_BUFFER_MULTIKEY snow3g_f8_8_buffer_multikey_sse
#define SNOW3G_F8_N_BUFFER_MULTIKEY snow3g_f8_n_buffer_multikey_sse
#define SNOW3G_F9_1_BUFFER snow3g_f9_1_buffer_sse
#define SNOW3G_INIT_KEY_SCHED snow3g_init_key_sched_sse
#define SNOW3G_KEY_SCHED_SIZE snow3g_key_sched_size_sse
#define CLEAR_SCRATCH_SIMD_REGS clear_scratch_xmms_sse

#include "include/snow3g_common.h"

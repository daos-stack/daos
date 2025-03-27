/*****************************************************************************
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
*****************************************************************************/

#ifndef XVALIDAPP_MISC_H
#define XVALIDAPP_MISC_H

/* RAX, RBX, RCX, RDX, RDI, RSI, R8-R15 */
#define GP_MEM_SIZE 14*8

#define XMM_MEM_SIZE 16*16
#define YMM_MEM_SIZE 16*32
#define ZMM_MEM_SIZE 32*64

/* Memory allocated in BSS section in misc.asm */
extern uint8_t gps[GP_MEM_SIZE];
extern uint8_t simd_regs[ZMM_MEM_SIZE];

/* Read RSP pointer */
void *rdrsp(void);

/* Functions to dump all registers into predefined memory */
void dump_gps(void);
void dump_xmms_sse(void);
void dump_xmms_avx(void);
void dump_ymms(void);
void dump_zmms(void);

/* Functions to clear all scratch SIMD registers */
void clr_scratch_xmms_sse(void);
void clr_scratch_xmms_avx(void);
void clr_scratch_ymms(void);
void clr_scratch_zmms(void);

#endif /* XVALIDAPP_MISC_H */

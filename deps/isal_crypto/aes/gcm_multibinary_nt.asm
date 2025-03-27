;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2017 Intel Corporation All rights reserved.
;
;  Redistribution and use in source and binary forms, with or without
;  modification, are permitted provided that the following conditions
;  are met:
;    * Redistributions of source code must retain the above copyright
;      notice, this list of conditions and the following disclaimer.
;    * Redistributions in binary form must reproduce the above copyright
;      notice, this list of conditions and the following disclaimer in
;      the documentation and/or other materials provided with the
;      distribution.
;    * Neither the name of Intel Corporation nor the names of its
;      contributors may be used to endorse or promote products derived
;      from this software without specific prior written permission.
;
;  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
;  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
;  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
;  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
;  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

default rel
[bits 64]

%include "reg_sizes.asm"

extern aes_gcm_enc_128_sse_nt
extern aes_gcm_enc_128_avx_gen4_nt
extern aes_gcm_enc_128_avx_gen2_nt
extern aes_gcm_enc_128_update_sse_nt
extern aes_gcm_enc_128_update_avx_gen4_nt
extern aes_gcm_enc_128_update_avx_gen2_nt

extern aes_gcm_dec_128_sse_nt
extern aes_gcm_dec_128_avx_gen4_nt
extern aes_gcm_dec_128_avx_gen2_nt
extern aes_gcm_dec_128_update_sse_nt
extern aes_gcm_dec_128_update_avx_gen4_nt
extern aes_gcm_dec_128_update_avx_gen2_nt

extern aes_gcm_enc_256_sse_nt
extern aes_gcm_enc_256_avx_gen4_nt
extern aes_gcm_enc_256_avx_gen2_nt
extern aes_gcm_enc_256_update_sse_nt
extern aes_gcm_enc_256_update_avx_gen4_nt
extern aes_gcm_enc_256_update_avx_gen2_nt

extern aes_gcm_dec_256_sse_nt
extern aes_gcm_dec_256_avx_gen4_nt
extern aes_gcm_dec_256_avx_gen2_nt
extern aes_gcm_dec_256_update_sse_nt
extern aes_gcm_dec_256_update_avx_gen4_nt
extern aes_gcm_dec_256_update_avx_gen2_nt

%if (AS_FEATURE_LEVEL) >= 10
extern aes_gcm_enc_128_update_vaes_avx512_nt
extern aes_gcm_dec_128_update_vaes_avx512_nt
extern aes_gcm_enc_128_vaes_avx512_nt
extern aes_gcm_dec_128_vaes_avx512_nt

extern aes_gcm_enc_256_update_vaes_avx512_nt
extern aes_gcm_dec_256_update_vaes_avx512_nt
extern aes_gcm_enc_256_vaes_avx512_nt
extern aes_gcm_dec_256_vaes_avx512_nt
%endif

section .text

%include "multibinary.asm"

;;;;
; instantiate aes_gcm NT interfaces enc, enc_update, dec, dec_update
;;;;
mbin_interface     aes_gcm_enc_128_nt
mbin_dispatch_init7 aes_gcm_enc_128_nt, aes_gcm_enc_128_sse_nt, aes_gcm_enc_128_sse_nt, aes_gcm_enc_128_avx_gen2_nt, aes_gcm_enc_128_avx_gen4_nt, aes_gcm_enc_128_avx_gen4_nt, aes_gcm_enc_128_vaes_avx512_nt

mbin_interface     aes_gcm_enc_128_update_nt
mbin_dispatch_init7 aes_gcm_enc_128_update_nt, aes_gcm_enc_128_update_sse_nt, aes_gcm_enc_128_update_sse_nt, aes_gcm_enc_128_update_avx_gen2_nt, aes_gcm_enc_128_update_avx_gen4_nt, aes_gcm_enc_128_update_avx_gen4_nt, aes_gcm_enc_128_update_vaes_avx512_nt

mbin_interface     aes_gcm_dec_128_nt
mbin_dispatch_init7 aes_gcm_dec_128_nt, aes_gcm_dec_128_sse_nt, aes_gcm_dec_128_sse_nt, aes_gcm_dec_128_avx_gen2_nt, aes_gcm_dec_128_avx_gen4_nt, aes_gcm_dec_128_avx_gen4_nt, aes_gcm_dec_128_vaes_avx512_nt

mbin_interface     aes_gcm_dec_128_update_nt
mbin_dispatch_init7 aes_gcm_dec_128_update_nt, aes_gcm_dec_128_update_sse_nt, aes_gcm_dec_128_update_sse_nt, aes_gcm_dec_128_update_avx_gen2_nt, aes_gcm_dec_128_update_avx_gen4_nt, aes_gcm_dec_128_update_avx_gen4_nt, aes_gcm_dec_128_update_vaes_avx512_nt

;;;;
; instantiate aesni_gcm interfaces init, enc, enc_update, enc_finalize, dec, dec_update, dec_finalize and precomp
;;;;
mbin_interface     aes_gcm_enc_256_nt
mbin_dispatch_init7 aes_gcm_enc_256_nt, aes_gcm_enc_256_sse_nt, aes_gcm_enc_256_sse_nt, aes_gcm_enc_256_avx_gen2_nt, aes_gcm_enc_256_avx_gen4_nt, aes_gcm_enc_256_avx_gen4_nt, aes_gcm_enc_256_vaes_avx512_nt

mbin_interface     aes_gcm_enc_256_update_nt
mbin_dispatch_init7 aes_gcm_enc_256_update_nt, aes_gcm_enc_256_update_sse_nt, aes_gcm_enc_256_update_sse_nt, aes_gcm_enc_256_update_avx_gen2_nt, aes_gcm_enc_256_update_avx_gen4_nt, aes_gcm_enc_256_update_avx_gen4_nt, aes_gcm_enc_256_update_vaes_avx512_nt

mbin_interface     aes_gcm_dec_256_nt
mbin_dispatch_init7 aes_gcm_dec_256_nt, aes_gcm_dec_256_sse_nt, aes_gcm_dec_256_sse_nt, aes_gcm_dec_256_avx_gen2_nt, aes_gcm_dec_256_avx_gen4_nt, aes_gcm_dec_256_avx_gen4_nt, aes_gcm_dec_256_vaes_avx512_nt

mbin_interface     aes_gcm_dec_256_update_nt
mbin_dispatch_init7 aes_gcm_dec_256_update_nt, aes_gcm_dec_256_update_sse_nt, aes_gcm_dec_256_update_sse_nt, aes_gcm_dec_256_update_avx_gen2_nt, aes_gcm_dec_256_update_avx_gen4_nt, aes_gcm_dec_256_update_avx_gen4_nt, aes_gcm_dec_256_update_vaes_avx512_nt


;;;       func				core, ver, snum
slversion aes_gcm_enc_128_nt,		00,   00,  02e1
slversion aes_gcm_dec_128_nt,		00,   00,  02e2
slversion aes_gcm_enc_128_update_nt,	00,   00,  02e3
slversion aes_gcm_dec_128_update_nt,	00,   00,  02e4
slversion aes_gcm_enc_256_nt,		00,   00,  02e5
slversion aes_gcm_dec_256_nt,		00,   00,  02e6
slversion aes_gcm_enc_256_update_nt,	00,   00,  02e7
slversion aes_gcm_dec_256_update_nt,	00,   00,  02e8

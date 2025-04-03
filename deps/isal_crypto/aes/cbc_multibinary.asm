;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2016 Intel Corporation All rights reserved.
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

%include "reg_sizes.asm"

default rel
[bits 64]

extern aes_cbc_dec_128_sse
extern aes_cbc_dec_128_avx
extern aes_cbc_dec_192_sse
extern aes_cbc_dec_192_avx
extern aes_cbc_dec_256_sse
extern aes_cbc_dec_256_avx


extern aes_cbc_enc_128_x4
extern aes_cbc_enc_128_x8
extern aes_cbc_enc_192_x4
extern aes_cbc_enc_192_x8
extern aes_cbc_enc_256_x4
extern aes_cbc_enc_256_x8

%include "multibinary.asm"

;;;;
; instantiate aesni_cbc interfaces enc and dec
;;;;
mbin_interface     aes_cbc_dec_128
mbin_dispatch_init aes_cbc_dec_128, aes_cbc_dec_128_sse, aes_cbc_dec_128_avx, aes_cbc_dec_128_avx
mbin_interface     aes_cbc_dec_192
mbin_dispatch_init aes_cbc_dec_192, aes_cbc_dec_192_sse, aes_cbc_dec_192_avx, aes_cbc_dec_192_avx
mbin_interface     aes_cbc_dec_256
mbin_dispatch_init aes_cbc_dec_256, aes_cbc_dec_256_sse, aes_cbc_dec_256_avx, aes_cbc_dec_256_avx

mbin_interface     aes_cbc_enc_128
mbin_dispatch_init aes_cbc_enc_128, aes_cbc_enc_128_x4, aes_cbc_enc_128_x8, aes_cbc_enc_128_x8
mbin_interface     aes_cbc_enc_192
mbin_dispatch_init aes_cbc_enc_192, aes_cbc_enc_192_x4, aes_cbc_enc_192_x8, aes_cbc_enc_192_x8
mbin_interface     aes_cbc_enc_256
mbin_dispatch_init aes_cbc_enc_256, aes_cbc_enc_256_x4, aes_cbc_enc_256_x8, aes_cbc_enc_256_x8



;;;       func            		core, ver, snum
slversion aes_cbc_enc_128,		00,   00,  0291
slversion aes_cbc_dec_128,		00,   00,  0292
slversion aes_cbc_enc_192,		00,   00,  0293
slversion aes_cbc_dec_192,		00,   00,  0294
slversion aes_cbc_enc_256,		00,   00,  0295
slversion aes_cbc_dec_256,		00,   00,  0296

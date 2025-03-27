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

default rel
[bits 64]

%include "reg_sizes.asm"

extern XTS_AES_256_enc_sse
extern XTS_AES_256_enc_avx

extern XTS_AES_256_enc_expanded_key_sse
extern XTS_AES_256_enc_expanded_key_avx

extern XTS_AES_256_dec_sse
extern XTS_AES_256_dec_avx

extern XTS_AES_256_dec_expanded_key_sse
extern XTS_AES_256_dec_expanded_key_avx

%if (AS_FEATURE_LEVEL) >= 10
extern XTS_AES_256_enc_vaes
extern XTS_AES_256_enc_expanded_key_vaes
extern XTS_AES_256_dec_vaes
extern XTS_AES_256_dec_expanded_key_vaes
%endif

section .text

%include "multibinary.asm"

;;;;
; instantiate XTS_AES_256_enc, XTS_AES_256_enc_expanded_key, XTS_AES_256_dec, and XTS_AES_256_dec_expanded_key
;;;;
mbin_interface     XTS_AES_256_enc
mbin_dispatch_init7 XTS_AES_256_enc, XTS_AES_256_enc_sse, XTS_AES_256_enc_sse, XTS_AES_256_enc_avx, XTS_AES_256_enc_avx, XTS_AES_256_enc_avx, XTS_AES_256_enc_vaes

mbin_interface     XTS_AES_256_enc_expanded_key
mbin_dispatch_init7 XTS_AES_256_enc_expanded_key, XTS_AES_256_enc_expanded_key_sse, XTS_AES_256_enc_expanded_key_sse, XTS_AES_256_enc_expanded_key_avx, XTS_AES_256_enc_expanded_key_avx, XTS_AES_256_enc_expanded_key_avx, XTS_AES_256_enc_expanded_key_vaes

mbin_interface     XTS_AES_256_dec
mbin_dispatch_init7 XTS_AES_256_dec, XTS_AES_256_dec_sse, XTS_AES_256_dec_sse, XTS_AES_256_dec_avx, XTS_AES_256_dec_avx, XTS_AES_256_dec_avx, XTS_AES_256_dec_vaes

mbin_interface     XTS_AES_256_dec_expanded_key
mbin_dispatch_init7 XTS_AES_256_dec_expanded_key, XTS_AES_256_dec_expanded_key_sse, XTS_AES_256_dec_expanded_key_sse, XTS_AES_256_dec_expanded_key_avx, XTS_AES_256_dec_expanded_key_avx, XTS_AES_256_dec_expanded_key_avx, XTS_AES_256_dec_expanded_key_vaes


;;;       func            		core, ver, snum
slversion XTS_AES_256_enc, 01,  04,  0076
slversion XTS_AES_256_enc_expanded_key, 01,  04,  0077
slversion XTS_AES_256_dec, 01,  04,  0078
slversion XTS_AES_256_dec_expanded_key, 01,  04,  0079

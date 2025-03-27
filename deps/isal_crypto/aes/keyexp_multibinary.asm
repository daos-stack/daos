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

extern aes_keyexp_128_sse
extern aes_keyexp_128_avx
extern aes_keyexp_128_enc_sse
extern aes_keyexp_128_enc_avx

extern aes_keyexp_192_sse
extern aes_keyexp_192_avx

extern aes_keyexp_256_sse
extern aes_keyexp_256_avx

%include "multibinary.asm"


;;;;
; instantiate aes_keyexp_128 interfaces
;;;;
mbin_interface     aes_keyexp_128
mbin_dispatch_init aes_keyexp_128, aes_keyexp_128_sse, aes_keyexp_128_avx, aes_keyexp_128_avx

mbin_interface     aes_keyexp_128_enc
mbin_dispatch_init aes_keyexp_128_enc, aes_keyexp_128_enc_sse, aes_keyexp_128_enc_avx, aes_keyexp_128_enc_avx

mbin_interface     aes_keyexp_192
mbin_dispatch_init aes_keyexp_192, aes_keyexp_192_sse, aes_keyexp_192_avx, aes_keyexp_192_avx

mbin_interface     aes_keyexp_256
mbin_dispatch_init aes_keyexp_256, aes_keyexp_256_sse, aes_keyexp_256_avx, aes_keyexp_256_avx

section .text
;;;       func            	core, ver, snum
slversion aes_keyexp_128,	00,   01,  02a1
slversion aes_keyexp_192,	00,   01,  02a2
slversion aes_keyexp_256,	00,   01,  02a3

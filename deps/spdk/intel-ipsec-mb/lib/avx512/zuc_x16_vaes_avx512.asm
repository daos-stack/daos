;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2021, Intel Corporation All rights reserved.
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

%define USE_GFNI_VAES_VPCLMUL 1
%define CIPHER_16 asm_ZucCipher_16_gfni_avx512
%define ZUC128_INIT asm_ZucInitialization_16_gfni_avx512
%define ZUC256_INIT asm_Zuc256Initialization_16_gfni_avx512
%define ZUC128_REMAINDER_16 asm_Eia3RemainderAVX512_16_VPCLMUL
%define ZUC256_REMAINDER_16 asm_Eia3_256_RemainderAVX512_16_VPCLMUL
%define ZUC_KEYGEN64B_16 asm_ZucGenKeystream64B_16_gfni_avx512
%define ZUC_KEYGEN8B_16 asm_ZucGenKeystream8B_16_gfni_avx512
%define ZUC_KEYGEN4B_16 asm_ZucGenKeystream4B_16_gfni_avx512
%define ZUC_KEYGEN_16 asm_ZucGenKeystream_16_gfni_avx512
%define ZUC_KEYGEN64B_SKIP8_16 asm_ZucGenKeystream64B_16_skip8_gfni_avx512
%define ZUC_KEYGEN8B_SKIP8_16 asm_ZucGenKeystream8B_16_skip8_gfni_avx512
%define ZUC_KEYGEN_SKIP8_16 asm_ZucGenKeystream_16_skip8_gfni_avx512
%define ZUC_ROUND64B_16 asm_Eia3Round64B_16_VPCLMUL
%define ZUC_EIA3_N64B asm_Eia3_Nx64B_AVX512_16_VPCLMUL
%include "avx512/zuc_x16_avx512.asm"

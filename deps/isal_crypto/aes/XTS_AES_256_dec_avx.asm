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
; XTS decrypt function with 256-bit AES
; input keys are not aligned
; keys are expanded in parallel with the tweak encryption
; plaintext and ciphertext are not aligned
; second key is stored in the stack as aligned to 16 Bytes
; first key is required only once, no need for storage of this key

%include "reg_sizes.asm"

default rel
%define TW              rsp     ; store 8 tweak values
%define keys    rsp + 16*8      ; store 15 expanded keys

%ifidn __OUTPUT_FORMAT__, win64
	%define _xmm    rsp + 16*23     ; store xmm6:xmm15
%endif

%ifidn __OUTPUT_FORMAT__, elf64
%define _gpr    rsp + 16*23     ; store rbx
%define VARIABLE_OFFSET 16*8 + 16*15 + 8*1     ; VARIABLE_OFFSET has to be an odd multiple of 8
%else
%define _gpr    rsp + 16*33     ; store rdi, rsi, rbx
%define VARIABLE_OFFSET 16*8 + 16*15 + 16*10 + 8*3     ; VARIABLE_OFFSET has to be an odd multiple of 8
%endif

%define GHASH_POLY 0x87

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void XTS_AES_256_dec_avx(
;               UINT8 *k2,      // key used for tweaking, 16*2 bytes
;               UINT8 *k1,      // key used for "ECB" encryption, 16*2 bytes
;               UINT8 *TW_initial,      // initial tweak value, 16 bytes
;               UINT64 N,       // sector size, in bytes
;               const UINT8 *ct,        // ciphertext sector input data
;               UINT8 *pt);     // plaintext sector output data
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; arguments for input parameters
%ifidn __OUTPUT_FORMAT__, elf64
	%xdefine ptr_key2 rdi
	%xdefine ptr_key1 rsi
	%xdefine T_val rdx
	%xdefine N_val rcx
	%xdefine ptr_plaintext r8
	%xdefine ptr_ciphertext r9
%else
	%xdefine ptr_key2 rcx
	%xdefine ptr_key1 rdx
	%xdefine T_val r8
	%xdefine N_val r9
	%xdefine ptr_plaintext r10; [rsp + VARIABLE_OFFSET + 8*5]
	%xdefine ptr_ciphertext r11; [rsp + VARIABLE_OFFSET + 8*6]
%endif

; arguments for temp parameters
%ifidn __OUTPUT_FORMAT__, elf64
	%define tmp1                    rdi
	%define target_ptr_val          rsi
	%define ghash_poly_8b           r10
	%define ghash_poly_8b_temp      r11
%else
	%define tmp1                    rcx
	%define target_ptr_val          rdx
	%define ghash_poly_8b           rdi
	%define ghash_poly_8b_temp      rsi
%endif

%define twtempl rax     ; global temp registers used for tweak computation
%define twtemph rbx


; produce the key for the next round
; raw_key is the output of vaeskeygenassist instruction
; round_key value before this key_expansion_128 macro is current round key
; round_key value after this key_expansion_128 macro is next round key
; 2 macros will be used for key generation in a flip-flopped fashion
%macro  key_expansion_256_flip  3
%define %%xraw_key      %1
%define %%xtmp  %2
%define %%xround_key    %3
	vpshufd  %%xraw_key,  %%xraw_key, 11111111b
	shufps  %%xtmp, %%xround_key, 00010000b
	vpxor    %%xround_key, %%xtmp
	shufps  %%xtmp, %%xround_key, 10001100b
	vpxor    %%xround_key, %%xtmp
	vpxor    %%xround_key,  %%xraw_key
%endmacro

%macro  key_expansion_256_flop  3
%define %%xraw_key      %1
%define %%xtmp  %2
%define %%xround_key    %3
	vpshufd  %%xraw_key,  %%xraw_key, 10101010b
	shufps  %%xtmp, %%xround_key, 00010000b
	vpxor    %%xround_key, %%xtmp
	shufps  %%xtmp, %%xround_key, 10001100b
	vpxor    %%xround_key, %%xtmp
	vpxor    %%xround_key,  %%xraw_key
%endmacro

; macro to encrypt the tweak value in parallel with key generation of both keys

%macro encrypt_T 11
%define %%xkey2         %1
%define %%xkey2_2       %2
%define %%xstate_tweak  %3
%define %%xkey1         %4
%define %%xkey1_2       %5
%define %%xraw_key      %6
%define %%xtmp          %7
%define %%xtmp2         %8
%define %%ptr_key2      %9
%define %%ptr_key1      %10
%define %%ptr_expanded_keys     %11


	vmovdqu  %%xkey2, [%%ptr_key2]
	vpxor    %%xstate_tweak, %%xkey2                         ; ARK for tweak encryption

	vmovdqu  %%xkey1, [%%ptr_key1]
	vmovdqa  [%%ptr_expanded_keys+16*14], %%xkey1

	vmovdqu  %%xkey2_2, [%%ptr_key2 + 16*1]
	vaesenc  %%xstate_tweak, %%xkey2_2                       ; round 1 for tweak encryption

	vmovdqu  %%xkey1_2, [%%ptr_key1 + 16*1]
	vaesimc  %%xtmp2, %%xkey1_2
	vmovdqa  [%%ptr_expanded_keys+16*13], %%xtmp2




	vaeskeygenassist         %%xraw_key, %%xkey2_2, 0x1      ; Generating round key 2 for key2
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist         %%xraw_key, %%xkey1_2, 0x1      ; Generating round key 2 for key1
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey1
	vaesenc                  %%xstate_tweak, %%xkey2         ; round 2 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1
	vmovdqa                  [%%ptr_expanded_keys+16*12], %%xtmp2

	vaeskeygenassist         %%xraw_key, %%xkey2, 0x1        ; Generating round key 3 for key2
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey2_2
	vaeskeygenassist         %%xraw_key, %%xkey1, 0x1        ; Generating round key 3 for key1
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey1_2
	vaesenc                  %%xstate_tweak, %%xkey2_2       ; round 3 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1_2
	vmovdqa                  [%%ptr_expanded_keys+16*11], %%xtmp2



	vaeskeygenassist         %%xraw_key, %%xkey2_2, 0x2      ; Generating round key 4 for key2
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist         %%xraw_key, %%xkey1_2, 0x2      ; Generating round key 4 for key1
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey1
	vaesenc                  %%xstate_tweak, %%xkey2         ; round 4 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1
	vmovdqa                  [%%ptr_expanded_keys+16*10], %%xtmp2

	vaeskeygenassist         %%xraw_key, %%xkey2, 0x2        ; Generating round key 5 for key2
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey2_2
	vaeskeygenassist         %%xraw_key, %%xkey1, 0x2        ; Generating round key 5 for key1
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey1_2
	vaesenc                  %%xstate_tweak, %%xkey2_2       ; round 5 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1_2
	vmovdqa                  [%%ptr_expanded_keys+16*9], %%xtmp2



	vaeskeygenassist         %%xraw_key, %%xkey2_2, 0x4      ; Generating round key 6 for key2
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist         %%xraw_key, %%xkey1_2, 0x4      ; Generating round key 6 for key1
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey1
	vaesenc                  %%xstate_tweak, %%xkey2         ; round 6 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1
	vmovdqa                  [%%ptr_expanded_keys+16*8], %%xtmp2

	vaeskeygenassist         %%xraw_key, %%xkey2, 0x4        ; Generating round key 7 for key2
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey2_2
	vaeskeygenassist         %%xraw_key, %%xkey1, 0x4        ; Generating round key 7 for key1
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey1_2
	vaesenc                  %%xstate_tweak, %%xkey2_2       ; round 7 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1_2
	vmovdqa                  [%%ptr_expanded_keys+16*7], %%xtmp2


	vaeskeygenassist         %%xraw_key, %%xkey2_2, 0x8      ; Generating round key 8 for key2
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist         %%xraw_key, %%xkey1_2, 0x8      ; Generating round key 8 for key1
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey1
	vaesenc                  %%xstate_tweak, %%xkey2         ; round 8 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1
	vmovdqa                  [%%ptr_expanded_keys+16*6], %%xtmp2

	vaeskeygenassist         %%xraw_key, %%xkey2, 0x8        ; Generating round key 9 for key2
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey2_2
	vaeskeygenassist         %%xraw_key, %%xkey1, 0x8        ; Generating round key 9 for key1
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey1_2
	vaesenc                  %%xstate_tweak, %%xkey2_2       ; round 9 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1_2
	vmovdqa                  [%%ptr_expanded_keys+16*5], %%xtmp2


	vaeskeygenassist         %%xraw_key, %%xkey2_2, 0x10     ; Generating round key 10 for key2
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist         %%xraw_key, %%xkey1_2, 0x10     ; Generating round key 10 for key1
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey1
	vaesenc                  %%xstate_tweak, %%xkey2         ; round 10 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1
	vmovdqa                  [%%ptr_expanded_keys+16*4], %%xtmp2

	vaeskeygenassist         %%xraw_key, %%xkey2, 0x10       ; Generating round key 11 for key2
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey2_2
	vaeskeygenassist         %%xraw_key, %%xkey1, 0x10       ; Generating round key 11 for key1
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey1_2
	vaesenc                  %%xstate_tweak, %%xkey2_2       ; round 11 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1_2
	vmovdqa                  [%%ptr_expanded_keys+16*3], %%xtmp2


	vaeskeygenassist         %%xraw_key, %%xkey2_2, 0x20     ; Generating round key 12 for key2
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist         %%xraw_key, %%xkey1_2, 0x20     ; Generating round key 12 for key1
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey1
	vaesenc                  %%xstate_tweak, %%xkey2         ; round 12 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1
	vmovdqa                  [%%ptr_expanded_keys+16*2], %%xtmp2

	vaeskeygenassist         %%xraw_key, %%xkey2, 0x20       ; Generating round key 13 for key2
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey2_2
	vaeskeygenassist         %%xraw_key, %%xkey1, 0x20       ; Generating round key 13 for key1
	key_expansion_256_flop  %%xraw_key, %%xtmp, %%xkey1_2
	vaesenc                  %%xstate_tweak, %%xkey2_2       ; round 13 for tweak encryption
	vaesimc                  %%xtmp2, %%xkey1_2
	vmovdqa                  [%%ptr_expanded_keys+16*1], %%xtmp2


	vaeskeygenassist         %%xraw_key, %%xkey2_2, 0x40     ; Generating round key 14 for key2
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist         %%xraw_key, %%xkey1_2, 0x40     ; Generating round key 14 for key1
	key_expansion_256_flip  %%xraw_key, %%xtmp, %%xkey1
	vaesenclast              %%xstate_tweak, %%xkey2         ; round 14 for tweak encryption
	vmovdqa                  [%%ptr_expanded_keys+16*0], %%xkey1

	vmovdqa  [TW], %%xstate_tweak    ; Store the encrypted Tweak value
%endmacro


; generate initial tweak values
; load initial plaintext values
%macro  initialize 16

%define %%ST1   %1      ; state 1
%define %%ST2   %2      ; state 2
%define %%ST3   %3      ; state 3
%define %%ST4   %4      ; state 4
%define %%ST5   %5      ; state 5
%define %%ST6   %6      ; state 6
%define %%ST7   %7      ; state 7
%define %%ST8   %8      ; state 8

%define %%TW1   %9      ; tweak 1
%define %%TW2   %10     ; tweak 2
%define %%TW3   %11     ; tweak 3
%define %%TW4   %12     ; tweak 4
%define %%TW5   %13     ; tweak 5
%define %%TW6   %14     ; tweak 6
%define %%TW7   %15     ; tweak 7

%define %%num_initial_blocks    %16


		; generate next Tweak values
		vmovdqa  %%TW1, [TW+16*0]
		mov     twtempl, [TW+8*0]
		mov     twtemph, [TW+8*1]
		vmovdqu  %%ST1, [ptr_plaintext+16*0]
%if (%%num_initial_blocks>=2)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*2], twtempl
		mov     [TW+8*3], twtemph;
		vmovdqa  %%TW2, [TW+16*1]
		vmovdqu  %%ST2, [ptr_plaintext+16*1]
%endif
%if (%%num_initial_blocks>=3)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*4], twtempl
		mov     [TW+8*5], twtemph;
		vmovdqa  %%TW3, [TW+16*2]
		vmovdqu  %%ST3, [ptr_plaintext+16*2]
%endif
%if (%%num_initial_blocks>=4)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*6], twtempl
		mov     [TW+8*7], twtemph;
		vmovdqa  %%TW4, [TW+16*3]
		vmovdqu  %%ST4, [ptr_plaintext+16*3]
%endif
%if (%%num_initial_blocks>=5)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*8], twtempl
		mov     [TW+8*9], twtemph;
		vmovdqa  %%TW5, [TW+16*4]
		vmovdqu  %%ST5, [ptr_plaintext+16*4]
%endif
%if (%%num_initial_blocks>=6)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*10], twtempl
		mov     [TW+8*11], twtemph;
		vmovdqa  %%TW6, [TW+16*5]
		vmovdqu  %%ST6, [ptr_plaintext+16*5]
%endif
%if (%%num_initial_blocks>=7)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*12], twtempl
		mov     [TW+8*13], twtemph;
		vmovdqa  %%TW7, [TW+16*6]
		vmovdqu  %%ST7, [ptr_plaintext+16*6]
%endif



%endmacro


; encrypt initial blocks of AES
; 1, 2, 3, 4, 5, 6 or 7 blocks are encrypted
; next 8 Tweak values are generated
%macro  encrypt_initial 18
%define %%ST1   %1      ; state 1
%define %%ST2   %2      ; state 2
%define %%ST3   %3      ; state 3
%define %%ST4   %4      ; state 4
%define %%ST5   %5      ; state 5
%define %%ST6   %6      ; state 6
%define %%ST7   %7      ; state 7
%define %%ST8   %8      ; state 8

%define %%TW1   %9      ; tweak 1
%define %%TW2   %10     ; tweak 2
%define %%TW3   %11     ; tweak 3
%define %%TW4   %12     ; tweak 4
%define %%TW5   %13     ; tweak 5
%define %%TW6   %14     ; tweak 6
%define %%TW7   %15     ; tweak 7
%define %%T0    %16     ; Temp register
%define %%num_blocks    %17
; %%num_blocks blocks encrypted
; %%num_blocks can be 1, 2, 3, 4, 5, 6, 7

%define %%lt128  %18     ; less than 128 bytes

	; xor Tweak value
	vpxor    %%ST1, %%TW1
%if (%%num_blocks>=2)
	vpxor    %%ST2, %%TW2
%endif
%if (%%num_blocks>=3)
	vpxor    %%ST3, %%TW3
%endif
%if (%%num_blocks>=4)
	vpxor    %%ST4, %%TW4
%endif
%if (%%num_blocks>=5)
	vpxor    %%ST5, %%TW5
%endif
%if (%%num_blocks>=6)
	vpxor    %%ST6, %%TW6
%endif
%if (%%num_blocks>=7)
	vpxor    %%ST7, %%TW7
%endif


	; ARK
	vmovdqa  %%T0, [keys]
	vpxor    %%ST1, %%T0
%if (%%num_blocks>=2)
	vpxor    %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vpxor    %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vpxor    %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vpxor    %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vpxor    %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vpxor    %%ST7, %%T0
%endif


	%if (0 == %%lt128)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
	%endif

	; round 1
	vmovdqa  %%T0, [keys + 16*1]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif
	%if (0 == %%lt128)
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*0], twtempl     ; next Tweak1 generated
		mov     [TW + 8*1], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	%endif

	; round 2
	vmovdqa  %%T0, [keys + 16*2]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif

	%if (0 == %%lt128)
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*2], twtempl ; next Tweak2 generated
	%endif

	; round 3
	vmovdqa  %%T0, [keys + 16*3]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif
	%if (0 == %%lt128)
		mov     [TW + 8*3], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
	%endif

	; round 4
	vmovdqa  %%T0, [keys + 16*4]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif

	%if (0 == %%lt128)
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*4], twtempl ; next Tweak3 generated
		mov     [TW + 8*5], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
	%endif

	; round 5
	vmovdqa  %%T0, [keys + 16*5]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif

	%if (0 == %%lt128)
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*6], twtempl ; next Tweak4 generated
		mov     [TW + 8*7], twtemph
	%endif

	; round 6
	vmovdqa  %%T0, [keys + 16*6]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif

	%if (0 == %%lt128)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*8], twtempl ; next Tweak5 generated
		mov     [TW + 8*9], twtemph
	%endif

	; round 7
	vmovdqa  %%T0, [keys + 16*7]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif

	%if (0 == %%lt128)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*10], twtempl ; next Tweak6 generated
		mov     [TW + 8*11], twtemph
	%endif
	; round 8
	vmovdqa  %%T0, [keys + 16*8]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif

	%if (0 == %%lt128)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*12], twtempl ; next Tweak7 generated
		mov     [TW + 8*13], twtemph
	%endif
	; round 9
	vmovdqa  %%T0, [keys + 16*9]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif

	%if (0 == %%lt128)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*14], twtempl ; next Tweak8 generated
		mov     [TW + 8*15], twtemph
	%endif
	; round 10
	vmovdqa  %%T0, [keys + 16*10]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif
	; round 11
	vmovdqa  %%T0, [keys + 16*11]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif

	; round 12
	vmovdqa  %%T0, [keys + 16*12]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif

	; round 13
	vmovdqa  %%T0, [keys + 16*13]
	vaesdec  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdec  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdec  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdec  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdec  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdec  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdec  %%ST7, %%T0
%endif

	; round 14
	vmovdqa  %%T0, [keys + 16*14]
	vaesdeclast      %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesdeclast      %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesdeclast      %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesdeclast      %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesdeclast      %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesdeclast      %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesdeclast      %%ST7, %%T0
%endif

	; xor Tweak values
	vpxor    %%ST1, %%TW1
%if (%%num_blocks>=2)
	vpxor    %%ST2, %%TW2
%endif
%if (%%num_blocks>=3)
	vpxor    %%ST3, %%TW3
%endif
%if (%%num_blocks>=4)
	vpxor    %%ST4, %%TW4
%endif
%if (%%num_blocks>=5)
	vpxor    %%ST5, %%TW5
%endif
%if (%%num_blocks>=6)
	vpxor    %%ST6, %%TW6
%endif
%if (%%num_blocks>=7)
	vpxor    %%ST7, %%TW7
%endif


%if (0 == %%lt128)
		; load next Tweak values
		vmovdqa  %%TW1, [TW + 16*0]
		vmovdqa  %%TW2, [TW + 16*1]
		vmovdqa  %%TW3, [TW + 16*2]
		vmovdqa  %%TW4, [TW + 16*3]
		vmovdqa  %%TW5, [TW + 16*4]
		vmovdqa  %%TW6, [TW + 16*5]
		vmovdqa  %%TW7, [TW + 16*6]

%endif

%endmacro


; Encrypt 8 blocks in parallel
; generate next 8 tweak values
%macro  encrypt_by_eight 18
%define %%ST1   %1      ; state 1
%define %%ST2   %2      ; state 2
%define %%ST3   %3      ; state 3
%define %%ST4   %4      ; state 4
%define %%ST5   %5      ; state 5
%define %%ST6   %6      ; state 6
%define %%ST7   %7      ; state 7
%define %%ST8   %8      ; state 8
%define %%TW1   %9      ; tweak 1
%define %%TW2   %10     ; tweak 2
%define %%TW3   %11     ; tweak 3
%define %%TW4   %12     ; tweak 4
%define %%TW5   %13     ; tweak 5
%define %%TW6   %14     ; tweak 6
%define %%TW7   %15     ; tweak 7
%define %%TW8   %16     ; tweak 8
%define %%T0    %17     ; Temp register
%define %%last_eight     %18

	; xor Tweak values
	vpxor    %%ST1, %%TW1
	vpxor    %%ST2, %%TW2
	vpxor    %%ST3, %%TW3
	vpxor    %%ST4, %%TW4
	vpxor    %%ST5, %%TW5
	vpxor    %%ST6, %%TW6
	vpxor    %%ST7, %%TW7
	vpxor    %%ST8, %%TW8

	; ARK
	vmovdqa %%T0, [keys]
	vpxor    %%ST1, %%T0
	vpxor    %%ST2, %%T0
	vpxor    %%ST3, %%T0
	vpxor    %%ST4, %%T0
	vpxor    %%ST5, %%T0
	vpxor    %%ST6, %%T0
	vpxor    %%ST7, %%T0
	vpxor    %%ST8, %%T0

%if (0 == %%last_eight)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
%endif
	; round 1
	vmovdqa %%T0, [keys + 16*1]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*0], twtempl
		mov     [TW + 8*1], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
%endif
	; round 2
	vmovdqa %%T0, [keys + 16*2]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp

%endif
	; round 3
	vmovdqa %%T0, [keys + 16*3]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		mov     [TW + 8*2], twtempl
		mov     [TW + 8*3], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
%endif
	; round 4
	vmovdqa %%T0, [keys + 16*4]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*4], twtempl
%endif
	; round 5
	vmovdqa %%T0, [keys + 16*5]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		mov     [TW + 8*5], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
%endif
	; round 6
	vmovdqa %%T0, [keys + 16*6]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*6], twtempl
		mov     [TW + 8*7], twtemph
%endif
	; round 7
	vmovdqa %%T0, [keys + 16*7]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
%endif
	; round 8
	vmovdqa %%T0, [keys + 16*8]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*8], twtempl
		mov     [TW + 8*9], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
%endif
	; round 9
	vmovdqa %%T0, [keys + 16*9]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
%endif
	; round 10
	vmovdqa %%T0, [keys + 16*10]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		mov     [TW + 8*10], twtempl
		mov     [TW + 8*11], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
%endif
	; round 11
	vmovdqa %%T0, [keys + 16*11]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*12], twtempl
%endif
	; round 12
	vmovdqa %%T0, [keys + 16*12]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		mov     [TW + 8*13], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
%endif
	; round 13
	vmovdqa %%T0, [keys + 16*13]
	vaesdec  %%ST1, %%T0
	vaesdec  %%ST2, %%T0
	vaesdec  %%ST3, %%T0
	vaesdec  %%ST4, %%T0
	vaesdec  %%ST5, %%T0
	vaesdec  %%ST6, %%T0
	vaesdec  %%ST7, %%T0
	vaesdec  %%ST8, %%T0
%if (0 == %%last_eight)
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
;               mov     [TW + 8*14], twtempl
;               mov     [TW + 8*15], twtemph
%endif
	; round 14
	vmovdqa %%T0, [keys + 16*14]
	vaesdeclast      %%ST1, %%T0
	vaesdeclast      %%ST2, %%T0
	vaesdeclast      %%ST3, %%T0
	vaesdeclast      %%ST4, %%T0
	vaesdeclast      %%ST5, %%T0
	vaesdeclast      %%ST6, %%T0
	vaesdeclast      %%ST7, %%T0
	vaesdeclast      %%ST8, %%T0

	; xor Tweak values
	vpxor    %%ST1, %%TW1
	vpxor    %%ST2, %%TW2
	vpxor    %%ST3, %%TW3
	vpxor    %%ST4, %%TW4
	vpxor    %%ST5, %%TW5
	vpxor    %%ST6, %%TW6
	vpxor    %%ST7, %%TW7
	vpxor    %%ST8, %%TW8

		mov     [TW + 8*14], twtempl
		mov     [TW + 8*15], twtemph
		; load next Tweak values
		vmovdqa  %%TW1, [TW + 16*0]
		vmovdqa  %%TW2, [TW + 16*1]
		vmovdqa  %%TW3, [TW + 16*2]
		vmovdqa  %%TW4, [TW + 16*3]
		vmovdqa  %%TW5, [TW + 16*4]
		vmovdqa  %%TW6, [TW + 16*5]
		vmovdqa  %%TW7, [TW + 16*6]

%endmacro


section .text

mk_global XTS_AES_256_dec_avx, function
XTS_AES_256_dec_avx:
	endbranch

	sub     rsp, VARIABLE_OFFSET

	mov     [_gpr + 8*0], rbx
%ifidn __OUTPUT_FORMAT__, win64
	mov     [_gpr + 8*1], rdi
	mov     [_gpr + 8*2], rsi

	vmovdqa  [_xmm + 16*0], xmm6
	vmovdqa  [_xmm + 16*1], xmm7
	vmovdqa  [_xmm + 16*2], xmm8
	vmovdqa  [_xmm + 16*3], xmm9
	vmovdqa  [_xmm + 16*4], xmm10
	vmovdqa  [_xmm + 16*5], xmm11
	vmovdqa  [_xmm + 16*6], xmm12
	vmovdqa  [_xmm + 16*7], xmm13
	vmovdqa  [_xmm + 16*8], xmm14
	vmovdqa  [_xmm + 16*9], xmm15
%endif

	mov     ghash_poly_8b, GHASH_POLY       ; load 0x87 to ghash_poly_8b


	vmovdqu  xmm1, [T_val]                   ; read initial Tweak value
	vpxor    xmm4, xmm4                      ; for key expansion
	encrypt_T       xmm0, xmm5, xmm1, xmm2, xmm6, xmm3, xmm4, xmm7, ptr_key2, ptr_key1, keys


%ifidn __OUTPUT_FORMAT__, win64
	mov             ptr_plaintext, [rsp + VARIABLE_OFFSET + 8*5]            ; plaintext pointer
	mov             ptr_ciphertext, [rsp + VARIABLE_OFFSET + 8*6]           ; ciphertext pointer
%endif



	mov             target_ptr_val, N_val
	and             target_ptr_val, -16             ; target_ptr_val = target_ptr_val - (target_ptr_val mod 16)
	sub             target_ptr_val, 128             ; adjust target_ptr_val because last 4 blocks will not be stitched with Tweak calculations
	jl              _less_than_128_bytes

	add             target_ptr_val, ptr_ciphertext


	mov             tmp1, N_val
	and             tmp1, (7 << 4)
	jz              _initial_num_blocks_is_0

	cmp             tmp1, (4 << 4)
	je              _initial_num_blocks_is_4



	cmp             tmp1, (6 << 4)
	je              _initial_num_blocks_is_6

	cmp             tmp1, (5 << 4)
	je              _initial_num_blocks_is_5



	cmp             tmp1, (3 << 4)
	je              _initial_num_blocks_is_3

	cmp             tmp1, (2 << 4)
	je              _initial_num_blocks_is_2

	cmp             tmp1, (1 << 4)
	je              _initial_num_blocks_is_1

_initial_num_blocks_is_7:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 7
	add     ptr_plaintext, 16*7
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 7, 0
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	vmovdqu  [ptr_ciphertext+16*4], xmm5
	vmovdqu  [ptr_ciphertext+16*5], xmm6
	vmovdqu  [ptr_ciphertext+16*6], xmm7
	add     ptr_ciphertext, 16*7

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop
_initial_num_blocks_is_6:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 6
	add     ptr_plaintext, 16*6
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 6, 0
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	vmovdqu  [ptr_ciphertext+16*4], xmm5
	vmovdqu  [ptr_ciphertext+16*5], xmm6
	add     ptr_ciphertext, 16*6

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop
_initial_num_blocks_is_5:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 5
	add     ptr_plaintext, 16*5
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 5, 0
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	vmovdqu  [ptr_ciphertext+16*4], xmm5
	add     ptr_ciphertext, 16*5

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop
_initial_num_blocks_is_4:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 4
	add     ptr_plaintext, 16*4
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 4, 0
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	add     ptr_ciphertext, 16*4

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop


_initial_num_blocks_is_3:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 3
	add     ptr_plaintext, 16*3
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 3, 0
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	add     ptr_ciphertext, 16*3

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop
_initial_num_blocks_is_2:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 2
	add     ptr_plaintext, 16*2
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 2, 0
	; store ciphertext
	vmovdqu  [ptr_ciphertext], xmm1
	vmovdqu  [ptr_ciphertext+16], xmm2
	add     ptr_ciphertext, 16*2

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop

_initial_num_blocks_is_1:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 1
	add     ptr_plaintext, 16*1
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 1, 0
	; store ciphertext
	vmovdqu  [ptr_ciphertext], xmm1
	add     ptr_ciphertext, 16

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop

_initial_num_blocks_is_0:
		mov             twtempl, [TW+8*0]
		mov             twtemph, [TW+8*1]
		vmovdqa          xmm9, [TW+16*0]

		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*2], twtempl
		mov             [TW+8*3], twtemph
		vmovdqa          xmm10, [TW+16*1]

		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*4], twtempl
		mov             [TW+8*5], twtemph
		vmovdqa          xmm11, [TW+16*2]


		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*6], twtempl
		mov             [TW+8*7], twtemph
		vmovdqa          xmm12, [TW+16*3]


		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*8], twtempl
		mov             [TW+8*9], twtemph
		vmovdqa          xmm13, [TW+16*4]

		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*10], twtempl
		mov             [TW+8*11], twtemph
		vmovdqa          xmm14, [TW+16*5]

		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*12], twtempl
		mov             [TW+8*13], twtemph
		vmovdqa          xmm15, [TW+16*6]

		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*14], twtempl
		mov             [TW+8*15], twtemph
		;vmovdqa         xmm16, [TW+16*7]

		cmp             ptr_ciphertext, target_ptr_val
		je              _last_eight
_main_loop:
	; load plaintext
	vmovdqu  xmm1, [ptr_plaintext+16*0]
	vmovdqu  xmm2, [ptr_plaintext+16*1]
	vmovdqu  xmm3, [ptr_plaintext+16*2]
	vmovdqu  xmm4, [ptr_plaintext+16*3]
	vmovdqu  xmm5, [ptr_plaintext+16*4]
	vmovdqu  xmm6, [ptr_plaintext+16*5]
	vmovdqu  xmm7, [ptr_plaintext+16*6]
	vmovdqu  xmm8, [ptr_plaintext+16*7]

	add     ptr_plaintext, 128

	encrypt_by_eight xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, [TW+16*7], xmm0, 0

	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	vmovdqu  [ptr_ciphertext+16*4], xmm5
	vmovdqu  [ptr_ciphertext+16*5], xmm6
	vmovdqu  [ptr_ciphertext+16*6], xmm7
	vmovdqu  [ptr_ciphertext+16*7], xmm8
	add     ptr_ciphertext, 128

	cmp     ptr_ciphertext, target_ptr_val
	jne     _main_loop

_last_eight:

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done_final

	; generate next Tweak value
	xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	shl     twtempl, 1
	adc     twtemph, twtemph
	cmovc   ghash_poly_8b_temp, ghash_poly_8b
	xor     twtempl, ghash_poly_8b_temp
	vmovdqa  xmm1, [TW + 16*7]
	vmovdqa  [TW + 16*0], xmm1       ; swap tweak values for cipher stealing for decrypt

	mov     [TW + 16*7], twtempl
	mov     [TW + 16*7+8], twtemph

	; load plaintext
	vmovdqu  xmm1, [ptr_plaintext+16*0]
	vmovdqu  xmm2, [ptr_plaintext+16*1]
	vmovdqu  xmm3, [ptr_plaintext+16*2]
	vmovdqu  xmm4, [ptr_plaintext+16*3]
	vmovdqu  xmm5, [ptr_plaintext+16*4]
	vmovdqu  xmm6, [ptr_plaintext+16*5]
	vmovdqu  xmm7, [ptr_plaintext+16*6]
	vmovdqu  xmm8, [ptr_plaintext+16*7]
	encrypt_by_eight xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, [TW+16*7], xmm0, 1

	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	vmovdqu  [ptr_ciphertext+16*4], xmm5
	vmovdqu  [ptr_ciphertext+16*5], xmm6
	vmovdqu  [ptr_ciphertext+16*6], xmm7
	jmp     _steal_cipher


_done_final:
	; load plaintext
	vmovdqu  xmm1, [ptr_plaintext+16*0]
	vmovdqu  xmm2, [ptr_plaintext+16*1]
	vmovdqu  xmm3, [ptr_plaintext+16*2]
	vmovdqu  xmm4, [ptr_plaintext+16*3]
	vmovdqu  xmm5, [ptr_plaintext+16*4]
	vmovdqu  xmm6, [ptr_plaintext+16*5]
	vmovdqu  xmm7, [ptr_plaintext+16*6]
	vmovdqu  xmm8, [ptr_plaintext+16*7]
	encrypt_by_eight xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, [TW+16*7], xmm0, 1

	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	vmovdqu  [ptr_ciphertext+16*4], xmm5
	vmovdqu  [ptr_ciphertext+16*5], xmm6
	vmovdqu  [ptr_ciphertext+16*6], xmm7

	jmp      _done


_steal_cipher:
	; start cipher stealing


	vmovdqa  xmm2, xmm8

	; shift xmm8 to the left by 16-N_val bytes
	lea     twtempl, [vpshufb_shf_table]
	vmovdqu  xmm0, [twtempl+N_val]
	vpshufb  xmm8, xmm0


	vmovdqu  xmm3, [ptr_plaintext + 112 + N_val]      ; state register is temporarily xmm3 to eliminate a move
	vmovdqu  [ptr_ciphertext + 112 + N_val], xmm8

	; shift xmm3 to the right by 16-N_val bytes
	lea     twtempl, [vpshufb_shf_table +16]
	sub     twtempl, N_val
	vmovdqu  xmm0, [twtempl]
	vpxor    xmm0, [mask1]
	vpshufb  xmm3, xmm0

	vpblendvb       xmm3,  xmm3, xmm2, xmm0      ;xmm0 is implicit

	; xor Tweak value
	vmovdqa  xmm8, [TW]
	vpxor    xmm8, xmm3      ; state register is xmm8, instead of a move from xmm3 to xmm8, destination register of vpxor instruction is swapped


	;encrypt last block with cipher stealing
	vpxor    xmm8, [keys]                    ; ARK
	vaesdec  xmm8, [keys + 16*1]             ; round 1
	vaesdec  xmm8, [keys + 16*2]             ; round 2
	vaesdec  xmm8, [keys + 16*3]             ; round 3
	vaesdec  xmm8, [keys + 16*4]             ; round 4
	vaesdec  xmm8, [keys + 16*5]             ; round 5
	vaesdec  xmm8, [keys + 16*6]             ; round 6
	vaesdec  xmm8, [keys + 16*7]             ; round 7
	vaesdec  xmm8, [keys + 16*8]             ; round 8
	vaesdec  xmm8, [keys + 16*9]             ; round 9
	vaesdec  xmm8, [keys + 16*10]            ; round 9
	vaesdec  xmm8, [keys + 16*11]            ; round 9
	vaesdec  xmm8, [keys + 16*12]            ; round 9
	vaesdec  xmm8, [keys + 16*13]            ; round 9
	vaesdeclast      xmm8, [keys + 16*14]    ; round 10

	; xor Tweak value
	vpxor    xmm8, [TW]

_done:
	; store last ciphertext value
	vmovdqu  [ptr_ciphertext+16*7], xmm8

_ret_:

	mov     rbx, [_gpr + 8*0]
%ifidn __OUTPUT_FORMAT__, win64
	mov     rdi, [_gpr + 8*1]
	mov     rsi, [_gpr + 8*2]


	vmovdqa  xmm6, [_xmm + 16*0]
	vmovdqa  xmm7, [_xmm + 16*1]
	vmovdqa  xmm8, [_xmm + 16*2]
	vmovdqa  xmm9, [_xmm + 16*3]
	vmovdqa  xmm10, [_xmm + 16*4]
	vmovdqa  xmm11, [_xmm + 16*5]
	vmovdqa  xmm12, [_xmm + 16*6]
	vmovdqa  xmm13, [_xmm + 16*7]
	vmovdqa  xmm14, [_xmm + 16*8]
	vmovdqa  xmm15, [_xmm + 16*9]
%endif

	add     rsp, VARIABLE_OFFSET

	ret





_less_than_128_bytes:
	cmp N_val, 16
	jb _ret_

	mov     tmp1, N_val
	and     tmp1, (7 << 4)
	cmp     tmp1, (6 << 4)
	je      _num_blocks_is_6
	cmp     tmp1, (5 << 4)
	je      _num_blocks_is_5
	cmp     tmp1, (4 << 4)
	je      _num_blocks_is_4
	cmp     tmp1, (3 << 4)
	je      _num_blocks_is_3
	cmp     tmp1, (2 << 4)
	je      _num_blocks_is_2
	cmp     tmp1, (1 << 4)
	je      _num_blocks_is_1




_num_blocks_is_7:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 7

	sub     ptr_plaintext, 16*1

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done_7

_steal_cipher_7:
	xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	shl     twtempl, 1
	adc     twtemph, twtemph
	cmovc   ghash_poly_8b_temp, ghash_poly_8b
	xor     twtempl, ghash_poly_8b_temp
	mov     [TW+8*2], twtempl
	mov     [TW+8*3], twtemph

	vmovdqa  [TW + 16*0] , xmm15
	vmovdqa  xmm15, [TW+16*1]

	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 7, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	vmovdqu  [ptr_ciphertext+16*4], xmm5
	vmovdqu  [ptr_ciphertext+16*5], xmm6

	sub     ptr_ciphertext, 16*1
	vmovdqa  xmm8, xmm7
	jmp     _steal_cipher

_done_7:
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 7, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	vmovdqu  [ptr_ciphertext+16*4], xmm5
	vmovdqu  [ptr_ciphertext+16*5], xmm6

	sub     ptr_ciphertext, 16*1
	vmovdqa  xmm8, xmm7
	jmp     _done






_num_blocks_is_6:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 6

	sub     ptr_plaintext, 16*2

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done_6

_steal_cipher_6:
	xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	shl     twtempl, 1
	adc     twtemph, twtemph
	cmovc   ghash_poly_8b_temp, ghash_poly_8b
	xor     twtempl, ghash_poly_8b_temp
	mov     [TW+8*2], twtempl
	mov     [TW+8*3], twtemph

	vmovdqa  [TW + 16*0] , xmm14
	vmovdqa  xmm14, [TW+16*1]

	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 6, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	vmovdqu  [ptr_ciphertext+16*4], xmm5

	sub     ptr_ciphertext, 16*2
	vmovdqa  xmm8, xmm6
	jmp     _steal_cipher

_done_6:
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 6, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4
	vmovdqu  [ptr_ciphertext+16*4], xmm5

	sub     ptr_ciphertext, 16*2
	vmovdqa  xmm8, xmm6
	jmp     _done





_num_blocks_is_5:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 5

	sub     ptr_plaintext, 16*3

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done_5

_steal_cipher_5:
	xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	shl     twtempl, 1
	adc     twtemph, twtemph
	cmovc   ghash_poly_8b_temp, ghash_poly_8b
	xor     twtempl, ghash_poly_8b_temp
	mov     [TW+8*2], twtempl
	mov     [TW+8*3], twtemph

	vmovdqa  [TW + 16*0] , xmm13
	vmovdqa  xmm13, [TW+16*1]

	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 5, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4

	sub     ptr_ciphertext, 16*3
	vmovdqa  xmm8, xmm5
	jmp     _steal_cipher

_done_5:
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 5, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3
	vmovdqu  [ptr_ciphertext+16*3], xmm4

	sub     ptr_ciphertext, 16*3
	vmovdqa  xmm8, xmm5
	jmp     _done





_num_blocks_is_4:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 4

	sub     ptr_plaintext, 16*4

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done_4

_steal_cipher_4:
	xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	shl     twtempl, 1
	adc     twtemph, twtemph
	cmovc   ghash_poly_8b_temp, ghash_poly_8b
	xor     twtempl, ghash_poly_8b_temp
	mov     [TW+8*2], twtempl
	mov     [TW+8*3], twtemph

	vmovdqa  [TW + 16*0] , xmm12
	vmovdqa  xmm12, [TW+16*1]

	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 4, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3

	sub     ptr_ciphertext, 16*4
	vmovdqa  xmm8, xmm4
	jmp     _steal_cipher

_done_4:
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 4, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2
	vmovdqu  [ptr_ciphertext+16*2], xmm3

	sub     ptr_ciphertext, 16*4
	vmovdqa  xmm8, xmm4
	jmp     _done




_num_blocks_is_3:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 3

	sub     ptr_plaintext, 16*5

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done_3

_steal_cipher_3:
	xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	shl     twtempl, 1
	adc     twtemph, twtemph
	cmovc   ghash_poly_8b_temp, ghash_poly_8b
	xor     twtempl, ghash_poly_8b_temp
	mov     [TW+8*2], twtempl
	mov     [TW+8*3], twtemph

	vmovdqa  [TW + 16*0] , xmm11
	vmovdqa  xmm11, [TW+16*1]

	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 3, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2

	sub     ptr_ciphertext, 16*5
	vmovdqa  xmm8, xmm3
	jmp     _steal_cipher

_done_3:
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 3, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext+16*0], xmm1
	vmovdqu  [ptr_ciphertext+16*1], xmm2

	sub     ptr_ciphertext, 16*5
	vmovdqa  xmm8, xmm3
	jmp     _done






_num_blocks_is_2:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 2

	sub     ptr_plaintext, 16*6

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done_2

_steal_cipher_2:
	xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	shl     twtempl, 1
	adc     twtemph, twtemph
	cmovc   ghash_poly_8b_temp, ghash_poly_8b
	xor     twtempl, ghash_poly_8b_temp
	mov     [TW+8*2], twtempl
	mov     [TW+8*3], twtemph

	vmovdqa  [TW + 16*0] , xmm10
	vmovdqa  xmm10, [TW+16*1]

	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 2, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext], xmm1

	sub     ptr_ciphertext, 16*6
	vmovdqa  xmm8, xmm2
	jmp     _steal_cipher

_done_2:
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 2, 1
	; store ciphertext
	vmovdqu  [ptr_ciphertext], xmm1

	sub     ptr_ciphertext, 16*6
	vmovdqa  xmm8, xmm2
	jmp     _done













_num_blocks_is_1:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 1

	sub     ptr_plaintext, 16*7

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done_1

_steal_cipher_1:
	xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	shl     twtempl, 1
	adc     twtemph, twtemph
	cmovc   ghash_poly_8b_temp, ghash_poly_8b
	xor     twtempl, ghash_poly_8b_temp
	mov     [TW+8*2], twtempl
	mov     [TW+8*3], twtemph

	vmovdqa  [TW + 16*0] , xmm9
	vmovdqa  xmm9, [TW+16*1]

	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 1, 1
	; store ciphertext

	sub     ptr_ciphertext, 16*7
	vmovdqa  xmm8, xmm1
	jmp     _steal_cipher

_done_1:
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 1, 1
	; store ciphertext

	sub     ptr_ciphertext, 16*7
	vmovdqa  xmm8, xmm1
	jmp     _done

section .data
align 16

vpshufb_shf_table:
; use these values for shift constants for the vpshufb instruction
; different alignments result in values as shown:
;       dq 0x8887868584838281, 0x008f8e8d8c8b8a89 ; shl 15 (16-1) / shr1
;       dq 0x8988878685848382, 0x01008f8e8d8c8b8a ; shl 14 (16-3) / shr2
;       dq 0x8a89888786858483, 0x0201008f8e8d8c8b ; shl 13 (16-4) / shr3
;       dq 0x8b8a898887868584, 0x030201008f8e8d8c ; shl 12 (16-4) / shr4
;       dq 0x8c8b8a8988878685, 0x04030201008f8e8d ; shl 11 (16-5) / shr5
;       dq 0x8d8c8b8a89888786, 0x0504030201008f8e ; shl 10 (16-6) / shr6
;       dq 0x8e8d8c8b8a898887, 0x060504030201008f ; shl 9  (16-7) / shr7
;       dq 0x8f8e8d8c8b8a8988, 0x0706050403020100 ; shl 8  (16-8) / shr8
;       dq 0x008f8e8d8c8b8a89, 0x0807060504030201 ; shl 7  (16-9) / shr9
;       dq 0x01008f8e8d8c8b8a, 0x0908070605040302 ; shl 6  (16-10) / shr10
;       dq 0x0201008f8e8d8c8b, 0x0a09080706050403 ; shl 5  (16-11) / shr11
;       dq 0x030201008f8e8d8c, 0x0b0a090807060504 ; shl 4  (16-12) / shr12
;       dq 0x04030201008f8e8d, 0x0c0b0a0908070605 ; shl 3  (16-13) / shr13
;       dq 0x0504030201008f8e, 0x0d0c0b0a09080706 ; shl 2  (16-14) / shr14
;       dq 0x060504030201008f, 0x0e0d0c0b0a090807 ; shl 1  (16-15) / shr15
dq 0x8786858483828100, 0x8f8e8d8c8b8a8988
dq 0x0706050403020100, 0x000e0d0c0b0a0908

mask1:
dq 0x8080808080808080, 0x8080808080808080

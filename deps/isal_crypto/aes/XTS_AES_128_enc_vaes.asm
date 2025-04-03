;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2020 Intel Corporation All rights reserved.
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
; XTS encrypt function with 128-bit AES
; input keys are not aligned
; keys are expanded in parallel with the tweak encryption
; plaintext and ciphertext are not aligned
; second key is stored in the stack as aligned to 16 Bytes
; first key is required only once, no need for storage of this key

%include "reg_sizes.asm"

%if (AS_FEATURE_LEVEL) >= 10

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
;void XTS_AES_128_enc_vavx(
;               UINT8 *k2,      // key used for tweaking, 16*2 bytes
;               UINT8 *k1,      // key used for "ECB" encryption, 16*2 bytes
;               UINT8 *TW_initial,      // initial tweak value, 16 bytes
;               UINT64 N,       // sector size, in bytes
;               const UINT8 *pt,        // plaintext sector input data
;               UINT8 *ct);     // ciphertext sector output data
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
	%define ghash_poly_8b           r10
	%define ghash_poly_8b_temp      r11
%else
	%define tmp1                    rcx
	%define ghash_poly_8b           rdi
	%define ghash_poly_8b_temp      rsi
%endif

%define twtempl rax     ; global temp registers used for tweak computation
%define twtemph rbx
%define zpoly   zmm25


; produce the key for the next round
; raw_key is the output of vaeskeygenassist instruction
; round_key value before this key_expansion_128 macro is current round key
; round_key value after this key_expansion_128 macro is next round key
%macro	key_expansion_128	3
%define	%%xraw_key	%1
%define	%%xtmp	%2
%define	%%xround_key	%3
	vpshufd	%%xraw_key,  %%xraw_key, 11111111b
	shufps	%%xtmp, %%xround_key, 00010000b
	vpxor	%%xround_key, %%xtmp
	shufps	%%xtmp, %%xround_key, 10001100b
	vpxor	%%xround_key, %%xtmp
	vpxor	%%xround_key,  %%xraw_key
%endmacro



; macro to encrypt the tweak value in parallel with key generation of both keys

%macro encrypt_T 8
%define	%%xkey2	%1
%define	%%xstate_tweak	%2
%define	%%xkey1	%3
%define	%%xraw_key	%4
%define	%%xtmp	%5
%define	%%ptr_key2	%6
%define	%%ptr_key1	%7
%define	%%ptr_expanded_keys	%8


	vmovdqu	%%xkey2, [%%ptr_key2]
	vmovdqu	%%xkey1, [%%ptr_key1]
	vmovdqa	[%%ptr_expanded_keys+16*0], %%xkey1

	vpxor	%%xstate_tweak, %%xkey2				; ARK for tweak encryption

	vaeskeygenassist		%%xraw_key, %%xkey2, 0x1	; Generating round key 1 for key2
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist		%%xraw_key, %%xkey1, 0x1	; Generating round key 1 for key1
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey1
	vaesenc			%%xstate_tweak, %%xkey2		; round 1 for tweak encryption
	vmovdqa			[%%ptr_expanded_keys+16*1], %%xkey1

	vaeskeygenassist		%%xraw_key, %%xkey2, 0x2	; Generating round key 2 for key2
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist		%%xraw_key, %%xkey1, 0x2	; Generating round key 2 for key1
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey1
	vaesenc			%%xstate_tweak, %%xkey2		; round 2 for tweak encryption
	vmovdqa			[%%ptr_expanded_keys+16*2], %%xkey1

	vaeskeygenassist		%%xraw_key, %%xkey2, 0x4	; Generating round key 3 for key2
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist		%%xraw_key, %%xkey1, 0x4	; Generating round key 3 for key1
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey1
	vaesenc			%%xstate_tweak, %%xkey2		; round 3 for tweak encryption
	vmovdqa			[%%ptr_expanded_keys + 16*3], %%xkey1

	vaeskeygenassist		%%xraw_key, %%xkey2, 0x8	; Generating round key 4 for key2
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist		%%xraw_key, %%xkey1, 0x8	; Generating round key 4 for key1
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey1
	vaesenc			%%xstate_tweak, %%xkey2		; round 4 for tweak encryption
	vmovdqa			[%%ptr_expanded_keys + 16*4], %%xkey1

	vaeskeygenassist		%%xraw_key, %%xkey2, 0x10	; Generating round key 5 for key2
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist		%%xraw_key, %%xkey1, 0x10	; Generating round key 5 for key1
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey1
	vaesenc			%%xstate_tweak, %%xkey2		; round 5 for tweak encryption
	vmovdqa			[%%ptr_expanded_keys + 16*5], %%xkey1

	vaeskeygenassist		%%xraw_key, %%xkey2, 0x20	; Generating round key 6 for key2
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist		%%xraw_key, %%xkey1, 0x20	; Generating round key 6 for key1
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey1
	vaesenc			%%xstate_tweak, %%xkey2		; round 6 for tweak encryption
	vmovdqa			[%%ptr_expanded_keys + 16*6], %%xkey1

	vaeskeygenassist		%%xraw_key, %%xkey2, 0x40	; Generating round key 7 for key2
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist		%%xraw_key, %%xkey1, 0x40	; Generating round key 7 for key1
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey1
	vaesenc			%%xstate_tweak, %%xkey2		; round 7 for tweak encryption
	vmovdqa			[%%ptr_expanded_keys + 16*7], %%xkey1

	vaeskeygenassist		%%xraw_key, %%xkey2, 0x80	; Generating round key 8 for key2
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist		%%xraw_key, %%xkey1, 0x80	; Generating round key 8 for key1
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey1
	vaesenc			%%xstate_tweak, %%xkey2		; round 8 for tweak encryption
	vmovdqa			[%%ptr_expanded_keys + 16*8], %%xkey1

	vaeskeygenassist		%%xraw_key, %%xkey2, 0x1b	; Generating round key 9 for key2
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist		%%xraw_key, %%xkey1, 0x1b	; Generating round key 9 for key1
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey1
	vaesenc			%%xstate_tweak, %%xkey2		; round 9 for tweak encryption
	vmovdqa			[%%ptr_expanded_keys + 16*9], %%xkey1

	vaeskeygenassist		%%xraw_key, %%xkey2, 0x36	; Generating round key 10 for key2
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey2
	vaeskeygenassist		%%xraw_key, %%xkey1, 0x36	; Generating round key 10 for key1
	key_expansion_128	%%xraw_key, %%xtmp, %%xkey1
	vaesenclast		%%xstate_tweak, %%xkey2		; round 10 for tweak encryption
	vmovdqa			[%%ptr_expanded_keys + 16*10], %%xkey1

	vmovdqa	[TW], %%xstate_tweak		; Store the encrypted Tweak value
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
	vaesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesenc  %%ST7, %%T0
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
	vaesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesenc  %%ST7, %%T0
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
	vaesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesenc  %%ST7, %%T0
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
	vaesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesenc  %%ST7, %%T0
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
	vaesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesenc  %%ST7, %%T0
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
	vaesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesenc  %%ST7, %%T0
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
	vaesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesenc  %%ST7, %%T0
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
	vaesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesenc  %%ST7, %%T0
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
	vaesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesenc  %%ST7, %%T0
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
	vaesenclast      %%ST1, %%T0
%if (%%num_blocks>=2)
	vaesenclast      %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	vaesenclast      %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	vaesenclast      %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	vaesenclast      %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	vaesenclast      %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	vaesenclast      %%ST7, %%T0
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
%macro  encrypt_by_eight_zmm 6
%define %%ST1   %1      ; state 1
%define %%ST2   %2      ; state 2
%define %%TW1   %3      ; tweak 1
%define %%TW2   %4      ; tweak 2
%define %%T0    %5     ; Temp register
%define %%last_eight     %6

	; xor Tweak values
	vpxorq    %%ST1, %%TW1
	vpxorq    %%ST2, %%TW2

	; ARK
	vbroadcasti32x4 %%T0, [keys]
	vpxorq    %%ST1, %%T0
	vpxorq    %%ST2, %%T0

%if (0 == %%last_eight)
		vpsrldq		zmm13, %%TW1, 15
		vpclmulqdq	zmm14, zmm13, zpoly, 0
		vpslldq		zmm15, %%TW1, 1
		vpxord		zmm15, zmm15, zmm14
%endif
	; round 1
	vbroadcasti32x4 %%T0, [keys + 16*1]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0

	; round 2
	vbroadcasti32x4 %%T0, [keys + 16*2]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0

	; round 3
	vbroadcasti32x4 %%T0, [keys + 16*3]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0
%if (0 == %%last_eight)
		vpsrldq		zmm13, %%TW2, 15
		vpclmulqdq	zmm14, zmm13, zpoly, 0
		vpslldq		zmm16, %%TW2, 1
		vpxord		zmm16, zmm16, zmm14
%endif
	; round 4
	vbroadcasti32x4 %%T0, [keys + 16*4]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0

	; round 5
	vbroadcasti32x4 %%T0, [keys + 16*5]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0

	; round 6
	vbroadcasti32x4 %%T0, [keys + 16*6]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0

	; round 7
	vbroadcasti32x4 %%T0, [keys + 16*7]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0

	; round 8
	vbroadcasti32x4 %%T0, [keys + 16*8]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0

	; round 9
	vbroadcasti32x4 %%T0, [keys + 16*9]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0

	; round 10
	vbroadcasti32x4 %%T0, [keys + 16*10]
	vaesenclast  %%ST1, %%T0
	vaesenclast  %%ST2, %%T0

	; xor Tweak values
	vpxorq    %%ST1, %%TW1
	vpxorq    %%ST2, %%TW2

	; load next Tweak values
	vmovdqa32  %%TW1, zmm15
	vmovdqa32  %%TW2, zmm16
%endmacro


; Encrypt 16 blocks in parallel
; generate next 8 tweak values
%macro  encrypt_by_16_zmm 10
%define %%ST1   %1      ; state 1
%define %%ST2   %2      ; state 2
%define %%ST3   %3      ; state 3
%define %%ST4   %4      ; state 4

%define %%TW1   %5      ; tweak 1
%define %%TW2   %6      ; tweak 2
%define %%TW3   %7      ; tweak 3
%define %%TW4   %8      ; tweak 4

%define %%T0    %9     ; Temp register
%define %%last_eight     %10

	; xor Tweak values
	vpxorq    %%ST1, %%TW1
	vpxorq    %%ST2, %%TW2
	vpxorq    %%ST3, %%TW3
	vpxorq    %%ST4, %%TW4

	; ARK
	vbroadcasti32x4 %%T0, [keys]
	vpxorq    %%ST1, %%T0
	vpxorq    %%ST2, %%T0
	vpxorq    %%ST3, %%T0
	vpxorq    %%ST4, %%T0

%if (0 == %%last_eight)
		vpsrldq		zmm13, %%TW3, 15
		vpclmulqdq	zmm14, zmm13, zpoly, 0
		vpslldq		zmm15, %%TW3, 1
		vpxord		zmm15, zmm15, zmm14
%endif
	; round 1
	vbroadcasti32x4 %%T0, [keys + 16*1]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0
	vaesenc  %%ST3, %%T0
	vaesenc  %%ST4, %%T0

	; round 2
	vbroadcasti32x4 %%T0, [keys + 16*2]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0
	vaesenc  %%ST3, %%T0
	vaesenc  %%ST4, %%T0

	; round 3
	vbroadcasti32x4 %%T0, [keys + 16*3]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0
	vaesenc  %%ST3, %%T0
	vaesenc  %%ST4, %%T0
%if (0 == %%last_eight)
		vpsrldq		zmm13, %%TW4, 15
		vpclmulqdq	zmm14, zmm13, zpoly, 0
		vpslldq		zmm16, %%TW4, 1
		vpxord		zmm16, zmm16, zmm14
%endif
	; round 4
	vbroadcasti32x4 %%T0, [keys + 16*4]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0
	vaesenc  %%ST3, %%T0
	vaesenc  %%ST4, %%T0

	; round 5
	vbroadcasti32x4 %%T0, [keys + 16*5]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0
	vaesenc  %%ST3, %%T0
	vaesenc  %%ST4, %%T0

	; round 6
	vbroadcasti32x4 %%T0, [keys + 16*6]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0
	vaesenc  %%ST3, %%T0
	vaesenc  %%ST4, %%T0
%if (0 == %%last_eight)
		vpsrldq		zmm13, zmm15, 15
		vpclmulqdq	zmm14, zmm13, zpoly, 0
		vpslldq		zmm17, zmm15, 1
		vpxord		zmm17, zmm17, zmm14
%endif
	; round 7
	vbroadcasti32x4 %%T0, [keys + 16*7]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0
	vaesenc  %%ST3, %%T0
	vaesenc  %%ST4, %%T0

	; round 8
	vbroadcasti32x4 %%T0, [keys + 16*8]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0
	vaesenc  %%ST3, %%T0
	vaesenc  %%ST4, %%T0

	; round 9
	vbroadcasti32x4 %%T0, [keys + 16*9]
	vaesenc  %%ST1, %%T0
	vaesenc  %%ST2, %%T0
	vaesenc  %%ST3, %%T0
	vaesenc  %%ST4, %%T0
%if (0 == %%last_eight)
		vpsrldq		zmm13, zmm16, 15
		vpclmulqdq	zmm14, zmm13, zpoly, 0
		vpslldq		zmm18, zmm16, 1
		vpxord		zmm18, zmm18, zmm14
%endif
	; round 10
	vbroadcasti32x4 %%T0, [keys + 16*10]
	vaesenclast  %%ST1, %%T0
	vaesenclast  %%ST2, %%T0
	vaesenclast  %%ST3, %%T0
	vaesenclast  %%ST4, %%T0

	; xor Tweak values
	vpxorq    %%ST1, %%TW1
	vpxorq    %%ST2, %%TW2
	vpxorq    %%ST3, %%TW3
	vpxorq    %%ST4, %%TW4

	; load next Tweak values
	vmovdqa32  %%TW1, zmm15
	vmovdqa32  %%TW2, zmm16
	vmovdqa32  %%TW3, zmm17
	vmovdqa32  %%TW4, zmm18
%endmacro


section .text

mk_global XTS_AES_128_enc_vaes, function
XTS_AES_128_enc_vaes:
	endbranch

%define ALIGN_STACK
%ifdef ALIGN_STACK
	push		rbp
	mov		rbp, rsp
	sub		rsp, VARIABLE_OFFSET
	and		rsp, ~63
%else
	sub		rsp, VARIABLE_OFFSET
%endif

	mov		[_gpr + 8*0], rbx
%ifidn __OUTPUT_FORMAT__, win64
	mov		[_gpr + 8*1], rdi
	mov		[_gpr + 8*2], rsi

	vmovdqa		[_xmm + 16*0], xmm6
	vmovdqa		[_xmm + 16*1], xmm7
	vmovdqa		[_xmm + 16*2], xmm8
	vmovdqa		[_xmm + 16*3], xmm9
	vmovdqa		[_xmm + 16*4], xmm10
	vmovdqa		[_xmm + 16*5], xmm11
	vmovdqa		[_xmm + 16*6], xmm12
	vmovdqa		[_xmm + 16*7], xmm13
	vmovdqa		[_xmm + 16*8], xmm14
	vmovdqa		[_xmm + 16*9], xmm15
%endif

	mov		ghash_poly_8b, GHASH_POLY       ; load 0x87 to ghash_poly_8b


	vmovdqu		xmm1, [T_val]                   ; read initial Tweak value
	vpxor		xmm4, xmm4                      ; for key expansion
	encrypt_T       xmm0, xmm1, xmm2, xmm3, xmm4, ptr_key2, ptr_key1, keys


%ifidn __OUTPUT_FORMAT__, win64
	mov             ptr_plaintext, [rsp + VARIABLE_OFFSET + 8*5]	; plaintext pointer
	mov             ptr_ciphertext, [rsp + VARIABLE_OFFSET + 8*6]	; ciphertext pointer
%endif

	cmp		N_val, 128
	jl              _less_than_128_bytes

	vpbroadcastq	zpoly, ghash_poly_8b

	cmp		N_val, 256
	jge		_start_by16

	cmp		N_val, 128
	jge		_start_by8

_do_n_blocks:
	cmp		N_val, 0
	je		_ret_

	cmp		N_val, (7*16)
	jge		_remaining_num_blocks_is_7

	cmp		N_val, (6*16)
	jge		_remaining_num_blocks_is_6

	cmp		N_val, (5*16)
	jge		_remaining_num_blocks_is_5

	cmp		N_val, (4*16)
	jge		_remaining_num_blocks_is_4

	cmp		N_val, (3*16)
	jge		_remaining_num_blocks_is_3

	cmp		N_val, (2*16)
	jge		_remaining_num_blocks_is_2

	cmp		N_val, (1*16)
	jge		_remaining_num_blocks_is_1

;; _remaining_num_blocks_is_0:
	vmovdqa		xmm8, xmm0
	vmovdqa		xmm0, xmm9
	jmp		_steal_cipher

_remaining_num_blocks_is_7:
	mov		tmp1, -1
	shr		tmp1, 16
	kmovq		k1, tmp1
	vmovdqu8	zmm1, [ptr_plaintext+16*0]
	vmovdqu8	zmm2 {k1}, [ptr_plaintext+16*4]
	add		ptr_plaintext, 16*7
	encrypt_by_eight_zmm  zmm1, zmm2, zmm9, zmm10, zmm0, 1
	vmovdqu8	[ptr_ciphertext+16*0], zmm1
	vmovdqu8	[ptr_ciphertext+16*4] {k1}, zmm2
	add		ptr_ciphertext, 16*7

	vextracti32x4	xmm8, zmm2, 0x2
	vextracti32x4	xmm0, zmm10, 0x3
	and		N_val, 15
	je		_ret_
	jmp		_steal_cipher

_remaining_num_blocks_is_6:
	vmovdqu8	zmm1, [ptr_plaintext+16*0]
	vmovdqu8	ymm2, [ptr_plaintext+16*4]
	add		ptr_plaintext, 16*6
	encrypt_by_eight_zmm  zmm1, zmm2, zmm9, zmm10, zmm0, 1
	vmovdqu8	[ptr_ciphertext+16*0], zmm1
	vmovdqu8	[ptr_ciphertext+16*4], ymm2
	add		ptr_ciphertext, 16*6

	vextracti32x4	xmm8, zmm2, 0x1
	vextracti32x4	xmm0, zmm10, 0x2
	and		N_val, 15
	je		_ret_
	jmp		_steal_cipher

_remaining_num_blocks_is_5:
	vmovdqu8	zmm1, [ptr_plaintext+16*0]
	vmovdqu		xmm2, [ptr_plaintext+16*4]
	add		ptr_plaintext, 16*5
	encrypt_by_eight_zmm  zmm1, zmm2, zmm9, zmm10, zmm0, 1
	vmovdqu8	[ptr_ciphertext+16*0], zmm1
	vmovdqu		[ptr_ciphertext+16*4], xmm2
	add		ptr_ciphertext, 16*5

	movdqa		xmm8, xmm2
	vextracti32x4	xmm0, zmm10, 0x1
	and		N_val, 15
	je		_ret_
	jmp		_steal_cipher

_remaining_num_blocks_is_4:
	vmovdqu8	zmm1, [ptr_plaintext+16*0]
	add		ptr_plaintext, 16*4
	encrypt_by_eight_zmm  zmm1, zmm2, zmm9, zmm10, zmm0, 1
	vmovdqu8	[ptr_ciphertext+16*0], zmm1
	add		ptr_ciphertext, 16*4

	vextracti32x4	xmm8, zmm1, 0x3
	vextracti32x4	xmm0, zmm10, 0x0
	and		N_val, 15
	je		_ret_
	jmp		_steal_cipher

_remaining_num_blocks_is_3:
	vextracti32x4	xmm10, zmm9, 1
	vextracti32x4	xmm11, zmm9, 2
	vmovdqu		xmm1, [ptr_plaintext+16*0]
	vmovdqu		xmm2, [ptr_plaintext+16*1]
	vmovdqu		xmm3, [ptr_plaintext+16*2]
	add		ptr_plaintext, 16*3
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 3, 1
	vmovdqu		[ptr_ciphertext+16*0], xmm1
	vmovdqu		[ptr_ciphertext+16*1], xmm2
	vmovdqu		[ptr_ciphertext+16*2], xmm3
	add		ptr_ciphertext, 16*3

	vmovdqa		xmm8, xmm3
	vextracti32x4	xmm0, zmm9, 3
	and		N_val, 15
	je		_ret_
	jmp		_steal_cipher

_remaining_num_blocks_is_2:
	vextracti32x4	xmm10, zmm9, 1
	vmovdqu		xmm1, [ptr_plaintext+16*0]
	vmovdqu		xmm2, [ptr_plaintext+16*1]
	add		ptr_plaintext, 16*2
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 2, 1
	vmovdqu		[ptr_ciphertext+16*0], xmm1
	vmovdqu		[ptr_ciphertext+16*1], xmm2
	add		ptr_ciphertext, 16*2

	vmovdqa		xmm8, xmm2
	vextracti32x4	xmm0, zmm9, 2
	and		N_val, 15
	je		_ret_
	jmp		_steal_cipher

_remaining_num_blocks_is_1:
	vmovdqu		xmm1, [ptr_plaintext]
	add		ptr_plaintext, 16
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 1, 1
	vmovdqu		[ptr_ciphertext], xmm1
	add		ptr_ciphertext, 16

	vmovdqa		xmm8, xmm1
	vextracti32x4	xmm0, zmm9, 1
	and		N_val, 15
	je		_ret_
	jmp		_steal_cipher


_start_by16:
	; Make first 7 tweak values
	vbroadcasti32x4	zmm0, [TW]
	vbroadcasti32x4	zmm8, [shufb_15_7]
	mov		tmp1, 0xaa
	kmovq		k2, tmp1

	; Mult tweak by 2^{3, 2, 1, 0}
	vpshufb		zmm1, zmm0, zmm8		; mov 15->0, 7->8
	vpsllvq		zmm4, zmm0, [const_dq3210]	; shift l 3,2,1,0
	vpsrlvq		zmm2, zmm1, [const_dq5678]	; shift r 5,6,7,8
	vpclmulqdq      zmm3, zmm2, zpoly, 0x00
	vpxorq		zmm4 {k2}, zmm4, zmm2		; tweaks shifted by 3-0
	vpxord		zmm9, zmm3, zmm4

	; Mult tweak by 2^{7, 6, 5, 4}
	vpsllvq		zmm5, zmm0, [const_dq7654]	; shift l 7,6,5,4
	vpsrlvq		zmm6, zmm1, [const_dq1234]	; shift r 1,2,3,4
	vpclmulqdq      zmm7, zmm6, zpoly, 0x00
	vpxorq		zmm5 {k2}, zmm5, zmm6		; tweaks shifted by 7-4
	vpxord		zmm10, zmm7, zmm5

	; Make next 8 tweak values by all x 2^8
	vpsrldq		zmm13, zmm9, 15
	vpclmulqdq	zmm14, zmm13, zpoly, 0
	vpslldq		zmm11, zmm9, 1
	vpxord		zmm11, zmm11, zmm14

	vpsrldq		zmm15, zmm10, 15
	vpclmulqdq	zmm16, zmm15, zpoly, 0
	vpslldq		zmm12, zmm10, 1
	vpxord		zmm12, zmm12, zmm16

_main_loop_run_16:
	vmovdqu8	zmm1, [ptr_plaintext+16*0]
	vmovdqu8	zmm2, [ptr_plaintext+16*4]
	vmovdqu8	zmm3, [ptr_plaintext+16*8]
	vmovdqu8	zmm4, [ptr_plaintext+16*12]
	add		ptr_plaintext, 256

	encrypt_by_16_zmm  zmm1, zmm2, zmm3, zmm4, zmm9, zmm10, zmm11, zmm12, zmm0, 0

	vmovdqu8	[ptr_ciphertext+16*0], zmm1
	vmovdqu8	[ptr_ciphertext+16*4], zmm2
	vmovdqu8	[ptr_ciphertext+16*8], zmm3
	vmovdqu8	[ptr_ciphertext+16*12], zmm4
	add		ptr_ciphertext, 256
	sub		N_val, 256

	cmp		N_val, 256
	jge		_main_loop_run_16

	cmp		N_val, 128
	jge		_main_loop_run_8

	vextracti32x4	xmm0, zmm4, 0x3 ; keep last crypted block
	jmp		_do_n_blocks

_start_by8:
	; Make first 7 tweak values
	vbroadcasti32x4	zmm0, [TW]
	vbroadcasti32x4	zmm8, [shufb_15_7]
	mov		tmp1, 0xaa
	kmovq		k2, tmp1

	; Mult tweak by 2^{3, 2, 1, 0}
	vpshufb		zmm1, zmm0, zmm8		; mov 15->0, 7->8
	vpsllvq		zmm4, zmm0, [const_dq3210]	; shift l 3,2,1,0
	vpsrlvq		zmm2, zmm1, [const_dq5678]	; shift r 5,6,7,8
	vpclmulqdq      zmm3, zmm2, zpoly, 0x00
	vpxorq		zmm4 {k2}, zmm4, zmm2		; tweaks shifted by 3-0
	vpxord		zmm9, zmm3, zmm4

	; Mult tweak by 2^{7, 6, 5, 4}
	vpsllvq		zmm5, zmm0, [const_dq7654]	; shift l 7,6,5,4
	vpsrlvq		zmm6, zmm1, [const_dq1234]	; shift r 1,2,3,4
	vpclmulqdq      zmm7, zmm6, zpoly, 0x00
	vpxorq		zmm5 {k2}, zmm5, zmm6		; tweaks shifted by 7-4
	vpxord		zmm10, zmm7, zmm5

_main_loop_run_8:
	; load plaintext
	vmovdqu8	zmm1, [ptr_plaintext+16*0]
	vmovdqu8	zmm2, [ptr_plaintext+16*4]
	add		ptr_plaintext, 128

	encrypt_by_eight_zmm  zmm1, zmm2, zmm9, zmm10, zmm0, 0

	; store ciphertext
	vmovdqu8	[ptr_ciphertext+16*0], zmm1
	vmovdqu8	[ptr_ciphertext+16*4], zmm2
	add		ptr_ciphertext, 128
	sub		N_val, 128

	cmp		N_val, 128
	jge		_main_loop_run_8

	vextracti32x4	xmm0, zmm2, 0x3 ; keep last crypted block
	jmp		_do_n_blocks

_steal_cipher_next:
	; generate next Tweak value
	xor		ghash_poly_8b_temp, ghash_poly_8b_temp
	shl		twtempl, 1
	adc		twtemph, twtemph
	cmovc		ghash_poly_8b_temp, ghash_poly_8b
	xor		twtempl, ghash_poly_8b_temp
	mov		[TW], twtempl
	mov		[TW + 8], twtemph
	vmovdqa		xmm0, [TW]

_steal_cipher:
	; start cipher stealing simplified: xmm8 - last cipher block, xmm0 - next tweak
	vmovdqa		xmm2, xmm8

	; shift xmm8 to the left by 16-N_val bytes
	lea		twtempl, [vpshufb_shf_table]
	vmovdqu		xmm10, [twtempl+N_val]
	vpshufb		xmm8, xmm10

	vmovdqu		xmm3, [ptr_plaintext - 16 + N_val]
	vmovdqu		[ptr_ciphertext - 16 + N_val], xmm8

	; shift xmm3 to the right by 16-N_val bytes
	lea		twtempl, [vpshufb_shf_table +16]
	sub		twtempl, N_val
	vmovdqu		xmm10, [twtempl]
	vpxor		xmm10, [mask1]
	vpshufb		xmm3, xmm10

	vpblendvb	xmm3, xmm3, xmm2, xmm10

	; xor Tweak value
	vpxor		xmm8, xmm3, xmm0

	;encrypt last block with cipher stealing
	vpxor		xmm8, [keys]		; ARK
	vaesenc		xmm8, [keys + 16*1]	; round 1
	vaesenc		xmm8, [keys + 16*2]	; round 2
	vaesenc		xmm8, [keys + 16*3]	; round 3
	vaesenc		xmm8, [keys + 16*4]	; round 4
	vaesenc		xmm8, [keys + 16*5]	; round 5
	vaesenc		xmm8, [keys + 16*6]	; round 6
	vaesenc		xmm8, [keys + 16*7]	; round 7
	vaesenc		xmm8, [keys + 16*8]	; round 8
	vaesenc		xmm8, [keys + 16*9]	; round 9
	vaesenclast	xmm8, [keys + 16*10]	; round 10

	; xor Tweak value
	vpxor		xmm8, xmm8, xmm0

	; store last ciphertext value
	vmovdqu		[ptr_ciphertext - 16], xmm8

_ret_:
	mov		rbx, [_gpr + 8*0]

%ifidn __OUTPUT_FORMAT__, win64
	mov		rdi, [_gpr + 8*1]
	mov		rsi, [_gpr + 8*2]

	vmovdqa		xmm6, [_xmm + 16*0]
	vmovdqa		xmm7, [_xmm + 16*1]
	vmovdqa		xmm8, [_xmm + 16*2]
	vmovdqa		xmm9, [_xmm + 16*3]
	vmovdqa		xmm10, [_xmm + 16*4]
	vmovdqa		xmm11, [_xmm + 16*5]
	vmovdqa		xmm12, [_xmm + 16*6]
	vmovdqa		xmm13, [_xmm + 16*7]
	vmovdqa		xmm14, [_xmm + 16*8]
	vmovdqa		xmm15, [_xmm + 16*9]
%endif

%ifndef ALIGN_STACK
	add		rsp, VARIABLE_OFFSET
%else
	mov		rsp, rbp
	pop		rbp
%endif
	ret


_less_than_128_bytes:
	cmp		N_val, 16
	jb		_ret_

	mov		tmp1, N_val
	and		tmp1, (7*16)
	cmp		tmp1, (6*16)
	je		_num_blocks_is_6
	cmp		tmp1, (5*16)
	je		_num_blocks_is_5
	cmp		tmp1, (4*16)
	je		_num_blocks_is_4
	cmp		tmp1, (3*16)
	je		_num_blocks_is_3
	cmp		tmp1, (2*16)
	je		_num_blocks_is_2
	cmp		tmp1, (1*16)
	je		_num_blocks_is_1

_num_blocks_is_7:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 7
	add		ptr_plaintext, 16*7
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 7, 1
	; store ciphertext
	vmovdqu		[ptr_ciphertext+16*0], xmm1
	vmovdqu		[ptr_ciphertext+16*1], xmm2
	vmovdqu		[ptr_ciphertext+16*2], xmm3
	vmovdqu		[ptr_ciphertext+16*3], xmm4
	vmovdqu		[ptr_ciphertext+16*4], xmm5
	vmovdqu		[ptr_ciphertext+16*5], xmm6
	vmovdqu		[ptr_ciphertext+16*6], xmm7
	add		ptr_ciphertext, 16*7
	vmovdqa		xmm8, xmm7

	and		N_val, 15               ; N_val = N_val mod 16
	je		_ret_
	jmp		_steal_cipher_next
_num_blocks_is_6:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 6
	add		ptr_plaintext, 16*6
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 6, 1
	; store ciphertext
	vmovdqu		[ptr_ciphertext+16*0], xmm1
	vmovdqu		[ptr_ciphertext+16*1], xmm2
	vmovdqu		[ptr_ciphertext+16*2], xmm3
	vmovdqu		[ptr_ciphertext+16*3], xmm4
	vmovdqu		[ptr_ciphertext+16*4], xmm5
	vmovdqu		[ptr_ciphertext+16*5], xmm6

	add		ptr_ciphertext, 16*6
	vmovdqa		xmm8, xmm6

	and		N_val, 15               ; N_val = N_val mod 16
	je		_ret_
	jmp		_steal_cipher_next
_num_blocks_is_5:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 5
	add		ptr_plaintext, 16*5
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 5, 1
	; store ciphertext
	vmovdqu		[ptr_ciphertext+16*0], xmm1
	vmovdqu		[ptr_ciphertext+16*1], xmm2
	vmovdqu		[ptr_ciphertext+16*2], xmm3
	vmovdqu		[ptr_ciphertext+16*3], xmm4
	vmovdqu		[ptr_ciphertext+16*4], xmm5

	add		ptr_ciphertext, 16*5
	vmovdqa		xmm8, xmm5

	and		N_val, 15               ; N_val = N_val mod 16
	je		_ret_
	jmp		_steal_cipher_next
_num_blocks_is_4:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 4
	add		ptr_plaintext, 16*4
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 4, 1
	; store ciphertext
	vmovdqu		[ptr_ciphertext+16*0], xmm1
	vmovdqu		[ptr_ciphertext+16*1], xmm2
	vmovdqu		[ptr_ciphertext+16*2], xmm3
	vmovdqu		[ptr_ciphertext+16*3], xmm4

	add		ptr_ciphertext, 16*4
	vmovdqa		xmm8, xmm4

	and		N_val, 15               ; N_val = N_val mod 16
	je		_ret_
	jmp		_steal_cipher_next
_num_blocks_is_3:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 3
	add		ptr_plaintext, 16*3
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 3, 1
	; store ciphertext
	vmovdqu		[ptr_ciphertext+16*0], xmm1
	vmovdqu		[ptr_ciphertext+16*1], xmm2
	vmovdqu		[ptr_ciphertext+16*2], xmm3

	add		ptr_ciphertext, 16*3
	vmovdqa		xmm8, xmm3

	and		N_val, 15               ; N_val = N_val mod 16
	je		_ret_
	jmp		_steal_cipher_next

_num_blocks_is_2:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 2
	add		ptr_plaintext, 16*2
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 2, 1
	; store ciphertext
	vmovdqu		[ptr_ciphertext], xmm1
	vmovdqu		[ptr_ciphertext+16], xmm2

	add		ptr_ciphertext, 16*2
	vmovdqa		xmm8, xmm2

	and		N_val, 15               ; N_val = N_val mod 16
	je		_ret_
	jmp		_steal_cipher_next

_num_blocks_is_1:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 1
	add		ptr_plaintext, 16*1
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 1, 1
	vmovdqu		[ptr_ciphertext], xmm1
	add		ptr_ciphertext, 16*1
	vmovdqa		xmm8, xmm1
	and		N_val, 15               ; N_val = N_val mod 16
	je		_ret_
	jmp		_steal_cipher_next

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

const_dq3210: dq 0, 0, 1, 1, 2, 2, 3, 3
const_dq5678: dq 8, 8, 7, 7, 6, 6, 5, 5
const_dq7654: dq 4, 4, 5, 5, 6, 6, 7, 7
const_dq1234: dq 4, 4, 3, 3, 2, 2, 1, 1

shufb_15_7: db 15, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff

%else  ; Assembler doesn't understand these opcodes. Add empty symbol for windows.
%ifidn __OUTPUT_FORMAT__, win64
global no_XTS_AES_128_enc_vaes
no_XTS_AES_128_enc_vaes:
%endif
%endif ; (AS_FEATURE_LEVEL) >= 10

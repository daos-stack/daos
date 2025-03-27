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
; XTS encrypt function with 128-bit AES
; expanded keys are not aligned
; plaintext and ciphertext are not aligned
; second key is stored in the stack as aligned to 16 Bytes
; first key is required only once, no need for storage of this key

%include "reg_sizes.asm"

default rel
%define TW              rsp     ; store 8 tweak values
%define keys    rsp + 16*8      ; store 11 expanded keys

%ifidn __OUTPUT_FORMAT__, win64
	%define _xmm    rsp + 16*19     ; store xmm6:xmm15
%endif

%ifidn __OUTPUT_FORMAT__, elf64
%define _gpr    rsp + 16*19     ; store rbx
%define VARIABLE_OFFSET 16*8 + 16*11 + 8*1     ; VARIABLE_OFFSET has to be an odd multiple of 8
%else
%define _gpr    rsp + 16*29     ; store rdi, rsi, rbx
%define VARIABLE_OFFSET 16*8 + 16*11 + 16*10 + 8*3     ; VARIABLE_OFFSET has to be an odd multiple of 8
%endif

%define GHASH_POLY 0x87

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void XTS_AES_128_enc_expanded_key_sse(
;               UINT8 *k2,      // key used for tweaking, 16*11 bytes
;               UINT8 *k1,      // key used for "ECB" encryption, 16*11 bytes
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


; macro to encrypt the tweak value

%macro  encrypt_T 8
%define %%xkey2         %1
%define %%xstate_tweak  %2
%define %%xkey1         %3
%define %%xraw_key      %4
%define %%xtmp          %5
%define %%ptr_key2      %6
%define %%ptr_key1      %7
%define %%ptr_expanded_keys     %8

	movdqu  %%xkey2, [%%ptr_key2]
	pxor    %%xstate_tweak, %%xkey2                         ; ARK for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1]
	movdqa  [%%ptr_expanded_keys+16*0], %%xkey1             ; store round keys in stack

	movdqu  %%xkey2, [%%ptr_key2 + 16*1]
	aesenc  %%xstate_tweak, %%xkey2                         ; round 1 for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1 + 16*1]
	movdqa  [%%ptr_expanded_keys+16*1], %%xkey1             ; store round keys in stack


	movdqu  %%xkey2, [%%ptr_key2 + 16*2]
	aesenc  %%xstate_tweak, %%xkey2                         ; round 2 for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1 + 16*2]
	movdqa  [%%ptr_expanded_keys+16*2], %%xkey1             ; store round keys in stack

	movdqu  %%xkey2, [%%ptr_key2 + 16*3]
	aesenc  %%xstate_tweak, %%xkey2                         ; round 3 for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1 + 16*3]
	movdqa  [%%ptr_expanded_keys+16*3], %%xkey1             ; store round keys in stack

	movdqu  %%xkey2, [%%ptr_key2 + 16*4]
	aesenc  %%xstate_tweak, %%xkey2                         ; round 4 for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1 + 16*4]
	movdqa  [%%ptr_expanded_keys+16*4], %%xkey1             ; store round keys in stack

	movdqu  %%xkey2, [%%ptr_key2 + 16*5]
	aesenc  %%xstate_tweak, %%xkey2                         ; round 5 for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1 + 16*5]
	movdqa  [%%ptr_expanded_keys+16*5], %%xkey1             ; store round keys in stack

	movdqu  %%xkey2, [%%ptr_key2 + 16*6]
	aesenc  %%xstate_tweak, %%xkey2                         ; round 6 for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1 + 16*6]
	movdqa  [%%ptr_expanded_keys+16*6], %%xkey1             ; store round keys in stack

	movdqu  %%xkey2, [%%ptr_key2 + 16*7]
	aesenc  %%xstate_tweak, %%xkey2                         ; round 7 for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1 + 16*7]
	movdqa  [%%ptr_expanded_keys+16*7], %%xkey1             ; store round keys in stack


	movdqu  %%xkey2, [%%ptr_key2 + 16*8]
	aesenc  %%xstate_tweak, %%xkey2                         ; round 8 for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1 + 16*8]
	movdqa  [%%ptr_expanded_keys+16*8], %%xkey1             ; store round keys in stack


	movdqu  %%xkey2, [%%ptr_key2 + 16*9]
	aesenc  %%xstate_tweak, %%xkey2                         ; round 9 for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1 + 16*9]
	movdqa  [%%ptr_expanded_keys+16*9], %%xkey1             ; store round keys in stack


	movdqu  %%xkey2, [%%ptr_key2 + 16*10]
	aesenclast      %%xstate_tweak, %%xkey2                 ; round 10 for tweak encryption

	movdqu  %%xkey1, [%%ptr_key1 + 16*10]
	movdqa  [%%ptr_expanded_keys+16*10], %%xkey1            ; store round keys in stack

	movdqa  [TW], %%xstate_tweak                            ; Store the encrypted Tweak value
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
		movdqa  %%TW1, [TW+16*0]
		mov     twtempl, [TW+8*0]
		mov     twtemph, [TW+8*1]
		movdqu  %%ST1, [ptr_plaintext+16*0]
%if (%%num_initial_blocks>=2)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*2], twtempl
		mov     [TW+8*3], twtemph;
		movdqa  %%TW2, [TW+16*1]
		movdqu  %%ST2, [ptr_plaintext+16*1]
%endif
%if (%%num_initial_blocks>=3)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*4], twtempl
		mov     [TW+8*5], twtemph;
		movdqa  %%TW3, [TW+16*2]
		movdqu  %%ST3, [ptr_plaintext+16*2]
%endif
%if (%%num_initial_blocks>=4)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*6], twtempl
		mov     [TW+8*7], twtemph;
		movdqa  %%TW4, [TW+16*3]
		movdqu  %%ST4, [ptr_plaintext+16*3]
%endif
%if (%%num_initial_blocks>=5)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*8], twtempl
		mov     [TW+8*9], twtemph;
		movdqa  %%TW5, [TW+16*4]
		movdqu  %%ST5, [ptr_plaintext+16*4]
%endif
%if (%%num_initial_blocks>=6)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*10], twtempl
		mov     [TW+8*11], twtemph;
		movdqa  %%TW6, [TW+16*5]
		movdqu  %%ST6, [ptr_plaintext+16*5]
%endif
%if (%%num_initial_blocks>=7)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW+8*12], twtempl
		mov     [TW+8*13], twtemph;
		movdqa  %%TW7, [TW+16*6]
		movdqu  %%ST7, [ptr_plaintext+16*6]
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
	pxor    %%ST1, %%TW1
%if (%%num_blocks>=2)
	pxor    %%ST2, %%TW2
%endif
%if (%%num_blocks>=3)
	pxor    %%ST3, %%TW3
%endif
%if (%%num_blocks>=4)
	pxor    %%ST4, %%TW4
%endif
%if (%%num_blocks>=5)
	pxor    %%ST5, %%TW5
%endif
%if (%%num_blocks>=6)
	pxor    %%ST6, %%TW6
%endif
%if (%%num_blocks>=7)
	pxor    %%ST7, %%TW7
%endif


	; ARK
	movdqa  %%T0, [keys]
	pxor    %%ST1, %%T0
%if (%%num_blocks>=2)
	pxor    %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	pxor    %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	pxor    %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	pxor    %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	pxor    %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	pxor    %%ST7, %%T0
%endif


	%if (0 == %%lt128)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
	%endif

	; round 1
	movdqa  %%T0, [keys + 16*1]
	aesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	aesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	aesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	aesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	aesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	aesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	aesenc  %%ST7, %%T0
%endif
	%if (0 == %%lt128)
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*0], twtempl     ; next Tweak1 generated
		mov     [TW + 8*1], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	%endif

	; round 2
	movdqa  %%T0, [keys + 16*2]
	aesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	aesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	aesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	aesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	aesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	aesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	aesenc  %%ST7, %%T0
%endif

	%if (0 == %%lt128)
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*2], twtempl ; next Tweak2 generated
	%endif

	; round 3
	movdqa  %%T0, [keys + 16*3]
	aesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	aesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	aesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	aesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	aesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	aesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	aesenc  %%ST7, %%T0
%endif
	%if (0 == %%lt128)
		mov     [TW + 8*3], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
	%endif

	; round 4
	movdqa  %%T0, [keys + 16*4]
	aesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	aesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	aesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	aesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	aesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	aesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	aesenc  %%ST7, %%T0
%endif

	%if (0 == %%lt128)
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*4], twtempl ; next Tweak3 generated
		mov     [TW + 8*5], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
	%endif

	; round 5
	movdqa  %%T0, [keys + 16*5]
	aesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	aesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	aesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	aesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	aesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	aesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	aesenc  %%ST7, %%T0
%endif

	%if (0 == %%lt128)
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*6], twtempl ; next Tweak4 generated
		mov     [TW + 8*7], twtemph
	%endif

	; round 6
	movdqa  %%T0, [keys + 16*6]
	aesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	aesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	aesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	aesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	aesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	aesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	aesenc  %%ST7, %%T0
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
	movdqa  %%T0, [keys + 16*7]
	aesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	aesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	aesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	aesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	aesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	aesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	aesenc  %%ST7, %%T0
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
	movdqa  %%T0, [keys + 16*8]
	aesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	aesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	aesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	aesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	aesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	aesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	aesenc  %%ST7, %%T0
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
	movdqa  %%T0, [keys + 16*9]
	aesenc  %%ST1, %%T0
%if (%%num_blocks>=2)
	aesenc  %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	aesenc  %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	aesenc  %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	aesenc  %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	aesenc  %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	aesenc  %%ST7, %%T0
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
	movdqa  %%T0, [keys + 16*10]
	aesenclast      %%ST1, %%T0
%if (%%num_blocks>=2)
	aesenclast      %%ST2, %%T0
%endif
%if (%%num_blocks>=3)
	aesenclast      %%ST3, %%T0
%endif
%if (%%num_blocks>=4)
	aesenclast      %%ST4, %%T0
%endif
%if (%%num_blocks>=5)
	aesenclast      %%ST5, %%T0
%endif
%if (%%num_blocks>=6)
	aesenclast      %%ST6, %%T0
%endif
%if (%%num_blocks>=7)
	aesenclast      %%ST7, %%T0
%endif

	; xor Tweak values
	pxor    %%ST1, %%TW1
%if (%%num_blocks>=2)
	pxor    %%ST2, %%TW2
%endif
%if (%%num_blocks>=3)
	pxor    %%ST3, %%TW3
%endif
%if (%%num_blocks>=4)
	pxor    %%ST4, %%TW4
%endif
%if (%%num_blocks>=5)
	pxor    %%ST5, %%TW5
%endif
%if (%%num_blocks>=6)
	pxor    %%ST6, %%TW6
%endif
%if (%%num_blocks>=7)
	pxor    %%ST7, %%TW7
%endif


%if (0 == %%lt128)
		; load next Tweak values
		movdqa  %%TW1, [TW + 16*0]
		movdqa  %%TW2, [TW + 16*1]
		movdqa  %%TW3, [TW + 16*2]
		movdqa  %%TW4, [TW + 16*3]
		movdqa  %%TW5, [TW + 16*4]
		movdqa  %%TW6, [TW + 16*5]
		movdqa  %%TW7, [TW + 16*6]

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
	pxor    %%ST1, %%TW1
	pxor    %%ST2, %%TW2
	pxor    %%ST3, %%TW3
	pxor    %%ST4, %%TW4
	pxor    %%ST5, %%TW5
	pxor    %%ST6, %%TW6
	pxor    %%ST7, %%TW7
	pxor    %%ST8, %%TW8

	; ARK
	movdqa %%T0, [keys]
	pxor    %%ST1, %%T0
	pxor    %%ST2, %%T0
	pxor    %%ST3, %%T0
	pxor    %%ST4, %%T0
	pxor    %%ST5, %%T0
	pxor    %%ST6, %%T0
	pxor    %%ST7, %%T0
	pxor    %%ST8, %%T0

%if (0 == %%last_eight)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
%endif
	; round 1
	movdqa %%T0, [keys + 16*1]
	aesenc  %%ST1, %%T0
	aesenc  %%ST2, %%T0
	aesenc  %%ST3, %%T0
	aesenc  %%ST4, %%T0
	aesenc  %%ST5, %%T0
	aesenc  %%ST6, %%T0
	aesenc  %%ST7, %%T0
	aesenc  %%ST8, %%T0
%if (0 == %%last_eight)
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*0], twtempl
		mov     [TW + 8*1], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
%endif
	; round 2
	movdqa %%T0, [keys + 16*2]
	aesenc  %%ST1, %%T0
	aesenc  %%ST2, %%T0
	aesenc  %%ST3, %%T0
	aesenc  %%ST4, %%T0
	aesenc  %%ST5, %%T0
	aesenc  %%ST6, %%T0
	aesenc  %%ST7, %%T0
	aesenc  %%ST8, %%T0
%if (0 == %%last_eight)
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp

%endif
	; round 3
	movdqa %%T0, [keys + 16*3]
	aesenc  %%ST1, %%T0
	aesenc  %%ST2, %%T0
	aesenc  %%ST3, %%T0
	aesenc  %%ST4, %%T0
	aesenc  %%ST5, %%T0
	aesenc  %%ST6, %%T0
	aesenc  %%ST7, %%T0
	aesenc  %%ST8, %%T0
%if (0 == %%last_eight)
		mov     [TW + 8*2], twtempl
		mov     [TW + 8*3], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
%endif
	; round 4
	movdqa %%T0, [keys + 16*4]
	aesenc  %%ST1, %%T0
	aesenc  %%ST2, %%T0
	aesenc  %%ST3, %%T0
	aesenc  %%ST4, %%T0
	aesenc  %%ST5, %%T0
	aesenc  %%ST6, %%T0
	aesenc  %%ST7, %%T0
	aesenc  %%ST8, %%T0
%if (0 == %%last_eight)
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*4], twtempl
%endif
	; round 5
	movdqa %%T0, [keys + 16*5]
	aesenc  %%ST1, %%T0
	aesenc  %%ST2, %%T0
	aesenc  %%ST3, %%T0
	aesenc  %%ST4, %%T0
	aesenc  %%ST5, %%T0
	aesenc  %%ST6, %%T0
	aesenc  %%ST7, %%T0
	aesenc  %%ST8, %%T0
%if (0 == %%last_eight)
		mov     [TW + 8*5], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
%endif
	; round 6
	movdqa %%T0, [keys + 16*6]
	aesenc  %%ST1, %%T0
	aesenc  %%ST2, %%T0
	aesenc  %%ST3, %%T0
	aesenc  %%ST4, %%T0
	aesenc  %%ST5, %%T0
	aesenc  %%ST6, %%T0
	aesenc  %%ST7, %%T0
	aesenc  %%ST8, %%T0
%if (0 == %%last_eight)
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*6], twtempl
		mov     [TW + 8*7], twtemph
%endif
	; round 7
	movdqa %%T0, [keys + 16*7]
	aesenc  %%ST1, %%T0
	aesenc  %%ST2, %%T0
	aesenc  %%ST3, %%T0
	aesenc  %%ST4, %%T0
	aesenc  %%ST5, %%T0
	aesenc  %%ST6, %%T0
	aesenc  %%ST7, %%T0
	aesenc  %%ST8, %%T0
%if (0 == %%last_eight)
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
%endif
	; round 8
	movdqa %%T0, [keys + 16*8]
	aesenc  %%ST1, %%T0
	aesenc  %%ST2, %%T0
	aesenc  %%ST3, %%T0
	aesenc  %%ST4, %%T0
	aesenc  %%ST5, %%T0
	aesenc  %%ST6, %%T0
	aesenc  %%ST7, %%T0
	aesenc  %%ST8, %%T0
%if (0 == %%last_eight)
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*8], twtempl
		mov     [TW + 8*9], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
%endif
	; round 9
	movdqa %%T0, [keys + 16*9]
	aesenc  %%ST1, %%T0
	aesenc  %%ST2, %%T0
	aesenc  %%ST3, %%T0
	aesenc  %%ST4, %%T0
	aesenc  %%ST5, %%T0
	aesenc  %%ST6, %%T0
	aesenc  %%ST7, %%T0
	aesenc  %%ST8, %%T0
%if (0 == %%last_eight)
		shl     twtempl, 1
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
%endif

%if (0 == %%last_eight)
		mov     [TW + 8*10], twtempl
		mov     [TW + 8*11], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
%endif

%if (0 == %%last_eight)
		adc     twtemph, twtemph
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
		mov     [TW + 8*12], twtempl
%endif

%if (0 == %%last_eight)
		mov     [TW + 8*13], twtemph
		xor     ghash_poly_8b_temp, ghash_poly_8b_temp
		shl     twtempl, 1
		adc     twtemph, twtemph
%endif

%if (0 == %%last_eight)
		cmovc   ghash_poly_8b_temp, ghash_poly_8b
		xor     twtempl, ghash_poly_8b_temp
;               mov     [TW + 8*14], twtempl
;               mov     [TW + 8*15], twtemph
%endif
	; round 10
	movdqa %%T0, [keys + 16*10]
	aesenclast      %%ST1, %%T0
	aesenclast      %%ST2, %%T0
	aesenclast      %%ST3, %%T0
	aesenclast      %%ST4, %%T0
	aesenclast      %%ST5, %%T0
	aesenclast      %%ST6, %%T0
	aesenclast      %%ST7, %%T0
	aesenclast      %%ST8, %%T0

	; xor Tweak values
	pxor    %%ST1, %%TW1
	pxor    %%ST2, %%TW2
	pxor    %%ST3, %%TW3
	pxor    %%ST4, %%TW4
	pxor    %%ST5, %%TW5
	pxor    %%ST6, %%TW6
	pxor    %%ST7, %%TW7
	pxor    %%ST8, %%TW8

		mov     [TW + 8*14], twtempl
		mov     [TW + 8*15], twtemph
		; load next Tweak values
		movdqa  %%TW1, [TW + 16*0]
		movdqa  %%TW2, [TW + 16*1]
		movdqa  %%TW3, [TW + 16*2]
		movdqa  %%TW4, [TW + 16*3]
		movdqa  %%TW5, [TW + 16*4]
		movdqa  %%TW6, [TW + 16*5]
		movdqa  %%TW7, [TW + 16*6]

%endmacro


section .text

mk_global XTS_AES_128_enc_expanded_key_sse, function
XTS_AES_128_enc_expanded_key_sse:
	endbranch

	sub     rsp, VARIABLE_OFFSET

	mov     [_gpr + 8*0], rbx
%ifidn __OUTPUT_FORMAT__, win64
	mov     [_gpr + 8*1], rdi
	mov     [_gpr + 8*2], rsi

	movdqa  [_xmm + 16*0], xmm6
	movdqa  [_xmm + 16*1], xmm7
	movdqa  [_xmm + 16*2], xmm8
	movdqa  [_xmm + 16*3], xmm9
	movdqa  [_xmm + 16*4], xmm10
	movdqa  [_xmm + 16*5], xmm11
	movdqa  [_xmm + 16*6], xmm12
	movdqa  [_xmm + 16*7], xmm13
	movdqa  [_xmm + 16*8], xmm14
	movdqa  [_xmm + 16*9], xmm15
%endif

	mov     ghash_poly_8b, GHASH_POLY       ; load 0x87 to ghash_poly_8b


	movdqu  xmm1, [T_val]                   ; read initial Tweak value
	pxor    xmm4, xmm4                      ; for key expansion
	encrypt_T       xmm0, xmm1, xmm2, xmm3, xmm4, ptr_key2, ptr_key1, keys


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
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3
	movdqu  [ptr_ciphertext+16*3], xmm4
	movdqu  [ptr_ciphertext+16*4], xmm5
	movdqu  [ptr_ciphertext+16*5], xmm6
	movdqu  [ptr_ciphertext+16*6], xmm7
	add     ptr_ciphertext, 16*7

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop
_initial_num_blocks_is_6:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 6
	add     ptr_plaintext, 16*6
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 6, 0
	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3
	movdqu  [ptr_ciphertext+16*3], xmm4
	movdqu  [ptr_ciphertext+16*4], xmm5
	movdqu  [ptr_ciphertext+16*5], xmm6
	add     ptr_ciphertext, 16*6

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop
_initial_num_blocks_is_5:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 5
	add     ptr_plaintext, 16*5
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 5, 0
	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3
	movdqu  [ptr_ciphertext+16*3], xmm4
	movdqu  [ptr_ciphertext+16*4], xmm5
	add     ptr_ciphertext, 16*5

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop
_initial_num_blocks_is_4:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 4
	add     ptr_plaintext, 16*4
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 4, 0
	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3
	movdqu  [ptr_ciphertext+16*3], xmm4
	add     ptr_ciphertext, 16*4

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop


_initial_num_blocks_is_3:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 3
	add     ptr_plaintext, 16*3
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 3, 0
	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3
	add     ptr_ciphertext, 16*3

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop
_initial_num_blocks_is_2:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 2
	add     ptr_plaintext, 16*2
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 2, 0
	; store ciphertext
	movdqu  [ptr_ciphertext], xmm1
	movdqu  [ptr_ciphertext+16], xmm2
	add     ptr_ciphertext, 16*2

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop

_initial_num_blocks_is_1:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 1
	add     ptr_plaintext, 16*1
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 1, 0
	; store ciphertext
	movdqu  [ptr_ciphertext], xmm1
	add     ptr_ciphertext, 16

	cmp     ptr_ciphertext, target_ptr_val
	je      _last_eight

	jmp     _main_loop

_initial_num_blocks_is_0:
		mov             twtempl, [TW+8*0]
		mov             twtemph, [TW+8*1]
		movdqa          xmm9, [TW+16*0]

		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*2], twtempl
		mov             [TW+8*3], twtemph
		movdqa          xmm10, [TW+16*1]

		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*4], twtempl
		mov             [TW+8*5], twtemph
		movdqa          xmm11, [TW+16*2]


		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*6], twtempl
		mov             [TW+8*7], twtemph
		movdqa          xmm12, [TW+16*3]


		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*8], twtempl
		mov             [TW+8*9], twtemph
		movdqa          xmm13, [TW+16*4]

		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*10], twtempl
		mov             [TW+8*11], twtemph
		movdqa          xmm14, [TW+16*5]

		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*12], twtempl
		mov             [TW+8*13], twtemph
		movdqa          xmm15, [TW+16*6]

		xor             ghash_poly_8b_temp, ghash_poly_8b_temp
		shl             twtempl, 1
		adc             twtemph, twtemph
		cmovc           ghash_poly_8b_temp, ghash_poly_8b
		xor             twtempl, ghash_poly_8b_temp
		mov             [TW+8*14], twtempl
		mov             [TW+8*15], twtemph
		;movdqa         xmm16, [TW+16*7]

		cmp             ptr_ciphertext, target_ptr_val
		je              _last_eight
_main_loop:
	; load plaintext
	movdqu  xmm1, [ptr_plaintext+16*0]
	movdqu  xmm2, [ptr_plaintext+16*1]
	movdqu  xmm3, [ptr_plaintext+16*2]
	movdqu  xmm4, [ptr_plaintext+16*3]
	movdqu  xmm5, [ptr_plaintext+16*4]
	movdqu  xmm6, [ptr_plaintext+16*5]
	movdqu  xmm7, [ptr_plaintext+16*6]
	movdqu  xmm8, [ptr_plaintext+16*7]

	add     ptr_plaintext, 128

	encrypt_by_eight xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, [TW+16*7], xmm0, 0

	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3
	movdqu  [ptr_ciphertext+16*3], xmm4
	movdqu  [ptr_ciphertext+16*4], xmm5
	movdqu  [ptr_ciphertext+16*5], xmm6
	movdqu  [ptr_ciphertext+16*6], xmm7
	movdqu  [ptr_ciphertext+16*7], xmm8
	add     ptr_ciphertext, 128

	cmp     ptr_ciphertext, target_ptr_val
	jne     _main_loop

_last_eight:
	; load plaintext
	movdqu  xmm1, [ptr_plaintext+16*0]
	movdqu  xmm2, [ptr_plaintext+16*1]
	movdqu  xmm3, [ptr_plaintext+16*2]
	movdqu  xmm4, [ptr_plaintext+16*3]
	movdqu  xmm5, [ptr_plaintext+16*4]
	movdqu  xmm6, [ptr_plaintext+16*5]
	movdqu  xmm7, [ptr_plaintext+16*6]
	movdqu  xmm8, [ptr_plaintext+16*7]
	encrypt_by_eight xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, [TW+16*7], xmm0, 1

	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3
	movdqu  [ptr_ciphertext+16*3], xmm4
	movdqu  [ptr_ciphertext+16*4], xmm5
	movdqu  [ptr_ciphertext+16*5], xmm6
	movdqu  [ptr_ciphertext+16*6], xmm7


	and     N_val, 15               ; N_val = N_val mod 16
	je      _done
_steal_cipher:
	; start cipher stealing

	; generate next Tweak value
	xor     ghash_poly_8b_temp, ghash_poly_8b_temp
	shl     twtempl, 1
	adc     twtemph, twtemph
	cmovc   ghash_poly_8b_temp, ghash_poly_8b
	xor     twtempl, ghash_poly_8b_temp
	mov     [TW], twtempl
	mov     [TW + 8], twtemph

	movdqa  xmm2, xmm8

	; shift xmm8 to the left by 16-N_val bytes
	lea     twtempl, [pshufb_shf_table]
	movdqu  xmm0, [twtempl+N_val]
	pshufb  xmm8, xmm0


	movdqu  xmm3, [ptr_plaintext + 112 + N_val]      ; state register is temporarily xmm3 to eliminate a move
	movdqu  [ptr_ciphertext + 112 + N_val], xmm8

	; shift xmm3 to the right by 16-N_val bytes
	lea     twtempl, [pshufb_shf_table +16]
	sub     twtempl, N_val
	movdqu  xmm0, [twtempl]
	pxor    xmm0, [mask1]
	pshufb  xmm3, xmm0

	pblendvb        xmm3, xmm2      ;xmm0 is implicit

	; xor Tweak value
	movdqa  xmm8, [TW]
	pxor    xmm8, xmm3      ; state register is xmm8, instead of a move from xmm3 to xmm8, destination register of pxor instruction is swapped


	;encrypt last block with cipher stealing
	pxor    xmm8, [keys]                    ; ARK
	aesenc  xmm8, [keys + 16*1]             ; round 1
	aesenc  xmm8, [keys + 16*2]             ; round 2
	aesenc  xmm8, [keys + 16*3]             ; round 3
	aesenc  xmm8, [keys + 16*4]             ; round 4
	aesenc  xmm8, [keys + 16*5]             ; round 5
	aesenc  xmm8, [keys + 16*6]             ; round 6
	aesenc  xmm8, [keys + 16*7]             ; round 7
	aesenc  xmm8, [keys + 16*8]             ; round 8
	aesenc  xmm8, [keys + 16*9]             ; round 9
	aesenclast      xmm8, [keys + 16*10]    ; round 10

	; xor Tweak value
	pxor    xmm8, [TW]

_done:
	; store last ciphertext value
	movdqu  [ptr_ciphertext+16*7], xmm8

_ret_:

	mov     rbx, [_gpr + 8*0]
%ifidn __OUTPUT_FORMAT__, win64
	mov     rdi, [_gpr + 8*1]
	mov     rsi, [_gpr + 8*2]


	movdqa  xmm6, [_xmm + 16*0]
	movdqa  xmm7, [_xmm + 16*1]
	movdqa  xmm8, [_xmm + 16*2]
	movdqa  xmm9, [_xmm + 16*3]
	movdqa  xmm10, [_xmm + 16*4]
	movdqa  xmm11, [_xmm + 16*5]
	movdqa  xmm12, [_xmm + 16*6]
	movdqa  xmm13, [_xmm + 16*7]
	movdqa  xmm14, [_xmm + 16*8]
	movdqa  xmm15, [_xmm + 16*9]
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
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 7, 1
	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3
	movdqu  [ptr_ciphertext+16*3], xmm4
	movdqu  [ptr_ciphertext+16*4], xmm5
	movdqu  [ptr_ciphertext+16*5], xmm6

	sub     ptr_ciphertext, 16*1
	movdqa  xmm8, xmm7

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done
	jmp     _steal_cipher
_num_blocks_is_6:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 6
	sub     ptr_plaintext, 16*2
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 6, 1
	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3
	movdqu  [ptr_ciphertext+16*3], xmm4
	movdqu  [ptr_ciphertext+16*4], xmm5

	sub     ptr_ciphertext, 16*2
	movdqa  xmm8, xmm6

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done
	jmp     _steal_cipher
_num_blocks_is_5:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 5
	sub     ptr_plaintext, 16*3
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 5, 1
	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3
	movdqu  [ptr_ciphertext+16*3], xmm4

	sub     ptr_ciphertext, 16*3
	movdqa  xmm8, xmm5

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done
	jmp     _steal_cipher
_num_blocks_is_4:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 4
	sub     ptr_plaintext, 16*4
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 4, 1
	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2
	movdqu  [ptr_ciphertext+16*2], xmm3

	sub     ptr_ciphertext, 16*4
	movdqa  xmm8, xmm4

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done
	jmp     _steal_cipher
_num_blocks_is_3:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 3
	sub     ptr_plaintext, 16*5
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 3, 1
	; store ciphertext
	movdqu  [ptr_ciphertext+16*0], xmm1
	movdqu  [ptr_ciphertext+16*1], xmm2

	sub     ptr_ciphertext, 16*5
	movdqa  xmm8, xmm3

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done
	jmp     _steal_cipher

_num_blocks_is_2:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 2
	sub     ptr_plaintext, 16*6
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 2, 1
	; store ciphertext
	movdqu  [ptr_ciphertext], xmm1

	sub     ptr_ciphertext, 16*6
	movdqa  xmm8, xmm2

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done
	jmp     _steal_cipher


_num_blocks_is_1:
	initialize xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, 1

	sub     ptr_plaintext, 16*7
	encrypt_initial xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm0, 1, 1
	; store ciphertext

	sub     ptr_ciphertext, 16*7
	movdqa  xmm8, xmm1

	and     N_val, 15               ; N_val = N_val mod 16
	je      _done
	jmp     _steal_cipher

section .data
align 16

pshufb_shf_table:
; use these values for shift constants for the pshufb instruction
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


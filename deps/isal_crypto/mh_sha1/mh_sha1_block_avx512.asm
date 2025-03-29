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

;; code to compute 16 SHA1 using AVX-512
;;

%include "reg_sizes.asm"

%ifdef HAVE_AS_KNOWS_AVX512

[bits 64]
default rel
section .text

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define VMOVPS	vmovdqu64
;SIMD variables definition
%define A	zmm0
%define B	zmm1
%define C	zmm2
%define D	zmm3
%define E	zmm4
%define HH0	zmm5
%define HH1	zmm6
%define HH2	zmm7
%define HH3	zmm8
%define HH4	zmm9
%define KT	zmm10
%define XTMP0	zmm11
%define XTMP1	zmm12
%define SHUF_MASK	zmm13
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;using extra 16 ZMM registers to place the inverse input data
%define W0	zmm16
%define W1	zmm17
%define W2	zmm18
%define W3	zmm19
%define W4	zmm20
%define W5	zmm21
%define W6	zmm22
%define W7	zmm23
%define W8	zmm24
%define W9	zmm25
%define W10	zmm26
%define W11	zmm27
%define W12	zmm28
%define W13	zmm29
%define W14	zmm30
%define W15	zmm31
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;macros definition
%macro ROTATE_ARGS 0
%xdefine TMP_ E
%xdefine E D
%xdefine D C
%xdefine C B
%xdefine B A
%xdefine A TMP_
%endm

%macro PROCESS_LOOP 2
%define %%WT		%1
%define %%F_IMMED	%2

	; T = ROTL_5(A) + Ft(B,C,D) + E + Kt + Wt
	; E=D, D=C, C=ROTL_30(B), B=A, A=T

	; Ft
	;  0-19 Ch(B,C,D) = (B&C) ^ (~B&D)
	; 20-39, 60-79 Parity(B,C,D) = B ^ C ^ D
	; 40-59 Maj(B,C,D) = (B&C) ^ (B&D) ^ (C&D)

	vmovdqa32	XTMP1, B		; Copy B
	vpaddd		E, E, %%WT		; E = E + Wt
	vpternlogd	XTMP1, C, D, %%F_IMMED	; TMP1 = Ft(B,C,D)
	vpaddd		E, E, KT		; E = E + Wt + Kt
	vprold		XTMP0, A, 5		; TMP0 = ROTL_5(A)
	vpaddd		E, E, XTMP1		; E = Ft(B,C,D) + E + Kt + Wt
	vprold		B, B, 30		; B = ROTL_30(B)
	vpaddd		E, E, XTMP0		; E = T

	ROTATE_ARGS
%endmacro

%macro MSG_SCHED_ROUND_16_79 4
%define %%WT	%1
%define %%WTp2	%2
%define %%WTp8	%3
%define %%WTp13	%4
	; Wt = ROTL_1(Wt-3 ^ Wt-8 ^ Wt-14 ^ Wt-16)
	; Wt+16 = ROTL_1(Wt+13 ^ Wt+8 ^ Wt+2 ^ Wt)
	vpternlogd	%%WT, %%WTp2, %%WTp8, 0x96
	vpxord		%%WT, %%WT, %%WTp13
	vprold		%%WT, %%WT, 1
%endmacro

%define APPEND(a,b) a %+ b
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%ifidn __OUTPUT_FORMAT__, elf64
 ; Linux
 %define arg0  rdi
 %define arg1  rsi
 %define arg2  rdx
 %define arg3  rcx

 %define arg4  r8
 %define arg5  r9

 %define tmp1  r10
 %define tmp2  r11
 %define tmp3  r12		; must be saved and restored
 %define tmp4  r13		; must be saved and restored
 %define tmp5  r14		; must be saved and restored
 %define tmp6  r15		; must be saved and restored
 %define return rax

 %define func(x) x:
 %macro FUNC_SAVE 0
	push	r12
	push	r13
	push	r14
	push	r15
 %endmacro
 %macro FUNC_RESTORE 0
	pop	r15
	pop	r14
	pop	r13
	pop	r12
 %endmacro
%else
 ; Windows
 %define arg0   rcx
 %define arg1   rdx
 %define arg2   r8
 %define arg3   r9

 %define arg4   r10
 %define arg5   r11
 %define tmp1   r12		; must be saved and restored
 %define tmp2   r13		; must be saved and restored
 %define tmp3   r14		; must be saved and restored
 %define tmp4   r15		; must be saved and restored
 %define tmp5   rdi		; must be saved and restored
 %define tmp6   rsi		; must be saved and restored
 %define return rax

 %define stack_size  10*16 + 7*8		; must be an odd multiple of 8
 ; remove unwind info macros
 %define func(x) x: endbranch
 %macro FUNC_SAVE 0
	sub	rsp, stack_size
	movdqa	[rsp + 0*16], xmm6
	movdqa	[rsp + 1*16], xmm7
	movdqa	[rsp + 2*16], xmm8
	movdqa	[rsp + 3*16], xmm9
	movdqa	[rsp + 4*16], xmm10
	movdqa	[rsp + 5*16], xmm11
	movdqa	[rsp + 6*16], xmm12
	movdqa	[rsp + 7*16], xmm13
	movdqa	[rsp + 8*16], xmm14
	movdqa	[rsp + 9*16], xmm15
	mov	[rsp + 10*16 + 0*8], r12
	mov	[rsp + 10*16 + 1*8], r13
	mov	[rsp + 10*16 + 2*8], r14
	mov	[rsp + 10*16 + 3*8], r15
	mov	[rsp + 10*16 + 4*8], rdi
	mov	[rsp + 10*16 + 5*8], rsi
 %endmacro

 %macro FUNC_RESTORE 0
	movdqa	xmm6, [rsp + 0*16]
	movdqa	xmm7, [rsp + 1*16]
	movdqa	xmm8, [rsp + 2*16]
	movdqa	xmm9, [rsp + 3*16]
	movdqa	xmm10, [rsp + 4*16]
	movdqa	xmm11, [rsp + 5*16]
	movdqa	xmm12, [rsp + 6*16]
	movdqa	xmm13, [rsp + 7*16]
	movdqa	xmm14, [rsp + 8*16]
	movdqa	xmm15, [rsp + 9*16]
	mov	r12,  [rsp + 10*16 + 0*8]
	mov	r13,  [rsp + 10*16 + 1*8]
	mov	r14,  [rsp + 10*16 + 2*8]
	mov	r15,  [rsp + 10*16 + 3*8]
	mov	rdi,  [rsp + 10*16 + 4*8]
	mov	rsi,  [rsp + 10*16 + 5*8]
	add	rsp, stack_size
 %endmacro
%endif
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define loops 		arg3
;variables of mh_sha1
%define mh_in_p  	arg0
%define mh_digests_p 	arg1
%define mh_data_p	arg2
%define mh_segs  	tmp1
;variables used by storing segs_digests on stack
%define RSP_SAVE	tmp2

%define pref		tmp3
%macro PREFETCH_X 1
%define %%mem  %1
	prefetchnta %%mem
%endmacro

;init hash digests
; segs_digests:low addr-> high_addr
; a  | b  |  c | ...|  p | (16)
; h0 | h0 | h0 | ...| h0 |    | Aa| Ab | Ac |...| Ap |
; h1 | h1 | h1 | ...| h1 |    | Ba| Bb | Bc |...| Bp |
; ....
; h4 | h4 | h4 | ...| h4 |    | Ea| Eb | Ec |...| Ep |

[bits 64]
section .text
align 32

;void mh_sha1_block_avx512(const uint8_t * input_data, uint32_t digests[SHA1_DIGEST_WORDS][HASH_SEGS],
;		uint8_t frame_buffer[MH_SHA1_BLOCK_SIZE], uint32_t num_blocks);
; arg 0 pointer to input data
; arg 1 pointer to digests, include segments digests(uint32_t digests[16][5])
; arg 2 pointer to aligned_frame_buffer which is used to save the big_endian data.
; arg 3 number  of 1KB blocks
;
global mh_sha1_block_avx512
func(mh_sha1_block_avx512)
	endbranch
	FUNC_SAVE

	; save rsp
	mov	RSP_SAVE, rsp

	cmp	loops, 0
	jle	.return

	; align rsp to 64 Bytes needed by avx512
	and	rsp, ~0x3f

	; copy segs_digests into registers.
	VMOVPS  HH0, [mh_digests_p + 64*0]
	VMOVPS  HH1, [mh_digests_p + 64*1]
	VMOVPS  HH2, [mh_digests_p + 64*2]
	VMOVPS  HH3, [mh_digests_p + 64*3]
	VMOVPS  HH4, [mh_digests_p + 64*4]
	;a mask used to transform to big-endian data
	vmovdqa64 SHUF_MASK, [PSHUFFLE_BYTE_FLIP_MASK]

.block_loop:
	;transform to big-endian data and store on aligned_frame
	;using extra 16 ZMM registers instead of stack
%assign I 0
%rep 8
%assign J (I+1)
	VMOVPS	APPEND(W,I),[mh_in_p + I*64+0*64]
	VMOVPS	APPEND(W,J),[mh_in_p + I*64+1*64]

	vpshufb	APPEND(W,I), APPEND(W,I), SHUF_MASK
	vpshufb	APPEND(W,J), APPEND(W,J), SHUF_MASK
%assign I (I+2)
%endrep

	vmovdqa64  A, HH0
	vmovdqa64  B, HH1
	vmovdqa64  C, HH2
	vmovdqa64  D, HH3
	vmovdqa64  E, HH4

	vmovdqa32	KT, [K00_19]
%assign I 0xCA
%assign J 0
%assign K 2
%assign L 8
%assign M 13
%assign N 0
%rep 80
	PROCESS_LOOP  APPEND(W,J),  I
	%if N < 64
	MSG_SCHED_ROUND_16_79  APPEND(W,J), APPEND(W,K), APPEND(W,L), APPEND(W,M)
	%endif
	%if N = 19
		vmovdqa32	KT, [K20_39]
		%assign I 0x96
	%elif N = 39
		vmovdqa32	KT, [K40_59]
		%assign I 0xE8
	%elif N = 59
		vmovdqa32	KT, [K60_79]
		%assign I 0x96
	%endif
	%if N % 10 = 9
		PREFETCH_X [mh_in_p + 1024+128*(N / 10)]
	%endif
%assign J ((J+1)% 16)
%assign K ((K+1)% 16)
%assign L ((L+1)% 16)
%assign M ((M+1)% 16)
%assign N (N+1)
%endrep

	; Add old digest
	vpaddd  HH0,A, HH0
	vpaddd  HH1,B, HH1
	vpaddd  HH2,C, HH2
	vpaddd  HH3,D, HH3
	vpaddd  HH4,E, HH4

	add 	mh_in_p,   1024
	sub     loops, 1
	jne     .block_loop

	; copy segs_digests to mh_digests_p
	VMOVPS  [mh_digests_p + 64*0], HH0
	VMOVPS  [mh_digests_p + 64*1], HH1
	VMOVPS  [mh_digests_p + 64*2], HH2
	VMOVPS  [mh_digests_p + 64*3], HH3
	VMOVPS  [mh_digests_p + 64*4], HH4

	mov	rsp, RSP_SAVE			; restore rsp

.return:
	FUNC_RESTORE
	ret


section .data align=64

align 64
PSHUFFLE_BYTE_FLIP_MASK: dq 0x0405060700010203
			 dq 0x0c0d0e0f08090a0b
			 dq 0x0405060700010203
			 dq 0x0c0d0e0f08090a0b
			 dq 0x0405060700010203
			 dq 0x0c0d0e0f08090a0b
			 dq 0x0405060700010203
			 dq 0x0c0d0e0f08090a0b

K00_19:			dq 0x5A8279995A827999
			dq 0x5A8279995A827999
			dq 0x5A8279995A827999
			dq 0x5A8279995A827999
			dq 0x5A8279995A827999
			dq 0x5A8279995A827999
			dq 0x5A8279995A827999
			dq 0x5A8279995A827999

K20_39:			dq  0x6ED9EBA16ED9EBA1
			dq  0x6ED9EBA16ED9EBA1
			dq  0x6ED9EBA16ED9EBA1
			dq  0x6ED9EBA16ED9EBA1
			dq  0x6ED9EBA16ED9EBA1
			dq  0x6ED9EBA16ED9EBA1
			dq  0x6ED9EBA16ED9EBA1
			dq  0x6ED9EBA16ED9EBA1

K40_59:			dq  0x8F1BBCDC8F1BBCDC
			dq  0x8F1BBCDC8F1BBCDC
			dq  0x8F1BBCDC8F1BBCDC
			dq  0x8F1BBCDC8F1BBCDC
			dq  0x8F1BBCDC8F1BBCDC
			dq  0x8F1BBCDC8F1BBCDC
			dq  0x8F1BBCDC8F1BBCDC
			dq  0x8F1BBCDC8F1BBCDC

K60_79:			dq  0xCA62C1D6CA62C1D6
			dq  0xCA62C1D6CA62C1D6
			dq  0xCA62C1D6CA62C1D6
			dq  0xCA62C1D6CA62C1D6
			dq  0xCA62C1D6CA62C1D6
			dq  0xCA62C1D6CA62C1D6
			dq  0xCA62C1D6CA62C1D6
			dq  0xCA62C1D6CA62C1D6

%else
%ifidn __OUTPUT_FORMAT__, win64
global no_mh_sha1_block_avx512
no_mh_sha1_block_avx512:
%endif
%endif ; HAVE_AS_KNOWS_AVX512

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

%include "sha1_mb_mgr_datastruct.asm"
%include "reg_sizes.asm"

[bits 64]
default rel
section .text

;; code to compute oct SHA1 using SSE-256
;; outer calling routine takes care of save and restore of XMM registers

;; Function clobbers: rax, rcx, rdx,   rbx, rsi, rdi, r9-r15; ymm0-15
;; Windows clobbers:  rax rbx     rdx rsi rdi        r9 r10 r11 r12 r13 r14 r15
;; Windows preserves:         rcx             rbp r8
;;
;; Linux clobbers:    rax rbx rcx rdx rsi            r9 r10 r11 r12 r13 r14 r15
;; Linux preserves:                       rdi rbp r8
;;
;; clobbers ymm0-15


; TRANSPOSE8 r0, r1, r2, r3, r4, r5, r6, r7, t0, t1
; "transpose" data in {r0...r7} using temps {t0...t1}
; Input looks like: {r0 r1 r2 r3 r4 r5 r6 r7}
; r0 = {a7 a6 a5 a4   a3 a2 a1 a0}
; r1 = {b7 b6 b5 b4   b3 b2 b1 b0}
; r2 = {c7 c6 c5 c4   c3 c2 c1 c0}
; r3 = {d7 d6 d5 d4   d3 d2 d1 d0}
; r4 = {e7 e6 e5 e4   e3 e2 e1 e0}
; r5 = {f7 f6 f5 f4   f3 f2 f1 f0}
; r6 = {g7 g6 g5 g4   g3 g2 g1 g0}
; r7 = {h7 h6 h5 h4   h3 h2 h1 h0}
;
; Output looks like: {r0 r1 r2 r3 r4 r5 r6 r7}
; r0 = {h0 g0 f0 e0   d0 c0 b0 a0}
; r1 = {h1 g1 f1 e1   d1 c1 b1 a1}
; r2 = {h2 g2 f2 e2   d2 c2 b2 a2}
; r3 = {h3 g3 f3 e3   d3 c3 b3 a3}
; r4 = {h4 g4 f4 e4   d4 c4 b4 a4}
; r5 = {h5 g5 f5 e5   d5 c5 b5 a5}
; r6 = {h6 g6 f6 e6   d6 c6 b6 a6}
; r7 = {h7 g7 f7 e7   d7 c7 b7 a7}
;
%macro TRANSPOSE8 10
%define %%r0 %1
%define %%r1 %2
%define %%r2 %3
%define %%r3 %4
%define %%r4 %5
%define %%r5 %6
%define %%r6 %7
%define %%r7 %8
%define %%t0 %9
%define %%t1 %10
	; process top half (r0..r3) {a...d}
	vshufps	%%t0, %%r0, %%r1, 0x44	; t0 = {b5 b4 a5 a4   b1 b0 a1 a0}
	vshufps	%%r0, %%r0, %%r1, 0xEE	; r0 = {b7 b6 a7 a6   b3 b2 a3 a2}
	vshufps %%t1, %%r2, %%r3, 0x44	; t1 = {d5 d4 c5 c4   d1 d0 c1 c0}
	vshufps	%%r2, %%r2, %%r3, 0xEE	; r2 = {d7 d6 c7 c6   d3 d2 c3 c2}
	vshufps	%%r3, %%t0, %%t1, 0xDD	; r3 = {d5 c5 b5 a5   d1 c1 b1 a1}
	vshufps	%%r1, %%r0, %%r2, 0x88	; r1 = {d6 c6 b6 a6   d2 c2 b2 a2}
	vshufps	%%r0, %%r0, %%r2, 0xDD	; r0 = {d7 c7 b7 a7   d3 c3 b3 a3}
	vshufps	%%t0, %%t0, %%t1, 0x88	; t0 = {d4 c4 b4 a4   d0 c0 b0 a0}

	; use r2 in place of t0
	; process bottom half (r4..r7) {e...h}
	vshufps	%%r2, %%r4, %%r5, 0x44	; r2 = {f5 f4 e5 e4   f1 f0 e1 e0}
	vshufps	%%r4, %%r4, %%r5, 0xEE	; r4 = {f7 f6 e7 e6   f3 f2 e3 e2}
	vshufps %%t1, %%r6, %%r7, 0x44	; t1 = {h5 h4 g5 g4   h1 h0 g1 g0}
	vshufps	%%r6, %%r6, %%r7, 0xEE	; r6 = {h7 h6 g7 g6   h3 h2 g3 g2}
	vshufps	%%r7, %%r2, %%t1, 0xDD	; r7 = {h5 g5 f5 e5   h1 g1 f1 e1}
	vshufps	%%r5, %%r4, %%r6, 0x88	; r5 = {h6 g6 f6 e6   h2 g2 f2 e2}
	vshufps	%%r4, %%r4, %%r6, 0xDD	; r4 = {h7 g7 f7 e7   h3 g3 f3 e3}
	vshufps	%%t1, %%r2, %%t1, 0x88	; t1 = {h4 g4 f4 e4   h0 g0 f0 e0}

	vperm2f128	%%r6, %%r5, %%r1, 0x13	; h6...a6
	vperm2f128	%%r2, %%r5, %%r1, 0x02	; h2...a2
	vperm2f128	%%r5, %%r7, %%r3, 0x13	; h5...a5
	vperm2f128	%%r1, %%r7, %%r3, 0x02	; h1...a1
	vperm2f128	%%r7, %%r4, %%r0, 0x13	; h7...a7
	vperm2f128	%%r3, %%r4, %%r0, 0x02	; h3...a3
	vperm2f128	%%r4, %%t1, %%t0, 0x13	; h4...a4
	vperm2f128	%%r0, %%t1, %%t0, 0x02	; h0...a0
%endmacro

;;
;; Magic functions defined in FIPS 180-1
;;
;MAGIC_F0 MACRO regF:REQ,regB:REQ,regC:REQ,regD:REQ,regT:REQ ;; F0 = ((B & C) | ((~B) & D))
%macro MAGIC_F0 5
%define %%regF %1
%define %%regB %2
%define %%regC %3
%define %%regD %4
%define %%regT %5
    vpand  %%regF, %%regB,%%regC
    vpandn %%regT, %%regB,%%regD
    vpor   %%regF, %%regT,%%regF
%endmacro

;MAGIC_F1 MACRO regF:REQ,regB:REQ,regC:REQ,regD:REQ,regT:REQ ;; (B ^ C ^ D)
%macro MAGIC_F1 5
%define %%regF %1
%define %%regB %2
%define %%regC %3
%define %%regD %4
%define %%regT %5
    vpxor  %%regF,%%regD,%%regC
    vpxor  %%regF,%%regF,%%regB
%endmacro



;MAGIC_F2 MACRO regF:REQ,regB:REQ,regC:REQ,regD:REQ,regT:REQ ;; ((B & C) | (B & D) | (C & D))
%macro MAGIC_F2 5
%define %%regF %1
%define %%regB %2
%define %%regC %3
%define %%regD %4
%define %%regT %5
    vpor   %%regF,%%regB,%%regC
    vpand  %%regT,%%regB,%%regC
    vpand  %%regF,%%regF,%%regD
    vpor   %%regF,%%regF,%%regT
%endmacro

;MAGIC_F3 MACRO regF:REQ,regB:REQ,regC:REQ,regD:REQ,regT:REQ
%macro MAGIC_F3 5
%define %%regF %1
%define %%regB %2
%define %%regC %3
%define %%regD %4
%define %%regT %5
    MAGIC_F1 %%regF,%%regB,%%regC,%%regD,%%regT
%endmacro

; PROLD reg, imm, tmp
%macro PROLD 3
%define %%reg %1
%define %%imm %2
%define %%tmp %3
	vpsrld	%%tmp, %%reg, (32-%%imm)
	vpslld	%%reg, %%reg, %%imm
	vpor	%%reg, %%reg, %%tmp
%endmacro

; PROLD reg, imm, tmp
%macro PROLD_nd 4
%define %%reg %1
%define %%imm %2
%define %%tmp %3
%define %%src %4
	vpsrld	%%tmp, %%src, (32-%%imm)
	vpslld	%%reg, %%src, %%imm
	vpor	%%reg, %%reg, %%tmp
%endmacro

%macro SHA1_STEP_00_15 10
%define %%regA	%1
%define %%regB	%2
%define %%regC	%3
%define %%regD	%4
%define %%regE	%5
%define %%regT	%6
%define %%regF	%7
%define %%memW	%8
%define %%immCNT %9
%define %%MAGIC	%10
	vpaddd	%%regE, %%regE,%%immCNT
	vpaddd	%%regE, %%regE,[rsp + (%%memW * 32)]
	PROLD_nd	%%regT,5, %%regF,%%regA
	vpaddd	%%regE, %%regE,%%regT
	%%MAGIC	%%regF,%%regB,%%regC,%%regD,%%regT      ;; FUN  = MAGIC_Fi(B,C,D)
	PROLD	%%regB,30, %%regT
	vpaddd	%%regE, %%regE,%%regF
%endmacro

%macro SHA1_STEP_16_79 10
%define %%regA	%1
%define %%regB	%2
%define %%regC	%3
%define %%regD	%4
%define %%regE	%5
%define %%regT	%6
%define %%regF	%7
%define %%memW	%8
%define %%immCNT %9
%define %%MAGIC	%10
	vpaddd	%%regE, %%regE,%%immCNT

	vmovdqu	W14, [rsp + ((%%memW - 14) & 15) * 32]
	vpxor	W16, W16, W14
	vpxor	W16, W16, [rsp + ((%%memW -  8) & 15) * 32]
	vpxor	W16, W16, [rsp + ((%%memW -  3) & 15) * 32]

	vpsrld	%%regF, W16, (32-1)
	vpslld	W16, W16, 1
	vpor	%%regF, %%regF, W16
	ROTATE_W

	vmovdqu	[rsp + ((%%memW - 0) & 15) * 32],%%regF
	vpaddd	%%regE, %%regE,%%regF

	PROLD_nd	%%regT,5, %%regF, %%regA
	vpaddd	%%regE, %%regE,%%regT
	%%MAGIC	%%regF,%%regB,%%regC,%%regD,%%regT      ;; FUN  = MAGIC_Fi(B,C,D)
	PROLD	%%regB,30, %%regT
	vpaddd	%%regE,%%regE,%%regF
%endmacro


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; FRAMESZ plus pushes must be an odd multiple of 8
%define YMM_SAVE (15-15)*32
%define FRAMESZ	32*16 + 0*8 + YMM_SAVE
%define _YMM     FRAMESZ - YMM_SAVE

%define VMOVPS	vmovups

%define IDX  rax
%define inp0 r9
%define inp1 r10
%define inp2 r11
%define inp3 r12
%define inp4 r13
%define inp5 r14
%define inp6 r15

%ifidn __OUTPUT_FORMAT__, elf64
	%define inp7 rcx
	%define arg1 rdi
	%define arg2 rsi
	%define RSP_SAVE rdx
%else
	%define inp7 rdi
	%define arg1 rcx
	%define arg2 rdx
	%define RSP_SAVE rsi
%endif


; ymm0	A
; ymm1	B
; ymm2	C
; ymm3	D
; ymm4	E
; ymm5		F	AA
; ymm6		T0	BB
; ymm7		T1	CC
; ymm8		T2	DD
; ymm9		T3	EE
; ymm10		T4	TMP
; ymm11		T5	FUN
; ymm12		T6	K
; ymm13		T7	W14
; ymm14		T8	W15
; ymm15		T9	W16

%define A	ymm0
%define B	ymm1
%define C	ymm2
%define D	ymm3
%define E	ymm4

%define F	ymm5
%define T0	ymm6
%define T1	ymm7
%define T2	ymm8
%define T3	ymm9
%define T4	ymm10
%define T5	ymm11
%define T6	ymm12
%define T7	ymm13
%define T8	ymm14
%define T9	ymm15

%define AA	ymm5
%define BB	ymm6
%define CC	ymm7
%define DD	ymm8
%define EE	ymm9
%define TMP	ymm10
%define FUN	ymm11
%define K	ymm12
%define W14	ymm13
%define W15	ymm14
%define W16	ymm15


%macro ROTATE_ARGS 0
%xdefine TMP_ E
%xdefine E D
%xdefine D C
%xdefine C B
%xdefine B A
%xdefine A TMP_
%endm

%macro ROTATE_W 0
%xdefine TMP_ W16
%xdefine W16 W15
%xdefine W15 W14
%xdefine W14 TMP_
%endm

%define DIGEST_SIZE 	(8*5*4)	; 8 streams x 5 32bit words per digest x 4 bytes per word

align 32

; void sha1_x8_avx2(SHA1_MB_ARGS_X8, uint32_t size)
; arg 1 : pointer to input data
; arg 2 : size (in blocks) ;; assumed to be >= 1
mk_global sha1_mb_x8_avx2, function, internal
sha1_mb_x8_avx2:
	endbranch

	push	RSP_SAVE

	; save rsp
	mov	RSP_SAVE, rsp
	sub	rsp, FRAMESZ	;; FRAMESZ + pushes must be even multiple of 8

	; align rsp to 32 Bytes
	and	rsp, ~0x1F

	;; Initialize digests
	vmovdqu	A, [arg1 + 0*32]
	vmovdqu	B, [arg1 + 1*32]
	vmovdqu	C, [arg1 + 2*32]
	vmovdqu	D, [arg1 + 3*32]
	vmovdqu	E, [arg1 + 4*32]

	;; transpose input onto stack
	mov	inp0,[arg1+_data_ptr+0*8]
	mov	inp1,[arg1+_data_ptr+1*8]
	mov	inp2,[arg1+_data_ptr+2*8]
	mov	inp3,[arg1+_data_ptr+3*8]
	mov	inp4,[arg1+_data_ptr+4*8]
	mov	inp5,[arg1+_data_ptr+5*8]
	mov	inp6,[arg1+_data_ptr+6*8]
	mov	inp7,[arg1+_data_ptr+7*8]

	xor	IDX, IDX
lloop:
	vmovdqu	F, [PSHUFFLE_BYTE_FLIP_MASK]
%assign I 0
%rep 2
	VMOVPS	T0,[inp0+IDX]
	VMOVPS	T1,[inp1+IDX]
	VMOVPS	T2,[inp2+IDX]
	VMOVPS	T3,[inp3+IDX]
	VMOVPS	T4,[inp4+IDX]
	VMOVPS	T5,[inp5+IDX]
	VMOVPS	T6,[inp6+IDX]
	VMOVPS	T7,[inp7+IDX]
	TRANSPOSE8	T0, T1, T2, T3, T4, T5, T6, T7, T8, T9
	vpshufb	T0, T0, F
	vmovdqu	[rsp+(I*8+0)*32],T0
	vpshufb	T1, T1, F
	vmovdqu	[rsp+(I*8+1)*32],T1
	vpshufb	T2, T2, F
	vmovdqu	[rsp+(I*8+2)*32],T2
	vpshufb	T3, T3, F
	vmovdqu	[rsp+(I*8+3)*32],T3
	vpshufb	T4, T4, F
	vmovdqu	[rsp+(I*8+4)*32],T4
	vpshufb	T5, T5, F
	vmovdqu	[rsp+(I*8+5)*32],T5
	vpshufb	T6, T6, F
	vmovdqu	[rsp+(I*8+6)*32],T6
	vpshufb	T7, T7, F
	vmovdqu	[rsp+(I*8+7)*32],T7
	add	IDX, 32
%assign I (I+1)
%endrep


	; save old digests
	vmovdqu	AA, A
	vmovdqu	BB, B
	vmovdqu	CC, C
	vmovdqu	DD, D
	vmovdqu	EE, E

;;
;; perform 0-79 steps
;;
	vmovdqu	K, [K00_19]
;; do rounds 0...15
%assign I 0
%rep 16
	SHA1_STEP_00_15 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F0
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 16...19
	vmovdqu	W16, [rsp + ((16 - 16) & 15) * 32]
	vmovdqu	W15, [rsp + ((16 - 15) & 15) * 32]
%rep 4
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F0
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 20...39
	vmovdqu	K, [K20_39]
%rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F1
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 40...59
	vmovdqu	K, [K40_59]
%rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F2
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 60...79
	vmovdqu	K, [K60_79]
%rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F3
	ROTATE_ARGS
%assign I (I+1)
%endrep

	vpaddd	A,A,AA
	vpaddd	B,B,BB
	vpaddd	C,C,CC
	vpaddd	D,D,DD
	vpaddd	E,E,EE

	sub	arg2, 1
	jne	lloop

	; write out digests
	vmovdqu	[arg1 + 0*32], A
	vmovdqu	[arg1 + 1*32], B
	vmovdqu	[arg1 + 2*32], C
	vmovdqu	[arg1 + 3*32], D
	vmovdqu	[arg1 + 4*32], E

	;; update input pointers
	add	inp0, IDX
	add	inp1, IDX
	add	inp2, IDX
	add	inp3, IDX
	add	inp4, IDX
	add	inp5, IDX
	add	inp6, IDX
	add	inp7, IDX
	mov	[arg1+_data_ptr+0*8], inp0
	mov	[arg1+_data_ptr+1*8], inp1
	mov	[arg1+_data_ptr+2*8], inp2
	mov	[arg1+_data_ptr+3*8], inp3
	mov	[arg1+_data_ptr+4*8], inp4
	mov	[arg1+_data_ptr+5*8], inp5
	mov	[arg1+_data_ptr+6*8], inp6
	mov	[arg1+_data_ptr+7*8], inp7

	;;;;;;;;;;;;;;;;
	;; Postamble

	mov	rsp, RSP_SAVE

	pop	RSP_SAVE
	ret



section .data align=32

align 32
K00_19:			dq 0x5A8279995A827999, 0x5A8279995A827999
			dq 0x5A8279995A827999, 0x5A8279995A827999
K20_39:                 dq 0x6ED9EBA16ED9EBA1, 0x6ED9EBA16ED9EBA1
			dq 0x6ED9EBA16ED9EBA1, 0x6ED9EBA16ED9EBA1
K40_59:                 dq 0x8F1BBCDC8F1BBCDC, 0x8F1BBCDC8F1BBCDC
			dq 0x8F1BBCDC8F1BBCDC, 0x8F1BBCDC8F1BBCDC
K60_79:                 dq 0xCA62C1D6CA62C1D6, 0xCA62C1D6CA62C1D6
			dq 0xCA62C1D6CA62C1D6, 0xCA62C1D6CA62C1D6

PSHUFFLE_BYTE_FLIP_MASK: dq 0x0405060700010203, 0x0c0d0e0f08090a0b
			 dq 0x0405060700010203, 0x0c0d0e0f08090a0b

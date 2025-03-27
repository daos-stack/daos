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

%include "sha512_mb_mgr_datastruct.asm"
%include "reg_sizes.asm"

%ifdef HAVE_AS_KNOWS_AVX512

[bits 64]
default rel
section .text

;; code to compute quad SHA512 using AVX512
;; use ZMMs to tackle the larger digest size
;; outer calling routine takes care of save and restore of XMM registers
;; Logic designed/laid out by JDG

;; Function clobbers: rax, rcx, rdx,   rbx, rsi, rdi, r9-r15; zmm0-31
;; Stack must be aligned to 32 bytes before call
;; Windows clobbers:  rax rbx     rdx     rdi  rbp r8 r9 r10 r11 r12 r13 r14 r15
;; Windows preserves:         rcx     rsi
;;
;; Linux clobbers:    rax rbx rcx     rsi     rbp r8 r9 r10 r11 r12 r13 r14 r15
;; Linux preserves:               rdx     rdi
;;
;; clobbers zmm0-31

%define APPEND(a,b) a %+ b

%ifidn __OUTPUT_FORMAT__, win64
   %define arg1 rcx	; arg0 preserved
   %define arg2 rdx	; arg1
   %define reg3 r8	; arg2 preserved
   %define reg4 r9	; arg3
   %define var1 rdi	; usable
   %define var2 rsi
   %define local_func_decl(func_name) global func_name
 %else
   %define arg1 rdi	; arg0
   %define arg2 rsi	; arg1
   %define var2 rdx	; arg2
   %define var1 rcx	; arg3 usable
   %define local_func_decl(func_name) mk_global func_name, function, internal
%endif

%define state    arg1
%define num_blks arg2

%define	IN	(state + _data_ptr)
%define DIGEST	state
%define SIZE	num_blks

%define IDX  var1
%define TBL  r8

%define VMOVDQ32  vmovdqu32

%define SHA512_DIGEST_WORD_SIZE 8
%define NUM_SHA512_DIGEST_WORDS 8
%define SHA512_DIGEST_ROW_SIZE 8*8
%define PTR_SZ 8
%define _data_ptr_sha512 _data_ptr

%define NUM_LANES          8
%define SZ                 8
%define SZ8	           8 * SZ
%define DIGEST_SZ          8 * SZ8
%define DIGEST_SAVE	   NUM_LANES * DIGEST_SZ
%define RSP_SAVE           1*8

; Define Stack Layout
START_FIELDS
;;;     name            size            align
FIELD	_DIGEST_SAVE,	NUM_LANES*8*64,	64
FIELD	_RSP,		8,	        8
%assign STACK_SPACE	_FIELD_OFFSET


%define inp0	r9
%define inp1	r10
%define inp2	r11
%define inp3	r12
%define inp4	r13
%define inp5	r14
%define inp6	r15
%define inp7	rax

%define A	zmm0
%define B	zmm1
%define C	zmm2
%define D	zmm3
%define E	zmm4
%define F	zmm5
%define G	zmm6
%define H	zmm7
%define T1	zmm8
%define TMP0	zmm9
%define TMP1	zmm10
%define TMP2	zmm11
%define TMP3	zmm12
%define TMP4	zmm13
%define TMP5	zmm14
%define TMP6	zmm15


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

; from sha256_fips180-2.pdf
; define rotates for Sigma function for main loop steps
%define BIG_SIGMA_0_0 28	; Sigma0
%define BIG_SIGMA_0_1 34
%define BIG_SIGMA_0_2 39
%define BIG_SIGMA_1_0 14	; Sigma1
%define BIG_SIGMA_1_1 18
%define BIG_SIGMA_1_2 41

; define rotates for Sigma function for scheduling steps

%define SMALL_SIGMA_0_0 1	; sigma0
%define SMALL_SIGMA_0_1 8
%define SMALL_SIGMA_0_2 7
%define SMALL_SIGMA_1_0 19	; sigma1
%define SMALL_SIGMA_1_1 61
%define SMALL_SIGMA_1_2 6

%define SHA_MAX_ROUNDS 80
%define SHA_ROUNDS_LESS_16 (SHA_MAX_ROUNDS - 16)

%macro TRANSPOSE8 12
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
%define %%PERM_INDEX1 %11
%define %%PERM_INDEX2 %12


; each x(i) is 32 bits, 16 * 32 = 512 ==> a full digest length, 32 single precision quantities
; r0  = {a7 a6 a5 a4   a3 a2 a1 a0}
; r1  = {b7 b6 b5 b4   b3 b2 b1 b0}
; r2  = {c7 c6 c5 c4   c3 c2 c1 c0}
; r3  = {d7 d6 d5 d4   d3 d2 d1 d0}
; r4  = {e7 e6 e5 e4   e3 e2 e1 e0}
; r5  = {f7 f6 f5 f4   f3 f2 f1 f0}
; r6  = {g7 g6 g5 g4   g3 g2 g1 g0}
; r7  = {h7 h6 h5 h4   h3 h2 h1 h0}

        ;; ;;;  will not get clobbered
        vmovdqa32 %%PERM_INDEX1, [TRANSPOSE8_PERM_INDEX_1] ;  temp
        vmovdqa32 %%PERM_INDEX2, [TRANSPOSE8_PERM_INDEX_2]  ; temp

        ; process top half (r0..r3) {a...d}
        vshufpd	%%t0, %%r0, %%r1, 0x00	; t0 = {b6 a6 b4 a4   b2 a2 b0 a0}
        vshufpd	%%r0, %%r0, %%r1, 0xFF	; r0 = {b7 a7 b5 a5   b3 a3 b1 a1}
        vshufpd	%%t1, %%r2, %%r3, 0x00	; t1 = {d6 c6 d4 c4   d2 c2 d0 c0}
        vshufpd	%%r2, %%r2, %%r3, 0xFF	; r2 = {d7 c7 d5 c5   d3 c3 d1 c1}

        vmovdqa32   %%r1, %%t0		     ; r1 and r3 free
        vpermt2q    %%r1, %%PERM_INDEX1,%%t1   ; r1 = {d4 c4 b4 a4   d0 c0 b0 a0}
        vpermt2q    %%t0, %%PERM_INDEX2,%%t1   ; t0 = {d6 c6 b6 a6   d2 c2 b2 a2}

        vmovdqa32   %%t1, %%r0		       ; t1 and r3 free
        vpermt2q    %%t1, %%PERM_INDEX1,%%r2   ; t1 = {d5 c5 b5 a5   d1 c1 b1 a1}
        vpermt2q    %%r0, %%PERM_INDEX2,%%r2   ; r0 = {d7 c7 b7 a7   d3 c3 b3 a3}

        ;; Likewise for top half ; r2 and r3 free
        vshufpd	%%r2, %%r4, %%r5, 0x00	; r2 = {f6 e6 f4 e4   f2 e2 f0 e0}
        vshufpd	%%r4, %%r4, %%r5, 0xFF	; r4 = {f7 e7 f5 e5   f3 e3 f1 e1}
        vshufpd	%%r3, %%r6, %%r7, 0x00	; r3 = {h6 g6 h4 g4   h2 g2 h0 g0}
        vshufpd	%%r6, %%r6, %%r7, 0xFF	; r6 = {h7 g7 h5 g5   h3 g3 h1 g1}

        vmovdqa32   %%r5, %%r2		     ; r5 and r7 free
        vpermt2q    %%r5, %%PERM_INDEX1,%%r3   ; r5 = {h4 g4 f4 e4   h0 g0 f0 e0}
        vpermt2q    %%r2, %%PERM_INDEX2,%%r3   ; r2 = {h6 g6 f6 e6   h2 g2 f2 e2}

        vmovdqa32   %%r7, %%r4
        vpermt2q    %%r7, %%PERM_INDEX1,%%r6   ; r7 = {h5 g5 f5 e5   h1 g1 f1 e1}
        vpermt2q    %%r4, %%PERM_INDEX2,%%r6   ; r4 = {h7 g7 f7 e7   h3 g3 f3 e3}

;;;  free r3, r6
        vshuff64x2  %%r6, %%t0, %%r2, 0xEE ; r6 = {h6 g6 f6 e6   d6 c6 b6 a6}
        vshuff64x2  %%r2, %%t0, %%r2, 0x44 ; r2 = {h2 g2 f2 e2   d2 c2 b2 a2}

;;; t0 and r3 free
        vshuff64x2  %%r3, %%r0, %%r4, 0x44 ; r3 = {h3 g3 f3 e3   d3 c3 b3 a3}
        vshuff64x2  %%t0, %%r0, %%r4, 0xEE ; t0 = {h7 g7 f7 e7   d7 c7 b7 a7}

        vshuff64x2  %%r4, %%r1, %%r5, 0xEE ; r4 = {h4 g4 f4 e4   d4 c4 b4 a4}
        vshuff64x2  %%r0, %%r1, %%r5, 0x44 ; r0 = {h0 g0 f0 e0   d0 c0 b0 a0}


        vshuff64x2  %%r5, %%t1, %%r7, 0xEE ; r5 = {h5 g5 f5 e5   d5 c5 b5 a5}
        vshuff64x2  %%r1, %%t1, %%r7, 0x44 ; r1 = {h1 g1 f1 e1   d1 c1 b1 a1}

        ;;  will re-order input to avoid move
        ;vmovdqa32   %%r7, %%t0

        ; Output looks like: {r0 r1 r2 r3 r4 r5 r6 r7}
        ; r0 = {h0 g0 f0 e0   d0 c0 b0 a0}
        ; r1 = {h1 g1 f1 e1   d1 c1 b1 a1}
        ; r2 = {h2 g2 f2 e2   d2 c2 b2 a2}
        ; r3 = {h3 g3 f3 e3   d3 c3 b3 a3}
        ; r4 = {h4 g4 f4 e4   d4 c4 b4 a4}
        ; r5 = {h5 g5 f5 e5   d5 c5 b5 a5}
        ; r6 = {h6 g6 f6 e6   d6 c6 b6 a6}
        ; temp
        ; r7 = {h7 g7 f7 e7   d7 c7 b7 a7}
%endmacro

%macro ROTATE_ARGS 0
%xdefine TMP_ H
%xdefine H G
%xdefine G F
%xdefine F E
%xdefine E D
%xdefine D C
%xdefine C B
%xdefine B A
%xdefine A TMP_
%endm



;;  CH(A, B, C) = (A&B) ^ (~A&C)
;; MAJ(E, F, G) = (E&F) ^ (E&G) ^ (F&G)
;; SIGMA0 = ROR_28  ^ ROR_34 ^ ROR_39
;; SIGMA1 = ROR_14  ^ ROR_18 ^ ROR_41
;; sigma0 = ROR_1  ^ ROR_8 ^ SHR_7
;; sigma1 = ROR_19 ^ ROR_61 ^ SHR_6

;; Main processing loop per round
;;  equivalent to %macro ROUND_00_15 2
%macro PROCESS_LOOP 2
%define %%WT	%1
%define %%ROUND	%2
        ;; T1 = H + BIG_SIGMA_1(E) + CH(E, F, G) + Kt + Wt
        ;; T2 = BIG_SIGMA_0(A) + MAJ(A, B, C)
        ;; H=G, G=F, F=E, E=D+T1, D=C, C=B, B=A, A=T1+T2

        ;; H becomes T2, then add T1 for A
        ;; D becomes D + T1 for E

        vpaddq		T1, H, TMP3		; T1 = H + Kt
        vmovdqa32	TMP0, E
        ;; compute BIG_SIGMA_1(E)
        vprorq		TMP1, E, BIG_SIGMA_1_0 		; ROR_14(E)
        vprorq		TMP2, E, BIG_SIGMA_1_1		; ROR_18(E)
        vprorq		TMP3, E, BIG_SIGMA_1_2		; ROR_41(E)
        vpternlogq	TMP1, TMP2, TMP3, 0x96	; TMP1 = BIG_SIGMA_1(E)
        vpternlogq	TMP0, F, G, 0xCA	; TMP0 = CH(E,F,G)
        vpaddq		T1, T1, %%WT		; T1 = T1 + Wt
        vpaddq		T1, T1, TMP0		; T1 = T1 + CH(E,F,G)
        vpaddq		T1, T1, TMP1		; T1 = T1 + BIG_SIGMA_1(E)
        vpaddq		D, D, T1		; D = D + T1
        vprorq		H, A, BIG_SIGMA_0_0     ;ROR_28(A)
        vprorq		TMP2, A, BIG_SIGMA_0_1  ;ROR_34(A)
        vprorq		TMP3, A, BIG_SIGMA_0_2	;ROR_39(A)
        vmovdqa32	TMP0, A
        vpternlogq	TMP0, B, C, 0xE8	; TMP0 = MAJ(A,B,C)
        vpternlogq	H, TMP2, TMP3, 0x96	; H(T2) = BIG_SIGMA_0(A)
        vpaddq		H, H, TMP0		; H(T2) = BIG_SIGMA_0(A) + MAJ(A,B,C)
        vpaddq		H, H, T1		; H(A) = H(T2) + T1
        vmovdqa32	TMP3, [TBL + ((%%ROUND+1)*64)]	; Next Kt

        ;; Rotate the args A-H (rotation of names associated with regs)
        ROTATE_ARGS
%endmacro

%macro MSG_SCHED_ROUND_16_79 4
%define %%WT	%1
%define %%WTp1	%2
%define %%WTp9	%3
%define %%WTp14	%4
        vprorq		TMP4, %%WTp14, SMALL_SIGMA_1_0 	; ROR_19(Wt-2)
        vprorq		TMP5, %%WTp14, SMALL_SIGMA_1_1 	; ROR_61(Wt-2)
        vpsrlq		TMP6, %%WTp14, SMALL_SIGMA_1_2 	; SHR_6(Wt-2)
        vpternlogq	TMP4, TMP5, TMP6, 0x96	        ; TMP4 = sigma_1(Wt-2)

        vpaddq		%%WT, %%WT, TMP4	; Wt = Wt-16 + sigma_1(Wt-2)
        vpaddq		%%WT, %%WT, %%WTp9	; Wt = Wt-16 + sigma_1(Wt-2) + Wt-7

        vprorq		TMP4, %%WTp1, SMALL_SIGMA_0_0 	; ROR_1(Wt-15)
        vprorq		TMP5, %%WTp1, SMALL_SIGMA_0_1 	; ROR_8(Wt-15)
        vpsrlq		TMP6, %%WTp1, SMALL_SIGMA_0_2 	; SHR_7(Wt-15)
        vpternlogq	TMP4, TMP5, TMP6, 0x96	        ; TMP4 = sigma_0(Wt-15)

        vpaddq		%%WT, %%WT, TMP4	; Wt = Wt-16 + sigma_1(Wt-2) +
                                                ; Wt-7 + sigma_0(Wt-15) +

%endmacro

align 64

; void sha512_mb_x8_avx512(SHA512_MB_ARGS_X8, uint32_t size)
; arg 1 : pointer to input data
; arg 2 : size (in blocks) ;; assumed to be >= 1
local_func_decl(sha512_mb_x8_avx512)
sha512_mb_x8_avx512:
	endbranch
        mov	rax, rsp
        sub     rsp, STACK_SPACE
        and	rsp, ~63	; align stack to multiple of 64
        mov	[rsp + _RSP], rax
	lea	TBL,[TABLE]

    ;; Initialize digests
    vmovups	A,    [DIGEST + 0*8*8]
    vmovups	B,    [DIGEST + 1*8*8]
    vmovups	C,    [DIGEST + 2*8*8]
    vmovups	D,    [DIGEST + 3*8*8]
    vmovups	E,    [DIGEST + 4*8*8]
    vmovups	F,    [DIGEST + 5*8*8]
    vmovups	G,    [DIGEST + 6*8*8]
    vmovups	H,    [DIGEST + 7*8*8]

    xor	IDX, IDX
    ;; Read in input data address, saving them in registers because
    ;; they will serve as variables, which we shall keep incrementing
    mov	inp0, [IN + 0*8]
    mov	inp1, [IN + 1*8]
    mov	inp2, [IN + 2*8]
    mov	inp3, [IN + 3*8]
    mov	inp4, [IN + 4*8]
    mov	inp5, [IN + 5*8]
    mov	inp6, [IN + 6*8]
    mov	inp7, [IN + 7*8]

lloop:

    ;;  first half of 1024 (need to transpose before use)
    vmovups	W0,[inp0 + IDX ]
    vmovups	W1,[inp1 + IDX ]
    vmovups	W2,[inp2 + IDX ]
    vmovups	W3,[inp3 + IDX ]
    vmovups	W4,[inp4 + IDX ]
    vmovups	W5,[inp5 + IDX ]
    vmovups	W6,[inp6 + IDX ]
    vmovups	TMP0,[inp7 + IDX ]
    TRANSPOSE8  W0, W1, W2, W3, W4, W5, W6, TMP0,  W7, TMP1, TMP2, TMP3
    ;;  second half of 1024 (need to transpose before use)
    vmovups     W8,[inp0  + SZ8 + IDX ]
    vmovups	W9,[inp1  + SZ8 + IDX ]
    vmovups	W10,[inp2 + SZ8 + IDX ]
    vmovups	W11,[inp3 + SZ8 + IDX ]
    vmovups	W12,[inp4 + SZ8 + IDX ]
    vmovups	W13,[inp5 + SZ8 + IDX ]
    vmovups	W14,[inp6 + SZ8 + IDX ]
    vmovups	TMP0,[inp7 + SZ8 + IDX ]
    TRANSPOSE8  W8, W9, W10, W11, W12, W13, W14, TMP0,  W15, TMP1, TMP2, TMP3

    vmovdqa32	TMP2, [PSHUFFLE_BYTE_FLIP_MASK]

    vmovdqa32	TMP3, [TBL]	; First K

        ; Save digests for later addition
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*0], A
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*1], B
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*2], C
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*3], D
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*4], E
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*5], F
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*6], G
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*7], H

    add	IDX, 128  	; increment by message block length in bytes




%assign I 0
%rep 16
;;;  little endian to big endian
     vpshufb	APPEND(W,I), APPEND(W,I), TMP2
%assign I (I+1)
%endrep
    ; Save digests for later addition
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*0], A
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*1], B
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*2], C
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*3], D
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*4], E
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*5], F
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*6], G
    vmovdqa32	[rsp + _DIGEST_SAVE + 64*7], H

        ; MSG Schedule for W0-W15 is now complete in registers
        ; Process first (max-rounds -16)
        ; Calculate next Wt+16 after processing is complete and Wt is unneeded

        ; PROCESS_LOOP_00_79 APPEND(W,J), I, APPEND(W,K), APPEND(W,L), APPEND(W,M)

%assign I 0
%assign J 0
%assign K 1
%assign L 9
%assign M 14
%rep SHA_ROUNDS_LESS_16
        PROCESS_LOOP  APPEND(W,J),  I
        MSG_SCHED_ROUND_16_79  APPEND(W,J), APPEND(W,K), APPEND(W,L), APPEND(W,M)
%assign I (I+1)
%assign J ((J+1)% 16)
%assign K ((K+1)% 16)
%assign L ((L+1)% 16)
%assign M ((M+1)% 16)
%endrep
        ; Check is this is the last block
        sub 	SIZE, 1
        je	lastLoop

        ; Process last 16 rounds
        ; Read in next block msg data for use in first 16 words of msg sched
%assign I SHA_ROUNDS_LESS_16
%assign J 0
%rep 16
        PROCESS_LOOP  APPEND(W,J), I
%assign I (I+1)
%assign J (J+1)
%endrep
       ; Add old digest
        vpaddq		A, A, [rsp + _DIGEST_SAVE + 64*0]
        vpaddq		B, B, [rsp + _DIGEST_SAVE + 64*1]
        vpaddq		C, C, [rsp + _DIGEST_SAVE + 64*2]
        vpaddq		D, D, [rsp + _DIGEST_SAVE + 64*3]
        vpaddq		E, E, [rsp + _DIGEST_SAVE + 64*4]
        vpaddq		F, F, [rsp + _DIGEST_SAVE + 64*5]
        vpaddq		G, G, [rsp + _DIGEST_SAVE + 64*6]
        vpaddq		H, H, [rsp + _DIGEST_SAVE + 64*7]

        jmp	lloop


lastLoop:
        ; Process last 16 rounds
%assign I SHA_ROUNDS_LESS_16
%assign J 0

%rep 16
        PROCESS_LOOP  APPEND(W,J), I
%assign I (I+1)
%assign J (J+1)
%endrep

        ; Add old digest
        vpaddq		A, A, [rsp + _DIGEST_SAVE + 64*0]
        vpaddq		B, B, [rsp + _DIGEST_SAVE + 64*1]
        vpaddq		C, C, [rsp + _DIGEST_SAVE + 64*2]
        vpaddq		D, D, [rsp + _DIGEST_SAVE + 64*3]
        vpaddq		E, E, [rsp + _DIGEST_SAVE + 64*4]
        vpaddq		F, F, [rsp + _DIGEST_SAVE + 64*5]
        vpaddq		G, G, [rsp + _DIGEST_SAVE + 64*6]
        vpaddq		H, H, [rsp + _DIGEST_SAVE + 64*7]

;; update into data pointers
%assign I 0
%rep 4
        mov    inp0, [IN + (2*I)*8]
        mov    inp1, [IN + (2*I +1)*8]
        add    inp0, IDX
        add    inp1, IDX
        mov    [IN + (2*I)*8], inp0
        mov    [IN + (2*I+1)*8], inp1
%assign I (I+1)
%endrep

        VMOVDQ32	[DIGEST + 0*8*8], A
        VMOVDQ32	[DIGEST + 1*8*8], B
        VMOVDQ32	[DIGEST + 2*8*8], C
        VMOVDQ32	[DIGEST + 3*8*8], D
        VMOVDQ32	[DIGEST + 4*8*8], E
        VMOVDQ32	[DIGEST + 5*8*8], F
        VMOVDQ32	[DIGEST + 6*8*8], G
        VMOVDQ32	[DIGEST + 7*8*8], H

        mov     rsp, [rsp + _RSP]
        ret

        section .data
align 64
; 80 constants for SHA512
; replicating for each lane, thus 8*80
; to aid in SIMD .. space tradeoff for time!
; local to asm file, used nowhere else
TABLE:
    dq 0x428a2f98d728ae22, 0x428a2f98d728ae22, 0x428a2f98d728ae22, 0x428a2f98d728ae22, 0x428a2f98d728ae22, 0x428a2f98d728ae22, 0x428a2f98d728ae22, 0x428a2f98d728ae22
    dq 0x7137449123ef65cd, 0x7137449123ef65cd, 0x7137449123ef65cd, 0x7137449123ef65cd, 0x7137449123ef65cd, 0x7137449123ef65cd, 0x7137449123ef65cd, 0x7137449123ef65cd
    dq 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f
    dq 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc
    dq 0x3956c25bf348b538, 0x3956c25bf348b538, 0x3956c25bf348b538, 0x3956c25bf348b538, 0x3956c25bf348b538, 0x3956c25bf348b538, 0x3956c25bf348b538, 0x3956c25bf348b538
    dq 0x59f111f1b605d019, 0x59f111f1b605d019, 0x59f111f1b605d019, 0x59f111f1b605d019, 0x59f111f1b605d019, 0x59f111f1b605d019, 0x59f111f1b605d019, 0x59f111f1b605d019
    dq 0x923f82a4af194f9b, 0x923f82a4af194f9b, 0x923f82a4af194f9b, 0x923f82a4af194f9b, 0x923f82a4af194f9b, 0x923f82a4af194f9b, 0x923f82a4af194f9b, 0x923f82a4af194f9b
    dq 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118
    dq 0xd807aa98a3030242, 0xd807aa98a3030242, 0xd807aa98a3030242, 0xd807aa98a3030242, 0xd807aa98a3030242, 0xd807aa98a3030242, 0xd807aa98a3030242, 0xd807aa98a3030242
    dq 0x12835b0145706fbe, 0x12835b0145706fbe, 0x12835b0145706fbe, 0x12835b0145706fbe, 0x12835b0145706fbe, 0x12835b0145706fbe, 0x12835b0145706fbe, 0x12835b0145706fbe
    dq 0x243185be4ee4b28c, 0x243185be4ee4b28c, 0x243185be4ee4b28c, 0x243185be4ee4b28c, 0x243185be4ee4b28c, 0x243185be4ee4b28c, 0x243185be4ee4b28c, 0x243185be4ee4b28c
    dq 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2
    dq 0x72be5d74f27b896f, 0x72be5d74f27b896f, 0x72be5d74f27b896f, 0x72be5d74f27b896f, 0x72be5d74f27b896f, 0x72be5d74f27b896f, 0x72be5d74f27b896f, 0x72be5d74f27b896f
    dq 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1
    dq 0x9bdc06a725c71235, 0x9bdc06a725c71235, 0x9bdc06a725c71235, 0x9bdc06a725c71235, 0x9bdc06a725c71235, 0x9bdc06a725c71235, 0x9bdc06a725c71235, 0x9bdc06a725c71235
    dq 0xc19bf174cf692694, 0xc19bf174cf692694, 0xc19bf174cf692694, 0xc19bf174cf692694, 0xc19bf174cf692694, 0xc19bf174cf692694, 0xc19bf174cf692694, 0xc19bf174cf692694
    dq 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2
    dq 0xefbe4786384f25e3, 0xefbe4786384f25e3, 0xefbe4786384f25e3, 0xefbe4786384f25e3, 0xefbe4786384f25e3, 0xefbe4786384f25e3, 0xefbe4786384f25e3, 0xefbe4786384f25e3
    dq 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5
    dq 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65
    dq 0x2de92c6f592b0275, 0x2de92c6f592b0275, 0x2de92c6f592b0275, 0x2de92c6f592b0275, 0x2de92c6f592b0275, 0x2de92c6f592b0275, 0x2de92c6f592b0275, 0x2de92c6f592b0275
    dq 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483
    dq 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4
    dq 0x76f988da831153b5, 0x76f988da831153b5, 0x76f988da831153b5, 0x76f988da831153b5, 0x76f988da831153b5, 0x76f988da831153b5, 0x76f988da831153b5, 0x76f988da831153b5
    dq 0x983e5152ee66dfab, 0x983e5152ee66dfab, 0x983e5152ee66dfab, 0x983e5152ee66dfab, 0x983e5152ee66dfab, 0x983e5152ee66dfab, 0x983e5152ee66dfab, 0x983e5152ee66dfab
    dq 0xa831c66d2db43210, 0xa831c66d2db43210, 0xa831c66d2db43210, 0xa831c66d2db43210, 0xa831c66d2db43210, 0xa831c66d2db43210, 0xa831c66d2db43210, 0xa831c66d2db43210
    dq 0xb00327c898fb213f, 0xb00327c898fb213f, 0xb00327c898fb213f, 0xb00327c898fb213f, 0xb00327c898fb213f, 0xb00327c898fb213f, 0xb00327c898fb213f, 0xb00327c898fb213f
    dq 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4
    dq 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2
    dq 0xd5a79147930aa725, 0xd5a79147930aa725, 0xd5a79147930aa725, 0xd5a79147930aa725, 0xd5a79147930aa725, 0xd5a79147930aa725, 0xd5a79147930aa725, 0xd5a79147930aa725
    dq 0x06ca6351e003826f, 0x06ca6351e003826f, 0x06ca6351e003826f, 0x06ca6351e003826f, 0x06ca6351e003826f, 0x06ca6351e003826f, 0x06ca6351e003826f, 0x06ca6351e003826f
    dq 0x142929670a0e6e70, 0x142929670a0e6e70, 0x142929670a0e6e70, 0x142929670a0e6e70, 0x142929670a0e6e70, 0x142929670a0e6e70, 0x142929670a0e6e70, 0x142929670a0e6e70
    dq 0x27b70a8546d22ffc, 0x27b70a8546d22ffc, 0x27b70a8546d22ffc, 0x27b70a8546d22ffc, 0x27b70a8546d22ffc, 0x27b70a8546d22ffc, 0x27b70a8546d22ffc, 0x27b70a8546d22ffc
    dq 0x2e1b21385c26c926, 0x2e1b21385c26c926, 0x2e1b21385c26c926, 0x2e1b21385c26c926, 0x2e1b21385c26c926, 0x2e1b21385c26c926, 0x2e1b21385c26c926, 0x2e1b21385c26c926
    dq 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed
    dq 0x53380d139d95b3df, 0x53380d139d95b3df, 0x53380d139d95b3df, 0x53380d139d95b3df, 0x53380d139d95b3df, 0x53380d139d95b3df, 0x53380d139d95b3df, 0x53380d139d95b3df
    dq 0x650a73548baf63de, 0x650a73548baf63de, 0x650a73548baf63de, 0x650a73548baf63de, 0x650a73548baf63de, 0x650a73548baf63de, 0x650a73548baf63de, 0x650a73548baf63de
    dq 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8
    dq 0x81c2c92e47edaee6, 0x81c2c92e47edaee6, 0x81c2c92e47edaee6, 0x81c2c92e47edaee6, 0x81c2c92e47edaee6, 0x81c2c92e47edaee6, 0x81c2c92e47edaee6, 0x81c2c92e47edaee6
    dq 0x92722c851482353b, 0x92722c851482353b, 0x92722c851482353b, 0x92722c851482353b, 0x92722c851482353b, 0x92722c851482353b, 0x92722c851482353b, 0x92722c851482353b
    dq 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364
    dq 0xa81a664bbc423001, 0xa81a664bbc423001, 0xa81a664bbc423001, 0xa81a664bbc423001, 0xa81a664bbc423001, 0xa81a664bbc423001, 0xa81a664bbc423001, 0xa81a664bbc423001
    dq 0xc24b8b70d0f89791, 0xc24b8b70d0f89791, 0xc24b8b70d0f89791, 0xc24b8b70d0f89791, 0xc24b8b70d0f89791, 0xc24b8b70d0f89791, 0xc24b8b70d0f89791, 0xc24b8b70d0f89791
    dq 0xc76c51a30654be30, 0xc76c51a30654be30, 0xc76c51a30654be30, 0xc76c51a30654be30, 0xc76c51a30654be30, 0xc76c51a30654be30, 0xc76c51a30654be30, 0xc76c51a30654be30
    dq 0xd192e819d6ef5218, 0xd192e819d6ef5218, 0xd192e819d6ef5218, 0xd192e819d6ef5218, 0xd192e819d6ef5218, 0xd192e819d6ef5218, 0xd192e819d6ef5218, 0xd192e819d6ef5218
    dq 0xd69906245565a910, 0xd69906245565a910, 0xd69906245565a910, 0xd69906245565a910, 0xd69906245565a910, 0xd69906245565a910, 0xd69906245565a910, 0xd69906245565a910
    dq 0xf40e35855771202a, 0xf40e35855771202a, 0xf40e35855771202a, 0xf40e35855771202a, 0xf40e35855771202a, 0xf40e35855771202a, 0xf40e35855771202a, 0xf40e35855771202a
    dq 0x106aa07032bbd1b8, 0x106aa07032bbd1b8, 0x106aa07032bbd1b8, 0x106aa07032bbd1b8, 0x106aa07032bbd1b8, 0x106aa07032bbd1b8, 0x106aa07032bbd1b8, 0x106aa07032bbd1b8
    dq 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8
    dq 0x1e376c085141ab53, 0x1e376c085141ab53, 0x1e376c085141ab53, 0x1e376c085141ab53, 0x1e376c085141ab53, 0x1e376c085141ab53, 0x1e376c085141ab53, 0x1e376c085141ab53
    dq 0x2748774cdf8eeb99, 0x2748774cdf8eeb99, 0x2748774cdf8eeb99, 0x2748774cdf8eeb99, 0x2748774cdf8eeb99, 0x2748774cdf8eeb99, 0x2748774cdf8eeb99, 0x2748774cdf8eeb99
    dq 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8
    dq 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63
    dq 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb
    dq 0x5b9cca4f7763e373, 0x5b9cca4f7763e373, 0x5b9cca4f7763e373, 0x5b9cca4f7763e373, 0x5b9cca4f7763e373, 0x5b9cca4f7763e373, 0x5b9cca4f7763e373, 0x5b9cca4f7763e373
    dq 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3
    dq 0x748f82ee5defb2fc, 0x748f82ee5defb2fc, 0x748f82ee5defb2fc, 0x748f82ee5defb2fc, 0x748f82ee5defb2fc, 0x748f82ee5defb2fc, 0x748f82ee5defb2fc, 0x748f82ee5defb2fc
    dq 0x78a5636f43172f60, 0x78a5636f43172f60, 0x78a5636f43172f60, 0x78a5636f43172f60, 0x78a5636f43172f60, 0x78a5636f43172f60, 0x78a5636f43172f60, 0x78a5636f43172f60
    dq 0x84c87814a1f0ab72, 0x84c87814a1f0ab72, 0x84c87814a1f0ab72, 0x84c87814a1f0ab72, 0x84c87814a1f0ab72, 0x84c87814a1f0ab72, 0x84c87814a1f0ab72, 0x84c87814a1f0ab72
    dq 0x8cc702081a6439ec, 0x8cc702081a6439ec, 0x8cc702081a6439ec, 0x8cc702081a6439ec, 0x8cc702081a6439ec, 0x8cc702081a6439ec, 0x8cc702081a6439ec, 0x8cc702081a6439ec
    dq 0x90befffa23631e28, 0x90befffa23631e28, 0x90befffa23631e28, 0x90befffa23631e28, 0x90befffa23631e28, 0x90befffa23631e28, 0x90befffa23631e28, 0x90befffa23631e28
    dq 0xa4506cebde82bde9, 0xa4506cebde82bde9, 0xa4506cebde82bde9, 0xa4506cebde82bde9, 0xa4506cebde82bde9, 0xa4506cebde82bde9, 0xa4506cebde82bde9, 0xa4506cebde82bde9
    dq 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915
    dq 0xc67178f2e372532b, 0xc67178f2e372532b, 0xc67178f2e372532b, 0xc67178f2e372532b, 0xc67178f2e372532b, 0xc67178f2e372532b, 0xc67178f2e372532b, 0xc67178f2e372532b
    dq 0xca273eceea26619c, 0xca273eceea26619c, 0xca273eceea26619c, 0xca273eceea26619c, 0xca273eceea26619c, 0xca273eceea26619c, 0xca273eceea26619c, 0xca273eceea26619c
    dq 0xd186b8c721c0c207, 0xd186b8c721c0c207, 0xd186b8c721c0c207, 0xd186b8c721c0c207, 0xd186b8c721c0c207, 0xd186b8c721c0c207, 0xd186b8c721c0c207, 0xd186b8c721c0c207
    dq 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e
    dq 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178
    dq 0x06f067aa72176fba, 0x06f067aa72176fba, 0x06f067aa72176fba, 0x06f067aa72176fba, 0x06f067aa72176fba, 0x06f067aa72176fba, 0x06f067aa72176fba, 0x06f067aa72176fba
    dq 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6
    dq 0x113f9804bef90dae, 0x113f9804bef90dae, 0x113f9804bef90dae, 0x113f9804bef90dae, 0x113f9804bef90dae, 0x113f9804bef90dae, 0x113f9804bef90dae, 0x113f9804bef90dae
    dq 0x1b710b35131c471b, 0x1b710b35131c471b, 0x1b710b35131c471b, 0x1b710b35131c471b, 0x1b710b35131c471b, 0x1b710b35131c471b, 0x1b710b35131c471b, 0x1b710b35131c471b
    dq 0x28db77f523047d84, 0x28db77f523047d84, 0x28db77f523047d84, 0x28db77f523047d84, 0x28db77f523047d84, 0x28db77f523047d84, 0x28db77f523047d84, 0x28db77f523047d84
    dq 0x32caab7b40c72493, 0x32caab7b40c72493, 0x32caab7b40c72493, 0x32caab7b40c72493, 0x32caab7b40c72493, 0x32caab7b40c72493, 0x32caab7b40c72493, 0x32caab7b40c72493
    dq 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc
    dq 0x431d67c49c100d4c, 0x431d67c49c100d4c, 0x431d67c49c100d4c, 0x431d67c49c100d4c, 0x431d67c49c100d4c, 0x431d67c49c100d4c, 0x431d67c49c100d4c, 0x431d67c49c100d4c
    dq 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6
    dq 0x597f299cfc657e2a, 0x597f299cfc657e2a, 0x597f299cfc657e2a, 0x597f299cfc657e2a, 0x597f299cfc657e2a, 0x597f299cfc657e2a, 0x597f299cfc657e2a, 0x597f299cfc657e2a
    dq 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec
    dq 0x6c44198c4a475817, 0x6c44198c4a475817, 0x6c44198c4a475817, 0x6c44198c4a475817, 0x6c44198c4a475817, 0x6c44198c4a475817, 0x6c44198c4a475817, 0x6c44198c4a475817

align 64
; this does the big endian to little endian conversion over a quad word .. ZMM
;; shuffle on ZMM is shuffle on 4 XMM size chunks, 128 bits
PSHUFFLE_BYTE_FLIP_MASK: dq 0x0001020304050607, 0x08090a0b0c0d0e0f
			 dq 0x1011121314151617, 0x18191a1b1c1d1e1f
			 dq 0x2021222324252627, 0x28292a2b2c2d2e2f
                         dq 0x3031323334353637, 0x38393a3b3c3d3e3f

align 64
TRANSPOSE8_PERM_INDEX_1: 	dq 0x0000000000000000
                                dq 0x0000000000000001
                                dq 0x0000000000000008
                                dq 0x0000000000000009
                                dq 0x0000000000000004
                                dq 0x0000000000000005
                                dq 0x000000000000000C
                                dq 0x000000000000000D

TRANSPOSE8_PERM_INDEX_2: 	dq 0x0000000000000002
                                dq 0x0000000000000003
                                dq 0x000000000000000A
                                dq 0x000000000000000B
                                dq 0x0000000000000006
                                dq 0x0000000000000007
                                dq 0x000000000000000E
                                dq 0x000000000000000F

%else
%ifidn __OUTPUT_FORMAT__, win64
global no_sha512_mb_x8_avx512
no_sha512_mb_x8_avx512:
%endif
%endif ; HAVE_AS_KNOWS_AVX512

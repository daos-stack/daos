;;
;; Copyright (c) 2017-2021, Intel Corporation
;;
;; Redistribution and use in source and binary forms, with or without
;; modification, are permitted provided that the following conditions are met:
;;
;;     * Redistributions of source code must retain the above copyright notice,
;;       this list of conditions and the following disclaimer.
;;     * Redistributions in binary form must reproduce the above copyright
;;       notice, this list of conditions and the following disclaimer in the
;;       documentation and/or other materials provided with the distribution.
;;     * Neither the name of Intel Corporation nor the names of its contributors
;;       may be used to endorse or promote products derived from this software
;;       without specific prior written permission.
;;
;; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
;; AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
;; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
;; DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
;; FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
;; DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
;; SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
;; CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
;; OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;; OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;

;; Stack must be aligned to 32 bytes before call
;;
;; Registers:		RAX RBX RCX RDX RBP RSI RDI R8  R9  R10 R11 R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Windows clobbers:	RAX         RDX         RDI R8  R9  R10 R11 R12 R13 R14 R15
;; Windows preserves:	    RBX RCX     RBP RSI
;;			-----------------------------------------------------------
;; Linux clobbers:	RAX         RDX     RSI     R8  R9  R10 R11 R12 R13 R14 R15
;; Linux preserves:	    RBX RCX     RBP     RDI
;;			-----------------------------------------------------------
;; Clobbers ZMM0-31

;; code to compute quad SHA512 using AVX512

%include "include/os.asm"
;%define DO_DBGPRINT
%include "include/dbgprint.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/transpose_avx512.asm"
%include "include/clear_regs.asm"
%include "include/cet.inc"
%define APPEND(a,b) a %+ b

%ifdef LINUX
; Linux register definitions
%define arg1 	rdi
%define arg2	rsi
%define arg3	rcx
%define arg4	rdx
%else
; Windows definitions
%define arg1 	rcx
%define arg2 	rdx
%define arg3	rsi
%define arg4	rdi
%endif

%define STATE		arg1
%define INP_SIZE	arg2

%define IDX	arg4
%define TBL	r8

;; retaining XMM_SAVE,  because the top half of YMM registers no saving required, only bottom half, the XMM part
%define NUM_LANES          8
%define XMM_SAVE           (15-5)*16
%define SZ                 8
%define SZ8	           8 * SZ
%define DIGEST_SZ          8 * SZ8
%define DIGEST_SAVE	   NUM_LANES * DIGEST_SZ
%define RSP_SAVE           1*8

; Define Stack Layout
START_FIELDS
;;;     name            size            align
FIELD	_DIGEST_SAVE,	NUM_LANES*8*64,	64
FIELD	_XMM_SAVE,	XMM_SAVE,	16
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

mksection .rodata
default rel

align 64
; 80 constants for SHA512
; replicating for each lane, thus 8*80
; to aid in SIMD .. space tradeoff for time!
; local to asm file, used nowhere else
TABLE:
	dq	0x428a2f98d728ae22, 0x428a2f98d728ae22, 0x428a2f98d728ae22, 0x428a2f98d728ae22
	dq	0x428a2f98d728ae22, 0x428a2f98d728ae22, 0x428a2f98d728ae22, 0x428a2f98d728ae22
	dq 	0x7137449123ef65cd, 0x7137449123ef65cd, 0x7137449123ef65cd, 0x7137449123ef65cd
	dq	0x7137449123ef65cd, 0x7137449123ef65cd, 0x7137449123ef65cd, 0x7137449123ef65cd
	dq	0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f
	dq	0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f
	dq	0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc
	dq	0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc
	dq	0x3956c25bf348b538, 0x3956c25bf348b538, 0x3956c25bf348b538, 0x3956c25bf348b538
	dq	0x3956c25bf348b538, 0x3956c25bf348b538, 0x3956c25bf348b538, 0x3956c25bf348b538
	dq	0x59f111f1b605d019, 0x59f111f1b605d019, 0x59f111f1b605d019, 0x59f111f1b605d019
	dq	0x59f111f1b605d019, 0x59f111f1b605d019, 0x59f111f1b605d019, 0x59f111f1b605d019
	dq	0x923f82a4af194f9b, 0x923f82a4af194f9b, 0x923f82a4af194f9b, 0x923f82a4af194f9b
	dq	0x923f82a4af194f9b, 0x923f82a4af194f9b, 0x923f82a4af194f9b, 0x923f82a4af194f9b
	dq	0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118
	dq	0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118
	dq	0xd807aa98a3030242, 0xd807aa98a3030242, 0xd807aa98a3030242, 0xd807aa98a3030242
	dq	0xd807aa98a3030242, 0xd807aa98a3030242, 0xd807aa98a3030242, 0xd807aa98a3030242
	dq	0x12835b0145706fbe, 0x12835b0145706fbe, 0x12835b0145706fbe, 0x12835b0145706fbe
	dq	0x12835b0145706fbe, 0x12835b0145706fbe, 0x12835b0145706fbe, 0x12835b0145706fbe
	dq	0x243185be4ee4b28c, 0x243185be4ee4b28c, 0x243185be4ee4b28c, 0x243185be4ee4b28c
	dq	0x243185be4ee4b28c, 0x243185be4ee4b28c, 0x243185be4ee4b28c, 0x243185be4ee4b28c
	dq	0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2
	dq	0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2
	dq	0x72be5d74f27b896f, 0x72be5d74f27b896f, 0x72be5d74f27b896f, 0x72be5d74f27b896f
	dq	0x72be5d74f27b896f, 0x72be5d74f27b896f, 0x72be5d74f27b896f, 0x72be5d74f27b896f
	dq	0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1
	dq	0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1
	dq	0x9bdc06a725c71235, 0x9bdc06a725c71235, 0x9bdc06a725c71235, 0x9bdc06a725c71235
	dq	0x9bdc06a725c71235, 0x9bdc06a725c71235, 0x9bdc06a725c71235, 0x9bdc06a725c71235
	dq	0xc19bf174cf692694, 0xc19bf174cf692694, 0xc19bf174cf692694, 0xc19bf174cf692694
	dq	0xc19bf174cf692694, 0xc19bf174cf692694, 0xc19bf174cf692694, 0xc19bf174cf692694
	dq	0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2
	dq	0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2
	dq	0xefbe4786384f25e3, 0xefbe4786384f25e3, 0xefbe4786384f25e3, 0xefbe4786384f25e3
	dq	0xefbe4786384f25e3, 0xefbe4786384f25e3, 0xefbe4786384f25e3, 0xefbe4786384f25e3
	dq	0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5
	dq	0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5
	dq	0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65
	dq	0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65
	dq	0x2de92c6f592b0275, 0x2de92c6f592b0275, 0x2de92c6f592b0275, 0x2de92c6f592b0275
	dq	0x2de92c6f592b0275, 0x2de92c6f592b0275, 0x2de92c6f592b0275, 0x2de92c6f592b0275
	dq	0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483
	dq	0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483
	dq	0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4
	dq	0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4
	dq	0x76f988da831153b5, 0x76f988da831153b5, 0x76f988da831153b5, 0x76f988da831153b5
	dq	0x76f988da831153b5, 0x76f988da831153b5, 0x76f988da831153b5, 0x76f988da831153b5
	dq	0x983e5152ee66dfab, 0x983e5152ee66dfab, 0x983e5152ee66dfab, 0x983e5152ee66dfab
	dq	0x983e5152ee66dfab, 0x983e5152ee66dfab, 0x983e5152ee66dfab, 0x983e5152ee66dfab
	dq	0xa831c66d2db43210, 0xa831c66d2db43210, 0xa831c66d2db43210, 0xa831c66d2db43210
	dq	0xa831c66d2db43210, 0xa831c66d2db43210, 0xa831c66d2db43210, 0xa831c66d2db43210
	dq	0xb00327c898fb213f, 0xb00327c898fb213f, 0xb00327c898fb213f, 0xb00327c898fb213f
	dq	0xb00327c898fb213f, 0xb00327c898fb213f, 0xb00327c898fb213f, 0xb00327c898fb213f
	dq	0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4
	dq	0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4
	dq	0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2
	dq	0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2
	dq	0xd5a79147930aa725, 0xd5a79147930aa725, 0xd5a79147930aa725, 0xd5a79147930aa725
	dq	0xd5a79147930aa725, 0xd5a79147930aa725, 0xd5a79147930aa725, 0xd5a79147930aa725
	dq	0x06ca6351e003826f, 0x06ca6351e003826f, 0x06ca6351e003826f, 0x06ca6351e003826f
	dq	0x06ca6351e003826f, 0x06ca6351e003826f, 0x06ca6351e003826f, 0x06ca6351e003826f
	dq	0x142929670a0e6e70, 0x142929670a0e6e70, 0x142929670a0e6e70, 0x142929670a0e6e70
	dq	0x142929670a0e6e70, 0x142929670a0e6e70, 0x142929670a0e6e70, 0x142929670a0e6e70
	dq	0x27b70a8546d22ffc, 0x27b70a8546d22ffc, 0x27b70a8546d22ffc, 0x27b70a8546d22ffc
	dq	0x27b70a8546d22ffc, 0x27b70a8546d22ffc, 0x27b70a8546d22ffc, 0x27b70a8546d22ffc
	dq	0x2e1b21385c26c926, 0x2e1b21385c26c926, 0x2e1b21385c26c926, 0x2e1b21385c26c926
	dq	0x2e1b21385c26c926, 0x2e1b21385c26c926, 0x2e1b21385c26c926, 0x2e1b21385c26c926
	dq	0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed
	dq	0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed
	dq	0x53380d139d95b3df, 0x53380d139d95b3df, 0x53380d139d95b3df, 0x53380d139d95b3df
	dq	0x53380d139d95b3df, 0x53380d139d95b3df, 0x53380d139d95b3df, 0x53380d139d95b3df
	dq	0x650a73548baf63de, 0x650a73548baf63de, 0x650a73548baf63de, 0x650a73548baf63de
	dq	0x650a73548baf63de, 0x650a73548baf63de, 0x650a73548baf63de, 0x650a73548baf63de
	dq	0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8
	dq	0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8
	dq	0x81c2c92e47edaee6, 0x81c2c92e47edaee6, 0x81c2c92e47edaee6, 0x81c2c92e47edaee6
	dq	0x81c2c92e47edaee6, 0x81c2c92e47edaee6, 0x81c2c92e47edaee6, 0x81c2c92e47edaee6
	dq	0x92722c851482353b, 0x92722c851482353b, 0x92722c851482353b, 0x92722c851482353b
	dq	0x92722c851482353b, 0x92722c851482353b, 0x92722c851482353b, 0x92722c851482353b
	dq	0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364
	dq	0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364
	dq	0xa81a664bbc423001, 0xa81a664bbc423001, 0xa81a664bbc423001, 0xa81a664bbc423001
	dq	0xa81a664bbc423001, 0xa81a664bbc423001, 0xa81a664bbc423001, 0xa81a664bbc423001
	dq	0xc24b8b70d0f89791, 0xc24b8b70d0f89791, 0xc24b8b70d0f89791, 0xc24b8b70d0f89791
	dq	0xc24b8b70d0f89791, 0xc24b8b70d0f89791, 0xc24b8b70d0f89791, 0xc24b8b70d0f89791
	dq	0xc76c51a30654be30, 0xc76c51a30654be30, 0xc76c51a30654be30, 0xc76c51a30654be30
	dq	0xc76c51a30654be30, 0xc76c51a30654be30, 0xc76c51a30654be30, 0xc76c51a30654be30
	dq	0xd192e819d6ef5218, 0xd192e819d6ef5218, 0xd192e819d6ef5218, 0xd192e819d6ef5218
	dq	0xd192e819d6ef5218, 0xd192e819d6ef5218, 0xd192e819d6ef5218, 0xd192e819d6ef5218
	dq	0xd69906245565a910, 0xd69906245565a910, 0xd69906245565a910, 0xd69906245565a910
	dq	0xd69906245565a910, 0xd69906245565a910, 0xd69906245565a910, 0xd69906245565a910
	dq	0xf40e35855771202a, 0xf40e35855771202a, 0xf40e35855771202a, 0xf40e35855771202a
	dq	0xf40e35855771202a, 0xf40e35855771202a, 0xf40e35855771202a, 0xf40e35855771202a
	dq	0x106aa07032bbd1b8, 0x106aa07032bbd1b8, 0x106aa07032bbd1b8, 0x106aa07032bbd1b8
	dq	0x106aa07032bbd1b8, 0x106aa07032bbd1b8, 0x106aa07032bbd1b8, 0x106aa07032bbd1b8
	dq	0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8
	dq	0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8
	dq	0x1e376c085141ab53, 0x1e376c085141ab53, 0x1e376c085141ab53, 0x1e376c085141ab53
	dq	0x1e376c085141ab53, 0x1e376c085141ab53, 0x1e376c085141ab53, 0x1e376c085141ab53
	dq	0x2748774cdf8eeb99, 0x2748774cdf8eeb99, 0x2748774cdf8eeb99, 0x2748774cdf8eeb99
	dq	0x2748774cdf8eeb99, 0x2748774cdf8eeb99, 0x2748774cdf8eeb99, 0x2748774cdf8eeb99
	dq	0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8
	dq	0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8
	dq	0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63
	dq	0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63
	dq	0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb
	dq	0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb
	dq	0x5b9cca4f7763e373, 0x5b9cca4f7763e373, 0x5b9cca4f7763e373, 0x5b9cca4f7763e373
	dq	0x5b9cca4f7763e373, 0x5b9cca4f7763e373, 0x5b9cca4f7763e373, 0x5b9cca4f7763e373
	dq	0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3
	dq	0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3
	dq	0x748f82ee5defb2fc, 0x748f82ee5defb2fc, 0x748f82ee5defb2fc, 0x748f82ee5defb2fc
	dq	0x748f82ee5defb2fc, 0x748f82ee5defb2fc, 0x748f82ee5defb2fc, 0x748f82ee5defb2fc
	dq	0x78a5636f43172f60, 0x78a5636f43172f60, 0x78a5636f43172f60, 0x78a5636f43172f60
	dq	0x78a5636f43172f60, 0x78a5636f43172f60, 0x78a5636f43172f60, 0x78a5636f43172f60
	dq	0x84c87814a1f0ab72, 0x84c87814a1f0ab72, 0x84c87814a1f0ab72, 0x84c87814a1f0ab72
	dq	0x84c87814a1f0ab72, 0x84c87814a1f0ab72, 0x84c87814a1f0ab72, 0x84c87814a1f0ab72
	dq	0x8cc702081a6439ec, 0x8cc702081a6439ec, 0x8cc702081a6439ec, 0x8cc702081a6439ec
	dq	0x8cc702081a6439ec, 0x8cc702081a6439ec, 0x8cc702081a6439ec, 0x8cc702081a6439ec
	dq	0x90befffa23631e28, 0x90befffa23631e28, 0x90befffa23631e28, 0x90befffa23631e28
	dq	0x90befffa23631e28, 0x90befffa23631e28, 0x90befffa23631e28, 0x90befffa23631e28
	dq	0xa4506cebde82bde9, 0xa4506cebde82bde9, 0xa4506cebde82bde9, 0xa4506cebde82bde9
	dq	0xa4506cebde82bde9, 0xa4506cebde82bde9, 0xa4506cebde82bde9, 0xa4506cebde82bde9
	dq	0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915
	dq	0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915
	dq	0xc67178f2e372532b, 0xc67178f2e372532b, 0xc67178f2e372532b, 0xc67178f2e372532b
	dq	0xc67178f2e372532b, 0xc67178f2e372532b, 0xc67178f2e372532b, 0xc67178f2e372532b
	dq	0xca273eceea26619c, 0xca273eceea26619c, 0xca273eceea26619c, 0xca273eceea26619c
	dq	0xca273eceea26619c, 0xca273eceea26619c, 0xca273eceea26619c, 0xca273eceea26619c
	dq	0xd186b8c721c0c207, 0xd186b8c721c0c207, 0xd186b8c721c0c207, 0xd186b8c721c0c207
	dq	0xd186b8c721c0c207, 0xd186b8c721c0c207, 0xd186b8c721c0c207, 0xd186b8c721c0c207
	dq	0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e
	dq	0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e
	dq	0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178
	dq	0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178
	dq	0x06f067aa72176fba, 0x06f067aa72176fba, 0x06f067aa72176fba, 0x06f067aa72176fba
	dq	0x06f067aa72176fba, 0x06f067aa72176fba, 0x06f067aa72176fba, 0x06f067aa72176fba
	dq	0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6
	dq	0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6
	dq	0x113f9804bef90dae, 0x113f9804bef90dae, 0x113f9804bef90dae, 0x113f9804bef90dae
	dq	0x113f9804bef90dae, 0x113f9804bef90dae, 0x113f9804bef90dae, 0x113f9804bef90dae
	dq	0x1b710b35131c471b, 0x1b710b35131c471b, 0x1b710b35131c471b, 0x1b710b35131c471b
	dq	0x1b710b35131c471b, 0x1b710b35131c471b, 0x1b710b35131c471b, 0x1b710b35131c471b
	dq	0x28db77f523047d84, 0x28db77f523047d84, 0x28db77f523047d84, 0x28db77f523047d84
	dq	0x28db77f523047d84, 0x28db77f523047d84, 0x28db77f523047d84, 0x28db77f523047d84
	dq	0x32caab7b40c72493, 0x32caab7b40c72493, 0x32caab7b40c72493, 0x32caab7b40c72493
	dq	0x32caab7b40c72493, 0x32caab7b40c72493, 0x32caab7b40c72493, 0x32caab7b40c72493
	dq	0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc
	dq	0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc
	dq	0x431d67c49c100d4c, 0x431d67c49c100d4c, 0x431d67c49c100d4c, 0x431d67c49c100d4c
	dq	0x431d67c49c100d4c, 0x431d67c49c100d4c, 0x431d67c49c100d4c, 0x431d67c49c100d4c
	dq	0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6
	dq	0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6
	dq	0x597f299cfc657e2a, 0x597f299cfc657e2a, 0x597f299cfc657e2a, 0x597f299cfc657e2a
	dq	0x597f299cfc657e2a, 0x597f299cfc657e2a, 0x597f299cfc657e2a, 0x597f299cfc657e2a
	dq	0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec
	dq	0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec
	dq	0x6c44198c4a475817, 0x6c44198c4a475817, 0x6c44198c4a475817, 0x6c44198c4a475817
	dq	0x6c44198c4a475817, 0x6c44198c4a475817, 0x6c44198c4a475817, 0x6c44198c4a475817

align 64
; this does the big endian to little endian conversion over a quad word .. ZMM
;; shuffle on ZMM is shuffle on 4 XMM size chunks, 128 bits
PSHUFFLE_BYTE_FLIP_MASK:
	;ddq 0x08090a0b0c0d0e0f0001020304050607
	dq	0x0001020304050607, 0x08090a0b0c0d0e0f
        ;ddq 0x18191a1b1c1d1e1f1011121314151617
        dq	0x1011121314151617, 0x18191a1b1c1d1e1f
        ;ddq 0x28292a2b2c2d2e2f2021222324252627
        dq	0x2021222324252627, 0x28292a2b2c2d2e2f
        ;ddq 0x38393a3b3c3d3e3f3031323334353637
        dq	0x3031323334353637, 0x38393a3b3c3d3e3f

mksection .text

;; void sha512_x8_avx512(void *input_data, UINT64 *digest[NUM_LANES], const int size)
;; arg 1 : rcx : pointer to input data
;; arg 2 : rdx : pointer to UINT64 digest[8][num_lanes]
;; arg 3 : size in message block lengths (= 128 bytes)
MKGLOBAL(sha512_x8_avx512,function,internal)
align 64
sha512_x8_avx512:
        endbranch64
        mov	rax, rsp
        sub     rsp, STACK_SPACE
        and	rsp, ~63	; align stack to multiple of 64
        mov	[rsp + _RSP], rax

	;; Initialize digests ; organized uint64 digest[8][num_lanes]; no transpose required
	;; Digest is an array of pointers to digests
	vmovdqu32	A,    [STATE + 0*SHA512_DIGEST_ROW_SIZE]
	vmovdqu32	B,    [STATE + 1*SHA512_DIGEST_ROW_SIZE]
	vmovdqu32	C,    [STATE + 2*SHA512_DIGEST_ROW_SIZE]
	vmovdqu32	D,    [STATE + 3*SHA512_DIGEST_ROW_SIZE]
	vmovdqu32	E,    [STATE + 4*SHA512_DIGEST_ROW_SIZE]
	vmovdqu32	F,    [STATE + 5*SHA512_DIGEST_ROW_SIZE]
	vmovdqu32	G,    [STATE + 6*SHA512_DIGEST_ROW_SIZE]
        vmovdqu32	H,    [STATE + 7*SHA512_DIGEST_ROW_SIZE]

	lea	TBL,[rel TABLE]
	xor	IDX, IDX
	;; Read in input data address, saving them in registers because
	;; they will serve as variables, which we shall keep incrementing
	mov	inp0, [STATE + _data_ptr_sha512 + 0*PTR_SZ]
	mov	inp1, [STATE + _data_ptr_sha512 + 1*PTR_SZ]
	mov	inp2, [STATE + _data_ptr_sha512 + 2*PTR_SZ]
	mov	inp3, [STATE + _data_ptr_sha512 + 3*PTR_SZ]
	mov	inp4, [STATE + _data_ptr_sha512 + 4*PTR_SZ]
	mov	inp5, [STATE + _data_ptr_sha512 + 5*PTR_SZ]
	mov	inp6, [STATE + _data_ptr_sha512 + 6*PTR_SZ]
	mov	inp7, [STATE + _data_ptr_sha512 + 7*PTR_SZ]
	jmp	lloop

align 32
lloop:
	;; Load 64-byte blocks of data into ZMM registers before
	;; performing a 8x8 64-bit transpose.
	;; To speed up the transpose, data is loaded in chunks of 32 bytes,
	;; interleaving data between lane X and lane X+4.
	;; This way, final shuffles between top half and bottom half
	;; of the matrix are avoided.
	TRANSPOSE8_U64_LOAD8 W0, W1, W2, W3, W4, W5, W6, W7, \
			     inp0, inp1, inp2, inp3, inp4, inp5, \
			     inp6, inp7, IDX

	TRANSPOSE8_U64 W0, W1, W2, W3, W4, W5, W6, W7, TMP0, TMP1, TMP2, TMP3
	;;  Load next 512 bytes
	TRANSPOSE8_U64_LOAD8 W8, W9, W10, W11, W12, W13, W14, W15, \
			     inp0, inp1, inp2, inp3, inp4, inp5, \
			     inp6, inp7, IDX+SZ8

	TRANSPOSE8_U64 W8, W9, W10, W11, W12, W13, W14, W15, TMP0, TMP1, TMP2, TMP3

	vmovdqa32	TMP2, [rel PSHUFFLE_BYTE_FLIP_MASK]

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
        sub 	INP_SIZE, 1
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

align 32
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

        ; Write out digest
        ;;  results in A, B, C, D, E, F, G, H
	vmovdqu32	[STATE + 0*SHA512_DIGEST_ROW_SIZE], A
	vmovdqu32	[STATE + 1*SHA512_DIGEST_ROW_SIZE], B
	vmovdqu32	[STATE + 2*SHA512_DIGEST_ROW_SIZE], C
	vmovdqu32	[STATE + 3*SHA512_DIGEST_ROW_SIZE], D
	vmovdqu32	[STATE + 4*SHA512_DIGEST_ROW_SIZE], E
	vmovdqu32	[STATE + 5*SHA512_DIGEST_ROW_SIZE], F
	vmovdqu32	[STATE + 6*SHA512_DIGEST_ROW_SIZE], G
        vmovdqu32	[STATE + 7*SHA512_DIGEST_ROW_SIZE], H

	; update input pointers
%assign I 0
%rep 8
	add	[STATE + _data_ptr_sha512 + I*PTR_SZ], IDX
%assign I (I+1)
%endrep

%ifdef SAFE_DATA
        ;; Clear stack frame ((NUM_LANES*8)*64 bytes)
	clear_all_zmms_asm
%assign i 0
%rep (NUM_LANES*8)
	vmovdqa64 [rsp + i*64], zmm0
%assign i (i+1)
%endrep
%endif
        mov     rsp, [rsp + _RSP]
;hash_done:
        ret

mksection stack-noexec

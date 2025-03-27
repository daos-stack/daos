;;
;; Copyright (c) 2012-2021, Intel Corporation
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

%include "include/os.asm"

;%define DO_DBGPRINT
%include "include/dbgprint.asm"
%include "include/cet.inc"
%include "include/mb_mgr_datastruct.asm"
%include "include/clear_regs.asm"

mksection .rodata
default rel
align 16
PSHUFFLE_BYTE_FLIP_MASK: ;ddq 0x0c0d0e0f08090a0b0405060700010203
	dq 0x0405060700010203, 0x0c0d0e0f08090a0b
K00_19:                  ;ddq 0x5A8279995A8279995A8279995A827999
	dq 0x5A8279995A827999, 0x5A8279995A827999
K20_39:                  ;ddq 0x6ED9EBA16ED9EBA16ED9EBA16ED9EBA1
	dq 0x6ED9EBA16ED9EBA1, 0x6ED9EBA16ED9EBA1
K40_59:                  ;ddq 0x8F1BBCDC8F1BBCDC8F1BBCDC8F1BBCDC
	dq 0x8F1BBCDC8F1BBCDC, 0x8F1BBCDC8F1BBCDC
K60_79:                  ;ddq 0xCA62C1D6CA62C1D6CA62C1D6CA62C1D6
	dq 0xCA62C1D6CA62C1D6, 0xCA62C1D6CA62C1D6

mksection .text

;; code to compute quad SHA1 using SSE
;; derived from ...\sha1_multiple\sha1_quad4.asm
;; variation of sha1_mult2.asm : clobbers all xmm regs, rcx left intact
;; rbx, rsi, rdi, rbp, r12-r15 left intact
;; This version is not safe to call from C/C++

;; Stack must be aligned to 16 bytes before call
;; Windows clobbers:  rax         rdx             r8 r9 r10 r11
;; Windows preserves:     rbx rcx     rsi rdi rbp               r12 r13 r14 r15
;;
;; Linux clobbers:    rax             rsi         r8 r9 r10 r11
;; Linux preserves:       rbx rcx rdx     rdi rbp               r12 r13 r14 r15
;;
;; clobbers xmm0-15

; transpose r0, r1, r2, r3, t0, t1
; "transpose" data in {r0..r3} using temps {t0..t3}
; Input looks like: {r0 r1 r2 r3}
; r0 = {a3 a2 a1 a0}
; r1 = {b3 b2 b1 b0}
; r2 = {c3 c2 c1 c0}
; r3 = {d3 d2 d1 d0}
;
; output looks like: {t0 r1 r0 r3}
; t0 = {d0 c0 b0 a0}
; r1 = {d1 c1 b1 a1}
; r0 = {d2 c2 b2 a2}
; r3 = {d3 c3 b3 a3}
;
%macro TRANSPOSE 6
%define %%r0 %1
%define %%r1 %2
%define %%r2 %3
%define %%r3 %4
%define %%t0 %5
%define %%t1 %6
	movaps	%%t0, %%r0		; t0 = {a3 a2 a1 a0}
	shufps	%%t0, %%r1, 0x44	; t0 = {b1 b0 a1 a0}
	shufps	%%r0, %%r1, 0xEE	; r0 = {b3 b2 a3 a2}

	movaps	%%t1, %%r2		; t1 = {c3 c2 c1 c0}
	shufps	%%t1, %%r3, 0x44	; t1 = {d1 d0 c1 c0}
	shufps	%%r2, %%r3, 0xEE	; r2 = {d3 d2 c3 c2}

	movaps	%%r1, %%t0		; r1 = {b1 b0 a1 a0}
	shufps	%%r1, %%t1, 0xDD	; r1 = {d1 c1 b1 a1}

	movaps	%%r3, %%r0		; r3 = {b3 b2 a3 a2}
	shufps	%%r3, %%r2, 0xDD	; r3 = {d3 c3 b3 a3}

	shufps	%%r0, %%r2, 0x88	; r0 = {d2 c2 b2 a2}
	shufps	%%t0, %%t1, 0x88	; t0 = {d0 c0 b0 a0}
%endmacro
;;
;; Magic functions defined in FIPS 180-1
;;
; macro MAGIC_F0 F,B,C,D,T   ;; F = (D ^ (B & (C ^ D)))
%macro MAGIC_F0 5
%define %%regF %1
%define %%regB %2
%define %%regC %3
%define %%regD %4
%define %%regT %5
	movdqa  %%regF,%%regC
	pxor  %%regF,%%regD
	pand  %%regF,%%regB
	pxor  %%regF,%%regD
%endmacro

; macro MAGIC_F1 F,B,C,D,T   ;; F = (B ^ C ^ D)
%macro MAGIC_F1 5
%define %%regF %1
%define %%regB %2
%define %%regC %3
%define %%regD %4
%define %%regT %5
	movdqa  %%regF,%%regD
	pxor  %%regF,%%regC
	pxor  %%regF,%%regB
%endmacro

; macro MAGIC_F2 F,B,C,D,T   ;; F = ((B & C) | (B & D) | (C & D))
%macro MAGIC_F2 5
%define %%regF %1
%define %%regB %2
%define %%regC %3
%define %%regD %4
%define %%regT %5
	movdqa  %%regF,%%regB
	movdqa  %%regT,%%regB
	por   %%regF,%%regC
	pand  %%regT,%%regC
	pand  %%regF,%%regD
	por   %%regF,%%regT
%endmacro

; macro MAGIC_F3 F,B,C,D,T   ;; F = (B ^ C ^ D)
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
	movdqa	%%tmp, %%reg
	pslld	%%reg, %%imm
	psrld	%%tmp, (32-%%imm)
	por	%%reg, %%tmp
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
	paddd	%%regE,%%immCNT
	paddd	%%regE,[rsp + (%%memW * 16)]
	movdqa	%%regT,%%regA
	PROLD	%%regT,5, %%regF
	paddd	%%regE,%%regT
	%%MAGIC	%%regF,%%regB,%%regC,%%regD,%%regT      ;; FUN  = MAGIC_Fi(B,C,D)
	PROLD	%%regB,30, %%regT
	paddd	%%regE,%%regF
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
	paddd	%%regE,%%immCNT
	movdqa	W14, [rsp + ((%%memW - 14) & 15) * 16]
	pxor	W16, W14
	pxor	W16, [rsp + ((%%memW -  8) & 15) * 16]
	pxor	W16, [rsp + ((%%memW -  3) & 15) * 16]
	movdqa	%%regF, W16
	pslld	W16, 1
	psrld	%%regF, (32-1)
	por	%%regF, W16
	ROTATE_W

	movdqa	[rsp + ((%%memW - 0) & 15) * 16],%%regF
	paddd	%%regE,%%regF
	movdqa	%%regT,%%regA
	PROLD	%%regT,5, %%regF
	paddd	%%regE,%%regT
	%%MAGIC	%%regF,%%regB,%%regC,%%regD,%%regT      ;; FUN  = MAGIC_Fi(B,C,D)
	PROLD	%%regB,30, %%regT
	paddd	%%regE,%%regF
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; FRAMESZ must be an odd multiple of 8
%define FRAMESZ	16*16 + 8

%define MOVPS	movdqu

%ifdef LINUX
%define arg1	rdi
%define arg2	rsi
%else
%define arg1	rcx
%define arg2	rdx
%endif

%define inp0 r8
%define inp1 r9
%define inp2 r10
%define inp3 r11

%define IDX  rax

%define A	xmm0
%define B	xmm1
%define C	xmm2
%define D	xmm3
%define E	xmm4
%define F	xmm5 ; tmp
%define G       xmm6 ; tmp

%define TMP	G
%define FUN	F
%define K	xmm7

%define AA	xmm8
%define BB	xmm9
%define CC	xmm10
%define DD	xmm11
%define EE	xmm12

%define T0	xmm6
%define T1	xmm7
%define T2	xmm8
%define T3	xmm9
%define T4	xmm10
%define T5	xmm11

%define W14	xmm13
%define W15	xmm14
%define W16	xmm15

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

align 32

; XMM registers are clobbered. Saving/restoring must be done at a higher level

; void sha1_mult_sse(SHA1_ARGS *args, UINT32 size_in_blocks);
; arg 1 : rcx : pointer to args
; arg 2 : rdx : size (in blocks) ;; assumed to be >= 1
MKGLOBAL(sha1_mult_sse,function,internal)
sha1_mult_sse:

	sub	rsp, FRAMESZ

	;; Initialize digests
	movdqa	A, [arg1 + 0*SHA1_DIGEST_ROW_SIZE]
	movdqa	B, [arg1 + 1*SHA1_DIGEST_ROW_SIZE]
	movdqa	C, [arg1 + 2*SHA1_DIGEST_ROW_SIZE]
	movdqa	D, [arg1 + 3*SHA1_DIGEST_ROW_SIZE]
	movdqa	E, [arg1 + 4*SHA1_DIGEST_ROW_SIZE]
	DBGPRINTL_XMM "Sha1-SSE Incoming transposed digest", A, B, C, D, E
        ;; load input pointers
	mov	inp0,[arg1 + _data_ptr_sha1 + 0*PTR_SZ]
	mov	inp1,[arg1 + _data_ptr_sha1 + 1*PTR_SZ]
	mov	inp2,[arg1 + _data_ptr_sha1 + 2*PTR_SZ]
	mov	inp3,[arg1 + _data_ptr_sha1 + 3*PTR_SZ]
        DBGPRINTL64 "Sha1-SSE Incoming data ptrs", inp0, inp1, inp2, inp3
	xor	IDX, IDX
lloop:
	movdqa	F, [rel PSHUFFLE_BYTE_FLIP_MASK]
%assign I 0
%rep 4
	MOVPS	T2,[inp0+IDX]
	MOVPS	T1,[inp1+IDX]
	MOVPS	T4,[inp2+IDX]
	MOVPS	T3,[inp3+IDX]
	TRANSPOSE	T2, T1, T4, T3, T0, T5
        DBGPRINTL_XMM "sha1 incoming data", T0, T1, T2, T3
	pshufb	T0, F
	movdqa	[rsp+(I*4+0)*16],T0
	pshufb	T1, F
	movdqa	[rsp+(I*4+1)*16],T1
	pshufb	T2, F
	movdqa	[rsp+(I*4+2)*16],T2
	pshufb	T3, F
	movdqa	[rsp+(I*4+3)*16],T3
	add	IDX, 4*4
%assign I (I+1)
%endrep

	; save old digests
	movdqa	AA, A
	movdqa	BB, B
	movdqa	CC, C
	movdqa	DD, D
	movdqa	EE, E

;;
;; perform 0-79 steps
;;
	movdqa	K, [rel K00_19]
;; do rounds 0...15
%assign I 0
%rep 16
	SHA1_STEP_00_15 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F0
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 16...19
	movdqa	W16, [rsp + ((16 - 16) & 15) * 16]
	movdqa	W15, [rsp + ((16 - 15) & 15) * 16]
%rep 4
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F0
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 20...39
	movdqa	K, [rel K20_39]
%rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F1
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 40...59
	movdqa	K, [rel K40_59]
%rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F2
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 60...79
	movdqa	K, [rel K60_79]
%rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F3
	ROTATE_ARGS
%assign I (I+1)
%endrep

	paddd	A,AA
	paddd	B,BB
	paddd	C,CC
	paddd	D,DD
	paddd	E,EE

	sub	arg2, 1
	jne	lloop

	; write out digests
	movdqa	[arg1 + 0*SHA1_DIGEST_ROW_SIZE], A
	movdqa	[arg1 + 1*SHA1_DIGEST_ROW_SIZE], B
	movdqa	[arg1 + 2*SHA1_DIGEST_ROW_SIZE], C
	movdqa	[arg1 + 3*SHA1_DIGEST_ROW_SIZE], D
	movdqa	[arg1 + 4*SHA1_DIGEST_ROW_SIZE], E
        DBGPRINTL_XMM "Sha1 Outgoing transposed digest", A, B, C, D, E
	; update input pointers
	add	inp0, IDX
	mov	[arg1 + _data_ptr_sha1 + 0*PTR_SZ], inp0
	add	inp1, IDX
	mov	[arg1 + _data_ptr_sha1 + 1*PTR_SZ], inp1
	add	inp2, IDX
	mov	[arg1 + _data_ptr_sha1 + 2*PTR_SZ], inp2
	add	inp3, IDX
	mov	[arg1 + _data_ptr_sha1 + 3*PTR_SZ], inp3
        DBGPRINTL64 "Sha1-sse outgoing data ptrs", inp0, inp1, inp2, inp3
	;;;;;;;;;;;;;;;;
	;; Postamble

        ;; Clear stack frame (16*16 bytes)
%ifdef SAFE_DATA
        pxor    xmm0, xmm0
%assign i 0
%rep 16
        movdqa	[rsp + i*16], xmm0
%assign i (i+1)
%endrep
	clear_all_xmms_sse_asm
%endif

	add	rsp, FRAMESZ

	ret

mksection stack-noexec

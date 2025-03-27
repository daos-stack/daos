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

;; code to compute SHA512 by-2 using AVX
;; outer calling routine takes care of save and restore of XMM registers
;; Logic designed/laid out by JDG

;; Function clobbers: rax, rcx, rdx,   rbx, rsi, rdi, r9-r15; ymm0-15
;; Stack must be aligned to 16 bytes before call
;; Windows clobbers:  rax	  rdx		  r8 r9 r10 r11
;; Windows preserves:	  rbx rcx     rsi rdi rbp		r12 r13 r14 r15
;;
;; Linux clobbers:    rax	      rsi	  r8 r9 r10 r11
;; Linux preserves:	  rbx rcx rdx	  rdi rbp		r12 r13 r14 r15
;;
;; clobbers xmm0-15

%include "include/os.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/clear_regs.asm"
extern K512_2

mksection .rodata
default rel

align 32
; one from sha512_rorx
; this does the big endian to little endian conversion
; over a quad word
PSHUFFLE_BYTE_FLIP_MASK: ;ddq 0x08090a0b0c0d0e0f0001020304050607
	dq 0x0001020304050607, 0x08090a0b0c0d0e0f
	;ddq 0x18191a1b1c1d1e1f1011121314151617
	dq 0x1011121314151617, 0x18191a1b1c1d1e1f

mksection .text

%ifdef LINUX ; Linux definitions
%define arg1	rdi
%define arg2	rsi
%else ; Windows definitions
%define arg1	rcx
%define arg2	rdx
%endif

; Common definitions
%define STATE	 arg1
%define INP_SIZE arg2

%define IDX	rax
%define ROUND	r8
%define TBL	r11

%define inp0 r9
%define inp1 r10

%define a xmm0
%define b xmm1
%define c xmm2
%define d xmm3
%define e xmm4
%define f xmm5
%define g xmm6
%define h xmm7

%define a0 xmm8
%define a1 xmm9
%define a2 xmm10

%define TT0 xmm14
%define TT1 xmm13
%define TT2 xmm12
%define TT3 xmm11
%define TT4 xmm10
%define TT5 xmm9

%define T1  xmm14
%define TMP xmm15

%define SZ2	2*SHA512_DIGEST_WORD_SIZE	; Size of one vector register
%define ROUNDS 80*SZ2

; Define stack usage

struc STACK
_DATA:		resb	SZ2 * 16
_DIGEST:	resb	SZ2 * NUM_SHA512_DIGEST_WORDS
		resb	8	; for alignment, must be odd multiple of 8
endstruc

%define VMOVPD	vmovupd

; transpose r0, r1, t0
; Input looks like {r0 r1}
; r0 = {a1 a0}
; r1 = {b1 b0}
;
; output looks like
; r0 = {b0, a0}
; t0 = {b1, a1}

%macro TRANSPOSE 3
%define %%r0 %1
%define %%r1 %2
%define %%t0 %3
	vshufpd	%%t0, %%r0, %%r1, 11b	; t0 = b1 a1
	vshufpd	%%r0, %%r0, %%r1, 00b	; r0 = b0 a0
%endm

%macro ROTATE_ARGS 0
%xdefine TMP_ h
%xdefine h g
%xdefine g f
%xdefine f e
%xdefine e d
%xdefine d c
%xdefine c b
%xdefine b a
%xdefine a TMP_
%endm

; PRORQ reg, imm, tmp
; packed-rotate-right-double
; does a rotate by doing two shifts and an or
%macro PRORQ 3
%define %%reg %1
%define %%imm %2
%define %%tmp %3
	vpsllq	%%tmp, %%reg, (64-(%%imm))
	vpsrlq	%%reg, %%reg, %%imm
	vpor	%%reg, %%reg, %%tmp
%endmacro

; non-destructive
; PRORQ_nd reg, imm, tmp, src
%macro PRORQ_nd 4
%define %%reg %1
%define %%imm %2
%define %%tmp %3
%define %%src %4
	vpsllq	%%tmp, %%src, (64-(%%imm))
	vpsrlq	%%reg, %%src, %%imm
	vpor	%%reg, %%reg, %%tmp
%endmacro

; PRORQ dst/src, amt
%macro PRORQ 2
	PRORQ	%1, %2, TMP
%endmacro

; PRORQ_nd dst, src, amt
%macro PRORQ_nd 3
	PRORQ_nd	%1, %3, TMP, %2
%endmacro

;; arguments passed implicitly in preprocessor symbols i, a...h
%macro ROUND_00_15 2
%define %%T1 %1
%define %%i  %2
	PRORQ_nd a0, e, (18-14)	; sig1: a0 = (e >> 4)

	vpxor	a2, f, g	; ch: a2 = f^g
	vpand	a2, a2, e	; ch: a2 = (f^g)&e
	vpxor	a2, a2, g	; a2 = ch

	PRORQ_nd a1, e, 41	; sig1: a1 = (e >> 41)
	vmovdqa	[SZ2*(%%i&0xf) + rsp + _DATA],%%T1
	vpaddq	%%T1,%%T1,[TBL + ROUND]	; T1 = W + K
	vpxor	a0, a0, e	; sig1: a0 = e ^ (e >> 5)
	PRORQ	a0, 14		; sig1: a0 = (e >> 14) ^ (e >> 18)
	vpaddq	h, h, a2	; h = h + ch
	PRORQ_nd a2, a, (34-28)	; sig0: a2 = (a >> 6)
	vpaddq	h, h, %%T1	; h = h + ch + W + K
	vpxor	a0, a0, a1	; a0 = sigma1
	vmovdqa	%%T1, a		; maj: T1 = a
	PRORQ_nd a1, a, 39	; sig0: a1 = (a >> 39)
	vpxor	%%T1, %%T1, c	; maj: T1 = a^c
	add	ROUND, SZ2 ; ROUND++
	vpand	%%T1, %%T1, b	; maj: T1 = (a^c)&b
	vpaddq	h, h, a0

	vpaddq	d, d, h

	vpxor	a2, a2, a	; sig0: a2 = a ^ (a >> 11)
	PRORQ	a2, 28		; sig0: a2 = (a >> 28) ^ (a >> 34)
	vpxor	a2, a2, a1	; a2 = sig0
	vpand	a1, a, c	; maj: a1 = a&c
	vpor	a1, a1, %%T1	; a1 = maj
	vpaddq	h, h, a1	; h = h + ch + W + K + maj
	vpaddq	h, h, a2	; h = h + ch + W + K + maj + sigma0
	ROTATE_ARGS

%endm

;; arguments passed implicitly in preprocessor symbols i, a...h
%macro ROUND_16_XX 2
%define %%T1 %1
%define %%i  %2
	vmovdqa	%%T1, [SZ2*((%%i-15)&0xf) + rsp + _DATA]
	vmovdqa	a1, [SZ2*((%%i-2)&0xf) + rsp + _DATA]
	vmovdqa	a0, %%T1
	PRORQ	%%T1, 8-1
	vmovdqa	a2, a1
	PRORQ	a1, 61-19
	vpxor	%%T1, %%T1, a0
	PRORQ	%%T1, 1
	vpxor	a1, a1, a2
	PRORQ	a1, 19
	vpsrlq	a0, a0, 7
	vpxor	%%T1, %%T1, a0
	vpsrlq	a2, a2, 6
	vpxor	a1, a1, a2
	vpaddq	%%T1, %%T1, [SZ2*((%%i-16)&0xf) + rsp + _DATA]
	vpaddq	a1, a1, [SZ2*((%%i-7)&0xf) + rsp + _DATA]
	vpaddq	%%T1, %%T1, a1

	ROUND_00_15 %%T1, %%i

%endm

;; SHA512_ARGS:
;;   UINT128 digest[8];	 // transposed digests
;;   UINT8  *data_ptr[2];
;;

;; void sha512_x2_avx(SHA512_ARGS *args, UINT64 msg_size_in_blocks)
;; arg 1 : STATE    : pointer args
;; arg 2 : INP_SIZE : size of data in blocks (assumed >= 1)
;;
MKGLOBAL(sha512_x2_avx,function,internal)
align 32
sha512_x2_avx:
	; general registers preserved in outer calling routine
	; outer calling routine saves all the XMM registers

	sub	rsp, STACK_size

     ;; Load the pre-transposed incoming digest.
     vmovdqa	a,[STATE + 0 * SHA512_DIGEST_ROW_SIZE]
     vmovdqa	b,[STATE + 1 * SHA512_DIGEST_ROW_SIZE]
     vmovdqa	c,[STATE + 2 * SHA512_DIGEST_ROW_SIZE]
     vmovdqa	d,[STATE + 3 * SHA512_DIGEST_ROW_SIZE]
     vmovdqa	e,[STATE + 4 * SHA512_DIGEST_ROW_SIZE]
     vmovdqa	f,[STATE + 5 * SHA512_DIGEST_ROW_SIZE]
     vmovdqa	g,[STATE + 6 * SHA512_DIGEST_ROW_SIZE]
     vmovdqa	h,[STATE + 7 * SHA512_DIGEST_ROW_SIZE]

	lea	TBL,[rel K512_2]

	;; load the address of each of the 2 message lanes
	;; getting ready to transpose input onto stack
	mov	inp0,[STATE + _data_ptr_sha512 +0*PTR_SZ]
	mov	inp1,[STATE + _data_ptr_sha512 +1*PTR_SZ]

	xor	IDX, IDX
lloop:

	xor	ROUND, ROUND

	;; save old digest
	vmovdqa	[rsp + _DIGEST + 0*SZ2], a
	vmovdqa	[rsp + _DIGEST + 1*SZ2], b
	vmovdqa	[rsp + _DIGEST + 2*SZ2], c
	vmovdqa	[rsp + _DIGEST + 3*SZ2], d
	vmovdqa	[rsp + _DIGEST + 4*SZ2], e
	vmovdqa	[rsp + _DIGEST + 5*SZ2], f
	vmovdqa	[rsp + _DIGEST + 6*SZ2], g
	vmovdqa	[rsp + _DIGEST + 7*SZ2], h

%assign i 0
%rep 8
	;; load up the shuffler for little-endian to big-endian format
	vmovdqa	TMP, [rel PSHUFFLE_BYTE_FLIP_MASK]
	VMOVPD	TT0,[inp0+IDX+i*16] ;; double precision is 64 bits
	VMOVPD	TT2,[inp1+IDX+i*16]

	TRANSPOSE	TT0, TT2, TT1
	vpshufb	TT0, TT0, TMP
	vpshufb	TT1, TT1, TMP

	ROUND_00_15	TT0,(i*2+0)
	ROUND_00_15	TT1,(i*2+1)
%assign i (i+1)
%endrep

;; Increment IDX by message block size == 8 (loop) * 16 (XMM width in bytes)
	add	IDX, 8 * 16

%assign i (i*4)

	jmp	Lrounds_16_xx
align 16
Lrounds_16_xx:
%rep 16
	ROUND_16_XX	T1, i
%assign i (i+1)
%endrep

	cmp	ROUND,ROUNDS
	jb	Lrounds_16_xx

	;; add old digest
	vpaddq	a, a, [rsp + _DIGEST + 0*SZ2]
	vpaddq	b, b, [rsp + _DIGEST + 1*SZ2]
	vpaddq	c, c, [rsp + _DIGEST + 2*SZ2]
	vpaddq	d, d, [rsp + _DIGEST + 3*SZ2]
	vpaddq	e, e, [rsp + _DIGEST + 4*SZ2]
	vpaddq	f, f, [rsp + _DIGEST + 5*SZ2]
	vpaddq	g, g, [rsp + _DIGEST + 6*SZ2]
	vpaddq	h, h, [rsp + _DIGEST + 7*SZ2]

	sub	INP_SIZE, 1 ;; consumed one message block
	jne	lloop

	; write back to memory (state object) the transposed digest
	vmovdqa	[STATE+0*SHA512_DIGEST_ROW_SIZE],a
	vmovdqa	[STATE+1*SHA512_DIGEST_ROW_SIZE],b
	vmovdqa	[STATE+2*SHA512_DIGEST_ROW_SIZE],c
	vmovdqa	[STATE+3*SHA512_DIGEST_ROW_SIZE],d
	vmovdqa	[STATE+4*SHA512_DIGEST_ROW_SIZE],e
	vmovdqa	[STATE+5*SHA512_DIGEST_ROW_SIZE],f
	vmovdqa	[STATE+6*SHA512_DIGEST_ROW_SIZE],g
	vmovdqa	[STATE+7*SHA512_DIGEST_ROW_SIZE],h

	; update input pointers
	add	inp0, IDX
	mov	[STATE + _data_ptr_sha512 + 0*PTR_SZ], inp0
	add	inp1, IDX
	mov [STATE + _data_ptr_sha512 + 1*PTR_SZ], inp1

	;;;;;;;;;;;;;;;;
	;; Postamble

        ;; Clear stack frame ((16 + 8)*16 bytes)
%ifdef SAFE_DATA
        clear_all_xmms_avx_asm
%assign i 0
%rep (16+NUM_SHA512_DIGEST_WORDS)
        vmovdqa [rsp + i*SZ2], xmm0
%assign i (i+1)
%endrep
%endif

	add	rsp, STACK_size

	; outer calling routine restores XMM and other GP registers
	ret

mksection stack-noexec

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

;; code to compute quad SHA256 using AVX
;; outer calling routine takes care of save and restore of XMM registers
;; Logic designed/laid out by JDG

;; Stack must be aligned to 16 bytes before call
;; Windows clobbers:  rax rbx     rdx             r8 r9 r10 r11 r12
;; Windows preserves:         rcx     rsi rdi rbp                   r12 r14 r15
;;
;; Linux clobbers:    rax rbx         rsi         r8 r9 r10 r11 r12
;; Linux preserves:           rcx rdx     rdi rbp                   r13 r14 r15
;;
;; clobbers xmm0-15

%include "include/os.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/clear_regs.asm"

extern K256_4

%ifdef LINUX
 %define arg1 	rdi
 %define arg2	rsi
%else
 ; Windows definitions
 %define arg1 	rcx
 %define arg2 	rdx
%endif

; Common definitions
%define STATE    arg1
%define INP_SIZE arg2

%define IDX     rax
%define ROUND	rbx
%define TBL	r12

%define inp0 r8
%define inp1 r9
%define inp2 r10
%define inp3 r11

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

%define SZ4	4*SHA256_DIGEST_WORD_SIZE	; Size of one vector register
%define ROUNDS 64*SZ4

; Define stack usage
struc STACK
_DATA:		resb	SZ4 * 16
_DIGEST:	resb	SZ4 * NUM_SHA256_DIGEST_WORDS
		resb	8 	; for alignment, must be odd multiple of 8
endstruc

%define VMOVPS	vmovups

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
	vshufps	%%t0, %%r0, %%r1, 0x44	; t0 = {b1 b0 a1 a0}
	vshufps	%%r0, %%r0, %%r1, 0xEE	; r0 = {b3 b2 a3 a2}

	vshufps	%%t1, %%r2, %%r3, 0x44	; t1 = {d1 d0 c1 c0}
	vshufps	%%r2, %%r2, %%r3, 0xEE	; r2 = {d3 d2 c3 c2}

	vshufps	%%r1, %%t0, %%t1, 0xDD	; r1 = {d1 c1 b1 a1}

	vshufps	%%r3, %%r0, %%r2, 0xDD	; r3 = {d3 c3 b3 a3}

	vshufps	%%r0, %%r0, %%r2, 0x88	; r0 = {d2 c2 b2 a2}
	vshufps	%%t0, %%t0, %%t1, 0x88	; t0 = {d0 c0 b0 a0}
%endmacro

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

; PRORD reg, imm, tmp
%macro PRORD 3
%define %%reg %1
%define %%imm %2
%define %%tmp %3
	vpslld	%%tmp, %%reg, (32-(%%imm))
	vpsrld	%%reg, %%reg, %%imm
	vpor	%%reg, %%reg, %%tmp
%endmacro

; non-destructive
; PRORD_nd reg, imm, tmp, src
%macro PRORD_nd 4
%define %%reg %1
%define %%imm %2
%define %%tmp %3
%define %%src %4
	;vmovdqa	%%tmp, %%reg
	vpslld	%%tmp, %%src, (32-(%%imm))
	vpsrld	%%reg, %%src, %%imm
	vpor	%%reg, %%reg, %%tmp
%endmacro

; PRORD dst/src, amt
%macro PRORD 2
	PRORD	%1, %2, TMP
%endmacro

; PRORD_nd dst, src, amt
%macro PRORD_nd 3
	PRORD_nd	%1, %3, TMP, %2
%endmacro

;; arguments passed implicitly in preprocessor symbols i, a...h
%macro ROUND_00_15 2
%define %%T1 %1
%define %%i  %2
	PRORD_nd	a0, e, (11-6)	; sig1: a0 = (e >> 5)

	vpxor	a2, f, g	; ch: a2 = f^g
	vpand	a2, a2, e	; ch: a2 = (f^g)&e
	vpxor	a2, a2, g	; a2 = ch

	PRORD_nd	a1, e, 25		; sig1: a1 = (e >> 25)
	vmovdqa	[SZ4*(%%i&0xf) + rsp + _DATA], %%T1
	vpaddd	%%T1, %%T1, [TBL + ROUND]	; T1 = W + K
	vpxor	a0, a0, e	; sig1: a0 = e ^ (e >> 5)
	PRORD	a0, 6		; sig1: a0 = (e >> 6) ^ (e >> 11)
	vpaddd	h, h, a2	; h = h + ch
	PRORD_nd	a2, a, (13-2)	; sig0: a2 = (a >> 11)
	vpaddd	h, h, %%T1	; h = h + ch + W + K
	vpxor	a0, a0, a1	; a0 = sigma1
	PRORD_nd	a1, a, 22	; sig0: a1 = (a >> 22)
	vpxor	%%T1, a, c	; maj: T1 = a^c
	add	ROUND, SZ4	; ROUND++
	vpand	%%T1, %%T1, b	; maj: T1 = (a^c)&b
	vpaddd	h, h, a0

	vpaddd	d, d, h

	vpxor	a2, a2, a	; sig0: a2 = a ^ (a >> 11)
	PRORD	a2, 2		; sig0: a2 = (a >> 2) ^ (a >> 13)
	vpxor	a2, a2, a1	; a2 = sig0
	vpand	a1, a, c	; maj: a1 = a&c
	vpor	a1, a1, %%T1	; a1 = maj
	vpaddd	h, h, a1	; h = h + ch + W + K + maj
	vpaddd	h, h, a2	; h = h + ch + W + K + maj + sigma0

	ROTATE_ARGS
%endm

;; arguments passed implicitly in preprocessor symbols i, a...h
%macro ROUND_16_XX 2
%define %%T1 %1
%define %%i  %2
	vmovdqa	%%T1, [SZ4*((%%i-15)&0xf) + rsp + _DATA]
	vmovdqa	a1, [SZ4*((%%i-2)&0xf) + rsp + _DATA]
	vmovdqa	a0, %%T1
	PRORD	%%T1, 18-7
	vmovdqa	a2, a1
	PRORD	a1, 19-17
	vpxor	%%T1, %%T1, a0
	PRORD	%%T1, 7
	vpxor	a1, a1, a2
	PRORD	a1, 17
	vpsrld	a0, a0, 3
	vpxor	%%T1, %%T1, a0
	vpsrld	a2, a2, 10
	vpxor	a1, a1, a2
	vpaddd	%%T1, %%T1, [SZ4*((%%i-16)&0xf) + rsp + _DATA]
	vpaddd	a1, a1, [SZ4*((%%i-7)&0xf) + rsp + _DATA]
	vpaddd	%%T1, %%T1, a1

	ROUND_00_15 %%T1, %%i
%endm

mksection .rodata
default rel
align 16
PSHUFFLE_BYTE_FLIP_MASK: ;ddq 0x0c0d0e0f08090a0b0405060700010203
	dq 0x0405060700010203, 0x0c0d0e0f08090a0b

mksection .text

;; SHA256_ARGS:
;;   UINT128 digest[8];  // transposed digests
;;   UINT8  *data_ptr[4];
;;

;; void sha_256_mult_avx(SHA256_ARGS *args, UINT64 num_blocks);
;; arg 1 : STATE    : pointer args
;; arg 2 : INP_SIZE : size of data in blocks (assumed >= 1)
;;
MKGLOBAL(sha_256_mult_avx,function,internal)
align 16
sha_256_mult_avx:
	; general registers preserved in outer calling routine
	; outer calling routine saves all the XMM registers
	sub	rsp, STACK_size

	;; Load the pre-transposed incoming digest.
	vmovdqa	a,[STATE+0*SHA256_DIGEST_ROW_SIZE]
	vmovdqa	b,[STATE+1*SHA256_DIGEST_ROW_SIZE]
	vmovdqa	c,[STATE+2*SHA256_DIGEST_ROW_SIZE]
	vmovdqa	d,[STATE+3*SHA256_DIGEST_ROW_SIZE]
	vmovdqa	e,[STATE+4*SHA256_DIGEST_ROW_SIZE]
	vmovdqa	f,[STATE+5*SHA256_DIGEST_ROW_SIZE]
	vmovdqa	g,[STATE+6*SHA256_DIGEST_ROW_SIZE]
	vmovdqa	h,[STATE+7*SHA256_DIGEST_ROW_SIZE]

	lea	TBL,[rel K256_4]

	;; load the address of each of the 4 message lanes
	;; getting ready to transpose input onto stack
	mov	inp0,[STATE + _data_ptr_sha256 + 0*PTR_SZ]
	mov	inp1,[STATE + _data_ptr_sha256 + 1*PTR_SZ]
	mov	inp2,[STATE + _data_ptr_sha256 + 2*PTR_SZ]
	mov	inp3,[STATE + _data_ptr_sha256 + 3*PTR_SZ]

	xor	IDX, IDX
lloop:
	xor	ROUND, ROUND

	;; save old digest
	vmovdqa	[rsp + _DIGEST + 0*SZ4], a
	vmovdqa	[rsp + _DIGEST + 1*SZ4], b
	vmovdqa	[rsp + _DIGEST + 2*SZ4], c
	vmovdqa	[rsp + _DIGEST + 3*SZ4], d
	vmovdqa	[rsp + _DIGEST + 4*SZ4], e
	vmovdqa	[rsp + _DIGEST + 5*SZ4], f
	vmovdqa	[rsp + _DIGEST + 6*SZ4], g
	vmovdqa	[rsp + _DIGEST + 7*SZ4], h

%assign i 0
%rep 4
	vmovdqa	TMP, [rel PSHUFFLE_BYTE_FLIP_MASK]
	VMOVPS	TT2,[inp0+IDX+i*16]
	VMOVPS	TT1,[inp1+IDX+i*16]
	VMOVPS	TT4,[inp2+IDX+i*16]
	VMOVPS	TT3,[inp3+IDX+i*16]
	TRANSPOSE	TT2, TT1, TT4, TT3, TT0, TT5
	vpshufb	TT0, TT0, TMP
	vpshufb	TT1, TT1, TMP
	vpshufb	TT2, TT2, TMP
	vpshufb	TT3, TT3, TMP
	ROUND_00_15	TT0,(i*4+0)
	ROUND_00_15	TT1,(i*4+1)
	ROUND_00_15	TT2,(i*4+2)
	ROUND_00_15	TT3,(i*4+3)
%assign i (i+1)
%endrep
	add	IDX, 4*4*4

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
	vpaddd	a, a, [rsp + _DIGEST + 0*SZ4]
	vpaddd	b, b, [rsp + _DIGEST + 1*SZ4]
	vpaddd	c, c, [rsp + _DIGEST + 2*SZ4]
	vpaddd	d, d, [rsp + _DIGEST + 3*SZ4]
	vpaddd	e, e, [rsp + _DIGEST + 4*SZ4]
	vpaddd	f, f, [rsp + _DIGEST + 5*SZ4]
	vpaddd	g, g, [rsp + _DIGEST + 6*SZ4]
	vpaddd	h, h, [rsp + _DIGEST + 7*SZ4]

	sub	INP_SIZE, 1  ;; unit is blocks
	jne	lloop

	; write back to memory (state object) the transposed digest
	vmovdqa	[STATE+0*SHA256_DIGEST_ROW_SIZE],a
	vmovdqa	[STATE+1*SHA256_DIGEST_ROW_SIZE],b
	vmovdqa	[STATE+2*SHA256_DIGEST_ROW_SIZE],c
	vmovdqa	[STATE+3*SHA256_DIGEST_ROW_SIZE],d
	vmovdqa	[STATE+4*SHA256_DIGEST_ROW_SIZE],e
	vmovdqa	[STATE+5*SHA256_DIGEST_ROW_SIZE],f
	vmovdqa	[STATE+6*SHA256_DIGEST_ROW_SIZE],g
	vmovdqa	[STATE+7*SHA256_DIGEST_ROW_SIZE],h

	; update input pointers
	add	inp0, IDX
	mov	[STATE + _data_ptr_sha256 + 0*8], inp0
	add	inp1, IDX
	mov	[STATE + _data_ptr_sha256 + 1*8], inp1
	add	inp2, IDX
	mov	[STATE + _data_ptr_sha256 + 2*8], inp2
	add	inp3, IDX
	mov	[STATE + _data_ptr_sha256 + 3*8], inp3

	;;;;;;;;;;;;;;;;
	;; Postamble

%ifdef SAFE_DATA
        ;; Clear stack frame ((16 + 8)*16 bytes)
        clear_all_xmms_avx_asm
%assign i 0
%rep (16+NUM_SHA256_DIGEST_WORDS)
        vmovdqa [rsp + i*SZ4], xmm0
%assign i (i+1)
%endrep
%endif

	add	rsp, STACK_size
	; outer calling routine restores XMM and other GP registers
	ret

mksection stack-noexec

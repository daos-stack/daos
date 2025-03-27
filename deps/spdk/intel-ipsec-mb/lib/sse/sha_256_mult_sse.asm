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

;; code to compute quad SHA256 using SSE
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

;%define DO_DBGPRINT
%include "include/dbgprint.asm"

mksection .rodata
default rel
align 64
MKGLOBAL(K256_4,data,internal)
K256_4:
	dq	0x428a2f98428a2f98, 0x428a2f98428a2f98
	dq	0x7137449171374491, 0x7137449171374491
	dq	0xb5c0fbcfb5c0fbcf, 0xb5c0fbcfb5c0fbcf
	dq	0xe9b5dba5e9b5dba5, 0xe9b5dba5e9b5dba5
	dq	0x3956c25b3956c25b, 0x3956c25b3956c25b
	dq	0x59f111f159f111f1, 0x59f111f159f111f1
	dq	0x923f82a4923f82a4, 0x923f82a4923f82a4
	dq	0xab1c5ed5ab1c5ed5, 0xab1c5ed5ab1c5ed5
	dq	0xd807aa98d807aa98, 0xd807aa98d807aa98
	dq	0x12835b0112835b01, 0x12835b0112835b01
	dq	0x243185be243185be, 0x243185be243185be
	dq	0x550c7dc3550c7dc3, 0x550c7dc3550c7dc3
	dq	0x72be5d7472be5d74, 0x72be5d7472be5d74
	dq	0x80deb1fe80deb1fe, 0x80deb1fe80deb1fe
	dq	0x9bdc06a79bdc06a7, 0x9bdc06a79bdc06a7
	dq	0xc19bf174c19bf174, 0xc19bf174c19bf174
	dq	0xe49b69c1e49b69c1, 0xe49b69c1e49b69c1
	dq	0xefbe4786efbe4786, 0xefbe4786efbe4786
	dq	0x0fc19dc60fc19dc6, 0x0fc19dc60fc19dc6
	dq	0x240ca1cc240ca1cc, 0x240ca1cc240ca1cc
	dq	0x2de92c6f2de92c6f, 0x2de92c6f2de92c6f
	dq	0x4a7484aa4a7484aa, 0x4a7484aa4a7484aa
	dq	0x5cb0a9dc5cb0a9dc, 0x5cb0a9dc5cb0a9dc
	dq	0x76f988da76f988da, 0x76f988da76f988da
	dq	0x983e5152983e5152, 0x983e5152983e5152
	dq	0xa831c66da831c66d, 0xa831c66da831c66d
	dq	0xb00327c8b00327c8, 0xb00327c8b00327c8
	dq	0xbf597fc7bf597fc7, 0xbf597fc7bf597fc7
	dq	0xc6e00bf3c6e00bf3, 0xc6e00bf3c6e00bf3
	dq	0xd5a79147d5a79147, 0xd5a79147d5a79147
	dq	0x06ca635106ca6351, 0x06ca635106ca6351
	dq	0x1429296714292967, 0x1429296714292967
	dq	0x27b70a8527b70a85, 0x27b70a8527b70a85
	dq	0x2e1b21382e1b2138, 0x2e1b21382e1b2138
	dq	0x4d2c6dfc4d2c6dfc, 0x4d2c6dfc4d2c6dfc
	dq	0x53380d1353380d13, 0x53380d1353380d13
	dq	0x650a7354650a7354, 0x650a7354650a7354
	dq	0x766a0abb766a0abb, 0x766a0abb766a0abb
	dq	0x81c2c92e81c2c92e, 0x81c2c92e81c2c92e
	dq	0x92722c8592722c85, 0x92722c8592722c85
	dq	0xa2bfe8a1a2bfe8a1, 0xa2bfe8a1a2bfe8a1
	dq	0xa81a664ba81a664b, 0xa81a664ba81a664b
	dq	0xc24b8b70c24b8b70, 0xc24b8b70c24b8b70
	dq	0xc76c51a3c76c51a3, 0xc76c51a3c76c51a3
	dq	0xd192e819d192e819, 0xd192e819d192e819
	dq	0xd6990624d6990624, 0xd6990624d6990624
	dq	0xf40e3585f40e3585, 0xf40e3585f40e3585
	dq	0x106aa070106aa070, 0x106aa070106aa070
	dq	0x19a4c11619a4c116, 0x19a4c11619a4c116
	dq	0x1e376c081e376c08, 0x1e376c081e376c08
	dq	0x2748774c2748774c, 0x2748774c2748774c
	dq	0x34b0bcb534b0bcb5, 0x34b0bcb534b0bcb5
	dq	0x391c0cb3391c0cb3, 0x391c0cb3391c0cb3
	dq	0x4ed8aa4a4ed8aa4a, 0x4ed8aa4a4ed8aa4a
	dq	0x5b9cca4f5b9cca4f, 0x5b9cca4f5b9cca4f
	dq	0x682e6ff3682e6ff3, 0x682e6ff3682e6ff3
	dq	0x748f82ee748f82ee, 0x748f82ee748f82ee
	dq	0x78a5636f78a5636f, 0x78a5636f78a5636f
	dq	0x84c8781484c87814, 0x84c8781484c87814
	dq	0x8cc702088cc70208, 0x8cc702088cc70208
	dq	0x90befffa90befffa, 0x90befffa90befffa
	dq	0xa4506ceba4506ceb, 0xa4506ceba4506ceb
	dq	0xbef9a3f7bef9a3f7, 0xbef9a3f7bef9a3f7
	dq	0xc67178f2c67178f2, 0xc67178f2c67178f2
PSHUFFLE_BYTE_FLIP_MASK: ;ddq 0x0c0d0e0f08090a0b0405060700010203
	dq 0x0405060700010203, 0x0c0d0e0f08090a0b

mksection .text

%ifdef LINUX ; Linux definitions
 %define arg1 	rdi
 %define arg2	rsi
%else ; Windows definitions
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

%define MOVPS	movups

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
	movdqa	%%tmp, %%reg
	psrld	%%reg, %%imm
	pslld	%%tmp, (32-(%%imm))
	por	%%reg, %%tmp
%endmacro

%macro PRORD 2
	PRORD	%1, %2, TMP
%endmacro

;; arguments passed implicitly in preprocessor symbols i, a...h
%macro ROUND_00_15 2
%define %%T1 %1
%define %%i  %2
	movdqa	a0, e		; sig1: a0 = e
	movdqa	a1, e		; sig1: s1 = e
	PRORD	a0, (11-6)	; sig1: a0 = (e >> 5)

	movdqa	a2, f		; ch: a2 = f
	pxor	a2, g		; ch: a2 = f^g
	pand	a2, e		; ch: a2 = (f^g)&e
	pxor	a2, g		; a2 = ch

	PRORD	a1, 25		; sig1: a1 = (e >> 25)
	movdqa	[SZ4*(%%i&0xf) + rsp],%%T1
	paddd	%%T1,[TBL + ROUND]	; T1 = W + K
	pxor	a0, e		; sig1: a0 = e ^ (e >> 5)
	PRORD	a0, 6		; sig1: a0 = (e >> 6) ^ (e >> 11)
	paddd	h, a2		; h = h + ch
	movdqa	a2, a		; sig0: a2 = a
	PRORD	a2, (13-2)	; sig0: a2 = (a >> 11)
	paddd	h, %%T1		; h = h + ch + W + K
	pxor	a0, a1		; a0 = sigma1
	movdqa	a1, a		; sig0: a1 = a
	movdqa	%%T1, a		; maj: T1 = a
	PRORD	a1, 22		; sig0: a1 = (a >> 22)
	pxor	%%T1, c		; maj: T1 = a^c
	add	ROUND, SZ4	; ROUND++
	pand	%%T1, b		; maj: T1 = (a^c)&b
	paddd	h, a0

	paddd	d, h

	pxor	a2, a		; sig0: a2 = a ^ (a >> 11)
	PRORD	a2, 2		; sig0: a2 = (a >> 2) ^ (a >> 13)
	pxor	a2, a1		; a2 = sig0
	movdqa	a1, a		; maj: a1 = a
	pand	a1, c		; maj: a1 = a&c
	por	a1, %%T1	; a1 = maj
	paddd	h, a1		; h = h + ch + W + K + maj
	paddd	h, a2		; h = h + ch + W + K + maj + sigma0

	ROTATE_ARGS
%endm

;; arguments passed implicitly in preprocessor symbols i, a...h
%macro ROUND_16_XX 2
%define %%T1 %1
%define %%i  %2
	movdqa	%%T1, [SZ4*((%%i-15)&0xf) + rsp]
	movdqa	a1, [SZ4*((%%i-2)&0xf) + rsp]
	movdqa	a0, %%T1
	PRORD	%%T1, 18-7
	movdqa	a2, a1
	PRORD	a1, 19-17
	pxor	%%T1, a0
	PRORD	%%T1, 7
	pxor	a1, a2
	PRORD	a1, 17
	psrld	a0, 3
	pxor	%%T1, a0
	psrld	a2, 10
	pxor	a1, a2
	paddd	%%T1, [SZ4*((%%i-16)&0xf) + rsp]
	paddd	a1, [SZ4*((%%i-7)&0xf) + rsp]
	paddd	%%T1, a1

	ROUND_00_15 %%T1, %%i
%endm

;; SHA256_ARGS:
;;   UINT128 digest[8];  // transposed digests
;;   UINT8  *data_ptr[4];
;;

;; void sha_256_mult_sse(SHA256_ARGS *args, UINT64 num_blocks);
;; arg 1 : STATE    : pointer args
;; arg 2 : INP_SIZE : size of data in blocks (assumed >= 1)
;;
MKGLOBAL(sha_256_mult_sse,function,internal)
align 32
sha_256_mult_sse:
	; general registers preserved in outer calling routine
	; outer calling routine saves all the XMM registers
	sub	rsp, STACK_size

	;; Load the pre-transposed incoming digest.
	movdqa	a,[STATE + 0 * SHA256_DIGEST_ROW_SIZE ]
	movdqa	b,[STATE + 1 * SHA256_DIGEST_ROW_SIZE ]
	movdqa	c,[STATE + 2 * SHA256_DIGEST_ROW_SIZE ]
	movdqa	d,[STATE + 3 * SHA256_DIGEST_ROW_SIZE ]
	movdqa	e,[STATE + 4 * SHA256_DIGEST_ROW_SIZE ]
	movdqa	f,[STATE + 5 * SHA256_DIGEST_ROW_SIZE ]
	movdqa	g,[STATE + 6 * SHA256_DIGEST_ROW_SIZE ]
	movdqa	h,[STATE + 7 * SHA256_DIGEST_ROW_SIZE ]

        DBGPRINTL_XMM "incoming transposed sha256 digest", a, b, c, d, e, f, g, h
	lea	TBL,[rel K256_4]

	;; load the address of each of the 4 message lanes
	;; getting ready to transpose input onto stack
	mov	inp0,[STATE + _data_ptr_sha256 + 0*PTR_SZ]
	mov	inp1,[STATE + _data_ptr_sha256 + 1*PTR_SZ]
	mov	inp2,[STATE + _data_ptr_sha256 + 2*PTR_SZ]
	mov	inp3,[STATE + _data_ptr_sha256 + 3*PTR_SZ]
        DBGPRINTL64 "incoming input data ptrs ", inp0, inp1, inp2, inp3
	xor	IDX, IDX
lloop:
	xor	ROUND, ROUND

	;; save old digest
	movdqa	[rsp + _DIGEST + 0*SZ4], a
	movdqa	[rsp + _DIGEST + 1*SZ4], b
	movdqa	[rsp + _DIGEST + 2*SZ4], c
	movdqa	[rsp + _DIGEST + 3*SZ4], d
	movdqa	[rsp + _DIGEST + 4*SZ4], e
	movdqa	[rsp + _DIGEST + 5*SZ4], f
	movdqa	[rsp + _DIGEST + 6*SZ4], g
	movdqa	[rsp + _DIGEST + 7*SZ4], h

%assign i 0
%rep 4
	movdqa	TMP, [rel PSHUFFLE_BYTE_FLIP_MASK]
	MOVPS	TT2,[inp0+IDX+i*16]
	MOVPS	TT1,[inp1+IDX+i*16]
	MOVPS	TT4,[inp2+IDX+i*16]
	MOVPS	TT3,[inp3+IDX+i*16]
	TRANSPOSE	TT2, TT1, TT4, TT3, TT0, TT5
	pshufb	TT0, TMP
	pshufb	TT1, TMP
	pshufb	TT2, TMP
	pshufb	TT3, TMP
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
	paddd	a, [rsp + _DIGEST + 0*SZ4]
	paddd	b, [rsp + _DIGEST + 1*SZ4]
	paddd	c, [rsp + _DIGEST + 2*SZ4]
	paddd	d, [rsp + _DIGEST + 3*SZ4]
	paddd	e, [rsp + _DIGEST + 4*SZ4]
	paddd	f, [rsp + _DIGEST + 5*SZ4]
	paddd	g, [rsp + _DIGEST + 6*SZ4]
	paddd	h, [rsp + _DIGEST + 7*SZ4]

	sub	INP_SIZE, 1  ;; unit is blocks
	jne	lloop

	; write back to memory (state object) the transposed digest
	movdqa	[STATE+0*SHA256_DIGEST_ROW_SIZE ],a
	movdqa	[STATE+1*SHA256_DIGEST_ROW_SIZE ],b
	movdqa	[STATE+2*SHA256_DIGEST_ROW_SIZE ],c
	movdqa	[STATE+3*SHA256_DIGEST_ROW_SIZE ],d
	movdqa	[STATE+4*SHA256_DIGEST_ROW_SIZE ],e
	movdqa	[STATE+5*SHA256_DIGEST_ROW_SIZE ],f
	movdqa	[STATE+6*SHA256_DIGEST_ROW_SIZE ],g
	movdqa	[STATE+7*SHA256_DIGEST_ROW_SIZE ],h
	DBGPRINTL_XMM "updated transposed sha256 digest", a, b, c, d, e, f, g, h

	; update input pointers
	add	inp0, IDX
	mov	[STATE + _data_ptr_sha256 + 0*8], inp0
	add	inp1, IDX
	mov	[STATE + _data_ptr_sha256 + 1*8], inp1
	add	inp2, IDX
	mov	[STATE + _data_ptr_sha256 + 2*8], inp2
	add	inp3, IDX
	mov	[STATE + _data_ptr_sha256 + 3*8], inp3

        DBGPRINTL64 "updated input data ptrs ", inp0, inp1, inp2, inp3

	;;;;;;;;;;;;;;;;
	;; Postamble

%ifdef SAFE_DATA
        ;; Clear stack frame ((16 + 8)*16 bytes)
        clear_all_xmms_sse_asm
%assign i 0
%rep (16+NUM_SHA256_DIGEST_WORDS)
        movdqa	[rsp + i*SZ4], xmm0
%assign i (i+1)
%endrep
%endif

	add	rsp, STACK_size
	; outer calling routine restores XMM and other GP registers
	ret

mksection stack-noexec

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

;; code to compute SHA512 by-2 using SSE
;; outer calling routine takes care of save and restore of XMM registers
;; Logic designed/laid out by JDG

;; Function clobbers: rax, rcx, rdx,   rbx, rsi, rdi, r9-r15; ymm0-15
;; Stack must be aligned to 16 bytes before call
;; Windows clobbers:  rax         rdx             r8 r9 r10 r11
;; Windows preserves:     rbx rcx     rsi rdi rbp               r12 r13 r14 r15
;;
;; Linux clobbers:    rax             rsi         r8 r9 r10 r11
;; Linux preserves:       rbx rcx rdx     rdi rbp               r12 r13 r14 r15
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
MKGLOBAL(K512_2,data,internal)
K512_2:
	dq	0x428a2f98d728ae22, 0x428a2f98d728ae22
	dq	0x7137449123ef65cd, 0x7137449123ef65cd
	dq	0xb5c0fbcfec4d3b2f, 0xb5c0fbcfec4d3b2f
	dq	0xe9b5dba58189dbbc, 0xe9b5dba58189dbbc
	dq	0x3956c25bf348b538, 0x3956c25bf348b538
	dq	0x59f111f1b605d019, 0x59f111f1b605d019
	dq	0x923f82a4af194f9b, 0x923f82a4af194f9b
	dq	0xab1c5ed5da6d8118, 0xab1c5ed5da6d8118
	dq	0xd807aa98a3030242, 0xd807aa98a3030242
	dq	0x12835b0145706fbe, 0x12835b0145706fbe
	dq	0x243185be4ee4b28c, 0x243185be4ee4b28c
	dq	0x550c7dc3d5ffb4e2, 0x550c7dc3d5ffb4e2
	dq	0x72be5d74f27b896f, 0x72be5d74f27b896f
	dq	0x80deb1fe3b1696b1, 0x80deb1fe3b1696b1
	dq	0x9bdc06a725c71235, 0x9bdc06a725c71235
	dq	0xc19bf174cf692694, 0xc19bf174cf692694
	dq	0xe49b69c19ef14ad2, 0xe49b69c19ef14ad2
	dq	0xefbe4786384f25e3, 0xefbe4786384f25e3
	dq	0x0fc19dc68b8cd5b5, 0x0fc19dc68b8cd5b5
	dq	0x240ca1cc77ac9c65, 0x240ca1cc77ac9c65
	dq	0x2de92c6f592b0275, 0x2de92c6f592b0275
	dq	0x4a7484aa6ea6e483, 0x4a7484aa6ea6e483
	dq	0x5cb0a9dcbd41fbd4, 0x5cb0a9dcbd41fbd4
	dq	0x76f988da831153b5, 0x76f988da831153b5
	dq	0x983e5152ee66dfab, 0x983e5152ee66dfab
	dq	0xa831c66d2db43210, 0xa831c66d2db43210
	dq	0xb00327c898fb213f, 0xb00327c898fb213f
	dq	0xbf597fc7beef0ee4, 0xbf597fc7beef0ee4
	dq	0xc6e00bf33da88fc2, 0xc6e00bf33da88fc2
	dq	0xd5a79147930aa725, 0xd5a79147930aa725
	dq	0x06ca6351e003826f, 0x06ca6351e003826f
	dq	0x142929670a0e6e70, 0x142929670a0e6e70
	dq	0x27b70a8546d22ffc, 0x27b70a8546d22ffc
	dq	0x2e1b21385c26c926, 0x2e1b21385c26c926
	dq	0x4d2c6dfc5ac42aed, 0x4d2c6dfc5ac42aed
	dq	0x53380d139d95b3df, 0x53380d139d95b3df
	dq	0x650a73548baf63de, 0x650a73548baf63de
	dq	0x766a0abb3c77b2a8, 0x766a0abb3c77b2a8
	dq	0x81c2c92e47edaee6, 0x81c2c92e47edaee6
	dq	0x92722c851482353b, 0x92722c851482353b
	dq	0xa2bfe8a14cf10364, 0xa2bfe8a14cf10364
	dq	0xa81a664bbc423001, 0xa81a664bbc423001
	dq	0xc24b8b70d0f89791, 0xc24b8b70d0f89791
	dq	0xc76c51a30654be30, 0xc76c51a30654be30
	dq	0xd192e819d6ef5218, 0xd192e819d6ef5218
	dq	0xd69906245565a910, 0xd69906245565a910
	dq	0xf40e35855771202a, 0xf40e35855771202a
	dq	0x106aa07032bbd1b8, 0x106aa07032bbd1b8
	dq	0x19a4c116b8d2d0c8, 0x19a4c116b8d2d0c8
	dq	0x1e376c085141ab53, 0x1e376c085141ab53
	dq	0x2748774cdf8eeb99, 0x2748774cdf8eeb99
	dq	0x34b0bcb5e19b48a8, 0x34b0bcb5e19b48a8
	dq	0x391c0cb3c5c95a63, 0x391c0cb3c5c95a63
	dq	0x4ed8aa4ae3418acb, 0x4ed8aa4ae3418acb
	dq	0x5b9cca4f7763e373, 0x5b9cca4f7763e373
	dq	0x682e6ff3d6b2b8a3, 0x682e6ff3d6b2b8a3
	dq	0x748f82ee5defb2fc, 0x748f82ee5defb2fc
	dq	0x78a5636f43172f60, 0x78a5636f43172f60
	dq	0x84c87814a1f0ab72, 0x84c87814a1f0ab72
	dq	0x8cc702081a6439ec, 0x8cc702081a6439ec
	dq	0x90befffa23631e28, 0x90befffa23631e28
	dq	0xa4506cebde82bde9, 0xa4506cebde82bde9
	dq	0xbef9a3f7b2c67915, 0xbef9a3f7b2c67915
	dq	0xc67178f2e372532b, 0xc67178f2e372532b
	dq	0xca273eceea26619c, 0xca273eceea26619c
	dq	0xd186b8c721c0c207, 0xd186b8c721c0c207
	dq	0xeada7dd6cde0eb1e, 0xeada7dd6cde0eb1e
	dq	0xf57d4f7fee6ed178, 0xf57d4f7fee6ed178
	dq	0x06f067aa72176fba, 0x06f067aa72176fba
	dq	0x0a637dc5a2c898a6, 0x0a637dc5a2c898a6
	dq	0x113f9804bef90dae, 0x113f9804bef90dae
	dq	0x1b710b35131c471b, 0x1b710b35131c471b
	dq	0x28db77f523047d84, 0x28db77f523047d84
	dq	0x32caab7b40c72493, 0x32caab7b40c72493
	dq	0x3c9ebe0a15c9bebc, 0x3c9ebe0a15c9bebc
	dq	0x431d67c49c100d4c, 0x431d67c49c100d4c
	dq	0x4cc5d4becb3e42b6, 0x4cc5d4becb3e42b6
	dq	0x597f299cfc657e2a, 0x597f299cfc657e2a
	dq	0x5fcb6fab3ad6faec, 0x5fcb6fab3ad6faec
	dq	0x6c44198c4a475817, 0x6c44198c4a475817

PSHUFFLE_BYTE_FLIP_MASK: ;ddq	0x08090a0b0c0d0e0f0001020304050607
	dq 0x0001020304050607, 0x08090a0b0c0d0e0f

mksection .text

%ifdef LINUX ; Linux definitions
 %define arg1    rdi
 %define arg2    rsi
%else ; Windows definitions
 %define arg1    rcx
 %define arg2    rdx
%endif

; Common definitions
%define STATE    arg1
%define INP_SIZE arg2

%define IDX     rax
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
		resb	8 	; for alignment, must be odd multiple of 8
endstruc

%define MOVPD	movupd

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
	movapd  %%t0, %%r0		; t0 = a1 a0
	shufpd	%%r0, %%r1, 00b		; r0 = b0 a0
	shufpd	%%t0, %%r1, 11b		; t0 = b1 a1
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
	movdqa	%%tmp, %%reg
	psllq  	%%tmp, (64-(%%imm))
	psrlq	%%reg, %%imm
	por		%%reg, %%tmp
%endmacro

; PRORQ dst/src, amt
%macro PRORQ 2
	PRORQ	%1, %2, TMP
%endmacro

;; arguments passed implicitly in preprocessor symbols i, a...h
%macro ROUND_00_15 2
%define %%T1 %1
%define %%i  %2
	movdqa	a0, e		; sig1: a0 = e
	movdqa	a1, e		; sig1: s1 = e
	PRORQ	a0, (18-14)	; sig1: a0 = (e >> 4)

	movdqa	a2, f		; ch: a2 = f
	pxor	a2, g		; ch: a2 = f^g
	pand	a2, e		; ch: a2 = (f^g)&e
	pxor	a2, g		; a2 = ch

	PRORQ	a1, 41		; sig1: a1 = (e >> 41)
        movdqa	[SZ2*(%%i&0xf) + rsp],%%T1
	paddq	%%T1,[TBL + ROUND]	; T1 = W + K
	pxor	a0, e		; sig1: a0 = e ^ (e >> 5)
	PRORQ	a0, 14		; sig1: a0 = (e >> 14) ^ (e >> 18)
	paddq	h, a2		; h = h + ch
	movdqa	a2, a		; sig0: a2 = a
	PRORQ	a2, (34-28)	; sig0: a2 = (a >> 6)
	paddq	h, %%T1		; h = h + ch + W + K
	pxor	a0, a1		; a0 = sigma1
	movdqa	a1, a		; sig0: a1 = a
	movdqa	%%T1, a		; maj: T1 = a
	PRORQ	a1, 39		; sig0: a1 = (a >> 39)
	pxor	%%T1, c		; maj: T1 = a^c
	add	ROUND, SZ2	; ROUND++
	pand	%%T1, b		; maj: T1 = (a^c)&b
	paddq	h, a0

	paddq	d, h

	pxor	a2, a		; sig0: a2 = a ^ (a >> 11)
	PRORQ	a2, 28		; sig0: a2 = (a >> 28) ^ (a >> 34)
	pxor	a2, a1		; a2 = sig0
	movdqa	a1, a		; maj: a1 = a
	pand	a1, c		; maj: a1 = a&c
	por	a1, %%T1	; a1 = maj
	paddq	h, a1		; h = h + ch + W + K + maj
	paddq	h, a2		; h = h + ch + W + K + maj + sigma0

	ROTATE_ARGS
%endm

;; arguments passed implicitly in preprocessor symbols i, a...h
%macro ROUND_16_XX 2
%define %%T1 %1
%define %%i  %2
	movdqa	%%T1, [SZ2*((%%i-15)&0xf) + rsp]
	movdqa	a1, [SZ2*((%%i-2)&0xf) + rsp]
	movdqa	a0, %%T1
	PRORQ	%%T1, 8-1
	movdqa	a2, a1
	PRORQ	a1, 61-19
	pxor	%%T1, a0
	PRORQ	%%T1, 1
	pxor	a1, a2
	PRORQ	a1, 19
	psrlq	a0, 7
	pxor	%%T1, a0
	psrlq	a2, 6
	pxor	a1, a2
	paddq	%%T1, [SZ2*((%%i-16)&0xf) + rsp]
	paddq	a1, [SZ2*((%%i-7)&0xf) + rsp]
	paddq	%%T1, a1

	ROUND_00_15 %%T1, %%i
%endm

;; SHA512_ARGS:
;;   UINT128 digest[8];  // transposed digests
;;   UINT8  *data_ptr[2];
;;

;; void sha512_x2_sse(SHA512_ARGS *args, UINT64 num_blocks);
;; arg 1 : STATE    : pointer args
;; arg 2 : INP_SIZE : size of data in blocks (assumed >= 1)
;;
MKGLOBAL(sha512_x2_sse,function,internal)
align 32
sha512_x2_sse:
	; general registers preserved in outer calling routine
	; outer calling routine saves all the XMM registers
	sub	rsp, STACK_size

	;; Load the pre-transposed incoming digest.
	movdqa	a,[STATE + 0 * SHA512_DIGEST_ROW_SIZE]
	movdqa	b,[STATE + 1 * SHA512_DIGEST_ROW_SIZE]
	movdqa	c,[STATE + 2 * SHA512_DIGEST_ROW_SIZE]
	movdqa	d,[STATE + 3 * SHA512_DIGEST_ROW_SIZE]
	movdqa	e,[STATE + 4 * SHA512_DIGEST_ROW_SIZE]
	movdqa	f,[STATE + 5 * SHA512_DIGEST_ROW_SIZE]
	movdqa	g,[STATE + 6 * SHA512_DIGEST_ROW_SIZE]
	movdqa	h,[STATE + 7 * SHA512_DIGEST_ROW_SIZE]

	DBGPRINTL_XMM "incoming transposed sha512 digest", a, b, c, d, e, f, g, h
	lea	TBL,[rel K512_2]

	;; load the address of each of the 2 message lanes
	;; getting ready to transpose input onto stack
	mov	inp0,[STATE + _data_ptr_sha512  +0*PTR_SZ]
	mov	inp1,[STATE + _data_ptr_sha512  +1*PTR_SZ]

	xor	IDX, IDX
lloop:
	xor	ROUND, ROUND
	DBGPRINTL64  "lloop enter INP_SIZE ", INP_SIZE
	DBGPRINTL64 " IDX = ", IDX
	;; save old digest
	movdqa	[rsp + _DIGEST + 0*SZ2], a
	movdqa	[rsp + _DIGEST + 1*SZ2], b
	movdqa	[rsp + _DIGEST + 2*SZ2], c
	movdqa	[rsp + _DIGEST + 3*SZ2], d
	movdqa	[rsp + _DIGEST + 4*SZ2], e
	movdqa	[rsp + _DIGEST + 5*SZ2], f
	movdqa	[rsp + _DIGEST + 6*SZ2], g
	movdqa	[rsp + _DIGEST + 7*SZ2], h

	DBGPRINTL "incoming data["
%assign i 0
%rep 8
	;; load up the shuffler for little-endian to big-endian format
	movdqa	TMP, [rel PSHUFFLE_BYTE_FLIP_MASK]
	MOVPD	TT0,[inp0+IDX+i*16] ;; double precision is 64 bits
	MOVPD	TT2,[inp1+IDX+i*16]
	DBGPRINTL_XMM "input message block", TT0
	TRANSPOSE	TT0, TT2, TT1
	pshufb	TT0, TMP
	pshufb	TT1, TMP
	ROUND_00_15	TT0,(i*2+0)
	ROUND_00_15	TT1,(i*2+1)
%assign i (i+1)
%endrep
	DBGPRINTL "]"
	add	IDX, 8 * 16 ;; increment by a message block

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
	paddq	a, [rsp + _DIGEST + 0*SZ2]
	paddq	b, [rsp + _DIGEST + 1*SZ2]
	paddq	c, [rsp + _DIGEST + 2*SZ2]
	paddq	d, [rsp + _DIGEST + 3*SZ2]
	paddq	e, [rsp + _DIGEST + 4*SZ2]
	paddq	f, [rsp + _DIGEST + 5*SZ2]
	paddq	g, [rsp + _DIGEST + 6*SZ2]
	paddq	h, [rsp + _DIGEST + 7*SZ2]

	sub	INP_SIZE, 1  ;; unit is blocks
	jne	lloop

	; write back to memory (state object) the transposed digest
	movdqa	[STATE + 0*SHA512_DIGEST_ROW_SIZE],a
	movdqa	[STATE + 1*SHA512_DIGEST_ROW_SIZE],b
	movdqa	[STATE + 2*SHA512_DIGEST_ROW_SIZE],c
	movdqa	[STATE + 3*SHA512_DIGEST_ROW_SIZE],d
	movdqa	[STATE + 4*SHA512_DIGEST_ROW_SIZE],e
	movdqa	[STATE + 5*SHA512_DIGEST_ROW_SIZE],f
	movdqa	[STATE + 6*SHA512_DIGEST_ROW_SIZE],g
	movdqa	[STATE + 7*SHA512_DIGEST_ROW_SIZE],h
	DBGPRINTL_XMM "exit transposed digest ", a, b, c, d, e, f, g, h

	; update input pointers
	add	inp0, IDX
	mov	[STATE + _data_ptr_sha512  + 0*PTR_SZ], inp0
	add	inp1, IDX
	mov	[STATE + _data_ptr_sha512  + 1*PTR_SZ], inp1

	;;;;;;;;;;;;;;;;
	;; Postamble

        ;; Clear stack frame ((16 + 8)*16 bytes)
%ifdef SAFE_DATA
        clear_all_xmms_sse_asm
%assign i 0
%rep (16+NUM_SHA512_DIGEST_WORDS)
        movdqa	[rsp + i*SZ2], xmm0
%assign i (i+1)
%endrep
%endif

	add	rsp, STACK_size
DBGPRINTL "====================== exit sha512_x2_sse code =====================\n"
	ret

mksection stack-noexec

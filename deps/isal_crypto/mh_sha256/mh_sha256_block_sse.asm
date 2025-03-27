;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2017 Intel Corporation All rights reserved.
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

;; code to compute 16 SHA256 using SSE
;;

%include "reg_sizes.asm"

[bits 64]
default rel
section .text

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
 %define func(x) proc_frame x
 %macro FUNC_SAVE 0
	alloc_stack	stack_size
	save_xmm128	xmm6, 0*16
	save_xmm128	xmm7, 1*16
	save_xmm128	xmm8, 2*16
	save_xmm128	xmm9, 3*16
	save_xmm128	xmm10, 4*16
	save_xmm128	xmm11, 5*16
	save_xmm128	xmm12, 6*16
	save_xmm128	xmm13, 7*16
	save_xmm128	xmm14, 8*16
	save_xmm128	xmm15, 9*16
	save_reg	r12,  10*16 + 0*8
	save_reg	r13,  10*16 + 1*8
	save_reg	r14,  10*16 + 2*8
	save_reg	r15,  10*16 + 3*8
	save_reg	rdi,  10*16 + 4*8
	save_reg	rsi,  10*16 + 5*8
	end_prolog
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
;variables of mh_sha256
%define mh_in_p  	arg0
%define mh_digests_p 	arg1
%define mh_data_p	arg2
%define mh_segs  	tmp1
;variables used by storing segs_digests on stack
%define RSP_SAVE	tmp2
%define FRAMESZ 	4*8*16		;BYTES*DWORDS*SEGS

; Common definitions
%define ROUND	tmp4
%define TBL	tmp5

%define pref	tmp3
%macro PREFETCH_X 1
%define %%mem  %1
	prefetchnta %%mem
%endmacro
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define MOVPS  movups

%define SZ	4
%define SZ4	4*SZ
%define ROUNDS 64*SZ4

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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
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
	movdqa  %%tmp, %%reg
	psrld   %%reg, %%imm
	pslld   %%tmp, (32-(%%imm))
	por     %%reg, %%tmp
%endmacro

; PRORD dst/src, amt
%macro PRORD 2
	PRORD	%1, %2, TMP
%endmacro

;; arguments passed implicitly in preprocessor symbols i, a...h
%macro ROUND_00_15_R 3
%define %%T1 %1
%define %%i  %2
%define %%data %3

	movdqa	a0, e		; sig1: a0 = e
	movdqa	a1, e		; sig1: s1 = e
	PRORD	a0, (11-6)	; sig1: a0 = (e >> 5)

	movdqa	a2, f		; ch: a2 = f
	pxor	a2, g		; ch: a2 = f^g
	pand	a2, e		; ch: a2 = (f^g)&e
	pxor	a2, g		; a2 = ch

	PRORD	a1, 25		; sig1: a1 = (e >> 25)
	movdqa	%%T1,[SZ4*(%%i&0xf) + %%data]
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
%macro ROUND_00_15_W 3
%define %%T1 %1
%define %%i  %2
%define %%data %3

	movdqa	a0, e		; sig1: a0 = e
	movdqa	a1, e		; sig1: s1 = e
	PRORD	a0, (11-6)	; sig1: a0 = (e >> 5)

	movdqa	a2, f		; ch: a2 = f
	pxor	a2, g		; ch: a2 = f^g
	pand	a2, e		; ch: a2 = (f^g)&e
	pxor	a2, g		; a2 = ch

	PRORD	a1, 25		; sig1: a1 = (e >> 25)
	movdqa	[SZ4*(%%i&0xf) + %%data], %%T1
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
%macro ROUND_16_XX 3
%define %%T1 %1
%define %%i  %2
%define %%data %3

	movdqa	%%T1, [SZ4*((%%i-15)&0xf) + %%data]
	movdqa	a1, [SZ4*((%%i-2)&0xf) + %%data]
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
	paddd	%%T1, [SZ4*((%%i-16)&0xf) + %%data]
	paddd	a1, [SZ4*((%%i-7)&0xf) + %%data]
	paddd	%%T1, a1

	ROUND_00_15_W %%T1, %%i, %%data

%endm

;init hash digests
; segs_digests:low addr-> high_addr
; a  | b  |  c | ...|  p | (16)
; h0 | h0 | h0 | ...| h0 |    | Aa| Ab | Ac |...| Ap |
; h1 | h1 | h1 | ...| h1 |    | Ba| Bb | Bc |...| Bp |
; ....
; h7 | h7 | h7 | ...| h7 |    | Ha| Hb | Hc |...| Hp |

align 32

;void mh_sha256_block_sse(const uint8_t * input_data, uint32_t digests[SHA256_DIGEST_WORDS][HASH_SEGS],
;		uint8_t frame_buffer[MH_SHA256_BLOCK_SIZE], uint32_t num_blocks);
; arg 0 pointer to input data
; arg 1 pointer to digests, include segments digests(uint32_t digests[16][8])
; arg 2 pointer to aligned_frame_buffer which is used to save the big_endian data.
; arg 3 number  of 1KB blocks
;
mk_global mh_sha256_block_sse, function, internal
func(mh_sha256_block_sse)
	endbranch
	FUNC_SAVE
	; save rsp
	mov	RSP_SAVE, rsp

	cmp	loops, 0
	jle	.return

	; leave enough space to store segs_digests
	sub     rsp, FRAMESZ
	; align rsp to 16 Bytes needed by sse
	and	rsp, ~0x0F
	lea	TBL,[TABLE]

 %assign I 0					; copy segs_digests into stack
 %rep 8
	MOVPS  a, [mh_digests_p + I*64 + 16*0]
	MOVPS  b, [mh_digests_p + I*64 + 16*1]
	MOVPS  c, [mh_digests_p + I*64 + 16*2]
	MOVPS  d, [mh_digests_p + I*64 + 16*3]

	movdqa [rsp + I*64 + 16*0], a
	movdqa [rsp + I*64 + 16*1], b
	movdqa [rsp + I*64 + 16*2], c
	movdqa [rsp + I*64 + 16*3], d
 %assign I (I+1)
 %endrep

.block_loop:
	;transform to big-endian data and store on aligned_frame
	movdqa  TMP, [PSHUFFLE_BYTE_FLIP_MASK]
	;transform input data from DWORD*16_SEGS*8 to DWORD*4_SEGS*8*4
 %assign I 0
 %rep 16
	MOVPS   TT0,[mh_in_p + I*64+0*16]
	MOVPS   TT1,[mh_in_p + I*64+1*16]
	MOVPS   TT2,[mh_in_p + I*64+2*16]
	MOVPS   TT3,[mh_in_p + I*64+3*16]

	pshufb  TT0, TMP
	movdqa  [mh_data_p +(I)*16 +0*256],TT0
	pshufb  TT1, TMP
	movdqa  [mh_data_p +(I)*16 +1*256],TT1
	pshufb  TT2, TMP
	movdqa  [mh_data_p +(I)*16 +2*256],TT2
	pshufb  TT3, TMP
	movdqa  [mh_data_p +(I)*16 +3*256],TT3
 %assign I (I+1)
 %endrep

	mov	mh_segs, 0			;start from the first 4 segments
	mov	pref, 1024			;avoid prefetch repeadtedly
 .segs_loop:
	xor	ROUND, ROUND
	;; Initialize digests
	movdqa  a, [rsp + 0*64 + mh_segs]
	movdqa  b, [rsp + 1*64 + mh_segs]
	movdqa  c, [rsp + 2*64 + mh_segs]
	movdqa  d, [rsp + 3*64 + mh_segs]
	movdqa  e, [rsp + 4*64 + mh_segs]
	movdqa  f, [rsp + 5*64 + mh_segs]
	movdqa  g, [rsp + 6*64 + mh_segs]
	movdqa  h, [rsp + 7*64 + mh_segs]

  %assign i 0
  %rep 4
	ROUND_00_15_R	TT0, (i*4+0), mh_data_p
	ROUND_00_15_R	TT1, (i*4+1), mh_data_p
	ROUND_00_15_R	TT2, (i*4+2), mh_data_p
	ROUND_00_15_R	TT3, (i*4+3), mh_data_p
  %assign i (i+1)
  %endrep
	PREFETCH_X [mh_in_p + pref+128*0]

  %assign i 16
  %rep 48
	%if i = 48
		PREFETCH_X [mh_in_p + pref+128*1]
	%endif
	ROUND_16_XX	T1, i, mh_data_p
  %assign i (i+1)
  %endrep

	;; add old digest
	paddd	a, [rsp + 0*64 + mh_segs]
	paddd	b, [rsp + 1*64 + mh_segs]
	paddd	c, [rsp + 2*64 + mh_segs]
	paddd	d, [rsp + 3*64 + mh_segs]
	paddd	e, [rsp + 4*64 + mh_segs]
	paddd	f, [rsp + 5*64 + mh_segs]
	paddd	g, [rsp + 6*64 + mh_segs]
	paddd	h, [rsp + 7*64 + mh_segs]

	; write out digests
	movdqa  [rsp + 0*64 + mh_segs], a
	movdqa  [rsp + 1*64 + mh_segs], b
	movdqa  [rsp + 2*64 + mh_segs], c
	movdqa  [rsp + 3*64 + mh_segs], d
	movdqa  [rsp + 4*64 + mh_segs], e
	movdqa  [rsp + 5*64 + mh_segs], f
	movdqa  [rsp + 6*64 + mh_segs], g
	movdqa  [rsp + 7*64 + mh_segs], h

	add	pref,      256
	add	mh_data_p, 256
	add 	mh_segs,   16
	cmp	mh_segs,   64
	jc 	.segs_loop

	sub	mh_data_p, (1024)
	add 	mh_in_p,   (1024)
	sub     loops,     1
	jne     .block_loop

 %assign I 0					; copy segs_digests back to mh_digests_p
 %rep 8
	movdqa a, [rsp + I*64 + 16*0]
	movdqa b, [rsp + I*64 + 16*1]
	movdqa c, [rsp + I*64 + 16*2]
	movdqa d, [rsp + I*64 + 16*3]

	MOVPS  [mh_digests_p + I*64 + 16*0], a
	MOVPS  [mh_digests_p + I*64 + 16*1], b
	MOVPS  [mh_digests_p + I*64 + 16*2], c
	MOVPS  [mh_digests_p + I*64 + 16*3], d
 %assign I (I+1)
 %endrep
	mov	rsp, RSP_SAVE			; restore rsp

.return:
	FUNC_RESTORE
	ret

endproc_frame

section .data align=16

align 16
TABLE:
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
PSHUFFLE_BYTE_FLIP_MASK: dq 0x0405060700010203, 0x0c0d0e0f08090a0b


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Implement fast SHA-256 with SSSE3 instructions. (x86_64)
;
; Copyright (C) 2013 Intel Corporation.
;
; Authors:
;     James Guilford <james.guilford@intel.com>
;     Kirk Yap <kirk.s.yap@intel.com>
;     Tim Chen <tim.c.chen@linux.intel.com>
; Transcoded by:
;     Xiaodong Liu <xiaodong.liu@intel.com>
;
; This software is available to you under the OpenIB.org BSD license
; below:
;
;     Redistribution and use in source and binary forms, with or
;     without modification, are permitted provided that the following
;     conditions are met:
;
;      - Redistributions of source code must retain the above
;        copyright notice, this list of conditions and the following
;        disclaimer.
;
;      - Redistributions in binary form must reproduce the above
;        copyright notice, this list of conditions and the following
;        disclaimer in the documentation and/or other materials
;        provided with the distribution.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
; MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
; BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
; ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
; CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
; SOFTWARE.
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; This code is described in an Intel White-Paper:
; "Fast SHA-256 Implementations on Intel Architecture Processors"
;
; To find it, surf to http://www.intel.com/p/en_US/embedded
; and search for that title.
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%include "sha256_mb_mgr_datastruct.asm"
%include "reg_sizes.asm"

[bits 64]
default rel
section .text

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%ifidn __OUTPUT_FORMAT__, elf64
 ; Linux
 %define arg0  rdi
 %define arg1  rsi
%else
 ; Windows
 %define arg0   rcx
 %define arg1   rdx
%endif

%xdefine X0 xmm4
%xdefine X1 xmm5
%xdefine X2 xmm6
%xdefine X3 xmm7

%xdefine XTMP0 xmm0
%xdefine XTMP1 xmm1
%xdefine XTMP2 xmm2
%xdefine XTMP3 xmm3
%xdefine XTMP4 xmm8
%xdefine XFER xmm9

%define SHUF_00BA xmm10      ; shuffle xBxA -> 00BA
%define SHUF_DC00 xmm11      ; shuffle xDxC -> DC00
%define BYTE_FLIP_MASK xmm12

; arg index is start from 0 while mgr_flush/submit is from 1
%define MGR	arg0	; rdi or rcx
%define NBLK	arg1	; rsi or rdx
%define IDX	r8	; local variable -- consistent with caller
%define NLANX4	r10	; consistent with caller, should be r10

%define TMGR r9	; data pointer stored in stack named _TMGR
%define INP r9	; data pointer stored in stack named _INP
%define SRND r9	; clobbers INP
%define TMP r9	; local variable -- assistant to address digest

%xdefine TBL rbp
%xdefine c ecx
%xdefine d esi
%xdefine e edx
%xdefine a eax
%xdefine b ebx

%xdefine f edi
%xdefine g r12d
%xdefine h r11d

%xdefine y0 r13d
%xdefine y1 r14d
%xdefine y2 r15d


;; FRAMESZ plus pushes must be an odd multiple of 8
%define _STACK_ALIGN_SIZE 8	; 0 or 8 depends on pushes
%define _INP_END_SIZE 8
%define _INP_SIZE 8
%define _TMGR_SIZE 8
%define _XFER_SIZE 16
%define _XMM_SAVE_SIZE 0
%define _GPR_SAVE_SIZE 8*9	;rbx, rdx, rbp, (rdi, rsi), r12~r15

%define _STACK_ALIGN 0
%define _INP_END (_STACK_ALIGN  + _STACK_ALIGN_SIZE)
%define _INP (_INP_END  + _INP_END_SIZE)
%define _TMGR (_INP + _INP_SIZE)
%define _XFER (_TMGR + _TMGR_SIZE)
%define _XMM_SAVE (_XFER + _XFER_SIZE)
%define _GPR_SAVE (_XMM_SAVE + _XMM_SAVE_SIZE)
%define STACK_SIZE (_GPR_SAVE + _GPR_SAVE_SIZE)

;; assume buffers not aligned
%define    MOVDQ movdqu

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Define Macros

; addm [mem], reg
; Add reg to mem using reg-mem add and store
%macro addm 2
        add     %2, %1 ;changed
        mov     %1, %2 ;changed
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; COPY_XMM_AND_BSWAP xmm, [mem], byte_flip_mask
; Load xmm with mem and byte swap each dword
%macro COPY_XMM_AND_BSWAP 3
        MOVDQ %1, %2 ;changed
        pshufb %1, %3 ;changed
%endmacro

; rotate_Xs
; Rotate values of symbols X0...X3
%macro rotate_Xs 0
%xdefine X_ X0
%xdefine X0 X1
%xdefine X1 X2
%xdefine X2 X3
%xdefine X3 X_
%endmacro

; ROTATE_ARGS
; Rotate values of symbols a...h
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
%endmacro

%macro FOUR_ROUNDS_AND_SCHED 0
	;; compute s0 four at a time and s1 two at a time
	;; compute W[-16] + W[-7] 4 at a time
	movdqa  XTMP0, X3
	mov     y0, e 			; y0 = e
	ror     y0, (25-11)             ; y0 = e >> (25-11)
	mov     y1, a                   ; y1 = a
	palignr XTMP0, X2, 4            ; XTMP0 = W[-7]
	ror     y1, (22-13)             ; y1 = a >> (22-13)
	xor     y0, e                   ; y0 = e ^ (e >> (25-11))
	mov     y2, f                   ; y2 = f
	ror     y0, (11-6)              ; y0 = (e >> (11-6)) ^ (e >> (25-6))
	movdqa  XTMP1, X1
	xor     y1, a                   ; y1 = a ^ (a >> (22-13)
	xor     y2, g                   ; y2 = f^g
	paddd   XTMP0, X0               ; XTMP0 = W[-7] + W[-16]
	xor     y0, e                   ; y0 = e ^ (e >> (11-6)) ^ (e >> (25-6))
	and     y2, e                   ; y2 = (f^g)&e
	ror     y1, (13-2)              ; y1 = (a >> (13-2)) ^ (a >> (22-2))
	;; compute s0
	palignr XTMP1, X0, 4            ; XTMP1 = W[-15]
	xor     y1, a                   ; y1 = a ^ (a >> (13-2)) ^ (a >> (22-2))
	ror     y0, 6                   ; y0 = S1 = (e>>6) & (e>>11) ^ (e>>25)
	xor     y2, g                   ; y2 = CH = ((f^g)&e)^g
	movdqa  XTMP2, XTMP1            ; XTMP2 = W[-15]
	ror     y1, 2                   ; y1 = S0 = (a>>2) ^ (a>>13) ^ (a>>22)
	add     y2, y0                  ; y2 = S1 + CH
	add     y2 , [rsp + _XFER]      ; y2 = k + w + S1 + CH
	movdqa  XTMP3, XTMP1            ; XTMP3 = W[-15]
	mov     y0, a                   ; y0 = a
	add     h, y2                   ; h = h + S1 + CH + k + w
	mov     y2, a                   ; y2 = a
	pslld   XTMP1, (32-7)           ;
	or      y0, c                   ; y0 = a|c
	add     d, h                    ; d = d + h + S1 + CH + k + w
	and     y2, c                   ; y2 = a&c
	psrld   XTMP2, 7                ;
	and     y0, b                   ; y0 = (a|c)&b
	add     h, y1                   ; h = h + S1 + CH + k + w + S0
	por     XTMP1, XTMP2            ; XTMP1 = W[-15] ror 7
	or      y0, y2                  ; y0 = MAJ = (a|c)&b)|(a&c)
	add     h, y0                   ; h = h + S1 + CH + k + w + S0 + MAJ

	ROTATE_ARGS
	movdqa  XTMP2, XTMP3            ; XTMP2 = W[-15]
	mov     y0, e                   ; y0 = e
	mov     y1, a                   ; y1 = a
	movdqa  XTMP4, XTMP3            ; XTMP4 = W[-15]
	ror     y0, (25-11)             ; y0 = e >> (25-11)
	xor     y0, e                   ; y0 = e ^ (e >> (25-11))
	mov     y2, f                   ; y2 = f
	ror     y1, (22-13)             ; y1 = a >> (22-13)
	pslld   XTMP3, (32-18)          ;
	xor     y1, a                   ; y1 = a ^ (a >> (22-13)
	ror     y0, (11-6)              ; y0 = (e >> (11-6)) ^ (e >> (25-6))
	xor     y2, g                   ; y2 = f^g
	psrld   XTMP2, 18               ;
	ror     y1, (13-2)              ; y1 = (a >> (13-2)) ^ (a >> (22-2))
	xor     y0, e                   ; y0 = e ^ (e >> (11-6)) ^ (e >> (25-6))
	and     y2, e                   ; y2 = (f^g)&e
	ror     y0, 6                   ; y0 = S1 = (e>>6) & (e>>11) ^ (e>>25)
	pxor    XTMP1, XTMP3
	xor     y1, a                   ; y1 = a ^ (a >> (13-2)) ^ (a >> (22-2))
	xor     y2, g                   ; y2 = CH = ((f^g)&e)^g
	psrld   XTMP4, 3                ; XTMP4 = W[-15] >> 3
	add     y2, y0                  ; y2 = S1 + CH
	add     y2, [rsp + (1*4 + _XFER)] ; y2 = k + w + S1 + CH
	ror     y1, 2                   ; y1 = S0 = (a>>2) ^ (a>>13) ^ (a>>22)
	pxor    XTMP1, XTMP2            ; XTMP1 = W[-15] ror 7 ^ W[-15] ror 18
	mov     y0, a                   ; y0 = a
	add     h, y2                   ; h = h + S1 + CH + k + w
	mov     y2, a                   ; y2 = a
	pxor    XTMP1, XTMP4            ; XTMP1 = s0
	or      y0, c                   ; y0 = a|c
	add     d, h                    ; d = d + h + S1 + CH + k + w
	and     y2, c                   ; y2 = a&c
	;; compute low s1
	pshufd  XTMP2, X3, 11111010B    ; XTMP2 = W[-2] {BBAA}
	and     y0, b 			; y0 = (a|c)&b
	add     h, y1                   ; h = h + S1 + CH + k + w + S0
	paddd   XTMP0, XTMP1            ; XTMP0 = W[-16] + W[-7] + s0
	or      y0, y2                  ; y0 = MAJ = (a|c)&b)|(a&c)
	add     h, y0                   ; h = h + S1 + CH + k + w + S0 + MAJ

	ROTATE_ARGS
	movdqa  XTMP3, XTMP2            ; XTMP3 = W[-2] {BBAA}
	mov     y0, e                   ; y0 = e
	mov     y1, a                   ; y1 = a
	ror     y0, (25-11)             ; y0 = e >> (25-11)
	movdqa  XTMP4, XTMP2            ; XTMP4 = W[-2] {BBAA}
	xor     y0, e                   ; y0 = e ^ (e >> (25-11))
	ror     y1, (22-13)             ; y1 = a >> (22-13)
	mov     y2, f                   ; y2 = f
	xor     y1, a                   ; y1 = a ^ (a >> (22-13)
	ror     y0, (11-6)              ; y0 = (e >> (11-6)) ^ (e >> (25-6))
	psrlq   XTMP2, 17               ; XTMP2 = W[-2] ror 17 {xBxA}
	xor     y2, g                   ; y2 = f^g
	psrlq   XTMP3, 19               ; XTMP3 = W[-2] ror 19 {xBxA}
	xor     y0, e                   ; y0 = e ^ (e >> (11-6)) ^ (e >> (25-6))
	and     y2, e                   ; y2 = (f^g)&e
	psrld   XTMP4, 10               ; XTMP4 = W[-2] >> 10 {BBAA}
	ror     y1, (13-2)              ; y1 = (a >> (13-2)) ^ (a >> (22-2))
	xor     y1, a                   ; y1 = a ^ (a >> (13-2)) ^ (a >> (22-2))
	xor     y2, g                   ; y2 = CH = ((f^g)&e)^g
	ror     y0, 6                   ; y0 = S1 = (e>>6) & (e>>11) ^ (e>>25)
	pxor    XTMP2, XTMP3
	add     y2, y0                  ; y2 = S1 + CH
	ror     y1, 2                   ; y1 = S0 = (a>>2) ^ (a>>13) ^ (a>>22)
	add     y2, [rsp + (2*4 + _XFER)] ; y2 = k + w + S1 + CH
	pxor    XTMP4, XTMP2            ; XTMP4 = s1 {xBxA}
	mov     y0, a                   ; y0 = a
	add     h, y2                   ; h = h + S1 + CH + k + w
	mov     y2, a                   ; y2 = a
	pshufb  XTMP4, SHUF_00BA        ; XTMP4 = s1 {00BA}
	or      y0, c                   ; y0 = a|c
	add     d, h                    ; d = d + h + S1 + CH + k + w
	and     y2, c                   ; y2 = a&c
	paddd   XTMP0, XTMP4            ; XTMP0 = {..., ..., W[1], W[0]}
	and     y0, b                   ; y0 = (a|c)&b
	add     h, y1                   ; h = h + S1 + CH + k + w + S0
	;; compute high s1
	pshufd  XTMP2, XTMP0, 01010000B ; XTMP2 = W[-2] {BBAA}
	or      y0, y2                  ; y0 = MAJ = (a|c)&b)|(a&c)
	add     h, y0                   ; h = h + S1 + CH + k + w + S0 + MAJ

	ROTATE_ARGS
	movdqa  XTMP3, XTMP2            ; XTMP3 = W[-2] {DDCC}
	mov     y0, e                   ; y0 = e
	ror     y0, (25-11)             ; y0 = e >> (25-11)
	mov     y1, a                   ; y1 = a
	movdqa  X0, XTMP2               ; X0    = W[-2] {DDCC}
	ror     y1, (22-13)             ; y1 = a >> (22-13)
	xor     y0, e                   ; y0 = e ^ (e >> (25-11))
	mov     y2, f                   ; y2 = f
	ror     y0, (11-6)              ; y0 = (e >> (11-6)) ^ (e >> (25-6))
	psrlq   XTMP2, 17               ; XTMP2 = W[-2] ror 17 {xDxC}
	xor     y1, a                   ; y1 = a ^ (a >> (22-13)
	xor     y2, g                   ; y2 = f^g
	psrlq   XTMP3, 19               ; XTMP3 = W[-2] ror 19 {xDxC}
	xor     y0, e                   ; y0 = e ^ (e >> (11-6)) ^ (e >> (25
	and     y2, e                   ; y2 = (f^g)&e
	ror     y1, (13-2)              ; y1 = (a >> (13-2)) ^ (a >> (22-2))
	psrld   X0, 10                  ; X0 = W[-2] >> 10 {DDCC}
	xor     y1, a                   ; y1 = a ^ (a >> (13-2)) ^ (a >> (22
	ror     y0, 6                   ; y0 = S1 = (e>>6) & (e>>11) ^ (e>>2
	xor     y2, g                   ; y2 = CH = ((f^g)&e)^g
	pxor    XTMP2, XTMP3            ;
	ror     y1, 2                   ; y1 = S0 = (a>>2) ^ (a>>13) ^ (a>>2
	add     y2, y0                  ; y2 = S1 + CH
	add     y2, [rsp + (3*4 + _XFER)] ; y2 = k + w + S1 + CH
	pxor    X0, XTMP2               ; X0 = s1 {xDxC}
	mov     y0, a                   ; y0 = a
	add     h, y2                   ; h = h + S1 + CH + k + w
	mov     y2, a                   ; y2 = a
	pshufb  X0, SHUF_DC00           ; X0 = s1 {DC00}
	or      y0, c                   ; y0 = a|c
	add     d, h                    ; d = d + h + S1 + CH + k + w
	and     y2, c                   ; y2 = a&c
	paddd   X0, XTMP0               ; X0 = {W[3], W[2], W[1], W[0]}
	and     y0, b                   ; y0 = (a|c)&b
	add     h, y1                   ; h = h + S1 + CH + k + w + S0
	or      y0, y2                  ; y0 = MAJ = (a|c)&b)|(a&c)
	add     h, y0                   ; h = h + S1 + CH + k + w + S0 + MAJ

	ROTATE_ARGS
	rotate_Xs
%endmacro

;; input is [rsp + _XFER + %1 * 4]
%macro DO_ROUND 1
	mov     y0, e                 ; y0 = e
	ror     y0, (25-11)           ; y0 = e >> (25-11)
	mov     y1, a                 ; y1 = a
	xor     y0, e                 ; y0 = e ^ (e >> (25-11))
	ror     y1, (22-13)           ; y1 = a >> (22-13)
	mov     y2, f                 ; y2 = f
	xor     y1, a                 ; y1 = a ^ (a >> (22-13)
	ror     y0, (11-6)            ; y0 = (e >> (11-6)) ^ (e >> (25-6))
	xor     y2, g                 ; y2 = f^g
	xor     y0, e                 ; y0 = e ^ (e >> (11-6)) ^ (e >> (25-6))
	ror     y1, (13-2)            ; y1 = (a >> (13-2)) ^ (a >> (22-2))
	and     y2, e                 ; y2 = (f^g)&e
	xor     y1, a                 ; y1 = a ^ (a >> (13-2)) ^ (a >> (22-2))
	ror     y0, 6                 ; y0 = S1 = (e>>6) & (e>>11) ^ (e>>25)
	xor     y2, g                 ; y2 = CH = ((f^g)&e)^g
	add     y2, y0                ; y2 = S1 + CH
	ror     y1, 2                 ; y1 = S0 = (a>>2) ^ (a>>13) ^ (a>>22)
	%xdefine offset (%1 * 4 + _XFER)
	add     y2, [rsp + offset]    ; y2 = k + w + S1 + CH
	mov     y0, a                 ; y0 = a
	add     h, y2                 ; h = h + S1 + CH + k + w
	mov     y2, a                 ; y2 = a
	or      y0, c                 ; y0 = a|c
	add     d, h                  ; d = d + h + S1 + CH + k + w
	and     y2, c                 ; y2 = a&c
	and     y0, b                 ; y0 = (a|c)&b
	add     h, y1                 ; h = h + S1 + CH + k + w + S0
	or      y0, y2 		      ; y0 = MAJ = (a|c)&b)|(a&c)
	add     h, y0 		      ; h = h + S1 + CH + k + w + S0 + MAJ
	ROTATE_ARGS
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; void sha1_opt_x1(SHA1_MB_ARGS_Xn *args, uint32_t size_in_blocks);
; arg 0 : MGR : pointer to args (only 4 of the 16 lanes used)
; arg 1 : NBLK : size (in blocks) ;; assumed to be >= 1
; invisibile arg 2 : IDX : hash on which lane
; invisibile arg 3 : NLANX4 : max lanes*4 for this arch (digest is placed by it)
; 		 (sse/avx is 4, avx2 is 8, avx512 is 16)
;
; Clobbers registers: all general regs, xmm0-xmm12
;	{rbx, rdx, rbp, (rdi, rsi), r12~r15 are saved on stack}
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text
mk_global sha256_opt_x1, function, internal
sha256_opt_x1:
	endbranch
	sub     rsp, STACK_SIZE
	mov     [rsp + _GPR_SAVE + 8*0], rbx
	mov     [rsp + _GPR_SAVE + 8*1], rbp
%ifidn __OUTPUT_FORMAT__, win64
	mov     [rsp + _GPR_SAVE + 8*2], rdi
	mov     [rsp + _GPR_SAVE + 8*3], rsi
	; caller has already stored XMM6~10
%endif
	mov     [rsp + _GPR_SAVE + 8*4], r12
	mov     [rsp + _GPR_SAVE + 8*5], r13
	mov     [rsp + _GPR_SAVE + 8*6], r14
	mov     [rsp + _GPR_SAVE + 8*7], r15
	mov     [rsp + _GPR_SAVE + 8*8], rdx

	shl     NBLK, 6 		 ; convert to bytes
	jz      done_hash

	; detach idx from nlanx4
	mov	IDX, NLANX4
	shr	NLANX4, 8
	and	IDX, 0xff

	mov     [rsp + _TMGR], MGR
	;; Load input pointers
	mov     INP, [MGR + _data_ptr + IDX*8]
	mov     [rsp + _INP], INP
	;; nblk is used to indicate data end
	add     NBLK, INP
	mov     [rsp + _INP_END], NBLK  ; pointer to end of data


	mov     TMGR, [rsp + _TMGR]
	;; load initial digest
	lea	TMP, [TMGR + 4*IDX]
	mov     a, [TMP + 0*NLANX4]
	mov     b, [TMP + 1*NLANX4]
	mov     c, [TMP + 2*NLANX4]
	lea	TMP, [TMP + 2*NLANX4]	; MGR + 4*IDX + 2*NLANX4
	mov     d, [TMP + 1*NLANX4]
	mov     e, [TMP + 2*NLANX4]
	mov     g, [TMP + 4*NLANX4]
	lea	TMP, [TMP + 1*NLANX4]	; MGR + 4*IDX + 3*NLANX4
	mov     f, [TMP + 2*NLANX4]
	mov     h, [TMP + 4*NLANX4]

	movdqa  BYTE_FLIP_MASK, [PSHUFFLE_BYTE_FLIP_MASK]
	movdqa  SHUF_00BA, [_SHUF_00BA]
	movdqa  SHUF_DC00, [_SHUF_DC00]

	mov     INP, [rsp + _INP]
loop0:
	lea     TBL, [K256]

	;; byte swap first 16 dwords
	COPY_XMM_AND_BSWAP      X0, [INP + 0*16], BYTE_FLIP_MASK
	COPY_XMM_AND_BSWAP      X1, [INP + 1*16], BYTE_FLIP_MASK
	COPY_XMM_AND_BSWAP      X2, [INP + 2*16], BYTE_FLIP_MASK
	COPY_XMM_AND_BSWAP      X3, [INP + 3*16], BYTE_FLIP_MASK

	mov     [rsp + _INP], INP

	;; schedule 48 input dwords, by doing 3 rounds of 16 each
	mov     SRND, 3

loop1:
	movdqa  XFER, [TBL]
	paddd   XFER, X0
	movdqa  [rsp + _XFER], XFER
	FOUR_ROUNDS_AND_SCHED

	movdqa  XFER, [TBL + 1*16]
	paddd   XFER, X0
	movdqa  [rsp + _XFER], XFER
	FOUR_ROUNDS_AND_SCHED

	movdqa  XFER, [TBL + 2*16]
	paddd   XFER, X0
	movdqa  [rsp + _XFER], XFER
	FOUR_ROUNDS_AND_SCHED

	movdqa  XFER, [TBL + 3*16]
	paddd   XFER, X0
	movdqa  [rsp + _XFER], XFER
	add     TBL, 4*16
	FOUR_ROUNDS_AND_SCHED

	sub     SRND, 1
	jne     loop1

	mov     SRND, 2
loop2:
	paddd   X0, [TBL]
	movdqa  [rsp + _XFER], X0
	DO_ROUND        0
	DO_ROUND        1
	DO_ROUND        2
	DO_ROUND        3
	paddd   X1, [TBL + 1*16]
	movdqa  [rsp + _XFER], X1
	add     TBL, 2*16
	DO_ROUND        0
	DO_ROUND        1
	DO_ROUND        2
	DO_ROUND        3

	movdqa  X0, X2
	movdqa  X1, X3

	sub     SRND, 1
	jne     loop2

	; write out digests
	mov     TMGR, [rsp + _TMGR]
	lea	TMP, [TMGR + 4*IDX]
	addm    a, [TMP + 0*NLANX4]
	addm    b, [TMP + 1*NLANX4]
	addm    c, [TMP + 2*NLANX4]
	lea	TMP, [TMP + 2*NLANX4]	; MGR + 4*IDX + 2*NLANX4
	addm    d, [TMP + 1*NLANX4]
	addm    e, [TMP + 2*NLANX4]
	addm    g, [TMP + 4*NLANX4]
	lea	TMP, [TMP + 1*NLANX4]	; MGR + 4*IDX + 3*NLANX4
	addm    f, [TMP + 2*NLANX4]
	addm    h, [TMP + 4*NLANX4]

	mov     INP, [rsp + _INP]
	add     INP, 64
	cmp     INP, [rsp + _INP_END]
	jne     loop0

done_hash:
	mov     MGR, [rsp + _TMGR]

	mov     rdx, [rsp + _GPR_SAVE + 8*8]
	mov     r15, [rsp + _GPR_SAVE + 8*7]
	mov     r14, [rsp + _GPR_SAVE + 8*6]
	mov     r13, [rsp + _GPR_SAVE + 8*5]
	mov     r12, [rsp + _GPR_SAVE + 8*4]
%ifidn __OUTPUT_FORMAT__, win64
	mov     rsi, [rsp + _GPR_SAVE + 8*3]
	mov     rdi, [rsp + _GPR_SAVE + 8*2]
%endif
	mov     rbp, [rsp + _GPR_SAVE + 8*1]
	mov     rbx, [rsp + _GPR_SAVE + 8*0]
	add     rsp, STACK_SIZE

	ret

section .data
align 64
K256:
        DD 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
        DD 0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
        DD 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
        DD 0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
        DD 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
        DD 0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
        DD 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
        DD 0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
        DD 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
        DD 0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
        DD 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
        DD 0xd192e819,0xd6990624,0xf40e3585,0x106aa070
        DD 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
        DD 0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
        DD 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
        DD 0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2

PSHUFFLE_BYTE_FLIP_MASK:
	DQ 0x0405060700010203, 0x0c0d0e0f08090a0b

; shuffle xBxA -> 00BA
_SHUF_00BA:
	DQ 0x0b0a090803020100, 0xFFFFFFFFFFFFFFFF

; shuffle xDxC -> DC00
_SHUF_DC00:
	DQ 0xFFFFFFFFFFFFFFFF, 0x0b0a090803020100

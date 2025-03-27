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

%include "sha1_mb_mgr_datastruct.asm"
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
%else
 ; Windows
 %define arg0   rcx
 %define arg1   rdx
%endif

;; FRAMESZ plus pushes must be an odd multiple of 8
_GPR_SAVE_SIZE  equ 8*9	;rbx, rdx, rbp, (rdi, rsi), r12~r15
_WK_SAVE_SIZE	equ 16*4

_WK_SAVE	equ 0
_GPR_SAVE	equ _WK_SAVE + _WK_SAVE_SIZE
STACK_SPACE	equ _GPR_SAVE + _GPR_SAVE_SIZE

; arg index is start from 0 while mgr_flush/submit is from 1
%define MGR	arg0
%define NBLK	arg1
%define NLANX4	r10	; consistent with caller
; rax~rdx, rsi, rdi, rbp are used for RR
%define N_MGR	r8
%define IDX	r9	; local variable -- consistent with caller
%define K_BASE	r11
%define BUFFER_PTR r12
%define BUFFER_END r13
%define TMP	r14	; local variable -- assistant to address digest

%xdefine W_TMP  xmm0
%xdefine W_TMP2 xmm9

%xdefine W0  xmm1
%xdefine W4  xmm2
%xdefine W8  xmm3
%xdefine W12 xmm4
%xdefine W16 xmm5
%xdefine W20 xmm6
%xdefine W24 xmm7
%xdefine W28 xmm8

%xdefine XMM_SHUFB_BSWAP xmm10

;; we keep window of 64 w[i]+K pre-calculated values in a circular buffer
%xdefine WK(t) (rsp + (t & 15)*4)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Constants

%xdefine K1 0x5a827999
%xdefine K2 0x6ed9eba1
%xdefine K3 0x8f1bbcdc
%xdefine K4 0xca62c1d6

%xdefine W_PRECALC_AHEAD   16
%xdefine W_NO_TAIL_PRECALC 0

; Rounds macros

%macro REGALLOC 0
    %xdefine A ecx
    %xdefine B esi
    %xdefine C edi
    %xdefine D ebp
    %xdefine E edx

    %xdefine T1 eax
    %xdefine T2 ebx
%endmacro

%macro F1 3
        mov T1,%2
        xor T1,%3
        and T1,%1
        xor T1,%3
%endmacro

%macro F2 3
        mov T1,%3
        xor T1,%2
        xor T1,%1
%endmacro

%macro F3 3
        mov T1,%2
        mov T2,%1
        or  T1,%1
        and T2,%2
        and T1,%3
        or  T1,T2
%endmacro

%define F4 F2

%macro UPDATE_HASH 2
     add %2, %1
     mov %1, %2
%endmacro


%macro W_PRECALC 1
    %xdefine i (%1)

    %if (i < 20)
        %xdefine K_XMM  0
    %elif (i < 40)
        %xdefine K_XMM  16
    %elif (i < 60)
        %xdefine K_XMM  32
    %else
        %xdefine K_XMM  48
    %endif

    %if (i<16 || (i>=80 && i<(80 + W_PRECALC_AHEAD)))

      %if (W_NO_TAIL_PRECALC == 0)

        %xdefine i ((%1) % 80)        ;; pre-compute for the next iteration

        %if (i == 0)
          W_PRECALC_RESET
        %endif


        W_PRECALC_00_15
      %endif

    %elif (i < 32)
        W_PRECALC_16_31
    %elif (i < 80)   ;; rounds 32-79
        W_PRECALC_32_79
    %endif
%endmacro

%macro W_PRECALC_RESET 0
    %xdefine    W             W0
    %xdefine    W_minus_04    W4
    %xdefine    W_minus_08    W8
    %xdefine    W_minus_12    W12
    %xdefine    W_minus_16    W16
    %xdefine    W_minus_20    W20
    %xdefine    W_minus_24    W24
    %xdefine    W_minus_28    W28
    %xdefine    W_minus_32    W
%endmacro

%macro W_PRECALC_ROTATE 0
    %xdefine    W_minus_32    W_minus_28
    %xdefine    W_minus_28    W_minus_24
    %xdefine    W_minus_24    W_minus_20
    %xdefine    W_minus_20    W_minus_16
    %xdefine    W_minus_16    W_minus_12
    %xdefine    W_minus_12    W_minus_08
    %xdefine    W_minus_08    W_minus_04
    %xdefine    W_minus_04    W
    %xdefine    W             W_minus_32
%endmacro

%macro W_PRECALC_00_15 0
      ;; message scheduling pre-compute for rounds 0-15
  %if ((i & 3) == 0)       ;; blended SSE and ALU instruction scheduling, 1 vector iteration per 4 rounds
    movdqu W_TMP, [BUFFER_PTR + (i * 4)]
  %elif ((i & 3) == 1)
    pshufb W_TMP, XMM_SHUFB_BSWAP
    movdqa W, W_TMP
  %elif ((i & 3) == 2)
    paddd  W_TMP, [K_BASE]
  %elif ((i & 3) == 3)
    movdqa  [WK(i&~3)], W_TMP

    W_PRECALC_ROTATE
  %endif
%endmacro

%macro W_PRECALC_16_31 0
      ;; message scheduling pre-compute for rounds 16-31
      ;; calculating last 32 w[i] values in 8 XMM registers
      ;; pre-calculate K+w[i] values and store to mem, for later load by ALU add instruction
      ;;
      ;; "brute force" vectorization for rounds 16-31 only due to w[i]->w[i-3] dependency
      ;;
  %if ((i & 3) == 0)    ;; blended SSE and ALU instruction scheduling, 1 vector iteration per 4 rounds
    movdqa  W, W_minus_12
    palignr W, W_minus_16, 8       ;; w[i-14]
    movdqa  W_TMP, W_minus_04
    psrldq  W_TMP, 4               ;; w[i-3]
    pxor    W, W_minus_08
  %elif ((i & 3) == 1)
    pxor    W_TMP, W_minus_16
    pxor    W, W_TMP
    movdqa  W_TMP2, W
    movdqa  W_TMP, W
    pslldq  W_TMP2, 12
  %elif ((i & 3) == 2)
    psrld   W, 31
    pslld   W_TMP, 1
    por     W_TMP, W
    movdqa  W, W_TMP2
    psrld   W_TMP2, 30
    pslld   W, 2
  %elif ((i & 3) == 3)
    pxor    W_TMP, W
    pxor    W_TMP, W_TMP2
    movdqa  W, W_TMP
    paddd   W_TMP, [K_BASE + K_XMM]
    movdqa  [WK(i&~3)],W_TMP

    W_PRECALC_ROTATE
  %endif
%endmacro

%macro W_PRECALC_32_79 0
    ;; in SHA-1 specification: w[i] = (w[i-3] ^ w[i-8]  ^ w[i-14] ^ w[i-16]) rol 1
    ;; instead we do equal:    w[i] = (w[i-6] ^ w[i-16] ^ w[i-28] ^ w[i-32]) rol 2
    ;; allows more efficient vectorization since w[i]=>w[i-3] dependency is broken
    ;;
  %if ((i & 3) == 0)    ;; blended SSE and ALU instruction scheduling, 1 vector iteration per 4 rounds
    movdqa  W_TMP, W_minus_04
    pxor    W, W_minus_28         ;; W is W_minus_32 before xor
    palignr W_TMP, W_minus_08, 8
  %elif ((i & 3) == 1)
    pxor    W, W_minus_16
    pxor    W, W_TMP
    movdqa  W_TMP, W
  %elif ((i & 3) == 2)
    psrld   W, 30
    pslld   W_TMP, 2
    por     W_TMP, W
  %elif ((i & 3) == 3)
    movdqa  W, W_TMP
    paddd   W_TMP, [K_BASE + K_XMM]
    movdqa  [WK(i&~3)],W_TMP

    W_PRECALC_ROTATE
  %endif
%endmacro

%macro RR 6             ;; RR does two rounds of SHA-1 back to back with W pre-calculation

   ;;     TEMP = A
   ;;     A = F( i, B, C, D ) + E + ROTATE_LEFT( A, 5 ) + W[i] + K(i)
   ;;     C = ROTATE_LEFT( B, 30 )
   ;;     D = C
   ;;     E = D
   ;;     B = TEMP

    W_PRECALC (%6 + W_PRECALC_AHEAD)
    F    %2, %3, %4     ;; F returns result in T1
    add  %5, [WK(%6)]
    rol  %2, 30
    mov  T2, %1
    add  %4, [WK(%6 + 1)]
    rol  T2, 5
    add  %5, T1

    W_PRECALC (%6 + W_PRECALC_AHEAD + 1)
    add  T2, %5
    mov  %5, T2
    rol  T2, 5
    add  %4, T2
    F    %1, %2, %3    ;; F returns result in T1
    add  %4, T1
    rol  %1, 30

;; write:  %1, %2
;; rotate: %1<=%4, %2<=%5, %3<=%1, %4<=%2, %5<=%3
%endmacro
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; void sha1_opt_x1(SHA1_MB_ARGS_Xn *args, uint32_t size_in_blocks);
; arg 0 : MGR : pointer to args (only 4 of the 16 lanes used)
; arg 1 : NBLK : size (in blocks) ;; assumed to be >= 1
; invisibile arg 2 : IDX : hash on which lane
; invisibile arg 3 : NLANX4 : max lanes*4 for this arch (digest is placed by it)
; 		 (sse/avx is 4, avx2 is 8, avx512 is 16)
;
; Clobbers registers: all general regs (except r15), xmm0-xmm10
;	{rbx, rdx, rbp, (rdi, rsi), r12~r15 are saved on stack}
;
mk_global sha1_opt_x1, function, internal
sha1_opt_x1:
	endbranch

	sub     rsp, STACK_SPACE
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


	shl	NBLK, 6		; transform blk amount into bytes
	jz	.lend
	; detach idx from nlanx4
	mov	IDX, NLANX4
	shr	NLANX4, 8
	and	IDX, 0xff

	;; let sha1_opt sb takes over r8~r11
	;; Load input pointers
	mov	N_MGR, MGR
	mov     BUFFER_PTR, [MGR + _data_ptr + IDX*8]
	;; nblk is used to indicate data end
	add	NBLK, BUFFER_PTR
        mov     BUFFER_END, NBLK

        lea     K_BASE, [K_XMM_AR]
        movdqu	XMM_SHUFB_BSWAP, [bswap_shufb_ctl]

        REGALLOC

	lea	TMP, [N_MGR + 4*IDX]
	;; Initialize digest
	mov	A, [TMP + 0*NLANX4]
	mov	B, [TMP + 1*NLANX4]
	mov	C, [TMP + 2*NLANX4]
	lea	TMP, [TMP + 2*NLANX4]	; MGR + 4*IDX + 2*NLANX4
	mov	D, [TMP + 1*NLANX4]
	mov	E, [TMP + 2*NLANX4]

  %assign i 0
  %rep    W_PRECALC_AHEAD
        W_PRECALC i
  %assign i i+1
  %endrep

  %xdefine F F1

.lloop:
        cmp BUFFER_PTR, K_BASE          ;; we use K_BASE value as a signal of a last block,
        jne .lbegin                    ;; it is set below by: cmovae BUFFER_PTR, K_BASE
        jmp .lend

.lbegin:
        RR A,B,C,D,E,0
        RR D,E,A,B,C,2
        RR B,C,D,E,A,4
        RR E,A,B,C,D,6
        RR C,D,E,A,B,8

        RR A,B,C,D,E,10
        RR D,E,A,B,C,12
        RR B,C,D,E,A,14
        RR E,A,B,C,D,16
        RR C,D,E,A,B,18

  %xdefine F F2

        RR A,B,C,D,E,20
        RR D,E,A,B,C,22
        RR B,C,D,E,A,24
        RR E,A,B,C,D,26
        RR C,D,E,A,B,28

        RR A,B,C,D,E,30
        RR D,E,A,B,C,32
        RR B,C,D,E,A,34
        RR E,A,B,C,D,36
        RR C,D,E,A,B,38

  %xdefine F F3

        RR A,B,C,D,E,40
        RR D,E,A,B,C,42
        RR B,C,D,E,A,44
        RR E,A,B,C,D,46
        RR C,D,E,A,B,48

        RR A,B,C,D,E,50
        RR D,E,A,B,C,52
        RR B,C,D,E,A,54
        RR E,A,B,C,D,56
        RR C,D,E,A,B,58

  %xdefine F F4

        add   BUFFER_PTR, 64            ;; move to next 64-byte block
        cmp   BUFFER_PTR, BUFFER_END    ;; check if current block is the last one
        cmovae BUFFER_PTR, K_BASE       ;; smart way to signal the last iteration

        RR A,B,C,D,E,60
        RR D,E,A,B,C,62
        RR B,C,D,E,A,64
        RR E,A,B,C,D,66
        RR C,D,E,A,B,68

        RR A,B,C,D,E,70
        RR D,E,A,B,C,72
        RR B,C,D,E,A,74
        RR E,A,B,C,D,76
        RR C,D,E,A,B,78

	lea	TMP, [N_MGR + 4*IDX]
        UPDATE_HASH [TMP + 0*NLANX4],A
        UPDATE_HASH [TMP + 1*NLANX4],B
        UPDATE_HASH [TMP + 2*NLANX4],C
	lea	TMP, [TMP + 2*NLANX4]	; MGR + 4*IDX + 2*NLANX4
        UPDATE_HASH [TMP + 1*NLANX4],D
        UPDATE_HASH [TMP + 2*NLANX4],E

        jmp .lloop

    .lend:
	mov	MGR, N_MGR

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
	add     rsp, STACK_SPACE

	ret


;;----------------------
section .data align=64

align 128
K_XMM_AR:
    DD K1, K1, K1, K1
    DD K2, K2, K2, K2
    DD K3, K3, K3, K3
    DD K4, K4, K4, K4

align 16
bswap_shufb_ctl:
    DD 00010203h
    DD 04050607h
    DD 08090a0bh
    DD 0c0d0e0fh

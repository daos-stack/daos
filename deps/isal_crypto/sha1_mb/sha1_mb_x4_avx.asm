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

%include "sha1_mb_mgr_datastruct.asm"
%include "reg_sizes.asm"

[bits 64]
default rel
section .text

;; code to compute quad SHA1 using AVX
;; derived from ...\sha1_multiple\sha1_quad4.asm
;; variation of sha1_mult2.asm : clobbers all xmm regs, rcx left intact

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
	vshufps %%t0, %%r0, %%r1, 0x44  ; t0 = {b1 b0 a1 a0}
	vshufps %%r0, %%r0, %%r1, 0xEE  ; r0 = {b3 b2 a3 a2}

	vshufps %%t1, %%r2, %%r3, 0x44  ; t1 = {d1 d0 c1 c0}
	vshufps %%r2, %%r2, %%r3, 0xEE  ; r2 = {d3 d2 c3 c2}

	vshufps %%r1, %%t0, %%t1, 0xDD  ; r1 = {d1 c1 b1 a1}

	vshufps %%r3, %%r0, %%r2, 0xDD  ; r3 = {d3 c3 b3 a3}

	vshufps %%r0, %%r0, %%r2, 0x88  ; r0 = {d2 c2 b2 a2}
	vshufps %%t0, %%t0, %%t1, 0x88  ; t0 = {d0 c0 b0 a0}
%endmacro
;;
;; Magic functions defined in FIPS 180-1
;;
; macro MAGIC_F0 F,B,C,D,T   ;; F = ((B & C) | ((~ B) & D) )
%macro MAGIC_F0 5
%define %%regF %1
%define %%regB %2
%define %%regC %3
%define %%regD %4
%define %%regT %5
    vpand  %%regF, %%regB,%%regC
    vpandn %%regT, %%regB,%%regD
    vpor   %%regF, %%regT,%%regF
%endmacro

; macro MAGIC_F1 F,B,C,D,T   ;; F = (B ^ C ^ D)
%macro MAGIC_F1 5
%define %%regF %1
%define %%regB %2
%define %%regC %3
%define %%regD %4
%define %%regT %5
    vpxor  %%regF,%%regD,%%regC
    vpxor  %%regF,%%regF,%%regB
%endmacro

; macro MAGIC_F2 F,B,C,D,T   ;; F = ((B & C) | (B & D) | (C & D))
%macro MAGIC_F2 5
%define %%regF %1
%define %%regB %2
%define %%regC %3
%define %%regD %4
%define %%regT %5
    vpor   %%regF,%%regB,%%regC
    vpand  %%regT,%%regB,%%regC
    vpand  %%regF,%%regF,%%regD
    vpor   %%regF,%%regF,%%regT
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
	vpsrld  %%tmp, %%reg, (32-(%%imm))
	vpslld  %%reg, %%reg, %%imm
	vpor    %%reg, %%reg, %%tmp
%endmacro

; non-destructive
; PROLD_nd reg, imm, tmp, src
%macro PROLD_nd 4
%define %%reg %1
%define %%imm %2
%define %%tmp %3
%define %%src %4
	vpsrld  %%tmp, %%src, (32-(%%imm))
	vpslld  %%reg, %%src, %%imm
	vpor    %%reg, %%reg, %%tmp
%endmacro

%macro SHA1_STEP_00_15 10
%define %%regA  %1
%define %%regB  %2
%define %%regC  %3
%define %%regD  %4
%define %%regE  %5
%define %%regT  %6
%define %%regF  %7
%define %%memW  %8
%define %%immCNT %9
%define %%MAGIC %10
	vpaddd  %%regE, %%regE,%%immCNT
	vpaddd  %%regE, %%regE,[rsp + (%%memW * 16)]
	PROLD_nd        %%regT,5, %%regF,%%regA
	vpaddd  %%regE, %%regE,%%regT
	%%MAGIC %%regF,%%regB,%%regC,%%regD,%%regT      ;; FUN  = MAGIC_Fi(B,C,D)
	PROLD   %%regB,30, %%regT
	vpaddd  %%regE, %%regE,%%regF
%endmacro

%macro SHA1_STEP_16_79 10
%define %%regA  %1
%define %%regB  %2
%define %%regC  %3
%define %%regD  %4
%define %%regE  %5
%define %%regT  %6
%define %%regF  %7
%define %%memW  %8
%define %%immCNT %9
%define %%MAGIC %10
	vpaddd  %%regE, %%regE,%%immCNT

	vmovdqa W14, [rsp + ((%%memW - 14) & 15) * 16]
	vpxor   W16, W16, W14
	vpxor   W16, W16, [rsp + ((%%memW -  8) & 15) * 16]
	vpxor   W16, W16, [rsp + ((%%memW -  3) & 15) * 16]

	vpsrld  %%regF, W16, (32-1)
	vpslld  W16, W16, 1
	vpor    %%regF, %%regF, W16
	ROTATE_W

	vmovdqa [rsp + ((%%memW - 0) & 15) * 16],%%regF
	vpaddd  %%regE, %%regE,%%regF

	PROLD_nd        %%regT,5, %%regF, %%regA
	vpaddd  %%regE, %%regE,%%regT
	%%MAGIC %%regF,%%regB,%%regC,%%regD,%%regT      ;; FUN  = MAGIC_Fi(B,C,D)
	PROLD   %%regB,30, %%regT
	vpaddd  %%regE,%%regE,%%regF
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; FRAMESZ plus pushes must be an odd multiple of 8
%define XMM_SAVE ((15-15)*16 + 1*8)
%define FRAMESZ 16*16 + XMM_SAVE
%define _XMM     FRAMESZ - XMM_SAVE

%define VMOVPS  vmovups

%define inp0 r8
%define inp1 r9
%define inp2 r10
%define inp3 r11

%define IDX  rax

%define A       xmm0
%define B       xmm1
%define C       xmm2
%define D       xmm3
%define E       xmm4
%define F       xmm5 ; tmp
%define G       xmm6 ; tmp

%define TMP     G
%define FUN     F
%define K       xmm7

%define AA      xmm8
%define BB      xmm9
%define CC      xmm10
%define DD      xmm11
%define EE      xmm12

%define T0      xmm6
%define T1      xmm7
%define T2      xmm8
%define T3      xmm9
%define T4      xmm10
%define T5      xmm11

%macro ROTATE_ARGS 0
%xdefine TMP_ E
%xdefine E D
%xdefine D C
%xdefine C B
%xdefine B A
%xdefine A TMP_
%endm

%define W14     xmm13
%define W15     xmm14
%define W16     xmm15

%macro ROTATE_W 0
%xdefine TMP_ W16
%xdefine W16 W15
%xdefine W15 W14
%xdefine W14 TMP_
%endm

%define DIGEST_SIZE (4*5*4)

;%ifdef LINUX
%ifidn __OUTPUT_FORMAT__, elf64
 %define ARG1 rdi
 %define ARG2 rsi
%else
 ; Windows
 %define ARG1 rcx
 %define ARG2 rdx
%endif

align 32

; void sha1_mb_x4_avx(SHA1_MB_ARGS_X8 *args, uint32_t size_in_blocks);
; arg 1 : ARG1 : pointer to args  (only 4 of the 8 lanes used)
; arg 2 : ARG2 : size (in blocks) ;; assumed to be >= 1
;
; Clobbers registers: ARG2, rax, r8-r11, xmm0-xmm15
;
mk_global sha1_mb_x4_avx, function, internal
sha1_mb_x4_avx:
	endbranch

	sub     rsp, FRAMESZ    ;; FRAMESZ + pushes must be odd multiple of 8

	;; Initialize digests
	vmovdqa A, [ARG1 + 0*16]
	vmovdqa B, [ARG1 + 1*16]
	vmovdqa C, [ARG1 + 2*16]
	vmovdqa D, [ARG1 + 3*16]
	vmovdqa E, [ARG1 + 4*16]

	;; load input pointers
	mov     inp0,[ARG1 + _data_ptr + 0*8]
	mov     inp1,[ARG1 + _data_ptr + 1*8]
	mov     inp2,[ARG1 + _data_ptr + 2*8]
	mov     inp3,[ARG1 + _data_ptr + 3*8]

	xor     IDX, IDX
lloop:
	vmovdqa F, [PSHUFFLE_BYTE_FLIP_MASK]
%assign I 0
%rep 4
	VMOVPS  T2,[inp0+IDX]
	VMOVPS  T1,[inp1+IDX]
	VMOVPS  T4,[inp2+IDX]
	VMOVPS  T3,[inp3+IDX]
	TRANSPOSE       T2, T1, T4, T3, T0, T5
	vpshufb T0, T0, F
	vmovdqa [rsp+(I*4+0)*16],T0
	vpshufb T1, T1, F
	vmovdqa [rsp+(I*4+1)*16],T1
	vpshufb T2, T2, F
	vmovdqa [rsp+(I*4+2)*16],T2
	vpshufb T3, T3, F
	vmovdqa [rsp+(I*4+3)*16],T3
	add     IDX, 4*4
%assign I (I+1)
%endrep

	; save old digests
	vmovdqa AA, A
	vmovdqa BB, B
	vmovdqa CC, C
	vmovdqa DD, D
	vmovdqa EE, E

;;
;; perform 0-79 steps
;;
	vmovdqa K, [K00_19]
;; do rounds 0...15
%assign I 0
%rep 16
	SHA1_STEP_00_15 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F0
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 16...19
	vmovdqa W16, [rsp + ((16 - 16) & 15) * 16]
	vmovdqa W15, [rsp + ((16 - 15) & 15) * 16]
%rep 4
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F0
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 20...39
	vmovdqa K, [K20_39]
%rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F1
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 40...59
	vmovdqa K, [K40_59]
%rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F2
	ROTATE_ARGS
%assign I (I+1)
%endrep

;; do rounds 60...79
	vmovdqa K, [K60_79]
%rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F3
	ROTATE_ARGS
%assign I (I+1)
%endrep

	vpaddd  A,A,AA
	vpaddd  B,B,BB
	vpaddd  C,C,CC
	vpaddd  D,D,DD
	vpaddd  E,E,EE

	sub     ARG2, 1
	jne     lloop

	; write out digests
	vmovdqa [ARG1 + 0*16], A
	vmovdqa [ARG1 + 1*16], B
	vmovdqa [ARG1 + 2*16], C
	vmovdqa [ARG1 + 3*16], D
	vmovdqa [ARG1 + 4*16], E

	; update input pointers
	add     inp0, IDX
	mov     [ARG1 + _data_ptr + 0*8], inp0
	add     inp1, IDX
	mov     [ARG1 + _data_ptr + 1*8], inp1
	add     inp2, IDX
	mov     [ARG1 + _data_ptr + 2*8], inp2
	add     inp3, IDX
	mov     [ARG1 + _data_ptr + 3*8], inp3

	;;;;;;;;;;;;;;;;
	;; Postamble

	add     rsp, FRAMESZ

	ret


section .data align=16

align 16
PSHUFFLE_BYTE_FLIP_MASK: dq 0x0405060700010203, 0x0c0d0e0f08090a0b
K00_19:                  dq 0x5A8279995A827999, 0x5A8279995A827999
K20_39:                  dq 0x6ED9EBA16ED9EBA1, 0x6ED9EBA16ED9EBA1
K40_59:                  dq 0x8F1BBCDC8F1BBCDC, 0x8F1BBCDC8F1BBCDC
K60_79:                  dq 0xCA62C1D6CA62C1D6, 0xCA62C1D6CA62C1D6

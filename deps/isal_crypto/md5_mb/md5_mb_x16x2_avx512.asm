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

%include "md5_mb_mgr_datastruct.asm"
%include "reg_sizes.asm"

%ifdef HAVE_AS_KNOWS_AVX512

[bits 64]
default rel
section .text


;; code to compute double octal MD5 using AVX512

;; Stack must be aligned to 64 bytes before call

;; Windows clobbers:  rax rbx     rdx rsi rdi     r8 r9 r10 r11 r12 r13 r14 r15
;; Windows preserves:         rcx             rbp
;;
;; Linux clobbers:    rax rbx rcx rdx rsi         r8 r9 r10 r11 r12 r13 r14 r15
;; Linux preserves:                       rdi rbp
;;
;; clobbers zmm0-8, 14-31

;; clobbers all GPRs other than arg1 and rbp

%ifidn __OUTPUT_FORMAT__, win64
   %define arg1 rcx	; arg0
   %define arg2 rdx	; arg1
   %define reg3 r8	; arg2
   %define reg4 r9	; arg3
   %define var1 rdi
   %define var2 rsi
   %define local_func_decl(func_name) global func_name
 %else
   %define arg1 rdi	; arg0
   %define arg2 rsi	; arg1
   %define var1 rdx	; arg2
   %define var2 rcx	; arg3
   %define local_func_decl(func_name) mk_global func_name, function, internal
%endif

%define state    arg1
%define num_blks arg2

%define	IN	(state + _data_ptr)
%define DIGEST	state
%define SIZE	num_blks
;; These are pointers to data block1 and block2 in the stack
; which will ping pong back and forth
%define DPTR1	rbx
%define DPTR2	var2
%define IDX	var1
%define TBL	rax

%define inp0 r8
%define inp1 r9
%define inp2 r10
%define inp3 r11
%define inp4 r12
%define inp5 r13
%define inp6 r14
%define inp7 r15

;; Transposed Digest Storage
%define A	zmm0
%define B	zmm1
%define C	zmm2
%define D	zmm3
%define A1	zmm4
%define B1	zmm5
%define C1	zmm6
%define D1	zmm7

%define md5c	zmm16

%define MASK0	zmm17
%define MASK1	zmm18

%define TMP0	zmm20
%define TMP1	zmm21


;; Data are stored into the Wx after transposition
%define W0	zmm8
%define W1	zmm9
%define W2	zmm10
%define W3	zmm11
%define W4	zmm12
%define W5	zmm13
%define W6	zmm14
%define W7	zmm15

%define W8	zmm24
%define W9	zmm25
%define W10	zmm26
%define W11	zmm27
%define W12	zmm28
%define W13	zmm29
%define W14	zmm30
%define W15	zmm31

%define MD5_DIGEST_ROW_SIZE (16*4)
%define APPEND(a,b) a %+ b
%define APPEND3(a,b,c) a %+ b %+ c

;; Temporary registers used during data transposition

%define RESZ	resb 64*
;; Assume stack aligned to 64 bytes before call
;; Therefore FRAMESIZE mod 64 must be 64-8 = 56
struc STACK
_DATA:		RESZ	2*2*16  ; 2 blocks * 2 sets of lanes * 16 regs
_DIGEST:	RESZ	8	; stores Z_AA-Z_DD, Z_AA2-Z_DD2
_TMPDIGEST:	RESZ	2	; stores Z_AA, Z_BB temporarily
_RSP_SAVE:	RESQ    1 	; original RSP
endstruc

%define	Z_AA	rsp + _DIGEST + 64*0
%define	Z_BB	rsp + _DIGEST + 64*1
%define	Z_CC	rsp + _DIGEST + 64*2
%define	Z_DD	rsp + _DIGEST + 64*3
%define	Z_AA1	rsp + _DIGEST + 64*4
%define	Z_BB1	rsp + _DIGEST + 64*5
%define	Z_CC1	rsp + _DIGEST + 64*6
%define	Z_DD1	rsp + _DIGEST + 64*7

%define MD5_DIGEST_ROW_SIZE (32*4)


;;
;; MD5 left rotations (number of bits)
;;
%define rot11 7
%define rot12 12
%define rot13 17
%define rot14 22
%define rot21 5
%define rot22 9
%define rot23 14
%define rot24 20
%define rot31 4
%define rot32 11
%define rot33 16
%define rot34 23
%define rot41 6
%define rot42 10
%define rot43 15
%define rot44 21

%macro TRANSPOSE16 18
%define %%r0 %1
%define %%r1 %2
%define %%r2 %3
%define %%r3 %4
%define %%r4 %5
%define %%r5 %6
%define %%r6 %7
%define %%r7 %8
%define %%r8 %9
%define %%r9 %10
%define %%r10 %11
%define %%r11 %12
%define %%r12 %13
%define %%r13 %14
%define %%r14 %15
%define %%r15 %16
%define %%t0 %17
%define %%t1 %18

; r0  = {a15 a14 a13 a12   a11 a10 a9 a8   a7 a6 a5 a4   a3 a2 a1 a0}
; r1  = {b15 b14 b13 b12   b11 b10 b9 b8   b7 b6 b5 b4   b3 b2 b1 b0}
; r2  = {c15 c14 c13 c12   c11 c10 c9 c8   c7 c6 c5 c4   c3 c2 c1 c0}
; r3  = {d15 d14 d13 d12   d11 d10 d9 d8   d7 d6 d5 d4   d3 d2 d1 d0}
; r4  = {e15 e14 e13 e12   e11 e10 e9 e8   e7 e6 e5 e4   e3 e2 e1 e0}
; r5  = {f15 f14 f13 f12   f11 f10 f9 f8   f7 f6 f5 f4   f3 f2 f1 f0}
; r6  = {g15 g14 g13 g12   g11 g10 g9 g8   g7 g6 g5 g4   g3 g2 g1 g0}
; r7  = {h15 h14 h13 h12   h11 h10 h9 h8   h7 h6 h5 h4   h3 h2 h1 h0}
; r8  = {i15 i14 i13 i12   i11 i10 i9 i8   i7 i6 i5 i4   i3 i2 i1 i0}
; r9  = {j15 j14 j13 j12   j11 j10 j9 j8   j7 j6 j5 j4   j3 j2 j1 j0}
; r10 = {k15 k14 k13 k12   k11 k10 k9 k8   k7 k6 k5 k4   k3 k2 k1 k0}
; r11 = {l15 l14 l13 l12   l11 l10 l9 l8   l7 l6 l5 l4   l3 l2 l1 l0}
; r12 = {m15 m14 m13 m12   m11 m10 m9 m8   m7 m6 m5 m4   m3 m2 m1 m0}
; r13 = {n15 n14 n13 n12   n11 n10 n9 n8   n7 n6 n5 n4   n3 n2 n1 n0}
; r14 = {o15 o14 o13 o12   o11 o10 o9 o8   o7 o6 o5 o4   o3 o2 o1 o0}
; r15 = {p15 p14 p13 p12   p11 p10 p9 p8   p7 p6 p5 p4   p3 p2 p1 p0}

; r0   = {p0  o0  n0  m0    l0  k0  j0  i0    h0  g0  f0  e0    d0  c0  b0  a0}
; r1   = {p1  o1  n1  m1    l1  k1  j1  i1    h1  g1  f1  e1    d1  c1  b1  a1}
; r2   = {p2  o2  n2  m2    l2  k2  j2  i2    h2  g2  f2  e2    d2  c2  b2  a2}
; r3   = {p3  o3  n3  m3    l3  k3  j3  i3    h3  g3  f3  e3    d3  c3  b3  a3}
; r4   = {p4  o4  n4  m4    l4  k4  j4  i4    h4  g4  f4  e4    d4  c4  b4  a4}
; r5   = {p5  o5  n5  m5    l5  k5  j5  i5    h5  g5  f5  e5    d5  c5  b5  a5}
; r6   = {p6  o6  n6  m6    l6  k6  j6  i6    h6  g6  f6  e6    d6  c6  b6  a6}
; r7   = {p7  o7  n7  m7    l7  k7  j7  i7    h7  g7  f7  e7    d7  c7  b7  a7}
; r8   = {p8  o8  n8  m8    l8  k8  j8  i8    h8  g8  f8  e8    d8  c8  b8  a8}
; r9   = {p9  o9  n9  m9    l9  k9  j9  i9    h9  g9  f9  e9    d9  c9  b9  a9}
; r10  = {p10 o10 n10 m10   l10 k10 j10 i10   h10 g10 f10 e10   d10 c10 b10 a10}
; r11  = {p11 o11 n11 m11   l11 k11 j11 i11   h11 g11 f11 e11   d11 c11 b11 a11}
; r12  = {p12 o12 n12 m12   l12 k12 j12 i12   h12 g12 f12 e12   d12 c12 b12 a12}
; r13  = {p13 o13 n13 m13   l13 k13 j13 i13   h13 g13 f13 e13   d13 c13 b13 a13}
; r14  = {p14 o14 n14 m14   l14 k14 j14 i14   h14 g14 f14 e14   d14 c14 b14 a14}
; r15  = {p15 o15 n15 m15   l15 k15 j15 i15   h15 g15 f15 e15   d15 c15 b15 a15}


	; process top half (r0..r3) {a...d}
	vshufps	%%t0, %%r0, %%r1, 0x44	; t0 = {b13 b12 a13 a12   b9  b8  a9  a8   b5 b4 a5 a4   b1 b0 a1 a0}
	vshufps	%%r0, %%r0, %%r1, 0xEE	; r0 = {b15 b14 a15 a14   b11 b10 a11 a10  b7 b6 a7 a6   b3 b2 a3 a2}
	vshufps	%%t1, %%r2, %%r3, 0x44	; t1 = {d13 d12 c13 c12   d9  d8  c9  c8   d5 d4 c5 c4   d1 d0 c1 c0}
	vshufps	%%r2, %%r2, %%r3, 0xEE	; r2 = {d15 d14 c15 c14   d11 d10 c11 c10  d7 d6 c7 c6   d3 d2 c3 c2}

	vshufps	%%r3, %%t0, %%t1, 0xDD	; r3 = {d13 c13 b13 a13   d9  c9  b9  a9   d5 c5 b5 a5   d1 c1 b1 a1}
	vshufps	%%r1, %%r0, %%r2, 0x88	; r1 = {d14 c14 b14 a14   d10 c10 b10 a10  d6 c6 b6 a6   d2 c2 b2 a2}
	vshufps	%%r0, %%r0, %%r2, 0xDD	; r0 = {d15 c15 b15 a15   d11 c11 b11 a11  d7 c7 b7 a7   d3 c3 b3 a3}
	vshufps	%%t0, %%t0, %%t1, 0x88	; t0 = {d12 c12 b12 a12   d8  c8  b8  a8   d4 c4 b4 a4   d0 c0 b0 a0}

	; use r2 in place of t0
	vshufps	%%r2, %%r4, %%r5, 0x44	; r2 = {f13 f12 e13 e12   f9  f8  e9  e8   f5 f4 e5 e4   f1 f0 e1 e0}
	vshufps	%%r4, %%r4, %%r5, 0xEE	; r4 = {f15 f14 e15 e14   f11 f10 e11 e10  f7 f6 e7 e6   f3 f2 e3 e2}
	vshufps %%t1, %%r6, %%r7, 0x44	; t1 = {h13 h12 g13 g12   h9  h8  g9  g8   h5 h4 g5 g4   h1 h0 g1 g0}
	vshufps	%%r6, %%r6, %%r7, 0xEE	; r6 = {h15 h14 g15 g14   h11 h10 g11 g10  h7 h6 g7 g6   h3 h2 g3 g2}

	vshufps	%%r7, %%r2, %%t1, 0xDD	; r7 = {h13 g13 f13 e13   h9  g9  f9  e9   h5 g5 f5 e5   h1 g1 f1 e1}
	vshufps	%%r5, %%r4, %%r6, 0x88	; r5 = {h14 g14 f14 e14   h10 g10 f10 e10  h6 g6 f6 e6   h2 g2 f2 e2}
	vshufps	%%r4, %%r4, %%r6, 0xDD	; r4 = {h15 g15 f15 e15   h11 g11 f11 e11  h7 g7 f7 e7   h3 g3 f3 e3}
	vshufps	%%r2, %%r2, %%t1, 0x88	; r2 = {h12 g12 f12 e12   h8  g8  f8  e8   h4 g4 f4 e4   h0 g0 f0 e0}

	; use r6 in place of t0
	vshufps	%%r6, %%r8, %%r9,    0x44	; r6  = {j13 j12 i13 i12   j9  j8  i9  i8   j5 j4 i5 i4   j1 j0 i1 i0}
	vshufps	%%r8, %%r8, %%r9,    0xEE	; r8  = {j15 j14 i15 i14   j11 j10 i11 i10  j7 j6 i7 i6   j3 j2 i3 i2}
	vshufps	%%t1, %%r10, %%r11,  0x44	; t1  = {l13 l12 k13 k12   l9  l8  k9  k8   l5 l4 k5 k4   l1 l0 k1 k0}
	vshufps	%%r10, %%r10, %%r11, 0xEE	; r10 = {l15 l14 k15 k14   l11 l10 k11 k10  l7 l6 k7 k6   l3 l2 k3 k2}

	vshufps	%%r11, %%r6, %%t1, 0xDD		; r11 = {l13 k13 j13 113   l9  k9  j9  i9   l5 k5 j5 i5   l1 k1 j1 i1}
	vshufps	%%r9, %%r8, %%r10, 0x88		; r9  = {l14 k14 j14 114   l10 k10 j10 i10  l6 k6 j6 i6   l2 k2 j2 i2}
	vshufps	%%r8, %%r8, %%r10, 0xDD		; r8  = {l15 k15 j15 115   l11 k11 j11 i11  l7 k7 j7 i7   l3 k3 j3 i3}
	vshufps	%%r6, %%r6, %%t1,  0x88		; r6  = {l12 k12 j12 112   l8  k8  j8  i8   l4 k4 j4 i4   l0 k0 j0 i0}

	; use r10 in place of t0
	vshufps	%%r10, %%r12, %%r13, 0x44	; r10 = {n13 n12 m13 m12   n9  n8  m9  m8   n5 n4 m5 m4   n1 n0 a1 m0}
	vshufps	%%r12, %%r12, %%r13, 0xEE	; r12 = {n15 n14 m15 m14   n11 n10 m11 m10  n7 n6 m7 m6   n3 n2 a3 m2}
	vshufps	%%t1, %%r14, %%r15,  0x44	; t1  = {p13 p12 013 012   p9  p8  09  08   p5 p4 05 04   p1 p0 01 00}
	vshufps	%%r14, %%r14, %%r15, 0xEE	; r14 = {p15 p14 015 014   p11 p10 011 010  p7 p6 07 06   p3 p2 03 02}

	vshufps	%%r15, %%r10, %%t1,  0xDD	; r15 = {p13 013 n13 m13   p9  09  n9  m9   p5 05 n5 m5   p1 01 n1 m1}
	vshufps	%%r13, %%r12, %%r14, 0x88	; r13 = {p14 014 n14 m14   p10 010 n10 m10  p6 06 n6 m6   p2 02 n2 m2}
	vshufps	%%r12, %%r12, %%r14, 0xDD	; r12 = {p15 015 n15 m15   p11 011 n11 m11  p7 07 n7 m7   p3 03 n3 m3}
	vshufps	%%r10, %%r10, %%t1,  0x88	; r10 = {p12 012 n12 m12   p8  08  n8  m8   p4 04 n4 m4   p0 00 n0 m0}

;; At this point, the registers that contain interesting data are:
;; t0, r3, r1, r0, r2, r7, r5, r4, r6, r11, r9, r8, r10, r15, r13, r12
;; Can use t1 and r14 as scratch registers

	vmovdqa32 %%r14, MASK0
	vpermi2q  %%r14, %%t0, %%r2		; r14 = {h8  g8  f8  e8   d8  c8  b8  a8   h0 g0 f0 e0	 d0 c0 b0 a0}
	vmovdqa32 %%t1,  MASK1
	vpermi2q  %%t1,  %%t0, %%r2		; t1  = {h12 g12 f12 e12  d12 c12 b12 a12  h4 g4 f4 e4	 d4 c4 b4 a4}

	vmovdqa32 %%r2, MASK0
	vpermi2q  %%r2, %%r3, %%r7		; r2  = {h9  g9  f9  e9   d9  c9  b9  a9   h1 g1 f1 e1	 d1 c1 b1 a1}
	vmovdqa32 %%t0, MASK1
	vpermi2q  %%t0, %%r3, %%r7		; t0  = {h13 g13 f13 e13  d13 c13 b13 a13  h5 g5 f5 e5	 d5 c5 b5 a5}

	vmovdqa32 %%r3, MASK0
	vpermi2q  %%r3, %%r1, %%r5		; r3  = {h10 g10 f10 e10  d10 c10 b10 a10  h2 g2 f2 e2	 d2 c2 b2 a2}
	vmovdqa32 %%r7, MASK1
	vpermi2q  %%r7, %%r1, %%r5		; r7  = {h14 g14 f14 e14  d14 c14 b14 a14  h6 g6 f6 e6	 d6 c6 b6 a6}

	vmovdqa32 %%r1, MASK0
	vpermi2q  %%r1, %%r0, %%r4		; r1  = {h11 g11 f11 e11  d11 c11 b11 a11  h3 g3 f3 e3	 d3 c3 b3 a3}
	vmovdqa32 %%r5, MASK1
	vpermi2q  %%r5, %%r0, %%r4		; r5  = {h15 g15 f15 e15  d15 c15 b15 a15  h7 g7 f7 e7	 d7 c7 b7 a7}

	vmovdqa32 %%r0, MASK0
	vpermi2q  %%r0, %%r6, %%r10		; r0 = {p8  o8  n8  m8   l8  k8  j8  i8   p0 o0 n0 m0	 l0 k0 j0 i0}
	vmovdqa32 %%r4,  MASK1
	vpermi2q  %%r4, %%r6, %%r10		; r4  = {p12 o12 n12 m12  l12 k12 j12 i12  p4 o4 n4 m4	 l4 k4 j4 i4}

	vmovdqa32 %%r6, MASK0
	vpermi2q  %%r6, %%r11, %%r15		; r6  = {p9  o9  n9  m9   l9  k9  j9  i9   p1 o1 n1 m1	 l1 k1 j1 i1}
	vmovdqa32 %%r10, MASK1
	vpermi2q  %%r10, %%r11, %%r15		; r10 = {p13 o13 n13 m13  l13 k13 j13 i13  p5 o5 n5 m5	 l5 k5 j5 i5}

	vmovdqa32 %%r11, MASK0
	vpermi2q  %%r11, %%r9, %%r13		; r11 = {p10 o10 n10 m10  l10 k10 j10 i10  p2 o2 n2 m2	 l2 k2 j2 i2}
	vmovdqa32 %%r15, MASK1
	vpermi2q  %%r15, %%r9, %%r13		; r15 = {p14 o14 n14 m14  l14 k14 j14 i14  p6 o6 n6 m6	 l6 k6 j6 i6}

	vmovdqa32 %%r9, MASK0
	vpermi2q  %%r9, %%r8, %%r12		; r9  = {p11 o11 n11 m11  l11 k11 j11 i11  p3 o3 n3 m3	 l3 k3 j3 i3}
	vmovdqa32 %%r13, MASK1
	vpermi2q  %%r13, %%r8, %%r12		; r13 = {p15 o15 n15 m15  l15 k15 j15 i15  p7 o7 n7 m7	 l7 k7 j7 i7}

;; At this point r8 and r12 can be used as scratch registers

	vshuff64x2 %%r8, %%r14, %%r0, 0xEE 	; r8  = {p8  o8  n8  m8   l8  k8  j8  i8   h8 g8 f8 e8   d8 c8 b8 a8}
	vshuff64x2 %%r0, %%r14, %%r0, 0x44 	; r0  = {p0  o0  n0  m0   l0  k0  j0  i0   h0 g0 f0 e0   d0 c0 b0 a0}

	vshuff64x2 %%r12, %%t1, %%r4, 0xEE 	; r12 = {p12 o12 n12 m12  l12 k12 j12 i12  h12 g12 f12 e12  d12 c12 b12 a12}
	vshuff64x2 %%r4, %%t1, %%r4, 0x44 	; r4  = {p4  o4  n4  m4   l4  k4  j4  i4   h4 g4 f4 e4   d4 c4 b4 a4}

	vshuff64x2 %%r14, %%r7, %%r15, 0xEE 	; r14 = {p14 o14 n14 m14  l14 k14 j14 i14  h14 g14 f14 e14  d14 c14 b14 a14}
	vshuff64x2 %%t1, %%r7, %%r15, 0x44 	; t1  = {p6  o6  n6  m6   l6  k6  j6  i6   h6 g6 f6 e6   d6 c6 b6 a6}

	vshuff64x2 %%r15, %%r5, %%r13, 0xEE 	; r15 = {p15 o15 n15 m15  l15 k15 j15 i15  h15 g15 f15 e15  d15 c15 b15 a15}
	vshuff64x2 %%r7, %%r5, %%r13, 0x44 	; r7  = {p7  o7  n7  m7   l7  k7  j7  i7   h7 g7 f7 e7   d7 c7 b7 a7}

	vshuff64x2 %%r13, %%t0, %%r10, 0xEE 	; r13 = {p13 o13 n13 m13  l13 k13 j13 i13  h13 g13 f13 e13  d13 c13 b13 a13}
	vshuff64x2 %%r5, %%t0, %%r10, 0x44 	; r5  = {p5  o5  n5  m5   l5  k5  j5  i5   h5 g5 f5 e5   d5 c5 b5 a5}

	vshuff64x2 %%r10, %%r3, %%r11, 0xEE 	; r10 = {p10 o10 n10 m10  l10 k10 j10 i10  h10 g10 f10 e10  d10 c10 b10 a10}
	vshuff64x2 %%t0, %%r3, %%r11, 0x44 	; t0  = {p2  o2  n2  m2   l2  k2  j2  i2   h2 g2 f2 e2   d2 c2 b2 a2}

	vshuff64x2 %%r11, %%r1, %%r9, 0xEE 	; r11 = {p11 o11 n11 m11  l11 k11 j11 i11  h11 g11 f11 e11  d11 c11 b11 a11}
	vshuff64x2 %%r3, %%r1, %%r9, 0x44 	; r3  = {p3  o3  n3  m3   l3  k3  j3  i3   h3 g3 f3 e3   d3 c3 b3 a3}

	vshuff64x2 %%r9, %%r2, %%r6, 0xEE 	; r9  = {p9  o9  n9  m9   l9  k9  j9  i9   h9 g9 f9 e9   d9 c9 b9 a9}
	vshuff64x2 %%r1, %%r2, %%r6, 0x44 	; r1  = {p1  o1  n1  m1   l1  k1  j1  i1   h1 g1 f1 e1   d1 c1 b1 a1}

	vmovdqa32 %%r2, %%t0			; r2  = {p2  o2  n2  m2   l2  k2  j2  i2   h2 g2 f2 e2   d2 c2 b2 a2}
	vmovdqa32 %%r6, %%t1			; r6  = {p6  o6  n6  m6   l6  k6  j6  i6   h6 g6 f6 e6   d6 c6 b6 a6}

%endmacro

%macro ROTATE_ARGS 0
%xdefine TMP_ D
%xdefine D C
%xdefine C B
%xdefine B A
%xdefine A TMP_
%endm

%macro ROTATE_ARGS1 0
%xdefine TMP_ D1
%xdefine D1 C1
%xdefine C1 B1
%xdefine B1 A1
%xdefine A1 TMP_
%endm

;;
;; single MD5 step
;;
;; A = B +ROL32((A +Ft(B,C,D) +data +const), nrot)
;;eg: PROCESS_LOOP MD5constx, Mdatax, F_IMMEDx, NROTx
%macro PROCESS_LOOP 6
%define %%MD5const	%1
%define %%data		%2
%define %%F_IMMED	%3
%define %%NROT		%4
%define %%TMP_PR0	%5
%define %%TMP_PR1	%6
	; a=b+((a+Ft(b,c,d)+Mj+ti)<<s)

	; Ft
	;  0-15 Ft:F(X,Y,Z)=(X&Y)|((~X)&Z)	0xca
	; 16-31 Ft:G(X,Y,Z)=(X&Z)|(Y&(~Z))	0xe4
	; 32-47 Ft:H(X,Y,Z)=X^Y^Z		0x96
	; 48-63 Ft:I(X,Y,Z)=Y^(X|(~Z))		0x39

	vpaddd		A, A, %%MD5const
		vpaddd		A1, A1, %%MD5const
	vpaddd		A, A, [%%data]
		vpaddd		A1, A1, [%%data + 16*64]
	vmovdqa32	%%TMP_PR0, B		; Copy B
		vmovdqa32	%%TMP_PR1, B1		; Copy B
	vpternlogd	%%TMP_PR0, C, D, %%F_IMMED
		vpternlogd	%%TMP_PR1, C1, D1, %%F_IMMED
	vpaddd		A, A, %%TMP_PR0
		vpaddd		A1, A1, %%TMP_PR1
	vprold		A, A, %%NROT
		vprold		A1, A1, %%NROT
	vpaddd		A, A, B
		vpaddd		A1, A1, B1

	ROTATE_ARGS
	ROTATE_ARGS1
%endmacro

align 64

; void md5_mb_x16x2_avx512(MD5_ARGS *args, UINT64 num_blks)
; arg 1 : pointer to MD5_ARGS structure
; arg 2 : number of blocks (>=1)

local_func_decl(md5_mb_x16x2_avx512)
md5_mb_x16x2_avx512:
	endbranch
	mov	rax, rsp
	sub	rsp, STACK_size
	and	rsp, -64
	mov	[rsp + _RSP_SAVE], rax

	mov	DPTR1, rsp
	lea	DPTR2, [rsp + 64*32]

	;; Load MD5 constant pointer to register
	lea	TBL, [MD5_TABLE]
	vmovdqa32 MASK0, [PSHUFFLE_TRANSPOSE16_MASK1]
	vmovdqa32 MASK1, [PSHUFFLE_TRANSPOSE16_MASK2]

	;; Preload input data from 16 segments.
	xor IDX, IDX

	;; transpose input onto stack
	;; first 16 lanes read
	mov	inp0, [IN + 0*8]
	mov	inp1, [IN + 1*8]
	mov	inp2, [IN + 2*8]
	mov	inp3, [IN + 3*8]
	mov	inp4, [IN + 4*8]
	mov	inp5, [IN + 5*8]
	mov	inp6, [IN + 6*8]
	mov	inp7, [IN + 7*8]
	vmovdqu32	W0,[inp0+IDX]
	vmovdqu32	W1,[inp1+IDX]
	vmovdqu32	W2,[inp2+IDX]
	vmovdqu32	W3,[inp3+IDX]
	vmovdqu32	W4,[inp4+IDX]
	vmovdqu32	W5,[inp5+IDX]
	vmovdqu32	W6,[inp6+IDX]
	vmovdqu32	W7,[inp7+IDX]
	mov	inp0, [IN + 8*8]
	mov	inp1, [IN + 9*8]
	mov	inp2, [IN +10*8]
	mov	inp3, [IN +11*8]
	mov	inp4, [IN +12*8]
	mov	inp5, [IN +13*8]
	mov	inp6, [IN +14*8]
	mov	inp7, [IN +15*8]
	vmovdqu32	W8, [inp0+IDX]
	vmovdqu32	W9, [inp1+IDX]
	vmovdqu32	W10,[inp2+IDX]
	vmovdqu32	W11,[inp3+IDX]
	vmovdqu32	W12,[inp4+IDX]
	vmovdqu32	W13,[inp5+IDX]
	vmovdqu32	W14,[inp6+IDX]
	vmovdqu32	W15,[inp7+IDX]
	;; first 16 lanes trans&write
	TRANSPOSE16 W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15, TMP0, TMP1
	vmovdqa32	[DPTR1+_DATA+(0)*64],W0
	vmovdqa32	[DPTR1+_DATA+(1)*64],W1
	vmovdqa32	[DPTR1+_DATA+(2)*64],W2
	vmovdqa32	[DPTR1+_DATA+(3)*64],W3
	vmovdqa32	[DPTR1+_DATA+(4)*64],W4
	vmovdqa32	[DPTR1+_DATA+(5)*64],W5
	vmovdqa32	[DPTR1+_DATA+(6)*64],W6
	vmovdqa32	[DPTR1+_DATA+(7)*64],W7
	vmovdqa32	[DPTR1+_DATA+(8)*64],W8
	vmovdqa32	[DPTR1+_DATA+(9)*64],W9
	vmovdqa32	[DPTR1+_DATA+(10)*64],W10
	vmovdqa32	[DPTR1+_DATA+(11)*64],W11
	vmovdqa32	[DPTR1+_DATA+(12)*64],W12
	vmovdqa32	[DPTR1+_DATA+(13)*64],W13
	vmovdqa32	[DPTR1+_DATA+(14)*64],W14
	vmovdqa32	[DPTR1+_DATA+(15)*64],W15

	;; second 16 lanes read
	mov	inp0, [IN + 16*8]
	mov	inp1, [IN + 17*8]
	mov	inp2, [IN + 18*8]
	mov	inp3, [IN + 19*8]
	mov	inp4, [IN + 20*8]
	mov	inp5, [IN + 21*8]
	mov	inp6, [IN + 22*8]
	mov	inp7, [IN + 23*8]
	vmovdqu32	W0,[inp0+IDX]
	vmovdqu32	W1,[inp1+IDX]
	vmovdqu32	W2,[inp2+IDX]
	vmovdqu32	W3,[inp3+IDX]
	vmovdqu32	W4,[inp4+IDX]
	vmovdqu32	W5,[inp5+IDX]
	vmovdqu32	W6,[inp6+IDX]
	vmovdqu32	W7,[inp7+IDX]
	mov	inp0, [IN + 24*8]
	mov	inp1, [IN + 25*8]
	mov	inp2, [IN + 26*8]
	mov	inp3, [IN + 27*8]
	mov	inp4, [IN + 28*8]
	mov	inp5, [IN + 29*8]
	mov	inp6, [IN + 30*8]
	mov	inp7, [IN + 31*8]
	vmovdqu32	W8, [inp0+IDX]
	vmovdqu32	W9, [inp1+IDX]
	vmovdqu32	W10,[inp2+IDX]
	vmovdqu32	W11,[inp3+IDX]
	vmovdqu32	W12,[inp4+IDX]
	vmovdqu32	W13,[inp5+IDX]
	vmovdqu32	W14,[inp6+IDX]
	vmovdqu32	W15,[inp7+IDX]
	;; second 16 lanes trans&write
	TRANSPOSE16 W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15, TMP0, TMP1
	vmovdqa32	[DPTR1+_DATA+(16+0)*64],W0
	vmovdqa32	[DPTR1+_DATA+(16+1)*64],W1
	vmovdqa32	[DPTR1+_DATA+(16+2)*64],W2
	vmovdqa32	[DPTR1+_DATA+(16+3)*64],W3
	vmovdqa32	[DPTR1+_DATA+(16+4)*64],W4
	vmovdqa32	[DPTR1+_DATA+(16+5)*64],W5
	vmovdqa32	[DPTR1+_DATA+(16+6)*64],W6
	vmovdqa32	[DPTR1+_DATA+(16+7)*64],W7
	vmovdqa32	[DPTR1+_DATA+(16+8)*64],W8
	vmovdqa32	[DPTR1+_DATA+(16+9)*64],W9
	vmovdqa32	[DPTR1+_DATA+(16+10)*64],W10
	vmovdqa32	[DPTR1+_DATA+(16+11)*64],W11
	vmovdqa32	[DPTR1+_DATA+(16+12)*64],W12
	vmovdqa32	[DPTR1+_DATA+(16+13)*64],W13
	vmovdqa32	[DPTR1+_DATA+(16+14)*64],W14
	vmovdqa32	[DPTR1+_DATA+(16+15)*64],W15

	;; Initialize digests
	;; vmovdqu32 replace vmovdqa32
	vmovdqu32	A, [DIGEST + 0 * MD5_DIGEST_ROW_SIZE]
	vmovdqu32	B, [DIGEST + 1 * MD5_DIGEST_ROW_SIZE]
	vmovdqu32	C, [DIGEST + 2 * MD5_DIGEST_ROW_SIZE]
	vmovdqu32	D, [DIGEST + 3 * MD5_DIGEST_ROW_SIZE]
	; Load the digest for each stream (9-16)
	vmovdqu32	A1,[DIGEST + 0 * MD5_DIGEST_ROW_SIZE + 64]
	vmovdqu32	B1,[DIGEST + 1 * MD5_DIGEST_ROW_SIZE + 64]
	vmovdqu32	C1,[DIGEST + 2 * MD5_DIGEST_ROW_SIZE + 64]
	vmovdqu32	D1,[DIGEST + 3 * MD5_DIGEST_ROW_SIZE + 64]

.lloop:
	;; Increment IDX to point to next data block (64 bytes per block)
	add	IDX, 64

	; Save digests for later addition
	vmovdqa32	[Z_AA], A
	vmovdqa32	[Z_BB], B
	vmovdqa32	[Z_CC], C
	vmovdqa32	[Z_DD], D
	vmovdqa32	[Z_AA1], A1
	vmovdqa32	[Z_BB1], B1
	vmovdqa32	[Z_CC1], C1
	vmovdqa32	[Z_DD1], D1

	sub	SIZE, 1
	je	.LastLoop

%assign I 	0
%assign I_fimm  0xCA
%rep 16		; 0<=I<=15
 %assign I_rotX	I/16+1
 %assign I_rotY	(I % 4 + 1)
 %assign I_data	I
	vpbroadcastd md5c, [TBL + I * 4]
	PROCESS_LOOP md5c, DPTR1+ I_data*64, I_fimm, APPEND3(rot, I_rotX, I_rotY), TMP0, TMP1
 %assign I (I+1)
%endrep
	;; first 16 lanes read
	mov	inp0, [IN + 0*8]
	mov	inp1, [IN + 1*8]
	mov	inp2, [IN + 2*8]
	mov	inp3, [IN + 3*8]
	mov	inp4, [IN + 4*8]
	mov	inp5, [IN + 5*8]
	mov	inp6, [IN + 6*8]
	mov	inp7, [IN + 7*8]
	vmovdqu32	W0,[inp0+IDX]
	vmovdqu32	W1,[inp1+IDX]
	vmovdqu32	W2,[inp2+IDX]
	vmovdqu32	W3,[inp3+IDX]
	vmovdqu32	W4,[inp4+IDX]
	vmovdqu32	W5,[inp5+IDX]
	vmovdqu32	W6,[inp6+IDX]
	vmovdqu32	W7,[inp7+IDX]
	mov	inp0, [IN + 8*8]
	mov	inp1, [IN + 9*8]
	mov	inp2, [IN +10*8]
	mov	inp3, [IN +11*8]
	mov	inp4, [IN +12*8]
	mov	inp5, [IN +13*8]
	mov	inp6, [IN +14*8]
	mov	inp7, [IN +15*8]
	vmovdqu32	W8, [inp0+IDX]
	vmovdqu32	W9, [inp1+IDX]
	vmovdqu32	W10,[inp2+IDX]
	vmovdqu32	W11,[inp3+IDX]
	vmovdqu32	W12,[inp4+IDX]
	vmovdqu32	W13,[inp5+IDX]
	vmovdqu32	W14,[inp6+IDX]
	vmovdqu32	W15,[inp7+IDX]

%assign I 	16
%assign I_fimm  0xE4
%rep 16		; 16<=I<=31
 %assign I_data	((5*I+1) % 16)
 %assign I_rotX	I/16+1
 %assign I_rotY	(I % 4 + 1)
	vpbroadcastd md5c, [TBL + I * 4]
	PROCESS_LOOP md5c, DPTR1+ I_data*64, I_fimm, APPEND3(rot, I_rotX, I_rotY), TMP0, TMP1
 %assign I (I+1)
%endrep

	;; first 16 lanes trans&write
	TRANSPOSE16 W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15, TMP0, TMP1
	vmovdqa32	[DPTR2+_DATA+(0)*64],W0
	vmovdqa32	[DPTR2+_DATA+(1)*64],W1
	vmovdqa32	[DPTR2+_DATA+(2)*64],W2
	vmovdqa32	[DPTR2+_DATA+(3)*64],W3
	vmovdqa32	[DPTR2+_DATA+(4)*64],W4
	vmovdqa32	[DPTR2+_DATA+(5)*64],W5
	vmovdqa32	[DPTR2+_DATA+(6)*64],W6
	vmovdqa32	[DPTR2+_DATA+(7)*64],W7
	vmovdqa32	[DPTR2+_DATA+(8)*64],W8
	vmovdqa32	[DPTR2+_DATA+(9)*64],W9
	vmovdqa32	[DPTR2+_DATA+(10)*64],W10
	vmovdqa32	[DPTR2+_DATA+(11)*64],W11
	vmovdqa32	[DPTR2+_DATA+(12)*64],W12
	vmovdqa32	[DPTR2+_DATA+(13)*64],W13
	vmovdqa32	[DPTR2+_DATA+(14)*64],W14
	vmovdqa32	[DPTR2+_DATA+(15)*64],W15

%assign I 	32
%assign I_fimm  0x96
%rep 16		; 32<=I<=47
 %assign I_data	((3*I+5) % 16)
 %assign I_rotX	I/16+1
 %assign I_rotY	(I % 4 + 1)
	vpbroadcastd md5c, [TBL + I * 4]
	PROCESS_LOOP md5c, DPTR1+ I_data*64, I_fimm, APPEND3(rot, I_rotX, I_rotY), TMP0, TMP1
 %assign I (I+1)
%endrep

	;; second 16 lanes read
	mov	inp0, [IN + 16*8]
	mov	inp1, [IN + 17*8]
	mov	inp2, [IN + 18*8]
	mov	inp3, [IN + 19*8]
	mov	inp4, [IN + 20*8]
	mov	inp5, [IN + 21*8]
	mov	inp6, [IN + 22*8]
	mov	inp7, [IN + 23*8]
	vmovdqu32	W0,[inp0+IDX]
	vmovdqu32	W1,[inp1+IDX]
	vmovdqu32	W2,[inp2+IDX]
	vmovdqu32	W3,[inp3+IDX]
	vmovdqu32	W4,[inp4+IDX]
	vmovdqu32	W5,[inp5+IDX]
	vmovdqu32	W6,[inp6+IDX]
	vmovdqu32	W7,[inp7+IDX]
	mov	inp0, [IN + 24*8]
	mov	inp1, [IN + 25*8]
	mov	inp2, [IN + 26*8]
	mov	inp3, [IN + 27*8]
	mov	inp4, [IN + 28*8]
	mov	inp5, [IN + 29*8]
	mov	inp6, [IN + 30*8]
	mov	inp7, [IN + 31*8]
	vmovdqu32	W8, [inp0+IDX]
	vmovdqu32	W9, [inp1+IDX]
	vmovdqu32	W10,[inp2+IDX]
	vmovdqu32	W11,[inp3+IDX]
	vmovdqu32	W12,[inp4+IDX]
	vmovdqu32	W13,[inp5+IDX]
	vmovdqu32	W14,[inp6+IDX]
	vmovdqu32	W15,[inp7+IDX]

%assign I 	48
%assign I_fimm  0x39
%rep 16	; 48<=I<=63
 %assign I_rotX	(I/16+1)
 %assign I_rotY	(I % 4 + 1)
 %assign I_data	((7*I) % 16)
	vpbroadcastd md5c, [TBL + I * 4]
	PROCESS_LOOP md5c,  DPTR1+ I_data*64, I_fimm, APPEND3(rot, I_rotX, I_rotY), TMP0, TMP1
 %assign I (I+1)
%endrep

	;; second 16 lanes trans&write
	TRANSPOSE16 W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15, TMP0, TMP1
	vmovdqa32	[DPTR2+_DATA+(16+0)*64],W0
	vmovdqa32	[DPTR2+_DATA+(16+1)*64],W1
	vmovdqa32	[DPTR2+_DATA+(16+2)*64],W2
	vmovdqa32	[DPTR2+_DATA+(16+3)*64],W3
	vmovdqa32	[DPTR2+_DATA+(16+4)*64],W4
	vmovdqa32	[DPTR2+_DATA+(16+5)*64],W5
	vmovdqa32	[DPTR2+_DATA+(16+6)*64],W6
	vmovdqa32	[DPTR2+_DATA+(16+7)*64],W7
	vmovdqa32	[DPTR2+_DATA+(16+8)*64],W8
	vmovdqa32	[DPTR2+_DATA+(16+9)*64],W9
	vmovdqa32	[DPTR2+_DATA+(16+10)*64],W10
	vmovdqa32	[DPTR2+_DATA+(16+11)*64],W11
	vmovdqa32	[DPTR2+_DATA+(16+12)*64],W12
	vmovdqa32	[DPTR2+_DATA+(16+13)*64],W13
	vmovdqa32	[DPTR2+_DATA+(16+14)*64],W14
	vmovdqa32	[DPTR2+_DATA+(16+15)*64],W15

	; Add old digest
	vpaddd		A,A,[Z_AA]
	vpaddd		B,B,[Z_BB]
	vpaddd		C,C,[Z_CC]
	vpaddd		D,D,[Z_DD]
	vpaddd		A1,A1,[Z_AA1]
	vpaddd		B1,B1,[Z_BB1]
	vpaddd		C1,C1,[Z_CC1]
	vpaddd		D1,D1,[Z_DD1]

	; Swap DPTR1 and DPTR2
	xchg	DPTR1, DPTR2
	 ;; Proceed to processing of next block
	jmp 	.lloop

.LastLoop:
%assign I 	0
%assign I_fimm  0xCA
%rep 16		; 0<=I<=15
 %assign I_rotX	I/16+1
 %assign I_rotY	(I % 4 + 1)
 %assign I_data	I
	vpbroadcastd md5c, [TBL + I * 4]
	PROCESS_LOOP md5c, DPTR1+ I_data*64, I_fimm, APPEND3(rot, I_rotX, I_rotY), TMP0, TMP1
 %assign I (I+1)
%endrep

%assign I 	16
%assign I_fimm  0xE4
%rep 16		; 16<=I<=31
 %assign I_data	((5*I+1) % 16)
 %assign I_rotX	I/16+1
 %assign I_rotY	(I % 4 + 1)
	vpbroadcastd md5c, [TBL + I * 4]
	PROCESS_LOOP md5c, DPTR1+ I_data*64, I_fimm, APPEND3(rot, I_rotX, I_rotY), TMP0, TMP1
 %assign I (I+1)
%endrep

%assign I 	32
%assign I_fimm  0x96
%rep 16		; 32<=I<=47
 %assign I_data	((3*I+5) % 16)
 %assign I_rotX	I/16+1
 %assign I_rotY	(I % 4 + 1)
	vpbroadcastd md5c, [TBL + I * 4]
	PROCESS_LOOP md5c, DPTR1+ I_data*64, I_fimm, APPEND3(rot, I_rotX, I_rotY), TMP0, TMP1
 %assign I (I+1)
%endrep

%assign I 	48
%assign I_fimm  0x39
%rep 16	; 48<=I<=63
 %assign I_rotX	(I/16+1)
 %assign I_rotY	(I % 4 + 1)
 %assign I_data	((7*I) % 16)
	vpbroadcastd md5c, [TBL + I * 4]
	PROCESS_LOOP md5c,  DPTR1+ I_data*64, I_fimm, APPEND3(rot, I_rotX, I_rotY), TMP0, TMP1
 %assign I (I+1)
%endrep

	; Add old digest
	vpaddd		A,A,[Z_AA]
	vpaddd		B,B,[Z_BB]
	vpaddd		C,C,[Z_CC]
	vpaddd		D,D,[Z_DD]
	vpaddd		A1,A1,[Z_AA1]
	vpaddd		B1,B1,[Z_BB1]
	vpaddd		C1,C1,[Z_CC1]
	vpaddd		D1,D1,[Z_DD1]

	;; update into data pointers
%assign I 0
%rep 16
	mov    inp0, [IN + (2*I)*8]
	mov    inp1, [IN + (2*I +1)*8]
	add    inp0, IDX
	add    inp1, IDX
	mov    [IN + (2*I)*8], inp0
	mov    [IN + (2*I+1)*8], inp1
%assign I (I+1)
%endrep

	vmovdqu32	[DIGEST + 0*MD5_DIGEST_ROW_SIZE  ], A
	vmovdqu32	[DIGEST + 1*MD5_DIGEST_ROW_SIZE  ], B
	vmovdqu32	[DIGEST + 2*MD5_DIGEST_ROW_SIZE  ], C
	vmovdqu32	[DIGEST + 3*MD5_DIGEST_ROW_SIZE  ], D
	; Store the digest for each stream (9-16)
	vmovdqu32	[DIGEST + 0 * MD5_DIGEST_ROW_SIZE + 64], A1
	vmovdqu32	[DIGEST + 1 * MD5_DIGEST_ROW_SIZE + 64], B1
	vmovdqu32	[DIGEST + 2 * MD5_DIGEST_ROW_SIZE + 64], C1
	vmovdqu32	[DIGEST + 3 * MD5_DIGEST_ROW_SIZE + 64], D1

	mov	rsp, [rsp + _RSP_SAVE]
	ret

section .data
align 64
MD5_TABLE:
	dd	0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee
	dd	0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501
	dd	0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be
	dd	0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821
	dd	0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa
	dd	0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8
	dd	0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed
	dd	0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a
	dd	0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c
	dd	0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70
	dd	0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05
	dd	0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665
	dd	0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039
	dd	0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1
	dd	0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1
	dd	0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391

PSHUFFLE_TRANSPOSE16_MASK1: 	dq 0x0000000000000000
				dq 0x0000000000000001
				dq 0x0000000000000008
				dq 0x0000000000000009
				dq 0x0000000000000004
				dq 0x0000000000000005
				dq 0x000000000000000C
				dq 0x000000000000000D

PSHUFFLE_TRANSPOSE16_MASK2: 	dq 0x0000000000000002
				dq 0x0000000000000003
				dq 0x000000000000000A
				dq 0x000000000000000B
				dq 0x0000000000000006
				dq 0x0000000000000007
				dq 0x000000000000000E
				dq 0x000000000000000F

%else
%ifidn __OUTPUT_FORMAT__, win64
global no_md5_mb_x16x2_avx512
no_md5_mb_x16x2_avx512:
%endif
%endif ; HAVE_AS_KNOWS_AVX512

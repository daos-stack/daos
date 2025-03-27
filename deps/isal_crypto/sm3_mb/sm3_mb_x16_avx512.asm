;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2020 Intel Corporation All rights reserved.
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

%include "sm3_mb_mgr_datastruct.asm"
%include "reg_sizes.asm"


%ifdef HAVE_AS_KNOWS_AVX512

[bits 64]
default rel
section .text

; Define Stack Layout
START_FIELDS
;;;     name            size    align
FIELD	_DIGEST_SAVE,	8*64,	64
FIELD	_rsp,		8,	8
%assign STACK_SPACE	_FIELD_OFFSET

%ifidn __OUTPUT_FORMAT__, win64
   %define arg1 rcx	; arg0 preserved
   %define arg2 rdx	; arg1
   %define reg3 r8	; arg2 preserved
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

%define	IN	(state + _data_ptr) ; rdi + 8*16
%define DIGEST	state		    ; rdi
%define SIZE	num_blks	    ; rsi

%define IDX  var1
%define TBL  var2

%define APPEND(a,b) a %+ b


%define A	zmm0
%define B	zmm1
%define C	zmm2
%define D	zmm3
%define E	zmm4
%define F	zmm5
%define G	zmm6
%define H	zmm7

;
; 4 ZMM for tmp data
;
%define TMP0	zmm8
%define TMP1	zmm9
%define TMP2	zmm10
%define TMP3	zmm11

;
; Word W[] will be expand to array size 64
; Word WB[] will be expand to array size 68
; WB[j] :
; 	tmp = WB[j - 16] ^ WB[j - 9] ^ rol32(WB[j - 3], 15);
;	WB[j] = P1(tmp) ^ (rol32(WB[j - 13], 7)) ^ WB[j - 6];
; W[j]:
; 	W[j] = WB[j] xor WB[j+4]
;
; so we used zmm12~31 20 numbers ZMM to keep WB
; it is because once we calc W[j] value, we need
; WB[j - 16] to WB[j + 4] , it is 20 WB number.
;
; And also we keep the lane into ZMM12~ZMM27
; once we calc WB value, lane will not work
;
%define WB0	zmm12
%define WB1	zmm13
%define WB2	zmm14
%define WB3	zmm15
%define WB4	zmm16
%define WB5	zmm17
%define WB6	zmm18
%define WB7	zmm19

%define WB8	zmm20
%define WB9	zmm21
%define WB10	zmm22
%define WB11	zmm23
%define WB12	zmm24
%define WB13	zmm25
%define WB14	zmm26
%define WB15	zmm27

%define WB16	zmm28
%define WB17	zmm29
%define WB18	zmm30
%define WB19	zmm31


%define inp0	r9
%define inp1	r10
%define inp2	r11
%define inp3	r12
%define inp4	r13
%define inp5	r14
%define inp6	r15
%define inp7	rax

;
; same as sha256
;
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

	vmovdqa32 %%r14, [PSHUFFLE_TRANSPOSE16_MASK1]
	vpermi2q  %%r14, %%t0, %%r2		; r14 = {h8  g8  f8  e8   d8  c8  b8  a8   h0 g0 f0 e0	 d0 c0 b0 a0}
	vmovdqa32 %%t1,  [PSHUFFLE_TRANSPOSE16_MASK2]
	vpermi2q  %%t1,  %%t0, %%r2		; t1  = {h12 g12 f12 e12  d12 c12 b12 a12  h4 g4 f4 e4	 d4 c4 b4 a4}

	vmovdqa32 %%r2, [PSHUFFLE_TRANSPOSE16_MASK1]
	vpermi2q  %%r2, %%r3, %%r7		; r2  = {h9  g9  f9  e9   d9  c9  b9  a9   h1 g1 f1 e1	 d1 c1 b1 a1}
	vmovdqa32 %%t0, [PSHUFFLE_TRANSPOSE16_MASK2]
	vpermi2q  %%t0, %%r3, %%r7		; t0  = {h13 g13 f13 e13  d13 c13 b13 a13  h5 g5 f5 e5	 d5 c5 b5 a5}

	vmovdqa32 %%r3, [PSHUFFLE_TRANSPOSE16_MASK1]
	vpermi2q  %%r3, %%r1, %%r5		; r3  = {h10 g10 f10 e10  d10 c10 b10 a10  h2 g2 f2 e2	 d2 c2 b2 a2}
	vmovdqa32 %%r7, [PSHUFFLE_TRANSPOSE16_MASK2]
	vpermi2q  %%r7, %%r1, %%r5		; r7  = {h14 g14 f14 e14  d14 c14 b14 a14  h6 g6 f6 e6	 d6 c6 b6 a6}

	vmovdqa32 %%r1, [PSHUFFLE_TRANSPOSE16_MASK1]
	vpermi2q  %%r1, %%r0, %%r4		; r1  = {h11 g11 f11 e11  d11 c11 b11 a11  h3 g3 f3 e3	 d3 c3 b3 a3}
	vmovdqa32 %%r5, [PSHUFFLE_TRANSPOSE16_MASK2]
	vpermi2q  %%r5, %%r0, %%r4		; r5  = {h15 g15 f15 e15  d15 c15 b15 a15  h7 g7 f7 e7	 d7 c7 b7 a7}

	vmovdqa32 %%r0, [PSHUFFLE_TRANSPOSE16_MASK1]
	vpermi2q  %%r0, %%r6, %%r10		; r0 = {p8  o8  n8  m8   l8  k8  j8  i8   p0 o0 n0 m0	 l0 k0 j0 i0}
	vmovdqa32 %%r4,  [PSHUFFLE_TRANSPOSE16_MASK2]
	vpermi2q  %%r4, %%r6, %%r10		; r4  = {p12 o12 n12 m12  l12 k12 j12 i12  p4 o4 n4 m4	 l4 k4 j4 i4}

	vmovdqa32 %%r6, [PSHUFFLE_TRANSPOSE16_MASK1]
	vpermi2q  %%r6, %%r11, %%r15		; r6  = {p9  o9  n9  m9   l9  k9  j9  i9   p1 o1 n1 m1	 l1 k1 j1 i1}
	vmovdqa32 %%r10, [PSHUFFLE_TRANSPOSE16_MASK2]
	vpermi2q  %%r10, %%r11, %%r15		; r10 = {p13 o13 n13 m13  l13 k13 j13 i13  p5 o5 n5 m5	 l5 k5 j5 i5}

	vmovdqa32 %%r11, [PSHUFFLE_TRANSPOSE16_MASK1]
	vpermi2q  %%r11, %%r9, %%r13		; r11 = {p10 o10 n10 m10  l10 k10 j10 i10  p2 o2 n2 m2	 l2 k2 j2 i2}
	vmovdqa32 %%r15, [PSHUFFLE_TRANSPOSE16_MASK2]
	vpermi2q  %%r15, %%r9, %%r13		; r15 = {p14 o14 n14 m14  l14 k14 j14 i14  p6 o6 n6 m6	 l6 k6 j6 i6}

	vmovdqa32 %%r9, [PSHUFFLE_TRANSPOSE16_MASK1]
	vpermi2q  %%r9, %%r8, %%r12		; r9  = {p11 o11 n11 m11  l11 k11 j11 i11  p3 o3 n3 m3	 l3 k3 j3 i3}
	vmovdqa32 %%r13, [PSHUFFLE_TRANSPOSE16_MASK2]
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
	%xdefine A TMP3
	%xdefine TMP3 TMP_

	%xdefine TMP2_ H
	%xdefine H G
	%xdefine G F
	%xdefine F E
	%xdefine E TMP0
	%xdefine TMP0 TMP2_
%endmacro

;
; P() Save in TMP0
; used TMP1
%macro P 1
%define %%A     %1
        vprold TMP0,%%A,9
        vprold TMP1,%%A,17

        vpternlogd TMP0,TMP1,%%A,0x96

%endmacro

;
; P1() Save in TMP0
; used TMP1
%macro P1 1
%define %%A 	%1

	vprold TMP0,%%A,15
	vprold TMP1,%%A,23

	vpternlogd TMP0,TMP1,%%A,0x96
%endmacro

;
; FF_16() Save in TMP0
;
%macro FF_16 3
%define %%X 	%1
%define %%Y 	%2
%define %%Z 	%3
	; I < 16 return (X ^ Y ^ Z)
	vmovups TMP0,%%X
	vpternlogd TMP0,%%Y,%%Z,0x96
%endmacro


;
; FF_64() Save in TMP0
; used TMP1
%macro FF_64 3

%define %%X 	%1
%define %%Y 	%2
%define %%Z 	%3
	; I > 16 return (x & y) | (x & z) | (y & z)
	; Same as (x & y) | (z & (x | y))
	vporq TMP0,%%X,%%Y
	vpandq TMP0,%%Z
	vpandq TMP1,%%X,%%Y
	vporq TMP0,TMP1
%endmacro


;
; GG() Save in TMP0
; used TMP1
%macro GG_16 3
%define %%X 	%1
%define %%Y 	%2
%define %%Z 	%3
	; I < 16 return (x ^ y ^ z)
        vmovups TMP0,%%X
        vpternlogd TMP0,%%Y,%%Z,0x96
%endmacro

%macro GG_64 3

%define %%X 	%1
%define %%Y 	%2
%define %%Z 	%3

	; I > 16 return (x & y) | ((~x) & z)
	vpandq TMP0,%%X,%%Y
	vpandnd TMP1,%%X,%%Z
	vporq TMP0,TMP1
%endmacro

;; void sm3_mb_x16_avx512(SM3_MB_ARGS_X16, uint32_t size)
; arg 1 : pointer to input data
; arg 2 : size (in blocks) ;; assumed to be >= 1
local_func_decl(sm3_mb_x16_avx512)
sm3_mb_x16_avx512:
	endbranch

	mov	rax, rsp
        sub     rsp, STACK_SPACE
	and	rsp, ~63	; align stack to multiple of 64
	mov	[rsp + _rsp], rax

	lea	TBL, [TABLE]

	;; Initialize digests
	vmovups	A, [DIGEST + 0*64] ; mov unsigned
	vmovups	B, [DIGEST + 1*64]
	vmovups	C, [DIGEST + 2*64]
	vmovups	D, [DIGEST + 3*64]
	vmovups	E, [DIGEST + 4*64]
	vmovups	F, [DIGEST + 5*64]
	vmovups	G, [DIGEST + 6*64]
	vmovups	H, [DIGEST + 7*64]

	xor IDX, IDX

%assign cur_loop 0
lloop:
	;; start message expand
	;; Transpose input data
	mov	inp0, [IN + 0*8]
	mov	inp1, [IN + 1*8]
	mov	inp2, [IN + 2*8]
	mov	inp3, [IN + 3*8]
	mov	inp4, [IN + 4*8]
	mov	inp5, [IN + 5*8]
	mov	inp6, [IN + 6*8]
	mov	inp7, [IN + 7*8]

	;; stored B(i) to W(1)...W(15)
	;; in zmm16....zmm31

	vmovups	WB0,[inp0+IDX]
	vmovups	WB1,[inp1+IDX]
	vmovups	WB2,[inp2+IDX]
	vmovups	WB3,[inp3+IDX]
	vmovups	WB4,[inp4+IDX]
	vmovups	WB5,[inp5+IDX]
	vmovups	WB6,[inp6+IDX]
	vmovups	WB7,[inp7+IDX]

	mov	inp0, [IN + 8*8]
	mov	inp1, [IN + 9*8]
	mov	inp2, [IN +10*8]
	mov	inp3, [IN +11*8]
	mov	inp4, [IN +12*8]
	mov	inp5, [IN +13*8]
	mov	inp6, [IN +14*8]
	mov	inp7, [IN +15*8]

	vmovups	WB8, [inp0+IDX]
	vmovups	WB9, [inp1+IDX]
	vmovups	WB10,[inp2+IDX]
	vmovups	WB11,[inp3+IDX]
	vmovups	WB12,[inp4+IDX]
	vmovups	WB13,[inp5+IDX]
	vmovups	WB14,[inp6+IDX]
	vmovups	WB15,[inp7+IDX]

	vmovdqa32	[rsp + _DIGEST_SAVE + 64*0], A
        vmovdqa32	[rsp + _DIGEST_SAVE + 64*1], B
        vmovdqa32	[rsp + _DIGEST_SAVE + 64*2], C
        vmovdqa32	[rsp + _DIGEST_SAVE + 64*3], D
        vmovdqa32	[rsp + _DIGEST_SAVE + 64*4], E
        vmovdqa32	[rsp + _DIGEST_SAVE + 64*5], F
        vmovdqa32	[rsp + _DIGEST_SAVE + 64*6], G
        vmovdqa32	[rsp + _DIGEST_SAVE + 64*7], H

	add	IDX, 64

	; flat shuffle
	 TRANSPOSE16 WB0, WB1, WB2, WB3, WB4, WB5, WB6, WB7, WB8, WB9, WB10, WB11, WB12, WB13, WB14, WB15, TMP0, TMP1

	; little endian to big endian
	vmovdqa32 TMP0, [SHUF_MASK]
	vpshufb WB0,TMP0
	vpshufb WB1,TMP0
	vpshufb WB2,TMP0
	vpshufb WB3,TMP0
	vpshufb WB4,TMP0
	vpshufb WB5,TMP0
	vpshufb WB6,TMP0
	vpshufb WB7,TMP0
	vpshufb WB8,TMP0
	vpshufb WB9,TMP0
	vpshufb WB10,TMP0
	vpshufb WB11,TMP0
	vpshufb WB12,TMP0
	vpshufb WB13,TMP0
	vpshufb WB14,TMP0
	vpshufb WB15,TMP0

%assign I 0
%rep 12
	%assign J I+4

	; (A <<< 12)
	; store in TMP0
	vprold TMP0,A,12

	; SS1 = ((A <<< 12) + E + (T(j) <<< j)) <<< 7
	; (T(j) <<< j) store in TBL
	; SS1 store in TMP2
	vmovdqa32 TMP2, [TBL + (I*64)]
	vpaddd TMP2,E

	vpaddd TMP2,TMP0
	vprold TMP2,7

	; SS2 = SS1 ^ (A <<< 12)
	; SS2 store in TMP3
	vpxord TMP3,TMP2,TMP0

	; TT2 = GG(E,F,G) + H + SS1 + WB(I)
	GG_16 E,F,G
	vpaddd TMP2,TMP0
	vpaddd TMP2,H

	vpaddd TMP2,APPEND(WB,I)

	; TT1 = FF(A,B,C) + D + SS2 + W(I)
	; TT1 store in TMP3
	FF_16 A,B,C
	vpaddd TMP3,TMP0
	vpaddd TMP3,D
	; W(I) = WB(I) ^ W(I+4)
	vpxord TMP0,APPEND(WB,I),APPEND(WB,J)
	vpaddd TMP3,TMP0


	; D = C
	; C = B <<< 9
	; B = A
	; A = TT1
	; H = G
	; G = F <<< 19
	; F = E
	; E = P(TT2)
	vmovups D,C
	vprold B,9
	vmovups C,B
	vmovups B,A
	vmovups A,TMP3
	vmovups H,G
	vprold F,19
	vmovups G,F
	vmovups F,E
	P TMP2
	vmovups E,TMP0

	;vprold B,9
	;vprold F,19
	;P TMP2
	;ROTATE_ARGS

	%assign I (I+1)
%endrep


;tmp = WB[j - 16] ^ WB[j - 9] ^ rol32(WB[j - 3], 15);
;WB[j] = P1(tmp) ^ (rol32(WB[j - 13], 7)) ^ WB[j - 6];

; round 12-16 here
%rep 4
	%assign J I+4

	%assign J_3 J-3
	%assign J_16 J-16
	%assign J_9 J-9
	%assign J_13 J-13
	%assign J_6 J-6

	; clac WB(I+4)
	vprold APPEND(WB,J),APPEND(WB,J_3),15
	vpxord APPEND(WB,J),APPEND(WB,J_16)
	vpxord APPEND(WB,J),APPEND(WB,J_9)

	P1 APPEND(WB,J)

	vprold APPEND(WB,J),APPEND(WB,J_13),7
	vpxord APPEND(WB,J),TMP0
	vpxord APPEND(WB,J),APPEND(WB,J_6)

	; (A <<< 12)
	; store in TMP0
	vprold TMP0,A,12

	; SS1 = ((A <<< 12) + E + (T(j) <<< j)) <<< 7
	; (T(j) <<< j) store in TBL
	; SS1 store in TMP2
	vmovdqa32 TMP2, [TBL + (I*64)]
	vpaddd TMP2,E

	vpaddd TMP2,TMP0
	vprold TMP2,7

	; SS2 = SS1 ^ (A <<< 12)
	; SS2 store in TMP3
	vpxord TMP3,TMP2,TMP0

	; TT2 = GG(E,F,G) + H + SS1 + WB(I)
	GG_16 E,F,G
	vpaddd TMP2,TMP0
	vpaddd TMP2,H

	vpaddd TMP2,APPEND(WB,I)

	; TT1 = FF(A,B,C) + D + SS2 + W(I)
	; TT1 store in TMP3
	FF_16 A,B,C
	vpaddd TMP3,TMP0
	vpaddd TMP3,D
	; W(I) = WB(I) ^ W(I+4)
	vpxord TMP0,APPEND(WB,I),APPEND(WB,J)
	vpaddd TMP3,TMP0

	; D = C
	; C = B <<< 9
	; B = A
	; A = TT1
	; H = G
	; G = F <<< 19
	; F = E
	; E = P(TT2)
	vmovups D,C
	vprold B,9
	vmovups C,B
	vmovups B,A
	vmovups A,TMP3
	vmovups H,G
	vprold F,19
	vmovups G,F
	vmovups F,E
	P TMP2
	vmovups E,TMP0

	%assign I (I+1)
%endrep

%rep 48
	%assign J (((I+4) % 20) + 20)

	%assign J_3 ((J-3) % 20)
	%assign J_16 ((J-16) % 20)
	%assign J_9 ((J-9) % 20)
	%assign J_13 ((J-13) % 20)
	%assign J_6 ((J-6) % 20)

	%assign I_20 (I % 20)
	%assign J (((I+4) % 20))

	vprold APPEND(WB,J),APPEND(WB,J_3),15
	vpxord APPEND(WB,J),APPEND(WB,J_16)
	vpxord APPEND(WB,J),APPEND(WB,J_9)

	P1 APPEND(WB,J)

	vprold APPEND(WB,J),APPEND(WB,J_13),7
	vpxord APPEND(WB,J),TMP0
	vpxord APPEND(WB,J),APPEND(WB,J_6)

	; (A <<< 12)
	; store in TMP0
	vprold TMP0,A,12

	; SS1 = ((A <<< 12) + E + (T(j) <<< j)) <<< 7
	; (T(j) <<< j) store in TBL
	; SS1 store in TMP2
	vmovdqa32 TMP2, [TBL + (I*64)]
	vpaddd TMP2,E

	vpaddd TMP2,TMP0
	vprold TMP2,7

	; SS2 = SS1 ^ (A <<< 12)
	; SS2 store in TMP3
	vpxord TMP3,TMP2,TMP0

	; TT2 = GG(E,F,G) + H + SS1 + WB(I)
	GG_64 E,F,G
	vpaddd TMP2,TMP0
	vpaddd TMP2,H

	vpaddd TMP2,APPEND(WB,I_20)

	; TT1 = FF(A,B,C) + D + SS2 + W(I)
	; TT1 store in TMP3
	FF_64 A,B,C
	vpaddd TMP3,TMP0
	vpaddd TMP3,D
	; W(I) = WB(I) ^ W(I+4)
	vpxord TMP0,APPEND(WB,I_20),APPEND(WB,J)
	vpaddd TMP3,TMP0

	; D = C
	; C = B <<< 9
	; B = A
	; A = TT1
	; H = G
	; G = F <<< 19
	; F = E
	; E = P(TT2)
	vmovups D,C
	vprold B,9
	vmovups C,B
	vmovups B,A
	vmovups A,TMP3
	vmovups H,G
	vprold F,19
	vmovups G,F
	vmovups F,E
	P TMP2
	vmovups E,TMP0

	%assign I (I+1)
%endrep
	; Xor old digest
        vpxord		A, A, [rsp + _DIGEST_SAVE + 64*0]
        vpxord		B, B, [rsp + _DIGEST_SAVE + 64*1]
        vpxord		C, C, [rsp + _DIGEST_SAVE + 64*2]
        vpxord		D, D, [rsp + _DIGEST_SAVE + 64*3]
        vpxord		E, E, [rsp + _DIGEST_SAVE + 64*4]
        vpxord		F, F, [rsp + _DIGEST_SAVE + 64*5]
        vpxord		G, G, [rsp + _DIGEST_SAVE + 64*6]
        vpxord		H, H, [rsp + _DIGEST_SAVE + 64*7]

	%assign cur_loop cur_loop+1
	sub 	SIZE, 1
	je	last_loop

	jmp	lloop


last_loop:

%assign I 0
%rep 8
        mov    inp0, [IN + (2*I)*8]
        mov    inp1, [IN + (2*I +1)*8]
        add    inp0, IDX
        add    inp1, IDX
        mov    [IN + (2*I)*8], inp0
        mov    [IN + (2*I+1)*8], inp1
%assign I (I+1)
%endrep
	; Write out digest
	vmovups	[DIGEST + 0*64], A
	vmovups	[DIGEST + 1*64], B
	vmovups	[DIGEST + 2*64], C
	vmovups	[DIGEST + 3*64], D
	vmovups	[DIGEST + 4*64], E
	vmovups	[DIGEST + 5*64], F
	vmovups	[DIGEST + 6*64], G
	vmovups	[DIGEST + 7*64], H


        mov     rsp, [rsp + _rsp]
        ret


section .data
align 64
TABLE:
	dq 0x79cc451979cc4519,0x79cc451979cc4519
	dq 0x79cc451979cc4519,0x79cc451979cc4519
	dq 0x79cc451979cc4519,0x79cc451979cc4519
	dq 0x79cc451979cc4519,0x79cc451979cc4519
	dq 0xf3988a32f3988a32,0xf3988a32f3988a32
	dq 0xf3988a32f3988a32,0xf3988a32f3988a32
	dq 0xf3988a32f3988a32,0xf3988a32f3988a32
	dq 0xf3988a32f3988a32,0xf3988a32f3988a32
	dq 0xe7311465e7311465,0xe7311465e7311465
	dq 0xe7311465e7311465,0xe7311465e7311465
	dq 0xe7311465e7311465,0xe7311465e7311465
	dq 0xe7311465e7311465,0xe7311465e7311465
	dq 0xce6228cbce6228cb,0xce6228cbce6228cb
	dq 0xce6228cbce6228cb,0xce6228cbce6228cb
	dq 0xce6228cbce6228cb,0xce6228cbce6228cb
	dq 0xce6228cbce6228cb,0xce6228cbce6228cb
	dq 0x9cc451979cc45197,0x9cc451979cc45197
	dq 0x9cc451979cc45197,0x9cc451979cc45197
	dq 0x9cc451979cc45197,0x9cc451979cc45197
	dq 0x9cc451979cc45197,0x9cc451979cc45197
	dq 0x3988a32f3988a32f,0x3988a32f3988a32f
	dq 0x3988a32f3988a32f,0x3988a32f3988a32f
	dq 0x3988a32f3988a32f,0x3988a32f3988a32f
	dq 0x3988a32f3988a32f,0x3988a32f3988a32f
	dq 0x7311465e7311465e,0x7311465e7311465e
	dq 0x7311465e7311465e,0x7311465e7311465e
	dq 0x7311465e7311465e,0x7311465e7311465e
	dq 0x7311465e7311465e,0x7311465e7311465e
	dq 0xe6228cbce6228cbc,0xe6228cbce6228cbc
	dq 0xe6228cbce6228cbc,0xe6228cbce6228cbc
	dq 0xe6228cbce6228cbc,0xe6228cbce6228cbc
	dq 0xe6228cbce6228cbc,0xe6228cbce6228cbc
	dq 0xcc451979cc451979,0xcc451979cc451979
	dq 0xcc451979cc451979,0xcc451979cc451979
	dq 0xcc451979cc451979,0xcc451979cc451979
	dq 0xcc451979cc451979,0xcc451979cc451979
	dq 0x988a32f3988a32f3,0x988a32f3988a32f3
	dq 0x988a32f3988a32f3,0x988a32f3988a32f3
	dq 0x988a32f3988a32f3,0x988a32f3988a32f3
	dq 0x988a32f3988a32f3,0x988a32f3988a32f3
	dq 0x311465e7311465e7,0x311465e7311465e7
	dq 0x311465e7311465e7,0x311465e7311465e7
	dq 0x311465e7311465e7,0x311465e7311465e7
	dq 0x311465e7311465e7,0x311465e7311465e7
	dq 0x6228cbce6228cbce,0x6228cbce6228cbce
	dq 0x6228cbce6228cbce,0x6228cbce6228cbce
	dq 0x6228cbce6228cbce,0x6228cbce6228cbce
	dq 0x6228cbce6228cbce,0x6228cbce6228cbce
	dq 0xc451979cc451979c,0xc451979cc451979c
	dq 0xc451979cc451979c,0xc451979cc451979c
	dq 0xc451979cc451979c,0xc451979cc451979c
	dq 0xc451979cc451979c,0xc451979cc451979c
	dq 0x88a32f3988a32f39,0x88a32f3988a32f39
	dq 0x88a32f3988a32f39,0x88a32f3988a32f39
	dq 0x88a32f3988a32f39,0x88a32f3988a32f39
	dq 0x88a32f3988a32f39,0x88a32f3988a32f39
	dq 0x11465e7311465e73,0x11465e7311465e73
	dq 0x11465e7311465e73,0x11465e7311465e73
	dq 0x11465e7311465e73,0x11465e7311465e73
	dq 0x11465e7311465e73,0x11465e7311465e73
	dq 0x228cbce6228cbce6,0x228cbce6228cbce6
	dq 0x228cbce6228cbce6,0x228cbce6228cbce6
	dq 0x228cbce6228cbce6,0x228cbce6228cbce6
	dq 0x228cbce6228cbce6,0x228cbce6228cbce6
	dq 0x9d8a7a879d8a7a87,0x9d8a7a879d8a7a87
	dq 0x9d8a7a879d8a7a87,0x9d8a7a879d8a7a87
	dq 0x9d8a7a879d8a7a87,0x9d8a7a879d8a7a87
	dq 0x9d8a7a879d8a7a87,0x9d8a7a879d8a7a87
	dq 0x3b14f50f3b14f50f,0x3b14f50f3b14f50f
	dq 0x3b14f50f3b14f50f,0x3b14f50f3b14f50f
	dq 0x3b14f50f3b14f50f,0x3b14f50f3b14f50f
	dq 0x3b14f50f3b14f50f,0x3b14f50f3b14f50f
	dq 0x7629ea1e7629ea1e,0x7629ea1e7629ea1e
	dq 0x7629ea1e7629ea1e,0x7629ea1e7629ea1e
	dq 0x7629ea1e7629ea1e,0x7629ea1e7629ea1e
	dq 0x7629ea1e7629ea1e,0x7629ea1e7629ea1e
	dq 0xec53d43cec53d43c,0xec53d43cec53d43c
	dq 0xec53d43cec53d43c,0xec53d43cec53d43c
	dq 0xec53d43cec53d43c,0xec53d43cec53d43c
	dq 0xec53d43cec53d43c,0xec53d43cec53d43c
	dq 0xd8a7a879d8a7a879,0xd8a7a879d8a7a879
	dq 0xd8a7a879d8a7a879,0xd8a7a879d8a7a879
	dq 0xd8a7a879d8a7a879,0xd8a7a879d8a7a879
	dq 0xd8a7a879d8a7a879,0xd8a7a879d8a7a879
	dq 0xb14f50f3b14f50f3,0xb14f50f3b14f50f3
	dq 0xb14f50f3b14f50f3,0xb14f50f3b14f50f3
	dq 0xb14f50f3b14f50f3,0xb14f50f3b14f50f3
	dq 0xb14f50f3b14f50f3,0xb14f50f3b14f50f3
	dq 0x629ea1e7629ea1e7,0x629ea1e7629ea1e7
	dq 0x629ea1e7629ea1e7,0x629ea1e7629ea1e7
	dq 0x629ea1e7629ea1e7,0x629ea1e7629ea1e7
	dq 0x629ea1e7629ea1e7,0x629ea1e7629ea1e7
	dq 0xc53d43cec53d43ce,0xc53d43cec53d43ce
	dq 0xc53d43cec53d43ce,0xc53d43cec53d43ce
	dq 0xc53d43cec53d43ce,0xc53d43cec53d43ce
	dq 0xc53d43cec53d43ce,0xc53d43cec53d43ce
	dq 0x8a7a879d8a7a879d,0x8a7a879d8a7a879d
	dq 0x8a7a879d8a7a879d,0x8a7a879d8a7a879d
	dq 0x8a7a879d8a7a879d,0x8a7a879d8a7a879d
	dq 0x8a7a879d8a7a879d,0x8a7a879d8a7a879d
	dq 0x14f50f3b14f50f3b,0x14f50f3b14f50f3b
	dq 0x14f50f3b14f50f3b,0x14f50f3b14f50f3b
	dq 0x14f50f3b14f50f3b,0x14f50f3b14f50f3b
	dq 0x14f50f3b14f50f3b,0x14f50f3b14f50f3b
	dq 0x29ea1e7629ea1e76,0x29ea1e7629ea1e76
	dq 0x29ea1e7629ea1e76,0x29ea1e7629ea1e76
	dq 0x29ea1e7629ea1e76,0x29ea1e7629ea1e76
	dq 0x29ea1e7629ea1e76,0x29ea1e7629ea1e76
	dq 0x53d43cec53d43cec,0x53d43cec53d43cec
	dq 0x53d43cec53d43cec,0x53d43cec53d43cec
	dq 0x53d43cec53d43cec,0x53d43cec53d43cec
	dq 0x53d43cec53d43cec,0x53d43cec53d43cec
	dq 0xa7a879d8a7a879d8,0xa7a879d8a7a879d8
	dq 0xa7a879d8a7a879d8,0xa7a879d8a7a879d8
	dq 0xa7a879d8a7a879d8,0xa7a879d8a7a879d8
	dq 0xa7a879d8a7a879d8,0xa7a879d8a7a879d8
	dq 0x4f50f3b14f50f3b1,0x4f50f3b14f50f3b1
	dq 0x4f50f3b14f50f3b1,0x4f50f3b14f50f3b1
	dq 0x4f50f3b14f50f3b1,0x4f50f3b14f50f3b1
	dq 0x4f50f3b14f50f3b1,0x4f50f3b14f50f3b1
	dq 0x9ea1e7629ea1e762,0x9ea1e7629ea1e762
	dq 0x9ea1e7629ea1e762,0x9ea1e7629ea1e762
	dq 0x9ea1e7629ea1e762,0x9ea1e7629ea1e762
	dq 0x9ea1e7629ea1e762,0x9ea1e7629ea1e762
	dq 0x3d43cec53d43cec5,0x3d43cec53d43cec5
	dq 0x3d43cec53d43cec5,0x3d43cec53d43cec5
	dq 0x3d43cec53d43cec5,0x3d43cec53d43cec5
	dq 0x3d43cec53d43cec5,0x3d43cec53d43cec5
	dq 0x7a879d8a7a879d8a,0x7a879d8a7a879d8a
	dq 0x7a879d8a7a879d8a,0x7a879d8a7a879d8a
	dq 0x7a879d8a7a879d8a,0x7a879d8a7a879d8a
	dq 0x7a879d8a7a879d8a,0x7a879d8a7a879d8a
	dq 0xf50f3b14f50f3b14,0xf50f3b14f50f3b14
	dq 0xf50f3b14f50f3b14,0xf50f3b14f50f3b14
	dq 0xf50f3b14f50f3b14,0xf50f3b14f50f3b14
	dq 0xf50f3b14f50f3b14,0xf50f3b14f50f3b14
	dq 0xea1e7629ea1e7629,0xea1e7629ea1e7629
	dq 0xea1e7629ea1e7629,0xea1e7629ea1e7629
	dq 0xea1e7629ea1e7629,0xea1e7629ea1e7629
	dq 0xea1e7629ea1e7629,0xea1e7629ea1e7629
	dq 0xd43cec53d43cec53,0xd43cec53d43cec53
	dq 0xd43cec53d43cec53,0xd43cec53d43cec53
	dq 0xd43cec53d43cec53,0xd43cec53d43cec53
	dq 0xd43cec53d43cec53,0xd43cec53d43cec53
	dq 0xa879d8a7a879d8a7,0xa879d8a7a879d8a7
	dq 0xa879d8a7a879d8a7,0xa879d8a7a879d8a7
	dq 0xa879d8a7a879d8a7,0xa879d8a7a879d8a7
	dq 0xa879d8a7a879d8a7,0xa879d8a7a879d8a7
	dq 0x50f3b14f50f3b14f,0x50f3b14f50f3b14f
	dq 0x50f3b14f50f3b14f,0x50f3b14f50f3b14f
	dq 0x50f3b14f50f3b14f,0x50f3b14f50f3b14f
	dq 0x50f3b14f50f3b14f,0x50f3b14f50f3b14f
	dq 0xa1e7629ea1e7629e,0xa1e7629ea1e7629e
	dq 0xa1e7629ea1e7629e,0xa1e7629ea1e7629e
	dq 0xa1e7629ea1e7629e,0xa1e7629ea1e7629e
	dq 0xa1e7629ea1e7629e,0xa1e7629ea1e7629e
	dq 0x43cec53d43cec53d,0x43cec53d43cec53d
	dq 0x43cec53d43cec53d,0x43cec53d43cec53d
	dq 0x43cec53d43cec53d,0x43cec53d43cec53d
	dq 0x43cec53d43cec53d,0x43cec53d43cec53d
	dq 0x879d8a7a879d8a7a,0x879d8a7a879d8a7a
	dq 0x879d8a7a879d8a7a,0x879d8a7a879d8a7a
	dq 0x879d8a7a879d8a7a,0x879d8a7a879d8a7a
	dq 0x879d8a7a879d8a7a,0x879d8a7a879d8a7a
	dq 0x0f3b14f50f3b14f5,0x0f3b14f50f3b14f5
	dq 0x0f3b14f50f3b14f5,0x0f3b14f50f3b14f5
	dq 0x0f3b14f50f3b14f5,0x0f3b14f50f3b14f5
	dq 0x0f3b14f50f3b14f5,0x0f3b14f50f3b14f5
	dq 0x1e7629ea1e7629ea,0x1e7629ea1e7629ea
	dq 0x1e7629ea1e7629ea,0x1e7629ea1e7629ea
	dq 0x1e7629ea1e7629ea,0x1e7629ea1e7629ea
	dq 0x1e7629ea1e7629ea,0x1e7629ea1e7629ea
	dq 0x3cec53d43cec53d4,0x3cec53d43cec53d4
	dq 0x3cec53d43cec53d4,0x3cec53d43cec53d4
	dq 0x3cec53d43cec53d4,0x3cec53d43cec53d4
	dq 0x3cec53d43cec53d4,0x3cec53d43cec53d4
	dq 0x79d8a7a879d8a7a8,0x79d8a7a879d8a7a8
	dq 0x79d8a7a879d8a7a8,0x79d8a7a879d8a7a8
	dq 0x79d8a7a879d8a7a8,0x79d8a7a879d8a7a8
	dq 0x79d8a7a879d8a7a8,0x79d8a7a879d8a7a8
	dq 0xf3b14f50f3b14f50,0xf3b14f50f3b14f50
	dq 0xf3b14f50f3b14f50,0xf3b14f50f3b14f50
	dq 0xf3b14f50f3b14f50,0xf3b14f50f3b14f50
	dq 0xf3b14f50f3b14f50,0xf3b14f50f3b14f50
	dq 0xe7629ea1e7629ea1,0xe7629ea1e7629ea1
	dq 0xe7629ea1e7629ea1,0xe7629ea1e7629ea1
	dq 0xe7629ea1e7629ea1,0xe7629ea1e7629ea1
	dq 0xe7629ea1e7629ea1,0xe7629ea1e7629ea1
	dq 0xcec53d43cec53d43,0xcec53d43cec53d43
	dq 0xcec53d43cec53d43,0xcec53d43cec53d43
	dq 0xcec53d43cec53d43,0xcec53d43cec53d43
	dq 0xcec53d43cec53d43,0xcec53d43cec53d43
	dq 0x9d8a7a879d8a7a87,0x9d8a7a879d8a7a87
	dq 0x9d8a7a879d8a7a87,0x9d8a7a879d8a7a87
	dq 0x9d8a7a879d8a7a87,0x9d8a7a879d8a7a87
	dq 0x9d8a7a879d8a7a87,0x9d8a7a879d8a7a87
	dq 0x3b14f50f3b14f50f,0x3b14f50f3b14f50f
	dq 0x3b14f50f3b14f50f,0x3b14f50f3b14f50f
	dq 0x3b14f50f3b14f50f,0x3b14f50f3b14f50f
	dq 0x3b14f50f3b14f50f,0x3b14f50f3b14f50f
	dq 0x7629ea1e7629ea1e,0x7629ea1e7629ea1e
	dq 0x7629ea1e7629ea1e,0x7629ea1e7629ea1e
	dq 0x7629ea1e7629ea1e,0x7629ea1e7629ea1e
	dq 0x7629ea1e7629ea1e,0x7629ea1e7629ea1e
	dq 0xec53d43cec53d43c,0xec53d43cec53d43c
	dq 0xec53d43cec53d43c,0xec53d43cec53d43c
	dq 0xec53d43cec53d43c,0xec53d43cec53d43c
	dq 0xec53d43cec53d43c,0xec53d43cec53d43c
	dq 0xd8a7a879d8a7a879,0xd8a7a879d8a7a879
	dq 0xd8a7a879d8a7a879,0xd8a7a879d8a7a879
	dq 0xd8a7a879d8a7a879,0xd8a7a879d8a7a879
	dq 0xd8a7a879d8a7a879,0xd8a7a879d8a7a879
	dq 0xb14f50f3b14f50f3,0xb14f50f3b14f50f3
	dq 0xb14f50f3b14f50f3,0xb14f50f3b14f50f3
	dq 0xb14f50f3b14f50f3,0xb14f50f3b14f50f3
	dq 0xb14f50f3b14f50f3,0xb14f50f3b14f50f3
	dq 0x629ea1e7629ea1e7,0x629ea1e7629ea1e7
	dq 0x629ea1e7629ea1e7,0x629ea1e7629ea1e7
	dq 0x629ea1e7629ea1e7,0x629ea1e7629ea1e7
	dq 0x629ea1e7629ea1e7,0x629ea1e7629ea1e7
	dq 0xc53d43cec53d43ce,0xc53d43cec53d43ce
	dq 0xc53d43cec53d43ce,0xc53d43cec53d43ce
	dq 0xc53d43cec53d43ce,0xc53d43cec53d43ce
	dq 0xc53d43cec53d43ce,0xc53d43cec53d43ce
	dq 0x8a7a879d8a7a879d,0x8a7a879d8a7a879d
	dq 0x8a7a879d8a7a879d,0x8a7a879d8a7a879d
	dq 0x8a7a879d8a7a879d,0x8a7a879d8a7a879d
	dq 0x8a7a879d8a7a879d,0x8a7a879d8a7a879d
	dq 0x14f50f3b14f50f3b,0x14f50f3b14f50f3b
	dq 0x14f50f3b14f50f3b,0x14f50f3b14f50f3b
	dq 0x14f50f3b14f50f3b,0x14f50f3b14f50f3b
	dq 0x14f50f3b14f50f3b,0x14f50f3b14f50f3b
	dq 0x29ea1e7629ea1e76,0x29ea1e7629ea1e76
	dq 0x29ea1e7629ea1e76,0x29ea1e7629ea1e76
	dq 0x29ea1e7629ea1e76,0x29ea1e7629ea1e76
	dq 0x29ea1e7629ea1e76,0x29ea1e7629ea1e76
	dq 0x53d43cec53d43cec,0x53d43cec53d43cec
	dq 0x53d43cec53d43cec,0x53d43cec53d43cec
	dq 0x53d43cec53d43cec,0x53d43cec53d43cec
	dq 0x53d43cec53d43cec,0x53d43cec53d43cec
	dq 0xa7a879d8a7a879d8,0xa7a879d8a7a879d8
	dq 0xa7a879d8a7a879d8,0xa7a879d8a7a879d8
	dq 0xa7a879d8a7a879d8,0xa7a879d8a7a879d8
	dq 0xa7a879d8a7a879d8,0xa7a879d8a7a879d8
	dq 0x4f50f3b14f50f3b1,0x4f50f3b14f50f3b1
	dq 0x4f50f3b14f50f3b1,0x4f50f3b14f50f3b1
	dq 0x4f50f3b14f50f3b1,0x4f50f3b14f50f3b1
	dq 0x4f50f3b14f50f3b1,0x4f50f3b14f50f3b1
	dq 0x9ea1e7629ea1e762,0x9ea1e7629ea1e762
	dq 0x9ea1e7629ea1e762,0x9ea1e7629ea1e762
	dq 0x9ea1e7629ea1e762,0x9ea1e7629ea1e762
	dq 0x9ea1e7629ea1e762,0x9ea1e7629ea1e762
	dq 0x3d43cec53d43cec5,0x3d43cec53d43cec5
	dq 0x3d43cec53d43cec5,0x3d43cec53d43cec5
	dq 0x3d43cec53d43cec5,0x3d43cec53d43cec5
	dq 0x3d43cec53d43cec5,0x3d43cec53d43cec5



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

SHUF_MASK:			dq 0x0405060700010203,0x0c0d0e0f08090a0b
				dq 0x0405060700010203,0x0c0d0e0f08090a0b
				dq 0x0405060700010203,0x0c0d0e0f08090a0b
				dq 0x0405060700010203,0x0c0d0e0f08090a0b

%else
%ifidn __OUTPUT_FORMAT__, win64
global no_sm3_mb_x16_avx512
no_sm3_mb_x16_avx512:
%endif
%endif ; HAVE_AS_KNOWS_AVX512

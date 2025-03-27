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

%ifndef _TRANSPOSE_AVX512_ASM_
%define _TRANSPOSE_AVX512_ASM_

%include "include/os.asm"
%include "include/reg_sizes.asm"

mksection .rodata
default rel
align 64
PSHUFFLE_TRANSPOSE_MASK1: 	dq 0x0000000000000000
				dq 0x0000000000000001
				dq 0x0000000000000008
				dq 0x0000000000000009
				dq 0x0000000000000004
				dq 0x0000000000000005
				dq 0x000000000000000C
				dq 0x000000000000000D

align 64
PSHUFFLE_TRANSPOSE_MASK2: 	dq 0x0000000000000002
				dq 0x0000000000000003
				dq 0x000000000000000A
				dq 0x000000000000000B
				dq 0x0000000000000006
				dq 0x0000000000000007
				dq 0x000000000000000E
				dq 0x000000000000000F

; LOAD FIRST 8 LANES FOR 16x16 32-BIT TRANSPOSE
;
; r0-r15 [out] zmm registers which will contain the data to be transposed
; addr0-addr7 [in] pointers to the next 64-byte block of data to be fetch for the first 8 lanes
; ptr_offset [in] offset to be applied on all pointers (addr0-addr7)
%macro TRANSPOSE16_U32_LOAD_FIRST8 25
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
%define %%addr0 %17
%define %%addr1 %18
%define %%addr2 %19
%define %%addr3 %20
%define %%addr4 %21
%define %%addr5 %22
%define %%addr6 %23
%define %%addr7 %24
%define %%ptr_offset %25

; Expected output data
;
; r0  = {X X X X  X X X X  a7  a6  a5  a4    a3  a2  a1 a0}
; r1  = {X X X X  X X X X  b7  b6  b5  b4    b3  b2  b1 b0}
; r2  = {X X X X  X X X X  c7  c6  c5  c4    c3  c2  c1 c0}
; r3  = {X X X X  X X X X  d7  d6  d5  d4    d3  d2  d1 d0}
; r4  = {X X X X  X X X X  e7  e6  e5  e4    e3  e2  e1 e0}
; r5  = {X X X X  X X X X  f7  f6  f5  f4    f3  f2  f1 f0}
; r6  = {X X X X  X X X X  g7  g6  g5  g4    g3  g2  g1 g0}
; r7  = {X X X X  X X X X  h7  h6  h5  h4    h3  h2  h1 h0}
; r8  = {X X X X  X X X X  a15 a14 a13 a12   a11 a10 a9 a8}
; r9  = {X X X X  X X X X  b15 b14 b13 b12   b11 b10 b9 b8}
; r10 = {X X X X  X X X X  c15 c14 c13 c12   c11 c10 c9 c8}
; r11 = {X X X X  X X X X  d15 d14 d13 d12   d11 d10 d9 d8}
; r12 = {X X X X  X X X X  e15 e14 e13 e12   e11 e10 e9 e8}
; r13 = {X X X X  X X X X  f15 f14 f13 f12   f11 f10 f9 f8}
; r14 = {X X X X  X X X X  g15 g14 g13 g12   g11 g10 g9 g8}
; r15 = {X X X X  X X X X  h15 h14 h13 h12   h11 h10 h9 h8}
	vmovups	YWORD(%%r0),[%%addr0+%%ptr_offset]
	vmovups	YWORD(%%r1),[%%addr1+%%ptr_offset]
	vmovups	YWORD(%%r2),[%%addr2+%%ptr_offset]
	vmovups	YWORD(%%r3),[%%addr3+%%ptr_offset]
	vmovups	YWORD(%%r4),[%%addr4+%%ptr_offset]
	vmovups	YWORD(%%r5),[%%addr5+%%ptr_offset]
	vmovups	YWORD(%%r6),[%%addr6+%%ptr_offset]
	vmovups	YWORD(%%r7),[%%addr7+%%ptr_offset]
	vmovups	YWORD(%%r8),[%%addr0+%%ptr_offset+32]
	vmovups	YWORD(%%r9),[%%addr1+%%ptr_offset+32]
	vmovups	YWORD(%%r10),[%%addr2+%%ptr_offset+32]
	vmovups	YWORD(%%r11),[%%addr3+%%ptr_offset+32]
	vmovups	YWORD(%%r12),[%%addr4+%%ptr_offset+32]
	vmovups	YWORD(%%r13),[%%addr5+%%ptr_offset+32]
	vmovups	YWORD(%%r14),[%%addr6+%%ptr_offset+32]
	vmovups	YWORD(%%r15),[%%addr7+%%ptr_offset+32]

%endmacro

; LOAD LAST 8 LANES FOR 16x16 32-BIT TRANSPOSE
;
; r0-r15 [in/out] zmm registers which will contain the data to be transposed
; addr0-addr7 [in] pointers to the next 64-byte block of data to be fetch for the last 8 lanes
; ptr_offset [in] offset to be applied on all pointers (addr0-addr7)
%macro TRANSPOSE16_U32_LOAD_LAST8 25
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
%define %%addr0 %17
%define %%addr1 %18
%define %%addr2 %19
%define %%addr3 %20
%define %%addr4 %21
%define %%addr5 %22
%define %%addr6 %23
%define %%addr7 %24
%define %%ptr_offset %25

; Expected output data
;
; r0  = {i7  i6  i5  i4    i3  i2  i1 i0   a7  a6  a5  a4    a3  a2  a1 a0}
; r1  = {j7  j6  j5  j4    j3  j2  j1 j0   b7  b6  b5  b4    b3  b2  b1 b0}
; r2  = {k7  k6  k5  k4    k3  k2  k1 k0   c7  c6  c5  c4    c3  c2  c1 c0}
; r3  = {l7  l6  l5  l4    l3  l2  l1 l0   d7  d6  d5  d4    d3  d2  d1 d0}
; r4  = {m7  m6  m5  m4    m3  m2  m1 m0   e7  e6  e5  e4    e3  e2  e1 e0}
; r5  = {n7  n6  n5  n4    n3  n2  n1 n0   f7  f6  f5  f4    f3  f2  f1 f0}
; r6  = {o7  o6  o5  o4    o3  o2  o1 o0   g7  g6  g5  g4    g3  g2  g1 g0}
; r7  = {p7  p6  p5  p4    p3  p2  p1 p0   h7  h6  h5  h4    h3  h2  h1 h0}
; r8  = {i15 i14 i13 i12   i11 i10 i9 i8   a15 a14 a13 a12   a11 a10 a9 a8}
; r9  = {j15 j14 j13 j12   j11 j10 j9 j8   b15 b14 b13 b12   b11 b10 b9 b8}
; r10 = {k15 k14 k13 k12   k11 k10 k9 k8   c15 c14 c13 c12   c11 c10 c9 c8}
; r11 = {l15 l14 l13 l12   l11 l10 l9 l8   d15 d14 d13 d12   d11 d10 d9 d8}
; r12 = {m15 m14 m13 m12   m11 m10 m9 m8   e15 e14 e13 e12   e11 e10 e9 e8}
; r13 = {n15 n14 n13 n12   n11 n10 n9 n8   f15 f14 f13 f12   f11 f10 f9 f8}
; r14 = {o15 o14 o13 o12   o11 o10 o9 o8   g15 g14 g13 g12   g11 g10 g9 g8}
; r15 = {p15 p14 p13 p12   p11 p10 p9 p8   h15 h14 h13 h12   h11 h10 h9 h8}

	vinserti64x4 %%r0, %%r0, [%%addr0+%%ptr_offset], 0x01
	vinserti64x4 %%r1, %%r1, [%%addr1+%%ptr_offset], 0x01
	vinserti64x4 %%r2, %%r2, [%%addr2+%%ptr_offset], 0x01
	vinserti64x4 %%r3, %%r3, [%%addr3+%%ptr_offset], 0x01
	vinserti64x4 %%r4, %%r4, [%%addr4+%%ptr_offset], 0x01
	vinserti64x4 %%r5, %%r5, [%%addr5+%%ptr_offset], 0x01
	vinserti64x4 %%r6, %%r6, [%%addr6+%%ptr_offset], 0x01
	vinserti64x4 %%r7, %%r7, [%%addr7+%%ptr_offset], 0x01
	vinserti64x4 %%r8, %%r8, [%%addr0+%%ptr_offset+32], 0x01
	vinserti64x4 %%r9, %%r9, [%%addr1+%%ptr_offset+32], 0x01
	vinserti64x4 %%r10, %%r10, [%%addr2+%%ptr_offset+32], 0x01
	vinserti64x4 %%r11, %%r11, [%%addr3+%%ptr_offset+32], 0x01
	vinserti64x4 %%r12, %%r12, [%%addr4+%%ptr_offset+32], 0x01
	vinserti64x4 %%r13, %%r13, [%%addr5+%%ptr_offset+32], 0x01
	vinserti64x4 %%r14, %%r14, [%%addr6+%%ptr_offset+32], 0x01
	vinserti64x4 %%r15, %%r15, [%%addr7+%%ptr_offset+32], 0x01

%endmacro

; 16x16 32-BIT TRANSPOSE AFTER INTERLEAVED LOADS
;
; Before calling this macro, TRANSPOSE16_U32_LOAD_FIRST8 and TRANSPOSE16_U32_LOAD_LAST8
; must be called.
;
; r0-r7 [in/out] zmm registers containing bytes 0-31 of each 64B block (e.g. zmm0 = [i7-i0 a7-a0])
; r8-r15 [in/out] zmm registers containing bytes 32-63 of each 64B block (e.g. zmm8 = [i15-i8 a15-a8])
; t0-t1 [clobbered] zmm temporary registers
; m0-m1 [clobbered] zmm registers for shuffle mask storing
%macro TRANSPOSE16_U32_PRELOADED 20
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
%define %%m0 %19
%define %%m1 %20

; Input data
;
; r0  = {i7  i6  i5  i4    i3  i2  i1 i0   a7  a6  a5  a4    a3  a2  a1 a0}
; r1  = {j7  j6  j5  j4    j3  j2  j1 j0   b7  b6  b5  b4    b3  b2  b1 b0}
; r2  = {k7  k6  k5  k4    k3  k2  k1 k0   c7  c6  c5  c4    c3  c2  c1 c0}
; r3  = {l7  l6  l5  l4    l3  l2  l1 l0   d7  d6  d5  d4    d3  d2  d1 d0}
; r4  = {m7  m6  m5  m4    m3  m2  m1 m0   e7  e6  e5  e4    e3  e2  e1 e0}
; r5  = {n7  n6  n5  n4    n3  n2  n1 n0   f7  f6  f5  f4    f3  f2  f1 f0}
; r6  = {o7  o6  o5  o4    o3  o2  o1 o0   g7  g6  g5  g4    g3  g2  g1 g0}
; r7  = {p7  p6  p5  p4    p3  p2  p1 p0   h7  h6  h5  h4    h3  h2  h1 h0}
; r8  = {i15 i14 i13 i12   i11 i10 i9 i8   a15 a14 a13 a12   a11 a10 a9 a8}
; r9  = {j15 j14 j13 j12   j11 j10 j9 j8   b15 b14 b13 b12   b11 b10 b9 b8}
; r10 = {k15 k14 k13 k12   k11 k10 k9 k8   c15 c14 c13 c12   c11 c10 c9 c8}
; r11 = {l15 l14 l13 l12   l11 l10 l9 l8   d15 d14 d13 d12   d11 d10 d9 d8}
; r12 = {m15 m14 m13 m12   m11 m10 m9 m8   e15 e14 e13 e12   e11 e10 e9 e8}
; r13 = {n15 n14 n13 n12   n11 n10 n9 n8   f15 f14 f13 f12   f11 f10 f9 f8}
; r14 = {o15 o14 o13 o12   o11 o10 o9 o8   g15 g14 g13 g12   g11 g10 g9 g8}
; r15 = {p15 p14 p13 p12   p11 p10 p9 p8   h15 h14 h13 h12   h11 h10 h9 h8}

; Expected output data
;
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

	; process first 4 rows (r0..r3)
	vshufps	%%t0, %%r0, %%r1, 0x44	; t0 = {j5 j4 i5 i4  j1 j0 i1 i0  b5 b4 a5 a4  b1 b0 a1 a0}
	vshufps	%%r0, %%r0, %%r1, 0xEE	; r0 = {j7 j6 i7 i6  j3 j2 i3 i2  b7 b6 a7 a6  b3 b2 a3 a2}
	vshufps	%%t1, %%r2, %%r3, 0x44	; t1 = {l5 l4 k5 k4  l1 l0 k1 k0  d5 d4 c5 c4  d1 d0 c1 c0}
	vshufps	%%r2, %%r2, %%r3, 0xEE	; r2 = {l7 l6 k7 k6  l3 l2 k3 k2  d7 d6 c7 c6  d3 d2 c3 c2}

	vshufps	%%r3, %%t0, %%t1, 0xDD	; r3 = {l5 k5 j5 i5  l1 k1 j1 i1  d5 c5 b5 a5  d1 c1 b1 a1}
	vshufps	%%r1, %%r0, %%r2, 0x88	; r1 = {l6 k6 j6 i6  l2 k2 j2 i2  d6 c6 b6 a6  d2 c2 b2 a2}
	vshufps	%%r0, %%r0, %%r2, 0xDD	; r0 = {l7 k7 j7 i7  l3 k3 j3 i3  d7 c7 b7 a7  d3 c3 b3 a3}
	vshufps	%%t0, %%t0, %%t1, 0x88	; t0 = {l4 k4 j4 i4  l0 k0 j0 i0  d4 c4 b4 a4  d0 c0 b0 a0}

	; Load permute masks
	vmovdqa64	%%m0, [PSHUFFLE_TRANSPOSE_MASK1]
	vmovdqa64	%%m1, [PSHUFFLE_TRANSPOSE_MASK2]

	; process second 4 rows (r4..r7)
	vshufps	%%r2, %%r4, %%r5, 0x44	; r2 = {n5 n4 m5 m4  n1 n0 m1 m0  f5 f4 e5 e4  f1 f0 e1 e0}
	vshufps	%%r4, %%r4, %%r5, 0xEE	; r4 = {n7 n6 m7 m6  n3 n2 m3 m2  f7 f6 e7 e6  f3 f2 e3 e2}
	vshufps %%t1, %%r6, %%r7, 0x44	; t1 = {p5 p4 o5 o4  p1 p0 o1 o0  h5 h4 g5 g4  h1 h0 g1 g0}
	vshufps	%%r6, %%r6, %%r7, 0xEE	; r6 = {p7 p6 o7 o6  p3 p2 o3 o2  h7 h6 g7 g6  h3 h2 g3 g2}

	vshufps	%%r7, %%r2, %%t1, 0xDD	; r7 = {p5 o5 n5 m5  p1 o1 n1 m1  h5 g5 f5 e5   h1 g1 f1 e1}
	vshufps	%%r5, %%r4, %%r6, 0x88	; r5 = {p6 o6 n6 m6  p2 o2 n2 m2  h6 g6 f6 e6   h2 g2 f2 e2}
	vshufps	%%r4, %%r4, %%r6, 0xDD	; r4 = {p7 o7 n7 m7  p3 o3 n3 m3  h7 g7 f7 e7   h3 g3 f3 e3}
	vshufps	%%r2, %%r2, %%t1, 0x88	; r2 = {p4 o4 n4 m4  p0 o0 n0 m0  h4 g4 f4 e4   h0 g0 f0 e0}

	; process third 4 rows (r8..r11)
	vshufps	%%r6, %%r8, %%r9,    0x44	; r6  = {j13 j12 i13 i12  j9  j8  i9  i8   b13 b12 a13 a12  b9  b8  a9  a8 }
	vshufps	%%r8, %%r8, %%r9,    0xEE	; r8  = {j15 j14 i15 i14  j11 j10 i11 i10  b15 b14 a15 a14  b11 b10 a11 a10}
	vshufps	%%t1, %%r10, %%r11,  0x44	; t1  = {l13 l12 k13 k12  l9  l8  k9  k8   d13 d12 c13 c12  d9  d8  c9  c8 }
	vshufps	%%r10, %%r10, %%r11, 0xEE	; r10 = {l15 l14 k15 k14  l11 l10 k11 k10  d15 d14 c15 c14  d11 d10 c11 c10}

	vshufps	%%r11, %%r6, %%t1, 0xDD		; r11 = {l13 k13 j13 i13  l9  k9  j9  i9   d13 c13 b13 a13  d9  c9  b9  a9 }
	vshufps	%%r9, %%r8, %%r10, 0x88		; r9  = {l14 k14 j14 i14  l10 k10 j10 i10  d14 c14 b14 a14  d10 c10 b10 a10}
	vshufps	%%r8, %%r8, %%r10, 0xDD		; r8  = {l15 k15 j15 i15  l11 k11 j11 i11  d15 c15 b15 a15  d11 c11 b11 a11}
	vshufps	%%r6, %%r6, %%t1,  0x88		; r6  = {l12 k12 j12 i12  l8  k8  j8  i8   d12 c12 b12 a12  d8  c8  b8  a8 }

	; process fourth 4 rows (r12..r15)
	vshufps	%%r10, %%r12, %%r13, 0x44	; r10 = {n13 n12 m13 m12  n9  n8  m9  m8   f13 f12 e13 e12  f9  f8  e9  e8 }
	vshufps	%%r12, %%r12, %%r13, 0xEE	; r12 = {n15 n14 m15 m14  n11 n10 m11 m10  f15 f14 e15 e14  f11 f10 e11 e10}
	vshufps	%%t1, %%r14, %%r15,  0x44	; t1  = {p13 p12 o13 o12  p9  p8  o9  o8   h13 h12 g13 g12  h9  h8  g9  g8 }
	vshufps	%%r14, %%r14, %%r15, 0xEE	; r14 = {p15 p14 o15 o14  p11 p10 o11 o10  h15 h14 g15 g14  h11 h10 g11 g10}

	vshufps	%%r15, %%r10, %%t1,  0xDD	; r15 = {p13 o13 n13 m13  p9  o9  n9  m9   h13 g13 f13 e13  h9  g9  f9  e9 }
	vshufps	%%r13, %%r12, %%r14, 0x88	; r13 = {p14 o14 n14 m14  p10 o10 n10 m10  h14 g14 f14 e14  h10 g10 f10 e10}
	vshufps	%%r12, %%r12, %%r14, 0xDD	; r12 = {p15 o15 n15 m15  p11 o11 n11 m11  h15 g15 f15 e15  h11 g11 f11 e11}
	vshufps	%%r10, %%r10, %%t1,  0x88	; r10 = {p12 o12 n12 m12  p8  o8  n8  m8   h12 g12 f12 e12  h8  g8  f8  e8 }

	; perform final shuffles on bottom half, producing r8-r15
	vmovdqu32 %%t1, %%m0
	vpermi2q  %%t1, %%r9, %%r13		; t1 =  {p10 o10 n10 m10  l10 k10 j10 i10  h10 g10 f10 e10  d10 c10 b10 a10}
	vmovdqu32 %%r14, %%m1
	vpermi2q  %%r14, %%r9, %%r13		; r14 = {p14 o14 n14 m14  l14 k14 j14 i14  h14 g14 f14 e14  d14 c14 b14 a14}

	vmovdqu32 %%r9, %%m0
	vpermi2q  %%r9, %%r11, %%r15		; r9  = {p9  o9  n9  m9   l9  k9  j9  i9   h9  g9  f9  e9   d9  c9  b9  a9}
	vmovdqu32 %%r13, %%m1
	vpermi2q  %%r13, %%r11, %%r15		; r13 = {p13 o13 n13 m13  l13 k13 j13 i13  h13 g13 f13 e13  d13 c13 b13 a13}

	vmovdqu32 %%r11, %%m0
	vpermi2q  %%r11, %%r8, %%r12		; r11 = {p11 o11 n11 m11  l11 k11 j11 i11  h11 g11 f11 e11  d11 c11 b11 a11}
	vmovdqu32 %%r15, %%m1
	vpermi2q  %%r15, %%r8, %%r12		; r15 = {p15 o15 n15 m15  l15 k15 j15 i15  h15 g15 f15 e15  d15 c15 b15 a15}

	vmovdqu32 %%r8, %%m0
	vpermi2q  %%r8, %%r6, %%r10		; r8  = {p8  o8  n8  m8   l8  k8  j8  i8   h8  g8  f8  e8    d8  c8  b8  a8}
	vmovdqu32 %%r12, %%m1
	vpermi2q  %%r12, %%r6, %%r10		; r12 = {p12 o12 n12 m12  l12 k12 j12 i12  h12 g12 f12 e12   d12 c12 b12 a12}

	vmovdqu32 %%r10, %%t1			; r10 = {p10 o10 n10 m10  l10 k10 j10 i10  h10 g10 f10 e10  d10 c10 b10 a10}

	; perform final shuffles on top half, producing r0-r7
	vmovdqu32 %%t1, %%m0
	vpermi2q  %%t1, %%r1, %%r5		; t1 = {p2 o2 n2 m2  l2 k2 j2 i2  h2 g2 f2 e2  d2 c2 b2 a2}
	vmovdqu32 %%r6, %%m1
	vpermi2q  %%r6, %%r1, %%r5		; r6 = {p6 o6 n6 m6  l6 k6 j6 i6  h6 g6 f6 e6  d6 c6 b6 a6}

	vmovdqu32 %%r1, %%m0
	vpermi2q  %%r1, %%r3, %%r7		; r1 = {p1 o1 n1 m1  l1 k1 j1 i1  h1 g1 f1 e1  d1 c1 b1 a1}
	vmovdqu32 %%r5, %%m1
	vpermi2q  %%r5, %%r3, %%r7		; r5 = {p5 o5 n5 m5  l5 k5 j5 i5  h5 g5 f5 e5  d5 c5 b5 a5}

	vmovdqu32 %%r3, %%m0
	vpermi2q  %%r3, %%r0, %%r4		; r3 = {p3 o3 n3 m3  l3 k3 j3 i3  h3 g3 f3 e3  d3 c3 b3 a3}
	vmovdqu32 %%r7, %%m1
	vpermi2q  %%r7, %%r0, %%r4		; r7 = {p7 o7 n7 m7  l7 k7 j7 i7  h7 g7 f7 e7  d7 c7 b7 a7}

	vmovdqu32 %%r0, %%m0
	vpermi2q  %%r0, %%t0, %%r2		; r0 = {p0 o0 n0 m0  l0 k0 j0 i0  h0 g0 f0 e0  d0 c0 b0 a0}
	vmovdqu32 %%r4, %%m1
	vpermi2q  %%r4,  %%t0, %%r2		; r4 = {p4 o4 n4 m4  l4 k4 j4 i4  h4 g4 f4 e4  d4 c4 b4 a4}

	vmovdqu32 %%r2, %%t1			; r2 = {p2 o2 n2 m2  l2 k2 j2 i2  h2 g2 f2 e2  d2 c2 b2 a2}

%endmacro

;;; 16x16 32-BIT TRANSPOSE
;;;
;;; IN00-IN15 aka L0/R0 - L7/R7 [in/out]:
;;;          in:  L0 - 16 x word0, R0 - 16 x word1, ... R7 - 16 x word15
;;;          out: L0 - lane 0 data, R0 - lane 1 data, ... R7 - lane 15 data
;;; T0-T3 [clobbered] - temporary zmm registers
;;; K0-K5 [clobbered] - temporary zmm registers
;;; H0-H3 [clobbered] - temporary zmm registers
%macro TRANSPOSE16_U32 30
%define %%IN00 %1               ; L0
%define %%IN01 %2               ; R0
%define %%IN02 %3               ; L1
%define %%IN03 %4               ; R1
%define %%IN04 %5               ; L2
%define %%IN05 %6               ; R2
%define %%IN06 %7               ; L3
%define %%IN07 %8               ; R3
%define %%IN08 %9               ; L4
%define %%IN09 %10              ; R4
%define %%IN10 %11              ; L5
%define %%IN11 %12              ; R5
%define %%IN12 %13              ; L6
%define %%IN13 %14              ; R6
%define %%IN14 %15              ; L7
%define %%IN15 %16              ; R7
%define %%T0 %17
%define %%T1 %18
%define %%T2 %19
%define %%T3 %20
%define %%K0 %21
%define %%K1 %22
%define %%K2 %23
%define %%K3 %24
%define %%K4 %25
%define %%K5 %26
%define %%H0 %27
%define %%H1 %28
%define %%H2 %29
%define %%H3 %30

        vpunpckldq      %%K0, %%IN00, %%IN01
        vpunpckhdq      %%K1, %%IN00, %%IN01
        vpunpckldq      %%T0, %%IN02, %%IN03
        vpunpckhdq      %%T1, %%IN02, %%IN03

        vpunpckldq      %%IN00, %%IN04, %%IN05
        vpunpckhdq      %%IN01, %%IN04, %%IN05
        vpunpckldq      %%IN02, %%IN06, %%IN07
        vpunpckhdq      %%IN03, %%IN06, %%IN07

        vpunpcklqdq     %%K2, %%K0, %%T0
        vpunpckhqdq     %%T2, %%K0, %%T0
        vpunpcklqdq     %%K3, %%K1, %%T1
        vpunpckhqdq     %%T3, %%K1, %%T1

        vpunpcklqdq     %%K0, %%IN00, %%IN02
        vpunpckhqdq     %%K1, %%IN00, %%IN02
        vpunpcklqdq     %%T0, %%IN01, %%IN03
        vpunpckhqdq     %%T1, %%IN01, %%IN03

        vpunpckldq      %%K4, %%IN08, %%IN09
        vpunpckhdq      %%K5, %%IN08, %%IN09
        vpunpckldq      %%IN04, %%IN10, %%IN11
        vpunpckhdq      %%IN05, %%IN10, %%IN11
        vpunpckldq      %%IN06, %%IN12, %%IN13
        vpunpckhdq      %%IN07, %%IN12, %%IN13
        vpunpckldq      %%IN10, %%IN14, %%IN15
        vpunpckhdq      %%IN11, %%IN14, %%IN15

        vpunpcklqdq     %%IN12, %%K4, %%IN04
        vpunpckhqdq     %%IN13, %%K4, %%IN04
        vpunpcklqdq     %%IN14, %%K5, %%IN05
        vpunpckhqdq     %%IN15, %%K5, %%IN05
        vpunpcklqdq     %%IN00, %%IN06, %%IN10
        vpunpckhqdq     %%IN01, %%IN06, %%IN10
        vpunpcklqdq     %%IN02, %%IN07, %%IN11
        vpunpckhqdq     %%IN03, %%IN07, %%IN11

        vshufi64x2      %%H0, %%K2, %%K0, 0x44
        vshufi64x2      %%H1, %%K2, %%K0, 0xee
        vshufi64x2      %%H2, %%IN12, %%IN00, 0x44
        vshufi64x2      %%H3, %%IN12, %%IN00, 0xee
        vshufi64x2      %%IN00, %%H0, %%H2, 0x88    ; L0
        vshufi64x2      %%IN04, %%H0, %%H2, 0xdd    ; L2
        vshufi64x2      %%IN08, %%H1, %%H3, 0x88    ; L4
        vshufi64x2      %%IN12, %%H1, %%H3, 0xdd    ; L6

        vshufi64x2      %%H0, %%T2, %%K1, 0x44
        vshufi64x2      %%H1, %%T2, %%K1, 0xee
        vshufi64x2      %%H2, %%IN13, %%IN01, 0x44
        vshufi64x2      %%H3, %%IN13, %%IN01, 0xee
        vshufi64x2      %%IN01, %%H0, %%H2, 0x88    ; R0
        vshufi64x2      %%IN05, %%H0, %%H2, 0xdd    ; R2
        vshufi64x2      %%IN09, %%H1, %%H3, 0x88    ; R4
        vshufi64x2      %%IN13, %%H1, %%H3, 0xdd    ; R6

        vshufi64x2      %%H0, %%K3, %%T0, 0x44
        vshufi64x2      %%H1, %%K3, %%T0, 0xee
        vshufi64x2      %%H2, %%IN14, %%IN02, 0x44
        vshufi64x2      %%H3, %%IN14, %%IN02, 0xee
        vshufi64x2      %%IN02, %%H0, %%H2, 0x88    ; L1
        vshufi64x2      %%IN06, %%H0, %%H2, 0xdd    ; L3
        vshufi64x2      %%IN10, %%H1, %%H3, 0x88    ; L5
        vshufi64x2      %%IN14, %%H1, %%H3, 0xdd    ; L7

        vshufi64x2      %%H0, %%T3, %%T1, 0x44
        vshufi64x2      %%H1, %%T3, %%T1, 0xee
        vshufi64x2      %%H2, %%IN15, %%IN03, 0x44
        vshufi64x2      %%H3, %%IN15, %%IN03, 0xee
        vshufi64x2      %%IN03, %%H0, %%H2, 0x88    ; R1
        vshufi64x2      %%IN07, %%H0, %%H2, 0xdd    ; R3
        vshufi64x2      %%IN11, %%H1, %%H3, 0x88    ; R5
        vshufi64x2      %%IN15, %%H1, %%H3, 0xdd    ; R7
%endmacro

;; AVX512 variant of AVX2 TRANSPOSE8_U32 macro
;; - vperm2i128/vperm2f128 replaced with vshufi32x4
%macro TRANSPOSE8_U32_AVX512 10
%define %%r0 %1   ;; [in/out] YMM with a0..a7
%define %%r1 %2   ;; [in/out] YMM with b0..b7
%define %%r2 %3   ;; [in/out] YMM with c0..c7
%define %%r3 %4   ;; [in/out] YMM with d0..d7
%define %%r4 %5   ;; [in/out] YMM with e0..e7
%define %%r5 %6   ;; [in/out] YMM with f0..f7
%define %%r6 %7   ;; [in/out] YMM with g0..g7
%define %%r7 %8   ;; [in/out] YMM with h0..h7
%define %%t0 %9   ;; [clobbered] temporary YMM
%define %%t1 %10  ;; [clobbered] temporary YMM

        ; process top half (r0..r3) {a...d}
        vshufps %%t0, %%r0, %%r1, 0x44  ; t0 = {b5 b4 a5 a4   b1 b0 a1 a0}
        vshufps %%r0, %%r0, %%r1, 0xEE  ; r0 = {b7 b6 a7 a6   b3 b2 a3 a2}
        vshufps %%t1, %%r2, %%r3, 0x44  ; t1 = {d5 d4 c5 c4   d1 d0 c1 c0}
        vshufps %%r2, %%r2, %%r3, 0xEE  ; r2 = {d7 d6 c7 c6   d3 d2 c3 c2}
        vshufps %%r3, %%t0, %%t1, 0xDD  ; r3 = {d5 c5 b5 a5   d1 c1 b1 a1}
        vshufps %%r1, %%r0, %%r2, 0x88  ; r1 = {d6 c6 b6 a6   d2 c2 b2 a2}
        vshufps %%r0, %%r0, %%r2, 0xDD  ; r0 = {d7 c7 b7 a7   d3 c3 b3 a3}
        vshufps %%t0, %%t0, %%t1, 0x88  ; t0 = {d4 c4 b4 a4   d0 c0 b0 a0}

        ; use r2 in place of t0
        ; process bottom half (r4..r7) {e...h}
        vshufps %%r2, %%r4, %%r5, 0x44  ; r2 = {f5 f4 e5 e4   f1 f0 e1 e0}
        vshufps %%r4, %%r4, %%r5, 0xEE  ; r4 = {f7 f6 e7 e6   f3 f2 e3 e2}
        vshufps %%t1, %%r6, %%r7, 0x44  ; t1 = {h5 h4 g5 g4   h1 h0 g1 g0}
        vshufps %%r6, %%r6, %%r7, 0xEE  ; r6 = {h7 h6 g7 g6   h3 h2 g3 g2}
        vshufps %%r7, %%r2, %%t1, 0xDD  ; r7 = {h5 g5 f5 e5   h1 g1 f1 e1}
        vshufps %%r5, %%r4, %%r6, 0x88  ; r5 = {h6 g6 f6 e6   h2 g2 f2 e2}
        vshufps %%r4, %%r4, %%r6, 0xDD  ; r4 = {h7 g7 f7 e7   h3 g3 f3 e3}
        vshufps %%t1, %%r2, %%t1, 0x88  ; t1 = {h4 g4 f4 e4   h0 g0 f0 e0}

        vshufi32x4      %%r6, %%r1, %%r5, 0000_0011b ; h6...a6
        vshufi32x4      %%r2, %%r1, %%r5, 0000_0000b ; h2...a2
        vshufi32x4      %%r5, %%r3, %%r7, 0000_0011b ; h5...a5
        vshufi32x4      %%r1, %%r3, %%r7, 0000_0000b ; h1...a1
        vshufi32x4      %%r7, %%r0, %%r4, 0000_0011b ; h7...a7
        vshufi32x4      %%r3, %%r0, %%r4, 0000_0000b ; h3...a3
        vshufi32x4      %%r4, %%t0, %%t1, 0000_0011b ; h4...a4
        vshufi32x4      %%r0, %%t0, %%t1, 0000_0000b ; h0...a0
%endmacro

; LOAD ALL 8 LANES FOR 8x8 64-BIT TRANSPOSE
;
; r0-r7       [out] zmm registers which will contain the data to be transposed
; addr0-addr7 [in]  pointers to the next 64-byte block of data to be fetch for all 8 lanes
; ptr_offset  [in] offset to be applied on all pointers (addr0-addr7)
%macro TRANSPOSE8_U64_LOAD8 17
%define %%r0 %1
%define %%r1 %2
%define %%r2 %3
%define %%r3 %4
%define %%r4 %5
%define %%r5 %6
%define %%r6 %7
%define %%r7 %8
%define %%addr0 %9
%define %%addr1 %10
%define %%addr2 %11
%define %%addr3 %12
%define %%addr4 %13
%define %%addr5 %14
%define %%addr6 %15
%define %%addr7 %16
%define %%ptr_offset %17

; Expected output data
;
; r0 = {e3 e2 e1 e0  a3 a2 a1 a0}
; r1 = {f3 f2 f1 f0  b3 b2 b1 b0}
; r2 = {g3 g2 g1 g0  c3 c2 c1 c0}
; r3 = {h3 h2 h1 h0  d3 d2 d1 d0}
; r4 = {e7 e6 e5 e4  a7 a6 a5 a4}
; r5 = {f7 f6 f5 f4  b7 b6 b5 b4}
; r6 = {g7 g6 g5 g4  c7 c6 c5 c4}
; r7 = {h7 h6 h5 h4  d7 d6 d5 d4}

	vmovups	YWORD(%%r0),[%%addr0+%%ptr_offset]
	vmovups	YWORD(%%r1),[%%addr1+%%ptr_offset]
	vmovups	YWORD(%%r2),[%%addr2+%%ptr_offset]
	vmovups	YWORD(%%r3),[%%addr3+%%ptr_offset]
	vmovups	YWORD(%%r4),[%%addr0+%%ptr_offset+32]
	vmovups	YWORD(%%r5),[%%addr1+%%ptr_offset+32]
	vmovups	YWORD(%%r6),[%%addr2+%%ptr_offset+32]
	vmovups	YWORD(%%r7),[%%addr3+%%ptr_offset+32]

	vinserti64x4 %%r0, %%r0, [%%addr4+%%ptr_offset], 0x01
	vinserti64x4 %%r1, %%r1, [%%addr5+%%ptr_offset], 0x01
	vinserti64x4 %%r2, %%r2, [%%addr6+%%ptr_offset], 0x01
	vinserti64x4 %%r3, %%r3, [%%addr7+%%ptr_offset], 0x01
	vinserti64x4 %%r4, %%r4, [%%addr4+%%ptr_offset+32], 0x01
	vinserti64x4 %%r5, %%r5, [%%addr5+%%ptr_offset+32], 0x01
	vinserti64x4 %%r6, %%r6, [%%addr6+%%ptr_offset+32], 0x01
	vinserti64x4 %%r7, %%r7, [%%addr7+%%ptr_offset+32], 0x01

%endmacro

; 8x8 64-BIT TRANSPOSE
;
; Before calling this macro, TRANSPOSE8_U64_LOAD8 must be called.
;
; r0-r3          [in/out]    zmm registers containing bytes 0-31 of each 64B block (e.g. zmm0 = [e3-e0 a3-a0])
; r4-r7          [in/out]    zmm registers containing bytes 32-63 of each 64B block (e.g. zmm4 = [e4-e7 a4-a7])
; t0-t1          [clobbered] zmm temporary registers
; PERM_INDEX1-2  [clobbered] zmm registers for shuffle mask storing
%macro TRANSPOSE8_U64 12
%define %%r0 %1
%define %%r1 %2
%define %%r2 %3
%define %%r3 %4
%define %%r4 %5
%define %%r5 %6
%define %%r6 %7
%define %%r7 %8
%define %%t0 %9
%define %%t1 %10
%define %%PERM_INDEX1 %11
%define %%PERM_INDEX2 %12

; each x(i) is 64 bits, 8 * 64 = 512 ==> a full digest length, 64-bit double precision quantities

; Input data
;
; r0 = {e3 e2 e1 e0  a3 a2 a1 a0}
; r1 = {f3 f2 f1 f0  b3 b2 b1 b0}
; r2 = {g3 g2 g1 g0  c3 c2 c1 c0}
; r3 = {h3 h2 h1 h0  d3 d2 d1 d0}
; r4 = {e7 e6 e5 e4  a7 a6 a5 a4}
; r5 = {f7 f6 f5 f4  b7 b6 b5 b4}
; r6 = {g7 g6 g5 g4  c7 c6 c5 c4}
; r7 = {h7 h6 h5 h4  d7 d6 d5 d4}
;
; Expected output data
;
; r0 = {h0 g0 f0 e0  d0 c0 b0 a0}
; r1 = {h1 g1 f1 e1  d1 c1 b1 a1}
; r2 = {h2 g2 f2 e2  d2 c2 b2 a2}
; r3 = {h3 g3 f3 e3  d3 c3 b3 a3}
; r4 = {h4 g4 f4 e4  d4 c4 b4 a4}
; r5 = {h5 g5 f5 e5  d5 c5 b5 a5}
; r6 = {h6 g6 f6 e6  d6 c6 b6 a6}
; r7 = {h7 g7 f7 e7  d7 c7 b7 a7}

        ;; ;;;  will not get clobbered
        vmovdqa32 %%PERM_INDEX1, [PSHUFFLE_TRANSPOSE_MASK1] ; temp
        vmovdqa32 %%PERM_INDEX2, [PSHUFFLE_TRANSPOSE_MASK2] ; temp

        ; process top half (r0..r3)
        vshufpd	%%t0, %%r0, %%r1, 0x00	; t0 = {f2 e2 f0 e0  b2 a2 b0 a0}
        vshufpd	%%r1, %%r0, %%r1, 0xFF	; r0 = {f3 e3 f1 e1  b3 a3 b1 a1}
        vshufpd	%%t1, %%r2, %%r3, 0x00	; t1 = {h2 g2 h0 g0  d2 c2 d0 c0}
        vshufpd	%%r2, %%r2, %%r3, 0xFF	; r2 = {h3 g3 h1 g1  d3 c3 d1 c1}

        vmovdqa32   %%r3, %%r1
        vpermt2q    %%r1, %%PERM_INDEX1,%%r2   ; r1 = {h1 g1 f1 e1  d1 c1 b1 a1}
        vpermt2q    %%r3, %%PERM_INDEX2,%%r2   ; r3 = {h3 g3 f3 e3  d3 c3 b3 a3}

        vmovdqa32   %%r0, %%t0
        vmovdqa32   %%r2, %%t0
        vpermt2q    %%r0, %%PERM_INDEX1,%%t1   ; r0 = {h0 g0 f0 e0  d0 c0 b0 a0}
        vpermt2q    %%r2, %%PERM_INDEX2,%%t1   ; r2 = {h2 g2 f2 e2  d2 c2 b2 a2}

        ; process top bottom (r4..r7)
        vshufpd	%%t0, %%r4, %%r5, 0x00	; t0 = {f6 e6 f4 e4  b6 a6 b4 a4}
        vshufpd	%%r5, %%r4, %%r5, 0xFF	; r0 = {f7 e7 f5 e5  b7 a7 b5 a5}
        vshufpd	%%t1, %%r6, %%r7, 0x00	; t1 = {h6 g6 h4 g4  d6 c6 d4 c4}
        vshufpd	%%r6, %%r6, %%r7, 0xFF	; r2 = {h7 g7 h5 g5  d7 c7 d5 c5}

        vmovdqa32   %%r7, %%r5
        vpermt2q    %%r5, %%PERM_INDEX1,%%r6   ; r5 = {h5 g5 f5 e5  d5 c5 b5 a5}
        vpermt2q    %%r7, %%PERM_INDEX2,%%r6   ; r7 = {h7 g7 f7 e7  d7 c7 b7 a7}

        vmovdqa32   %%r4, %%t0
        vmovdqa32   %%r6, %%t0
        vpermt2q    %%r4, %%PERM_INDEX1,%%t1   ; r4 = {h4 g4 f4 e4  d4 c4 b4 a4}
        vpermt2q    %%r6, %%PERM_INDEX2,%%t1   ; r6 = {h6 g6 f6 e6  d6 c6 b6 a6}
%endmacro

; 4x4 128-BIT TRANSPOSE
;
; addr0-addr3    [in]  pointers to the next 64-byte block of data to be fetch for all 4 lanes
; r0-r3          [out] zmm registers which will contain the data transposed
; t0-t1          [clobbered] zmm temporary registers
; PERM_INDEX1-2  [clobbered] zmm registers for shuffle mask storing
%macro TRANSPOSE4_U128 12
%define %%addr0 %1
%define %%addr1 %2
%define %%addr2 %3
%define %%addr3 %4
%define %%r0 %5
%define %%r1 %6
%define %%r2 %7
%define %%r3 %8
%define %%t0 %9
%define %%t1 %10
%define %%PERM_INDEX1 %11
%define %%PERM_INDEX2 %12

        vmovdqu64       YWORD(%%r0), [%%addr0]
        vmovdqu64       YWORD(%%r1), [%%addr1]
        vmovdqu64       YWORD(%%r2), [%%addr0+32]
        vmovdqu64       YWORD(%%r3), [%%addr1+32]
        vinserti64x4    %%r0, %%r0, [%%addr2], 0x01
        vinserti64x4    %%r1, %%r1, [%%addr3], 0x01
        vinserti64x4    %%r2, %%r2, [%%addr2+32], 0x01
        vinserti64x4    %%r3, %%r3, [%%addr3+32], 0x01

        vmovdqa32       %%PERM_INDEX1, [rel PSHUFFLE_TRANSPOSE_MASK1]
        vmovdqa32       %%PERM_INDEX2, [rel PSHUFFLE_TRANSPOSE_MASK2]

        vmovdqa32       %%t0, %%r0
        vpermt2q        %%r0, %%PERM_INDEX1,%%r1
        vpermt2q        %%t0, %%PERM_INDEX2,%%r1

        vmovdqa32       %%t1, %%r2
        vpermt2q        %%r2, %%PERM_INDEX1,%%r3
        vpermt2q        %%t1, %%PERM_INDEX2,%%r3

        vmovdqa64       %%r1, %%t0
        vmovdqa64       %%r3, %%t1
%endmacro

; 4x4 128-BIT TRANSPOSE
;
; addr0-addr3    [in]  pointers to the next 64-byte block of data to be fetch for all 4 lanes
; r0-r3          [out] zmm registers which will contain the data transposed
; t0-t1          [clobbered] zmm temporary registers
; PERM_INDEX1-2  [clobbered] zmm registers for shuffle mask storing and temporary ZMM register
%macro TRANSPOSE4_U128_INPLACE 8
%define %%r0 %1
%define %%r1 %2
%define %%r2 %3
%define %%r3 %4
%define %%t0 %5
%define %%t1 %6
%define %%PERM_INDEX1 %7
%define %%PERM_INDEX2 %8

%define %%t2 %%PERM_INDEX1

        vmovdqa64       %%PERM_INDEX1, [rel PSHUFFLE_TRANSPOSE_MASK1]
        vmovdqa64       %%PERM_INDEX2, [rel PSHUFFLE_TRANSPOSE_MASK2]

        vmovdqa64       %%t0, %%r0
        vpermt2q        %%r0, %%PERM_INDEX1,%%r1 ; 0 4 2 6
        vpermt2q        %%t0, %%PERM_INDEX2,%%r1 ; 1 5 3 7

        vmovdqa64       %%t1, %%r2
        vpermt2q        %%r2, %%PERM_INDEX1,%%r3 ; 8 12 10 14
        vpermt2q        %%t1, %%PERM_INDEX2,%%r3 ; 9 13 11 15

        vshufi64x2      %%t2, %%r0, %%r2, 0x44 ; 0 4 8  12
        vshufi64x2      %%r2, %%r0, %%r2, 0xee ; 2 6 10 14
        vmovdqa64       %%r0, %%t2
        vshufi64x2      %%r1, %%t0, %%t1, 0x44 ; 1 5 9  13
        vshufi64x2      %%r3, %%t0, %%t1, 0xee ; 3 7 11 15
%endmacro
%endif ;; _TRANSPOSE_AVX512_ASM_

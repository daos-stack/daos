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

;; code to compute octal MD5 using SSE

;; Stack must be aligned to 16 bytes before call
;; Windows clobbers:  rax rbx     rdx rsi rdi     r8 r9 r10 r11 r12 r13 r14 r15
;; Windows preserves:         rcx             rbp
;;
;; Linux clobbers:    rax rbx rcx rdx rsi         r8 r9 r10 r11 r12 r13 r14 r15
;; Linux preserves:                       rdi rbp
;;
;; clobbers xmm0-15

%include "include/os.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/clear_regs.asm"

mksection .rodata align=64
default rel

align 64
MKGLOBAL(MD5_TABLE,data,internal)
MD5_TABLE:
        dd      0xd76aa478, 0xd76aa478, 0xd76aa478, 0xd76aa478
        dd      0xe8c7b756, 0xe8c7b756, 0xe8c7b756, 0xe8c7b756
        dd      0x242070db, 0x242070db, 0x242070db, 0x242070db
        dd      0xc1bdceee, 0xc1bdceee, 0xc1bdceee, 0xc1bdceee
        dd      0xf57c0faf, 0xf57c0faf, 0xf57c0faf, 0xf57c0faf
        dd      0x4787c62a, 0x4787c62a, 0x4787c62a, 0x4787c62a
        dd      0xa8304613, 0xa8304613, 0xa8304613, 0xa8304613
        dd      0xfd469501, 0xfd469501, 0xfd469501, 0xfd469501
        dd      0x698098d8, 0x698098d8, 0x698098d8, 0x698098d8
        dd      0x8b44f7af, 0x8b44f7af, 0x8b44f7af, 0x8b44f7af
        dd      0xffff5bb1, 0xffff5bb1, 0xffff5bb1, 0xffff5bb1
        dd      0x895cd7be, 0x895cd7be, 0x895cd7be, 0x895cd7be
        dd      0x6b901122, 0x6b901122, 0x6b901122, 0x6b901122
        dd      0xfd987193, 0xfd987193, 0xfd987193, 0xfd987193
        dd      0xa679438e, 0xa679438e, 0xa679438e, 0xa679438e
        dd      0x49b40821, 0x49b40821, 0x49b40821, 0x49b40821
        dd      0xf61e2562, 0xf61e2562, 0xf61e2562, 0xf61e2562
        dd      0xc040b340, 0xc040b340, 0xc040b340, 0xc040b340
        dd      0x265e5a51, 0x265e5a51, 0x265e5a51, 0x265e5a51
        dd      0xe9b6c7aa, 0xe9b6c7aa, 0xe9b6c7aa, 0xe9b6c7aa
        dd      0xd62f105d, 0xd62f105d, 0xd62f105d, 0xd62f105d
        dd      0x02441453, 0x02441453, 0x02441453, 0x02441453
        dd      0xd8a1e681, 0xd8a1e681, 0xd8a1e681, 0xd8a1e681
        dd      0xe7d3fbc8, 0xe7d3fbc8, 0xe7d3fbc8, 0xe7d3fbc8
        dd      0x21e1cde6, 0x21e1cde6, 0x21e1cde6, 0x21e1cde6
        dd      0xc33707d6, 0xc33707d6, 0xc33707d6, 0xc33707d6
        dd      0xf4d50d87, 0xf4d50d87, 0xf4d50d87, 0xf4d50d87
        dd      0x455a14ed, 0x455a14ed, 0x455a14ed, 0x455a14ed
        dd      0xa9e3e905, 0xa9e3e905, 0xa9e3e905, 0xa9e3e905
        dd      0xfcefa3f8, 0xfcefa3f8, 0xfcefa3f8, 0xfcefa3f8
        dd      0x676f02d9, 0x676f02d9, 0x676f02d9, 0x676f02d9
        dd      0x8d2a4c8a, 0x8d2a4c8a, 0x8d2a4c8a, 0x8d2a4c8a
        dd      0xfffa3942, 0xfffa3942, 0xfffa3942, 0xfffa3942
        dd      0x8771f681, 0x8771f681, 0x8771f681, 0x8771f681
        dd      0x6d9d6122, 0x6d9d6122, 0x6d9d6122, 0x6d9d6122
        dd      0xfde5380c, 0xfde5380c, 0xfde5380c, 0xfde5380c
        dd      0xa4beea44, 0xa4beea44, 0xa4beea44, 0xa4beea44
        dd      0x4bdecfa9, 0x4bdecfa9, 0x4bdecfa9, 0x4bdecfa9
        dd      0xf6bb4b60, 0xf6bb4b60, 0xf6bb4b60, 0xf6bb4b60
        dd      0xbebfbc70, 0xbebfbc70, 0xbebfbc70, 0xbebfbc70
        dd      0x289b7ec6, 0x289b7ec6, 0x289b7ec6, 0x289b7ec6
        dd      0xeaa127fa, 0xeaa127fa, 0xeaa127fa, 0xeaa127fa
        dd      0xd4ef3085, 0xd4ef3085, 0xd4ef3085, 0xd4ef3085
        dd      0x04881d05, 0x04881d05, 0x04881d05, 0x04881d05
        dd      0xd9d4d039, 0xd9d4d039, 0xd9d4d039, 0xd9d4d039
        dd      0xe6db99e5, 0xe6db99e5, 0xe6db99e5, 0xe6db99e5
        dd      0x1fa27cf8, 0x1fa27cf8, 0x1fa27cf8, 0x1fa27cf8
        dd      0xc4ac5665, 0xc4ac5665, 0xc4ac5665, 0xc4ac5665
        dd      0xf4292244, 0xf4292244, 0xf4292244, 0xf4292244
        dd      0x432aff97, 0x432aff97, 0x432aff97, 0x432aff97
        dd      0xab9423a7, 0xab9423a7, 0xab9423a7, 0xab9423a7
        dd      0xfc93a039, 0xfc93a039, 0xfc93a039, 0xfc93a039
        dd      0x655b59c3, 0x655b59c3, 0x655b59c3, 0x655b59c3
        dd      0x8f0ccc92, 0x8f0ccc92, 0x8f0ccc92, 0x8f0ccc92
        dd      0xffeff47d, 0xffeff47d, 0xffeff47d, 0xffeff47d
        dd      0x85845dd1, 0x85845dd1, 0x85845dd1, 0x85845dd1
        dd      0x6fa87e4f, 0x6fa87e4f, 0x6fa87e4f, 0x6fa87e4f
        dd      0xfe2ce6e0, 0xfe2ce6e0, 0xfe2ce6e0, 0xfe2ce6e0
        dd      0xa3014314, 0xa3014314, 0xa3014314, 0xa3014314
        dd      0x4e0811a1, 0x4e0811a1, 0x4e0811a1, 0x4e0811a1
        dd      0xf7537e82, 0xf7537e82, 0xf7537e82, 0xf7537e82
        dd      0xbd3af235, 0xbd3af235, 0xbd3af235, 0xbd3af235
        dd      0x2ad7d2bb, 0x2ad7d2bb, 0x2ad7d2bb, 0x2ad7d2bb
        dd      0xeb86d391, 0xeb86d391, 0xeb86d391, 0xeb86d391

ONES:
        dd      0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff

mksection .text

%ifdef LINUX
;; Linux Registers
%define arg1	rdi
%define arg2	rsi
%define mem1    rcx
%define mem2    rdx
%else
%define arg1	rcx
%define arg2	rdx
%define mem1    rdi
%define mem2    rsi
%endif

;; rbp is not clobbered

%define inp0 r8
%define inp1 r9
%define inp2 r10
%define inp3 r11
%define inp4 r12
%define inp5 r13
%define inp6 r14
%define inp7 r15

%define TBL   rax
%define IDX   rbx

%define A       xmm0
%define B       xmm1
%define C       xmm2
%define D       xmm3
%define E       xmm4 ; tmp
%define F       xmm5 ; tmp

%define A2      xmm6
%define B2      xmm7
%define C2      xmm8
%define D2      xmm9

%define FUN     E
%define TMP     F
%define FUN2    xmm10
%define TMP2    xmm11

%define T0      xmm10
%define T1      xmm11
%define T2      xmm12
%define T3      xmm13
%define T4      xmm14
%define T5      xmm15

; Stack Layout
;
; 470 DD2
; 460 CC2
; 450 BB2
; 440 AA2
; 430 DD
; 420 CC
; 410 BB
; 400 AA
;
; 3F0 data2[15] for lanes 7...4   \
; ...                              \
; 300 data2[0]  for lanes 7...4     \
; 2F0 data2[15] for lanes 3...0      > mem block 2
; ...                               /
; 210 data2[1]  for lanes 3...0    /
; 200 data2[0]  for lanes 3...0   /
;
; 1F0 data1[15] for lanes 7...4   \
; ...                              \
; 100 data1[0]  for lanes 7...4     \
;  F0 data1[15] for lanes 3...0      > mem block 1
; ...                               /
;  10 data1[1]  for lanes 3...0    /
;   0 data1[0]  for lanes 3...0   /

; stack size must be an odd multiple of 8 bytes in size
struc STACK
_DATA:		reso	2*2*16	; 2 blocks * 2 sets of lanes * 16 regs
_DIGEST:	reso	8	; stores AA-DD, AA2-DD2
		resb	8	; for alignment
endstruc
%define STACK_SIZE STACK_size

%define AA      rsp + _DIGEST + 16*0
%define BB      rsp + _DIGEST + 16*1
%define CC      rsp + _DIGEST + 16*2
%define DD      rsp + _DIGEST + 16*3
%define AA2     rsp + _DIGEST + 16*4
%define BB2     rsp + _DIGEST + 16*5
%define CC2     rsp + _DIGEST + 16*6
%define DD2     rsp + _DIGEST + 16*7

;;
;; MD5 left rotations (number of bits)
;;
rot11 equ  7
rot12 equ  12
rot13 equ  17
rot14 equ  22
rot21 equ  5
rot22 equ  9
rot23 equ  14
rot24 equ  20
rot31 equ  4
rot32 equ  11
rot33 equ  16
rot34 equ  23
rot41 equ  6
rot42 equ  10
rot43 equ  15
rot44 equ  21

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
        movdqa  %%t0, %%r0
        shufps  %%t0, %%r1, 0x44        ; t0 = {b1 b0 a1 a0}
        shufps  %%r0, %%r1, 0xEE        ; r0 = {b3 b2 a3 a2}

        movdqa  %%t1, %%r2
        shufps  %%t1, %%r3, 0x44        ; t1 = {d1 d0 c1 c0}
        shufps  %%r2, %%r3, 0xEE        ; r2 = {d3 d2 c3 c2}

        movdqa  %%r1, %%t0
        shufps  %%r1, %%t1, 0xDD        ; r1 = {d1 c1 b1 a1}

        movdqa  %%r3, %%r0
        shufps  %%r3, %%r2, 0xDD        ; r3 = {d3 c3 b3 a3}

        shufps  %%r0, %%r2, 0x88        ; r0 = {d2 c2 b2 a2}
        shufps  %%t0, %%t1, 0x88        ; t0 = {d0 c0 b0 a0}
%endmacro

;;
;; Magic functions defined in RFC 1321
;;
; macro MAGIC_F F,X,Y,Z   ;; F = ((Z) ^ ((X) & ((Y) ^ (Z))))
%macro MAGIC_F 4
%define %%F %1
%define %%X %2
%define %%Y %3
%define %%Z %4
        movdqa  %%F,%%Z
        pxor    %%F,%%Y
        pand    %%F,%%X
        pxor    %%F,%%Z
%endmacro

; macro MAGIC_G F,X,Y,Z   ;; F = F((Z),(X),(Y))
%macro MAGIC_G 4
%define %%F %1
%define %%X %2
%define %%Y %3
%define %%Z %4
        MAGIC_F %%F,%%Z,%%X,%%Y
%endmacro

; macro MAGIC_H F,X,Y,Z   ;; F = ((X) ^ (Y) ^ (Z))
%macro MAGIC_H 4
%define %%F %1
%define %%X %2
%define %%Y %3
%define %%Z %4
        movdqa  %%F,%%Z
        pxor    %%F,%%Y
        pxor    %%F,%%X
%endmacro

; macro MAGIC_I F,X,Y,Z   ;; F =  ((Y) ^ ((X) | ~(Z)))
%macro MAGIC_I 4
%define %%F %1
%define %%X %2
%define %%Y %3
%define %%Z %4
        movdqa  %%F,%%Z
        pxor    %%F,[rel ONES]  ; pnot     %%F
        por     %%F,%%X
        pxor    %%F,%%Y
%endmacro

; PROLD reg, imm, tmp
%macro PROLD 3
%define %%reg %1
%define %%imm %2
%define %%tmp %3
        movdqa  %%tmp, %%reg
        psrld   %%tmp, (32-%%imm)
        pslld   %%reg, %%imm
        por     %%reg, %%tmp
%endmacro

;;
;; single MD5 step
;;
;; A = B +ROL32((A +MAGIC(B,C,D) +data +const), nrot)
;;
; macro MD5_STEP1 MAGIC_FUN, A,B,C,D, A2,B2,C3,D2, FUN, TMP, data, MD5const, nrot
%macro MD5_STEP1 14
%define %%MAGIC_FUN     %1
%define %%A             %2
%define %%B             %3
%define %%C             %4
%define %%D             %5
%define %%A2            %6
%define %%B2            %7
%define %%C2            %8
%define %%D2            %9
%define %%FUN           %10
%define %%TMP           %11
%define %%data          %12
%define %%MD5const      %13
%define %%nrot          %14

        paddd       %%A, %%MD5const
                paddd       %%A2, %%MD5const
        paddd       %%A, [%%data]
                paddd       %%A2, [%%data + 16*16]
        %%MAGIC_FUN %%FUN, %%B,%%C,%%D
        paddd       %%A, %%FUN
                %%MAGIC_FUN %%FUN, %%B2,%%C2,%%D2
                paddd       %%A2, %%FUN
        PROLD       %%A,%%nrot, %%TMP
                PROLD       %%A2,%%nrot, %%TMP
        paddd       %%A, %%B
                paddd       %%A2, %%B2
%endmacro

;;
;; single MD5 step
;;
;; A = B +ROL32((A +MAGIC(B,C,D) +data +const), nrot)
;;
; macro MD5_STEP MAGIC_FUN, A,B,C,D, A2,B2,C3,D2, FUN, TMP, FUN2, TMP2, data,
;                MD5const, nrot
%macro MD5_STEP 16
%define %%MAGIC_FUN     %1
%define %%A             %2
%define %%B             %3
%define %%C             %4
%define %%D             %5
%define %%A2            %6
%define %%B2            %7
%define %%C2            %8
%define %%D2            %9
%define %%FUN           %10
%define %%TMP           %11
%define %%FUN2          %12
%define %%TMP2          %13
%define %%data          %14
%define %%MD5const      %15
%define %%nrot          %16

        paddd       %%A, %%MD5const
                paddd       %%A2, %%MD5const
        paddd       %%A, [%%data]
                paddd       %%A2, [%%data + 16*16]
        %%MAGIC_FUN %%FUN, %%B,%%C,%%D
                %%MAGIC_FUN %%FUN2, %%B2,%%C2,%%D2
        paddd       %%A, %%FUN
                paddd       %%A2, %%FUN2
        PROLD       %%A,%%nrot, %%TMP
                PROLD       %%A2,%%nrot, %%TMP2
        paddd       %%A, %%B
                paddd       %%A2, %%B2
%endmacro

; void md5_x4x2_sse(MD5_ARGS *args, UINT64 num_blks)
; arg 1 : pointer to MD5_ARGS structure
; arg 2 : number of blocks (>=1)
;
align 32
MKGLOBAL(md5_x4x2_sse,function,internal)
md5_x4x2_sse:

        sub     rsp, STACK_SIZE

        ;; each row of transposed digests is split into 2 parts, the right half stored in A, and left half in A2
        ;; Initialize digests
        movdqa  A,[arg1 + 0*16 + 0*MD5_DIGEST_ROW_SIZE]
        movdqa  B,[arg1 + 0*16 + 1*MD5_DIGEST_ROW_SIZE]
        movdqa  C,[arg1 + 0*16 + 2*MD5_DIGEST_ROW_SIZE]
        movdqa  D,[arg1 + 0*16 + 3*MD5_DIGEST_ROW_SIZE]

                ;; Initialize digests
                movdqa  A2,[arg1 + 1*16 + 0*MD5_DIGEST_ROW_SIZE]
                movdqa  B2,[arg1 + 1*16 + 1*MD5_DIGEST_ROW_SIZE]
                movdqa  C2,[arg1 + 1*16 + 2*MD5_DIGEST_ROW_SIZE]
                movdqa  D2,[arg1 + 1*16 + 3*MD5_DIGEST_ROW_SIZE]

        lea     TBL, [rel MD5_TABLE]

        ;; load input pointers
        mov     inp0,[arg1+_data_ptr_md5  +0*PTR_SZ]
        mov     inp1,[arg1+_data_ptr_md5  +1*PTR_SZ]
        mov     inp2,[arg1+_data_ptr_md5  +2*PTR_SZ]
        mov     inp3,[arg1+_data_ptr_md5  +3*PTR_SZ]
                mov     inp4,[arg1+_data_ptr_md5  +4*PTR_SZ]
                mov     inp5,[arg1+_data_ptr_md5  +5*PTR_SZ]
                mov     inp6,[arg1+_data_ptr_md5  +6*PTR_SZ]
                mov     inp7,[arg1+_data_ptr_md5  +7*PTR_SZ]
        xor     IDX, IDX

        ; Make ping-pong pointers to the two memory blocks
        mov     mem1, rsp
        lea     mem2, [rsp + 16*16*2]

;; Load first block of data and save back to stack
%assign I 0
%rep 4
        movdqu  T2,[inp0+IDX+I*16]
        movdqu  T1,[inp1+IDX+I*16]
        movdqu  T4,[inp2+IDX+I*16]
        movdqu  T3,[inp3+IDX+I*16]
        TRANSPOSE       T2, T1, T4, T3, T0, T5
        movdqa  [mem1+(I*4+0)*16],T0
        movdqa  [mem1+(I*4+1)*16],T1
        movdqa  [mem1+(I*4+2)*16],T2
        movdqa  [mem1+(I*4+3)*16],T3

        movdqu  T2,[inp4+IDX+I*16]
        movdqu  T1,[inp5+IDX+I*16]
        movdqu  T4,[inp6+IDX+I*16]
        movdqu  T3,[inp7+IDX+I*16]
        TRANSPOSE       T2, T1, T4, T3, T0, T5
        movdqa  [mem1+(I*4+0)*16 + 16*16],T0
        movdqa  [mem1+(I*4+1)*16 + 16*16],T1
        movdqa  [mem1+(I*4+2)*16 + 16*16],T2
        movdqa  [mem1+(I*4+3)*16 + 16*16],T3
%assign I (I+1)
%endrep

lloop:
        ; save old digests
        movdqa  [AA], A
        movdqa  [BB], B
        movdqa  [CC], C
        movdqa  [DD], D
                ; save old digests
                movdqa  [AA2], A2
                movdqa  [BB2], B2
                movdqa  [CC2], C2
                movdqa  [DD2], D2

        add     IDX, 4*16
        sub     arg2, 1
        je      lastblock

        MD5_STEP1 MAGIC_F, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 0*16, [TBL+ 0*16], rot11
        MD5_STEP1 MAGIC_F, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 + 1*16, [TBL+ 1*16], rot12
        MD5_STEP1 MAGIC_F, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 + 2*16, [TBL+ 2*16], rot13
        MD5_STEP1 MAGIC_F, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 + 3*16, [TBL+ 3*16], rot14
        MD5_STEP1 MAGIC_F, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 4*16, [TBL+ 4*16], rot11
        MD5_STEP1 MAGIC_F, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 + 5*16, [TBL+ 5*16], rot12
        MD5_STEP1 MAGIC_F, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 + 6*16, [TBL+ 6*16], rot13
        MD5_STEP1 MAGIC_F, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 + 7*16, [TBL+ 7*16], rot14

%assign I 0
        movdqu  T2,[inp0+IDX+I*16]
        movdqu  T1,[inp1+IDX+I*16]
        movdqu  T4,[inp2+IDX+I*16]
        movdqu  T3,[inp3+IDX+I*16]
        TRANSPOSE       T2, T1, T4, T3, T0, T5
        movdqa  [mem2+(I*4+0)*16],T0
        movdqa  [mem2+(I*4+1)*16],T1
        movdqa  [mem2+(I*4+2)*16],T2
        movdqa  [mem2+(I*4+3)*16],T3

        MD5_STEP1 MAGIC_F, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 8*16, [TBL+ 8*16], rot11
        MD5_STEP1 MAGIC_F, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 + 9*16, [TBL+ 9*16], rot12
        MD5_STEP1 MAGIC_F, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 +10*16, [TBL+10*16], rot13
        MD5_STEP1 MAGIC_F, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 +11*16, [TBL+11*16], rot14
        MD5_STEP1 MAGIC_F, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 +12*16, [TBL+12*16], rot11
        MD5_STEP1 MAGIC_F, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 +13*16, [TBL+13*16], rot12
        MD5_STEP1 MAGIC_F, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 +14*16, [TBL+14*16], rot13
        MD5_STEP1 MAGIC_F, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 +15*16, [TBL+15*16], rot14

        movdqu  T2,[inp4+IDX+I*16]
        movdqu  T1,[inp5+IDX+I*16]
        movdqu  T4,[inp6+IDX+I*16]
        movdqu  T3,[inp7+IDX+I*16]
        TRANSPOSE       T2, T1, T4, T3, T0, T5
        movdqa  [mem2+(I*4+0)*16 + 16*16],T0
        movdqa  [mem2+(I*4+1)*16 + 16*16],T1
        movdqa  [mem2+(I*4+2)*16 + 16*16],T2
        movdqa  [mem2+(I*4+3)*16 + 16*16],T3
%assign I (I+1)

        MD5_STEP1 MAGIC_G, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 1*16, [TBL+16*16], rot21
        MD5_STEP1 MAGIC_G, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 + 6*16, [TBL+17*16], rot22
        MD5_STEP1 MAGIC_G, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 +11*16, [TBL+18*16], rot23
        MD5_STEP1 MAGIC_G, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 + 0*16, [TBL+19*16], rot24
        MD5_STEP1 MAGIC_G, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 5*16, [TBL+20*16], rot21
        MD5_STEP1 MAGIC_G, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 +10*16, [TBL+21*16], rot22
        MD5_STEP1 MAGIC_G, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 +15*16, [TBL+22*16], rot23
        MD5_STEP1 MAGIC_G, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 + 4*16, [TBL+23*16], rot24

        movdqu  T2,[inp0+IDX+I*16]
        movdqu  T1,[inp1+IDX+I*16]
        movdqu  T4,[inp2+IDX+I*16]
        movdqu  T3,[inp3+IDX+I*16]
        TRANSPOSE       T2, T1, T4, T3, T0, T5
        movdqa  [mem2+(I*4+0)*16],T0
        movdqa  [mem2+(I*4+1)*16],T1
        movdqa  [mem2+(I*4+2)*16],T2
        movdqa  [mem2+(I*4+3)*16],T3

        MD5_STEP1 MAGIC_G, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 9*16, [TBL+24*16], rot21
        MD5_STEP1 MAGIC_G, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 +14*16, [TBL+25*16], rot22
        MD5_STEP1 MAGIC_G, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 + 3*16, [TBL+26*16], rot23
        MD5_STEP1 MAGIC_G, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 + 8*16, [TBL+27*16], rot24
        MD5_STEP1 MAGIC_G, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 +13*16, [TBL+28*16], rot21
        MD5_STEP1 MAGIC_G, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 + 2*16, [TBL+29*16], rot22
        MD5_STEP1 MAGIC_G, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 + 7*16, [TBL+30*16], rot23
        MD5_STEP1 MAGIC_G, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 +12*16, [TBL+31*16], rot24

        movdqu  T2,[inp4+IDX+I*16]
        movdqu  T1,[inp5+IDX+I*16]
        movdqu  T4,[inp6+IDX+I*16]
        movdqu  T3,[inp7+IDX+I*16]
        TRANSPOSE       T2, T1, T4, T3, T0, T5
        movdqa  [mem2+(I*4+0)*16 + 16*16],T0
        movdqa  [mem2+(I*4+1)*16 + 16*16],T1
        movdqa  [mem2+(I*4+2)*16 + 16*16],T2
        movdqa  [mem2+(I*4+3)*16 + 16*16],T3
%assign I (I+1)

        MD5_STEP1 MAGIC_H, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 5*16, [TBL+32*16], rot31
        MD5_STEP1 MAGIC_H, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 + 8*16, [TBL+33*16], rot32
        MD5_STEP1 MAGIC_H, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 +11*16, [TBL+34*16], rot33
        MD5_STEP1 MAGIC_H, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 +14*16, [TBL+35*16], rot34
        MD5_STEP1 MAGIC_H, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 1*16, [TBL+36*16], rot31
        MD5_STEP1 MAGIC_H, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 + 4*16, [TBL+37*16], rot32
        MD5_STEP1 MAGIC_H, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 + 7*16, [TBL+38*16], rot33
        MD5_STEP1 MAGIC_H, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 +10*16, [TBL+39*16], rot34

        movdqu  T2,[inp0+IDX+I*16]
        movdqu  T1,[inp1+IDX+I*16]
        movdqu  T4,[inp2+IDX+I*16]
        movdqu  T3,[inp3+IDX+I*16]
        TRANSPOSE       T2, T1, T4, T3, T0, T5
        movdqa  [mem2+(I*4+0)*16],T0
        movdqa  [mem2+(I*4+1)*16],T1
        movdqa  [mem2+(I*4+2)*16],T2
        movdqa  [mem2+(I*4+3)*16],T3

        MD5_STEP1 MAGIC_H, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 +13*16, [TBL+40*16], rot31
        MD5_STEP1 MAGIC_H, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 + 0*16, [TBL+41*16], rot32
        MD5_STEP1 MAGIC_H, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 + 3*16, [TBL+42*16], rot33
        MD5_STEP1 MAGIC_H, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 + 6*16, [TBL+43*16], rot34
        MD5_STEP1 MAGIC_H, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 9*16, [TBL+44*16], rot31
        MD5_STEP1 MAGIC_H, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 +12*16, [TBL+45*16], rot32
        MD5_STEP1 MAGIC_H, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 +15*16, [TBL+46*16], rot33
        MD5_STEP1 MAGIC_H, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 + 2*16, [TBL+47*16], rot34

        movdqu  T2,[inp4+IDX+I*16]
        movdqu  T1,[inp5+IDX+I*16]
        movdqu  T4,[inp6+IDX+I*16]
        movdqu  T3,[inp7+IDX+I*16]
        TRANSPOSE       T2, T1, T4, T3, T0, T5
        movdqa  [mem2+(I*4+0)*16 + 16*16],T0
        movdqa  [mem2+(I*4+1)*16 + 16*16],T1
        movdqa  [mem2+(I*4+2)*16 + 16*16],T2
        movdqa  [mem2+(I*4+3)*16 + 16*16],T3
%assign I (I+1)

        MD5_STEP1 MAGIC_I, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 0*16, [TBL+48*16], rot41
        MD5_STEP1 MAGIC_I, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 + 7*16, [TBL+49*16], rot42
        MD5_STEP1 MAGIC_I, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 +14*16, [TBL+50*16], rot43
        MD5_STEP1 MAGIC_I, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 + 5*16, [TBL+51*16], rot44
        MD5_STEP1 MAGIC_I, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 +12*16, [TBL+52*16], rot41
        MD5_STEP1 MAGIC_I, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 + 3*16, [TBL+53*16], rot42
        MD5_STEP1 MAGIC_I, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 +10*16, [TBL+54*16], rot43
        MD5_STEP1 MAGIC_I, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 + 1*16, [TBL+55*16], rot44

        movdqu  T2,[inp0+IDX+I*16]
        movdqu  T1,[inp1+IDX+I*16]
        movdqu  T4,[inp2+IDX+I*16]
        movdqu  T3,[inp3+IDX+I*16]
        TRANSPOSE       T2, T1, T4, T3, T0, T5
        movdqa  [mem2+(I*4+0)*16],T0
        movdqa  [mem2+(I*4+1)*16],T1
        movdqa  [mem2+(I*4+2)*16],T2
        movdqa  [mem2+(I*4+3)*16],T3

        MD5_STEP1 MAGIC_I, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 8*16, [TBL+56*16], rot41
        MD5_STEP1 MAGIC_I, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 +15*16, [TBL+57*16], rot42
        MD5_STEP1 MAGIC_I, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 + 6*16, [TBL+58*16], rot43
        MD5_STEP1 MAGIC_I, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 +13*16, [TBL+59*16], rot44
        MD5_STEP1 MAGIC_I, A,B,C,D, A2,B2,C2,D2, FUN,TMP, mem1 + 4*16, [TBL+60*16], rot41
        MD5_STEP1 MAGIC_I, D,A,B,C, D2,A2,B2,C2, FUN,TMP, mem1 +11*16, [TBL+61*16], rot42
        MD5_STEP1 MAGIC_I, C,D,A,B, C2,D2,A2,B2, FUN,TMP, mem1 + 2*16, [TBL+62*16], rot43
        MD5_STEP1 MAGIC_I, B,C,D,A, B2,C2,D2,A2, FUN,TMP, mem1 + 9*16, [TBL+63*16], rot44

        movdqu  T2,[inp4+IDX+I*16]
        movdqu  T1,[inp5+IDX+I*16]
        movdqu  T4,[inp6+IDX+I*16]
        movdqu  T3,[inp7+IDX+I*16]
        TRANSPOSE       T2, T1, T4, T3, T0, T5
        movdqa  [mem2+(I*4+0)*16 + 16*16],T0
        movdqa  [mem2+(I*4+1)*16 + 16*16],T1
        movdqa  [mem2+(I*4+2)*16 + 16*16],T2
        movdqa  [mem2+(I*4+3)*16 + 16*16],T3
%assign I (I+1)

        paddd   A,[AA]
        paddd   B,[BB]
        paddd   C,[CC]
        paddd   D,[DD]

                paddd   A2,[AA2]
                paddd   B2,[BB2]
                paddd   C2,[CC2]
                paddd   D2,[DD2]

        ; swap mem1 and mem2
        xchg    mem1, mem2

        jmp     lloop

lastblock:

        MD5_STEP MAGIC_F, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 0*16, [TBL+ 0*16], rot11
        MD5_STEP MAGIC_F, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 + 1*16, [TBL+ 1*16], rot12
        MD5_STEP MAGIC_F, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 + 2*16, [TBL+ 2*16], rot13
        MD5_STEP MAGIC_F, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 + 3*16, [TBL+ 3*16], rot14
        MD5_STEP MAGIC_F, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 4*16, [TBL+ 4*16], rot11
        MD5_STEP MAGIC_F, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 + 5*16, [TBL+ 5*16], rot12
        MD5_STEP MAGIC_F, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 + 6*16, [TBL+ 6*16], rot13
        MD5_STEP MAGIC_F, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 + 7*16, [TBL+ 7*16], rot14
        MD5_STEP MAGIC_F, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 8*16, [TBL+ 8*16], rot11
        MD5_STEP MAGIC_F, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 + 9*16, [TBL+ 9*16], rot12
        MD5_STEP MAGIC_F, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 +10*16, [TBL+10*16], rot13
        MD5_STEP MAGIC_F, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 +11*16, [TBL+11*16], rot14
        MD5_STEP MAGIC_F, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 +12*16, [TBL+12*16], rot11
        MD5_STEP MAGIC_F, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 +13*16, [TBL+13*16], rot12
        MD5_STEP MAGIC_F, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 +14*16, [TBL+14*16], rot13
        MD5_STEP MAGIC_F, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 +15*16, [TBL+15*16], rot14

        MD5_STEP MAGIC_G, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 1*16, [TBL+16*16], rot21
        MD5_STEP MAGIC_G, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 + 6*16, [TBL+17*16], rot22
        MD5_STEP MAGIC_G, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 +11*16, [TBL+18*16], rot23
        MD5_STEP MAGIC_G, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 + 0*16, [TBL+19*16], rot24
        MD5_STEP MAGIC_G, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 5*16, [TBL+20*16], rot21
        MD5_STEP MAGIC_G, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 +10*16, [TBL+21*16], rot22
        MD5_STEP MAGIC_G, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 +15*16, [TBL+22*16], rot23
        MD5_STEP MAGIC_G, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 + 4*16, [TBL+23*16], rot24
        MD5_STEP MAGIC_G, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 9*16, [TBL+24*16], rot21
        MD5_STEP MAGIC_G, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 +14*16, [TBL+25*16], rot22
        MD5_STEP MAGIC_G, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 + 3*16, [TBL+26*16], rot23
        MD5_STEP MAGIC_G, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 + 8*16, [TBL+27*16], rot24
        MD5_STEP MAGIC_G, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 +13*16, [TBL+28*16], rot21
        MD5_STEP MAGIC_G, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 + 2*16, [TBL+29*16], rot22
        MD5_STEP MAGIC_G, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 + 7*16, [TBL+30*16], rot23
        MD5_STEP MAGIC_G, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 +12*16, [TBL+31*16], rot24

        MD5_STEP MAGIC_H, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 5*16, [TBL+32*16], rot31
        MD5_STEP MAGIC_H, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 + 8*16, [TBL+33*16], rot32
        MD5_STEP MAGIC_H, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 +11*16, [TBL+34*16], rot33
        MD5_STEP MAGIC_H, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 +14*16, [TBL+35*16], rot34
        MD5_STEP MAGIC_H, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 1*16, [TBL+36*16], rot31
        MD5_STEP MAGIC_H, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 + 4*16, [TBL+37*16], rot32
        MD5_STEP MAGIC_H, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 + 7*16, [TBL+38*16], rot33
        MD5_STEP MAGIC_H, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 +10*16, [TBL+39*16], rot34
        MD5_STEP MAGIC_H, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 +13*16, [TBL+40*16], rot31
        MD5_STEP MAGIC_H, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 + 0*16, [TBL+41*16], rot32
        MD5_STEP MAGIC_H, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 + 3*16, [TBL+42*16], rot33
        MD5_STEP MAGIC_H, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 + 6*16, [TBL+43*16], rot34
        MD5_STEP MAGIC_H, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 9*16, [TBL+44*16], rot31
        MD5_STEP MAGIC_H, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 +12*16, [TBL+45*16], rot32
        MD5_STEP MAGIC_H, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 +15*16, [TBL+46*16], rot33
        MD5_STEP MAGIC_H, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 + 2*16, [TBL+47*16], rot34

        MD5_STEP MAGIC_I, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 0*16, [TBL+48*16], rot41
        MD5_STEP MAGIC_I, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 + 7*16, [TBL+49*16], rot42
        MD5_STEP MAGIC_I, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 +14*16, [TBL+50*16], rot43
        MD5_STEP MAGIC_I, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 + 5*16, [TBL+51*16], rot44
        MD5_STEP MAGIC_I, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 +12*16, [TBL+52*16], rot41
        MD5_STEP MAGIC_I, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 + 3*16, [TBL+53*16], rot42
        MD5_STEP MAGIC_I, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 +10*16, [TBL+54*16], rot43
        MD5_STEP MAGIC_I, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 + 1*16, [TBL+55*16], rot44
        MD5_STEP MAGIC_I, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 8*16, [TBL+56*16], rot41
        MD5_STEP MAGIC_I, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 +15*16, [TBL+57*16], rot42
        MD5_STEP MAGIC_I, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 + 6*16, [TBL+58*16], rot43
        MD5_STEP MAGIC_I, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 +13*16, [TBL+59*16], rot44
        MD5_STEP MAGIC_I, A,B,C,D, A2,B2,C2,D2, FUN,TMP, FUN2,TMP2, mem1 + 4*16, [TBL+60*16], rot41
        MD5_STEP MAGIC_I, D,A,B,C, D2,A2,B2,C2, FUN,TMP, FUN2,TMP2, mem1 +11*16, [TBL+61*16], rot42
        MD5_STEP MAGIC_I, C,D,A,B, C2,D2,A2,B2, FUN,TMP, FUN2,TMP2, mem1 + 2*16, [TBL+62*16], rot43
        MD5_STEP MAGIC_I, B,C,D,A, B2,C2,D2,A2, FUN,TMP, FUN2,TMP2, mem1 + 9*16, [TBL+63*16], rot44

        paddd   A,[AA]
        paddd   B,[BB]
        paddd   C,[CC]
        paddd   D,[DD]

                paddd   A2,[AA2]
                paddd   B2,[BB2]
                paddd   C2,[CC2]
                paddd   D2,[DD2]

        ; write out digests
        movdqu  [arg1 + 0*16 + 0*MD5_DIGEST_ROW_SIZE], A
        movdqu  [arg1 + 0*16 + 1*MD5_DIGEST_ROW_SIZE], B
        movdqu  [arg1 + 0*16 + 2*MD5_DIGEST_ROW_SIZE], C
        movdqu  [arg1 + 0*16 + 3*MD5_DIGEST_ROW_SIZE], D
                movdqu  [arg1 + 1*16 + 0*MD5_DIGEST_ROW_SIZE], A2
                movdqu  [arg1 + 1*16 + 1*MD5_DIGEST_ROW_SIZE], B2
                movdqu  [arg1 + 1*16 + 2*MD5_DIGEST_ROW_SIZE], C2
                movdqu  [arg1 + 1*16 + 3*MD5_DIGEST_ROW_SIZE], D2

        ;; update input pointers
        add     inp0, IDX
        add     inp1, IDX
        add     inp2, IDX
        add     inp3, IDX
        add     inp4, IDX
        add     inp5, IDX
        add     inp6, IDX
        add     inp7, IDX
        mov     [arg1 +_data_ptr_md5  + 0*PTR_SZ], inp0
        mov     [arg1 +_data_ptr_md5  + 1*PTR_SZ], inp1
        mov     [arg1 +_data_ptr_md5  + 2*PTR_SZ], inp2
        mov     [arg1 +_data_ptr_md5  + 3*PTR_SZ], inp3
        mov     [arg1 +_data_ptr_md5  + 4*PTR_SZ], inp4
        mov     [arg1 +_data_ptr_md5  + 5*PTR_SZ], inp5
        mov     [arg1 +_data_ptr_md5  + 6*PTR_SZ], inp6
        mov     [arg1 +_data_ptr_md5  + 7*PTR_SZ], inp7

        ;; Clear stack frame (72*16 bytes)
%ifdef SAFE_DATA
        pxor    xmm0, xmm0
%assign i 0
%rep (2*2*16+8)
        movdqa	[rsp + i*16], xmm0
%assign i (i+1)
%endrep
	clear_all_xmms_sse_asm
%endif

        ;;;;;;;;;;;;;;;;
        ;; Postamble
        add     rsp, STACK_SIZE
        ret

mksection stack-noexec

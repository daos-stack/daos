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

;; code to compute double octal MD5 using AVX2

;; Stack must be aligned to 32 bytes before call
;; Windows clobbers:  rax rbx     rdx rsi rdi     r8 r9 r10 r11 r12 r13 r14 r15
;; Windows preserves:         rcx             rbp
;;
;; Linux clobbers:    rax rbx rcx rdx rsi         r8 r9 r10 r11 r12 r13 r14 r15
;; Linux preserves:                       rdi rbp
;;
;; clobbers ymm0-15

%include "include/os.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/transpose_avx2.asm"
%include "include/clear_regs.asm"
%include "include/cet.inc"
mksection .rodata
default rel
align 64
MD5_TABLE:
	dd	0xd76aa478, 0xd76aa478, 0xd76aa478, 0xd76aa478
	dd	0xd76aa478, 0xd76aa478, 0xd76aa478, 0xd76aa478
	dd	0xe8c7b756, 0xe8c7b756, 0xe8c7b756, 0xe8c7b756
	dd	0xe8c7b756, 0xe8c7b756, 0xe8c7b756, 0xe8c7b756
	dd	0x242070db, 0x242070db, 0x242070db, 0x242070db
	dd	0x242070db, 0x242070db, 0x242070db, 0x242070db
	dd	0xc1bdceee, 0xc1bdceee, 0xc1bdceee, 0xc1bdceee
	dd	0xc1bdceee, 0xc1bdceee, 0xc1bdceee, 0xc1bdceee
	dd	0xf57c0faf, 0xf57c0faf, 0xf57c0faf, 0xf57c0faf
	dd	0xf57c0faf, 0xf57c0faf, 0xf57c0faf, 0xf57c0faf
	dd	0x4787c62a, 0x4787c62a, 0x4787c62a, 0x4787c62a
	dd	0x4787c62a, 0x4787c62a, 0x4787c62a, 0x4787c62a
	dd	0xa8304613, 0xa8304613, 0xa8304613, 0xa8304613
	dd	0xa8304613, 0xa8304613, 0xa8304613, 0xa8304613
	dd	0xfd469501, 0xfd469501, 0xfd469501, 0xfd469501
	dd	0xfd469501, 0xfd469501, 0xfd469501, 0xfd469501
	dd	0x698098d8, 0x698098d8, 0x698098d8, 0x698098d8
	dd	0x698098d8, 0x698098d8, 0x698098d8, 0x698098d8
	dd	0x8b44f7af, 0x8b44f7af, 0x8b44f7af, 0x8b44f7af
	dd	0x8b44f7af, 0x8b44f7af, 0x8b44f7af, 0x8b44f7af
	dd	0xffff5bb1, 0xffff5bb1, 0xffff5bb1, 0xffff5bb1
	dd	0xffff5bb1, 0xffff5bb1, 0xffff5bb1, 0xffff5bb1
	dd	0x895cd7be, 0x895cd7be, 0x895cd7be, 0x895cd7be
	dd	0x895cd7be, 0x895cd7be, 0x895cd7be, 0x895cd7be
	dd	0x6b901122, 0x6b901122, 0x6b901122, 0x6b901122
	dd	0x6b901122, 0x6b901122, 0x6b901122, 0x6b901122
	dd	0xfd987193, 0xfd987193, 0xfd987193, 0xfd987193
	dd	0xfd987193, 0xfd987193, 0xfd987193, 0xfd987193
	dd	0xa679438e, 0xa679438e, 0xa679438e, 0xa679438e
	dd	0xa679438e, 0xa679438e, 0xa679438e, 0xa679438e
	dd	0x49b40821, 0x49b40821, 0x49b40821, 0x49b40821
	dd	0x49b40821, 0x49b40821, 0x49b40821, 0x49b40821
	dd	0xf61e2562, 0xf61e2562, 0xf61e2562, 0xf61e2562
	dd	0xf61e2562, 0xf61e2562, 0xf61e2562, 0xf61e2562
	dd	0xc040b340, 0xc040b340, 0xc040b340, 0xc040b340
	dd	0xc040b340, 0xc040b340, 0xc040b340, 0xc040b340
	dd	0x265e5a51, 0x265e5a51, 0x265e5a51, 0x265e5a51
	dd	0x265e5a51, 0x265e5a51, 0x265e5a51, 0x265e5a51
	dd	0xe9b6c7aa, 0xe9b6c7aa, 0xe9b6c7aa, 0xe9b6c7aa
	dd	0xe9b6c7aa, 0xe9b6c7aa, 0xe9b6c7aa, 0xe9b6c7aa
	dd	0xd62f105d, 0xd62f105d, 0xd62f105d, 0xd62f105d
	dd	0xd62f105d, 0xd62f105d, 0xd62f105d, 0xd62f105d
	dd	0x02441453, 0x02441453, 0x02441453, 0x02441453
	dd	0x02441453, 0x02441453, 0x02441453, 0x02441453
	dd	0xd8a1e681, 0xd8a1e681, 0xd8a1e681, 0xd8a1e681
	dd	0xd8a1e681, 0xd8a1e681, 0xd8a1e681, 0xd8a1e681
	dd	0xe7d3fbc8, 0xe7d3fbc8, 0xe7d3fbc8, 0xe7d3fbc8
	dd	0xe7d3fbc8, 0xe7d3fbc8, 0xe7d3fbc8, 0xe7d3fbc8
	dd	0x21e1cde6, 0x21e1cde6, 0x21e1cde6, 0x21e1cde6
	dd	0x21e1cde6, 0x21e1cde6, 0x21e1cde6, 0x21e1cde6
	dd	0xc33707d6, 0xc33707d6, 0xc33707d6, 0xc33707d6
	dd	0xc33707d6, 0xc33707d6, 0xc33707d6, 0xc33707d6
	dd	0xf4d50d87, 0xf4d50d87, 0xf4d50d87, 0xf4d50d87
	dd	0xf4d50d87, 0xf4d50d87, 0xf4d50d87, 0xf4d50d87
	dd	0x455a14ed, 0x455a14ed, 0x455a14ed, 0x455a14ed
	dd	0x455a14ed, 0x455a14ed, 0x455a14ed, 0x455a14ed
	dd	0xa9e3e905, 0xa9e3e905, 0xa9e3e905, 0xa9e3e905
	dd	0xa9e3e905, 0xa9e3e905, 0xa9e3e905, 0xa9e3e905
	dd	0xfcefa3f8, 0xfcefa3f8, 0xfcefa3f8, 0xfcefa3f8
	dd	0xfcefa3f8, 0xfcefa3f8, 0xfcefa3f8, 0xfcefa3f8
	dd	0x676f02d9, 0x676f02d9, 0x676f02d9, 0x676f02d9
	dd	0x676f02d9, 0x676f02d9, 0x676f02d9, 0x676f02d9
	dd	0x8d2a4c8a, 0x8d2a4c8a, 0x8d2a4c8a, 0x8d2a4c8a
	dd	0x8d2a4c8a, 0x8d2a4c8a, 0x8d2a4c8a, 0x8d2a4c8a
	dd	0xfffa3942, 0xfffa3942, 0xfffa3942, 0xfffa3942
	dd	0xfffa3942, 0xfffa3942, 0xfffa3942, 0xfffa3942
	dd	0x8771f681, 0x8771f681, 0x8771f681, 0x8771f681
	dd	0x8771f681, 0x8771f681, 0x8771f681, 0x8771f681
	dd	0x6d9d6122, 0x6d9d6122, 0x6d9d6122, 0x6d9d6122
	dd	0x6d9d6122, 0x6d9d6122, 0x6d9d6122, 0x6d9d6122
	dd	0xfde5380c, 0xfde5380c, 0xfde5380c, 0xfde5380c
	dd	0xfde5380c, 0xfde5380c, 0xfde5380c, 0xfde5380c
	dd	0xa4beea44, 0xa4beea44, 0xa4beea44, 0xa4beea44
	dd	0xa4beea44, 0xa4beea44, 0xa4beea44, 0xa4beea44
	dd	0x4bdecfa9, 0x4bdecfa9, 0x4bdecfa9, 0x4bdecfa9
	dd	0x4bdecfa9, 0x4bdecfa9, 0x4bdecfa9, 0x4bdecfa9
	dd	0xf6bb4b60, 0xf6bb4b60, 0xf6bb4b60, 0xf6bb4b60
	dd	0xf6bb4b60, 0xf6bb4b60, 0xf6bb4b60, 0xf6bb4b60
	dd	0xbebfbc70, 0xbebfbc70, 0xbebfbc70, 0xbebfbc70
	dd	0xbebfbc70, 0xbebfbc70, 0xbebfbc70, 0xbebfbc70
	dd	0x289b7ec6, 0x289b7ec6, 0x289b7ec6, 0x289b7ec6
	dd	0x289b7ec6, 0x289b7ec6, 0x289b7ec6, 0x289b7ec6
	dd	0xeaa127fa, 0xeaa127fa, 0xeaa127fa, 0xeaa127fa
	dd	0xeaa127fa, 0xeaa127fa, 0xeaa127fa, 0xeaa127fa
	dd	0xd4ef3085, 0xd4ef3085, 0xd4ef3085, 0xd4ef3085
	dd	0xd4ef3085, 0xd4ef3085, 0xd4ef3085, 0xd4ef3085
	dd	0x04881d05, 0x04881d05, 0x04881d05, 0x04881d05
	dd	0x04881d05, 0x04881d05, 0x04881d05, 0x04881d05
	dd	0xd9d4d039, 0xd9d4d039, 0xd9d4d039, 0xd9d4d039
	dd	0xd9d4d039, 0xd9d4d039, 0xd9d4d039, 0xd9d4d039
	dd	0xe6db99e5, 0xe6db99e5, 0xe6db99e5, 0xe6db99e5
	dd	0xe6db99e5, 0xe6db99e5, 0xe6db99e5, 0xe6db99e5
	dd	0x1fa27cf8, 0x1fa27cf8, 0x1fa27cf8, 0x1fa27cf8
	dd	0x1fa27cf8, 0x1fa27cf8, 0x1fa27cf8, 0x1fa27cf8
	dd	0xc4ac5665, 0xc4ac5665, 0xc4ac5665, 0xc4ac5665
	dd	0xc4ac5665, 0xc4ac5665, 0xc4ac5665, 0xc4ac5665
	dd	0xf4292244, 0xf4292244, 0xf4292244, 0xf4292244
	dd	0xf4292244, 0xf4292244, 0xf4292244, 0xf4292244
	dd	0x432aff97, 0x432aff97, 0x432aff97, 0x432aff97
	dd	0x432aff97, 0x432aff97, 0x432aff97, 0x432aff97
	dd	0xab9423a7, 0xab9423a7, 0xab9423a7, 0xab9423a7
	dd	0xab9423a7, 0xab9423a7, 0xab9423a7, 0xab9423a7
	dd	0xfc93a039, 0xfc93a039, 0xfc93a039, 0xfc93a039
	dd	0xfc93a039, 0xfc93a039, 0xfc93a039, 0xfc93a039
	dd	0x655b59c3, 0x655b59c3, 0x655b59c3, 0x655b59c3
	dd	0x655b59c3, 0x655b59c3, 0x655b59c3, 0x655b59c3
	dd	0x8f0ccc92, 0x8f0ccc92, 0x8f0ccc92, 0x8f0ccc92
	dd	0x8f0ccc92, 0x8f0ccc92, 0x8f0ccc92, 0x8f0ccc92
	dd	0xffeff47d, 0xffeff47d, 0xffeff47d, 0xffeff47d
	dd	0xffeff47d, 0xffeff47d, 0xffeff47d, 0xffeff47d
	dd	0x85845dd1, 0x85845dd1, 0x85845dd1, 0x85845dd1
	dd	0x85845dd1, 0x85845dd1, 0x85845dd1, 0x85845dd1
	dd	0x6fa87e4f, 0x6fa87e4f, 0x6fa87e4f, 0x6fa87e4f
	dd	0x6fa87e4f, 0x6fa87e4f, 0x6fa87e4f, 0x6fa87e4f
	dd	0xfe2ce6e0, 0xfe2ce6e0, 0xfe2ce6e0, 0xfe2ce6e0
	dd	0xfe2ce6e0, 0xfe2ce6e0, 0xfe2ce6e0, 0xfe2ce6e0
	dd	0xa3014314, 0xa3014314, 0xa3014314, 0xa3014314
	dd	0xa3014314, 0xa3014314, 0xa3014314, 0xa3014314
	dd	0x4e0811a1, 0x4e0811a1, 0x4e0811a1, 0x4e0811a1
	dd	0x4e0811a1, 0x4e0811a1, 0x4e0811a1, 0x4e0811a1
	dd	0xf7537e82, 0xf7537e82, 0xf7537e82, 0xf7537e82
	dd	0xf7537e82, 0xf7537e82, 0xf7537e82, 0xf7537e82
	dd	0xbd3af235, 0xbd3af235, 0xbd3af235, 0xbd3af235
	dd	0xbd3af235, 0xbd3af235, 0xbd3af235, 0xbd3af235
	dd	0x2ad7d2bb, 0x2ad7d2bb, 0x2ad7d2bb, 0x2ad7d2bb
	dd	0x2ad7d2bb, 0x2ad7d2bb, 0x2ad7d2bb, 0x2ad7d2bb
	dd	0xeb86d391, 0xeb86d391, 0xeb86d391, 0xeb86d391
	dd	0xeb86d391, 0xeb86d391, 0xeb86d391, 0xeb86d391
ONES:	dd	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
	dd	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff

mksection .text

%ifndef LINUX
   %define arg1 rcx
   %define arg2 rdx
   %define reg3 rdi
   %define reg4 rsi
%else
   %define arg1 rdi
   %define arg2 rsi
   %define reg3 rcx
   %define reg4 rdx
%endif

;; rbp is not clobbered

%define state    arg1
%define num_blks arg2

%define inp0 r8
%define inp1 r9
%define inp2 r10
%define inp3 r11
%define inp4 r12
%define inp5 r13
%define inp6 r14
%define inp7 r15

;; These are pointers to data block1 and block2 in the stack
; which will ping pong back and forth
%define DPTR1	rbx
%define DPTR2	reg3

%define TBL  rax
%define IDX  reg4

;; Transposed Digest Storage
%define Y_A	ymm0
%define Y_B	ymm1
%define Y_C	ymm2
%define Y_D	ymm3
%define Y_A2	ymm4
%define Y_B2	ymm5
%define Y_C2	ymm6
%define Y_D2	ymm7

;; Temp YMM registers corresponding to the Temp XMM registers
;; used during the transposition of the digests
%define Y_KTMP1	ymm12
%define Y_KTMP2	ymm13
;; Temporary registers used during MD5 round operations
%define Y_FUN	ymm8
%define Y_TMP	ymm9
%define Y_FUN2	ymm10
%define Y_TMP2	ymm11

;; YMM registers used during data fetching.
;; Data are stored into the stack after transposition
%define Y_DAT0	ymm8
%define Y_DAT1	ymm9
%define Y_DAT2	ymm10
%define Y_DAT3	ymm11
%define Y_DAT4	ymm12
%define Y_DAT5	ymm13
%define Y_DAT6	ymm14
%define Y_DAT7	ymm15

;; Temporary registers used during data transposition
%define Y_DTMP1	ymm0
%define Y_DTMP2	ymm1

%define RESY	resb 32*
;; Assume stack aligned to 32 bytes before call
;; Therefore FRAMESIZE mod 32 must be 32-8 = 24
struc STACK
_DATA:		RESY	2*2*16  ; 2 blocks * 2 sets of lanes * 16 regs
_DIGEST:	RESY	8	; stores Y_AA-Y_DD, Y_AA2-Y_DD2
_TMPDIGEST:	RESY	2	; stores Y_AA, Y_BB temporarily
		resb	24	; align
endstruc

%define	Y_AA	rsp + _DIGEST + 32*0
%define	Y_BB	rsp + _DIGEST + 32*1
%define	Y_CC	rsp + _DIGEST + 32*2
%define	Y_DD	rsp + _DIGEST + 32*3
%define	Y_AA2	rsp + _DIGEST + 32*4
%define	Y_BB2	rsp + _DIGEST + 32*5
%define	Y_CC2	rsp + _DIGEST + 32*6
%define	Y_DD2	rsp + _DIGEST + 32*7

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

;;
;; Magic functions defined in RFC 1321
;;
; macro MAGIC_F F,X,Y,Z   ;; F = ((Z) ^ ((X) & ((Y) ^ (Z))))
%macro MAGIC_F 4
%define %%F %1
%define %%X %2
%define %%Y %3
%define %%Z %4
   vpxor     %%F,%%Z, %%Y
   vpand     %%F,%%F,%%X
   vpxor     %%F,%%F,%%Z
%endmacro

; macro MAGIC_G F,X,Y,Z   ;; F = F((Z),(X),(Y))
%macro MAGIC_G 4
%define %%F %1
%define %%X %2
%define %%Y %3
%define %%Z %4
   MAGIC_F  %%F,%%Z,%%X,%%Y
%endmacro

; macro MAGIC_H F,X,Y,Z   ;; F = ((X) ^ (Y) ^ (Z))
%macro MAGIC_H 4
%define %%F %1
%define %%X %2
%define %%Y %3
%define %%Z %4
   vpxor     %%F,%%Z, %%Y
   vpxor     %%F,%%F, %%X
%endmacro

; macro MAGIC_I F,X,Y,Z   ;; F =  ((Y) ^ ((X) | ~(Z)))
%macro MAGIC_I 4
%define %%F %1
%define %%X %2
%define %%Y %3
%define %%Z %4
   vpxor     %%F,%%Z,[rel ONES]  ; pnot     %%F
   vpor      %%F,%%F,%%X
   vpxor     %%F,%%F,%%Y
%endmacro

; PROLD reg, imm, tmp
%macro PROLD 3
%define %%reg %1
%define %%imm %2
%define %%tmp %3
	vpsrld	%%tmp, %%reg, (32-%%imm)
	vpslld	%%reg, %%reg, %%imm
	vpor	%%reg, %%reg, %%tmp
%endmacro

;;
;; single MD5 step
;;
;; A = B +ROL32((A +MAGIC(B,C,D) +data +const), nrot)
;;
; macro MD5_STEP MAGIC_FUN, A,B,C,D, A2,B2,C3,D2, FUN, TMP, FUN2, TMP2, data,
;                MD5const, nrot
%macro MD5_STEP 16
%define %%MAGIC_FUN	%1
%define %%rA		%2
%define %%rB		%3
%define %%rC		%4
%define %%rD		%5
%define %%rA2		%6
%define %%rB2		%7
%define %%rC2		%8
%define %%rD2		%9
%define %%FUN		%10
%define %%TMP		%11
%define %%FUN2		%12
%define %%TMP2		%13
%define %%data		%14
%define %%MD5const	%15
%define %%nrot		%16

	vpaddd       %%rA, %%rA, %%MD5const
		vpaddd       %%rA2, %%rA2, %%MD5const
	vpaddd       %%rA, %%rA, [%%data]
		vpaddd       %%rA2, %%rA2, [%%data + 16*32]
	%%MAGIC_FUN %%FUN, %%rB,%%rC,%%rD
		%%MAGIC_FUN %%FUN2, %%rB2,%%rC2,%%rD2
	vpaddd       %%rA, %%rA, %%FUN
		vpaddd       %%rA2, %%rA2, %%FUN2
	PROLD       %%rA,%%nrot, %%TMP
		PROLD       %%rA2,%%nrot, %%TMP2
	vpaddd       %%rA, %%rA, %%rB
		vpaddd       %%rA2, %%rA2, %%rB2
%endmacro

align 32

; void md5_x8x2_avx(MD5_ARGS *args, UINT64 num_blks)
; arg 1 : pointer to MD5_ARGS structure
; arg 2 : number of blocks (>=1)

MKGLOBAL(md5_x8x2_avx2,function,internal)
md5_x8x2_avx2:
        endbranch64
	sub	rsp, STACK_size

	mov	DPTR1, rsp
	lea	DPTR2, [rsp + 32*32]

	;; Load MD5 constant pointer to register
	lea	TBL, [rel MD5_TABLE]

	; Initialize index for data retrieval
	xor	IDX, IDX

	;; Fetch Pointers to Data Stream 1 to 8
	mov	inp0,[state + _data_ptr_md5+0*PTR_SZ]
	mov	inp1,[state + _data_ptr_md5+1*PTR_SZ]
	mov	inp2,[state + _data_ptr_md5+2*PTR_SZ]
	mov	inp3,[state + _data_ptr_md5+3*PTR_SZ]
	mov	inp4,[state + _data_ptr_md5+4*PTR_SZ]
	mov	inp5,[state + _data_ptr_md5+5*PTR_SZ]
	mov	inp6,[state + _data_ptr_md5+6*PTR_SZ]
	mov	inp7,[state + _data_ptr_md5+7*PTR_SZ]

%assign I 0
%rep 2
	TRANSPOSE8_U32_LOAD8 Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, \
			     inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, IDX+I*32

	TRANSPOSE8_U32_PRELOADED	Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, Y_DTMP1, Y_DTMP2
	vmovdqa	[DPTR1+_DATA+(I*8+0)*32],Y_DAT0
	vmovdqa	[DPTR1+_DATA+(I*8+1)*32],Y_DAT1
	vmovdqa	[DPTR1+_DATA+(I*8+2)*32],Y_DAT2
	vmovdqa	[DPTR1+_DATA+(I*8+3)*32],Y_DAT3
	vmovdqa	[DPTR1+_DATA+(I*8+4)*32],Y_DAT4
	vmovdqa	[DPTR1+_DATA+(I*8+5)*32],Y_DAT5
	vmovdqa	[DPTR1+_DATA+(I*8+6)*32],Y_DAT6
	vmovdqa	[DPTR1+_DATA+(I*8+7)*32],Y_DAT7

%assign I (I+1)
%endrep

	;; Fetch Pointers to Data Stream 9 to 16
	mov	inp0,[state + _data_ptr_md5 + 8*8]
	mov	inp1,[state + _data_ptr_md5 + 9*8]
	mov	inp2,[state + _data_ptr_md5 + 10*8]
	mov	inp3,[state + _data_ptr_md5 + 11*8]
	mov	inp4,[state + _data_ptr_md5 + 12*8]
	mov	inp5,[state + _data_ptr_md5 + 13*8]
	mov	inp6,[state + _data_ptr_md5 + 14*8]
	mov	inp7,[state + _data_ptr_md5 + 15*8]

%assign I 0
%rep 2
	TRANSPOSE8_U32_LOAD8 Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, \
			     inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, IDX+I*32

	TRANSPOSE8_U32_PRELOADED	Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, Y_DTMP1, Y_DTMP2
	vmovdqa	[DPTR1+_DATA+((I+2)*8+0)*32],Y_DAT0
	vmovdqa	[DPTR1+_DATA+((I+2)*8+1)*32],Y_DAT1
	vmovdqa	[DPTR1+_DATA+((I+2)*8+2)*32],Y_DAT2
	vmovdqa	[DPTR1+_DATA+((I+2)*8+3)*32],Y_DAT3
	vmovdqa	[DPTR1+_DATA+((I+2)*8+4)*32],Y_DAT4
	vmovdqa	[DPTR1+_DATA+((I+2)*8+5)*32],Y_DAT5
	vmovdqa	[DPTR1+_DATA+((I+2)*8+6)*32],Y_DAT6
	vmovdqa	[DPTR1+_DATA+((I+2)*8+7)*32],Y_DAT7

%assign I (I+1)
%endrep
        ;; digests are already transposed
	vmovdqu	Y_A,[state + 0 * MD5_DIGEST_ROW_SIZE ]
	vmovdqu	Y_B,[state + 1 * MD5_DIGEST_ROW_SIZE ]
	vmovdqu	Y_C,[state + 2 * MD5_DIGEST_ROW_SIZE ]
        vmovdqu	Y_D,[state + 3 * MD5_DIGEST_ROW_SIZE ]

	; Load the digest for each stream (9-16)
	vmovdqu	Y_A2,[state + 0 * MD5_DIGEST_ROW_SIZE + 32]
	vmovdqu	Y_B2,[state + 1 * MD5_DIGEST_ROW_SIZE + 32]
	vmovdqu	Y_C2,[state + 2 * MD5_DIGEST_ROW_SIZE + 32]
        vmovdqu	Y_D2,[state + 3 * MD5_DIGEST_ROW_SIZE + 32]

lloop:
        endbranch64
	; save old digests to stack
	vmovdqa	[Y_AA], Y_A
	vmovdqa	[Y_BB], Y_B
	vmovdqa	[Y_CC], Y_C
	vmovdqa	[Y_DD], Y_D

	vmovdqa	[Y_AA2], Y_A2
	vmovdqa	[Y_BB2], Y_B2
	vmovdqa	[Y_CC2], Y_C2
	vmovdqa	[Y_DD2], Y_D2

	;; Increment IDX to point to next data block (64 bytes per block)
	add	IDX, 64

	;; Update size of remaining blocks to process
	sub	num_blks, 1
	je	lastblock

	; Perform the 64 rounds of processing ...
	MD5_STEP MAGIC_F, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 0*32, [TBL+ 0*32], rot11
	MD5_STEP MAGIC_F, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 1*32, [TBL+ 1*32], rot12
	MD5_STEP MAGIC_F, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 2*32, [TBL+ 2*32], rot13
	MD5_STEP MAGIC_F, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 3*32, [TBL+ 3*32], rot14
	MD5_STEP MAGIC_F, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 4*32, [TBL+ 4*32], rot11
	MD5_STEP MAGIC_F, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 5*32, [TBL+ 5*32], rot12
	MD5_STEP MAGIC_F, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 6*32, [TBL+ 6*32], rot13
	MD5_STEP MAGIC_F, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 7*32, [TBL+ 7*32], rot14

	;; Fetch Pointers to Data Stream 1 to 8 ??
	mov	inp0,[state + _data_ptr_md5 + 0*8]
	mov	inp1,[state + _data_ptr_md5 + 1*8]
	mov	inp2,[state + _data_ptr_md5 + 2*8]
	mov	inp3,[state + _data_ptr_md5 + 3*8]
	mov	inp4,[state + _data_ptr_md5 + 4*8]
	mov	inp5,[state + _data_ptr_md5 + 5*8]
	mov	inp6,[state + _data_ptr_md5 + 6*8]
	mov	inp7,[state + _data_ptr_md5 + 7*8]

	MD5_STEP MAGIC_F, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 8*32, [TBL+ 8*32], rot11
	MD5_STEP MAGIC_F, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 9*32, [TBL+ 9*32], rot12
	MD5_STEP MAGIC_F, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+10*32, [TBL+10*32], rot13
	MD5_STEP MAGIC_F, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+11*32, [TBL+11*32], rot14
	MD5_STEP MAGIC_F, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+12*32, [TBL+12*32], rot11
	MD5_STEP MAGIC_F, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+13*32, [TBL+13*32], rot12
	MD5_STEP MAGIC_F, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+14*32, [TBL+14*32], rot13
	MD5_STEP MAGIC_F, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+15*32, [TBL+15*32], rot14

%assign I 0

	; Y_A and Y_B share the same registers with Y_DTMP1 and Y_DTMP2
	; Therefore we need to save these to stack and restore after transpose
	vmovdqa  [rsp + _TMPDIGEST + 0*32], Y_A
	vmovdqa  [rsp + _TMPDIGEST + 1*32], Y_B

	TRANSPOSE8_U32_LOAD8 Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, \
			     inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, IDX+I*32

	TRANSPOSE8_U32_PRELOADED	Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, Y_DTMP1, Y_DTMP2
	vmovdqa	[DPTR2+_DATA+(I*8+0)*32],Y_DAT0
	vmovdqa	[DPTR2+_DATA+(I*8+1)*32],Y_DAT1
	vmovdqa	[DPTR2+_DATA+(I*8+2)*32],Y_DAT2
	vmovdqa	[DPTR2+_DATA+(I*8+3)*32],Y_DAT3
	vmovdqa	[DPTR2+_DATA+(I*8+4)*32],Y_DAT4
	vmovdqa	[DPTR2+_DATA+(I*8+5)*32],Y_DAT5
	vmovdqa	[DPTR2+_DATA+(I*8+6)*32],Y_DAT6
	vmovdqa	[DPTR2+_DATA+(I*8+7)*32],Y_DAT7

	; Restore Y_A and Y_B
	vmovdqa  Y_A, [rsp + _TMPDIGEST + 0*32]
	vmovdqa  Y_B, [rsp + _TMPDIGEST + 1*32]

	MD5_STEP MAGIC_G, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 1*32, [TBL+16*32], rot21
	MD5_STEP MAGIC_G, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 6*32, [TBL+17*32], rot22
	MD5_STEP MAGIC_G, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+11*32, [TBL+18*32], rot23
	MD5_STEP MAGIC_G, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 0*32, [TBL+19*32], rot24
	MD5_STEP MAGIC_G, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 5*32, [TBL+20*32], rot21
	MD5_STEP MAGIC_G, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+10*32, [TBL+21*32], rot22
	MD5_STEP MAGIC_G, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+15*32, [TBL+22*32], rot23
	MD5_STEP MAGIC_G, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 4*32, [TBL+23*32], rot24
	MD5_STEP MAGIC_G, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 9*32, [TBL+24*32], rot21
	MD5_STEP MAGIC_G, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+14*32, [TBL+25*32], rot22
	MD5_STEP MAGIC_G, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 3*32, [TBL+26*32], rot23
	MD5_STEP MAGIC_G, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 8*32, [TBL+27*32], rot24
	MD5_STEP MAGIC_G, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+13*32, [TBL+28*32], rot21
	MD5_STEP MAGIC_G, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 2*32, [TBL+29*32], rot22
	MD5_STEP MAGIC_G, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 7*32, [TBL+30*32], rot23
	MD5_STEP MAGIC_G, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+12*32, [TBL+31*32], rot24

%assign I (I+1)

	; Y_A and Y_B share the same registers with Y_DTMP1 and Y_DTMP2
	; Therefore we need to save these to stack and restore after transpose
	vmovdqa  [rsp + _TMPDIGEST + 0*32], Y_A
	vmovdqa  [rsp + _TMPDIGEST + 1*32], Y_B

	TRANSPOSE8_U32_LOAD8 Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, \
			     inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, IDX+I*32

	TRANSPOSE8_U32_PRELOADED	Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, Y_DTMP1, Y_DTMP2
	vmovdqa	[DPTR2+_DATA+(I*8+0)*32],Y_DAT0
	vmovdqa	[DPTR2+_DATA+(I*8+1)*32],Y_DAT1
	vmovdqa	[DPTR2+_DATA+(I*8+2)*32],Y_DAT2
	vmovdqa	[DPTR2+_DATA+(I*8+3)*32],Y_DAT3
	vmovdqa	[DPTR2+_DATA+(I*8+4)*32],Y_DAT4
	vmovdqa	[DPTR2+_DATA+(I*8+5)*32],Y_DAT5
	vmovdqa	[DPTR2+_DATA+(I*8+6)*32],Y_DAT6
	vmovdqa	[DPTR2+_DATA+(I*8+7)*32],Y_DAT7

	; Restore Y_A and Y_B
	vmovdqa  Y_A, [rsp + _TMPDIGEST + 0*32]
	vmovdqa  Y_B, [rsp + _TMPDIGEST + 1*32]

	MD5_STEP MAGIC_H, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 5*32, [TBL+32*32], rot31
	MD5_STEP MAGIC_H, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 8*32, [TBL+33*32], rot32
	MD5_STEP MAGIC_H, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+11*32, [TBL+34*32], rot33
	MD5_STEP MAGIC_H, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+14*32, [TBL+35*32], rot34
	MD5_STEP MAGIC_H, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 1*32, [TBL+36*32], rot31
	MD5_STEP MAGIC_H, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 4*32, [TBL+37*32], rot32
	MD5_STEP MAGIC_H, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 7*32, [TBL+38*32], rot33
	MD5_STEP MAGIC_H, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+10*32, [TBL+39*32], rot34

	;; Fetch Pointers to Data Stream 9 to 16
	mov	inp0,[state + _data_ptr_md5 + 8*8]
	mov	inp1,[state + _data_ptr_md5 + 9*8]
	mov	inp2,[state + _data_ptr_md5 + 10*8]
	mov	inp3,[state + _data_ptr_md5 + 11*8]
	mov	inp4,[state + _data_ptr_md5 + 12*8]
	mov	inp5,[state + _data_ptr_md5 + 13*8]
	mov	inp6,[state + _data_ptr_md5 + 14*8]
	mov	inp7,[state + _data_ptr_md5 + 15*8]

	MD5_STEP MAGIC_H, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+13*32, [TBL+40*32], rot31
	MD5_STEP MAGIC_H, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 0*32, [TBL+41*32], rot32
	MD5_STEP MAGIC_H, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 3*32, [TBL+42*32], rot33
	MD5_STEP MAGIC_H, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 6*32, [TBL+43*32], rot34
	MD5_STEP MAGIC_H, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 9*32, [TBL+44*32], rot31
	MD5_STEP MAGIC_H, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+12*32, [TBL+45*32], rot32
	MD5_STEP MAGIC_H, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+15*32, [TBL+46*32], rot33
	MD5_STEP MAGIC_H, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 2*32, [TBL+47*32], rot34

%assign I 0

	; Y_A and Y_B share the same registers with Y_DTMP1 and Y_DTMP2
	; Therefore we need to save these to stack and restore after transpose
	vmovdqa  [rsp + _TMPDIGEST + 0*32], Y_A
	vmovdqa  [rsp + _TMPDIGEST + 1*32], Y_B

	TRANSPOSE8_U32_LOAD8 Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, \
			     inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, IDX+I*32

	TRANSPOSE8_U32_PRELOADED	Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, Y_DTMP1, Y_DTMP2
	vmovdqa	[DPTR2+_DATA+((I+2)*8+0)*32],Y_DAT0
	vmovdqa	[DPTR2+_DATA+((I+2)*8+1)*32],Y_DAT1
	vmovdqa	[DPTR2+_DATA+((I+2)*8+2)*32],Y_DAT2
	vmovdqa	[DPTR2+_DATA+((I+2)*8+3)*32],Y_DAT3
	vmovdqa	[DPTR2+_DATA+((I+2)*8+4)*32],Y_DAT4
	vmovdqa	[DPTR2+_DATA+((I+2)*8+5)*32],Y_DAT5
	vmovdqa	[DPTR2+_DATA+((I+2)*8+6)*32],Y_DAT6
	vmovdqa	[DPTR2+_DATA+((I+2)*8+7)*32],Y_DAT7

	; Restore Y_A and Y_B
	vmovdqa  Y_A, [rsp + _TMPDIGEST + 0*32]
	vmovdqa  Y_B, [rsp + _TMPDIGEST + 1*32]

	MD5_STEP MAGIC_I, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 0*32, [TBL+48*32], rot41
	MD5_STEP MAGIC_I, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 7*32, [TBL+49*32], rot42
	MD5_STEP MAGIC_I, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+14*32, [TBL+50*32], rot43
	MD5_STEP MAGIC_I, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 5*32, [TBL+51*32], rot44
	MD5_STEP MAGIC_I, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+12*32, [TBL+52*32], rot41
	MD5_STEP MAGIC_I, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 3*32, [TBL+53*32], rot42
	MD5_STEP MAGIC_I, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+10*32, [TBL+54*32], rot43
	MD5_STEP MAGIC_I, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 1*32, [TBL+55*32], rot44
	MD5_STEP MAGIC_I, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 8*32, [TBL+56*32], rot41
	MD5_STEP MAGIC_I, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+15*32, [TBL+57*32], rot42
	MD5_STEP MAGIC_I, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 6*32, [TBL+58*32], rot43
	MD5_STEP MAGIC_I, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+13*32, [TBL+59*32], rot44
	MD5_STEP MAGIC_I, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 4*32, [TBL+60*32], rot41
	MD5_STEP MAGIC_I, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+11*32, [TBL+61*32], rot42
	MD5_STEP MAGIC_I, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 2*32, [TBL+62*32], rot43
	MD5_STEP MAGIC_I, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 9*32, [TBL+63*32], rot44

%assign I (I+1)

	; Y_A and Y_B share the same registers with Y_DTMP1 and Y_DTMP2
	; Therefore we need to save these to stack and restore after transpose
	vmovdqa  [rsp + _TMPDIGEST + 0*32], Y_A
	vmovdqa  [rsp + _TMPDIGEST + 1*32], Y_B

	TRANSPOSE8_U32_LOAD8 Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, \
			     inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, IDX+I*32

	TRANSPOSE8_U32_PRELOADED	Y_DAT0, Y_DAT1, Y_DAT2, Y_DAT3, Y_DAT4, Y_DAT5, Y_DAT6, Y_DAT7, Y_DTMP1, Y_DTMP2
	vmovdqa	[DPTR2+_DATA+((I+2)*8+0)*32],Y_DAT0
	vmovdqa	[DPTR2+_DATA+((I+2)*8+1)*32],Y_DAT1
	vmovdqa	[DPTR2+_DATA+((I+2)*8+2)*32],Y_DAT2
	vmovdqa	[DPTR2+_DATA+((I+2)*8+3)*32],Y_DAT3
	vmovdqa	[DPTR2+_DATA+((I+2)*8+4)*32],Y_DAT4
	vmovdqa	[DPTR2+_DATA+((I+2)*8+5)*32],Y_DAT5
	vmovdqa	[DPTR2+_DATA+((I+2)*8+6)*32],Y_DAT6
	vmovdqa	[DPTR2+_DATA+((I+2)*8+7)*32],Y_DAT7

	; Restore Y_A and Y_B
	vmovdqa  Y_A, [rsp + _TMPDIGEST + 0*32]
	vmovdqa  Y_B, [rsp + _TMPDIGEST + 1*32]

	; Add results to old digest values

	vpaddd	Y_A,Y_A,[Y_AA]
	vpaddd	Y_B,Y_B,[Y_BB]
	vpaddd	Y_C,Y_C,[Y_CC]
	vpaddd	Y_D,Y_D,[Y_DD]

	vpaddd	Y_A2,Y_A2,[Y_AA2]
	vpaddd	Y_B2,Y_B2,[Y_BB2]
	vpaddd	Y_C2,Y_C2,[Y_CC2]
	vpaddd	Y_D2,Y_D2,[Y_DD2]

	; Swap DPTR1 and DPTR2
	xchg	DPTR1, DPTR2

	;; Proceed to processing of next block
	jmp 	lloop

lastblock:

	; Perform the 64 rounds of processing ...
	MD5_STEP MAGIC_F, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 0*32, [TBL+ 0*32], rot11
	MD5_STEP MAGIC_F, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 1*32, [TBL+ 1*32], rot12
	MD5_STEP MAGIC_F, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 2*32, [TBL+ 2*32], rot13
	MD5_STEP MAGIC_F, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 3*32, [TBL+ 3*32], rot14
	MD5_STEP MAGIC_F, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 4*32, [TBL+ 4*32], rot11
	MD5_STEP MAGIC_F, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 5*32, [TBL+ 5*32], rot12
	MD5_STEP MAGIC_F, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 6*32, [TBL+ 6*32], rot13
	MD5_STEP MAGIC_F, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 7*32, [TBL+ 7*32], rot14
	MD5_STEP MAGIC_F, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 8*32, [TBL+ 8*32], rot11
	MD5_STEP MAGIC_F, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 9*32, [TBL+ 9*32], rot12
	MD5_STEP MAGIC_F, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+10*32, [TBL+10*32], rot13
	MD5_STEP MAGIC_F, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+11*32, [TBL+11*32], rot14
	MD5_STEP MAGIC_F, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+12*32, [TBL+12*32], rot11
	MD5_STEP MAGIC_F, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+13*32, [TBL+13*32], rot12
	MD5_STEP MAGIC_F, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+14*32, [TBL+14*32], rot13
	MD5_STEP MAGIC_F, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+15*32, [TBL+15*32], rot14

	MD5_STEP MAGIC_G, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 1*32, [TBL+16*32], rot21
	MD5_STEP MAGIC_G, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 6*32, [TBL+17*32], rot22
	MD5_STEP MAGIC_G, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+11*32, [TBL+18*32], rot23
	MD5_STEP MAGIC_G, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 0*32, [TBL+19*32], rot24
	MD5_STEP MAGIC_G, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 5*32, [TBL+20*32], rot21
	MD5_STEP MAGIC_G, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+10*32, [TBL+21*32], rot22
	MD5_STEP MAGIC_G, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+15*32, [TBL+22*32], rot23
	MD5_STEP MAGIC_G, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 4*32, [TBL+23*32], rot24
	MD5_STEP MAGIC_G, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 9*32, [TBL+24*32], rot21
	MD5_STEP MAGIC_G, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+14*32, [TBL+25*32], rot22
	MD5_STEP MAGIC_G, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 3*32, [TBL+26*32], rot23
	MD5_STEP MAGIC_G, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 8*32, [TBL+27*32], rot24
	MD5_STEP MAGIC_G, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+13*32, [TBL+28*32], rot21
	MD5_STEP MAGIC_G, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 2*32, [TBL+29*32], rot22
	MD5_STEP MAGIC_G, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 7*32, [TBL+30*32], rot23
	MD5_STEP MAGIC_G, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+12*32, [TBL+31*32], rot24

	MD5_STEP MAGIC_H, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 5*32, [TBL+32*32], rot31
	MD5_STEP MAGIC_H, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 8*32, [TBL+33*32], rot32
	MD5_STEP MAGIC_H, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+11*32, [TBL+34*32], rot33
	MD5_STEP MAGIC_H, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+14*32, [TBL+35*32], rot34
	MD5_STEP MAGIC_H, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 1*32, [TBL+36*32], rot31
	MD5_STEP MAGIC_H, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 4*32, [TBL+37*32], rot32
	MD5_STEP MAGIC_H, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 7*32, [TBL+38*32], rot33
	MD5_STEP MAGIC_H, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+10*32, [TBL+39*32], rot34
	MD5_STEP MAGIC_H, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+13*32, [TBL+40*32], rot31
	MD5_STEP MAGIC_H, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 0*32, [TBL+41*32], rot32
	MD5_STEP MAGIC_H, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 3*32, [TBL+42*32], rot33
	MD5_STEP MAGIC_H, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 6*32, [TBL+43*32], rot34
	MD5_STEP MAGIC_H, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 9*32, [TBL+44*32], rot31
	MD5_STEP MAGIC_H, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+12*32, [TBL+45*32], rot32
	MD5_STEP MAGIC_H, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+15*32, [TBL+46*32], rot33
	MD5_STEP MAGIC_H, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 2*32, [TBL+47*32], rot34

	MD5_STEP MAGIC_I, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 0*32, [TBL+48*32], rot41
	MD5_STEP MAGIC_I, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 7*32, [TBL+49*32], rot42
	MD5_STEP MAGIC_I, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+14*32, [TBL+50*32], rot43
	MD5_STEP MAGIC_I, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 5*32, [TBL+51*32], rot44
	MD5_STEP MAGIC_I, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+12*32, [TBL+52*32], rot41
	MD5_STEP MAGIC_I, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 3*32, [TBL+53*32], rot42
	MD5_STEP MAGIC_I, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+10*32, [TBL+54*32], rot43
	MD5_STEP MAGIC_I, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 1*32, [TBL+55*32], rot44
	MD5_STEP MAGIC_I, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 8*32, [TBL+56*32], rot41
	MD5_STEP MAGIC_I, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+15*32, [TBL+57*32], rot42
	MD5_STEP MAGIC_I, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 6*32, [TBL+58*32], rot43
	MD5_STEP MAGIC_I, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+13*32, [TBL+59*32], rot44
	MD5_STEP MAGIC_I, Y_A,Y_B,Y_C,Y_D, Y_A2,Y_B2,Y_C2,Y_D2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 4*32, [TBL+60*32], rot41
	MD5_STEP MAGIC_I, Y_D,Y_A,Y_B,Y_C, Y_D2,Y_A2,Y_B2,Y_C2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+11*32, [TBL+61*32], rot42
	MD5_STEP MAGIC_I, Y_C,Y_D,Y_A,Y_B, Y_C2,Y_D2,Y_A2,Y_B2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 2*32, [TBL+62*32], rot43
	MD5_STEP MAGIC_I, Y_B,Y_C,Y_D,Y_A, Y_B2,Y_C2,Y_D2,Y_A2, Y_FUN,Y_TMP, Y_FUN2,Y_TMP2, DPTR1+ 9*32, [TBL+63*32], rot44

        ;; update into data pointers
%assign I 0
%rep 8
        mov    inp0, [state + _data_ptr_md5 + (2*I)*8]
        mov    inp1, [state + _data_ptr_md5 + (2*I +1)*8]
        add    inp0, IDX
        add    inp1, IDX
        mov    [state + _data_ptr_md5 + (2*I)*8], inp0
        mov    [state + _data_ptr_md5 + (2*I+1)*8], inp1
%assign I (I+1)
%endrep

	vpaddd	Y_A,Y_A,[Y_AA]
	vpaddd	Y_B,Y_B,[Y_BB]
	vpaddd	Y_C,Y_C,[Y_CC]
	vpaddd	Y_D,Y_D,[Y_DD]

	vpaddd	Y_A2,Y_A2,[Y_AA2]
	vpaddd	Y_B2,Y_B2,[Y_BB2]
	vpaddd	Y_C2,Y_C2,[Y_CC2]
	vpaddd	Y_D2,Y_D2,[Y_DD2]

	vmovdqu	[state + 0*MD5_DIGEST_ROW_SIZE  ],Y_A
        vmovdqu	[state + 1*MD5_DIGEST_ROW_SIZE  ],Y_B
        vmovdqu	[state + 2*MD5_DIGEST_ROW_SIZE  ],Y_C
        vmovdqu	[state + 3*MD5_DIGEST_ROW_SIZE  ],Y_D

        vmovdqu	[state + 0*MD5_DIGEST_ROW_SIZE  + 32 ],Y_A2   ;; 32 is YMM width
        vmovdqu	[state + 1*MD5_DIGEST_ROW_SIZE  + 32 ],Y_B2
        vmovdqu	[state + 2*MD5_DIGEST_ROW_SIZE  + 32 ],Y_C2
        vmovdqu	[state + 3*MD5_DIGEST_ROW_SIZE  + 32 ],Y_D2

	;;;;;;;;;;;;;;;;
	;; Postamble

        ;; Clear stack frame ((64+8+2)*32 bytes)
%ifdef SAFE_DATA
	clear_all_ymms_asm
%assign i 0
%rep (2*2*16+8+2)
        vmovdqa [rsp + i*32], ymm0
%assign i (i+1)
%endrep
%endif

	add	rsp, STACK_size

        ret

mksection stack-noexec

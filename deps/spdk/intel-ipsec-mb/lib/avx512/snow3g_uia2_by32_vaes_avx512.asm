;;
;; Copyright (c) 2021, Intel Corporation
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

%include "include/os.asm"
%include "include/reg_sizes.asm"
%include "include/cet.inc"
%include "include/memcpy.asm"
%include "include/const.inc"
%include "include/aes_common.asm"
%define APPEND(a,b) a %+ b
%define APPEND3(a,b,c) a %+ b %+ c

%ifdef LINUX
%define arg1 rdi
%define arg2 rsi
%define arg3 rdx
%define arg4 rcx
%else
%define arg1 rcx
%define arg2 rdx
%define arg3 r8
%define arg4 r9
%endif

%define E                  rax
%define qword_len          r12
%define offset             r10
%define tmp                r10
%define tmp2               arg4
%define tmp3               r11
%define tmp4               r13
%define tmp5               r14
%define tmp6               r15
%define in_ptr             arg1
%define KS                 arg2
%define bit_len            arg3
%define end_offset         tmp3

%define EV                 xmm2
%define SNOW3G_CONST       xmm7
%define P1                 xmm8
%define BSWAP64_MASK       zmm12

%define INSERT_HIGH64_MASK k1

mksection .rodata
default rel

align 64
snow3g_constant:
dq      0x000000000000001b, 0x0000000000000000
dq      0x000000000000001b, 0x0000000000000000
dq      0x000000000000001b, 0x0000000000000000
dq      0x000000000000001b, 0x0000000000000000

align 64
bswap64:
dq      0x0001020304050607, 0x08090a0b0c0d0e0f
dq      0x0001020304050607, 0x08090a0b0c0d0e0f
dq      0x0001020304050607, 0x08090a0b0c0d0e0f
dq      0x0001020304050607, 0x08090a0b0c0d0e0f

align 64
mask64:
dq      0xffffffffffffffff,
dq      0xff,
dq      0xffff,
dq      0xffffff,
dq      0xffffffff,
dq      0xffffffffff,
dq      0xffffffffffff,
dq      0xffffffffffffff

align 64
swap_qwords:
dq      0x0f0e0d0c0b0a0908, 0x0706050403020100
dq      0x0f0e0d0c0b0a0908, 0x0706050403020100
dq      0x0f0e0d0c0b0a0908, 0x0706050403020100
dq      0x0f0e0d0c0b0a0908, 0x0706050403020100

mksection .text

%ifidn __OUTPUT_FORMAT__, win64
        %define XMM_STORAGE     16*7
        %define GP_STORAGE      8*8
%else
        %define XMM_STORAGE     0
        %define GP_STORAGE      6*8
%endif

%define CONSTANTS_STORAGE       8*32 ;; 32 8 byte blocks

%define VARIABLE_OFFSET XMM_STORAGE + GP_STORAGE + CONSTANTS_STORAGE
%define GP_OFFSET XMM_STORAGE
%define CONSTANTS_OFFSET XMM_STORAGE + GP_STORAGE

%macro FUNC_SAVE 0
        mov     r11, rsp
        sub     rsp, VARIABLE_OFFSET
        and     rsp, ~15

%ifidn __OUTPUT_FORMAT__, win64
        ; maintain xmms on Windows
        vmovdqa [rsp + 0*16], xmm6
        vmovdqa [rsp + 1*16], xmm7
        vmovdqa [rsp + 2*16], xmm8
        vmovdqa [rsp + 3*16], xmm9
        vmovdqa [rsp + 4*16], xmm10
        vmovdqa [rsp + 5*16], xmm11
        vmovdqa [rsp + 6*16], xmm12
        mov     [rsp + GP_OFFSET + 48], rdi
        mov     [rsp + GP_OFFSET + 56], rsi
%endif
        mov     [rsp + GP_OFFSET],      r12
        mov     [rsp + GP_OFFSET + 8],  r13
        mov     [rsp + GP_OFFSET + 16], r14
        mov     [rsp + GP_OFFSET + 24], r15
        mov     [rsp + GP_OFFSET + 32], rbx
        mov     [rsp + GP_OFFSET + 40], r11 ;; rsp pointer
%endmacro

%macro FUNC_RESTORE 0

%ifidn __OUTPUT_FORMAT__, win64
        vmovdqa xmm6,  [rsp + 0*16]
        vmovdqa xmm7,  [rsp + 1*16]
        vmovdqa xmm8,  [rsp + 2*16]
        vmovdqa xmm9,  [rsp + 3*16]
        vmovdqa xmm10,  [rsp + 4*16]
        vmovdqa xmm11,  [rsp + 5*16]
        vmovdqa xmm12,  [rsp + 6*16]
        mov     rdi, [rsp + GP_OFFSET + 48]
        mov     rsi, [rsp + GP_OFFSET + 56]
%endif
        mov     r12, [rsp + GP_OFFSET]
        mov     r13, [rsp + GP_OFFSET + 8]
        mov     r14, [rsp + GP_OFFSET + 16]
        mov     r15, [rsp + GP_OFFSET + 24]
        mov     rbx, [rsp + GP_OFFSET + 32]
        mov     rsp, [rsp + GP_OFFSET + 40]
%endmacro

;; Horizontal XOR - 4 x 128bits xored together
%macro VHPXORI4x128 2
%define %%REG   %1      ;; [in/out] zmm with 4x128bits to xor; 128bit output
%define %%TMP   %2      ;; [clobbered] zmm temporary register

        vextracti64x4   YWORD(%%TMP), %%REG, 1
        vpxorq          YWORD(%%REG), YWORD(%%REG), YWORD(%%TMP)
        vextracti32x4   XWORD(%%TMP), YWORD(%%REG), 1
        vpxorq          XWORD(%%REG), XWORD(%%REG), XWORD(%%TMP)

%endmacro

;; Horizontal XOR - 2 x 128bits xored together
%macro VHPXORI2x128 2
%define %%REG   %1      ; [in/out] YMM/ZMM with 2x128bits to xor; 128bit output
%define %%TMP   %2      ; [clobbered] XMM/YMM/ZMM temporary register
        vextracti32x4   XWORD(%%TMP), %%REG, 1
        vpxorq          XWORD(%%REG), XWORD(%%REG), XWORD(%%TMP)
%endmacro

;; Reduce from 128 bits to 64 bits
%macro REDUCE_TO_64 2
%define %%IN_OUT        %1 ;; [in/out]
%define %%XTMP          %2 ;; [clobbered]

        vpclmulqdq      %%XTMP, %%IN_OUT, SNOW3G_CONST, 0x01
        vpxor           %%IN_OUT, %%IN_OUT, %%XTMP

        vpclmulqdq      %%XTMP, %%XTMP, SNOW3G_CONST, 0x01
        vpxor           %%IN_OUT, %%IN_OUT, %%XTMP

%endmacro

;; Multiply 64b x 64b and reduce result to 64 bits
;; Lower 64-bits of xmms are multiplied
%macro MUL_AND_REDUCE_64x64_LOW 2-3
%define %%IN0_OUT       %1 ;; [in/out]
%define %%IN1           %2 ;; [in] Note: clobbered when only 2 args passed
%define %%XTMP          %3 ;; [clobbered]

        vpclmulqdq      %%IN0_OUT, %%IN0_OUT, %%IN1, 0x00
%if %0 == 2
        ;; clobber XTMP1 if 3 args passed, otherwise preserve
        REDUCE_TO_64    %%IN0_OUT, %%IN1
%else
        REDUCE_TO_64    %%IN0_OUT, %%XTMP
%endif
%endmacro

;; Multiply 64b x 64b blocks and reduce result to 64 bits.
;; Lower and higher 64-bits of all 128-bit lanes are multiplied.
;; Passing different size regs will operate on a different number of blocks
;; - xmms => 2x2 blocks
;; - ymms => 4x4 blocks
;; - zmms => 8x8 blocks
;; Results are combined and returned in single register
%macro  MUL_AND_REDUCE_64x64  7
%define %%IN0_OUT       %1 ;; [in/out] xmm/ymm/zmm with multiply first operands
%define %%IN1           %2 ;; [in] xmm/ymm/zmm with multiply second operands
%define %%T1            %3 ;; [clobbered] xmm/ymm/zmm
%define %%T2            %4 ;; [clobbered] xmm/ymm/zmm
%define %%T3            %5 ;; [clobbered] xmm/ymm/zmm
%define %%T4            %6 ;; [clobbered] xmm/ymm/zmm
%define %%SNOW3G_CONST  %7 ;; [in] xmm/ymm/zmm with SNOW3G constant

        ;; perform multiplication
        vpclmulqdq      %%T1, %%IN0_OUT, %%IN1, 0x00  ; %%T1 = a0*b0 (for xmms)
        vpclmulqdq      %%T2, %%IN0_OUT, %%IN1, 0x11  ; %%T2 = a1*b1

        ;; perform reduction on results
        vpclmulqdq      %%T3, %%T1, %%SNOW3G_CONST, 0x01
        vpclmulqdq      %%T4, %%T2, %%SNOW3G_CONST, 0x01

        vpxorq          %%T1, %%T1, %%T3
        vpxorq          %%T2, %%T2, %%T4

        vpclmulqdq      %%T3, %%T3, %%SNOW3G_CONST, 0x01
        vpclmulqdq      %%T4, %%T4, %%SNOW3G_CONST, 0x01

        vpxorq          %%IN0_OUT, %%T1, %%T3
        vpxorq          %%T2, %%T2, %%T4

        ;; combine results into single register
        vpunpcklqdq     %%IN0_OUT, %%IN0_OUT, %%T2

%endmacro

;; Precompute powers of P up to P^4 or P^32
;; Results are arranged from highest power to lowest at 128b granularity
;; Example:
;;   For up to P^4, results are returned in a 2 XMM registers
;;   register in the following order:
;;       OUT0[P1P2]
;;       OUT1[P3P4]
;;   For up to P^32, results are returned in 4 ZMM registers
;;   in the following order:
;;       OUT0[P7P8,     P5P6,   P3P4,   P1P2]
;;       OUT1[P15P16, P13P14, P11P12,  P9P10]
;;       OUT2[P23P24, P21P22, P19P20, P17P18]
;;       OUT3[P31P32, P29P30, P27P28, P25P26]
;; Powers are also reorganized and stored on the stack for processing final <32 blocks
%macro  PRECOMPUTE_CONSTANTS 8-11
%define %%P1            %1  ;; [in] initial P^1 to be multiplied
%define %%HIGHEST_POWER %2  ;; [in] highest power to calculate (4 or 32)
%define %%OUT0          %3  ;; [out] ymm/zmm containing results
%define %%OUT1          %4  ;; [out] ymm/zmm containing results
%define %%T1            %5  ;; [clobbered] xmm
%define %%T2            %6  ;; [clobbered] xmm
%define %%T3            %7  ;; [clobbered] xmm
%define %%T4            %8  ;; [clobbered] xmm
%define %%T5            %9  ;; [clobbered] xmm
%define %%OUT2          %10 ;; [out] zmm containing results
%define %%OUT3          %11 ;; [out] zmm containing results

%if %0 > 8
%xdefine %%Y_OUT0 YWORD(%%OUT0)
%xdefine %%Y_OUT1 YWORD(%%OUT1)
%xdefine %%YT1 YWORD(%%T1)
%xdefine %%YT2 YWORD(%%T2)
%xdefine %%YT3 YWORD(%%T3)
%xdefine %%YT4 YWORD(%%T4)
%xdefine %%YT5 YWORD(%%T5)

%xdefine %%Z_OUT0 ZWORD(%%OUT0)
%xdefine %%Z_OUT1 ZWORD(%%OUT1)
%xdefine %%Z_OUT2 ZWORD(%%OUT2)
%xdefine %%Z_OUT3 ZWORD(%%OUT3)
%xdefine %%ZT1 ZWORD(%%T1)
%xdefine %%ZT2 ZWORD(%%T2)
%xdefine %%ZT3 ZWORD(%%T3)
%xdefine %%ZT4 ZWORD(%%T4)
%xdefine %%ZT5 ZWORD(%%T5)
%endif

        vmovdqa         %%T1, %%P1
        MUL_AND_REDUCE_64x64_LOW %%T1, %%P1, %%T4       ;; %%T1 = P2
        vmovdqa         %%T2, %%T1
        MUL_AND_REDUCE_64x64_LOW %%T2, %%P1, %%T4       ;; %%T2 = P3
        vmovq           %%T2, %%T2
        vmovdqa         %%T3, %%T2
        MUL_AND_REDUCE_64x64_LOW %%T3, %%P1, %%T4       ;; %%T3 = P4

%if %%HIGHEST_POWER <= 4
        ;; if highest power is 4 then put
        ;; P1P2 in OUT0 and P3P4 OUT1 and finish
        vpunpcklqdq     %%OUT0, %%P1, %%T1              ;; P1P2
        vpunpcklqdq     %%OUT1, %%T2, %%T3              ;; P3P4
%else
        ;; otherwise arrange powers later
        vpunpcklqdq     %%OUT0, %%P1, %%T1              ;; P1P2
        vpunpcklqdq     %%T1, %%T2, %%T3                ;; P3P4
        vinserti64x2    %%Y_OUT0, %%Y_OUT0, %%T1, 0x1   ;; P1P2P3P4
        vpermq          %%YT1, %%Y_OUT0, 0xff           ;; broadcast P4 across T1

        ;;   P1 P2 P3 P4
        ;; * P4 P4 P4 P4
        ;; ------------------
        ;;   P5 P6 P7 P8
        MUL_AND_REDUCE_64x64 %%YT1, %%Y_OUT0, %%YT2, %%YT3, %%YT4, %%YT5, YWORD(SNOW3G_CONST)

        ;; T1 contains P5-P8
        ;; insert P5P6P7P8 into high 256bits of OUT0
        vinserti64x4    %%Z_OUT0, %%Z_OUT0, %%YT1, 0x1

        ;; broadcast P8 across OUT1 and multiply
        valignq         %%YT1, %%YT1, %%YT1, 3
        vpbroadcastq    %%Z_OUT1, %%T1

        ;;   P1 P2 P3 P4 P5 P6 P7 P8
        ;; * P8 P8 P8 P8 P8 P8 P8 P8
        ;; -------------------------
        ;;   P9P10P11P12P13P14P15P16
        MUL_AND_REDUCE_64x64 %%Z_OUT1, %%Z_OUT0, %%ZT2, %%ZT3, %%ZT4, %%ZT5, ZWORD(SNOW3G_CONST)

        ;; broadcast P16 across OUT2 and multiply
        valignq         %%ZT1, %%Z_OUT1, %%Z_OUT1, 7
        vpbroadcastq    %%Z_OUT2, %%T1

        ;;   P1   P2  P3  P4  P5  P6  P7  P8
        ;; * P16 P16 P16 P16 P16 P16 P16 P16
        ;; -------------------------
        ;;   P17 P18 P19 P20 P21 P22 P23 P24
        MUL_AND_REDUCE_64x64 %%Z_OUT2, %%Z_OUT0, %%ZT2, %%ZT3, %%ZT4, %%ZT5, ZWORD(SNOW3G_CONST)

        ;; broadcast P24 across OUT3 and multiply
        valignq         %%ZT1, %%Z_OUT2, %%Z_OUT2, 7
        vpbroadcastq    %%Z_OUT3, %%T1

        ;;   P1   P2  P3  P4  P5  P6  P7  P8
        ;; * P24 P24 P24 P24 P24 P24 P24 P24
        ;; -------------------------
        ;;   P25 P26 P27 P28 P29 P30 P31 P32
        MUL_AND_REDUCE_64x64 %%Z_OUT3, %%Z_OUT0, %%ZT2, %%ZT3, %%ZT4, %%ZT5, ZWORD(SNOW3G_CONST)

        ;; put the highest powers to the lower lanes
        vshufi64x2      %%Z_OUT0, %%Z_OUT0, %%Z_OUT0, 00_01_10_11b ;;  P7P8P,   P5P6,   P3P4, P1P2
        vshufi64x2      %%Z_OUT1, %%Z_OUT1, %%Z_OUT1, 00_01_10_11b ;; P15P16, P13P14, P11P12, P9P10
        vshufi64x2      %%Z_OUT2, %%Z_OUT2, %%Z_OUT2, 00_01_10_11b ;; P23P24, P21P22, P19P20, P17P18
        vshufi64x2      %%Z_OUT3, %%Z_OUT3, %%Z_OUT3, 00_01_10_11b ;; P31P32, P29P30, P27P28, P25P26

        ;; store powers on stack in sequential order
        ;; (e.g. P32, P31 ... P2, P1) for processing final blocks
        vmovdqa64       %%ZT5, [rel swap_qwords]
        vpshufb         %%ZT1, %%Z_OUT0, %%ZT5
        vpshufb         %%ZT2, %%Z_OUT1, %%ZT5
        vpshufb         %%ZT3, %%Z_OUT2, %%ZT5
        vpshufb         %%ZT4, %%Z_OUT3, %%ZT5

        vmovdqu64       [rsp + CONSTANTS_OFFSET + 0*64], %%ZT4
        vmovdqu64       [rsp + CONSTANTS_OFFSET + 1*64], %%ZT3
        vmovdqu64       [rsp + CONSTANTS_OFFSET + 2*64], %%ZT2
        vmovdqu64       [rsp + CONSTANTS_OFFSET + 3*64], %%ZT1

%endif
%endmacro

;; Process final 1 - 31 blocks
%macro  PROCESS_FINAL_BLOCKS 21
%define %%MAX_BLOCKS       %1  ;; [in] max possible number of final blocks
%define %%IN               %2  ;; [in] gp reg with input pointer
%define %%NUM_BLOCKS       %3  ;; [in] gp reg with number remaining full blocks
%define %%MASK             %4  ;; [in] final 1-8 blocks mask
%define %%CONST_TAB_OFFSET %5  ;; [in] gp reg with constants table offset
%define %%DATA0            %6  ;; [clobbered] zmm to store message data
%define %%DATA1            %7  ;; [clobbered] zmm to store message data
%define %%DATA2            %8  ;; [clobbered] zmm to store message data
%define %%DATA3            %9  ;; [clobbered] zmm to store message data
%define %%TMP0             %10 ;; [clobbered] zmm
%define %%TMP1             %11 ;; [clobbered] zmm
%define %%TMP2             %12 ;; [clobbered] zmm
%define %%TMP3             %13 ;; [clobbered] zmm
%define %%TMP4             %14 ;; [clobbered] zmm
%define %%TMP5             %15 ;; [clobbered] zmm
%define %%TMP6             %16 ;; [clobbered] zmm
%define %%TMP7             %17 ;; [clobbered] zmm
%define %%TMP8             %18 ;; [clobbered] zmm
%define %%TMP9             %19 ;; [clobbered] zmm
%define %%TMP10            %20 ;; [clobbered] zmm
%define %%TMP11            %21 ;; [clobbered] zmm

%ifidn %%MAX_BLOCKS, 31
        ;; up to 31 blocks
        ZMM_LOAD_MASKED_BLOCKS_0_16 16, %%IN, 0, %%DATA0, %%DATA1, %%DATA2, %%DATA3, %%MASK
        ZMM_LOAD_MASKED_BLOCKS_0_16 16, rsp, %%CONST_TAB_OFFSET*8 + CONSTANTS_OFFSET, \
                                    %%TMP0, %%TMP1, %%TMP2, %%TMP3, %%MASK
%endif

%ifidn %%MAX_BLOCKS, 24
        ;; up to 24 blocks
        ZMM_LOAD_MASKED_BLOCKS_0_16 12, %%IN, 0, %%DATA0, %%DATA1, %%DATA2, %%DATA3, %%MASK
        ZMM_LOAD_MASKED_BLOCKS_0_16 12, rsp, %%CONST_TAB_OFFSET*8 + CONSTANTS_OFFSET, \
                                    %%TMP0, %%TMP1, %%TMP2, %%TMP3, %%MASK
        ;; zero unused blocks
        vpxorq          %%DATA3, %%DATA3, %%DATA3
        vpxorq          %%TMP3, %%TMP3, %%TMP3
%endif

%ifidn %%MAX_BLOCKS, 16
        ;; up to 16 blocks
        ZMM_LOAD_MASKED_BLOCKS_0_16 8, %%IN, 0, %%DATA0, %%DATA1, %%DATA2, %%DATA3, %%MASK
        ZMM_LOAD_MASKED_BLOCKS_0_16 8, rsp, %%CONST_TAB_OFFSET*8 + CONSTANTS_OFFSET, \
                                    %%TMP0, %%TMP1, %%TMP2, %%TMP3, %%MASK
        ;; zero unused blocks
        vpxorq          %%DATA2, %%DATA2, %%DATA2
        vpxorq          %%DATA3, %%DATA3, %%DATA3
        vpxorq          %%TMP2, %%TMP2, %%TMP2
        vpxorq          %%TMP3, %%TMP3, %%TMP3
%endif

%ifidn %%MAX_BLOCKS, 8
        ;; 1 to 8 blocks
        ZMM_LOAD_MASKED_BLOCKS_0_16 4, %%IN, 0, %%DATA0, %%DATA1, %%DATA2, %%DATA3, %%MASK
        ZMM_LOAD_MASKED_BLOCKS_0_16 4, rsp, %%CONST_TAB_OFFSET*8 + CONSTANTS_OFFSET, \
                                    %%TMP0, %%TMP1, %%TMP2, %%TMP3, %%MASK
        ;; zero unused blocks
        vpxorq          %%DATA1, %%DATA1, %%DATA1
        vpxorq          %%DATA2, %%DATA2, %%DATA2
        vpxorq          %%DATA3, %%DATA3, %%DATA3

        vpxorq          %%TMP1, %%TMP1, %%TMP1
        vpxorq          %%TMP2, %%TMP2, %%TMP2
        vpxorq          %%TMP3, %%TMP3, %%TMP3
%endif

        ;; reverse block byte order
        vpshufb         %%DATA0, %%DATA0, BSWAP64_MASK
        vpshufb         %%DATA1, %%DATA1, BSWAP64_MASK
        vpshufb         %%DATA2, %%DATA2, BSWAP64_MASK
        vpshufb         %%DATA3, %%DATA3, BSWAP64_MASK

        ;; add EV
        vpxorq          %%DATA0, %%DATA0, ZWORD(EV)

        ;; perform multiplication
        vpclmulqdq      %%TMP4, %%DATA0, %%TMP0, 0x00
        vpclmulqdq      %%TMP5, %%DATA0, %%TMP0, 0x11

        vpclmulqdq      %%TMP6, %%DATA1, %%TMP1, 0x00
        vpclmulqdq      %%TMP7, %%DATA1, %%TMP1, 0x11

        vpclmulqdq      %%TMP8, %%DATA2, %%TMP2, 0x00
        vpclmulqdq      %%TMP9, %%DATA2, %%TMP2, 0x11

        vpclmulqdq      %%TMP10, %%DATA3, %%TMP3, 0x00
        vpclmulqdq      %%TMP11, %%DATA3, %%TMP3, 0x11

        ;; sum results
        vpternlogq      %%TMP6, %%TMP4, %%TMP5, 0x96
        vpternlogq      %%TMP7, %%TMP8, %%TMP9, 0x96
        vpternlogq      %%TMP6, %%TMP10, %%TMP11, 0x96
        vpxorq          ZWORD(EV), %%TMP6, %%TMP7

        VHPXORI4x128    ZWORD(EV), %%TMP0
        REDUCE_TO_64    EV, XWORD(%%TMP0)
        vmovq XWORD(EV), XWORD(EV)

        ;; update input pointer
        shl     %%NUM_BLOCKS, 3
        add     %%IN, %%NUM_BLOCKS

%endmacro

;; uint32_t
;; snow3g_f9_1_buffer_internal_vaes_avx512(const uint64_t *pBufferIn,
;;                                         const uint32_t KS[5],
;;                                         const uint64_t lengthInBits);
align 64
MKGLOBAL(snow3g_f9_1_buffer_internal_vaes_avx512,function,internal)
snow3g_f9_1_buffer_internal_vaes_avx512:
        endbranch64

        FUNC_SAVE

        vmovdqa64       ZWORD(SNOW3G_CONST), [rel snow3g_constant]
        vmovdqa64       BSWAP64_MASK, [rel bswap64]
        mov             tmp, 10101010b
        kmovq           INSERT_HIGH64_MASK, tmp

        vpxor           EV, EV

        ;; P1 = ((uint64_t)KS[0] << 32) | ((uint64_t)KS[1])
        vmovq   P1, [KS]
        vpshufd P1, P1, 1110_0001b

        mov     qword_len, bit_len              ;; lenInBits -> lenInQwords
        shr     qword_len, 6

        jz      full_blocks_complete

        ;; precompute up to P^32
        PRECOMPUTE_CONSTANTS P1, 32, xmm0, xmm1, xmm3, xmm4, xmm5, xmm6, xmm9, xmm20, xmm21

        cmp     qword_len, 32                   ;; <32 blocks go to final blocks
        jb      lt32_blocks

start_32_block_loop:
        vmovdqu64       zmm3, [in_ptr]
        vmovdqu64       zmm4, [in_ptr + 64]
        vmovdqu64       zmm23, [in_ptr + 128]
        vmovdqu64       zmm24, [in_ptr + 192]

        vpshufb         zmm3, zmm3, BSWAP64_MASK
        vpshufb         zmm4, zmm4, BSWAP64_MASK
        vpshufb         zmm23, zmm23, BSWAP64_MASK
        vpshufb         zmm24, zmm24, BSWAP64_MASK

        vpxorq          zmm3, zmm3, ZWORD(EV)

        vpclmulqdq      zmm5, zmm24, zmm0, 0x10  ;;   p8 -  p1
        vpclmulqdq      zmm6, zmm24, zmm0, 0x01  ;; x m24 - m31

        vpclmulqdq      zmm10, zmm23, zmm1, 0x10 ;;   p16 - p9
        vpclmulqdq      zmm11, zmm23, zmm1, 0x01 ;; x m16  - m23

        vpclmulqdq      zmm25, zmm4, zmm20, 0x10 ;;   p24 -  p17
        vpclmulqdq      zmm26, zmm4, zmm20, 0x01 ;; x m8 - m15

        vpclmulqdq      zmm30, zmm3, zmm21, 0x10 ;;   p32 - p25
        vpclmulqdq      zmm31, zmm3, zmm21, 0x01 ;; x m0  - m7

        ;; sum results
        vpternlogq      zmm10, zmm5, zmm6, 0x96
        vpternlogq      zmm11, zmm25, zmm26, 0x96
        vpternlogq      zmm10, zmm30, zmm31, 0x96
        vpxorq          ZWORD(EV), zmm10, zmm11
        VHPXORI4x128    ZWORD(EV), zmm4

        REDUCE_TO_64    EV, xmm3
        vmovq XWORD(EV), XWORD(EV)

        add     in_ptr, 32*8
        sub     qword_len, 32

        ;; check no more full blocks
        jz      full_blocks_complete

        ;; check less than 32 blocks left
        cmp     qword_len, 32
        jb      lt32_blocks

        jmp     start_32_block_loop

lt32_blocks:
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Less than 32 blocks remaining
        ;; Process between 1 and 31 final full blocks
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        ;; set mask to load final 1-8 blocks
        mov     tmp, qword_len
        and     tmp, 0x7
        lea     tmp2, [rel mask64]
        kmovq   k2, [tmp2 + tmp*8]

        ;; calculate offset to load constants from stack
        mov     tmp4, 32
        sub     tmp4, qword_len

        cmp     qword_len, 16
        jbe     le16_blocks

        cmp     qword_len, 24
        jbe     le24_blocks

gt24_blocks:
        ;; 25 - 31 blocks
        PROCESS_FINAL_BLOCKS 31, in_ptr, qword_len, k2, tmp4, zmm3, zmm4, zmm23, zmm24, zmm0, \
                             zmm1, zmm20, zmm21, zmm5, zmm6, zmm10, zmm11, zmm25, zmm26, zmm30, zmm31

        jmp     full_blocks_complete

le24_blocks:
        ;; 17 - 24 blocks
        PROCESS_FINAL_BLOCKS 24, in_ptr, qword_len, k2, tmp4, zmm3, zmm4, zmm23, zmm24, zmm0, \
                             zmm1, zmm20, zmm21, zmm5, zmm6, zmm10, zmm11, zmm25, zmm26, zmm30, zmm31
        jmp     full_blocks_complete

le16_blocks:
        cmp     qword_len, 8
        jbe     le8_blocks

gt8_blocks:
        ;; 9 - 16 blocks
        PROCESS_FINAL_BLOCKS 16, in_ptr, qword_len, k2, tmp4, zmm3, zmm4, zmm23, zmm24, zmm0, \
                             zmm1, zmm20, zmm21, zmm5, zmm6, zmm10, zmm11, zmm25, zmm26, zmm30, zmm31
        jmp     full_blocks_complete

le8_blocks:
        ;; 1 - 8 blocks
        PROCESS_FINAL_BLOCKS 8, in_ptr, qword_len, k2, tmp4, zmm3, zmm4, zmm23, zmm24, zmm0, \
                             zmm1, zmm20, zmm21, zmm5, zmm6, zmm10, zmm11, zmm25, zmm26, zmm30, zmm31

        jmp     full_blocks_complete

full_blocks_complete:

        mov     tmp5, 0x3f      ;; len_in_bits % 64
        and     tmp5, bit_len
        jz      skip_rem_bits

        ;; load last N bytes
        mov     tmp2, tmp5      ;; (rem_bits + 7) / 8
        add     tmp2, 7
        shr     tmp2, 3

        simd_load_avx_15_1 xmm3, in_ptr, tmp2
        vmovq   tmp3, xmm3
        bswap   tmp3

        mov     tmp, 0xffffffffffffffff
        mov     tmp6, 64
        sub     tmp6, tmp5

        SHIFT_GP tmp, tmp6, tmp, tmp5, left

        and     tmp3, tmp       ;; V &= (((uint64_t)-1) << (64 - rem_bits)); /* mask extra bits */
        vmovq   xmm0, tmp3
        vpxor   EV, xmm0

        MUL_AND_REDUCE_64x64_LOW EV, P1, xmm3

skip_rem_bits:
        ;; /* Multiply by Q */
        ;; E = multiply_and_reduce64(E ^ lengthInBits,
        ;;                           (((uint64_t)z[2] << 32) | ((uint64_t)z[3])));
        ;; /* Final MAC */
        ;; *(uint32_t *)pDigest =
        ;;        (uint32_t)BSWAP64(E ^ ((uint64_t)z[4] << 32));
        vmovq   xmm3, bit_len
        vpxor   EV, xmm3

        vmovq   xmm1, [KS + 8]                  ;; load z[2:3]
        vpshufd xmm1, xmm1, 1110_0001b

        mov     DWORD(tmp4), [KS + (4 * 4)]     ;; tmp4 == z[4] << 32
        shl     tmp4, 32

        MUL_AND_REDUCE_64x64_LOW EV, xmm1, xmm3
        vmovq   E, EV
        xor     E, tmp4

        bswap   E                               ;; return E (rax/eax)

        FUNC_RESTORE

        ret

mksection stack-noexec

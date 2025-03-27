;;
;; Copyright (c) 2020-2021, Intel Corporation
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
%use smartalign

%include "include/imb_job.asm"
%include "include/reg_sizes.asm"
%include "include/os.asm"
%include "include/clear_regs.asm"
%include "include/aes_common.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/cet.inc"
default rel

extern ethernet_fcs_avx512_local

;; In System V AMD64 ABI
;;	callee saves: RBX, RBP, R12-R15
;; Windows x64 ABI
;;	callee saves: RBX, RBP, RDI, RSI, RSP, R12-R15

%define CONCAT(a,b) a %+ b

struc STACKFRAME
_rsp_save:      resq    1
_job_save:      resq    1
_gpr_save:	resq	4
endstruc

%ifdef LINUX
%define arg1	rdi
%define arg2	rsi
%define arg3	rdx
%else
%define arg1	rcx
%define arg2	rdx
%define arg3	r8
%endif

%define job     arg1

%define tmp1	rbx
%define tmp2	rbp
%define tmp3	r10
%define tmp4	r11
%define tmp5	r12
%define tmp6	r13
%define tmp7	r8
%define tmp8	r9

mksection .rodata

;;; Precomputed constants for CRC32 (Ethernet FCS)
;;;   Details of the CRC algorithm and 4 byte buffer of
;;;   {0x01, 0x02, 0x03, 0x04}:
;;;     Result     Poly       Init        RefIn  RefOut  XorOut
;;;     0xB63CFBCD 0x04C11DB7 0xFFFFFFFF  true   true    0xFFFFFFFF

align 16
rk5:
        dq 0x00000000ccaa009e, 0x0000000163cd6124
rk7:
        dq 0x00000001f7011640, 0x00000001db710640

align 16

fold_by_16: ;; fold by 16x128-bits
        dq 0x00000000e95c1271, 0x00000000ce3371cb
fold_by_8: ;; fold by 8x128-bits
        dq 0x000000014a7fe880, 0x00000001e88ef372
fold_by_4: ;; fold by 4x128-bits
        dq 0x00000001c6e41596, 0x0000000154442bd4
fold_by_2: ;; fold by 2x128-bits
        dq 0x000000015a546366, 0x00000000f1da05aa
fold_by_1: ;; fold by 1x128-bits
        dq 0x00000000ccaa009e, 0x00000001751997d0

align 16
pshufb_shf_table:
        ;;  use these values for shift registers with the pshufb instruction
        dq 0x8786858483828100, 0x8f8e8d8c8b8a8988
        dq 0x0706050403020100, 0x000e0d0c0b0a0908

align 16
init_crc_value:
        dq 0x00000000FFFFFFFF, 0x0000000000000000

align 16
mask:
        dq 0xFFFFFFFFFFFFFFFF, 0x0000000000000000

align 16
mask2:
        dq 0xFFFFFFFF00000000, 0xFFFFFFFFFFFFFFFF
align 16
mask3:
        dq 0x8080808080808080, 0x8080808080808080

align 16
mask_out_top_bytes:
        dq 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF
        dq 0x0000000000000000, 0x0000000000000000

;;; partial block read/write table
align 64
byte_len_to_mask_table:
        dw      0x0000, 0x0001, 0x0003, 0x0007,
        dw      0x000f, 0x001f, 0x003f, 0x007f,
        dw      0x00ff, 0x01ff, 0x03ff, 0x07ff,
        dw      0x0fff, 0x1fff, 0x3fff, 0x7fff,
        dw      0xffff

mksection .text

;; ===================================================================
;; ===================================================================
;; CRC multiply before XOR against data block
;; ===================================================================
%macro CRC_CLMUL 4
%define %%XCRC_IN_OUT   %1 ; [in/out] XMM with CRC (can be anything if "no_crc" below)
%define %%XCRC_MUL      %2 ; [in] XMM with CRC constant  (can be anything if "no_crc" below)
%define %%XCRC_DATA     %3 ; [in] XMM with data block
%define %%XTMP          %4 ; [clobbered] temporary XMM

        vpclmulqdq      %%XTMP, %%XCRC_IN_OUT, %%XCRC_MUL, 0x01
        vpclmulqdq      %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%XCRC_MUL, 0x10
        vpternlogq      %%XCRC_IN_OUT, %%XTMP, %%XCRC_DATA, 0x96 ; XCRC = XCRC ^ XTMP ^ DATA
%endmacro

;; ===================================================================
;; ===================================================================
;; CRC32 calculation on 16 byte data
;; ===================================================================
%macro CRC_UPDATE16 6
%define %%INP           %1  ; [in/out] GP with input text pointer or "no_load"
%define %%XCRC_IN_OUT   %2  ; [in/out] XMM with CRC (can be anything if "no_crc" below)
%define %%XCRC_MUL      %3  ; [in] XMM with CRC multiplier constant
%define %%TXMM1         %4  ; [clobbered|in] XMM temporary or data in (no_load)
%define %%TXMM2         %5  ; [clobbered] XMM temporary
%define %%CRC_TYPE      %6  ; [in] "first_crc" or "next_crc" or "no_crc"

        ;; load data and increment in pointer
%ifnidn %%INP, no_load
        vmovdqu64       %%TXMM1, [%%INP]
        add             %%INP,  16
%endif

        ;; CRC calculation
%ifidn %%CRC_TYPE, next_crc
        CRC_CLMUL %%XCRC_IN_OUT, %%XCRC_MUL, %%TXMM1, %%TXMM2
%endif
%ifidn %%CRC_TYPE, first_crc
        ;; in the first run just XOR initial CRC with the first block
        vpxorq          %%XCRC_IN_OUT, %%TXMM1
%endif

%endmacro

;; ===================================================================
;; ===================================================================
;; Barrett reduction from 128-bits to 32-bits modulo Ethernet FCS polynomial
;; ===================================================================
%macro CRC32_REDUCE_128_TO_32 5
%define %%CRC   %1         ; [out] GP to store 32-bit Ethernet FCS value
%define %%XCRC  %2         ; [in/clobbered] XMM with CRC
%define %%XT1   %3         ; [clobbered] temporary xmm register
%define %%XT2   %4         ; [clobbered] temporary xmm register
%define %%XT3   %5         ; [clobbered] temporary xmm register

%define %%XCRCKEY %%XT3

        ;;  compute crc of a 128-bit value
        vmovdqa64       %%XCRCKEY, [rel rk5]

        ;; 64b fold
        vpclmulqdq      %%XT1, %%XCRC, %%XCRCKEY, 0x00
        vpsrldq         %%XCRC, %%XCRC, 8
        vpxorq          %%XCRC, %%XCRC, %%XT1

        ;; 32b fold
        vpslldq         %%XT1, %%XCRC, 4
        vpclmulqdq      %%XT1, %%XT1, %%XCRCKEY, 0x10
        vpxorq          %%XCRC, %%XCRC, %%XT1

%%_crc_barrett:
        ;; Barrett reduction
        vpandq          %%XCRC, [rel mask2]
        vmovdqa64       %%XT1, %%XCRC
        vmovdqa64       %%XT2, %%XCRC
        vmovdqa64       %%XCRCKEY, [rel rk7]

        vpclmulqdq      %%XCRC, %%XCRCKEY, 0x00
        vpxorq          %%XCRC, %%XT2
        vpandq          %%XCRC, [rel mask]
        vmovdqa64       %%XT2, %%XCRC
        vpclmulqdq      %%XCRC, %%XCRCKEY, 0x10
        vpternlogq      %%XCRC, %%XT2, %%XT1, 0x96 ; XCRC = XCRC ^ XT2 ^ XT1
        vpextrd         DWORD(%%CRC), %%XCRC, 2 ; 32-bit CRC value
        not             DWORD(%%CRC)
%endmacro

;; ===================================================================
;; ===================================================================
;; Barrett reduction from 64-bits to 32-bits modulo Ethernet FCS polynomial
;; ===================================================================
%macro CRC32_REDUCE_64_TO_32 5
%define %%CRC   %1         ; [out] GP to store 32-bit Ethernet FCS value
%define %%XCRC  %2         ; [in/clobbered] XMM with CRC
%define %%XT1   %3         ; [clobbered] temporary xmm register
%define %%XT2   %4         ; [clobbered] temporary xmm register
%define %%XT3   %5         ; [clobbered] temporary xmm register

%define %%XCRCKEY %%XT3

        ;; Barrett reduction
        vpandq          %%XCRC, [rel mask2]
        vmovdqa64       %%XT1, %%XCRC
        vmovdqa64       %%XT2, %%XCRC
        vmovdqa64       %%XCRCKEY, [rel rk7]

        vpclmulqdq      %%XCRC, %%XCRCKEY, 0x00
        vpxorq          %%XCRC, %%XT2
        vpandq          %%XCRC, [rel mask]
        vmovdqa64       %%XT2, %%XCRC
        vpclmulqdq      %%XCRC, %%XCRCKEY, 0x10
        vpternlogq      %%XCRC, %%XT2, %%XT1, 0x96 ; XCRC = XCRC ^ XT2 ^ XT1
        vpextrd         DWORD(%%CRC), %%XCRC, 2 ; 32-bit CRC value
        not             DWORD(%%CRC)
%endmacro

;; ===================================================================
;; ===================================================================
;; ETHERNET FCS CRC
;; ===================================================================
%macro ETHERNET_FCS_CRC 9
%define %%p_in          %1  ; [in] pointer to the buffer (GPR)
%define %%bytes_to_crc  %2  ; [in] number of bytes in the buffer (GPR)
%define %%ethernet_fcs  %3  ; [out] GPR to put CRC value into (32 bits)
%define %%xcrc          %4  ; [in] initial CRC value (xmm)
%define %%tmp           %5  ; [clobbered] temporary GPR
%define %%xcrckey       %6  ; [clobbered] temporary XMM / CRC multiplier
%define %%xtmp1         %7  ; [clobbered] temporary XMM
%define %%xtmp2         %8  ; [clobbered] temporary XMM
%define %%xtmp3         %9  ; [clobbered] temporary XMM

        ;; load CRC constants
        vmovdqa64       %%xcrckey, [rel fold_by_1]

        cmp             %%bytes_to_crc, 32
        jae             %%_at_least_32_bytes

        ;; less than 32 bytes
        cmp             %%bytes_to_crc, 16
        je              %%_exact_16_left
        jl              %%_less_than_16_left

        ;; load the plain-text
        vmovdqu64       %%xtmp1, [%%p_in]
        vpxorq          %%xcrc, %%xtmp1   ; xor the initial crc value
        add             %%p_in, 16
        sub             %%bytes_to_crc, 16
        jmp             %%_crc_two_xmms

%%_exact_16_left:
        vmovdqu64       %%xtmp1, [%%p_in]
        vpxorq          %%xcrc, %%xtmp1 ; xor the initial CRC value
        jmp             %%_128_done

%%_less_than_16_left:
        lea             %%tmp, [rel byte_len_to_mask_table]
        kmovw           k1, [%%tmp + %%bytes_to_crc*2]
        vmovdqu8        %%xtmp1{k1}{z}, [%%p_in]

        vpxorq          %%xcrc, %%xtmp1 ; xor the initial CRC value

        cmp             %%bytes_to_crc, 4
        jb              %%_less_than_4_left

        lea             %%tmp, [rel pshufb_shf_table]
        vmovdqu64       %%xtmp1, [%%tmp + %%bytes_to_crc]
        vpshufb         %%xcrc, %%xtmp1
        jmp             %%_128_done

%%_less_than_4_left:
        ;; less than 4 bytes left
        cmp             %%bytes_to_crc, 3
        jne             %%_less_than_3_left
        vpslldq         %%xcrc, 5
        jmp             %%_do_barret

%%_less_than_3_left:
        cmp             %%bytes_to_crc, 2
        jne             %%_less_than_2_left
        vpslldq         %%xcrc, 6
        jmp             %%_do_barret

%%_less_than_2_left:
        vpslldq         %%xcrc, 7

%%_do_barret:
        CRC32_REDUCE_64_TO_32 %%ethernet_fcs, %%xcrc, %%xtmp1, %%xtmp2, %%xcrckey
        jmp             %%_64_done

%%_at_least_32_bytes:
        CRC_UPDATE16 %%p_in, %%xcrc, %%xcrckey, %%xtmp1, %%xtmp2, first_crc
        sub             %%bytes_to_crc, 16

%%_main_loop:
        cmp             %%bytes_to_crc, 16
        jb              %%_exit_loop
        CRC_UPDATE16 %%p_in, %%xcrc, %%xcrckey, %%xtmp1, %%xtmp2, next_crc
        sub             %%bytes_to_crc, 16
        jz              %%_128_done
        jmp             %%_main_loop

%%_exit_loop:

        ;; Partial bytes left - complete CRC calculation
%%_crc_two_xmms:
        lea             %%tmp, [rel pshufb_shf_table]
        vmovdqu64       %%xtmp2, [%%tmp + %%bytes_to_crc]
        vmovdqu64       %%xtmp1, [%%p_in - 16 + %%bytes_to_crc]  ; xtmp1 = data for CRC
        vmovdqa64       %%xtmp3, %%xcrc
        vpshufb         %%xcrc, %%xtmp2  ; top num_bytes with LSB xcrc
        vpxorq          %%xtmp2, [rel mask3]
        vpshufb         %%xtmp3, %%xtmp2 ; bottom (16 - num_bytes) with MSB xcrc

        ;; data num_bytes (top) blended with MSB bytes of CRC (bottom)
        vpblendvb       %%xtmp3, %%xtmp1, %%xtmp2

        ;; final CRC calculation
        CRC_CLMUL %%xcrc, %%xcrckey, %%xtmp3, %%xtmp1

%%_128_done:
        CRC32_REDUCE_128_TO_32 %%ethernet_fcs, %%xcrc, %%xtmp1, %%xtmp2, %%xcrckey
%%_64_done:
%endmacro

;; ===================================================================
;; ===================================================================
;; AES128/256 CBC decryption on 1 to 16 blocks
;; ===================================================================
%macro AES_CBC_DEC_1_TO_16 17
%define %%SRC           %1  ; [in] GP with pointer to source buffer
%define %%DST           %2  ; [in] GP with pointer to destination buffer
%define %%NUMBL         %3  ; [in] numerical constant with number of blocks to process
%define %%OFFS          %4  ; [in/out] GP with src/dst buffer offset
%define %%NBYTES        %5  ; [in/out] GP with number of bytes to decrypt
%define %%KEY_PTR       %6  ; [in] GP with pointer to expanded AES decrypt keys
%define %%ZIV           %7  ; [in/out] IV in / last cipher text block on out (xmm0 - xmm15)
%define %%NROUNDS       %8  ; [in] number of rounds; numerical value
%define %%CIPHER_00_03  %9  ; [out] ZMM next 0-3 cipher blocks
%define %%CIPHER_04_07  %10 ; [out] ZMM next 4-7 cipher blocks
%define %%CIPHER_08_11  %11 ; [out] ZMM next 8-11 cipher blocks
%define %%CIPHER_12_15  %12 ; [out] ZMM next 12-15 cipher blocks
%define %%ZT1           %13 ; [clobbered] ZMM temporary
%define %%ZT2           %14 ; [clobbered] ZMM temporary
%define %%ZT3           %15 ; [clobbered] ZMM temporary
%define %%ZT4           %16 ; [clobbered] ZMM temporary
%define %%ZT5           %17 ; [clobbered] ZMM temporary

        ;; /////////////////////////////////////////////////
        ;; load cipher text
        ZMM_LOAD_BLOCKS_0_16 %%NUMBL, %%SRC, %%OFFS, \
                %%CIPHER_00_03, %%CIPHER_04_07, \
                %%CIPHER_08_11, %%CIPHER_12_15

        ;; /////////////////////////////////////////////////
        ;; prepare cipher text blocks for an XOR after AES-DEC rounds
        valignq         %%ZT1, %%CIPHER_00_03, %%ZIV, 6
%if %%NUMBL > 4
        valignq         %%ZT2, %%CIPHER_04_07, %%CIPHER_00_03, 6
%endif
%if %%NUMBL > 8
        valignq         %%ZT3, %%CIPHER_08_11, %%CIPHER_04_07, 6
%endif
%if %%NUMBL > 12
        valignq         %%ZT4, %%CIPHER_12_15, %%CIPHER_08_11, 6
%endif

        ;; /////////////////////////////////////////////////
        ;; update IV with the last cipher block
%if %%NUMBL < 4
        valignq         %%ZIV, %%CIPHER_00_03, %%CIPHER_00_03, ((%%NUMBL % 4) * 2)
%elif %%NUMBL == 4
        vmovdqa64       %%ZIV, %%CIPHER_00_03
%elif %%NUMBL < 8
        valignq         %%ZIV, %%CIPHER_04_07, %%CIPHER_04_07, ((%%NUMBL % 4) * 2)
%elif %%NUMBL == 8
        vmovdqa64       %%ZIV, %%CIPHER_04_07
%elif %%NUMBL < 12
        valignq         %%ZIV, %%CIPHER_08_11, %%CIPHER_08_11, ((%%NUMBL % 4) * 2)
%elif %%NUMBL == 12
        vmovdqa64       %%ZIV, %%CIPHER_08_11
%elif %%NUMBL < 16
        valignq         %%ZIV, %%CIPHER_12_15, %%CIPHER_12_15, ((%%NUMBL % 4) * 2)
%else ;; %%NUMBL == 16
        vmovdqa64       %%ZIV, %%CIPHER_12_15
%endif

        ;; /////////////////////////////////////////////////
        ;; AES rounds including XOR
%assign j 0
%rep (%%NROUNDS + 2)
     vbroadcastf64x2    %%ZT5, [%%KEY_PTR + (j * 16)]
     ZMM_AESDEC_ROUND_BLOCKS_0_16 %%CIPHER_00_03, %%CIPHER_04_07, \
                        %%CIPHER_08_11, %%CIPHER_12_15, \
                        %%ZT5, j, %%ZT1, %%ZT2, %%ZT3, %%ZT4, \
                        %%NUMBL, %%NROUNDS
%assign j (j + 1)
%endrep

        ;; /////////////////////////////////////////////////
        ;; write plain text back to output
        ZMM_STORE_BLOCKS_0_16 %%NUMBL, %%DST, %%OFFS, \
                %%CIPHER_00_03, %%CIPHER_04_07, \
                %%CIPHER_08_11, %%CIPHER_12_15

        ;; /////////////////////////////////////////////////
        ;; update lengths and offset
        add             %%OFFS, (%%NUMBL * 16)
        sub             %%NBYTES, (%%NUMBL * 16)
%endmacro       ;; AES_CBC_DEC_1_TO_16

;; ===================================================================
;; ===================================================================
;; CRC32 on 1 to 16 blocks (first_crc case only)
;; ===================================================================
%macro CRC32_FIRST_1_TO_16 13
%define %%CRC_MUL    %1  ; [in] XMM with CRC multiplier
%define %%CRC_IN_OUT %2  ; [in/out] current CRC value
%define %%XTMP       %3  ; [clobbered] temporary XMM
%define %%XTMP2      %4  ; [clobbered] temporary XMM
%define %%NUMBL      %5  ; [in] number of blocks of clear text to compute CRC on
%define %%ZCRCIN0    %6  ; [in] clear text 4 blocks
%define %%ZCRCIN1    %7  ; [in] clear text 4 blocks
%define %%ZCRCIN2    %8  ; [in] clear text 4 blocks
%define %%ZCRCIN3    %9  ; [in] clear text 4 blocks
%define %%ZCRCSUM0   %10 ; [clobbered] temporary ZMM
%define %%ZCRCSUM1   %11 ; [clobbered] temporary ZMM
%define %%ZCRCSUM2   %12 ; [clobbered] temporary ZMM
%define %%ZCRCSUM3   %13 ; [clobbered] temporary ZMM

%xdefine %%ZTMP0 ZWORD(%%XTMP)
%xdefine %%ZTMP1 ZWORD(%%XTMP2)

%if (%%NUMBL == 0)
        ;; do nothing
%elif (%%NUMBL == 1)
        vpxorq          %%CRC_IN_OUT, XWORD(%%ZCRCIN0)
%elif (%%NUMBL == 16)
        vmovdqa64       %%ZCRCSUM0, %%ZCRCIN0
        vmovdqa64       %%ZCRCSUM1, %%ZCRCIN1
        vmovdqa64       %%ZCRCSUM2, %%ZCRCIN2
        vmovdqa64       %%ZCRCSUM3, %%ZCRCIN3

        ;; Add current CRC sum into block 0
        vmovdqa64       %%CRC_IN_OUT, %%CRC_IN_OUT
        vpxorq          %%ZCRCSUM0, %%ZCRCSUM0, ZWORD(%%CRC_IN_OUT)
        ;; fold 16 x 128 bits -> 8 x 128 bits
        vbroadcastf64x2 %%ZTMP0, [rel fold_by_8]
        vpclmulqdq      %%ZTMP1, %%ZCRCSUM0, %%ZTMP0, 0x01
        vpclmulqdq      %%ZCRCSUM0, %%ZCRCSUM0, %%ZTMP0, 0x10
        vpternlogq      %%ZCRCSUM0, %%ZCRCSUM2, %%ZTMP1, 0x96

        vpclmulqdq      %%ZTMP1, %%ZCRCSUM1, %%ZTMP0, 0x01
        vpclmulqdq      %%ZCRCSUM1, %%ZCRCSUM1, %%ZTMP0, 0x10
        vpternlogq      %%ZCRCSUM1, %%ZCRCSUM3, %%ZTMP1, 0x96

        ;; fold 8 x 128 bits -> 4 x 128 bits
        vbroadcastf64x2 %%ZTMP0, [rel fold_by_4]
        vpclmulqdq      %%ZTMP1, %%ZCRCSUM0, %%ZTMP0, 0x01
        vpclmulqdq      %%ZCRCSUM0, %%ZCRCSUM0, %%ZTMP0, 0x10
        vpternlogq      %%ZCRCSUM0, %%ZCRCSUM1, %%ZTMP1, 0x96

        ;; fold 4 x 128 bits -> 2 x 128 bits
        vbroadcastf64x2 YWORD(%%ZTMP0), [rel fold_by_2]
        vextracti64x4   YWORD(%%ZCRCSUM1), %%ZCRCSUM0, 1
        vpclmulqdq      YWORD(%%ZTMP1), YWORD(%%ZCRCSUM0), YWORD(%%ZTMP0), 0x01
        vpclmulqdq      YWORD(%%ZCRCSUM0), YWORD(%%ZCRCSUM0), YWORD(%%ZTMP0), 0x10
        vpternlogq      YWORD(%%ZCRCSUM0), YWORD(%%ZCRCSUM1), YWORD(%%ZTMP1), 0x96

        ;; fold 2 x 128 bits -> 1 x 128 bits
        vmovdqa64       XWORD(%%ZTMP0), [rel fold_by_1]
        vextracti64x2   XWORD(%%ZCRCSUM1), YWORD(%%ZCRCSUM0), 1
        vpclmulqdq      XWORD(%%ZTMP1), XWORD(%%ZCRCSUM0), XWORD(%%ZTMP0), 0x01
        vpclmulqdq      XWORD(%%ZCRCSUM0), XWORD(%%ZCRCSUM0), XWORD(%%ZTMP0), 0x10
        vpternlogq      XWORD(%%ZCRCSUM0), XWORD(%%ZCRCSUM1), XWORD(%%ZTMP1), 0x96
        vmovdqa64       %%CRC_IN_OUT, XWORD(%%ZCRCSUM0)

%else

        vpxorq          %%ZCRCSUM0, %%ZCRCSUM0
        vpxorq          %%ZCRCSUM1, %%ZCRCSUM1
        vpxorq          %%ZCRCSUM2, %%ZCRCSUM2
        vpxorq          %%ZCRCSUM3, %%ZCRCSUM3

        vmovdqa64       %%ZCRCSUM0, %%ZCRCIN0
%if %%NUMBL > 4
        vmovdqa64       %%ZCRCSUM1, %%ZCRCIN1
%endif
%if %%NUMBL > 8
        vmovdqa64       %%ZCRCSUM2, %%ZCRCIN2
%endif
%if %%NUMBL > 12
        vmovdqa64       %%ZCRCSUM3, %%ZCRCIN3
%endif

        ;; Add current CRC sum into block 0
        vmovdqa64       %%CRC_IN_OUT, %%CRC_IN_OUT
        vpxorq          %%ZCRCSUM0, %%ZCRCSUM0, ZWORD(%%CRC_IN_OUT)

%assign blocks_left %%NUMBL

%if (%%NUMBL >= 12)
        vbroadcastf64x2 %%ZTMP0, [rel fold_by_4]
        vpclmulqdq      %%ZTMP1, %%ZCRCSUM0, %%ZTMP0, 0x01
        vpclmulqdq      %%ZCRCSUM0, %%ZCRCSUM0, %%ZTMP0, 0x10
        vpternlogq      %%ZCRCSUM0, %%ZCRCSUM1, %%ZTMP1, 0x96

        vpclmulqdq      %%ZTMP1, %%ZCRCSUM0, %%ZTMP0, 0x01
        vpclmulqdq      %%ZCRCSUM0, %%ZCRCSUM0, %%ZTMP0, 0x10
        vpternlogq      %%ZCRCSUM0, %%ZCRCSUM2, %%ZTMP1, 0x96
        vmovdqa64       %%ZCRCSUM1, %%ZCRCSUM3

%assign blocks_left (blocks_left - 8)

%elif (%%NUMBL >= 8)
        vbroadcastf64x2 %%ZTMP0, [rel fold_by_4]
        vpclmulqdq      %%ZTMP1, %%ZCRCSUM0, %%ZTMP0, 0x01
        vpclmulqdq      %%ZCRCSUM0, %%ZCRCSUM0, %%ZTMP0, 0x10
        vpternlogq      %%ZCRCSUM0, %%ZCRCSUM1, %%ZTMP1, 0x96
        vmovdqa64       %%ZCRCSUM1, %%ZCRCSUM2

%assign blocks_left (blocks_left - 4)
%endif

        ;; 1 to 8 blocks left in ZCRCSUM0 and ZCRCSUM1

%if blocks_left >= 4
        ;; fold 4 x 128 bits -> 2 x 128 bits
        vbroadcastf64x2 YWORD(%%ZTMP0), [rel fold_by_2]
        vextracti64x4   YWORD(%%ZCRCSUM3), %%ZCRCSUM0, 1
        vpclmulqdq      YWORD(%%ZTMP1), YWORD(%%ZCRCSUM0), YWORD(%%ZTMP0), 0x01
        vpclmulqdq      YWORD(%%ZCRCSUM0), YWORD(%%ZCRCSUM0), YWORD(%%ZTMP0), 0x10
        vpternlogq      YWORD(%%ZCRCSUM0), YWORD(%%ZCRCSUM3), YWORD(%%ZTMP1), 0x96

        ;; fold 2 x 128 bits -> 1 x 128 bits
        vmovdqa64       XWORD(%%ZTMP0), [rel fold_by_1]
        vextracti64x2   XWORD(%%ZCRCSUM3), YWORD(%%ZCRCSUM0), 1
        vpclmulqdq      XWORD(%%ZTMP1), XWORD(%%ZCRCSUM0), XWORD(%%ZTMP0), 0x01
        vpclmulqdq      XWORD(%%ZCRCSUM0), XWORD(%%ZCRCSUM0), XWORD(%%ZTMP0), 0x10
        vpternlogq      XWORD(%%ZCRCSUM0), XWORD(%%ZCRCSUM3), XWORD(%%ZTMP1), 0x96

        vmovdqa64       %%CRC_IN_OUT, XWORD(%%ZCRCSUM0)

        vmovdqa64       %%ZCRCSUM0, %%ZCRCSUM1

%assign blocks_left (blocks_left - 4)

%else
        vmovdqa64       %%CRC_IN_OUT, XWORD(%%ZCRCSUM0)
        vshufi64x2      %%ZCRCSUM0, %%ZCRCSUM0, %%ZCRCSUM0, 0011_1001b

%assign blocks_left (blocks_left - 1)
%endif

%rep blocks_left
        vmovdqa64       %%XTMP, XWORD(%%ZCRCSUM0)
        CRC_CLMUL       %%CRC_IN_OUT, %%CRC_MUL, %%XTMP, %%XTMP2
        vshufi64x2      %%ZCRCSUM0, %%ZCRCSUM0, %%ZCRCSUM0, 0011_1001b
%endrep

%endif  ;; %%NUMBL > 0

%endmacro       ;; CRC32_FIRST_1_TO_16

;; ===================================================================
;; ===================================================================
;; Stitched AES128/256 CBC decryption & CRC32 on 16 blocks
;; ===================================================================
%macro AES_CBC_DEC_CRC32_16 22
%define %%SRC        %1  ; [in] GP with pointer to source buffer
%define %%DST        %2  ; [in] GP with pointer to destination buffer
%define %%OFFS       %3  ; [in/out] GP with src/dst buffer offset
%define %%NBYTES     %4  ; [in/out] GP with number of bytes to decrypt
%define %%KEY_PTR    %5  ; [in] GP with pointer to expanded AES decrypt keys
%define %%ZIV        %6  ; [in/out] IV in / last cipher text block on out
%define %%ZD0        %7  ; [clobbered] temporary ZMM
%define %%ZD1        %8  ; [clobbered] temporary ZMM
%define %%ZD2        %9  ; [clobbered] temporary ZMM
%define %%ZD3        %10 ; [clobbered] temporary ZMM
%define %%ZC0        %11 ; [clobbered] temporary ZMM
%define %%ZC1        %12 ; [clobbered] temporary ZMM
%define %%ZC2        %13 ; [clobbered] temporary ZMM
%define %%ZC3        %14 ; [clobbered] temporary ZMM
%define %%ZTMP0      %15 ; [clobbered] temporary ZMM
%define %%ZTMP1      %16 ; [clobbered] temporary ZMM
%define %%NROUNDS    %17 ; [in] Number of rounds (9 or 13)
%define %%ZCRC_SUM0  %18 ; [in/out] current CRC value
%define %%ZCRC_SUM1  %19 ; [in/out] current CRC value
%define %%ZCRC_SUM2  %20 ; [in/out] current CRC value
%define %%ZCRC_SUM3  %21 ; [in/out] current CRC value
%define %%LAST_BLOCK %22 ; [out] xmm to store the last clear text block

        ;; /////////////////////////////////////////////////
        ;; load cipher text blocks
        ZMM_LOAD_BLOCKS_0_16 16, %%SRC, %%OFFS, \
                %%ZC0, %%ZC1, %%ZC2, %%ZC3

        ;; /////////////////////////////////////////////////
        ;; prepare cipher text blocks for an XOR after AES-DEC rounds
        valignq         %%ZD0, %%ZC0, %%ZIV, 6
        valignq         %%ZD1, %%ZC1, %%ZC0, 6
        valignq         %%ZD2, %%ZC2, %%ZC1, 6
        valignq         %%ZD3, %%ZC3, %%ZC2, 6

        ;; /////////////////////////////////////////////////
        ;; update IV for the next round (block 3 in ZIV)
        vmovdqa64       %%ZIV, %%ZC3

        ;; /////////////////////////////////////////////////
        ;; AES rounds 0 to 10/14 & CRC

%assign round 0
%rep (%%NROUNDS + 2)
        ;; /////////////////////////////////////////////////
        ;; AES decrypt round
        vbroadcastf64x2 %%ZTMP0, [%%KEY_PTR + (round*16)]
        ZMM_AESDEC_ROUND_BLOCKS_0_16 %%ZC0, %%ZC1, %%ZC2, %%ZC3, \
                        %%ZTMP0, round, %%ZD0, %%ZD1, %%ZD2, %%ZD3, \
                        16, %%NROUNDS
%assign round (round + 1)
%endrep

        ;; /////////////////////////////////////////////////
        ;; store clear text
        ZMM_STORE_BLOCKS_0_16 16, %%DST, %%OFFS, \
                %%ZC0, %%ZC1, %%ZC2, %%ZC3

                ;; \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\
                ;; CRC just decrypted blocks
                vbroadcastf64x2 %%ZTMP0, [rel fold_by_16]
	        vpclmulqdq	%%ZTMP1, %%ZCRC_SUM0, %%ZTMP0, 0x10
	        vpclmulqdq	%%ZCRC_SUM0, %%ZCRC_SUM0, %%ZTMP0, 0x01
                vpternlogq      %%ZCRC_SUM0, %%ZTMP1, %%ZC0, 0x96

                vpclmulqdq	%%ZTMP1, %%ZCRC_SUM1, %%ZTMP0, 0x10
	        vpclmulqdq	%%ZCRC_SUM1, %%ZCRC_SUM1, %%ZTMP0, 0x01
                vpternlogq      %%ZCRC_SUM1, %%ZTMP1, %%ZC1, 0x96

	        vpclmulqdq	%%ZTMP1, %%ZCRC_SUM2, %%ZTMP0, 0x10
	        vpclmulqdq	%%ZCRC_SUM2, %%ZCRC_SUM2, %%ZTMP0, 0x01
                vpternlogq      %%ZCRC_SUM2, %%ZTMP1, %%ZC2, 0x96

                vpclmulqdq	%%ZTMP1, %%ZCRC_SUM3, %%ZTMP0, 0x10
	        vpclmulqdq	%%ZCRC_SUM3, %%ZCRC_SUM3, %%ZTMP0, 0x01
                vpternlogq      %%ZCRC_SUM3, %%ZTMP1, %%ZC3, 0x96

                vextracti64x2   %%LAST_BLOCK, %%ZC3, 3

        ;; /////////////////////////////////////////////////
        ;; update lengths and offset
        add             %%OFFS, (16 * 16)
        sub             %%NBYTES, (16 * 16)

%endmacro       ;; AES_CBC_DEC_CRC32_16

;; ===================================================================
;; ===================================================================
;; DOCSIS SEC BPI decryption + CRC32
;; This macro is handling the case when the two components are
;; executed together.
;; ===================================================================
%macro DOCSIS_DEC_CRC32 40
%define %%KEYS       %1   ;; [in] GP with pointer to expanded keys (decrypt)
%define %%SRC        %2   ;; [in] GP with pointer to source buffer
%define %%DST        %3   ;; [in] GP with pointer to destination buffer
%define %%NUM_BYTES  %4   ;; [in/clobbered] GP with number of bytes to decrypt
%define %%KEYS_ENC   %5   ;; [in] GP with pointer to expanded keys (encrypt)
%define %%GT1        %6   ;; [clobbered] temporary GP
%define %%GT2        %7   ;; [clobbered] temporary GP
%define %%XCRC_INIT  %8   ;; [in/out] CRC initial value
%define %%XIV        %9   ;; [in/out] cipher IV
%define %%ZT1        %10  ;; [clobbered] temporary ZMM
%define %%ZT2        %11  ;; [clobbered] temporary ZMM
%define %%ZT3        %12  ;; [clobbered] temporary ZMM
%define %%ZT4        %13  ;; [clobbered] temporary ZMM
%define %%ZT5        %14  ;; [clobbered] temporary ZMM
%define %%ZT6        %15  ;; [clobbered] temporary ZMM
%define %%ZT7        %16  ;; [clobbered] temporary ZMM
%define %%ZT8        %17  ;; [clobbered] temporary ZMM
%define %%ZT9        %18  ;; [clobbered] temporary ZMM
%define %%ZT10       %19  ;; [clobbered] temporary ZMM
%define %%ZT11       %20  ;; [clobbered] temporary ZMM
%define %%ZT12       %21  ;; [clobbered] temporary ZMM
%define %%ZT13       %22  ;; [clobbered] temporary ZMM
                          ;; no ZT14 - taken by XIV
                          ;; no ZT15 - taken by CRC_INIT
%define %%ZT16       %23  ;; [clobbered] temporary ZMM
%define %%ZT17       %24  ;; [clobbered] temporary ZMM
%define %%ZT18       %25  ;; [clobbered] temporary ZMM
%define %%ZT19       %26  ;; [clobbered] temporary ZMM
%define %%ZT20       %27  ;; [clobbered] temporary ZMM
%define %%ZT21       %28  ;; [clobbered] temporary ZMM
%define %%ZT22       %29  ;; [clobbered] temporary ZMM
%define %%ZT23       %30  ;; [clobbered] temporary ZMM
%define %%ZT24       %31  ;; [clobbered] temporary ZMM
%define %%ZT25       %32  ;; [clobbered] temporary ZMM
%define %%ZT26       %33  ;; [clobbered] temporary ZMM
%define %%ZT27       %34  ;; [clobbered] temporary ZMM
%define %%ZT28       %35  ;; [clobbered] temporary ZMM
%define %%ZT29       %36  ;; [clobbered] temporary ZMM
%define %%ZT30       %37  ;; [clobbered] temporary ZMM
%define %%ZT31       %38  ;; [clobbered] temporary ZMM
%define %%ZT32       %39  ;; [clobbered] temporary ZMM
%define %%NROUNDS    %40  ;; [in] Number of rounds (9 or 13)

%define %%NUM_BLOCKS %%GT1
%define %%OFFSET     %%GT2

%xdefine %%ZIV ZWORD(%%XIV)

%xdefine %%XTMP0  XWORD(%%ZT1)
%xdefine %%XTMP1  XWORD(%%ZT2)

%xdefine %%XCRC_TMP    XWORD(%%ZT3)
%xdefine %%XCRC_MUL    XWORD(%%ZT4)
%xdefine %%XCRC_IN_OUT %%XCRC_INIT

%xdefine %%ZCRC0 %%ZT5
%xdefine %%ZCRC1 %%ZT6
%xdefine %%ZCRC2 %%ZT7
%xdefine %%ZCRC3 %%ZT8
%xdefine %%XCRC0 XWORD(%%ZCRC0)

%xdefine %%ZCIPH0 %%ZT9
%xdefine %%ZCIPH1 %%ZT10
%xdefine %%ZCIPH2 %%ZT11
%xdefine %%ZCIPH3 %%ZT12

%xdefine %%ZTMP0 %%ZT20
%xdefine %%ZTMP1 %%ZT21
%xdefine %%ZTMP2 %%ZT22
%xdefine %%ZTMP3 %%ZT23
%xdefine %%ZTMP4 %%ZT24
%xdefine %%ZTMP5 %%ZT25
%xdefine %%ZTMP6 %%ZT26
%xdefine %%ZTMP7 %%ZT27
%xdefine %%ZTMP8 %%ZT28
%xdefine %%ZTMP9 %%ZT29

%xdefine %%ZCRC_IN_OUT0   ZWORD(%%XCRC_IN_OUT)
%xdefine %%ZCRC_IN_OUT1   %%ZT30
%xdefine %%ZCRC_IN_OUT2   %%ZT31
%xdefine %%ZCRC_IN_OUT3   %%ZT32

        vmovdqa64       %%XCRC_MUL, [rel fold_by_1]
        vmovdqa64       %%XCRC_INIT, %%XCRC_INIT

        xor     %%OFFSET, %%OFFSET

        cmp     %%NUM_BYTES, 16
        jb      %%_check_partial_block

        cmp     %%NUM_BYTES, (16 * 16) + 16
        jb      %%_below_17_blocks

        cmp     %%NUM_BYTES, (32 * 16) + 16
        jb      %%_below_33_blocks

        ;; =====================================================================
        ;; =====================================================================
        ;; Part handling messages bigger-equal 33 blocks
        ;; - decrypt & crc performed per 16 block basis
        ;; =====================================================================

        ;; Decrypt 16 blocks first.
        ;; Make sure IV is in the top 128 bits of ZMM.
        vshufi64x2      %%ZIV, %%ZIV, %%ZIV, 0000_0000b

        AES_CBC_DEC_1_TO_16     %%SRC, %%DST, 16, %%OFFSET, %%NUM_BYTES, \
                                %%KEYS, %%ZIV, %%NROUNDS, \
                                %%ZTMP0, %%ZCRC_IN_OUT1, \
                                %%ZCRC_IN_OUT2, %%ZCRC_IN_OUT3, \
                                %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5

        ;; Start of CRC is just reading the data and adding initial value.
        ;; In the next round multiply and add operations will apply.
        vpxorq          %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT0, %%ZTMP0

        vextracti64x2   %%XCRC0, %%ZCRC_IN_OUT3, 3

%%_main_loop:
        cmp     %%NUM_BYTES, (16 * 16) + 16
        jb      %%_main_loop_exit

        ;; Stitched cipher and CRC on 16 blocks
        AES_CBC_DEC_CRC32_16    %%SRC, %%DST, %%OFFSET, %%NUM_BYTES, \
                                %%KEYS, %%ZIV, \
                                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, \
                                %%ZTMP5, %%ZTMP6, %%ZTMP7, %%ZTMP8, %%ZTMP9, \
                                %%NROUNDS, \
                                %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT1, \
                                %%ZCRC_IN_OUT2, %%ZCRC_IN_OUT3, \
                                %%XCRC0

        jmp     %%_main_loop

%%_main_loop_exit:
        ;; Up to 16 (inclusive) blocks left to process
        ;; - decrypt the blocks first
        ;; - then crc decrypted blocks minus one block

        ;; broadcast IV across ZMM (4th and 1st 128-bit positions are only important really)
        vshufi64x2      %%ZIV, %%ZIV, %%ZIV, 1111_1111b

        mov     %%NUM_BLOCKS, %%NUM_BYTES
        shr     %%NUM_BLOCKS, 4
        and     %%NUM_BLOCKS, 15
        jz	%%_decrypt_eq0

        cmp     %%NUM_BLOCKS, 8
        jg      %%_decrypt_gt8
        je      %%_decrypt_eq8

        ;; 1 to 7 blocks
	cmp	%%NUM_BLOCKS, 4
	jg	%%_decrypt_gt4
	je	%%_decrypt_eq4

%%_decrypt_lt4:
        ;; 1 to 3 blocks
	cmp	%%NUM_BLOCKS, 2
	jg	%%_decrypt_eq3
	je	%%_decrypt_eq2
        jmp     %%_decrypt_eq1

%%_decrypt_gt4:
        ;; 5 to 7
	cmp	%%NUM_BLOCKS, 6
	jg	%%_decrypt_eq7
	je	%%_decrypt_eq6
        jmp     %%_decrypt_eq5

%%_decrypt_gt8:
        ;; 9 to 15
	cmp	%%NUM_BLOCKS, 12
	jg	%%_decrypt_gt12
	je	%%_decrypt_eq12

        ;; 9 to 11
	cmp	%%NUM_BLOCKS, 10
	jg	%%_decrypt_eq11
	je	%%_decrypt_eq10
        jmp     %%_decrypt_eq9

%%_decrypt_gt12:
        ;; 13 to 15
	cmp	%%NUM_BLOCKS, 14
	jg	%%_decrypt_eq15
	je	%%_decrypt_eq14
        jmp     %%_decrypt_eq13

%assign number_of_blocks 1
%rep 15
%%_decrypt_eq %+ number_of_blocks :
        ;; decrypt selected number of blocks
        AES_CBC_DEC_1_TO_16 %%SRC, %%DST, number_of_blocks, %%OFFSET, %%NUM_BYTES, \
                        %%KEYS, %%ZIV, %%NROUNDS, \
                        %%ZTMP6, %%ZTMP7, %%ZTMP8, %%ZTMP9, \
                        %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5

        ;; extract & save the last decrypted block as crc for it is done separately
        ;; towards the end of this macro
%if number_of_blocks < 5
        vextracti64x2   %%XCRC0, %%ZTMP6, (number_of_blocks - 1)
%elif  number_of_blocks < 9
        vextracti64x2   %%XCRC0, %%ZTMP7, (number_of_blocks - 4 - 1)
%elif  number_of_blocks < 13
        vextracti64x2   %%XCRC0, %%ZTMP8, (number_of_blocks - 8 - 1)
%else
        vextracti64x2   %%XCRC0, %%ZTMP9, (number_of_blocks - 12 - 1)
%endif

        ;; set number of blocks for CRC
        mov             %%NUM_BLOCKS, (number_of_blocks - 1)

        ;; extract latest IV into XIV for partial block processing
        vextracti32x4   %%XIV, %%ZIV, 3
        jmp             %%_decrypt_done_fold_by8

%assign number_of_blocks (number_of_blocks + 1)
%endrep

%%_decrypt_eq0:
        ;; Special case. Check if there are full 16 blocks for decrypt
        ;; - it can happen here because the main loop checks for 17 blocks
        ;; If yes then decrypt them and fall through to folding/crc section
        ;; identifying 15 blocks for CRC
        cmp             %%NUM_BYTES, (16 * 16)
        jb              %%_cbc_decrypt_done

        AES_CBC_DEC_1_TO_16 %%SRC, %%DST, 16, %%OFFSET, %%NUM_BYTES, \
                        %%KEYS, %%ZIV, %%NROUNDS, \
                        %%ZTMP6, %%ZTMP7, %%ZTMP8, %%ZTMP9, \
                        %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5

        mov             %%NUM_BLOCKS, 15
        vextracti32x4   %%XIV, %%ZIV, 3
        vextracti64x2   %%XCRC0, %%ZTMP9, 3

%%_decrypt_done_fold_by8:
        ;; Register content at this point:
        ;; ZTMP6 - ZTMP9 => decrypted blocks (16 to 31)
        ;; ZCRC_IN_OUT0 - ZCRC_IN_OUT3 - fold by 16 CRC sums
        ;; NUM_BLOCKS - number of blocks to CRC

        ;; fold 16 x 128 bits -> 8 x 128 bits
        vbroadcastf64x2 %%ZTMP2, [rel fold_by_8]
        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT0, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT0, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT2, %%ZTMP1, 0x96

        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT1, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT1, %%ZCRC_IN_OUT1, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT1, %%ZCRC_IN_OUT3, %%ZTMP1, 0x96

%%_decrypt_done_no_fold_16_to_8:
        ;; CRC 8 blocks of already decrypted text
        test            %%NUM_BLOCKS, 8
        jz              %%_skip_crc_by8

        vbroadcastf64x2 %%ZTMP2, [rel fold_by_8]
        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT0, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT0, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT0, %%ZTMP6, %%ZTMP1, 0x96

        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT1, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT1, %%ZCRC_IN_OUT1, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT1, %%ZTMP7, %%ZTMP1, 0x96

        vmovdqa64       %%ZTMP6, %%ZTMP8
        vmovdqa64       %%ZTMP7, %%ZTMP9

%%_skip_crc_by8:
        ;; fold 8 x 128 bits -> 4 x 128 bits
        vbroadcastf64x2 %%ZTMP2, [rel fold_by_4]
        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT0, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT0, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT1, %%ZTMP1, 0x96

        ;; CRC 4 blocks of already decrypted text
        test            %%NUM_BLOCKS, 4
        jz              %%_skip_crc_by4

        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT0, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT0, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT0, %%ZTMP6, %%ZTMP1, 0x96

        vmovdqa64       %%ZTMP6, %%ZTMP7

%%_skip_crc_by4:
        ;; fold 4 x 128 bits -> 2 x 128 bits
        vbroadcastf64x2 YWORD(%%ZTMP2), [rel fold_by_2]
        vextracti64x4   YWORD(%%ZCRC_IN_OUT1), %%ZCRC_IN_OUT0, 1
        vpclmulqdq      YWORD(%%ZTMP1), YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZTMP2), 0x01
        vpclmulqdq      YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZTMP2), 0x10
        vpternlogq      YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZCRC_IN_OUT1), YWORD(%%ZTMP1), 0x96

        ;; CRC 2 blocks of already decrypted text
        test            %%NUM_BLOCKS, 2
        jz              %%_skip_crc_by2

        vpclmulqdq      YWORD(%%ZTMP1), YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZTMP2), 0x01
        vpclmulqdq      YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZTMP2), 0x10
        vpternlogq      YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZTMP6), YWORD(%%ZTMP1), 0x96

        vshufi64x2      %%ZTMP6, %%ZTMP6, %%ZTMP6, 1110_1110b

%%_skip_crc_by2:
        ;; fold 2 x 128 bits -> 1 x 128 bits
        vmovdqa64       XWORD(%%ZTMP2), [rel fold_by_1]
        vextracti64x2   XWORD(%%ZCRC_IN_OUT1), YWORD(%%ZCRC_IN_OUT0), 1
        vpclmulqdq      XWORD(%%ZTMP1), XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZTMP2), 0x01
        vpclmulqdq      XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZTMP2), 0x10
        vpternlogq      XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZCRC_IN_OUT1), XWORD(%%ZTMP1), 0x96

        ;; CRC 1 block of already decrypted text
        test            %%NUM_BLOCKS, 1
        jz              %%_skip_crc_by1

        vpclmulqdq      XWORD(%%ZTMP1), XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZTMP2), 0x01
        vpclmulqdq      XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZTMP2), 0x10
        vpternlogq      XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZTMP6), XWORD(%%ZTMP1), 0x96

%%_skip_crc_by1:
        jmp             %%_check_partial_block

%%_cbc_decrypt_done:
        ;; No blocks left to compute CRC for. Just fold the sums from 16x128-bits into 1x128-bits.
        ;; Register content at this point:
        ;; ZCRC_IN_OUT0 - ZCRC_IN_OUT3 - fold by 16 CRC sums
        ;; XCRC0 - includes the last decrypted block to be passed to partial check case

        ;; fold 16 x 128 bits -> 8 x 128 bits
        vbroadcastf64x2 %%ZTMP2, [rel fold_by_8]
        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT0, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT0, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT2, %%ZTMP1, 0x96

        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT1, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT1, %%ZCRC_IN_OUT1, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT1, %%ZCRC_IN_OUT3, %%ZTMP1, 0x96

%%_cbc_decrypt_done_fold_8_to_4:
        ;; fold 8 x 128 bits -> 4 x 128 bits
        vbroadcastf64x2 %%ZTMP2, [rel fold_by_4]
        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT0, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT0, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT1, %%ZTMP1, 0x96

        ;; fold 4 x 128 bits -> 2 x 128 bits
        vbroadcastf64x2 YWORD(%%ZTMP2), [rel fold_by_2]
        vextracti64x4   YWORD(%%ZCRC_IN_OUT1), %%ZCRC_IN_OUT0, 1
        vpclmulqdq      YWORD(%%ZTMP1), YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZTMP2), 0x01
        vpclmulqdq      YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZTMP2), 0x10
        vpternlogq      YWORD(%%ZCRC_IN_OUT0), YWORD(%%ZCRC_IN_OUT1), YWORD(%%ZTMP1), 0x96

        ;; fold 2 x 128 bits -> 1 x 128 bits
        vmovdqa64       XWORD(%%ZTMP2), [rel fold_by_1]
        vextracti64x2   XWORD(%%ZCRC_IN_OUT1), YWORD(%%ZCRC_IN_OUT0), 1
        vpclmulqdq      XWORD(%%ZTMP1), XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZTMP2), 0x01
        vpclmulqdq      XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZTMP2), 0x10
        vpternlogq      XWORD(%%ZCRC_IN_OUT0), XWORD(%%ZCRC_IN_OUT1), XWORD(%%ZTMP1), 0x96

        ;; - keep the last block out from the calculation
        ;;   (this may be a partial block - additional checks follow)
        jmp             %%_check_partial_block

        ;; =====================================================================
        ;; =====================================================================
        ;; Part handling messages from 16 - 32 blocks
        ;; =====================================================================
%%_below_33_blocks:
        ;; Decrypt 16 blocks first
        ;; Make sure IV is in the top 128 bits of ZMM.
        vshufi64x2      %%ZIV, %%ZIV, %%ZIV, 0000_0000b

        AES_CBC_DEC_1_TO_16     %%SRC, %%DST, 16, %%OFFSET, %%NUM_BYTES, \
                                %%KEYS, %%ZIV, %%NROUNDS, \
                                %%ZTMP0, %%ZCRC_IN_OUT1, \
                                %%ZCRC_IN_OUT2, %%ZCRC_IN_OUT3, \
                                %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5

        ;; Start of CRC is just reading the data and adding initial value.
        vpxorq          %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT0, %%ZTMP0

        ;; Use fold by 8 approach to start the CRC.
        ;; ZCRC_IN_OUT0 and ZCRC_IN_OUT1 include CRC sums.
        vbroadcastf64x2 %%ZTMP2, [rel fold_by_8]
        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT0, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT0, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT0, %%ZCRC_IN_OUT2, %%ZTMP1, 0x96

        vpclmulqdq      %%ZTMP1, %%ZCRC_IN_OUT1, %%ZTMP2, 0x01
        vpclmulqdq      %%ZCRC_IN_OUT1, %%ZCRC_IN_OUT1, %%ZTMP2, 0x10
        vpternlogq      %%ZCRC_IN_OUT1, %%ZCRC_IN_OUT3, %%ZTMP1, 0x96

        ;; Decrypt rest of the message.
        mov     %%NUM_BLOCKS, %%NUM_BYTES
        shr     %%NUM_BLOCKS, 4
        and     %%NUM_BLOCKS, 15
        jz	%%_decrypt2_eq0

        cmp     %%NUM_BLOCKS, 8
        jg      %%_decrypt2_gt8
        je      %%_decrypt2_eq8

        ;; 1 to 7 blocks
	cmp	%%NUM_BLOCKS, 4
	jg	%%_decrypt2_gt4
	je	%%_decrypt2_eq4

%%_decrypt2_lt4:
        ;; 1 to 3 blocks
	cmp	%%NUM_BLOCKS, 2
	jg	%%_decrypt2_eq3
	je	%%_decrypt2_eq2
        jmp     %%_decrypt2_eq1

%%_decrypt2_gt4:
        ;; 5 to 7
	cmp	%%NUM_BLOCKS, 6
	jg	%%_decrypt2_eq7
	je	%%_decrypt2_eq6
        jmp     %%_decrypt2_eq5

%%_decrypt2_gt8:
        ;; 9 to 15
	cmp	%%NUM_BLOCKS, 12
	jg	%%_decrypt2_gt12
	je	%%_decrypt2_eq12

        ;; 9 to 11
	cmp	%%NUM_BLOCKS, 10
	jg	%%_decrypt2_eq11
	je	%%_decrypt2_eq10
        jmp     %%_decrypt2_eq9

%%_decrypt2_gt12:
        ;; 13 to 15
	cmp	%%NUM_BLOCKS, 14
	jg	%%_decrypt2_eq15
	je	%%_decrypt2_eq14
        jmp     %%_decrypt2_eq13

%assign number_of_blocks 1
%rep 15
%%_decrypt2_eq %+ number_of_blocks :
        AES_CBC_DEC_1_TO_16 %%SRC, %%DST, number_of_blocks, %%OFFSET, %%NUM_BYTES, \
                        %%KEYS, %%ZIV, %%NROUNDS, \
                        %%ZTMP6, %%ZTMP7, %%ZTMP8, %%ZTMP9, \
                        %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5

%if number_of_blocks < 5
        vextracti64x2   %%XCRC0, %%ZTMP6, (number_of_blocks - 1)
%elif  number_of_blocks < 9
        vextracti64x2   %%XCRC0, %%ZTMP7, (number_of_blocks - 4 - 1)
%elif  number_of_blocks < 13
        vextracti64x2   %%XCRC0, %%ZTMP8, (number_of_blocks - 8 - 1)
%else
        vextracti64x2   %%XCRC0, %%ZTMP9, (number_of_blocks - 12 - 1)
%endif

        ;; Update XIV
        mov             %%NUM_BLOCKS, (number_of_blocks - 1)

        ;; Extract latest IV
        vextracti32x4   %%XIV, %%ZIV, 3
        jmp             %%_decrypt_done_no_fold_16_to_8

%assign number_of_blocks (number_of_blocks + 1)
%endrep

%%_decrypt2_eq0:
        ;; Special case. Check if there are full 16 blocks for decrypt.
        ;; If yes then decrypt them and fall through to folding/crc section
        ;; identifying 15 blocks for CRC
        cmp             %%NUM_BYTES, (16 * 16)
        jb              %%_cbc_decrypt_done_fold_8_to_4

        AES_CBC_DEC_1_TO_16 %%SRC, %%DST, 16, %%OFFSET, %%NUM_BYTES, \
                        %%KEYS, %%ZIV, %%NROUNDS, \
                        %%ZTMP6, %%ZTMP7, %%ZTMP8, %%ZTMP9, \
                        %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5

        mov             %%NUM_BLOCKS, 15
        vextracti32x4   %%XIV, %%ZIV, 3
        vextracti64x2   %%XCRC0, %%ZTMP9, 3
        jmp             %%_decrypt_done_no_fold_16_to_8

        ;; =====================================================================
        ;; =====================================================================
        ;; Part handling messages up to from 1 to 16 blocks
        ;; =====================================================================
%%_below_17_blocks:
        ;; Make sure IV is in the top 128 bits of ZMM.
        vshufi64x2      %%ZIV, %%ZIV, %%ZIV, 0000_0000b

        mov     %%NUM_BLOCKS, %%NUM_BYTES
        shr     %%NUM_BLOCKS, 4
        and     %%NUM_BLOCKS, 15
        jz	%%_eq16

        cmp     %%NUM_BLOCKS, 8
        jg      %%_gt8
        je      %%_eq8

        ;; 1 to 7 blocks
	cmp	%%NUM_BLOCKS, 4
	jg	%%_gt4
	je	%%_eq4

%%_lt4:
        ;; 1 to 3 blocks
	cmp	%%NUM_BLOCKS, 2
	jg	%%_eq3
	je	%%_eq2
        jmp     %%_eq1

%%_gt4:
        ;; 5 to 7
	cmp	%%NUM_BLOCKS, 6
	jg	%%_eq7
	je	%%_eq6
        jmp     %%_eq5

%%_gt8:
        ;; 9 to 15
	cmp	%%NUM_BLOCKS, 12
	jg	%%_gt12
	je	%%_eq12

        ;; 9 to 11
	cmp	%%NUM_BLOCKS, 10
	jg	%%_eq11
	je	%%_eq10
        jmp     %%_eq9

%%_gt12:
        ;; 13 to 15
	cmp	%%NUM_BLOCKS, 14
	jg	%%_eq15
	je	%%_eq14
        jmp     %%_eq13

%assign number_of_blocks 1
%rep 16
%%_eq %+ number_of_blocks :
        ;; Start building the pipeline by decrypting number of blocks
        ;; - later cipher & CRC operations get stitched
        AES_CBC_DEC_1_TO_16 %%SRC, %%DST, number_of_blocks, %%OFFSET, %%NUM_BYTES, \
                           %%KEYS, %%ZIV, %%NROUNDS, \
                           %%ZCRC0, %%ZCRC1, %%ZCRC2, %%ZCRC3, \
                           %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5

        vextracti32x4       %%XIV, %%ZIV, 3

        ;; Less than 16 blocks remaining in the message:
        ;; - compute CRC on decrypted blocks (minus one, in case it is the last one)
        ;; - then check for any partial block left
%assign number_of_blocks_to_crc (number_of_blocks - 1)
        CRC32_FIRST_1_TO_16     %%XCRC_MUL, %%XCRC_IN_OUT, %%XTMP0, %%XTMP1, \
                                number_of_blocks_to_crc, \
                                %%ZCRC0, %%ZCRC1, %%ZCRC2, %%ZCRC3, \
                                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3

%if number_of_blocks_to_crc == 0
%elif number_of_blocks_to_crc < 4
        vextracti32x4   %%XCRC0, %%ZCRC0, (number_of_blocks_to_crc % 4)
%elif number_of_blocks_to_crc < 8
        vextracti32x4   %%XCRC0, %%ZCRC1, (number_of_blocks_to_crc % 4)
%elif number_of_blocks_to_crc < 12
        vextracti32x4   %%XCRC0, %%ZCRC2, (number_of_blocks_to_crc % 4)
%else ;; number_of_blocks_to_crc < 16
        vextracti32x4   %%XCRC0, %%ZCRC3, (number_of_blocks_to_crc % 4)
%endif
        jmp     %%_check_partial_block

%assign number_of_blocks (number_of_blocks + 1)
%endrep

        ;; =====================================================================
        ;; =====================================================================
        ;; Part handling decrypt & CRC of partial block and
        ;; CRC of the second last block.
        ;; Register content at entry to this section:
        ;;     XCRC0 - last 16 bytes of clear text to compute crc on (optional)
        ;;     XCRC_IN_OUT - 128-bit crc fold product
        ;;     OFFSET - current offset
        ;;     NUM_BYTES - number of bytes left to decrypt
        ;;     XIV - IV for decrypt operation
        ;; =====================================================================
%%_check_partial_block:
        or              %%NUM_BYTES, %%NUM_BYTES
        jz              %%_no_partial_bytes

        ;; AES128/256-CFB on the partial block
        lea             %%GT1, [rel byte_len_to_mask_table]
        kmovw           k1, [%%GT1 + %%NUM_BYTES*2]
        vmovdqu8        %%XTMP1{k1}{z}, [%%SRC + %%OFFSET + 0]
        vpxorq          %%XTMP0, %%XIV, [%%KEYS_ENC + 0*16]
%assign i 1
%rep %%NROUNDS
        vaesenc         %%XTMP0, [%%KEYS_ENC + i*16]
%assign i (i + 1)
%endrep
        vaesenclast     %%XTMP0, [%%KEYS_ENC + i*16]
        vpxorq          %%XTMP0, %%XTMP0, %%XTMP1
        vmovdqu8        [%%DST + %%OFFSET + 0]{k1}, %%XTMP0

%%_no_partial_bytes:
        ;; At this stage:
        ;; - whole message is decrypted the focus moves to complete CRC
        ;;     - XCRC_IN_OUT includes folded data from all payload apart from
        ;;       the last full block and (potential) partial bytes
        ;;     - max 2 blocks (minus 1 byte) remain for CRC calculation
        ;; - %%OFFSET == 0 is used to check
        ;;   if message consists of partial block only
        or      %%OFFSET, %%OFFSET
        jz      %%_no_block_pending_crc

        ;; Data block(s) was previously decrypted
        ;; - move to the last decrypted block
        ;; - calculate number of bytes to compute CRC for (less CRC field size)
        add     %%NUM_BYTES, (16 - 4)
        sub     %%OFFSET, 16
        jz      %%_no_partial_bytes__start_crc

        cmp     %%NUM_BYTES, 16
        jb      %%_no_partial_bytes__lt16

        ;; XCRC0 has copy of the last full decrypted block
        CRC_UPDATE16   no_load, %%XCRC_IN_OUT, %%XCRC_MUL, %%XCRC0, %%XTMP1, next_crc

        sub     %%NUM_BYTES, 16
        add     %%OFFSET, 16    ; compensate for the subtract above

%%_no_partial_bytes__lt16:
        or              %%NUM_BYTES, %%NUM_BYTES
        jz              %%_no_partial_bytes__128_done

        ;; Partial bytes left - complete CRC calculation
        lea             %%GT1, [rel pshufb_shf_table]
        vmovdqu64       %%XTMP0, [%%GT1 + %%NUM_BYTES]
        lea             %%GT1, [%%DST + %%OFFSET]
        vmovdqu64       %%XTMP1, [%%GT1 - 16 + %%NUM_BYTES]  ; xtmp1 = data for CRC
        vmovdqa64       %%XCRC_TMP, %%XCRC_IN_OUT
        vpshufb         %%XCRC_IN_OUT, %%XTMP0  ; top num_bytes with LSB xcrc
        vpxorq          %%XTMP0, [rel mask3]
        vpshufb         %%XCRC_TMP, %%XTMP0 ; bottom (16 - num_bytes) with MSB xcrc

        ;; data num_bytes (top) blended with MSB bytes of CRC (bottom)
        vpblendvb       %%XCRC_TMP, %%XTMP1, %%XTMP0

        CRC_CLMUL %%XCRC_IN_OUT, %%XCRC_MUL, %%XCRC_TMP, %%XTMP1

%%_no_partial_bytes__128_done:
        CRC32_REDUCE_128_TO_32 rax, %%XCRC_IN_OUT, %%XTMP1, %%XTMP0, %%XCRC_TMP
        jmp     %%_do_return

%%_no_partial_bytes__start_crc:
        ;; - CRC was not started yet
        ;; - CBC decryption could have taken place and/or CFB
        ;; - DST is never modified so it points to start of the buffer that
        ;;   is subject of CRC calculation
        ETHERNET_FCS_CRC %%DST, %%NUM_BYTES, rax, %%XCRC_IN_OUT, %%GT1, \
                         %%XCRC_MUL, %%XTMP0, %%XTMP1, %%XCRC_TMP
        jmp     %%_do_return

%%_no_block_pending_crc:
        ;; Message consists of partial block only (first_crc not employed yet)
        ;; - XTMP0 includes clear text from CFB processing above
        ;; - k1 includes mask of bytes belonging to the message
        ;; - NUM_BYTES is length of cipher, CRC is 4 bytes shorter
        ;;     - ignoring hash lengths 1 to 4
        cmp             %%NUM_BYTES, 5
        jb              %%_do_return

        ;; clear top 4 bytes of the data
        kshiftrw        k1, k1, 4
        vmovdqu8        %%XTMP0{k1}{z}, %%XTMP0
        vpxorq          %%XCRC_IN_OUT, %%XTMP0 ; xor the data in

        sub             %%NUM_BYTES, 4

        ;; CRC calculation for payload lengths below 4 is different
        cmp             %%NUM_BYTES, 4
        jb              %%_no_block_pending_crc__lt4

        ;; 4 or more bytes left
        lea             %%GT1, [rel pshufb_shf_table]
        vmovdqu64       %%XTMP1, [%%GT1 + %%NUM_BYTES]
        vpshufb         %%XCRC_IN_OUT, %%XTMP1

        CRC32_REDUCE_128_TO_32 rax, %%XCRC_IN_OUT, %%XTMP0, %%XTMP1, %%XCRC_TMP
        jmp             %%_do_return

%%_no_block_pending_crc__lt4:
        ;; less than 4 bytes left for CRC
        cmp             %%NUM_BYTES, 3
        jne             %%_no_block_pending_crc__neq3
        vpslldq         %%XCRC_IN_OUT, 5
        jmp             %%_do_barret

%%_no_block_pending_crc__neq3:
        cmp             %%NUM_BYTES, 2
        jne             %%_no_block_pending_crc__neq2
        vpslldq         %%XCRC_IN_OUT, 6
        jmp             %%_do_barret

%%_no_block_pending_crc__neq2:
        vpslldq         %%XCRC_IN_OUT, 7

%%_do_barret:
        CRC32_REDUCE_64_TO_32 rax, %%XCRC_IN_OUT, %%XTMP0, %%XTMP1, %%XCRC_TMP

%%_do_return:
        ;; result in rax

%endmacro       ;; DOCSIS_DEC_CRC32

;; ===================================================================
;; ===================================================================
;; MACRO IMPLEMENTING API FOR STITCHED DOCSIS DECRYPT AND CRC32
;; ===================================================================
%macro AES_DOCSIS_DEC_CRC32 1
%define %%NROUNDS %1    ; [in] Number of rounds (9 or 13)

        mov	        rax, rsp
	sub	        rsp, STACKFRAME_size
	and	        rsp, -64
	mov	        [rsp + _rsp_save], rax	; original SP
        mov             [rsp + _gpr_save + 0*8], r12
        mov             [rsp + _gpr_save + 1*8], r13
        mov             [rsp + _gpr_save + 2*8], rbx
        mov             [rsp + _gpr_save + 3*8], rbp

        vmovdqa64       xmm15, [rel init_crc_value]

        mov             tmp1, [job + _src]
        add             tmp1, [job + _hash_start_src_offset_in_bytes]   ; CRC only start

        cmp             qword [job + _msg_len_to_cipher_in_bytes], 0
        jz              %%aes_docsis_dec_crc32_avx512__no_cipher

        mov             tmp2, [job + _cipher_start_src_offset_in_bytes]
        cmp             tmp2, [job + _hash_start_src_offset_in_bytes]
        jbe             %%aes_docsis_dec_crc32_avx512__skip_aad       ; avoid zero lengths or negative cases

        sub             tmp2, [job + _hash_start_src_offset_in_bytes]   ; CRC only size / AAD

        ETHERNET_FCS_CRC tmp1, tmp2, rax, xmm15, tmp3, xmm0, xmm1, xmm2, xmm3

        not             eax             ; carry CRC value into the combined part
        vmovd           xmm15, eax      ; initial CRC value

%%aes_docsis_dec_crc32_avx512__skip_aad:
        mov             tmp1, [job + _iv]
	vmovdqu64       xmm14, [tmp1]   ; load IV

        mov             tmp2, [job + _src]
        add             tmp2, [job + _cipher_start_src_offset_in_bytes] ; AES start

        mov             tmp3, [job + _dst]                              ; AES destination

        mov             tmp4, [job + _msg_len_to_cipher_in_bytes]       ; CRC + AES size
        mov             tmp5, [job + _dec_keys]
        mov             tmp6, [job + _enc_keys]

        DOCSIS_DEC_CRC32 tmp5, tmp2, tmp3, tmp4, tmp6, \
                         tmp7, tmp8, \
                         xmm15, xmm14, \
                         zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, \
                         zmm8, zmm9, zmm10, zmm11, zmm12, zmm13, \
                         zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                         zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                         %%NROUNDS

        jmp             %%aes_docsis_dec_crc32_avx512__exit

%%aes_docsis_dec_crc32_avx512__no_cipher:
        ;; tmp1 - already points to hash start
        ;; job is arg1
        mov             [rsp + _job_save], job
        mov             arg2, [job + _msg_len_to_hash_in_bytes]
        xor             arg3, arg3
        mov             arg1, tmp1
        call            ethernet_fcs_avx512_local
        mov             job, [rsp + _job_save]

%%aes_docsis_dec_crc32_avx512__exit:
        mov             tmp1, [job + _auth_tag_output]
	mov             [tmp1], eax        ; store CRC32 value

        or              qword [job + _status], IMB_STATUS_COMPLETED_CIPHER

        ;; restore stack pointer and registers
        mov             r12, [rsp + _gpr_save + 0*8]
        mov             r13, [rsp + _gpr_save + 1*8]
        mov             rbx, [rsp + _gpr_save + 2*8]
        mov             rbp, [rsp + _gpr_save + 3*8]
	mov	        rsp, [rsp + _rsp_save]	; original SP

%ifdef SAFE_DATA
	clear_all_zmms_asm
%endif ;; SAFE_DATA
%endmacro

;; ===================================================================
;; ===================================================================
;; input: arg1 = job
;; ===================================================================
align 64
MKGLOBAL(aes_docsis128_dec_crc32_vaes_avx512,function,internal)
aes_docsis128_dec_crc32_vaes_avx512:
        endbranch64
        AES_DOCSIS_DEC_CRC32 9

        ret

align 64
MKGLOBAL(aes_docsis256_dec_crc32_vaes_avx512,function,internal)
aes_docsis256_dec_crc32_vaes_avx512:
        endbranch64
        AES_DOCSIS_DEC_CRC32 13

        ret

mksection stack-noexec

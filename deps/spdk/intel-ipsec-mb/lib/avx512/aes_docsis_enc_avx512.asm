;;
;; Copyright (c) 2019-2021, Intel Corporation
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

;;; DOCSIS SEC BPI (AES128-CBC + AES128-CFB) encryption
;;; stitched together with CRC32

%use smartalign

%include "include/os.asm"
%include "include/imb_job.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/reg_sizes.asm"
%include "include/const.inc"
%include "include/clear_regs.asm"
%include "include/cet.inc"
%define APPEND(a,b) a %+ b

%define CRC_LANE_STATE_TO_START    0x01
%define CRC_LANE_STATE_DONE        0x00
%define CRC_LANE_STATE_IN_PROGRESS 0xff

struc STACK
_gpr_save:      resq    8
_rsp_save:      resq    1
_idx:           resq    1
_len:           resq    1
endstruc

%ifdef LINUX
%define arg1    rdi
%define arg2    rsi
%define TMP2    rcx
%define TMP3    rdx
%else
%define arg1    rcx
%define arg2    rdx
%define TMP2    rdi
%define TMP3    rsi
%endif

%define TMP0    r11
%define TMP1    rbx
%define TMP4    rbp
%define TMP5    r8
%define TMP6    r9
%define TMP7    r10
%define TMP8    rax
%define TMP9    r12
%define TMP10   r13
%define TMP11   r14
%define TMP12   r15

mksection .rodata
default rel

align 16
dupw:
        dq 0x0100010001000100, 0x0100010001000100

align 16
len_masks:
        dq 0x000000000000FFFF, 0x0000000000000000
        dq 0x00000000FFFF0000, 0x0000000000000000
        dq 0x0000FFFF00000000, 0x0000000000000000
        dq 0xFFFF000000000000, 0x0000000000000000
        dq 0x0000000000000000, 0x000000000000FFFF
        dq 0x0000000000000000, 0x00000000FFFF0000
        dq 0x0000000000000000, 0x0000FFFF00000000
        dq 0x0000000000000000, 0xFFFF000000000000

one:    dq  1
two:    dq  2
three:  dq  3
four:   dq  4
five:   dq  5
six:    dq  6
seven:  dq  7

;;; Precomputed constants for CRC32 (Ethernet FCS)
;;;   Details of the CRC algorithm and 4 byte buffer of
;;;   {0x01, 0x02, 0x03, 0x04}:
;;;     Result     Poly       Init        RefIn  RefOut  XorOut
;;;     0xB63CFBCD 0x04C11DB7 0xFFFFFFFF  true   true    0xFFFFFFFF
align 16
rk1:
        dq 0x00000000ccaa009e, 0x00000001751997d0

align 16
rk5:
        dq 0x00000000ccaa009e, 0x0000000163cd6124

align 16
rk7:
        dq 0x00000001f7011640, 0x00000001db710640

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
        vmovdqa64       %%xcrckey, [rel rk1] ; rk1 and rk2 in xcrckey

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

;; =====================================================================
;; =====================================================================
;; Creates stack frame and saves registers
;; =====================================================================
%macro FUNC_ENTRY 0
        mov     rax, rsp
        sub     rsp, STACK_size
        and     rsp, -16

        mov     [rsp + _gpr_save + 8*0], rbx
        mov     [rsp + _gpr_save + 8*1], rbp
        mov     [rsp + _gpr_save + 8*2], r12
        mov     [rsp + _gpr_save + 8*3], r13
        mov     [rsp + _gpr_save + 8*4], r14
        mov     [rsp + _gpr_save + 8*5], r15
%ifndef LINUX
        mov     [rsp + _gpr_save + 8*6], rsi
        mov     [rsp + _gpr_save + 8*7], rdi
%endif
        mov     [rsp + _rsp_save], rax  ; original SP

%endmacro       ; FUNC_ENTRY

;; =====================================================================
;; =====================================================================
;; Restores registers and removes the stack frame
;; =====================================================================
%macro FUNC_EXIT 0
        mov     rbx, [rsp + _gpr_save + 8*0]
        mov     rbp, [rsp + _gpr_save + 8*1]
        mov     r12, [rsp + _gpr_save + 8*2]
        mov     r13, [rsp + _gpr_save + 8*3]
        mov     r14, [rsp + _gpr_save + 8*4]
        mov     r15, [rsp + _gpr_save + 8*5]
%ifndef LINUX
        mov     rsi, [rsp + _gpr_save + 8*6]
        mov     rdi, [rsp + _gpr_save + 8*7]
%endif
        mov     rsp, [rsp + _rsp_save]  ; original SP

%ifdef SAFE_DATA
       clear_all_zmms_asm
%endif ;; SAFE_DATA

%endmacro

;; =====================================================================
;; =====================================================================
;; CRC32 computation round
;; =====================================================================
%macro CRC32_ROUND 17-18
%define %%FIRST         %1      ; [in] "first_possible" or "no_first"
%define %%LAST          %2      ; [in] "last_possible" or "no_last"
%define %%ARG           %3      ; [in] GP with pointer to OOO manager / arguments
%define %%LANEID        %4      ; [in] numerical value with lane id
%define %%XDATA         %5      ; [in] an XMM (any) with input data block for CRC calculation
%define %%XCRC_VAL      %6      ; [clobbered] temporary XMM (xmm0-15)
%define %%XCRC_DAT      %7      ; [clobbered] temporary XMM (xmm0-15)
%define %%XCRC_MUL      %8      ; [clobbered] temporary XMM (xmm0-15)
%define %%XCRC_TMP      %9      ; [clobbered] temporary XMM (xmm0-15)
%define %%XCRC_TMP2     %10     ; [clobbered] temporary XMM (xmm0-15)
%define %%IN            %11     ; [clobbered] temporary GPR (last partial only)
%define %%IDX           %12     ; [in] GP with data offset (last partial only)
%define %%OFFS          %13     ; [in] numerical offset (last partial only)
%define %%GT8           %14     ; [clobbered] temporary GPR (last partial only)
%define %%GT9           %15     ; [clobbered] temporary GPR (last partial only)
%define %%CRC32         %16     ; [clobbered] temporary GPR (last partial only)
%define %%LANEDAT       %17     ; [in/out] CRC cumulative sum
%define %%SUBLEN        %18     ; [in/optional] if "dont_subtract_len" length not subtracted

        cmp             byte [%%ARG + _docsis_crc_args_done + %%LANEID], CRC_LANE_STATE_DONE
        je              %%_crc_lane_done

%ifnidn %%FIRST, no_first
        cmp             byte [%%ARG + _docsis_crc_args_done + %%LANEID], CRC_LANE_STATE_TO_START
        je              %%_crc_lane_first_round
%endif  ; !no_first

%ifnidn %%LAST, no_last
        cmp             word [%%ARG + _docsis_crc_args_len + 2*%%LANEID], 16
        jb              %%_crc_lane_last_partial
%endif  ; no_last

        ;; The most common case: next block for CRC
        vmovdqa64       %%XCRC_VAL, %%LANEDAT
        CRC_CLMUL       %%XCRC_VAL, %%XCRC_MUL, %%XDATA, %%XCRC_TMP
        vmovdqa64       %%LANEDAT, %%XCRC_VAL
%ifnidn %%SUBLEN, dont_subtract_len
        sub             word [%%ARG + _docsis_crc_args_len + 2*%%LANEID], 16
%endif
%ifidn %%LAST, no_last
%ifidn %%FIRST, no_first
        ;; no jump needed - just fall through
%else
        jmp             %%_crc_lane_done
%endif  ; no_first
%else
        jmp             %%_crc_lane_done
%endif  ; np_last

%ifnidn %%LAST, no_last
%%_crc_lane_last_partial:
        ;; Partial block case (the last block)
        ;; - last CRC round is specific
        ;; - followed by CRC reduction and write back of the CRC
        vmovdqa64       %%XCRC_VAL, %%LANEDAT
        movzx           %%GT9, word [%%ARG + _docsis_crc_args_len + %%LANEID*2] ; GT9 = bytes_to_crc
        lea             %%GT8, [rel pshufb_shf_table]
        vmovdqu64       %%XCRC_TMP, [%%GT8 + %%GT9]
        mov             %%IN, [%%ARG + _aesarg_in + 8*%%LANEID]
        lea             %%GT8, [%%IN + %%IDX + %%OFFS]
        vmovdqu64       %%XCRC_DAT, [%%GT8 - 16 + %%GT9]  ; XCRC_DAT = data for CRC
        vmovdqa64       %%XCRC_TMP2, %%XCRC_VAL
        vpshufb         %%XCRC_VAL, %%XCRC_TMP  ; top bytes_to_crc with LSB XCRC_VAL
        vpxorq          %%XCRC_TMP, [rel mask3]
        vpshufb         %%XCRC_TMP2, %%XCRC_TMP ; bottom (16 - bytes_to_crc) with MSB XCRC_VAL

        vpblendvb       %%XCRC_DAT, %%XCRC_TMP2, %%XCRC_DAT, %%XCRC_TMP

        CRC_CLMUL       %%XCRC_VAL, %%XCRC_MUL, %%XCRC_DAT, %%XCRC_TMP
        CRC32_REDUCE_128_TO_32 %%CRC32, %%XCRC_VAL, %%XCRC_TMP, %%XCRC_DAT, %%XCRC_TMP2

        ;; save final CRC value in init
        vmovd           %%LANEDAT,  DWORD(%%CRC32)

        ;; write back CRC value into source buffer
        movzx           %%GT9, word [%%ARG + _docsis_crc_args_len + %%LANEID*2]
        lea             %%GT8, [%%IN + %%IDX + %%OFFS]
        mov             [%%GT8 + %%GT9], DWORD(%%CRC32)

        ;; reload the data for cipher (includes just computed CRC) - @todo store to load
        vmovdqu64       %%XDATA, [%%IN + %%IDX + %%OFFS]

        mov             word [%%ARG + _docsis_crc_args_len + 2*%%LANEID], 0
        ;; mark as done
        mov             byte [%%ARG + _docsis_crc_args_done + %%LANEID], CRC_LANE_STATE_DONE
%ifnidn %%FIRST, no_first
        jmp             %%_crc_lane_done
%endif  ; no_first
%endif  ; no_last

%ifnidn %%FIRST, no_first
%%_crc_lane_first_round:
        ;; Case of less than 16 bytes will not happen here since
        ;; submit code takes care of it.
        ;; in the first round just XOR initial CRC with the first block
        vpxorq          %%LANEDAT, %%LANEDAT, %%XDATA
        ;; mark first block as done
        mov             byte [%%ARG + _docsis_crc_args_done + %%LANEID], CRC_LANE_STATE_IN_PROGRESS
        sub             word [%%ARG + _docsis_crc_args_len + 2*%%LANEID], 16
%endif  ; no_first

%%_crc_lane_done:
%endmacro       ; CRC32_ROUND

;; =====================================================================
;; =====================================================================
;; CRC32 computation middle round only
;; - uses K registers to allow/disallow update of lane's CRC value
;; =====================================================================
%macro CRC32_MID_ROUND 7
%define %%ARG           %1      ; [in] GP with pointer to OOO manager / arguments
%define %%LANEID        %2      ; [in] numerical value with lane id
%define %%XDATA         %3      ; [in] an XMM (any) with input data block for CRC calculation
%define %%XCRC_VAL      %4      ; [clobbered] temporary XMM (xmm0-15)
%define %%XCRC_MUL      %5      ; [clobbered] temporary XMM (xmm0-15)
%define %%XCRC_TMP      %6      ; [clobbered] temporary XMM (xmm0-15)
%define %%LANEDAT       %7      ; [in/out] CRC cumulative sum

%if %%LANEID < 7

        ;; use K registers
%assign kreg (%%LANEID + 1)

        ;; The most common case: next block for CRC
        vmovdqa64       %%XCRC_VAL, %%LANEDAT
        CRC_CLMUL       %%XCRC_VAL, %%XCRC_MUL, %%XDATA, %%XCRC_TMP
        vmovdqa64       %%LANEDAT{APPEND(k,kreg)}, %%XCRC_VAL

%else

        cmp             byte [%%ARG + _docsis_crc_args_done + %%LANEID], CRC_LANE_STATE_DONE
        je              %%_crc_lane_done

        ;; The most common case: next block for CRC
        vmovdqa64       %%XCRC_VAL, %%LANEDAT
        CRC_CLMUL       %%XCRC_VAL, %%XCRC_MUL, %%XDATA, %%XCRC_TMP
        vmovdqa64       %%LANEDAT, %%XCRC_VAL
%%_crc_lane_done:

%endif  ;; !(%%LANEID < 7)

%endmacro       ; CRC32_MID_ROUND

;; =====================================================================
;; =====================================================================
;; AES128/256-CBC encryption combined with CRC32 operations
;; =====================================================================
%macro AES_CBC_ENC_CRC32_PARALLEL 48
%define %%ARG   %1      ; [in/out] GPR with pointer to arguments structure (updated on output)
%define %%LEN   %2      ; [in/clobbered] number of bytes to be encrypted on all lanes
%define %%GT0   %3      ; [clobbered] GP register
%define %%GT1   %4      ; [clobbered] GP register
%define %%GT2   %5      ; [clobbered] GP register
%define %%GT3   %6      ; [clobbered] GP register
%define %%GT4   %7      ; [clobbered] GP register
%define %%GT5   %8      ; [clobbered] GP register
%define %%GT6   %9      ; [clobbered] GP register
%define %%GT7   %10     ; [clobbered] GP register
%define %%GT8   %11     ; [clobbered] GP register
%define %%GT9   %12     ; [clobbered] GP register
%define %%GT10  %13     ; [clobbered] GP register
%define %%GT11  %14     ; [clobbered] GP register
%define %%GT12  %15     ; [clobbered] GP register
%define %%ZT0   %16     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT1   %17     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT2   %18     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT3   %19     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT4   %20     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT5   %21     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT6   %22     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT7   %23     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT8   %24     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT9   %25     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT10  %26     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT11  %27     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT12  %28     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT13  %29     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT14  %30     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT15  %31     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT16  %32     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT17  %33     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT18  %34     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT19  %35     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT20  %36     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT21  %37     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT22  %38     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT23  %39     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT24  %40     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT25  %41     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT26  %42     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT27  %43     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT28  %44     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT29  %45     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT30  %46     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT31  %47     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%NROUNDS %48   ; [in] Number of rounds (9 or 13, based on key size)

%define %%KEYS0 %%GT0
%define %%KEYS1 %%GT1
%define %%KEYS2 %%GT2
%define %%KEYS3 %%GT3
%define %%KEYS4 %%GT4
%define %%KEYS5 %%GT5
%define %%KEYS6 %%GT6
%define %%KEYS7 %%GT7

%define %%GP1   %%GT10
%define %%CRC32 %%GT11
%define %%IDX   %%GT12

%define %%XCIPH0 XWORD(%%ZT0)
%define %%XCIPH1 XWORD(%%ZT1)
%define %%XCIPH2 XWORD(%%ZT2)
%define %%XCIPH3 XWORD(%%ZT3)
%define %%XCIPH4 XWORD(%%ZT4)
%define %%XCIPH5 XWORD(%%ZT5)
%define %%XCIPH6 XWORD(%%ZT6)
%define %%XCIPH7 XWORD(%%ZT7)

%define %%XCRC_MUL XWORD(%%ZT8)
%define %%XCRC_TMP XWORD(%%ZT9)
%define %%XCRC_DAT XWORD(%%ZT10)
%define %%XCRC_VAL XWORD(%%ZT11)
%define %%XCRC_TMP2 XWORD(%%ZT12)
%define %%XTMP  %%XCRC_TMP2

;; used for loading plain text
%define %%XDATA0 XWORD(%%ZT16)
%define %%XDATA1 XWORD(%%ZT17)
%define %%XDATA2 XWORD(%%ZT18)
%define %%XDATA3 XWORD(%%ZT19)
%define %%XDATA4 XWORD(%%ZT20)
%define %%XDATA5 XWORD(%%ZT21)
%define %%XDATA6 XWORD(%%ZT22)
%define %%XDATA7 XWORD(%%ZT23)

;; used for current CRC sums
%define %%XDATB0 XWORD(%%ZT27)
%define %%XDATB1 XWORD(%%ZT28)
%define %%XDATB2 XWORD(%%ZT29)
%define %%XDATB3 XWORD(%%ZT30)
%define %%XDATB4 XWORD(%%ZT31)
%define %%XDATB5 XWORD(%%ZT13)
%define %%XDATB6 XWORD(%%ZT14)
%define %%XDATB7 XWORD(%%ZT15)

        xor             %%IDX, %%IDX

        vmovdqa64       %%XCRC_MUL, [rel rk1]

        vmovdqa64       %%XCIPH0, [%%ARG + _aesarg_IV + 16*0]
        vmovdqa64       %%XCIPH1, [%%ARG + _aesarg_IV + 16*1]
        vmovdqa64       %%XCIPH2, [%%ARG + _aesarg_IV + 16*2]
        vmovdqa64       %%XCIPH3, [%%ARG + _aesarg_IV + 16*3]
        vmovdqa64       %%XCIPH4, [%%ARG + _aesarg_IV + 16*4]
        vmovdqa64       %%XCIPH5, [%%ARG + _aesarg_IV + 16*5]
        vmovdqa64       %%XCIPH6, [%%ARG + _aesarg_IV + 16*6]
        vmovdqa64       %%XCIPH7, [%%ARG + _aesarg_IV + 16*7]

        mov             %%KEYS0, [%%ARG + _aesarg_keys + 8*0]
        mov             %%KEYS1, [%%ARG + _aesarg_keys + 8*1]
        mov             %%KEYS2, [%%ARG + _aesarg_keys + 8*2]
        mov             %%KEYS3, [%%ARG + _aesarg_keys + 8*3]
        mov             %%KEYS4, [%%ARG + _aesarg_keys + 8*4]
        mov             %%KEYS5, [%%ARG + _aesarg_keys + 8*5]
        mov             %%KEYS6, [%%ARG + _aesarg_keys + 8*6]
        mov             %%KEYS7, [%%ARG + _aesarg_keys + 8*7]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Pipeline start

        ;; CRC32 rounds on all lanes - first and last cases are possible
        ;; - load current CRC sum
        ;; - load plain text block
        ;; - do the initial CRC round
        ;; - keep CRC lane status in K register (lanes 0 to 6)
%assign crc_lane 0
%rep 8
        mov             %%GP1, [%%ARG + _aesarg_in + (8 * crc_lane)]
        vmovdqu64       APPEND(%%XDATA, crc_lane), [%%GP1 + %%IDX]

        vmovdqa64       APPEND(%%XDATB, crc_lane), [%%ARG + _docsis_crc_args_init + (16 *crc_lane)]

        CRC32_ROUND first_possible, last_possible, %%ARG, crc_lane, \
                    APPEND(%%XDATA, crc_lane), %%XCRC_VAL, %%XCRC_DAT, \
                    %%XCRC_MUL, %%XCRC_TMP, %%XCRC_TMP2, \
                    %%GP1, %%IDX, 0, %%GT8, %%GT9, %%CRC32, APPEND(%%XDATB, crc_lane)
%if crc_lane < 7
        ;; lanes 0 to 6 use k1 to k7
        ;; lane 7 uses condition check
%assign k_reg (crc_lane + 1)
        kmovb           APPEND(k,k_reg), [%%ARG + _docsis_crc_args_done + (crc_lane * 1)]
%endif
%assign crc_lane (crc_lane + 1)
%endrep

        ;; check if only 16 bytes in this execution
        sub             %%LEN, 16
        je              %%_encrypt_the_last_block

%%_main_enc_loop:
        ;; if 16 bytes left (for CRC) then
        ;; go to the code variant where CRC last block case is checked
        cmp             %%LEN, 16
        je              %%_encrypt_and_crc_the_last_block

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; - use ternary logic for: plain-text XOR IV and AES ARK(0)
        ;;      - IV = XCIPHx
        ;;      - plain-text = XDATAx
        ;;      - ARK = [%%KEYSx + 16*0]

        vpternlogq      %%XCIPH0, %%XDATA0, [%%KEYS0 + 16*0], 0x96
        vpternlogq      %%XCIPH1, %%XDATA1, [%%KEYS1 + 16*0], 0x96
        vpternlogq      %%XCIPH2, %%XDATA2, [%%KEYS2 + 16*0], 0x96
        vpternlogq      %%XCIPH3, %%XDATA3, [%%KEYS3 + 16*0], 0x96
        vpternlogq      %%XCIPH4, %%XDATA4, [%%KEYS4 + 16*0], 0x96
        vpternlogq      %%XCIPH5, %%XDATA5, [%%KEYS5 + 16*0], 0x96
        vpternlogq      %%XCIPH6, %%XDATA6, [%%KEYS6 + 16*0], 0x96
        vpternlogq      %%XCIPH7, %%XDATA7, [%%KEYS7 + 16*0], 0x96

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 1 to NROUNDS (9 or 13)
%assign crc_lane 0
%assign i 1
%rep %%NROUNDS
        vaesenc         %%XCIPH0, [%%KEYS0 + 16*i]
        vaesenc         %%XCIPH1, [%%KEYS1 + 16*i]
        vaesenc         %%XCIPH2, [%%KEYS2 + 16*i]
        vaesenc         %%XCIPH3, [%%KEYS3 + 16*i]
        vaesenc         %%XCIPH4, [%%KEYS4 + 16*i]
        vaesenc         %%XCIPH5, [%%KEYS5 + 16*i]
        vaesenc         %%XCIPH6, [%%KEYS6 + 16*i]
        vaesenc         %%XCIPH7, [%%KEYS7 + 16*i]

%if (i > 1) && (crc_lane < 8)
        ;; don't start with AES round 1
        mov             %%GP1, [%%ARG + _aesarg_in + (8 * crc_lane)]
        vmovdqu64       APPEND(%%XDATA,crc_lane), [%%GP1 + %%IDX + 16]

        CRC32_MID_ROUND %%ARG, crc_lane, APPEND(%%XDATA, crc_lane), \
                        %%XCRC_VAL, %%XCRC_MUL, %%XCRC_TMP, \
                        APPEND(%%XDATB, crc_lane)

%assign crc_lane (crc_lane + 1)
%endif  ;; i > 1 && crc_lane < 8

%assign i (i + 1)
%endrep

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 10 or 14
        vaesenclast     %%XCIPH0, [%%KEYS0 + 16*i]
        vaesenclast     %%XCIPH1, [%%KEYS1 + 16*i]
        vaesenclast     %%XCIPH2, [%%KEYS2 + 16*i]
        vaesenclast     %%XCIPH3, [%%KEYS3 + 16*i]
        vaesenclast     %%XCIPH4, [%%KEYS4 + 16*i]
        vaesenclast     %%XCIPH5, [%%KEYS5 + 16*i]
        vaesenclast     %%XCIPH6, [%%KEYS6 + 16*i]
        vaesenclast     %%XCIPH7, [%%KEYS7 + 16*i]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; store cipher text
        ;; - XCIPHx is an IV for the next block

        mov             %%GT8, [%%ARG + _aesarg_out + 8*0]
        mov             %%GP1, [%%ARG + _aesarg_out + 8*1]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH0
        vmovdqu64       [%%GP1 + %%IDX], %%XCIPH1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*2]
        mov             %%GP1, [%%ARG + _aesarg_out + 8*3]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH2
        vmovdqu64       [%%GP1 + %%IDX], %%XCIPH3
        mov             %%GT8, [%%ARG + _aesarg_out + 8*4]
        mov             %%GP1, [%%ARG + _aesarg_out + 8*5]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH4
        vmovdqu64       [%%GP1 + %%IDX], %%XCIPH5
        mov             %%GT8, [%%ARG + _aesarg_out + 8*6]
        mov             %%GP1, [%%ARG + _aesarg_out + 8*7]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH6
        vmovdqu64       [%%GP1 + %%IDX], %%XCIPH7

        add             %%IDX, 16
        sub             %%LEN, 16

        jmp             %%_main_enc_loop

%%_encrypt_and_crc_the_last_block:
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Main loop doesn't subtract lengths to save cycles
        ;; - all subtracts get accumulated and are done below
        vmovdqa64       %%XCRC_TMP, [%%ARG + _docsis_crc_args_len + 2*0]
        vpbroadcastw    %%XCRC_TMP2, WORD(%%IDX)
        vpsubw          %%XCRC_TMP, %%XCRC_TMP, %%XCRC_TMP2
        vmovdqa64       [%%ARG + _docsis_crc_args_len + 2*0], %%XCRC_TMP

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; - load key pointers to perform AES rounds
        ;; - use ternary logic for: plain-text XOR IV and AES ARK(0)
        ;;      - IV = XCIPHx
        ;;      - plain-text = XDATAx
        ;;      - ARK = [%%KEYSx + 16*0]

        vpternlogq      %%XCIPH0, %%XDATA0, [%%KEYS0 + 16*0], 0x96
        vpternlogq      %%XCIPH1, %%XDATA1, [%%KEYS1 + 16*0], 0x96
        vpternlogq      %%XCIPH2, %%XDATA2, [%%KEYS2 + 16*0], 0x96
        vpternlogq      %%XCIPH3, %%XDATA3, [%%KEYS3 + 16*0], 0x96
        vpternlogq      %%XCIPH4, %%XDATA4, [%%KEYS4 + 16*0], 0x96
        vpternlogq      %%XCIPH5, %%XDATA5, [%%KEYS5 + 16*0], 0x96
        vpternlogq      %%XCIPH6, %%XDATA6, [%%KEYS6 + 16*0], 0x96
        vpternlogq      %%XCIPH7, %%XDATA7, [%%KEYS7 + 16*0], 0x96

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 1 to NROUNDS (9 or 13)
%assign crc_lane 0
%assign i 1
%rep %%NROUNDS
        vaesenc         %%XCIPH0, [%%KEYS0 + 16*i]
        vaesenc         %%XCIPH1, [%%KEYS1 + 16*i]
        vaesenc         %%XCIPH2, [%%KEYS2 + 16*i]
        vaesenc         %%XCIPH3, [%%KEYS3 + 16*i]
        vaesenc         %%XCIPH4, [%%KEYS4 + 16*i]
        vaesenc         %%XCIPH5, [%%KEYS5 + 16*i]
        vaesenc         %%XCIPH6, [%%KEYS6 + 16*i]
        vaesenc         %%XCIPH7, [%%KEYS7 + 16*i]

%if (i > 1) && (crc_lane < 8)
        ;; don't start with AES round 1
        mov             %%GP1, [%%ARG + _aesarg_in + (8 * crc_lane)]
        vmovdqu64       APPEND(%%XDATA, crc_lane), [%%GP1 + %%IDX + 16]

        CRC32_ROUND no_first, last_possible, %%ARG, crc_lane, \
                    APPEND(%%XDATA, crc_lane), %%XCRC_VAL, %%XCRC_DAT, \
                    %%XCRC_MUL, %%XCRC_TMP, %%XCRC_TMP2, \
                    %%GP1, %%IDX, 16, %%GT8, %%GT9, %%CRC32, APPEND(%%XDATB, crc_lane)
%assign crc_lane (crc_lane + 1)
%endif  ;; i > 1 && crc_lane < 8

%assign i (i + 1)
%endrep

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 10 or 14
        vaesenclast     %%XCIPH0, [%%KEYS0 + 16*i]
        vaesenclast     %%XCIPH1, [%%KEYS1 + 16*i]
        vaesenclast     %%XCIPH2, [%%KEYS2 + 16*i]
        vaesenclast     %%XCIPH3, [%%KEYS3 + 16*i]
        vaesenclast     %%XCIPH4, [%%KEYS4 + 16*i]
        vaesenclast     %%XCIPH5, [%%KEYS5 + 16*i]
        vaesenclast     %%XCIPH6, [%%KEYS6 + 16*i]
        vaesenclast     %%XCIPH7, [%%KEYS7 + 16*i]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; store cipher text
        ;; - XCIPHx is an IV for the next block

        mov             %%GT8, [%%ARG + _aesarg_out + 8*0]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*1]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH0
        vmovdqu64       [%%GT9 + %%IDX], %%XCIPH1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*2]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*3]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH2
        vmovdqu64       [%%GT9 + %%IDX], %%XCIPH3
        mov             %%GT8, [%%ARG + _aesarg_out + 8*4]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*5]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH4
        vmovdqu64       [%%GT9 + %%IDX], %%XCIPH5
        mov             %%GT8, [%%ARG + _aesarg_out + 8*6]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*7]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH6
        vmovdqu64       [%%GT9 + %%IDX], %%XCIPH7

        add             %%IDX, 16
        sub             %%LEN, 16

%%_encrypt_the_last_block:
        ;; NOTE: XDATA[0-7] preloaded with data blocks from corresponding lanes

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; - load key pointers to perform AES rounds
        ;; - use ternary logic for: plain-text XOR IV and AES ARK(0)
        ;;      - IV = XCIPHx
        ;;      - plain-text = XDATAx
        ;;      - ARK = [%%KEYSx + 16*0]

        vpternlogq      %%XCIPH0, %%XDATA0, [%%KEYS0 + 16*0], 0x96
        vpternlogq      %%XCIPH1, %%XDATA1, [%%KEYS1 + 16*0], 0x96
        vpternlogq      %%XCIPH2, %%XDATA2, [%%KEYS2 + 16*0], 0x96
        vpternlogq      %%XCIPH3, %%XDATA3, [%%KEYS3 + 16*0], 0x96
        vpternlogq      %%XCIPH4, %%XDATA4, [%%KEYS4 + 16*0], 0x96
        vpternlogq      %%XCIPH5, %%XDATA5, [%%KEYS5 + 16*0], 0x96
        vpternlogq      %%XCIPH6, %%XDATA6, [%%KEYS6 + 16*0], 0x96
        vpternlogq      %%XCIPH7, %%XDATA7, [%%KEYS7 + 16*0], 0x96

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 1 to NROUNDS (9 or 13)
%assign crc_lane 0
%assign i 1
%rep %%NROUNDS
        vaesenc         %%XCIPH0, [%%KEYS0 + 16*i]
        vaesenc         %%XCIPH1, [%%KEYS1 + 16*i]
        vaesenc         %%XCIPH2, [%%KEYS2 + 16*i]
        vaesenc         %%XCIPH3, [%%KEYS3 + 16*i]
        vaesenc         %%XCIPH4, [%%KEYS4 + 16*i]
        vaesenc         %%XCIPH5, [%%KEYS5 + 16*i]
        vaesenc         %%XCIPH6, [%%KEYS6 + 16*i]
        vaesenc         %%XCIPH7, [%%KEYS7 + 16*i]

%if crc_lane < 8
        ;; save CRC sum from registers back into the context structure
        vmovdqa64       [%%ARG + _docsis_crc_args_init + (16 * crc_lane)], APPEND(%%XDATB, crc_lane)
%endif
%assign crc_lane (crc_lane + 1)
%assign i (i + 1)
%endrep

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 10 or 14
        vaesenclast     %%XCIPH0, [%%KEYS0 + 16*i]
        vaesenclast     %%XCIPH1, [%%KEYS1 + 16*i]
        vaesenclast     %%XCIPH2, [%%KEYS2 + 16*i]
        vaesenclast     %%XCIPH3, [%%KEYS3 + 16*i]
        vaesenclast     %%XCIPH4, [%%KEYS4 + 16*i]
        vaesenclast     %%XCIPH5, [%%KEYS5 + 16*i]
        vaesenclast     %%XCIPH6, [%%KEYS6 + 16*i]
        vaesenclast     %%XCIPH7, [%%KEYS7 + 16*i]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; store cipher text
        ;; - XCIPHx is an IV for the next block

        mov             %%GT8, [%%ARG + _aesarg_out + 8*0]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*1]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH0
        vmovdqu64       [%%GT9 + %%IDX], %%XCIPH1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*2]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*3]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH2
        vmovdqu64       [%%GT9 + %%IDX], %%XCIPH3
        mov             %%GT8, [%%ARG + _aesarg_out + 8*4]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*5]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH4
        vmovdqu64       [%%GT9 + %%IDX], %%XCIPH5
        mov             %%GT8, [%%ARG + _aesarg_out + 8*6]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*7]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH6
        vmovdqu64       [%%GT9 + %%IDX], %%XCIPH7

        add             %%IDX, 16

%%_enc_done:
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; update IV
        vmovdqa64       [%%ARG + _aesarg_IV + 16*0], %%XCIPH0
        vmovdqa64       [%%ARG + _aesarg_IV + 16*1], %%XCIPH1
        vmovdqa64       [%%ARG + _aesarg_IV + 16*2], %%XCIPH2
        vmovdqa64       [%%ARG + _aesarg_IV + 16*3], %%XCIPH3
        vmovdqa64       [%%ARG + _aesarg_IV + 16*4], %%XCIPH4
        vmovdqa64       [%%ARG + _aesarg_IV + 16*5], %%XCIPH5
        vmovdqa64       [%%ARG + _aesarg_IV + 16*6], %%XCIPH6
        vmovdqa64       [%%ARG + _aesarg_IV + 16*7], %%XCIPH7

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; update IN and OUT pointers
        vpbroadcastq    %%ZT0, %%IDX
        vpaddq          %%ZT1, %%ZT0, [%%ARG + _aesarg_in]
        vpaddq          %%ZT2, %%ZT0, [%%ARG + _aesarg_out]
        vmovdqu64       [%%ARG + _aesarg_in], %%ZT1
        vmovdqu64       [%%ARG + _aesarg_out], %%ZT2

%endmacro       ; AES_CBC_ENC_CRC32_PARALLEL

;; =====================================================================
;; =====================================================================
;; DOCSIS SEC BPI + CRC32 SUBMIT / FLUSH macro
;; =====================================================================
%macro SUBMIT_FLUSH_DOCSIS_CRC32 49
%define %%STATE %1      ; [in/out] GPR with pointer to arguments structure (updated on output)
%define %%JOB   %2      ; [in] number of bytes to be encrypted on all lanes
%define %%GT0   %3      ; [clobbered] GP register
%define %%GT1   %4      ; [clobbered] GP register
%define %%GT2   %5      ; [clobbered] GP register
%define %%GT3   %6      ; [clobbered] GP register
%define %%GT4   %7      ; [clobbered] GP register
%define %%GT5   %8      ; [clobbered] GP register
%define %%GT6   %9      ; [clobbered] GP register
%define %%GT7   %10     ; [clobbered] GP register
%define %%GT8   %11     ; [clobbered] GP register
%define %%GT9   %12     ; [clobbered] GP register
%define %%GT10  %13     ; [clobbered] GP register
%define %%GT11  %14     ; [clobbered] GP register
%define %%GT12  %15     ; [clobbered] GP register
%define %%ZT0   %16     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT1   %17     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT2   %18     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT3   %19     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT4   %20     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT5   %21     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT6   %22     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT7   %23     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT8   %24     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT9   %25     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT10  %26     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT11  %27     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT12  %28     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT13  %29     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT14  %30     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT15  %31     ; [clobbered] ZMM register (zmm0 - zmm15)
%define %%ZT16  %32     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT17  %33     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT18  %34     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT19  %35     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT20  %36     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT21  %37     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT22  %38     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT23  %39     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT24  %40     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT25  %41     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT26  %42     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT27  %43     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT28  %44     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT29  %45     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT30  %46     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%ZT31  %47     ; [clobbered] ZMM register (zmm16 - zmm31)
%define %%SUBMIT_FLUSH %48 ; [in] "submit" or "flush"; %%JOB ignored for "flush"
%define %%NROUNDS %49   ; [in] Number of rounds (9 or 13, based on key size)

%define %%idx           %%GT0
%define %%unused_lanes  %%GT3
%define %%job_rax       rax
%define %%len2          arg2

%ifidn %%SUBMIT_FLUSH, submit
        ;; /////////////////////////////////////////////////
        ;; SUBMIT

; idx needs to be in rbp
%define %%len           %%GT0
%define %%tmp           %%GT0
%define %%lane          %%GT1
%define %%iv            %%GT2

        mov             %%unused_lanes, [%%STATE + _aes_unused_lanes]
        mov             %%lane, %%unused_lanes
        and             %%lane, 0xF
        shr             %%unused_lanes, 4
        mov             %%len, [%%JOB + _msg_len_to_cipher_in_bytes]
        ;; DOCSIS may pass size unaligned to block size
        and             %%len, -16
        mov             %%iv, [%%JOB + _iv]
        mov             [%%STATE + _aes_unused_lanes], %%unused_lanes

        mov             [%%STATE + _aes_job_in_lane + %%lane*8], %%JOB

        vmovdqa         xmm0, [%%STATE + _aes_lens]
        XVPINSRW        xmm0, xmm1, %%tmp, %%lane, %%len, scale_x16
        vmovdqa         [%%STATE + _aes_lens], xmm0

        mov             %%tmp, [%%JOB + _src]
        add             %%tmp, [%%JOB + _cipher_start_src_offset_in_bytes]
        vmovdqu         xmm0, [%%iv]
        mov             [%%STATE + _aes_args_in + %%lane*8], %%tmp
        mov             %%tmp, [%%JOB + _enc_keys]
        mov             [%%STATE + _aes_args_keys + %%lane*8], %%tmp
        mov             %%tmp, [%%JOB + _dst]
        mov             [%%STATE + _aes_args_out + %%lane*8], %%tmp
        mov             byte [%%STATE + _docsis_crc_args_done + %%lane], CRC_LANE_STATE_DONE
        shl             %%lane, 4       ; multiply by 16
        vmovdqa64       [%%STATE + _aes_args_IV + %%lane], xmm0

        cmp             qword [%%JOB + _msg_len_to_hash_in_bytes], 14
        jb              %%_crc_complete

        ;; there is CRC to calculate - now in one go or in chunks
        ;; - load init value into the lane
        vmovdqa64       XWORD(%%ZT0), [rel init_crc_value]
        vmovdqa64       [%%STATE + _docsis_crc_args_init + %%lane], XWORD(%%ZT0)
        shr             %%lane, 4

        mov             %%GT6, [%%JOB + _src]
        add             %%GT6, [%%JOB + _hash_start_src_offset_in_bytes]

        prefetchw       [%%GT6 + 0*64]
        prefetchw       [%%GT6 + 1*64]

        vmovdqa64       XWORD(%%ZT1), [rel rk1]

        cmp             qword [%%JOB + _msg_len_to_cipher_in_bytes], (3 * 16)
        jae             %%_crc_in_chunks

        ;; this is short message - compute whole CRC in one go
        mov             %%GT5, [%%JOB + _msg_len_to_hash_in_bytes]
        mov             [%%STATE + _docsis_crc_args_len + %%lane*2], WORD(%%GT5)

        ;; GT6 - ptr, GT5 - length, ZT1 - CRC_MUL, ZT0 - CRC_IN_OUT
        ETHERNET_FCS_CRC %%GT6, %%GT5, %%GT7, XWORD(%%ZT0), %%GT2, \
                         XWORD(%%ZT1), XWORD(%%ZT2), XWORD(%%ZT3), XWORD(%%ZT4)

        mov             %%GT6, [%%JOB + _src]
        add             %%GT6, [%%JOB + _hash_start_src_offset_in_bytes]
        add             %%GT6, [%%JOB + _msg_len_to_hash_in_bytes]
        mov             [%%GT6], DWORD(%%GT7)
        shl             %%lane, 4
        mov             [%%STATE + _docsis_crc_args_init + %%lane], DWORD(%%GT7)
        shr             %%lane, 4
        jmp             %%_crc_complete

%%_crc_in_chunks:
        ;; CRC in chunks will follow
        mov             %%GT5, [%%JOB + _msg_len_to_cipher_in_bytes]
        sub             %%GT5, 4
        mov             [%%STATE + _docsis_crc_args_len + %%lane*2], WORD(%%GT5)
        mov             byte [%%STATE + _docsis_crc_args_done + %%lane], CRC_LANE_STATE_TO_START

        ;; now calculate only CRC on bytes before cipher start
        mov             %%GT5, [%%JOB + _cipher_start_src_offset_in_bytes]
        sub             %%GT5, [%%JOB + _hash_start_src_offset_in_bytes]

        ;; GT6 - ptr, GT5 - length, ZT1 - CRC_MUL, ZT0 - CRC_IN_OUT
        ETHERNET_FCS_CRC %%GT6, %%GT5, %%GT7, XWORD(%%ZT0), %%GT2, \
                         XWORD(%%ZT1), XWORD(%%ZT2), XWORD(%%ZT3), XWORD(%%ZT4)

        not             DWORD(%%GT7)
        vmovd           xmm8, DWORD(%%GT7)
        shl             %%lane, 4
        vmovdqa64       [%%STATE + _docsis_crc_args_init + %%lane], xmm8
        shr             %%lane, 4

%%_crc_complete:
        cmp             %%unused_lanes, 0xf
        je              %%_load_lens
        xor             %%job_rax, %%job_rax    ; return NULL
        jmp             %%_return

%%_load_lens:
        ;; load lens into xmm0
        vmovdqa64       xmm0, [%%STATE + _aes_lens]

%else
        ;; /////////////////////////////////////////////////
        ;; FLUSH

%define %%tmp1             %%GT1
%define %%good_lane        %%GT2
%define %%tmp              %%GT3
%define %%tmp2             %%GT4
%define %%tmp3             %%GT5

        ; check for empty
        mov             %%unused_lanes, [%%STATE + _aes_unused_lanes]
        bt              %%unused_lanes, 32+3
        jnc             %%_find_non_null_lane

        xor             %%job_rax, %%job_rax    ; return NULL
        jmp             %%_return

%%_find_non_null_lane:
        ; find a lane with a non-null job
        xor             %%good_lane, %%good_lane
        cmp             qword [%%STATE + _aes_job_in_lane + 1*8], 0
        cmovne          %%good_lane, [rel one]
        cmp             qword [%%STATE + _aes_job_in_lane + 2*8], 0
        cmovne          %%good_lane, [rel two]
        cmp             qword [%%STATE + _aes_job_in_lane + 3*8], 0
        cmovne          %%good_lane, [rel three]
        cmp             qword [%%STATE + _aes_job_in_lane + 4*8], 0
        cmovne          %%good_lane, [rel four]
        cmp             qword [%%STATE + _aes_job_in_lane + 5*8], 0
        cmovne          %%good_lane, [rel five]
        cmp             qword [%%STATE + _aes_job_in_lane + 6*8], 0
        cmovne          %%good_lane, [rel six]
        cmp             qword [%%STATE + _aes_job_in_lane + 7*8], 0
        cmovne          %%good_lane, [rel seven]

        ; copy good_lane to empty lanes
        mov             %%tmp1, [%%STATE + _aes_args_in + %%good_lane*8]
        mov             %%tmp2, [%%STATE + _aes_args_out + %%good_lane*8]
        mov             %%tmp3, [%%STATE + _aes_args_keys + %%good_lane*8]
        mov             WORD(%%GT6), [%%STATE + _docsis_crc_args_len + %%good_lane*2]
        mov             BYTE(%%GT7), [%%STATE + _docsis_crc_args_done + %%good_lane]
        shl             %%good_lane, 4 ; multiply by 16
        vmovdqa64       xmm2, [%%STATE + _aes_args_IV + %%good_lane]
        vmovdqa64       xmm3, [%%STATE + _docsis_crc_args_init + %%good_lane]
        vmovdqa64       xmm0, [%%STATE + _aes_lens]

%assign I 0
%rep 8
        cmp             qword [%%STATE + _aes_job_in_lane + I*8], 0
        jne             APPEND(%%_skip_,I)
        mov             [%%STATE + _aes_args_in + I*8], %%tmp1
        mov             [%%STATE + _aes_args_out + I*8], %%tmp2
        mov             [%%STATE + _aes_args_keys + I*8], %%tmp3
        mov             [%%STATE + _docsis_crc_args_len + I*2], WORD(%%GT6)
        mov             [%%STATE + _docsis_crc_args_done + I], BYTE(%%GT7)
        vmovdqa64       [%%STATE + _aes_args_IV + I*16], xmm2
        vmovdqa64       [%%STATE + _docsis_crc_args_init + I*16], xmm3
        vporq           xmm0, xmm0, [rel len_masks + 16*I]
APPEND(%%_skip_,I):
%assign I (I+1)
%endrep

%endif  ;; SUBMIT / FLUSH

%%_find_min_job:
        ;; Find min length (xmm0 includes vector of 8 lengths)
        ;; vmovdqa64       xmm0, [%%STATE + _aes_lens] => not needed xmm0 already loaded with lengths
        vphminposuw     xmm1, xmm0
        vpextrw         DWORD(%%len2), xmm1, 0  ; min value
        vpextrw         DWORD(%%idx), xmm1, 1   ; min index (0...7)
        cmp             %%len2, 0
        je              %%_len_is_0

        vpshufb         xmm1, xmm1, [rel dupw]   ; duplicate words across all lanes
        vpsubw          xmm0, xmm0, xmm1
        vmovdqa64       [%%STATE + _aes_lens], xmm0

        mov             [rsp + _idx], %%idx

        AES_CBC_ENC_CRC32_PARALLEL %%STATE, %%len2, \
                        %%GT0, %%GT1, %%GT2, %%GT3, %%GT4, %%GT5, %%GT6, \
                        %%GT7, %%GT8, %%GT9, %%GT10, %%GT11, %%GT12, \
                        %%ZT0,  %%ZT1,  %%ZT2,  %%ZT3,  %%ZT4,  %%ZT5,  %%ZT6,  %%ZT7, \
                        %%ZT8,  %%ZT9,  %%ZT10, %%ZT11, %%ZT12, %%ZT13, %%ZT14, %%ZT15, \
                        %%ZT16, %%ZT17, %%ZT18, %%ZT19, %%ZT20, %%ZT21, %%ZT22, %%ZT23, \
                        %%ZT24, %%ZT25, %%ZT26, %%ZT27, %%ZT28, %%ZT29, %%ZT30, %%ZT31, \
                        %%NROUNDS

        mov             %%idx, [rsp + _idx]

%%_len_is_0:
        mov             %%job_rax, [%%STATE + _aes_job_in_lane + %%idx*8]

        ;; CRC the remaining bytes
        cmp             byte [%%STATE + _docsis_crc_args_done + %%idx], CRC_LANE_STATE_DONE
        je              %%_crc_is_complete

        ;; some bytes left to complete CRC
        movzx           %%GT3, word [%%STATE + _docsis_crc_args_len + %%idx*2]
        mov             %%GT4, [%%STATE + _aes_args_in + %%idx*8]

        or              %%GT3, %%GT3
        jz              %%_crc_read_reduce

        shl             %%idx, 1
        vmovdqa64       xmm8, [%%STATE + _docsis_crc_args_init + %%idx*8]
        shr             %%idx, 1

        lea             %%GT5, [rel pshufb_shf_table]
        vmovdqu64       xmm10, [%%GT5 + %%GT3]
        vmovdqu64       xmm9, [%%GT4 - 16 + %%GT3]
        vmovdqa64       xmm11, xmm8
        vpshufb         xmm8, xmm10  ; top num_bytes with LSB xcrc
        vpxorq          xmm10, [rel mask3]
        vpshufb         xmm11, xmm10 ; bottom (16 - num_bytes) with MSB xcrc

        ;; data num_bytes (top) blended with MSB bytes of CRC (bottom)
        vpblendvb       xmm11, xmm9, xmm10

        ;; final CRC calculation
        vmovdqa64       xmm9, [rel rk1]
        CRC_CLMUL       xmm8, xmm9, xmm11, xmm12
        jmp             %%_crc_reduce

;; complete the last block

%%_crc_read_reduce:
        shl             %%idx, 1
        vmovdqa64       xmm8, [%%STATE + _docsis_crc_args_init + %%idx*8]
        shr             %%idx, 1

%%_crc_reduce:
        ;; GT3 - offset in bytes to put the CRC32 value into
        ;; GT4 - src buffer pointer
        ;; xmm8 - current CRC value for reduction
        ;; - write CRC value into SRC buffer for further cipher
        ;; - keep CRC value in init field
        CRC32_REDUCE_128_TO_32 %%GT7, xmm8, xmm9, xmm10, xmm11
        mov             [%%GT4 + %%GT3], DWORD(%%GT7)
        shl             %%idx, 1
        mov             [%%STATE + _docsis_crc_args_init + %%idx*8], DWORD(%%GT7)
        shr             %%idx, 1

%%_crc_is_complete:
        mov             %%GT3, [%%job_rax + _msg_len_to_cipher_in_bytes]
        and             %%GT3, 0xf
        jz              %%_no_partial_block_cipher

        ;; AES128/256-CFB on the partial block
        mov             %%GT4, [%%STATE + _aes_args_in + %%idx*8]
        mov             %%GT5, [%%STATE + _aes_args_out + %%idx*8]
        mov             %%GT6, [%%job_rax + _enc_keys]
        shl             %%idx, 1
        vmovdqa64       xmm2, [%%STATE + _aes_args_IV + %%idx*8]
        shr             %%idx, 1
        lea             %%GT2, [rel byte_len_to_mask_table]
        kmovw           k1, [%%GT2 + %%GT3*2]
        vmovdqu8        xmm3{k1}{z}, [%%GT4]
        vpxorq          xmm1, xmm2, [%%GT6 + 0*16]
%assign i 1
%rep %%NROUNDS
        vaesenc         xmm1, [%%GT6 + i*16]
%assign i (i + 1)
%endrep
        vaesenclast     xmm1, [%%GT6 + i*16]
        vpxorq          xmm1, xmm1, xmm3
        vmovdqu8        [%%GT5]{k1}, xmm1

%%_no_partial_block_cipher:
        ;;  - copy CRC value into auth tag
        ;; - process completed job "idx"
        shl             %%idx, 1
        mov             DWORD(%%GT7), [%%STATE + _docsis_crc_args_init + %%idx*8]
        shr             %%idx, 1
        mov             %%GT6, [%%job_rax + _auth_tag_output]
        mov             [%%GT6], DWORD(%%GT7)

        mov             %%unused_lanes, [%%STATE + _aes_unused_lanes]
        mov             qword [%%STATE + _aes_job_in_lane + %%idx*8], 0
        or              dword [%%job_rax + _status], IMB_STATUS_COMPLETED_CIPHER
        shl             %%unused_lanes, 4
        or              %%unused_lanes, %%idx
        mov             [%%STATE + _aes_unused_lanes], %%unused_lanes

%ifdef SAFE_DATA
%ifidn %%SUBMIT_FLUSH, submit
        ;; Clear IV
        vpxor           xmm0, xmm0
        shl             %%idx, 3 ; multiply by 8
        mov             qword [%%STATE + _aes_args_keys + %%idx], 0
        vmovdqa         [%%STATE + _docsis_crc_args_init + %%idx*2], xmm0
%else
        ;; Clear IVs of returned job and "NULL lanes"
        vpxor   xmm0, xmm0
%assign I 0
%rep 8
        cmp             qword [%%STATE + _aes_job_in_lane + I*8], 0
        jne             APPEND(%%_skip_clear_,I)
        mov             qword [%%STATE + _aes_args_keys + I*8], 0
        vmovdqa         [%%STATE + _docsis_crc_args_init + I*16], xmm0
APPEND(%%_skip_clear_,I):
%assign I (I+1)
%endrep
%endif  ;; SUBMIT / FLUSH
%endif  ;; SAFE_DATA

%%_return:

%endmacro

;; ===========================================================================
;; JOB* SUBMIT_JOB_DOCSIS128_SEC_CRC_ENC(MB_MGR_AES_OOO *state, IMB_JOB *job)
;; arg 1 : state
;; arg 2 : job

align 64
MKGLOBAL(submit_job_aes_docsis128_enc_crc32_avx512,function,internal)
submit_job_aes_docsis128_enc_crc32_avx512:
        endbranch64
        FUNC_ENTRY

        SUBMIT_FLUSH_DOCSIS_CRC32 arg1, arg2, \
                        TMP0,  TMP1,  TMP2,  TMP3,  TMP4,  TMP5,  TMP6, \
                        TMP7,  TMP8,  TMP9,  TMP10, TMP11, TMP12, \
                        zmm0,  zmm1,  zmm2,  zmm3,  zmm4,  zmm5,  zmm6,  zmm7, \
                        zmm8,  zmm9,  zmm10, zmm11, zmm12, zmm13, zmm14, zmm15, \
                        zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                        zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                        submit, 9
        FUNC_EXIT
        ret

;; ===========================================================================
;; JOB* SUBMIT_JOB_DOCSIS256_SEC_CRC_ENC(MB_MGR_AES_OOO *state, IMB_JOB *job)
;; arg 1 : state
;; arg 2 : job

align 64
MKGLOBAL(submit_job_aes_docsis256_enc_crc32_avx512,function,internal)
submit_job_aes_docsis256_enc_crc32_avx512:
        endbranch64
        FUNC_ENTRY

        SUBMIT_FLUSH_DOCSIS_CRC32 arg1, arg2, \
                        TMP0,  TMP1,  TMP2,  TMP3,  TMP4,  TMP5,  TMP6, \
                        TMP7,  TMP8,  TMP9,  TMP10, TMP11, TMP12, \
                        zmm0,  zmm1,  zmm2,  zmm3,  zmm4,  zmm5,  zmm6,  zmm7, \
                        zmm8,  zmm9,  zmm10, zmm11, zmm12, zmm13, zmm14, zmm15, \
                        zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                        zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                        submit, 13
        FUNC_EXIT
        ret

;; =====================================================================
;; JOB* FLUSH128(MB_MGR_AES_OOO *state)
;; arg 1 : state
align 64
MKGLOBAL(flush_job_aes_docsis128_enc_crc32_avx512,function,internal)
flush_job_aes_docsis128_enc_crc32_avx512:
        endbranch64
        FUNC_ENTRY

        SUBMIT_FLUSH_DOCSIS_CRC32 arg1, arg2, \
                        TMP0,  TMP1,  TMP2,  TMP3,  TMP4,  TMP5,  TMP6, \
                        TMP7,  TMP8,  TMP9,  TMP10, TMP11, TMP12, \
                        zmm0,  zmm1,  zmm2,  zmm3,  zmm4,  zmm5,  zmm6,  zmm7, \
                        zmm8,  zmm9,  zmm10, zmm11, zmm12, zmm13, zmm14, zmm15, \
                        zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                        zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                        flush, 9
        FUNC_EXIT
        ret

;; =====================================================================
;; JOB* FLUSH256(MB_MGR_AES_OOO *state)
;; arg 1 : state
align 64
MKGLOBAL(flush_job_aes_docsis256_enc_crc32_avx512,function,internal)
flush_job_aes_docsis256_enc_crc32_avx512:
        endbranch64
        FUNC_ENTRY

        SUBMIT_FLUSH_DOCSIS_CRC32 arg1, arg2, \
                        TMP0,  TMP1,  TMP2,  TMP3,  TMP4,  TMP5,  TMP6, \
                        TMP7,  TMP8,  TMP9,  TMP10, TMP11, TMP12, \
                        zmm0,  zmm1,  zmm2,  zmm3,  zmm4,  zmm5,  zmm6,  zmm7, \
                        zmm8,  zmm9,  zmm10, zmm11, zmm12, zmm13, zmm14, zmm15, \
                        zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                        zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                        flush, 13

        FUNC_EXIT
        ret

mksection stack-noexec

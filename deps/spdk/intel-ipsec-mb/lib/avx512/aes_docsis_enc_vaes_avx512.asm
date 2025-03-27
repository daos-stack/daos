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

;;; DOCSIS SEC BPI (AES128/256-CBC + AES128/256-CFB) encryption
;;; stitched together with CRC32

%use smartalign

%include "include/os.asm"
%include "include/imb_job.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/reg_sizes.asm"
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

align  16
crc_state_to_start_x16:
%rep 8
        db CRC_LANE_STATE_TO_START, CRC_LANE_STATE_TO_START
%endrep

align  16
crc_state_in_progress_x16:
%rep 8
        db CRC_LANE_STATE_IN_PROGRESS, CRC_LANE_STATE_IN_PROGRESS
%endrep

align  16
crc_state_done_x16:
%rep 8
        db CRC_LANE_STATE_DONE, CRC_LANE_STATE_DONE
%endrep

align  32
dw_16_x16:
%rep 4
        dw 16, 16, 16, 16
%endrep

align  32
dw_15_x16:
%rep 4
        dw 15, 15, 15, 15
%endrep

align 16
map_4bits_to_8bits:
        db 0000_0000b, 0000_0011b, 0000_1100b, 0000_1111b
        db 0011_0000b, 0011_0011b, 0011_1100b, 0011_1111b
        db 1100_0000b, 1100_0011b, 1100_1100b, 1100_1111b
        db 1111_0000b, 1111_0011b, 1111_1100b, 1111_1111b

align 16
map_index_0_15_to_mask:
        dw (1 << 0),  (1 << 1),  (1 << 2),  (1 << 3)
        dw (1 << 4),  (1 << 5),  (1 << 6),  (1 << 7)
        dw (1 << 8),  (1 << 9),  (1 << 10), (1 << 11)
        dw (1 << 12), (1 << 13), (1 << 14), (1 << 15)

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
        ;; Values for shifting XMM registers with the pshufb instruction.
        ;; Some usages of this table:
        ;; - pshufb with pattern from [pshufb_shf_table + <index>]
        ;;   results with shifting <index> bytes from LSB to MSB
        ;; - pshufb with pattern from [pshufb_shf_table pointer + 16 - <index]
        ;;   results in shifting XMM left by <index> bytes
        ;; - pshufb with pattern from [pshufb_shf_table + <index>] xor'ed with 0x80 pattern
        ;;   results with shifting XMM right by <index> bytes
        dq 0x8786858483828180, 0x8f8e8d8c8b8a8988
        dq 0x0706050403020100, 0x0f0e0d0c0b0a0908

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

;;; partial block read/write bytes mask table
;;; - 1's start from bit 0
align 64
byte_len_to_mask_table:
        dw      0x0000, 0x0001, 0x0003, 0x0007
        dw      0x000f, 0x001f, 0x003f, 0x007f
        dw      0x00ff, 0x01ff, 0x03ff, 0x07ff
        dw      0x0fff, 0x1fff, 0x3fff, 0x7fff
        dw      0xffff, 0xffff, 0xffff, 0xffff
        dw      0xffff, 0xffff, 0xffff, 0xffff

;;; partial block read/write bytes reflected mask table
;;; - 1's start from bit 15
align 64
byte_len_to_mask_ref_table:
        dw      0x0000, 0x8000, 0xc000, 0xe000
        dw      0xf000, 0xf800, 0xfc00, 0xfe00
        dw      0xff00, 0xff80, 0xffc0, 0xffe0
        dw      0xfff0, 0xfff8, 0xfffc, 0xfffe
        dw      0xffff, 0xffff, 0xffff, 0xffff
        dw      0xffff, 0xffff, 0xffff, 0xffff

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
%else
        vzeroupper
%endif ;; SAFE_DATA

%endmacro

;; =====================================================================
;; =====================================================================
;; CRC32 FIRST computation round
;; =====================================================================
%macro CRC32_ROUND_FIRST 14
%define %%ARG           %1      ; [in] pointer to OOO structure / arguments
%define %%ZDATA0        %2      ; [in] ZMM with block of data for CRC (lanes 0 to 3)
%define %%ZDATA1        %3      ; [in] ZMM with block of data for CRC (lanes 4 to 7)
%define %%ZDATA2        %4      ; [in] ZMM with block of data for CRC (lanes 8 to 11)
%define %%ZDATA3        %5      ; [in] ZMM with block of data for CRC (lanes 12 to 15)
%define %%ZCRCS0        %6      ; [in/out] ZMM with CRC sums (lanes 0 to 3)
%define %%ZCRCS1        %7      ; [in/out] ZMM with CRC sums (lanes 4 to 7)
%define %%ZCRCS2        %8      ; [in/out] ZMM with CRC sums (lanes 8 to 11)
%define %%ZCRCS3        %9      ; [in/out] ZMM with CRC sums (lanes 12 to 15)
%define %%tmp1          %10     ; [clobbered] temporary GPR
%define %%tmp2          %11     ; [clobbered] temporary GPR
%define %%tmp3          %12     ; [clobbered] temporary GPR
%define %%ZT0           %13     ; [clobbered] temporary ZMM
%define %%KREG          %14     ; [out] k register to put mask into (k5 to k7)

        vmovdqa64       XWORD(%%ZT0), [%%ARG + _docsis_crc_args_done]
        vpcmpeqb        %%KREG, XWORD(%%ZT0), [rel crc_state_to_start_x16]
        ktestw          %%KREG, %%KREG
        jz              %%_crc32_round_first_done

        kmovw           DWORD(%%tmp3), %%KREG
        lea             %%tmp1, [rel map_4bits_to_8bits]

        ;; - get 4 x 8-bit masks for null lanes out of 16-bit mask
        ;; - update CRC sums

        mov             DWORD(%%tmp2), DWORD(%%tmp3)
        and             DWORD(%%tmp2), 15
        shr             DWORD(%%tmp3), 4
        kmovb           k1, [%%tmp1 + %%tmp2]
        vpxorq          %%ZCRCS0{k1}, %%ZCRCS0, %%ZDATA0

        mov             DWORD(%%tmp2), DWORD(%%tmp3)
        and             DWORD(%%tmp2), 15
        shr             DWORD(%%tmp3), 4
        kmovb           k2, [%%tmp1 + %%tmp2]
        vpxorq          %%ZCRCS1{k2}, %%ZCRCS1, %%ZDATA1

        mov             DWORD(%%tmp2), DWORD(%%tmp3)
        and             DWORD(%%tmp2), 15
        shr             DWORD(%%tmp3), 4
        kmovb           k3, [%%tmp1 + %%tmp2]
        vpxorq          %%ZCRCS2{k3}, %%ZCRCS2, %%ZDATA2

        and             DWORD(%%tmp3), 15
        kmovb           k4, [%%tmp1 + %%tmp3]
        vpxorq          %%ZCRCS3{k4}, %%ZCRCS3, %%ZDATA3

%%_crc32_round_first_done:
%endmacro       ; CRC32_ROUND_FIRST

;; =====================================================================
;; =====================================================================
;; CRC32 UPDATE computation round (more than 15 bytes left in the buffer)
;; =====================================================================
%macro CRC32_ROUND_UPDATE 17
%define %%ARG           %1      ; [in] pointer to OOO manager
%define %%ZDATA0        %2      ; [in] ZMM with block of data for CRC (lanes 0 to 3)
%define %%ZDATA1        %3      ; [in] ZMM with block of data for CRC (lanes 4 to 7)
%define %%ZDATA2        %4      ; [in] ZMM with block of data for CRC (lanes 8 to 11)
%define %%ZDATA3        %5      ; [in] ZMM with block of data for CRC (lanes 12 to 15)
%define %%ZCRCS0        %6      ; [in/out] ZMM with CRC sums (lanes 0 to 3)
%define %%ZCRCS1        %7      ; [in/out] ZMM with CRC sums (lanes 4 to 7)
%define %%ZCRCS2        %8      ; [in/out] ZMM with CRC sums (lanes 8 to 11)
%define %%ZCRCS3        %9      ; [in/out] ZMM with CRC sums (lanes 12 to 15)
%define %%KREG1         %10     ; [out] k register to put 'update' mask into (k5 to k7)
%define %%KREG2         %11     ; [out] k register to put 'last' mask into (k5 to k7)
%define %%ZCRC_MUL      %12     ; [in] ZMM with CRC fold constant
%define %%tmp1          %13     ; [clobbered] temporary GPR
%define %%tmp2          %14     ; [clobbered] temporary GPR
%define %%tmp3          %15     ; [clobbered] temporary GPR
%define %%ZT0           %16     ; [clobbered] temporary ZMM
%define %%ZT1           %17     ; [clobbered] temporary ZMM

        vmovdqa64       XWORD(%%ZT0), [%%ARG + _docsis_crc_args_done]
        vmovdqu64       YWORD(%%ZT1), [%%ARG + _docsis_crc_args_len]
        vpcmpeqb        %%KREG2, XWORD(%%ZT0), [rel crc_state_in_progress_x16]
        vpcmpgtw        %%KREG1, YWORD(%%ZT1), [rel dw_15_x16]
        kandw           %%KREG1, %%KREG1, %%KREG2       ;; KREG1 = in_progress && >= 16
        kxorw           %%KREG2, %%KREG2, %%KREG1       ;; KREG2 = in_progress && < 16
        ktestw          %%KREG1, %%KREG1
        jz              %%_crc32_round_update_done

        lea             %%tmp1, [rel map_4bits_to_8bits]
        kmovw           DWORD(%%tmp3), %%KREG1

        ;; get 4 x 8-bit masks for null lanes out of 16-bit mask
        mov             DWORD(%%tmp2), DWORD(%%tmp3)
        shr             DWORD(%%tmp3), 4
        and             DWORD(%%tmp2), 15
        jz              %%_crc_update_nibble1
        kmovb           k1, [%%tmp1 + %%tmp2]

        vpclmulqdq      %%ZT0, %%ZCRCS0, %%ZCRC_MUL, 0x01
        vpclmulqdq      %%ZT1, %%ZCRCS0, %%ZCRC_MUL, 0x10
        vpternlogq      %%ZT1, %%ZT0, %%ZDATA0, 0x96
        vmovdqa64       %%ZCRCS0{k1}, %%ZT1

%%_crc_update_nibble1:
        mov             DWORD(%%tmp2), DWORD(%%tmp3)
        shr             DWORD(%%tmp3), 4
        and             DWORD(%%tmp2), 15
        jz              %%_crc_update_nibble2
        kmovb           k2, [%%tmp1 + %%tmp2]

        vpclmulqdq      %%ZT0, %%ZCRCS1, %%ZCRC_MUL, 0x01
        vpclmulqdq      %%ZT1, %%ZCRCS1, %%ZCRC_MUL, 0x10
        vpternlogq      %%ZT1, %%ZT0, %%ZDATA1, 0x96
        vmovdqa64       %%ZCRCS1{k2}, %%ZT1

%%_crc_update_nibble2:
        mov             DWORD(%%tmp2), DWORD(%%tmp3)
        shr             DWORD(%%tmp3), 4
        and             DWORD(%%tmp2), 15
        jz              %%_crc_update_nibble3
        kmovb           k3, [%%tmp1 + %%tmp2]

        vpclmulqdq      %%ZT0, %%ZCRCS2, %%ZCRC_MUL, 0x01
        vpclmulqdq      %%ZT1, %%ZCRCS2, %%ZCRC_MUL, 0x10
        vpternlogq      %%ZT1, %%ZT0, %%ZDATA2, 0x96
        vmovdqa64       %%ZCRCS2{k3}, %%ZT1

%%_crc_update_nibble3:
        and             DWORD(%%tmp3), 15
        jz              %%_crc_update_nibble4
        kmovb           k4, [%%tmp1 + %%tmp3]

        vpclmulqdq      %%ZT0, %%ZCRCS3, %%ZCRC_MUL, 0x01
        vpclmulqdq      %%ZT1, %%ZCRCS3, %%ZCRC_MUL, 0x10
        vpternlogq      %%ZT1, %%ZT0, %%ZDATA3, 0x96
        vmovdqa64       %%ZCRCS3{k4}, %%ZT1
%%_crc_update_nibble4:

%%_crc32_round_update_done:
%endmacro       ; CRC32_ROUND_UPDATE

;; =====================================================================
;; =====================================================================
;; CRC32 computation round (last partial block)
;; =====================================================================
%macro CRC32_ROUND_LAST 24
%define %%ARG           %1      ; [in] GP with pointer to OOO manager / arguments
%define %%IDX           %2      ; [in] GP with data offset (last partial only)
%define %%OFFS          %3      ; [in] numerical offset (last partial only)
%define %%LANEID        %4      ; [in] numerical value with lane id
%define %%ZDATA0        %5      ; [in/out] ZMM with block of data for lanes 0 - 3
%define %%ZDATA1        %6      ; [in/out] ZMM with block of data for lanes 4 - 7
%define %%ZDATA2        %7      ; [in/out] ZMM with block of data for lanes 8 - 11
%define %%ZDATA3        %8      ; [in/out] ZMM with block of data for lanes 12 - 15
%define %%ZCRCS0        %9      ; [in/out] ZMM with CRC sum for lanes 0 - 3
%define %%ZCRCS1        %10     ; [in/out] ZMM with CRC sum for lanes 4 - 7
%define %%ZCRCS2        %11     ; [in/out] ZMM with CRC sum for lanes 8 - 11
%define %%ZCRCS3        %12     ; [in/out] ZMM with CRC sum for lanes 12 - 15
%define %%ZCRC_MUL      %13     ; [in] ZMM with CRC fold constant
%define %%ZT0           %14     ; [clobbered] temporary ZMM
%define %%ZT1           %15     ; [clobbered] temporary ZMM
%define %%ZT2           %16     ; [clobbered] temporary ZMM
%define %%ZT3           %17     ; [clobbered] temporary ZMM
%define %%ZT4           %18     ; [clobbered] temporary ZMM
%define %%ZT5           %19     ; [clobbered] temporary ZMM
%define %%GT0           %20     ; [clobbered] temporary GPR
%define %%GT1           %21     ; [clobbered] temporary GPR
%define %%GT2           %22     ; [clobbered] temporary GPR
%define %%GT3           %23     ; [clobbered] temporary GPR
%define %%KREG          %24     ; [clobbered] K register

%xdefine %%IN           %%GT0

%xdefine %%XCRCS        XWORD(%%ZT0)
%xdefine %%XDATA        XWORD(%%ZT1)

        ;; bit test on k-register was slower than these 2 x cmp
        ;; perhaps this is something that could be explored in the future
        cmp             byte [%%ARG + _docsis_crc_args_done + %%LANEID], CRC_LANE_STATE_IN_PROGRESS
        jne             %%_crc_lane_done

        cmp             word [%%ARG + _docsis_crc_args_len + 2*%%LANEID], 16
        jae             %%_crc_lane_done

%if %%LANEID == 0
        vmovdqa64       %%XCRCS, XWORD(%%ZCRCS0)
%elif %%LANEID < 4
        vextracti32x4   %%XCRCS, %%ZCRCS0, %%LANEID
%elif %%LANEID == 4
        vmovdqa64       %%XCRCS, XWORD(%%ZCRCS1)
%elif %%LANEID < 8
        vextracti32x4   %%XCRCS, %%ZCRCS1, %%LANEID - 4
%elif %%LANEID == 8
        vmovdqa64       %%XCRCS, XWORD(%%ZCRCS2)
%elif %%LANEID < 12
        vextracti32x4   %%XCRCS, %%ZCRCS2, %%LANEID - 8
%elif %%LANEID == 12
        vmovdqa64       %%XCRCS, XWORD(%%ZCRCS3)
%else
        vextracti32x4   %%XCRCS, %%ZCRCS3, %%LANEID - 12
%endif

        ;; Partial block case (the last block)
        ;; - last CRC round is specific
        ;; - followed by CRC reduction and write back of the CRC
        ;; - then construction of plain text block for encryption

        movzx           DWORD(%%GT2), word [%%ARG + _docsis_crc_args_len + %%LANEID*2]
        ;; GT2 = bytes_to_crc
        mov             %%IN, [%%ARG + _aesarg_in + PTR_SZ * %%LANEID]
        lea             %%IN, [%%IN + %%IDX + %%OFFS]

        lea             %%GT1, [rel byte_len_to_mask_ref_table]
        kmovw           %%KREG, [%%GT1 + %%GT2*2]
        vmovdqu8        XWORD(%%ZT3){%%KREG}{z}, [%%IN + %%GT2 - 16]
        ;; XWORD(%%ZT3) = partial bytes of plain text block shifted and aligned to MSB

        lea             %%GT1, [rel byte_len_to_mask_table]
        kmovw           %%KREG, [%%GT1 + %%GT2*2]
        vmovdqu8        %%XDATA{%%KREG}{z}, [%%IN]
        ;; XDATA = partial bytes of plain text block aligned to LSB

        lea             %%GT1, [rel pshufb_shf_table]
        vmovdqu64       XWORD(%%ZT4), [%%GT1 + %%GT2]
        vpshufb         XWORD(%%ZT2), %%XCRCS, XWORD(%%ZT4)
        ;; XWORD(%%ZT2) register contents at this stage:
        ;; MS byte [ < LSB bytes of CRC sum : size =  bytes_to_crc> |
        ;;           < zero padding : size = (16 - bytes_to_crc>) ] LS byte
        vpxorq          XWORD(%%ZT4), [rel mask3]
        vpshufb         XWORD(%%ZT5), %%XCRCS, XWORD(%%ZT4)
        ;; XWORD(%%ZT5) register contents at this stage:
        ;; MS byte [ < zero padding : size =  bytes_to_crc> |
        ;;           < MSB bytes of CRC sum : size = (16 - bytes_to_crc>) ] LS byte

        ;; XWORD(%%ZT3) register content after the OR operation:
        ;; MS byte [ < plain text : size = bytes_to_crc > |
        ;;           < MSB of CRC sum : size = (16 - bytes_to_crc > ] LS byte
        vporq           XWORD(%%ZT3), XWORD(%%ZT3), XWORD(%%ZT5)

        CRC_CLMUL       XWORD(%%ZT2), XWORD(%%ZCRC_MUL), XWORD(%%ZT3), XWORD(%%ZT4)
        CRC32_REDUCE_128_TO_32 %%GT3, XWORD(%%ZT2), XWORD(%%ZT4), XWORD(%%ZT3), XWORD(%%ZT5)

        ;; save final CRC value in init
        vmovd           %%XCRCS, DWORD(%%GT3)

        ;; write back CRC value into source buffer
        mov             [%%IN + %%GT2], DWORD(%%GT3)

        ;; combine already loaded plain text with computed CRC  for cipher operation
        ;; - shift left CRC value in XCRCS by bytes_to_crc number of bytes
        sub             %%GT1, %%GT2
        vmovdqu64       XWORD(%%ZT4), [%%GT1 + 16]
        vpshufb         XWORD(%%ZT5), %%XCRCS, XWORD(%%ZT4)
        vporq           %%XDATA, %%XDATA, XWORD(%%ZT5)

        ;; - put updated data block into adequate ZMM for encryption
        ;; - put computed CRC value into current CRC sum too
%if %%LANEID < 4
        vinserti32x4    %%ZDATA0, %%XDATA, %%LANEID
        vinserti32x4    %%ZCRCS0, %%XCRCS, %%LANEID
%elif %%LANEID < 8
        vinserti32x4    %%ZDATA1, %%XDATA, %%LANEID - 4
        vinserti32x4    %%ZCRCS1, %%XCRCS, %%LANEID - 4
%elif %%LANEID < 12
        vinserti32x4    %%ZDATA2, %%XDATA, %%LANEID - 8
        vinserti32x4    %%ZCRCS2, %%XCRCS, %%LANEID - 8
%else
        vinserti32x4    %%ZDATA3, %%XDATA, %%LANEID - 12
        vinserti32x4    %%ZCRCS3, %%XCRCS, %%LANEID - 12
%endif

%%_crc_lane_done:
%endmacro       ; CRC32_ROUND_LAST

;; =====================================================================
;; =====================================================================
;; Transforms and inserts AES expanded keys into OOO data structure
;; =====================================================================
%macro INSERT_KEYS 7
%define %%ARG     %1 ; [in] pointer to OOO structure
%define %%KP      %2 ; [in] GP reg with pointer to expanded keys
%define %%LANE    %3 ; [in] GP reg with lane number
%define %%NROUNDS %4 ; [in] number of round keys (numerical value)
%define %%COL     %5 ; [clobbered] GP reg
%define %%ZTMP    %6 ; [clobbered] ZMM reg
%define %%IA0     %7 ; [clobbered] GP reg

%assign ROW (16*16)

        mov             %%COL, %%LANE
        shl             %%COL, 4
        lea             %%IA0, [%%ARG + _aes_args_key_tab]
        add             %%COL, %%IA0

        vmovdqu64       %%ZTMP, [%%KP + (0 * 16)]
        vmovdqu64       [%%COL + ROW*0], XWORD(%%ZTMP)
        vextracti64x2   [%%COL + ROW*1], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*2], %%ZTMP, 2
        vextracti64x2   [%%COL + ROW*3], %%ZTMP, 3

        vmovdqu64       %%ZTMP, [%%KP + (4 * 16)]
        vmovdqu64       [%%COL + ROW*4], XWORD(%%ZTMP)
        vextracti64x2   [%%COL + ROW*5], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*6], %%ZTMP, 2
        vextracti64x2   [%%COL + ROW*7], %%ZTMP, 3

%if %%NROUNDS == 9
        ;; 128-bit key (11 keys)
        vmovdqu64       YWORD(%%ZTMP), [%%KP + (8 * 16)]
        vmovdqu64       [%%COL + ROW*8], XWORD(%%ZTMP)
        vextracti64x2   [%%COL + ROW*9], YWORD(%%ZTMP), 1
        vmovdqu64       XWORD(%%ZTMP), [%%KP + (10 * 16)]
        vmovdqu64       [%%COL + ROW*10], XWORD(%%ZTMP)
%else
        ;; 192-bit key or 256-bit key (13 and 15 keys)
        vmovdqu64       %%ZTMP, [%%KP + (8 * 16)]
        vmovdqu64       [%%COL + ROW*8], XWORD(%%ZTMP)
        vextracti64x2   [%%COL + ROW*9], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*10], %%ZTMP, 2
        vextracti64x2   [%%COL + ROW*11], %%ZTMP, 3

%if %%NROUNDS == 11
        ;; 192-bit key (13 keys)
        vmovdqu64       XWORD(%%ZTMP), [%%KP + (12 * 16)]
        vmovdqu64       [%%COL + ROW*12], XWORD(%%ZTMP)
%else
        ;; 256-bit key (15 keys)
        vmovdqu64       YWORD(%%ZTMP), [%%KP + (12 * 16)]
        vmovdqu64       [%%COL + ROW*12], XWORD(%%ZTMP)
        vextracti64x2   [%%COL + ROW*13], YWORD(%%ZTMP), 1
        vmovdqu64       XWORD(%%ZTMP), [%%KP + (14 * 16)]
        vmovdqu64       [%%COL + ROW*14], XWORD(%%ZTMP)
%endif
%endif

%endmacro

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

%xdefine %%INP0 %%GT0
%xdefine %%INP1 %%GT1
%xdefine %%INP2 %%GT2
%xdefine %%INP3 %%GT3
%xdefine %%INP4 %%GT4
%xdefine %%INP5 %%GT5
%xdefine %%INP6 %%GT6
%xdefine %%INP7 %%GT7

;; GT8 - GT11 used as temporary registers
%define %%IDX   %%GT12

;; used for IV and AES rounds
%xdefine %%ZCIPH0 %%ZT0
%xdefine %%ZCIPH1 %%ZT1
%xdefine %%ZCIPH2 %%ZT2
%xdefine %%ZCIPH3 %%ZT3

%xdefine %%XCIPH0 XWORD(%%ZCIPH0)
%xdefine %%XCIPH1 XWORD(%%ZCIPH1)
%xdefine %%XCIPH2 XWORD(%%ZCIPH2)
%xdefine %%XCIPH3 XWORD(%%ZCIPH3)

;; used for per lane CRC multiply
%xdefine %%ZCRC_MUL %%ZT4

;; used for loading plain text
%xdefine %%ZDATA0 %%ZT9
%xdefine %%ZDATA1 %%ZT10
%xdefine %%ZDATA2 %%ZT11
%xdefine %%ZDATA3 %%ZT12

%xdefine %%XDATA0 XWORD(%%ZDATA0)
%xdefine %%XDATA1 XWORD(%%ZDATA1)
%xdefine %%XDATA2 XWORD(%%ZDATA2)
%xdefine %%XDATA3 XWORD(%%ZDATA3)

;; used for current CRC sums
%xdefine %%ZDATB0 %%ZT13
%xdefine %%ZDATB1 %%ZT14
%xdefine %%ZDATB2 %%ZT15
%xdefine %%ZDATB3 %%ZT16

%xdefine %%XDATB0 XWORD(%%ZDATB0)
%xdefine %%XDATB1 XWORD(%%ZDATB1)
%xdefine %%XDATB2 XWORD(%%ZDATB2)
%xdefine %%XDATB3 XWORD(%%ZDATB3)

;; ZT17 to ZT20 used as temporary registers
;; ZT5 to ZT8 used as temporary registers

;; ZT21 to ZT31 used to preload keys
%xdefine %%KEYSET0ARK   %%ZT21
%xdefine %%KEYSET1ARK   %%ZT22
%xdefine %%KEYSET2ARK   %%ZT23
%xdefine %%KEYSET3ARK   %%ZT24

%xdefine %%KEYSET0LAST  %%ZT25
%xdefine %%KEYSET1LAST  %%ZT26
%xdefine %%KEYSET2LAST  %%ZT27
%xdefine %%KEYSET3LAST  %%ZT28

        xor             %%IDX, %%IDX

        ;; broadcast CRC multiplier
        vbroadcasti32x4 %%ZCRC_MUL, [rel rk1]

        ;; load IV's
        vmovdqa64       %%ZCIPH0, [%%ARG + _aesarg_IV + 16*0]
        vmovdqa64       %%ZCIPH1, [%%ARG + _aesarg_IV + 16*4]
        vmovdqa64       %%ZCIPH2, [%%ARG + _aesarg_IV + 16*8]
        vmovdqa64       %%ZCIPH3, [%%ARG + _aesarg_IV + 16*12]

        ;; pre-load ARK keys
        vmovdqa64       %%KEYSET0ARK, [%%ARG + _aesarg_key_tab + (16 * 0)]
        vmovdqa64       %%KEYSET1ARK, [%%ARG + _aesarg_key_tab + (16 * 4)]
        vmovdqa64       %%KEYSET2ARK, [%%ARG + _aesarg_key_tab + (16 * 8)]
        vmovdqa64       %%KEYSET3ARK, [%%ARG + _aesarg_key_tab + (16 * 12)]

        ;; pre-load last round keys
%assign key_offset ((%%NROUNDS + 1) * (16 * 16))
        vmovdqa64       %%KEYSET0LAST, [%%ARG + _aesarg_key_tab + key_offset + (16 * 0)]
        vmovdqa64       %%KEYSET1LAST, [%%ARG + _aesarg_key_tab + key_offset + (16 * 4)]
        vmovdqa64       %%KEYSET2LAST, [%%ARG + _aesarg_key_tab + key_offset + (16 * 8)]
        vmovdqa64       %%KEYSET3LAST, [%%ARG + _aesarg_key_tab + key_offset + (16 * 12)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Pipeline start

        ;; CRC32 rounds on all lanes - first and last cases are possible
        ;; - pre-load 8 input pointers
        ;; - load current CRC sums
        ;; - load plain text blocks
        ;; - do the initial CRC round

        vmovdqa64       %%ZDATB0, [%%ARG + _docsis_crc_args_init + (16 * 0)]
        vmovdqa64       %%ZDATB1, [%%ARG + _docsis_crc_args_init + (16 * 4)]
        vmovdqa64       %%ZDATB2, [%%ARG + _docsis_crc_args_init + (16 * 8)]
        vmovdqa64       %%ZDATB3, [%%ARG + _docsis_crc_args_init + (16 * 12)]

        mov             %%INP0, [%%ARG + _aesarg_in + (PTR_SZ * 0)]
        mov             %%INP1, [%%ARG + _aesarg_in + (PTR_SZ * 1)]
        vmovdqu64       XWORD(%%ZDATA0), [%%INP0 + %%IDX]
        vinserti32x4    %%ZDATA0, [%%INP1 + %%IDX], 1
        mov             %%INP2, [%%ARG + _aesarg_in + (PTR_SZ * 2)]
        mov             %%INP3, [%%ARG + _aesarg_in + (PTR_SZ * 3)]
        vinserti32x4    %%ZDATA0, [%%INP2 + %%IDX], 2
        vinserti32x4    %%ZDATA0, [%%INP3 + %%IDX], 3

        mov             %%INP4, [%%ARG + _aesarg_in + (PTR_SZ * 4)]
        mov             %%INP5, [%%ARG + _aesarg_in + (PTR_SZ * 5)]
        vmovdqu64       XWORD(%%ZDATA1), [%%INP4 + %%IDX]
        vinserti32x4    %%ZDATA1, [%%INP5 + %%IDX], 1
        mov             %%INP6, [%%ARG + _aesarg_in + (PTR_SZ * 6)]
        mov             %%INP7, [%%ARG + _aesarg_in + (PTR_SZ * 7)]
        vinserti32x4    %%ZDATA1, [%%INP6 + %%IDX], 2
        vinserti32x4    %%ZDATA1, [%%INP7 + %%IDX], 3

        mov             %%GT8, [%%ARG + _aesarg_in + (PTR_SZ * 8)]
        mov             %%GT9, [%%ARG + _aesarg_in + (PTR_SZ * 9)]
        vmovdqu64       XWORD(%%ZDATA2), [%%GT8 + %%IDX]
        vinserti32x4    %%ZDATA2, [%%GT9 + %%IDX], 1
        mov             %%GT8, [%%ARG + _aesarg_in + (PTR_SZ * 10)]
        mov             %%GT9, [%%ARG + _aesarg_in + (PTR_SZ * 11)]
        vinserti32x4    %%ZDATA2, [%%GT8 + %%IDX], 2
        vinserti32x4    %%ZDATA2, [%%GT9 + %%IDX], 3

        mov             %%GT8, [%%ARG + _aesarg_in + (PTR_SZ * 12)]
        mov             %%GT9, [%%ARG + _aesarg_in + (PTR_SZ * 13)]
        vmovdqu64       XWORD(%%ZDATA3), [%%GT8 + %%IDX]
        vinserti32x4    %%ZDATA3, [%%GT9 + %%IDX], 1
        mov             %%GT8, [%%ARG + _aesarg_in + (PTR_SZ * 14)]
        mov             %%GT9, [%%ARG + _aesarg_in + (PTR_SZ * 15)]
        vinserti32x4    %%ZDATA3, [%%GT8 + %%IDX], 2
        vinserti32x4    %%ZDATA3, [%%GT9 + %%IDX], 3

        CRC32_ROUND_FIRST %%ARG, \
                        %%ZDATA0, %%ZDATA1, %%ZDATA2, %%ZDATA3, \
                        %%ZDATB0, %%ZDATB1, %%ZDATB2, %%ZDATB3, \
                        %%GT8, %%GT9, %%GT10, %%ZT20, k7

        CRC32_ROUND_UPDATE %%ARG, \
                        %%ZDATA0, %%ZDATA1, %%ZDATA2, %%ZDATA3, \
                        %%ZDATB0, %%ZDATB1, %%ZDATB2, %%ZDATB3, \
                        k5, k6, %%ZCRC_MUL, %%GT8, %%GT9, %%GT10, %%ZT17, %%ZT18

        ktestw          k6, k6
        jz              %%_no_last_crc_blocks

%assign crc_lane 0
%rep 16
        CRC32_ROUND_LAST %%ARG, %%IDX, 0, crc_lane, \
                        %%ZDATA0, %%ZDATA1, %%ZDATA2, %%ZDATA3, \
                        %%ZDATB0, %%ZDATB1, %%ZDATB2, %%ZDATB3, \
                        %%ZCRC_MUL, \
                        %%ZT17, %%ZT18, %%ZT5, %%ZT6, %%ZT7, %%ZT8, \
                        %%GT8, %%GT9, %%GT10, %%GT11, k1
%assign crc_lane (crc_lane + 1)
%endrep

%%_no_last_crc_blocks:

        ;; Update CRC lengths and state
        ;; - subtract 16 from CRC length for first (k7) and update cases (k5)
        ;;   - k5 = k5 | k7 is a mask of lane in_progress now. It will be used later on.
        ;; - zero the length for the last partial block cases (k6)
        ;; - change crc status from first to in_progress (k7)
        ;; - change crc status from in_progress to done (k6)
        korw            k5, k7, k5
        vmovdqu64       YWORD(%%ZT19), [%%ARG + _docsis_crc_args_len]
        vmovdqa64       XWORD(%%ZT20), [%%ARG + _docsis_crc_args_done]
        vpsubw          YWORD(%%ZT19){k6}, YWORD(%%ZT19), YWORD(%%ZT19)
        vpsubw          YWORD(%%ZT19){k5}, YWORD(%%ZT19), [rel dw_16_x16]
        vmovdqu8        XWORD(%%ZT20){k7}, [rel crc_state_in_progress_x16]
        vmovdqu8        XWORD(%%ZT20){k6}, [rel crc_state_done_x16]
        vmovdqu64       [%%ARG + _docsis_crc_args_len], YWORD(%%ZT19)
        vmovdqu64       [%%ARG + _docsis_crc_args_done], XWORD(%%ZT20)

        ;; check if only 16 bytes in this execution
        sub             %%LEN, 16
        je              %%_encrypt_the_last_block

        ;; Status 'done' is 0x00 and 'in-progress' if 0xff.
        ;; This property is leveregaed with use of k-registers.
        ;; k1 => lanes 0 to 3
        ;; k2 => lanes 4 to 7
        ;; k3 => lanes 8 to 11
        ;; k4 => lanes 12 to 15
        vmovq           %%GT8, XWORD(%%ZT20)
        vpextrq         %%GT9, XWORD(%%ZT20), 1
        kmovd           k1, DWORD(%%GT8)
        shr             %%GT8, 32
        kmovd           k2, DWORD(%%GT8)
        kmovd           k3, DWORD(%%GT9)
        shr             %%GT9, 32
        kmovd           k4, DWORD(%%GT9)

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

        vpternlogq      %%ZCIPH0, %%ZDATA0, %%KEYSET0ARK, 0x96
        vpternlogq      %%ZCIPH1, %%ZDATA1, %%KEYSET1ARK, 0x96
        vpternlogq      %%ZCIPH2, %%ZDATA2, %%KEYSET2ARK, 0x96
        vpternlogq      %%ZCIPH3, %%ZDATA3, %%KEYSET3ARK, 0x96

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 1 to NROUNDS (9 or 13)

%assign i 1
%rep %%NROUNDS
%assign key_offset (i * (16 * 16))

        vaesenc         %%ZCIPH0, %%ZCIPH0, [%%ARG + _aesarg_key_tab + key_offset + (16 * 0)]
        vaesenc         %%ZCIPH1, %%ZCIPH1, [%%ARG + _aesarg_key_tab + key_offset + (16 * 4)]
        vaesenc         %%ZCIPH2, %%ZCIPH2, [%%ARG + _aesarg_key_tab + key_offset + (16 * 8)]
        vaesenc         %%ZCIPH3, %%ZCIPH3, [%%ARG + _aesarg_key_tab + key_offset + (16 * 12)]

                ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
                ;; CRC: interleave load of the next block & CRC updates
                ;;      in between AES rounds
%if (i == 1)
                ;; Load one block of data from lanes 0 to 3 in ZDATA0
                vmovdqu64       %%XDATA0, [%%INP0 + %%IDX + 16]
                vinserti32x4    %%ZDATA0, [%%INP1 + %%IDX + 16], 1
                vinserti32x4    %%ZDATA0, [%%INP2 + %%IDX + 16], 2
                vinserti32x4    %%ZDATA0, [%%INP3 + %%IDX + 16], 3
%elif (i == 2)
                ;; CRC update for lanes 0 to 3
                vpclmulqdq      %%ZT19, %%ZDATB0, %%ZCRC_MUL, 0x01
                vpclmulqdq      %%ZT20, %%ZDATB0, %%ZCRC_MUL, 0x10
                vpternlogq      %%ZT20, %%ZT19, %%ZDATA0, 0x96
                vmovdqu16       %%ZDATB0{k1}, %%ZT20
%elif (i == 3)
                ;; Load one block of data from lanes 4 to 7 in ZDATA1
                vmovdqu64       %%XDATA1, [%%INP4 + %%IDX + 16]
                vinserti32x4    %%ZDATA1, [%%INP5 + %%IDX + 16], 1
                vinserti32x4    %%ZDATA1, [%%INP6 + %%IDX + 16], 2
                vinserti32x4    %%ZDATA1, [%%INP7 + %%IDX + 16], 3
%elif (i == 4)
                ;; CRC update for lanes 4 to 7
                vpclmulqdq      %%ZT19, %%ZDATB1, %%ZCRC_MUL, 0x01
                vpclmulqdq      %%ZT20, %%ZDATB1, %%ZCRC_MUL, 0x10
                vpternlogq      %%ZT20, %%ZT19, %%ZDATA1, 0x96
                vmovdqu16       %%ZDATB1{k2}, %%ZT20
%elif (i == 5)
                ;; Load one block of data from lanes 8 to 11 in ZDATA2
                mov             %%GT9, [%%ARG + _aesarg_in + (8 * 8)]
                mov             %%GT8, [%%ARG + _aesarg_in + (8 * 9)]
                vmovdqu64       %%XDATA2, [%%GT9 + %%IDX + 16]
                vinserti32x4    %%ZDATA2, [%%GT8 + %%IDX + 16], 1
                mov             %%GT9, [%%ARG + _aesarg_in + (8 * 10)]
                mov             %%GT8, [%%ARG + _aesarg_in + (8 * 11)]
                vinserti32x4    %%ZDATA2, [%%GT9 + %%IDX + 16], 2
                vinserti32x4    %%ZDATA2, [%%GT8 + %%IDX + 16], 3
%elif (i == 6)
                ;; CRC update for lanes 8 to 11
                vpclmulqdq      %%ZT19, %%ZDATB2, %%ZCRC_MUL, 0x01
                vpclmulqdq      %%ZT20, %%ZDATB2, %%ZCRC_MUL, 0x10
                vpternlogq      %%ZT20, %%ZT19, %%ZDATA2, 0x96
                vmovdqu16       %%ZDATB2{k3}, %%ZT20
%elif (i == 7)
                ;; Load one block of data from lanes 12 to 15 in ZDATA3
                mov             %%GT9, [%%ARG + _aesarg_in + (8 * 12)]
                mov             %%GT8, [%%ARG + _aesarg_in + (8 * 13)]
                vmovdqu64       %%XDATA3, [%%GT9 + %%IDX + 16]
                vinserti32x4    %%ZDATA3, [%%GT8 + %%IDX + 16], 1
                mov             %%GT9, [%%ARG + _aesarg_in + (8 * 14)]
                mov             %%GT8, [%%ARG + _aesarg_in + (8 * 15)]
                vinserti32x4    %%ZDATA3, [%%GT9 + %%IDX + 16], 2
                vinserti32x4    %%ZDATA3, [%%GT8 + %%IDX + 16], 3
%elif (i == 8)
                ;; CRC update for lanes 12 to 15
                vpclmulqdq      %%ZT19, %%ZDATB3, %%ZCRC_MUL, 0x01
                vpclmulqdq      %%ZT20, %%ZDATB3, %%ZCRC_MUL, 0x10
                vpternlogq      %%ZT20, %%ZT19, %%ZDATA3, 0x96
                vmovdqu16       %%ZDATB3{k4}, %%ZT20
%endif

%assign i (i + 1)
%endrep

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 10 or 14
        vaesenclast     %%ZCIPH0, %%ZCIPH0, %%KEYSET0LAST
        vaesenclast     %%ZCIPH1, %%ZCIPH1, %%KEYSET1LAST
        vaesenclast     %%ZCIPH2, %%ZCIPH2, %%KEYSET2LAST
        vaesenclast     %%ZCIPH3, %%ZCIPH3, %%KEYSET3LAST

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Store 16 cipher text blocks
        ;; - ZCIPHx is an IV for the next round

        mov             %%GT8, [%%ARG + _aesarg_out + 8*0]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*1]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH0
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH0, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*2]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*3]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH0, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH0, 3

        mov             %%GT8, [%%ARG + _aesarg_out + 8*4]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*5]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH1
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH1, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*6]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*7]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH1, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH1, 3

        mov             %%GT8, [%%ARG + _aesarg_out + 8*8]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*9]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH2, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*10]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*11]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH2, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH2, 3

        mov             %%GT8, [%%ARG + _aesarg_out + 8*12]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*13]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH3
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH3, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*14]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*15]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH3, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH3, 3

        add             %%IDX, 16
        sub             %%LEN, 16

        jmp             %%_main_enc_loop

%%_encrypt_and_crc_the_last_block:
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Main loop doesn't subtract lengths to save cycles
        ;; - all subtracts get accumulated and are done here
        vmovdqa64       YWORD(%%ZT17), [%%ARG + _docsis_crc_args_len]
        vpbroadcastw    YWORD(%%ZT18), WORD(%%IDX)
        vpsubw          YWORD(%%ZT17){k5}, YWORD(%%ZT17), YWORD(%%ZT18)
        vmovdqa64       [%%ARG + _docsis_crc_args_len], YWORD(%%ZT17)

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; - load key pointers to perform AES rounds
        ;; - use ternary logic for: plain-text XOR IV and AES ARK(0)
        ;;      - IV = ZCIPHx
        ;;      - plain-text = ZDATAx

        vpternlogq      %%ZCIPH0, %%ZDATA0, %%KEYSET0ARK, 0x96
        vpternlogq      %%ZCIPH1, %%ZDATA1, %%KEYSET1ARK, 0x96
        vpternlogq      %%ZCIPH2, %%ZDATA2, %%KEYSET2ARK, 0x96
        vpternlogq      %%ZCIPH3, %%ZDATA3, %%KEYSET3ARK, 0x96

                ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
                ;; CRC: load new data
                vmovdqu64       %%XDATA0, [%%INP0 + %%IDX + 16]
                vinserti32x4    %%ZDATA0, [%%INP1 + %%IDX + 16], 1
                vinserti32x4    %%ZDATA0, [%%INP2 + %%IDX + 16], 2
                vinserti32x4    %%ZDATA0, [%%INP3 + %%IDX + 16], 3

                vmovdqu64       %%XDATA1, [%%INP4 + %%IDX + 16]
                vinserti32x4    %%ZDATA1, [%%INP5 + %%IDX + 16], 1
                vinserti32x4    %%ZDATA1, [%%INP6 + %%IDX + 16], 2
                vinserti32x4    %%ZDATA1, [%%INP7 + %%IDX + 16], 3

                mov             %%GT9, [%%ARG + _aesarg_in + (8 * 8)]
                mov             %%GT8, [%%ARG + _aesarg_in + (8 * 9)]
                vmovdqu64       %%XDATA2, [%%GT9 + %%IDX + 16]
                vinserti32x4    %%ZDATA2, [%%GT8 + %%IDX + 16], 1
                mov             %%GT9, [%%ARG + _aesarg_in + (8 * 10)]
                mov             %%GT8, [%%ARG + _aesarg_in + (8 * 11)]
                vinserti32x4    %%ZDATA2, [%%GT9 + %%IDX + 16], 2
                vinserti32x4    %%ZDATA2, [%%GT8 + %%IDX + 16], 3

                mov             %%GT9, [%%ARG + _aesarg_in + (8 * 12)]
                mov             %%GT8, [%%ARG + _aesarg_in + (8 * 13)]
                vmovdqu64       %%XDATA3, [%%GT9 + %%IDX + 16]
                vinserti32x4    %%ZDATA3, [%%GT8 + %%IDX + 16], 1
                mov             %%GT9, [%%ARG + _aesarg_in + (8 * 14)]
                mov             %%GT8, [%%ARG + _aesarg_in + (8 * 15)]
                vinserti32x4    %%ZDATA3, [%%GT9 + %%IDX + 16], 2
                vinserti32x4    %%ZDATA3, [%%GT8 + %%IDX + 16], 3

                CRC32_ROUND_UPDATE %%ARG, \
                                 %%ZDATA0, %%ZDATA1, %%ZDATA2, %%ZDATA3, \
                                 %%ZDATB0, %%ZDATB1, %%ZDATB2, %%ZDATB3, \
                                 k5, k6, %%ZCRC_MUL, %%GT8, %%GT9, %%GT10, %%ZT17, %%ZT18

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; CRC 16 lanes and mix it with AES rounds

%assign crc_lane 0
%assign i 1
%rep 16
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 1 to NROUNDS (10 or 14)
%if (i <= %%NROUNDS)
%assign key_offset (i * (16 * 16))
        vaesenc         %%ZCIPH0, %%ZCIPH0, [%%ARG + _aesarg_key_tab + key_offset + (16 * 0)]
        vaesenc         %%ZCIPH1, %%ZCIPH1, [%%ARG + _aesarg_key_tab + key_offset + (16 * 4)]
        vaesenc         %%ZCIPH2, %%ZCIPH2, [%%ARG + _aesarg_key_tab + key_offset + (16 * 8)]
        vaesenc         %%ZCIPH3, %%ZCIPH3, [%%ARG + _aesarg_key_tab + key_offset + (16 * 12)]
%elif (i == (%%NROUNDS + 1))
%assign key_offset (i * (16 * 16))
        vaesenclast     %%ZCIPH0, %%ZCIPH0, %%KEYSET0LAST
        vaesenclast     %%ZCIPH1, %%ZCIPH1, %%KEYSET1LAST
        vaesenclast     %%ZCIPH2, %%ZCIPH2, %%KEYSET2LAST
        vaesenclast     %%ZCIPH3, %%ZCIPH3, %%KEYSET3LAST
%endif

                ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
                ;; CRC LAST round update on one of the lanes
                CRC32_ROUND_LAST   %%ARG, %%IDX, 16, crc_lane, \
                        %%ZDATA0, %%ZDATA1, %%ZDATA2, %%ZDATA3, \
                        %%ZDATB0, %%ZDATB1, %%ZDATB2, %%ZDATB3, \
                        %%ZCRC_MUL, \
                        %%ZT17, %%ZT18, %%ZT5, %%ZT6, %%ZT7, %%ZT8, \
                        %%GT8, %%GT9, %%GT10, %%GT11, k1

%assign crc_lane (crc_lane + 1)
%assign i (i + 1)
%endrep

                ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
                ;; CRC update lengths and status
                ;; k5 - bit mask of lanes that have been 'updated'
                ;; k6 - mask of lanes that have been finished
                vmovdqu64       YWORD(%%ZT19), [%%ARG + _docsis_crc_args_len]
                vmovdqa64       XWORD(%%ZT20), [%%ARG + _docsis_crc_args_done]
                vpsubw          YWORD(%%ZT19){k6}, YWORD(%%ZT19), YWORD(%%ZT19)
                vpsubw          YWORD(%%ZT19){k5}, [rel dw_16_x16]
                vmovdqu8        XWORD(%%ZT20){k6}, [rel crc_state_done_x16]
                vmovdqu64       [%%ARG + _docsis_crc_args_len], YWORD(%%ZT19)
                vmovdqu64       [%%ARG + _docsis_crc_args_done], XWORD(%%ZT20)

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Store 16 cipher text blocks
        ;; - ZCIPHx is an IV for the next round

        mov             %%GT8, [%%ARG + _aesarg_out + 8*0]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*1]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH0
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH0, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*2]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*3]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH0, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH0, 3

        mov             %%GT8, [%%ARG + _aesarg_out + 8*4]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*5]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH1
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH1, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*6]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*7]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH1, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH1, 3

        mov             %%GT8, [%%ARG + _aesarg_out + 8*8]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*9]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH2, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*10]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*11]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH2, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH2, 3

        mov             %%GT8, [%%ARG + _aesarg_out + 8*12]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*13]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH3
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH3, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*14]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*15]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH3, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH3, 3

        add             %%IDX, 16
        sub             %%LEN, 16

%%_encrypt_the_last_block:
        ;; NOTE: XDATA[0-3] preloaded with data blocks from corresponding lanes

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; - use ternary logic for: plain-text XOR IV and AES ARK(0)
        ;;      - IV = ZCIPHx
        ;;      - plain-text = ZDATAx

        vpternlogq      %%ZCIPH0, %%ZDATA0, %%KEYSET0ARK, 0x96
        vpternlogq      %%ZCIPH1, %%ZDATA1, %%KEYSET1ARK, 0x96
        vpternlogq      %%ZCIPH2, %%ZDATA2, %%KEYSET2ARK, 0x96
        vpternlogq      %%ZCIPH3, %%ZDATA3, %%KEYSET3ARK, 0x96

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 1 to NROUNDS (9 or 13)
%assign i 1
%rep %%NROUNDS
%assign key_offset (i * (16 * 16))

        vaesenc         %%ZCIPH0, %%ZCIPH0, [%%ARG + _aesarg_key_tab + key_offset + (16 * 0)]
        vaesenc         %%ZCIPH1, %%ZCIPH1, [%%ARG + _aesarg_key_tab + key_offset + (16 * 4)]
        vaesenc         %%ZCIPH2, %%ZCIPH2, [%%ARG + _aesarg_key_tab + key_offset + (16 * 8)]
        vaesenc         %%ZCIPH3, %%ZCIPH3, [%%ARG + _aesarg_key_tab + key_offset + (16 * 12)]

                ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
                ;; CRC: CRC sum from registers back into the context structure
%if i == 2
                vmovdqa64       [%%ARG + _docsis_crc_args_init + (16 * 0)], %%ZDATB0
%elif i == 4
                vmovdqa64       [%%ARG + _docsis_crc_args_init + (16 * 4)], %%ZDATB1
%elif i == 6
                vmovdqa64       [%%ARG + _docsis_crc_args_init + (16 * 8)], %%ZDATB2
%elif i == 8
                vmovdqa64       [%%ARG + _docsis_crc_args_init + (16 * 12)], %%ZDATB3
%endif

%assign i (i + 1)
%endrep

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES ROUNDS 10 or 14
%assign key_offset (i * (16 * 16))

        vaesenclast     %%ZCIPH0, %%ZCIPH0, %%KEYSET0LAST
        vaesenclast     %%ZCIPH1, %%ZCIPH1, %%KEYSET1LAST
        vaesenclast     %%ZCIPH2, %%ZCIPH2, %%KEYSET2LAST
        vaesenclast     %%ZCIPH3, %%ZCIPH3, %%KEYSET3LAST

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Store 16 cipher text blocks
        ;; - ZCIPHx is an IV for the next round

        mov             %%GT8, [%%ARG + _aesarg_out + 8*0]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*1]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH0
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH0, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*2]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*3]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH0, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH0, 3

        mov             %%GT8, [%%ARG + _aesarg_out + 8*4]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*5]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH1
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH1, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*6]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*7]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH1, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH1, 3

        mov             %%GT8, [%%ARG + _aesarg_out + 8*8]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*9]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH2, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*10]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*11]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH2, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH2, 3

        mov             %%GT8, [%%ARG + _aesarg_out + 8*12]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*13]
        vmovdqu64       [%%GT8 + %%IDX], %%XCIPH3
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH3, 1
        mov             %%GT8, [%%ARG + _aesarg_out + 8*14]
        mov             %%GT9, [%%ARG + _aesarg_out + 8*15]
        vextracti32x4   [%%GT8 + %%IDX], %%ZCIPH3, 2
        vextracti32x4   [%%GT9 + %%IDX], %%ZCIPH3, 3

        add             %%IDX, 16

%%_enc_done:
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; update IV
        vmovdqa64       [%%ARG + _aesarg_IV + 16*0], %%ZCIPH0
        vmovdqa64       [%%ARG + _aesarg_IV + 16*4], %%ZCIPH1
        vmovdqa64       [%%ARG + _aesarg_IV + 16*8], %%ZCIPH2
        vmovdqa64       [%%ARG + _aesarg_IV + 16*12], %%ZCIPH3

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; update IN and OUT pointers
        vpbroadcastq    %%ZT0, %%IDX
        vpaddq          %%ZT1, %%ZT0, [%%ARG + _aesarg_in + 0*8]
        vpaddq          %%ZT2, %%ZT0, [%%ARG + _aesarg_in + 8*8]
        vpaddq          %%ZT3, %%ZT0, [%%ARG + _aesarg_out + 0*8]
        vpaddq          %%ZT4, %%ZT0, [%%ARG + _aesarg_out + 8*8]
        vmovdqu64       [%%ARG + _aesarg_in + 0*8], %%ZT1
        vmovdqu64       [%%ARG + _aesarg_in + 8*8], %%ZT2
        vmovdqu64       [%%ARG + _aesarg_out + 0*8], %%ZT3
        vmovdqu64       [%%ARG + _aesarg_out + 8*8], %%ZT4

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
%define %%tmp2          %%GT2

        add             qword [%%STATE + _aes_lanes_in_use], 1

        mov             %%unused_lanes, [%%STATE + _aes_unused_lanes]
        mov             %%lane, %%unused_lanes
        and             %%lane, 0xF
        shr             %%unused_lanes, 4
        mov             [%%STATE + _aes_unused_lanes], %%unused_lanes

        mov             [%%STATE + _aes_job_in_lane + %%lane * PTR_SZ], %%JOB

        mov             %%len, [%%JOB + _msg_len_to_cipher_in_bytes]
        ;; DOCSIS may pass size unaligned to block size
        and             %%len, -16
        lea             %%tmp2, [rel map_index_0_15_to_mask]
        kmovw           k1, [%%tmp2 + %%lane*2]
        vpbroadcastw    ymm1, WORD(%%len)
        vmovdqu16       ymm0, [%%STATE + _aes_lens]
        vmovdqu16       ymm0{k1}, ymm1
        vmovdqu16       [%%STATE + _aes_lens], ymm0

        ;; Insert expanded keys
        mov             %%tmp, [%%JOB + _enc_keys]
        mov             [%%STATE + _aes_args_keys + %%lane*8], %%tmp
        INSERT_KEYS     %%STATE, %%tmp, %%lane, %%NROUNDS, %%GT8, zmm2, %%GT9

        ;; Update input pointer
        mov             %%tmp, [%%JOB + _src]
        add             %%tmp, [%%JOB + _cipher_start_src_offset_in_bytes]
        mov             [%%STATE + _aes_args_in + %%lane*8], %%tmp

        ;; Update output pointer
        mov             %%tmp, [%%JOB + _dst]
        mov             [%%STATE + _aes_args_out + %%lane*8], %%tmp

        ;; Set default CRC state
        mov             byte [%%STATE + _docsis_crc_args_done + %%lane], CRC_LANE_STATE_DONE

        ;; Set IV
        mov             %%tmp2, [%%JOB + _iv]
        vmovdqu64       xmm0, [%%tmp2]
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

        vmovdqa64       XWORD(%%ZT1), [rel rk1]

        cmp             qword [%%JOB + _msg_len_to_cipher_in_bytes], (2 * 16)
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
        vmovd           XWORD(%%ZT0), DWORD(%%GT7)
        vmovdqa64       [%%STATE + _docsis_crc_args_init + %%lane], XWORD(%%ZT0)
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

%%_crc_complete:
        cmp             qword [%%STATE + _aes_lanes_in_use], 16
        je              %%_load_lens
        xor             %%job_rax, %%job_rax    ; return NULL
        jmp             %%_return

%%_load_lens:
        ;; load lens into xmm0 and xmm9
        vmovdqa64       xmm0, [%%STATE + _aes_lens + (0 * 2)]
        vmovdqa64       xmm9, [%%STATE + _aes_lens + (8 * 2)]

        vphminposuw     xmm10, xmm0
        vphminposuw     xmm11, xmm9
%else
        ;; /////////////////////////////////////////////////
        ;; FLUSH

%define %%tmp1             %%GT1
%define %%good_lane        %%GT2
%define %%tmp              %%GT3
%define %%tmp2             %%GT4
%define %%tmp3             %%GT5

        ; check for empty
        xor             %%job_rax, %%job_rax    ; return NULL (default)

        cmp             qword [%%STATE + _aes_lanes_in_use], 0
        je              %%_return

%%_find_non_null_lane:
        ; find a lane with a non-null job
        vpxorq          zmm0, zmm0, zmm0
        vmovdqu64       zmm1, [%%STATE + _aes_job_in_lane + (0 * PTR_SZ)]
        vmovdqu64       zmm2, [%%STATE + _aes_job_in_lane + (8 * PTR_SZ)]
        vpcmpq          k1, zmm1, zmm0, 4 ; NEQ
        vpcmpq          k2, zmm2, zmm0, 4 ; NEQ
        kunpckbw        k3, k2, k1      ; k3 = mask of all non-null jobs in k3
        kmovw           DWORD(%%tmp), k3
        knotb           k4, k1          ; k4 = mask of null jobs lanes 0..7
        knotb           k5, k2          ; k5 = mask of null jobs lanes 8..15
        kunpckbw        k6, k5, k4      ; k6 = ~k3 (mask of all null jobs)
        xor             %%good_lane, %%good_lane
        bsf             WORD(%%good_lane), WORD(%%tmp)  ; index of the 1st set bit in tmp2

        ;; copy good lane data into NULL lanes
        ;; - in pointer
        mov             %%tmp, [%%STATE + _aes_args_in + %%good_lane * PTR_SZ]
        vpbroadcastq    zmm1, %%tmp
        vmovdqa64       [%%STATE + _aes_args_in + (0 * PTR_SZ)]{k4}, zmm1
        vmovdqa64       [%%STATE + _aes_args_in + (8 * PTR_SZ)]{k5}, zmm1

        ;; - out pointer
        mov             %%tmp, [%%STATE + _aes_args_out + %%good_lane * PTR_SZ]
        vpbroadcastq    zmm1, %%tmp
        vmovdqa64       [%%STATE + _aes_args_out + (0 * PTR_SZ)]{k4}, zmm1
        vmovdqa64       [%%STATE + _aes_args_out + (8 * PTR_SZ)]{k5}, zmm1

        ;; - key pointer
        mov             %%tmp, [%%STATE + _aes_args_keys + %%good_lane * PTR_SZ]
        vpbroadcastq    zmm1, %%tmp
        vmovdqa64       [%%STATE + _aes_args_keys + (0 * PTR_SZ)]{k4}, zmm1
        vmovdqa64       [%%STATE + _aes_args_keys + (8 * PTR_SZ)]{k5}, zmm1

        ;; - CRC lengths
        mov             WORD(%%tmp), [%%STATE + _docsis_crc_args_len + %%good_lane*2]
        vpbroadcastw    ymm3, WORD(%%tmp)
        vmovdqa64       ymm1, [%%STATE + _docsis_crc_args_len]
        vmovdqu16       ymm1{k6}, ymm3
        vmovdqa64       [%%STATE + _docsis_crc_args_len], ymm1

        ;; - CRC status
        vpbroadcastb    xmm3, [%%STATE + _docsis_crc_args_done + %%good_lane]
        vmovdqu8        xmm0, [%%STATE + _docsis_crc_args_done]
        vmovdqu8        xmm0{k6}, xmm3
        vmovdqu8        [%%STATE + _docsis_crc_args_done], xmm0

        ;; - set len to UINT16_MAX
        mov             WORD(%%tmp), 0xffff
        vpbroadcastw    ymm3, WORD(%%tmp)
        vmovdqa64       ymm0, [%%STATE + _aes_lens]
        vmovdqu16       ymm0{k6}, ymm3
        vmovdqa64       [%%STATE + _aes_lens], ymm0

        ;; find min value
        vextracti128    xmm9, ymm0, 1
        vphminposuw     xmm10, xmm0
        vphminposuw     xmm11, xmm9

        ;; - copy IV, AES keys and CRC state to null lanes

        shl             %%good_lane, 4

        ;; get 4 x 8-bit masks for null lanes out of 16-bit mask
        lea             %%tmp2, [rel map_4bits_to_8bits]
        kmovw           DWORD(%%tmp), k6
        and             DWORD(%%tmp), 15
        kmovb           k2, [%%tmp2 + %%tmp]
        kmovw           DWORD(%%tmp), k6
        shr             DWORD(%%tmp), 4
        and             DWORD(%%tmp), 15
        kmovb           k3, [%%tmp2 + %%tmp]
        kmovw           DWORD(%%tmp), k6
        shr             DWORD(%%tmp), 8
        and             DWORD(%%tmp), 15
        kmovb           k4, [%%tmp2 + %%tmp]
        kmovw           DWORD(%%tmp), k6
        shr             DWORD(%%tmp), 12
        and             DWORD(%%tmp), 15
        kmovb           k5, [%%tmp2 + %%tmp]

        ;; populate CRC state across NULL lanes
        vbroadcasti32x4 zmm4, [%%STATE + _docsis_crc_args_init + %%good_lane]
        vmovdqa64       [%%STATE + _docsis_crc_args_init + (0 * 16)]{k2}, zmm4
        vmovdqa64       [%%STATE + _docsis_crc_args_init + (4 * 16)]{k3}, zmm4
        vmovdqa64       [%%STATE + _docsis_crc_args_init + (8 * 16)]{k4}, zmm4
        vmovdqa64       [%%STATE + _docsis_crc_args_init + (12 * 16)]{k5}, zmm4

        ;; populate IV across NULL lanes
        vbroadcasti32x4 zmm4, [%%STATE + _aes_args_IV + %%good_lane]
        vmovdqa64       [%%STATE + _aes_args_IV + (0 * 16)]{k2}, zmm4
        vmovdqa64       [%%STATE + _aes_args_IV + (4 * 16)]{k3}, zmm4
        vmovdqa64       [%%STATE + _aes_args_IV + (8 * 16)]{k4}, zmm4
        vmovdqa64       [%%STATE + _aes_args_IV + (12 * 16)]{k5}, zmm4

        ;; populate keys across NULL lanes
        lea             %%tmp, [%%STATE + _aesarg_key_tab]
%assign key_offset 0
%rep (%%NROUNDS + 2)
        vbroadcasti32x4 zmm4, [%%tmp + %%good_lane + key_offset]
        vmovdqa64       [%%tmp + key_offset + (0 * 16)]{k2}, zmm4
        vmovdqa64       [%%tmp + key_offset + (4 * 16)]{k3}, zmm4
        vmovdqa64       [%%tmp + key_offset + (8 * 16)]{k4}, zmm4
        vmovdqa64       [%%tmp + key_offset + (12 * 16)]{k5}, zmm4

%assign key_offset (key_offset + (16*16))
%endrep

%endif  ;; SUBMIT / FLUSH

%%_find_min_job:
        ;; xmm0 includes vector of 8 lengths 0-7
        ;; xmm9 includes vector of 8 lengths 8-15
        ;; xmm10 - result of phmin on xmm0
        ;; xmm11 - result of phmin on xmm9

        vpextrw         DWORD(%%len2), xmm10, 0 ; min value (0...7)
        vpextrw         DWORD(%%GT4), xmm11, 0  ; min value (8...15)

        vpextrw         DWORD(%%idx), xmm10, 1  ; min index (0...7)
        vpextrw         DWORD(%%GT5), xmm11, 1  ; min index (8...15)

        cmp             DWORD(%%len2), DWORD(%%GT4)
        jbe             %%_min_on_lanes_0_7_goes
        ;; min is on lanes 8..15
        mov             DWORD(%%len2), DWORD(%%GT4)
        lea             DWORD(%%idx), [DWORD(%%GT5) + 8]
%%_min_on_lanes_0_7_goes:
        cmp             DWORD(%%len2), 0
        je              %%_len_is_0

        vinserti32x4    ymm0, xmm9, 1
        vpbroadcastw    ymm1, WORD(%%len2)
        vpsubw          ymm0, ymm0, ymm1
        vmovdqa64       [%%STATE + _aes_lens], ymm0

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

        shl             %%idx, 4        ;; scale idx up to x16
        vmovdqa64       xmm8, [%%STATE + _docsis_crc_args_init + %%idx]

        or              %%GT3, %%GT3
        jz              %%_crc_reduce

        lea             %%GT5, [rel byte_len_to_mask_ref_table]
        kmovw           k7, [%%GT5 + %%GT3*2]
        vmovdqu8        xmm9{k7}{z}, [%%GT4 - 16 + %%GT3]
        lea             %%GT5, [rel pshufb_shf_table]
        vmovdqu64       xmm10, [%%GT5 + %%GT3]

        vmovdqa64       xmm11, xmm8
        vpshufb         xmm8, xmm10  ; top num_bytes with LSB xcrc
        vpxorq          xmm10, [rel mask3]
        vpshufb         xmm11, xmm10 ; bottom (16 - num_bytes) with MSB xcrc

        ;; data num_bytes (top) blended with MSB bytes of CRC (bottom)
        vporq           xmm11, xmm11, xmm9

        ;; final CRC calculation
        vmovdqa64       xmm9, [rel rk1]
        CRC_CLMUL       xmm8, xmm9, xmm11, xmm12

%%_crc_reduce:
        ;; %%idx - is scaled up x16 at this point
        ;; GT3 - offset in bytes to put the CRC32 value into
        ;; GT4 - src buffer pointer
        ;; xmm8 - current CRC value for reduction
        ;; - write CRC value into SRC buffer for further cipher
        ;; - keep CRC value in init field
        CRC32_REDUCE_128_TO_32 %%GT7, xmm8, xmm9, xmm10, xmm11
        vmovd           xmm11, DWORD(%%GT7)
        vmovdqa64       [%%STATE + _docsis_crc_args_init + %%idx], xmm11
        shr             %%idx, 4        ;; scale idx back to normal

        ;; load partial block bytes with just computed CRC into xmm3
        lea             %%GT2, [rel byte_len_to_mask_table]
        kmovw           k2, [%%GT2 + %%GT3*2]
        kmovw           k1, [%%GT2 + %%GT3*2 + 4*2]     ;; assume cipher size = crc size + 4
        vmovdqu8        xmm3{k2}{z}, [%%GT4]
        vmovd           xmm2, DWORD(%%GT7)
        lea             %%GT2, [rel pshufb_shf_table + 16]
        sub             %%GT2, %%GT3
        vmovdqu64       xmm1, [%%GT2]
        vpshufb         xmm2, xmm2, xmm1
        vporq           xmm3, xmm3, xmm2
        vmovdqu8        [%%GT4]{k1}, xmm3

        ;; xmm3 - plain text for cipher
        ;; k1 - mask for masked store
        jmp             %%_partial_block_cipher_no_load

align 32
%%_crc_is_complete:
        mov             %%GT3, [%%job_rax + _msg_len_to_cipher_in_bytes]
        and             %%GT3, 0xf
        jz              %%_no_partial_block_cipher

        ;; AES128/256-CFB on the partial block
        lea             %%GT2, [rel byte_len_to_mask_table]
        kmovw           k1, [%%GT2 + %%GT3*2]
        mov             %%GT4, [%%STATE + _aes_args_in + %%idx*8]
        vmovdqu8        xmm3{k1}{z}, [%%GT4]

align 32
%%_partial_block_cipher_no_load:
        mov             %%GT5, [%%STATE + _aes_args_out + %%idx*8]
        mov             %%GT6, [%%job_rax + _enc_keys]
        shl             %%idx, 4
        vmovdqa64       xmm2, [%%STATE + _aes_args_IV + %%idx]
        shr             %%idx, 4

        vpxorq          xmm1, xmm2, [%%GT6 + 0*16]
%assign i 1
%rep %%NROUNDS
        vaesenc         xmm1, [%%GT6 + i*16]
%assign i (i + 1)
%endrep
        vaesenclast     xmm1, [%%GT6 + i*16]

        vpxorq          xmm1, xmm1, xmm3
        vmovdqu8        [%%GT5]{k1}, xmm1

align 32
%%_no_partial_block_cipher:
        ;; - copy CRC value into auth tag
        ;; - process completed job "idx"
        shl             %%idx, 4
        vmovdqa64       xmm0, [%%STATE + _docsis_crc_args_init + %%idx]
        shr             %%idx, 4
        mov             %%GT6, [%%job_rax + _auth_tag_output]
        vmovd           [%%GT6], xmm0

        mov             %%unused_lanes, [%%STATE + _aes_unused_lanes]
        mov             qword [%%STATE + _aes_job_in_lane + %%idx*8], 0
        or              dword [%%job_rax + _status], IMB_STATUS_COMPLETED_CIPHER
        shl             %%unused_lanes, 4
        or              %%unused_lanes, %%idx
        mov             [%%STATE + _aes_unused_lanes], %%unused_lanes
        sub             qword [%%STATE + _aes_lanes_in_use], 1

%ifdef SAFE_DATA
%ifidn %%SUBMIT_FLUSH, submit
        ;; - clear key pointer
        mov             qword [%%STATE + _aes_args_keys + %%idx], 0

        ;; - clear CRC state
        shl             %%idx, 3 ; multiply by 8
        vpxor           xmm0, xmm0
        vmovdqa64       [%%STATE + _docsis_crc_args_init + %%idx*2], xmm0

        ;; - clear expanded keys
%assign key_offset 0
%rep (%%NROUNDS + 2)
        vmovdqa64       [%%STATE + _aes_args_key_tab + key_offset + %%idx*2], xmm0
%assign key_offset (key_offset + (16 * 16))
%endrep

%else
        ;; clear data for all NULL jobs
        vmovdqu64       zmm0, [%%STATE + _aes_job_in_lane + (0 * PTR_SZ)]
        vmovdqu64       zmm1, [%%STATE + _aes_job_in_lane + (8 * PTR_SZ)]
        vpxorq          zmm2, zmm2
        vpcmpeqq        k2, zmm0, zmm2
        vpcmpeqq        k3, zmm1, zmm2

        ;; clear key pointers
        vmovdqu64       [%%STATE + _aes_args_keys + (0 * PTR_SZ)]{k2}, zmm2
        vmovdqu64       [%%STATE + _aes_args_keys + (8 * PTR_SZ)]{k3}, zmm2

        ;; get new masks to clear 128-bit data with a 64-bit operations
        lea             %%GT6, [rel map_4bits_to_8bits]
        kmovw           DWORD(%%GT7), k2
        and             DWORD(%%GT7), 15
        kmovb           k4, [%%GT6 + %%GT7]
        kmovw           DWORD(%%GT7), k2
        shr             DWORD(%%GT7), 4
        and             DWORD(%%GT7), 15
        kmovb           k5, [%%GT6 + %%GT7]
        kmovw           DWORD(%%GT7), k3
        and             DWORD(%%GT7), 15
        kmovb           k6, [%%GT6 + %%GT7]
        kmovw           DWORD(%%GT7), k3
        shr             DWORD(%%GT7), 4
        and             DWORD(%%GT7), 15
        kmovb           k7, [%%GT6 + %%GT7]

        ;; clear CRC state
        vmovdqa64       [%%STATE + _docsis_crc_args_init + (0 * 16)]{k4}, zmm2
        vmovdqa64       [%%STATE + _docsis_crc_args_init + (4 * 16)]{k5}, zmm2
        vmovdqa64       [%%STATE + _docsis_crc_args_init + (8 * 16)]{k6}, zmm2
        vmovdqa64       [%%STATE + _docsis_crc_args_init + (12 * 16)]{k7}, zmm2

        ;; clear keys
%assign key_offset 0
%rep (%%NROUNDS + 2)
        vmovdqa64       [%%STATE + _aes_args_key_tab + key_offset + (0 * 16)]{k4}, zmm2
        vmovdqa64       [%%STATE + _aes_args_key_tab + key_offset + (4 * 16)]{k5}, zmm2
        vmovdqa64       [%%STATE + _aes_args_key_tab + key_offset + (8 * 16)]{k6}, zmm2
        vmovdqa64       [%%STATE + _aes_args_key_tab + key_offset + (12 * 16)]{k7}, zmm2
%assign key_offset (key_offset + (16 * 16))
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
MKGLOBAL(submit_job_aes_docsis128_enc_crc32_vaes_avx512,function,internal)
submit_job_aes_docsis128_enc_crc32_vaes_avx512:
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
MKGLOBAL(submit_job_aes_docsis256_enc_crc32_vaes_avx512,function,internal)
submit_job_aes_docsis256_enc_crc32_vaes_avx512:
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
MKGLOBAL(flush_job_aes_docsis128_enc_crc32_vaes_avx512,function,internal)
flush_job_aes_docsis128_enc_crc32_vaes_avx512:
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
MKGLOBAL(flush_job_aes_docsis256_enc_crc32_vaes_avx512,function,internal)
flush_job_aes_docsis256_enc_crc32_vaes_avx512:
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

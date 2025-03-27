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
%use smartalign

%include "include/imb_job.asm"
%include "include/reg_sizes.asm"
%include "include/os.asm"
%include "include/clear_regs.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/cet.inc"
;; In System V AMD64 ABI
;;	callee saves: RBX, RBP, R12-R15
;; Windows x64 ABI
;;	callee saves: RBX, RBP, RDI, RSI, RSP, R12-R15

%define CONCAT(a,b) a %+ b

struc STACKFRAME
_rsp_save:      resq    1
_gpr_save:	resq	4
endstruc

%ifdef LINUX
%define arg1	rdi
%define arg2	rsi
%else
%define arg1	rcx
%define arg2	rdx
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
default rel

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

;; ===================================================================
;; ===================================================================
;; AES128/256 CBC decryption on 1 to 8 blocks
;; ===================================================================
%macro AES_CBC_DEC_1_TO_8 27-34
%define %%SRC        %1  ; [in] GP with pointer to source buffer
%define %%DST        %2  ; [in] GP with pointer to destination buffer
%define %%NUMBL      %3  ; [in] numerical constant with number of blocks to process
%define %%OFFS       %4  ; [in/out] GP with src/dst buffer offset
%define %%NBYTES     %5  ; [in/out] GP with number of bytes to decrypt
%define %%XKEY0      %6  ; [in] XMM with preloaded key 0 / ARK (xmm0 - xmm15)
%define %%KEY_PTR    %7  ; [in] GP with pointer to expanded AES decrypt keys
%define %%XIV        %8  ; [in/out] IV in / last cipher text block on out (xmm0 - xmm15)
%define %%XD0        %9  ; [clobbered] temporary XMM (any xmm)
%define %%XD1        %10 ; [clobbered] temporary XMM (any xmm)
%define %%XD2        %11 ; [clobbered] temporary XMM (any xmm)
%define %%XD3        %12 ; [clobbered] temporary XMM (any xmm)
%define %%XD4        %13 ; [clobbered] temporary XMM (any xmm)
%define %%XD5        %14 ; [clobbered] temporary XMM (any xmm)
%define %%XD6        %15 ; [clobbered] temporary XMM (any xmm)
%define %%XD7        %16 ; [clobbered] temporary XMM (any xmm)
%define %%XC0        %17 ; [out] block of clear text (xmm0 - xmm15)
%define %%XC1        %18 ; [out] block of clear text (xmm0 - xmm15)
%define %%XC2        %19 ; [out] block of clear text (xmm0 - xmm15)
%define %%XC3        %20 ; [out] block of clear text (xmm0 - xmm15)
%define %%XC4        %21 ; [out] block of clear text (xmm0 - xmm15)
%define %%XC5        %22 ; [out] block of clear text (xmm0 - xmm15)
%define %%XC6        %23 ; [out] block of clear text (xmm0 - xmm15)
%define %%XC7        %24 ; [out] block of clear text (xmm0 - xmm15)
%define %%XTKEY      %25 ; [clobbered] temporary XMM (xmm0 - xmm15)
%define %%NROUNDS    %26 ; [in] Number of rounds (9 or 13)
%define %%XCRCB0     %27 ; [out] XMM (any) to receive copy of clear text, or "no_reg_copy"
%define %%XCRCB1     %28 ; [optional/out] clear text XMM (XCRCB0 != "no_reg_copy")
%define %%XCRCB2     %29 ; [optional/out] clear text XMM (XCRCB0 != "no_reg_copy")
%define %%XCRCB3     %30 ; [optional/out] clear text XMM (XCRCB0 != "no_reg_copy")
%define %%XCRCB4     %31 ; [optional/out] clear text XMM (XCRCB0 != "no_reg_copy")
%define %%XCRCB5     %32 ; [optional/out] clear text XMM (XCRCB0 != "no_reg_copy")
%define %%XCRCB6     %33 ; [optional/out] clear text XMM (XCRCB0 != "no_reg_copy")
%define %%XCRCB7     %34 ; [optional/out] clear text XMM (XCRCB0 != "no_reg_copy")

        ;; /////////////////////////////////////////////////
        ;; load cipher text blocks XD0-XD7
%assign i 0
%rep %%NUMBL
        vmovdqu64       %%XD %+ i, [%%SRC + %%OFFS + (i*16)]
%assign i (i+1)
%endrep

        ;; /////////////////////////////////////////////////
        ;; perform ARK => result in XC0-XC7
%assign i 0
%rep %%NUMBL
	vpxorq          %%XC %+ i, %%XD %+ i, %%XKEY0
%assign i (i+1)
%endrep

        ;; AES rounds 1 to 9/13 & CRC blocks 0 to 8
%assign crc_block 0
%assign round 1
%rep %%NROUNDS

        ;; /////////////////////////////////////////////////
        ;; AES decrypt round
%assign i 0
        vmovdqa64       %%XTKEY, [%%KEY_PTR + (round*16)]
%rep %%NUMBL
	vaesdec         %%XC %+ i, %%XC %+ i, %%XTKEY
%assign i (i+1)
%endrep ;; number of blocks
%assign round (round + 1)

%endrep ;; 9/13 x AES round (8 is max number of CRC blocks)

        ;; /////////////////////////////////////////////////
        ;; AES round 10/14 (the last one)
        vmovdqa64       %%XTKEY, [%%KEY_PTR + (round*16)]
%assign i 0
%rep %%NUMBL
	vaesdeclast     %%XC %+ i, %%XC %+ i, %%XTKEY
%assign i (i+1)
%endrep ;; number of blocks

        ;; /////////////////////////////////////////////////
        ;; AES-CBC final XOR operations against IV / cipher text blocks
        ;; put the last cipher text block into XIV
        vpxorq          %%XC0, %%XC0, %%XIV

%assign i_ciph 1
%assign i_data 0
%rep (%%NUMBL - 1)
        vpxorq          %%XC %+ i_ciph, %%XC %+ i_ciph, %%XD %+ i_data
%assign i_ciph (i_ciph + 1)
%assign i_data (i_data + 1)
%endrep

%assign i (%%NUMBL - 1)
        vmovdqa64       %%XIV, %%XD %+ i

        ;; /////////////////////////////////////////////////
        ;; store clear text
%assign i 0
%rep %%NUMBL
	vmovdqu64       [%%DST + %%OFFS + (i*16)], %%XC %+ i
%assign i (i+1)
%endrep

        ;; \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\
        ;; Copy clear text into different registers for CRC
%ifnidn %%XCRCB0, no_reg_copy
%assign i 0
%rep %%NUMBL
        vmovdqa64       %%XCRCB %+ i, %%XC %+ i
%assign i (i+1)
%endrep
%endif          ;; !no_reg_copy
        ;; /////////////////////////////////////////////////
        ;; update lengths and offset
        add             %%OFFS, (%%NUMBL * 16)
        sub             %%NBYTES, (%%NUMBL * 16)
%endmacro       ;; AES_CBC_DEC_1_TO_8

;; ===================================================================
;; ===================================================================
;; CRC32 on 1 to 8 blocks
;; ===================================================================
%macro CRC32_1_TO_8 6-13
%define %%CRC_TYPE   %1  ; [in] CRC operation: "first_crc" or "next_crc"
%define %%CRC_MUL    %2  ; [in] XMM with CRC multiplier (xmm0 - xmm15)
%define %%CRC_IN_OUT %3  ; [in/out] current CRC value
%define %%XTMP       %4  ; [clobbered] temporary XMM (xmm0 - xmm15)
%define %%NUMBL      %5  ; [in] number of blocks of clear text to compute CRC on
%define %%CRCIN0     %6  ; [in] clear text block
%define %%CRCIN1     %7  ; [in] clear text block
%define %%CRCIN2     %8  ; [in] clear text block
%define %%CRCIN3     %9  ; [in] clear text block
%define %%CRCIN4     %10 ; [in] clear text block
%define %%CRCIN5     %11 ; [in] clear text block
%define %%CRCIN6     %12 ; [in] clear text block
%define %%CRCIN7     %13 ; [in] clear text block

%if %%NUMBL > 0
        ;; block 0 - check first vs next
        CRC_UPDATE16    no_load, %%CRC_IN_OUT, %%CRC_MUL, %%CRCIN0, %%XTMP, %%CRC_TYPE

        ;; blocks 1 to 7 - no difference between first / next here
%assign crc_block 1
%rep (%%NUMBL - 1)
        CRC_UPDATE16    no_load, %%CRC_IN_OUT, %%CRC_MUL, %%CRCIN %+ crc_block, \
                        %%XTMP, next_crc
%assign crc_block (crc_block + 1)
%endrep
%endif  ;; %%NUMBL > 0

%endmacro       ;; CRC32_1_TO_8

;; ===================================================================
;; ===================================================================
;; Stitched AES128/256 CBC decryption & CRC32 on 1 to 8 blocks
;; XCRCINx - on input they include clear text input for CRC.
;;           They get updated towards the end of the macro with
;;           just decrypted set of blocks.
;; ===================================================================
%macro AES_CBC_DEC_CRC32_1_TO_8 39
%define %%SRC        %1  ; [in] GP with pointer to source buffer
%define %%DST        %2  ; [in] GP with pointer to destination buffer
%define %%NUMBL      %3  ; [in] numerical constant with number of blocks to cipher
%define %%NUMBL_CRC  %4  ; [in] numerical constant with number of blocks to CRC32
%define %%OFFS       %5  ; [in/out] GP with src/dst buffer offset
%define %%NBYTES     %6  ; [in/out] GP with number of bytes to decrypt
%define %%XKEY0      %7  ; [in] XMM with preloaded key 0 / AES ARK (any xmm)
%define %%KEY_PTR    %8  ; [in] GP with pointer to expanded AES decrypt keys
%define %%XIV        %9  ; [in/out] IV in / last cipher text block on out (xmm0 - xmm15)
%define %%XD0        %10 ; [clobbered] temporary XMM (any xmm)
%define %%XD1        %11 ; [clobbered] temporary XMM (any xmm)
%define %%XD2        %12 ; [clobbered] temporary XMM (any xmm)
%define %%XD3        %13 ; [clobbered] temporary XMM (any xmm)
%define %%XD4        %14 ; [clobbered] temporary XMM (any xmm)
%define %%XD5        %15 ; [clobbered] temporary XMM (any xmm)
%define %%XD6        %16 ; [clobbered] temporary XMM (any xmm)
%define %%XD7        %17 ; [clobbered] temporary XMM (any xmm)
%define %%XC0        %18 ; [clobbered] temporary XMM (xmm0 - xmm15) - for AES
%define %%XC1        %19 ; [clobbered] temporary XMM (xmm0 - xmm15) - for AES
%define %%XC2        %20 ; [clobbered] temporary XMM (xmm0 - xmm15) - for AES
%define %%XC3        %21 ; [clobbered] temporary XMM (xmm0 - xmm15) - for AES
%define %%XC4        %22 ; [clobbered] temporary XMM (xmm0 - xmm15) - for AES
%define %%XC5        %23 ; [clobbered] temporary XMM (xmm0 - xmm15) - for AES
%define %%XC6        %24 ; [clobbered] temporary XMM (xmm0 - xmm15) - for AES
%define %%XC7        %25 ; [clobbered] temporary XMM (xmm0 - xmm15) - for AES
%define %%XTKEY      %26 ; [clobbered] temporary XMM (xmm0 - xmm15) - for AES
%define %%NROUNDS    %27 ; [in] Number of rounds (9 or 13)
%define %%CRC_TYPE   %28 ; [in] CRC operation: "first_crc" or "next_crc"
%define %%CRC_MUL    %29 ; [in] XMM with CRC multiplier (xmm0 - xmm15)
%define %%CRC_IN_OUT %30 ; [in/out] current CRC value
%define %%XTMP       %31 ; [clobbered] temporary XMM (xmm0 - xmm15) - for CRC
%define %%XCRCIN0    %32 ; [in/out] clear text block
%define %%XCRCIN1    %33 ; [in/out] clear text block
%define %%XCRCIN2    %34 ; [in/out] clear text block
%define %%XCRCIN3    %35 ; [in/out] clear text block
%define %%XCRCIN4    %36 ; [in/out] clear text block
%define %%XCRCIN5    %37 ; [in/out] clear text block
%define %%XCRCIN6    %38 ; [in/out] clear text block
%define %%XCRCIN7    %39 ; [in/out] clear text block

        ;; /////////////////////////////////////////////////
        ;; load cipher text blocks XD0-XD7
%assign i 0
%rep %%NUMBL
        vmovdqu64       %%XD %+ i, [%%SRC + %%OFFS + (i*16)]
%assign i (i+1)
%endrep

        ;; /////////////////////////////////////////////////
        ;; perform ARK => result in XC0-XC7
%assign i 0
%rep %%NUMBL
	vpxorq          %%XC %+ i, %%XD %+ i, %%XKEY0
%assign i (i+1)
%endrep

        ;; AES rounds 1 to 9/13 & CRC blocks 0 to 8
%assign crc_block 0
%assign round 1
%rep %%NROUNDS

        ;; /////////////////////////////////////////////////
        ;; AES decrypt round
%assign i 0
        vmovdqa64       %%XTKEY, [%%KEY_PTR + (round*16)]
%rep %%NUMBL
	vaesdec         %%XC %+ i, %%XC %+ i, %%XTKEY
%assign i (i+1)
%endrep
%assign round (round + 1)

                ;; \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\
                ;; CRC on previously decrypted blocks
%if crc_block < %%NUMBL_CRC
%ifidn %%CRC_TYPE, first_crc
%if crc_block > 0
                CRC_CLMUL       %%CRC_IN_OUT, %%CRC_MUL, %%XCRCIN %+ crc_block, %%XTMP
%else
                vpxorq          %%CRC_IN_OUT, %%CRC_IN_OUT, %%XCRCIN %+ crc_block
%endif
%endif          ;; first_crc
%ifidn %%CRC_TYPE, next_crc
                CRC_CLMUL       %%CRC_IN_OUT, %%CRC_MUL, %%XCRCIN %+ crc_block, %%XTMP
%endif          ;; next_crc
%endif          ;; crc_block < %%NUMBL_CRC
%assign crc_block (crc_block + 1)

%endrep ;; 9/13 x AES round (8 is max number of CRC blocks)

        ;; /////////////////////////////////////////////////
        ;; AES round 10/14 (the last one)
        vmovdqa64       %%XTKEY, [%%KEY_PTR + (round*16)]
%assign i 0
%rep %%NUMBL
	vaesdeclast     %%XC %+ i, %%XC %+ i, %%XTKEY
%assign i (i+1)
%endrep

        ;; /////////////////////////////////////////////////
        ;; AES-CBC final XOR operations against IV / cipher text blocks
        ;; put the last cipher text block into XIV
        vpxorq          %%XC0, %%XC0, %%XIV

%assign i_ciph 1
%assign i_data 0
%rep (%%NUMBL - 1)
        vpxorq          %%XC %+ i_ciph, %%XC %+ i_ciph, %%XD %+ i_data
%assign i_ciph (i_ciph + 1)
%assign i_data (i_data + 1)
%endrep

%assign i (%%NUMBL - 1)
        vmovdqa64       %%XIV, %%XD %+ i

        ;; /////////////////////////////////////////////////
        ;; store clear text
%assign i 0
%rep %%NUMBL
	vmovdqu64       [%%DST + %%OFFS + (i*16)], %%XC %+ i
%assign i (i+1)
%endrep

                ;; \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\
                ;; CRC - keep clear text blocks for the next round
%assign i 0
%rep %%NUMBL
                vmovdqa64       %%XCRCIN %+ i, %%XC %+ i
%assign i (i+1)
%endrep

        ;; /////////////////////////////////////////////////
        ;; update lengths and offset
        add             %%OFFS, (%%NUMBL * 16)
        sub             %%NBYTES, (%%NUMBL * 16)

%endmacro       ;; AES_CBC_DEC_CRC32_1_TO_8

;; ===================================================================
;; ===================================================================
;; DOCSIS SEC BPI (AES based) decryption + CRC32
;; ===================================================================
%macro DOCSIS_DEC_CRC32 40
%define %%KEYS       %1   ;; [in] GP with pointer to expanded keys (decrypt)
%define %%SRC        %2   ;; [in] GP with pointer to source buffer
%define %%DST        %3   ;; [in] GP with pointer to destination buffer
%define %%NUM_BYTES  %4   ;; [in/clobbered] GP with number of bytes to decrypt
%define %%KEYS_ENC   %5   ;; [in] GP with pointer to expanded keys (encrypt)
%define %%GT1        %6   ;; [clobbered] temporary GP
%define %%GT2        %7   ;; [clobbered] temporary GP
%define %%XCRC_INIT  %8   ;; [in/out] CRC initial value (xmm0 - xmm15)
%define %%XIV        %9   ;; [in/out] cipher IV
%define %%ZT1        %10  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT2        %11  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT3        %12  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT4        %13  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT5        %14  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT6        %15  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT7        %16  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT8        %17  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT9        %18  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT10       %19  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT11       %20  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT12       %21  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
%define %%ZT13       %22  ;; [clobbered] temporary ZMM (zmm0 - zmm15)
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

        ;; Usable for AVX encoding
%xdefine %%XCIPH0 XWORD(%%ZT1)
%xdefine %%XCIPH1 XWORD(%%ZT2)
%xdefine %%XCIPH2 XWORD(%%ZT3)
%xdefine %%XCIPH3 XWORD(%%ZT4)
%xdefine %%XCIPH4 XWORD(%%ZT5)
%xdefine %%XCIPH5 XWORD(%%ZT6)
%xdefine %%XCIPH6 XWORD(%%ZT7)
%xdefine %%XCIPH7 XWORD(%%ZT8)

%xdefine %%XCRC_TMP    XWORD(%%ZT9)
%xdefine %%XCRC_MUL    XWORD(%%ZT10)
%xdefine %%XCRC_IN_OUT %%XCRC_INIT

%xdefine %%XTMP0  XWORD(%%ZT11)
%xdefine %%XTMP1  XWORD(%%ZT12)

        ;; Usable for AVX512 only encoding
%xdefine %%XCRC0 XWORD(%%ZT16)
%xdefine %%XCRC1 XWORD(%%ZT17)
%xdefine %%XCRC2 XWORD(%%ZT18)
%xdefine %%XCRC3 XWORD(%%ZT19)
%xdefine %%XCRC4 XWORD(%%ZT20)
%xdefine %%XCRC5 XWORD(%%ZT21)
%xdefine %%XCRC6 XWORD(%%ZT22)
%xdefine %%XCRC7 XWORD(%%ZT23)

%xdefine %%XT0 XWORD(%%ZT24)
%xdefine %%XT1 XWORD(%%ZT25)
%xdefine %%XT2 XWORD(%%ZT26)
%xdefine %%XT3 XWORD(%%ZT27)
%xdefine %%XT4 XWORD(%%ZT28)
%xdefine %%XT5 XWORD(%%ZT29)
%xdefine %%XT6 XWORD(%%ZT30)
%xdefine %%XT7 XWORD(%%ZT31)

%xdefine %%XKEY0  XWORD(%%ZT32)

        prefetchw       [%%SRC + 0*64]
        prefetchw       [%%SRC + 1*64]

        vmovdqa64       %%XCRC_MUL, [rel rk1]

        xor     %%OFFSET, %%OFFSET

        cmp     %%NUM_BYTES, 16
        jb      %%_check_partial_block

        vmovdqa64       %%XKEY0, [%%KEYS + 0*16]

        mov     %%NUM_BLOCKS, %%NUM_BYTES
        shr     %%NUM_BLOCKS, 4
        and     %%NUM_BLOCKS, 7
        jz	%%_eq8

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

%assign align_blocks 1
%rep 8
%%_eq %+ align_blocks :
        ;; Start building the pipeline by decrypting number of blocks
        ;; - later cipher & CRC operations get stitched
        AES_CBC_DEC_1_TO_8 %%SRC, %%DST, align_blocks, %%OFFSET, %%NUM_BYTES, \
                           %%XKEY0, %%KEYS, %%XIV, \
                           %%XT0, %%XT1, %%XT2, %%XT3, \
                           %%XT4, %%XT5, %%XT6, %%XT7, \
                           %%XCIPH0, %%XCIPH1, %%XCIPH2, %%XCIPH3, \
                           %%XCIPH4, %%XCIPH5, %%XCIPH6, %%XCIPH7, \
                           %%XTMP0, %%NROUNDS, \
                           %%XCRC0, %%XCRC1, %%XCRC2, %%XCRC3,     \
                           %%XCRC4, %%XCRC5, %%XCRC6, %%XCRC7
        cmp     %%NUM_BYTES, (8*16)
        jae     %%_eq %+ align_blocks %+ _next_8

        ;; Less than 8 blocks remaining in the message:
        ;; - compute CRC on decrypted blocks (minus one, in case it is the last one)
        ;; - then check for any partial block left
%assign align_blocks_for_crc (align_blocks - 1)
        CRC32_1_TO_8    first_crc, %%XCRC_MUL, %%XCRC_IN_OUT, %%XTMP0, align_blocks_for_crc, \
                        %%XCRC0, %%XCRC1, %%XCRC2, %%XCRC3,     \
                        %%XCRC4, %%XCRC5, %%XCRC6, %%XCRC7
        vmovdqa64       %%XCRC0, %%XCRC %+ align_blocks_for_crc
        jmp     %%_check_partial_block

%%_eq %+ align_blocks %+ _next_8:
        ;;  8 or more blocks remaining in the message
        ;; - compute CRC on decrypted blocks while decrypting next 8 blocks
        ;; - next jump to the main loop to do parallel decrypt and crc32
        AES_CBC_DEC_CRC32_1_TO_8 %%SRC, %%DST, 8, align_blocks, %%OFFSET, %%NUM_BYTES, \
                                 %%XKEY0, %%KEYS, %%XIV, \
                                 %%XT0, %%XT1, %%XT2, %%XT3, \
                                 %%XT4, %%XT5, %%XT6, %%XT7, \
                                 %%XCIPH0, %%XCIPH1, %%XCIPH2, %%XCIPH3, \
                                 %%XCIPH4, %%XCIPH5, %%XCIPH6, %%XCIPH7, \
                                 %%XTMP0, %%NROUNDS, \
                                 first_crc, %%XCRC_MUL, %%XCRC_IN_OUT, %%XTMP1, \
                                 %%XCRC0, %%XCRC1, %%XCRC2, %%XCRC3,       \
                                 %%XCRC4, %%XCRC5, %%XCRC6, %%XCRC7
        jmp	%%_main_loop

%assign align_blocks (align_blocks + 1)
%endrep

%%_main_loop:
        cmp     %%NUM_BYTES, (8 * 16)
        jb      %%_exit_loop

        prefetchw       [%%SRC + %%OFFSET + 2*64]
        prefetchw       [%%SRC + %%OFFSET + 3*64]
        ;; Stitched cipher and CRC
        ;; - ciphered blocks: n + 0, n + 1, n + 2, n + 3, n + 4, n + 5, n + 6, n + 7
        ;; - crc'ed blocks: n - 8, n - 7, n - 6, n - 5, n - 4, n - 3, n - 2, n - 1
        AES_CBC_DEC_CRC32_1_TO_8 %%SRC, %%DST, 8, 8, %%OFFSET, %%NUM_BYTES, \
                              %%XKEY0, %%KEYS, %%XIV, \
                              %%XT0, %%XT1, %%XT2, %%XT3, \
                              %%XT4, %%XT5, %%XT6, %%XT7, \
                              %%XCIPH0, %%XCIPH1, %%XCIPH2, %%XCIPH3, \
                              %%XCIPH4, %%XCIPH5, %%XCIPH6, %%XCIPH7, \
                              %%XTMP0, %%NROUNDS, \
                              next_crc, %%XCRC_MUL, %%XCRC_IN_OUT, %%XTMP1, \
                              %%XCRC0, %%XCRC1, %%XCRC2, %%XCRC3,       \
                              %%XCRC4, %%XCRC5, %%XCRC6, %%XCRC7
       jmp	%%_main_loop

%%_exit_loop:
        ;; Calculate CRC for already decrypted blocks
        ;; - keep the last block out from the calculation
        ;;   (this may be a partial block - additional checks follow)
        CRC32_1_TO_8    next_crc, %%XCRC_MUL, %%XCRC_IN_OUT, %%XTMP0, 7, \
                        %%XCRC0, %%XCRC1, %%XCRC2, %%XCRC3,     \
                        %%XCRC4, %%XCRC5, %%XCRC6, %%XCRC7
        vmovdqa64       %%XCRC0, %%XCRC7

%%_check_partial_block:
        or      %%NUM_BYTES, %%NUM_BYTES
        jz      %%_no_partial_bytes

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
        ;;     - max 2 blocks (minus 1) remain for CRC calculation
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

%macro AES_DOCSIS_DEC_CRC32 1
%define %%NROUNDS %1 ; [in] Number of rounds (9 or 13)
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

        prefetchw       [tmp1 + 0*64]
        prefetchw       [tmp1 + 1*64]

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
        mov             tmp2, [job + _msg_len_to_hash_in_bytes]
        ETHERNET_FCS_CRC tmp1, tmp2, rax, xmm15, tmp3, xmm0, xmm1, xmm2, xmm3

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
MKGLOBAL(aes_docsis128_dec_crc32_avx512,function,internal)
aes_docsis128_dec_crc32_avx512:
        endbranch64
        AES_DOCSIS_DEC_CRC32 9

        ret

align 64
MKGLOBAL(aes_docsis256_dec_crc32_avx512,function,internal)
aes_docsis256_dec_crc32_avx512:
        endbranch64
        AES_DOCSIS_DEC_CRC32 13

        ret

mksection stack-noexec

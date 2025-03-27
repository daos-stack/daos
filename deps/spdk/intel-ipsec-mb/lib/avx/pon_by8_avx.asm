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
%include "include/os.asm"
%include "include/memcpy.asm"
%include "include/clear_regs.asm"
%include "include/cet.inc"
%include "include/error.inc"

;;; This is implementation of stitched algorithms: AES128-CTR + CRC32 + BIP
;;; This combination is required by PON/xPON/gPON standard.
;;; Note: BIP is running XOR of double words
;;; Order of operations:
;;; - encrypt: HEC update (XGEM header), CRC32 (Ethernet FCS), AES-CTR and BIP
;;; - decrypt: BIP, AES-CTR and CRC32 (Ethernet FCS)

extern byteswap_const
extern ddq_add_1

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

align 16
ddq_add_1_1:
        dq 0x1, 0x1

;; Precomputed constants for HEC calculation (XGEM header)
;; POLY 0x53900000:
;;         k1    = 0xf9800000
;;         k2    = 0xa0900000
;;         k3    = 0x7cc00000
;;         q     = 0x46b927ec
;;         p_res = 0x53900000

align 16
k3_q:
        dq 0x7cc00000, 0x46b927ec

align 16
p_res:
        dq 0x53900000, 0

align 16
mask_out_top_64bits:
        dq 0xffffffff_ffffffff, 0

mksection .text

%define NUM_AES_ROUNDS 10

%define xcounter xmm0
%define xbip    xmm1
%define xcrc    xmm2
%define xcrckey xmm3
%define xtmp1   xmm4
%define xtmp2   xmm5
%define xtmp3   xmm6
%define xtmp4   xmm7
%define xtmp5   xmm8
%define xtmp6   xmm9
%define xtmp7   xmm10
%define xtmp8   xmm11
%define xtmp9   xmm12
%define xtmp10  xmm13
%define xtmp11  xmm14

%ifdef LINUX
%define arg1    rdi
%define arg2    rsi
%define arg3    rdx
%define arg4    rcx
%define tmp_1   r8
%define tmp_2   r9
%define tmp_3   r10
%define tmp_4   r11
%define tmp_5   r12
%define tmp_6   r13
%define tmp_7   r14
%else
%define arg1    rcx
%define arg2    rdx
%define arg3    r8
%define arg4    r9
%define tmp_1   r10
%define tmp_2   r11
%define tmp_3   rax
%define tmp_4   r12
%define tmp_5   r13
%define tmp_6   r14
%define tmp_7   r15
%endif

%define job     arg1

%define p_in    arg2
%define p_keys  arg3
%define p_out   arg4

%define num_bytes       tmp_1   ; bytes to cipher
%define tmp             tmp_2
%define ctr_check       tmp_3   ; counter block overflow check
%define bytes_to_crc    tmp_4   ; number of bytes to crc ( < num_bytes)

%define ethernet_fcs    tmp_6   ; not used together with tmp3
%define tmp2            tmp_5
%define tmp3            tmp_6

%define write_back_crc   tmp_7
%define decrypt_not_done tmp_7

;;; ============================================================================
;;; Does all AES encryption rounds
%macro AES_ENC_ROUNDS 3
%define %%KP            %1      ; [in] pointer to expanded keys
%define %%N_ROUNDS      %2      ; [in] max rounds (128bit: 10, 12, 14)
%define %%BLOCK         %3      ; [in/out] XMM with encrypted block

%assign round 0
        vpxor           %%BLOCK, %%BLOCK, [%%KP + (round * 16)]

%rep (%%N_ROUNDS - 1)
%assign round (round + 1)
        vaesenc         %%BLOCK, %%BLOCK, [%%KP + (round * 16)]
%endrep

%assign round (round + 1)
        vaesenclast     %%BLOCK, %%BLOCK, [%%KP + (round * 16)]

%endmacro

;;; ============================================================================
;;; Does all AES encryption rounds on 4 blocks
%macro AES_ENC_ROUNDS_4 7
%define %%KP            %1      ; [in] pointer to expanded keys
%define %%N_ROUNDS      %2      ; [in] max rounds (128bit: 10, 12, 14)
%define %%BLOCK1        %3      ; [in/out] XMM with encrypted block
%define %%BLOCK2        %4      ; [in/out] XMM with encrypted block
%define %%BLOCK3        %5      ; [in/out] XMM with encrypted block
%define %%BLOCK4        %6      ; [in/out] XMM with encrypted block
%define %%XT1           %7      ; [clobbered] temporary XMM register

%assign round 0
        vmovdqa         %%XT1, [%%KP + (round * 16)]
        vpxor           %%BLOCK1, %%BLOCK1, %%XT1
        vpxor           %%BLOCK2, %%BLOCK2, %%XT1
        vpxor           %%BLOCK3, %%BLOCK3, %%XT1
        vpxor           %%BLOCK4, %%BLOCK4, %%XT1

%rep (%%N_ROUNDS - 1)
%assign round (round + 1)
        vmovdqa         %%XT1, [%%KP + (round * 16)]
        vaesenc         %%BLOCK1, %%BLOCK1, %%XT1
        vaesenc         %%BLOCK2, %%BLOCK2, %%XT1
        vaesenc         %%BLOCK3, %%BLOCK3, %%XT1
        vaesenc         %%BLOCK4, %%BLOCK4, %%XT1
%endrep

%assign round (round + 1)
        vmovdqa         %%XT1, [%%KP + (round * 16)]
        vaesenclast     %%BLOCK1, %%BLOCK1, %%XT1
        vaesenclast     %%BLOCK2, %%BLOCK2, %%XT1
        vaesenclast     %%BLOCK3, %%BLOCK3, %%XT1
        vaesenclast     %%BLOCK4, %%BLOCK4, %%XT1
%endmacro

;;; ============================================================================
;;; CRC multiply before XOR against data block
%macro CRC_CLMUL 3
%define %%XCRC_IN_OUT   %1 ; [in/out] XMM with CRC (can be anything if "no_crc" below)
%define %%XCRC_MUL      %2 ; [in] XMM with CRC constant  (can be anything if "no_crc" below)
%define %%XTMP          %3 ; [clobbered] temporary XMM

        vpclmulqdq      %%XTMP, %%XCRC_IN_OUT, %%XCRC_MUL, 0x01
        vpclmulqdq      %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%XCRC_MUL, 0x10
        vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%XTMP
%endmacro

;;; ============================================================================
;;; PON stitched algorithm round on a single AES block (16 bytes):
;;;   AES-CTR (optional, depending on %%CIPH)
;;;   - prepares counter block
;;;   - encrypts counter block
;;;   - loads text
;;;   - xor's text against encrypted blocks
;;;   - stores cipher text
;;;   BIP
;;;   - BIP update on 4 x 32-bits
;;;   CRC32
;;;   - CRC32 calculation
;;; Note: via selection of no_crc, no_bip, no_load, no_store different macro
;;;       behaviour can be achieved to match needs of the overall algorithm.
%macro DO_PON 15
%define %%KP            %1      ; [in] GP, pointer to expanded keys
%define %%N_ROUNDS      %2      ; [in] number of AES rounds (10, 12 or 14)
%define %%CTR           %3      ; [in/out] XMM with counter block
%define %%INP           %4      ; [in/out] GP with input text pointer or "no_load"
%define %%OUTP          %5      ; [in/out] GP with output text pointer or "no_store"
%define %%XBIP_IN_OUT   %6      ; [in/out] XMM with BIP value or "no_bip"
%define %%XCRC_IN_OUT   %7      ; [in/out] XMM with CRC (can be anything if "no_crc" below)
%define %%XCRC_MUL      %8      ; [in] XMM with CRC constant (can be anything if "no_crc" below)
%define %%TXMM0         %9      ; [clobbered|out] XMM temporary or data out (no_store)
%define %%TXMM1         %10     ; [clobbered|in] XMM temporary or data in (no_load)
%define %%TXMM2         %11     ; [clobbered] XMM temporary
%define %%CRC_TYPE      %12     ; [in] "first_crc" or "next_crc" or "no_crc"
%define %%DIR           %13     ; [in] "ENC" or "DEC"
%define %%CIPH          %14     ; [in] "CTR" or "NO_CTR"
%define %%CTR_CHECK     %15     ; [in/out] GP with 64bit counter (to identify overflow)

%ifidn %%CIPH, CTR
        ;; prepare counter blocks for encryption
        vpshufb         %%TXMM0, %%CTR, [rel byteswap_const]
        ;; perform 1 increment on whole 128 bits
        add             %%CTR_CHECK, 1
        jc              %%_ctr_overflow
        vpaddq          %%CTR, %%CTR, [rel ddq_add_1]
        jmp             %%_ctr_overflow_done
%%_ctr_overflow:
        vpaddq          %%CTR, %%CTR, [rel ddq_add_1_1]
%%_ctr_overflow_done:
%endif

        ;; CRC calculation
%ifidn %%CRC_TYPE, next_crc
                ;; CRC_MUL macro could be used here but its xor affects
                ;; performance (blocks cipher xor's) so doing CLMUL
                ;; only here and xor is done after the cipher.
                vpclmulqdq      %%TXMM2, %%XCRC_IN_OUT, %%XCRC_MUL, 0x01
                vpclmulqdq      %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%XCRC_MUL, 0x10
%endif

%ifnidn %%INP, no_load
        vmovdqu         %%TXMM1, [%%INP]
%endif

%ifidn %%CIPH, CTR
        ;; AES rounds
        AES_ENC_ROUNDS  %%KP, %%N_ROUNDS, %%TXMM0

        ;; xor plaintext/ciphertext against encrypted counter blocks
        vpxor           %%TXMM0, %%TXMM0, %%TXMM1
%else ;; CIPH = NO_CTR
        ;; register copy is needed as no_load/no_store options need it
        vmovdqa         %%TXMM0, %%TXMM1
%endif ;; CIPH = CTR

%ifnidn %%CRC_TYPE, no_crc
%ifidn %%CRC_TYPE, next_crc
                ;; Finish split CRC_MUL() operation
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXMM2
%endif
%ifidn %%CIPH, CTR
        ;; CRC calculation for ENCRYPTION/DECRYPTION
        ;; - always XOR against plaintext block
%ifidn %%DIR, ENC
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXMM1
%else
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXMM0
%endif                        ; DECRYPT
%else ;; CIPH = NO_CTR
        ;; CRC calculation for NO CIPHER option
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXMM1
%endif ;; CIPH = CTR
%endif ;; CRC_TYPE != NO_CRC

        ;; store the result in the output buffer
%ifnidn %%OUTP, no_store
%ifidn %%CIPH, CTR
        vmovdqu         [%%OUTP], %%TXMM0
%else ;; CIPH = NO_CTR
        vmovdqu         [%%OUTP], %%TXMM1
%endif ;; CIPH = CTR
%endif

        ;; update BIP value - always use cipher text for BIP
%ifnidn %%XBIP_IN_OUT, no_bip
%ifidn %%CIPH, CTR
%ifidn %%DIR, ENC
        vpxor           %%XBIP_IN_OUT, %%XBIP_IN_OUT, %%TXMM0
%else
        vpxor           %%XBIP_IN_OUT, %%XBIP_IN_OUT, %%TXMM1
%endif                          ; DECRYPT
%else ;; CIPH = NO_CTR
        vpxor           %%XBIP_IN_OUT, %%XBIP_IN_OUT, %%TXMM1
%endif ;; CIPH = CTR
%endif ;; !NO_BIP

        ;; increment in/out pointers
%ifnidn %%INP, no_load
        add             %%INP,  16
%endif
%ifnidn %%OUTP, no_store
        add             %%OUTP, 16
%endif
%endmacro                       ; DO_PON

;;; ============================================================================
;;; PON stitched algorithm round on a single AES block (16 bytes):
;;;   AES-CTR (optional, depending on %%CIPH)
;;;   - prepares counter block
;;;   - encrypts counter block
;;;   - loads text
;;;   - xor's text against encrypted blocks
;;;   - stores cipher text
;;;   BIP
;;;   - BIP update on 4 x 32-bits
;;;   CRC32
;;;   - CRC32 calculation
;;; Note: via selection of no_crc, no_bip, no_load, no_store different macro
;;;       behaviour can be achieved to match needs of the overall algorithm.
%macro DO_PON_4 23
%define %%KP            %1      ; [in] GP, pointer to expanded keys
%define %%N_ROUNDS      %2      ; [in] number of AES rounds (10, 12 or 14)
%define %%CTR           %3      ; [in/out] XMM with counter block
%define %%INP           %4      ; [in/out] GP with input text pointer or "no_load"
%define %%OUTP          %5      ; [in/out] GP with output text pointer or "no_store"
%define %%XBIP_IN_OUT   %6      ; [in/out] XMM with BIP value or "no_bip"
%define %%XCRC_IN_OUT   %7      ; [in/out] XMM with CRC (can be anything if "no_crc" below)
%define %%XCRC_MUL      %8      ; [in] XMM with CRC constant (can be anything if "no_crc" below)
%define %%T0            %9      ; [clobbered] XMM temporary
%define %%T1            %10     ; [clobbered] XMM temporary
%define %%T2            %11     ; [clobbered] XMM temporary
%define %%T3            %12     ; [clobbered] XMM temporary
%define %%T4            %13     ; [clobbered] XMM temporary
%define %%T5            %14     ; [clobbered] XMM temporary
%define %%T6            %15     ; [clobbered] XMM temporary
%define %%T7            %16     ; [clobbered] XMM temporary
%define %%T8            %17     ; [clobbered] XMM temporary
%define %%T9            %18     ; [clobbered] XMM temporary
%define %%T10           %19     ; [clobbered] XMM temporary
%define %%CRC_TYPE      %20     ; [in] "first_crc" or "next_crc" or "no_crc"
%define %%DIR           %21     ; [in] "ENC" or "DEC"
%define %%CIPH          %22     ; [in] "CTR" or "NO_CTR"
%define %%CTR_CHECK     %23     ; [in/out] GP with 64bit counter (to identify overflow)

%define %%CTR1 %%T3
%define %%CTR2 %%T4
%define %%CTR3 %%T5
%define %%CTR4 %%T6

%define %%TXT1 %%T7
%define %%TXT2 %%T8
%define %%TXT3 %%T9
%define %%TXT4 %%T10

%ifidn %%CIPH, CTR
        ;; prepare counter blocks for encryption
        vmovdqa         %%T0, [rel ddq_add_1]
        vmovdqa         %%T2, [rel byteswap_const]

        ;; CTR1: copy saved CTR value as CTR1
        vmovdqa         %%CTR1, %%CTR

        cmp             %%CTR_CHECK, 0xffff_ffff_ffff_ffff - 4
        ja              %%_ctr_will_overflow

        ;; case in which 64-bit counter will not overflow
        vpaddq          %%CTR2, %%CTR1, %%T0
        vpaddq          %%CTR3, %%CTR2, %%T0
        vpaddq          %%CTR4, %%CTR3, %%T0
        vpaddq          %%CTR,  %%CTR4, %%T0
        vpshufb         %%CTR1, %%CTR1, %%T2
        vpshufb         %%CTR2, %%CTR2, %%T2
        vpshufb         %%CTR3, %%CTR3, %%T2
        vpshufb         %%CTR4, %%CTR4, %%T2
        add             %%CTR_CHECK, 4
        jmp             %%_ctr_update_done

%%_ctr_will_overflow:
        vmovdqa         %%T1, [rel ddq_add_1_1]
        ;; CTR2: perform 1 increment on whole 128 bits
        add             %%CTR_CHECK, 1
        jc              %%_ctr2_overflow
        vpaddq          %%CTR2, %%CTR1, %%T0
        jmp             %%_ctr2_overflow_done
%%_ctr2_overflow:
        vpaddq          %%CTR2, %%CTR1, %%T1
%%_ctr2_overflow_done:
        vpshufb         %%CTR1, %%CTR1, %%T2

        ;; CTR3: perform 1 increment on whole 128 bits
        add             %%CTR_CHECK, 1
        jc              %%_ctr3_overflow
        vpaddq          %%CTR3, %%CTR2, %%T0
        jmp             %%_ctr3_overflow_done
%%_ctr3_overflow:
        vpaddq          %%CTR3, %%CTR2, %%T1
%%_ctr3_overflow_done:
        vpshufb         %%CTR2, %%CTR2, %%T2

        ;; CTR4: perform 1 increment on whole 128 bits
        add             %%CTR_CHECK, 1
        jc              %%_ctr4_overflow
        vpaddq          %%CTR4, %%CTR3, %%T0
        jmp             %%_ctr4_overflow_done
%%_ctr4_overflow:
        vpaddq          %%CTR4, %%CTR3, %%T1
%%_ctr4_overflow_done:
        vpshufb         %%CTR3, %%CTR3, %%T2

        ;; CTR: perform 1 increment on whole 128 bits (for the next iteration)
        add             %%CTR_CHECK, 1
        jc              %%_ctr_overflow
        vpaddq          %%CTR, %%CTR4, %%T0
        jmp             %%_ctr_overflow_done
%%_ctr_overflow:
        vpaddq          %%CTR, %%CTR4, %%T1
%%_ctr_overflow_done:
        vpshufb         %%CTR4, %%CTR4, %%T2
%%_ctr_update_done:
%endif

%ifidn %%CRC_TYPE, next_crc
                ;; CRC_MUL macro could be used here but its xor affects
                ;; performance (blocks cipher xor's) so doing CLMUL
                ;; only here and xor is done after the cipher.
                vpclmulqdq      %%T2, %%XCRC_IN_OUT, %%XCRC_MUL, 0x01
                vpclmulqdq      %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%XCRC_MUL, 0x10
%endif

        ;; load plaintext/ciphertext
        vmovdqu         %%TXT1, [%%INP]
        vmovdqu         %%TXT2, [%%INP + 16]
        vmovdqu         %%TXT3, [%%INP + 32]
        vmovdqu         %%TXT4, [%%INP + 48]

%ifidn %%CIPH, CTR
        AES_ENC_ROUNDS_4  %%KP, %%N_ROUNDS, %%CTR1, %%CTR2, %%CTR3, %%CTR4, %%T0

        ;; xor plaintext/ciphertext against encrypted counter blocks
        vpxor           %%CTR1, %%CTR1, %%TXT1
        vpxor           %%CTR2, %%CTR2, %%TXT2
        vpxor           %%CTR3, %%CTR3, %%TXT3
        vpxor           %%CTR4, %%CTR4, %%TXT4
%endif ;; CIPH = CTR

%ifidn %%CRC_TYPE, next_crc
                ;; Finish split CRC_MUL() operation
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%T2
%endif
%ifidn %%CIPH, CTR
%ifidn %%DIR, ENC
                ;; CRC calculation for ENCRYPTION (blocks 1 & 2)
                ;; - XOR CRC against plaintext block
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXT1

                CRC_CLMUL       %%XCRC_IN_OUT, %%XCRC_MUL, %%T2
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXT2

                CRC_CLMUL       %%XCRC_IN_OUT, %%XCRC_MUL, %%T2
%else
                ;; CRC calculation for DECRYPTION (blocks 1 & 2)
                ;; - XOR CRC against plaintext block
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%CTR1

                CRC_CLMUL       %%XCRC_IN_OUT, %%XCRC_MUL, %%T2
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%CTR2

                CRC_CLMUL       %%XCRC_IN_OUT, %%XCRC_MUL, %%T2
%endif                        ; DECRYPT
%else ;; CIPH = NO_CTR
                ;; CRC calculation for NO CIPHER option (blocks 1 & 2)
                ;; - XOR CRC against plaintext block
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXT1

                CRC_CLMUL       %%XCRC_IN_OUT, %%XCRC_MUL, %%T2
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXT2

                CRC_CLMUL       %%XCRC_IN_OUT, %%XCRC_MUL, %%T2
%endif ;; CIPH = CTR

        ;; store ciphertext/plaintext
%ifidn %%CIPH, CTR
        vmovdqu         [%%OUTP], %%CTR1
        vmovdqu         [%%OUTP + 16], %%CTR2
        vmovdqu         [%%OUTP + 32], %%CTR3
        vmovdqu         [%%OUTP + 48], %%CTR4
%else ;; CIPH = NO_CTR
        vmovdqu         [%%OUTP], %%TXT1
        vmovdqu         [%%OUTP + 16], %%TXT2
        vmovdqu         [%%OUTP + 32], %%TXT3
        vmovdqu         [%%OUTP + 48], %%TXT4
%endif ;; CIPH = CTR

        ;; update BIP value
%ifidn %%CIPH, CTR
        ;; - always use ciphertext for BIP
%ifidn %%DIR, ENC
        vpxor           %%T0, %%CTR1, %%CTR2
        vpxor           %%T1, %%CTR3, %%CTR4
%else
        vpxor           %%T0, %%TXT1, %%TXT2
        vpxor           %%T1, %%TXT3, %%TXT4
%endif                          ; DECRYPT
%else ;; CIPH = NO_CTR
        vpxor           %%T0, %%TXT1, %%TXT2
        vpxor           %%T1, %%TXT3, %%TXT4
%endif ;; CIPH = CTR
        vpxor           %%XBIP_IN_OUT, %%XBIP_IN_OUT, %%T0
        vpxor           %%XBIP_IN_OUT, %%XBIP_IN_OUT, %%T1

        ;; increment in/out pointers
        add             %%INP,  64
        add             %%OUTP, 64

%ifidn %%CIPH, CTR
%ifidn %%DIR, ENC
                ;; CRC calculation for ENCRYPTION (blocks 3 & 4)
                ;; - XOR CRC against plaintext block
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXT3

                CRC_CLMUL       %%XCRC_IN_OUT, %%XCRC_MUL, %%T2
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXT4
%else
                ;; CRC calculation for DECRYPTION (blocks 3 & 4)
                ;; - XOR CRC against plaintext block
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%CTR3

                CRC_CLMUL       %%XCRC_IN_OUT, %%XCRC_MUL, %%T2
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%CTR4
%endif                        ; DECRYPT
%else ;; CIPH = NO_CTR
                ;; CRC calculation for NO CIPHER option (blocks 3 & 4)
                ;; - XOR CRC against plaintext block
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXT3

                CRC_CLMUL       %%XCRC_IN_OUT, %%XCRC_MUL, %%T2
                vpxor           %%XCRC_IN_OUT, %%XCRC_IN_OUT, %%TXT4
%endif ;; CIPH = CTR

%endmacro                       ; DO_PON_4

;;; ============================================================================
;;; CIPHER and BIP specified number of bytes
%macro CIPHER_BIP_REST 14
%define %%NUM_BYTES   %1        ; [in/clobbered] number of bytes to cipher
%define %%DIR         %2        ; [in] "ENC" or "DEC"
%define %%CIPH        %3        ; [in] "CTR" or "NO_CTR"
%define %%PTR_IN      %4        ; [in/clobbered] GPR pointer to input buffer
%define %%PTR_OUT     %5        ; [in/clobbered] GPR pointer to output buffer
%define %%PTR_KEYS    %6        ; [in] GPR pointer to expanded keys
%define %%XBIP_IN_OUT %7        ; [in/out] XMM 128-bit BIP state
%define %%XCTR_IN_OUT %8        ; [in/out] XMM 128-bit AES counter block
%define %%XMMT1       %9        ; [clobbered] temporary XMM
%define %%XMMT2       %10       ; [clobbered] temporary XMM
%define %%XMMT3       %11       ; [clobbered] temporary XMM
%define %%CTR_CHECK   %12       ; [in/out] GP with 64bit counter (to identify overflow)
%define %%GPT1        %13       ; [clobbered] temporary GP
%define %%GPT2        %14       ; [clobbered] temporary GP

        align 16
%%_cipher_last_blocks:
        cmp     %%NUM_BYTES, 16
        jb      %%_partial_block_left

        DO_PON  %%PTR_KEYS, NUM_AES_ROUNDS, %%XCTR_IN_OUT, %%PTR_IN, %%PTR_OUT, %%XBIP_IN_OUT, \
                no_crc, no_crc, %%XMMT1, %%XMMT2, %%XMMT3, no_crc, %%DIR, %%CIPH, %%CTR_CHECK
        sub     %%NUM_BYTES, 16
        jz      %%_bip_done
        jmp     %%_cipher_last_blocks

%%_partial_block_left:
        simd_load_avx_15_1 %%XMMT2, %%PTR_IN, %%NUM_BYTES

        ;; DO_PON() is not loading nor storing the data in this case:
        ;; XMMT2 = data in
        ;; XMMT1 = data out
        DO_PON  %%PTR_KEYS, NUM_AES_ROUNDS, %%XCTR_IN_OUT, no_load, no_store, no_bip, \
                no_crc, no_crc, %%XMMT1, %%XMMT2, %%XMMT3, no_crc, %%DIR, %%CIPH, %%CTR_CHECK

        ;; bip update for partial block (mask out bytes outside the message)
        lea     %%GPT1, [rel mask_out_top_bytes + 16]
        sub     %%GPT1, %%NUM_BYTES
        vmovdqu %%XMMT3, [%%GPT1]
        ;; put masked cipher text into XMMT2 for BIP update
%ifidn %%DIR, ENC
        vpand   %%XMMT2, %%XMMT1, %%XMMT3
%else
        vpand   %%XMMT2, %%XMMT2, %%XMMT3
%endif
        vpxor   %%XBIP_IN_OUT, %%XMMT2

        ;; store partial bytes in the output buffer
        simd_store_avx_15 %%PTR_OUT, %%XMMT1, %%NUM_BYTES, %%GPT1, %%GPT2

%%_bip_done:
%endmacro                       ; CIPHER_BIP_REST

;; =============================================================================
;; Barrett reduction from 128-bits to 32-bits modulo Ethernet FCS polynomial
%macro CRC32_REDUCE_128_TO_32 5
%define %%CRC   %1         ; [out] GP to store 32-bit Ethernet FCS value
%define %%XCRC  %2         ; [in/clobbered] XMM with CRC
%define %%XT1   %3         ; [clobbered] temporary xmm register
%define %%XT2   %4         ; [clobbered] temporary xmm register
%define %%XT3   %5         ; [clobbered] temporary xmm register

%define %%XCRCKEY %%XT3

        ;;  compute crc of a 128-bit value
        vmovdqa         %%XCRCKEY, [rel rk5]

        ;; 64b fold
        vpclmulqdq      %%XT1, %%XCRC, %%XCRCKEY, 0x00
        vpsrldq         %%XCRC, %%XCRC, 8
        vpxor           %%XCRC, %%XCRC, %%XT1

        ;; 32b fold
        vpslldq         %%XT1, %%XCRC, 4
        vpclmulqdq      %%XT1, %%XT1, %%XCRCKEY, 0x10
        vpxor           %%XCRC, %%XCRC, %%XT1

%%_crc_barrett:
        ;; Barrett reduction
        vpand           %%XCRC, [rel mask2]
        vmovdqa         %%XT1, %%XCRC
        vmovdqa         %%XT2, %%XCRC
        vmovdqa         %%XCRCKEY, [rel rk7]

        vpclmulqdq      %%XCRC, %%XCRCKEY, 0x00
        vpxor           %%XCRC, %%XT2
        vpand           %%XCRC, [rel mask]
        vmovdqa         %%XT2, %%XCRC
        vpclmulqdq      %%XCRC, %%XCRCKEY, 0x10
        vpxor           %%XCRC, %%XT2
        vpxor           %%XCRC, %%XT1
        vpextrd         DWORD(%%CRC), %%XCRC, 2 ; 32-bit CRC value
        not             DWORD(%%CRC)
%endmacro

;; =============================================================================
;; Barrett reduction from 64 bits to 32 bits modulo Ethernet FCS polynomial
%macro CRC32_REDUCE_64_TO_32 5
%define %%CRC   %1         ; [out] GP to store 32-bit Ethernet FCS value
%define %%XCRC  %2         ; [in/clobbered] XMM with CRC
%define %%XT1   %3         ; [clobbered] temporary xmm register
%define %%XT2   %4         ; [clobbered] temporary xmm register
%define %%XT3   %5         ; [clobbered] temporary xmm register

%define %%XCRCKEY %%XT3
        ;; Barrett reduction
        vpand           %%XCRC, [rel mask2]
        vmovdqa         %%XT1, %%XCRC
        vmovdqa         %%XT2, %%XCRC
        vmovdqa         %%XCRCKEY, [rel rk7]

        vpclmulqdq      %%XCRC, %%XCRCKEY, 0x00
        vpxor           %%XCRC, %%XT2
        vpand           %%XCRC, [rel mask]
        vmovdqa         %%XT2, %%XCRC
        vpclmulqdq      %%XCRC, %%XCRCKEY, 0x10
        vpxor           %%XCRC, %%XT2
        vpxor           %%XCRC, %%XT1
        vpextrd         DWORD(%%CRC), %%XCRC, 2 ; 32-bit CRC value
        not             DWORD(%%CRC)
%endmacro

;; =============================================================================
;; Barrett reduction from 128-bits to 32-bits modulo 0x53900000 polynomial

%macro HEC_REDUCE_128_TO_32 4
%define %%XMM_IN_OUT %1         ; [in/out] xmm register with data in and out
%define %%XT1        %2         ; [clobbered] temporary xmm register
%define %%XT2        %3         ; [clobbered] temporary xmm register
%define %%XT3        %4         ; [clobbered] temporary xmm register

%define %%K3_Q  %%XT1
%define %%P_RES %%XT2
%define %%XTMP  %%XT3

        ;; 128 to 64 bit reduction
        vmovdqa         %%K3_Q,  [k3_q]
        vmovdqa         %%P_RES, [p_res]

        vpclmulqdq      %%XTMP, %%XMM_IN_OUT, %%K3_Q, 0x01 ; K3
        vpxor           %%XTMP, %%XTMP, %%XMM_IN_OUT

        vpclmulqdq      %%XTMP, %%XTMP, %%K3_Q, 0x01 ; K3
        vpxor           %%XMM_IN_OUT, %%XTMP, %%XMM_IN_OUT

        vpand           %%XMM_IN_OUT, [rel mask_out_top_64bits]

        ;; 64 to 32 bit reduction
        vpsrldq         %%XTMP, %%XMM_IN_OUT, 4
        vpclmulqdq      %%XTMP, %%XTMP, %%K3_Q, 0x10 ; Q
        vpxor           %%XTMP, %%XTMP, %%XMM_IN_OUT
        vpsrldq         %%XTMP, %%XTMP, 4

        vpclmulqdq      %%XTMP, %%XTMP, %%P_RES, 0x00 ; P
        vpxor           %%XMM_IN_OUT, %%XTMP, %%XMM_IN_OUT
%endmacro

;; =============================================================================
;; Barrett reduction from 64-bits to 32-bits modulo 0x53900000 polynomial

%macro HEC_REDUCE_64_TO_32 4
%define %%XMM_IN_OUT %1         ; [in/out] xmm register with data in and out
%define %%XT1        %2         ; [clobbered] temporary xmm register
%define %%XT2        %3         ; [clobbered] temporary xmm register
%define %%XT3        %4         ; [clobbered] temporary xmm register

%define %%K3_Q  %%XT1
%define %%P_RES %%XT2
%define %%XTMP  %%XT3

        vmovdqa         %%K3_Q,  [k3_q]
        vmovdqa         %%P_RES, [p_res]

        ;; 64 to 32 bit reduction
        vpsrldq         %%XTMP, %%XMM_IN_OUT, 4
        vpclmulqdq      %%XTMP, %%XTMP, %%K3_Q, 0x10 ; Q
        vpxor           %%XTMP, %%XTMP, %%XMM_IN_OUT
        vpsrldq         %%XTMP, %%XTMP, 4

        vpclmulqdq      %%XTMP, %%XTMP, %%P_RES, 0x00 ; P
        vpxor           %%XMM_IN_OUT, %%XTMP, %%XMM_IN_OUT
%endmacro

;; =============================================================================
;; HEC compute and header update for 32-bit XGEM headers
%macro HEC_COMPUTE_32 6
%define %%HEC_IN_OUT %1         ; [in/out] GP register with HEC in LE format
%define %%GT1        %2         ; [clobbered] temporary GP register
%define %%XT1        %3         ; [clobbered] temporary xmm register
%define %%XT2        %4         ; [clobbered] temporary xmm register
%define %%XT3        %5         ; [clobbered] temporary xmm register
%define %%XT4        %6         ; [clobbered] temporary xmm register

        mov             DWORD(%%GT1), DWORD(%%HEC_IN_OUT)
        ;; shift out 13 bits of HEC value for CRC computation
        shr             DWORD(%%GT1), 13

        ;; mask out current HEC value to merge with an updated HEC at the end
        and             DWORD(%%HEC_IN_OUT), 0xffff_e000

        ;; prepare the message for CRC computation
        vmovd           %%XT1, DWORD(%%GT1)
        vpslldq         %%XT1, 4         ; shift left by 32-bits

        HEC_REDUCE_64_TO_32 %%XT1, %%XT2, %%XT3, %%XT4

        ;; extract 32-bit value
        ;; - normally perform 20 bit shift right but bit 0 is a parity bit
        vmovd           DWORD(%%GT1), %%XT1
        shr             DWORD(%%GT1), (20 - 1)

        ;; merge header bytes with updated 12-bit CRC value and
        ;; compute parity
        or              DWORD(%%GT1), DWORD(%%HEC_IN_OUT)
        popcnt          DWORD(%%HEC_IN_OUT), DWORD(%%GT1)
        and             DWORD(%%HEC_IN_OUT), 1
        or              DWORD(%%HEC_IN_OUT), DWORD(%%GT1)
%endmacro

;; =============================================================================
;; HEC compute and header update for 64-bit XGEM headers
%macro HEC_COMPUTE_64 6
%define %%HEC_IN_OUT %1         ; [in/out] GP register with HEC in LE format
%define %%GT1        %2         ; [clobbered] temporary GP register
%define %%XT1        %3         ; [clobbered] temporary xmm register
%define %%XT2        %4         ; [clobbered] temporary xmm register
%define %%XT3        %5         ; [clobbered] temporary xmm register
%define %%XT4        %6         ; [clobbered] temporary xmm register

        mov             %%GT1, %%HEC_IN_OUT
        ;; shift out 13 bits of HEC value for CRC computation
        shr             %%GT1, 13

        ;; mask out current HEC value to merge with an updated HEC at the end
        and             %%HEC_IN_OUT, 0xffff_ffff_ffff_e000

        ;; prepare the message for CRC computation
        vmovq           %%XT1, %%GT1
        vpslldq         %%XT1, 4         ; shift left by 32-bits

        HEC_REDUCE_128_TO_32 %%XT1, %%XT2, %%XT3, %%XT4

        ;; extract 32-bit value
        ;; - normally perform 20 bit shift right but bit 0 is a parity bit
        vmovd           DWORD(%%GT1), %%XT1
        shr             DWORD(%%GT1), (20 - 1)

        ;; merge header bytes with updated 12-bit CRC value and
        ;; compute parity
        or              %%GT1, %%HEC_IN_OUT
        popcnt          %%HEC_IN_OUT, %%GT1
        and             %%HEC_IN_OUT, 1
        or              %%HEC_IN_OUT, %%GT1
%endmacro

;;; ============================================================================
;;; PON stitched algorithm of AES128-CTR, CRC and BIP
;;; - this is master macro that implements encrypt/decrypt API
;;; - calls other macros and directly uses registers
;;;   defined at the top of the file
%macro AES128_CTR_PON 2
%define %%DIR   %1              ; [in] direction "ENC" or "DEC"
%define %%CIPH  %2              ; [in] cipher "CTR" or "NO_CTR"

        push    r12
        push    r13
        push    r14
%ifndef LINUX
        push    r15
%endif

%ifidn %%DIR, ENC
        ;; by default write back CRC for encryption
        mov     DWORD(write_back_crc), 1
%else
        ;; mark decryption as finished
        mov     DWORD(decrypt_not_done), 1
%endif
        ;; START BIP (and update HEC if encrypt direction)
        ;; - load XGEM header (8 bytes) for BIP (not part of encrypted payload)
        ;; - convert it into LE
        ;; - update HEC field in the header
        ;; - convert it into BE
        ;; - store back the header (with updated HEC)
        ;; - start BIP
        ;; (free to use tmp_1, tmp2 and tmp_3 at this stage)
        mov     tmp_2, [job + _src]
        add     tmp_2, [job + _hash_start_src_offset_in_bytes]
        mov     tmp_3, [tmp_2]
%ifidn %%DIR, ENC
        bswap   tmp_3                   ; go to LE
        HEC_COMPUTE_64 tmp_3, tmp_1, xtmp1, xtmp2, xtmp3, xtmp4
        mov     bytes_to_crc, tmp_3
        shr     bytes_to_crc, (48 + 2)  ; PLI = MSB 14 bits
        bswap   tmp_3                   ; go back to BE
        mov     [tmp_2], tmp_3
        vmovq   xbip, tmp_3
%else
        vmovq   xbip, tmp_3
        mov     bytes_to_crc, tmp_3
        bswap   bytes_to_crc            ; go to LE
        shr     bytes_to_crc, (48 + 2)  ; PLI = MSB 14 bits
%endif
        cmp     bytes_to_crc, 4
        ja      %%_crc_not_zero
        ;; XGEM payload shorter or equal to 4 bytes
%ifidn %%DIR, ENC
        ;; On encryption, do not write Ethernet FCS back into the message
        xor     DWORD(write_back_crc), DWORD(write_back_crc)
%else
        ;; Mark decryption as not finished
        ;; - Ethernet FCS is not computed
        ;; - decrypt + BIP to be done at the end
        xor     DWORD(decrypt_not_done), DWORD(decrypt_not_done)
%endif
        mov     DWORD(bytes_to_crc), 4  ; it will be zero after the next line (avoid jmp)
%%_crc_not_zero:
        sub     bytes_to_crc, 4         ; subtract size of the CRC itself

%ifidn %%CIPH, CTR
        ;; - read 16 bytes of IV
        ;; - convert to little endian format
        ;; - save least significant 8 bytes in GP register for overflow check
        mov     tmp, [job + _iv]
        vmovdqu xcounter, [tmp]
        vpshufb xcounter, [rel byteswap_const]
        vmovq   ctr_check, xcounter
%endif

        ;; get input buffer (after XGEM header)
        mov     p_in, [job + _src]
        add     p_in, [job + _cipher_start_src_offset_in_bytes]

        ;; get output buffer
        mov     p_out, [job + _dst]

%ifidn %%CIPH, CTR
        ;; get key pointers
        mov     p_keys, [job + _enc_keys]
%endif

        ;; initial CRC value
        vmovdqa xcrc, [rel init_crc_value]

        ;; load CRC constants
        vmovdqa xcrckey, [rel rk1] ; rk1 and rk2 in xcrckey

        ;; get number of bytes to cipher
%ifidn %%CIPH, CTR
        mov     num_bytes, [job + _msg_len_to_cipher_in_bytes]
%else
        ;; Message length to cipher is 0
        ;; - length is obtained from message length to hash (BIP) minus XGEM header size
        mov     num_bytes, [job + _msg_len_to_hash_in_bytes]
        sub     num_bytes, 8
%endif
        or      bytes_to_crc, bytes_to_crc
        jz      %%_crc_done

        cmp     bytes_to_crc, 32
        jae     %%_at_least_32_bytes

%ifidn %%DIR, DEC
        ;; decrypt the buffer first
        mov     tmp, num_bytes
        CIPHER_BIP_REST tmp, %%DIR, %%CIPH, p_in, p_out, p_keys, xbip, \
                        xcounter, xtmp1, xtmp2, xtmp3, ctr_check, tmp2, tmp3

        ;; correct in/out pointers - go back to start of the buffers
        mov     tmp, num_bytes
        and     tmp, -16        ; partial block handler doesn't increment pointers
        sub     p_in, tmp
        sub     p_out, tmp
%endif                          ; DECRYPTION

        ;; less than 32 bytes
        cmp     bytes_to_crc, 16
        je      %%_exact_16_left
        jl      %%_less_than_16_left
        ;; load the plaintext
%ifidn %%DIR, ENC
        vmovdqu xtmp1, [p_in]
%else
        vmovdqu xtmp1, [p_out]
%endif
        vpxor   xcrc, xtmp1   ; xor the initial crc value
        lea     tmp, [rel pshufb_shf_table]
        vmovdqu xtmp2, [tmp + bytes_to_crc - 16]
        jmp     %%_crc_two_xmms

%%_exact_16_left:
%ifidn %%DIR, ENC
        vmovdqu xtmp1, [p_in]
%else
        vmovdqu xtmp1, [p_out]
%endif
        vpxor   xcrc, xtmp1 ; xor the initial crc value
        jmp     %%_128_done

%%_less_than_16_left:
%ifidn %%DIR, ENC
        simd_load_avx_15_1 xtmp1, p_in, bytes_to_crc
%else
        simd_load_avx_15_1 xtmp1, p_out, bytes_to_crc
%endif
        vpxor   xcrc, xtmp1 ; xor the initial crc value

        cmp     bytes_to_crc, 4
        jl      %%only_less_than_4

        lea     tmp, [rel pshufb_shf_table]
        vmovdqu xtmp1, [tmp + bytes_to_crc]
        vpshufb xcrc, xtmp1
        jmp     %%_128_done

%%only_less_than_4:
        cmp     bytes_to_crc, 3
        jl      %%only_less_than_3
        vpslldq xcrc, 5
        CRC32_REDUCE_64_TO_32 ethernet_fcs, xcrc, xtmp1, xtmp2, xcrckey
        jmp     %%_crc_done

%%only_less_than_3:
        cmp     bytes_to_crc, 2
        jl      %%only_less_than_2
        vpslldq xcrc, 6
        CRC32_REDUCE_64_TO_32 ethernet_fcs, xcrc, xtmp1, xtmp2, xcrckey
        jmp     %%_crc_done
%%only_less_than_2:
        vpslldq xcrc, 7
        CRC32_REDUCE_64_TO_32 ethernet_fcs, xcrc, xtmp1, xtmp2, xcrckey
        jmp     %%_crc_done

%%_at_least_32_bytes:
        cmp     bytes_to_crc, 64
        jb      %%_crc_below_64_bytes

        DO_PON_4 p_keys, NUM_AES_ROUNDS, xcounter, p_in, p_out, xbip, \
                xcrc, xcrckey, xtmp1, xtmp2, xtmp3, xtmp4, xtmp5, xtmp6, \
                xtmp7, xtmp8, xtmp9, xtmp10, xtmp11, first_crc, %%DIR, \
                %%CIPH, ctr_check
        sub     num_bytes, 64
        sub     bytes_to_crc, 64
%ifidn %%DIR, ENC
        jz      %%_128_done
%endif

        align 16
%%_main_loop_64:
        cmp     bytes_to_crc, 64
        jb      %%_main_loop

        DO_PON_4 p_keys, NUM_AES_ROUNDS, xcounter, p_in, p_out, xbip, \
                xcrc, xcrckey, xtmp1, xtmp2, xtmp3, xtmp4, xtmp5, xtmp6, \
                xtmp7, xtmp8, xtmp9, xtmp10, xtmp11, next_crc, %%DIR, \
                %%CIPH, ctr_check
        sub     num_bytes, 64
        sub     bytes_to_crc, 64
%ifidn %%DIR, ENC
        jz      %%_128_done
%endif
        jmp     %%_main_loop_64

%%_crc_below_64_bytes:
        DO_PON  p_keys, NUM_AES_ROUNDS, xcounter, p_in, p_out, xbip, \
                xcrc, xcrckey, xtmp1, xtmp2, xtmp3, first_crc, %%DIR, \
                %%CIPH, ctr_check
        sub     num_bytes, 16
        sub     bytes_to_crc, 16

        align 16
%%_main_loop:
        cmp     bytes_to_crc, 16
        jb      %%_exit_loop
        DO_PON  p_keys, NUM_AES_ROUNDS, xcounter, p_in, p_out, xbip, \
                xcrc, xcrckey, xtmp1, xtmp2, xtmp3, next_crc, %%DIR, \
                %%CIPH, ctr_check
        sub     num_bytes, 16
        sub     bytes_to_crc, 16
%ifidn %%DIR, ENC
        jz      %%_128_done
%endif
        jmp     %%_main_loop

%%_exit_loop:

%ifidn %%DIR, DEC
        ;; decrypt rest of the message including CRC and optional padding
        mov     tmp, num_bytes

        CIPHER_BIP_REST tmp, %%DIR, %%CIPH, p_in, p_out, p_keys, xbip, \
                        xcounter, xtmp1, xtmp2, xtmp3, ctr_check, tmp2, tmp3

        mov     tmp, num_bytes  ; correct in/out pointers - to point before cipher & BIP
        and     tmp, -16        ; partial block handler doesn't increment pointers
        sub     p_in, tmp
        sub     p_out, tmp

        or      bytes_to_crc, bytes_to_crc
        jz      %%_128_done
%endif                          ; DECRYPTION

        ;; Partial bytes left - complete CRC calculation
        lea             tmp, [rel pshufb_shf_table]
        vmovdqu         xtmp2, [tmp + bytes_to_crc]
        ;; @note: in case of in-place operation (default) this load is
        ;; creating store-to-load problem.
        ;; However, there is no easy way to address it at the moment.
%%_crc_two_xmms:
%ifidn %%DIR, ENC
        vmovdqu         xtmp1, [p_in - 16 + bytes_to_crc]  ; xtmp1 = data for CRC
%else
        vmovdqu         xtmp1, [p_out - 16 + bytes_to_crc]  ; xtmp1 = data for CRC
%endif
        vmovdqa         xtmp3, xcrc
        vpshufb         xcrc, xtmp2  ; top num_bytes with LSB xcrc
        vpxor           xtmp2, [rel mask3]
        vpshufb         xtmp3, xtmp2 ; bottom (16 - num_bytes) with MSB xcrc

        ;; data bytes_to_crc (top) blended with MSB bytes of CRC (bottom)
        vpblendvb       xtmp3, xtmp1, xtmp2

        ;; final CRC calculation
        vpclmulqdq      xtmp1, xcrc, xcrckey, 0x01
        vpclmulqdq      xcrc, xcrc, xcrckey, 0x10
        vpxor           xcrc, xtmp3
        vpxor           xcrc, xtmp1

%%_128_done:
        CRC32_REDUCE_128_TO_32 ethernet_fcs, xcrc, xtmp1, xtmp2, xcrckey

%%_crc_done:
        ;; @todo - store-to-load problem in ENC case (to be fixed later)
        ;; - store CRC in input buffer and authentication tag output
        ;; - encrypt remaining bytes
%ifidn %%DIR, ENC
        or      DWORD(write_back_crc), DWORD(write_back_crc)
        jz      %%_skip_crc_write_back
        mov     [p_in + bytes_to_crc], DWORD(ethernet_fcs)
%%_skip_crc_write_back:
%endif
        mov     tmp, [job + _auth_tag_output]
        mov     [tmp + 4], DWORD(ethernet_fcs)

        or      num_bytes, num_bytes
        jz      %%_do_not_cipher_the_rest

        ;; encrypt rest of the message
        ;; - partial bytes including CRC and optional padding
        ;; decrypt rest of the message
        ;; - this may only happen when XGEM payload is short and padding is added
%ifidn %%DIR, DEC
        or      DWORD(decrypt_not_done), DWORD(decrypt_not_done)
        jnz     %%_do_not_cipher_the_rest
%endif
        CIPHER_BIP_REST num_bytes, %%DIR, %%CIPH, p_in, p_out, p_keys, xbip, \
                        xcounter, xtmp1, xtmp2, xtmp3, ctr_check, tmp2, tmp3

%%_do_not_cipher_the_rest:

        ;; finalize BIP
        vpsrldq xtmp1, xbip, 4
        vpsrldq xtmp2, xbip, 8
        vpsrldq xtmp3, xbip, 12
        vpxor   xtmp1, xtmp1, xtmp2
        vpxor   xbip, xbip, xtmp3
        vpxor   xbip, xbip, xtmp1
        vmovd   [tmp], xbip     ; tmp already holds _auth_tag_output

        ;; set job status
        or      dword [job + _status], IMB_STATUS_COMPLETED

        ;;  return job
        mov     rax, job

%ifndef LINUX
        pop     r15
%endif
        pop     r14
        pop     r13
        pop     r12

%ifdef SAFE_DATA
	clear_all_xmms_avx_asm
%endif ;; SAFE_DATA

%endmacro                       ; AES128_CTR_PON

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; submit_job_pon_enc_avx(IMB_JOB *job)
align 64
MKGLOBAL(submit_job_pon_enc_avx,function,internal)
submit_job_pon_enc_avx:
        AES128_CTR_PON ENC, CTR
        ret

;;; submit_job_pon_dec_avx(IMB_JOB *job)
align 64
MKGLOBAL(submit_job_pon_dec_avx,function,internal)
submit_job_pon_dec_avx:
        AES128_CTR_PON DEC, CTR
        ret

;;; submit_job_pon_enc_no_ctr_avx(IMB_JOB *job)
align 64
MKGLOBAL(submit_job_pon_enc_no_ctr_avx,function,internal)
submit_job_pon_enc_no_ctr_avx:
        AES128_CTR_PON ENC, NO_CTR
        ret

;;; submit_job_pon_dec_no_ctr_avx(IMB_JOB *job)
align 64
MKGLOBAL(submit_job_pon_dec_no_ctr_avx,function,internal)
submit_job_pon_dec_no_ctr_avx:
        AES128_CTR_PON DEC, NO_CTR
        ret

;; uint32_t hec_32_sse(const uint8_t *in)
align 64
MKGLOBAL(hec_32_avx,function,)
hec_32_avx:
        endbranch64

%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check in != NULL
        or      arg1, arg1
        jz      error_hec32
%endif

        mov     eax, [arg1]
        bswap   eax
        HEC_COMPUTE_32 rax, tmp_1, xtmp1, xtmp2, xtmp3, xtmp4
        bswap   eax
        ret

%ifdef SAFE_PARAM
error_hec32:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check in != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_SRC

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax

        ret
%endif

;; uint32_t hec_64_sse(const uint8_t *in)
align 64
MKGLOBAL(hec_64_avx,function,)
hec_64_avx:
        endbranch64

%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check in != NULL
        or      arg1, arg1
        jz      error_hec64
%endif

        mov     rax, [arg1]
        bswap   rax
        HEC_COMPUTE_64 rax, tmp_1, xtmp1, xtmp2, xtmp3, xtmp4
        bswap   rax
        ret

%ifdef SAFE_PARAM
error_hec64:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check in != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_SRC

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax

        ret
%endif

mksection stack-noexec

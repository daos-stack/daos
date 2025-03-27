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
;;; - encrypt: CRC32, AES-CTR and BIP
;;; - decrypt: BIP, AES-CTR and CRC32

%ifndef DEC_FN_NAME
%define DEC_FN_NAME submit_job_pon_dec_sse
%endif
%ifndef ENC_FN_NAME
%define ENC_FN_NAME submit_job_pon_enc_sse
%endif
%ifndef ENC_NO_CTR_FN_NAME
%define ENC_NO_CTR_FN_NAME submit_job_pon_enc_no_ctr_sse
%endif
%ifndef DEC_NO_CTR_FN_NAME
%define DEC_NO_CTR_FN_NAME submit_job_pon_dec_no_ctr_sse
%endif
%ifndef HEC_32
%define HEC_32 hec_32_sse
%endif
%ifndef HEC_64
%define HEC_64 hec_64_sse
%endif

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

;; note: leave xmm0 free for implicit blend
%define xcounter xmm7
%define xbip    xmm1
%define xcrc    xmm2
%define xcrckey xmm3
%define xtmp1   xmm4
%define xtmp2   xmm5
%define xtmp3   xmm6
%define xtmp4   xmm8

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
%define bytes_to_crc    tmp_4   ; number of bytes to CRC ( < num_bytes)

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
        pxor            %%BLOCK, [%%KP + (round * 16)]

%rep (%%N_ROUNDS - 1)
%assign round (round + 1)
        aesenc          %%BLOCK, [%%KP + (round * 16)]
%endrep

%assign round (round + 1)
        aesenclast      %%BLOCK, [%%KP + (round * 16)]

%endmacro

;;; ============================================================================
;;; PON stitched algorithm round on a single AES block (16 bytes):
;;;   AES-CTR (optional, depending on %%CIPH)
;;;   - prepares counter blocks
;;;   - encrypts counter blocks
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
%define %%XCRC_MUL      %8      ; [in] XMM with CRC multiplier constant (can be anything if "no_crc" below)
%define %%TXMM0         %9      ; [clobbered|out] XMM temporary or data out (no_store)
%define %%TXMM1         %10     ; [clobbered|in] XMM temporary or data in (no_load)
%define %%TXMM2         %11     ; [clobbered] XMM temporary
%define %%CRC_TYPE      %12     ; [in] "first_crc" or "next_crc" or "no_crc"
%define %%DIR           %13     ; [in] "ENC" or "DEC"
%define %%CIPH          %14     ; [in] "CTR" or "NO_CTR"
%define %%CTR_CHECK     %15     ; [in/out] GP with 64bit counter (to identify overflow)

%ifidn %%CIPH, CTR
        ;; prepare counter blocks for encryption
        movdqa          %%TXMM0, %%CTR
        pshufb          %%TXMM0, [rel byteswap_const]
        ;; perform 1 increment on whole 128 bits
        movdqa          %%TXMM2,  [rel ddq_add_1]
        paddq           %%CTR, %%TXMM2
        add             %%CTR_CHECK, 1
        jnc             %%_no_ctr_overflow
        ;; Add 1 to the top 64 bits. First shift left value 1 by 64 bits.
        pslldq          %%TXMM2, 8
        paddq           %%CTR, %%TXMM2
%%_no_ctr_overflow:
%endif
        ;; CRC calculation
%ifidn %%CRC_TYPE, next_crc
        movdqa          %%TXMM2, %%XCRC_IN_OUT
        pclmulqdq       %%TXMM2, %%XCRC_MUL, 0x01
        pclmulqdq       %%XCRC_IN_OUT, %%XCRC_MUL, 0x10
%endif

%ifnidn %%INP, no_load
        movdqu          %%TXMM1, [%%INP]
%endif

%ifidn %%CIPH, CTR
        ;; AES rounds
        AES_ENC_ROUNDS  %%KP, %%N_ROUNDS, %%TXMM0

        ;; xor plaintext/ciphertext against encrypted counter blocks
        pxor            %%TXMM0, %%TXMM1
%else ;; CIPH = NO_CTR
        ;; if no encryption needs to be done, move from input to output reg
        movdqa          %%TXMM0, %%TXMM1
%endif ;; CIPH = CTR

%ifidn %%CIPH, CTR
%ifidn %%DIR, ENC
        ;; CRC calculation for ENCRYPTION
%ifidn %%CRC_TYPE, first_crc
        ;; in the first run just XOR initial CRC with the first block
        pxor            %%XCRC_IN_OUT, %%TXMM1
%endif
%ifidn %%CRC_TYPE, next_crc
        ;; - XOR results of CLMUL's together
        ;; - then XOR against text block
        pxor            %%XCRC_IN_OUT, %%TXMM2
        pxor            %%XCRC_IN_OUT, %%TXMM1
%endif
%else
        ;; CRC calculation for DECRYPTION
%ifidn %%CRC_TYPE, first_crc
        ;; in the first run just XOR initial CRC with the first block
        pxor            %%XCRC_IN_OUT, %%TXMM0
%endif
%ifidn %%CRC_TYPE, next_crc
        ;; - XOR results of CLMUL's together
        ;; - then XOR against text block
        pxor            %%XCRC_IN_OUT, %%TXMM2
        pxor            %%XCRC_IN_OUT, %%TXMM0
%endif
%endif                        ; DECRYPT
%else ;; CIPH = NO_CTR
        ;; CRC calculation for DECRYPTION
%ifidn %%CRC_TYPE, first_crc
        ;; in the first run just XOR initial CRC with the first block
        pxor            %%XCRC_IN_OUT, %%TXMM1
%endif
%ifidn %%CRC_TYPE, next_crc
        ;; - XOR results of CLMUL's together
        ;; - then XOR against text block
        pxor            %%XCRC_IN_OUT, %%TXMM2
        pxor            %%XCRC_IN_OUT, %%TXMM1
%endif

%endif ;; CIPH = CTR

        ;; store the result in the output buffer
%ifnidn %%OUTP, no_store
        movdqu          [%%OUTP], %%TXMM0
%endif

        ;; update BIP value - always use cipher text for BIP
%ifidn %%DIR, ENC
%ifnidn %%XBIP_IN_OUT, no_bip
        pxor            %%XBIP_IN_OUT, %%TXMM0
%endif
%else
%ifnidn %%XBIP_IN_OUT, no_bip
        pxor            %%XBIP_IN_OUT, %%TXMM1
%endif
%endif                          ; DECRYPT

        ;; increment in/out pointers
%ifnidn %%INP, no_load
        add             %%INP,  16
%endif
%ifnidn %%OUTP, no_store
        add             %%OUTP, 16
%endif
%endmacro                       ; DO_PON

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

%%_cipher_last_blocks:
        cmp     %%NUM_BYTES, 16
        jb      %%_partial_block_left

        DO_PON  %%PTR_KEYS, NUM_AES_ROUNDS, %%XCTR_IN_OUT, %%PTR_IN, %%PTR_OUT, %%XBIP_IN_OUT, \
                no_crc, no_crc, %%XMMT1, %%XMMT2, %%XMMT3, no_crc, %%DIR, %%CIPH, %%CTR_CHECK
        sub     %%NUM_BYTES, 16
        jz      %%_bip_done
        jmp     %%_cipher_last_blocks

%%_partial_block_left:
        simd_load_sse_15_1 %%XMMT2, %%PTR_IN, %%NUM_BYTES

        ;; DO_PON() is not loading nor storing the data in this case:
        ;; XMMT2 = data in
        ;; XMMT1 = data out
        DO_PON  %%PTR_KEYS, NUM_AES_ROUNDS, %%XCTR_IN_OUT, no_load, no_store, no_bip, \
                no_crc, no_crc, %%XMMT1, %%XMMT2, %%XMMT3, no_crc, %%DIR, %%CIPH, %%CTR_CHECK

        ;; BIP update for partial block (mask out bytes outside the message)
        lea     %%GPT1, [rel mask_out_top_bytes + 16]
        sub     %%GPT1, %%NUM_BYTES
        movdqu  %%XMMT3, [%%GPT1]
        ;; put masked cipher text into XMMT2 for BIP update
%ifidn %%DIR, ENC
        movdqa  %%XMMT2, %%XMMT1
        pand    %%XMMT2, %%XMMT3
%else
        pand    %%XMMT2, %%XMMT3
%endif
        pxor    %%XBIP_IN_OUT, %%XMMT2

        ;; store partial bytes in the output buffer
        simd_store_sse_15 %%PTR_OUT, %%XMMT1, %%NUM_BYTES, %%GPT1, %%GPT2

%%_bip_done:
%endmacro                       ; CIPHER_BIP_REST

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
        pand            %%XCRC, [rel mask2]
        movdqa          %%XT1, %%XCRC
        movdqa          %%XT2, %%XCRC
        movdqa          %%XCRCKEY, [rel rk7]

        pclmulqdq       %%XCRC, %%XCRCKEY, 0x00
        pxor            %%XCRC, %%XT2
        pand            %%XCRC, [rel mask]
        movdqa          %%XT2, %%XCRC
        pclmulqdq       %%XCRC, %%XCRCKEY, 0x10
        pxor            %%XCRC, %%XT2
        pxor            %%XCRC, %%XT1
        pextrd          DWORD(%%CRC), %%XCRC, 2 ; 32-bit CRC value
        not             DWORD(%%CRC)
%endmacro

;; =============================================================================
;; Barrett reduction from 128-bits to 32-bits modulo Ethernet FCS polynomial

%macro CRC32_REDUCE_128_TO_32 5
%define %%CRC   %1         ; [out] GP to store 32-bit Ethernet FCS value
%define %%XCRC  %2         ; [in/clobbered] XMM with CRC
%define %%XT1   %3         ; [clobbered] temporary xmm register
%define %%XT2   %4         ; [clobbered] temporary xmm register
%define %%XT3   %5         ; [clobbered] temporary xmm register

%define %%XCRCKEY %%XT3

        ;;  compute CRC of a 128-bit value
        movdqa          %%XCRCKEY, [rel rk5]

        ;; 64b fold
        movdqa          %%XT1, %%XCRC
        pclmulqdq       %%XT1, %%XCRCKEY, 0x00
        psrldq          %%XCRC, 8
        pxor            %%XCRC, %%XT1

        ;; 32b fold
        movdqa          %%XT1, %%XCRC
        pslldq          %%XT1, 4
        pclmulqdq       %%XT1, %%XCRCKEY, 0x10
        pxor            %%XCRC, %%XT1

%%_crc_barrett:
        ;; Barrett reduction
        pand            %%XCRC, [rel mask2]
        movdqa          %%XT1, %%XCRC
        movdqa          %%XT2, %%XCRC
        movdqa          %%XCRCKEY, [rel rk7]

        pclmulqdq       %%XCRC, %%XCRCKEY, 0x00
        pxor            %%XCRC, %%XT2
        pand            %%XCRC, [rel mask]
        movdqa          %%XT2, %%XCRC
        pclmulqdq       %%XCRC, %%XCRCKEY, 0x10
        pxor            %%XCRC, %%XT2
        pxor            %%XCRC, %%XT1
        pextrd          DWORD(%%CRC), %%XCRC, 2 ; 32-bit CRC value
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
        movdqa          %%K3_Q,  [k3_q]
        movdqa          %%P_RES, [p_res]

        movdqa          %%XTMP, %%XMM_IN_OUT
        pclmulqdq       %%XTMP, %%K3_Q, 0x01 ; K3
        pxor            %%XTMP, %%XMM_IN_OUT

        pclmulqdq       %%XTMP, %%K3_Q, 0x01 ; K3
        pxor            %%XMM_IN_OUT, %%XTMP

        pand            %%XMM_IN_OUT, [rel mask_out_top_64bits]

        ;; 64 to 32 bit reduction
        movdqa          %%XTMP, %%XMM_IN_OUT
        psrldq          %%XTMP, 4
        pclmulqdq       %%XTMP, %%K3_Q, 0x10 ; Q
        pxor            %%XTMP, %%XMM_IN_OUT
        psrldq          %%XTMP, 4

        pclmulqdq       %%XTMP, %%P_RES, 0x00 ; P
        pxor            %%XMM_IN_OUT, %%XTMP
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

        movdqa          %%K3_Q,  [k3_q]
        movdqa          %%P_RES, [p_res]

        ;; 64 to 32 bit reduction
        movdqa          %%XTMP, %%XMM_IN_OUT
        psrldq          %%XTMP, 4
        pclmulqdq       %%XTMP, %%K3_Q, 0x10 ; Q
        pxor            %%XTMP, %%XMM_IN_OUT
        psrldq          %%XTMP, 4

        pclmulqdq       %%XTMP, %%P_RES, 0x00 ; P
        pxor            %%XMM_IN_OUT, %%XTMP
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
        movd            %%XT1, DWORD(%%GT1)
        pslldq          %%XT1, 4         ; shift left by 32-bits

        HEC_REDUCE_64_TO_32 %%XT1, %%XT2, %%XT3, %%XT4

        ;; extract 32-bit value
        ;; - normally perform 20 bit shift right but bit 0 is a parity bit
        movd            DWORD(%%GT1), %%XT1
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
        movq            %%XT1, %%GT1
        pslldq          %%XT1, 4         ; shift left by 32-bits

        HEC_REDUCE_128_TO_32 %%XT1, %%XT2, %%XT3, %%XT4

        ;; extract 32-bit value
        ;; - normally perform 20 bit shift right but bit 0 is a parity bit
        movd            DWORD(%%GT1), %%XT1
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
        ;; (free to use tmp_1, tmp_2 and tmp_3 at this stage)
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
        movq    xbip, tmp_3
%else
        movq    xbip, tmp_3
        mov     bytes_to_crc, tmp_3
        bswap   bytes_to_crc            ; go to LE
        shr     bytes_to_crc, (48 + 2)  ; PLI = MSB 14 bits
%endif
        cmp     bytes_to_crc, 4
        ja      %%_crc_not_zero
        ;; XGEM payload shorter or equal to 4 bytes
%ifidn %%DIR, ENC
        ;; Don't write Ethernet FCS on encryption
       xor     DWORD(write_back_crc), DWORD(write_back_crc)
%else
        ;; Mark decryption as not finished
        ;; - Ethernet FCS is not computed
        ;; - decrypt + BIP to be done at the end
        xor     DWORD(decrypt_not_done), DWORD(decrypt_not_done)
%endif
        mov     DWORD(bytes_to_crc), 4  ; it will be zero after the sub (avoid jmp)
%%_crc_not_zero:
        sub     bytes_to_crc, 4         ; subtract size of the CRC itself

%ifidn %%CIPH, CTR
        ;; - read 16 bytes of IV
        ;; - convert to little endian format
        ;; - save least significant 8 bytes in GP register for overflow check
        mov     tmp, [job + _iv]
        movdqu  xcounter, [tmp]
        pshufb  xcounter, [rel byteswap_const]
        movq    ctr_check, xcounter
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
        movdqa  xcrc, [rel init_crc_value]

        ;; load CRC constants
        movdqa  xcrckey, [rel rk1] ; rk1 and rk2 in xcrckey

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
        movdqu  xtmp1, [p_in]
%else
        movdqu  xtmp1, [p_out]
%endif
        pxor    xcrc, xtmp1   ; xor the initial crc value
        ;; Partial bytes left - complete CRC calculation
        lea     tmp, [rel pshufb_shf_table]
        movdqu  xtmp2, [tmp + bytes_to_crc - 16]
        jmp     %%_crc_two_xmms

%%_exact_16_left:
%ifidn %%DIR, ENC
        movdqu  xtmp1, [p_in]
%else
        movdqu  xtmp1, [p_out]
%endif
        pxor    xcrc, xtmp1 ; xor the initial CRC value
        jmp     %%_128_done

%%_less_than_16_left:
%ifidn %%DIR, ENC
        simd_load_sse_15_1 xtmp1, p_in, bytes_to_crc
%else
        simd_load_sse_15_1 xtmp1, p_out, bytes_to_crc
%endif
        pxor    xcrc, xtmp1 ; xor the initial CRC value

        cmp     bytes_to_crc, 4
        jl      %%only_less_than_4

        lea     tmp, [rel pshufb_shf_table]
        movdqu  xtmp1, [tmp + bytes_to_crc]
        pshufb  xcrc, xtmp1
        jmp     %%_128_done

%%only_less_than_4:
        cmp     bytes_to_crc, 3
        jl      %%only_less_than_3
        pslldq  xcrc, 5
        CRC32_REDUCE_64_TO_32 ethernet_fcs, xcrc, xtmp1, xtmp2, xcrckey
        jmp     %%_crc_done

%%only_less_than_3:
        cmp     bytes_to_crc, 2
        jl      %%only_less_than_2
        pslldq  xcrc, 6
        CRC32_REDUCE_64_TO_32 ethernet_fcs, xcrc, xtmp1, xtmp2, xcrckey
        jmp     %%_crc_done
%%only_less_than_2:
        pslldq  xcrc, 7
        CRC32_REDUCE_64_TO_32 ethernet_fcs, xcrc, xtmp1, xtmp2, xcrckey
        jmp     %%_crc_done

%%_at_least_32_bytes:
        DO_PON  p_keys, NUM_AES_ROUNDS, xcounter, p_in, p_out, xbip, \
                xcrc, xcrckey, xtmp1, xtmp2, xtmp3, first_crc, %%DIR, %%CIPH, ctr_check
        sub     num_bytes, 16
        sub     bytes_to_crc, 16

%%_main_loop:
        cmp     bytes_to_crc, 16
        jb      %%_exit_loop
        DO_PON  p_keys, NUM_AES_ROUNDS, xcounter, p_in, p_out, xbip, \
                xcrc, xcrckey, xtmp1, xtmp2, xtmp3, next_crc, %%DIR, %%CIPH, ctr_check
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
        movdqu          xtmp2, [tmp + bytes_to_crc]
%%_crc_two_xmms:
%ifidn %%DIR, ENC
        movdqu          xtmp1, [p_in - 16 + bytes_to_crc]  ; xtmp1 = data for CRC
%else
        movdqu          xtmp1, [p_out - 16 + bytes_to_crc]  ; xtmp1 = data for CRC
%endif
        movdqa          xtmp3, xcrc
        pshufb          xcrc, xtmp2  ; top num_bytes with LSB xcrc
        pxor            xtmp2, [rel mask3]
        pshufb          xtmp3, xtmp2 ; bottom (16 - num_bytes) with MSB xcrc

        ;; data num_bytes (top) blended with MSB bytes of CRC (bottom)
        movdqa          xmm0, xtmp2
        pblendvb        xtmp3, xtmp1 ; xmm0 implicit

        ;; final CRC calculation
        movdqa          xtmp1, xcrc
        pclmulqdq       xtmp1, xcrckey, 0x01
        pclmulqdq       xcrc, xcrckey, 0x10
        pxor            xcrc, xtmp3
        pxor            xcrc, xtmp1

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
        movdqa  xtmp1, xbip
        movdqa  xtmp2, xbip
        movdqa  xtmp3, xbip
        psrldq  xtmp1, 4
        psrldq  xtmp2, 8
        psrldq  xtmp3, 12
        pxor    xtmp1, xtmp2
        pxor    xbip, xtmp3
        pxor    xbip, xtmp1
        movd    [tmp], xbip

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
	clear_all_xmms_sse_asm
%endif ;; SAFE_DATA

%endmacro                       ; AES128_CTR_PON

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; aes_cntr_128_pon_enc_sse(IMB_JOB *job)
align 32
MKGLOBAL(ENC_FN_NAME,function,internal)
ENC_FN_NAME:
        AES128_CTR_PON ENC, CTR
        ret

;;; aes_cntr_128_pon_dec_sse(IMB_JOB *job)
align 32
MKGLOBAL(DEC_FN_NAME,function,internal)
DEC_FN_NAME:
        AES128_CTR_PON DEC, CTR
        ret

;;; aes_cntr_128_pon_enc_no_ctr_sse(IMB_JOB *job)
align 32
MKGLOBAL(ENC_NO_CTR_FN_NAME,function,internal)
ENC_NO_CTR_FN_NAME:
        AES128_CTR_PON ENC, NO_CTR
        ret

;;; aes_cntr_128_pon_dec_no_ctr_sse(IMB_JOB *job)
align 32
MKGLOBAL(DEC_NO_CTR_FN_NAME,function,internal)
DEC_NO_CTR_FN_NAME:
        AES128_CTR_PON DEC, NO_CTR
        ret

;; uint32_t hec_32_sse(const uint8_t *in)
align 64
MKGLOBAL(HEC_32,function,)
HEC_32:
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
MKGLOBAL(HEC_64,function,)
HEC_64:
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

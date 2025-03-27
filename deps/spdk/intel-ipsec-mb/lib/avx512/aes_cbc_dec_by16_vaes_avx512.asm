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

%include "include/os.asm"
%include "include/reg_sizes.asm"
%include "include/aes_common.asm"
%include "include/clear_regs.asm"
%include "include/cet.inc"
%define zIV        zmm0
%define zBLK_0_3   zmm1
%define zBLK_4_7   zmm2
%define zBLK_8_11  zmm3
%define zBLK_12_15 zmm4
%define zTMP0      zmm5
%define zTMP1      zmm6
%define zTMP2      zmm7
%define zTMP3      zmm8

%define ZKEY0      zmm17
%define ZKEY1      zmm18
%define ZKEY2      zmm19
%define ZKEY3      zmm20
%define ZKEY4      zmm21
%define ZKEY5      zmm22
%define ZKEY6      zmm23
%define ZKEY7      zmm24
%define ZKEY8      zmm25
%define ZKEY9      zmm26
%define ZKEY10     zmm27
%define ZKEY11     zmm28
%define ZKEY12     zmm29
%define ZKEY13     zmm30
%define ZKEY14     zmm31

%ifdef LINUX
%define p_in       rdi
%define p_IV       rsi
%define p_keys     rdx
%define p_out      rcx
%define num_bytes  r8
%define next_iv    r9
%else
%define p_in       rcx
%define p_IV       rdx
%define p_keys     r8
%define p_out      r9
%define num_bytes  rax
%define next_iv    r11
%endif

%define tmp        r10
%define tmp2       r11

%ifdef CBCS
%define OFFSET 160
%else
%define OFFSET 16
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; macro to preload keys
;;; - uses ZKEY[0-14] registers (ZMM)
%macro LOAD_KEYS 2
%define %%KEYS          %1      ; [in] key pointer
%define %%NROUNDS       %2      ; [in] numerical value, number of AES rounds
                                ;      excluding 1st and last rounds.
                                ;      Example: AES-128 -> value 9

%assign i 0
%rep (%%NROUNDS + 2)
        vbroadcastf64x2 ZKEY %+ i, [%%KEYS + 16*i]
%assign i (i + 1)
%endrep

%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; This macro is used to "cool down" pipeline after DECRYPT_16_PARALLEL macro
;;; code as the number of final blocks is variable.
;;; Processes the last %%num_final_blocks blocks (1 to 15, can't be 0)

%macro FINAL_BLOCKS 14
%define %%PLAIN_OUT             %1      ; [in] output buffer
%define %%CIPH_IN               %2      ; [in] input buffer
%define %%LAST_CIPH_BLK         %3      ; [in/out] ZMM with IV/last cipher blk (in idx 3)
%define %%num_final_blocks      %4      ; [in] numerical value (1 - 15)
%define %%CIPHER_PLAIN_0_3      %5      ; [out] ZMM next 0-3 cipher blocks
%define %%CIPHER_PLAIN_4_7      %6      ; [out] ZMM next 4-7 cipher blocks
%define %%CIPHER_PLAIN_8_11     %7      ; [out] ZMM next 8-11 cipher blocks
%define %%CIPHER_PLAIN_12_15    %8      ; [out] ZMM next 12-15 cipher blocks
%define %%ZT1                   %9      ; [clobbered] ZMM temporary
%define %%ZT2                   %10     ; [clobbered] ZMM temporary
%define %%ZT3                   %11     ; [clobbered] ZMM temporary
%define %%ZT4                   %12     ; [clobbered] ZMM temporary
%define %%IA0                   %13     ; [clobbered] GP temporary
%define %%NROUNDS               %14     ; [in] number of rounds; numerical value

        ;; load plain/cipher text
%ifdef CBCS
        ZMM_LOAD_BLOCKS_0_16_OFFSET %%num_final_blocks, %%CIPH_IN, \
                OFFSET, %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_4_7, \
                %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_12_15
%else
        ZMM_LOAD_BLOCKS_0_16 %%num_final_blocks, %%CIPH_IN, 0, \
                %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_4_7, \
                %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_12_15
%endif
        ;; Prepare final cipher text blocks to
        ;; be XOR'd later after AESDEC
        valignq         %%ZT1, %%CIPHER_PLAIN_0_3, %%LAST_CIPH_BLK, 6
%if %%num_final_blocks > 4
        valignq         %%ZT2, %%CIPHER_PLAIN_4_7, %%CIPHER_PLAIN_0_3, 6
%endif
%if %%num_final_blocks > 8
        valignq         %%ZT3, %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_4_7, 6
%endif
%if %%num_final_blocks > 12
        valignq         %%ZT4, %%CIPHER_PLAIN_12_15, %%CIPHER_PLAIN_8_11, 6
%endif

        ;; Update IV with last cipher block
        ;; to be used later in DECRYPT_16_PARALLEL
%if %%num_final_blocks == 1
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_0_3, 2
%elif %%num_final_blocks == 2
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_0_3, 4
%elif %%num_final_blocks == 3
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_0_3, 6
%elif %%num_final_blocks == 4
        vmovdqa64       %%LAST_CIPH_BLK, %%CIPHER_PLAIN_0_3
%elif %%num_final_blocks == 5
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_4_7, %%CIPHER_PLAIN_4_7, 2
%elif %%num_final_blocks == 6
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_4_7, %%CIPHER_PLAIN_4_7, 4
%elif %%num_final_blocks == 7
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_4_7, %%CIPHER_PLAIN_4_7, 6
%elif %%num_final_blocks == 8
        vmovdqa64       %%LAST_CIPH_BLK, %%CIPHER_PLAIN_4_7
%elif %%num_final_blocks == 9
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_8_11, 2
%elif %%num_final_blocks == 10
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_8_11, 4
%elif %%num_final_blocks == 11
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_8_11, 6
%elif %%num_final_blocks == 12
        vmovdqa64       %%LAST_CIPH_BLK, %%CIPHER_PLAIN_8_11
%elif %%num_final_blocks == 13
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_12_15, %%CIPHER_PLAIN_12_15, 2
%elif %%num_final_blocks == 14
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_12_15, %%CIPHER_PLAIN_12_15, 4
%elif %%num_final_blocks == 15
        valignq         %%LAST_CIPH_BLK, %%CIPHER_PLAIN_12_15, %%CIPHER_PLAIN_12_15, 6
%endif

        ;; AES rounds
%assign j 0
%rep (%%NROUNDS + 2)
     ZMM_AESDEC_ROUND_BLOCKS_0_16 %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_4_7, \
                        %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_12_15, \
                        ZKEY %+ j, j, no_data, no_data, no_data, no_data, \
                        %%num_final_blocks, %%NROUNDS
%assign j (j + 1)
%endrep

        ;; XOR with decrypted blocks to get plain text
        vpxorq          %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_0_3, %%ZT1
%if %%num_final_blocks > 4
        vpxorq          %%CIPHER_PLAIN_4_7, %%CIPHER_PLAIN_4_7, %%ZT2
%endif
%if %%num_final_blocks > 8
        vpxorq          %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_8_11, %%ZT3
%endif
%if %%num_final_blocks > 12
        vpxorq          %%CIPHER_PLAIN_12_15, %%CIPHER_PLAIN_12_15, %%ZT4
%endif

        ;; write plain text back to output
%ifdef CBCS
        ZMM_STORE_BLOCKS_0_16_OFFSET %%num_final_blocks, %%PLAIN_OUT, \
                OFFSET, %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_4_7, \
                %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_12_15
%else
        ZMM_STORE_BLOCKS_0_16 %%num_final_blocks, %%PLAIN_OUT, 0, \
                %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_4_7, \
                %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_12_15
%endif

%endmacro       ; FINAL_BLOCKS

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Main AES-CBC decrypt macro
;;; - operates on single stream
;;; - decrypts 16 blocks at a time
%macro DECRYPT_16_PARALLEL 14
%define %%PLAIN_OUT             %1      ; [in] output buffer
%define %%CIPH_IN               %2      ; [in] input buffer
%define %%LENGTH                %3      ; [in/out] number of bytes to process
%define %%LAST_CIPH_BLK         %4      ; [in/out] ZMM with IV (first block) or last cipher block (idx 3)
%define %%CIPHER_PLAIN_0_3      %5      ; [out] ZMM next 0-3 cipher blocks
%define %%CIPHER_PLAIN_4_7      %6      ; [out] ZMM next 4-7 cipher blocks
%define %%CIPHER_PLAIN_8_11     %7      ; [out] ZMM next 8-11 cipher blocks
%define %%CIPHER_PLAIN_12_15    %8      ; [out] ZMM next 12-15 cipher blocks
%define %%ZT1                   %9      ; [clobbered] ZMM temporary
%define %%ZT2                   %10     ; [clobbered] ZMM temporary
%define %%ZT3                   %11     ; [clobbered] ZMM temporary
%define %%ZT4                   %12     ; [clobbered] ZMM temporary
%define %%NROUNDS               %13     ; [in] number of rounds; numerical value
%define %%IA0                   %14     ; [clobbered] GP temporary

%ifdef CBCS
       ZMM_LOAD_BLOCKS_0_16_OFFSET 16, %%CIPH_IN, OFFSET, \
                %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_4_7, \
                %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_12_15
%else
        vmovdqu8        %%CIPHER_PLAIN_0_3, [%%CIPH_IN]
        vmovdqu8        %%CIPHER_PLAIN_4_7, [%%CIPH_IN + 64]
        vmovdqu8        %%CIPHER_PLAIN_8_11, [%%CIPH_IN + 128]
        vmovdqu8        %%CIPHER_PLAIN_12_15, [%%CIPH_IN + 192]
%endif
        ;; prepare first set of cipher blocks for later XOR'ing
        valignq         %%ZT1, %%CIPHER_PLAIN_0_3, %%LAST_CIPH_BLK, 6
        valignq         %%ZT2, %%CIPHER_PLAIN_4_7, %%CIPHER_PLAIN_0_3, 6
        valignq         %%ZT3, %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_4_7, 6
        valignq         %%ZT4, %%CIPHER_PLAIN_12_15, %%CIPHER_PLAIN_8_11, 6

        ;; store last cipher text block to be used for next 16 blocks
        vmovdqa64       %%LAST_CIPH_BLK, %%CIPHER_PLAIN_12_15

        ;; AES rounds
%assign j 0
%rep (%%NROUNDS + 2)
        ZMM_AESDEC_ROUND_BLOCKS_0_16 %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_4_7, \
                        %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_12_15, \
                        ZKEY %+ j, j, no_data, no_data, no_data, no_data, \
                        16, %%NROUNDS
%assign j (j + 1)
%endrep

        ;; XOR with decrypted blocks to get plain text
        vpxorq          %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_0_3, %%ZT1
        vpxorq          %%CIPHER_PLAIN_4_7, %%CIPHER_PLAIN_4_7, %%ZT2
        vpxorq          %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_8_11, %%ZT3
        vpxorq          %%CIPHER_PLAIN_12_15, %%CIPHER_PLAIN_12_15, %%ZT4

        ;; write plain text back to output
%ifdef CBCS
       ZMM_STORE_BLOCKS_0_16_OFFSET 16, %%PLAIN_OUT, OFFSET, \
                %%CIPHER_PLAIN_0_3, %%CIPHER_PLAIN_4_7, \
                %%CIPHER_PLAIN_8_11, %%CIPHER_PLAIN_12_15
%else
        vmovdqu8        [%%PLAIN_OUT], %%CIPHER_PLAIN_0_3
        vmovdqu8        [%%PLAIN_OUT + 64], %%CIPHER_PLAIN_4_7
        vmovdqu8        [%%PLAIN_OUT + 128], %%CIPHER_PLAIN_8_11
        vmovdqu8        [%%PLAIN_OUT + 192], %%CIPHER_PLAIN_12_15
%endif
        ;; adjust input pointer and length
        sub             %%LENGTH, (16 * 16)
        add             %%CIPH_IN, (16 * OFFSET)
        add             %%PLAIN_OUT, (16 * OFFSET)

%endmacro       ; DECRYPT_16_PARALLEL

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; AES_CBC_DEC macro decrypts given data.
;;; Flow:
;;; - Decrypt all blocks (multiple of 16) up to final 1-15 blocks
;;; - Decrypt final blocks (1-15 blocks)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro AES_CBC_DEC 7
%define %%CIPH_IN       %1 ;; [in] pointer to input buffer
%define %%PLAIN_OUT     %2 ;; [in] pointer to output buffer
%define %%KEYS          %3 ;; [in] pointer to expanded keys
%define %%IV            %4 ;; [in] pointer to IV
%define %%LENGTH        %5 ;; [in/out] GP register with length in bytes
%define %%NROUNDS       %6 ;; [in] Number of AES rounds; numerical value
%define %%TMP           %7 ;; [clobbered] GP register

        cmp     %%LENGTH, 0
        je      %%cbc_dec_done

        vinserti64x2 zIV, zIV, [%%IV], 3

        ;; preload keys
        LOAD_KEYS %%KEYS, %%NROUNDS

%%decrypt_16_parallel:
        cmp     %%LENGTH, 256
        jb      %%final_blocks

        DECRYPT_16_PARALLEL %%PLAIN_OUT, %%CIPH_IN, %%LENGTH, zIV, \
                            zBLK_0_3, zBLK_4_7, zBLK_8_11, zBLK_12_15, \
                            zTMP0, zTMP1, zTMP2, zTMP3, %%NROUNDS, %%TMP
        jmp     %%decrypt_16_parallel

%%final_blocks:
        ;; get num final blocks
        shr     %%LENGTH, 4
        and     %%LENGTH, 0xf
        je      %%cbc_dec_done

        cmp     %%LENGTH, 8
        je      %%final_num_blocks_is_8
        jl      %%final_blocks_is_1_7

        ; Final blocks 9-15
        cmp     %%LENGTH, 12
        je      %%final_num_blocks_is_12
        jl      %%final_blocks_is_9_11

        ; Final blocks 13-15
        cmp     %%LENGTH, 15
        je      %%final_num_blocks_is_15
        cmp     %%LENGTH, 14
        je      %%final_num_blocks_is_14
        cmp     %%LENGTH, 13
        je      %%final_num_blocks_is_13

%%final_blocks_is_9_11:
        cmp     %%LENGTH, 11
        je      %%final_num_blocks_is_11
        cmp     %%LENGTH, 10
        je      %%final_num_blocks_is_10
        cmp     %%LENGTH, 9
        je      %%final_num_blocks_is_9

%%final_blocks_is_1_7:
        cmp     %%LENGTH, 4
        je      %%final_num_blocks_is_4
        jl      %%final_blocks_is_1_3

        ; Final blocks 5-7
        cmp     %%LENGTH, 7
        je      %%final_num_blocks_is_7
        cmp     %%LENGTH, 6
        je      %%final_num_blocks_is_6
        cmp     %%LENGTH, 5
        je      %%final_num_blocks_is_5

%%final_blocks_is_1_3:
        cmp     %%LENGTH, 3
        je      %%final_num_blocks_is_3
        cmp     %%LENGTH, 2
        je      %%final_num_blocks_is_2
        jmp     %%final_num_blocks_is_1

%%final_num_blocks_is_15:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 15, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_14:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 14, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_13:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 13, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_12:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 12, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_11:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 11, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_10:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 10, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_9:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 9, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_8:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 8, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_7:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 7, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_6:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 6, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_5:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 5, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_4:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 4, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_3:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 3, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_2:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 2, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS
        jmp     %%cbc_dec_done

%%final_num_blocks_is_1:
        FINAL_BLOCKS %%PLAIN_OUT, %%CIPH_IN, zIV, 1, zBLK_0_3, zBLK_4_7, \
                     zBLK_8_11, zBLK_12_15, zTMP0, zTMP1, zTMP2, zTMP3, \
                     %%TMP, %%NROUNDS

%%cbc_dec_done:
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

mksection .text

%ifndef CBCS
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; aes_cbc_dec_128_vaes_avx512(void *in, void *IV, void *keys, void *out, UINT64 num_bytes)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(aes_cbc_dec_128_vaes_avx512,function,internal)
aes_cbc_dec_128_vaes_avx512:
        endbranch64
%ifndef LINUX
        mov     num_bytes, [rsp + 8*5]
%endif
        AES_CBC_DEC p_in, p_out, p_keys, p_IV, num_bytes, 9, tmp

%ifdef SAFE_DATA
	clear_all_zmms_asm
%endif ;; SAFE_DATA

        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; aes_cbc_dec_192_vaes_avx512(void *in, void *IV, void *keys, void *out, UINT64 num_bytes)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(aes_cbc_dec_192_vaes_avx512,function,internal)
aes_cbc_dec_192_vaes_avx512:
        endbranch64
%ifndef LINUX
        mov     num_bytes, [rsp + 8*5]
%endif
        AES_CBC_DEC p_in, p_out, p_keys, p_IV, num_bytes, 11, tmp

%ifdef SAFE_DATA
	clear_all_zmms_asm
%endif ;; SAFE_DATA

        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; aes_cbc_dec_256_vaes_avx512(void *in, void *IV, void *keys, void *out, UINT64 num_bytes)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(aes_cbc_dec_256_vaes_avx512,function,internal)
aes_cbc_dec_256_vaes_avx512:
        endbranch64
%ifndef LINUX
        mov     num_bytes, [rsp + 8*5]
%endif
        AES_CBC_DEC p_in, p_out, p_keys, p_IV, num_bytes, 13, tmp

%ifdef SAFE_DATA
	clear_all_zmms_asm
%endif ;; SAFE_DATA

        ret

mksection stack-noexec

%endif ;; CBCS

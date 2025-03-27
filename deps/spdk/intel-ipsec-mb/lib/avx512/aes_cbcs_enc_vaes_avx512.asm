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

;;; routines to do 128 bit AES in CBCS mode encryption

%include "include/os.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/reg_sizes.asm"
%include "include/clear_regs.asm"
%include "include/cet.inc"
struc STACK
_gpr_save:      resq    1
endstruc

%define GPR_SAVE_AREA   rsp + _gpr_save

%ifdef LINUX
%define arg1            rdi
%define arg2            rsi
%define arg3            rcx
%define arg4            rdx
%else
%define arg1            rcx
%define arg2            rdx
%define arg3            rdi
%define arg4            rsi
%endif

%define ARG             arg1
%define LEN             arg2

%define IA0             rax
%define IA1             rbx
%define IA2             arg3
%define IN              arg4
%define OUT             rbp
%define IN_L0           r8
%define IN_L1           r9
%define IN_L2           r10
%define IN_L3           r11
%define IN_L8           r12
%define IN_L9           r13
%define IN_L10          r14
%define IN_L11          r15

%define ZIV00_03        zmm8
%define ZIV04_07        zmm9
%define ZIV08_11        zmm10
%define UNUSED_0        zmm11

%define ZT0             zmm16
%define ZT1             zmm17
%define ZT2             zmm18
%define ZT3             zmm19
%define ZT4             zmm20
%define ZT5             zmm21
%define ZT6             zmm22

%define ZT7             zmm23
%define ZT8             zmm24
%define ZT9             zmm25
%define ZT10            zmm26
%define ZT11            zmm27
%define ZT12            zmm28
%define ZT13            zmm29
%define ZT14            zmm30
%define ZT15            zmm31
%define ZT19            zmm15

%define ZT16            zmm12
%define ZT17            zmm13
%define ZT18            zmm14

%define R0_K0_3         zmm0
%define R0_K4_7         zmm1
%define R0_K8_11        zmm2
%define R2_K0_3         zmm3
%define R2_K4_7         zmm4
%define R2_K8_11        zmm5

;; Save registers states
%macro FUNC_SAVE 0
        sub             rsp, STACK_size
        mov             [GPR_SAVE_AREA + 8*0], rbp
%endmacro

;; Restore register states
%macro FUNC_RESTORE 0
        ;; XMMs are saved at a higher level
        mov             rbp, [GPR_SAVE_AREA + 8*0]
        add             rsp, STACK_size
        vzeroupper
%endmacro

%macro LOAD_STORE_4x1 10
%define %%LANE_A        %1  ; [in] lane index to load/store (numerical)
%define %%LANE_B        %2  ; [in] lane index to load/store (numerical)
%define %%LANE_C        %3  ; [in] lane index to load/store (numerical)
%define %%LANE_D        %4  ; [in] lane index to load/store (numerical)
%define %%DATA_PTR      %5  ; [in] GP reg with ptr to lane input table
%define %%OFFSET        %6  ; [in] GP reg input/output buffer offset
%define %%ZDATA         %7  ; [in/out] ZMM reg to load/store data
%define %%GP0           %8  ; [clobbered] tmp GP reg
%define %%GP1           %9  ; [clobbered] tmp GP reg
%define %%LOAD_STORE    %10 ; [in] string value to select LOAD or STORE

        mov             %%GP0, [%%DATA_PTR + 8*(%%LANE_A)]
        mov             %%GP1, [%%DATA_PTR + 8*(%%LANE_B)]

%ifidn %%LOAD_STORE, LOAD
        vmovdqu64       XWORD(%%ZDATA), [%%GP0 + %%OFFSET]
        vinserti64x2    %%ZDATA, [%%GP1 + %%OFFSET], 1

        mov             %%GP0, [%%DATA_PTR + 8*(%%LANE_C)]
        mov             %%GP1, [%%DATA_PTR + 8*(%%LANE_D)]

        vinserti64x2    %%ZDATA, [%%GP0 + %%OFFSET], 2
        vinserti64x2    %%ZDATA, [%%GP1 + %%OFFSET], 3
%else   ; STORE
        vmovdqu64       [%%GP0 + %%OFFSET], XWORD(%%ZDATA)
        vextracti64x2   [%%GP1 + %%OFFSET], %%ZDATA, 1

        mov             %%GP0, [%%DATA_PTR + 8*(%%LANE_C)]
        mov             %%GP1, [%%DATA_PTR + 8*(%%LANE_D)]

        vextracti64x2   [%%GP0 + %%OFFSET], %%ZDATA, 2
        vextracti64x2   [%%GP1 + %%OFFSET], %%ZDATA, 3
%endif ; LOAD/STORE
%endmacro

%macro LOAD_STORE_4x1_PRELOAD 7
%define %%PTR_A         %1  ; [in] GP reg with pointer to lane a data to load/store
%define %%PTR_B         %2  ; [in] GP reg with pointer to lane b data to load/store
%define %%PTR_C         %3  ; [in] GP reg with pointer to lane c data to load/store
%define %%PTR_D         %4  ; [in] GP reg with pointer to lane d data to load/store
%define %%OFFSET        %5  ; [in] GP reg input/output buffer offset
%define %%ZDATA         %6  ; [in/out] ZMM reg to load/store data
%define %%LOAD_STORE    %7  ; [in] string value to select LOAD or STORE

%ifidn %%LOAD_STORE, LOAD
        vmovdqu64       XWORD(%%ZDATA), [%%PTR_A + %%OFFSET]
        vinserti64x2    %%ZDATA, [%%PTR_B + %%OFFSET], 1
        vinserti64x2    %%ZDATA, [%%PTR_C + %%OFFSET], 2
        vinserti64x2    %%ZDATA, [%%PTR_D + %%OFFSET], 3
%else   ; STORE
        vmovdqu64       [%%PTR_A + %%OFFSET], XWORD(%%ZDATA)
        vextracti64x2   [%%PTR_B + %%OFFSET], %%ZDATA, 1
        vextracti64x2   [%%PTR_C + %%OFFSET], %%ZDATA, 2
        vextracti64x2   [%%PTR_D + %%OFFSET], %%ZDATA, 3
%endif ; LOAD/STORE
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; AESENC_ROUNDS_x12 macro
; - 12 lanes, 1 block per lane
; - performs AES encrypt rounds 1-NROUNDS
; - optional selection of final destination register
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro AESENC_ROUNDS_x12 4-7
%define %%L00_03     %1 ; [in/out] ZMM with lane 0-3 blocks
%define %%L04_07     %2 ; [in/out] ZMM with lane 4-7 blocks
%define %%L08_11     %3 ; [in/out] ZMM with lane 8-11 blocks
%define %%NROUNDS    %4 ; [in] number of aes rounds
%define %%OUT_00_03  %5 ; [out] ZMM with lane 0-3 blocks (optional)
%define %%OUT_04_07  %6 ; [out] ZMM with lane 4-7 blocks (optional)
%define %%OUT_08_11  %7 ; [out] ZMM with lane 8-11 blocks (optional)
%define %%NUM_ARGS   %0

%define %%KP            ARG + _aesarg_key_tab
%define %%K00_03_OFFSET 0
%define %%K04_07_OFFSET 64
%define %%K08_11_OFFSET 128

%assign ROUND 1
%rep (%%NROUNDS + 1)

%if ROUND <= %%NROUNDS

%if ROUND == 2 ;; round 2 keys preloaded for some lanes
        vaesenc         %%L00_03, %%L00_03, R2_K0_3
        vaesenc         %%L04_07, %%L04_07, R2_K4_7
        vaesenc         %%L08_11, %%L08_11, R2_K8_11
%else
        ;; rounds 1 to 9/11/13
        vaesenc         %%L00_03, %%L00_03, [%%KP + %%K00_03_OFFSET + ROUND * (16*16)]
        vaesenc         %%L04_07, %%L04_07, [%%KP + %%K04_07_OFFSET + ROUND * (16*16)]
        vaesenc         %%L08_11, %%L08_11, [%%KP + %%K08_11_OFFSET + ROUND * (16*16)]
%endif
%else
        ;; the last round
%if %%NUM_ARGS == 7
        ;; store result in different output registers
        vaesenclast     %%OUT_00_03, %%L00_03, [%%KP + %%K00_03_OFFSET + ROUND * (16*16)]
        vaesenclast     %%OUT_04_07, %%L04_07, [%%KP + %%K04_07_OFFSET + ROUND * (16*16)]
        vaesenclast     %%OUT_08_11, %%L08_11, [%%KP + %%K08_11_OFFSET + ROUND * (16*16)]
%else
        vaesenclast     %%L00_03, %%L00_03, [%%KP + %%K00_03_OFFSET + ROUND * (16*16)]
        vaesenclast     %%L04_07, %%L04_07, [%%KP + %%K04_07_OFFSET + ROUND * (16*16)]
        vaesenclast     %%L08_11, %%L08_11, [%%KP + %%K08_11_OFFSET + ROUND * (16*16)]
%endif
%endif

%assign ROUND (ROUND + 1)
%endrep

%endmacro                       ; AESENC_ROUNDS_x12

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; ENCRYPT_12_PARALLEL - Encode all blocks up to multiple of 2
; - Operation
;   - loop encrypting %%LENGTH bytes of input data
;   - each loop encrypts 2 blocks across 12 lanes
;   - stop when %%LENGTH is less than 32 bytes (2 blocks)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro ENCRYPT_12_PARALLEL 15
%define %%ZIV00_03      %1  ;; [in] lane 0-3 IVs
%define %%ZIV04_07      %2  ;; [in] lane 4-7 IVs
%define %%ZIV08_11      %3  ;; [in] lane 8-11 IVs
%define %%LENGTH        %4  ;; [in/out] GP register with length in bytes
%define %%NROUNDS       %5  ;; [in] Number of AES rounds; numerical value
%define %%IDX           %6  ;; [clobbered] GP reg to maintain idx
%define %%L00_03        %7  ;; [clobbered] tmp ZMM register
%define %%L04_07        %8  ;; [clobbered] tmp ZMM register
%define %%L08_11        %9 ;; [clobbered] tmp ZMM register
%define %%ZTMP0         %10 ;; [clobbered] tmp ZMM register
%define %%ZTMP1         %11 ;; [clobbered] tmp ZMM register
%define %%ZTMP2         %12 ;; [clobbered] tmp ZMM register
%define %%TMP0          %13 ;; [clobbered] tmp GP register
%define %%TMP1          %14 ;; [clobbered] tmp GP register
%define %%OFFSET        %15 ;; offset between blocks (numerical value)

%define %%KP            ARG + _aesarg_key_tab
%define %%K00_03_OFFSET 0
%define %%K04_07_OFFSET 64
%define %%K08_11_OFFSET 128
%define %%NEXT_L00_03   %%ZIV00_03
%define %%NEXT_L04_07   %%ZIV04_07
%define %%NEXT_L08_11   %%ZIV08_11

        xor             %%IDX, %%IDX

        ;; check for at least 2 blocks
        cmp             %%LENGTH, 32
        jb              %%encrypt_12_end

        ;; skip length check on first loop
        jmp             %%encrypt_12_first

%%encrypt_12_start:
        cmp             %%LENGTH, 32
        jb              %%encrypt_12_end

%%encrypt_12_first:

        ;; load and XOR first block lanes with IV / ciphertexxt and round 0 key
        LOAD_STORE_4x1_PRELOAD IN_L0, IN_L1, IN_L2, IN_L3, %%IDX, %%L00_03, LOAD
        vpternlogq      %%L00_03, %%ZIV00_03, [%%KP + %%K00_03_OFFSET], 0x96

        LOAD_STORE_4x1 4, 5, 6, 7, IN, %%IDX, %%L04_07, %%TMP0, %%TMP1, LOAD
        vpternlogq      %%L04_07, %%ZIV04_07, [%%KP + %%K04_07_OFFSET], 0x96

        LOAD_STORE_4x1_PRELOAD IN_L8, IN_L9, IN_L10, IN_L11, %%IDX, %%L08_11, LOAD
        vpternlogq      %%L08_11, %%ZIV08_11, [%%KP + %%K08_11_OFFSET], 0x96

        ;; encrypt first block lanes
        AESENC_ROUNDS_x12 %%L00_03, %%L04_07, %%L08_11, %%NROUNDS

        ;; store ciphertext
        LOAD_STORE_4x1 0, 1, 2, 3, OUT, %%IDX, %%L00_03, %%TMP0, %%TMP1, STORE
        LOAD_STORE_4x1 4, 5, 6, 7, OUT, %%IDX, %%L04_07, %%TMP0, %%TMP1, STORE
        LOAD_STORE_4x1 8, 9, 10, 11, OUT, %%IDX, %%L08_11, %%TMP0, %%TMP1, STORE

        ;; load and XOR next block lanes with previous block and round 0 key
        add     %%IDX, %%OFFSET

        LOAD_STORE_4x1_PRELOAD IN_L0, IN_L1, IN_L2, IN_L3, %%IDX, %%NEXT_L00_03, LOAD
        vpternlogq      %%NEXT_L00_03, %%L00_03, [%%KP + %%K00_03_OFFSET], 0x96

        LOAD_STORE_4x1 4, 5, 6, 7, IN, %%IDX, %%NEXT_L04_07, %%TMP0, %%TMP1, LOAD
        vpternlogq      %%NEXT_L04_07, %%L04_07, [%%KP + %%K04_07_OFFSET], 0x96

        LOAD_STORE_4x1_PRELOAD IN_L8, IN_L9, IN_L10, IN_L11, %%IDX, %%NEXT_L08_11, LOAD
        vpternlogq      %%NEXT_L08_11, %%L08_11, [%%KP + %%K08_11_OFFSET], 0x96

        ;; encrypt block next lanes
        AESENC_ROUNDS_x12 %%NEXT_L00_03, %%NEXT_L04_07, %%NEXT_L08_11, %%NROUNDS

        ;; store ciphertext
        LOAD_STORE_4x1 0, 1, 2, 3, OUT, %%IDX, %%NEXT_L00_03, %%TMP0, %%TMP1, STORE
        LOAD_STORE_4x1 4, 5, 6, 7, OUT, %%IDX, %%NEXT_L04_07, %%TMP0, %%TMP1, STORE
        LOAD_STORE_4x1 8, 9, 10, 11, OUT, %%IDX, %%NEXT_L08_11, %%TMP0, %%TMP1, STORE

        sub             %%LENGTH, 32
        add             %%IDX, %%OFFSET
        jmp             %%encrypt_12_start

%%encrypt_12_end:

        shr             %%LENGTH, 4
        and             %%LENGTH, 3

        ;; check for final block
        je              %%encrypt_12_done

        ;; process final block
        ;; load and XOR first / final block lanes with IV and round 0 key
        LOAD_STORE_4x1_PRELOAD IN_L0, IN_L1, IN_L2, IN_L3, %%IDX, %%L00_03, LOAD
        vpternlogq      %%L00_03, %%ZIV00_03, [%%KP + %%K00_03_OFFSET], 0x96

        LOAD_STORE_4x1 4, 5, 6, 7, IN, %%IDX, %%L04_07, %%TMP0, %%TMP1, LOAD
        vpternlogq      %%L04_07, %%ZIV04_07, [%%KP + %%K04_07_OFFSET], 0x96

        LOAD_STORE_4x1_PRELOAD IN_L8, IN_L9, IN_L10, IN_L11, %%IDX, %%L08_11, LOAD
        vpternlogq      %%L08_11, %%ZIV08_11, [%%KP + %%K08_11_OFFSET], 0x96

        ;; encrypt first / final block lanes and store result in IV regs
        AESENC_ROUNDS_x12 %%L00_03, %%L04_07, %%L08_11, %%NROUNDS, \
                          %%ZIV00_03, %%ZIV04_07, %%ZIV08_11

        ;; store ciphertext
        LOAD_STORE_4x1 0, 1, 2, 3, OUT, %%IDX, %%ZIV00_03, %%TMP0, %%TMP1, STORE
        LOAD_STORE_4x1 4, 5, 6, 7, OUT, %%IDX, %%ZIV04_07, %%TMP0, %%TMP1, STORE
        LOAD_STORE_4x1 8, 9, 10, 11, OUT, %%IDX, %%ZIV08_11, %%TMP0, %%TMP1, STORE

%%encrypt_12_done:
        ;; update in/out pointers
        vpbroadcastq    %%ZTMP2, %%IDX
        vpaddq          %%ZTMP0, %%ZTMP2, [IN]
        vpaddq          %%ZTMP1, %%ZTMP2, [IN + 64]
        vmovdqa64       [IN], %%ZTMP0
        vmovdqa64       [IN + 64], %%ZTMP1
        vpaddq          %%ZTMP0, %%ZTMP2, [OUT]
        vpaddq          %%ZTMP1, %%ZTMP2, [OUT + 64]
        vmovdqa64       [OUT], %%ZTMP0
        vmovdqa64       [OUT + 64], %%ZTMP1

%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; CBCS_ENC Encodes given data.
; Requires the input data be at least 1 block (16 bytes) long
; Input:  Number of AES rounds
;         Offset between blocks to be encrypted
;
; First encrypts block up to multiple of 2
; Then encrypts final block
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro CBCS_ENC 2
%define %%ROUNDS        %1
%define %%OFFSET        %2

%define %%K00_03_OFFSET 0
%define %%K04_07_OFFSET 64
%define %%K08_11_OFFSET 128

%define %%KP    ARG + _aesarg_key_tab
%define %%IV    ARG + _aesarg_IV
%define %%IN    ARG + _aesarg_in
%define %%OUT   ARG + _aesarg_out

        ;; convert CBCS length to standard number of CBC blocks
        ;; ((num_bytes + 9 blocks) / 160) = num blocks to decrypt
%ifdef LINUX
        ;; save rdx, only on Linux, as for Windows, rdx = len, which is updated here
        mov     IA1, rdx
%endif
        mov     rax, LEN        ;; mov len to rax for div
        xor     rdx, rdx        ;; zero rdx for div
        add     rax, (%%OFFSET-16) ;; add 9 blocks
        mov     IA2, 160
        div     IA2             ;; divide rax by 160
        shl     rax, 4          ;; multiply by 16 to get num bytes
        mov     LEN, rax        ;; set LEN
%ifdef LINUX
        mov     rdx, IA1        ;; restore rdx
%endif

        ;; load IV's per lane
        vmovdqa64       ZIV00_03, [%%IV + 16*0]
        vmovdqa64       ZIV04_07, [%%IV + 16*4]
        vmovdqa64       ZIV08_11, [%%IV + 16*8]

	;; preload some input pointers
        mov     IN_L0, [%%IN + 8*0]
        mov     IN_L1, [%%IN + 8*1]
        mov     IN_L2, [%%IN + 8*2]
        mov     IN_L3, [%%IN + 8*3]
        mov     IN_L8, [%%IN + 8*8]
        mov     IN_L9, [%%IN + 8*9]
        mov     IN_L10, [%%IN + 8*10]
        mov     IN_L11, [%%IN + 8*11]

        lea     IN, [%%IN]
        lea     OUT, [%%OUT]

        ;; preload some round keys
        vmovdqu64 R0_K0_3, [%%KP + %%K00_03_OFFSET]
        vmovdqu64 R0_K4_7, [%%KP + %%K04_07_OFFSET]
        vmovdqu64 R0_K8_11,[%%KP + %%K08_11_OFFSET]
        vmovdqu64 R2_K0_3, [%%KP + %%K00_03_OFFSET + 2 * (16*16)]
        vmovdqu64 R2_K4_7, [%%KP + %%K04_07_OFFSET + 2 * (16*16)]
        vmovdqu64 R2_K8_11,[%%KP + %%K08_11_OFFSET + 2 * (16*16)]

        ENCRYPT_12_PARALLEL ZIV00_03, ZIV04_07, ZIV08_11, \
                            LEN, %%ROUNDS, IA0, ZT0, ZT1, ZT2, ZT4, ZT5, \
                            ZT6, IA1, IA2, %%OFFSET

        ;; store IV's per lane
        vmovdqa64       [%%IV + 16*0],  ZIV00_03
        vmovdqa64       [%%IV + 16*4],  ZIV04_07
        vmovdqa64       [%%IV + 16*8],  ZIV08_11
%endmacro

mksection .text

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;  void aes_cbcs_1_9_enc_128_vaes_avx512(AES_ARGS *args, uint64_t len_in_bytes);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(aes_cbcs_1_9_enc_128_vaes_avx512,function,internal)
aes_cbcs_1_9_enc_128_vaes_avx512:
        endbranch64
        FUNC_SAVE
        CBCS_ENC 9, 160
        FUNC_RESTORE

%ifdef SAFE_DATA
	clear_all_zmms_asm
%endif ;; SAFE_DATA

        ret

mksection stack-noexec

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

extern idx_rows_avx512
extern all_7fs
extern all_80s

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Parallel lookup 64x8-bits with table to be loaded from memory (AVX-512 only)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro LOOKUP8_64_AVX512 13
%define %%INDICES       %1  ;; [in] ZMM register with 64x8-bit index
%define %%RET_VALUES    %2  ;; [out] ZMM register with 64x8-bit values
%define %%TABLE         %3  ;; [in] Table pointer
%define %%LOW_NIBBLE    %4  ;; [clobbered] Temporary ZMM register
%define %%HIGH_NIBBLE   %5  ;; [clobbered] Temporary ZMM register
%define %%ZTMP1         %6  ;; [clobbered] Temporary ZMM register
%define %%ZTMP2         %7  ;; [clobbered] Temporary ZMM register
%define %%ZTMP3         %8  ;; [clobbered] Temporary ZMM register
%define %%ZTMP4         %9  ;; [clobbered] Temporary ZMM register
%define %%KR1           %10 ;; [clobbered] Temporary k-register
%define %%KR2           %11 ;; [clobbered] Temporary k-register
%define %%KR3           %12 ;; [clobbered] Temporary k-register
%define %%KR4           %13 ;; [clobbered] Temporary k-register

        vmovdqa64       %%ZTMP1, [rel idx_rows_avx512 + (15 * 64)]
        vpsrlq          %%ZTMP2, %%ZTMP1, 4

        vpandq          %%HIGH_NIBBLE, %%ZTMP1, %%INDICES ;; top nibble part of the index
        vpandq          %%LOW_NIBBLE, %%ZTMP2, %%INDICES  ;; low nibble part of the index

        vpcmpb          %%KR1,  %%HIGH_NIBBLE, [rel idx_rows_avx512 + (0 * 64)], 0
        vbroadcastf64x2 %%ZTMP1, [%%TABLE + (0 * 16)]
        vpcmpb          %%KR2, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (1 * 64)], 0
        vbroadcastf64x2 %%ZTMP2, [%%TABLE + (1 * 16)]
        vpcmpb          %%KR3, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (2 * 64)], 0
        vbroadcastf64x2 %%ZTMP3, [%%TABLE + (2 * 16)]
        vpcmpb          %%KR4, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (3 * 64)], 0
        vbroadcastf64x2 %%ZTMP4, [%%TABLE + (3 * 16)]

        vpshufb         %%RET_VALUES{%%KR1}{z}, %%ZTMP1, %%LOW_NIBBLE
        vpshufb         %%ZTMP2{%%KR2}{z}, %%ZTMP2, %%LOW_NIBBLE
        vpshufb         %%ZTMP3{%%KR3}{z}, %%ZTMP3, %%LOW_NIBBLE
        vpshufb         %%ZTMP4{%%KR4}{z}, %%ZTMP4, %%LOW_NIBBLE

        vpternlogq      %%RET_VALUES, %%ZTMP2, %%ZTMP3, 0xFE
        vporq           %%RET_VALUES, %%ZTMP4

        vpcmpb          %%KR1,  %%HIGH_NIBBLE, [rel idx_rows_avx512 + (4 * 64)], 0
        vbroadcastf64x2 %%ZTMP1, [%%TABLE + (4 * 16)]
        vpcmpb          %%KR2, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (5 * 64)], 0
        vbroadcastf64x2 %%ZTMP2, [%%TABLE + (5 * 16)]
        vpcmpb          %%KR3, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (6 * 64)], 0
        vbroadcastf64x2 %%ZTMP3, [%%TABLE + (6 * 16)]
        vpcmpb          %%KR4, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (7 * 64)], 0
        vbroadcastf64x2 %%ZTMP4, [%%TABLE + (7 * 16)]

        vpshufb         %%ZTMP1{%%KR1}{z}, %%ZTMP1, %%LOW_NIBBLE
        vpshufb         %%ZTMP2{%%KR2}{z}, %%ZTMP2, %%LOW_NIBBLE
        vpshufb         %%ZTMP3{%%KR3}{z}, %%ZTMP3, %%LOW_NIBBLE
        vpshufb         %%ZTMP4{%%KR4}{z}, %%ZTMP4, %%LOW_NIBBLE

        vpternlogq      %%RET_VALUES, %%ZTMP1, %%ZTMP2, 0xFE
        vpternlogq      %%RET_VALUES, %%ZTMP3, %%ZTMP4, 0xFE

        vpcmpb          %%KR1,  %%HIGH_NIBBLE, [rel idx_rows_avx512 + (8 * 64)], 0
        vbroadcastf64x2 %%ZTMP1, [%%TABLE + (8 * 16)]
        vpcmpb          %%KR2, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (9 * 64)], 0
        vbroadcastf64x2 %%ZTMP2, [%%TABLE + (9 * 16)]
        vpcmpb          %%KR3, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (10 * 64)], 0
        vbroadcastf64x2 %%ZTMP3, [%%TABLE + (10 * 16)]
        vpcmpb          %%KR4, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (11 * 64)], 0
        vbroadcastf64x2 %%ZTMP4, [%%TABLE + (11 * 16)]

        vpshufb         %%ZTMP1{%%KR1}{z}, %%ZTMP1, %%LOW_NIBBLE
        vpshufb         %%ZTMP2{%%KR2}{z}, %%ZTMP2, %%LOW_NIBBLE
        vpshufb         %%ZTMP3{%%KR3}{z}, %%ZTMP3, %%LOW_NIBBLE
        vpshufb         %%ZTMP4{%%KR4}{z}, %%ZTMP4, %%LOW_NIBBLE

        vpternlogq      %%RET_VALUES, %%ZTMP1, %%ZTMP2, 0xFE
        vpternlogq      %%RET_VALUES, %%ZTMP3, %%ZTMP4, 0xFE

        vpcmpb          %%KR1,  %%HIGH_NIBBLE, [rel idx_rows_avx512 + (12 * 64)], 0
        vbroadcastf64x2 %%ZTMP1, [%%TABLE + (12 * 16)]
        vpcmpb          %%KR2, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (13 * 64)], 0
        vbroadcastf64x2 %%ZTMP2, [%%TABLE + (13 * 16)]
        vpcmpb          %%KR3, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (14 * 64)], 0
        vbroadcastf64x2 %%ZTMP3, [%%TABLE + (14 * 16)]
        vpcmpb          %%KR4, %%HIGH_NIBBLE, [rel idx_rows_avx512 + (15 * 64)], 0
        vbroadcastf64x2 %%ZTMP4, [%%TABLE + (15 * 16)]

        vpshufb         %%ZTMP1{%%KR1}{z}, %%ZTMP1, %%LOW_NIBBLE
        vpshufb         %%ZTMP2{%%KR2}{z}, %%ZTMP2, %%LOW_NIBBLE
        vpshufb         %%ZTMP3{%%KR3}{z}, %%ZTMP3, %%LOW_NIBBLE
        vpshufb         %%ZTMP4{%%KR4}{z}, %%ZTMP4, %%LOW_NIBBLE

        vpternlogq      %%RET_VALUES, %%ZTMP1, %%ZTMP2, 0xFE
        vpternlogq      %%RET_VALUES, %%ZTMP3, %%ZTMP4, 0xFE

%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Parallel lookup 64x8-bits with table to be loaded from memory (AVX512-VBMI)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro LOOKUP8_64_AVX512_VBMI 11
%define %%INDICES       %1      ;; [in] zmm register with 64x8-bit index
%define %%RET_VALUES    %2      ;; [out] zmm register with 64x8-bit values
%define %%TABLE         %3      ;; [in] table pointer
%define %%HIGH_BIT      %4	;; [clobbered] temporary zmm register
%define %%ZTMP1         %5	;; [clobbered] temporary zmm register
%define %%ZTMP2         %6	;; [clobbered] temporary zmm register
%define %%MAP_TAB_0     %7      ;; [clobbered] lookup values for bytes eq 0-3f
%define %%MAP_TAB_1     %8      ;; [clobbered] lookup values for bytes eq 40-7f
%define %%MAP_TAB_2     %9      ;; [clobbered] lookup values for bytes eq 80-bf
%define %%MAP_TAB_3     %10     ;; [clobbered] lookup values for bytes eq c0-ff
%define %%KR1           %11     ;; [clobbered] temporary k-register

        vmovdqu64       %%MAP_TAB_0, [%%TABLE]
        vmovdqu64       %%MAP_TAB_1, [%%TABLE + 64]
        vmovdqu64       %%MAP_TAB_2, [%%TABLE + 64*2]
        vmovdqu64       %%MAP_TAB_3, [%%TABLE + 64*3]

        vpandq          %%ZTMP1, %%INDICES, [rel all_7fs] ; 7 LSB on each byte
        vpandq          %%HIGH_BIT, %%INDICES, [rel all_80s] ; 1 MSB on each byte
        vmovdqa64       %%ZTMP2, %%ZTMP1

        vpxorq          %%RET_VALUES, %%RET_VALUES
        vpcmpb          %%KR1, %%HIGH_BIT, %%RET_VALUES, 4
        ; Permutes the bytes of tables based on bits 0-5 of indices (ZTMP1/2),
        ; and chooses between table 1 or 2 based on bit 6
        vpermi2b        %%ZTMP1, %%MAP_TAB_0, %%MAP_TAB_1
        vpermi2b        %%ZTMP2, %%MAP_TAB_2, %%MAP_TAB_3
        ; Blends results based on MSB of indices
        vpblendmb       %%RET_VALUES{%%KR1}, %%ZTMP1, %%ZTMP2

%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Parallel lookup 64x8-bits with table already loaded into registers (AVX512-VBMI)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro LOOKUP8_64_AVX512_VBMI_4_MAP_TABLES 10
%define %%INDICES           %1  ;; [in] zmm register with values for lookup
%define %%RET_VALUES        %2  ;; [out] zmm register filled with looked up values
%define %%HIGH_BIT          %3  ;; [clobbered] temporary zmm register
%define %%ZTMP1             %4  ;; [clobbered] temporary zmm register
%define %%ZTMP2             %5  ;; [clobbered] temporary zmm register
%define %%MAP_TAB_0         %6  ;; [in] lookup values for bytes eq 0-3f
%define %%MAP_TAB_1         %7  ;; [in] lookup values for bytes eq 40-7f
%define %%MAP_TAB_2         %8  ;; [in] lookup values for bytes eq 80-bf
%define %%MAP_TAB_3         %9  ;; [in] lookup values for bytes eq c0-ff
%define %%KR1               %10 ;; [clobbered] temporary k-register

        vpandq          %%ZTMP1, %%INDICES, [rel all_7fs] ; 7 LSB on each byte
        vpandq          %%HIGH_BIT, %%INDICES, [rel all_80s] ; 1 MSB on each byte
        vmovdqa64       %%ZTMP2, %%ZTMP1

        vpxorq          %%RET_VALUES, %%RET_VALUES
        vpcmpb          %%KR1, %%HIGH_BIT, %%RET_VALUES, 4
        ; Permutes the bytes of tables based on bits 0-5 of indices (ZTMP1/2),
        ; and chooses between table 1 or 2 based on bit 6
        vpermi2b        %%ZTMP1, %%MAP_TAB_0, %%MAP_TAB_1
        vpermi2b        %%ZTMP2, %%MAP_TAB_2, %%MAP_TAB_3
        ; Blends results based on MSB of indices
        vpblendmb       %%RET_VALUES{%%KR1}, %%ZTMP1, %%ZTMP2

%endmacro

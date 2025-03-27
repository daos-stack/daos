;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2018-2021, Intel Corporation All rights reserved.
;
;  Redistribution and use in source and binary forms, with or without
;  modification, are permitted provided that the following conditions
;  are met:
;    * Redistributions of source code must retain the above copyright
;      notice, this list of conditions and the following disclaimer.
;    * Redistributions in binary form must reproduce the above copyright
;      notice, this list of conditions and the following disclaimer in
;      the documentation and/or other materials provided with the
;      distribution.
;    * Neither the name of Intel Corporation nor the names of its
;      contributors may be used to endorse or promote products derived
;      from this software without specific prior written permission.
;
;  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
;  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
;  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
;  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
;  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;
; Authors:
;       Erdinc Ozturk
;       Vinodh Gopal
;       James Guilford
;       Tomasz Kantecki
;
;
; References:
;       This code was derived and highly optimized from the code described in paper:
;               Vinodh Gopal et. al. Optimized Galois-Counter-Mode Implementation on Intel Architecture Processors. August, 2010
;       The details of the implementation is explained in:
;               Erdinc Ozturk et. al. Enabling High-Performance Galois-Counter-Mode on Intel Architecture Processors. October, 2012.
;
;
;
;
; Assumptions:
;
;
;
; iv:
;       0                   1                   2                   3
;       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                             Salt  (From the SA)               |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                     Initialization Vector                     |
;       |         (This is the sequence number from IPSec header)       |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                              0x1                              |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;
;
;
; AAD:
;       AAD will be padded with 0 to the next 16byte multiple
;       for example, assume AAD is a u32 vector
;
;       if AAD is 8 bytes:
;       AAD[3] = {A0, A1};
;       padded AAD in xmm register = {A1 A0 0 0}
;
;       0                   1                   2                   3
;       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                               SPI (A1)                        |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                     32-bit Sequence Number (A0)               |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                              0x0                              |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;
;                                       AAD Format with 32-bit Sequence Number
;
;       if AAD is 12 bytes:
;       AAD[3] = {A0, A1, A2};
;       padded AAD in xmm register = {A2 A1 A0 0}
;
;       0                   1                   2                   3
;       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                               SPI (A2)                        |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                 64-bit Extended Sequence Number {A1,A0}       |
;       |                                                               |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                              0x0                              |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;
;        AAD Format with 64-bit Extended Sequence Number
;
;
; aadLen:
;       Must be a multiple of 4 bytes and from the definition of the spec.
;       The code additionally supports any aadLen length.
;
; TLen:
;       from the definition of the spec, TLen can only be 8, 12 or 16 bytes.
;
; poly = x^128 + x^127 + x^126 + x^121 + 1
; throughout the code, one tab and two tab indentations are used. one tab is for GHASH part, two tabs is for AES part.
;

%ifndef GCM_VAES_AVX512_INC
%define GCM_VAES_AVX512_INC

%include "include/os.asm"
%include "include/reg_sizes.asm"
%include "include/clear_regs.asm"
%include "include/gcm_defines.asm"
%include "include/gcm_keys_vaes_avx512.asm"
%include "include/memcpy.asm"
%include "include/aes_common.asm"
%include "include/cet.inc"
%ifndef GCM128_MODE
%ifndef GCM192_MODE
%ifndef GCM256_MODE
%error "No GCM mode selected for gcm_avx512.asm!"
%endif
%endif
%endif

;; Decide on AES-GCM key size to compile for
%ifdef GCM128_MODE
%define NROUNDS 9
%define FN_NAME(x,y) aes_gcm_ %+ x %+ _128 %+ y %+ vaes_avx512
%define GMAC_FN_NAME(x) imb_aes_gmac_ %+ x %+ _128_ %+ vaes_avx512
%endif

%ifdef GCM192_MODE
%define NROUNDS 11
%define FN_NAME(x,y) aes_gcm_ %+ x %+ _192 %+ y %+ vaes_avx512
%define GMAC_FN_NAME(x) imb_aes_gmac_ %+ x %+ _192_ %+ vaes_avx512
%endif

%ifdef GCM256_MODE
%define NROUNDS 13
%define FN_NAME(x,y) aes_gcm_ %+ x %+ _256 %+ y %+ vaes_avx512
%define GMAC_FN_NAME(x) imb_aes_gmac_ %+ x %+ _256_ %+ vaes_avx512
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Stack frame definition
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%ifidn __OUTPUT_FORMAT__, win64
        %define XMM_STORAGE     (10*16)      ; space for 10 XMM registers
        %define GP_STORAGE      ((9*8) + 24) ; space for 9 GP registers + 24 bytes for 64 byte alignment
%else
        %define XMM_STORAGE     0
        %define GP_STORAGE      (8*8)   ; space for 7 GP registers + 1 for alignment
%endif
%define LOCAL_STORAGE           (48*16) ; space for up to 48 AES blocks

;;; sequence is (bottom-up): GP, XMM, local
%define STACK_GP_OFFSET         0
%define STACK_XMM_OFFSET        (STACK_GP_OFFSET + GP_STORAGE)
%define STACK_LOCAL_OFFSET      (STACK_XMM_OFFSET + XMM_STORAGE)
%define STACK_FRAME_SIZE        (STACK_LOCAL_OFFSET + LOCAL_STORAGE)

;; for compatibility with stack argument definitions in gcm_defines.asm
%define STACK_OFFSET 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Utility Macros
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; ===========================================================================
;;; ===========================================================================
;;; Horizontal XOR - 4 x 128bits xored together
%macro VHPXORI4x128 2
%define %%REG   %1      ; [in/out] ZMM with 4x128bits to xor; 128bit output
%define %%TMP   %2      ; [clobbered] ZMM temporary register
        vextracti64x4   YWORD(%%TMP), %%REG, 1
        vpxorq          YWORD(%%REG), YWORD(%%REG), YWORD(%%TMP)
        vextracti32x4   XWORD(%%TMP), YWORD(%%REG), 1
        vpxorq          XWORD(%%REG), XWORD(%%REG), XWORD(%%TMP)
%endmacro               ; VHPXORI4x128

;;; ===========================================================================
;;; ===========================================================================
;;; AVX512 reduction macro
%macro VCLMUL_REDUCE 6
%define %%OUT   %1      ; [out] zmm/ymm/xmm: result (must not be %%TMP1 or %%HI128)
%define %%POLY  %2      ; [in] zmm/ymm/xmm: polynomial
%define %%HI128 %3      ; [in] zmm/ymm/xmm: high 128b of hash to reduce
%define %%LO128 %4      ; [in] zmm/ymm/xmm: low 128b of hash to reduce
%define %%TMP0  %5      ; [in] zmm/ymm/xmm: temporary register
%define %%TMP1  %6      ; [in] zmm/ymm/xmm: temporary register

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; first phase of the reduction
        vpclmulqdq      %%TMP0, %%POLY, %%LO128, 0x01
        vpslldq         %%TMP0, %%TMP0, 8       ; shift-L 2 DWs
        vpxorq          %%TMP0, %%LO128, %%TMP0 ; first phase of the reduction complete

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; second phase of the reduction
        vpclmulqdq      %%TMP1, %%POLY, %%TMP0, 0x00
        vpsrldq         %%TMP1, %%TMP1, 4       ; shift-R only 1-DW to obtain 2-DWs shift-R

        vpclmulqdq      %%OUT, %%POLY, %%TMP0, 0x10
        vpslldq         %%OUT, %%OUT, 4         ; shift-L 1-DW to obtain result with no shifts

        vpternlogq      %%OUT, %%TMP1, %%HI128, 0x96    ; OUT/GHASH = OUT xor TMP1 xor HI128
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%endmacro

;;; ===========================================================================
;;; ===========================================================================
;;; schoolbook multiply of 16 blocks (8 x 16 bytes)
;;; - it is assumed that data read from %%INPTR is already shuffled and
;;;   %%INPTR address is 64 byte aligned
;;; - there is an option to pass ready blocks through ZMM registers too.
;;;   4 extra parameters need to passed in such case and 21st argument can be empty
%macro GHASH_16 21-25
%define %%TYPE  %1      ; [in] ghash type: start (xor hash), mid, end (same as mid; no reduction),
                        ;      end_reduce (end with reduction), start_reduce
%define %%GH    %2      ; [in/out] ZMM ghash sum: high 128-bits
%define %%GM    %3      ; [in/out] ZMM ghash sum: middle 128-bits
%define %%GL    %4      ; [in/out] ZMM ghash sum: low 128-bits
%define %%INPTR %5      ; [in] data input pointer
%define %%INOFF %6      ; [in] data input offset
%define %%INDIS %7      ; [in] data input displacement
%define %%HKPTR %8      ; [in] hash key pointer
%define %%HKOFF %9      ; [in] hash key offset
%define %%HKDIS %10     ; [in] hash key displacement
%define %%HASH  %11     ; [in/out] ZMM hash value in/out
%define %%ZTMP0 %12     ; [clobbered] temporary ZMM
%define %%ZTMP1 %13     ; [clobbered] temporary ZMM
%define %%ZTMP2 %14     ; [clobbered] temporary ZMM
%define %%ZTMP3 %15     ; [clobbered] temporary ZMM
%define %%ZTMP4 %16     ; [clobbered] temporary ZMM
%define %%ZTMP5 %17     ; [clobbered] temporary ZMM
%define %%ZTMP6 %18     ; [clobbered] temporary ZMM
%define %%ZTMP7 %19     ; [clobbered] temporary ZMM
%define %%ZTMP8 %20     ; [clobbered] temporary ZMM
%define %%ZTMP9 %21     ; [clobbered] temporary ZMM
%define %%DAT0  %22     ; [in] ZMM with 4 blocks of input data (INPTR, INOFF, INDIS unused)
%define %%DAT1  %23     ; [in] ZMM with 4 blocks of input data (INPTR, INOFF, INDIS unused)
%define %%DAT2  %24     ; [in] ZMM with 4 blocks of input data (INPTR, INOFF, INDIS unused)
%define %%DAT3  %25     ; [in] ZMM with 4 blocks of input data (INPTR, INOFF, INDIS unused)

%assign start_ghash 0
%assign do_reduction 0

%ifidn %%TYPE, start
%assign start_ghash 1
%endif

%ifidn %%TYPE, start_reduce
%assign start_ghash 1
%assign do_reduction 1
%endif

%ifidn %%TYPE, end_reduce
%assign do_reduction 1
%endif

        ;; ghash blocks 0-3
%if %0 == 21
        vmovdqa64       %%ZTMP9, [%%INPTR + %%INOFF + %%INDIS]
%else
%xdefine %%ZTMP9 %%DAT0
%endif

%if start_ghash != 0
        vpxorq          %%ZTMP9, %%HASH
%endif
        vmovdqu64       %%ZTMP8, [%%HKPTR + %%HKOFF + %%HKDIS]
        vpclmulqdq      %%ZTMP0, %%ZTMP9, %%ZTMP8, 0x11     ; T0H = a1*b1
        vpclmulqdq      %%ZTMP1, %%ZTMP9, %%ZTMP8, 0x00     ; T0L = a0*b0
        vpclmulqdq      %%ZTMP2, %%ZTMP9, %%ZTMP8, 0x01     ; T0M1 = a1*b0
        vpclmulqdq      %%ZTMP3, %%ZTMP9, %%ZTMP8, 0x10     ; T0M2 = a0*b1
        ;; ghash blocks 4-7
%if %0 == 21
        vmovdqa64       %%ZTMP9, [%%INPTR + %%INOFF + %%INDIS + 64]
%else
%xdefine %%ZTMP9 %%DAT1
%endif
        vmovdqu64       %%ZTMP8, [%%HKPTR + %%HKOFF + %%HKDIS + 64]
        vpclmulqdq      %%ZTMP4, %%ZTMP9, %%ZTMP8, 0x11     ; T1H = a1*b1
        vpclmulqdq      %%ZTMP5, %%ZTMP9, %%ZTMP8, 0x00     ; T1L = a0*b0
        vpclmulqdq      %%ZTMP6, %%ZTMP9, %%ZTMP8, 0x01     ; T1M1 = a1*b0
        vpclmulqdq      %%ZTMP7, %%ZTMP9, %%ZTMP8, 0x10     ; T1M2 = a0*b1
        ;; update sums
%if start_ghash != 0
        vpxorq          %%GM, %%ZTMP2, %%ZTMP6              ; GM = T0M1 + T1M1
        vpxorq          %%GH, %%ZTMP0, %%ZTMP4              ; GH = T0H + T1H
        vpxorq          %%GL, %%ZTMP1, %%ZTMP5              ; GL = T0L + T1L
        vpternlogq      %%GM, %%ZTMP3, %%ZTMP7, 0x96        ; GM = T0M2 + T1M1
%else ;; mid, end, end_reduce
        vpternlogq      %%GM, %%ZTMP2, %%ZTMP6, 0x96        ; GM += T0M1 + T1M1
        vpternlogq      %%GH, %%ZTMP0, %%ZTMP4, 0x96        ; GH += T0H + T1H
        vpternlogq      %%GL, %%ZTMP1, %%ZTMP5, 0x96        ; GL += T0L + T1L
        vpternlogq      %%GM, %%ZTMP3, %%ZTMP7, 0x96        ; GM += T0M2 + T1M1
%endif
        ;; ghash blocks 8-11
%if %0 == 21
        vmovdqa64       %%ZTMP9, [%%INPTR + %%INOFF + %%INDIS + 128]
%else
%xdefine %%ZTMP9 %%DAT2
%endif
        vmovdqu64       %%ZTMP8, [%%HKPTR + %%HKOFF + %%HKDIS + 128]
        vpclmulqdq      %%ZTMP0, %%ZTMP9, %%ZTMP8, 0x11     ; T0H = a1*b1
        vpclmulqdq      %%ZTMP1, %%ZTMP9, %%ZTMP8, 0x00     ; T0L = a0*b0
        vpclmulqdq      %%ZTMP2, %%ZTMP9, %%ZTMP8, 0x01     ; T0M1 = a1*b0
        vpclmulqdq      %%ZTMP3, %%ZTMP9, %%ZTMP8, 0x10     ; T0M2 = a0*b1
        ;; ghash blocks 12-15
%if %0 == 21
        vmovdqa64       %%ZTMP9, [%%INPTR + %%INOFF + %%INDIS + 192]
%else
%xdefine %%ZTMP9 %%DAT3
%endif
        vmovdqu64       %%ZTMP8, [%%HKPTR + %%HKOFF + %%HKDIS + 192]
        vpclmulqdq      %%ZTMP4, %%ZTMP9, %%ZTMP8, 0x11     ; T1H = a1*b1
        vpclmulqdq      %%ZTMP5, %%ZTMP9, %%ZTMP8, 0x00     ; T1L = a0*b0
        vpclmulqdq      %%ZTMP6, %%ZTMP9, %%ZTMP8, 0x01     ; T1M1 = a1*b0
        vpclmulqdq      %%ZTMP7, %%ZTMP9, %%ZTMP8, 0x10     ; T1M2 = a0*b1
        ;; update sums
        vpternlogq      %%GM, %%ZTMP2, %%ZTMP6, 0x96        ; GM += T0M1 + T1M1
        vpternlogq      %%GH, %%ZTMP0, %%ZTMP4, 0x96        ; GH += T0H + T1H
        vpternlogq      %%GL, %%ZTMP1, %%ZTMP5, 0x96        ; GL += T0L + T1L
        vpternlogq      %%GM, %%ZTMP3, %%ZTMP7, 0x96        ; GM += T0M2 + T1M1

%if do_reduction != 0
        ;; integrate GM into GH and GL
        vpsrldq         %%ZTMP0, %%GM, 8
        vpslldq         %%ZTMP1, %%GM, 8
        vpxorq          %%GH, %%GH, %%ZTMP0
        vpxorq          %%GL, %%GL, %%ZTMP1

        ;; add GH and GL 128-bit words horizontally
        VHPXORI4x128    %%GH, %%ZTMP0
        VHPXORI4x128    %%GL, %%ZTMP1

        ;; reduction
        vmovdqa64       XWORD(%%ZTMP2), [rel POLY2]
        VCLMUL_REDUCE   XWORD(%%HASH), XWORD(%%ZTMP2), \
                        XWORD(%%GH), XWORD(%%GL), XWORD(%%ZTMP0), XWORD(%%ZTMP1)
%endif
%endmacro

;;; ===========================================================================
;;; ===========================================================================
;;; GHASH 1 to 16 blocks of cipher text
;;; - performs reduction at the end
;;; - it doesn't load the data and it assumed it is already loaded and
;;;   shuffled
;;; - single_call scenario only
%macro  GHASH_1_TO_16 17-20
%define %%KP            %1      ; [in] pointer to expanded keys
%define %%GHASH         %2      ; [out] ghash output
%define %%T0H           %3      ; [clobbered] temporary ZMM
%define %%T0L           %4      ; [clobbered] temporary ZMM
%define %%T0M1          %5      ; [clobbered] temporary ZMM
%define %%T0M2          %6      ; [clobbered] temporary ZMM
%define %%T1H           %7      ; [clobbered] temporary ZMM
%define %%T1L           %8      ; [clobbered] temporary ZMM
%define %%T1M1          %9      ; [clobbered] temporary ZMM
%define %%T1M2          %10     ; [clobbered] temporary ZMM
%define %%HK            %11     ; [clobbered] temporary ZMM
%define %%AAD_HASH_IN   %12     ; [in] input hash value
%define %%CIPHER_IN0    %13     ; [in] ZMM with cipher text blocks 0-3
%define %%CIPHER_IN1    %14     ; [in] ZMM with cipher text blocks 4-7
%define %%CIPHER_IN2    %15     ; [in] ZMM with cipher text blocks 8-11
%define %%CIPHER_IN3    %16     ; [in] ZMM with cipher text blocks 12-15
%define %%NUM_BLOCKS    %17     ; [in] numerical value, number of blocks
%define %%GH            %18     ; [in] ZMM with hi product part
%define %%GM            %19     ; [in] ZMM with mid product part
%define %%GL            %20     ; [in] ZMM with lo product part

%assign hashk           HashKey_ %+ %%NUM_BLOCKS

%if %0 == 17
        vpxorq          %%CIPHER_IN0, %%CIPHER_IN0, %%AAD_HASH_IN
%endif

%if %%NUM_BLOCKS == 16

        vmovdqu64       %%HK, [%%KP + hashk]
        vpclmulqdq      %%T0H,  %%CIPHER_IN0, %%HK, 0x11  ; H = a1*b1
        vpclmulqdq      %%T0L,  %%CIPHER_IN0, %%HK, 0x00  ; L = a0*b0
        vpclmulqdq      %%T0M1, %%CIPHER_IN0, %%HK, 0x01  ; M1 = a1*b0
        vpclmulqdq      %%T0M2, %%CIPHER_IN0, %%HK, 0x10  ; M2 = a0*b1
        vmovdqu64       %%HK, [%%KP + hashk + (1*64)]
        vpclmulqdq      %%T1H,  %%CIPHER_IN1, %%HK, 0x11  ; H = a1*b1
        vpclmulqdq      %%T1L,  %%CIPHER_IN1, %%HK, 0x00  ; L = a0*b0
        vpclmulqdq      %%T1M1, %%CIPHER_IN1, %%HK, 0x01  ; M1 = a1*b0
        vpclmulqdq      %%T1M2, %%CIPHER_IN1, %%HK, 0x10  ; M2 = a0*b1
        vmovdqu64       %%HK, [%%KP + hashk + (2*64)]
        vpclmulqdq      %%CIPHER_IN0,  %%CIPHER_IN2, %%HK, 0x11  ; H = a1*b1
        vpclmulqdq      %%CIPHER_IN1,  %%CIPHER_IN2, %%HK, 0x00  ; L = a0*b0
        vpternlogq      %%T0H, %%CIPHER_IN0, %%T1H, 0x96
        vpternlogq      %%T0L, %%CIPHER_IN1, %%T1L, 0x96
        vpclmulqdq      %%CIPHER_IN0, %%CIPHER_IN2, %%HK, 0x01  ; M1 = a1*b0
        vpclmulqdq      %%CIPHER_IN1, %%CIPHER_IN2, %%HK, 0x10  ; M2 = a0*b1
        vpternlogq      %%T0M1, %%CIPHER_IN0, %%T1M1, 0x96
        vpternlogq      %%T0M2, %%CIPHER_IN1, %%T1M2, 0x96
        vmovdqu64       %%HK, [%%KP + hashk + (3*64)]
        vpclmulqdq      %%T1H,  %%CIPHER_IN3, %%HK, 0x11  ; H = a1*b1
        vpclmulqdq      %%T1L,  %%CIPHER_IN3, %%HK, 0x00  ; L = a0*b0
        vpclmulqdq      %%T1M1, %%CIPHER_IN3, %%HK, 0x01  ; M1 = a1*b0
        vpclmulqdq      %%T1M2, %%CIPHER_IN3, %%HK, 0x10  ; M2 = a0*b1
        vpxorq          %%T1H, %%T0H, %%T1H
        vpxorq          %%T1L, %%T0L, %%T1L
        vpxorq          %%T1M1, %%T0M1, %%T1M1
        vpxorq          %%T1M2, %%T0M2, %%T1M2

%elif %%NUM_BLOCKS >= 12

        vmovdqu64       %%HK, [%%KP + hashk]
        vpclmulqdq      %%T0H,  %%CIPHER_IN0, %%HK, 0x11  ; H = a1*b1
        vpclmulqdq      %%T0L,  %%CIPHER_IN0, %%HK, 0x00  ; L = a0*b0
        vpclmulqdq      %%T0M1, %%CIPHER_IN0, %%HK, 0x01  ; M1 = a1*b0
        vpclmulqdq      %%T0M2, %%CIPHER_IN0, %%HK, 0x10  ; M2 = a0*b1
        vmovdqu64       %%HK, [%%KP + hashk + (1*64)]
        vpclmulqdq      %%T1H,  %%CIPHER_IN1, %%HK, 0x11  ; H = a1*b1
        vpclmulqdq      %%T1L,  %%CIPHER_IN1, %%HK, 0x00  ; L = a0*b0
        vpclmulqdq      %%T1M1, %%CIPHER_IN1, %%HK, 0x01  ; M1 = a1*b0
        vpclmulqdq      %%T1M2, %%CIPHER_IN1, %%HK, 0x10  ; M2 = a0*b1
        vmovdqu64       %%HK, [%%KP + hashk + (2*64)]
        vpclmulqdq      %%CIPHER_IN0,  %%CIPHER_IN2, %%HK, 0x11  ; H = a1*b1
        vpclmulqdq      %%CIPHER_IN1,  %%CIPHER_IN2, %%HK, 0x00  ; L = a0*b0
        vpternlogq      %%T1H, %%CIPHER_IN0, %%T0H, 0x96
        vpternlogq      %%T1L, %%CIPHER_IN1, %%T0L, 0x96
        vpclmulqdq      %%CIPHER_IN0, %%CIPHER_IN2, %%HK, 0x01  ; M1 = a1*b0
        vpclmulqdq      %%CIPHER_IN1, %%CIPHER_IN2, %%HK, 0x10  ; M2 = a0*b1
        vpternlogq      %%T1M1, %%CIPHER_IN0, %%T0M1, 0x96
        vpternlogq      %%T1M2, %%CIPHER_IN1, %%T0M2, 0x96

%elif %%NUM_BLOCKS >= 8

        vmovdqu64       %%HK, [%%KP + hashk]
        vpclmulqdq      %%T0H,  %%CIPHER_IN0, %%HK, 0x11  ; H = a1*b1
        vpclmulqdq      %%T0L,  %%CIPHER_IN0, %%HK, 0x00  ; L = a0*b0
        vpclmulqdq      %%T0M1, %%CIPHER_IN0, %%HK, 0x01  ; M1 = a1*b0
        vpclmulqdq      %%T0M2, %%CIPHER_IN0, %%HK, 0x10  ; M2 = a0*b1
        vmovdqu64       %%HK, [%%KP + hashk + (1*64)]
        vpclmulqdq      %%T1H,  %%CIPHER_IN1, %%HK, 0x11  ; H = a1*b1
        vpclmulqdq      %%T1L,  %%CIPHER_IN1, %%HK, 0x00  ; L = a0*b0
        vpclmulqdq      %%T1M1, %%CIPHER_IN1, %%HK, 0x01  ; M1 = a1*b0
        vpclmulqdq      %%T1M2, %%CIPHER_IN1, %%HK, 0x10  ; M2 = a0*b1
        vpxorq          %%T1H, %%T0H, %%T1H
        vpxorq          %%T1L, %%T0L, %%T1L
        vpxorq          %%T1M1, %%T0M1, %%T1M1
        vpxorq          %%T1M2, %%T0M2, %%T1M2

%elif %%NUM_BLOCKS >= 4

        vmovdqu64       %%HK, [%%KP + hashk]
        vpclmulqdq      %%T1H,  %%CIPHER_IN0, %%HK, 0x11  ; H = a1*b1
        vpclmulqdq      %%T1L,  %%CIPHER_IN0, %%HK, 0x00  ; L = a0*b0
        vpclmulqdq      %%T1M1, %%CIPHER_IN0, %%HK, 0x01  ; M1 = a1*b0
        vpclmulqdq      %%T1M2, %%CIPHER_IN0, %%HK, 0x10  ; M2 = a0*b1

%endif

        ;; T1H/L/M1/M2 - hold current product sums (provided %%NUM_BLOCKS >= 4)
%assign blocks_left (%%NUM_BLOCKS % 4)

%if blocks_left > 0
        ;; =====================================================
        ;; There are 1, 2 or 3 blocks left to process.
        ;; It may also be that they are the only blocks to process.

;; Set hash key and register index position for the remaining 1 to 3 blocks
%assign hashk   HashKey_ %+ blocks_left
%assign reg_idx (%%NUM_BLOCKS / 4)

%xdefine %%REG_IN %%CIPHER_IN %+ reg_idx

%if blocks_left == 1
        vmovdqu64       XWORD(%%HK), [%%KP + hashk]
        vpclmulqdq      XWORD(%%T0M1), XWORD(%%REG_IN), XWORD(%%HK), 0x01     ; M1 = a1*b0
        vpclmulqdq      XWORD(%%T0M2), XWORD(%%REG_IN), XWORD(%%HK), 0x10     ; M2 = a0*b1
        vpclmulqdq      XWORD(%%T0H), XWORD(%%REG_IN), XWORD(%%HK), 0x11      ; H = a1*b1
        vpclmulqdq      XWORD(%%T0L), XWORD(%%REG_IN), XWORD(%%HK), 0x00      ; L = a0*b0
%elif blocks_left == 2
        vmovdqu64       YWORD(%%HK), [%%KP + hashk]
        vpclmulqdq      YWORD(%%T0M1), YWORD(%%REG_IN), YWORD(%%HK), 0x01     ; M1 = a1*b0
        vpclmulqdq      YWORD(%%T0M2), YWORD(%%REG_IN), YWORD(%%HK), 0x10     ; M2 = a0*b1
        vpclmulqdq      YWORD(%%T0H), YWORD(%%REG_IN), YWORD(%%HK), 0x11      ; H = a1*b1
        vpclmulqdq      YWORD(%%T0L), YWORD(%%REG_IN), YWORD(%%HK), 0x00      ; L = a0*b0
%else ; blocks_left == 3
        vmovdqu64       YWORD(%%HK), [%%KP + hashk]
        vinserti64x2    %%HK, [%%KP + hashk + 32], 2
        vpclmulqdq      %%T0M1, %%REG_IN, %%HK, 0x01     ; M1 = a1*b0
        vpclmulqdq      %%T0M2, %%REG_IN, %%HK, 0x10     ; M2 = a0*b1
        vpclmulqdq      %%T0H, %%REG_IN, %%HK, 0x11      ; H = a1*b1
        vpclmulqdq      %%T0L, %%REG_IN, %%HK, 0x00      ; L = a0*b0
%endif ; blocks_left

%undef %%REG_IN

%if %0 == 20
        ;; *** GH/GM/GL passed as arguments
%if %%NUM_BLOCKS >= 4
        ;; add ghash product sums from the first 4, 8 or 12 blocks
        vpxorq          %%T0M1, %%T0M1, %%T1M1
        vpternlogq      %%T0M2, %%GM, %%T1M2, 0x96
        vpternlogq      %%T0H, %%GH, %%T1H, 0x96
        vpternlogq      %%T0L, %%GL, %%T1L, 0x96
%else
        vpxorq          %%T0M1, %%T0M1, %%GM
        vpxorq          %%T0H, %%T0H, %%GH
        vpxorq          %%T0L, %%T0L, %%GL
%endif ;; %%NUM_BLOCKS >= 4
%else
        ;; *** GH/GM/GL NOT passed as arguments
%if %%NUM_BLOCKS >= 4
        ;; add ghash product sums from the first 4, 8 or 12 blocks
        vpxorq          %%T0M1, %%T0M1, %%T1M1
        vpxorq          %%T0M2, %%T0M2, %%T1M2
        vpxorq          %%T0H, %%T0H, %%T1H
        vpxorq          %%T0L, %%T0L, %%T1L
%endif ;; %%NUM_BLOCKS >= 4
%endif ;; %0 == 20

        ;; integrate TM into TH and TL
        vpxorq          %%T0M1, %%T0M1, %%T0M2
        vpsrldq         %%T1M1, %%T0M1, 8
        vpslldq         %%T1M2, %%T0M1, 8
        vpxorq          %%T0H, %%T0H, %%T1M1
        vpxorq          %%T0L, %%T0L, %%T1M2
%else
        ;; =====================================================
        ;; number of blocks is 4, 8, 12 or 16
        ;; T1H/L/M1/M2 include product sums not T0H/L/M1/M2
%if %0 == 20
        ;; *** GH/GM/GL passed as arguments
        vpxorq          %%T1M1, %%T1M1, %%GM
        vpxorq          %%T1H, %%T1H, %%GH
        vpxorq          %%T1L, %%T1L, %%GL
%endif
        ;; integrate TM into TH and TL
        vpxorq          %%T1M1, %%T1M1, %%T1M2
        vpsrldq         %%T0M1, %%T1M1, 8
        vpslldq         %%T0M2, %%T1M1, 8
        vpxorq          %%T0H, %%T1H, %%T0M1
        vpxorq          %%T0L, %%T1L, %%T0M2
%endif ; blocks_left > 0

        ;; add TH and TL 128-bit words horizontally
        VHPXORI4x128    %%T0H, %%T1M1
        VHPXORI4x128    %%T0L, %%T1M2

        ;; reduction
        vmovdqa64       XWORD(%%HK), [rel POLY2]
        VCLMUL_REDUCE   XWORD(%%GHASH), XWORD(%%HK), \
                        XWORD(%%T0H), XWORD(%%T0L), XWORD(%%T0M1), XWORD(%%T0M2)
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; GHASH_MUL MACRO to implement: Data*HashKey mod (128,127,126,121,0)
;;; Input: A and B (128-bits each, bit-reflected)
;;; Output: C = A*B*x mod poly, (i.e. >>1 )
;;; To compute GH = GH*HashKey mod poly, give HK = HashKey<<1 mod poly as input
;;; GH = GH * HK * x mod poly which is equivalent to GH*HashKey mod poly.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro  GHASH_MUL  7
%define %%GH %1         ;; [in/out] xmm/ymm/zmm with multiply operand(s) (128-bits)
%define %%HK %2         ;; [in] xmm/ymm/zmm with hash key value(s) (128-bits)
%define %%T1 %3         ;; [clobbered] xmm/ymm/zmm
%define %%T2 %4         ;; [clobbered] xmm/ymm/zmm
%define %%T3 %5         ;; [clobbered] xmm/ymm/zmm
%define %%T4 %6         ;; [clobbered] xmm/ymm/zmm
%define %%T5 %7         ;; [clobbered] xmm/ymm/zmm

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        vpclmulqdq      %%T1, %%GH, %%HK, 0x11  ; %%T1 = a1*b1
        vpclmulqdq      %%T2, %%GH, %%HK, 0x00  ; %%T2 = a0*b0
        vpclmulqdq      %%T3, %%GH, %%HK, 0x01  ; %%T3 = a1*b0
        vpclmulqdq      %%GH, %%GH, %%HK, 0x10  ; %%GH = a0*b1
        vpxorq          %%GH, %%GH, %%T3

        vpsrldq         %%T3, %%GH, 8           ; shift-R %%GH 2 DWs
        vpslldq         %%GH, %%GH, 8           ; shift-L %%GH 2 DWs

        vpxorq          %%T1, %%T1, %%T3
        vpxorq          %%GH, %%GH, %%T2

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;first phase of the reduction
        vmovdqu64       %%T3, [rel POLY2]

        vpclmulqdq      %%T2, %%T3, %%GH, 0x01
        vpslldq         %%T2, %%T2, 8           ; shift-L %%T2 2 DWs

        vpxorq          %%GH, %%GH, %%T2        ; first phase of the reduction complete
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;second phase of the reduction
        vpclmulqdq      %%T2, %%T3, %%GH, 0x00
        vpsrldq         %%T2, %%T2, 4           ; shift-R only 1-DW to obtain 2-DWs shift-R

        vpclmulqdq      %%GH, %%T3, %%GH, 0x10
        vpslldq         %%GH, %%GH, 4           ; Shift-L 1-DW to obtain result with no shifts

        ; second phase of the reduction complete, the result is in %%GH
        vpternlogq      %%GH, %%T1, %%T2, 0x96  ; GH = GH xor T1 xor T2
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; In PRECOMPUTE, the commands filling Hashkey_i_k are not required for avx512
;;; functions, but are kept to allow users to switch cpu architectures between calls
;;; of pre, init, update, and finalize.
%macro  PRECOMPUTE 10
%define %%GDATA %1      ;; [in/out] GPR, pointer to GCM key data structure, content updated
%define %%HK    %2      ;; [in] xmm, hash key
%define %%T1    %3      ;; [clobbered] xmm
%define %%T2    %4      ;; [clobbered] xmm
%define %%T3    %5      ;; [clobbered] xmm
%define %%T4    %6      ;; [clobbered] xmm
%define %%T5    %7      ;; [clobbered] xmm
%define %%T6    %8      ;; [clobbered] xmm
%define %%T7    %9      ;; [clobbered] xmm
%define %%T8    %10     ;; [clobbered] xmm

%xdefine %%ZT1 ZWORD(%%T1)
%xdefine %%ZT2 ZWORD(%%T2)
%xdefine %%ZT3 ZWORD(%%T3)
%xdefine %%ZT4 ZWORD(%%T4)
%xdefine %%ZT5 ZWORD(%%T5)
%xdefine %%ZT6 ZWORD(%%T6)
%xdefine %%ZT7 ZWORD(%%T7)
%xdefine %%ZT8 ZWORD(%%T8)

        vmovdqa64       %%T5, %%HK
        vinserti64x2    %%ZT7, %%HK, 3

        ;; calculate HashKey^2<<1 mod poly
        GHASH_MUL       %%T5, %%HK, %%T1, %%T3, %%T4, %%T6, %%T2
        vmovdqu64       [%%GDATA + HashKey_2], %%T5
        vinserti64x2    %%ZT7, %%T5, 2

        ;; calculate HashKey^3<<1 mod poly
        GHASH_MUL       %%T5, %%HK, %%T1, %%T3, %%T4, %%T6, %%T2
        vmovdqu64       [%%GDATA + HashKey_3], %%T5
        vinserti64x2    %%ZT7, %%T5, 1

        ;; calculate HashKey^4<<1 mod poly
        GHASH_MUL       %%T5, %%HK, %%T1, %%T3, %%T4, %%T6, %%T2
        vmovdqu64       [%%GDATA + HashKey_4], %%T5
        vinserti64x2    %%ZT7, %%T5, 0

        ;; switch to 4x128-bit computations now
        vshufi64x2      %%ZT5, %%ZT5, %%ZT5, 0x00       ;; broadcast HashKey^4 across all ZT5
        vmovdqa64       %%ZT8, %%ZT7                    ;; save HashKey^4 to HashKey^1 in ZT8

        ;; calculate HashKey^5<<1 mod poly, HashKey^6<<1 mod poly, ... HashKey^8<<1 mod poly
        GHASH_MUL       %%ZT7, %%ZT5, %%ZT1, %%ZT3, %%ZT4, %%ZT6, %%ZT2
        vmovdqu64       [%%GDATA + HashKey_8], %%ZT7    ;; HashKey^8 to HashKey^5 in ZT7 now
        vshufi64x2      %%ZT5, %%ZT7, %%ZT7, 0x00       ;; broadcast HashKey^8 across all ZT5

        ;; calculate HashKey^9<<1 mod poly, HashKey^10<<1 mod poly, ... HashKey^48<<1 mod poly
        ;; use HashKey^8 as multiplier against ZT8 and ZT7 - this allows deeper ooo execution
%assign i 12
%rep ((48 - 8) / 8)
        ;; compute HashKey^(4 + n), HashKey^(3 + n), ... HashKey^(1 + n)
        GHASH_MUL       %%ZT8, %%ZT5, %%ZT1, %%ZT3, %%ZT4, %%ZT6, %%ZT2
        vmovdqu64       [%%GDATA + HashKey_ %+ i], %%ZT8
%assign i (i + 4)

        ;; compute HashKey^(8 + n), HashKey^(7 + n), ... HashKey^(5 + n)
        GHASH_MUL       %%ZT7, %%ZT5, %%ZT1, %%ZT3, %%ZT4, %%ZT6, %%ZT2
        vmovdqu64       [%%GDATA + HashKey_ %+ i], %%ZT7
%assign i (i + 4)
%endrep
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; READ_SMALL_DATA_INPUT
;;; Packs xmm register with data when data input is less or equal to 16 bytes
;;; Returns 0 if data has length 0
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro READ_SMALL_DATA_INPUT    6
%define %%OUTPUT        %1 ; [out] xmm register
%define %%INPUT         %2 ; [in] buffer pointer to read from
%define %%LENGTH        %3 ; [in] number of bytes to read
%define %%TMP1          %4 ; [clobbered]
%define %%TMP2          %5 ; [clobbered]
%define %%MASK          %6 ; [out] k1 to k7 register to store the partial block mask

        mov             DWORD(%%TMP2), 16
        lea             %%TMP1, [rel byte_len_to_mask_table]
        cmp             %%LENGTH, %%TMP2
        cmovb           %%TMP2, %%LENGTH
%ifidn __OUTPUT_FORMAT__, win64
        add             %%TMP1, %%TMP2
        add             %%TMP1, %%TMP2
        kmovw           %%MASK, [%%TMP1]
%else
        kmovw           %%MASK, [%%TMP1 + %%TMP2*2]
%endif
        vmovdqu8        %%OUTPUT{%%MASK}{z}, [%%INPUT]
%endmacro ; READ_SMALL_DATA_INPUT

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; CALC_AAD_HASH: Calculates the hash of the data which will not be encrypted.
; Input: The input data (A_IN), that data's length (A_LEN), and the hash key (HASH_KEY).
; Output: The hash of the data (AAD_HASH).
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro  CALC_AAD_HASH   26
%define %%A_IN          %1      ; [in] AAD text pointer
%define %%A_LEN         %2      ; [in] AAD length
%define %%AAD_HASH      %3      ; [in/out] xmm ghash value
%define %%GDATA_KEY     %4      ; [in] pointer to keys
%define %%ZT0           %5      ; [clobbered] ZMM register
%define %%ZT1           %6      ; [clobbered] ZMM register
%define %%ZT2           %7      ; [clobbered] ZMM register
%define %%ZT3           %8      ; [clobbered] ZMM register
%define %%ZT4           %9      ; [clobbered] ZMM register
%define %%ZT5           %10     ; [clobbered] ZMM register
%define %%ZT6           %11     ; [clobbered] ZMM register
%define %%ZT7           %12     ; [clobbered] ZMM register
%define %%ZT8           %13     ; [clobbered] ZMM register
%define %%ZT9           %14     ; [clobbered] ZMM register
%define %%ZT10          %15     ; [clobbered] ZMM register
%define %%ZT11          %16     ; [clobbered] ZMM register
%define %%ZT12          %17     ; [clobbered] ZMM register
%define %%ZT13          %18     ; [clobbered] ZMM register
%define %%ZT14          %19     ; [clobbered] ZMM register
%define %%ZT15          %20     ; [clobbered] ZMM register
%define %%ZT16          %21     ; [clobbered] ZMM register
%define %%ZT17          %22     ; [clobbered] ZMM register
%define %%T1            %23     ; [clobbered] GP register
%define %%T2            %24     ; [clobbered] GP register
%define %%T3            %25     ; [clobbered] GP register
%define %%MASKREG       %26     ; [clobbered] mask register

%define %%SHFMSK %%ZT13

        mov             %%T1, %%A_IN            ; T1 = AAD
        mov             %%T2, %%A_LEN           ; T2 = aadLen

        or              %%T2, %%T2
        jz              %%_CALC_AAD_done

        vmovdqa64       %%SHFMSK, [rel SHUF_MASK]

%%_get_AAD_loop48x16:
        cmp             %%T2, (48*16)
        jl              %%_exit_AAD_loop48x16

        vmovdqu64       %%ZT1, [%%T1 + 64*0]  ; Blocks 0-3
        vmovdqu64       %%ZT2, [%%T1 + 64*1]  ; Blocks 4-7
        vmovdqu64       %%ZT3, [%%T1 + 64*2]  ; Blocks 8-11
        vmovdqu64       %%ZT4, [%%T1 + 64*3]  ; Blocks 12-15
        vpshufb         %%ZT1, %%SHFMSK
        vpshufb         %%ZT2, %%SHFMSK
        vpshufb         %%ZT3, %%SHFMSK
        vpshufb         %%ZT4, %%SHFMSK

        GHASH_16        start, %%ZT5, %%ZT6, %%ZT7, \
                        NO_INPUT_PTR, NO_INPUT_PTR, NO_INPUT_PTR, \
                        %%GDATA_KEY, HashKey_48, 0, ZWORD(%%AAD_HASH), \
                        %%ZT0, %%ZT8, %%ZT9, %%ZT10, %%ZT11, %%ZT12, \
                        %%ZT14, %%ZT15, %%ZT16, NO_ZMM, \
                        %%ZT1, %%ZT2, %%ZT3, %%ZT4

        vmovdqu64       %%ZT1, [%%T1 + 16*16 + 64*0]  ; Blocks 16-19
        vmovdqu64       %%ZT2, [%%T1 + 16*16 + 64*1]  ; Blocks 20-23
        vmovdqu64       %%ZT3, [%%T1 + 16*16 + 64*2]  ; Blocks 24-27
        vmovdqu64       %%ZT4, [%%T1 + 16*16 + 64*3]  ; Blocks 28-31
        vpshufb         %%ZT1, %%SHFMSK
        vpshufb         %%ZT2, %%SHFMSK
        vpshufb         %%ZT3, %%SHFMSK
        vpshufb         %%ZT4, %%SHFMSK

        GHASH_16        mid, %%ZT5, %%ZT6, %%ZT7, \
                        NO_INPUT_PTR, NO_INPUT_PTR, NO_INPUT_PTR, \
                        %%GDATA_KEY, HashKey_32, 0, NO_HASH_IN_OUT, \
                        %%ZT0, %%ZT8, %%ZT9, %%ZT10, %%ZT11, %%ZT12, \
                        %%ZT14, %%ZT15, %%ZT16, NO_ZMM, \
                        %%ZT1, %%ZT2, %%ZT3, %%ZT4

        vmovdqu64       %%ZT1, [%%T1 + 32*16 + 64*0]  ; Blocks 32-35
        vmovdqu64       %%ZT2, [%%T1 + 32*16 + 64*1]  ; Blocks 36-39
        vmovdqu64       %%ZT3, [%%T1 + 32*16 + 64*2]  ; Blocks 40-43
        vmovdqu64       %%ZT4, [%%T1 + 32*16 + 64*3]  ; Blocks 44-47
        vpshufb         %%ZT1, %%SHFMSK
        vpshufb         %%ZT2, %%SHFMSK
        vpshufb         %%ZT3, %%SHFMSK
        vpshufb         %%ZT4, %%SHFMSK

        GHASH_16        end_reduce, %%ZT5, %%ZT6, %%ZT7, \
                        NO_INPUT_PTR, NO_INPUT_PTR, NO_INPUT_PTR, \
                        %%GDATA_KEY, HashKey_16, 0, ZWORD(%%AAD_HASH), \
                        %%ZT0, %%ZT8, %%ZT9, %%ZT10, %%ZT11, %%ZT12, \
                        %%ZT14, %%ZT15, %%ZT16, NO_ZMM, \
                        %%ZT1, %%ZT2, %%ZT3, %%ZT4

        sub             %%T2, (48*16)
        je              %%_CALC_AAD_done

        add             %%T1, (48*16)
        jmp             %%_get_AAD_loop48x16

%%_exit_AAD_loop48x16:
        ; Less than 48x16 bytes remaining
        cmp             %%T2, (32*16)
        jl              %%_less_than_32x16

        ; Get next 16 blocks
        vmovdqu64       %%ZT1, [%%T1 + 64*0]
        vmovdqu64       %%ZT2, [%%T1 + 64*1]
        vmovdqu64       %%ZT3, [%%T1 + 64*2]
        vmovdqu64       %%ZT4, [%%T1 + 64*3]
        vpshufb         %%ZT1, %%SHFMSK
        vpshufb         %%ZT2, %%SHFMSK
        vpshufb         %%ZT3, %%SHFMSK
        vpshufb         %%ZT4, %%SHFMSK

        GHASH_16        start, %%ZT5, %%ZT6, %%ZT7, \
                        NO_INPUT_PTR, NO_INPUT_PTR, NO_INPUT_PTR, \
                        %%GDATA_KEY, HashKey_32, 0, ZWORD(%%AAD_HASH), \
                        %%ZT0, %%ZT8, %%ZT9, %%ZT10, %%ZT11, %%ZT12, \
                        %%ZT14, %%ZT15, %%ZT16, NO_ZMM, \
                        %%ZT1, %%ZT2, %%ZT3, %%ZT4

        vmovdqu64       %%ZT1, [%%T1 + 16*16 + 64*0]
        vmovdqu64       %%ZT2, [%%T1 + 16*16 + 64*1]
        vmovdqu64       %%ZT3, [%%T1 + 16*16 + 64*2]
        vmovdqu64       %%ZT4, [%%T1 + 16*16 + 64*3]
        vpshufb         %%ZT1, %%SHFMSK
        vpshufb         %%ZT2, %%SHFMSK
        vpshufb         %%ZT3, %%SHFMSK
        vpshufb         %%ZT4, %%SHFMSK

        GHASH_16        end_reduce, %%ZT5, %%ZT6, %%ZT7, \
                        NO_INPUT_PTR, NO_INPUT_PTR, NO_INPUT_PTR, \
                        %%GDATA_KEY, HashKey_16, 0, ZWORD(%%AAD_HASH), \
                        %%ZT0, %%ZT8, %%ZT9, %%ZT10, %%ZT11, %%ZT12, \
                        %%ZT14, %%ZT15, %%ZT16, NO_ZMM, \
                        %%ZT1, %%ZT2, %%ZT3, %%ZT4

        sub             %%T2, (32*16)
        je              %%_CALC_AAD_done

        add             %%T1, (32*16)
        jmp             %%_less_than_16x16

%%_less_than_32x16:
        cmp             %%T2, (16*16)
        jl              %%_less_than_16x16
        ; Get next 16 blocks
        vmovdqu64       %%ZT1, [%%T1 + 64*0]
        vmovdqu64       %%ZT2, [%%T1 + 64*1]
        vmovdqu64       %%ZT3, [%%T1 + 64*2]
        vmovdqu64       %%ZT4, [%%T1 + 64*3]
        vpshufb         %%ZT1, %%SHFMSK
        vpshufb         %%ZT2, %%SHFMSK
        vpshufb         %%ZT3, %%SHFMSK
        vpshufb         %%ZT4, %%SHFMSK

        GHASH_16        start_reduce, %%ZT5, %%ZT6, %%ZT7, \
                        NO_INPUT_PTR, NO_INPUT_PTR, NO_INPUT_PTR, \
                        %%GDATA_KEY, HashKey_16, 0, ZWORD(%%AAD_HASH), \
                        %%ZT0, %%ZT8, %%ZT9, %%ZT10, %%ZT11, %%ZT12, \
                        %%ZT14, %%ZT15, %%ZT16, NO_ZMM, \
                        %%ZT1, %%ZT2, %%ZT3, %%ZT4

        sub             %%T2, (16*16)
        je              %%_CALC_AAD_done

        add             %%T1, (16*16)

        ; Less than 16x16 bytes remaining
%%_less_than_16x16:
        ;; prep mask source address
        lea             %%T3, [rel byte64_len_to_mask_table]
        lea             %%T3, [%%T3 + %%T2*8]

        ;; calculate number of blocks to ghash (including partial bytes)
        add             DWORD(%%T2), 15
        shr             DWORD(%%T2), 4
        cmp             DWORD(%%T2), 2
        jb              %%_AAD_blocks_1
        je              %%_AAD_blocks_2
        cmp             DWORD(%%T2), 4
        jb              %%_AAD_blocks_3
        je              %%_AAD_blocks_4
        cmp             DWORD(%%T2), 6
        jb              %%_AAD_blocks_5
        je              %%_AAD_blocks_6
        cmp             DWORD(%%T2), 8
        jb              %%_AAD_blocks_7
        je              %%_AAD_blocks_8
        cmp             DWORD(%%T2), 10
        jb              %%_AAD_blocks_9
        je              %%_AAD_blocks_10
        cmp             DWORD(%%T2), 12
        jb              %%_AAD_blocks_11
        je              %%_AAD_blocks_12
        cmp             DWORD(%%T2), 14
        jb              %%_AAD_blocks_13
        je              %%_AAD_blocks_14
        cmp             DWORD(%%T2), 15
        je              %%_AAD_blocks_15
        ;; fall through for 16 blocks

        ;; The flow of each of these cases is identical:
        ;; - load blocks plain text
        ;; - shuffle loaded blocks
        ;; - xor in current hash value into block 0
        ;; - perform up multiplications with ghash keys
        ;; - jump to reduction code

%assign I 16
        ;; generate all 16 cases using preprocessor
%rep 16

%%_AAD_blocks_ %+ I:
%if I > 12
        sub             %%T3, 12 * 16 * 8
%elif I > 8
        sub             %%T3, 8 * 16 * 8
%elif I > 4
        sub             %%T3, 4 * 16 * 8
%endif
        kmovq           %%MASKREG, [%%T3]

        ZMM_LOAD_MASKED_BLOCKS_0_16 \
                        I, %%T1, 0, \
                        %%ZT1, %%ZT2, %%ZT3, %%ZT4, %%MASKREG

        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 \
                        I, vpshufb, \
                        %%ZT1, %%ZT2, %%ZT3, %%ZT4, \
                        %%ZT1, %%ZT2, %%ZT3, %%ZT4, \
                        %%SHFMSK, %%SHFMSK, %%SHFMSK, %%SHFMSK

        GHASH_1_TO_16 %%GDATA_KEY, ZWORD(%%AAD_HASH), \
                        %%ZT0, %%ZT5, %%ZT6, %%ZT7, %%ZT8, \
                        %%ZT9, %%ZT10, %%ZT11, %%ZT12, \
                        ZWORD(%%AAD_HASH), %%ZT1, %%ZT2, %%ZT3, %%ZT4, I
%if I > 1
        ;; fall through to CALC_AAD_done in 1 block case
        jmp             %%_CALC_AAD_done
%endif

%assign I (I - 1)
%endrep

%%_CALC_AAD_done:
        ;; result in AAD_HASH

%endmacro ; CALC_AAD_HASH

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; PARTIAL_BLOCK
;;; Handles encryption/decryption and the tag partial blocks between
;;; update calls.
;;; Requires the input data be at least 1 byte long.
;;; Output:
;;; A cipher/plain of the first partial block (CIPH_PLAIN_OUT),
;;; AAD_HASH and updated GDATA_CTX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro PARTIAL_BLOCK 22
%define %%GDATA_KEY             %1 ; [in] key pointer
%define %%GDATA_CTX             %2 ; [in] context pointer
%define %%CIPH_PLAIN_OUT        %3 ; [in] output buffer
%define %%PLAIN_CIPH_IN         %4 ; [in] input buffer
%define %%PLAIN_CIPH_LEN        %5 ; [in] buffer length
%define %%DATA_OFFSET           %6 ; [out] data offset (gets set)
%define %%AAD_HASH              %7 ; [out] updated GHASH value
%define %%ENC_DEC               %8 ; [in] cipher direction
%define %%GPTMP0                %9 ; [clobbered] GP temporary register
%define %%GPTMP1                %10 ; [clobbered] GP temporary register
%define %%GPTMP2                %11 ; [clobbered] GP temporary register
%define %%ZTMP0                 %12 ; [clobbered] ZMM temporary register
%define %%ZTMP1                 %13 ; [clobbered] ZMM temporary register
%define %%ZTMP2                 %14 ; [clobbered] ZMM temporary register
%define %%ZTMP3                 %15 ; [clobbered] ZMM temporary register
%define %%ZTMP4                 %16 ; [clobbered] ZMM temporary register
%define %%ZTMP5                 %17 ; [clobbered] ZMM temporary register
%define %%ZTMP6                 %18 ; [clobbered] ZMM temporary register
%define %%ZTMP7                 %19 ; [clobbered] ZMM temporary register
%define %%ZTMP8                 %20 ; [clobbered] ZMM temporary register
%define %%ZTMP9                 %21 ; [clobbered] ZMM temporary register
%define %%MASKREG               %22 ; [clobbered] mask temporary register

%define %%XTMP0 XWORD(%%ZTMP0)
%define %%XTMP1 XWORD(%%ZTMP1)
%define %%XTMP2 XWORD(%%ZTMP2)
%define %%XTMP3 XWORD(%%ZTMP3)
%define %%XTMP4 XWORD(%%ZTMP4)
%define %%XTMP5 XWORD(%%ZTMP5)
%define %%XTMP6 XWORD(%%ZTMP6)
%define %%XTMP7 XWORD(%%ZTMP7)
%define %%XTMP8 XWORD(%%ZTMP8)
%define %%XTMP9 XWORD(%%ZTMP9)

%define %%LENGTH        %%DATA_OFFSET
%define %%IA0           %%GPTMP1
%define %%IA1           %%GPTMP2
%define %%IA2           %%GPTMP0

        ;; if no partial block present then LENGTH/DATA_OFFSET will be set to zero
        mov             %%LENGTH, [%%GDATA_CTX + PBlockLen]
        or              %%LENGTH, %%LENGTH
        je              %%_partial_block_done           ;Leave Macro if no partial blocks

        READ_SMALL_DATA_INPUT   %%XTMP0, %%PLAIN_CIPH_IN, %%PLAIN_CIPH_LEN, %%IA0, %%IA2, %%MASKREG

        ;; XTMP1 = my_ctx_data.partial_block_enc_key
        vmovdqu64       %%XTMP1, [%%GDATA_CTX + PBlockEncKey]
        vmovdqu64       %%XTMP2, [%%GDATA_KEY + HashKey]

        ;; adjust the shuffle mask pointer to be able to shift right %%LENGTH bytes
        ;; (16 - %%LENGTH) is the number of bytes in plaintext mod 16)
        lea             %%IA0, [rel SHIFT_MASK]
        add             %%IA0, %%LENGTH
        vmovdqu64       %%XTMP3, [%%IA0]   ; shift right shuffle mask
        vpshufb         %%XTMP1, %%XTMP3

%ifidn  %%ENC_DEC, DEC
        ;;  keep copy of cipher text in %%XTMP4
        vmovdqa64       %%XTMP4, %%XTMP0
%endif
        vpxorq          %%XTMP1, %%XTMP0      ; Ciphertext XOR E(K, Yn)

        ;; Set %%IA1 to be the amount of data left in CIPH_PLAIN_IN after filling the block
        ;; Determine if partial block is not being filled and shift mask accordingly
%ifidn __OUTPUT_FORMAT__, win64
        mov             %%IA1, %%PLAIN_CIPH_LEN
        add             %%IA1, %%LENGTH
%else
        lea             %%IA1, [%%PLAIN_CIPH_LEN + %%LENGTH]
%endif
        sub             %%IA1, 16
        jge             %%_no_extra_mask
        sub             %%IA0, %%IA1
%%_no_extra_mask:
        ;; get the appropriate mask to mask out bottom %%LENGTH bytes of %%XTMP1
        ;; - mask out bottom %%LENGTH bytes of %%XTMP1
        vmovdqu64       %%XTMP0, [%%IA0 + ALL_F - SHIFT_MASK]
        vpand           %%XTMP1, %%XTMP0

%ifidn  %%ENC_DEC, DEC
        vpand           %%XTMP4, %%XTMP0
        vpshufb         %%XTMP4, [rel SHUF_MASK]
        vpshufb         %%XTMP4, %%XTMP3
        vpxorq          %%AAD_HASH, %%XTMP4
%else
        vpshufb         %%XTMP1, [rel SHUF_MASK]
        vpshufb         %%XTMP1, %%XTMP3
        vpxorq          %%AAD_HASH, %%XTMP1
%endif
        cmp             %%IA1, 0
        jl              %%_partial_incomplete

        ;; GHASH computation for the last <16 Byte block
        GHASH_MUL       %%AAD_HASH, %%XTMP2, %%XTMP5, %%XTMP6, %%XTMP7, %%XTMP8, %%XTMP9

        mov             qword [%%GDATA_CTX + PBlockLen], 0

        ;;  Set %%LENGTH to be the number of bytes to write out
        mov             %%IA0, %%LENGTH
        mov             %%LENGTH, 16
        sub             %%LENGTH, %%IA0
        jmp             %%_enc_dec_done

%%_partial_incomplete:
%ifidn __OUTPUT_FORMAT__, win64
        mov             %%IA0, %%PLAIN_CIPH_LEN
        add             [%%GDATA_CTX + PBlockLen], %%IA0
%else
        add             [%%GDATA_CTX + PBlockLen], %%PLAIN_CIPH_LEN
%endif
        mov             %%LENGTH, %%PLAIN_CIPH_LEN

%%_enc_dec_done:
        ;; output encrypted Bytes

        lea             %%IA0, [rel byte_len_to_mask_table]
        kmovw           %%MASKREG, [%%IA0 + %%LENGTH*2]
        vmovdqu64       [%%GDATA_CTX + AadHash], %%AAD_HASH

%ifidn  %%ENC_DEC, ENC
        ;; shuffle XTMP1 back to output as ciphertext
        vpshufb         %%XTMP1, [rel SHUF_MASK]
        vpshufb         %%XTMP1, %%XTMP3
%endif
        vmovdqu8        [%%CIPH_PLAIN_OUT]{%%MASKREG}, %%XTMP1
%%_partial_block_done:
%endmacro ; PARTIAL_BLOCK

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Ciphers 1 to 16 blocks and prepares them for later GHASH compute operation
%macro INITIAL_BLOCKS_PARTIAL_CIPHER 25
%define %%GDATA_KEY             %1  ; [in] key pointer
%define %%GDATA_CTX             %2  ; [in] context pointer
%define %%CIPH_PLAIN_OUT        %3  ; [in] text output pointer
%define %%PLAIN_CIPH_IN         %4  ; [in] text input pointer
%define %%LENGTH                %5  ; [in/clobbered] length in bytes
%define %%DATA_OFFSET           %6  ; [in/out] current data offset (updated)
%define %%NUM_BLOCKS            %7  ; [in] can only be 1, 2, 3, 4, 5, ..., 15 or 16 (not 0)
%define %%CTR                   %8  ; [in/out] current counter value
%define %%ENC_DEC               %9  ; [in] cipher direction (ENC/DEC)
%define %%INSTANCE_TYPE         %10 ; [in] multi_call or single_call
%define %%DAT0                  %11 ; [out] ZMM with cipher text shuffled for GHASH
%define %%DAT1                  %12 ; [out] ZMM with cipher text shuffled for GHASH
%define %%DAT2                  %13 ; [out] ZMM with cipher text shuffled for GHASH
%define %%DAT3                  %14 ; [out] ZMM with cipher text shuffled for GHASH
%define %%LAST_CIPHER_BLK       %15 ; [out] XMM to put ciphered counter block partially xor'ed with text
%define %%LAST_GHASH_BLK        %16 ; [out] XMM to put last cipher text block shuffled for GHASH
%define %%CTR0                  %17 ; [clobbered] ZMM temporary
%define %%CTR1                  %18 ; [clobbered] ZMM temporary
%define %%CTR2                  %19 ; [clobbered] ZMM temporary
%define %%CTR3                  %20 ; [clobbered] ZMM temporary
%define %%ZT1                   %21 ; [clobbered] ZMM temporary
%define %%IA0                   %22 ; [clobbered] GP temporary
%define %%IA1                   %23 ; [clobbered] GP temporary
%define %%MASKREG               %24 ; [clobbered] mask register
%define %%SHUFMASK              %25 ; [out] ZMM loaded with BE/LE shuffle mask

%if %%NUM_BLOCKS == 1
        vmovdqa64       XWORD(%%SHUFMASK), [rel SHUF_MASK]
%elif %%NUM_BLOCKS == 2
        vmovdqa64       YWORD(%%SHUFMASK), [rel SHUF_MASK]
%else
        vmovdqa64       %%SHUFMASK, [rel SHUF_MASK]
%endif
        ;; prepare AES counter blocks
%if %%NUM_BLOCKS == 1
        vpaddd          XWORD(%%CTR0), %%CTR, [rel ONE]
%elif %%NUM_BLOCKS == 2
        vshufi64x2      YWORD(%%CTR0), YWORD(%%CTR), YWORD(%%CTR), 0
        vpaddd          YWORD(%%CTR0), YWORD(%%CTR0), [rel ddq_add_1234]
%else
        vshufi64x2      ZWORD(%%CTR), ZWORD(%%CTR), ZWORD(%%CTR), 0
        vpaddd          %%CTR0, ZWORD(%%CTR), [rel ddq_add_1234]
%if %%NUM_BLOCKS > 4
        vpaddd          %%CTR1, ZWORD(%%CTR), [rel ddq_add_5678]
%endif
%if %%NUM_BLOCKS > 8
        vpaddd          %%CTR2, %%CTR0, [rel ddq_add_8888]
%endif
%if %%NUM_BLOCKS > 12
        vpaddd          %%CTR3, %%CTR1, [rel ddq_add_8888]
%endif
%endif

        ;; get load/store mask
        lea             %%IA0, [rel byte64_len_to_mask_table]
        mov             %%IA1, %%LENGTH
%if %%NUM_BLOCKS > 12
        sub             %%IA1, 3 * 64
%elif %%NUM_BLOCKS > 8
        sub             %%IA1, 2 * 64
%elif %%NUM_BLOCKS > 4
        sub             %%IA1, 64
%endif
        kmovq           %%MASKREG, [%%IA0 + %%IA1*8]

        ;; extract new counter value
        ;; shuffle the counters for AES rounds
%ifidn %%INSTANCE_TYPE, multi_call
%if %%NUM_BLOCKS <= 4
        vextracti32x4   %%CTR, %%CTR0, (%%NUM_BLOCKS - 1)
%elif %%NUM_BLOCKS <= 8
        vextracti32x4   %%CTR, %%CTR1, (%%NUM_BLOCKS - 5)
%elif %%NUM_BLOCKS <= 12
        vextracti32x4   %%CTR, %%CTR2, (%%NUM_BLOCKS - 9)
%else
        vextracti32x4   %%CTR, %%CTR3, (%%NUM_BLOCKS - 13)
%endif
%endif
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vpshufb, \
                        %%CTR0, %%CTR1, %%CTR2, %%CTR3, \
                        %%CTR0, %%CTR1, %%CTR2, %%CTR3, \
                        %%SHUFMASK, %%SHUFMASK, %%SHUFMASK, %%SHUFMASK

        ;; load plain/cipher text
       ZMM_LOAD_MASKED_BLOCKS_0_16 %%NUM_BLOCKS, %%PLAIN_CIPH_IN, %%DATA_OFFSET, \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, %%MASKREG

        ;; AES rounds and XOR with plain/cipher text
%assign j 0
%rep (NROUNDS + 2)
        vbroadcastf64x2 %%ZT1, [%%GDATA_KEY + (j * 16)]
        ZMM_AESENC_ROUND_BLOCKS_0_16 %%CTR0, %%CTR1, %%CTR2, %%CTR3, \
                        %%ZT1, j, \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, \
                        %%NUM_BLOCKS, NROUNDS
%assign j (j + 1)
%endrep

%ifidn %%INSTANCE_TYPE, multi_call
        ;; retrieve the last cipher counter block (partially XOR'ed with text)
        ;; - this is needed for partial block cases
%if %%NUM_BLOCKS <= 4
        vextracti32x4   %%LAST_CIPHER_BLK, %%CTR0, (%%NUM_BLOCKS - 1)
%elif %%NUM_BLOCKS <= 8
        vextracti32x4   %%LAST_CIPHER_BLK, %%CTR1, (%%NUM_BLOCKS - 5)
%elif %%NUM_BLOCKS <= 12
        vextracti32x4   %%LAST_CIPHER_BLK, %%CTR2, (%%NUM_BLOCKS - 9)
%else
        vextracti32x4   %%LAST_CIPHER_BLK, %%CTR3, (%%NUM_BLOCKS - 13)
%endif
%endif
        ;; write cipher/plain text back to output and
        ZMM_STORE_MASKED_BLOCKS_0_16 %%NUM_BLOCKS, %%CIPH_PLAIN_OUT, %%DATA_OFFSET, \
                        %%CTR0, %%CTR1, %%CTR2, %%CTR3, %%MASKREG

        ;; zero bytes outside the mask before hashing
%if %%NUM_BLOCKS <= 4
        vmovdqu8        %%CTR0{%%MASKREG}{z}, %%CTR0
%elif %%NUM_BLOCKS <= 8
        vmovdqu8        %%CTR1{%%MASKREG}{z}, %%CTR1
%elif %%NUM_BLOCKS <= 12
        vmovdqu8        %%CTR2{%%MASKREG}{z}, %%CTR2
%else
        vmovdqu8        %%CTR3{%%MASKREG}{z}, %%CTR3
%endif

        ;; Shuffle the cipher text blocks for hashing part
        ;; ZT5 and ZT6 are expected outputs with blocks for hashing
%ifidn  %%ENC_DEC, DEC
        ;; Decrypt case
        ;; - cipher blocks are in ZT5 & ZT6
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vpshufb, \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, \
                        %%SHUFMASK, %%SHUFMASK, %%SHUFMASK, %%SHUFMASK
%else
        ;; Encrypt case
        ;; - cipher blocks are in CTR0-CTR3
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vpshufb, \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, \
                        %%CTR0, %%CTR1, %%CTR2, %%CTR3, \
                        %%SHUFMASK, %%SHUFMASK, %%SHUFMASK, %%SHUFMASK
%endif                          ; Encrypt

%ifidn %%INSTANCE_TYPE, multi_call
        ;; Extract the last block for partials and multi_call cases
%if %%NUM_BLOCKS <= 4
        vextracti32x4   %%LAST_GHASH_BLK, %%DAT0, %%NUM_BLOCKS - 1
%elif %%NUM_BLOCKS <= 8
        vextracti32x4   %%LAST_GHASH_BLK, %%DAT1, %%NUM_BLOCKS - 5
%elif %%NUM_BLOCKS <= 12
        vextracti32x4   %%LAST_GHASH_BLK, %%DAT2, %%NUM_BLOCKS - 9
%else
        vextracti32x4   %%LAST_GHASH_BLK, %%DAT3, %%NUM_BLOCKS - 13
%endif
%endif

%endmacro                       ; INITIAL_BLOCKS_PARTIAL_CIPHER

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Computes GHASH on 1 to 16 blocks
%macro INITIAL_BLOCKS_PARTIAL_GHASH 22-25
%define %%GDATA_KEY             %1  ; [in] key pointer
%define %%GDATA_CTX             %2  ; [in] context pointer
%define %%LENGTH                %3  ; [in/clobbered] length in bytes
%define %%NUM_BLOCKS            %4  ; [in] can only be 1, 2, 3, 4, 5, ..., 15 or 16 (not 0)
%define %%HASH_IN_OUT           %5  ; [in/out] XMM ghash in/out value
%define %%ENC_DEC               %6  ; [in] cipher direction (ENC/DEC)
%define %%INSTANCE_TYPE         %7  ; [in] multi_call or single_call
%define %%DAT0                  %8  ; [in] ZMM with cipher text shuffled for GHASH
%define %%DAT1                  %9  ; [in] ZMM with cipher text shuffled for GHASH
%define %%DAT2                  %10 ; [in] ZMM with cipher text shuffled for GHASH
%define %%DAT3                  %11 ; [in] ZMM with cipher text shuffled for GHASH
%define %%LAST_CIPHER_BLK       %12 ; [in] XMM with ciphered counter block partially xor'ed with text
%define %%LAST_GHASH_BLK        %13 ; [in] XMM with last cipher text block shuffled for GHASH
%define %%ZT0                   %14 ; [clobbered] ZMM temporary
%define %%ZT1                   %15 ; [clobbered] ZMM temporary
%define %%ZT2                   %16 ; [clobbered] ZMM temporary
%define %%ZT3                   %17 ; [clobbered] ZMM temporary
%define %%ZT4                   %18 ; [clobbered] ZMM temporary
%define %%ZT5                   %19 ; [clobbered] ZMM temporary
%define %%ZT6                   %20 ; [clobbered] ZMM temporary
%define %%ZT7                   %21 ; [clobbered] ZMM temporary
%define %%ZT8                   %22 ; [clobbered] ZMM temporary
%define %%GH                    %23 ; [in] ZMM with hi product part
%define %%GM                    %24 ; [in] ZMM with mid prodcut part
%define %%GL                    %25 ; [in] ZMM with lo product part

%ifidn %%INSTANCE_TYPE, single_call
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;;; SINGLE CALL case
        ;;; - hash all data including partial block
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%if %0 == 22
        ;; start GHASH compute
        GHASH_1_TO_16 %%GDATA_KEY, %%HASH_IN_OUT, \
                        %%ZT0, %%ZT1, %%ZT2, %%ZT3, %%ZT4, \
                        %%ZT5, %%ZT6, %%ZT7, %%ZT8, ZWORD(%%HASH_IN_OUT), \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, %%NUM_BLOCKS
%elif %0 == 25
        ;; continue GHASH compute
        GHASH_1_TO_16 %%GDATA_KEY, %%HASH_IN_OUT, \
                        %%ZT0, %%ZT1, %%ZT2, %%ZT3, %%ZT4, \
                        %%ZT5, %%ZT6, %%ZT7, %%ZT8, ZWORD(%%HASH_IN_OUT), \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, %%NUM_BLOCKS, %%GH, %%GM, %%GL
%endif

%else
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;;; MULTI CALL (SGL) case
        ;;; - hash all but the last partial block of data
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        ;; update data offset
%if %%NUM_BLOCKS > 1
        ;; The final block of data may be <16B
        sub     %%LENGTH, 16 * (%%NUM_BLOCKS - 1)
%endif

%if %%NUM_BLOCKS < 16
        ;; NOTE: the 'jl' is always taken for num_initial_blocks = 16.
        ;;      This is run in the context of GCM_ENC_DEC_SMALL for length < 256.
        cmp     %%LENGTH, 16
        jl      %%_small_initial_partial_block

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;;; Handle a full length final block - encrypt and hash all blocks
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        sub     %%LENGTH, 16
        mov     qword [%%GDATA_CTX + PBlockLen], 0

        ;; Hash all of the data
%if %0 == 22
        ;; start GHASH compute
        GHASH_1_TO_16 %%GDATA_KEY, %%HASH_IN_OUT, \
                        %%ZT0, %%ZT1, %%ZT2, %%ZT3, %%ZT4, \
                        %%ZT5, %%ZT6, %%ZT7, %%ZT8, ZWORD(%%HASH_IN_OUT), \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, %%NUM_BLOCKS
%elif %0 == 25
        ;; continue GHASH compute
        GHASH_1_TO_16 %%GDATA_KEY, %%HASH_IN_OUT, \
                        %%ZT0, %%ZT1, %%ZT2, %%ZT3, %%ZT4, \
                        %%ZT5, %%ZT6, %%ZT7, %%ZT8, ZWORD(%%HASH_IN_OUT), \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, %%NUM_BLOCKS, %%GH, %%GM, %%GL
%endif
        jmp             %%_small_initial_compute_done
%endif                          ; %if %%NUM_BLOCKS < 16

%%_small_initial_partial_block:

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;;; Handle ghash for a <16B final block
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        ;; In this case if it's a single call to encrypt we can
        ;; hash all of the data but if it's an init / update / finalize
        ;; series of call we need to leave the last block if it's
        ;; less than a full block of data.

        mov             [%%GDATA_CTX + PBlockLen], %%LENGTH
        vmovdqu64       [%%GDATA_CTX + PBlockEncKey], %%LAST_CIPHER_BLK

%assign k (%%NUM_BLOCKS - 1)
%assign last_block_to_hash 1

%if (%%NUM_BLOCKS > last_block_to_hash)

        ;; ZT12-ZT20 - temporary registers
%if %0 == 22
        ;; start GHASH compute
        GHASH_1_TO_16 %%GDATA_KEY, %%HASH_IN_OUT, \
                        %%ZT0, %%ZT1, %%ZT2, %%ZT3, %%ZT4, \
                        %%ZT5, %%ZT6, %%ZT7, %%ZT8, ZWORD(%%HASH_IN_OUT), \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, k
%elif %0 == 25
        ;; continue GHASH compute
        GHASH_1_TO_16 %%GDATA_KEY, %%HASH_IN_OUT, \
                        %%ZT0, %%ZT1, %%ZT2, %%ZT3, %%ZT4, \
                        %%ZT5, %%ZT6, %%ZT7, %%ZT8, ZWORD(%%HASH_IN_OUT), \
                        %%DAT0, %%DAT1, %%DAT2, %%DAT3, k, %%GH, %%GM, %%GL
%endif
        ;; just fall through no jmp needed
%else

%if %0 == 25
        ;; Reduction is required in this case.
        ;; Integrate GM into GH and GL.
        vpsrldq         %%ZT0, %%GM, 8
        vpslldq         %%ZT1, %%GM, 8
        vpxorq          %%GH, %%GH, %%ZT0
        vpxorq          %%GL, %%GL, %%ZT1

        ;; Add GH and GL 128-bit words horizontally
        VHPXORI4x128    %%GH, %%ZT0
        VHPXORI4x128    %%GL, %%ZT1

        ;; 256-bit to 128-bit reduction
        vmovdqa64       XWORD(%%ZT0), [rel POLY2]
        VCLMUL_REDUCE   XWORD(%%HASH_IN_OUT), XWORD(%%ZT0), \
                        XWORD(%%GH), XWORD(%%GL), XWORD(%%ZT1), XWORD(%%ZT2)
%endif
        ;; Record that a reduction is not needed -
        ;; In this case no hashes are computed because there
        ;; is only one initial block and it is < 16B in length.
        ;; We only need to check if a reduction is needed if
        ;; initial_blocks == 1 and init/update/final is being used.
        ;; In this case we may just have a partial block, and that
        ;; gets hashed in finalize.

        ;; The hash should end up in HASH_IN_OUT.
        ;; The only way we should get here is if there is
        ;; a partial block of data, so xor that into the hash.
        vpxorq          %%HASH_IN_OUT, %%HASH_IN_OUT, %%LAST_GHASH_BLK
        ;; The result is in %%HASH_IN_OUT
        jmp             %%_after_reduction
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; After GHASH reduction
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%%_small_initial_compute_done:
        ;; If using init/update/finalize, we need to xor any partial block data
        ;; into the hash.
%if %%NUM_BLOCKS > 1
        ;; NOTE: for %%NUM_BLOCKS = 0 the xor never takes place
%if %%NUM_BLOCKS != 16
        ;; NOTE: for %%NUM_BLOCKS = 16, %%LENGTH, stored in [PBlockLen] is never zero
        or              %%LENGTH, %%LENGTH
        je              %%_after_reduction
%endif                          ; %%NUM_BLOCKS != 16
        vpxorq          %%HASH_IN_OUT, %%HASH_IN_OUT, %%LAST_GHASH_BLK
%endif                          ; %%NUM_BLOCKS > 1

%%_after_reduction:

%endif                          ; %%INSTANCE_TYPE, multi_call

        ;; Final hash is now in HASH_IN_OUT

%endmacro                       ; INITIAL_BLOCKS_PARTIAL_GHASH

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; INITIAL_BLOCKS_PARTIAL macro with support for a partial final block.
;;; It may look similar to INITIAL_BLOCKS but its usage is different:
;;; - first encrypts/decrypts required number of blocks and then
;;;   ghashes these blocks
;;; - Small packets or left over data chunks (<256 bytes)
;;;     - single or multi call
;;; - Remaining data chunks below 256 bytes (multi buffer code)
;;;
;;; num_initial_blocks is expected to include the partial final block
;;; in the count.
%macro INITIAL_BLOCKS_PARTIAL 30
%define %%GDATA_KEY             %1  ; [in] key pointer
%define %%GDATA_CTX             %2  ; [in] context pointer
%define %%CIPH_PLAIN_OUT        %3  ; [in] text output pointer
%define %%PLAIN_CIPH_IN         %4  ; [in] text input pointer
%define %%LENGTH                %5  ; [in/clobbered] length in bytes
%define %%DATA_OFFSET           %6  ; [in/out] current data offset (updated)
%define %%NUM_BLOCKS            %7  ; [in] can only be 1, 2, 3, 4, 5, ..., 15 or 16 (not 0)
%define %%CTR                   %8  ; [in/out] current counter value
%define %%HASH_IN_OUT           %9  ; [in/out] XMM ghash in/out value
%define %%ENC_DEC               %10 ; [in] cipher direction (ENC/DEC)
%define %%INSTANCE_TYPE         %11 ; [in] multi_call or single_call
%define %%CTR0                  %12 ; [clobbered] ZMM temporary
%define %%CTR1                  %13 ; [clobbered] ZMM temporary
%define %%CTR2                  %14 ; [clobbered] ZMM temporary
%define %%CTR3                  %15 ; [clobbered] ZMM temporary
%define %%DAT0                  %16 ; [clobbered] ZMM temporary
%define %%DAT1                  %17 ; [clobbered] ZMM temporary
%define %%DAT2                  %18 ; [clobbered] ZMM temporary
%define %%DAT3                  %19 ; [clobbered] ZMM temporary
%define %%LAST_CIPHER_BLK       %20 ; [clobbered] ZMM temporary
%define %%LAST_GHASH_BLK        %21 ; [clobbered] ZMM temporary
%define %%ZT0                   %22 ; [clobbered] ZMM temporary
%define %%ZT1                   %23 ; [clobbered] ZMM temporary
%define %%ZT2                   %24 ; [clobbered] ZMM temporary
%define %%ZT3                   %25 ; [clobbered] ZMM temporary
%define %%ZT4                   %26 ; [clobbered] ZMM temporary
%define %%IA0                   %27 ; [clobbered] GP temporary
%define %%IA1                   %28 ; [clobbered] GP temporary
%define %%MASKREG               %29 ; [clobbered] mask register
%define %%SHUFMASK              %30 ; [clobbered] ZMM for BE/LE shuffle mask

        INITIAL_BLOCKS_PARTIAL_CIPHER \
                        %%GDATA_KEY, %%GDATA_CTX, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, \
                        %%LENGTH, %%DATA_OFFSET, %%NUM_BLOCKS, %%CTR, \
                        %%ENC_DEC, %%INSTANCE_TYPE, %%DAT0, %%DAT1, %%DAT2, %%DAT3, \
                        XWORD(%%LAST_CIPHER_BLK), XWORD(%%LAST_GHASH_BLK), \
                        %%CTR0, %%CTR1, %%CTR2, %%CTR3, %%ZT0, \
                        %%IA0, %%IA1, %%MASKREG, %%SHUFMASK

        INITIAL_BLOCKS_PARTIAL_GHASH \
                        %%GDATA_KEY, %%GDATA_CTX, %%LENGTH, \
                        %%NUM_BLOCKS, %%HASH_IN_OUT, %%ENC_DEC, \
                        %%INSTANCE_TYPE, %%DAT0, %%DAT1, %%DAT2, %%DAT3, \
                        XWORD(%%LAST_CIPHER_BLK), XWORD(%%LAST_GHASH_BLK), \
                        %%CTR0, %%CTR1, %%CTR2, %%CTR3, %%ZT0, %%ZT1, \
                        %%ZT2, %%ZT3, %%ZT4

%endmacro                       ; INITIAL_BLOCKS_PARTIAL

;;; ===========================================================================
;;; ===========================================================================
;;; Stitched GHASH of 16 blocks (with reduction) with encryption of N blocks
;;; followed with GHASH of the N blocks.
%macro GHASH_16_ENCRYPT_N_GHASH_N 47
%define %%GDATA                 %1  ; [in] key pointer
%define %%GCTX                  %2  ; [in] context pointer
%define %%CIPH_PLAIN_OUT        %3  ; [in] pointer to output buffer
%define %%PLAIN_CIPH_IN         %4  ; [in] pointer to input buffer
%define %%DATA_OFFSET           %5  ; [in] data offset
%define %%LENGTH                %6  ; [in] data length
%define %%CTR_BE                %7  ; [in/out] ZMM counter blocks (last 4) in big-endian
%define %%CTR_CHECK             %8  ; [in/out] GP with 8-bit counter for overflow check
%define %%HASHKEY_OFFSET        %9  ; [in] numerical offset for the highest hash key
%define %%GHASHIN_BLK_OFFSET    %10 ; [in] numerical offset for GHASH blocks in
%define %%SHFMSK                %11 ; [in] ZMM with byte swap mask for pshufb
%define %%B00_03                %12 ; [clobbered] temporary ZMM
%define %%B04_07                %13 ; [clobbered] temporary ZMM
%define %%B08_11                %14 ; [clobbered] temporary ZMM
%define %%B12_15                %15 ; [clobbered] temporary ZMM
%define %%GH1H_UNUSED           %16 ; [clobbered] temporary ZMM
%define %%GH1L                  %17 ; [clobbered] temporary ZMM
%define %%GH1M                  %18 ; [clobbered] temporary ZMM
%define %%GH1T                  %19 ; [clobbered] temporary ZMM
%define %%GH2H                  %20 ; [clobbered] temporary ZMM
%define %%GH2L                  %21 ; [clobbered] temporary ZMM
%define %%GH2M                  %22 ; [clobbered] temporary ZMM
%define %%GH2T                  %23 ; [clobbered] temporary ZMM
%define %%GH3H                  %24 ; [clobbered] temporary ZMM
%define %%GH3L                  %25 ; [clobbered] temporary ZMM
%define %%GH3M                  %26 ; [clobbered] temporary ZMM
%define %%GH3T                  %27 ; [clobbered] temporary ZMM
%define %%AESKEY1               %28 ; [clobbered] temporary ZMM
%define %%AESKEY2               %29 ; [clobbered] temporary ZMM
%define %%GHKEY1                %30 ; [clobbered] temporary ZMM
%define %%GHKEY2                %31 ; [clobbered] temporary ZMM
%define %%GHDAT1                %32 ; [clobbered] temporary ZMM
%define %%GHDAT2                %33 ; [clobbered] temporary ZMM
%define %%ZT01                  %34 ; [clobbered] temporary ZMM
%define %%ADDBE_4x4             %35 ; [in] ZMM with 4x128bits 4 in big-endian
%define %%ADDBE_1234            %36 ; [in] ZMM with 4x128bits 1, 2, 3 and 4 in big-endian
%define %%GHASH_TYPE            %37 ; [in] "start", "start_reduce", "mid", "end_reduce"
%define %%TO_REDUCE_L           %38 ; [in] ZMM for low 4x128-bit GHASH sum
%define %%TO_REDUCE_H           %39 ; [in] ZMM for hi 4x128-bit GHASH sum
%define %%TO_REDUCE_M           %40 ; [in] ZMM for medium 4x128-bit GHASH sum
%define %%ENC_DEC               %41 ; [in] cipher direction
%define %%HASH_IN_OUT           %42 ; [in/out] XMM ghash in/out value
%define %%IA0                   %43 ; [clobbered] GP temporary
%define %%IA1                   %44 ; [clobbered] GP temporary
%define %%MASKREG               %45 ; [clobbered] mask register
%define %%NUM_BLOCKS            %46 ; [in] numerical value with number of blocks to be encrypted/ghashed (1 to 16)
%define %%INSTANCE_TYPE         %47 ; [in] multi_call or single_call

%xdefine %%GH1H %%HASH_IN_OUT   ; this is to avoid additional move in do_reduction case

%define %%LAST_GHASH_BLK  %%GH1L
%define %%LAST_CIPHER_BLK %%GH1T

%define %%RED_POLY %%GH2T
%define %%RED_P1   %%GH2L
%define %%RED_T1   %%GH2H
%define %%RED_T2   %%GH2M

%define %%DATA1 %%GH3H
%define %%DATA2 %%GH3L
%define %%DATA3 %%GH3M
%define %%DATA4 %%GH3T

;; do reduction after the 16 blocks ?
%assign do_reduction 0

;; is 16 block chunk a start?
%assign is_start     0

%ifidn %%GHASH_TYPE, start_reduce
%assign is_start     1
%assign do_reduction 1
%endif

%ifidn %%GHASH_TYPE, start
%assign is_start    1
%endif

%ifidn %%GHASH_TYPE, end_reduce
%assign do_reduction 1
%endif

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; - get load/store mask
        ;; - load plain/cipher text
        ;; get load/store mask
        lea             %%IA0, [rel byte64_len_to_mask_table]
        mov             %%IA1, %%LENGTH
%if %%NUM_BLOCKS > 12
        sub             %%IA1, 3 * 64
%elif %%NUM_BLOCKS > 8
        sub             %%IA1, 2 * 64
%elif %%NUM_BLOCKS > 4
        sub             %%IA1, 64
%endif
        kmovq           %%MASKREG, [%%IA0 + %%IA1*8]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; prepare counter blocks

        cmp             DWORD(%%CTR_CHECK), (256 - %%NUM_BLOCKS)
        jae             %%_16_blocks_overflow

        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vpaddd, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%CTR_BE, %%B00_03, %%B04_07, %%B08_11, \
                        %%ADDBE_1234, %%ADDBE_4x4, %%ADDBE_4x4, %%ADDBE_4x4
        jmp             %%_16_blocks_ok

%%_16_blocks_overflow:
        vpshufb         %%CTR_BE, %%CTR_BE, %%SHFMSK
        vpaddd          %%B00_03, %%CTR_BE, [rel ddq_add_1234]
%if %%NUM_BLOCKS > 4
        vmovdqa64       %%B12_15, [rel ddq_add_4444]
        vpaddd          %%B04_07, %%B00_03, %%B12_15
%endif
%if %%NUM_BLOCKS > 8
        vpaddd          %%B08_11, %%B04_07, %%B12_15
%endif
%if %%NUM_BLOCKS > 12
        vpaddd          %%B12_15, %%B08_11, %%B12_15
%endif
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vpshufb, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%SHFMSK, %%SHFMSK, %%SHFMSK, %%SHFMSK
%%_16_blocks_ok:

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; - pre-load constants
        ;; - add current hash into the 1st block
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 0)]
%if is_start != 0
        vpxorq          %%GHDAT1, %%HASH_IN_OUT, [rsp + %%GHASHIN_BLK_OFFSET + (0*64)]
%else
        vmovdqa64       %%GHDAT1, [rsp + %%GHASHIN_BLK_OFFSET + (0*64)]
%endif
        vmovdqu64       %%GHKEY1, [%%GDATA + %%HASHKEY_OFFSET + (0*64)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; save counter for the next round
        ;; increment counter overflow check register
%ifidn %%INSTANCE_TYPE, multi_call
%if %%NUM_BLOCKS <= 4
        vextracti32x4   XWORD(%%CTR_BE), %%B00_03, (%%NUM_BLOCKS - 1)
%elif %%NUM_BLOCKS <= 8
        vextracti32x4   XWORD(%%CTR_BE), %%B04_07, (%%NUM_BLOCKS - 5)
%elif %%NUM_BLOCKS <= 12
        vextracti32x4   XWORD(%%CTR_BE), %%B08_11, (%%NUM_BLOCKS - 9)
%else
        vextracti32x4   XWORD(%%CTR_BE), %%B12_15, (%%NUM_BLOCKS - 13)
%endif
        vshufi64x2      %%CTR_BE, %%CTR_BE, %%CTR_BE, 0000_0000b
%endif

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; pre-load constants
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 1)]
        vmovdqu64       %%GHKEY2, [%%GDATA + %%HASHKEY_OFFSET + (1*64)]
        vmovdqa64       %%GHDAT2, [rsp + %%GHASHIN_BLK_OFFSET + (1*64)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; stitch AES rounds with GHASH

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 0 - ARK

        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vpxorq, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY1, %%AESKEY1, %%AESKEY1, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 2)]

        ;;==================================================
        ;; GHASH 4 blocks (15 to 12)
        vpclmulqdq      %%GH1H, %%GHDAT1, %%GHKEY1, 0x11     ; a1*b1
        vpclmulqdq      %%GH1L, %%GHDAT1, %%GHKEY1, 0x00     ; a0*b0
        vpclmulqdq      %%GH1M, %%GHDAT1, %%GHKEY1, 0x01     ; a1*b0
        vpclmulqdq      %%GH1T, %%GHDAT1, %%GHKEY1, 0x10     ; a0*b1

        vmovdqu64       %%GHKEY1, [%%GDATA + %%HASHKEY_OFFSET + (2*64)]
        vmovdqa64       %%GHDAT1, [rsp + %%GHASHIN_BLK_OFFSET + (2*64)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 1
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY2, %%AESKEY2, %%AESKEY2, %%AESKEY2
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 3)]

        ;; =================================================
        ;; GHASH 4 blocks (11 to 8)
        vpclmulqdq      %%GH2M, %%GHDAT2, %%GHKEY2, 0x10     ; a0*b1
        vpclmulqdq      %%GH2T, %%GHDAT2, %%GHKEY2, 0x01     ; a1*b0
        vpclmulqdq      %%GH2H, %%GHDAT2, %%GHKEY2, 0x11     ; a1*b1
        vpclmulqdq      %%GH2L, %%GHDAT2, %%GHKEY2, 0x00     ; a0*b0

        vmovdqu64       %%GHKEY2, [%%GDATA + %%HASHKEY_OFFSET + (3*64)]
        vmovdqa64       %%GHDAT2, [rsp + %%GHASHIN_BLK_OFFSET + (3*64)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 2
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY1, %%AESKEY1, %%AESKEY1, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 4)]

        ;; =================================================
        ;; GHASH 4 blocks (7 to 4)
        vpclmulqdq      %%GH3M, %%GHDAT1, %%GHKEY1, 0x10     ; a0*b1
        vpclmulqdq      %%GH3T, %%GHDAT1, %%GHKEY1, 0x01     ; a1*b0
        vpclmulqdq      %%GH3H, %%GHDAT1, %%GHKEY1, 0x11     ; a1*b1
        vpclmulqdq      %%GH3L, %%GHDAT1, %%GHKEY1, 0x00     ; a0*b0

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES rounds 3
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY2, %%AESKEY2, %%AESKEY2, %%AESKEY2
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 5)]

        ;; =================================================
        ;; Gather (XOR) GHASH for 12 blocks
        vpternlogq      %%GH1H, %%GH2H, %%GH3H, 0x96
        vpternlogq      %%GH1L, %%GH2L, %%GH3L, 0x96
        vpternlogq      %%GH1T, %%GH2T, %%GH3T, 0x96
        vpternlogq      %%GH1M, %%GH2M, %%GH3M, 0x96

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES rounds 4
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY1, %%AESKEY1, %%AESKEY1, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 6)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; load plain/cipher text
        ZMM_LOAD_MASKED_BLOCKS_0_16 %%NUM_BLOCKS, %%PLAIN_CIPH_IN, %%DATA_OFFSET, \
                        %%DATA1, %%DATA2, %%DATA3, %%DATA4, %%MASKREG

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES rounds 5
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY2, %%AESKEY2, %%AESKEY2, %%AESKEY2
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 7)]

        ;; =================================================
        ;; GHASH 4 blocks (3 to 0)
        vpclmulqdq      %%GH2M, %%GHDAT2, %%GHKEY2, 0x10     ; a0*b1
        vpclmulqdq      %%GH2T, %%GHDAT2, %%GHKEY2, 0x01     ; a1*b0
        vpclmulqdq      %%GH2H, %%GHDAT2, %%GHKEY2, 0x11     ; a1*b1
        vpclmulqdq      %%GH2L, %%GHDAT2, %%GHKEY2, 0x00     ; a0*b0

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 6
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY1, %%AESKEY1, %%AESKEY1, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 8)]

        ;; =================================================
        ;; gather GHASH in GH1L (low), GH1H (high), GH1M (mid)
        ;; - add GH2[MTLH] to GH1[MTLH]
        vpternlogq      %%GH1M, %%GH1T, %%GH2T, 0x96
%if do_reduction != 0

%if is_start != 0
        vpxorq          %%GH1M, %%GH1M, %%GH2M
%else
        vpternlogq      %%GH1H, %%TO_REDUCE_H, %%GH2H, 0x96
        vpternlogq      %%GH1L, %%TO_REDUCE_L, %%GH2L, 0x96
        vpternlogq      %%GH1M, %%TO_REDUCE_M, %%GH2M, 0x96
%endif

%else
        ;; Update H/M/L hash sums if not carrying reduction
%if is_start != 0
        vpxorq          %%TO_REDUCE_H, %%GH1H, %%GH2H
        vpxorq          %%TO_REDUCE_L, %%GH1L, %%GH2L
        vpxorq          %%TO_REDUCE_M, %%GH1M, %%GH2M
%else
        vpternlogq      %%TO_REDUCE_H, %%GH1H, %%GH2H, 0x96
        vpternlogq      %%TO_REDUCE_L, %%GH1L, %%GH2L, 0x96
        vpternlogq      %%TO_REDUCE_M, %%GH1M, %%GH2M, 0x96
%endif

%endif

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 7
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY2, %%AESKEY2, %%AESKEY2, %%AESKEY2
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 9)]

        ;; =================================================
        ;; prepare mid sum for adding to high & low
        ;; load polynomial constant for reduction
%if do_reduction != 0
        vpsrldq         %%GH2M, %%GH1M, 8
        vpslldq         %%GH1M, %%GH1M, 8

        vmovdqa64       XWORD(%%RED_POLY), [rel POLY2]
%endif
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 8
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY1, %%AESKEY1, %%AESKEY1, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 10)]

        ;; =================================================
        ;; Add mid product to high and low
%if do_reduction != 0
%if is_start != 0
        vpternlogq      %%GH1H, %%GH2H, %%GH2M, 0x96    ; TH = TH1 + TH2 + TM>>64
        vpternlogq      %%GH1L, %%GH2L, %%GH1M, 0x96    ; TL = TL1 + TL2 + TM<<64
%else
        vpxorq          %%GH1H, %%GH1H, %%GH2M          ; TH = TH1 + TM>>64
        vpxorq          %%GH1L, %%GH1L, %%GH1M          ; TL = TL1 + TM<<64
%endif
%endif
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 9
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY2, %%AESKEY2, %%AESKEY2, %%AESKEY2

        ;; =================================================
        ;; horizontal xor of low and high 4x128
%if do_reduction != 0
        VHPXORI4x128    %%GH1H, %%GH2H
        VHPXORI4x128    %%GH1L, %%GH2L
%endif

%if (NROUNDS >= 11)
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 11)]
%endif
        ;; =================================================
        ;; first phase of reduction
%if do_reduction != 0
        vpclmulqdq      XWORD(%%RED_P1), XWORD(%%RED_POLY), XWORD(%%GH1L), 0x01
        vpslldq         XWORD(%%RED_P1), XWORD(%%RED_P1), 8             ; shift-L 2 DWs
        vpxorq          XWORD(%%RED_P1), XWORD(%%GH1L), XWORD(%%RED_P1) ; first phase of the reduct
%endif
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES rounds up to 11 (AES192) or 13 (AES256)
        ;; AES128 is done
%if (NROUNDS >= 11)
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY1, %%AESKEY1, %%AESKEY1, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 12)]

        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY2, %%AESKEY2, %%AESKEY2, %%AESKEY2
%if (NROUNDS == 13)
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 13)]

        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY1, %%AESKEY1, %%AESKEY1, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 14)]

        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenc, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY2, %%AESKEY2, %%AESKEY2, %%AESKEY2
%endif ; GCM256 / NROUNDS = 13 (15 including the first and the last)
%endif ; GCM192 / NROUNDS = 11 (13 including the first and the last)

        ;; =================================================
        ;; second phase of the reduction
%if do_reduction != 0
        vpclmulqdq      XWORD(%%RED_T1), XWORD(%%RED_POLY), XWORD(%%RED_P1), 0x00
        vpsrldq         XWORD(%%RED_T1), XWORD(%%RED_T1), 4 ; shift-R 1-DW to obtain 2-DWs shift-R

        vpclmulqdq      XWORD(%%RED_T2), XWORD(%%RED_POLY), XWORD(%%RED_P1), 0x10
        vpslldq         XWORD(%%RED_T2), XWORD(%%RED_T2), 4 ; shift-L 1-DW for result without shifts
        ;; GH1H = GH1H + RED_T1 + RED_T2
        vpternlogq      XWORD(%%GH1H), XWORD(%%RED_T2), XWORD(%%RED_T1), 0x96
%endif
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; the last AES round
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vaesenclast, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%AESKEY1, %%AESKEY1, %%AESKEY1, %%AESKEY1

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; XOR against plain/cipher text
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vpxorq, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%DATA1, %%DATA2, %%DATA3, %%DATA4

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; retrieve the last cipher counter block (partially XOR'ed with text)
        ;; - this is needed for partial block cases
%ifidn %%INSTANCE_TYPE, multi_call
%if %%NUM_BLOCKS <= 4
        vextracti32x4   XWORD(%%LAST_CIPHER_BLK), %%B00_03, (%%NUM_BLOCKS - 1)
%elif %%NUM_BLOCKS <= 8
        vextracti32x4   XWORD(%%LAST_CIPHER_BLK), %%B04_07, (%%NUM_BLOCKS - 5)
%elif %%NUM_BLOCKS <= 12
        vextracti32x4   XWORD(%%LAST_CIPHER_BLK), %%B08_11, (%%NUM_BLOCKS - 9)
%else
        vextracti32x4   XWORD(%%LAST_CIPHER_BLK), %%B12_15, (%%NUM_BLOCKS - 13)
%endif
%endif

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; store cipher/plain text
        ZMM_STORE_MASKED_BLOCKS_0_16 %%NUM_BLOCKS, %%CIPH_PLAIN_OUT, %%DATA_OFFSET, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, %%MASKREG

        ;; =================================================
        ;; shuffle cipher text blocks for GHASH computation
%ifidn %%ENC_DEC, ENC
        ;; zero bytes outside the mask before hashing
%if %%NUM_BLOCKS <= 4
        vmovdqu8        %%B00_03{%%MASKREG}{z}, %%B00_03
%elif %%NUM_BLOCKS <= 8
        vmovdqu8        %%B04_07{%%MASKREG}{z}, %%B04_07
%elif %%NUM_BLOCKS <= 12
        vmovdqu8        %%B08_11{%%MASKREG}{z}, %%B08_11
%else
        vmovdqu8        %%B12_15{%%MASKREG}{z}, %%B12_15
%endif

        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vpshufb, \
                        %%DATA1, %%DATA2, %%DATA3, %%DATA4, \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, \
                        %%SHFMSK, %%SHFMSK, %%SHFMSK, %%SHFMSK
%else
        ;; zero bytes outside the mask before hashing
%if %%NUM_BLOCKS <= 4
        vmovdqu8        %%DATA1{%%MASKREG}{z}, %%DATA1
%elif %%NUM_BLOCKS <= 8
        vmovdqu8        %%DATA2{%%MASKREG}{z}, %%DATA2
%elif %%NUM_BLOCKS <= 12
        vmovdqu8        %%DATA3{%%MASKREG}{z}, %%DATA3
%else
        vmovdqu8        %%DATA4{%%MASKREG}{z}, %%DATA4
%endif

        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 %%NUM_BLOCKS, vpshufb, \
                        %%DATA1, %%DATA2, %%DATA3, %%DATA4, \
                        %%DATA1, %%DATA2, %%DATA3, %%DATA4, \
                        %%SHFMSK, %%SHFMSK, %%SHFMSK, %%SHFMSK
%endif

        ;; =================================================
        ;; Extract the last block for partial / multi_call cases
%ifidn %%INSTANCE_TYPE, multi_call
%if %%NUM_BLOCKS <= 4
        vextracti32x4   XWORD(%%LAST_GHASH_BLK), %%DATA1, %%NUM_BLOCKS - 1
%elif %%NUM_BLOCKS <= 8
        vextracti32x4   XWORD(%%LAST_GHASH_BLK), %%DATA2, %%NUM_BLOCKS - 5
%elif %%NUM_BLOCKS <= 12
        vextracti32x4   XWORD(%%LAST_GHASH_BLK), %%DATA3, %%NUM_BLOCKS - 9
%else
        vextracti32x4   XWORD(%%LAST_GHASH_BLK), %%DATA4, %%NUM_BLOCKS - 13
%endif
%endif

%if do_reduction != 0
        ;; GH1H holds reduced hash value
        ;; - normally do "vmovdqa64 XWORD(%%HASH_IN_OUT), XWORD(%%GH1H)"
        ;; - register rename trick obsoletes the above move
%endif

        ;; =================================================
        ;; GHASH last N blocks
        ;; - current hash value in HASH_IN_OUT or
        ;;   product parts in TO_REDUCE_H/M/L
        ;; - DATA1-DATA4 include blocks for GHASH

%if do_reduction == 0
        INITIAL_BLOCKS_PARTIAL_GHASH \
                        %%GDATA, %%GCTX, %%LENGTH, \
                        %%NUM_BLOCKS, XWORD(%%HASH_IN_OUT), %%ENC_DEC, \
                        %%INSTANCE_TYPE, %%DATA1, %%DATA2, %%DATA3, %%DATA4, \
                        XWORD(%%LAST_CIPHER_BLK), XWORD(%%LAST_GHASH_BLK), \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, %%GHDAT1, %%GHDAT2, \
                        %%AESKEY1, %%AESKEY2, %%GHKEY1, \
                        %%TO_REDUCE_H, %%TO_REDUCE_M, %%TO_REDUCE_L
%else
        INITIAL_BLOCKS_PARTIAL_GHASH \
                        %%GDATA, %%GCTX, %%LENGTH, \
                        %%NUM_BLOCKS, XWORD(%%HASH_IN_OUT), %%ENC_DEC, \
                        %%INSTANCE_TYPE, %%DATA1, %%DATA2, %%DATA3, %%DATA4, \
                        XWORD(%%LAST_CIPHER_BLK), XWORD(%%LAST_GHASH_BLK), \
                        %%B00_03, %%B04_07, %%B08_11, %%B12_15, %%GHDAT1, %%GHDAT2, \
                        %%AESKEY1, %%AESKEY2, %%GHKEY1
%endif
%endmacro

;;; ===========================================================================
;;; ===========================================================================
;;; Stitched GHASH of 16 blocks (with reduction) with encryption of N blocks
;;; followed with GHASH of the N blocks.
%macro GCM_ENC_DEC_LAST 46
%define %%GDATA                 %1  ; [in] key pointer
%define %%GCTX                  %2  ; [in] context pointer
%define %%CIPH_PLAIN_OUT        %3  ; [in] pointer to output buffer
%define %%PLAIN_CIPH_IN         %4  ; [in] pointer to input buffer
%define %%DATA_OFFSET           %5  ; [in] data offset
%define %%LENGTH                %6  ; [in/clobbered] data length
%define %%CTR_BE                %7  ; [in/out] ZMM counter blocks (last 4) in big-endian
%define %%CTR_CHECK             %8  ; [in/out] GP with 8-bit counter for overflow check
%define %%HASHKEY_OFFSET        %9  ; [in] numerical offset for the highest hash key
%define %%GHASHIN_BLK_OFFSET    %10 ; [in] numerical offset for GHASH blocks in
%define %%SHFMSK                %11 ; [in] ZMM with byte swap mask for pshufb
%define %%ZT00                  %12 ; [clobbered] temporary ZMM
%define %%ZT01                  %13 ; [clobbered] temporary ZMM
%define %%ZT02                  %14 ; [clobbered] temporary ZMM
%define %%ZT03                  %15 ; [clobbered] temporary ZMM
%define %%ZT04                  %16 ; [clobbered] temporary ZMM
%define %%ZT05                  %17 ; [clobbered] temporary ZMM
%define %%ZT06                  %18 ; [clobbered] temporary ZMM
%define %%ZT07                  %19 ; [clobbered] temporary ZMM
%define %%ZT08                  %20 ; [clobbered] temporary ZMM
%define %%ZT09                  %21 ; [clobbered] temporary ZMM
%define %%ZT10                  %22 ; [clobbered] temporary ZMM
%define %%ZT11                  %23 ; [clobbered] temporary ZMM
%define %%ZT12                  %24 ; [clobbered] temporary ZMM
%define %%ZT13                  %25 ; [clobbered] temporary ZMM
%define %%ZT14                  %26 ; [clobbered] temporary ZMM
%define %%ZT15                  %27 ; [clobbered] temporary ZMM
%define %%ZT16                  %28 ; [clobbered] temporary ZMM
%define %%ZT17                  %29 ; [clobbered] temporary ZMM
%define %%ZT18                  %30 ; [clobbered] temporary ZMM
%define %%ZT19                  %31 ; [clobbered] temporary ZMM
%define %%ZT20                  %32 ; [clobbered] temporary ZMM
%define %%ZT21                  %33 ; [clobbered] temporary ZMM
%define %%ZT22                  %34 ; [clobbered] temporary ZMM
%define %%ADDBE_4x4             %35 ; [in] ZMM with 4x128bits 4 in big-endian
%define %%ADDBE_1234            %36 ; [in] ZMM with 4x128bits 1, 2, 3 and 4 in big-endian
%define %%GHASH_TYPE            %37 ; [in] "start", "start_reduce", "mid", "end_reduce"
%define %%TO_REDUCE_L           %38 ; [in] ZMM for low 4x128-bit GHASH sum
%define %%TO_REDUCE_H           %39 ; [in] ZMM for hi 4x128-bit GHASH sum
%define %%TO_REDUCE_M           %40 ; [in] ZMM for medium 4x128-bit GHASH sum
%define %%ENC_DEC               %41 ; [in] cipher direction
%define %%HASH_IN_OUT           %42 ; [in/out] XMM ghash in/out value
%define %%IA0                   %43 ; [clobbered] GP temporary
%define %%IA1                   %44 ; [clobbered] GP temporary
%define %%MASKREG               %45 ; [clobbered] mask register
%define %%INSTANCE_TYPE         %46 ; [in] multi_call or single_call

        mov     DWORD(%%IA0), DWORD(%%LENGTH)
        add     DWORD(%%IA0), 15
        shr     DWORD(%%IA0), 4
        je      %%_last_num_blocks_is_0

        cmp     DWORD(%%IA0), 8
        je      %%_last_num_blocks_is_8
        jb      %%_last_num_blocks_is_7_1

        cmp     DWORD(%%IA0), 12
        je      %%_last_num_blocks_is_12
        jb      %%_last_num_blocks_is_11_9

        ;; 16, 15, 14 or 13
        cmp     DWORD(%%IA0), 15
        je      %%_last_num_blocks_is_15
        ja      %%_last_num_blocks_is_16
        cmp     DWORD(%%IA0), 14
        je      %%_last_num_blocks_is_14
        jmp     %%_last_num_blocks_is_13

%%_last_num_blocks_is_11_9:
        ;; 11, 10 or 9
        cmp     DWORD(%%IA0), 10
        je      %%_last_num_blocks_is_10
        ja      %%_last_num_blocks_is_11
        jmp     %%_last_num_blocks_is_9

%%_last_num_blocks_is_7_1:
        cmp     DWORD(%%IA0), 4
        je      %%_last_num_blocks_is_4
        jb      %%_last_num_blocks_is_3_1
        ;; 7, 6 or 5
        cmp     DWORD(%%IA0), 6
        ja      %%_last_num_blocks_is_7
        je      %%_last_num_blocks_is_6
        jmp     %%_last_num_blocks_is_5

%%_last_num_blocks_is_3_1:
        ;; 3, 2 or 1
        cmp     DWORD(%%IA0), 2
        ja      %%_last_num_blocks_is_3
        je      %%_last_num_blocks_is_2
        ;; fall through for `jmp %%_last_num_blocks_is_1`

        ;; Use rep to generate different block size variants
        ;; - one block size has to be the first one
%assign num_blocks 1
%rep 16
%%_last_num_blocks_is_ %+ num_blocks :
        GHASH_16_ENCRYPT_N_GHASH_N \
                %%GDATA, %%GCTX, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, \
                %%DATA_OFFSET, %%LENGTH, %%CTR_BE, %%CTR_CHECK, \
                %%HASHKEY_OFFSET, %%GHASHIN_BLK_OFFSET, %%SHFMSK, \
                %%ZT00, %%ZT01, %%ZT02, %%ZT03, %%ZT04, %%ZT05, %%ZT06, %%ZT07, \
                %%ZT08, %%ZT09, %%ZT10, %%ZT11, %%ZT12, %%ZT13, %%ZT14, %%ZT15, \
                %%ZT16, %%ZT17, %%ZT18, %%ZT19, %%ZT20, %%ZT21, %%ZT22, \
                %%ADDBE_4x4, %%ADDBE_1234, %%GHASH_TYPE, \
                %%TO_REDUCE_L, %%TO_REDUCE_H, %%TO_REDUCE_M, \
                %%ENC_DEC, %%HASH_IN_OUT, %%IA0, %%IA1, %%MASKREG, \
                num_blocks, %%INSTANCE_TYPE

        jmp     %%_last_blocks_done
%assign num_blocks (num_blocks + 1)
%endrep

%%_last_num_blocks_is_0:
;; if there is 0 blocks to cipher then there are only 16 blocks for ghash and reduction
;; - convert mid into end_reduce
;; - convert start into start_reduce
%ifidn %%GHASH_TYPE, mid
%xdefine %%GHASH_TYPE end_reduce
%endif
%ifidn %%GHASH_TYPE, start
%xdefine %%GHASH_TYPE start_reduce
%endif

        GHASH_16        %%GHASH_TYPE, %%TO_REDUCE_H, %%TO_REDUCE_M, %%TO_REDUCE_L, \
                        rsp, %%GHASHIN_BLK_OFFSET, 0, %%GDATA, %%HASHKEY_OFFSET, 0, %%HASH_IN_OUT, \
                        %%ZT00, %%ZT01, %%ZT02, %%ZT03, %%ZT04, %%ZT05, %%ZT06, %%ZT07, %%ZT08, %%ZT09

%%_last_blocks_done:

%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Main GCM macro stitching cipher with GHASH
;;; - operates on single stream
;;; - encrypts 16 blocks at a time
;;; - ghash the 16 previously encrypted ciphertext blocks
;;; - no partial block or multi_call handling here
%macro  GHASH_16_ENCRYPT_16_PARALLEL 42
%define %%GDATA                 %1  ; [in] key pointer
%define %%CIPH_PLAIN_OUT        %2  ; [in] pointer to output buffer
%define %%PLAIN_CIPH_IN         %3  ; [in] pointer to input buffer
%define %%DATA_OFFSET           %4  ; [in] data offset
%define %%CTR_BE                %5  ; [in/out] ZMM counter blocks (last 4) in big-endian
%define %%CTR_CHECK             %6  ; [in/out] GP with 8-bit counter for overflow check
%define %%HASHKEY_OFFSET        %7  ; [in] numerical offset for the highest hash key
%define %%AESOUT_BLK_OFFSET     %8  ; [in] numerical offset for AES-CTR out
%define %%GHASHIN_BLK_OFFSET    %9  ; [in] numerical offset for GHASH blocks in
%define %%SHFMSK                %10 ; [in] ZMM with byte swap mask for pshufb
%define %%ZT1                   %11 ; [clobbered] temporary ZMM (cipher)
%define %%ZT2                   %12 ; [clobbered] temporary ZMM (cipher)
%define %%ZT3                   %13 ; [clobbered] temporary ZMM (cipher)
%define %%ZT4                   %14 ; [clobbered] temporary ZMM (cipher)
%define %%ZT5                   %15 ; [clobbered/out] temporary ZMM or GHASH OUT (final_reduction)
%define %%ZT6                   %16 ; [clobbered] temporary ZMM (cipher)
%define %%ZT7                   %17 ; [clobbered] temporary ZMM (cipher)
%define %%ZT8                   %18 ; [clobbered] temporary ZMM (cipher)
%define %%ZT9                   %19 ; [clobbered] temporary ZMM (cipher)
%define %%ZT10                  %20 ; [clobbered] temporary ZMM (ghash)
%define %%ZT11                  %21 ; [clobbered] temporary ZMM (ghash)
%define %%ZT12                  %22 ; [clobbered] temporary ZMM (ghash)
%define %%ZT13                  %23 ; [clobbered] temporary ZMM (ghash)
%define %%ZT14                  %24 ; [clobbered] temporary ZMM (ghash)
%define %%ZT15                  %25 ; [clobbered] temporary ZMM (ghash)
%define %%ZT16                  %26 ; [clobbered] temporary ZMM (ghash)
%define %%ZT17                  %27 ; [clobbered] temporary ZMM (ghash)
%define %%ZT18                  %28 ; [clobbered] temporary ZMM (ghash)
%define %%ZT19                  %29 ; [clobbered] temporary ZMM
%define %%ZT20                  %30 ; [clobbered] temporary ZMM
%define %%ZT21                  %31 ; [clobbered] temporary ZMM
%define %%ZT22                  %32 ; [clobbered] temporary ZMM
%define %%ZT23                  %33 ; [clobbered] temporary ZMM
%define %%ADDBE_4x4             %34 ; [in] ZMM with 4x128bits 4 in big-endian
%define %%ADDBE_1234            %35 ; [in] ZMM with 4x128bits 1, 2, 3 and 4 in big-endian
%define %%TO_REDUCE_L           %36 ; [in/out] ZMM for low 4x128-bit GHASH sum
%define %%TO_REDUCE_H           %37 ; [in/out] ZMM for hi 4x128-bit GHASH sum
%define %%TO_REDUCE_M           %38 ; [in/out] ZMM for medium 4x128-bit GHASH sum
%define %%DO_REDUCTION          %39 ; [in] "no_reduction", "final_reduction", "first_time"
%define %%ENC_DEC               %40 ; [in] cipher direction
%define %%DATA_DISPL            %41 ; [in] fixed numerical data displacement/offset
%define %%GHASH_IN              %42 ; [in] current GHASH value or "no_ghash_in"

%define %%B00_03 %%ZT1
%define %%B04_07 %%ZT2
%define %%B08_11 %%ZT3
%define %%B12_15 %%ZT4

%define %%GH1H  %%ZT5 ; @note: do not change this mapping
%define %%GH1L  %%ZT6
%define %%GH1M  %%ZT7
%define %%GH1T  %%ZT8

%define %%GH2H  %%ZT9
%define %%GH2L  %%ZT10
%define %%GH2M  %%ZT11
%define %%GH2T  %%ZT12

%define %%RED_POLY %%GH2T
%define %%RED_P1   %%GH2L
%define %%RED_T1   %%GH2H
%define %%RED_T2   %%GH2M

%define %%GH3H  %%ZT13
%define %%GH3L  %%ZT14
%define %%GH3M  %%ZT15
%define %%GH3T  %%ZT16

%define %%DATA1 %%ZT13
%define %%DATA2 %%ZT14
%define %%DATA3 %%ZT15
%define %%DATA4 %%ZT16

%define %%AESKEY1  %%ZT17
%define %%AESKEY2  %%ZT18

%define %%GHKEY1  %%ZT19
%define %%GHKEY2  %%ZT20
%define %%GHDAT1  %%ZT21
%define %%GHDAT2  %%ZT22

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; prepare counter blocks

        cmp             BYTE(%%CTR_CHECK), (256 - 16)
        jae             %%_16_blocks_overflow
        vpaddd          %%B00_03, %%CTR_BE, %%ADDBE_1234
        vpaddd          %%B04_07, %%B00_03, %%ADDBE_4x4
        vpaddd          %%B08_11, %%B04_07, %%ADDBE_4x4
        vpaddd          %%B12_15, %%B08_11, %%ADDBE_4x4
        jmp             %%_16_blocks_ok
%%_16_blocks_overflow:
        vpshufb         %%CTR_BE, %%CTR_BE, %%SHFMSK
        vmovdqa64       %%B12_15, [rel ddq_add_4444]
        vpaddd          %%B00_03, %%CTR_BE, [rel ddq_add_1234]
        vpaddd          %%B04_07, %%B00_03, %%B12_15
        vpaddd          %%B08_11, %%B04_07, %%B12_15
        vpaddd          %%B12_15, %%B08_11, %%B12_15
        vpshufb         %%B00_03, %%SHFMSK
        vpshufb         %%B04_07, %%SHFMSK
        vpshufb         %%B08_11, %%SHFMSK
        vpshufb         %%B12_15, %%SHFMSK
%%_16_blocks_ok:

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; pre-load constants
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 0)]
%ifnidn %%GHASH_IN, no_ghash_in
        vpxorq          %%GHDAT1, %%GHASH_IN, [rsp + %%GHASHIN_BLK_OFFSET + (0*64)]
%else
        vmovdqa64       %%GHDAT1, [rsp + %%GHASHIN_BLK_OFFSET + (0*64)]
%endif
        vmovdqu64       %%GHKEY1, [%%GDATA + %%HASHKEY_OFFSET + (0*64)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; save counter for the next round
        ;; increment counter overflow check register
        vshufi64x2      %%CTR_BE, %%B12_15, %%B12_15, 1111_1111b
        add             BYTE(%%CTR_CHECK), 16

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; pre-load constants
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 1)]
        vmovdqu64       %%GHKEY2, [%%GDATA + %%HASHKEY_OFFSET + (1*64)]
        vmovdqa64       %%GHDAT2, [rsp + %%GHASHIN_BLK_OFFSET + (1*64)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; stitch AES rounds with GHASH

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 0 - ARK

        vpxorq          %%B00_03, %%AESKEY1
        vpxorq          %%B04_07, %%AESKEY1
        vpxorq          %%B08_11, %%AESKEY1
        vpxorq          %%B12_15, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 2)]

        ;;==================================================
        ;; GHASH 4 blocks (15 to 12)
        vpclmulqdq      %%GH1H, %%GHDAT1, %%GHKEY1, 0x11     ; a1*b1
        vpclmulqdq      %%GH1L, %%GHDAT1, %%GHKEY1, 0x00     ; a0*b0
        vpclmulqdq      %%GH1M, %%GHDAT1, %%GHKEY1, 0x01     ; a1*b0
        vpclmulqdq      %%GH1T, %%GHDAT1, %%GHKEY1, 0x10     ; a0*b1

        vmovdqu64       %%GHKEY1, [%%GDATA + %%HASHKEY_OFFSET + (2*64)]
        vmovdqa64       %%GHDAT1, [rsp + %%GHASHIN_BLK_OFFSET + (2*64)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 1
        vaesenc         %%B00_03, %%B00_03, %%AESKEY2
        vaesenc         %%B04_07, %%B04_07, %%AESKEY2
        vaesenc         %%B08_11, %%B08_11, %%AESKEY2
        vaesenc         %%B12_15, %%B12_15, %%AESKEY2
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 3)]

        ;; =================================================
        ;; GHASH 4 blocks (11 to 8)
        vpclmulqdq      %%GH2M, %%GHDAT2, %%GHKEY2, 0x10     ; a0*b1
        vpclmulqdq      %%GH2T, %%GHDAT2, %%GHKEY2, 0x01     ; a1*b0
        vpclmulqdq      %%GH2H, %%GHDAT2, %%GHKEY2, 0x11     ; a1*b1
        vpclmulqdq      %%GH2L, %%GHDAT2, %%GHKEY2, 0x00     ; a0*b0

        vmovdqu64       %%GHKEY2, [%%GDATA + %%HASHKEY_OFFSET + (3*64)]
        vmovdqa64       %%GHDAT2, [rsp + %%GHASHIN_BLK_OFFSET + (3*64)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 2
        vaesenc         %%B00_03, %%B00_03, %%AESKEY1
        vaesenc         %%B04_07, %%B04_07, %%AESKEY1
        vaesenc         %%B08_11, %%B08_11, %%AESKEY1
        vaesenc         %%B12_15, %%B12_15, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 4)]

        ;; =================================================
        ;; GHASH 4 blocks (7 to 4)
        vpclmulqdq      %%GH3M, %%GHDAT1, %%GHKEY1, 0x10     ; a0*b1
        vpclmulqdq      %%GH3T, %%GHDAT1, %%GHKEY1, 0x01     ; a1*b0
        vpclmulqdq      %%GH3H, %%GHDAT1, %%GHKEY1, 0x11     ; a1*b1
        vpclmulqdq      %%GH3L, %%GHDAT1, %%GHKEY1, 0x00     ; a0*b0

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES rounds 3
        vaesenc         %%B00_03, %%B00_03, %%AESKEY2
        vaesenc         %%B04_07, %%B04_07, %%AESKEY2
        vaesenc         %%B08_11, %%B08_11, %%AESKEY2
        vaesenc         %%B12_15, %%B12_15, %%AESKEY2
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 5)]

        ;; =================================================
        ;; Gather (XOR) GHASH for 12 blocks
        vpternlogq      %%GH1H, %%GH2H, %%GH3H, 0x96
        vpternlogq      %%GH1L, %%GH2L, %%GH3L, 0x96
        vpternlogq      %%GH1T, %%GH2T, %%GH3T, 0x96
        vpternlogq      %%GH1M, %%GH2M, %%GH3M, 0x96

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES rounds 4
        vaesenc         %%B00_03, %%B00_03, %%AESKEY1
        vaesenc         %%B04_07, %%B04_07, %%AESKEY1
        vaesenc         %%B08_11, %%B08_11, %%AESKEY1
        vaesenc         %%B12_15, %%B12_15, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 6)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; load plain/cipher text (recycle GH3xx registers)
        VX512LDR        %%DATA1, [%%PLAIN_CIPH_IN + %%DATA_OFFSET + %%DATA_DISPL + (0 * 64)]
        VX512LDR        %%DATA2, [%%PLAIN_CIPH_IN + %%DATA_OFFSET + %%DATA_DISPL + (1 * 64)]
        VX512LDR        %%DATA3, [%%PLAIN_CIPH_IN + %%DATA_OFFSET + %%DATA_DISPL + (2 * 64)]
        VX512LDR        %%DATA4, [%%PLAIN_CIPH_IN + %%DATA_OFFSET + %%DATA_DISPL + (3 * 64)]

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES rounds 5
        vaesenc         %%B00_03, %%B00_03, %%AESKEY2
        vaesenc         %%B04_07, %%B04_07, %%AESKEY2
        vaesenc         %%B08_11, %%B08_11, %%AESKEY2
        vaesenc         %%B12_15, %%B12_15, %%AESKEY2
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 7)]

        ;; =================================================
        ;; GHASH 4 blocks (3 to 0)
        vpclmulqdq      %%GH2M, %%GHDAT2, %%GHKEY2, 0x10     ; a0*b1
        vpclmulqdq      %%GH2T, %%GHDAT2, %%GHKEY2, 0x01     ; a1*b0
        vpclmulqdq      %%GH2H, %%GHDAT2, %%GHKEY2, 0x11     ; a1*b1
        vpclmulqdq      %%GH2L, %%GHDAT2, %%GHKEY2, 0x00     ; a0*b0

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 6
        vaesenc         %%B00_03, %%B00_03, %%AESKEY1
        vaesenc         %%B04_07, %%B04_07, %%AESKEY1
        vaesenc         %%B08_11, %%B08_11, %%AESKEY1
        vaesenc         %%B12_15, %%B12_15, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 8)]

        ;; =================================================
        ;; gather GHASH in GH1L (low) and GH1H (high)
%ifidn %%DO_REDUCTION, first_time
        vpternlogq      %%GH1M, %%GH1T, %%GH2T, 0x96        ; TM
        vpxorq          %%TO_REDUCE_M, %%GH1M, %%GH2M       ; TM
        vpxorq          %%TO_REDUCE_H, %%GH1H, %%GH2H       ; TH
        vpxorq          %%TO_REDUCE_L, %%GH1L, %%GH2L       ; TL
%endif
%ifidn %%DO_REDUCTION, no_reduction
        vpternlogq      %%GH1M, %%GH1T, %%GH2T, 0x96        ; TM
        vpternlogq      %%TO_REDUCE_M, %%GH1M, %%GH2M, 0x96 ; TM
        vpternlogq      %%TO_REDUCE_H, %%GH1H, %%GH2H, 0x96 ; TH
        vpternlogq      %%TO_REDUCE_L, %%GH1L, %%GH2L, 0x96 ; TL
%endif
%ifidn %%DO_REDUCTION, final_reduction
        ;; phase 1: add mid products together
        ;; also load polynomial constant for reduction
        vpternlogq      %%GH1M, %%GH1T, %%GH2T, 0x96 ; TM
        vpternlogq      %%GH1M, %%TO_REDUCE_M, %%GH2M, 0x96

        vpsrldq         %%GH2M, %%GH1M, 8
        vpslldq         %%GH1M, %%GH1M, 8

        vmovdqa64       XWORD(%%RED_POLY), [rel POLY2]
%endif

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 7
        vaesenc         %%B00_03, %%B00_03, %%AESKEY2
        vaesenc         %%B04_07, %%B04_07, %%AESKEY2
        vaesenc         %%B08_11, %%B08_11, %%AESKEY2
        vaesenc         %%B12_15, %%B12_15, %%AESKEY2
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 9)]

        ;; =================================================
        ;; Add mid product to high and low
%ifidn %%DO_REDUCTION, final_reduction
        vpternlogq      %%GH1H, %%GH2H, %%GH2M, 0x96    ; TH = TH1 + TH2 + TM>>64
        vpxorq          %%GH1H, %%TO_REDUCE_H
        vpternlogq      %%GH1L, %%GH2L, %%GH1M, 0x96    ; TL = TL1 + TL2 + TM<<64
        vpxorq          %%GH1L, %%TO_REDUCE_L
%endif

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 8
        vaesenc         %%B00_03, %%B00_03, %%AESKEY1
        vaesenc         %%B04_07, %%B04_07, %%AESKEY1
        vaesenc         %%B08_11, %%B08_11, %%AESKEY1
        vaesenc         %%B12_15, %%B12_15, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 10)]

        ;; =================================================
        ;; horizontal xor of low and high 4x128
%ifidn %%DO_REDUCTION, final_reduction
        VHPXORI4x128    %%GH1H, %%GH2H
        VHPXORI4x128    %%GH1L, %%GH2L
%endif

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES round 9
        vaesenc         %%B00_03, %%B00_03, %%AESKEY2
        vaesenc         %%B04_07, %%B04_07, %%AESKEY2
        vaesenc         %%B08_11, %%B08_11, %%AESKEY2
        vaesenc         %%B12_15, %%B12_15, %%AESKEY2
%if (NROUNDS >= 11)
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 11)]
%endif
        ;; =================================================
        ;; first phase of reduction
%ifidn %%DO_REDUCTION, final_reduction
        vpclmulqdq      XWORD(%%RED_P1), XWORD(%%RED_POLY), XWORD(%%GH1L), 0x01
        vpslldq         XWORD(%%RED_P1), XWORD(%%RED_P1), 8             ; shift-L 2 DWs
        vpxorq          XWORD(%%RED_P1), XWORD(%%GH1L), XWORD(%%RED_P1) ; first phase of the reduct
%endif

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; AES rounds up to 11 (AES192) or 13 (AES256)
        ;; AES128 is done
%if (NROUNDS >= 11)
        vaesenc         %%B00_03, %%B00_03, %%AESKEY1
        vaesenc         %%B04_07, %%B04_07, %%AESKEY1
        vaesenc         %%B08_11, %%B08_11, %%AESKEY1
        vaesenc         %%B12_15, %%B12_15, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 12)]

        vaesenc         %%B00_03, %%B00_03, %%AESKEY2
        vaesenc         %%B04_07, %%B04_07, %%AESKEY2
        vaesenc         %%B08_11, %%B08_11, %%AESKEY2
        vaesenc         %%B12_15, %%B12_15, %%AESKEY2
%if (NROUNDS == 13)
        vbroadcastf64x2 %%AESKEY2, [%%GDATA + (16 * 13)]

        vaesenc         %%B00_03, %%B00_03, %%AESKEY1
        vaesenc         %%B04_07, %%B04_07, %%AESKEY1
        vaesenc         %%B08_11, %%B08_11, %%AESKEY1
        vaesenc         %%B12_15, %%B12_15, %%AESKEY1
        vbroadcastf64x2 %%AESKEY1, [%%GDATA + (16 * 14)]

        vaesenc         %%B00_03, %%B00_03, %%AESKEY2
        vaesenc         %%B04_07, %%B04_07, %%AESKEY2
        vaesenc         %%B08_11, %%B08_11, %%AESKEY2
        vaesenc         %%B12_15, %%B12_15, %%AESKEY2
%endif ; GCM256 / NROUNDS = 13 (15 including the first and the last)
%endif ; GCM192 / NROUNDS = 11 (13 including the first and the last)

        ;; =================================================
        ;; second phase of the reduction
%ifidn %%DO_REDUCTION, final_reduction
        vpclmulqdq      XWORD(%%RED_T1), XWORD(%%RED_POLY), XWORD(%%RED_P1), 0x00
        vpsrldq         XWORD(%%RED_T1), XWORD(%%RED_T1), 4 ; shift-R 1-DW to obtain 2-DWs shift-R

        vpclmulqdq      XWORD(%%RED_T2), XWORD(%%RED_POLY), XWORD(%%RED_P1), 0x10
        vpslldq         XWORD(%%RED_T2), XWORD(%%RED_T2), 4 ; shift-L 1-DW for result without shifts
        ;; GH1H = GH1H x RED_T1 x RED_T2
        vpternlogq      XWORD(%%GH1H), XWORD(%%RED_T2), XWORD(%%RED_T1), 0x96
%endif

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; the last AES round
        vaesenclast     %%B00_03, %%B00_03, %%AESKEY1
        vaesenclast     %%B04_07, %%B04_07, %%AESKEY1
        vaesenclast     %%B08_11, %%B08_11, %%AESKEY1
        vaesenclast     %%B12_15, %%B12_15, %%AESKEY1

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; XOR against plain/cipher text
        vpxorq          %%B00_03, %%B00_03, %%DATA1
        vpxorq          %%B04_07, %%B04_07, %%DATA2
        vpxorq          %%B08_11, %%B08_11, %%DATA3
        vpxorq          %%B12_15, %%B12_15, %%DATA4

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; store cipher/plain text
        VX512STR        [%%CIPH_PLAIN_OUT + %%DATA_OFFSET + %%DATA_DISPL + (0 * 64)], %%B00_03
        VX512STR        [%%CIPH_PLAIN_OUT + %%DATA_OFFSET + %%DATA_DISPL + (1 * 64)], %%B04_07
        VX512STR        [%%CIPH_PLAIN_OUT + %%DATA_OFFSET + %%DATA_DISPL + (2 * 64)], %%B08_11
        VX512STR        [%%CIPH_PLAIN_OUT + %%DATA_OFFSET + %%DATA_DISPL + (3 * 64)], %%B12_15

        ;; =================================================
        ;; shuffle cipher text blocks for GHASH computation
%ifidn %%ENC_DEC, ENC
        vpshufb         %%B00_03, %%B00_03, %%SHFMSK
        vpshufb         %%B04_07, %%B04_07, %%SHFMSK
        vpshufb         %%B08_11, %%B08_11, %%SHFMSK
        vpshufb         %%B12_15, %%B12_15, %%SHFMSK
%else
        vpshufb         %%B00_03, %%DATA1, %%SHFMSK
        vpshufb         %%B04_07, %%DATA2, %%SHFMSK
        vpshufb         %%B08_11, %%DATA3, %%SHFMSK
        vpshufb         %%B12_15, %%DATA4, %%SHFMSK
%endif

        ;; =================================================
        ;; store shuffled cipher text for ghashing
        vmovdqa64       [rsp + %%AESOUT_BLK_OFFSET + (0*64)], %%B00_03
        vmovdqa64       [rsp + %%AESOUT_BLK_OFFSET + (1*64)], %%B04_07
        vmovdqa64       [rsp + %%AESOUT_BLK_OFFSET + (2*64)], %%B08_11
        vmovdqa64       [rsp + %%AESOUT_BLK_OFFSET + (3*64)], %%B12_15

%ifidn %%DO_REDUCTION, final_reduction
        ;; =================================================
        ;; Return GHASH value  through %%GH1H
%endif

%endmacro                       ; GHASH_16_ENCRYPT_16_PARALLEL

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Encryption of a single block
%macro  ENCRYPT_SINGLE_BLOCK 2
%define %%GDATA %1
%define %%XMM0  %2

                vpxorq          %%XMM0, %%XMM0, [%%GDATA+16*0]
%assign i 1
%rep NROUNDS
                vaesenc         %%XMM0, [%%GDATA+16*i]
%assign i (i+1)
%endrep
                vaesenclast     %%XMM0, [%%GDATA+16*i]
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Save register content for the caller
%macro FUNC_SAVE 0
        ;; Required for Update/GMC_ENC
        ;the number of pushes must equal STACK_OFFSET
        mov     rax, rsp

        sub     rsp, STACK_FRAME_SIZE
        and     rsp, ~63

        mov     [rsp + STACK_GP_OFFSET + 0*8], r12
        mov     [rsp + STACK_GP_OFFSET + 1*8], r13
        mov     [rsp + STACK_GP_OFFSET + 2*8], r14
        mov     [rsp + STACK_GP_OFFSET + 3*8], r15
        mov     [rsp + STACK_GP_OFFSET + 4*8], rax ; stack
        mov     r14, rax                               ; r14 is used to retrieve stack args
        mov     [rsp + STACK_GP_OFFSET + 5*8], rbp
        mov     [rsp + STACK_GP_OFFSET + 6*8], rbx
%ifidn __OUTPUT_FORMAT__, win64
        mov     [rsp + STACK_GP_OFFSET + 7*8], rdi
        mov     [rsp + STACK_GP_OFFSET + 8*8], rsi
%endif

%ifidn __OUTPUT_FORMAT__, win64
        ; xmm6:xmm15 need to be maintained for Windows
        vmovdqu [rsp + STACK_XMM_OFFSET + 0*16], xmm6
        vmovdqu [rsp + STACK_XMM_OFFSET + 1*16], xmm7
        vmovdqu [rsp + STACK_XMM_OFFSET + 2*16], xmm8
        vmovdqu [rsp + STACK_XMM_OFFSET + 3*16], xmm9
        vmovdqu [rsp + STACK_XMM_OFFSET + 4*16], xmm10
        vmovdqu [rsp + STACK_XMM_OFFSET + 5*16], xmm11
        vmovdqu [rsp + STACK_XMM_OFFSET + 6*16], xmm12
        vmovdqu [rsp + STACK_XMM_OFFSET + 7*16], xmm13
        vmovdqu [rsp + STACK_XMM_OFFSET + 8*16], xmm14
        vmovdqu [rsp + STACK_XMM_OFFSET + 9*16], xmm15
%endif
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Restore register content for the caller
%macro FUNC_RESTORE 0

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_zmms_asm
%else
        vzeroupper
%endif

%ifidn __OUTPUT_FORMAT__, win64
        vmovdqu xmm15, [rsp + STACK_XMM_OFFSET + 9*16]
        vmovdqu xmm14, [rsp + STACK_XMM_OFFSET + 8*16]
        vmovdqu xmm13, [rsp + STACK_XMM_OFFSET + 7*16]
        vmovdqu xmm12, [rsp + STACK_XMM_OFFSET + 6*16]
        vmovdqu xmm11, [rsp + STACK_XMM_OFFSET + 5*16]
        vmovdqu xmm10, [rsp + STACK_XMM_OFFSET + 4*16]
        vmovdqu xmm9, [rsp + STACK_XMM_OFFSET + 3*16]
        vmovdqu xmm8, [rsp + STACK_XMM_OFFSET + 2*16]
        vmovdqu xmm7, [rsp + STACK_XMM_OFFSET + 1*16]
        vmovdqu xmm6, [rsp + STACK_XMM_OFFSET + 0*16]
%endif

        ;; Required for Update/GMC_ENC
        mov     rbp, [rsp + STACK_GP_OFFSET + 5*8]
        mov     rbx, [rsp + STACK_GP_OFFSET + 6*8]
%ifidn __OUTPUT_FORMAT__, win64
        mov     rdi, [rsp + STACK_GP_OFFSET + 7*8]
        mov     rsi, [rsp + STACK_GP_OFFSET + 8*8]
%endif
        mov     r12, [rsp + STACK_GP_OFFSET + 0*8]
        mov     r13, [rsp + STACK_GP_OFFSET + 1*8]
        mov     r14, [rsp + STACK_GP_OFFSET + 2*8]
        mov     r15, [rsp + STACK_GP_OFFSET + 3*8]
        mov     rsp, [rsp + STACK_GP_OFFSET + 4*8] ; stack
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Calculate J0 for cases when IV length is different than 12 bytes
%macro CALC_J0 26
%define %%KEY           %1 ;; [in] Pointer to GCM KEY structure
%define %%IV            %2 ;; [in] Pointer to IV
%define %%IV_LEN        %3 ;; [in] IV length
%define %%J0            %4 ;; [out] XMM reg to contain J0
%define %%ZT0           %5 ;; [clobbered] ZMM register
%define %%ZT1           %6 ;; [clobbered] ZMM register
%define %%ZT2           %7 ;; [clobbered] ZMM register
%define %%ZT3           %8 ;; [clobbered] ZMM register
%define %%ZT4           %9 ;; [clobbered] ZMM register
%define %%ZT5           %10 ;; [clobbered] ZMM register
%define %%ZT6           %11 ;; [clobbered] ZMM register
%define %%ZT7           %12 ;; [clobbered] ZMM register
%define %%ZT8           %13 ;; [clobbered] ZMM register
%define %%ZT9           %14 ;; [clobbered] ZMM register
%define %%ZT10          %15 ;; [clobbered] ZMM register
%define %%ZT11          %16 ;; [clobbered] ZMM register
%define %%ZT12          %17 ;; [clobbered] ZMM register
%define %%ZT13          %18 ;; [clobbered] ZMM register
%define %%ZT14          %19 ;; [clobbered] ZMM register
%define %%ZT15          %20 ;; [clobbered] ZMM register
%define %%ZT16          %21 ;; [clobbered] ZMM register
%define %%ZT17          %22 ;; [clobbered] ZMM register
%define %%T1            %23 ;; [clobbered] GP register
%define %%T2            %24 ;; [clobbered] GP register
%define %%T3            %25 ;; [clobbered] GP register
%define %%MASKREG       %26 ;; [clobbered] mask register

        ;; J0 = GHASH(IV || 0s+64 || len(IV)64)
        ;; s = 16 * RoundUp(len(IV)/16) -  len(IV) */

        ;; Calculate GHASH of (IV || 0s)
        vpxor           %%J0, %%J0
        CALC_AAD_HASH   %%IV, %%IV_LEN, %%J0, %%KEY, %%ZT0, %%ZT1, %%ZT2, %%ZT3, \
                        %%ZT4, %%ZT5, %%ZT6, %%ZT7, %%ZT8, %%ZT9, %%ZT10, %%ZT11, \
                        %%ZT12, %%ZT13,  %%ZT14, %%ZT15, %%ZT16, %%ZT17, \
                        %%T1, %%T2, %%T3, %%MASKREG

        ;; Calculate GHASH of last 16-byte block (0 || len(IV)64)
        mov             %%T1, %%IV_LEN
        shl             %%T1, 3 ;; IV length in bits
        vmovq           XWORD(%%ZT2), %%T1

        ;; Might need shuffle of ZT2
        vpxorq          %%J0, XWORD(%%ZT2), %%J0

        vmovdqu64       XWORD(%%ZT0), [%%KEY + HashKey_1]
        GHASH_MUL       %%J0, XWORD(%%ZT0), XWORD(%%ZT1), XWORD(%%ZT2), XWORD(%%ZT3), XWORD(%%ZT4), XWORD(%%ZT5)

        vpshufb         %%J0, [rel SHUF_MASK] ; perform a 16Byte swap
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; GCM_INIT initializes a gcm_context_data struct to prepare for encoding/decoding.
;;; Input: gcm_key_data * (GDATA_KEY), gcm_context_data *(GDATA_CTX), IV,
;;; Additional Authentication data (A_IN), Additional Data length (A_LEN).
;;; Output: Updated GDATA_CTX with the hash of A_IN (AadHash) and initialized other parts of GDATA_CTX.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro  GCM_INIT        30-31
%define %%GDATA_KEY     %1      ; [in] GCM expanded keys pointer
%define %%GDATA_CTX     %2      ; [in] GCM context pointer
%define %%IV            %3      ; [in] IV pointer
%define %%A_IN          %4      ; [in] AAD pointer
%define %%A_LEN         %5      ; [in] AAD length in bytes
%define %%GPR1          %6      ; [clobbered] GP register
%define %%GPR2          %7      ; [clobbered] GP register
%define %%GPR3          %8      ; [clobbered] GP register
%define %%MASKREG       %9      ; [clobbered] mask register
%define %%AAD_HASH      %10     ; [out] XMM for AAD_HASH value (xmm14)
%define %%CUR_COUNT     %11     ; [out] XMM with current counter (xmm2)
%define %%ZT0           %12     ; [clobbered] ZMM register
%define %%ZT1           %13     ; [clobbered] ZMM register
%define %%ZT2           %14     ; [clobbered] ZMM register
%define %%ZT3           %15     ; [clobbered] ZMM register
%define %%ZT4           %16     ; [clobbered] ZMM register
%define %%ZT5           %17     ; [clobbered] ZMM register
%define %%ZT6           %18     ; [clobbered] ZMM register
%define %%ZT7           %19     ; [clobbered] ZMM register
%define %%ZT8           %20     ; [clobbered] ZMM register
%define %%ZT9           %21     ; [clobbered] ZMM register
%define %%ZT10          %22     ; [clobbered] ZMM register
%define %%ZT11          %23     ; [clobbered] ZMM register
%define %%ZT12          %24     ; [clobbered] ZMM register
%define %%ZT13          %25     ; [clobbered] ZMM register
%define %%ZT14          %26     ; [clobbered] ZMM register
%define %%ZT15          %27     ; [clobbered] ZMM register
%define %%ZT16          %28     ; [clobbered] ZMM register
%define %%ZT17          %29     ; [clobbered] ZMM register
%define %%INSTANCE_TYPE %30     ; [in] "single_call" or "multi_call"
%define %%IV_LEN        %31     ; [in] IV length

        ;; prepare IV
%if %0 == 31 ;; IV is different than 12 bytes
        CALC_J0 %%GDATA_KEY, %%IV, %%IV_LEN, %%CUR_COUNT, \
                        %%ZT0, %%ZT1, %%ZT2, %%ZT3, %%ZT4, %%ZT5, %%ZT6, %%ZT7, \
                        %%ZT8, %%ZT9, %%ZT10, %%ZT11, %%ZT12, %%ZT13, \
                        %%ZT14, %%ZT15, %%ZT16, %%ZT17, %%GPR1, %%GPR2, %%GPR3, %%MASKREG
%else ;; IV is 12 bytes
        ;; read 12 IV bytes and pad with 0x00000001
        vmovdqa64       %%CUR_COUNT, [rel ONEf]
        mov             %%GPR2, %%IV
        mov             DWORD(%%GPR1), 0x0000_0fff
        kmovd           %%MASKREG, DWORD(%%GPR1)
        vmovdqu8        %%CUR_COUNT{%%MASKREG}, [%%GPR2]      ; ctr = IV | 0x1
%endif

        ;; calculate AAD hash
        cmp             %%A_LEN, 12
        jne             %%_aad_is_not_12_bytes

        ;; load 12 bytes of AAD
%if %0 == 31 ;; IV is different than 12 bytes
        mov             DWORD(%%GPR1), 0x0000_0fff
        kmovd           %%MASKREG, DWORD(%%GPR1)
%endif
        mov             %%GPR1, %%A_IN
        vmovdqu8        XWORD(%%AAD_HASH){%%MASKREG}{z}, [%%GPR1]
        vmovdqu8        XWORD(%%ZT0), [%%GDATA_KEY + HashKey_1]
        vpshufb         XWORD(%%AAD_HASH), [rel SHUF_MASK]

        ;; GHASH 12 bytes of AAD
        GHASH_MUL       XWORD(%%AAD_HASH), XWORD(%%ZT0), \
                        XWORD(%%ZT1), XWORD(%%ZT2), XWORD(%%ZT3), XWORD(%%ZT4), XWORD(%%ZT5)

        jmp             %%_aad_compute_done

%%_aad_is_not_12_bytes:
        vpxor           %%AAD_HASH, %%AAD_HASH
        CALC_AAD_HASH   %%A_IN, %%A_LEN, %%AAD_HASH, %%GDATA_KEY, \
                        %%ZT0, %%ZT1, %%ZT2, %%ZT3, %%ZT4, %%ZT5, %%ZT6, %%ZT7, %%ZT8, %%ZT9, \
                        %%ZT10, %%ZT11, %%ZT12, %%ZT13,  %%ZT14, %%ZT15, %%ZT16, %%ZT17, \
                        %%GPR1, %%GPR2, %%GPR3, %%MASKREG
%%_aad_compute_done:

        ;; set up context fields
        mov             %%GPR1, %%A_LEN
        mov             [%%GDATA_CTX + AadLen], %%GPR1        ; ctx.aad_length = aad_length
        vmovdqu64       [%%GDATA_CTX + AadHash], %%AAD_HASH   ; ctx.aad hash = aad_hash

        xor             %%GPR1, %%GPR1
        mov             [%%GDATA_CTX + InLen], %%GPR1         ; ctx.in_length = 0
%ifidn %%INSTANCE_TYPE, multi_call
        mov             [%%GDATA_CTX + PBlockLen], %%GPR1     ; ctx.partial_block_length = 0
%endif

        vmovdqu64       [%%GDATA_CTX + OrigIV], %%CUR_COUNT    ; ctx.orig_IV = iv
        vpshufb         %%CUR_COUNT, [rel SHUF_MASK]
        vmovdqu64       [%%GDATA_CTX + CurCount], %%CUR_COUNT ; ctx.current_counter = iv (LE format)
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Cipher and ghash of payloads shorter than 256 bytes
;;; - number of blocks in the message comes as argument
;;; - depending on the number of blocks an optimized variant of
;;;   INITIAL_BLOCKS_PARTIAL is invoked
%macro  GCM_ENC_DEC_SMALL   39
%define %%GDATA_KEY         %1  ; [in] key pointer
%define %%GDATA_CTX         %2  ; [in] context pointer
%define %%CIPH_PLAIN_OUT    %3  ; [in] output buffer
%define %%PLAIN_CIPH_IN     %4  ; [in] input buffer
%define %%PLAIN_CIPH_LEN    %5  ; [in] buffer length
%define %%ENC_DEC           %6  ; [in] cipher direction
%define %%DATA_OFFSET       %7  ; [in] data offset
%define %%LENGTH            %8  ; [in] GP data length
%define %%NUM_BLOCKS        %9  ; [in] GP number of blocks to process 1 to 16
%define %%CTR               %10 ; [in/out] XMM counter block
%define %%HASH_IN_OUT       %11 ; [in/out] XMM GHASH value
%define %%INSTANCE_TYPE     %12 ; [in] single or multi call
%define %%ZTMP0             %13 ; [clobbered] ZMM register
%define %%ZTMP1             %14 ; [clobbered] ZMM register
%define %%ZTMP2             %15 ; [clobbered] ZMM register
%define %%ZTMP3             %16 ; [clobbered] ZMM register
%define %%ZTMP4             %17 ; [clobbered] ZMM register
%define %%ZTMP5             %18 ; [clobbered] ZMM register
%define %%ZTMP6             %19 ; [clobbered] ZMM register
%define %%ZTMP7             %20 ; [clobbered] ZMM register
%define %%ZTMP8             %21 ; [clobbered] ZMM register
%define %%ZTMP9             %22 ; [clobbered] ZMM register
%define %%ZTMP10            %23 ; [clobbered] ZMM register
%define %%ZTMP11            %24 ; [clobbered] ZMM register
%define %%ZTMP12            %25 ; [clobbered] ZMM register
%define %%ZTMP13            %26 ; [clobbered] ZMM register
%define %%ZTMP14            %27 ; [clobbered] ZMM register
%define %%ZTMP15            %28 ; [clobbered] ZMM register
%define %%ZTMP16            %29 ; [clobbered] ZMM register
%define %%ZTMP17            %30 ; [clobbered] ZMM register
%define %%ZTMP18            %31 ; [clobbered] ZMM register
%define %%ZTMP19            %32 ; [clobbered] ZMM register
%define %%ZTMP20            %33 ; [clobbered] ZMM register
%define %%ZTMP21            %34 ; [clobbered] ZMM register
%define %%ZTMP22            %35 ; [clobbered] ZMM register
%define %%IA0               %36 ; [clobbered] GP register
%define %%IA1               %37 ; [clobbered] GP register
%define %%MASKREG           %38 ; [clobbered] mask register
%define %%SHUFMASK          %39 ; [clobbered] ZMM to be used for BE/LE shuffle mask

        cmp     DWORD(%%NUM_BLOCKS), 8
        je      %%_small_initial_num_blocks_is_8
        jb      %%_small_initial_num_blocks_is_7_1

        cmp     DWORD(%%NUM_BLOCKS), 12
        je      %%_small_initial_num_blocks_is_12
        jb      %%_small_initial_num_blocks_is_11_9

        ;; 16, 15, 14 or 13
        cmp     DWORD(%%NUM_BLOCKS), 15
        ja      %%_small_initial_num_blocks_is_16
        je      %%_small_initial_num_blocks_is_15
        cmp     DWORD(%%NUM_BLOCKS), 14
        je      %%_small_initial_num_blocks_is_14
        jmp     %%_small_initial_num_blocks_is_13

%%_small_initial_num_blocks_is_11_9:
        ;; 11, 10 or 9
        cmp     DWORD(%%NUM_BLOCKS), 10
        ja      %%_small_initial_num_blocks_is_11
        je      %%_small_initial_num_blocks_is_10
        jmp     %%_small_initial_num_blocks_is_9

%%_small_initial_num_blocks_is_7_1:
        cmp     DWORD(%%NUM_BLOCKS), 4
        je      %%_small_initial_num_blocks_is_4
        jb      %%_small_initial_num_blocks_is_3_1
        ;; 7, 6 or 5
        cmp     DWORD(%%NUM_BLOCKS), 6
        ja      %%_small_initial_num_blocks_is_7
        je      %%_small_initial_num_blocks_is_6
        jmp     %%_small_initial_num_blocks_is_5

%%_small_initial_num_blocks_is_3_1:
        ;; 3, 2 or 1
        cmp     DWORD(%%NUM_BLOCKS), 2
        ja      %%_small_initial_num_blocks_is_3
        je      %%_small_initial_num_blocks_is_2

        ;; for %%NUM_BLOCKS == 1, just fall through and no 'jmp' needed

        ;; Use rep to generate different block size variants
        ;; - one block size has to be the first one
        ;; - ZTMP15 - ZTMP22 are free
%assign num_blocks 1
%rep 16
%%_small_initial_num_blocks_is_ %+ num_blocks :
        INITIAL_BLOCKS_PARTIAL  %%GDATA_KEY, %%GDATA_CTX, %%CIPH_PLAIN_OUT, \
                %%PLAIN_CIPH_IN, %%LENGTH, %%DATA_OFFSET, num_blocks, \
                %%CTR, %%HASH_IN_OUT, %%ENC_DEC, %%INSTANCE_TYPE, \
                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, \
                %%ZTMP5, %%ZTMP6, %%ZTMP7, %%ZTMP8, %%ZTMP9, \
                %%ZTMP10, %%ZTMP11, %%ZTMP12, %%ZTMP13, %%ZTMP14, \
                %%IA0, %%IA1, %%MASKREG, %%SHUFMASK
%if num_blocks != 16
        jmp     %%_small_initial_blocks_encrypted
%endif
%assign num_blocks (num_blocks + 1)
%endrep

%%_small_initial_blocks_encrypted:

%endmacro                       ; GCM_ENC_DEC_SMALL

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; GCM_ENC_DEC Encodes/Decodes given data. Assumes that the passed gcm_context_data struct
; has been initialized by GCM_INIT
; Requires the input data be at least 1 byte long because of READ_SMALL_INPUT_DATA.
; Input: gcm_key_data struct* (GDATA_KEY), gcm_context_data *(GDATA_CTX), input text (PLAIN_CIPH_IN),
; input text length (PLAIN_CIPH_LEN) and whether encoding or decoding (ENC_DEC).
; Output: A cipher of the given plain text (CIPH_PLAIN_OUT), and updated GDATA_CTX
; Clobbers rax, r10-r15, and zmm0-zmm31, k1
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro  GCM_ENC_DEC         7
%define %%GDATA_KEY         %1  ; [in] key pointer
%define %%GDATA_CTX         %2  ; [in] context pointer
%define %%CIPH_PLAIN_OUT    %3  ; [in] output buffer pointer
%define %%PLAIN_CIPH_IN     %4  ; [in] input buffer pointer
%define %%PLAIN_CIPH_LEN    %5  ; [in] buffer length
%define %%ENC_DEC           %6  ; [in] cipher direction
%define %%INSTANCE_TYPE     %7  ; [in] 'single_call' or 'multi_call' selection

%define %%IA0               r10
%define %%IA1               r12
%define %%IA2               r13
%define %%IA3               r15
%define %%IA4               r11
%define %%IA5               rax
%define %%IA6               rbx

%ifidn __OUTPUT_FORMAT__, win64
%define %%LENGTH            %%IA2
%endif
%define %%CTR_CHECK         %%IA3
%define %%DATA_OFFSET       %%IA4
%define %%HASHK_PTR         %%IA6

%define %%CTR_BLOCKz            zmm2
%define %%CTR_BLOCKx            xmm2 ; hardcoded in GCM_INIT

%define %%AAD_HASHz             zmm14
%define %%AAD_HASHx             xmm14 ; hardcoded in GCM_COMPLETE

%define %%ZTMP0                 zmm0
%define %%ZTMP1                 zmm3
%define %%ZTMP2                 zmm4
%define %%ZTMP3                 zmm5
%define %%ZTMP4                 zmm6
%define %%ZTMP5                 zmm7
%define %%ZTMP6                 zmm10
%define %%ZTMP7                 zmm11
%define %%ZTMP8                 zmm12
%define %%ZTMP9                 zmm13
%define %%ZTMP10                zmm15
%define %%ZTMP11                zmm16
%define %%ZTMP12                zmm17

%define %%ZTMP13                zmm19
%define %%ZTMP14                zmm20
%define %%ZTMP15                zmm21
%define %%ZTMP16                zmm30
%define %%ZTMP17                zmm31
%define %%ZTMP18                zmm1
%define %%ZTMP19                zmm18
%define %%ZTMP20                zmm8
%define %%ZTMP21                zmm22
%define %%ZTMP22                zmm23

%define %%GH                    zmm24
%define %%GL                    zmm25
%define %%GM                    zmm26
%define %%SHUF_MASK             zmm29

;;; Unused in the small packet path
%define %%ADDBE_4x4             zmm27
%define %%ADDBE_1234            zmm28

%define %%MASKREG               k1

;; reduction every 48 blocks, depth 32 blocks
;; @note 48 blocks is the maximum capacity of the stack frame
%assign big_loop_nblocks        48
%assign big_loop_depth          32

;;; Macro flow depending on packet size
;;; - LENGTH <= 16 blocks
;;;   - cipher followed by hashing (reduction)
;;; - 16 blocks < LENGTH < 32 blocks
;;;   - cipher 16 blocks
;;;   - cipher N blocks & hash 16 blocks, hash N blocks (reduction)
;;; - 32 blocks < LENGTH < 48 blocks
;;;   - cipher 2 x 16 blocks
;;;   - hash 16 blocks
;;;   - cipher N blocks & hash 16 blocks, hash N blocks (reduction)
;;; - LENGTH >= 48 blocks
;;;   - cipher 2 x 16 blocks
;;;   - while (data_to_cipher >= 48 blocks):
;;;     - cipher 16 blocks & hash 16 blocks
;;;     - cipher 16 blocks & hash 16 blocks
;;;     - cipher 16 blocks & hash 16 blocks (reduction)
;;;   - if (data_to_cipher >= 32 blocks):
;;;     - cipher 16 blocks & hash 16 blocks
;;;     - cipher 16 blocks & hash 16 blocks
;;;     - hash 16 blocks (reduction)
;;;     - cipher N blocks & hash 16 blocks, hash N blocks (reduction)
;;;   - elif (data_to_cipher >= 16 blocks):
;;;     - cipher 16 blocks & hash 16 blocks
;;;     - hash 16 blocks
;;;     - cipher N blocks & hash 16 blocks, hash N blocks (reduction)
;;;   - else:
;;;     - hash 16 blocks
;;;     - cipher N blocks & hash 16 blocks, hash N blocks (reduction)

%ifidn __OUTPUT_FORMAT__, win64
        cmp             %%PLAIN_CIPH_LEN, 0
%else
        or              %%PLAIN_CIPH_LEN, %%PLAIN_CIPH_LEN
%endif
        je              %%_enc_dec_done

        ;; Update length of data processed
%ifidn __OUTPUT_FORMAT__, win64
        mov             %%IA0, %%PLAIN_CIPH_LEN
        add             [%%GDATA_CTX + InLen], %%IA0
%else
        add             [%%GDATA_CTX + InLen], %%PLAIN_CIPH_LEN
%endif
        vmovdqu64       %%AAD_HASHx, [%%GDATA_CTX + AadHash]

%ifidn %%INSTANCE_TYPE, multi_call
        ;; NOTE: partial block processing makes only sense for multi_call here.
        ;; Used for the update flow - if there was a previous partial
        ;; block fill the remaining bytes here.
        PARTIAL_BLOCK %%GDATA_KEY, %%GDATA_CTX, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, \
                %%PLAIN_CIPH_LEN, %%DATA_OFFSET, %%AAD_HASHx, %%ENC_DEC, \
                %%IA0, %%IA1, %%IA2, %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, \
                %%ZTMP5, %%ZTMP6, %%ZTMP7, %%ZTMP8, %%ZTMP9, %%MASKREG
%else
        xor             %%DATA_OFFSET, %%DATA_OFFSET
%endif

%ifidn %%INSTANCE_TYPE, single_call
        ;;  use counter block from GCM_INIT
%else
        vmovdqu64       %%CTR_BLOCKx, [%%GDATA_CTX + CurCount]
%endif

        ;; Save the amount of data left to process in %%LENGTH
%ifidn __OUTPUT_FORMAT__, win64
        mov             %%LENGTH, %%PLAIN_CIPH_LEN
%else
%define %%LENGTH %%PLAIN_CIPH_LEN        ;; PLAIN_CIPH_LEN is a register on linux
%endif

%ifidn %%INSTANCE_TYPE, multi_call
        ;; NOTE: %%DATA_OFFSET is zero in single_call case.
        ;;      Consequently PLAIN_CIPH_LEN will never be zero after
        ;;      %%DATA_OFFSET subtraction below.
        ;; There may be no more data if it was consumed in the partial block.
        sub             %%LENGTH, %%DATA_OFFSET
        je              %%_enc_dec_done
%endif                          ; %%INSTANCE_TYPE, multi_call

        cmp             %%LENGTH, (16 * 16)
        jbe             %%_message_below_equal_16_blocks

        vmovdqa64       %%SHUF_MASK, [rel SHUF_MASK]
        vmovdqa64       %%ADDBE_4x4, [rel ddq_addbe_4444]
        vmovdqa64       %%ADDBE_1234, [rel ddq_addbe_1234]

        ;; start the pipeline
        ;; - 32 blocks aes-ctr
        ;; - 16 blocks ghash + aes-ctr

        ;; set up CTR_CHECK
        vmovd           DWORD(%%CTR_CHECK), %%CTR_BLOCKx
        and             DWORD(%%CTR_CHECK), 255

        ;; in LE format after init, convert to BE
        vshufi64x2      %%CTR_BLOCKz, %%CTR_BLOCKz, %%CTR_BLOCKz, 0
        vpshufb         %%CTR_BLOCKz, %%CTR_BLOCKz, %%SHUF_MASK

        ;; ==== AES-CTR - first 16 blocks
%assign aesout_offset (STACK_LOCAL_OFFSET + (0 * 16))
%assign data_in_out_offset 0

        INITIAL_BLOCKS_16       %%PLAIN_CIPH_IN, %%CIPH_PLAIN_OUT, %%GDATA_KEY, %%DATA_OFFSET, \
                                no_ghash, %%CTR_BLOCKz, %%CTR_CHECK, %%ADDBE_4x4, %%ADDBE_1234, \
                                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, \
                                %%ZTMP5, %%ZTMP6, %%ZTMP7, %%ZTMP8, \
                                %%SHUF_MASK, %%ENC_DEC, aesout_offset, data_in_out_offset

        cmp             %%LENGTH, (32 * 16)
        jb              %%_message_below_32_blocks

        ;; ==== AES-CTR - next 16 blocks
%assign aesout_offset (STACK_LOCAL_OFFSET + (16 * 16))
%assign data_in_out_offset (16 * 16)

        INITIAL_BLOCKS_16       %%PLAIN_CIPH_IN, %%CIPH_PLAIN_OUT, %%GDATA_KEY, %%DATA_OFFSET, \
                                no_ghash, %%CTR_BLOCKz, %%CTR_CHECK, %%ADDBE_4x4, %%ADDBE_1234, \
                                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, \
                                %%ZTMP5, %%ZTMP6, %%ZTMP7, %%ZTMP8, \
                                %%SHUF_MASK, %%ENC_DEC, aesout_offset, data_in_out_offset

        add             %%DATA_OFFSET, (32 * 16)
        sub             %%LENGTH, (32 * 16)

        cmp             %%LENGTH, (big_loop_nblocks * 16)
        jb              %%_no_more_big_nblocks

        ;; ====
        ;; ==== AES-CTR + GHASH - 48 blocks loop
        ;; ====
%%_encrypt_big_nblocks:
        ;; ==== AES-CTR + GHASH - 16 blocks, start
%assign aesout_offset (STACK_LOCAL_OFFSET + (32 * 16))
%assign data_in_out_offset (0 * 16)
%assign ghashin_offset (STACK_LOCAL_OFFSET + (0 * 16))

        GHASH_16_ENCRYPT_16_PARALLEL  %%GDATA_KEY, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, %%DATA_OFFSET, \
                %%CTR_BLOCKz, %%CTR_CHECK, \
                HashKey_48, aesout_offset, ghashin_offset, %%SHUF_MASK, \
                %%ZTMP0,  %%ZTMP1,  %%ZTMP2,  %%ZTMP3, \
                %%ZTMP4,  %%ZTMP5,  %%ZTMP6,  %%ZTMP7, \
                %%ZTMP8,  %%ZTMP9,  %%ZTMP10, %%ZTMP11,\
                %%ZTMP12, %%ZTMP13, %%ZTMP14, %%ZTMP15,\
                %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, \
                %%ZTMP20, %%ZTMP21, %%ZTMP22, \
                %%ADDBE_4x4, %%ADDBE_1234, \
                %%GL, %%GH, %%GM, \
                first_time, %%ENC_DEC, data_in_out_offset, %%AAD_HASHz

        ;; ==== AES-CTR + GHASH - 16 blocks, no reduction
%assign aesout_offset (STACK_LOCAL_OFFSET + (0 * 16))
%assign data_in_out_offset (16 * 16)
%assign ghashin_offset (STACK_LOCAL_OFFSET + (16 * 16))

        GHASH_16_ENCRYPT_16_PARALLEL  %%GDATA_KEY, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, %%DATA_OFFSET, \
                %%CTR_BLOCKz, %%CTR_CHECK, \
                HashKey_32, aesout_offset, ghashin_offset, %%SHUF_MASK, \
                %%ZTMP0,  %%ZTMP1,  %%ZTMP2,  %%ZTMP3, \
                %%ZTMP4,  %%ZTMP5,  %%ZTMP6,  %%ZTMP7, \
                %%ZTMP8,  %%ZTMP9,  %%ZTMP10, %%ZTMP11,\
                %%ZTMP12, %%ZTMP13, %%ZTMP14, %%ZTMP15,\
                %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, \
                %%ZTMP20, %%ZTMP21, %%ZTMP22, \
                %%ADDBE_4x4, %%ADDBE_1234, \
                %%GL, %%GH, %%GM, \
                no_reduction, %%ENC_DEC, data_in_out_offset, no_ghash_in

        ;; ==== AES-CTR + GHASH - 16 blocks, reduction
%assign aesout_offset (STACK_LOCAL_OFFSET + (16 * 16))
%assign data_in_out_offset (32 * 16)
%assign ghashin_offset (STACK_LOCAL_OFFSET + (32 * 16))

        GHASH_16_ENCRYPT_16_PARALLEL  %%GDATA_KEY, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, %%DATA_OFFSET, \
                %%CTR_BLOCKz, %%CTR_CHECK, \
                HashKey_16, aesout_offset, ghashin_offset, %%SHUF_MASK, \
                %%ZTMP0,  %%ZTMP1,  %%ZTMP2,  %%ZTMP3, \
                %%ZTMP4,  %%ZTMP5,  %%ZTMP6,  %%ZTMP7, \
                %%ZTMP8,  %%ZTMP9,  %%ZTMP10, %%ZTMP11,\
                %%ZTMP12, %%ZTMP13, %%ZTMP14, %%ZTMP15,\
                %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, \
                %%ZTMP20, %%ZTMP21, %%ZTMP22, \
                %%ADDBE_4x4, %%ADDBE_1234, \
                %%GL, %%GH, %%GM, \
                final_reduction, %%ENC_DEC, data_in_out_offset, no_ghash_in

        ;; === xor cipher block 0 with GHASH (ZT4)
        vmovdqa64       %%AAD_HASHz, %%ZTMP4

        add             %%DATA_OFFSET, (big_loop_nblocks * 16)
        sub             %%LENGTH, (big_loop_nblocks * 16)
        cmp             %%LENGTH, (big_loop_nblocks * 16)
        jae             %%_encrypt_big_nblocks

%%_no_more_big_nblocks:

        cmp             %%LENGTH, (32 * 16)
        jae             %%_encrypt_32_blocks

        cmp             %%LENGTH, (16 * 16)
        jae             %%_encrypt_16_blocks

        ;; =====================================================
        ;; =====================================================
        ;; ==== GHASH 1 x 16 blocks
        ;; ==== GHASH 1 x 16 blocks (reduction) & encrypt N blocks
        ;; ====      then GHASH N blocks
%%_encrypt_0_blocks_ghash_32:
        ;; calculate offset to the right hash key
%ifidn %%INSTANCE_TYPE, multi_call
        mov             DWORD(%%IA0), DWORD(%%LENGTH)
%else
        lea             DWORD(%%IA0), [DWORD(%%LENGTH) + 15]
%endif
        and             DWORD(%%IA0), ~15
        mov             DWORD(%%HASHK_PTR), HashKey_32
        sub             DWORD(%%HASHK_PTR), DWORD(%%IA0)

        ;; ==== GHASH 32 blocks and follow with reduction
        GHASH_16        start, %%GH, %%GM, %%GL, rsp, STACK_LOCAL_OFFSET, (0 * 16), %%GDATA_KEY, %%HASHK_PTR, 0, %%AAD_HASHz, \
                        %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, %%ZTMP6, %%ZTMP7, %%ZTMP8, %%ZTMP9

        ;; ==== GHASH 1 x 16 blocks with reduction + cipher and ghash on the reminder
%assign ghashin_offset (STACK_LOCAL_OFFSET + (16 * 16))
        add             DWORD(%%HASHK_PTR), (16 * 16)

        GCM_ENC_DEC_LAST \
                %%GDATA_KEY, %%GDATA_CTX, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, \
                %%DATA_OFFSET, %%LENGTH, %%CTR_BLOCKz, %%CTR_CHECK, \
                %%HASHK_PTR, ghashin_offset, %%SHUF_MASK, \
                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, %%ZTMP6, \
                %%ZTMP7, %%ZTMP8, %%ZTMP9, %%ZTMP10, %%ZTMP11, %%ZTMP12, %%ZTMP13, \
                %%ZTMP14, %%ZTMP15, %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, %%ZTMP20, \
                %%ZTMP21, %%ZTMP22, \
                %%ADDBE_4x4, %%ADDBE_1234, mid, %%GL, %%GH, %%GM, \
                %%ENC_DEC, %%AAD_HASHz, %%IA0, %%IA5, %%MASKREG, %%INSTANCE_TYPE

%ifidn %%INSTANCE_TYPE, multi_call
        vpshufb         %%CTR_BLOCKx, %%CTR_BLOCKx, XWORD(%%SHUF_MASK)
%endif
        jmp             %%_ghash_done

        ;; =====================================================
        ;; =====================================================
        ;; ==== GHASH & encrypt 1 x 16 blocks
        ;; ==== GHASH & encrypt 1 x 16 blocks
        ;; ==== GHASH 1 x 16 blocks (reduction)
        ;; ==== GHASH 1 x 16 blocks (reduction) & encrypt N blocks
        ;; ====      then GHASH N blocks
%%_encrypt_32_blocks:
        ;; ==== AES-CTR + GHASH - 16 blocks, start
%assign aesout_offset (STACK_LOCAL_OFFSET + (32 * 16))
%assign ghashin_offset (STACK_LOCAL_OFFSET + (0 * 16))
%assign data_in_out_offset (0 * 16)

        GHASH_16_ENCRYPT_16_PARALLEL  %%GDATA_KEY, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, %%DATA_OFFSET, \
                %%CTR_BLOCKz, %%CTR_CHECK, \
                HashKey_48, aesout_offset, ghashin_offset, %%SHUF_MASK, \
                %%ZTMP0,  %%ZTMP1,  %%ZTMP2,  %%ZTMP3, \
                %%ZTMP4,  %%ZTMP5,  %%ZTMP6,  %%ZTMP7, \
                %%ZTMP8,  %%ZTMP9,  %%ZTMP10, %%ZTMP11,\
                %%ZTMP12, %%ZTMP13, %%ZTMP14, %%ZTMP15,\
                %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, \
                %%ZTMP20, %%ZTMP21, %%ZTMP22, \
                %%ADDBE_4x4, %%ADDBE_1234, \
                %%GL, %%GH, %%GM, \
                first_time, %%ENC_DEC, data_in_out_offset, %%AAD_HASHz

        ;; ==== AES-CTR + GHASH - 16 blocks, no reduction
%assign aesout_offset (STACK_LOCAL_OFFSET + (0 * 16))
%assign ghashin_offset (STACK_LOCAL_OFFSET + (16 * 16))
%assign data_in_out_offset (16 * 16)

        GHASH_16_ENCRYPT_16_PARALLEL  %%GDATA_KEY, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, %%DATA_OFFSET, \
                %%CTR_BLOCKz, %%CTR_CHECK, \
                HashKey_32, aesout_offset, ghashin_offset, %%SHUF_MASK, \
                %%ZTMP0,  %%ZTMP1,  %%ZTMP2,  %%ZTMP3, \
                %%ZTMP4,  %%ZTMP5,  %%ZTMP6,  %%ZTMP7, \
                %%ZTMP8,  %%ZTMP9,  %%ZTMP10, %%ZTMP11,\
                %%ZTMP12, %%ZTMP13, %%ZTMP14, %%ZTMP15,\
                %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, \
                %%ZTMP20, %%ZTMP21, %%ZTMP22, \
                %%ADDBE_4x4, %%ADDBE_1234, \
                %%GL, %%GH, %%GM, \
                no_reduction, %%ENC_DEC, data_in_out_offset, no_ghash_in

        ;; ==== GHASH 16 blocks with reduction
        GHASH_16        end_reduce, %%GH, %%GM, %%GL, rsp, STACK_LOCAL_OFFSET, (32 * 16), %%GDATA_KEY, HashKey_16, 0, %%AAD_HASHz, \
                        %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, %%ZTMP6, %%ZTMP7, %%ZTMP8, %%ZTMP9

        ;; ==== GHASH 1 x 16 blocks with reduction + cipher and ghash on the reminder
%assign ghashin_offset (STACK_LOCAL_OFFSET + (0 * 16))

        sub             %%LENGTH, (32 * 16)
        add             %%DATA_OFFSET, (32 * 16)

        ;; calculate offset to the right hash key
%ifidn %%INSTANCE_TYPE, multi_call
        mov             DWORD(%%IA0), DWORD(%%LENGTH)
%else
        lea             DWORD(%%IA0), [DWORD(%%LENGTH) + 15]
%endif
        and             DWORD(%%IA0), ~15
        mov             DWORD(%%HASHK_PTR), HashKey_16
        sub             DWORD(%%HASHK_PTR), DWORD(%%IA0)

        GCM_ENC_DEC_LAST \
                %%GDATA_KEY, %%GDATA_CTX, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, \
                %%DATA_OFFSET, %%LENGTH, %%CTR_BLOCKz, %%CTR_CHECK, \
                %%HASHK_PTR, ghashin_offset, %%SHUF_MASK, \
                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, %%ZTMP6, \
                %%ZTMP7, %%ZTMP8, %%ZTMP9, %%ZTMP10, %%ZTMP11, %%ZTMP12, %%ZTMP13, \
                %%ZTMP14, %%ZTMP15, %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, %%ZTMP20, \
                %%ZTMP21, %%ZTMP22, \
                %%ADDBE_4x4, %%ADDBE_1234, start, %%GL, %%GH, %%GM, \
                %%ENC_DEC, %%AAD_HASHz, %%IA0, %%IA5, %%MASKREG, %%INSTANCE_TYPE

%ifidn %%INSTANCE_TYPE, multi_call
        vpshufb         %%CTR_BLOCKx, %%CTR_BLOCKx, XWORD(%%SHUF_MASK)
%endif
        jmp             %%_ghash_done

        ;; =====================================================
        ;; =====================================================
        ;; ==== GHASH & encrypt 16 blocks (done before)
        ;; ==== GHASH 1 x 16 blocks
        ;; ==== GHASH 1 x 16 blocks (reduction) & encrypt N blocks
        ;; ====      then GHASH N blocks
%%_encrypt_16_blocks:
        ;; ==== AES-CTR + GHASH - 16 blocks, start
%assign aesout_offset (STACK_LOCAL_OFFSET + (32 * 16))
%assign ghashin_offset (STACK_LOCAL_OFFSET + (0 * 16))
%assign data_in_out_offset (0 * 16)

        GHASH_16_ENCRYPT_16_PARALLEL  %%GDATA_KEY, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, %%DATA_OFFSET, \
                %%CTR_BLOCKz, %%CTR_CHECK, \
                HashKey_48, aesout_offset, ghashin_offset, %%SHUF_MASK, \
                %%ZTMP0,  %%ZTMP1,  %%ZTMP2,  %%ZTMP3, \
                %%ZTMP4,  %%ZTMP5,  %%ZTMP6,  %%ZTMP7, \
                %%ZTMP8,  %%ZTMP9,  %%ZTMP10, %%ZTMP11,\
                %%ZTMP12, %%ZTMP13, %%ZTMP14, %%ZTMP15,\
                %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, \
                %%ZTMP20, %%ZTMP21, %%ZTMP22, \
                %%ADDBE_4x4, %%ADDBE_1234, \
                %%GL, %%GH, %%GM, \
                first_time, %%ENC_DEC, data_in_out_offset, %%AAD_HASHz

        ;; ==== GHASH 1 x 16 blocks
        GHASH_16        mid, %%GH, %%GM, %%GL, rsp, STACK_LOCAL_OFFSET, (16 * 16), %%GDATA_KEY, HashKey_32, 0, no_hash_input, \
                        %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, %%ZTMP6, %%ZTMP7, %%ZTMP8, %%ZTMP9

        ;; ==== GHASH 1 x 16 blocks with reduction + cipher and ghash on the reminder
%assign ghashin_offset (STACK_LOCAL_OFFSET + (32 * 16))

        sub             %%LENGTH, (16 * 16)
        add             %%DATA_OFFSET, (16 * 16)

        GCM_ENC_DEC_LAST \
                %%GDATA_KEY, %%GDATA_CTX, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, \
                %%DATA_OFFSET, %%LENGTH, %%CTR_BLOCKz, %%CTR_CHECK, \
                HashKey_16, ghashin_offset, %%SHUF_MASK, \
                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, %%ZTMP6, \
                %%ZTMP7, %%ZTMP8, %%ZTMP9, %%ZTMP10, %%ZTMP11, %%ZTMP12, %%ZTMP13, \
                %%ZTMP14, %%ZTMP15, %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, %%ZTMP20, \
                %%ZTMP21, %%ZTMP22, \
                %%ADDBE_4x4, %%ADDBE_1234, end_reduce, %%GL, %%GH, %%GM, \
                %%ENC_DEC, %%AAD_HASHz, %%IA0, %%IA5, %%MASKREG, %%INSTANCE_TYPE

%ifidn %%INSTANCE_TYPE, multi_call
        vpshufb         %%CTR_BLOCKx, %%CTR_BLOCKx, XWORD(%%SHUF_MASK)
%endif
        jmp             %%_ghash_done

%%_message_below_32_blocks:
        ;; 32 > number of blocks > 16

        sub             %%LENGTH, (16 * 16)
        add             %%DATA_OFFSET, (16 * 16)

%assign ghashin_offset (STACK_LOCAL_OFFSET + (0 * 16))

        ;; calculate offset to the right hash key
%ifidn %%INSTANCE_TYPE, multi_call
        mov             DWORD(%%IA0), DWORD(%%LENGTH)
%else
        lea             DWORD(%%IA0), [DWORD(%%LENGTH) + 15]
%endif
        and             DWORD(%%IA0), ~15
        mov             DWORD(%%HASHK_PTR), HashKey_16
        sub             DWORD(%%HASHK_PTR), DWORD(%%IA0)

        GCM_ENC_DEC_LAST \
                %%GDATA_KEY, %%GDATA_CTX, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, \
                %%DATA_OFFSET, %%LENGTH, %%CTR_BLOCKz, %%CTR_CHECK, \
                %%HASHK_PTR, ghashin_offset, %%SHUF_MASK, \
                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, %%ZTMP6, \
                %%ZTMP7, %%ZTMP8, %%ZTMP9, %%ZTMP10, %%ZTMP11, %%ZTMP12, %%ZTMP13, \
                %%ZTMP14, %%ZTMP15, %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, %%ZTMP20, \
                %%ZTMP21, %%ZTMP22, \
                %%ADDBE_4x4, %%ADDBE_1234, start, %%GL, %%GH, %%GM, \
                %%ENC_DEC, %%AAD_HASHz, %%IA0, %%IA5, %%MASKREG, %%INSTANCE_TYPE

%ifidn %%INSTANCE_TYPE, multi_call
        vpshufb         %%CTR_BLOCKx, %%CTR_BLOCKx, XWORD(%%SHUF_MASK)
%endif
        jmp             %%_ghash_done

%%_message_below_equal_16_blocks:
        ;; Determine how many blocks to process
        ;; - process one additional block if there is a partial block
        mov             DWORD(%%IA1), DWORD(%%LENGTH)
        add             DWORD(%%IA1), 15
        shr             DWORD(%%IA1), 4
        ;; %%IA1 can be in the range from 0 to 16

        GCM_ENC_DEC_SMALL \
                %%GDATA_KEY, %%GDATA_CTX, %%CIPH_PLAIN_OUT, %%PLAIN_CIPH_IN, \
                %%PLAIN_CIPH_LEN, %%ENC_DEC, %%DATA_OFFSET, \
                %%LENGTH, %%IA1, %%CTR_BLOCKx, %%AAD_HASHx, %%INSTANCE_TYPE, \
                %%ZTMP0,  %%ZTMP1,  %%ZTMP2,  %%ZTMP3,  \
                %%ZTMP4,  %%ZTMP5,  %%ZTMP6,  %%ZTMP7,  \
                %%ZTMP8,  %%ZTMP9,  %%ZTMP10, %%ZTMP11, \
                %%ZTMP12, %%ZTMP13, %%ZTMP14, %%ZTMP15, \
                %%ZTMP16, %%ZTMP17, %%ZTMP18, %%ZTMP19, \
                %%ZTMP20, %%ZTMP21, %%ZTMP22, \
                %%IA0, %%IA3, %%MASKREG, %%SHUF_MASK

        ;; fall through to exit

%%_ghash_done:
%ifidn %%INSTANCE_TYPE, multi_call
        ;; save the last counter block
        vmovdqu64       [%%GDATA_CTX + CurCount], %%CTR_BLOCKx
%endif
        vmovdqu64       [%%GDATA_CTX + AadHash], %%AAD_HASHx
%%_enc_dec_done:

%endmacro                       ; GCM_ENC_DEC

;;; ===========================================================================
;;; ===========================================================================
;;; Encrypt/decrypt the initial 16 blocks
%macro INITIAL_BLOCKS_16 22
%define %%IN            %1      ; [in] input buffer
%define %%OUT           %2      ; [in] output buffer
%define %%KP            %3      ; [in] pointer to expanded keys
%define %%DATA_OFFSET   %4      ; [in] data offset
%define %%GHASH         %5      ; [in] ZMM with AAD (low 128 bits)
%define %%CTR           %6      ; [in] ZMM with CTR BE blocks 4x128 bits
%define %%CTR_CHECK     %7      ; [in/out] GPR with counter overflow check
%define %%ADDBE_4x4     %8      ; [in] ZMM 4x128bits with value 4 (big endian)
%define %%ADDBE_1234    %9      ; [in] ZMM 4x128bits with values 1, 2, 3 & 4 (big endian)
%define %%T0            %10     ; [clobered] temporary ZMM register
%define %%T1            %11     ; [clobered] temporary ZMM register
%define %%T2            %12     ; [clobered] temporary ZMM register
%define %%T3            %13     ; [clobered] temporary ZMM register
%define %%T4            %14     ; [clobered] temporary ZMM register
%define %%T5            %15     ; [clobered] temporary ZMM register
%define %%T6            %16     ; [clobered] temporary ZMM register
%define %%T7            %17     ; [clobered] temporary ZMM register
%define %%T8            %18     ; [clobered] temporary ZMM register
%define %%SHUF_MASK     %19     ; [in] ZMM with BE/LE shuffle mask
%define %%ENC_DEC       %20     ; [in] ENC (encrypt) or DEC (decrypt) selector
%define %%BLK_OFFSET    %21     ; [in] stack frame offset to ciphered blocks
%define %%DATA_DISPL    %22     ; [in] fixed numerical data displacement/offset

%define %%B00_03        %%T5
%define %%B04_07        %%T6
%define %%B08_11        %%T7
%define %%B12_15        %%T8

%assign stack_offset (%%BLK_OFFSET)

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; prepare counter blocks

        cmp             BYTE(%%CTR_CHECK), (256 - 16)
        jae             %%_next_16_overflow
        vpaddd          %%B00_03, %%CTR, %%ADDBE_1234
        vpaddd          %%B04_07, %%B00_03, %%ADDBE_4x4
        vpaddd          %%B08_11, %%B04_07, %%ADDBE_4x4
        vpaddd          %%B12_15, %%B08_11, %%ADDBE_4x4
        jmp             %%_next_16_ok
%%_next_16_overflow:
        vpshufb         %%CTR, %%CTR, %%SHUF_MASK
        vmovdqa64       %%B12_15, [rel ddq_add_4444]
        vpaddd          %%B00_03, %%CTR, [rel ddq_add_1234]
        vpaddd          %%B04_07, %%B00_03, %%B12_15
        vpaddd          %%B08_11, %%B04_07, %%B12_15
        vpaddd          %%B12_15, %%B08_11, %%B12_15
        vpshufb         %%B00_03, %%SHUF_MASK
        vpshufb         %%B04_07, %%SHUF_MASK
        vpshufb         %%B08_11, %%SHUF_MASK
        vpshufb         %%B12_15, %%SHUF_MASK
%%_next_16_ok:
        vshufi64x2      %%CTR, %%B12_15, %%B12_15, 1111_1111b
        add             BYTE(%%CTR_CHECK), 16

        ;; === load 16 blocks of data
        VX512LDR        %%T0, [%%IN + %%DATA_OFFSET + %%DATA_DISPL + (64*0)]
        VX512LDR        %%T1, [%%IN + %%DATA_OFFSET + %%DATA_DISPL + (64*1)]
        VX512LDR        %%T2, [%%IN + %%DATA_OFFSET + %%DATA_DISPL + (64*2)]
        VX512LDR        %%T3, [%%IN + %%DATA_OFFSET + %%DATA_DISPL + (64*3)]

        ;; move to AES encryption rounds
%assign i 0
        vbroadcastf64x2 %%T4, [%%KP + (16*i)]
        vpxorq          %%B00_03, %%B00_03, %%T4
        vpxorq          %%B04_07, %%B04_07, %%T4
        vpxorq          %%B08_11, %%B08_11, %%T4
        vpxorq          %%B12_15, %%B12_15, %%T4
%assign i (i + 1)

%rep NROUNDS
        vbroadcastf64x2 %%T4, [%%KP + (16*i)]
        vaesenc         %%B00_03, %%B00_03, %%T4
        vaesenc         %%B04_07, %%B04_07, %%T4
        vaesenc         %%B08_11, %%B08_11, %%T4
        vaesenc         %%B12_15, %%B12_15, %%T4
%assign i (i + 1)
%endrep

        vbroadcastf64x2 %%T4, [%%KP + (16*i)]
        vaesenclast     %%B00_03, %%B00_03, %%T4
        vaesenclast     %%B04_07, %%B04_07, %%T4
        vaesenclast     %%B08_11, %%B08_11, %%T4
        vaesenclast     %%B12_15, %%B12_15, %%T4

        ;;  xor against text
        vpxorq          %%B00_03, %%B00_03, %%T0
        vpxorq          %%B04_07, %%B04_07, %%T1
        vpxorq          %%B08_11, %%B08_11, %%T2
        vpxorq          %%B12_15, %%B12_15, %%T3

        ;; store
        VX512STR        [%%OUT + %%DATA_OFFSET + %%DATA_DISPL + (64*0)], %%B00_03
        VX512STR        [%%OUT + %%DATA_OFFSET + %%DATA_DISPL + (64*1)], %%B04_07
        VX512STR        [%%OUT + %%DATA_OFFSET + %%DATA_DISPL + (64*2)], %%B08_11
        VX512STR        [%%OUT + %%DATA_OFFSET + %%DATA_DISPL + (64*3)], %%B12_15

%ifidn  %%ENC_DEC, DEC
        ;; decryption - cipher text needs to go to GHASH phase
        vpshufb         %%B00_03, %%T0, %%SHUF_MASK
        vpshufb         %%B04_07, %%T1, %%SHUF_MASK
        vpshufb         %%B08_11, %%T2, %%SHUF_MASK
        vpshufb         %%B12_15, %%T3, %%SHUF_MASK
%else
        ;; encryption
        vpshufb         %%B00_03, %%B00_03, %%SHUF_MASK
        vpshufb         %%B04_07, %%B04_07, %%SHUF_MASK
        vpshufb         %%B08_11, %%B08_11, %%SHUF_MASK
        vpshufb         %%B12_15, %%B12_15, %%SHUF_MASK
%endif

%ifnidn %%GHASH, no_ghash
        ;; === xor cipher block 0 with GHASH for the next GHASH round
        vpxorq          %%B00_03, %%B00_03, %%GHASH
%endif

        vmovdqa64       [rsp + stack_offset + (0 * 64)], %%B00_03
        vmovdqa64       [rsp + stack_offset + (1 * 64)], %%B04_07
        vmovdqa64       [rsp + stack_offset + (2 * 64)], %%B08_11
        vmovdqa64       [rsp + stack_offset + (3 * 64)], %%B12_15
%endmacro                       ;INITIAL_BLOCKS_16

;;; ===========================================================================

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; GCM_COMPLETE Finishes Encryption/Decryption of last partial block after GCM_UPDATE finishes.
; Input: A gcm_key_data * (GDATA_KEY), gcm_context_data (GDATA_CTX).
; Output: Authorization Tag (AUTH_TAG) and Authorization Tag length (AUTH_TAG_LEN)
; Clobbers xmm0-xmm2, xmm5-xmm6, xmm9-xmm11, xmm13-xmm15
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro  GCM_COMPLETE            9
%define %%GDATA_KEY             %1      ; [in] GP with pointer to key structure
%define %%GDATA_CTX             %2      ; [in] GP with pointer to context structure
%define %%AUTH_TAG              %3      ; [in] pointer to store auth tag into (GP or mem)
%define %%AUTH_TAG_LEN          %4      ; [in] length in bytes of auth tag (GP or mem)
%define %%INSTANCE_TYPE         %5      ; [in] instance type "single_call" vs "multi_call"
%define %%MASKREG               %6      ; [clobbered] temporary K register
%define %%IA0                   %7      ; [clobbered] temporary GP
%define %%IA1                   %8      ; [clobbered] temporary GP
%define %%IA2                   %9      ; [clobbered] temporary GP

        ;; @note: xmm14 is hardcoded for hash input in singe_call case

        vmovdqu         xmm13, [%%GDATA_KEY + HashKey]
        ;; Start AES as early as possible
        vmovdqu         xmm9, [%%GDATA_CTX + OrigIV]    ; xmm9 = Y0
        ENCRYPT_SINGLE_BLOCK %%GDATA_KEY, xmm9  ; E(K, Y0)

%ifidn %%INSTANCE_TYPE, multi_call
        ;; If the GCM function is called as a single function call rather
        ;; than invoking the individual parts (init, update, finalize) we
        ;; can remove a write to read dependency on AadHash.
        vmovdqu         xmm14, [%%GDATA_CTX + AadHash]

        ;; Encrypt of the final partial block was already done in the main GCM_ENC_DEC macro.
        ;; It may be required to GHASH it now
        cmp             qword [%%GDATA_CTX + PBlockLen], 0
        je              %%_partial_done

        ;; GHASH computation for the last <16 Byte block
        GHASH_MUL       xmm14, xmm13, xmm0, xmm10, xmm11, xmm5, xmm6

%%_partial_done:
%endif
        vmovq           xmm15, [%%GDATA_CTX + InLen]
        vpinsrq         xmm15, [%%GDATA_CTX + AadLen], 1        ; xmm15 = len(A)||len(C)
        vpsllq          xmm15, xmm15, 3                         ; convert bytes into bits

        vpxor           xmm14, xmm15
        GHASH_MUL       xmm14, xmm13, xmm0, xmm10, xmm11, xmm5, xmm6
        vpshufb         xmm14, [rel SHUF_MASK]         ; perform a 16Byte swap

        vpxor           xmm9, xmm9, xmm14

        ;; xmm9 includes the final TAG
        mov             %%IA0, %%AUTH_TAG             ; r10 = authTag
        mov             %%IA1, %%AUTH_TAG_LEN         ; r11 = auth_tag_len

        cmp             %%IA1, 16
        je              %%_T_16

        cmp             %%IA1, 12
        je              %%_T_12

        cmp             %%IA1, 8
        je              %%_T_8

        lea             %%IA2, [rel byte64_len_to_mask_table]
        kmovq           %%MASKREG, [%%IA2 + %%IA1*8]
        vmovdqu8        [%%IA0]{%%MASKREG}, xmm9
        jmp             %%_return_T_done
%%_T_8:
        vmovq           [%%IA0], xmm9
        jmp             %%_return_T_done
%%_T_12:
        vmovq           [%%IA0], xmm9
        vpextrd         [%%IA0 + 8], xmm9, 2
        jmp             %%_return_T_done
%%_T_16:
        vmovdqu         [%%IA0], xmm9

%%_return_T_done:

%ifdef SAFE_DATA
        ;; Clear sensitive data from context structure
        vpxor           xmm0, xmm0
        vmovdqu         [%%GDATA_CTX + AadHash], xmm0
%ifidn %%INSTANCE_TYPE, multi_call
        vmovdqu         [%%GDATA_CTX + PBlockEncKey], xmm0
%endif
%endif
%endmacro ; GCM_COMPLETE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; PARTIAL_BLOCK_GMAC
;;; Handles the tag partial blocks between update calls.
;;; Requires the input data be at least 1 byte long.
;;; Output:
;;; Updated AAD_HASH, DATA_OFFSET and GDATA_CTX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro PARTIAL_BLOCK_GMAC 20
%define %%GDATA_KEY             %1  ; [in] Key pointer
%define %%GDATA_CTX             %2  ; [in] context pointer
%define %%PLAIN_IN              %3  ; [in] input buffer
%define %%PLAIN_LEN             %4  ; [in] buffer length
%define %%DATA_OFFSET           %5  ; [out] data offset
%define %%AAD_HASH              %6  ; [out] updated GHASH value
%define %%GPTMP0                %7  ; [clobbered] GP temporary register
%define %%GPTMP1                %8  ; [clobbered] GP temporary register
%define %%GPTMP2                %9  ; [clobbered] GP temporary register
%define %%ZTMP0                 %10 ; [clobbered] ZMM temporary register
%define %%ZTMP1                 %11 ; [clobbered] ZMM temporary register
%define %%ZTMP2                 %12 ; [clobbered] ZMM temporary register
%define %%ZTMP3                 %13 ; [clobbered] ZMM temporary register
%define %%ZTMP4                 %14 ; [clobbered] ZMM temporary register
%define %%ZTMP5                 %15 ; [clobbered] ZMM temporary register
%define %%ZTMP6                 %16 ; [clobbered] ZMM temporary register
%define %%ZTMP7                 %17 ; [clobbered] ZMM temporary register
%define %%ZTMP8                 %18 ; [clobbered] ZMM temporary register
%define %%ZTMP9                 %19 ; [clobbered] ZMM temporary register
%define %%MASKREG               %20 ; [clobbered] mask temporary register

%define %%XTMP0 XWORD(%%ZTMP0)
%define %%XTMP1 XWORD(%%ZTMP1)
%define %%XTMP2 XWORD(%%ZTMP2)
%define %%XTMP3 XWORD(%%ZTMP3)
%define %%XTMP4 XWORD(%%ZTMP4)
%define %%XTMP5 XWORD(%%ZTMP5)
%define %%XTMP6 XWORD(%%ZTMP6)
%define %%XTMP7 XWORD(%%ZTMP7)
%define %%XTMP8 XWORD(%%ZTMP8)
%define %%XTMP9 XWORD(%%ZTMP9)

%define %%LENGTH        %%GPTMP0
%define %%IA0           %%GPTMP1
%define %%IA1           %%GPTMP2

        mov             %%LENGTH, [%%GDATA_CTX + PBlockLen]
        or              %%LENGTH, %%LENGTH
        je              %%_partial_block_done           ;Leave Macro if no partial blocks

        READ_SMALL_DATA_INPUT   %%XTMP4, %%PLAIN_IN, %%PLAIN_LEN, %%IA0, %%IA1, %%MASKREG

        vmovdqu64       %%XTMP2, [%%GDATA_KEY + HashKey]

        ;; adjust the shuffle mask pointer to be able to shift right %%LENGTH bytes
        ;; (16 - %%LENGTH) is the number of bytes in plaintext mod 16)
        lea             %%IA0, [rel SHIFT_MASK]
        add             %%IA0, %%LENGTH
        vmovdqu64       %%XTMP3, [%%IA0]   ; shift right shuffle mask

        ;; Determine if partial block is not being filled and shift mask accordingly
        mov             %%IA1, %%PLAIN_LEN
        add             %%IA1, %%LENGTH
        sub             %%IA1, 16
        jge             %%_no_extra_mask
        sub             %%IA0, %%IA1
%%_no_extra_mask:
        ;; get the appropriate mask to mask out bottom %%LENGTH bytes of %%XTMP1
        ;; - mask out bottom %%LENGTH bytes of %%XTMP1
        vmovdqu64       %%XTMP0, [%%IA0 + ALL_F - SHIFT_MASK]

        vpand           %%XTMP4, %%XTMP0
        vpshufb         %%XTMP4, [rel SHUF_MASK]
        vpshufb         %%XTMP4, %%XTMP3
        vpxorq          %%AAD_HASH, %%XTMP4
        cmp             %%IA1, 0
        jl              %%_partial_incomplete

        ;; GHASH computation for the last <16 Byte block
        GHASH_MUL       %%AAD_HASH, %%XTMP2, %%XTMP5, %%XTMP6, %%XTMP7, %%XTMP8, %%XTMP9

        mov             qword [%%GDATA_CTX + PBlockLen], 0

        ;;  Set %%LENGTH to be the number of bytes to skip after this macro
        mov             %%IA0, %%LENGTH
        mov             %%LENGTH, 16
        sub             %%LENGTH, %%IA0
        jmp             %%_ghash_done

%%_partial_incomplete:
%ifidn __OUTPUT_FORMAT__, win64
        mov             %%IA0, %%PLAIN_LEN
        add             [%%GDATA_CTX + PBlockLen], %%IA0
%else
        add             [%%GDATA_CTX + PBlockLen], %%PLAIN_LEN
%endif
        mov             %%LENGTH, %%PLAIN_LEN

%%_ghash_done:
        vmovdqu64       [%%GDATA_CTX + AadHash], %%AAD_HASH

        mov             %%DATA_OFFSET, %%LENGTH
%%_partial_block_done:
%endmacro ; PARTIAL_BLOCK_GMAC

%endif  ; GCM_VAES_AVX512_INC

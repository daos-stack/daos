;
;; Copyright (c) 2017-2021, Intel Corporation
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

;; Authors:
;;   Shay Gueron (1, 2), Regev Shemy (2), Tomasz kantecki (2)
;;   (1) University of Haifa, Israel
;;   (2) Intel Corporation

;; In System V AMD64 ABI
;;	callee saves: RBX, RBP, R12-R15
;; Windows x64 ABI
;;	callee saves: RBX, RBP, RDI, RSI, RSP, R12-R15

;;
;; Registers:		RAX RBX RCX RDX RBP RSI RDI R8  R9  R10 R11 R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Windows clobbers:	RAX                         R8  R9  R10 R11
;; Windows preserves:	    RBX RCX RDX RBP RSI RDI                 R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Linux clobbers:	RAX     RCX RDX                     R10 R11
;; Linux preserves:	    RBX         RBP RSI RDI R8  R9          R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Clobbers ZMM0-31 and K1 to K7

%include "include/os.asm"
%include "include/reg_sizes.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/constants.asm"
;%define DO_DBGPRINT
%include "include/dbgprint.asm"
%include "include/clear_regs.asm"
%include "include/cet.inc"
%ifdef LINUX
%define arg1	rdi
%define arg2	rsi
%define arg3	rdx
%define arg4	rcx
%else
%define arg1	rcx
%define arg2	rdx
%define arg3	r8
%define arg4	r9
%endif

%define STATE	arg1
%define SIZE	arg2

%define OFFSET  rax

%define IA0     arg3
%define IA1     arg4
%define IA2     r10

%define INP0	r11
%define INP1	r12
%define INP2	r13
%define INP3	r14
%define INP4	r15

%define KSOFFSET r11

%define ZW0     zmm0
%define ZW1     zmm1
%define ZW2     zmm2
%define ZW3     zmm3
%define ZW4     zmm4
%define ZW5     zmm5
%define ZW6     zmm6
%define ZW7     zmm7
%define ZW8     zmm8
%define ZW9     zmm9
%define ZW10    zmm10
%define ZW11    zmm11
%define ZW12    zmm12
%define ZW13    zmm13
%define ZW14    zmm14
%define ZW15    zmm15

%define ZIV0    zmm16
%define ZIV1    zmm17

%define ZTMP0   zmm18
%define ZTMP1   zmm19
%define ZTMP2   zmm20
%define ZTMP3   zmm21
%define ZTMP4   zmm22
%define ZTMP5   zmm23
%define ZTMP6   zmm24
%define ZTMP7   zmm25
%define ZTMP8   zmm26
%define ZTMP9   zmm27
%define ZTMP10  zmm28
%define ZTMP11  zmm29
%define ZTMP12  zmm30
%define ZTMP13  zmm31

struc STACKFRAME
_key_sched:	resq	16*16   ; 16 lanes x 16 qwords; 16 x 128 bytes = 2048
_key_sched2:	resq	16*16   ; 16 lanes x 16 qwords; 16 x 128 bytes = 2048
_key_sched3:	resq	16*16   ; 16 lanes x 16 qwords; 16 x 128 bytes = 2048
_tmp_iv:	resq	16      ; 2 x 64 bytes
_tmp_in:	resq	16      ; 2 x 64 bytes
_tmp_out:	resq	16      ; 2 x 64 bytes
_tmp_mask:	resd	16      ; 1 x 64 bytes
_gpr_save:	resq	4       ; r12 to r15
_rsp_save:	resq	1
_mask_save:	resq	1
_size_save:	resq	1
endstruc

;;; ===========================================================================
;;; ===========================================================================
;;; MACROS
;;; ===========================================================================
;;; ===========================================================================

;;; ===========================================================================
;;; CLEAR TRANSPOSED KEY SCHEDULE (if SAFE_DATA is selected)
;;; ===========================================================================
%macro CLEAR_KEY_SCHEDULE 2
%define %%ALG   %1      ; [in] DES or 3DES
%define %%ZT    %2      ; [clobbered] temporary ZMM register

%ifdef SAFE_DATA
        vpxorq  %%ZT, %%ZT
%assign rep_num (2048 / 64)
%ifidn %%ALG, 3DES
%assign rep_num (rep_num * 3)
%endif

%assign offset 0
%rep rep_num
        vmovdqa64       [rsp + _key_sched + offset], %%ZT
%assign offset (offset + 64)
%endrep

%endif ; SAFE_DATA

%endmacro

;;; ===========================================================================
;;; PERMUTE
;;; ===========================================================================
;;; A [in/out] - zmm register
;;; B [in/out] - zmm register
;;; NSHIFT [in] - constant to shift words by
;;; MASK [in] - zmm or m512 with mask
;;; T0 [clobbered] - temporary zmm register
%macro PERMUTE 5
%define %%A %1
%define %%B %2
%define %%NSHIFT %3
%define %%MASK %4
%define %%T0 %5

        vpsrld  %%T0, %%A, %%NSHIFT
        vpxord  %%T0, %%T0, %%B
        vpandd  %%T0, %%T0, %%MASK
        vpxord  %%B, %%B, %%T0
        vpslld  %%T0, %%T0, %%NSHIFT
        vpxord  %%A, %%A, %%T0
%endmacro

;;; ===========================================================================
;;; INITIAL PERMUTATION
;;; ===========================================================================
;;; L [in/out] - zmm register
;;; R [in/out] - zmm register
;;; T0 [clobbered] - temporary zmm register
%macro IP_Z 3
%define %%L %1
%define %%R %2
%define %%T0 %3
        PERMUTE %%R, %%L, 4,  [rel init_perm_consts + 0*64], %%T0
        PERMUTE %%L, %%R, 16, [rel init_perm_consts + 1*64], %%T0
        PERMUTE %%R, %%L, 2,  [rel init_perm_consts + 2*64], %%T0
        PERMUTE %%L, %%R, 8,  [rel init_perm_consts + 3*64], %%T0
        PERMUTE %%R, %%L, 1,  [rel init_perm_consts + 4*64], %%T0
%endmacro

;;; ===========================================================================
;;; FINAL PERMUTATION
;;; ===========================================================================
;;; L [in/out] - zmm register
;;; R [in/out] - zmm register
;;; T0 [clobbered] - temporary zmm register
%macro FP_Z 3
%define %%L %1
%define %%R %2
%define %%T0 %3
        PERMUTE %%L, %%R, 1,  [rel init_perm_consts + 4*64], %%T0
        PERMUTE %%R, %%L, 8,  [rel init_perm_consts + 3*64], %%T0
        PERMUTE %%L, %%R, 2,  [rel init_perm_consts + 2*64], %%T0
        PERMUTE %%R, %%L, 16, [rel init_perm_consts + 1*64], %%T0
        PERMUTE %%L, %%R, 4,  [rel init_perm_consts + 0*64], %%T0
%endmacro

;;; ===========================================================================
;;; P PHASE
;;; ===========================================================================
;;; W0 [in/out] - zmm register
;;;               in: vector of 16 x 32bits from S phase
;;;               out: permuted in vector
;;; T0-T3 [clobbered] - temporary zmm register
%macro P_PHASE 5
%define %%W0 %1
%define %%T0 %2
%define %%T1 %3
%define %%T2 %4
%define %%T3 %5

        vprord  %%T0, %%W0, 3
	vpandd  %%T0, %%T0, [rel mask_values + 0*64]
        vprord  %%T1, %%W0, 5
	vpandd  %%T1, %%T1, [rel mask_values + 1*64]
	vpord   %%T0, %%T0, %%T1

        vprord  %%T1, %%W0, 24
	vpandd  %%T1, %%T1, [rel mask_values + 2*64]
        vprord  %%T2, %%W0, 26
	vpandd  %%T2, %%T2, [rel mask_values + 3*64]
	vpord   %%T1, %%T1, %%T2
	vpord   %%T0, %%T0, %%T1

        vprord  %%T1, %%W0, 15
	vpandd  %%T1, %%T1, [rel mask_values + 4*64]
        vprord  %%T2, %%W0, 17
	vpandd  %%T2, %%T2, [rel mask_values + 5*64]
	vpord   %%T1, %%T1, %%T2

        vprord  %%T2, %%W0, 6
	vpandd  %%T2, %%T2, [rel mask_values + 6*64]
        vprord  %%T3, %%W0, 21
	vpandd  %%T3, %%T3, [rel mask_values + 7*64]
	vpord   %%T2, %%T2, %%T3
	vpord   %%T1, %%T1, %%T2
	vpord   %%T0, %%T0, %%T1

        vprord  %%T1, %%W0, 12
	vpandd  %%T1, %%T1, [rel mask_values + 8*64]
        vprord  %%T2, %%W0, 14
	vpandd  %%T2, %%T2, [rel mask_values + 9*64]
	vpord   %%T1, %%T1, %%T2

        vprord  %%T2, %%W0, 4
	vpandd  %%T2, %%T2, [rel mask_values + 10*64]
        vprord  %%T3, %%W0, 11
	vpandd  %%T3, %%T3, [rel mask_values + 11*64]
	vpord   %%T2, %%T2, %%T3
	vpord   %%T1, %%T1, %%T2
	vpord   %%T0, %%T0, %%T1

        vprord  %%T1, %%W0, 16
	vpandd  %%T1, %%T1, [rel mask_values + 12*64]
        vprord  %%T2, %%W0, 22
	vpandd  %%T2, %%T2, [rel mask_values + 13*64]
	vpord   %%T1, %%T1, %%T2

        vprord  %%T2, %%W0, 19
	vpandd  %%T2, %%T2, [rel mask_values + 14*64]
        vprord  %%T3, %%W0, 10
	vpandd  %%T3, %%T3, [rel mask_values + 15*64]
	vpord   %%T2, %%T2, %%T3
	vpord   %%T1, %%T1, %%T2
	vpord   %%T0, %%T0, %%T1

        vprord  %%T1, %%W0, 9
	vpandd  %%T1, %%T1, [rel mask_values + 16*64]
        vprord  %%T2, %%W0, 13
	vpandd  %%T2, %%T2, [rel mask_values + 17*64]
	vpord   %%T1, %%T1, %%T2

        vprord  %%T2, %%W0, 25
	vpandd  %%T2, %%T2, [rel mask_values + 18*64]
	vpord   %%T1, %%T1, %%T2
	vpord   %%W0, %%T0, %%T1
%endmacro

;;; ===========================================================================
;;; E PHASE
;;; ===========================================================================
;;;
;;; Expands 16x32-bit words into 16x48-bit words
;;; plus XOR's result with the key schedule.
;;; The output is adjusted to be friendly as S phase input.
;;;
;;; in [in] - zmm register
;;; out0a [out] - zmm register
;;; out0b [out] - zmm register
;;; out1a [out] - zmm register
;;; out1b [out] - zmm register
;;; k0 [in] - key schedule; zmm or m512
;;; k1 [in] - key schedule; zmm or m512
;;; t0-t1 [clobbered] - temporary zmm register
%macro E_PHASE 9
%define %%IN %1
%define %%OUT0A %2
%define %%OUT0B %3
%define %%OUT1A %4
%define %%OUT1B %5
%define %%K0 %6
%define %%K1 %7
%define %%T0 %8
%define %%T1 %9

	vprord          %%T0, %%IN, 31
	vprord          %%T1, %%IN, 3
	vpshufb         %%T0, %%T0, [rel idx_e]
	vpshufb         %%T1, %%T1, [rel idx_e]
	vpunpcklbw      %%OUT0A, %%T0, %%T1
	vpunpckhbw      %%OUT1A, %%T0, %%T1
	vpxord          %%OUT0A, %%OUT0A, %%K0
	vpxord          %%OUT1A, %%OUT1A, %%K1
	vpandd          %%OUT0B, %%OUT0A, [rel and_eu]
	vpsrlw          %%OUT0B, %%OUT0B, 8
	vpandd          %%OUT0A, %%OUT0A, [rel and_ed]
	vpandd          %%OUT1B, %%OUT1A, [rel and_eu]
	vpsrlw          %%OUT1B, %%OUT1B, 8
	vpandd          %%OUT1A, %%OUT1A, [rel and_ed]
%endmacro

;;; ===========================================================================
;;; S-BOX
;;; ===========================================================================
;;;
;;; NOTE: clobbers k1-k6 OpMask registers
;;;
;;; IN0A [in] - zmm register; output from E-phase
;;; IN0B [in] - zmm register; output from E-phase
;;; IN1A [in] - zmm register; output from E-phase
;;; IN1B [in] - zmm register; output from E-phase
;;; OUT [out] - zmm register; output from E-phase
;;; T0-T5 [clobbered] - temporary zmm register
%macro S_PHASE 11
%define %%IN0A %1
%define %%IN0B %2
%define %%IN1A %3
%define %%IN1B %4
%define %%OUT %5
%define %%T0 %6
%define %%T1 %7
%define %%T2 %8
%define %%T3 %9
%define %%T4 %10
%define %%T5 %11

	vmovdqa64       %%T0, [rel reg_values16bit_7]
	vpcmpuw         k3, %%IN0A, %%T0, 2 ; 2 -> LE
	vpcmpuw         k4, %%IN0B, %%T0, 2 ; 2 -> LE
	vpcmpuw         k5, %%IN1A, %%T0, 2 ; 2 -> LE
	vpcmpuw         k6, %%IN1B, %%T0, 2 ; 2 -> LE

	mov             DWORD(IA0), 0x55555555
	kmovd           k1, DWORD(IA0)
	mov             DWORD(IA0), 0xaaaaaaaa
	kmovd           k2, DWORD(IA0)

        vmovdqa64       %%T0, [rel S_box_flipped + 0*64]
        vmovdqa64       %%T1, [rel S_box_flipped + 1*64]
        vmovdqa64       %%T2, [rel S_box_flipped + 4*64]
        vmovdqa64       %%T3, [rel S_box_flipped + 5*64]
	vpermw          %%T0{k1}{z}, %%IN0A, %%T0
	vpermw          %%T1{k1}{z}, %%IN0A, %%T1
	vpermw          %%T2{k2}{z}, %%IN0A, %%T2
	vpermw          %%T3{k2}{z}, %%IN0A, %%T3
	vpxord          %%T0, %%T0, %%T2
	vpxord          %%OUT, %%T1, %%T3
        vmovdqu16       %%OUT{k3}, %%T0

        vmovdqa64       %%T0, [rel S_box_flipped + 2*64]
        vmovdqa64       %%T1, [rel S_box_flipped + 3*64]
        vmovdqa64       %%T2, [rel S_box_flipped + 6*64]
        vmovdqa64       %%T3, [rel S_box_flipped + 7*64]
	vpermw          %%T0{k1}{z}, %%IN0B, %%T0
	vpermw          %%T1{k1}{z}, %%IN0B, %%T1
	vpermw          %%T2{k2}{z}, %%IN0B, %%T2
	vpermw          %%T3{k2}{z}, %%IN0B, %%T3
	vpxord          %%T0, %%T0, %%T2
	vpxord          %%T3, %%T1, %%T3
        vmovdqu16       %%T3{k4}, %%T0
	vpsllw          %%T3, %%T3, 4
	vpxord          %%OUT, %%OUT, %%T3

        vmovdqa64       %%T0, [rel S_box_flipped + 8*64]
        vmovdqa64       %%T1, [rel S_box_flipped + 9*64]
        vmovdqa64       %%T2, [rel S_box_flipped + 12*64]
        vmovdqa64       %%T3, [rel S_box_flipped + 13*64]
	vpermw          %%T0{k1}{z}, %%IN1A, %%T0
	vpermw          %%T1{k1}{z}, %%IN1A, %%T1
	vpermw          %%T2{k2}{z}, %%IN1A, %%T2
	vpermw          %%T3{k2}{z}, %%IN1A, %%T3
	vpxord          %%T0, %%T0, %%T2
	vpxord          %%T4, %%T1, %%T3
        vmovdqu16       %%T4{k5}, %%T0

        vmovdqa64       %%T0, [rel S_box_flipped + 10*64]
        vmovdqa64       %%T1, [rel S_box_flipped + 11*64]
        vmovdqa64       %%T2, [rel S_box_flipped + 14*64]
        vmovdqa64       %%T3, [rel S_box_flipped + 15*64]
	vpermw          %%T0{k1}{z}, %%IN1B, %%T0
	vpermw          %%T1{k1}{z}, %%IN1B, %%T1
	vpermw          %%T2{k2}{z}, %%IN1B, %%T2
	vpermw          %%T3{k2}{z}, %%IN1B, %%T3
	vpxord          %%T0, %%T0, %%T2
	vpxord          %%T5, %%T1, %%T3
        vmovdqu16       %%T5{k6}, %%T0
	vpsllw          %%T5, %%T5, 4

	vpxord          %%T4, %%T4, %%T5
	vpsllw          %%T4, %%T4, 8
	vpxord          %%OUT, %%OUT, %%T4
        vpshufb         %%OUT, %%OUT, [rel shuffle_reg]
%endmacro

;;; ===========================================================================
;;; DES encryption/decryption round
;;; ===========================================================================
;;;
;;; Clobbers k1-k6 OpMask registers
;;;
;;; ENC_DEC [in] - ENC for encryption, DEC for decryption
;;; R [in/out] - zmm register; plain text in & cipher text out
;;; L [in/out] - zmm register; plain text in & cipher text out
;;; KS [in] - pointer to the key schedule
;;; T0-T11 [clobbered] - temporary zmm register
%macro DES_ENC_DEC 16
%define %%ENC_DEC %1
%define %%R %2
%define %%L %3
%define %%KS %4
%define %%T0 %5
%define %%T1 %6
%define %%T2 %7
%define %%T3 %8
%define %%T4 %9
%define %%T5 %10
%define %%T6 %11
%define %%T7 %12
%define %%T8 %13
%define %%T9 %14
%define %%T10 %15
%define %%T11 %16

        IP_Z    %%R, %%L, %%T0

%ifidn  %%ENC_DEC, ENC
        ;; ENCRYPTION
        xor     KSOFFSET, KSOFFSET
%%_des_enc_loop:
        E_PHASE %%R, %%T1, %%T2, %%T3, %%T4, [%%KS + KSOFFSET + (0*64)], [%%KS + KSOFFSET + (1*64)], %%T6, %%T7
        S_PHASE %%T1, %%T2, %%T3, %%T4, %%T0, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11
        P_PHASE %%T0, %%T1, %%T2, %%T3, %%T4
        vpxord  %%L, %%L, %%T0

        E_PHASE %%L, %%T1, %%T2, %%T3, %%T4, [%%KS + KSOFFSET + (2*64)], [%%KS + KSOFFSET + (3*64)], %%T6, %%T7
        S_PHASE %%T1, %%T2, %%T3, %%T4, %%T0, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11
        P_PHASE %%T0, %%T1, %%T2, %%T3, %%T4
        vpxord  %%R, %%R, %%T0

        add     KSOFFSET, (4*64)
        cmp     KSOFFSET, (8*(4*64))
        jb      %%_des_enc_loop

%else
        ;; DECRYPTION
        mov     KSOFFSET, (8*(4*64))
%%_des_dec_loop:
        E_PHASE %%R, %%T1, %%T2, %%T3, %%T4, [%%KS + KSOFFSET - (2*64)], [%%KS + KSOFFSET - (1*64)], %%T6, %%T7
        S_PHASE %%T1, %%T2, %%T3, %%T4, %%T0, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11
        P_PHASE %%T0, %%T1, %%T2, %%T3, %%T4
        vpxord  %%L, %%L, %%T0

        E_PHASE %%L, %%T1, %%T2, %%T3, %%T4, [%%KS + KSOFFSET - (4*64)], [%%KS + KSOFFSET - (3*64)], %%T6, %%T7
        S_PHASE %%T1, %%T2, %%T3, %%T4, %%T0, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11
        P_PHASE %%T0, %%T1, %%T2, %%T3, %%T4
        vpxord  %%R, %%R, %%T0
        sub     KSOFFSET, (4*64)
        jnz     %%_des_dec_loop
%endif                          ; DECRYPTION

        FP_Z    %%R, %%L, %%T0
%endmacro

;;; ===========================================================================
;;; DATA TRANSPOSITION AT DATA INPUT
;;; ===========================================================================
;;;
;;; IN00 - IN15 [in/out]:
;;;          in:  IN00 - lane 0 data, IN01 - lane 1 data, ... IN15 - lane 15 data
;;;          out: R0 - 16 x word0, L0 - 16 x word1, ... L7 - 16 x word15
;;; T0-T3 [clobbered] - temporary zmm registers
;;; K0-K5 [clobbered] - temporary zmm registers
;;; H0-H3 [clobbered] - temporary zmm registers
%macro TRANSPOSE_IN 30
%define %%IN00 %1               ; R0
%define %%IN01 %2               ; L0
%define %%IN02 %3               ; R1
%define %%IN03 %4               ; L1
%define %%IN04 %5               ; R2
%define %%IN05 %6               ; L2
%define %%IN06 %7               ; R3
%define %%IN07 %8               ; L3
%define %%IN08 %9               ; R4
%define %%IN09 %10              ; L4
%define %%IN10 %11              ; R5
%define %%IN11 %12              ; L5
%define %%IN12 %13              ; R6
%define %%IN13 %14              ; L6
%define %%IN14 %15              ; R7
%define %%IN15 %16              ; L7
%define %%T0 %17
%define %%T1 %18
%define %%T2 %19
%define %%T3 %20
%define %%K0 %21
%define %%K1 %22
%define %%K2 %23
%define %%K3 %24
%define %%K4 %25
%define %%K5 %26
%define %%H0 %27
%define %%H1 %28
%define %%H2 %29
%define %%H3 %30

	vpunpckldq      %%K0, %%IN00, %%IN01
	vpunpckhdq      %%K1, %%IN00, %%IN01
	vpunpckldq      %%T0, %%IN02, %%IN03
	vpunpckhdq      %%T1, %%IN02, %%IN03

	vpunpckldq      %%IN00, %%IN04, %%IN05
	vpunpckhdq      %%IN01, %%IN04, %%IN05
	vpunpckldq      %%IN02, %%IN06, %%IN07
	vpunpckhdq      %%IN03, %%IN06, %%IN07

        vpunpcklqdq     %%K2, %%K0, %%T0
	vpunpckhqdq     %%T2, %%K0, %%T0
	vpunpcklqdq     %%K3, %%K1, %%T1
	vpunpckhqdq     %%T3, %%K1, %%T1

        vpunpcklqdq     %%K0, %%IN00, %%IN02
	vpunpckhqdq     %%K1, %%IN00, %%IN02
	vpunpcklqdq     %%T0, %%IN01, %%IN03
	vpunpckhqdq     %%T1, %%IN01, %%IN03

	vpunpckldq      %%K4, %%IN08, %%IN09
	vpunpckhdq      %%K5, %%IN08, %%IN09
	vpunpckldq      %%IN04, %%IN10, %%IN11
	vpunpckhdq      %%IN05, %%IN10, %%IN11
	vpunpckldq      %%IN06, %%IN12, %%IN13
	vpunpckhdq      %%IN07, %%IN12, %%IN13
	vpunpckldq      %%IN10, %%IN14, %%IN15
	vpunpckhdq      %%IN11, %%IN14, %%IN15

        vpunpcklqdq     %%IN12, %%K4, %%IN04
	vpunpckhqdq     %%IN13, %%K4, %%IN04
        vpunpcklqdq     %%IN14, %%K5, %%IN05
	vpunpckhqdq     %%IN15, %%K5, %%IN05
        vpunpcklqdq     %%IN00, %%IN06, %%IN10
	vpunpckhqdq     %%IN01, %%IN06, %%IN10
        vpunpcklqdq     %%IN02, %%IN07, %%IN11
	vpunpckhqdq     %%IN03, %%IN07, %%IN11

        vshufi64x2      %%H0, %%K2, %%K0, 0x44
        vshufi64x2      %%H1, %%K2, %%K0, 0xee
        vshufi64x2      %%H2, %%IN12, %%IN00, 0x44
        vshufi64x2      %%H3, %%IN12, %%IN00, 0xee
        vshufi64x2      %%IN00, %%H0, %%H2, 0x88    ; R0
        vshufi64x2      %%IN04, %%H0, %%H2, 0xdd    ; R2
        vshufi64x2      %%IN08, %%H1, %%H3, 0x88    ; R4
        vshufi64x2      %%IN12, %%H1, %%H3, 0xdd    ; R6

        vshufi64x2      %%H0, %%T2, %%K1, 0x44
        vshufi64x2      %%H1, %%T2, %%K1, 0xee
        vshufi64x2      %%H2, %%IN13, %%IN01, 0x44
        vshufi64x2      %%H3, %%IN13, %%IN01, 0xee
        vshufi64x2      %%IN01, %%H0, %%H2, 0x88    ; L0
        vshufi64x2      %%IN05, %%H0, %%H2, 0xdd    ; L2
        vshufi64x2      %%IN09, %%H1, %%H3, 0x88    ; L4
        vshufi64x2      %%IN13, %%H1, %%H3, 0xdd    ; L6

        vshufi64x2      %%H0, %%K3, %%T0, 0x44
        vshufi64x2      %%H1, %%K3, %%T0, 0xee
        vshufi64x2      %%H2, %%IN14, %%IN02, 0x44
        vshufi64x2      %%H3, %%IN14, %%IN02, 0xee
        vshufi64x2      %%IN02, %%H0, %%H2, 0x88    ; R1
        vshufi64x2      %%IN06, %%H0, %%H2, 0xdd    ; R3
        vshufi64x2      %%IN10, %%H1, %%H3, 0x88    ; R5
        vshufi64x2      %%IN14, %%H1, %%H3, 0xdd    ; R7

        vshufi64x2      %%H0, %%T3, %%T1, 0x44
        vshufi64x2      %%H1, %%T3, %%T1, 0xee
        vshufi64x2      %%H2, %%IN15, %%IN03, 0x44
        vshufi64x2      %%H3, %%IN15, %%IN03, 0xee
        vshufi64x2      %%IN03, %%H0, %%H2, 0x88    ; L1
        vshufi64x2      %%IN07, %%H0, %%H2, 0xdd    ; L3
        vshufi64x2      %%IN11, %%H1, %%H3, 0x88    ; L5
        vshufi64x2      %%IN15, %%H1, %%H3, 0xdd    ; L7
%endmacro

;;; ===========================================================================
;;; DATA TRANSPOSITION AT DATA OUTPUT
;;; ===========================================================================
;;;
;;; IN00-IN15 aka R0/L0 - R7/L7 [in/out]:
;;;          in:  R0 - 16 x word0, L0 - 16 x word1, ... L7 - 16 x word15
;;;          out: R0 - lane 0 data, L0 - lane 1 data, ... L7 - lane 15 data
;;; T0-T3 [clobbered] - temporary zmm registers
;;; K0-K5 [clobbered] - temporary zmm registers
;;; H0-H3 [clobbered] - temporary zmm registers
%macro TRANSPOSE_OUT 30
%define %%IN00 %1               ; R0
%define %%IN01 %2               ; L0
%define %%IN02 %3               ; R1
%define %%IN03 %4               ; L1
%define %%IN04 %5               ; R2
%define %%IN05 %6               ; L2
%define %%IN06 %7               ; R3
%define %%IN07 %8               ; L3
%define %%IN08 %9               ; R4
%define %%IN09 %10              ; L4
%define %%IN10 %11              ; R5
%define %%IN11 %12              ; L5
%define %%IN12 %13              ; R6
%define %%IN13 %14              ; L6
%define %%IN14 %15              ; R7
%define %%IN15 %16              ; L7
%define %%T0 %17
%define %%T1 %18
%define %%T2 %19
%define %%T3 %20
%define %%K0 %21
%define %%K1 %22
%define %%K2 %23
%define %%K3 %24
%define %%K4 %25
%define %%K5 %26
%define %%H0 %27
%define %%H1 %28
%define %%H2 %29
%define %%H3 %30

	vpunpckldq      %%K0, %%IN01, %%IN00
	vpunpckhdq      %%K1, %%IN01, %%IN00
	vpunpckldq      %%T0, %%IN03, %%IN02
	vpunpckhdq      %%T1, %%IN03, %%IN02

	vpunpckldq      %%IN00, %%IN05, %%IN04
	vpunpckhdq      %%IN01, %%IN05, %%IN04
	vpunpckldq      %%IN02, %%IN07, %%IN06
	vpunpckhdq      %%IN03, %%IN07, %%IN06

        vpunpcklqdq     %%K2, %%K0, %%T0
	vpunpckhqdq     %%T2, %%K0, %%T0
	vpunpcklqdq     %%K3, %%K1, %%T1
	vpunpckhqdq     %%T3, %%K1, %%T1

        vpunpcklqdq     %%K0, %%IN00, %%IN02
	vpunpckhqdq     %%K1, %%IN00, %%IN02
	vpunpcklqdq     %%T0, %%IN01, %%IN03
	vpunpckhqdq     %%T1, %%IN01, %%IN03

	vpunpckldq      %%K4, %%IN09, %%IN08
	vpunpckhdq      %%K5, %%IN09, %%IN08
	vpunpckldq      %%IN04, %%IN11, %%IN10
	vpunpckhdq      %%IN05, %%IN11, %%IN10
	vpunpckldq      %%IN06, %%IN13, %%IN12
	vpunpckhdq      %%IN07, %%IN13, %%IN12
	vpunpckldq      %%IN10, %%IN15, %%IN14
	vpunpckhdq      %%IN11, %%IN15, %%IN14

        vpunpcklqdq     %%IN12, %%K4, %%IN04
	vpunpckhqdq     %%IN13, %%K4, %%IN04
        vpunpcklqdq     %%IN14, %%K5, %%IN05
	vpunpckhqdq     %%IN15, %%K5, %%IN05
        vpunpcklqdq     %%IN00, %%IN06, %%IN10
	vpunpckhqdq     %%IN01, %%IN06, %%IN10
        vpunpcklqdq     %%IN02, %%IN07, %%IN11
	vpunpckhqdq     %%IN03, %%IN07, %%IN11

        vshufi64x2      %%H0, %%K2, %%K0, 0x44
        vshufi64x2      %%H1, %%K2, %%K0, 0xee
        vshufi64x2      %%H2, %%IN12, %%IN00, 0x44
        vshufi64x2      %%H3, %%IN12, %%IN00, 0xee
        vshufi64x2      %%IN00, %%H0, %%H2, 0x88    ; R0
        vshufi64x2      %%IN04, %%H0, %%H2, 0xdd    ; R2
        vshufi64x2      %%IN08, %%H1, %%H3, 0x88    ; R4
        vshufi64x2      %%IN12, %%H1, %%H3, 0xdd    ; R6

        vshufi64x2      %%H0, %%T2, %%K1, 0x44
        vshufi64x2      %%H1, %%T2, %%K1, 0xee
        vshufi64x2      %%H2, %%IN13, %%IN01, 0x44
        vshufi64x2      %%H3, %%IN13, %%IN01, 0xee
        vshufi64x2      %%IN01, %%H0, %%H2, 0x88    ; L0
        vshufi64x2      %%IN05, %%H0, %%H2, 0xdd    ; L2
        vshufi64x2      %%IN09, %%H1, %%H3, 0x88    ; L4
        vshufi64x2      %%IN13, %%H1, %%H3, 0xdd    ; L6

        vshufi64x2      %%H0, %%K3, %%T0, 0x44
        vshufi64x2      %%H1, %%K3, %%T0, 0xee
        vshufi64x2      %%H2, %%IN14, %%IN02, 0x44
        vshufi64x2      %%H3, %%IN14, %%IN02, 0xee
        vshufi64x2      %%IN02, %%H0, %%H2, 0x88    ; R1
        vshufi64x2      %%IN06, %%H0, %%H2, 0xdd    ; R3
        vshufi64x2      %%IN10, %%H1, %%H3, 0x88    ; R5
        vshufi64x2      %%IN14, %%H1, %%H3, 0xdd    ; R7

        vshufi64x2      %%H0, %%T3, %%T1, 0x44
        vshufi64x2      %%H1, %%T3, %%T1, 0xee
        vshufi64x2      %%H2, %%IN15, %%IN03, 0x44
        vshufi64x2      %%H3, %%IN15, %%IN03, 0xee
        vshufi64x2      %%IN03, %%H0, %%H2, 0x88    ; L1
        vshufi64x2      %%IN07, %%H0, %%H2, 0xdd    ; L3
        vshufi64x2      %%IN11, %%H1, %%H3, 0x88    ; L5
        vshufi64x2      %%IN15, %%H1, %%H3, 0xdd    ; L7
%endmacro

;;; ===========================================================================
;;; DATA TRANSPOSITION OF ONE DES BLOCK AT DATA INPUT
;;; ===========================================================================
;;;
;;; IN00-IN15 / R0/L0-R7/L7 [in/out]:
;;;          in:  IN00 - lane 0 data, IN01 - lane 1 data, ... IN15 - lane 15 data
;;;          out: R0 - 16 x word0, L0 - 16 x word1
;;; T0,T2 [clobbered] - temporary zmm registers
;;; K0-K4 [clobbered] - temporary zmm registers
;;; H0,H2 [clobbered] - temporary zmm registers
%macro TRANSPOSE_IN_ONE 24
%define %%IN00 %1               ; R0
%define %%IN01 %2               ; L0
%define %%IN02 %3               ; R1
%define %%IN03 %4               ; L1
%define %%IN04 %5               ; R2
%define %%IN05 %6               ; L2
%define %%IN06 %7               ; R3
%define %%IN07 %8               ; L3
%define %%IN08 %9               ; R4
%define %%IN09 %10              ; L4
%define %%IN10 %11              ; R5
%define %%IN11 %12              ; L5
%define %%IN12 %13              ; R6
%define %%IN13 %14              ; L6
%define %%IN14 %15              ; R7
%define %%IN15 %16              ; L7
%define %%T0 %17
%define %%T2 %18
%define %%K0 %19
%define %%K1 %20
%define %%K2 %21
%define %%K4 %22
%define %%H0 %23
%define %%H2 %24

	vpunpckldq      %%K0, %%IN00, %%IN01
	vpunpckhdq      %%K1, %%IN00, %%IN01
	vpunpckldq      %%T0, %%IN02, %%IN03

	vpunpckldq      %%IN00, %%IN04, %%IN05
	vpunpckhdq      %%IN01, %%IN04, %%IN05
	vpunpckldq      %%IN02, %%IN06, %%IN07

        vpunpcklqdq     %%K2, %%K0, %%T0
	vpunpckhqdq     %%T2, %%K0, %%T0

        vpunpcklqdq     %%K0, %%IN00, %%IN02
	vpunpckhqdq     %%K1, %%IN00, %%IN02

	vpunpckldq      %%K4, %%IN08, %%IN09
	vpunpckldq      %%IN04, %%IN10, %%IN11
	vpunpckldq      %%IN06, %%IN12, %%IN13
	vpunpckldq      %%IN10, %%IN14, %%IN15

        vpunpcklqdq     %%IN12, %%K4, %%IN04
	vpunpckhqdq     %%IN13, %%K4, %%IN04
        vpunpcklqdq     %%IN00, %%IN06, %%IN10
	vpunpckhqdq     %%IN01, %%IN06, %%IN10

        vshufi64x2      %%H0, %%K2, %%K0, 0x44
        vshufi64x2      %%H2, %%IN12, %%IN00, 0x44
        vshufi64x2      %%IN00, %%H0, %%H2, 0x88    ; R0

        vshufi64x2      %%H0, %%T2, %%K1, 0x44
        vshufi64x2      %%H2, %%IN13, %%IN01, 0x44
        vshufi64x2      %%IN01, %%H0, %%H2, 0x88    ; L0
%endmacro

;;; ===========================================================================
;;; DATA TRANSPOSITION OF ONE DES BLOCK AT DATA OUTPUT
;;; ===========================================================================
;;;
;;; IN00-IN15 aka R0/L0 - R7/L7 [in/out]:
;;;          in:  R0 - 16 x word0, L0 - 16 x word1
;;;          out: R0 - lane 0 data, L0 - lane 1 data, ... L7 - lane 15 data
;;; T0-T3 [clobbered] - temporary zmm registers
;;; K0-K3 [clobbered] - temporary zmm registers
;;; H0,H1 [clobbered] - temporary zmm registers
%macro TRANSPOSE_OUT_ONE 25
%define %%IN00 %1               ; R0
%define %%IN01 %2               ; L0
%define %%IN02 %3               ; R1
%define %%IN03 %4               ; L1
%define %%IN04 %5               ; R2
%define %%IN05 %6               ; L2
%define %%IN06 %7               ; R3
%define %%IN07 %8               ; L3
%define %%IN08 %9               ; R4
%define %%IN09 %10              ; L4
%define %%IN10 %11              ; R5
%define %%IN11 %12              ; L5
%define %%IN12 %13              ; R6
%define %%IN13 %14              ; L6
%define %%IN14 %15              ; R7
%define %%IN15 %16              ; L7
%define %%T0 %17
%define %%T2 %18
%define %%T3 %19
%define %%K0 %20
%define %%K1 %21
%define %%K2 %22
%define %%K3 %23
%define %%H0 %24
%define %%H1 %25

        vpxord          %%T0, %%T0, %%T0

	vpunpckldq      %%K0, %%IN01, %%IN00
	vpunpckhdq      %%K1, %%IN01, %%IN00

        vpunpcklqdq     %%K2, %%K0, %%T0
	vpunpckhqdq     %%T2, %%K0, %%T0
	vpunpcklqdq     %%K3, %%K1, %%T0
	vpunpckhqdq     %%T3, %%K1, %%T0

        vshufi64x2      %%H0, %%K2, %%T0, 0x44
        vshufi64x2      %%H1, %%K2, %%T0, 0xee
        vshufi64x2      %%IN00, %%H0, %%T0, 0x88    ; R0
        vshufi64x2      %%IN04, %%H0, %%T0, 0xdd    ; R2
        vshufi64x2      %%IN08, %%H1, %%T0, 0x88    ; R4
        vshufi64x2      %%IN12, %%H1, %%T0, 0xdd    ; R6

        vshufi64x2      %%H0, %%T2, %%T0, 0x44
        vshufi64x2      %%H1, %%T2, %%T0, 0xee
        vshufi64x2      %%IN01, %%H0, %%T0, 0x88    ; L0
        vshufi64x2      %%IN05, %%H0, %%T0, 0xdd    ; L2
        vshufi64x2      %%IN09, %%H1, %%T0, 0x88    ; L4
        vshufi64x2      %%IN13, %%H1, %%T0, 0xdd    ; L6

        vshufi64x2      %%H0, %%K3, %%T0, 0x44
        vshufi64x2      %%H1, %%K3, %%T0, 0xee
        vshufi64x2      %%IN02, %%H0, %%T0, 0x88    ; R1
        vshufi64x2      %%IN06, %%H0, %%T0, 0xdd    ; R3
        vshufi64x2      %%IN10, %%H1, %%T0, 0x88    ; R5
        vshufi64x2      %%IN14, %%H1, %%T0, 0xdd    ; R7

        vshufi64x2      %%H0, %%T3, %%T0, 0x44
        vshufi64x2      %%H1, %%T3, %%T0, 0xee
        vshufi64x2      %%IN03, %%H0, %%T0, 0x88    ; L1
        vshufi64x2      %%IN07, %%H0, %%T0, 0xdd    ; L3
        vshufi64x2      %%IN11, %%H1, %%T0, 0x88    ; L5
        vshufi64x2      %%IN15, %%H1, %%T0, 0xdd    ; L7
%endmacro

;;; ===========================================================================
;;; DES INITIALIZATION
;;; key schedule transposition and IV set up
;;; ===========================================================================
;;;
;;; STATE_KEYS [in]  - KEYS in DES OOO STATE
;;; STATE_IV [ in]  - IV in DES OOO STATE
;;; KS    [out] - place to store transposed key schedule or NULL
;;; IV0   [out] - r512; initialization vector
;;; IV1   [out] - r512; initialization vector
;;; T0-T27 [clobbered] - temporary r512
%macro DES_INIT 33
%define %%STATE_KEYS %1
%define %%STATE_IV   %2
%define %%KS   %3
%define %%IV0  %4
%define %%IV1  %5
%define %%T0   %6
%define %%T1   %7
%define %%T2   %8
%define %%T3   %9
%define %%T4   %10
%define %%T5   %11
%define %%T6   %12
%define %%T7   %13
%define %%T8   %14
%define %%T9   %15
%define %%T10  %16
%define %%T11  %17
%define %%T12  %18
%define %%T13  %19
%define %%T14  %20
%define %%T15  %21
%define %%T16  %22
%define %%T17  %23
%define %%T18  %24
%define %%T19  %25
%define %%T20  %26
%define %%T21  %27
%define %%T22  %28
%define %%T23  %29
%define %%T24  %30
%define %%T25  %31
%define %%T26  %32
%define %%T27  %33

        ;; set up the key schedule
        ;; - load first half of the keys & transpose
        ;; - transpose and store
        ;; note: we can use IV registers as temporary ones here
%assign IDX 0
%rep 16
        mov             IA0, [%%STATE_KEYS + (IDX*PTR_SZ)]
        vmovdqu64       %%T %+ IDX, [IA0]
%assign IDX (IDX + 1)
%endrep
        TRANSPOSE_IN %%T0, %%T1, %%T2, %%T3, %%T4, %%T5, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11, %%T12, %%T13, %%T14, %%T15, %%T16, %%T17, %%T18, %%T19, %%T20, %%T21, %%T22, %%T23, %%T24, %%T25, %%T26, %%T27, %%IV0, %%IV1
%assign IDX 0
%rep 16
        vmovdqu64       [%%KS + (IDX * 64)], %%T %+ IDX
%assign IDX (IDX + 1)
%endrep
        ;; - load second half of the keys & transpose
        ;; - transpose and store
        ;; note: we can use IV registers as temporary ones here
%assign IDX 0
%rep 16
        mov             IA0, [%%STATE_KEYS + (IDX*PTR_SZ)]
        vmovdqu64       %%T %+ IDX, [IA0 + 64]
%assign IDX (IDX + 1)
%endrep
        TRANSPOSE_IN %%T0, %%T1, %%T2, %%T3, %%T4, %%T5, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11, %%T12, %%T13, %%T14, %%T15, %%T16, %%T17, %%T18, %%T19, %%T20, %%T21, %%T22, %%T23, %%T24, %%T25, %%T26, %%T27, %%IV0, %%IV1
%assign IDX 0
%rep 16
        vmovdqu64       [%%KS + (16 * 64) + (IDX * 64)], %%T %+ IDX
%assign IDX (IDX + 1)
%endrep

        ;; set up IV
        ;; - they are already kept transposed so this is enough to load them
        vmovdqu64       %%IV0, [%%STATE_IV + (0 * 64)]
        vmovdqu64       %%IV1, [%%STATE_IV + (1 * 64)]
%endmacro

;;; ===========================================================================
;;; 3DES INITIALIZATION
;;; key schedule transposition and IV set up
;;; ===========================================================================
;;;
;;; STATE_KEYS [in]  - KEYS in 3DES OOO STATE
;;; STATE_IV [ in]  - IV in 3DES OOO STATE
;;; KS1   [out] - place to store transposed key schedule or NULL
;;; KS2   [out] - place to store transposed key schedule or NULL
;;; KS3   [out] - place to store transposed key schedule or NULL
;;; IV0   [out] - r512; initialization vector
;;; IV1   [out] - r512; initialization vector
;;; T0-T27 [clobbered] - temporary r512
;;; DIR   [in] - ENC/DEC (keys arranged in different order for enc/dec)
%macro DES3_INIT 36
%define %%STATE_KEYS %1
%define %%STATE_IV   %2
%define %%KS1  %3
%define %%KS2  %4
%define %%KS3  %5
%define %%IV0  %6
%define %%IV1  %7
%define %%T0   %8
%define %%T1   %9
%define %%T2   %10
%define %%T3   %11
%define %%T4   %12
%define %%T5   %13
%define %%T6   %14
%define %%T7   %15
%define %%T8   %16
%define %%T9   %17
%define %%T10  %18
%define %%T11  %19
%define %%T12  %20
%define %%T13  %21
%define %%T14  %22
%define %%T15  %23
%define %%T16  %24
%define %%T17  %25
%define %%T18  %26
%define %%T19  %27
%define %%T20  %28
%define %%T21  %29
%define %%T22  %30
%define %%T23  %31
%define %%T24  %32
%define %%T25  %33
%define %%T26  %34
%define %%T27  %35
%define %%DIR  %36

%ifidn %%DIR, ENC
%assign KEY_IDX 0
%else
%assign KEY_IDX 2
%endif
%assign KS_IDX  1

%rep 3
        ;; set up the key schedule
        ;; - load first half of the keys & transpose
        ;; - transpose and store
        ;; note: we can use IV registers as temporary ones here

%assign IDX 0
%rep 16
        mov             IA0, [%%STATE_KEYS + (IDX*PTR_SZ)]
        mov             IA0, [IA0 + (KEY_IDX * PTR_SZ)]
        vmovdqu64       %%T %+ IDX, [IA0]
%assign IDX (IDX + 1)
%endrep
        TRANSPOSE_IN %%T0, %%T1, %%T2, %%T3, %%T4, %%T5, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11, %%T12, %%T13, %%T14, %%T15, %%T16, %%T17, %%T18, %%T19, %%T20, %%T21, %%T22, %%T23, %%T24, %%T25, %%T26, %%T27, %%IV0, %%IV1
%assign IDX 0
%rep 16
        vmovdqu64       [%%KS %+ KS_IDX + (IDX * 64)], %%T %+ IDX
%assign IDX (IDX + 1)
%endrep
        ;; - load second half of the keys & transpose
        ;; - transpose and store
        ;; note: we can use IV registers as temporary ones here
%assign IDX 0
%rep 16
        mov             IA0, [%%STATE_KEYS + (IDX*PTR_SZ)]
        mov             IA0, [IA0 + (KEY_IDX * PTR_SZ)]
        vmovdqu64       %%T %+ IDX, [IA0 + 64]
%assign IDX (IDX + 1)
%endrep
        TRANSPOSE_IN %%T0, %%T1, %%T2, %%T3, %%T4, %%T5, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11, %%T12, %%T13, %%T14, %%T15, %%T16, %%T17, %%T18, %%T19, %%T20, %%T21, %%T22, %%T23, %%T24, %%T25, %%T26, %%T27, %%IV0, %%IV1
%assign IDX 0
%rep 16
        vmovdqu64       [%%KS %+ KS_IDX + (16 * 64) + (IDX * 64)], %%T %+ IDX
%assign IDX (IDX + 1)
%endrep

%ifidn %%DIR, ENC
%assign KEY_IDX (KEY_IDX + 1)
%else
%assign KEY_IDX (KEY_IDX - 1)
%endif
%assign KS_IDX (KS_IDX + 1)
%endrep                         ; KEY_IDX / KS_IDX

        ;; set up IV
        ;; - they are already kept transposed so this is enough to load them
        vmovdqu64       %%IV0, [%%STATE_IV + (0 * 64)]
        vmovdqu64       %%IV1, [%%STATE_IV + (1 * 64)]

%endmacro

;;; ===========================================================================
;;; DES FINISH
;;; Update in/out pointers and store IV
;;; ===========================================================================
;;;
;;; Needs: STATE & SIZE
;;; IV0   [in] - r512; initialization vector
;;; IV1   [in] - r512; initialization vector
;;; T0-T4 [clobbered] - temporary r512 registers
%macro DES_FINISH 7
%define %%IV0   %1
%define %%IV1   %2
%define %%T0    %3
%define %%T1    %4
%define %%T2    %5
%define %%T3    %6
%define %%T4    %7

        vpbroadcastq    %%T4, SIZE
        vmovdqu64       %%T0, [STATE + _des_args_in + (0 * PTR_SZ)]
        vmovdqu64       %%T1, [STATE + _des_args_in + (8 * PTR_SZ)]
        vmovdqu64       %%T2, [STATE + _des_args_out + (0 * PTR_SZ)]
        vmovdqu64       %%T3, [STATE + _des_args_out + (8 * PTR_SZ)]
        vpaddq          %%T0, %%T0, %%T4
        vpaddq          %%T1, %%T1, %%T4
        vpaddq          %%T2, %%T2, %%T4
        vpaddq          %%T3, %%T3, %%T4
        vmovdqu64       [STATE + _des_args_in + (0 * PTR_SZ)], %%T0
        vmovdqu64       [STATE + _des_args_in + (8 * PTR_SZ)], %%T1
        vmovdqu64       [STATE + _des_args_out + (0 * PTR_SZ)], %%T2
        vmovdqu64       [STATE + _des_args_out + (8 * PTR_SZ)], %%T3

        vmovdqu64       [STATE + _des_args_IV + (0 * 64)], %%IV0
        vmovdqu64       [STATE + _des_args_IV + (1 * 64)], %%IV1
%endmacro

;;; ===========================================================================
;;; DES CFB ENCRYPT/DECRYPT - ONE BLOCK ONLY
;;; ===========================================================================
;;;
;;; Needs: STATE, IA0-IA2
;;; ENC_DEC [in] - encyrpt (ENC) or decrypt (DEC) selection
;;; KS      [in] - key schedule
;;; T0-T24  [clobbered] - temporary r512
;;; T_IN    [in] - 16 * 8 byte storage
;;; T_OUT   [in] - 16 * 8 byte storage
;;; T_MASK  [in] - 16 * 4 byte storage
;;; T_IV    [in] - 16 * 8 byte storage
;;;
;;; NOTE: clobbers OpMask registers
%macro DES_CFB_ONE 31
%define %%ENC_DEC %1
%define %%KS      %2
%define %%T0      %3
%define %%T1      %4
%define %%T2      %5
%define %%T3      %6
%define %%T4      %7
%define %%T5      %8
%define %%T6      %9
%define %%T7      %10
%define %%T8      %11
%define %%T9      %12
%define %%T10     %13
%define %%T11     %14
%define %%T12     %15
%define %%T13     %16
%define %%T14     %17
%define %%T15     %18
%define %%T16     %19
%define %%T17     %20
%define %%T18     %21
%define %%T19     %22
%define %%T20     %23
%define %%T21     %24
%define %%T22     %25
%define %%T23     %26
%define %%T24     %27
%define %%T_IN    %28
%define %%T_OUT   %29
%define %%T_IV    %30
%define %%T_MASK  %31

        ;; - find mask for non-zero partial lengths
        vpxord          %%T10, %%T10, %%T10
        vmovdqu64       %%T0, [STATE + _des_args_PLen]
        vpcmpd          k3, %%T0, %%T10, 4 ; NEQ
        kmovw           DWORD(IA0), k3
        movzx           DWORD(IA0), WORD(IA0)
        or              DWORD(IA0), DWORD(IA0)
        jz              %%_des_cfb_one_end ; no non-zero partial lengths

%ifidn %%ENC_DEC, ENC
        ;; For encyrption case we need to make sure that
        ;; all full blocks are complete before proceeding
        ;; with CFB partial block.
        ;; To do that current out position is compared against
        ;; calculated last full block position.
        vmovdqu64       %%T1, [STATE + _des_args_out + (0*8)]
        vmovdqu64       %%T2, [STATE + _des_args_LOut + (0*8)]
        vmovdqu64       %%T3, [STATE + _des_args_out + (8*8)]
        vmovdqu64       %%T4, [STATE + _des_args_LOut + (8*8)]
        vpcmpq          k4, %%T1, %%T2, 0 ; EQ
        vpcmpq          k5, %%T3, %%T4, 0 ; EQ
        kmovw           DWORD(IA1), k4
        movzx           DWORD(IA1), BYTE(IA1)
        kmovw           DWORD(IA2), k5
        movzx           DWORD(IA2), BYTE(IA2)
        shl             DWORD(IA2), 8
        or              DWORD(IA2), DWORD(IA1)
        and             DWORD(IA0), DWORD(IA2)
        jz              %%_des_cfb_one_end ; no non-zero lengths left
        kmovw           k3, DWORD(IA0)
%endif
        ;; Calculate ((1 << partial_bytes) - 1)
        ;; in order to get the mask for loads and stores
        ;; k3 & IA0 - hold valid mask
        vmovdqa64       %%T1, [rel vec_ones_32b]
        vpsllvd         %%T2{k3}{z}, %%T1, %%T0
        vpsubd          %%T2{k3}{z}, %%T2, %%T1
        vmovdqu64       [%%T_MASK], %%T2

        ;; clear selected partial lens not to do them twice
        vmovdqu32       [STATE + _des_args_PLen]{k3}, %%T10

        ;; copy IV, in and out pointers
        vmovdqu64       %%T1, [STATE + _des_args_in + (0*PTR_SZ)]
        vmovdqu64       %%T2, [STATE + _des_args_in + (8*PTR_SZ)]
        vmovdqu64       %%T3, [STATE + _des_args_out + (0*PTR_SZ)]
        vmovdqu64       %%T4, [STATE + _des_args_out + (8*PTR_SZ)]
        vmovdqu64       %%T5, [STATE + _des_args_IV + (0*64)]
        vmovdqu64       %%T6, [STATE + _des_args_IV + (1*64)]
        vmovdqu64       [%%T_IN + (0*PTR_SZ)], %%T1
        vmovdqu64       [%%T_IN + (8*PTR_SZ)], %%T2
        vmovdqu64       [%%T_OUT + (0*PTR_SZ)], %%T3
        vmovdqu64       [%%T_OUT + (8*PTR_SZ)], %%T4
        vmovdqu64       [%%T_IV + (0*64)], %%T5
        vmovdqu64       [%%T_IV + (1*64)], %%T6

        ;; calculate last block case mask
        ;; - first block case requires no modifications to in/out/IV
        vmovdqu64       %%T1, [STATE + _des_args_BLen]
        vpcmpd          k2, %%T1, %%T10, 4 ; NEQ
        kmovw           DWORD(IA1), k2
        and             DWORD(IA1), DWORD(IA0)
        jz              %%_des_cfb_one_no_last_blocks

        ;; set up IV, in and out for the last block case
        ;; - Last block needs in and out to be set differently (decryption only)
        ;; - IA1 holds the last block mask
%ifidn %%ENC_DEC, DEC
        mov             DWORD(IA0), DWORD(IA1)
        mov             DWORD(IA2), DWORD(IA1)
        shr             DWORD(IA1), 8
        and             DWORD(IA2), 0xff
        kmovw           k4, DWORD(IA2)
        kmovw           k5, DWORD(IA1)
        vmovdqu64       %%T1, [STATE + _des_args_LOut + (0*PTR_SZ)]
        vmovdqu64       %%T2, [STATE + _des_args_LOut + (8*PTR_SZ)]
        vmovdqu64       %%T3, [STATE + _des_args_LIn + (0*PTR_SZ)]
        vmovdqu64       %%T4, [STATE + _des_args_LIn + (8*PTR_SZ)]
        vmovdqu64       [%%T_OUT + (0*PTR_SZ)]{k4}, %%T1
        vmovdqu64       [%%T_OUT + (8*PTR_SZ)]{k5}, %%T2
        vmovdqu64       [%%T_IN + (0*PTR_SZ)]{k4}, %%T3
        vmovdqu64       [%%T_IN + (8*PTR_SZ)]{k5}, %%T4
%endif                          ; decryption
        ;; - IV has to be set differently for CFB as well
        ;; - IA0 holds the last block mask
%assign IDX 0
%rep 16
        test            DWORD(IA0), (1 << IDX)
        jz              %%_des_cfb_one_copy_iv_next %+ IDX
%ifidn %%ENC_DEC, ENC
        mov             IA2, [STATE + _des_args_LOut + (IDX*PTR_SZ)]
%else
        mov             IA2, [STATE + _des_args_LIn + (IDX*PTR_SZ)]
%endif
        mov             IA2, [IA2 - 8]
        mov             [%%T_IV + (0*4) + (IDX*4)], DWORD(IA2)
        shr             IA2, 32
        mov             [%%T_IV + (16*4) + (IDX*4)], DWORD(IA2)
%%_des_cfb_one_copy_iv_next %+ IDX:
%assign IDX (IDX + 1)
%endrep

%%_des_cfb_one_no_last_blocks:
        ;; Uffff ... finally let's do some DES CFB
        ;; - let's use T_IN, T_OUT, T_IV and T_MASK

        ;; - load data with the corresponding masks & transpose
        ;; - T0 to T15 will hold the data
        xor             IA0, IA0
%assign IDX 0
%assign K_IDX 1
%rep 16
        mov             IA1, [%%T_IN + (IDX*PTR_SZ)]
        mov             DWORD(IA0), [%%T_MASK + (IDX*4)]
        kmovq           k %+ K_IDX, IA0
        vmovdqu8        %%T %+ IDX{k %+ K_IDX}{z}, [IA1]
%assign IDX (IDX + 1)
%assign K_IDX (K_IDX + 1)
%if K_IDX > 7
%assign K_IDX 1                 ; iterate through K1 to K7
%endif
%endrep
        ;; - transpose the data in T0 to T15, T16 to T23 are clobbered
        TRANSPOSE_IN_ONE %%T0, %%T1, %%T2, %%T3, %%T4, %%T5, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11, %%T12, %%T13, %%T14, %%T15, %%T16, %%T17, %%T18, %%T19, %%T20, %%T21, %%T22, %%T23

        ;; - set up IV and %%T16 & %%T17 used as IV0 and IV1
        vmovdqu64       %%T16, [%%T_IV + (0 * 64)] ;IV0
        vmovdqu64       %%T17, [%%T_IV + (1 * 64)] ;IV1
        ;; DES encrypt
        ;; - R0 - %%T0
        ;; - L0 - %%T1
        DES_ENC_DEC     ENC, %%T16, %%T17, %%KS, %%T2, %%T3, %%T4, %%T5, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11, %%T12, %%T13
        ;; CFB style xor with R0/L0 with IV
        ;; - IV0 - %%T16
        ;; - IV1 - %%T17
        vpxord          %%T2, %%T17, %%T0 ; R0 ^ IV1
        vpxord          %%T0, %%T16, %%T1 ; L0 ^ IV0
        vmovdqa64       %%T1, %%T2
        ;; - new R0 = L0 ^ IV0 (%%T0)
        ;; - new L0 = R0 ^ IV1 (%%T1)

        ;; Transpose the data out
        ;; - %%T2 to %%T24 clobbered
        TRANSPOSE_OUT_ONE %%T0, %%T1, %%T2, %%T3, %%T4, %%T5, %%T6, %%T7, %%T8, %%T9, %%T10, %%T11, %%T12, %%T13, %%T14, %%T15, %%T16, %%T17, %%T18, %%T19, %%T20, %%T21, %%T22, %%T23, %%T24

        ;; Store the transposed data
        ;; - T0 to T15 will hold the data
        xor             IA0, IA0
%assign IDX 0
%assign K_IDX 1
%rep 16
        mov             IA1, [%%T_OUT + (IDX*PTR_SZ)]
        mov             DWORD(IA0), [%%T_MASK + (IDX*4)]
        kmovq           k %+ K_IDX, IA0
        vmovdqu8        [IA1]{k %+ K_IDX}, %%T %+ IDX
%assign IDX (IDX + 1)
%assign K_IDX (K_IDX + 1)
%if K_IDX > 7
%assign K_IDX 1                 ; iterate through K1 to K7
%endif
%endrep

%ifdef SAFE_DATA
        ;; Clear copied IV's
        vpxorq          %%T5, %%T5
        vmovdqu64       [%%T_IV + (0*64)], %%T5
        vmovdqu64       [%%T_IV + (1*64)], %%T5
%endif

%%_des_cfb_one_end:

%endmacro

;;; ===========================================================================
;;; Converts length into mask of DES blocks
;;; ===========================================================================
;;;
;;; MASK [out] - mask8 for value; for masked 64b loads and stores (r64)
;;; USES: IA0, IA1 IA2
;;; ASSUMES: SIZE - OFFSET < 64
%macro GET_MASK8 1
%define %%MASK %1

%ifidn IA1, rcx
%define myrcx IA1
%else
%define myrcx rcx
        mov     IA1, rcx
%endif
        mov     myrcx, SIZE
        sub     myrcx, OFFSET
        ;; - myrcx - remaining length
        ;; - divide by 8 (DES block size)
        ;; - create bit mask of the result
        mov     DWORD(%%MASK), 1
        shr     DWORD(myrcx), 3
        shl     DWORD(%%MASK), BYTE(myrcx)
        sub     DWORD(%%MASK), 1
%ifnidn IA1, rcx
        mov     rcx, IA1
%endif
%endmacro

;;; ===========================================================================
;;; DES CBC ENCRYPT CIPHER ONLY (1 to 8 DES blocks only)
;;; ===========================================================================
;;;
;;; NUM_DES_BLOCKS [in] - 1 to 8 DES blocks only
;;; DES_KS         [in] - pointer to transposed key schedule
;;;
;;; NOTE: clobbers OpMask registers
;;; REQUIRES: ZTMP0 - ZTMP13, ZW0-ZW15 (depends on NUM_DES_BLOCKS), ZIV0, ZIV1
%macro GEN_DES_ENC_CIPHER 2
%define %%NUM_DES_BLOCKS %1
%define %%DES_KS         %2

%assign RN 0
%assign LN 1
%assign RNN 2
%assign LNN 3
%rep %%NUM_DES_BLOCKS - 1
        DES_ENC_DEC     ENC, ZW %+ RN, ZW %+ LN, %%DES_KS, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
        vpxord          ZW %+ RNN, ZW %+ RNN, ZW %+ LN ; R1 = R1 ^ L0
        vpxord          ZW %+ LNN, ZW %+ LNN, ZW %+ RN ; L1 = L1 ^ R0
%assign RN (RN + 2)
%assign LN (LN + 2)
%assign RNN (RNN + 2)
%assign LNN (LNN + 2)
%endrep
        DES_ENC_DEC     ENC, ZW %+ RN, ZW %+ LN, %%DES_KS, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
        vmovdqa64       ZIV0, ZW %+ LN ; IV0 = L7
        vmovdqa64       ZIV1, ZW %+ RN ; IV1 = R7
%endmacro

;;; ===========================================================================
;;; DES CBC DECRYPT CIPHER ONLY (1 to 8 DES blocks only)
;;; ===========================================================================
;;;
;;; NUM_DES_BLOCKS [in] - 1 to 8 DES blocks only
;;; DES_KS         [in] - pointer to transposed key schedule
;;;
;;; NOTE: clobbers OpMask registers
;;; REQUIRES: ZTMP0 - ZTMP13, ZW0-ZW15 (depends on NUM_DES_BLOCKS), ZIV0, ZIV1
%macro GEN_DES_DEC_CIPHER 2
%define %%NUM_DES_BLOCKS %1
%define %%DES_KS         %2

%assign RN 0
%assign LN 1
%rep %%NUM_DES_BLOCKS
        vmovdqa64       ZTMP12, ZW %+ RN        ; keep R0 as IV for the next round
        vmovdqa64       ZTMP13, ZW %+ LN        ; keep L0 as IV for the next round
        DES_ENC_DEC     DEC, ZW %+ RN, ZW %+ LN, %%DES_KS, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
        vpxord          ZW %+ RN, ZW %+ RN, ZIV1 ; R0 = R0 ^ IV1
        vpxord          ZW %+ LN, ZW %+ LN, ZIV0 ; L0 = L0 ^ IV0
        vmovdqa64       ZIV0, ZTMP12
        vmovdqa64       ZIV1, ZTMP13
%assign RN (RN + 2)
%assign LN (LN + 2)
%endrep
%endmacro

;;; ===========================================================================
;;; 3DES CBC ENCRYPT CIPHER ONLY (1 to 8 DES blocks only)
;;; ===========================================================================
;;;
;;; NUM_DES_BLOCKS [in] - 1 to 8 DES blocks only
;;; DES_KS1        [in] - pointer to transposed key schedule 1
;;; DES_KS2        [in] - pointer to transposed key schedule 2
;;; DES_KS3        [in] - pointer to transposed key schedule 3
;;;
;;; NOTE: clobbers OpMask registers
;;; REQUIRES: ZTMP0 - ZTMP13, ZW0-ZW15 (depends on NUM_DES_BLOCKS), ZIV0, ZIV1
%macro GEN_3DES_ENC_CIPHER 4
%define %%NUM_DES_BLOCKS %1
%define %%DES_KS1        %2
%define %%DES_KS2        %3
%define %%DES_KS3        %4

%assign RN 0
%assign LN 1
%assign RNN 2
%assign LNN 3
%rep %%NUM_DES_BLOCKS
        ;; ENC
        DES_ENC_DEC     ENC, ZW %+ RN, ZW %+ LN, %%DES_KS1, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
        ;; DEC
        DES_ENC_DEC     DEC, ZW %+ LN, ZW %+ RN, %%DES_KS2, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
        ;; ENC
        DES_ENC_DEC     ENC, ZW %+ RN, ZW %+ LN, %%DES_KS3, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
%if (RNN < (%%NUM_DES_BLOCKS * 2))
        vpxord          ZW %+ RNN, ZW %+ RNN, ZW %+ LN ; R1 = R1 ^ L0
        vpxord          ZW %+ LNN, ZW %+ LNN, ZW %+ RN ; L1 = L1 ^ R0
%else
        vmovdqa64       ZIV0, ZW %+ LN ; IV0 = L7
        vmovdqa64       ZIV1, ZW %+ RN ; IV1 = R7
%endif

%assign RN (RN + 2)
%assign LN (LN + 2)
%assign RNN (RNN + 2)
%assign LNN (LNN + 2)
%endrep

%endmacro

;;; ===========================================================================
;;; 3DES CBC DECRYPT CIPHER ONLY (1 to 8 DES blocks only)
;;; ===========================================================================
;;;
;;; NUM_DES_BLOCKS [in] - 1 to 8 DES blocks only
;;; DES_KS1        [in] - pointer to transposed key schedule 1
;;; DES_KS2        [in] - pointer to transposed key schedule 2
;;; DES_KS3        [in] - pointer to transposed key schedule 3
;;;
;;; NOTE: clobbers OpMask registers
;;; REQUIRES: ZTMP0 - ZTMP13, ZW0-ZW15 (depends on NUM_DES_BLOCKS), ZIV0, ZIV1
%macro GEN_3DES_DEC_CIPHER 4
%define %%NUM_DES_BLOCKS %1
%define %%DES_KS1        %2
%define %%DES_KS2        %3
%define %%DES_KS3        %4

%assign RN 0
%assign LN 1
%rep %%NUM_DES_BLOCKS
        vmovdqa64       ZTMP12, ZW %+ RN        ; keep R0 as IV for the next round
        vmovdqa64       ZTMP13, ZW %+ LN        ; keep L0 as IV for the next round
        ;; DEC
        DES_ENC_DEC     DEC, ZW %+ RN, ZW %+ LN, %%DES_KS1, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
        ;; ENC
        DES_ENC_DEC     ENC, ZW %+ LN, ZW %+ RN, %%DES_KS2, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
        ;; DEC
        DES_ENC_DEC     DEC, ZW %+ RN, ZW %+ LN, %%DES_KS3, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
        vpxord          ZW %+ RN, ZW %+ RN, ZIV1 ; R0 = R0 ^ IV1
        vpxord          ZW %+ LN, ZW %+ LN, ZIV0 ; L0 = L0 ^ IV0
        vmovdqa64       ZIV0, ZTMP12
        vmovdqa64       ZIV1, ZTMP13

%assign RN (RN + 2)
%assign LN (LN + 2)
%endrep

%endmacro

;;; ===========================================================================
;;; DES CBC / DOCSIS DES ENCRYPT
;;; ===========================================================================
;;;
;;; DES_DOCSIS [in] - select between DES (DES CBC), DOCSIS (DOCSIS DES) and
;;;                   3DES (3DES CBC)
;;;
;;; NOTE: clobbers OpMask registers
%macro GENERIC_DES_ENC 1
%define %%DES_DOCSIS %1

        ;; push the registers and allocate the stack frame
	mov	        rax, rsp
	sub	        rsp, STACKFRAME_size
	and	        rsp, -64
	mov	        [rsp + _rsp_save], rax	; original SP
        mov             [rsp + _gpr_save + 0*8], r12
        mov             [rsp + _gpr_save + 1*8], r13
        mov             [rsp + _gpr_save + 2*8], r14
        mov             [rsp + _gpr_save + 3*8], r15

%ifnidn %%DES_DOCSIS, 3DES
        ;; DES and DOCSIS DES
        DES_INIT        STATE + _des_args_keys, STATE + _des_args_IV, rsp + _key_sched, ZIV0, ZIV1, ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
%else
        ;; 3DES
        DES3_INIT        STATE + _des_args_keys, STATE + _des_args_IV, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3, ZIV0, ZIV1, ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11, ENC
%endif
        mov             [rsp + _size_save], SIZE
        and             SIZE, -64
        xor             OFFSET, OFFSET
        ;; This loop processes message in blocks of 64 bytes.
        ;; Anything smaller than 64 bytes is handled separately after the loop.
%%_gen_des_enc_loop:
        cmp             OFFSET, SIZE
        jz              %%_gen_des_enc_loop_end
        ;; run loads
        mov             IA0, [STATE + _des_args_in + (0*PTR_SZ)]
        mov             IA1, [STATE + _des_args_in + (1*PTR_SZ)]
        mov             IA2, [STATE + _des_args_in + (2*PTR_SZ)]
        mov             INP0, [STATE + _des_args_in + (3*PTR_SZ)]
        mov             INP1, [STATE + _des_args_in + (4*PTR_SZ)]
        mov             INP2, [STATE + _des_args_in + (5*PTR_SZ)]
        mov             INP3, [STATE + _des_args_in + (6*PTR_SZ)]
        mov             INP4, [STATE + _des_args_in + (7*PTR_SZ)]
        vmovdqu64       ZW0, [IA0 + OFFSET]
        vmovdqu64       ZW1, [IA1 + OFFSET]
        vmovdqu64       ZW2, [IA2 + OFFSET]
        vmovdqu64       ZW3, [INP0 + OFFSET]
        vmovdqu64       ZW4, [INP1 + OFFSET]
        vmovdqu64       ZW5, [INP2 + OFFSET]
        vmovdqu64       ZW6, [INP3 + OFFSET]
        vmovdqu64       ZW7, [INP4 + OFFSET]

        mov             IA0, [STATE + _des_args_in + (8*PTR_SZ)]
        mov             IA1, [STATE + _des_args_in + (9*PTR_SZ)]
        mov             IA2, [STATE + _des_args_in + (10*PTR_SZ)]
        mov             INP0, [STATE + _des_args_in + (11*PTR_SZ)]
        mov             INP1, [STATE + _des_args_in + (12*PTR_SZ)]
        mov             INP2, [STATE + _des_args_in + (13*PTR_SZ)]
        mov             INP3, [STATE + _des_args_in + (14*PTR_SZ)]
        mov             INP4, [STATE + _des_args_in + (15*PTR_SZ)]
        vmovdqu64       ZW8, [IA0 + OFFSET]
        vmovdqu64       ZW9, [IA1 + OFFSET]
        vmovdqu64       ZW10, [IA2 + OFFSET]
        vmovdqu64       ZW11, [INP0 + OFFSET]
        vmovdqu64       ZW12, [INP1 + OFFSET]
        vmovdqu64       ZW13, [INP2 + OFFSET]
        vmovdqu64       ZW14, [INP3 + OFFSET]
        vmovdqu64       ZW15, [INP4 + OFFSET]

        ;; Transpose input
        TRANSPOSE_IN    ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11, ZTMP12, ZTMP13

        ;; DES CBC ENC comes here
        vpxord          ZW0, ZW0, ZIV0 ; R0 = R0 ^ IV0
        vpxord          ZW1, ZW1, ZIV1 ; L0 = L0 ^ IV1

%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_ENC_CIPHER 8, rsp + _key_sched
%else
        GEN_3DES_ENC_CIPHER 8, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif

        ;; transpose data on output
        TRANSPOSE_OUT   ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11, ZTMP12, ZTMP13
        ;; run stores
        mov             IA0, [STATE + _des_args_out + (0*PTR_SZ)]
        mov             IA1, [STATE + _des_args_out + (1*PTR_SZ)]
        mov             IA2, [STATE + _des_args_out + (2*PTR_SZ)]
        mov             INP0, [STATE + _des_args_out + (3*PTR_SZ)]
        mov             INP1, [STATE + _des_args_out + (4*PTR_SZ)]
        mov             INP2, [STATE + _des_args_out + (5*PTR_SZ)]
        mov             INP3, [STATE + _des_args_out + (6*PTR_SZ)]
        mov             INP4, [STATE + _des_args_out + (7*PTR_SZ)]
        vmovdqu64       [IA0 + OFFSET], ZW0
        vmovdqu64       [IA1 + OFFSET], ZW1
        vmovdqu64       [IA2 + OFFSET], ZW2
        vmovdqu64       [INP0 + OFFSET], ZW3
        vmovdqu64       [INP1 + OFFSET], ZW4
        vmovdqu64       [INP2 + OFFSET], ZW5
        vmovdqu64       [INP3 + OFFSET], ZW6
        vmovdqu64       [INP4 + OFFSET], ZW7

        mov             IA0, [STATE + _des_args_out + (8*PTR_SZ)]
        mov             IA1, [STATE + _des_args_out + (9*PTR_SZ)]
        mov             IA2, [STATE + _des_args_out + (10*PTR_SZ)]
        mov             INP0, [STATE + _des_args_out + (11*PTR_SZ)]
        mov             INP1, [STATE + _des_args_out + (12*PTR_SZ)]
        mov             INP2, [STATE + _des_args_out + (13*PTR_SZ)]
        mov             INP3, [STATE + _des_args_out + (14*PTR_SZ)]
        mov             INP4, [STATE + _des_args_out + (15*PTR_SZ)]
        vmovdqu64       [IA0 + OFFSET], ZW8
        vmovdqu64       [IA1 + OFFSET], ZW9
        vmovdqu64       [IA2 + OFFSET], ZW10
        vmovdqu64       [INP0 + OFFSET], ZW11
        vmovdqu64       [INP1 + OFFSET], ZW12
        vmovdqu64       [INP2 + OFFSET], ZW13
        vmovdqu64       [INP3 + OFFSET], ZW14
        vmovdqu64       [INP4 + OFFSET], ZW15

        add             OFFSET, 64
        jmp             %%_gen_des_enc_loop
%%_gen_des_enc_loop_end:
        ;; This is where we check if there is anything less than 64 bytes
        ;; of message left for processing.
        mov             SIZE, [rsp + _size_save]
        cmp             OFFSET, SIZE
        jz              %%_gen_des_enc_part_end
        ;; calculate min of bytes_left and 64, convert to qword mask
        GET_MASK8       IA0        ; IA0 = mask

        kmovw           k7, DWORD(IA0)
        mov             [rsp + _mask_save], IA0
        ;; run masked loads
        mov             IA0, [STATE + _des_args_in + (0*PTR_SZ)]
        mov             IA1, [STATE + _des_args_in + (1*PTR_SZ)]
        mov             IA2, [STATE + _des_args_in + (2*PTR_SZ)]
        mov             INP0, [STATE + _des_args_in + (3*PTR_SZ)]
        mov             INP1, [STATE + _des_args_in + (4*PTR_SZ)]
        mov             INP2, [STATE + _des_args_in + (5*PTR_SZ)]
        mov             INP3, [STATE + _des_args_in + (6*PTR_SZ)]
        mov             INP4, [STATE + _des_args_in + (7*PTR_SZ)]
        vmovdqu64       ZW0{k7}{z}, [IA0 + OFFSET]
        vmovdqu64       ZW1{k7}{z}, [IA1 + OFFSET]
        vmovdqu64       ZW2{k7}{z}, [IA2 + OFFSET]
        vmovdqu64       ZW3{k7}{z}, [INP0 + OFFSET]
        vmovdqu64       ZW4{k7}{z}, [INP1 + OFFSET]
        vmovdqu64       ZW5{k7}{z}, [INP2 + OFFSET]
        vmovdqu64       ZW6{k7}{z}, [INP3 + OFFSET]
        vmovdqu64       ZW7{k7}{z}, [INP4 + OFFSET]

        mov             IA0, [STATE + _des_args_in + (8*PTR_SZ)]
        mov             IA1, [STATE + _des_args_in + (9*PTR_SZ)]
        mov             IA2, [STATE + _des_args_in + (10*PTR_SZ)]
        mov             INP0, [STATE + _des_args_in + (11*PTR_SZ)]
        mov             INP1, [STATE + _des_args_in + (12*PTR_SZ)]
        mov             INP2, [STATE + _des_args_in + (13*PTR_SZ)]
        mov             INP3, [STATE + _des_args_in + (14*PTR_SZ)]
        mov             INP4, [STATE + _des_args_in + (15*PTR_SZ)]
        vmovdqu64       ZW8{k7}{z}, [IA0 + OFFSET]
        vmovdqu64       ZW9{k7}{z}, [IA1 + OFFSET]
        vmovdqu64       ZW10{k7}{z}, [IA2 + OFFSET]
        vmovdqu64       ZW11{k7}{z}, [INP0 + OFFSET]
        vmovdqu64       ZW12{k7}{z}, [INP1 + OFFSET]
        vmovdqu64       ZW13{k7}{z}, [INP2 + OFFSET]
        vmovdqu64       ZW14{k7}{z}, [INP3 + OFFSET]
        vmovdqu64       ZW15{k7}{z}, [INP4 + OFFSET]

        ;; Transpose input
        TRANSPOSE_IN    ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11, ZTMP12, ZTMP13

        ;; DES CBC ENC comes here
        vpxord          ZW0, ZW0, ZIV0 ; R0 = R0 ^ IV0
        vpxord          ZW1, ZW1, ZIV1 ; L0 = L0 ^ IV1

        mov             IA0, [rsp + _mask_save]
        cmp             BYTE(IA0), 0x0f
        ja              %%_gt_4
        jz              %%_blocks_4

        cmp             BYTE(IA0), 0x03
        ja              %%_blocks_3
        jz              %%_blocks_2

        ;; process one block and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_ENC_CIPHER 1, rsp + _key_sched
%else
        GEN_3DES_ENC_CIPHER 1, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_blocks_2:
        ;; process two blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_ENC_CIPHER 2, rsp + _key_sched
%else
        GEN_3DES_ENC_CIPHER 2, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_blocks_3:
        ;; process three blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_ENC_CIPHER 3, rsp + _key_sched
%else
        GEN_3DES_ENC_CIPHER 3, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_blocks_4:
        ;; process four blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_ENC_CIPHER 4, rsp + _key_sched
%else
        GEN_3DES_ENC_CIPHER 4, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_gt_4:
        cmp             BYTE(IA0), 0x3f
        ja              %%_blocks_7
        jz              %%_blocks_6
%%_blocks_5:
        ;; process five blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_ENC_CIPHER 5, rsp + _key_sched
%else
        GEN_3DES_ENC_CIPHER 5, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_blocks_6:
        ;; process six blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_ENC_CIPHER 6, rsp + _key_sched
%else
        GEN_3DES_ENC_CIPHER 6, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_blocks_7:
        ;; process seven blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_ENC_CIPHER 7, rsp + _key_sched
%else
        GEN_3DES_ENC_CIPHER 7, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif

%%_transpose_out:
        ;; transpose data on output
        TRANSPOSE_OUT   ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11, ZTMP12, ZTMP13

        ;; run masked stores
        mov             IA0, [STATE + _des_args_out + (0*PTR_SZ)]
        mov             IA1, [STATE + _des_args_out + (1*PTR_SZ)]
        mov             IA2, [STATE + _des_args_out + (2*PTR_SZ)]
        mov             INP0, [STATE + _des_args_out + (3*PTR_SZ)]
        mov             INP1, [STATE + _des_args_out + (4*PTR_SZ)]
        mov             INP2, [STATE + _des_args_out + (5*PTR_SZ)]
        mov             INP3, [STATE + _des_args_out + (6*PTR_SZ)]
        mov             INP4, [STATE + _des_args_out + (7*PTR_SZ)]
        vmovdqu64       [IA0 + OFFSET]{k7}, ZW0
        vmovdqu64       [IA1 + OFFSET]{k7}, ZW1
        vmovdqu64       [IA2 + OFFSET]{k7}, ZW2
        vmovdqu64       [INP0 + OFFSET]{k7}, ZW3
        vmovdqu64       [INP1 + OFFSET]{k7}, ZW4
        vmovdqu64       [INP2 + OFFSET]{k7}, ZW5
        vmovdqu64       [INP3 + OFFSET]{k7}, ZW6
        vmovdqu64       [INP4 + OFFSET]{k7}, ZW7

        mov             IA0, [STATE + _des_args_out + (8*PTR_SZ)]
        mov             IA1, [STATE + _des_args_out + (9*PTR_SZ)]
        mov             IA2, [STATE + _des_args_out + (10*PTR_SZ)]
        mov             INP0, [STATE + _des_args_out + (11*PTR_SZ)]
        mov             INP1, [STATE + _des_args_out + (12*PTR_SZ)]
        mov             INP2, [STATE + _des_args_out + (13*PTR_SZ)]
        mov             INP3, [STATE + _des_args_out + (14*PTR_SZ)]
        mov             INP4, [STATE + _des_args_out + (15*PTR_SZ)]
        vmovdqu64       [IA0 + OFFSET]{k7}, ZW8
        vmovdqu64       [IA1 + OFFSET]{k7}, ZW9
        vmovdqu64       [IA2 + OFFSET]{k7}, ZW10
        vmovdqu64       [INP0 + OFFSET]{k7}, ZW11
        vmovdqu64       [INP1 + OFFSET]{k7}, ZW12
        vmovdqu64       [INP2 + OFFSET]{k7}, ZW13
        vmovdqu64       [INP3 + OFFSET]{k7}, ZW14
        vmovdqu64       [INP4 + OFFSET]{k7}, ZW15
%%_gen_des_enc_part_end:

        ;; store IV and update pointers
        DES_FINISH      ZIV0, ZIV1, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4

        ;; CFB part for DOCSIS
%ifidn %%DES_DOCSIS, DOCSIS
        DES_CFB_ONE     ENC, rsp + _key_sched, ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, rsp + _tmp_in, rsp + _tmp_out, rsp + _tmp_iv, rsp + _tmp_mask
%endif

        CLEAR_KEY_SCHEDULE %%DES_DOCSIS, ZW0

        ;; restore stack pointer and registers
        mov             r12, [rsp + _gpr_save + 0*8]
        mov             r13, [rsp + _gpr_save + 1*8]
        mov             r14, [rsp + _gpr_save + 2*8]
        mov             r15, [rsp + _gpr_save + 3*8]
	mov	        rsp, [rsp + _rsp_save]	; original SP

%ifdef SAFE_DATA
	clear_all_zmms_asm
%endif ;; SAFE_DATA

%endmacro

;;; ===========================================================================
;;; DES CBC / DOCSIS DES DECRYPT
;;; ===========================================================================
;;;
;;; DES_DOCSIS [in] - select between DES (DES CBC), DOCSIS (DOCSIS DES) and
;;;                   3DES (3DES CBC)
;;;
;;; NOTE: clobbers OpMask registers
%macro GENERIC_DES_DEC 1
%define %%DES_DOCSIS %1

        ;; push the registers and allocate the stack frame
	mov	        rax, rsp
	sub	        rsp, STACKFRAME_size
	and	        rsp, -64
	mov	        [rsp + _rsp_save], rax	; original SP
        mov             [rsp + _gpr_save + 0*8], r12
        mov             [rsp + _gpr_save + 1*8], r13
        mov             [rsp + _gpr_save + 2*8], r14
        mov             [rsp + _gpr_save + 3*8], r15

%ifnidn %%DES_DOCSIS, 3DES
        ;; DES and DOCSIS
        DES_INIT        STATE + _des_args_keys, STATE + _des_args_IV, rsp + _key_sched, ZIV0, ZIV1, ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11
%else
        ;; 3DES
        DES3_INIT       STATE + _des_args_keys, STATE + _des_args_IV, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3, ZIV0, ZIV1, ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11, DEC
%endif

        ;; CFB part for DOCSIS
%ifidn %%DES_DOCSIS, DOCSIS
        DES_CFB_ONE     DEC, rsp + _key_sched, ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, rsp + _tmp_in, rsp + _tmp_out, rsp + _tmp_iv, rsp + _tmp_mask
%endif

        mov             [rsp + _size_save], SIZE
        and             SIZE, -64
        xor             OFFSET, OFFSET
        ;; This loop processes message in blocks of 64 bytes.
        ;; Anything smaller than 64 bytes is handled separately after the loop.
%%_gen_des_dec_loop:
        cmp             OFFSET, SIZE
        jz              %%_gen_des_dec_loop_end
        ;; run loads
        mov             IA0, [STATE + _des_args_in + (0*PTR_SZ)]
        mov             IA1, [STATE + _des_args_in + (1*PTR_SZ)]
        mov             IA2, [STATE + _des_args_in + (2*PTR_SZ)]
        mov             INP0, [STATE + _des_args_in + (3*PTR_SZ)]
        mov             INP1, [STATE + _des_args_in + (4*PTR_SZ)]
        mov             INP2, [STATE + _des_args_in + (5*PTR_SZ)]
        mov             INP3, [STATE + _des_args_in + (6*PTR_SZ)]
        mov             INP4, [STATE + _des_args_in + (7*PTR_SZ)]
        vmovdqu64       ZW0, [IA0 + OFFSET]
        vmovdqu64       ZW1, [IA1 + OFFSET]
        vmovdqu64       ZW2, [IA2 + OFFSET]
        vmovdqu64       ZW3, [INP0 + OFFSET]
        vmovdqu64       ZW4, [INP1 + OFFSET]
        vmovdqu64       ZW5, [INP2 + OFFSET]
        vmovdqu64       ZW6, [INP3 + OFFSET]
        vmovdqu64       ZW7, [INP4 + OFFSET]

        mov             IA0, [STATE + _des_args_in + (8*PTR_SZ)]
        mov             IA1, [STATE + _des_args_in + (9*PTR_SZ)]
        mov             IA2, [STATE + _des_args_in + (10*PTR_SZ)]
        mov             INP0, [STATE + _des_args_in + (11*PTR_SZ)]
        mov             INP1, [STATE + _des_args_in + (12*PTR_SZ)]
        mov             INP2, [STATE + _des_args_in + (13*PTR_SZ)]
        mov             INP3, [STATE + _des_args_in + (14*PTR_SZ)]
        mov             INP4, [STATE + _des_args_in + (15*PTR_SZ)]
        vmovdqu64       ZW8, [IA0 + OFFSET]
        vmovdqu64       ZW9, [IA1 + OFFSET]
        vmovdqu64       ZW10, [IA2 + OFFSET]
        vmovdqu64       ZW11, [INP0 + OFFSET]
        vmovdqu64       ZW12, [INP1 + OFFSET]
        vmovdqu64       ZW13, [INP2 + OFFSET]
        vmovdqu64       ZW14, [INP3 + OFFSET]
        vmovdqu64       ZW15, [INP4 + OFFSET]

        ;; Transpose input
        TRANSPOSE_IN   ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11, ZTMP12, ZTMP13

%ifnidn %%DES_DOCSIS, 3DES
        ;; DES CBC DEC comes here
        GEN_DES_DEC_CIPHER 8, rsp + _key_sched
%else
        ;; 3DES CBC DEC comes here
        GEN_3DES_DEC_CIPHER 8, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif

        ;; transpose data on output
        TRANSPOSE_OUT   ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11, ZTMP12, ZTMP13

        ;; run stores
        mov             IA0, [STATE + _des_args_out + (0*PTR_SZ)]
        mov             IA1, [STATE + _des_args_out + (1*PTR_SZ)]
        mov             IA2, [STATE + _des_args_out + (2*PTR_SZ)]
        mov             INP0, [STATE + _des_args_out + (3*PTR_SZ)]
        mov             INP1, [STATE + _des_args_out + (4*PTR_SZ)]
        mov             INP2, [STATE + _des_args_out + (5*PTR_SZ)]
        mov             INP3, [STATE + _des_args_out + (6*PTR_SZ)]
        mov             INP4, [STATE + _des_args_out + (7*PTR_SZ)]
        vmovdqu64       [IA0 + OFFSET], ZW0
        vmovdqu64       [IA1 + OFFSET], ZW1
        vmovdqu64       [IA2 + OFFSET], ZW2
        vmovdqu64       [INP0 + OFFSET], ZW3
        vmovdqu64       [INP1 + OFFSET], ZW4
        vmovdqu64       [INP2 + OFFSET], ZW5
        vmovdqu64       [INP3 + OFFSET], ZW6
        vmovdqu64       [INP4 + OFFSET], ZW7

        mov             IA0, [STATE + _des_args_out + (8*PTR_SZ)]
        mov             IA1, [STATE + _des_args_out + (9*PTR_SZ)]
        mov             IA2, [STATE + _des_args_out + (10*PTR_SZ)]
        mov             INP0, [STATE + _des_args_out + (11*PTR_SZ)]
        mov             INP1, [STATE + _des_args_out + (12*PTR_SZ)]
        mov             INP2, [STATE + _des_args_out + (13*PTR_SZ)]
        mov             INP3, [STATE + _des_args_out + (14*PTR_SZ)]
        mov             INP4, [STATE + _des_args_out + (15*PTR_SZ)]
        vmovdqu64       [IA0 + OFFSET], ZW8
        vmovdqu64       [IA1 + OFFSET], ZW9
        vmovdqu64       [IA2 + OFFSET], ZW10
        vmovdqu64       [INP0 + OFFSET], ZW11
        vmovdqu64       [INP1 + OFFSET], ZW12
        vmovdqu64       [INP2 + OFFSET], ZW13
        vmovdqu64       [INP3 + OFFSET], ZW14
        vmovdqu64       [INP4 + OFFSET], ZW15

        add             OFFSET, 64
        jmp             %%_gen_des_dec_loop
%%_gen_des_dec_loop_end:
        ;; This is where we check if there is anything less than 64 bytes
        ;; of message left for processing.
        mov             SIZE, [rsp + _size_save]
        cmp             OFFSET, SIZE
        jz              %%_gen_des_dec_part_end
        ;; calculate min of bytes_left and 64, convert to qword mask
        GET_MASK8       IA0        ; IA0 = mask

        kmovw           k7, DWORD(IA0)
        mov             [rsp + _mask_save], IA0
        ;; run masked loads
        mov             IA0, [STATE + _des_args_in + (0*PTR_SZ)]
        mov             IA1, [STATE + _des_args_in + (1*PTR_SZ)]
        mov             IA2, [STATE + _des_args_in + (2*PTR_SZ)]
        mov             INP0, [STATE + _des_args_in + (3*PTR_SZ)]
        mov             INP1, [STATE + _des_args_in + (4*PTR_SZ)]
        mov             INP2, [STATE + _des_args_in + (5*PTR_SZ)]
        mov             INP3, [STATE + _des_args_in + (6*PTR_SZ)]
        mov             INP4, [STATE + _des_args_in + (7*PTR_SZ)]
        vmovdqu64       ZW0{k7}{z}, [IA0 + OFFSET]
        vmovdqu64       ZW1{k7}{z}, [IA1 + OFFSET]
        vmovdqu64       ZW2{k7}{z}, [IA2 + OFFSET]
        vmovdqu64       ZW3{k7}{z}, [INP0 + OFFSET]
        vmovdqu64       ZW4{k7}{z}, [INP1 + OFFSET]
        vmovdqu64       ZW5{k7}{z}, [INP2 + OFFSET]
        vmovdqu64       ZW6{k7}{z}, [INP3 + OFFSET]
        vmovdqu64       ZW7{k7}{z}, [INP4 + OFFSET]

        mov             IA0, [STATE + _des_args_in + (8*PTR_SZ)]
        mov             IA1, [STATE + _des_args_in + (9*PTR_SZ)]
        mov             IA2, [STATE + _des_args_in + (10*PTR_SZ)]
        mov             INP0, [STATE + _des_args_in + (11*PTR_SZ)]
        mov             INP1, [STATE + _des_args_in + (12*PTR_SZ)]
        mov             INP2, [STATE + _des_args_in + (13*PTR_SZ)]
        mov             INP3, [STATE + _des_args_in + (14*PTR_SZ)]
        mov             INP4, [STATE + _des_args_in + (15*PTR_SZ)]
        vmovdqu64       ZW8{k7}{z}, [IA0 + OFFSET]
        vmovdqu64       ZW9{k7}{z}, [IA1 + OFFSET]
        vmovdqu64       ZW10{k7}{z}, [IA2 + OFFSET]
        vmovdqu64       ZW11{k7}{z}, [INP0 + OFFSET]
        vmovdqu64       ZW12{k7}{z}, [INP1 + OFFSET]
        vmovdqu64       ZW13{k7}{z}, [INP2 + OFFSET]
        vmovdqu64       ZW14{k7}{z}, [INP3 + OFFSET]
        vmovdqu64       ZW15{k7}{z}, [INP4 + OFFSET]

        ;; Transpose input
        TRANSPOSE_IN    ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11, ZTMP12, ZTMP13

        ;; DES CBC DEC comes here
        mov             IA0, [rsp + _mask_save]
        cmp             BYTE(IA0), 0x0f
        ja              %%_gt_4
        jz              %%_blocks_4

        cmp             BYTE(IA0), 0x03
        ja              %%_blocks_3
        jz              %%_blocks_2
        ;; process one block and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_DEC_CIPHER 1, rsp + _key_sched
%else
        GEN_3DES_DEC_CIPHER 1, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_blocks_2:
        ;; process two blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_DEC_CIPHER 2, rsp + _key_sched
%else
        GEN_3DES_DEC_CIPHER 2, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_blocks_3:
        ;; process three blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_DEC_CIPHER 3, rsp + _key_sched
%else
        GEN_3DES_DEC_CIPHER 3, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_blocks_4:
        ;; process four blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_DEC_CIPHER 4, rsp + _key_sched
%else
        GEN_3DES_DEC_CIPHER 4, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_gt_4:
        cmp             BYTE(IA0), 0x3f
        ja              %%_blocks_7
        jz              %%_blocks_6
%%_blocks_5:
        ;; process five blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_DEC_CIPHER 5, rsp + _key_sched
%else
        GEN_3DES_DEC_CIPHER 5, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_blocks_6:
        ;; process six blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_DEC_CIPHER 6, rsp + _key_sched
%else
        GEN_3DES_DEC_CIPHER 6, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif
        jmp             %%_transpose_out

%%_blocks_7:
        ;; process seven blocks and move to transpose out
%ifnidn %%DES_DOCSIS, 3DES
        GEN_DES_DEC_CIPHER 7, rsp + _key_sched
%else
        GEN_3DES_DEC_CIPHER 7, rsp + _key_sched, rsp + _key_sched2, rsp + _key_sched3
%endif

%%_transpose_out:
        ;; transpose data on output
        TRANSPOSE_OUT   ZW0, ZW1, ZW2, ZW3, ZW4, ZW5, ZW6, ZW7, ZW8, ZW9, ZW10, ZW11, ZW12, ZW13, ZW14, ZW15, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4, ZTMP5, ZTMP6, ZTMP7, ZTMP8, ZTMP9, ZTMP10, ZTMP11, ZTMP12, ZTMP13

        ;; run masked stores
        mov             IA0, [STATE + _des_args_out + (0*PTR_SZ)]
        mov             IA1, [STATE + _des_args_out + (1*PTR_SZ)]
        mov             IA2, [STATE + _des_args_out + (2*PTR_SZ)]
        mov             INP0, [STATE + _des_args_out + (3*PTR_SZ)]
        mov             INP1, [STATE + _des_args_out + (4*PTR_SZ)]
        mov             INP2, [STATE + _des_args_out + (5*PTR_SZ)]
        mov             INP3, [STATE + _des_args_out + (6*PTR_SZ)]
        mov             INP4, [STATE + _des_args_out + (7*PTR_SZ)]
        vmovdqu64       [IA0 + OFFSET]{k7}, ZW0
        vmovdqu64       [IA1 + OFFSET]{k7}, ZW1
        vmovdqu64       [IA2 + OFFSET]{k7}, ZW2
        vmovdqu64       [INP0 + OFFSET]{k7}, ZW3
        vmovdqu64       [INP1 + OFFSET]{k7}, ZW4
        vmovdqu64       [INP2 + OFFSET]{k7}, ZW5
        vmovdqu64       [INP3 + OFFSET]{k7}, ZW6
        vmovdqu64       [INP4 + OFFSET]{k7}, ZW7

        mov             IA0, [STATE + _des_args_out + (8*PTR_SZ)]
        mov             IA1, [STATE + _des_args_out + (9*PTR_SZ)]
        mov             IA2, [STATE + _des_args_out + (10*PTR_SZ)]
        mov             INP0, [STATE + _des_args_out + (11*PTR_SZ)]
        mov             INP1, [STATE + _des_args_out + (12*PTR_SZ)]
        mov             INP2, [STATE + _des_args_out + (13*PTR_SZ)]
        mov             INP3, [STATE + _des_args_out + (14*PTR_SZ)]
        mov             INP4, [STATE + _des_args_out + (15*PTR_SZ)]
        vmovdqu64       [IA0 + OFFSET]{k7}, ZW8
        vmovdqu64       [IA1 + OFFSET]{k7}, ZW9
        vmovdqu64       [IA2 + OFFSET]{k7}, ZW10
        vmovdqu64       [INP0 + OFFSET]{k7}, ZW11
        vmovdqu64       [INP1 + OFFSET]{k7}, ZW12
        vmovdqu64       [INP2 + OFFSET]{k7}, ZW13
        vmovdqu64       [INP3 + OFFSET]{k7}, ZW14
        vmovdqu64       [INP4 + OFFSET]{k7}, ZW15
%%_gen_des_dec_part_end:

        ;; store IV and update pointers
        DES_FINISH      ZIV0, ZIV1, ZTMP0, ZTMP1, ZTMP2, ZTMP3, ZTMP4

        CLEAR_KEY_SCHEDULE %%DES_DOCSIS, ZW0

        ;; restore stack pointer and registers
        mov             r12, [rsp + _gpr_save + 0*8]
        mov             r13, [rsp + _gpr_save + 1*8]
        mov             r14, [rsp + _gpr_save + 2*8]
        mov             r15, [rsp + _gpr_save + 3*8]
	mov	        rsp, [rsp + _rsp_save]	; original SP

%ifdef SAFE_DATA
	clear_all_zmms_asm
%endif ;; SAFE_DATA

%endmacro

;;; ========================================================
;;; DATA

mksection .rodata
default rel
align 64
mask_values:
        dd 0x04000000, 0x04000000, 0x04000000, 0x04000000
        dd 0x04000000, 0x04000000, 0x04000000, 0x04000000
        dd 0x04000000, 0x04000000, 0x04000000, 0x04000000
        dd 0x04000000, 0x04000000, 0x04000000, 0x04000000
        dd 0x40240202, 0x40240202, 0x40240202, 0x40240202
        dd 0x40240202, 0x40240202, 0x40240202, 0x40240202
        dd 0x40240202, 0x40240202, 0x40240202, 0x40240202
        dd 0x40240202, 0x40240202, 0x40240202, 0x40240202
        dd 0x00001110, 0x00001110, 0x00001110, 0x00001110
        dd 0x00001110, 0x00001110, 0x00001110, 0x00001110
        dd 0x00001110, 0x00001110, 0x00001110, 0x00001110
        dd 0x00001110, 0x00001110, 0x00001110, 0x00001110
        dd 0x01088000, 0x01088000, 0x01088000, 0x01088000
        dd 0x01088000, 0x01088000, 0x01088000, 0x01088000
        dd 0x01088000, 0x01088000, 0x01088000, 0x01088000
        dd 0x01088000, 0x01088000, 0x01088000, 0x01088000
        dd 0x00000001, 0x00000001, 0x00000001, 0x00000001
        dd 0x00000001, 0x00000001, 0x00000001, 0x00000001
        dd 0x00000001, 0x00000001, 0x00000001, 0x00000001
        dd 0x00000001, 0x00000001, 0x00000001, 0x00000001
        dd 0x0081000C, 0x0081000C, 0x0081000C, 0x0081000C
        dd 0x0081000C, 0x0081000C, 0x0081000C, 0x0081000C
        dd 0x0081000C, 0x0081000C, 0x0081000C, 0x0081000C
        dd 0x0081000C, 0x0081000C, 0x0081000C, 0x0081000C
        dd 0x00000020, 0x00000020, 0x00000020, 0x00000020
        dd 0x00000020, 0x00000020, 0x00000020, 0x00000020
        dd 0x00000020, 0x00000020, 0x00000020, 0x00000020
        dd 0x00000020, 0x00000020, 0x00000020, 0x00000020
        dd 0x00000040, 0x00000040, 0x00000040, 0x00000040
        dd 0x00000040, 0x00000040, 0x00000040, 0x00000040
        dd 0x00000040, 0x00000040, 0x00000040, 0x00000040
        dd 0x00000040, 0x00000040, 0x00000040, 0x00000040
        dd 0x00400400, 0x00400400, 0x00400400, 0x00400400
        dd 0x00400400, 0x00400400, 0x00400400, 0x00400400
        dd 0x00400400, 0x00400400, 0x00400400, 0x00400400
        dd 0x00400400, 0x00400400, 0x00400400, 0x00400400
        dd 0x00000800, 0x00000800, 0x00000800, 0x00000800
        dd 0x00000800, 0x00000800, 0x00000800, 0x00000800
        dd 0x00000800, 0x00000800, 0x00000800, 0x00000800
        dd 0x00000800, 0x00000800, 0x00000800, 0x00000800
        dd 0x00002000, 0x00002000, 0x00002000, 0x00002000
        dd 0x00002000, 0x00002000, 0x00002000, 0x00002000
        dd 0x00002000, 0x00002000, 0x00002000, 0x00002000
        dd 0x00002000, 0x00002000, 0x00002000, 0x00002000
        dd 0x00100000, 0x00100000, 0x00100000, 0x00100000
        dd 0x00100000, 0x00100000, 0x00100000, 0x00100000
        dd 0x00100000, 0x00100000, 0x00100000, 0x00100000
        dd 0x00100000, 0x00100000, 0x00100000, 0x00100000
        dd 0x00004000, 0x00004000, 0x00004000, 0x00004000
        dd 0x00004000, 0x00004000, 0x00004000, 0x00004000
        dd 0x00004000, 0x00004000, 0x00004000, 0x00004000
        dd 0x00004000, 0x00004000, 0x00004000, 0x00004000
        dd 0x00020000, 0x00020000, 0x00020000, 0x00020000
        dd 0x00020000, 0x00020000, 0x00020000, 0x00020000
        dd 0x00020000, 0x00020000, 0x00020000, 0x00020000
        dd 0x00020000, 0x00020000, 0x00020000, 0x00020000
        dd 0x02000000, 0x02000000, 0x02000000, 0x02000000
        dd 0x02000000, 0x02000000, 0x02000000, 0x02000000
        dd 0x02000000, 0x02000000, 0x02000000, 0x02000000
        dd 0x02000000, 0x02000000, 0x02000000, 0x02000000
        dd 0x08000000, 0x08000000, 0x08000000, 0x08000000
        dd 0x08000000, 0x08000000, 0x08000000, 0x08000000
        dd 0x08000000, 0x08000000, 0x08000000, 0x08000000
        dd 0x08000000, 0x08000000, 0x08000000, 0x08000000
        dd 0x00000080, 0x00000080, 0x00000080, 0x00000080
        dd 0x00000080, 0x00000080, 0x00000080, 0x00000080
        dd 0x00000080, 0x00000080, 0x00000080, 0x00000080
        dd 0x00000080, 0x00000080, 0x00000080, 0x00000080
        dd 0x20000000, 0x20000000, 0x20000000, 0x20000000
        dd 0x20000000, 0x20000000, 0x20000000, 0x20000000
        dd 0x20000000, 0x20000000, 0x20000000, 0x20000000
        dd 0x20000000, 0x20000000, 0x20000000, 0x20000000
        dd 0x90000000, 0x90000000, 0x90000000, 0x90000000
        dd 0x90000000, 0x90000000, 0x90000000, 0x90000000
        dd 0x90000000, 0x90000000, 0x90000000, 0x90000000
        dd 0x90000000, 0x90000000, 0x90000000, 0x90000000

align 64
init_perm_consts:
        dd 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f
        dd 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f
        dd 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f
        dd 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f
        dd 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff
        dd 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff
        dd 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff
        dd 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff
        dd 0x33333333, 0x33333333, 0x33333333, 0x33333333
        dd 0x33333333, 0x33333333, 0x33333333, 0x33333333
        dd 0x33333333, 0x33333333, 0x33333333, 0x33333333
        dd 0x33333333, 0x33333333, 0x33333333, 0x33333333
        dd 0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff
        dd 0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff
        dd 0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff
        dd 0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff
        dd 0x55555555, 0x55555555, 0x55555555, 0x55555555
        dd 0x55555555, 0x55555555, 0x55555555, 0x55555555
        dd 0x55555555, 0x55555555, 0x55555555, 0x55555555
        dd 0x55555555, 0x55555555, 0x55555555, 0x55555555

;;; S-Box table
align 64
S_box_flipped:
        ;; SBOX0
        dw 0x07, 0x02, 0x0c, 0x0f, 0x04, 0x0b, 0x0a, 0x0c
        dw 0x0b, 0x07, 0x06, 0x09, 0x0d, 0x04, 0x00, 0x0a
        dw 0x02, 0x08, 0x05, 0x03, 0x0f, 0x06, 0x09, 0x05
        dw 0x08, 0x01, 0x03, 0x0e, 0x01, 0x0d, 0x0e, 0x00
        dw 0x00, 0x0f, 0x05, 0x0a, 0x07, 0x02, 0x09, 0x05
        dw 0x0e, 0x01, 0x03, 0x0c, 0x0b, 0x08, 0x0c, 0x06
        dw 0x0f, 0x03, 0x06, 0x0d, 0x04, 0x09, 0x0a, 0x00
        dw 0x02, 0x04, 0x0d, 0x07, 0x08, 0x0e, 0x01, 0x0b
        ;; SBOX1
        dw 0x0f, 0x00, 0x09, 0x0a, 0x06, 0x05, 0x03, 0x09
        dw 0x01, 0x0e, 0x04, 0x03, 0x0c, 0x0b, 0x0a, 0x04
        dw 0x08, 0x07, 0x0e, 0x01, 0x0d, 0x02, 0x00, 0x0c
        dw 0x07, 0x0d, 0x0b, 0x06, 0x02, 0x08, 0x05, 0x0f
        dw 0x0c, 0x0b, 0x03, 0x0d, 0x0f, 0x0c, 0x06, 0x00
        dw 0x02, 0x05, 0x08, 0x0e, 0x01, 0x02, 0x0d, 0x07
        dw 0x0b, 0x01, 0x00, 0x06, 0x04, 0x0f, 0x09, 0x0a
        dw 0x0e, 0x08, 0x05, 0x03, 0x07, 0x04, 0x0a, 0x09
        ;; SBOX2
        dw 0x05, 0x0b, 0x08, 0x0d, 0x06, 0x01, 0x0d, 0x0a
        dw 0x09, 0x02, 0x03, 0x04, 0x0f, 0x0c, 0x04, 0x07
        dw 0x00, 0x06, 0x0b, 0x08, 0x0c, 0x0f, 0x02, 0x05
        dw 0x07, 0x09, 0x0e, 0x03, 0x0a, 0x00, 0x01, 0x0e
        dw 0x0b, 0x08, 0x04, 0x02, 0x0c, 0x06, 0x03, 0x0d
        dw 0x00, 0x0b, 0x0a, 0x07, 0x06, 0x01, 0x0f, 0x04
        dw 0x0e, 0x05, 0x01, 0x0f, 0x02, 0x09, 0x0d, 0x0a
        dw 0x09, 0x00, 0x07, 0x0c, 0x05, 0x0e, 0x08, 0x03
        ;; SBOX3
        dw 0x0e, 0x05, 0x08, 0x0f, 0x00, 0x03, 0x0d, 0x0a
        dw 0x07, 0x09, 0x01, 0x0c, 0x09, 0x0e, 0x02, 0x01
        dw 0x0b, 0x06, 0x04, 0x08, 0x06, 0x0d, 0x03, 0x04
        dw 0x0c, 0x00, 0x0a, 0x07, 0x05, 0x0b, 0x0f, 0x02
        dw 0x0b, 0x0c, 0x02, 0x09, 0x06, 0x05, 0x08, 0x03
        dw 0x0d, 0x00, 0x04, 0x0a, 0x00, 0x0b, 0x07, 0x04
        dw 0x01, 0x0f, 0x0e, 0x02, 0x0f, 0x08, 0x05, 0x0e
        dw 0x0a, 0x06, 0x03, 0x0d, 0x0c, 0x01, 0x09, 0x07
        ;; SBOX4
        dw 0x04, 0x02, 0x01, 0x0f, 0x0e, 0x05, 0x0b, 0x06
        dw 0x02, 0x08, 0x0c, 0x03, 0x0d, 0x0e, 0x07, 0x00
        dw 0x03, 0x04, 0x0a, 0x09, 0x05, 0x0b, 0x00, 0x0c
        dw 0x08, 0x0d, 0x0f, 0x0a, 0x06, 0x01, 0x09, 0x07
        dw 0x07, 0x0d, 0x0a, 0x06, 0x02, 0x08, 0x0c, 0x05
        dw 0x04, 0x03, 0x0f, 0x00, 0x0b, 0x04, 0x01, 0x0a
        dw 0x0d, 0x01, 0x00, 0x0f, 0x0e, 0x07, 0x09, 0x02
        dw 0x03, 0x0e, 0x05, 0x09, 0x08, 0x0b, 0x06, 0x0c
        ;; SBOX5
        dw 0x03, 0x09, 0x00, 0x0e, 0x09, 0x04, 0x07, 0x08
        dw 0x05, 0x0f, 0x0c, 0x02, 0x06, 0x03, 0x0a, 0x0d
        dw 0x08, 0x07, 0x0b, 0x00, 0x04, 0x01, 0x0e, 0x0b
        dw 0x0f, 0x0a, 0x02, 0x05, 0x01, 0x0c, 0x0d, 0x06
        dw 0x05, 0x02, 0x06, 0x0d, 0x0e, 0x09, 0x00, 0x06
        dw 0x02, 0x04, 0x0b, 0x08, 0x09, 0x0f, 0x0c, 0x01
        dw 0x0f, 0x0c, 0x08, 0x07, 0x03, 0x0a, 0x0d, 0x00
        dw 0x04, 0x03, 0x07, 0x0e, 0x0a, 0x05, 0x01, 0x0b
        ;; SBOX6
        dw 0x02, 0x08, 0x0c, 0x05, 0x0f, 0x03, 0x0a, 0x00
        dw 0x04, 0x0d, 0x09, 0x06, 0x01, 0x0e, 0x06, 0x09
        dw 0x0d, 0x02, 0x03, 0x0f, 0x00, 0x0c, 0x05, 0x0a
        dw 0x07, 0x0b, 0x0e, 0x01, 0x0b, 0x07, 0x08, 0x04
        dw 0x0b, 0x06, 0x07, 0x09, 0x02, 0x08, 0x04, 0x07
        dw 0x0d, 0x0b, 0x0a, 0x00, 0x08, 0x05, 0x01, 0x0c
        dw 0x00, 0x0d, 0x0c, 0x0a, 0x09, 0x02, 0x0f, 0x04
        dw 0x0e, 0x01, 0x03, 0x0f, 0x05, 0x0e, 0x06, 0x03
        ;; SBOX7
        dw 0x0b, 0x0e, 0x05, 0x00, 0x06, 0x09, 0x0a, 0x0f
        dw 0x01, 0x02, 0x0c, 0x05, 0x0d, 0x07, 0x03, 0x0a
        dw 0x04, 0x0d, 0x09, 0x06, 0x0f, 0x03, 0x00, 0x0c
        dw 0x02, 0x08, 0x07, 0x0b, 0x08, 0x04, 0x0e, 0x01
        dw 0x08, 0x04, 0x03, 0x0f, 0x05, 0x02, 0x00, 0x0c
        dw 0x0b, 0x07, 0x06, 0x09, 0x0e, 0x01, 0x09, 0x06
        dw 0x0f, 0x08, 0x0a, 0x03, 0x0c, 0x05, 0x07, 0x0a
        dw 0x01, 0x0e, 0x0d, 0x00, 0x02, 0x0b, 0x04, 0x0d

;;; Used in DOCSIS DES partial block scheduling 16 x 32bit of value 1
align 64
vec_ones_32b:
        dd 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1

align 64
and_eu:
        dd 0x3f003f00, 0x3f003f00, 0x3f003f00, 0x3f003f00
        dd 0x3f003f00, 0x3f003f00, 0x3f003f00, 0x3f003f00
        dd 0x3f003f00, 0x3f003f00, 0x3f003f00, 0x3f003f00
        dd 0x3f003f00, 0x3f003f00, 0x3f003f00, 0x3f003f00

align 64
and_ed:
        dd 0x003f003f, 0x003f003f, 0x003f003f, 0x003f003f
        dd 0x003f003f, 0x003f003f, 0x003f003f, 0x003f003f
        dd 0x003f003f, 0x003f003f, 0x003f003f, 0x003f003f
        dd 0x003f003f, 0x003f003f, 0x003f003f, 0x003f003f

align 64
idx_e:
        dq 0x0d0c090805040100, 0x0f0e0b0a07060302
        dq 0x1d1c191815141110, 0x1f1e1b1a17161312
        dq 0x2d2c292825242120, 0x2f2e2b2a27262322
        dq 0x3d3c393835343130, 0x3f3e3b3a37363332

align 64
reg_values16bit_7:
        dq 0x001f001f001f001f, 0x001f001f001f001f
        dq 0x001f001f001f001f, 0x001f001f001f001f
        dq 0x001f001f001f001f, 0x001f001f001f001f
        dq 0x001f001f001f001f, 0x001f001f001f001f

align 64
shuffle_reg:
        dq 0x0705060403010200, 0x0f0d0e0c0b090a08
        dq 0x1715161413111210, 0x1f1d1e1c1b191a18
        dq 0x2725262423212220, 0x2f2d2e2c2b292a28
        dq 0x3735363433313230, 0x3f3d3e3c3b393a38

;;; ========================================================
;;; CODE
mksection .text

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : size in bytes
align 64
MKGLOBAL(des_x16_cbc_enc_avx512,function,internal)
des_x16_cbc_enc_avx512:
        endbranch64
        GENERIC_DES_ENC DES
        ret

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : size in bytes
align 64
MKGLOBAL(des_x16_cbc_dec_avx512,function,internal)
des_x16_cbc_dec_avx512:
        endbranch64
        GENERIC_DES_DEC DES
	ret

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : size in bytes
align 64
MKGLOBAL(des3_x16_cbc_enc_avx512,function,internal)
des3_x16_cbc_enc_avx512:
        endbranch64
        GENERIC_DES_ENC 3DES
        ret

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : size in bytes
align 64
MKGLOBAL(des3_x16_cbc_dec_avx512,function,internal)
des3_x16_cbc_dec_avx512:
        endbranch64
        GENERIC_DES_DEC 3DES
	ret

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : size in bytes
align 64
MKGLOBAL(docsis_des_x16_enc_avx512,function,internal)
docsis_des_x16_enc_avx512:
        endbranch64
        GENERIC_DES_ENC DOCSIS
	ret

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : size in bytes
align 64
MKGLOBAL(docsis_des_x16_dec_avx512,function,internal)
docsis_des_x16_dec_avx512:
        endbranch64
        GENERIC_DES_DEC DOCSIS
	ret

mksection stack-noexec

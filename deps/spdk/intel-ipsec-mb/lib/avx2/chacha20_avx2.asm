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

%include "include/os.asm"
%include "include/imb_job.asm"
%include "include/memcpy.asm"
%include "include/clear_regs.asm"
%include "include/transpose_avx2.asm"
%include "include/chacha_poly_defines.asm"
%include "include/cet.inc"
mksection .rodata
default rel

align 32
constants:
dd      0x61707865, 0x3320646e, 0x79622d32, 0x6b206574
dd      0x61707865, 0x3320646e, 0x79622d32, 0x6b206574

align 16
dword_1:
dd      0x00000001, 0x00000000, 0x00000000, 0x00000000

align 16
dword_2:
dd      0x00000002, 0x00000000, 0x00000000, 0x00000000

align 32
dword_1_8:
dd      0x00000001, 0x00000002, 0x00000003, 0x00000004
dd      0x00000005, 0x00000006, 0x00000007, 0x00000008

align 32
dword_8:
dd      0x00000008, 0x00000008, 0x00000008, 0x00000008
dd      0x00000008, 0x00000008, 0x00000008, 0x00000008

align 32
add_1_2:
dd      0x00000001, 0x00000000, 0x00000000, 0x00000000
dd      0x00000002, 0x00000000, 0x00000000, 0x00000000

align 32
add_3_4:
dd      0x00000003, 0x00000000, 0x00000000, 0x00000000
dd      0x00000004, 0x00000000, 0x00000000, 0x00000000

align 32
shuf_mask_rotl8:
db      3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14
db      3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14

align 32
shuf_mask_rotl16:
db      2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13
db      2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13

struc STACK
_STATE:         reso    32      ; Space to store first 8 states
_YMM_SAVE:      resy    2       ; Space to store up to 2 temporary YMM registers
_GP_SAVE:       resq    7       ; Space to store up to 7 GP registers
_RSP_SAVE:      resq    1       ; Space to store rsp pointer
endstruc
%define STACK_SIZE STACK_size

%ifdef LINUX
%define arg1    rdi
%define arg2    rsi
%define arg3    rdx
%define arg4    rcx
%define arg5    r8
%else
%define arg1    rcx
%define arg2    rdx
%define arg3    r8
%define arg4    r9
%define arg5    [rsp + 40]
%endif

%define job     arg1

%define APPEND(a,b) a %+ b

mksection .text

%macro ENCRYPT_0B_64B 10-11
%define %%SRC  %1 ; [in/out] Source pointer
%define %%DST  %2 ; [in/out] Destination pointer
%define %%LEN  %3 ; [in/clobbered] Length to encrypt
%define %%OFF  %4 ; [in] Offset into src/dst
%define %%KS0  %5 ; [in/out] Bytes 0-31 of keystream
%define %%KS1  %6 ; [in/out] Bytes 32-63 of keystream
%define %%PT0  %7 ; [in/clobbered] Bytes 0-31 of plaintext
%define %%PT1  %8 ; [in/clobbered] Bytes 32-63 of plaintext
%define %%TMP  %9 ; [clobbered] Temporary GP register
%define %%TMP2 %10 ; [clobbered] Temporary GP register
%define %%KS_PTR %11 ; [in] Pointer to keystream

        or      %%LEN, %%LEN
        jz      %%end_encrypt

        cmp     %%LEN, 32
        jbe     %%up_to_32B

%%up_to_64B:
        vmovdqu  %%PT0, [%%SRC + %%OFF]
%if %0 == 11
        vmovdqu  %%KS0, [%%KS_PTR]
        vmovdqu  %%KS1, [%%KS_PTR + 32]
%endif
        vpxor    %%PT0, %%KS0
        vmovdqu  [%%DST + %%OFF], %%PT0

        add     %%SRC, %%OFF
        add     %%DST, %%OFF
        add     %%SRC, 32
        add     %%DST, 32
        sub     %%LEN, 32

        simd_load_avx2 %%PT1, %%SRC, %%LEN, %%TMP, %%TMP2
        vpxor    %%PT1, %%KS1
        simd_store_avx2 %%DST, %%PT1, %%LEN, %%TMP, %%TMP2

        jmp     %%end_encrypt

%%up_to_32B:
        add     %%SRC, %%OFF
        add     %%DST, %%OFF
        ; XOR KS with plaintext and store resulting ciphertext
%if %0 == 11
        vmovdqu  %%KS0, [%%KS_PTR]
%endif
        simd_load_avx2 %%PT0, %%SRC, %%LEN, %%TMP, %%TMP2
        vpxor    %%PT0, %%KS0
        simd_store_avx2 %%DST, %%PT0, %%LEN, %%TMP, %%TMP2

%%end_encrypt:
        add     %%SRC, %%LEN
        add     %%DST, %%LEN
%endmacro

; Rotate dwords on a YMM registers to the left N_BITS
%macro VPROLD 3
%define %%YMM_IN %1 ; [in/out] YMM register to be rotated
%define %%N_BITS %2 ; [immediate] Number of bits to rotate
%define %%YTMP   %3 ; [clobbered] YMM temporary register
%if %%N_BITS == 8
        vpshufb %%YMM_IN, [rel shuf_mask_rotl8]
%elif %%N_BITS == 16
        vpshufb %%YMM_IN, [rel shuf_mask_rotl16]
%else
        vpsrld  %%YTMP, %%YMM_IN, (32-%%N_BITS)
        vpslld  %%YMM_IN, %%N_BITS
        vpor    %%YMM_IN, %%YTMP
%endif
%endmacro

;;
;; Performs a quarter round on all 4 columns,
;; resulting in a full round
;;
%macro QUARTER_ROUND_X4 5
%define %%A    %1 ;; [in/out] YMM register containing value A of all 4 columns
%define %%B    %2 ;; [in/out] YMM register containing value B of all 4 columns
%define %%C    %3 ;; [in/out] YMM register containing value C of all 4 columns
%define %%D    %4 ;; [in/out] YMM register containing value D of all 4 columns
%define %%YTMP %5 ;; [clobbered] Temporary YMM register

        vpaddd  %%A, %%B
        vpxor   %%D, %%A
        VPROLD  %%D, 16, %%YTMP
        vpaddd  %%C, %%D
        vpxor   %%B, %%C
        VPROLD  %%B, 12, %%YTMP
        vpaddd  %%A, %%B
        vpxor   %%D, %%A
        VPROLD  %%D, 8, %%YTMP
        vpaddd  %%C, %%D
        vpxor   %%B, %%C
        VPROLD  %%B, 7, %%YTMP

%endmacro

%macro QUARTER_ROUND_X8 9
%define %%A_L    %1 ;; [in/out] YMM register containing value A of all 4 columns
%define %%B_L    %2 ;; [in/out] YMM register containing value B of all 4 columns
%define %%C_L    %3 ;; [in/out] YMM register containing value C of all 4 columns
%define %%D_L    %4 ;; [in/out] YMM register containing value D of all 4 columns
%define %%A_H    %5 ;; [in/out] YMM register containing value A of all 4 columns
%define %%B_H    %6 ;; [in/out] YMM register containing value B of all 4 columns
%define %%C_H    %7 ;; [in/out] YMM register containing value C of all 4 columns
%define %%D_H    %8 ;; [in/out] YMM register containing value D of all 4 columns
%define %%YTMP   %9 ;; [clobbered] Temporary XMM register

        vpaddd  %%A_L, %%B_L
        vpaddd  %%A_H, %%B_H
        vpxor   %%D_L, %%A_L
        vpxor   %%D_H, %%A_H
        VPROLD  %%D_L, 16, %%YTMP
        VPROLD  %%D_H, 16, %%YTMP
        vpaddd  %%C_L, %%D_L
        vpaddd  %%C_H, %%D_H
        vpxor   %%B_L, %%C_L
        vpxor   %%B_H, %%C_H
        VPROLD  %%B_L, 12, %%YTMP
        VPROLD  %%B_H, 12, %%YTMP
        vpaddd  %%A_L, %%B_L
        vpaddd  %%A_H, %%B_H
        vpxor   %%D_L, %%A_L
        vpxor   %%D_H, %%A_H
        VPROLD  %%D_L, 8, %%YTMP
        VPROLD  %%D_H, 8, %%YTMP
        vpaddd  %%C_L, %%D_L
        vpaddd  %%C_H, %%D_H
        vpxor   %%B_L, %%C_L
        vpxor   %%B_H, %%C_H
        VPROLD  %%B_L, 7, %%YTMP
        VPROLD  %%B_H, 7, %%YTMP

%endmacro

;;
;; Rotates the registers to prepare the data
;; from column round to diagonal round
;;
%macro COLUMN_TO_DIAG 3
%define %%B %1 ;; [in/out] XMM register containing value B of all 4 columns
%define %%C %2 ;; [in/out] XMM register containing value C of all 4 columns
%define %%D %3 ;; [in/out] XMM register containing value D of all 4 columns

        vpshufd %%B, %%B, 0x39 ; 0b00111001 ;; 0,3,2,1
        vpshufd %%C, %%C, 0x4E ; 0b01001110 ;; 1,0,3,2
        vpshufd %%D, %%D, 0x93 ; 0b10010011 ;; 2,1,0,3

%endmacro

;;
;; Rotates the registers to prepare the data
;; from diagonal round to column round
;;
%macro DIAG_TO_COLUMN 3
%define %%B %1 ;; [in/out] XMM register containing value B of all 4 columns
%define %%C %2 ;; [in/out] XMM register containing value C of all 4 columns
%define %%D %3 ;; [in/out] XMM register containing value D of all 4 columns

        vpshufd %%B, %%B, 0x93 ; 0b10010011 ; 2,1,0,3
        vpshufd %%C, %%C, 0x4E ; 0b01001110 ; 1,0,3,2
        vpshufd %%D, %%D, 0x39 ; 0b00111001 ; 0,3,2,1

%endmacro

;;
;; Generates up to 64*4 bytes of keystream
;;
%macro GENERATE_256_KS 15
%define %%A_L_KS0        %1  ;; [out] YMM A / Bytes 0-31   of KS
%define %%B_L_KS2        %2  ;; [out] YMM B / Bytes 64-95  of KS
%define %%C_L_KS1        %3  ;; [out] YMM C / Bytes 32-63  of KS
%define %%D_L_KS3        %4  ;; [out] YMM D / Bytes 96-127 of KS
%define %%A_H_KS4        %5  ;; [out] YMM A / Bytes 128-159 of KS (or "none" in NUM_BLOCKS == 2)
%define %%B_H_KS6        %6  ;; [out] YMM B / Bytes 192-223 of KS (or "none" in NUM_BLOCKS == 2)
%define %%C_H_KS5        %7  ;; [out] YMM C / Bytes 160-191 of KS (or "none" in NUM_BLOCKS == 2)
%define %%D_H_KS7        %8  ;; [out] YMM D / Bytes 224-255 of KS (or "none" in NUM_BLOCKS == 2)
%define %%STATE_IN_A_L   %9  ;; [in] YMM containing state "A" part
%define %%STATE_IN_B_L   %10 ;; [in] YMM containing state "B" part
%define %%STATE_IN_C_L   %11 ;; [in] YMM containing state "C" part
%define %%STATE_IN_D_L   %12 ;; [in] YMM containing state "D" part
%define %%STATE_IN_D_H   %13 ;; [in] YMM containing state "D" part (or "none" in NUM_BLOCKS == 4)
%define %%YTMP           %14 ;; [clobbered] Temp YMM reg
%define %%NUM_BLOCKS     %15 ;; [in] Num blocks to encrypt (2 or 4)

        vmovdqa       %%A_L_KS0, %%STATE_IN_A_L
        vmovdqa       %%B_L_KS2, %%STATE_IN_B_L
        vmovdqa       %%C_L_KS1, %%STATE_IN_C_L
        vmovdqa       %%D_L_KS3, %%STATE_IN_D_L
%if %%NUM_BLOCKS == 4
        vmovdqa       %%A_H_KS4, %%STATE_IN_A_L
        vmovdqa       %%B_H_KS6, %%STATE_IN_B_L
        vmovdqa       %%C_H_KS5, %%STATE_IN_C_L
        vmovdqa       %%D_H_KS7, %%STATE_IN_D_H
%endif

%rep 10
%if %%NUM_BLOCKS == 2
        QUARTER_ROUND_X4 %%A_L_KS0, %%B_L_KS2, %%C_L_KS1, %%D_L_KS3, %%YTMP
        COLUMN_TO_DIAG %%B_L_KS2, %%C_L_KS1, %%D_L_KS3
        QUARTER_ROUND_X4 %%A_L_KS0, %%B_L_KS2, %%C_L_KS1, %%D_L_KS3, %%YTMP
        DIAG_TO_COLUMN %%B_L_KS2, %%C_L_KS1, %%D_L_KS3
%else
        QUARTER_ROUND_X8 %%A_L_KS0, %%B_L_KS2, %%C_L_KS1, %%D_L_KS3, \
                         %%A_H_KS4, %%B_H_KS6, %%C_H_KS5, %%D_H_KS7, %%YTMP
        COLUMN_TO_DIAG %%B_L_KS2, %%C_L_KS1, %%D_L_KS3
        COLUMN_TO_DIAG %%B_H_KS6, %%C_H_KS5, %%D_H_KS7
        QUARTER_ROUND_X8 %%A_L_KS0, %%B_L_KS2, %%C_L_KS1, %%D_L_KS3, \
                         %%A_H_KS4, %%B_H_KS6, %%C_H_KS5, %%D_H_KS7, %%YTMP
        DIAG_TO_COLUMN %%B_L_KS2, %%C_L_KS1, %%D_L_KS3
        DIAG_TO_COLUMN %%B_H_KS6, %%C_H_KS5, %%D_H_KS7
%endif ;; %%NUM_BLOCKS == 4
%endrep

        vpaddd %%A_L_KS0, %%STATE_IN_A_L
        vpaddd %%B_L_KS2, %%STATE_IN_B_L
        vpaddd %%C_L_KS1, %%STATE_IN_C_L
        vpaddd %%D_L_KS3, %%STATE_IN_D_L

        vperm2i128 %%YTMP, %%A_L_KS0, %%B_L_KS2, 0x20
        vperm2i128 %%B_L_KS2, %%A_L_KS0, %%B_L_KS2, 0x31
        vmovdqa %%A_L_KS0, %%YTMP

        vperm2i128 %%YTMP, %%C_L_KS1, %%D_L_KS3, 0x20
        vperm2i128 %%D_L_KS3, %%C_L_KS1, %%D_L_KS3, 0x31
        vmovdqa %%C_L_KS1, %%YTMP

%if %%NUM_BLOCKS == 4
        vpaddd %%A_H_KS4, %%STATE_IN_A_L
        vpaddd %%B_H_KS6, %%STATE_IN_B_L
        vpaddd %%C_H_KS5, %%STATE_IN_C_L
        vpaddd %%D_H_KS7, %%STATE_IN_D_H

        vperm2i128 %%YTMP, %%A_H_KS4, %%B_H_KS6, 0x20
        vperm2i128 %%B_H_KS6, %%A_H_KS4, %%B_H_KS6, 0x31
        vmovdqa %%A_H_KS4, %%YTMP

        vperm2i128 %%YTMP, %%C_H_KS5, %%D_H_KS7, 0x20
        vperm2i128 %%D_H_KS7, %%C_H_KS5, %%D_H_KS7, 0x31
        vmovdqa %%C_H_KS5, %%YTMP

%endif
%endmacro

; Perform 4 times the operation in first parameter
%macro YMM_OP_X8 9
%define %%OP         %1 ; [immediate] Instruction
%define %%DST_SRC1_1 %2 ; [in/out] First source/Destination 1
%define %%DST_SRC1_2 %3 ; [in/out] First source/Destination 2
%define %%DST_SRC1_3 %4 ; [in/out] First source/Destination 3
%define %%DST_SRC1_4 %5 ; [in/out] First source/Destination 4
%define %%SRC2_1     %6 ; [in] Second source 1
%define %%SRC2_2     %7 ; [in] Second source 2
%define %%SRC2_3     %8 ; [in] Second source 3
%define %%SRC2_4     %9 ; [in] Second source 4

        %%OP %%DST_SRC1_1, %%SRC2_1
        %%OP %%DST_SRC1_2, %%SRC2_2
        %%OP %%DST_SRC1_3, %%SRC2_3
        %%OP %%DST_SRC1_4, %%SRC2_4
%endmacro

%macro YMM_ROLS_X8  6
%define %%YMM_OP1_1      %1
%define %%YMM_OP1_2      %2
%define %%YMM_OP1_3      %3
%define %%YMM_OP1_4      %4
%define %%BITS_TO_ROTATE %5
%define %%YTMP           %6

        ; Store temporary register when bits to rotate is not 8 and 16,
        ; as the register will be clobbered in these cases,
        ; containing needed information
%if %%BITS_TO_ROTATE != 8 && %%BITS_TO_ROTATE != 16
        vmovdqa [rsp + _YMM_SAVE], %%YTMP
%endif
        VPROLD  %%YMM_OP1_1, %%BITS_TO_ROTATE, %%YTMP
        VPROLD  %%YMM_OP1_2, %%BITS_TO_ROTATE, %%YTMP
        VPROLD  %%YMM_OP1_3, %%BITS_TO_ROTATE, %%YTMP
        VPROLD  %%YMM_OP1_4, %%BITS_TO_ROTATE, %%YTMP
%if %%BITS_TO_ROTATE != 8 && %%BITS_TO_ROTATE != 16
        vmovdqa %%YTMP, [rsp + _YMM_SAVE]
%endif
%endmacro

;;
;; Performs a full chacha20 round on 8 states,
;; consisting of 8 quarter rounds, which are done in parallel
;;
%macro CHACHA20_ROUND 16
%define %%YMM_DWORD_A1  %1  ;; [in/out] YMM register containing dword A for first quarter round
%define %%YMM_DWORD_A2  %2  ;; [in/out] YMM register containing dword A for second quarter round
%define %%YMM_DWORD_A3  %3  ;; [in/out] YMM register containing dword A for third quarter round
%define %%YMM_DWORD_A4  %4  ;; [in/out] YMM register containing dword A for fourth quarter round
%define %%YMM_DWORD_B1  %5  ;; [in/out] YMM register containing dword B for first quarter round
%define %%YMM_DWORD_B2  %6  ;; [in/out] YMM register containing dword B for second quarter round
%define %%YMM_DWORD_B3  %7  ;; [in/out] YMM register containing dword B for third quarter round
%define %%YMM_DWORD_B4  %8  ;; [in/out] YMM register containing dword B for fourth quarter round
%define %%YMM_DWORD_C1  %9  ;; [in/out] YMM register containing dword C for first quarter round
%define %%YMM_DWORD_C2 %10  ;; [in/out] YMM register containing dword C for second quarter round
%define %%YMM_DWORD_C3 %11  ;; [in/out] YMM register containing dword C for third quarter round
%define %%YMM_DWORD_C4 %12  ;; [in/out] YMM register containing dword C for fourth quarter round
%define %%YMM_DWORD_D1 %13  ;; [in/out] YMM register containing dword D for first quarter round
%define %%YMM_DWORD_D2 %14  ;; [in/out] YMM register containing dword D for second quarter round
%define %%YMM_DWORD_D3 %15  ;; [in/out] YMM register containing dword D for third quarter round
%define %%YMM_DWORD_D4 %16  ;; [in/out] YMM register containing dword D for fourth quarter round

        ; A += B
        YMM_OP_X8 vpaddd, %%YMM_DWORD_A1, %%YMM_DWORD_A2, %%YMM_DWORD_A3, %%YMM_DWORD_A4, \
                         %%YMM_DWORD_B1, %%YMM_DWORD_B2, %%YMM_DWORD_B3, %%YMM_DWORD_B4
        ; D ^= A
        YMM_OP_X8 vpxor, %%YMM_DWORD_D1, %%YMM_DWORD_D2, %%YMM_DWORD_D3, %%YMM_DWORD_D4, \
                        %%YMM_DWORD_A1, %%YMM_DWORD_A2, %%YMM_DWORD_A3, %%YMM_DWORD_A4

        ; D <<< 16
        YMM_ROLS_X8 %%YMM_DWORD_D1, %%YMM_DWORD_D2, %%YMM_DWORD_D3, %%YMM_DWORD_D4, 16, \
                    %%YMM_DWORD_B1

        ; C += D
        YMM_OP_X8 vpaddd, %%YMM_DWORD_C1, %%YMM_DWORD_C2, %%YMM_DWORD_C3, %%YMM_DWORD_C4, \
                         %%YMM_DWORD_D1, %%YMM_DWORD_D2, %%YMM_DWORD_D3, %%YMM_DWORD_D4
        ; B ^= C
        YMM_OP_X8 vpxor, %%YMM_DWORD_B1, %%YMM_DWORD_B2, %%YMM_DWORD_B3, %%YMM_DWORD_B4, \
                        %%YMM_DWORD_C1, %%YMM_DWORD_C2, %%YMM_DWORD_C3, %%YMM_DWORD_C4

        ; B <<< 12
        YMM_ROLS_X8 %%YMM_DWORD_B1, %%YMM_DWORD_B2, %%YMM_DWORD_B3, %%YMM_DWORD_B4, 12, \
                    %%YMM_DWORD_D1

        ; A += B
        YMM_OP_X8 vpaddd, %%YMM_DWORD_A1, %%YMM_DWORD_A2, %%YMM_DWORD_A3, %%YMM_DWORD_A4, \
                          %%YMM_DWORD_B1, %%YMM_DWORD_B2, %%YMM_DWORD_B3, %%YMM_DWORD_B4
        ; D ^= A
        YMM_OP_X8 vpxor, %%YMM_DWORD_D1, %%YMM_DWORD_D2, %%YMM_DWORD_D3, %%YMM_DWORD_D4, \
                          %%YMM_DWORD_A1, %%YMM_DWORD_A2, %%YMM_DWORD_A3, %%YMM_DWORD_A4

        ; D <<< 8
        YMM_ROLS_X8 %%YMM_DWORD_D1, %%YMM_DWORD_D2, %%YMM_DWORD_D3, %%YMM_DWORD_D4, 8, \
                    %%YMM_DWORD_B1

        ; C += D
        YMM_OP_X8 vpaddd, %%YMM_DWORD_C1, %%YMM_DWORD_C2, %%YMM_DWORD_C3, %%YMM_DWORD_C4, \
                          %%YMM_DWORD_D1, %%YMM_DWORD_D2, %%YMM_DWORD_D3, %%YMM_DWORD_D4
        ; B ^= C
        YMM_OP_X8 vpxor, %%YMM_DWORD_B1, %%YMM_DWORD_B2, %%YMM_DWORD_B3, %%YMM_DWORD_B4, \
                          %%YMM_DWORD_C1, %%YMM_DWORD_C2, %%YMM_DWORD_C3, %%YMM_DWORD_C4

        ; B <<< 7
        YMM_ROLS_X8 %%YMM_DWORD_B1, %%YMM_DWORD_B2, %%YMM_DWORD_B3, %%YMM_DWORD_B4, 7, \
                    %%YMM_DWORD_D1
%endmacro

;;
;; Encodes 8 Chacha20 states, outputting 512 bytes of keystream
;; Data still needs to be transposed to get the keystream in the correct order
;;
%macro GENERATE_512_KS 16
%define %%YMM_DWORD_0   %1  ;; [out] YMM register to contain encoded dword 0 of the 8 Chacha20 states
%define %%YMM_DWORD_1   %2  ;; [out] YMM register to contain encoded dword 1 of the 8 Chacha20 states
%define %%YMM_DWORD_2   %3  ;; [out] YMM register to contain encoded dword 2 of the 8 Chacha20 states
%define %%YMM_DWORD_3   %4  ;; [out] YMM register to contain encoded dword 3 of the 8 Chacha20 states
%define %%YMM_DWORD_4   %5  ;; [out] YMM register to contain encoded dword 4 of the 8 Chacha20 states
%define %%YMM_DWORD_5   %6  ;; [out] YMM register to contain encoded dword 5 of the 8 Chacha20 states
%define %%YMM_DWORD_6   %7  ;; [out] YMM register to contain encoded dword 6 of the 8 Chacha20 states
%define %%YMM_DWORD_7   %8  ;; [out] YMM register to contain encoded dword 7 of the 8 Chacha20 states
%define %%YMM_DWORD_8   %9  ;; [out] YMM register to contain encoded dword 8 of the 8 Chacha20 states
%define %%YMM_DWORD_9  %10  ;; [out] YMM register to contain encoded dword 9 of the 8 Chacha20 states
%define %%YMM_DWORD_10 %11  ;; [out] YMM register to contain encoded dword 10 of the 8 Chacha20 states
%define %%YMM_DWORD_11 %12  ;; [out] YMM register to contain encoded dword 11 of the 8 Chacha20 states
%define %%YMM_DWORD_12 %13  ;; [out] YMM register to contain encoded dword 12 of the 8 Chacha20 states
%define %%YMM_DWORD_13 %14  ;; [out] YMM register to contain encoded dword 13 of the 8 Chacha20 states
%define %%YMM_DWORD_14 %15  ;; [out] YMM register to contain encoded dword 14 of the 8 Chacha20 states
%define %%YMM_DWORD_15 %16  ;; [out] YMM register to contain encoded dword 15 of the 8 Chacha20 states

%assign i 0
%rep 16
        vmovdqa APPEND(%%YMM_DWORD_, i), [rsp + _STATE + 32*i]
%assign i (i + 1)
%endrep

%rep 10
        CHACHA20_ROUND %%YMM_DWORD_0, %%YMM_DWORD_1, %%YMM_DWORD_2, %%YMM_DWORD_3, \
                       %%YMM_DWORD_4, %%YMM_DWORD_5, %%YMM_DWORD_6, %%YMM_DWORD_7, \
                       %%YMM_DWORD_8, %%YMM_DWORD_9, %%YMM_DWORD_10, %%YMM_DWORD_11, \
                       %%YMM_DWORD_12, %%YMM_DWORD_13, %%YMM_DWORD_14, %%YMM_DWORD_15

        CHACHA20_ROUND %%YMM_DWORD_0, %%YMM_DWORD_1, %%YMM_DWORD_2, %%YMM_DWORD_3, \
                       %%YMM_DWORD_5, %%YMM_DWORD_6, %%YMM_DWORD_7, %%YMM_DWORD_4, \
                       %%YMM_DWORD_10, %%YMM_DWORD_11, %%YMM_DWORD_8, %%YMM_DWORD_9, \
                       %%YMM_DWORD_15, %%YMM_DWORD_12, %%YMM_DWORD_13, %%YMM_DWORD_14
%endrep

%assign i 0
%rep 16
        vpaddd  APPEND(%%YMM_DWORD_, i), [rsp + _STATE + 32*i]
%assign i (i + 1)
%endrep
%endmacro

%macro PREPARE_NEXT_STATES_2_TO_4 11
%define %%STATE_IN_A_L   %1  ;; [out] YMM containing state "A" part for states 1-2
%define %%STATE_IN_B_L   %2  ;; [out] YMM containing state "B" part for states 1-2
%define %%STATE_IN_C_L   %3  ;; [out] YMM containing state "C" part for states 1-2
%define %%STATE_IN_D_L   %4  ;; [out] YMM containing state "D" part for states 1-2
%define %%STATE_IN_D_H   %5  ;; [out] YMM containing state "D" part for states 3-4 (or "none" in NUM_BLOCKS == 4)
%define %%YTMP0          %6  ;; [clobbered] YMM temp reg
%define %%YTMP1          %7  ;; [clobbered] YMM temp reg
%define %%LAST_BLK_CNT   %8  ;; [in] Last block counter
%define %%IV             %9  ;; [in] Pointer to IV
%define %%KEY            %10 ;; [in] Pointer to key
%define %%NUM_BLOCKS     %11 ;; [in] Number of state blocks to prepare (numerical)

        ;; Prepare next 4 states (or 2, if 2 or less blocks left)
        vmovdqu %%STATE_IN_B_L, [%%KEY]      ; Load key bytes 0-15
        vmovdqu %%STATE_IN_C_L, [%%KEY + 16] ; Load key bytes 16-31
        vperm2i128 %%STATE_IN_B_L,%%STATE_IN_B_L, 0x0
        vperm2i128 %%STATE_IN_C_L, %%STATE_IN_C_L, 0x0
        vmovq   XWORD(%%STATE_IN_D_L), [%%IV]
        vpinsrd XWORD(%%STATE_IN_D_L), [%%IV + 8], 2
        vpslldq %%STATE_IN_D_L, 4
        vperm2i128 %%STATE_IN_D_L, %%STATE_IN_D_L, 0x0

        vmovdqa %%STATE_IN_A_L, [rel constants]

%if %%NUM_BLOCKS == 4
        ;; Prepare chacha states 2-3 (A-C same as states 0-3)
        vmovdqa %%STATE_IN_D_H, %%STATE_IN_D_L
%endif

        ; Broadcast last block counter
        vmovd   XWORD(%%YTMP0), DWORD(%%LAST_BLK_CNT)
        vperm2i128 %%YTMP0, %%YTMP0, 0x00
%if %%NUM_BLOCKS == 2
        ; Add 1-2 to construct next block counters
        vpaddd  %%YTMP0, [rel add_1_2]
        vpor    %%STATE_IN_D_L, %%YTMP0
%else
        ; Add 1-8 to construct next block counters
        vmovdqa %%YTMP1, %%YTMP0
        vpaddd  %%YTMP0, [rel add_1_2]
        vpaddd  %%YTMP1, [rel add_3_4]
        vpor    %%STATE_IN_D_L, %%YTMP0
        vpor    %%STATE_IN_D_H, %%YTMP1
%endif
%endmacro

align 32
MKGLOBAL(submit_job_chacha20_enc_dec_avx2,function,internal)
submit_job_chacha20_enc_dec_avx2:
        endbranch64
%define src     r8
%define dst     r9
%define len     r10
%define iv      r11
%define keys    rdx
%define off     rax
%define tmp     iv
%define tmp2    keys

        ; Read pointers and length
        mov     len, [job + _msg_len_to_cipher_in_bytes]

        ; Check if there is nothing to encrypt
        or      len, len
        jz      exit

        mov     keys, [job + _enc_keys]
        mov     iv, [job + _iv]
        mov     src, [job + _src]
        add     src, [job + _cipher_start_src_offset_in_bytes]
        mov     dst, [job + _dst]

        mov     rax, rsp
        sub     rsp, STACK_SIZE
        and     rsp, -32
        mov     [rsp + _RSP_SAVE], rax ; save RSP

        xor     off, off

        ; If less than or equal to 64*4 bytes, prepare directly states for up to 4 blocks
        cmp     len, 64*4
        jbe     exit_loop

        ; Prepare first 8 chacha states from IV, key, constants and counter values
        vbroadcastss ymm0, [rel constants]
        vbroadcastss ymm1, [rel constants + 4]
        vbroadcastss ymm2, [rel constants + 8]
        vbroadcastss ymm3, [rel constants + 12]

        vbroadcastss ymm4, [keys]
        vbroadcastss ymm5, [keys + 4]
        vbroadcastss ymm6, [keys + 8]
        vbroadcastss ymm7, [keys + 12]
        vbroadcastss ymm8, [keys + 16]
        vbroadcastss ymm9, [keys + 20]
        vbroadcastss ymm10, [keys + 24]
        vbroadcastss ymm11, [keys + 28]

        vbroadcastss ymm13, [iv]
        vbroadcastss ymm14, [iv + 4]
        vbroadcastss ymm15, [iv + 8]

        ; Set block counters for first 8 Chacha20 states
        vmovdqa ymm12, [rel dword_1_8]

%assign i 0
%rep 16
        vmovdqa [rsp + _STATE + 32*i], ymm %+ i
%assign i (i + 1)
%endrep

        cmp     len, 64*8
        jb      exit_loop

align 32
start_loop:

        ; Generate 512 bytes of keystream
        GENERATE_512_KS ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, \
                        ymm8, ymm9, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15

        ;; Transpose state to get keystream and XOR with plaintext
        ;; to get ciphertext

        ; Save registers to be used as temp registers
        vmovdqa [rsp + _YMM_SAVE], ymm14
        vmovdqa [rsp + _YMM_SAVE + 32], ymm15

        ; Transpose to get [64*I : 64*I + 31] (I = 0-7) bytes of KS
        TRANSPOSE8_U32 ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, ymm14, ymm15

        vpxor   ymm0, [src + off]
        vpxor   ymm1, [src + off + 64]
        vmovdqu [dst + off], ymm0
        vmovdqu [dst + off + 64], ymm1

        vpxor   ymm2, [src + off + 64*2]
        vpxor   ymm3, [src + off + 64*3]
        vmovdqu [dst + off + 64*2], ymm2
        vmovdqu [dst + off + 64*3], ymm3

        vpxor   ymm4, [src + off + 64*4]
        vpxor   ymm5, [src + off + 64*5]
        vmovdqu [dst + off + 64*4], ymm4
        vmovdqu [dst + off + 64*5], ymm5

        vpxor   ymm6, [src + off + 64*6]
        vpxor   ymm7, [src + off + 64*7]
        vmovdqu [dst + off + 64*6], ymm6
        vmovdqu [dst + off + 64*7], ymm7

        ; Restore registers and use ymm0, ymm1 now that they are free
        vmovdqa ymm14, [rsp + _YMM_SAVE]
        vmovdqa ymm15, [rsp + _YMM_SAVE + 32]

        ; Transpose to get [64*I + 32 : 64*I + 63] (I = 0-7) bytes of KS
        TRANSPOSE8_U32 ymm8, ymm9, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15, ymm0, ymm1

        vpxor   ymm8, [src + off + 32]
        vpxor   ymm9, [src + off + 64 + 32]
        vmovdqu [dst + off + 32], ymm8
        vmovdqu [dst + off + 64 + 32], ymm9

        vpxor   ymm10, [src + off + 64*2 + 32]
        vpxor   ymm11, [src + off + 64*3 + 32]
        vmovdqu [dst + off + 64*2 + 32], ymm10
        vmovdqu [dst + off + 64*3 + 32], ymm11

        vpxor   ymm12, [src + off + 64*4 + 32]
        vpxor   ymm13, [src + off + 64*5 + 32]
        vmovdqu [dst + off + 64*4 + 32], ymm12
        vmovdqu [dst + off + 64*5 + 32], ymm13

        vpxor   ymm14, [src + off + 64*6 + 32]
        vpxor   ymm15, [src + off + 64*7 + 32]
        vmovdqu [dst + off + 64*6 + 32], ymm14
        vmovdqu [dst + off + 64*7 + 32], ymm15

        ; Update remaining length
        sub     len, 64*8
        add     off, 64*8

        ; Update counter values
        vmovdqa ymm12, [rsp + 32*12]
        vpaddd  ymm12, [rel dword_8]
        vmovdqa [rsp + 32*12], ymm12

        cmp     len, 64*8
        jae     start_loop

exit_loop:

        ; Check if there are no more bytes to encrypt
        or      len, len
        jz      no_partial_block

        cmp     len, 64*4
        ja      more_than_4_blocks_left

        cmp     len, 64*2
        ja      more_than_2_blocks_left

        ;; up to 2 blocks left

        ; Get last block counter dividing offset by 64
        shr     off, 6
        PREPARE_NEXT_STATES_2_TO_4 ymm4, ymm5, ymm6, ymm7, none, \
                                   ymm8, ymm9, off, iv, keys, 2
        shl     off, 6 ; Restore offset

        GENERATE_256_KS ymm0, ymm1, ymm8, ymm9, none, none, none, none, \
                        ymm4, ymm5, ymm6, ymm7, none, ymm10, 2

        cmp     len, 64*2
        jbe     less_than_2_full_blocks

        ; XOR next 128 bytes
        vpxor   ymm8, [src + off]
        vpxor   ymm9, [src + off + 32]
        vmovdqu [dst + off], ymm8
        vmovdqu [dst + off + 32], ymm9

        vpxor   ymm10, [src + off + 32*2]
        vpxor   ymm11, [src + off + 32*3]
        vmovdqu [dst + off + 32*2], ymm10
        vmovdqu [dst + off + 32*3], ymm11

        jmp     no_partial_block

more_than_2_blocks_left:

        ; Get last block counter dividing offset by 64
        shr     off, 6
        PREPARE_NEXT_STATES_2_TO_4 ymm4, ymm5, ymm6, ymm7, ymm12, \
                                   ymm8, ymm9, off, iv, keys, 4
        shl     off, 6 ; Restore offset

        GENERATE_256_KS ymm0, ymm1, ymm8, ymm9, ymm2, ymm3, ymm10, ymm11, \
                        ymm4, ymm5, ymm6, ymm7, ymm12, ymm13, 4

        cmp     len, 64*4
        jb      less_than_4_full_blocks

        ; XOR next 256 bytes
        vpxor   ymm0, [src + off]
        vpxor   ymm8, [src + off + 32]
        vmovdqu [dst + off], ymm0
        vmovdqu [dst + off + 32], ymm8

        vpxor   ymm1, [src + off + 32*2]
        vpxor   ymm9, [src + off + 32*3]
        vmovdqu [dst + off + 32*2], ymm1
        vmovdqu [dst + off + 32*3], ymm9

        vpxor   ymm2, [src + off + 32*4]
        vpxor   ymm10, [src + off + 32*5]
        vmovdqu [dst + off + 32*4], ymm2
        vmovdqu [dst + off + 32*5], ymm10

        vpxor   ymm3, [src + off + 32*6]
        vpxor   ymm11, [src + off + 32*7]
        vmovdqu [dst + off + 32*6], ymm3
        vmovdqu [dst + off + 32*7], ymm11

        jmp     no_partial_block

more_than_4_blocks_left:
        ; Generate 512 bytes of keystream
        GENERATE_512_KS ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, \
                        ymm8, ymm9, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15

        ; Save registers to be used as temp registers
        vmovdqa [rsp + _YMM_SAVE], ymm14
        vmovdqa [rsp + _YMM_SAVE + 32], ymm15

        ; Transpose to get [64*I : 64*I + 31] (I = 0-7) bytes of KS
        TRANSPOSE8_U32 ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, ymm14, ymm15

        ; Restore registers and save another two registers to be used as temp registers
        vmovdqa ymm14, [rsp + _YMM_SAVE]
        vmovdqa ymm15, [rsp + _YMM_SAVE + 32]
        vmovdqa [rsp + _YMM_SAVE], ymm0
        vmovdqa [rsp + _YMM_SAVE + 32], ymm1

        ; Transpose to get [64*I + 32 : 64*I + 63] (I = 0-7) bytes of KS
        TRANSPOSE8_U32 ymm8, ymm9, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15, ymm0, ymm1

        ; Restore registers
        vmovdqa ymm0, [rsp + _YMM_SAVE]
        vmovdqa ymm1, [rsp + _YMM_SAVE + 32]

        ; XOR next 256 bytes
        vpxor   ymm0, [src + off]
        vpxor   ymm8, [src + off + 32]
        vmovdqu [dst + off], ymm0
        vmovdqu [dst + off + 32], ymm8

        vpxor   ymm1, [src + off + 32*2]
        vpxor   ymm9, [src + off + 32*3]
        vmovdqu [dst + off + 32*2], ymm1
        vmovdqu [dst + off + 32*3], ymm9

        vpxor   ymm2, [src + off + 32*4]
        vpxor   ymm10, [src + off + 32*5]
        vmovdqu [dst + off + 32*4], ymm2
        vmovdqu [dst + off + 32*5], ymm10

        vpxor   ymm3, [src + off + 32*6]
        vpxor   ymm11, [src + off + 32*7]
        vmovdqu [dst + off + 32*6], ymm3
        vmovdqu [dst + off + 32*7], ymm11

        ; Update remaining length
        sub     len, 64*4
        add     off, 64*4

        or      len, len
        jz      no_partial_block

        ; Move last 256 bytes of KS to registers YMM0-3,YMM8-11
        vmovdqa ymm0, ymm4
        vmovdqa ymm1, ymm5
        vmovdqa ymm2, ymm6
        vmovdqa ymm3, ymm7
        vmovdqa ymm8, ymm12
        vmovdqa ymm9, ymm13
        vmovdqa ymm10, ymm14
        vmovdqa ymm11, ymm15

        cmp     len, 64*2
        jb      less_than_2_full_blocks

less_than_4_full_blocks:
        ; XOR next 128 bytes
        vpxor   ymm0, [src + off]
        vpxor   ymm8, [src + off + 32]
        vmovdqu [dst + off], ymm0
        vmovdqu [dst + off + 32], ymm8

        vpxor   ymm1, [src + off + 32*2]
        vpxor   ymm9, [src + off + 32*3]
        vmovdqu [dst + off + 32*2], ymm1
        vmovdqu [dst + off + 32*3], ymm9

        ; Update remaining length
        sub     len, 64*2
        add     off, 64*2

        or      len, len
        jz      no_partial_block

        ; Move last 128 bytes of KS to registers YMM0-1,YMM8-9
        vmovdqa ymm0, ymm2
        vmovdqa ymm1, ymm3
        vmovdqa ymm8, ymm10
        vmovdqa ymm9, ymm11

less_than_2_full_blocks:

        cmp     len, 64
        jb      less_than_1_full_block

        ; XOR next 64 bytes
        vpxor   ymm0, [src + off]
        vpxor   ymm8, [src + off + 32]
        vmovdqu [dst + off], ymm0
        vmovdqu [dst + off + 32], ymm8

        ; Update remaining length
        sub     len, 64
        add     off, 64

        or      len, len
        jz      no_partial_block

        ; Move last 64 bytes of KS to registers YMM0,YMM8
        vmovdqa ymm0, ymm1
        vmovdqa ymm8, ymm9

less_than_1_full_block:

        cmp     len, 32
        jb      partial_block

        ; XOR next 32 bytes
        vpxor   ymm0, [src + off]
        vmovdqu [dst + off], ymm0

        ; Update remaining length
        sub     len, 32
        add     off, 32

        or      len, len
        jz      no_partial_block

        ; Move last 32 bytes of KS to registers YMM0
        vmovdqa ymm0, ymm8

partial_block:

        add     src, off
        add     dst, off

        simd_load_avx2 ymm1, src, len, tmp, tmp2
        vpxor   ymm1, ymm0
        simd_store_avx2 dst, ymm1, len, tmp, tmp2

no_partial_block:
        endbranch64
%ifdef SAFE_DATA
        vpxor ymm0, ymm0
        ; Clear stack frame
%assign i 0
%rep 16
        vmovdqa [rsp + _STATE + 32*i], ymm0
%assign i (i + 1)
%endrep
        vmovdqa [rsp + _YMM_SAVE], ymm0
        vmovdqa [rsp + _YMM_SAVE + 32], ymm0
%endif

        mov     rsp, [rsp + _RSP_SAVE]

exit:
        mov     rax, job
        or      dword [rax + _status], IMB_STATUS_COMPLETED_CIPHER
        clear_all_ymms_asm

        ret

align 32
MKGLOBAL(chacha20_enc_dec_ks_avx2,function,internal)
chacha20_enc_dec_ks_avx2:
        endbranch64
%define blk_cnt r10

%define prev_ks r13
%define remain_ks r12
%define ctx     r11

%define src     arg1
%define dst     arg2
%define len     arg3
%define keys    arg4

%define iv      r15
%define off     rax
%define tmp     iv
%define tmp3    r14
%define tmp4    rbp
%define tmp5    rbx
%ifdef LINUX
%define tmp2 r9
%else
%define tmp2 rdi
%endif

        ; Check if there is nothing to encrypt
        or      len, len
        jz      exit_ks

        mov     ctx, arg5

        mov     rax, rsp
        sub     rsp, STACK_SIZE
        and     rsp, -32
        mov     [rsp + _GP_SAVE], r12
        mov     [rsp + _GP_SAVE + 8], r13
        mov     [rsp + _GP_SAVE + 16], r14
        mov     [rsp + _GP_SAVE + 24], r15
        mov     [rsp + _GP_SAVE + 32], rbx
        mov     [rsp + _GP_SAVE + 40], rbp
%ifndef LINUX
        mov     [rsp + _GP_SAVE + 48], rdi
%endif
        mov     [rsp + _RSP_SAVE], rax ; save RSP

        xor     off, off
        mov     blk_cnt, [ctx + LastBlkCount]
        lea     prev_ks, [ctx + LastKs]
        mov     remain_ks, [ctx + RemainKsBytes]

        ; Check if there are any remaining bytes of keystream
        mov     tmp3, remain_ks
        or      tmp3, tmp3
        jz      no_remain_ks_bytes

        mov     tmp4, 64
        sub     tmp4, tmp3

        ; Adjust pointer of previous KS to point at start of unused KS
        add     prev_ks, tmp4

        ; Set remaining bytes to length of input segment, if lower
        cmp     len, tmp3
        cmovbe  tmp3, len

        mov     tmp5, tmp3
        ; Read up to 63 bytes of KS and XOR the first bytes of message
        ; with the previous unused bytes of keystream
        ENCRYPT_0B_64B    src, dst, tmp3, off, ymm9, ymm10, \
                        ymm0, ymm1, tmp, tmp2, prev_ks

        ; Update remain bytes of KS
        sub     [ctx + RemainKsBytes], tmp5
        ; Restore pointer to previous KS
        sub     prev_ks, tmp4

        sub     len, tmp5
        jz      no_partial_block_ks

no_remain_ks_bytes:
        ; Reset remaining number of KS bytes
        mov     qword [ctx + RemainKsBytes], 0
        lea     iv, [ctx + IV]

        ; If less than or equal to 64*4 bytes, prepare directly states for up to 4 blocks
        cmp     len, 64*4
        jbe     exit_loop_ks

        ; Prepare first 8 chacha states from IV, key, constants and counter values
        vbroadcastss ymm0, [rel constants]
        vbroadcastss ymm1, [rel constants + 4]
        vbroadcastss ymm2, [rel constants + 8]
        vbroadcastss ymm3, [rel constants + 12]

        vbroadcastss ymm4, [keys]
        vbroadcastss ymm5, [keys + 4]
        vbroadcastss ymm6, [keys + 8]
        vbroadcastss ymm7, [keys + 12]
        vbroadcastss ymm8, [keys + 16]
        vbroadcastss ymm9, [keys + 20]
        vbroadcastss ymm10, [keys + 24]
        vbroadcastss ymm11, [keys + 28]

        vbroadcastss ymm13, [iv]
        vbroadcastss ymm14, [iv + 4]
        vbroadcastss ymm15, [iv + 8]

        ; Set block counters for next 8 Chacha20 states
        vmovd    xmm12, DWORD(blk_cnt)
        vpshufd  xmm12, xmm12, 0
        vperm2i128 ymm12, ymm12, ymm12, 0
        vpaddd  ymm12, [rel dword_1_8]

%assign i 0
%rep 16
        vmovdqa [rsp + _STATE + 32*i], ymm %+ i
%assign i (i + 1)
%endrep

        cmp     len, 64*8
        jb      exit_loop_ks

align 32
start_loop_ks:

        ; Generate 512 bytes of keystream
        GENERATE_512_KS ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, \
                        ymm8, ymm9, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15

        ;; Transpose state to get keystream and XOR with plaintext
        ;; to get ciphertext

        ; Save registers to be used as temp registers
        vmovdqa [rsp + _YMM_SAVE], ymm14
        vmovdqa [rsp + _YMM_SAVE + 32], ymm15

        ; Transpose to get [64*I : 64*I + 31] (I = 0-7) bytes of KS
        TRANSPOSE8_U32 ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, ymm14, ymm15

        vpxor   ymm0, [src + off]
        vpxor   ymm1, [src + off + 64]
        vmovdqu [dst + off], ymm0
        vmovdqu [dst + off + 64], ymm1

        vpxor   ymm2, [src + off + 64*2]
        vpxor   ymm3, [src + off + 64*3]
        vmovdqu [dst + off + 64*2], ymm2
        vmovdqu [dst + off + 64*3], ymm3

        vpxor   ymm4, [src + off + 64*4]
        vpxor   ymm5, [src + off + 64*5]
        vmovdqu [dst + off + 64*4], ymm4
        vmovdqu [dst + off + 64*5], ymm5

        vpxor   ymm6, [src + off + 64*6]
        vpxor   ymm7, [src + off + 64*7]
        vmovdqu [dst + off + 64*6], ymm6
        vmovdqu [dst + off + 64*7], ymm7

        ; Restore registers and use ymm0, ymm1 now that they are free
        vmovdqa ymm14, [rsp + _YMM_SAVE]
        vmovdqa ymm15, [rsp + _YMM_SAVE + 32]

        ; Transpose to get [64*I + 32 : 64*I + 63] (I = 0-7) bytes of KS
        TRANSPOSE8_U32 ymm8, ymm9, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15, ymm0, ymm1

        vpxor   ymm8, [src + off + 32]
        vpxor   ymm9, [src + off + 64 + 32]
        vmovdqu [dst + off + 32], ymm8
        vmovdqu [dst + off + 64 + 32], ymm9

        vpxor   ymm10, [src + off + 64*2 + 32]
        vpxor   ymm11, [src + off + 64*3 + 32]
        vmovdqu [dst + off + 64*2 + 32], ymm10
        vmovdqu [dst + off + 64*3 + 32], ymm11

        vpxor   ymm12, [src + off + 64*4 + 32]
        vpxor   ymm13, [src + off + 64*5 + 32]
        vmovdqu [dst + off + 64*4 + 32], ymm12
        vmovdqu [dst + off + 64*5 + 32], ymm13

        vpxor   ymm14, [src + off + 64*6 + 32]
        vpxor   ymm15, [src + off + 64*7 + 32]
        vmovdqu [dst + off + 64*6 + 32], ymm14
        vmovdqu [dst + off + 64*7 + 32], ymm15

        ; Update remaining length
        sub     len, 64*8
        add     off, 64*8
        add     blk_cnt, 8

        ; Update counter values
        vmovdqa ymm12, [rsp + 32*12]
        vpaddd  ymm12, [rel dword_8]
        vmovdqa [rsp + 32*12], ymm12

        cmp     len, 64*8
        jae     start_loop_ks

exit_loop_ks:

        ; Check if there are no more bytes to encrypt
        or      len, len
        jz      no_partial_block_ks

        cmp     len, 64*4
        ja      more_than_4_blocks_left_ks

        cmp     len, 64*2
        ja      more_than_2_blocks_left_ks

        ;; up to 2 blocks left

        ; Get last block counter dividing offset by 64
        PREPARE_NEXT_STATES_2_TO_4 ymm4, ymm5, ymm6, ymm7, none, \
                                   ymm8, ymm9, blk_cnt, iv, keys, 2

        GENERATE_256_KS ymm0, ymm1, ymm8, ymm9, none, none, none, none, \
                        ymm4, ymm5, ymm6, ymm7, none, ymm10, 2

        cmp     len, 64*2
        jbe     less_than_2_full_blocks_ks

        ; XOR next 128 bytes
        vpxor   ymm8, [src + off]
        vpxor   ymm9, [src + off + 32]
        vmovdqu [dst + off], ymm8
        vmovdqu [dst + off + 32], ymm9

        vpxor   ymm10, [src + off + 32*2]
        vpxor   ymm11, [src + off + 32*3]
        vmovdqu [dst + off + 32*2], ymm10
        vmovdqu [dst + off + 32*3], ymm11

        add     blk_cnt, 2

        jmp     no_partial_block_ks

more_than_2_blocks_left_ks:

        ; Get last block counter dividing offset by 64
        PREPARE_NEXT_STATES_2_TO_4 ymm4, ymm5, ymm6, ymm7, ymm12, \
                                   ymm8, ymm9, blk_cnt, iv, keys, 4

        GENERATE_256_KS ymm0, ymm1, ymm8, ymm9, ymm2, ymm3, ymm10, ymm11, \
                        ymm4, ymm5, ymm6, ymm7, ymm12, ymm13, 4

        cmp     len, 64*4
        jb      less_than_4_full_blocks_ks

        ; XOR next 256 bytes
        vpxor   ymm0, [src + off]
        vpxor   ymm8, [src + off + 32]
        vmovdqu [dst + off], ymm0
        vmovdqu [dst + off + 32], ymm8

        vpxor   ymm1, [src + off + 32*2]
        vpxor   ymm9, [src + off + 32*3]
        vmovdqu [dst + off + 32*2], ymm1
        vmovdqu [dst + off + 32*3], ymm9

        vpxor   ymm2, [src + off + 32*4]
        vpxor   ymm10, [src + off + 32*5]
        vmovdqu [dst + off + 32*4], ymm2
        vmovdqu [dst + off + 32*5], ymm10

        vpxor   ymm3, [src + off + 32*6]
        vpxor   ymm11, [src + off + 32*7]
        vmovdqu [dst + off + 32*6], ymm3
        vmovdqu [dst + off + 32*7], ymm11

        add     blk_cnt, 4

        jmp     no_partial_block_ks

more_than_4_blocks_left_ks:
        ; Generate 512 bytes of keystream
        GENERATE_512_KS ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, \
                        ymm8, ymm9, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15

        ; Save registers to be used as temp registers
        vmovdqa [rsp + _YMM_SAVE], ymm14
        vmovdqa [rsp + _YMM_SAVE + 32], ymm15

        ; Transpose to get [64*I : 64*I + 31] (I = 0-7) bytes of KS
        TRANSPOSE8_U32 ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, ymm14, ymm15

        ; Restore registers and save another two registers to be used as temp registers
        vmovdqa ymm14, [rsp + _YMM_SAVE]
        vmovdqa ymm15, [rsp + _YMM_SAVE + 32]
        vmovdqa [rsp + _YMM_SAVE], ymm0
        vmovdqa [rsp + _YMM_SAVE + 32], ymm1

        ; Transpose to get [64*I + 32 : 64*I + 63] (I = 0-7) bytes of KS
        TRANSPOSE8_U32 ymm8, ymm9, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15, ymm0, ymm1

        ; Restore registers
        vmovdqa ymm0, [rsp + _YMM_SAVE]
        vmovdqa ymm1, [rsp + _YMM_SAVE + 32]

        ; XOR next 256 bytes
        vpxor   ymm0, [src + off]
        vpxor   ymm8, [src + off + 32]
        vmovdqu [dst + off], ymm0
        vmovdqu [dst + off + 32], ymm8

        vpxor   ymm1, [src + off + 32*2]
        vpxor   ymm9, [src + off + 32*3]
        vmovdqu [dst + off + 32*2], ymm1
        vmovdqu [dst + off + 32*3], ymm9

        vpxor   ymm2, [src + off + 32*4]
        vpxor   ymm10, [src + off + 32*5]
        vmovdqu [dst + off + 32*4], ymm2
        vmovdqu [dst + off + 32*5], ymm10

        vpxor   ymm3, [src + off + 32*6]
        vpxor   ymm11, [src + off + 32*7]
        vmovdqu [dst + off + 32*6], ymm3
        vmovdqu [dst + off + 32*7], ymm11

        ; Update remaining length
        sub     len, 64*4
        add     off, 64*4
        add     blk_cnt, 4

        or      len, len
        jz      no_partial_block_ks

        ; Move last 256 bytes of KS to registers YMM0-3,YMM8-11
        vmovdqa ymm0, ymm4
        vmovdqa ymm1, ymm5
        vmovdqa ymm2, ymm6
        vmovdqa ymm3, ymm7
        vmovdqa ymm8, ymm12
        vmovdqa ymm9, ymm13
        vmovdqa ymm10, ymm14
        vmovdqa ymm11, ymm15

        cmp     len, 64*2
        jb      less_than_2_full_blocks_ks

less_than_4_full_blocks_ks:
        ; XOR next 128 bytes
        vpxor   ymm0, [src + off]
        vpxor   ymm8, [src + off + 32]
        vmovdqu [dst + off], ymm0
        vmovdqu [dst + off + 32], ymm8

        vpxor   ymm1, [src + off + 32*2]
        vpxor   ymm9, [src + off + 32*3]
        vmovdqu [dst + off + 32*2], ymm1
        vmovdqu [dst + off + 32*3], ymm9

        ; Update remaining length
        sub     len, 64*2
        add     off, 64*2
        add     blk_cnt, 2

        or      len, len
        jz      no_partial_block_ks

        ; Move last 128 bytes of KS to registers YMM0-1,YMM8-9
        vmovdqa ymm0, ymm2
        vmovdqa ymm1, ymm3
        vmovdqa ymm8, ymm10
        vmovdqa ymm9, ymm11

less_than_2_full_blocks_ks:

        cmp     len, 64
        jb      less_than_1_full_block_ks

        ; XOR next 64 bytes
        vpxor   ymm0, [src + off]
        vpxor   ymm8, [src + off + 32]
        vmovdqu [dst + off], ymm0
        vmovdqu [dst + off + 32], ymm8

        ; Update remaining length
        sub     len, 64
        add     off, 64
        inc     blk_cnt

        or      len, len
        jz      no_partial_block_ks

        ; Move last 64 bytes of KS to registers YMM0,YMM8
        vmovdqa ymm0, ymm1
        vmovdqa ymm8, ymm9

less_than_1_full_block_ks:

        ; Preserve len
        mov     tmp5, len
        ENCRYPT_0B_64B    src, dst, len, off, ymm0, ymm8, \
                        ymm1, ymm2, tmp, tmp2
        inc     blk_cnt

        ; Save last 64-byte block of keystream,
        ; in case it is needed in next segments
        vmovdqu  [prev_ks], ymm0
        vmovdqu  [prev_ks + 32], ymm8

        ; Update remain number of KS bytes
        mov     tmp, 64
        sub     tmp, tmp5
        mov     [ctx + RemainKsBytes], tmp

no_partial_block_ks:
        endbranch64
        mov     [ctx + LastBlkCount], blk_cnt

%ifdef SAFE_DATA
        vpxor ymm0, ymm0
        ; Clear stack frame
%assign i 0
%rep 16
        vmovdqa [rsp + _STATE + 32*i], ymm0
%assign i (i + 1)
%endrep
        vmovdqa [rsp + _YMM_SAVE], ymm0
        vmovdqa [rsp + _YMM_SAVE + 32], ymm0
%endif

        mov     r12, [rsp + _GP_SAVE]
        mov     r13, [rsp + _GP_SAVE + 8]
        mov     r14, [rsp + _GP_SAVE + 16]
        mov     r15, [rsp + _GP_SAVE + 24]
        mov     rbx, [rsp + _GP_SAVE + 32]
        mov     rbp, [rsp + _GP_SAVE + 40]
%ifndef LINUX
        mov     rdi, [rsp + _GP_SAVE + 48]
%endif
        mov     rsp, [rsp + _RSP_SAVE]

exit_ks:
        clear_all_ymms_asm

        ret

mksection stack-noexec

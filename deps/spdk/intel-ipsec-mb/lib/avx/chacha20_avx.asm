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
%include "include/chacha_poly_defines.asm"

mksection .rodata
default rel

align 16
constants0:
dd      0x61707865, 0x61707865, 0x61707865, 0x61707865

align 16
constants1:
dd      0x3320646e, 0x3320646e, 0x3320646e, 0x3320646e

align 16
constants2:
dd      0x79622d32, 0x79622d32, 0x79622d32, 0x79622d32

align 16
constants3:
dd      0x6b206574, 0x6b206574, 0x6b206574, 0x6b206574

align 16
constants:
dd      0x61707865, 0x3320646e, 0x79622d32, 0x6b206574

align 16
dword_1:
dd      0x00000001, 0x00000000, 0x00000000, 0x00000000

align 16
dword_2:
dd      0x00000002, 0x00000000, 0x00000000, 0x00000000

align 16
dword_1_4:
dd      0x00000001, 0x00000002, 0x00000003, 0x00000004

align 16
dword_4:
dd      0x00000004, 0x00000004, 0x00000004, 0x00000004

align 16
shuf_mask_rotl8:
db      3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14

align 16
shuf_mask_rotl16:
db      2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13

align 16
poly_clamp_r:
dq      0x0ffffffc0fffffff, 0x0ffffffc0ffffffc

struc STACK
_STATE:         reso    16      ; Space to store first 4 states
_XMM_SAVE:      reso    2       ; Space to store up to 2 temporary XMM registers
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

%macro ENCRYPT_0B_64B 14-15
%define %%SRC  %1 ; [in/out] Source pointer
%define %%DST  %2 ; [in/out] Destination pointer
%define %%LEN  %3 ; [in/clobbered] Length to encrypt
%define %%OFF  %4 ; [in] Offset into src/dst
%define %%KS0  %5 ; [in/out] Bytes 0-15 of keystream
%define %%KS1  %6 ; [in/out] Bytes 16-31 of keystream
%define %%KS2  %7 ; [in/out] Bytes 32-47 of keystream
%define %%KS3  %8 ; [in/out] Bytes 48-63 of keystream
%define %%PT0  %9 ; [in/clobbered] Bytes 0-15 of plaintext
%define %%PT1  %10 ; [in/clobbered] Bytes 16-31 of plaintext
%define %%PT2  %11 ; [in/clobbered] Bytes 32-47 of plaintext
%define %%PT3  %12 ; [in/clobbered] Bytes 48-63 of plaintext
%define %%TMP  %13 ; [clobbered] Temporary GP register
%define %%TMP2 %14 ; [clobbered] Temporary GP register
%define %%KS_PTR %15 ; [in] Pointer to keystream

        or      %%LEN, %%LEN
        jz      %%end_encrypt

        cmp     %%LEN, 16
        jbe     %%up_to_16B

        cmp     %%LEN, 32
        jbe     %%up_to_32B

        cmp     %%LEN, 48
        jbe     %%up_to_48B

%%up_to_64B:
        vmovdqu  %%PT0, [%%SRC + %%OFF]
        vmovdqu  %%PT1, [%%SRC + %%OFF + 16]
        vmovdqu  %%PT2, [%%SRC + %%OFF + 32]
%if %0 == 15
        vmovdqu  %%KS0, [%%KS_PTR]
        vmovdqu  %%KS1, [%%KS_PTR + 16]
        vmovdqu  %%KS2, [%%KS_PTR + 32]
%endif
        vpxor    %%PT0, %%KS0
        vpxor    %%PT1, %%KS1
        vpxor    %%PT2, %%KS2
        vmovdqu  [%%DST + %%OFF], %%PT0
        vmovdqu  [%%DST + %%OFF + 16], %%PT1
        vmovdqu  [%%DST + %%OFF + 32], %%PT2

        add     %%SRC, %%OFF
        add     %%DST, %%OFF
        add     %%SRC, 48
        add     %%DST, 48
        sub     %%LEN, 48
        simd_load_avx_16_1 %%PT3, %%SRC, %%LEN

        ; XOR KS with plaintext and store resulting ciphertext
%if %0 == 15
        vmovdqu  %%KS3, [%%KS_PTR + 48]
%endif
        vpxor    %%PT3, %%KS3

        simd_store_avx %%DST, %%PT3, %%LEN, %%TMP, %%TMP2

        jmp     %%end_encrypt

%%up_to_48B:
        vmovdqu  %%PT0, [%%SRC + %%OFF]
        vmovdqu  %%PT1, [%%SRC + %%OFF + 16]
%if %0 == 15
        vmovdqu  %%KS0, [%%KS_PTR]
        vmovdqu  %%KS1, [%%KS_PTR + 16]
%endif
        vpxor    %%PT0, %%KS0
        vpxor    %%PT1, %%KS1
        vmovdqu  [%%DST + %%OFF], %%PT0
        vmovdqu  [%%DST + %%OFF + 16], %%PT1

        add     %%SRC, %%OFF
        add     %%DST, %%OFF
        add     %%SRC, 32
        add     %%DST, 32
        sub     %%LEN, 32
        simd_load_avx_16_1 %%PT2, %%SRC, %%LEN

        ; XOR KS with plaintext and store resulting ciphertext
%if %0 == 15
        vmovdqu %%KS2, [%%KS_PTR + 32]
%endif
        vpxor   %%PT2, %%KS2

        simd_store_avx %%DST, %%PT2, %%LEN, %%TMP, %%TMP2

        jmp     %%end_encrypt

%%up_to_32B:
        vmovdqu  %%PT0, [%%SRC + %%OFF]
%if %0 == 15
        vmovdqu  %%KS0, [%%KS_PTR]
%endif
        vpxor    %%PT0, %%KS0
        vmovdqu  [%%DST + %%OFF], %%PT0

        add     %%SRC, %%OFF
        add     %%DST, %%OFF
        add     %%SRC, 16
        add     %%DST, 16
        sub     %%LEN, 16
        simd_load_avx_16_1 %%PT1, %%SRC, %%LEN

        ; XOR KS with plaintext and store resulting ciphertext
%if %0 == 15
        vmovdqu  %%KS1, [%%KS_PTR + 16]
%endif
        vpxor    %%PT1, %%KS1

        simd_store_avx %%DST, %%PT1, %%LEN, %%TMP, %%TMP2

        jmp     %%end_encrypt

%%up_to_16B:
        add     %%SRC, %%OFF
        add     %%DST, %%OFF
        simd_load_avx_16_1 %%PT0, %%SRC, %%LEN

        ; XOR KS with plaintext and store resulting ciphertext
%if %0 == 15
        vmovdqu  %%KS0, [%%KS_PTR]
%endif
        vpxor    %%PT0, %%KS0

        simd_store_avx %%DST, %%PT0, %%LEN, %%TMP, %%TMP2

%%end_encrypt:
        add     %%SRC, %%LEN
        add     %%DST, %%LEN
%endmacro

;; 4x4 32-bit transpose function
%macro TRANSPOSE4_U32 6
%define %%r0 %1 ;; [in/out] Input first row / output third column
%define %%r1 %2 ;; [in/out] Input second row / output second column
%define %%r2 %3 ;; [in/clobbered] Input third row
%define %%r3 %4 ;; [in/out] Input fourth row / output fourth column
%define %%t0 %5 ;; [out] Temporary XMM register / output first column
%define %%t1 %6 ;; [clobbered] Temporary XMM register

        vshufps	%%t0, %%r0, %%r1, 0x44	; t0 = {b1 b0 a1 a0}
        vshufps %%r0, %%r1, 0xEE	; r0 = {b3 b2 a3 a2}
        vshufps %%t1, %%r2, %%r3, 0x44	; t1 = {d1 d0 c1 c0}
        vshufps	%%r2, %%r3, 0xEE	; r2 = {d3 d2 c3 c2}

        vshufps	%%r1, %%t0, %%t1, 0xDD	; r1 = {d1 c1 b1 a1}
        vshufps	%%r3, %%r0, %%r2, 0xDD	; r3 = {d3 c3 b3 a3}
        vshufps	%%r0, %%r2, 0x88	; r0 = {d2 c2 b2 a2}
        vshufps	%%t0, %%t1, 0x88	; t0 = {d0 c0 b0 a0}
%endmacro

; Rotate dwords on a XMM registers to the left N_BITS
%macro VPROLD 3
%define %%XMM_IN %1 ; [in/out] XMM register to be rotated
%define %%N_BITS %2 ; [immediate] Number of bits to rotate
%define %%XTMP   %3 ; [clobbered] XMM temporary register
%if %%N_BITS == 8
        vpshufb %%XMM_IN, [rel shuf_mask_rotl8]
%elif %%N_BITS == 16
        vpshufb %%XMM_IN, [rel shuf_mask_rotl16]
%else
        vpsrld  %%XTMP, %%XMM_IN, (32-%%N_BITS)
        vpslld  %%XMM_IN, %%N_BITS
        vpor    %%XMM_IN, %%XTMP
%endif
%endmacro

;;
;; Performs a quarter round on all 4 columns,
;; resulting in a full round
;;
%macro quarter_round 5
%define %%A    %1 ;; [in/out] XMM register containing value A of all 4 columns
%define %%B    %2 ;; [in/out] XMM register containing value B of all 4 columns
%define %%C    %3 ;; [in/out] XMM register containing value C of all 4 columns
%define %%D    %4 ;; [in/out] XMM register containing value D of all 4 columns
%define %%XTMP %5 ;; [clobbered] Temporary XMM register

        vpaddd  %%A, %%B
        vpxor   %%D, %%A
        VPROLD  %%D, 16, %%XTMP
        vpaddd  %%C, %%D
        vpxor   %%B, %%C
        VPROLD  %%B, 12, %%XTMP
        vpaddd  %%A, %%B
        vpxor   %%D, %%A
        VPROLD  %%D, 8, %%XTMP
        vpaddd  %%C, %%D
        vpxor   %%B, %%C
        VPROLD  %%B, 7, %%XTMP

%endmacro

%macro quarter_round_x2 9
%define %%A_L    %1 ;; [in/out] XMM register containing value A of all 4 columns
%define %%B_L    %2 ;; [in/out] XMM register containing value B of all 4 columns
%define %%C_L    %3 ;; [in/out] XMM register containing value C of all 4 columns
%define %%D_L    %4 ;; [in/out] XMM register containing value D of all 4 columns
%define %%A_H    %5 ;; [in/out] XMM register containing value A of all 4 columns
%define %%B_H    %6 ;; [in/out] XMM register containing value B of all 4 columns
%define %%C_H    %7 ;; [in/out] XMM register containing value C of all 4 columns
%define %%D_H    %8 ;; [in/out] XMM register containing value D of all 4 columns
%define %%XTMP   %9 ;; [clobbered] Temporary XMM register

        vpaddd  %%A_L, %%B_L
        vpaddd  %%A_H, %%B_H
        vpxor   %%D_L, %%A_L
        vpxor   %%D_H, %%A_H
        VPROLD  %%D_L, 16, %%XTMP
        VPROLD  %%D_H, 16, %%XTMP
        vpaddd  %%C_L, %%D_L
        vpaddd  %%C_H, %%D_H
        vpxor   %%B_L, %%C_L
        vpxor   %%B_H, %%C_H
        VPROLD  %%B_L, 12, %%XTMP
        VPROLD  %%B_H, 12, %%XTMP
        vpaddd  %%A_L, %%B_L
        vpaddd  %%A_H, %%B_H
        vpxor   %%D_L, %%A_L
        vpxor   %%D_H, %%A_H
        VPROLD  %%D_L, 8, %%XTMP
        VPROLD  %%D_H, 8, %%XTMP
        vpaddd  %%C_L, %%D_L
        vpaddd  %%C_H, %%D_H
        vpxor   %%B_L, %%C_L
        vpxor   %%B_H, %%C_H
        VPROLD  %%B_L, 7, %%XTMP
        VPROLD  %%B_H, 7, %%XTMP

%endmacro

;;
;; Rotates the registers to prepare the data
;; from column round to diagonal round
;;
%macro column_to_diag 3
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
%macro diag_to_column 3
%define %%B %1 ;; [in/out] XMM register containing value B of all 4 columns
%define %%C %2 ;; [in/out] XMM register containing value C of all 4 columns
%define %%D %3 ;; [in/out] XMM register containing value D of all 4 columns

        vpshufd %%B, %%B, 0x93 ; 0b10010011 ; 2,1,0,3
        vpshufd %%C, %%C, 0x4E ; 0b01001110 ; 1,0,3,2
        vpshufd %%D, %%D, 0x39 ; 0b00111001 ; 0,3,2,1

%endmacro

;;
;; Generates 64 or 128 bytes of keystream
;; States IN A-C are the same for first 64 and last 64 bytes
;; State IN D differ because of the different block count
;;
%macro GENERATE_64_128_KS 9-14
%define %%STATE_IN_A      %1  ;; [in] XMM containing state A
%define %%STATE_IN_B      %2  ;; [in] XMM containing state B
%define %%STATE_IN_C      %3  ;; [in] XMM containing state C
%define %%STATE_IN_D_L    %4  ;; [in] XMM containing state D (low block count)
%define %%A_L_KS0         %5  ;; [out] XMM to contain keystream 0-15 bytes
%define %%B_L_KS1         %6  ;; [out] XMM to contain keystream 16-31 bytes
%define %%C_L_KS2         %7  ;; [out] XMM to contain keystream 32-47 bytes
%define %%D_L_KS3         %8  ;; [out] XMM to contain keystream 48-63 bytes
%define %%XTMP            %9  ;; [clobbered] Temporary XMM register
%define %%STATE_IN_D_H    %10  ;; [in] XMM containing state D (high block count)
%define %%A_H_KS4         %11  ;; [out] XMM to contain keystream 64-79 bytes
%define %%B_H_KS5         %12  ;; [out] XMM to contain keystream 80-95 bytes
%define %%C_H_KS6         %13  ;; [out] XMM to contain keystream 96-111 bytes
%define %%D_H_KS7         %14  ;; [out] XMM to contain keystream 112-127 bytes

        vmovdqa %%A_L_KS0, %%STATE_IN_A
        vmovdqa %%B_L_KS1, %%STATE_IN_B
        vmovdqa %%C_L_KS2, %%STATE_IN_C
        vmovdqa %%D_L_KS3, %%STATE_IN_D_L
%if %0 == 14
        vmovdqa %%A_H_KS4, %%STATE_IN_A
        vmovdqa %%B_H_KS5, %%STATE_IN_B
        vmovdqa %%C_H_KS6, %%STATE_IN_C
        vmovdqa %%D_H_KS7, %%STATE_IN_D_H
%endif
%rep 10
%if %0 == 14
        quarter_round_x2 %%A_L_KS0, %%B_L_KS1, %%C_L_KS2, %%D_L_KS3, \
                %%A_H_KS4, %%B_H_KS5, %%C_H_KS6, %%D_H_KS7, %%XTMP
        column_to_diag %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
        column_to_diag %%B_H_KS5, %%C_H_KS6, %%D_H_KS7
        quarter_round_x2 %%A_L_KS0, %%B_L_KS1, %%C_L_KS2, %%D_L_KS3, \
                %%A_H_KS4, %%B_H_KS5, %%C_H_KS6, %%D_H_KS7, %%XTMP
        diag_to_column %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
        diag_to_column %%B_H_KS5, %%C_H_KS6, %%D_H_KS7
%else
        quarter_round %%A_L_KS0, %%B_L_KS1, %%C_L_KS2, %%D_L_KS3, %%XTMP
        column_to_diag %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
        quarter_round %%A_L_KS0, %%B_L_KS1, %%C_L_KS2, %%D_L_KS3, %%XTMP
        diag_to_column %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
%endif
%endrep

        vpaddd  %%A_L_KS0, %%STATE_IN_A
        vpaddd  %%B_L_KS1, %%STATE_IN_B
        vpaddd  %%C_L_KS2, %%STATE_IN_C
        vpaddd  %%D_L_KS3, %%STATE_IN_D_L
%if %0 == 14
        vpaddd  %%A_H_KS4, %%STATE_IN_A
        vpaddd  %%B_H_KS5, %%STATE_IN_B
        vpaddd  %%C_H_KS6, %%STATE_IN_C
        vpaddd  %%D_H_KS7, %%STATE_IN_D_H
%endif
%endmacro

; Perform 4 times the operation in first parameter
%macro XMM_OP_X4 9
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

%macro XMM_ROLS_X4  6
%define %%XMM_OP1_1      %1
%define %%XMM_OP1_2      %2
%define %%XMM_OP1_3      %3
%define %%XMM_OP1_4      %4
%define %%BITS_TO_ROTATE %5
%define %%XTMP           %6

        ; Store temporary register when bits to rotate is not 8 and 16,
        ; as the register will be clobbered in these cases,
        ; containing needed information
%if %%BITS_TO_ROTATE != 8 && %%BITS_TO_ROTATE != 16
        vmovdqa [rsp + _XMM_SAVE], %%XTMP
%endif
        VPROLD  %%XMM_OP1_1, %%BITS_TO_ROTATE, %%XTMP
        VPROLD  %%XMM_OP1_2, %%BITS_TO_ROTATE, %%XTMP
        VPROLD  %%XMM_OP1_3, %%BITS_TO_ROTATE, %%XTMP
        VPROLD  %%XMM_OP1_4, %%BITS_TO_ROTATE, %%XTMP
%if %%BITS_TO_ROTATE != 8 && %%BITS_TO_ROTATE != 16
        vmovdqa %%XTMP, [rsp + _XMM_SAVE]
%endif
%endmacro

;;
;; Performs a full chacha20 round on 4 states,
;; consisting of 4 quarter rounds, which are done in parallel
;;
%macro CHACHA20_ROUND 16
%define %%XMM_DWORD_A1  %1  ;; [in/out] XMM register containing dword A for first quarter round
%define %%XMM_DWORD_A2  %2  ;; [in/out] XMM register containing dword A for second quarter round
%define %%XMM_DWORD_A3  %3  ;; [in/out] XMM register containing dword A for third quarter round
%define %%XMM_DWORD_A4  %4  ;; [in/out] XMM register containing dword A for fourth quarter round
%define %%XMM_DWORD_B1  %5  ;; [in/out] XMM register containing dword B for first quarter round
%define %%XMM_DWORD_B2  %6  ;; [in/out] XMM register containing dword B for second quarter round
%define %%XMM_DWORD_B3  %7  ;; [in/out] XMM register containing dword B for third quarter round
%define %%XMM_DWORD_B4  %8  ;; [in/out] XMM register containing dword B for fourth quarter round
%define %%XMM_DWORD_C1  %9  ;; [in/out] XMM register containing dword C for first quarter round
%define %%XMM_DWORD_C2 %10  ;; [in/out] XMM register containing dword C for second quarter round
%define %%XMM_DWORD_C3 %11  ;; [in/out] XMM register containing dword C for third quarter round
%define %%XMM_DWORD_C4 %12  ;; [in/out] XMM register containing dword C for fourth quarter round
%define %%XMM_DWORD_D1 %13  ;; [in/out] XMM register containing dword D for first quarter round
%define %%XMM_DWORD_D2 %14  ;; [in/out] XMM register containing dword D for second quarter round
%define %%XMM_DWORD_D3 %15  ;; [in/out] XMM register containing dword D for third quarter round
%define %%XMM_DWORD_D4 %16  ;; [in/out] XMM register containing dword D for fourth quarter round

        ; A += B
        XMM_OP_X4 vpaddd, %%XMM_DWORD_A1, %%XMM_DWORD_A2, %%XMM_DWORD_A3, %%XMM_DWORD_A4, \
                         %%XMM_DWORD_B1, %%XMM_DWORD_B2, %%XMM_DWORD_B3, %%XMM_DWORD_B4
        ; D ^= A
        XMM_OP_X4 vpxor, %%XMM_DWORD_D1, %%XMM_DWORD_D2, %%XMM_DWORD_D3, %%XMM_DWORD_D4, \
                        %%XMM_DWORD_A1, %%XMM_DWORD_A2, %%XMM_DWORD_A3, %%XMM_DWORD_A4

        ; D <<< 16
        XMM_ROLS_X4 %%XMM_DWORD_D1, %%XMM_DWORD_D2, %%XMM_DWORD_D3, %%XMM_DWORD_D4, 16, \
                    %%XMM_DWORD_B1

        ; C += D
        XMM_OP_X4 vpaddd, %%XMM_DWORD_C1, %%XMM_DWORD_C2, %%XMM_DWORD_C3, %%XMM_DWORD_C4, \
                         %%XMM_DWORD_D1, %%XMM_DWORD_D2, %%XMM_DWORD_D3, %%XMM_DWORD_D4
        ; B ^= C
        XMM_OP_X4 vpxor, %%XMM_DWORD_B1, %%XMM_DWORD_B2, %%XMM_DWORD_B3, %%XMM_DWORD_B4, \
                        %%XMM_DWORD_C1, %%XMM_DWORD_C2, %%XMM_DWORD_C3, %%XMM_DWORD_C4

        ; B <<< 12
        XMM_ROLS_X4 %%XMM_DWORD_B1, %%XMM_DWORD_B2, %%XMM_DWORD_B3, %%XMM_DWORD_B4, 12, \
                    %%XMM_DWORD_D1

        ; A += B
        XMM_OP_X4 vpaddd, %%XMM_DWORD_A1, %%XMM_DWORD_A2, %%XMM_DWORD_A3, %%XMM_DWORD_A4, \
                          %%XMM_DWORD_B1, %%XMM_DWORD_B2, %%XMM_DWORD_B3, %%XMM_DWORD_B4
        ; D ^= A
        XMM_OP_X4 vpxor, %%XMM_DWORD_D1, %%XMM_DWORD_D2, %%XMM_DWORD_D3, %%XMM_DWORD_D4, \
                          %%XMM_DWORD_A1, %%XMM_DWORD_A2, %%XMM_DWORD_A3, %%XMM_DWORD_A4

        ; D <<< 8
        XMM_ROLS_X4 %%XMM_DWORD_D1, %%XMM_DWORD_D2, %%XMM_DWORD_D3, %%XMM_DWORD_D4, 8, \
                    %%XMM_DWORD_B1

        ; C += D
        XMM_OP_X4 vpaddd, %%XMM_DWORD_C1, %%XMM_DWORD_C2, %%XMM_DWORD_C3, %%XMM_DWORD_C4, \
                          %%XMM_DWORD_D1, %%XMM_DWORD_D2, %%XMM_DWORD_D3, %%XMM_DWORD_D4
        ; B ^= C
        XMM_OP_X4 vpxor, %%XMM_DWORD_B1, %%XMM_DWORD_B2, %%XMM_DWORD_B3, %%XMM_DWORD_B4, \
                          %%XMM_DWORD_C1, %%XMM_DWORD_C2, %%XMM_DWORD_C3, %%XMM_DWORD_C4

        ; B <<< 7
        XMM_ROLS_X4 %%XMM_DWORD_B1, %%XMM_DWORD_B2, %%XMM_DWORD_B3, %%XMM_DWORD_B4, 7, \
                    %%XMM_DWORD_D1
%endmacro

;;
;; Encodes 4 Chacha20 states, outputting 256 bytes of keystream
;; Data still needs to be transposed to get the keystream in the correct order
;;
%macro GENERATE_256_KS 16
%define %%XMM_DWORD_0   %1  ;; [out] XMM register to contain encoded dword 0 of the 4 Chacha20 states
%define %%XMM_DWORD_1   %2  ;; [out] XMM register to contain encoded dword 1 of the 4 Chacha20 states
%define %%XMM_DWORD_2   %3  ;; [out] XMM register to contain encoded dword 2 of the 4 Chacha20 states
%define %%XMM_DWORD_3   %4  ;; [out] XMM register to contain encoded dword 3 of the 4 Chacha20 states
%define %%XMM_DWORD_4   %5  ;; [out] XMM register to contain encoded dword 4 of the 4 Chacha20 states
%define %%XMM_DWORD_5   %6  ;; [out] XMM register to contain encoded dword 5 of the 4 Chacha20 states
%define %%XMM_DWORD_6   %7  ;; [out] XMM register to contain encoded dword 6 of the 4 Chacha20 states
%define %%XMM_DWORD_7   %8  ;; [out] XMM register to contain encoded dword 7 of the 4 Chacha20 states
%define %%XMM_DWORD_8   %9  ;; [out] XMM register to contain encoded dword 8 of the 4 Chacha20 states
%define %%XMM_DWORD_9  %10  ;; [out] XMM register to contain encoded dword 9 of the 4 Chacha20 states
%define %%XMM_DWORD_10 %11  ;; [out] XMM register to contain encoded dword 10 of the 4 Chacha20 states
%define %%XMM_DWORD_11 %12  ;; [out] XMM register to contain encoded dword 11 of the 4 Chacha20 states
%define %%XMM_DWORD_12 %13  ;; [out] XMM register to contain encoded dword 12 of the 4 Chacha20 states
%define %%XMM_DWORD_13 %14  ;; [out] XMM register to contain encoded dword 13 of the 4 Chacha20 states
%define %%XMM_DWORD_14 %15  ;; [out] XMM register to contain encoded dword 14 of the 4 Chacha20 states
%define %%XMM_DWORD_15 %16  ;; [out] XMM register to contain encoded dword 15 of the 4 Chacha20 states

%assign i 0
%rep 16
        vmovdqa APPEND(%%XMM_DWORD_, i), [rsp + _STATE + 16*i]
%assign i (i + 1)
%endrep

%rep 10
        CHACHA20_ROUND %%XMM_DWORD_0, %%XMM_DWORD_1, %%XMM_DWORD_2, %%XMM_DWORD_3, \
                       %%XMM_DWORD_4, %%XMM_DWORD_5, %%XMM_DWORD_6, %%XMM_DWORD_7, \
                       %%XMM_DWORD_8, %%XMM_DWORD_9, %%XMM_DWORD_10, %%XMM_DWORD_11, \
                       %%XMM_DWORD_12, %%XMM_DWORD_13, %%XMM_DWORD_14, %%XMM_DWORD_15

        CHACHA20_ROUND %%XMM_DWORD_0, %%XMM_DWORD_1, %%XMM_DWORD_2, %%XMM_DWORD_3, \
                       %%XMM_DWORD_5, %%XMM_DWORD_6, %%XMM_DWORD_7, %%XMM_DWORD_4, \
                       %%XMM_DWORD_10, %%XMM_DWORD_11, %%XMM_DWORD_8, %%XMM_DWORD_9, \
                       %%XMM_DWORD_15, %%XMM_DWORD_12, %%XMM_DWORD_13, %%XMM_DWORD_14
%endrep

%assign i 0
%rep 16
        vpaddd  APPEND(%%XMM_DWORD_, i), [rsp + _STATE + 16*i]
%assign i (i + 1)
%endrep
%endmacro

align 32
MKGLOBAL(submit_job_chacha20_enc_dec_avx,function,internal)
submit_job_chacha20_enc_dec_avx:

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
        and     rsp, -16
        mov     [rsp + _RSP_SAVE], rax ; save RSP

        xor     off, off

        ; If less than or equal to 64*2 bytes, prepare directly states for
        ; up to 2 blocks
        cmp     len, 64*2
        jbe     check_1_or_2_blocks_left

        ; Prepare first 4 chacha states
        vmovdqa xmm0, [rel constants0]
        vmovdqa xmm1, [rel constants1]
        vmovdqa xmm2, [rel constants2]
        vmovdqa xmm3, [rel constants3]

        ; Broadcast 8 dwords from key into XMM4-11
        vmovdqu xmm12, [keys]
        vmovdqu xmm15, [keys + 16]
        vpshufd xmm4, xmm12, 0x0
        vpshufd xmm5, xmm12, 0x55
        vpshufd xmm6, xmm12, 0xAA
        vpshufd xmm7, xmm12, 0xFF
        vpshufd xmm8, xmm15, 0x0
        vpshufd xmm9, xmm15, 0x55
        vpshufd xmm10, xmm15, 0xAA
        vpshufd xmm11, xmm15, 0xFF

        ; Broadcast 3 dwords from IV into XMM13-15
        vmovd   xmm13, [iv]
        vmovd   xmm14, [iv + 4]
        vpshufd xmm13, xmm13, 0
        vpshufd xmm14, xmm14, 0
        vmovd   xmm15, [iv + 8]
        vpshufd xmm15, xmm15, 0

        ; Set block counters for first 4 Chacha20 states
        vmovdqa xmm12, [rel dword_1_4]

%assign i 0
%rep 16
        vmovdqa [rsp + _STATE + 16*i], xmm %+ i
%assign i (i + 1)
%endrep

        cmp     len, 64*4
        jb      exit_loop

align 32
start_loop:

        ; Generate 256 bytes of keystream
        GENERATE_256_KS xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, \
                        xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15

        ;; Transpose state to get keystream and XOR with plaintext
        ;; to get ciphertext

        ; Save registers to be used as temp registers
        vmovdqa [rsp + _XMM_SAVE], xmm14
        vmovdqa [rsp + _XMM_SAVE + 16], xmm15

        ; Transpose to get 0-15, 64-79, 128-143, 192-207 bytes of KS
        TRANSPOSE4_U32 xmm0, xmm1, xmm2, xmm3, xmm14, xmm15

        ; xmm14, xmm1, xmm0, xmm3
        ; xmm2, xmm15 free to use
        vmovdqu xmm2, [src + off]
        vmovdqu xmm15, [src + off + 16*4]
        vpxor   xmm14, xmm2
        vpxor   xmm1, xmm15
        vmovdqu [dst + off], xmm14
        vmovdqu [dst + off + 16*4], xmm1

        vmovdqu xmm2, [src + off + 16*8]
        vmovdqu xmm15, [src + off + 16*12]
        vpxor   xmm0, xmm2
        vpxor   xmm3, xmm15
        vmovdqu [dst + off + 16*8], xmm0
        vmovdqu [dst + off + 16*12], xmm3

        ; Restore registers and use xmm0, xmm1 now that they are free
        vmovdqa xmm14, [rsp + _XMM_SAVE]
        vmovdqa xmm15, [rsp + _XMM_SAVE + 16]

        ; Transpose to get 16-31, 80-95, 144-159, 208-223 bytes of KS
        TRANSPOSE4_U32 xmm4, xmm5, xmm6, xmm7, xmm0, xmm1

        ; xmm0, xmm5, xmm4, xmm7
        ; xmm6, xmm1 free to use
        vmovdqu xmm6, [src + off + 16]
        vmovdqu xmm1, [src + off + 16*5]
        vpxor   xmm0, xmm6
        vpxor   xmm5, xmm1
        vmovdqu [dst + off + 16], xmm0
        vmovdqu [dst + off + 16*5], xmm5

        vmovdqu xmm6, [src + off + 16*9]
        vmovdqu xmm1, [src + off + 16*13]
        vpxor   xmm4, xmm6
        vpxor   xmm7, xmm1
        vmovdqu [dst + off + 16*9], xmm4
        vmovdqu [dst + off + 16*13], xmm7

        ; Transpose to get 32-47, 96-111, 160-175, 224-239 bytes of KS
        TRANSPOSE4_U32 xmm8, xmm9, xmm10, xmm11, xmm0, xmm1

        ; xmm0, xmm9, xmm8, xmm11
        ; xmm10, xmm1 free to use
        vmovdqu xmm10, [src + off + 16*2]
        vmovdqu xmm1, [src + off + 16*6]
        vpxor   xmm0, xmm10
        vpxor   xmm9, xmm1
        vmovdqu [dst + off + 16*2], xmm0
        vmovdqu [dst + off + 16*6], xmm9

        vmovdqu xmm10, [src + off + 16*10]
        vmovdqu xmm1, [src + off + 16*14]
        vpxor   xmm8, xmm10
        vpxor   xmm11, xmm1
        vmovdqu [dst + off + 16*10], xmm8
        vmovdqu [dst + off + 16*14], xmm11

        ; Transpose to get 48-63, 112-127, 176-191, 240-255 bytes of KS
        TRANSPOSE4_U32 xmm12, xmm13, xmm14, xmm15, xmm0, xmm1

        ; xmm0, xmm13, xmm12, xmm15
        ; xmm14, xmm1 free to use
        vmovdqu xmm14, [src + off + 16*3]
        vmovdqu xmm1, [src + off + 16*7]
        vpxor   xmm0, xmm14
        vpxor   xmm13, xmm1
        vmovdqu [dst + off + 16*3], xmm0
        vmovdqu [dst + off + 16*7], xmm13

        vmovdqu xmm14, [src + off + 16*11]
        vmovdqu xmm1, [src + off + 16*15]
        vpxor   xmm12, xmm14
        vpxor   xmm15, xmm1
        vmovdqu [dst + off + 16*11], xmm12
        vmovdqu [dst + off + 16*15], xmm15
        ; Update remaining length
        sub     len, 64*4
        add     off, 64*4

        ; Update counter values
        vmovdqa xmm12, [rsp + 16*12]
        vpaddd  xmm12, [rel dword_4]
        vmovdqa [rsp + 16*12], xmm12

        cmp     len, 64*4
        jae     start_loop

exit_loop:

        ; Check if there are no more bytes to encrypt
        or      len, len
        jz      no_partial_block

        cmp     len, 64*2
        ja      more_than_2_blocks_left

check_1_or_2_blocks_left:
        cmp     len, 64
        ja      two_blocks_left

        ;; 1 block left

        ; Get last block counter dividing offset by 64
        shr     off, 6

        ; Prepare next chacha state from IV, key
        vmovdqu xmm1, [keys]          ; Load key bytes 0-15
        vmovdqu xmm2, [keys + 16]     ; Load key bytes 16-31
        ; Read nonce (12 bytes)
        vmovq   xmm3, [iv]
        vpinsrd xmm3, [iv + 8], 2
        vpslldq xmm3, 4
        vpinsrd xmm3, DWORD(off), 0
        vmovdqa xmm0, [rel constants]

        ; Increase block counter
        vpaddd  xmm3, [rel dword_1]
        shl     off, 6 ; Restore offset

        ; Generate 64 bytes of keystream
        GENERATE_64_128_KS xmm0, xmm1, xmm2, xmm3, xmm9, xmm10, xmm11, \
                           xmm12, xmm13

        cmp     len, 64
        jne     less_than_64

        ;; Exactly 64 bytes left

        ; Load plaintext, XOR with KS and store ciphertext
        vmovdqu xmm14, [src + off]
        vmovdqu xmm15, [src + off + 16]
        vpxor   xmm14, xmm9
        vpxor   xmm15, xmm10
        vmovdqu [dst + off], xmm14
        vmovdqu [dst + off + 16], xmm15

        vmovdqu xmm14, [src + off + 16*2]
        vmovdqu xmm15, [src + off + 16*3]
        vpxor   xmm14, xmm11
        vpxor   xmm15, xmm12
        vmovdqu [dst + off + 16*2], xmm14
        vmovdqu [dst + off + 16*3], xmm15

        jmp     no_partial_block

less_than_64:

        cmp     len, 48
        jb      less_than_48

        ; Load plaintext and XOR with keystream
        vmovdqu xmm13, [src + off]
        vmovdqu xmm14, [src + off + 16]
        vmovdqu xmm15, [src + off + 32]

        vpxor   xmm13, xmm9
        vpxor   xmm14, xmm10
        vpxor   xmm15, xmm11

        ; Store resulting ciphertext
        vmovdqu [dst + off], xmm13
        vmovdqu [dst + off + 16], xmm14
        vmovdqu [dst + off + 32], xmm15

        ; Store last KS in xmm9, for partial block
        vmovdqu xmm9, xmm12

        sub     len, 48
        add     off, 48

        jmp     check_partial
less_than_48:
        cmp     len, 32
        jb      less_than_32

        ; Load plaintext and XOR with keystream
        vmovdqu xmm13, [src + off]
        vmovdqu xmm14, [src + off + 16]

        vpxor   xmm13, xmm9
        vpxor   xmm14, xmm10

        ; Store resulting ciphertext
        vmovdqu [dst + off], xmm13
        vmovdqu [dst + off + 16], xmm14

        ; Store last KS in xmm9, for partial block
        vmovdqu xmm9, xmm11

        sub     len, 32
        add     off, 32

        jmp     check_partial

less_than_32:
        cmp     len, 16
        jb      check_partial

        ; Load plaintext and XOR with keystream
        vmovdqu xmm13, [src + off]

        vpxor   xmm13, xmm9

        ; Store resulting ciphertext
        vmovdqu [dst + off], xmm13

        ; Store last KS in xmm9, for partial block
        vmovdqu xmm9, xmm10

        sub     len, 16
        add     off, 16

check_partial:
        or      len, len
        jz      no_partial_block

        add     src, off
        add     dst, off
        ; Load plaintext
        simd_load_avx_15_1 xmm8, src, len

        ; XOR KS with plaintext and store resulting ciphertext
        vpxor   xmm8, xmm9

        simd_store_avx_15 dst, xmm8, len, tmp, tmp2

        jmp     no_partial_block

two_blocks_left:

        ; Get last block counter dividing offset by 64
        shr     off, 6

        ; Prepare next 2 chacha states from IV, key
        vmovdqu xmm1, [keys]          ; Load key bytes 0-15
        vmovdqu xmm2, [keys + 16]     ; Load key bytes 16-31
        ; Read nonce (12 bytes)
        vmovq   xmm3, [iv]
        vpinsrd xmm3, [iv + 8], 2
        vpslldq xmm3, 4
        vpinsrd xmm3, DWORD(off), 0
        vmovdqa xmm0, [rel constants]

        vmovdqa xmm8, xmm3

        ; Increase block counters
        vpaddd  xmm3, [rel dword_1]
        vpaddd  xmm8, [rel dword_2]

        shl     off, 6 ; Restore offset

        ; Generate 128 bytes of keystream
        GENERATE_64_128_KS xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, \
                           xmm13, xmm8, xmm9, xmm10, xmm11, xmm12

        cmp     len, 128
        jb      between_64_127

        ; Load plaintext, XOR with KS and store ciphertext
        vmovdqu xmm14, [src + off]
        vmovdqu xmm15, [src + off + 16]
        vpxor   xmm14, xmm4
        vpxor   xmm15, xmm5
        vmovdqu [dst + off], xmm14
        vmovdqu [dst + off + 16], xmm15

        vmovdqu xmm14, [src + off + 16*2]
        vmovdqu xmm15, [src + off + 16*3]
        vpxor   xmm14, xmm6
        vpxor   xmm15, xmm7
        vmovdqu [dst + off + 16*2], xmm14
        vmovdqu [dst + off + 16*3], xmm15

        vmovdqu xmm14, [src + off + 16*4]
        vmovdqu xmm15, [src + off + 16*5]
        vpxor   xmm14, xmm9
        vpxor   xmm15, xmm10
        vmovdqu [dst + off + 16*4], xmm14
        vmovdqu [dst + off + 16*5], xmm15

        vmovdqu xmm14, [src + off + 16*6]
        vmovdqu xmm15, [src + off + 16*7]
        vpxor   xmm14, xmm11
        vpxor   xmm15, xmm12
        vmovdqu [dst + off + 16*6], xmm14
        vmovdqu [dst + off + 16*7], xmm15

        jmp     no_partial_block

between_64_127:
        ; Load plaintext, XOR with KS and store ciphertext for first 64 bytes
        vmovdqu xmm14, [src + off]
        vmovdqu xmm15, [src + off + 16]
        vpxor   xmm14, xmm4
        vpxor   xmm15, xmm5
        vmovdqu [dst + off], xmm14
        vmovdqu [dst + off + 16], xmm15

        vmovdqu xmm14, [src + off + 16*2]
        vmovdqu xmm15, [src + off + 16*3]
        vpxor   xmm14, xmm6
        vpxor   xmm15, xmm7
        vmovdqu [dst + off + 16*2], xmm14
        vmovdqu [dst + off + 16*3], xmm15

        sub     len, 64
        add     off, 64
        ; Handle rest up to 63 bytes in "less_than_64"
        jmp     less_than_64

more_than_2_blocks_left:

        ; Generate 256 bytes of keystream
        GENERATE_256_KS xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, \
                        xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15

        ;; Transpose state to get keystream and XOR with plaintext
        ;; to get ciphertext

        ; Save registers to be used as temp registers
        vmovdqa [rsp + _XMM_SAVE], xmm14
        vmovdqa [rsp + _XMM_SAVE + 16], xmm15

        ; Transpose to get 0-15, 64-79, 128-143, 192-207 bytes of KS
        TRANSPOSE4_U32 xmm0, xmm1, xmm2, xmm3, xmm14, xmm15

        ; xmm14, xmm1, xmm0, xmm3
        vmovdqa xmm2, xmm0
        vmovdqa xmm0, xmm14
        ; xmm0-3 containing [64*I : 64*I + 15] (I = 0-3) bytes of KS

        ; Restore registers and save xmm0,xmm1 and use them instead
        vmovdqa xmm14, [rsp + _XMM_SAVE]
        vmovdqa xmm15, [rsp + _XMM_SAVE + 16]

        vmovdqa [rsp + _XMM_SAVE], xmm2
        vmovdqa [rsp + _XMM_SAVE + 16], xmm3

        ; Transpose to get 16-31, 80-95, 144-159, 208-223 bytes of KS
        TRANSPOSE4_U32 xmm4, xmm5, xmm6, xmm7, xmm2, xmm3

        ; xmm2, xmm5, xmm4, xmm7
        vmovdqa xmm6, xmm4
        vmovdqa xmm4, xmm2
        ; xmm4-7 containing [64*I + 16 : 64*I + 31] (I = 0-3) bytes of KS

        ; Transpose to get 32-47, 96-111, 160-175, 224-239 bytes of KS
        TRANSPOSE4_U32 xmm8, xmm9, xmm10, xmm11, xmm2, xmm3

        ; xmm2, xmm9, xmm8, xmm11
        vmovdqa xmm10, xmm8
        vmovdqa xmm8, xmm2
        ; xmm8-11 containing [64*I + 32 : 64*I + 47] (I = 0-3) bytes of KS

        ; Transpose to get 48-63, 112-127, 176-191, 240-255 bytes of KS
        TRANSPOSE4_U32 xmm12, xmm13, xmm14, xmm15, xmm2, xmm3

        ; xmm2, xmm13, xmm12, xmm15
        vmovdqa xmm14, xmm12
        vmovdqa xmm12, xmm2
        ; xmm12-15 containing [64*I + 48 : 64*I + 63] (I = 0-3) bytes of KS

        ; Encrypt first 128 bytes of plaintext (there are at least two 64 byte blocks to process)
        vmovdqu xmm2, [src + off]
        vmovdqu xmm3, [src + off + 16]
        vpxor   xmm0, xmm2
        vpxor   xmm4, xmm3
        vmovdqu [dst + off], xmm0
        vmovdqu [dst + off + 16], xmm4

        vmovdqu xmm2, [src + off + 16*2]
        vmovdqu xmm3, [src + off + 16*3]
        vpxor   xmm8, xmm2
        vpxor   xmm12, xmm3
        vmovdqu [dst + off + 16*2], xmm8
        vmovdqu [dst + off + 16*3], xmm12

        vmovdqu xmm2, [src + off + 16*4]
        vmovdqu xmm3, [src + off + 16*5]
        vpxor   xmm1, xmm2
        vpxor   xmm5, xmm3
        vmovdqu [dst + off + 16*4], xmm1
        vmovdqu [dst + off + 16*5], xmm5

        vmovdqu xmm2, [src + off + 16*6]
        vmovdqu xmm3, [src + off + 16*7]
        vpxor   xmm9, xmm2
        vpxor   xmm13, xmm3
        vmovdqu [dst + off + 16*6], xmm9
        vmovdqu [dst + off + 16*7], xmm13

        ; Restore xmm2,xmm3
        vmovdqa xmm2, [rsp + _XMM_SAVE]
        vmovdqa xmm3, [rsp + _XMM_SAVE + 16]

        sub     len, 128
        add     off, 128
        ; Use now xmm0,xmm1 as scratch registers

        ; Check if there is at least 64 bytes more to process
        cmp     len, 64
        jb      between_129_191

        ; Encrypt next 64 bytes (128-191)
        vmovdqu xmm0, [src + off]
        vmovdqu xmm1, [src + off + 16]
        vpxor   xmm2, xmm0
        vpxor   xmm6, xmm1
        vmovdqu [dst + off], xmm2
        vmovdqu [dst + off + 16], xmm6

        vmovdqu xmm0, [src + off + 16*2]
        vmovdqu xmm1, [src + off + 16*3]
        vpxor   xmm10, xmm0
        vpxor   xmm14, xmm1
        vmovdqu [dst + off + 16*2], xmm10
        vmovdqu [dst + off + 16*3], xmm14

        add     off, 64
        sub     len, 64

        ; Check if there are remaining bytes to process
        or      len, len
        jz      no_partial_block

        ; move last 64 bytes of KS to xmm9-12 (used in less_than_64)
        vmovdqa xmm9, xmm3
        vmovdqa xmm10, xmm7
        ; xmm11 is OK
        vmovdqa xmm12, xmm15

        jmp     less_than_64

between_129_191:
        ; move bytes 128-191 of KS to xmm9-12 (used in less_than_64)
        vmovdqa xmm9, xmm2
        vmovdqa xmm11, xmm10
        vmovdqa xmm10, xmm6
        vmovdqa xmm12, xmm14

        jmp     less_than_64

no_partial_block:

%ifdef SAFE_DATA
        clear_all_xmms_avx_asm
        ; Clear stack frame
%assign i 0
%rep 16
        vmovdqa [rsp + _STATE + 16*i], xmm0
%assign i (i + 1)
%endrep
        vmovdqa [rsp + _XMM_SAVE], xmm0
        vmovdqa [rsp + _XMM_SAVE + 16], xmm0
%endif

        mov     rsp, [rsp + _RSP_SAVE]

exit:
        mov     rax, job
        or      dword [rax + _status], IMB_STATUS_COMPLETED_CIPHER

        ret

align 32
MKGLOBAL(chacha20_enc_dec_ks_avx,function,internal)
chacha20_enc_dec_ks_avx:

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

        mov     ctx, arg5

        mov     rax, rsp
        sub     rsp, STACK_SIZE
        and     rsp, -16
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

        ; Check if there is nothing to encrypt
        or      len, len
        jz      exit_ks

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
        ENCRYPT_0B_64B    src, dst, tmp3, off, xmm9, xmm10, xmm11, xmm12, \
                        xmm0, xmm1, xmm2, xmm3, tmp, tmp2, prev_ks

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

        ; If less than or equal to 64*2 bytes, prepare directly states for
        ; up to 2 blocks
        cmp     len, 64*2
        jbe     check_1_or_2_blocks_left_ks

        ; Prepare first 4 chacha states
        vmovdqa  xmm0, [rel constants0]
        vmovdqa  xmm1, [rel constants1]
        vmovdqa  xmm2, [rel constants2]
        vmovdqa  xmm3, [rel constants3]

        ; Broadcast 8 dwords from key into XMM4-11
        vmovdqu  xmm12, [keys]
        vmovdqu  xmm15, [keys + 16]
        vpshufd  xmm4, xmm12, 0x0
        vpshufd  xmm5, xmm12, 0x55
        vpshufd  xmm6, xmm12, 0xAA
        vpshufd  xmm7, xmm12, 0xFF
        vpshufd  xmm8, xmm15, 0x0
        vpshufd  xmm9, xmm15, 0x55
        vpshufd  xmm10, xmm15, 0xAA
        vpshufd  xmm11, xmm15, 0xFF

        ; Broadcast 3 dwords from IV into XMM13-15
        vmovd    xmm13, [iv]
        vmovd    xmm14, [iv + 4]
        vpshufd  xmm13, xmm13, 0
        vpshufd  xmm14, xmm14, 0
        vmovd    xmm15, [iv + 8]
        vpshufd  xmm15, xmm15, 0

        ; Set block counters for next 4 Chacha20 states
        vmovd    xmm12, DWORD(blk_cnt)
        vpshufd  xmm12, xmm12, 0
        vpaddd   xmm12, [rel dword_1_4]

%assign i 0
%rep 16
        vmovdqa  [rsp + _STATE + 16*i], xmm %+ i
%assign i (i + 1)
%endrep

        cmp     len, 64*4
        jb      exit_loop_ks

align 32
start_loop_ks:

        ; Generate 256 bytes of keystream
        GENERATE_256_KS xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, \
                        xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15

        ;; Transpose state to get keystream and XOR with plaintext
        ;; to get ciphertext

        ; Save registers to be used as temp registers
        vmovdqa [rsp + _XMM_SAVE], xmm14
        vmovdqa [rsp + _XMM_SAVE + 16], xmm15

        ; Transpose to get 16-31, 80-95, 144-159, 208-223 bytes of KS
        TRANSPOSE4_U32 xmm0, xmm1, xmm2, xmm3, xmm14, xmm15

        ; xmm14, xmm1, xmm0, xmm3
        ; xmm2, xmm15 free to use
        vmovdqu  xmm2, [src + off]
        vmovdqu  xmm15, [src + off + 16*4]
        vpxor    xmm14, xmm2
        vpxor    xmm1, xmm15
        vmovdqu  [dst + off], xmm14
        vmovdqu  [dst + off + 16*4], xmm1

        vmovdqu  xmm2, [src + off + 16*8]
        vmovdqu  xmm15, [src + off + 16*12]
        vpxor    xmm0, xmm2
        vpxor    xmm3, xmm15
        vmovdqu  [dst + off + 16*8], xmm0
        vmovdqu  [dst + off + 16*12], xmm3

        ; Restore registers and use xmm0, xmm1 now that they are free
        vmovdqa xmm14, [rsp + _XMM_SAVE]
        vmovdqa xmm15, [rsp + _XMM_SAVE + 16]

        ; Transpose to get bytes 64-127 of KS
        TRANSPOSE4_U32 xmm4, xmm5, xmm6, xmm7, xmm0, xmm1

        ; xmm0, xmm5, xmm4, xmm7
        ; xmm6, xmm1 free to use
        vmovdqu  xmm6, [src + off + 16]
        vmovdqu  xmm1, [src + off + 16*5]
        vpxor    xmm0, xmm6
        vpxor    xmm5, xmm1
        vmovdqu  [dst + off + 16], xmm0
        vmovdqu  [dst + off + 16*5], xmm5

        vmovdqu  xmm6, [src + off + 16*9]
        vmovdqu  xmm1, [src + off + 16*13]
        vpxor    xmm4, xmm6
        vpxor    xmm7, xmm1
        vmovdqu  [dst + off + 16*9], xmm4
        vmovdqu  [dst + off + 16*13], xmm7

        ; Transpose to get 32-47, 96-111, 160-175, 224-239 bytes of KS
        TRANSPOSE4_U32 xmm8, xmm9, xmm10, xmm11, xmm0, xmm1

        ; xmm0, xmm9, xmm8, xmm11
        ; xmm10, xmm1 free to use
        vmovdqu  xmm10, [src + off + 16*2]
        vmovdqu  xmm1, [src + off + 16*6]
        vpxor    xmm0, xmm10
        vpxor    xmm9, xmm1
        vmovdqu  [dst + off + 16*2], xmm0
        vmovdqu  [dst + off + 16*6], xmm9

        vmovdqu  xmm10, [src + off + 16*10]
        vmovdqu  xmm1, [src + off + 16*14]
        vpxor    xmm8, xmm10
        vpxor    xmm11, xmm1
        vmovdqu  [dst + off + 16*10], xmm8
        vmovdqu  [dst + off + 16*14], xmm11

        ; Transpose to get 48-63, 112-127, 176-191, 240-255 bytes of KS
        TRANSPOSE4_U32 xmm12, xmm13, xmm14, xmm15, xmm0, xmm1

        ; xmm0, xmm13, xmm12, xmm15
        ; xmm14, xmm1 free to use
        vmovdqu  xmm14, [src + off + 16*3]
        vmovdqu  xmm1, [src + off + 16*7]
        vpxor    xmm0, xmm14
        vpxor    xmm13, xmm1
        vmovdqu  [dst + off + 16*3], xmm0
        vmovdqu  [dst + off + 16*7], xmm13

        vmovdqu  xmm14, [src + off + 16*11]
        vmovdqu  xmm1, [src + off + 16*15]
        vpxor    xmm12, xmm14
        vpxor    xmm15, xmm1
        vmovdqu  [dst + off + 16*11], xmm12
        vmovdqu  [dst + off + 16*15], xmm15
        ; Update remaining length
        sub     len, 64*4
        add     off, 64*4
        add     blk_cnt, 4

        ; Update counter values
        vmovdqa xmm12, [rsp + _STATE + 16*12]
        vpaddd  xmm12, [rel dword_4]
        vmovdqa [rsp + _STATE + 16*12], xmm12

        cmp     len, 64*4
        jae     start_loop_ks

exit_loop_ks:

        ; Check if there are no more bytes to encrypt
        or      len, len
        jz      no_partial_block_ks

        cmp     len, 64*2
        ja      more_than_2_blocks_left_ks

check_1_or_2_blocks_left_ks:
        cmp     len, 64
        ja      two_blocks_left_ks

        ;; 1 block left

        ; Prepare next chacha state from IV, key
        vmovdqu  xmm1, [keys]          ; Load key bytes 0-15
        vmovdqu  xmm2, [keys + 16]     ; Load key bytes 16-31
        ; Read nonce (12 bytes)
        vmovq    xmm3, [iv]
        vpinsrd  xmm3, [iv + 8], 2
        vpslldq  xmm3, 4
        vpinsrd  xmm3, DWORD(blk_cnt), 0
        vmovdqa  xmm0, [rel constants]

        ; Increase block counter
        vpaddd   xmm3, [rel dword_1]

        ; Generate 64 bytes of keystream
        GENERATE_64_128_KS xmm0, xmm1, xmm2, xmm3, xmm9, xmm10, xmm11, \
                           xmm12, xmm13

        cmp     len, 64
        jne     less_than_64_ks

        ;; Exactly 64 bytes left

        ; Load plaintext, XOR with KS and store ciphertext
        vmovdqu  xmm14, [src + off]
        vmovdqu  xmm15, [src + off + 16]
        vpxor    xmm14, xmm9
        vpxor    xmm15, xmm10
        vmovdqu  [dst + off], xmm14
        vmovdqu  [dst + off + 16], xmm15

        vmovdqu  xmm14, [src + off + 16*2]
        vmovdqu  xmm15, [src + off + 16*3]
        vpxor    xmm14, xmm11
        vpxor    xmm15, xmm12
        vmovdqu  [dst + off + 16*2], xmm14
        vmovdqu  [dst + off + 16*3], xmm15

        inc     blk_cnt

        jmp     no_partial_block_ks

less_than_64_ks:

        ; Preserve len
        mov     tmp5, len
        ENCRYPT_0B_64B    src, dst, len, off, xmm9, xmm10, xmm11, xmm12, \
                        xmm0, xmm1, xmm2, xmm3, src, off

        inc     blk_cnt
        ; Save last 64-byte block of keystream,
        ; in case it is needed in next segments
        vmovdqu  [prev_ks], xmm9
        vmovdqu  [prev_ks + 16], xmm10
        vmovdqu  [prev_ks + 32], xmm11
        vmovdqu  [prev_ks + 48], xmm12

        ; Update remain number of KS bytes
        mov     tmp, 64
        sub     tmp, tmp5
        mov     [ctx + RemainKsBytes], tmp
        jmp     no_partial_block_ks

two_blocks_left_ks:

        ; Prepare next 2 chacha states from IV, key
        vmovdqu  xmm1, [keys]          ; Load key bytes 0-15
        vmovdqu  xmm2, [keys + 16]     ; Load key bytes 16-31
        ; Read nonce (12 bytes)
        vmovq    xmm3, [iv]
        vpinsrd  xmm3, [iv + 8], 2
        vpslldq  xmm3, 4
        vpinsrd  xmm3, DWORD(blk_cnt), 0
        vmovdqa  xmm0, [rel constants]

        vmovdqa  xmm8, xmm3

        ; Increase block counters
        vpaddd   xmm3, [rel dword_1]
        vpaddd   xmm8, [rel dword_2]

        ; Generate 128 bytes of keystream
        GENERATE_64_128_KS xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, \
                           xmm13, xmm8, xmm9, xmm10, xmm11, xmm12

        cmp     len, 128
        jb      between_64_127_ks

        ; Load plaintext, XOR with KS and store ciphertext
        vmovdqu  xmm14, [src + off]
        vmovdqu  xmm15, [src + off + 16]
        vpxor    xmm14, xmm4
        vpxor    xmm15, xmm5
        vmovdqu  [dst + off], xmm14
        vmovdqu  [dst + off + 16], xmm15

        vmovdqu  xmm14, [src + off + 16*2]
        vmovdqu  xmm15, [src + off + 16*3]
        vpxor    xmm14, xmm6
        vpxor    xmm15, xmm7
        vmovdqu  [dst + off + 16*2], xmm14
        vmovdqu  [dst + off + 16*3], xmm15

        vmovdqu  xmm14, [src + off + 16*4]
        vmovdqu  xmm15, [src + off + 16*5]
        vpxor    xmm14, xmm9
        vpxor    xmm15, xmm10
        vmovdqu  [dst + off + 16*4], xmm14
        vmovdqu  [dst + off + 16*5], xmm15

        vmovdqu  xmm14, [src + off + 16*6]
        vmovdqu  xmm15, [src + off + 16*7]
        vpxor    xmm14, xmm11
        vpxor    xmm15, xmm12
        vmovdqu  [dst + off + 16*6], xmm14
        vmovdqu  [dst + off + 16*7], xmm15

        add     blk_cnt, 2

        jmp     no_partial_block_ks

between_64_127_ks:
        ; Load plaintext, XOR with KS and store ciphertext for first 64 bytes
        vmovdqu  xmm14, [src + off]
        vmovdqu  xmm15, [src + off + 16]
        vpxor    xmm14, xmm4
        vpxor    xmm15, xmm5
        vmovdqu  [dst + off], xmm14
        vmovdqu  [dst + off + 16], xmm15

        vmovdqu  xmm14, [src + off + 16*2]
        vmovdqu  xmm15, [src + off + 16*3]
        vpxor    xmm14, xmm6
        vpxor    xmm15, xmm7
        vmovdqu  [dst + off + 16*2], xmm14
        vmovdqu  [dst + off + 16*3], xmm15

        sub     len, 64
        add     off, 64

        add     blk_cnt, 1

        ; Handle rest up to 63 bytes in "less_than_64"
        jmp     less_than_64_ks

more_than_2_blocks_left_ks:

        ; Generate 256 bytes of keystream
        GENERATE_256_KS xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, \
                        xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15

        ;; Transpose state to get keystream and XOR with plaintext
        ;; to get ciphertext

        ; Save registers to be used as temp registers
        vmovdqa  [rsp + _XMM_SAVE], xmm14
        vmovdqa  [rsp + _XMM_SAVE + 16], xmm15

        ; Transpose to get 0-15, 64-79, 128-143, 192-207 bytes of KS
        TRANSPOSE4_U32 xmm0, xmm1, xmm2, xmm3, xmm14, xmm15

        ; xmm14, xmm1, xmm0, xmm3
        vmovdqa  xmm2, xmm0
        vmovdqa  xmm0, xmm14
        ; xmm0-3 containing [64*I : 64*I + 15] (I = 0-3) bytes of KS

        ; Restore registers and save xmm0,xmm1 and use them instead
        vmovdqa  xmm14, [rsp + _XMM_SAVE]
        vmovdqa  xmm15, [rsp + _XMM_SAVE + 16]

        vmovdqa  [rsp + _XMM_SAVE], xmm2
        vmovdqa  [rsp + _XMM_SAVE + 16], xmm3

        ; Transpose to get 16-31, 80-95, 144-159, 208-223 bytes of KS
        TRANSPOSE4_U32 xmm4, xmm5, xmm6, xmm7, xmm2, xmm3

        ; xmm2, xmm5, xmm4, xmm7
        vmovdqa  xmm6, xmm4
        vmovdqa  xmm4, xmm2
        ; xmm4-7 containing [64*I + 16 : 64*I + 31] (I = 0-3) bytes of KS

        ; Transpose to get 32-47, 96-111, 160-175, 224-239 bytes of KS
        TRANSPOSE4_U32 xmm8, xmm9, xmm10, xmm11, xmm2, xmm3

        ; xmm2, xmm9, xmm8, xmm11
        vmovdqa  xmm10, xmm8
        vmovdqa  xmm8, xmm2
        ; xmm8-11 containing [64*I + 32 : 64*I + 47] (I = 0-3) bytes of KS

        ; Transpose to get 48-63, 112-127, 176-191, 240-255 bytes of KS
        TRANSPOSE4_U32 xmm12, xmm13, xmm14, xmm15, xmm2, xmm3

        ; xmm2, xmm13, xmm12, xmm15
        vmovdqa  xmm14, xmm12
        vmovdqa  xmm12, xmm2
        ; xmm12-15 containing [64*I + 48 : 64*I + 63] (I = 0-3) bytes of KS

        ; Encrypt first 128 bytes of plaintext (there are at least two 64 byte blocks to process)
        vmovdqu  xmm2, [src + off]
        vmovdqu  xmm3, [src + off + 16]
        vpxor    xmm0, xmm2
        vpxor    xmm4, xmm3
        vmovdqu  [dst + off], xmm0
        vmovdqu  [dst + off + 16], xmm4

        vmovdqu  xmm2, [src + off + 16*2]
        vmovdqu  xmm3, [src + off + 16*3]
        vpxor    xmm8, xmm2
        vpxor    xmm12, xmm3
        vmovdqu  [dst + off + 16*2], xmm8
        vmovdqu  [dst + off + 16*3], xmm12

        vmovdqu  xmm2, [src + off + 16*4]
        vmovdqu  xmm3, [src + off + 16*5]
        vpxor    xmm1, xmm2
        vpxor    xmm5, xmm3
        vmovdqu  [dst + off + 16*4], xmm1
        vmovdqu  [dst + off + 16*5], xmm5

        vmovdqu  xmm2, [src + off + 16*6]
        vmovdqu  xmm3, [src + off + 16*7]
        vpxor    xmm9, xmm2
        vpxor    xmm13, xmm3
        vmovdqu  [dst + off + 16*6], xmm9
        vmovdqu  [dst + off + 16*7], xmm13

        ; Restore xmm2,xmm3
        vmovdqa  xmm2, [rsp + _XMM_SAVE]
        vmovdqa  xmm3, [rsp + _XMM_SAVE + 16]

        sub     len, 128
        add     off, 128
        add     blk_cnt, 2
        ; Use now xmm0,xmm1 as scratch registers

        ; Check if there is at least 64 bytes more to process
        cmp     len, 64
        jb      between_129_191_ks

        ; Encrypt next 64 bytes (128-191)
        vmovdqu  xmm0, [src + off]
        vmovdqu  xmm1, [src + off + 16]
        vpxor    xmm2, xmm0
        vpxor    xmm6, xmm1
        vmovdqu  [dst + off], xmm2
        vmovdqu  [dst + off + 16], xmm6

        vmovdqu  xmm0, [src + off + 16*2]
        vmovdqu  xmm1, [src + off + 16*3]
        vpxor    xmm10, xmm0
        vpxor    xmm14, xmm1
        vmovdqu  [dst + off + 16*2], xmm10
        vmovdqu  [dst + off + 16*3], xmm14

        add     off, 64
        sub     len, 64
        add     blk_cnt, 1

        ; Check if there are remaining bytes to process
        or      len, len
        jz      no_partial_block_ks

        ; move last 64 bytes of KS to xmm9-12 (used in less_than_64)
        vmovdqa  xmm9, xmm3
        vmovdqa  xmm10, xmm7
        ; xmm11 is OK
        vmovdqa  xmm12, xmm15

        jmp     less_than_64_ks

between_129_191_ks:
        ; move bytes 128-191 of KS to xmm9-12 (used in less_than_64)
        vmovdqa  xmm9, xmm2
        vmovdqa  xmm11, xmm10
        vmovdqa  xmm10, xmm6
        vmovdqa  xmm12, xmm14

        jmp     less_than_64_ks

no_partial_block_ks:

        mov     [ctx + LastBlkCount], blk_cnt

%ifdef SAFE_DATA
        clear_all_xmms_avx_asm
        ; Clear stack frame
%assign i 0
%rep 16
        vmovdqa  [rsp + _STATE + 16*i], xmm0
%assign i (i + 1)
%endrep
        vmovdqa  [rsp + _XMM_SAVE], xmm0
        vmovdqa  [rsp + _XMM_SAVE + 16], xmm0
%endif

exit_ks:
        mov     r12, [rsp + _GP_SAVE]
        mov     r13, [rsp + _GP_SAVE + 8]
        mov     r14, [rsp + _GP_SAVE + 16]
        mov     r15, [rsp + _GP_SAVE + 24]
        mov     rbx, [rsp + _GP_SAVE + 32]
        mov     rbp, [rsp + _GP_SAVE + 40]
%ifndef LINUX
        mov     rdi, [rsp + _GP_SAVE + 48]
%endif
        mov     rsp, [rsp + _RSP_SAVE]; restore RSP

        ret
;;
;; void poly1305_key_gen_avx(const void *key, const void *iv, void *poly_key)
align 32
MKGLOBAL(poly1305_key_gen_avx,function,internal)
poly1305_key_gen_avx:
        ;; prepare chacha state from IV, key
        vmovdqa xmm0, [rel constants]
        vmovdqu xmm1, [arg1]          ; Load key bytes 0-15
        vmovdqu xmm2, [arg1 + 16]     ; Load key bytes 16-31
        ;;  copy nonce (12 bytes)
        vmovq   xmm3, [arg2]
        vpinsrd xmm3, [arg2 + 8], 2
        vpslldq xmm3, 4

        ;; run one round of chacha20
        GENERATE_64_128_KS xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8

        ;; clamp R and store poly1305 key
        ;; R = KEY[0..15] & 0xffffffc0ffffffc0ffffffc0fffffff
        vpand   xmm4, [rel poly_clamp_r]
        vmovdqu [arg3 + 0 * 16], xmm4
        vmovdqu [arg3 + 1 * 16], xmm5

%ifdef SAFE_DATA
        clear_all_xmms_avx_asm
%endif
        ret

mksection stack-noexec

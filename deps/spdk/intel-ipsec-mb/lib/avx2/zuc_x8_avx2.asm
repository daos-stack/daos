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
%include "include/reg_sizes.asm"
%include "include/zuc_sbox.inc"
%include "include/transpose_avx2.asm"
%include "include/memcpy.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/cet.inc"
%define APPEND(a,b) a %+ b

mksection .rodata
default rel

align 32
Ek_d:
dd	0x0044D700, 0x0026BC00, 0x00626B00, 0x00135E00, 0x00578900, 0x0035E200, 0x00713500, 0x0009AF00
dd	0x004D7800, 0x002F1300, 0x006BC400, 0x001AF100, 0x005E2600, 0x003C4D00, 0x00789A00, 0x0047AC00

align 16
EK256_d64:
dd      0x00220000, 0x002F0000, 0x00240000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000

align 16
EK256_EIA3_4:
dd      0x00220000, 0x002F0000, 0x00250000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000

align 16
EK256_EIA3_8:
dd      0x00230000, 0x002F0000, 0x00240000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000

align 16
EK256_EIA3_16:
dd      0x00230000, 0x002F0000, 0x00250000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000

align 32
shuf_mask_key:
dd      0x00FFFFFF, 0x01FFFFFF, 0x02FFFFFF, 0x03FFFFFF, 0x04FFFFFF, 0x05FFFFFF, 0x06FFFFFF, 0x07FFFFFF,
dd      0x08FFFFFF, 0x09FFFFFF, 0x0AFFFFFF, 0x0BFFFFFF, 0x0CFFFFFF, 0x0DFFFFFF, 0x0EFFFFFF, 0x0FFFFFFF,

align 32
shuf_mask_iv:
dd      0xFFFFFF00, 0xFFFFFF01, 0xFFFFFF02, 0xFFFFFF03, 0xFFFFFF04, 0xFFFFFF05, 0xFFFFFF06, 0xFFFFFF07,
dd      0xFFFFFF08, 0xFFFFFF09, 0xFFFFFF0A, 0xFFFFFF0B, 0xFFFFFF0C, 0xFFFFFF0D, 0xFFFFFF0E, 0xFFFFFF0F,

align 16
shuf_mask_iv_17_19:
db      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x01, 0xFF, 0xFF, 0xFF, 0x02, 0xFF

align 16
clear_iv_mask:
db      0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x3F, 0x00

align 16
shuf_mask_iv_20_23:
db      0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x01, 0xFF, 0xFF, 0xFF, 0x02, 0xFF, 0xFF, 0xFF, 0x03, 0xFF

align 32
mask31:
dd	0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
dd	0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,

align 32
swap_mask:
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c

align 32
S1_S0_shuf:
db      0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F
db      0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F

align 32
S0_S1_shuf:
db      0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
db      0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,

align 32
rev_S1_S0_shuf:
db      0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F
db      0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F

align 32
rev_S0_S1_shuf:
db      0x08, 0x00, 0x09, 0x01, 0x0A, 0x02, 0x0B, 0x03, 0x0C, 0x04, 0x0D, 0x05, 0x0E, 0x06, 0x0F, 0x07
db      0x08, 0x00, 0x09, 0x01, 0x0A, 0x02, 0x0B, 0x03, 0x0C, 0x04, 0x0D, 0x05, 0x0E, 0x06, 0x0F, 0x07

align 32
rot8_mod32:
db      0x03, 0x00, 0x01, 0x02, 0x07, 0x04, 0x05, 0x06,
db      0x0B, 0x08, 0x09, 0x0A, 0x0F, 0x0C, 0x0D, 0x0E
db      0x03, 0x00, 0x01, 0x02, 0x07, 0x04, 0x05, 0x06,
db      0x0B, 0x08, 0x09, 0x0A, 0x0F, 0x0C, 0x0D, 0x0E

align 32
rot16_mod32:
db      0x02, 0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05,
db      0x0A, 0x0B, 0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D
db      0x02, 0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05,
db      0x0A, 0x0B, 0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D

align 32
rot24_mod32:
db      0x01, 0x02, 0x03, 0x00, 0x05, 0x06, 0x07, 0x04,
db      0x09, 0x0A, 0x0B, 0x08, 0x0D, 0x0E, 0x0F, 0x0C
db      0x01, 0x02, 0x03, 0x00, 0x05, 0x06, 0x07, 0x04,
db      0x09, 0x0A, 0x0B, 0x08, 0x0D, 0x0E, 0x0F, 0x0C

align 16
broadcast_word:
db      0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01
db      0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01

align 16
all_ffs:
dw      0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff

align 16
all_threes:
dw      0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003

align 16
all_fffcs:
dw      0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc

align 16
all_1fs:
dw      0x001f, 0x001f, 0x001f, 0x001f, 0x001f, 0x001f, 0x001f, 0x001f

align 16
all_20s:
dw      0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020

mksection .text
align 64

%define MASK31  ymm12

%define OFS_R1  (16*(2*16))
%define OFS_R2  (OFS_R1 + (2*16))
%define OFS_X0  (OFS_R2 + (2*16))
%define OFS_X1  (OFS_X0 + (2*16))
%define OFS_X2  (OFS_X1 + (2*16))

%ifidn __OUTPUT_FORMAT__, win64
        %define XMM_STORAGE     16*10
        %define GP_STORAGE      8*8
%else
        %define XMM_STORAGE     0
        %define GP_STORAGE      6*8
%endif

%define VARIABLE_OFFSET XMM_STORAGE + GP_STORAGE
%define GP_OFFSET XMM_STORAGE

%macro FUNC_SAVE 0
        mov     r11, rsp
        sub     rsp, VARIABLE_OFFSET
        and     rsp, ~15

%ifidn __OUTPUT_FORMAT__, win64
        ; xmm6:xmm15 need to be maintained for Windows
        vmovdqa [rsp + 0*16], xmm6
        vmovdqa [rsp + 1*16], xmm7
        vmovdqa [rsp + 2*16], xmm8
        vmovdqa [rsp + 3*16], xmm9
        vmovdqa [rsp + 4*16], xmm10
        vmovdqa [rsp + 5*16], xmm11
        vmovdqa [rsp + 6*16], xmm12
        vmovdqa [rsp + 7*16], xmm13
        vmovdqa [rsp + 8*16], xmm14
        vmovdqa [rsp + 9*16], xmm15
        mov     [rsp + GP_OFFSET + 48], rdi
        mov     [rsp + GP_OFFSET + 56], rsi
%endif
        mov     [rsp + GP_OFFSET],      r12
        mov     [rsp + GP_OFFSET + 8],  r13
        mov     [rsp + GP_OFFSET + 16], r14
        mov     [rsp + GP_OFFSET + 24], r15
        mov     [rsp + GP_OFFSET + 32], rbx
        mov     [rsp + GP_OFFSET + 40], r11 ;; rsp pointer
%endmacro

%macro FUNC_RESTORE 0

%ifidn __OUTPUT_FORMAT__, win64
        vmovdqa xmm6,  [rsp + 0*16]
        vmovdqa xmm7,  [rsp + 1*16]
        vmovdqa xmm8,  [rsp + 2*16]
        vmovdqa xmm9,  [rsp + 3*16]
        vmovdqa xmm10, [rsp + 4*16]
        vmovdqa xmm11, [rsp + 5*16]
        vmovdqa xmm12, [rsp + 6*16]
        vmovdqa xmm13, [rsp + 7*16]
        vmovdqa xmm14, [rsp + 8*16]
        vmovdqa xmm15, [rsp + 9*16]
        mov     rdi, [rsp + GP_OFFSET + 48]
        mov     rsi, [rsp + GP_OFFSET + 56]
%endif
        mov     r12, [rsp + GP_OFFSET]
        mov     r13, [rsp + GP_OFFSET + 8]
        mov     r14, [rsp + GP_OFFSET + 16]
        mov     r15, [rsp + GP_OFFSET + 24]
        mov     rbx, [rsp + GP_OFFSET + 32]
        mov     rsp, [rsp + GP_OFFSET + 40]
%endmacro

; This macro reorder the LFSR registers
; after N rounds (1 <= N <= 15), since the registers
; are shifted every round
;
; The macro clobbers YMM0-15
;
%macro REORDER_LFSR 2
%define %%STATE      %1
%define %%NUM_ROUNDS %2

%if %%NUM_ROUNDS != 16
%assign i 0
%rep 16
    vmovdqa APPEND(ymm,i), [%%STATE + 32*i]
%assign i (i+1)
%endrep

%assign i 0
%assign j %%NUM_ROUNDS
%rep 16
    vmovdqa [%%STATE + 32*i], APPEND(ymm,j)
%assign i (i+1)
%assign j ((j+1) % 16)
%endrep
%endif ;; %%NUM_ROUNDS != 16

%endmacro

;;
;;   make_u31()
;;
%macro  make_u31    4

%define %%Rt        %1
%define %%Ke        %2
%define %%Ek        %3
%define %%Iv        %4
    xor         %%Rt, %%Rt
    shrd        %%Rt, %%Iv, 8
    shrd        %%Rt, %%Ek, 15
    shrd        %%Rt, %%Ke, 9
%endmacro

;
;   bits_reorg8()
;
%macro  bits_reorg8 2-3
%define %%STATE     %1 ; [in] ZUC state
%define %%ROUND_NUM %2 ; [in] Round number
%define %%X3        %3 ; [out] YMM register containing X3 of all lanes
    ;
    ; ymm15 = LFSR_S15
    ; ymm14 = LFSR_S14
    ; ymm11 = LFSR_S11
    ; ymm9  = LFSR_S9
    ; ymm7  = LFSR_S7
    ; ymm5  = LFSR_S5
    ; ymm2  = LFSR_S2
    ; ymm0  = LFSR_S0
    ;
    vmovdqa     ymm15, [%%STATE + ((15 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm14, [%%STATE + ((14 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm11, [%%STATE + ((11 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm9,  [%%STATE + (( 9 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm7,  [%%STATE + (( 7 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm5,  [%%STATE + (( 5 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm2,  [%%STATE + (( 2 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm0,  [%%STATE + (( 0 + %%ROUND_NUM) % 16)*32]

    vpxor       ymm1, ymm1
    vpslld      ymm15, 1
    vpblendw    ymm3,  ymm14, ymm1, 0xAA
    vpblendw    ymm15, ymm3, ymm15, 0xAA

    vmovdqa     [%%STATE + OFS_X0], ymm15   ; BRC_X0
    vpslld      ymm11, 16
    vpsrld      ymm9, 15
    vpor        ymm11, ymm9
    vmovdqa     [%%STATE + OFS_X1], ymm11   ; BRC_X1
    vpslld      ymm7, 16
    vpsrld      ymm5, 15
    vpor        ymm7, ymm5
    vmovdqa     [%%STATE + OFS_X2], ymm7    ; BRC_X2
%if (%0 == 3)
    vpslld      ymm2, 16
    vpsrld      ymm0, 15
    vpor        %%X3, ymm2, ymm0 ; Store BRC_X3 in YMM register
%endif
%endmacro

;
;   rot_mod32()
;
;   uses ymm7
;
%macro  rot_mod32   3
%if (%3 == 8)
    vpshufb %1, %2, [rel rot8_mod32]
%elif (%3 == 16)
    vpshufb %1, %2, [rel rot16_mod32]
%elif (%3 == 24)
    vpshufb %1, %2, [rel rot24_mod32]
%else
    vpslld      %1, %2, %3
    vpsrld      ymm7, %2, (32 - %3)

    vpor        %1, ymm7
%endif
%endmacro

;
;   nonlin_fun8()
;
;   return
;       W value, updates F_R1[] / F_R2[]
;
%macro nonlin_fun8  1-2
%define %%STATE     %1  ; [in] ZUC state
%define %%W         %2  ; [out] YMM register to contain W for all lanes

%if (%0 == 2)
    vmovdqa     %%W, [%%STATE + OFS_X0]
    vpxor       %%W, [%%STATE + OFS_R1]
    vpaddd      %%W, [%%STATE + OFS_R2]    ; W = (BRC_X0 ^ F_R1) + F_R2
%endif

    vmovdqa     ymm1, [%%STATE + OFS_R1]
    vmovdqa     ymm2, [%%STATE + OFS_R2]
    vpaddd      ymm1, [%%STATE + OFS_X1]    ; W1 = F_R1 + BRC_X1
    vpxor       ymm2, [%%STATE + OFS_X2]    ; W2 = F_R2 ^ BRC_X2

    vpslld      ymm3, ymm1, 16
    vpsrld      ymm4, ymm1, 16
    vpslld      ymm5, ymm2, 16
    vpsrld      ymm6, ymm2, 16
    vpor        ymm1, ymm3, ymm6
    vpor        ymm2, ymm4, ymm5

    rot_mod32   ymm3, ymm1, 2
    rot_mod32   ymm4, ymm1, 10
    rot_mod32   ymm5, ymm1, 18
    rot_mod32   ymm6, ymm1, 24
    vpxor       ymm1, ymm3
    vpxor       ymm1, ymm4
    vpxor       ymm1, ymm5
    vpxor       ymm1, ymm6      ; XMM1 = U = L1(P)

    rot_mod32   ymm3, ymm2, 8
    rot_mod32   ymm4, ymm2, 14
    rot_mod32   ymm5, ymm2, 22
    rot_mod32   ymm6, ymm2, 30
    vpxor       ymm2, ymm3
    vpxor       ymm2, ymm4
    vpxor       ymm2, ymm5
    vpxor       ymm2, ymm6      ; XMM2 = V = L2(Q)

    ; Shuffle U and V to have all S0 lookups in XMM1 and all S1 lookups in XMM2

    ; Compress all S0 and S1 input values in each register
    vpshufb     ymm1, [rel S0_S1_shuf] ; S0: Bytes 0-7,16-23 S1: Bytes 8-15,24-31
    vpshufb     ymm2, [rel S1_S0_shuf] ; S1: Bytes 0-7,16-23 S0: Bytes 8-15,24-31

    vshufpd     ymm3, ymm1, ymm2, 0xA ; All S0 input values
    vshufpd     ymm4, ymm2, ymm1, 0xA ; All S1 input values

    ; Compute S0 and S1 values
    S0_comput_AVX2  ymm3, ymm1, ymm2
    S1_comput_AVX2  ymm4, ymm1, ymm2, ymm5

    ; Need to shuffle back ymm1 & ymm2 before storing output
    ; (revert what was done before S0 and S1 computations)
    vshufpd    ymm1, ymm3, ymm4, 0xA
    vshufpd    ymm2, ymm4, ymm3, 0xA

    vpshufb     ymm1, [rel rev_S0_S1_shuf]
    vpshufb     ymm2, [rel rev_S1_S0_shuf]

    vmovdqa     [%%STATE + OFS_R1], ymm1
    vmovdqa     [%%STATE + OFS_R2], ymm2
%endmacro

;
;   store32B_kstr8()
;
%macro  store32B_kstr8 8
%define %%DATA32B_L0  %1  ; [in] 32 bytes of keystream for lane 0
%define %%DATA32B_L1  %2  ; [in] 32 bytes of keystream for lane 1
%define %%DATA32B_L2  %3  ; [in] 32 bytes of keystream for lane 2
%define %%DATA32B_L3  %4  ; [in] 32 bytes of keystream for lane 3
%define %%DATA32B_L4  %5  ; [in] 32 bytes of keystream for lane 4
%define %%DATA32B_L5  %6  ; [in] 32 bytes of keystream for lane 5
%define %%DATA32B_L6  %7  ; [in] 32 bytes of keystream for lane 6
%define %%DATA32B_L7  %8  ; [in] 32 bytes of keystream for lane 7

    mov         rcx, [rsp]
    mov         rdx, [rsp + 8]
    mov         r8,  [rsp + 16]
    mov         r9,  [rsp + 24]
    vmovdqu     [rcx], %%DATA32B_L0
    vmovdqu     [rdx], %%DATA32B_L1
    vmovdqu     [r8],  %%DATA32B_L2
    vmovdqu     [r9],  %%DATA32B_L3

    mov         rcx, [rsp + 32]
    mov         rdx, [rsp + 40]
    mov         r8,  [rsp + 48]
    mov         r9,  [rsp + 56]
    vmovdqu     [rcx], %%DATA32B_L4
    vmovdqu     [rdx], %%DATA32B_L5
    vmovdqu     [r8],  %%DATA32B_L6
    vmovdqu     [r9],  %%DATA32B_L7

%endmacro

;
;   store4B_kstr8()
;
;   params
;
;   %1 - YMM register with OFS_X3
;   return
;
%macro  store4B_kstr8 1
    mov         rcx, [rsp]
    mov         rdx, [rsp + 8]
    mov         r8,  [rsp + 16]
    mov         r9,  [rsp + 24]
    vpextrd     [r9],  XWORD(%1), 3
    vpextrd     [r8],  XWORD(%1), 2
    vpextrd     [rdx], XWORD(%1), 1
    vmovd       [rcx], XWORD(%1)
    add         rcx, 4
    add         rdx, 4
    add         r8, 4
    add         r9, 4
    mov         [rsp],      rcx
    mov         [rsp + 8],  rdx
    mov         [rsp + 16], r8
    mov         [rsp + 24], r9

    vextracti128 XWORD(%1), %1, 1
    mov         rcx, [rsp + 32]
    mov         rdx, [rsp + 40]
    mov         r8,  [rsp + 48]
    mov         r9,  [rsp + 56]
    vpextrd     [r9],  XWORD(%1), 3
    vpextrd     [r8],  XWORD(%1), 2
    vpextrd     [rdx], XWORD(%1), 1
    vmovd       [rcx], XWORD(%1)
    add         rcx, 4
    add         rdx, 4
    add         r8, 4
    add         r9, 4
    mov         [rsp + 32], rcx
    mov         [rsp + 40], rdx
    mov         [rsp + 48], r8
    mov         [rsp + 56], r9

%endmacro

;
;   add_mod31()
;       add two 32-bit args and reduce mod (2^31-1)
;   params
;       %1  - arg1/res
;       %2  - arg2
;   uses
;       ymm2
;   return
;       %1
%macro  add_mod31   2
    vpaddd      %1, %2
    vpsrld      ymm2, %1, 31
    vpand       %1, MASK31
    vpaddd      %1, ymm2
%endmacro

;
;   rot_mod31()
;       rotate (mult by pow of 2) 32-bit arg and reduce mod (2^31-1)
;   params
;       %1  - arg
;       %2  - # of bits
;   uses
;       ymm2
;   return
;       %1
%macro  rot_mod31   2

    vpslld      ymm2, %1, %2
    vpsrld      %1, %1, (31 - %2)

    vpor        %1, ymm2
    vpand       %1, MASK31
%endmacro

;
;   lfsr_updt8()
;
;
%macro  lfsr_updt8  3
%define %%STATE     %1 ; [in] ZUC state
%define %%ROUND_NUM %2 ; [in] Round number
%define %%W         %3 ; [in/clobbered] YMM register to contain W for all lanes
    ;
    ; ymm1  = LFSR_S0
    ; ymm4  = LFSR_S4
    ; ymm10 = LFSR_S10
    ; ymm13 = LFSR_S13
    ; ymm15 = LFSR_S15
    ;
    vmovdqa     ymm1,  [%%STATE + (( 0 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm4,  [%%STATE + (( 4 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm10, [%%STATE + ((10 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm13, [%%STATE + ((13 + %%ROUND_NUM) % 16)*32]
    vmovdqa     ymm15, [%%STATE + ((15 + %%ROUND_NUM) % 16)*32]

    ; Calculate LFSR feedback
    add_mod31   %%W, ymm1
    rot_mod31   ymm1, 8
    add_mod31   %%W, ymm1
    rot_mod31   ymm4, 20
    add_mod31   %%W, ymm4
    rot_mod31   ymm10, 21
    add_mod31   %%W, ymm10
    rot_mod31   ymm13, 17
    add_mod31   %%W, ymm13
    rot_mod31   ymm15, 15
    add_mod31   %%W, ymm15

    vmovdqa     [%%STATE + (( 0 + %%ROUND_NUM) % 16)*32], %%W

    ; LFSR_S16 = (LFSR_S15++) = eax
%endmacro

;
; Initialize LFSR registers for a single lane, for ZUC-128
;
; This macro initializes 8 LFSR registers at time.
; so it needs to be called twice.
;
; From spec, s_i (LFSR) registers need to be loaded as follows:
;
; For 0 <= i <= 15, let s_i= k_i || d_i || iv_i.
; Where k_i is each byte of the key, d_i is a 15-bit constant
; and iv_i is each byte of the IV.
;
%macro INIT_LFSR_128 7
%define %%KEY       %1 ;; [in] Key pointer
%define %%IV        %2 ;; [in] IV pointer
%define %%SHUF_KEY  %3 ;; [in] Shuffle key mask
%define %%SHUF_IV   %4 ;; [in] Shuffle key mask
%define %%EKD_MASK  %5 ;; [in] Shuffle key mask
%define %%LFSR      %6 ;; [out] YMM register to contain initialized LFSR regs
%define %%YTMP      %7 ;; [clobbered] YMM temporary register

    vbroadcastf128  %%LFSR, [%%KEY]
    vbroadcastf128  %%YTMP, [%%IV]
    vpshufb         %%LFSR, %%SHUF_KEY
    vpsrld          %%LFSR, 1
    vpshufb         %%YTMP, %%SHUF_IV
    vpor            %%LFSR, %%YTMP
    vpor            %%LFSR, %%EKD_MASK

%endmacro

;
; Initialize LFSR registers for a single lane, for ZUC-256
;
%macro INIT_LFSR_256 8
%define %%KEY       %1 ;; [in] Key pointer
%define %%IV        %2 ;; [in] IV pointer
%define %%LFSR0_7   %3 ;; [out] YMM register to contain initialized LFSR regs 0-7
%define %%LFSR8_15  %4 ;; [out] YMM register to contain initialized LFSR regs 8-15
%define %%XTMP      %5 ;; [clobbered] XMM temporary register
%define %%XTMP2     %6 ;; [clobbered] XMM temporary register
%define %%TMP       %7 ;; [clobbered] GP temporary register
%define %%CONSTANTS %8 ;; [in] Address to constants

    ; s0 - s7
    vpxor          %%LFSR0_7, %%LFSR0_7
    vpinsrb        XWORD(%%LFSR0_7), [%%KEY], 3      ; s0
    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 1], 7  ; s1
    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 2], 11 ; s2
    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 3], 15 ; s3

    vpsrld         XWORD(%%LFSR0_7), 1

    vpor           XWORD(%%LFSR0_7), [%%CONSTANTS] ; s0 - s3

    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 21], 1 ; s0
    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 16], 0 ; s0

    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 22], 5 ; s1
    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 17], 4 ; s1

    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 23], 9 ; s2
    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 18], 8 ; s2

    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 24], 13 ; s3
    vpinsrb        XWORD(%%LFSR0_7), [%%KEY + 19], 12 ; s3

    vpxor          %%XTMP, %%XTMP
    vpinsrb        %%XTMP, [%%KEY + 4], 3   ; s4
    vpinsrb        %%XTMP, [%%IV], 7        ; s5
    vpinsrb        %%XTMP, [%%IV + 1], 11   ; s6
    vpinsrb        %%XTMP, [%%IV + 10], 15  ; s7

    vpsrld         %%XTMP, 1

    vpinsrb        %%XTMP, [%%KEY + 25], 1 ; s4
    vpinsrb        %%XTMP, [%%KEY + 20], 0 ; s4

    vpinsrb        %%XTMP, [%%KEY + 5], 5 ; s5
    vpinsrb        %%XTMP, [%%KEY + 26], 4 ; s5

    vpinsrb        %%XTMP, [%%KEY + 6], 9 ; s6
    vpinsrb        %%XTMP, [%%KEY + 27], 8 ; s6

    vpinsrb        %%XTMP, [%%KEY + 7], 13 ; s7
    vpinsrb        %%XTMP, [%%IV + 2], 12 ; s7

    vpor           %%XTMP, [%%CONSTANTS + 16] ; s4 - s7

    vmovd          %%XTMP2, [%%IV + 17]
    vpshufb        %%XTMP2, [rel shuf_mask_iv_17_19]
    vpand          %%XTMP2, [rel clear_iv_mask]

    vpor           %%XTMP, %%XTMP2

    vinserti128    %%LFSR0_7, %%XTMP, 1

    ; s8 - s15
    vpxor          %%LFSR8_15, %%LFSR8_15
    vpinsrb        XWORD(%%LFSR8_15), [%%KEY + 8], 3   ; s8
    vpinsrb        XWORD(%%LFSR8_15), [%%KEY + 9], 7   ; s9
    vpinsrb        XWORD(%%LFSR8_15), [%%IV + 5], 11   ; s10
    vpinsrb        XWORD(%%LFSR8_15), [%%KEY + 11], 15 ; s11

    vpsrld         XWORD(%%LFSR8_15), 1

    vpinsrb        XWORD(%%LFSR8_15), [%%IV + 3], 1 ; s8
    vpinsrb        XWORD(%%LFSR8_15), [%%IV + 11], 0 ; s8

    vpinsrb        XWORD(%%LFSR8_15), [%%IV + 12], 5 ; s9
    vpinsrb        XWORD(%%LFSR8_15), [%%IV + 4], 4 ; s9

    vpinsrb        XWORD(%%LFSR8_15), [%%KEY + 10], 9 ; s10
    vpinsrb        XWORD(%%LFSR8_15), [%%KEY + 28], 8 ; s10

    vpinsrb        XWORD(%%LFSR8_15), [%%IV + 6], 13 ; s11
    vpinsrb        XWORD(%%LFSR8_15), [%%IV + 13], 12 ; s11

    vpor           XWORD(%%LFSR8_15), [%%CONSTANTS + 32] ; s8 - s11

    vmovd          %%XTMP, [%%IV + 20]
    vpshufb        %%XTMP, [rel shuf_mask_iv_20_23]
    vpand          %%XTMP, [rel clear_iv_mask]

    vpor           XWORD(%%LFSR8_15), %%XTMP

    vpxor          %%XTMP, %%XTMP
    vpinsrb        %%XTMP, [%%KEY + 12], 3   ; s12
    vpinsrb        %%XTMP, [%%KEY + 13], 7   ; s13
    vpinsrb        %%XTMP, [%%KEY + 14], 11  ; s14
    vpinsrb        %%XTMP, [%%KEY + 15], 15  ; s15

    vpsrld         %%XTMP, 1

    vpinsrb        %%XTMP, [%%IV + 7], 1 ; s12
    vpinsrb        %%XTMP, [%%IV + 14], 0 ; s12

    vpinsrb        %%XTMP, [%%IV + 15], 5 ; s13
    vpinsrb        %%XTMP, [%%IV + 8], 4 ; s13

    vpinsrb        %%XTMP, [%%IV + 16], 9 ; s14
    vpinsrb        %%XTMP, [%%IV + 9], 8 ; s14

    vpinsrb        %%XTMP, [%%KEY + 30], 13 ; s15
    vpinsrb        %%XTMP, [%%KEY + 29], 12 ; s15

    vpor           %%XTMP, [%%CONSTANTS + 48] ; s12 - s15

    movzx          DWORD(%%TMP), byte [%%IV + 24]
    and            DWORD(%%TMP), 0x0000003f
    shl            DWORD(%%TMP), 16
    vmovd          %%XTMP2, DWORD(%%TMP)

    movzx          DWORD(%%TMP), byte [%%KEY + 31]
    shl            DWORD(%%TMP), 12
    and            DWORD(%%TMP), 0x000f0000 ; high nibble of K_31
    vpinsrd        %%XTMP2, DWORD(%%TMP), 2

    movzx          DWORD(%%TMP), byte [%%KEY + 31]
    shl            DWORD(%%TMP), 16
    and            DWORD(%%TMP), 0x000f0000 ; low nibble of K_31
    vpinsrd        %%XTMP2, DWORD(%%TMP), 3

    vpor           %%XTMP, %%XTMP2
    vinserti128    %%LFSR8_15, %%XTMP, 1
%endmacro

%macro ZUC_INIT_8 1
%define %%KEY_SIZE %1 ; [constant] Key size (128 or 256)

%ifdef LINUX
	%define		pKe	rdi
	%define		pIv	rsi
	%define		pState	rdx
	%define		tag_sz	rcx ; Only used in ZUC-256
%else
	%define		pKe	rcx
	%define		pIv	rdx
	%define		pState	r8
	%define		tag_sz	r9 ; Only used in ZUC-256
%endif

    FUNC_SAVE

    ; Zero out R1/R2 (only lower half is used)
    vpxor   ymm0, ymm0
%assign I 0
%rep 2
    vmovdqa [pState + OFS_R1 + I*32], ymm0
%assign I (I + 1)
%endrep

    ;;; Initialize all LFSR registers in two steps:
    ;;; first, registers 0-7, then registers 8-15

%if %%KEY_SIZE == 128
%assign off 0
%rep 2
    ; Set read-only registers for shuffle masks for key, IV and Ek_d for 8 registers
    vmovdqa ymm13, [rel shuf_mask_key + off]
    vmovdqa ymm14, [rel shuf_mask_iv + off]
    vmovdqa ymm15, [rel Ek_d + off]

    ; Set 8xLFSR registers for all packets
%assign idx 0
%rep 8
    mov     r9, [pKe + 8*idx]  ; Load Key N pointer
    lea     r10, [pIv + 32*idx] ; Load IV N pointer
    INIT_LFSR_128 r9, r10, ymm13, ymm14, ymm15, APPEND(ymm, idx), ymm12
%assign idx (idx + 1)
%endrep

    ; Store 8xLFSR registers in memory (reordering first,
    ; so all SX registers are together)
    TRANSPOSE8_U32  ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, ymm8, ymm9

%assign i 0
%rep 8
    vmovdqa [pState + 8*off + 32*i], APPEND(ymm, i)
%assign i (i+1)
%endrep

%assign off (off + 32)
%endrep
%else ;; %%KEY_SIZE == 256

    ; Get pointer to constants (depending on tag size, this will point at
    ; constants for encryption, authentication with 4-byte, 8-byte or 16-byte tags)
    lea    r13, [rel EK256_d64]
    bsf    DWORD(tag_sz), DWORD(tag_sz)
    dec    DWORD(tag_sz)
    shl    DWORD(tag_sz), 6
    add    r13, tag_sz

    ;;; Initialize all LFSR registers
%assign off 0
%rep 8
    ;; Load key and IV for each packet
    mov     r12, [pKe + off]
    lea     r10, [pIv + 4*off] ; Load IV N pointer

    ; Initialize S0-15 for each packet
    INIT_LFSR_256 r12, r10, ymm0, ymm1, xmm2, xmm3, r11, r13

%assign i 0
%rep 2
    vmovdqa [pState + 256*i + 4*off], APPEND(ymm, i)
%assign i (i+1)
%endrep

%assign off (off + 8)
%endrep

    ; Read, transpose and store, so all S_X from the 8 packets are in the same register
%assign off 0
%rep 2

%assign i 0
%rep 8
    vmovdqa APPEND(ymm, i), [pState + 32*i + off]
%assign i (i+1)
%endrep

    TRANSPOSE8_U32 ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, ymm8, ymm9

%assign i 0
%rep 8
    vmovdqa [pState + 32*i + off], APPEND(ymm, i)
%assign i (i+1)
%endrep

%assign off (off + 256)
%endrep
%endif ;; %%KEY_SIZE == 256

    ; Load read-only registers
    vmovdqa  ymm12, [rel mask31]

    mov rax, pState

    ; Shift LFSR 32-times, update state variables
%assign N 0
%rep 32
    bits_reorg8 rax, N
    nonlin_fun8 rax, ymm0
    vpsrld  ymm0,1           ; Shift out LSB of W
    lfsr_updt8  rax, N, ymm0 ; W (ymm0) used in LFSR update - not set to zero
%assign N N+1
%endrep

    ; And once more, initial round from keygen phase = 33 times
    bits_reorg8 rax, 0
    nonlin_fun8 rax

    vpxor    ymm0, ymm0
    lfsr_updt8  rax, 0, ymm0

    FUNC_RESTORE

    ret
%endmacro

MKGLOBAL(asm_ZucInitialization_8_avx2,function,internal)
asm_ZucInitialization_8_avx2:
        endbranch64
        ZUC_INIT_8 128

MKGLOBAL(asm_Zuc256Initialization_8_avx2,function,internal)
asm_Zuc256Initialization_8_avx2:
        endbranch64
        ZUC_INIT_8 256

;
; Generate N*4 bytes of keystream
; for 8 buffers (where N is number of rounds)
;
%macro KEYGEN_8_AVX2 1
%define %%NUM_ROUNDS    %1 ; [in] Number of 4-byte rounds

%ifdef LINUX
	%define		pState	rdi
	%define		pKS	rsi
%else
	%define		pState	rcx
	%define		pKS	rdx
%endif

    FUNC_SAVE

    ; Store 8 keystream pointers on the stack
    ; and reserve memory for storing keystreams for all 8 buffers
    mov     r10, rsp
    sub     rsp, (8*8 + %%NUM_ROUNDS * 32)
    and     rsp, -31

%assign i 0
%rep 2
    vmovdqa     ymm0, [pKS + 32*i]
    vmovdqa     [rsp + 32*i], ymm0
%assign i (i+1)
%endrep

    ; Load state pointer in RAX
    mov         rax, pState

    ; Load read-only registers
    vmovdqa     ymm12, [rel mask31]

    ; Generate N*4B of keystream in N rounds
%assign N 1
%rep %%NUM_ROUNDS
    bits_reorg8 rax, N, ymm10
    nonlin_fun8 rax, ymm0
    ; OFS_X3 XOR W (ymm0) and store in stack
    vpxor   ymm10, ymm0
    vmovdqa [rsp + 64 + (N-1)*32], ymm10
    vpxor        ymm0, ymm0
    lfsr_updt8  rax, N, ymm0
%assign N N+1
%endrep

%if (%%NUM_ROUNDS == 8)
    ;; Load all OFS_X3
    vmovdqa xmm0,[rsp + 64]
    vmovdqa xmm1,[rsp + 64 + 32*1]
    vmovdqa xmm2,[rsp + 64 + 32*2]
    vmovdqa xmm3,[rsp + 64 + 32*3]
    vmovdqa xmm4,[rsp + 64 + 16]
    vmovdqa xmm5,[rsp + 64 + 32*1 + 16]
    vmovdqa xmm6,[rsp + 64 + 32*2 + 16]
    vmovdqa xmm7,[rsp + 64 + 32*3 + 16]

    vinserti128 ymm0, ymm0, [rsp + 64 + 32*4], 0x01
    vinserti128 ymm1, ymm1, [rsp + 64 + 32*5], 0x01
    vinserti128 ymm2, ymm2, [rsp + 64 + 32*6], 0x01
    vinserti128 ymm3, ymm3, [rsp + 64 + 32*7], 0x01
    vinserti128 ymm4, ymm4, [rsp + 64 + 32*4 + 16], 0x01
    vinserti128 ymm5, ymm5, [rsp + 64 + 32*5 + 16], 0x01
    vinserti128 ymm6, ymm6, [rsp + 64 + 32*6 + 16], 0x01
    vinserti128 ymm7, ymm7, [rsp + 64 + 32*7 + 16], 0x01

    TRANSPOSE8_U32_PRELOADED ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7, ymm8, ymm9

    store32B_kstr8 ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7

%else ;; NUM_ROUNDS == 8
%assign idx 0
%rep %%NUM_ROUNDS
    vmovdqa APPEND(ymm, idx), [rsp + 64 + idx*32]
    store4B_kstr8 APPEND(ymm, idx)
%assign idx (idx + 1)
%endrep
%endif ;; NUM_ROUNDS == 8

    ;; Reorder LFSR registers, as not all 16 rounds have been completed
    REORDER_LFSR rax, %%NUM_ROUNDS

        ;; Clear stack frame containing keystream information
%ifdef SAFE_DATA
        vpxor   ymm0, ymm0
%assign i 0
%rep (2+%%NUM_ROUNDS)
	vmovdqa [rsp + i*32], ymm0
%assign i (i+1)
%endrep
%endif

    ;; Restore rsp pointer
    mov         rsp, r10

    FUNC_RESTORE

%endmacro

;;
;; void asm_ZucGenKeystream32B_8_avx2(state8_t *pSta, u32* pKeyStr[8])
;;
;; WIN64
;;  RCX    - pSta
;;  RDX    - pKeyStr
;;
;; LIN64
;;  RDI    - pSta
;;  RSI    - pKeyStr
;;
MKGLOBAL(asm_ZucGenKeystream32B_8_avx2,function,internal)
asm_ZucGenKeystream32B_8_avx2:
    endbranch64
    KEYGEN_8_AVX2 8
    vzeroupper
    ret

;;
;; void asm_ZucGenKeystream8B_8_avx2(state8_t *pSta, u32* pKeyStr[8])
;;
;; WIN64
;;  RCX    - pSta
;;  RDX    - pKeyStr
;;
;; LIN64
;;  RDI    - pSta
;;  RSI    - pKeyStr
;;
MKGLOBAL(asm_ZucGenKeystream8B_8_avx2,function,internal)
asm_ZucGenKeystream8B_8_avx2:
    endbranch64
    KEYGEN_8_AVX2 2
    vzeroupper
    ret

;;
;; void asm_ZucGenKeystream4B_8_avx2(state8_t *pSta, u32* pKeyStr[8])
;;
;; WIN64
;;  RCX    - pSta
;;  RDX    - pKeyStr
;;
;; LIN64
;;  RDI    - pSta
;;  RSI    - pKeyStr
;;
MKGLOBAL(asm_ZucGenKeystream4B_8_avx2,function,internal)
asm_ZucGenKeystream4B_8_avx2:
    endbranch64
    KEYGEN_8_AVX2 1
    vzeroupper
    ret

;;
;; Encrypt N*4B bytes on all 8 buffers
;; where N is number of rounds (up to 8)
;; In final call, an array of final bytes is read
;; from memory and only these final bytes are of
;; plaintext are read and XOR'ed.
%macro CIPHERNx4B_8 4
%define %%NROUNDS        %1
%define %%INITIAL_ROUND  %2
%define %%OFFSET         %3
%define %%LAST_CALL      %4

%ifdef LINUX
%define %%TMP1 r8
%define %%TMP2 r9
%else
%define %%TMP1 rdi
%define %%TMP2 rsi
%endif
        ; Load read-only registers
        vmovdqa ymm12, [rel mask31]

        ; Generate N*4B of keystream in N rounds
%assign N 1
%assign round (%%INITIAL_ROUND + N)
%rep %%NROUNDS
        bits_reorg8 rax, round, ymm10
        nonlin_fun8 rax, ymm0
        ; OFS_XR XOR W (ymm0)
        vpxor   ymm10, ymm0
        vmovdqa [rsp + (N-1)*32], ymm10
        vpxor   ymm0, ymm0
        lfsr_updt8  rax, round, ymm0
%assign N N+1
%assign round (round + 1)
%endrep

%assign N 0
%assign idx 8
%rep %%NROUNDS
        vmovdqa APPEND(ymm, idx), [rsp + N*32]
%assign N N+1
%assign idx (idx+1)
%endrep

        TRANSPOSE8_U32 ymm8, ymm9, ymm10, ymm11, ymm12, ymm13, ymm14, \
                       ymm15, ymm0, ymm1
        ;; XOR Input buffer with keystream in rounds of 32B

        mov     r12, [pIn]
        mov     r13, [pIn + 8]
        mov     r14, [pIn + 16]
        mov     r15, [pIn + 24]
%if (%%LAST_CALL == 1)
        ;; Save GP registers
        mov     [rsp + 32*8 + 16 + 8],  %%TMP1
        mov     [rsp + 32*8 + 16 + 16], %%TMP2

        ;; Read in r10 the word containing the number of final bytes to read for each lane
        movzx  r10d, word [rsp + 8*32]
        simd_load_avx2 ymm0, r12 + %%OFFSET, r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 2]
        simd_load_avx2 ymm1, r13 + %%OFFSET, r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 4]
        simd_load_avx2 ymm2, r14 + %%OFFSET, r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 6]
        simd_load_avx2 ymm3, r15 + %%OFFSET, r10, %%TMP1, %%TMP2
%else
        vmovdqu ymm0, [r12 + %%OFFSET]
        vmovdqu ymm1, [r13 + %%OFFSET]
        vmovdqu ymm2, [r14 + %%OFFSET]
        vmovdqu ymm3, [r15 + %%OFFSET]
%endif

        mov     r12, [pIn + 32]
        mov     r13, [pIn + 40]
        mov     r14, [pIn + 48]
        mov     r15, [pIn + 56]
%if (%%LAST_CALL == 1)
        movzx  r10d, word [rsp + 8*32 + 8]
        simd_load_avx2 ymm4, r12 + %%OFFSET, r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 10]
        simd_load_avx2 ymm5, r13 + %%OFFSET, r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 12]
        simd_load_avx2 ymm6, r14 + %%OFFSET, r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 14]
        simd_load_avx2 ymm7, r15 + %%OFFSET, r10, %%TMP1, %%TMP2
%else
        vmovdqu ymm4, [r12 + %%OFFSET]
        vmovdqu ymm5, [r13 + %%OFFSET]
        vmovdqu ymm6, [r14 + %%OFFSET]
        vmovdqu ymm7, [r15 + %%OFFSET]
%endif
        ; Shuffle all keystreams and XOR with plaintext
%assign %%I 0
%assign %%J 8
%rep 8
        vpshufb ymm %+ %%J, [rel swap_mask]
        vpxor   ymm %+ %%J, ymm %+ %%I
%assign %%I (%%I + 1)
%assign %%J (%%J + 1)
%endrep

        ;; Write output
        mov     r12, [pOut]
        mov     r13, [pOut + 8]
        mov     r14, [pOut + 16]
        mov     r15, [pOut + 24]

%if (%%LAST_CALL == 1)
        add     r12, %%OFFSET
        add     r13, %%OFFSET
        add     r14, %%OFFSET
        add     r15, %%OFFSET
        ;; Read in r10 the word containing the number of final bytes to write for each lane
        movzx  r10d, word [rsp + 8*32]
        simd_store_avx2 r12, ymm8,  r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 2]
        simd_store_avx2 r13, ymm9,  r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 4]
        simd_store_avx2 r14, ymm10, r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 6]
        simd_store_avx2 r15, ymm11, r10, %%TMP1, %%TMP2
%else
        vmovdqu [r12 + %%OFFSET], ymm8
        vmovdqu [r13 + %%OFFSET], ymm9
        vmovdqu [r14 + %%OFFSET], ymm10
        vmovdqu [r15 + %%OFFSET], ymm11
%endif

        mov     r12, [pOut + 32]
        mov     r13, [pOut + 40]
        mov     r14, [pOut + 48]
        mov     r15, [pOut + 56]

%if (%%LAST_CALL == 1)
        add     r12, %%OFFSET
        add     r13, %%OFFSET
        add     r14, %%OFFSET
        add     r15, %%OFFSET
        movzx  r10d, word [rsp + 8*32 + 8]
        simd_store_avx2 r12, ymm12, r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 10]
        simd_store_avx2 r13, ymm13, r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 12]
        simd_store_avx2 r14, ymm14, r10, %%TMP1, %%TMP2
        movzx  r10d, word [rsp + 8*32 + 14]
        simd_store_avx2 r15, ymm15, r10, %%TMP1, %%TMP2

        ; Restore registers
        mov     %%TMP1, [rsp + 32*8 + 16 + 8]
        mov     %%TMP2, [rsp + 32*8 + 16 + 16]
%else
        vmovdqu [r12 + %%OFFSET], ymm12
        vmovdqu [r13 + %%OFFSET], ymm13
        vmovdqu [r14 + %%OFFSET], ymm14
        vmovdqu [r15 + %%OFFSET], ymm15
%endif

%endmacro

;;
;; void asm_ZucCipher_8_avx2(state16_t *pSta, u64 *pIn[8],
;;                           u64 *pOut[8], u16 lengths, u64 min_length);
;;
;; WIN64
;;  RCX    - pSta
;;  RDX    - pIn
;;  R8     - pOut
;;  R9     - lengths
;;  rsp + 40 - min_length
;;
;; LIN64
;;  RDI - pSta
;;  RSI - pIn
;;  RDX - pOut
;;  RCX - lengths
;;  R8  - min_length
;;
MKGLOBAL(asm_ZucCipher_8_avx2,function,internal)
asm_ZucCipher_8_avx2:
        endbranch64
%ifdef LINUX
        %define         pState  rdi
        %define         pIn     rsi
        %define         pOut    rdx
        %define         lengths rcx
        %define         arg5    r8
%else
        %define         pState  rcx
        %define         pIn     rdx
        %define         pOut    r8
        %define         lengths r9
        %define         arg5    [rsp + 40]
%endif

%define min_length r10
%define buf_idx r11

        mov     min_length, arg5

        or      min_length, min_length
        jz      exit_cipher32

        FUNC_SAVE

        ;; Convert all lengths from UINT16_MAX (indicating that lane is not valid) to min length
        vmovd   xmm0, DWORD(min_length)
        vpshufb xmm0, xmm0, [rel broadcast_word]
        vmovdqa xmm1, [lengths]
        vpcmpeqw xmm2, xmm2 ;; Get all ff's in XMM register
        vpcmpeqw xmm3, xmm1, xmm2 ;; Mask with FFFF in NULL jobs

        vpand   xmm4, xmm3, xmm0 ;; Length of valid job in all NULL jobs
        vpxor   xmm2, xmm3 ;; Mask with 0000 in NULL jobs
        vpand   xmm1, xmm2 ;; Zero out lengths of NULL jobs
        vpor    xmm1, xmm4 ;; XMM1 contain updated lengths

        ; Round up to nearest multiple of 4 bytes
        vpaddw  xmm0, [rel all_threes]
        vpand   xmm0, [rel all_fffcs]

        ; Calculate remaining bytes to encrypt after function call
        vpsubw  xmm2, xmm1, xmm0
        vpxor   xmm3, xmm3
        vpcmpgtw xmm4, xmm2, xmm3 ;; Mask with FFFF in lengths > 0
        ; Set to zero the lengths of the lanes which are going to be completed (lengths < 0)
        vpand   xmm2, xmm4
        vmovdqa [lengths], xmm2 ; Update in memory the final updated lengths

        ; Calculate number of bytes to encrypt after round of 32 bytes (up to 31 bytes),
        ; for each lane, and store it in stack to be used in the last round
        vpsubw  xmm1, xmm2 ; Bytes to encrypt in all lanes
        vpand   xmm1, [rel all_1fs] ; Number of final bytes (up to 31 bytes) for each lane
        vpcmpeqw xmm2, xmm1, xmm3 ;; Mask with FFFF in lengths == 0
        vpand   xmm2, [rel all_20s] ;; 32 in positions where lengths was 0
        vpor    xmm1, xmm2          ;; Number of final bytes (up to 32 bytes) for each lane

        ; Allocate stack frame to store keystreams (32*8 bytes), number of final bytes (16 bytes),
        ; space for rsp (8 bytes) and 2 GP registers (16 bytes) that will be clobbered later
        mov     rax, rsp
        sub     rsp, (32*8 + 16 + 16 + 8)
        and     rsp, -31
        xor     buf_idx, buf_idx
        vmovdqu [rsp + 32*8], xmm1
        mov     [rsp + 32*8 + 16], rax

        ; Load state pointer in RAX
        mov     rax, pState

loop_cipher64:
        cmp     min_length, 64
        jl      exit_loop_cipher64

        CIPHERNx4B_8 8, 0, buf_idx, 0

        add     buf_idx, 32
        sub     min_length, 32

        CIPHERNx4B_8 8, 8, buf_idx, 0

        add     buf_idx, 32
        sub     min_length, 32

        jmp     loop_cipher64
exit_loop_cipher64:

        ; Check if at least 32 bytes are left to encrypt
        cmp     min_length, 32
        jl      less_than_32

        CIPHERNx4B_8 8, 0, buf_idx, 0
        REORDER_LFSR rax, 8

        add     buf_idx, 32
        sub     min_length, 32

        ; Check if there are more bytes left to encrypt
less_than_32:

        mov     r15, min_length
        add     r15, 3
        shr     r15, 2 ;; number of rounds left (round up length to nearest multiple of 4B)
        jz      exit_final_rounds

_final_rounds_is_1_8:
        cmp     r15, 4
        je      _num_final_rounds_is_4
        jl      _final_rounds_is_1_3

        ; Final rounds 5-8
        cmp     r15, 8
        je      _num_final_rounds_is_8
        cmp     r15, 7
        je      _num_final_rounds_is_7
        cmp     r15, 6
        je      _num_final_rounds_is_6
        cmp     r15, 5
        je      _num_final_rounds_is_5

_final_rounds_is_1_3:
        cmp     r15, 3
        je      _num_final_rounds_is_3
        cmp     r15, 2
        je      _num_final_rounds_is_2

        jmp     _num_final_rounds_is_1

        ; Perform encryption of last bytes (<= 31 bytes) and reorder LFSR registers
%assign I 1
%rep 8
APPEND(_num_final_rounds_is_,I):
        CIPHERNx4B_8 I, 0, buf_idx, 1
        REORDER_LFSR rax, I
        add     buf_idx, (I*4)
        jmp     exit_final_rounds
%assign I (I + 1)
%endrep

exit_final_rounds:
        ;; update in/out pointers

        ; Broadcast buf_idx in all qwords of ymm0
        vmovq           xmm0, buf_idx
        vpshufd         xmm0, xmm0, 0x44
        vperm2f128      ymm0, ymm0, 0x0
        vpaddq          ymm1, ymm0, [pIn]
        vpaddq          ymm2, ymm0, [pIn + 32]
        vmovdqa         [pIn], ymm1
        vmovdqa         [pIn + 32], ymm2
        vpaddq          ymm1, ymm0, [pOut]
        vpaddq          ymm2, ymm0, [pOut + 32]
        vmovdqa         [pOut], ymm1
        vmovdqa         [pOut + 32], ymm2

        ;; Clear stack frame containing keystream information
%ifdef SAFE_DATA
        vpxor   ymm0, ymm0
%assign i 0
%rep 8
	vmovdqa [rsp + i*32], ymm0
%assign i (i+1)
%endrep
%endif
        ; Restore rsp
        mov     rsp, [rsp + 32*8 + 16]

        FUNC_RESTORE

exit_cipher32:
        vzeroupper
        ret

;----------------------------------------------------------------------------------------
;----------------------------------------------------------------------------------------

mksection stack-noexec

;;
;; Copyright (c) 2009-2021, Intel Corporation
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
%include "include/memcpy.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/cet.inc"
%define APPEND(a,b) a %+ b

mksection .rodata
default rel

align 16
Ek_d:
dd      0x0044D700, 0x0026BC00, 0x00626B00, 0x00135E00,
dd      0x00578900, 0x0035E200, 0x00713500, 0x0009AF00
dd      0x004D7800, 0x002F1300, 0x006BC400, 0x001AF100,
dd      0x005E2600, 0x003C4D00, 0x00789A00, 0x0047AC00

; Constants to be used to initialize the LFSR registers
; This table contains four different sets of constants:
; 0-63 bytes: Encryption
; 64-127 bytes: Authentication with tag size = 4
; 128-191 bytes: Authentication with tag size = 8
; 192-255 bytes: Authentication with tag size = 16
align 16
EK256_d64:
dd      0x00220000, 0x002F0000, 0x00240000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000
dd      0x00220000, 0x002F0000, 0x00250000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000
dd      0x00230000, 0x002F0000, 0x00240000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000
dd      0x00230000, 0x002F0000, 0x00250000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000

align 16
shuf_mask_key:
dd      0x00FFFFFF, 0x01FFFFFF, 0x02FFFFFF, 0x03FFFFFF,
dd      0x04FFFFFF, 0x05FFFFFF, 0x06FFFFFF, 0x07FFFFFF,
dd      0x08FFFFFF, 0x09FFFFFF, 0x0AFFFFFF, 0x0BFFFFFF,
dd      0x0CFFFFFF, 0x0DFFFFFF, 0x0EFFFFFF, 0x0FFFFFFF,

align 16
shuf_mask_iv:
dd      0xFFFFFF00, 0xFFFFFF01, 0xFFFFFF02, 0xFFFFFF03,
dd      0xFFFFFF04, 0xFFFFFF05, 0xFFFFFF06, 0xFFFFFF07,
dd      0xFFFFFF08, 0xFFFFFF09, 0xFFFFFF0A, 0xFFFFFF0B,
dd      0xFFFFFF0C, 0xFFFFFF0D, 0xFFFFFF0E, 0xFFFFFF0F,

align 16
shuf_mask_iv_17_19:
db      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x01, 0xFF, 0xFF, 0xFF, 0x02, 0xFF

align 16
clear_iv_mask:
db      0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x3F, 0x00

align 16
shuf_mask_iv_20_23:
db      0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x01, 0xFF, 0xFF, 0xFF, 0x02, 0xFF, 0xFF, 0xFF, 0x03, 0xFF

align 16
mask31:
dd	0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF

align 16
bit_reverse_table_l:
db	0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e, 0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f

align 16
bit_reverse_table_h:
db	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0

align 16
bit_reverse_and_table:
db	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f

align 16
data_mask_64bits:
dd	0xffffffff, 0xffffffff, 0x00000000, 0x00000000

bit_mask_table:
db	0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe

align 16
swap_mask:
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c

align 16
S1_S0_shuf:
db      0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F

align 16
S0_S1_shuf:
db      0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E

align 16
rev_S1_S0_shuf:
db      0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F

align 16
rev_S0_S1_shuf:
db      0x08, 0x00, 0x09, 0x01, 0x0A, 0x02, 0x0B, 0x03, 0x0C, 0x04, 0x0D, 0x05, 0x0E, 0x06, 0x0F, 0x07

align 16
rot8_mod32:
db      0x03, 0x00, 0x01, 0x02, 0x07, 0x04, 0x05, 0x06,
db      0x0B, 0x08, 0x09, 0x0A, 0x0F, 0x0C, 0x0D, 0x0E

align 16
rot16_mod32:
db      0x02, 0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05,
db      0x0A, 0x0B, 0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D

align 16
rot24_mod32:
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
all_0fs:
dw      0x000f, 0x000f, 0x000f, 0x000f, 0x000f, 0x000f, 0x000f, 0x000f

align 16
all_10s:
dw      0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010

; Stack frame for ZucCipher function
struc STACK
_keystr_save    resq  2*4 ; Space for 4 keystreams
_rsp_save:      resq    1 ; Space for rsp pointer
_gpr_save:      resq    2 ; Space for GP registers
_rem_bytes_save resq    1 ; Space for number of remaining bytes
endstruc

mksection .text
align 64

%define MASK31  xmm12

%define OFS_R1  (16*16)
%define OFS_R2  (OFS_R1 + 16)
%define OFS_X0  (OFS_R2 + 16)
%define OFS_X1  (OFS_X0 + 16)
%define OFS_X2  (OFS_X1 + 16)

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

%macro TRANSPOSE4_U32 6
%define %%r0 %1
%define %%r1 %2
%define %%r2 %3
%define %%r3 %4
%define %%t0 %5
%define %%t1 %6

	vshufps	%%t0, %%r0, %%r1, 0x44	; t0 = {b1 b0 a1 a0}
	vshufps	%%r0, %%r0, %%r1, 0xEE	; r0 = {b3 b2 a3 a2}
	vshufps %%t1, %%r2, %%r3, 0x44	; t1 = {d1 d0 c1 c0}
	vshufps	%%r2, %%r2, %%r3, 0xEE	; r2 = {d3 d2 c3 c2}

	vshufps	%%r1, %%t0, %%t1, 0xDD	; r1 = {d1 c1 b1 a1}
	vshufps	%%r3, %%r0, %%r2, 0xDD	; r3 = {d3 c3 b3 a3}
	vshufps	%%r2, %%r0, %%r2, 0x88	; r2 = {d2 c2 b2 a2}
	vshufps	%%r0, %%t0, %%t1, 0x88	; r0 = {d0 c0 b0 a0}
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
;   bits_reorg4()
;
;   params
;       %1 - round number
;       %2 - XMM register storing X3
;       rax - LFSR pointer
;   uses
;
;   return
;
%macro  bits_reorg4 2-3
%define %%STATE     %1 ; [in] ZUC state
%define %%ROUND_NUM %2 ; [in] Round number
%define %%X3        %3 ; [out] XMM register containing X3 of all lanes
    ;
    ;
    ; xmm15 = LFSR_S15
    ; xmm14 = LFSR_S14
    ; xmm11 = LFSR_S11
    ; xmm9  = LFSR_S9
    ; xmm7  = LFSR_S7
    ; xmm5  = LFSR_S5
    ; xmm2  = LFSR_S2
    ; xmm0  = LFSR_S0
    ;
    vmovdqa     xmm15, [%%STATE + ((15 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm14, [%%STATE + ((14 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm11, [%%STATE + ((11 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm9,  [%%STATE + (( 9 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm7,  [%%STATE + (( 7 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm5,  [%%STATE + (( 5 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm2,  [%%STATE + (( 2 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm0,  [%%STATE + (( 0 + %%ROUND_NUM) % 16)*16]

    vpxor       xmm1, xmm1
    vpslld      xmm15, 1
    vpblendw    xmm3,  xmm14, xmm1, 0xAA
    vpblendw    xmm15, xmm3, xmm15, 0xAA

    vmovdqa     [%%STATE + OFS_X0], xmm15   ; BRC_X0
    vpslld      xmm11, 16
    vpsrld      xmm9, 15
    vpor        xmm11, xmm9
    vmovdqa     [%%STATE + OFS_X1], xmm11   ; BRC_X1
    vpslld      xmm7, 16
    vpsrld      xmm5, 15
    vpor        xmm7, xmm5
    vmovdqa     [%%STATE + OFS_X2], xmm7    ; BRC_X2
%if (%0 == 3)
    vpslld      xmm2, 16
    vpsrld      xmm0, 15
    vpor        %%X3, xmm2, xmm0
%endif
%endmacro

;
;   rot_mod32()
;
;   uses xmm7
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
    vpsrld      xmm7, %2, (32 - %3)

    vpor        %1, xmm7
%endif
%endmacro

;
;   nonlin_fun4()
;
;   return
;       W value, updates F_R1[] / F_R2[]
;
%macro nonlin_fun4  1-2
%define %%STATE     %1  ; [in] ZUC state
%define %%W         %2  ; [out] XMM register to contain W for all lanes

%if (%0 == 2)
    vmovdqa     %%W, [%%STATE + OFS_X0]
    vpxor       %%W, [%%STATE + OFS_R1]
    vpaddd      %%W, [%%STATE + OFS_R2]    ; W = (BRC_X0 ^ F_R1) + F_R2
%endif

    vmovdqa     xmm1, [%%STATE + OFS_R1]
    vmovdqa     xmm2, [%%STATE + OFS_R2]
    vpaddd      xmm1, [%%STATE + OFS_X1]    ; W1 = F_R1 + BRC_X1
    vpxor       xmm2, [%%STATE + OFS_X2]    ; W2 = F_R2 ^ BRC_X2

    vpslld      xmm3, xmm1, 16
    vpsrld      xmm4, xmm1, 16
    vpslld      xmm5, xmm2, 16
    vpsrld      xmm6, xmm2, 16
    vpor        xmm1, xmm3, xmm6
    vpor        xmm2, xmm4, xmm5

    rot_mod32   xmm3, xmm1, 2
    rot_mod32   xmm4, xmm1, 10
    rot_mod32   xmm5, xmm1, 18
    rot_mod32   xmm6, xmm1, 24
    vpxor       xmm1, xmm3
    vpxor       xmm1, xmm4
    vpxor       xmm1, xmm5
    vpxor       xmm1, xmm6      ; XMM1 = U = L1(P)

    rot_mod32   xmm3, xmm2, 8
    rot_mod32   xmm4, xmm2, 14
    rot_mod32   xmm5, xmm2, 22
    rot_mod32   xmm6, xmm2, 30
    vpxor       xmm2, xmm3
    vpxor       xmm2, xmm4
    vpxor       xmm2, xmm5
    vpxor       xmm2, xmm6      ; XMM2 = V = L2(Q)

    ; Shuffle U and V to have all S0 lookups in XMM1 and all S1 lookups in XMM2

    ; Compress all S0 and S1 input values in each register
    vpshufb     xmm1, [rel S0_S1_shuf] ; S0: Bytes 0-7, S1: Bytes 8-15
    vpshufb     xmm2, [rel S1_S0_shuf] ; S1: Bytes 0-7, S0: Bytes 8-15

    vshufpd     xmm3, xmm1, xmm2, 0x2 ; All S0 input values
    vshufpd     xmm4, xmm2, xmm1, 0x2 ; All S1 input values

    ; Compute S0 and S1 values
    S0_comput_AVX   xmm3, xmm1, xmm2
    S1_comput_AVX   xmm4, xmm1, xmm2, xmm5

    ; Need to shuffle back xmm1 & xmm2 before storing output
    ; (revert what was done before S0 and S1 computations)
    vshufpd    xmm1, xmm3, xmm4, 0x2
    vshufpd    xmm2, xmm4, xmm3, 0x2

    vpshufb     xmm1, [rel rev_S0_S1_shuf]
    vpshufb     xmm2, [rel rev_S1_S0_shuf]

    vmovdqa     [%%STATE + OFS_R1], xmm1
    vmovdqa     [%%STATE + OFS_R2], xmm2
%endmacro

;
;   store16B_kstr4()
;
%macro  store16B_kstr4 4
%define %%DATA16B_L0  %1  ; [in] 16 bytes of keystream for lane 0
%define %%DATA16B_L1  %2  ; [in] 16 bytes of keystream for lane 1
%define %%DATA16B_L2  %3  ; [in] 16 bytes of keystream for lane 2
%define %%DATA16B_L3  %4  ; [in] 16 bytes of keystream for lane 3

    mov         rcx, [rsp]
    mov         rdx, [rsp + 8]
    mov         r8,  [rsp + 16]
    mov         r9,  [rsp + 24]
    vmovdqu     [rcx], %%DATA16B_L0
    vmovdqu     [rdx], %%DATA16B_L1
    vmovdqu     [r8],  %%DATA16B_L2
    vmovdqu     [r9],  %%DATA16B_L3
%endmacro

;
;   store4B_kstr4()
;
;   params
;
;   %1 - XMM register with OFS_X3
;   return
;
%macro  store4B_kstr4 1
    mov         rcx, [rsp]
    mov         rdx, [rsp + 8]
    mov         r8,  [rsp + 16]
    mov         r9,  [rsp + 24]
    vpextrd     [r9], %1, 3
    vpextrd     [r8], %1, 2
    vpextrd     [rdx], %1, 1
    vmovd       [rcx], %1
    add         rcx, 4
    add         rdx, 4
    add         r8, 4
    add         r9, 4
    mov         [rsp],      rcx
    mov         [rsp + 8],  rdx
    mov         [rsp + 16], r8
    mov         [rsp + 24], r9
%endmacro

;
;   add_mod31()
;       add two 32-bit args and reduce mod (2^31-1)
;   params
;       %1  - arg1/res
;       %2  - arg2
;   uses
;       xmm2
;   return
;       %1
%macro  add_mod31   2
    vpaddd      %1, %2
    vpsrld      xmm2, %1, 31
    vpand       %1, MASK31
    vpaddd      %1, xmm2
%endmacro

;
;   rot_mod31()
;       rotate (mult by pow of 2) 32-bit arg and reduce mod (2^31-1)
;   params
;       %1  - arg
;       %2  - # of bits
;   uses
;       xmm2
;   return
;       %1
%macro  rot_mod31   2

    vpslld      xmm2, %1, %2
    vpsrld      %1, %1, (31 - %2)

    vpor        %1, xmm2
    vpand       %1, MASK31
%endmacro

;
;   lfsr_updt4()
;
%macro  lfsr_updt4  3
%define %%STATE     %1 ; [in] ZUC state
%define %%ROUND_NUM %2 ; [in] Round number
%define %%W         %3 ; [in/clobbered] XMM register to contain W for all lanes
    ;
    ; xmm1  = LFSR_S0
    ; xmm4  = LFSR_S4
    ; xmm10 = LFSR_S10
    ; xmm13 = LFSR_S13
    ; xmm15 = LFSR_S15
    ;
    vmovdqa     xmm1,  [%%STATE + (( 0 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm4,  [%%STATE + (( 4 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm10, [%%STATE + ((10 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm13, [%%STATE + ((13 + %%ROUND_NUM) % 16)*16]
    vmovdqa     xmm15, [%%STATE + ((15 + %%ROUND_NUM) % 16)*16]

    ; Calculate LFSR feedback
    add_mod31   %%W, xmm1
    rot_mod31   xmm1, 8
    add_mod31   %%W, xmm1
    rot_mod31   xmm4, 20
    add_mod31   %%W, xmm4
    rot_mod31   xmm10, 21
    add_mod31   %%W, xmm10
    rot_mod31   xmm13, 17
    add_mod31   %%W, xmm13
    rot_mod31   xmm15, 15
    add_mod31   %%W, xmm15

    vmovdqa     [%%STATE + (( 0 + %%ROUND_NUM) % 16)*16], %%W

    ; LFSR_S16 = (LFSR_S15++) = eax
%endmacro

;
; Initialize LFSR registers for a single lane, for ZUC-128
;
; This macro initializes 4 LFSR registers at a time.
; so it needs to be called four times.
;
; From spec, s_i (LFSR) registers need to be loaded as follows:
;
; For 0 <= i <= 15, let s_i= k_i || d_i || iv_i.
; Where k_i is each byte of the key, d_i is a 15-bit constant
; and iv_i is each byte of the IV.
;
%macro INIT_LFSR_128 7
%define %%KEY       %1 ;; [in] XMM register containing 16-byte key
%define %%IV        %2 ;; [in] XMM register containing 16-byte IV
%define %%SHUF_KEY  %3 ;; [in] Shuffle key mask
%define %%SHUF_IV   %4 ;; [in] Shuffle key mask
%define %%EKD_MASK  %5 ;; [in] Shuffle key mask
%define %%LFSR      %6 ;; [out] XMM register to contain initialized LFSR regs
%define %%XTMP      %7 ;; [clobbered] XMM temporary register

    vpshufb         %%LFSR, %%KEY, %%SHUF_KEY
    vpsrld          %%LFSR, 1
    vpshufb         %%XTMP, %%IV, %%SHUF_IV
    vpor            %%LFSR, %%XTMP
    vpor            %%LFSR, %%EKD_MASK

%endmacro

;
; Initialize LFSR registers for a single lane, for ZUC-256
;
%macro INIT_LFSR_256 9
%define %%KEY       %1 ;; [in] Key pointer
%define %%IV        %2 ;; [in] IV pointer
%define %%LFSR0_3   %3 ;; [out] XMM register to contain initialized LFSR regs 0-3
%define %%LFSR4_7   %4 ;; [out] XMM register to contain initialized LFSR regs 4-7
%define %%LFSR8_11  %5 ;; [out] XMM register to contain initialized LFSR regs 8-11
%define %%LFSR12_15 %6 ;; [out] XMM register to contain initialized LFSR regs 12-15
%define %%XTMP      %7 ;; [clobbered] XMM temporary register
%define %%TMP       %8 ;; [clobbered] GP temporary register
%define %%CONSTANTS %9 ;; [in] Address to constants

    ; s0 - s3
    vpxor          %%LFSR0_3, %%LFSR0_3
    vpinsrb        %%LFSR0_3, [%%KEY], 3      ; s0
    vpinsrb        %%LFSR0_3, [%%KEY + 1], 7  ; s1
    vpinsrb        %%LFSR0_3, [%%KEY + 2], 11 ; s2
    vpinsrb        %%LFSR0_3, [%%KEY + 3], 15 ; s3

    vpsrld         %%LFSR0_3, 1

    vpor           %%LFSR0_3, [%%CONSTANTS] ; s0 - s3

    vpinsrb        %%LFSR0_3, [%%KEY + 21], 1 ; s0
    vpinsrb        %%LFSR0_3, [%%KEY + 16], 0 ; s0

    vpinsrb        %%LFSR0_3, [%%KEY + 22], 5 ; s1
    vpinsrb        %%LFSR0_3, [%%KEY + 17], 4 ; s1

    vpinsrb        %%LFSR0_3, [%%KEY + 23], 9 ; s2
    vpinsrb        %%LFSR0_3, [%%KEY + 18], 8 ; s2

    vpinsrb        %%LFSR0_3, [%%KEY + 24], 13 ; s3
    vpinsrb        %%LFSR0_3, [%%KEY + 19], 12 ; s3

    ; s4 - s7
    vpxor          %%LFSR4_7, %%LFSR4_7
    vpinsrb        %%LFSR4_7, [%%KEY + 4], 3   ; s4
    vpinsrb        %%LFSR4_7, [%%IV], 7        ; s5
    vpinsrb        %%LFSR4_7, [%%IV + 1], 11   ; s6
    vpinsrb        %%LFSR4_7, [%%IV + 10], 15  ; s7

    vpsrld         %%LFSR4_7, 1

    vpinsrb        %%LFSR4_7, [%%KEY + 25], 1 ; s4
    vpinsrb        %%LFSR4_7, [%%KEY + 20], 0 ; s4

    vpinsrb        %%LFSR4_7, [%%KEY + 5], 5 ; s5
    vpinsrb        %%LFSR4_7, [%%KEY + 26], 4 ; s5

    vpinsrb        %%LFSR4_7, [%%KEY + 6], 9 ; s6
    vpinsrb        %%LFSR4_7, [%%KEY + 27], 8 ; s6

    vpinsrb        %%LFSR4_7, [%%KEY + 7], 13 ; s7
    vpinsrb        %%LFSR4_7, [%%IV + 2], 12 ; s7

    vpor           %%LFSR4_7, [%%CONSTANTS + 16] ; s4 - s7

    vmovd          %%XTMP, [%%IV + 17]
    vpshufb        %%XTMP, [rel shuf_mask_iv_17_19]
    vpand          %%XTMP, [rel clear_iv_mask]

    vpor           %%LFSR4_7, %%XTMP

    ; s8 - s11
    vpxor          %%LFSR8_11, %%LFSR8_11
    vpinsrb        %%LFSR8_11, [%%KEY + 8], 3   ; s8
    vpinsrb        %%LFSR8_11, [%%KEY + 9], 7   ; s9
    vpinsrb        %%LFSR8_11, [%%IV + 5], 11   ; s10
    vpinsrb        %%LFSR8_11, [%%KEY + 11], 15 ; s11

    vpsrld         %%LFSR8_11, 1

    vpinsrb        %%LFSR8_11, [%%IV + 3], 1 ; s8
    vpinsrb        %%LFSR8_11, [%%IV + 11], 0 ; s8

    vpinsrb        %%LFSR8_11, [%%IV + 12], 5 ; s9
    vpinsrb        %%LFSR8_11, [%%IV + 4], 4 ; s9

    vpinsrb        %%LFSR8_11, [%%KEY + 10], 9 ; s10
    vpinsrb        %%LFSR8_11, [%%KEY + 28], 8 ; s10

    vpinsrb        %%LFSR8_11, [%%IV + 6], 13 ; s11
    vpinsrb        %%LFSR8_11, [%%IV + 13], 12 ; s11

    vpor           %%LFSR8_11, [%%CONSTANTS + 32] ; s8 - s11

    vmovd          %%XTMP, [%%IV + 20]
    vpshufb        %%XTMP, [rel shuf_mask_iv_20_23]
    vpand          %%XTMP, [rel clear_iv_mask]

    vpor           %%LFSR8_11, %%XTMP

    ; s12 - s15
    vpxor          %%LFSR12_15, %%LFSR12_15
    vpinsrb        %%LFSR12_15, [%%KEY + 12], 3   ; s12
    vpinsrb        %%LFSR12_15, [%%KEY + 13], 7   ; s13
    vpinsrb        %%LFSR12_15, [%%KEY + 14], 11  ; s14
    vpinsrb        %%LFSR12_15, [%%KEY + 15], 15  ; s15

    vpsrld         %%LFSR12_15, 1

    vpinsrb        %%LFSR12_15, [%%IV + 7], 1 ; s12
    vpinsrb        %%LFSR12_15, [%%IV + 14], 0 ; s12

    vpinsrb        %%LFSR12_15, [%%IV + 15], 5 ; s13
    vpinsrb        %%LFSR12_15, [%%IV + 8], 4 ; s13

    vpinsrb        %%LFSR12_15, [%%IV + 16], 9 ; s14
    vpinsrb        %%LFSR12_15, [%%IV + 9], 8 ; s14

    vpinsrb        %%LFSR12_15, [%%KEY + 30], 13 ; s15
    vpinsrb        %%LFSR12_15, [%%KEY + 29], 12 ; s15

    vpor           %%LFSR12_15, [%%CONSTANTS + 48] ; s12 - s15

    movzx          DWORD(%%TMP), byte [%%IV + 24]
    and            DWORD(%%TMP), 0x0000003f
    shl            DWORD(%%TMP), 16
    vmovd          %%XTMP, DWORD(%%TMP)

    movzx          DWORD(%%TMP), byte [%%KEY + 31]
    shl            DWORD(%%TMP), 12
    and            DWORD(%%TMP), 0x000f0000 ; high nibble of K_31
    vpinsrd        %%XTMP, DWORD(%%TMP), 2

    movzx          DWORD(%%TMP), byte [%%KEY + 31]
    shl            DWORD(%%TMP), 16
    and            DWORD(%%TMP), 0x000f0000 ; low nibble of K_31
    vpinsrd        %%XTMP, DWORD(%%TMP), 3

    vpor          %%LFSR12_15, %%XTMP
%endmacro

%macro ZUC_INIT_4 1
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

    mov     rax, pState

    ; Zero out R1-R2 (only lower 128 bits)
    vpxor   xmm0, xmm0
%assign I 0
%rep 2
    vmovdqa [pState + OFS_R1 + I*16], xmm0
%assign I (I + 1)
%endrep

%if %%KEY_SIZE == 128

    ;; Load key and IVs
%assign off 0
%assign i 4
%assign j 8
%rep 4
    mov     r9,  [pKe + off]
    vmovdqu APPEND(xmm,i), [r9]
    ; Read 16 bytes of IV
    vmovdqa APPEND(xmm,j), [pIv + off*4]
%assign off (off + 8)
%assign i (i + 1)
%assign j (j + 1)
%endrep

    ;;; Initialize all LFSR registers in four steps:
    ;;; first, registers 0-3, then registers 4-7, 8-11, 12-15
%assign off 0
%rep 4
    ; Set read-only registers for shuffle masks for key, IV and Ek_d for 8 registers
    vmovdqa xmm13, [rel shuf_mask_key + off]
    vmovdqa xmm14, [rel shuf_mask_iv + off]
    vmovdqa xmm15, [rel Ek_d + off]

    ; Set 4xLFSR registers for all packets
%assign idx 0
%assign i 4
%assign j 8
%rep 4
    INIT_LFSR_128 APPEND(xmm,i), APPEND(xmm,j), xmm13, xmm14, xmm15, APPEND(xmm, idx), xmm12
%assign idx (idx + 1)
%assign i (i + 1)
%assign j (j + 1)
%endrep

    ; Store 4xLFSR registers in memory (reordering first,
    ; so all SX registers are together)
    TRANSPOSE4_U32  xmm0, xmm1, xmm2, xmm3, xmm13, xmm14

%assign i 0
%rep 4
    vmovdqa [pState + 4*off + 16*i], APPEND(xmm, i)
%assign i (i+1)
%endrep

%assign off (off + 16)
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
%rep 4
    ;; Load key and IV for each packet
    mov     r12, [pKe + off]
    lea     r10, [pIv + off*4]

    ; Initialize S0-15 for each packet
    INIT_LFSR_256 r12, r10, xmm0, xmm1, xmm2, xmm3, xmm4, r11, r13

%assign i 0
%rep 4
    vmovdqa [pState + 64*i + 2*off], APPEND(xmm, i)
%assign i (i+1)
%endrep

%assign off (off + 8)
%endrep

    ; Read, transpose and store, so all S_X from the 4 packets are in the same register
%assign off 0
%rep 4

%assign i 0
%rep 4
    vmovdqa APPEND(xmm, i), [pState + 16*i + off]
%assign i (i+1)
%endrep

    TRANSPOSE4_U32 xmm0, xmm1, xmm2, xmm3, xmm14, xmm15

%assign i 0
%rep 4
    vmovdqa [pState + 16*i + off], APPEND(xmm, i)
%assign i (i+1)
%endrep

%assign off (off + 64)
%endrep
%endif ;; %%KEY_SIZE == 256

    ; Load read-only registers
    vmovdqa  xmm12, [rel mask31]

    ; Shift LFSR 32-times, update state variables
%assign N 0
%rep 32
    bits_reorg4 rax, N
    nonlin_fun4 rax, xmm0
    vpsrld  xmm0,1              ; Shift out LSB of W
    lfsr_updt4  rax, N, xmm0    ; W (xmm0) used in LFSR update - not set to zero
%assign N N+1
%endrep

    ; And once more, initial round from keygen phase = 33 times
    bits_reorg4 rax, 0
    nonlin_fun4 rax
    vpxor    xmm0, xmm0
    lfsr_updt4  rax, 0, xmm0

    FUNC_RESTORE

    ret
%endmacro

MKGLOBAL(asm_ZucInitialization_4_avx,function,internal)
asm_ZucInitialization_4_avx:
        ZUC_INIT_4 128

MKGLOBAL(asm_Zuc256Initialization_4_avx,function,internal)
asm_Zuc256Initialization_4_avx:
        ZUC_INIT_4 256

; This macro reorder the LFSR registers
; after N rounds (1 <= N <= 15), since the registers
; are shifted every round
;
; The macro clobbers XMM0-15
;
%macro REORDER_LFSR 2
%define %%STATE      %1
%define %%NUM_ROUNDS %2

%if %%NUM_ROUNDS != 16
%assign %%i 0
%rep 16
    vmovdqa APPEND(xmm,%%i), [%%STATE + 16*%%i]
%assign %%i (%%i+1)
%endrep

%assign %%i 0
%assign %%j %%NUM_ROUNDS
%rep 16
    vmovdqa [%%STATE + 16*%%i], APPEND(xmm,%%j)
%assign %%i (%%i+1)
%assign %%j ((%%j+1) % 16)
%endrep
%endif ;; %%NUM_ROUNDS != 16

%endmacro

;
; Generate N*4 bytes of keystream
; for 4 buffers (where N is number of rounds)
;
%macro KEYGEN_4_AVX 1
%define %%NUM_ROUNDS    %1 ; [in] Number of 4-byte rounds

%ifdef LINUX
	%define		pState	rdi
	%define		pKS	rsi
%else
	%define		pState	rcx
	%define		pKS	rdx
%endif

    FUNC_SAVE

    ; Store 4 keystream pointers on the stack
    ; and reserve memory for storing keystreams for all 4 buffers
    mov     r10, rsp
    sub     rsp, (4*8 + %%NUM_ROUNDS * 16)
    and     rsp, -15

%assign i 0
%rep 2
    vmovdqa     xmm0, [pKS + 16*i]
    vmovdqa     [rsp + 16*i], xmm0
%assign i (i+1)
%endrep

    ; Load state pointer in RAX
    mov         rax, pState

    ; Load read-only registers
    vmovdqa     xmm12, [rel mask31]

    ; Generate N*4B of keystream in N rounds
%assign N 1
%rep %%NUM_ROUNDS
    bits_reorg4 rax, N, xmm10
    nonlin_fun4 rax, xmm0
    ; OFS_X3 XOR W (xmm0) and store in stack
    vpxor       xmm10, xmm0
    vmovdqa [rsp + 4*8 + (N-1)*16], xmm10
    vpxor       xmm0, xmm0
    lfsr_updt4  rax, N, xmm0
%assign N N+1
%endrep

%if (%%NUM_ROUNDS == 4)
    ;; Load all OFS_X3
%assign i 0
%rep 4
    vmovdqa     APPEND(xmm,i), [rsp + 4*8 + i*16]
%assign i (i+1)
%endrep

    TRANSPOSE4_U32 xmm0, xmm1, xmm2, xmm3, xmm4, xmm5

    store16B_kstr4 xmm0, xmm1, xmm2, xmm3
%else ;; NUM_ROUNDS != 4
%assign idx 0
%rep %%NUM_ROUNDS
    vmovdqa APPEND(xmm, idx), [rsp + 4*8 + idx*16]
    store4B_kstr4 APPEND(xmm, idx)
%assign idx (idx + 1)
%endrep
%endif ;; NUM_ROUNDS == 4

        ;; Clear stack frame containing keystream information
%ifdef SAFE_DATA
    vpxor   xmm0, xmm0
%assign i 0
%rep (2+%%NUM_ROUNDS)
    vmovdqa [rsp + i*16], xmm0
%assign i (i+1)
%endrep
%endif

    ;; Reorder memory for LFSR registers, as not all 16 rounds
    ;; will be completed (can be 4 or 2)
    REORDER_LFSR rax, %%NUM_ROUNDS

    ;; Restore rsp pointer to value before pushing keystreams
    mov         rsp, r10

    FUNC_RESTORE

%endmacro

;
;; void asm_ZucGenKeystream16B_4_avx(state4_t *pSta, u32* pKeyStr[4]);
;;
;; WIN64
;;  RCX    - pSta
;;  RDX    - pKeyStr
;;
;; LIN64
;;  RDI    - pSta
;;  RSI    - pKeyStr
;;
MKGLOBAL(asm_ZucGenKeystream16B_4_avx,function,internal)
asm_ZucGenKeystream16B_4_avx:

    KEYGEN_4_AVX 4

    ret

;
;; void asm_ZucGenKeystream8B_4_avx(state4_t *pSta, u32* pKeyStr[4]);
;;
;; WIN64
;;  RCX    - pSta
;;  RDX    - pKeyStr
;;
;; LIN64
;;  RDI    - pSta
;;  RSI    - pKeyStr
;;
MKGLOBAL(asm_ZucGenKeystream8B_4_avx,function,internal)
asm_ZucGenKeystream8B_4_avx:

    KEYGEN_4_AVX 2

    ret

;
;; void asm_ZucGenKeystream4B_4_avx(state4_t *pSta, u32* pKeyStr[4]);
;;
;; WIN64
;;  RCX    - pSta
;;  RDX    - pKeyStr
;;
;; LIN64
;;  RDI    - pSta
;;  RSI    - pKeyStr
;;
MKGLOBAL(asm_ZucGenKeystream4B_4_avx,function,internal)
asm_ZucGenKeystream4B_4_avx:

    KEYGEN_4_AVX 1

    ret

;;
;; Encrypt N*4B bytes on all 4 buffers
;; where N is number of rounds (up to 4)
;; In final call, an array of final bytes is read
;; from memory and only these final bytes are of
;; plaintext are read and XOR'ed.
;;
%macro CIPHERNx4B_4 4
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
        vmovdqa xmm12, [rel mask31]

        ; Generate N*4B of keystream in N rounds
%assign %%N 1
%assign %%round (%%INITIAL_ROUND + %%N)
%rep %%NROUNDS
        bits_reorg4 rax, %%round, xmm10
        nonlin_fun4 rax, xmm0
        ; OFS_XR XOR W (xmm0) and store in stack
        vpxor   xmm10, xmm0
        vmovdqa [rsp + _keystr_save + (%%N-1)*16], xmm10
        vpxor   xmm0, xmm0
        lfsr_updt4  rax, %%round, xmm0
%assign %%N (%%N + 1)
%assign %%round (%%round + 1)
%endrep

%assign %%N 0
%assign %%idx 4
%rep %%NROUNDS
        vmovdqa APPEND(xmm, %%idx), [rsp + _keystr_save + %%N*16]
%assign %%N (%%N + 1)
%assign %%idx (%%idx+1)
%endrep

        TRANSPOSE4_U32 xmm4, xmm5, xmm6, xmm7, xmm8, xmm9

        vmovdqa xmm15, [rel swap_mask]

        ;; XOR Input buffer with keystream in rounds of 16B
        mov     r12, [pIn]
        mov     r13, [pIn + 8]
        mov     r14, [pIn + 16]
        mov     r15, [pIn + 24]
%if (%%LAST_CALL == 4)
        ;; Save GP registers
        mov     [rsp + _gpr_save],  %%TMP1
        mov     [rsp + _gpr_save + 8], %%TMP2

        ;; Read in r10 the word containing the number of final bytes to read for each lane
        movzx  r10d, word [rsp + _rem_bytes_save]
        simd_load_avx_16_1 xmm0, r12 + %%OFFSET, r10
        movzx  r10d, word [rsp + _rem_bytes_save + 2]
        simd_load_avx_16_1 xmm1, r13 + %%OFFSET, r10
        movzx  r10d, word [rsp + _rem_bytes_save + 4]
        simd_load_avx_16_1 xmm2, r14 + %%OFFSET, r10
        movzx  r10d, word [rsp + _rem_bytes_save + 6]
        simd_load_avx_16_1 xmm3, r15 + %%OFFSET, r10
%else
        vmovdqu xmm0, [r12 + %%OFFSET]
        vmovdqu xmm1, [r13 + %%OFFSET]
        vmovdqu xmm2, [r14 + %%OFFSET]
        vmovdqu xmm3, [r15 + %%OFFSET]
%endif

        vpshufb xmm4, xmm15
        vpshufb xmm5, xmm15
        vpshufb xmm6, xmm15
        vpshufb xmm7, xmm15

        vpxor   xmm4, xmm0
        vpxor   xmm5, xmm1
        vpxor   xmm6, xmm2
        vpxor   xmm7, xmm3

        mov     r12, [pOut]
        mov     r13, [pOut + 8]
        mov     r14, [pOut + 16]
        mov     r15, [pOut + 24]

%if (%%LAST_CALL == 1)
        movzx  r10d, word [rsp + _rem_bytes_save]
        simd_store_avx r12, xmm4, r10, %%TMP1, %%TMP2, %%OFFSET
        movzx  r10d, word [rsp + _rem_bytes_save + 2]
        simd_store_avx r13, xmm5, r10, %%TMP1, %%TMP2, %%OFFSET
        movzx  r10d, word [rsp + _rem_bytes_save + 4]
        simd_store_avx r14, xmm6, r10, %%TMP1, %%TMP2, %%OFFSET
        movzx  r10d, word [rsp + _rem_bytes_save + 6]
        simd_store_avx r15, xmm7, r10, %%TMP1, %%TMP2, %%OFFSET

        ; Restore registers
        mov     %%TMP1, [rsp + _gpr_save]
        mov     %%TMP2, [rsp + _gpr_save + 8]
%else
        vmovdqu [r12 + %%OFFSET], xmm4
        vmovdqu [r13 + %%OFFSET], xmm5
        vmovdqu [r14 + %%OFFSET], xmm6
        vmovdqu [r15 + %%OFFSET], xmm7
%endif
%endmacro

;;
;; void asm_ZucCipher_4_avx(state16_t *pSta, u64 *pIn[4],
;;                          u64 *pOut[4], u16 *length[4], u64 min_length);
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
MKGLOBAL(asm_ZucCipher_4_avx,function,internal)
asm_ZucCipher_4_avx:

%ifdef LINUX
        %define         pState  rdi
        %define         pIn     rsi
        %define         pOut    rdx
        %define         lengths rcx
        %define         arg5    r8

        %define         nrounds r8
%else
        %define         pState  rcx
        %define         pIn     rdx
        %define         pOut    r8
        %define         lengths r9
        %define         arg5    [rsp + 40]

        %define         nrounds rdi
%endif

%define min_length r10
%define buf_idx r11

        mov     min_length, arg5

        or      min_length, min_length
        jz      exit_cipher

        FUNC_SAVE

        ;; Convert all lengths from UINT16_MAX (indicating that lane is not valid) to min length
        vmovd   xmm0, DWORD(min_length)
        vpshufb xmm0, [rel broadcast_word]
        vmovq   xmm1, [lengths]
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
        vmovq   [lengths], xmm2 ; Update in memory the final updated lengths

        ; Calculate number of bytes to encrypt after rounds of 16 bytes (up to 15 bytes),
        ; for each lane, and store it in stack to be used in the last round
        vpsubw  xmm1, xmm2 ; Bytes to encrypt in all lanes
        vpand   xmm1, [rel all_0fs] ; Number of final bytes (up to 15 bytes) for each lane
        vpcmpeqw xmm2, xmm1, xmm3 ;; Mask with FFFF in lengths == 0
        vpand   xmm2, [rel all_10s] ;; 16 in positions where lengths was 0
        vpor    xmm1, xmm2          ;; Number of final bytes (up to 16 bytes) for each lane

        ; Allocate stack frame to store keystreams (16*4 bytes), number of final bytes (8 bytes),
        ; space for rsp (8 bytes) and 2 GP registers (16 bytes) that will be clobbered later
        mov     rax, rsp
        sub     rsp, STACK_size
        and     rsp, -15
        xor     buf_idx, buf_idx
        vmovq   [rsp + _rem_bytes_save], xmm1
        mov     [rsp + _rsp_save], rax

        ; Load state pointer in RAX
        mov     rax, pState

loop_cipher64:
        cmp     min_length, 64
        jl      exit_loop_cipher64

%assign round_off 0
%rep 4
        CIPHERNx4B_4 4, round_off, buf_idx, 0

        add     buf_idx, 16
        sub     min_length, 16
%assign round_off (round_off + 4)
%endrep
        jmp     loop_cipher64
exit_loop_cipher64:

        ; Check if there are more bytes left to encrypt
        mov     r15, min_length
        add     r15, 3
        shr     r15, 2 ;; number of rounds left (round up length to nearest multiple of 4B)
        jz      exit_final_rounds

        cmp     r15, 8
        je      _num_final_rounds_is_8
        jb      _final_rounds_is_1_7

        ; Final blocks 9-16
        cmp     r15, 12
        je      _num_final_rounds_is_12
        ja      _final_rounds_is_13_16

        ; Final blocks 9-11
        cmp     r15, 10
        je      _num_final_rounds_is_10
        jb      _num_final_rounds_is_9
        ja      _num_final_rounds_is_11

_final_rounds_is_13_16:
        cmp     r15, 16
        je      _num_final_rounds_is_16
        cmp     r15, 14
        je      _num_final_rounds_is_14
        jb      _num_final_rounds_is_13
        ja      _num_final_rounds_is_15

_final_rounds_is_1_7:
        cmp     r15, 4
        je      _num_final_rounds_is_4
        jl      _final_rounds_is_1_3

        ; Final blocks 5-7
        cmp     r15, 6
        je      _num_final_rounds_is_6
        jb      _num_final_rounds_is_5
        ja      _num_final_rounds_is_7

_final_rounds_is_1_3:
        cmp     r15, 2
        je      _num_final_rounds_is_2
        ja      _num_final_rounds_is_3

        ; Perform encryption of last bytes (<= 63 bytes) and reorder LFSR registers
%assign I 1
%rep 4
APPEND(_num_final_rounds_is_,I):
        CIPHERNx4B_4 I, 0, buf_idx, 1
        REORDER_LFSR rax, I
        add     buf_idx, (I*4)
        jmp     exit_final_rounds
%assign I (I + 1)
%endrep

%assign I 5
%rep 4
APPEND(_num_final_rounds_is_,I):
        CIPHERNx4B_4 4, 0, buf_idx, 0
        add     buf_idx, 16
        CIPHERNx4B_4 (I-4), 4, buf_idx, 1
        add     buf_idx, ((I-4)*4)
        REORDER_LFSR rax, I
        jmp     exit_final_rounds
%assign I (I + 1)
%endrep

%assign I 9
%rep 4
APPEND(_num_final_rounds_is_,I):
        CIPHERNx4B_4 4, 0, buf_idx, 0
        add     buf_idx, 16
        CIPHERNx4B_4 4, 4, buf_idx, 0
        add     buf_idx, 16
        CIPHERNx4B_4 (I-8), 8, buf_idx, 1
        add     buf_idx, ((I-8)*4)
        REORDER_LFSR rax, I
        jmp     exit_final_rounds
%assign I (I + 1)
%endrep

%assign I 13
%rep 4
APPEND(_num_final_rounds_is_,I):
        CIPHERNx4B_4 4, 0, buf_idx, 0
        add     buf_idx, 16
        CIPHERNx4B_4 4, 4, buf_idx, 0
        add     buf_idx, 16
        CIPHERNx4B_4 4, 8, buf_idx, 0
        add     buf_idx, 16
        CIPHERNx4B_4 (I-12), 12, buf_idx, 1
        add     buf_idx, ((I-12)*4)
        REORDER_LFSR rax, I
        jmp     exit_final_rounds
%assign I (I + 1)
%endrep

exit_final_rounds:
        ;; update in/out pointers
        vmovq           xmm0, buf_idx
        vpshufd         xmm0, xmm0, 0x44
        vpaddq          xmm1, xmm0, [pIn]
        vpaddq          xmm2, xmm0, [pIn + 16]
        vmovdqa         [pIn], xmm1
        vmovdqa         [pIn + 16], xmm2
        vpaddq          xmm1, xmm0, [pOut]
        vpaddq          xmm2, xmm0, [pOut + 16]
        vmovdqa         [pOut], xmm1
        vmovdqa         [pOut + 16], xmm2

        ;; Clear stack frame containing keystream information
%ifdef SAFE_DATA
        vpxor   xmm0, xmm0
%assign i 0
%rep 4
	vmovdqa [rsp + _keystr_save + i*16], xmm0
%assign i (i+1)
%endrep
%endif
        ; Restore rsp
        mov     rsp, [rsp + _rsp_save]

        FUNC_RESTORE

exit_cipher:

        ret

;;
;; extern uint32_t asm_Eia3RemainderAVX(const void *ks, const void *data, uint64_t n_bits)
;;
;; Returns authentication update value to be XOR'ed with current authentication tag
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - DATA (data pointer)
;;      R8  - N_BITS (number data bits to process)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - DATA (data pointer)
;;      RDX - N_BITS (number data bits to process)
;;
align 64
MKGLOBAL(asm_Eia3RemainderAVX,function,internal)
asm_Eia3RemainderAVX:

%ifdef LINUX
	%define		KS	rdi
	%define		DATA	rsi
	%define		N_BITS	rdx
%else
	%define		KS	rcx
	%define		DATA	rdx
	%define		N_BITS	r8
%endif
        FUNC_SAVE

        vmovdqa  xmm5, [bit_reverse_table_l]
        vmovdqa  xmm6, [bit_reverse_table_h]
        vmovdqa  xmm7, [bit_reverse_and_table]
        vmovdqa  xmm10, [data_mask_64bits]
        vpxor    xmm9, xmm9

%rep 3
        cmp     N_BITS, 128
        jb      Eia3RoundsAVX_dq_end

        ;; read 16 bytes and reverse bits
        vmovdqu xmm0, [DATA]
        vpand   xmm1, xmm0, xmm7

        vpandn  xmm2, xmm7, xmm0
        vpsrld  xmm2, 4

        vpshufb xmm8, xmm6, xmm1 ; bit reverse low nibbles (use high table)
        vpshufb xmm4, xmm5, xmm2 ; bit reverse high nibbles (use low table)

        vpor    xmm8, xmm4
        ; xmm8 - bit reversed data bytes

        ;; ZUC authentication part
        ;; - 4x32 data bits
        ;; - set up KS
        vmovdqu xmm3, [KS + (0*4)]
        vmovdqu xmm4, [KS + (2*4)]
        vpshufd xmm0, xmm3, 0x61
        vpshufd xmm1, xmm4, 0x61

        ;;  - set up DATA
        vpand   xmm2, xmm8, xmm10
        vpshufd xmm3, xmm2, 0xdc
        vmovdqa xmm4, xmm3

        vpsrldq xmm8, 8
        vpshufd xmm13, xmm8, 0xdc
        vmovdqa xmm14, xmm13

        ;; - clmul
        ;; - xor the results from 4 32-bit words together
        vpclmulqdq xmm3, xmm0, 0x00
        vpclmulqdq xmm4, xmm0, 0x11
        vpclmulqdq xmm13, xmm1, 0x00
        vpclmulqdq xmm14, xmm1, 0x11

        vpxor    xmm3, xmm4
        vpxor    xmm13, xmm14
        vpxor    xmm9, xmm3
        vpxor    xmm9, xmm13
        lea     DATA, [DATA + 16]
        lea     KS, [KS + 16]
        sub     N_BITS, 128
%endrep
Eia3RoundsAVX_dq_end:

%rep 3
        cmp     N_BITS, 32
        jb      Eia3RoundsAVX_dw_end

        ;; swap dwords in KS
        vmovq   xmm1, [KS]
        vpshufd xmm4, xmm1, 0xf1

        ;;  bit-reverse 4 bytes of data
        vmovd   xmm0, [DATA]
        vpand   xmm1, xmm0, xmm7

        vpandn  xmm2, xmm7, xmm0
        vpsrld  xmm2, 4

        vpshufb xmm0, xmm6, xmm1 ; bit reverse low nibbles (use high table)
        vpshufb xmm3, xmm5, xmm2 ; bit reverse high nibbles (use low table)

        vpor    xmm0, xmm3

        ;; rol & xor
        vpclmulqdq xmm0, xmm4, 0
        vpxor    xmm9, xmm0

        lea     DATA, [DATA + 4]
        lea     KS, [KS + 4]
        sub     N_BITS, 32
%endrep

Eia3RoundsAVX_dw_end:
        vmovq   rax, xmm9
        shr     rax, 32

        or      N_BITS, N_BITS
        jz      Eia3RoundsAVX_byte_loop_end

        ;; get 64-bit key stream for the last data bits (less than 32)
        mov     KS, [KS]

        ;; process remaining data bytes and bits
Eia3RoundsAVX_byte_loop:
        or      N_BITS, N_BITS
        jz      Eia3RoundsAVX_byte_loop_end

        cmp     N_BITS, 8
        jb      Eia3RoundsAVX_byte_partial

        movzx   r11, byte [DATA]
        sub     N_BITS, 8
        jmp     Eia3RoundsAVX_byte_read

Eia3RoundsAVX_byte_partial:
        ;; process remaining bits (up to 7)
        lea     r11, [bit_mask_table]
        movzx   r10, byte [r11 + N_BITS]
        movzx   r11, byte [DATA]
        and     r11, r10
        xor     N_BITS, N_BITS
Eia3RoundsAVX_byte_read:

%assign DATATEST 0x80
%rep 8
        xor     r10, r10
        test    r11, DATATEST
        cmovne  r10, KS
        xor     rax, r10
        rol     KS, 1
%assign DATATEST (DATATEST >> 1)
%endrep                 ; byte boundary
        lea     DATA, [DATA + 1]
        jmp     Eia3RoundsAVX_byte_loop

Eia3RoundsAVX_byte_loop_end:

        ;; eax - holds the return value at this stage
        FUNC_RESTORE

        ret

%macro EIA3_ROUND 1
%define %%NUM_16B_ROUNDS %1

%ifdef LINUX
	%define		T	edi
	%define		KS	rsi
	%define		DATA	rdx
%else
	%define		T	ecx
	%define		KS	rdx
	%define		DATA	r8
%endif

        vmovdqa  xmm5, [bit_reverse_table_l]
        vmovdqa  xmm6, [bit_reverse_table_h]
        vmovdqa  xmm7, [bit_reverse_and_table]
        vmovdqa  xmm10, [data_mask_64bits]

        vpxor    xmm9, xmm9
%assign I 0
%rep %%NUM_16B_ROUNDS
        ;; read 16 bytes and reverse bits
        vmovdqu  xmm0, [DATA + 16*I]
        vpand    xmm1, xmm0, xmm7

        vpandn   xmm2, xmm7, xmm0
        vpsrld   xmm2, 4

        vpshufb  xmm8, xmm6, xmm1       ; bit reverse low nibbles (use high table)
        vpshufb  xmm4, xmm5, xmm2       ; bit reverse high nibbles (use low table)

        vpor     xmm8, xmm4
        ; xmm8 - bit reversed data bytes

        ;; ZUC authentication part
        ;; - 4x32 data bits
        ;; - set up KS
%if I != 0
        vmovdqa  xmm11, xmm12
        vmovdqu  xmm12, [KS + (I*16) + (4*4)]
%else
        vmovdqu  xmm11, [KS + (I*16) + (0*4)]
        vmovdqu  xmm12, [KS + (I*16) + (4*4)]
%endif
        vpalignr xmm13, xmm12, xmm11, 8
        vpshufd  xmm2, xmm11, 0x61
        vpshufd  xmm3, xmm13, 0x61

        ;;  - set up DATA
        vpand    xmm13, xmm10, xmm8
        vpshufd  xmm0, xmm13, 0xdc

        vpsrldq  xmm8, 8
        vpshufd  xmm1, xmm8, 0xdc

        ;; - clmul
        ;; - xor the results from 4 32-bit words together
%if I != 0
        vpclmulqdq xmm13, xmm0, xmm2, 0x00
        vpclmulqdq xmm14, xmm0, xmm2, 0x11
        vpclmulqdq xmm15, xmm1, xmm3, 0x00
        vpclmulqdq xmm8,  xmm1, xmm3, 0x11

        vpxor    xmm13, xmm14
        vpxor    xmm15, xmm8
        vpxor    xmm9, xmm13
        vpxor    xmm9, xmm15
%else
        vpclmulqdq xmm9, xmm0, xmm2, 0x00
        vpclmulqdq xmm13, xmm0, xmm2, 0x11
        vpclmulqdq xmm14, xmm1, xmm3, 0x00
        vpclmulqdq xmm15, xmm1, xmm3, 0x11

        vpxor    xmm14, xmm15
        vpxor    xmm9, xmm13
        vpxor    xmm9, xmm14
%endif

%assign I (I + 1)
%endrep

        ;; - update T
        vmovq   rax, xmm9
        shr     rax, 32
        xor     eax, T

%endmacro

;;
;;extern uint32_t asm_Eia3Round64BAVX(uint32_t T, const void *KS, const void *DATA)
;;
;; Updates authentication tag T based on keystream KS and DATA.
;; - it processes 64 bytes of DATA
;; - reads data in 16 byte chunks and bit reverses them
;; - reads and re-arranges KS
;; - employs clmul for the XOR & ROL part
;; - copies top 64 bytes of KS to bottom (for the next round)
;;
;; WIN64
;;	RCX - T
;;	RDX - KS pointer to key stream (2 x 64 bytes)
;;;     R8  - DATA pointer to data
;; LIN64
;;	RDI - T
;;	RSI - KS pointer to key stream (2 x 64 bytes)
;;      RDX - DATA pointer to data
;;
align 64
MKGLOBAL(asm_Eia3Round64BAVX,function,internal)
asm_Eia3Round64BAVX:

        FUNC_SAVE

        EIA3_ROUND 4

        FUNC_RESTORE

        ret

;;
;;extern uint32_t asm_Eia3Round32BAVX(uint32_t T, const void *KS, const void *DATA)
;;
;; Updates authentication tag T based on keystream KS and DATA.
;; - it processes 32 bytes of DATA
;; - reads data in 16 byte chunks and bit reverses them
;; - reads and re-arranges KS
;; - employs clmul for the XOR & ROL part
;; - copies top 32 bytes of KS to bottom (for the next round)
;;
;; WIN64
;;	RCX - T
;;	RDX - KS pointer to key stream (2 x 32 bytes)
;;;     R8  - DATA pointer to data
;; LIN64
;;	RDI - T
;;	RSI - KS pointer to key stream (2 x 32 bytes)
;;      RDX - DATA pointer to data
;;
align 64
MKGLOBAL(asm_Eia3Round32BAVX,function,internal)
asm_Eia3Round32BAVX:

        FUNC_SAVE

        EIA3_ROUND 2

        FUNC_RESTORE

        ret

;;
;;extern uint32_t asm_Eia3Round16BAVX(uint32_t T, const void *KS, const void *DATA)
;;
;; Updates authentication tag T based on keystream KS and DATA.
;; - it processes 16 bytes of DATA
;; - reads data in 16 byte chunks and bit reverses them
;; - reads and re-arranges KS
;; - employs clmul for the XOR & ROL part
;; - copies top 16 bytes of KS to bottom (for the next round)
;;
;; WIN64
;;	RCX - T
;;	RDX - KS pointer to key stream (2 x 16 bytes)
;;;     R8  - DATA pointer to data
;; LIN64
;;	RDI - T
;;	RSI - KS pointer to key stream (2 x 16 bytes)
;;      RDX - DATA pointer to data
;;
align 64
MKGLOBAL(asm_Eia3Round16BAVX,function,internal)
asm_Eia3Round16BAVX:

        FUNC_SAVE

        EIA3_ROUND 1

        FUNC_RESTORE

        ret

;----------------------------------------------------------------------------------------
;----------------------------------------------------------------------------------------

mksection stack-noexec

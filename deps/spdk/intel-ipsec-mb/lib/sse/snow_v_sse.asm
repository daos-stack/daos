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

;;  Implementation based on
;; Patrik Ekdahl1, Thomas Johansson2, Alexander Maximov1 and Jing Yang2
;; abstract : 'A new SNOW stream cipher called SNOW-V'
;; https://eprint.iacr.org/2018/1143.pdf

%include "include/os.asm"
%include "include/reg_sizes.asm"
%include "include/memcpy.asm"
%include "include/imb_job.asm"
%include "include/clear_regs.asm"
%include "include/cet.inc"

%ifndef SNOW_V
%define SNOW_V snow_v_sse
%endif

%ifndef SNOW_V_AEAD_INIT
%define SNOW_V_AEAD_INIT snow_v_aead_init_sse
%endif

mksection .rodata

align 16
alpha:
times 8 dw 0x990f

align 16
alpha_inv:
times 8 dw -0xcc87

align 16
beta:
times 8 dw 0xc963

align 16
beta_inv:
times 8 dw -0xe4b1

;; permutation: [ 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15 ]
align 16
sigma:
dq 0xd0905010c080400
dq 0xf0b07030e0a0602

align 16
aead_lsfr_b_lo:
dq 0x20646b4578656c41
dq 0x6d6f6854676E694a

%ifdef LINUX
      %define arg1      rdi
      %define offset    rcx
%else
      %define arg1      rcx
      %define offset    r8
%endif

%define job     arg1

mksection .text

;; Registers usage
;; xmm0, xmm1, xmm2, xmm3   : temporary space
;; xmm4                     : generated keystream
;; xmm5, xmm6, xmm7         : FSM (R1, R2, R3)
;; xmm8, xmm9, xmm10, xmm11 : LFSR_A, LFSR_B
;; xmm13, xmm14, xmm15      : constants gA, gB, inv_gA

%define gA       xmm13
%define gB       xmm14
%define inv_gA   xmm15

%define FSM_R1         xmm5
%define FSM_R2         xmm6
%define FSM_R3         xmm7

%define LFSR_A_LDQ     xmm8   ;; LSFR A: (a7, ..., a0)
%define LFSR_A_HDQ     xmm9   ;; LSFR A: (a15, ..., a8)
%define LFSR_B_LDQ     xmm10  ;; LSFR B: (b7, ..., b0)
%define LFSR_B_HDQ     xmm11  ;; LSFR B: (b15, ..., b8)

%define temp4          xmm12

;; =============================================================================
;; =============================================================================
;; Calculate 128-bit keystream
;; =============================================================================
%macro SNOW_V_KEYSTREAM 4

;; all input is expected to be xmm registers
%define %%KEYSTREAM   %1  ;; [out]  128 bit keystream
%define %%LFSR_B_HDQ  %2  ;; [in]  128 bit LFSR_B_HDQ (b15, ..., b8)
%define %%FSM_R1      %3  ;; [in]  128 bit FSM: R1
%define %%FSM_R2      %4  ;; [in]  128 bit FSM: R2

      movdqa %%KEYSTREAM, %%LFSR_B_HDQ
      paddd  %%KEYSTREAM, %%FSM_R1
      pxor   %%KEYSTREAM, %%FSM_R2

%endmacro ;; SNOW_V_KEYSTREAM

;; =============================================================================
;; =============================================================================
;; Update SNOW_V FSM
;; =============================================================================
%macro SNOW_V_FSM_UPDATE 5

;; this macro needs defined constant sigma
;; all input is expected to be xmm registers

%define %%FSM_R1      %1  ;; [in/out]  128 bit FSM: R1
%define %%FSM_R2      %2  ;; [in/out]  128 bit FSM: R2
%define %%FSM_R3      %3  ;; [in/out]  128 bit FSM: R3
%define %%T2          %4  ;; [in/clobbered] 128 bit T2 tap register
                          ;; containing copy of LFSR_A_LDQ (a7, ..., a0)
%define %%TEMP1       %5  ;; [clobbered] 128 bit register

      pxor   %%T2, %%FSM_R3         ;; T2 = R3 XOR LSFR_A [0:7]
      paddd  %%T2, %%FSM_R2         ;; T2 += R2
      pshufb %%T2, [rel sigma]      ;; T2 = sigma(T2)

      movdqa %%FSM_R3, %%FSM_R2     ;; R3 = R2
      movdqa %%FSM_R2, %%FSM_R1     ;; R2 = R1
      pxor   %%TEMP1, %%TEMP1       ;; TEMP1 = 0

      movdqa %%FSM_R1, %%T2         ;; R1 = sigma(T2)
      aesenc %%FSM_R3, %%TEMP1      ;; R3 = AESR(R2) (encryption round key C1 = 0)
      aesenc %%FSM_R2, %%TEMP1      ;; R2 = AESR(R1) (encryption round key C2 = 0)

%endmacro ;; SNOW_V_FSM_UPDATE

;; =============================================================================
;; =============================================================================
;; Update SNOW_V LSFR
;; =============================================================================
%macro SNOW_V_LFSR_UPDATE 11

;; this macro needs defined constant beta_inv
;; all input is expected to be xmm registers

%define %%LFSR_A_LDQ     %1   ;; [in/out]    128 bit LFSR_A_LDQ (a7, ..., a0)
%define %%LFSR_A_HDQ     %2   ;; [in/out]    128 bit LFSR_A_HDQ (a15, ..., a8)
%define %%LFSR_B_LDQ     %3   ;; [in/out]    128 bit LFSR_B_LDQ (b7, ..., b0)
%define %%LFSR_B_HDQ     %4   ;; [in/out]    128 bit LFSR_B_HDQ (b15, ..., b8)
%define %%T1             %5   ;; [in/out]    128 bit T1 tap register
%define %%T2             %6   ;; [out]       128 bit T2 tap register
%define %%TEMP1          %7   ;; [clobbered] 128 bit register
%define %%TEMP2          %8   ;; [clobbered] 128 bit register
%define %%gA             %9   ;; [in]        128 bit gA
%define %%gB             %10  ;; [in]        128 bit gB
%define %%inv_gA         %11  ;; [in]        128 bit inv_gA

;; LSFR Update
;; for i in [0,7]:
;;    tmpa_i = alpha*a_i + alpha^-1*a_(i+8) + b_i + a_(i+1) mod g^A(alpha)
;;    tmpb_i =  beta*b_i +  beta^−1*b_(i+8) + a_i + b_(i+3) mod g^ B(β)
;;
;; (a15, a14, ..., a0) = (tmpa_7, ..., tmpa_0, a15, ..., a8)
;; (b15, b14, ..., b0) = (tmpb_7, ..., tmpb_0, b15, ..., b8)
;;
;; alpha*x      = (x<<1) xor ((x >> 15) and gA)
;;
;; (alpha^-1)*x = if (x & 0x0001): (x >> 1) xor [inv_gA]
;;                else:            (x >> 1)
;;              = (x >> 1) xor signw(inv_gA, x << 15)

      ;; calculate mulx_A = (alpha*a7, ..., alpha*a0)
      movdqa %%TEMP1, %%LFSR_A_LDQ
      psraw  %%TEMP1, 15             ;; 16-bit mask with sign bits preserved
      pand   %%TEMP1, %%gA
      movdqa %%TEMP2, %%LFSR_A_LDQ
      psllw  %%TEMP2, 1
      pxor   %%TEMP2, %%TEMP1        ;; TEMP2 = mulx_A

      ;; calculate invx_A = (alpha^-1*a15, ..., alpha^-1*a8)
      movdqa %%TEMP1, %%LFSR_A_HDQ
      psllw  %%TEMP1, 15
      movdqa %%T2, %%inv_gA
      psignw %%T2, %%TEMP1           ;; negate bits in inv_gA depending on LFSR_A_HDQ << 15
      movdqa %%TEMP1, %%LFSR_A_HDQ
      psrlw  %%TEMP1, 1
      pxor   %%TEMP1, %%T2           ;; TEMP1 = invx_A

      movdqa %%T2, %%LFSR_A_HDQ      ;; make copy of LFSR_A_HDQ

      ;; LFSR_A_HDQ = mulx_A XOR invx_A XOR (b7, ..., b0) XOR (a8, ..., a1)
      pxor    %%TEMP1, %%TEMP2               ;; TEMP1 = invx_A xor mulx_A
      palignr %%LFSR_A_HDQ, %%LFSR_A_LDQ, 2  ;; T2 = (tmpa_8, ..., tmpa_1)
      pxor    %%LFSR_A_HDQ, %%LFSR_B_LDQ
      pxor    %%LFSR_A_HDQ, %%TEMP1

      ;; calculate mulx_B
      movdqa   %%TEMP1, %%LFSR_B_LDQ
      psraw    %%TEMP1, 15
      pand     %%TEMP1, %%gB
      movdqa   %%TEMP2, %%LFSR_B_LDQ
      psllw    %%TEMP2, 1
      pxor     %%TEMP1, %%TEMP2

      ;; T1 = mulx_B XOR (a7, ..., a0) XOR (b10, ..., b3)
      pxor     %%TEMP1, %%LFSR_A_LDQ
      palignr  %%T1, %%LFSR_B_LDQ, 6    ;; (b10, ..., b3)
      pxor     %%T1, %%TEMP1

      ;; calculate invx_B
      movdqa   %%TEMP1, %%LFSR_B_HDQ
      psllw    %%TEMP1, 15
      movdqa   %%TEMP2, [rel beta_inv]
      psignw   %%TEMP2, %%TEMP1
      movdqa   %%TEMP1, %%LFSR_B_HDQ
      psrlw    %%TEMP1, 1
      pxor     %%TEMP1, %%TEMP2

      ;; LFSR_B_HDQ = mulx_B XOR invx_B XOR (a7, ..., a0) XOR (b10, ..., b3)
      pxor     %%T1, %%TEMP1

      movdqa %%LFSR_B_LDQ, %%LFSR_B_HDQ   ;; LFSR_B_LDQ = LFSR_B_HDQ
      movdqa %%LFSR_A_LDQ, %%T2           ;; LFSR_A_LDQ = LFSR_A_HDQ
      movdqa %%LFSR_B_HDQ, %%T1

%endmacro ;; SNOW_V_LFSR_UPDATE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

MKGLOBAL(SNOW_V_AEAD_INIT,function,)
SNOW_V_AEAD_INIT:
      endbranch64
      ;; use offset to indicate AEAD mode
      mov DWORD(offset), 1
      movdqa LFSR_B_LDQ, [rel aead_lsfr_b_lo]
      jmp snow_v_common_init

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

MKGLOBAL(SNOW_V,function,)
SNOW_V:
      endbranch64
      ;; use offset to indicate AEAD mode
      xor DWORD(offset), DWORD(offset)
      pxor LFSR_B_LDQ, LFSR_B_LDQ

snow_v_common_init:

      ;; Init LSFR
      mov rax, [job + _enc_keys]
      movdqu   LFSR_A_HDQ, [rax]
      movdqu   LFSR_B_HDQ, [rax + 16]
      mov rax, [job + _iv]
      movdqu   LFSR_A_LDQ, [rax]

      ;; Init FSM: R1 = R2 = R3 = 0
      pxor FSM_R1, FSM_R1
      pxor FSM_R2, FSM_R2
      pxor FSM_R3, FSM_R3

      movdqa gA, [rel alpha]
      movdqa gB, [rel beta]
      movdqa inv_gA, [rel alpha_inv]

      movdqa xmm0, LFSR_B_HDQ ;; init T1 for LSFR update
      movdqa xmm1, LFSR_A_LDQ ;; init T2 for FSM update

      mov eax, 15

 init_fsm_lfsr_loop:

      SNOW_V_KEYSTREAM xmm4, LFSR_B_HDQ, FSM_R1, FSM_R2
      SNOW_V_FSM_UPDATE FSM_R1, FSM_R2, FSM_R3, xmm1, xmm2
      SNOW_V_LFSR_UPDATE LFSR_A_LDQ, LFSR_A_HDQ, LFSR_B_LDQ, LFSR_B_HDQ, \
                         xmm0, xmm1, xmm2, xmm3, gA, gB, inv_gA
      pxor LFSR_A_HDQ, xmm4
      dec eax
      jnz init_fsm_lfsr_loop

      mov rax, [job + _enc_keys]
      movdqu temp4, [rax]
      pxor FSM_R1, temp4

      SNOW_V_KEYSTREAM xmm4, LFSR_B_HDQ, FSM_R1, FSM_R2
      SNOW_V_FSM_UPDATE FSM_R1, FSM_R2, FSM_R3, xmm1, xmm2
      SNOW_V_LFSR_UPDATE LFSR_A_LDQ, LFSR_A_HDQ,LFSR_B_LDQ, LFSR_B_HDQ, \
                         xmm0, xmm1, xmm2, xmm3, gA, gB, inv_gA
      pxor LFSR_A_HDQ, xmm4
      movdqu temp4, [rax + 16]
      pxor FSM_R1, temp4

      ;; At this point FSM and LSFR are initialized

      or DWORD(offset), DWORD(offset)
      jz no_aead

      ;; in AEAD mode hkey = keystream_0 and endpad = keystream_1
      mov r11, [job + _snow_v_reserved]
      ;; generate hkey
      SNOW_V_KEYSTREAM xmm4, LFSR_B_HDQ, FSM_R1, FSM_R2
      movdqu [r11], xmm4

      ;; generate endpad
      SNOW_V_FSM_UPDATE FSM_R1, FSM_R2, FSM_R3, xmm1, xmm2
      SNOW_V_LFSR_UPDATE LFSR_A_LDQ, LFSR_A_HDQ,LFSR_B_LDQ, LFSR_B_HDQ, \
                         xmm0, xmm1, xmm2, xmm3, gA, gB, inv_gA
      SNOW_V_KEYSTREAM xmm4, LFSR_B_HDQ, FSM_R1, FSM_R2

      SNOW_V_FSM_UPDATE FSM_R1, FSM_R2, FSM_R3, xmm1, xmm2
      SNOW_V_LFSR_UPDATE LFSR_A_LDQ, LFSR_A_HDQ,LFSR_B_LDQ, LFSR_B_HDQ, \
                         xmm0, xmm1, xmm2, xmm3, gA, gB, inv_gA

      mov offset, [r11 + 24]
      movdqu [r11 + 16], xmm4
      or offset, offset
      ;; if last 8 bytes endpad are not 0 skip encrypt/decrypt operation
      ;; option used to calculate auth tag for decrypt and not overwrite
      ;; cipher by plain when the same src/dst pointer is used
      jnz no_partial_block_left

no_aead:

      ;; Process input
      mov r10, [job + _src]
      add r10, [job + _cipher_start_src_offset_in_bytes]
      mov r11, [job + _dst]
      mov rax, [job + _msg_len_to_cipher_in_bytes]
      xor offset, offset
      ;; deal with partial block less than 16b outside main loop
      and rax, 0xfffffffffffffff0
      jz final_bytes

encrypt_loop:

      movdqu temp4, [r10 + offset]

      SNOW_V_KEYSTREAM xmm4, LFSR_B_HDQ, FSM_R1, FSM_R2

      SNOW_V_FSM_UPDATE FSM_R1, FSM_R2, FSM_R3, xmm1, xmm2

      SNOW_V_LFSR_UPDATE LFSR_A_LDQ, LFSR_A_HDQ,LFSR_B_LDQ, LFSR_B_HDQ, \
                         xmm0, xmm1, xmm2, xmm3, gA, gB, inv_gA

      pxor   temp4, xmm4
      movdqu [r11 + offset], temp4
      add    offset, 16
      sub    rax, 16
      jnz    encrypt_loop

final_bytes:
      mov rax, [job + _msg_len_to_cipher_in_bytes]
      and rax, 0xf
      jz no_partial_block_left

      ;; load partial block into XMM register
      add    r10, offset
      simd_load_sse_15_1 temp4, r10, rax
      SNOW_V_KEYSTREAM xmm4, LFSR_B_HDQ, FSM_R1, FSM_R2
      pxor   temp4, xmm4
      add    r11, offset
      ;; use r10 and offset as temp [clobbered]
      simd_store_sse_15 r11, temp4, rax, r10, offset

no_partial_block_left:
      ;; Clear registers and return data
%ifdef SAFE_DATA
      clear_scratch_xmms_sse_asm
%endif

      mov   rax, job
      or    dword [rax + _status], IMB_STATUS_COMPLETED_CIPHER

ret

mksection stack-noexec

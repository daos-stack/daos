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
%define SNOW_V snow_v_avx
%endif

%ifndef SNOW_V_AEAD_INIT
%define SNOW_V_AEAD_INIT snow_v_aead_init_avx
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

%define job             arg1

mksection .text

;; Registers usage
;; xmm0                       : generated keystream
;; xmm1, xmm2, xmm3, xmm4     : temporary space
;; xmm5, xmm6, xmm7           : FSM (R1, R2, R3)
;; xmm8, xmm9, xmm10, xmm11   : LFSR_A, LFSR_B
;; xmm12, xmm13, xmm14, xmm15 : constants gA, gB, inv_gA, inv_gB

%define KEYSTREAM       xmm0

%define temp1           xmm1
%define temp2           xmm2
%define temp3           xmm3
%define temp4           xmm4

%define FSM_R1          xmm5
%define FSM_R2          xmm6
%define FSM_R3          xmm7

%define LFSR_A_LDQ      xmm8   ;; LSFR A: (a7, ..., a0)
%define LFSR_A_HDQ      xmm9   ;; LSFR A: (a15, ..., a8)
%define LFSR_B_LDQ      xmm10  ;; LSFR B: (b7, ..., b0)
%define LFSR_B_HDQ      xmm11  ;; LSFR B: (b15, ..., b8)

%define gA              xmm12
%define gB              xmm13
%define inv_gA          xmm14
%define inv_gB          xmm15

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

      vpaddd      %%KEYSTREAM, %%LFSR_B_HDQ, %%FSM_R1
      vpxor       %%KEYSTREAM, %%KEYSTREAM, %%FSM_R2

%endmacro ;; SNOW_V_KEYSTREAM

;; =============================================================================
;; =============================================================================
;; Update SNOW_V FSM
;; =============================================================================
%macro SNOW_V_FSM_UPDATE 6

;; this macro needs defined constant sigma
;; all input is expected to be xmm registers

%define %%FSM_R1        %1  ;; [in/out]  128 bit FSM: R1
%define %%FSM_R2        %2  ;; [in/out]  128 bit FSM: R2
%define %%FSM_R3        %3  ;; [in/out]  128 bit FSM: R3
%define %%LFSR_A_LDQ    %4  ;; [in] LFSR_A_LDQ (a7, ..., a0)
%define %%TEMP1         %5  ;; [clobbered] 128 bit register
%define %%TEMP2         %6  ;; [clobbered] 128 bit register

      vpxor       %%TEMP2, %%LFSR_A_LDQ, %%FSM_R3   ;; TEMP2 = R3 XOR LSFR_A [0:7]
      vpaddd      %%TEMP2, %%TEMP2, %%FSM_R2        ;; TEMP2 += R2

      vpxor       %%TEMP1, %%TEMP1, %%TEMP1         ;; TEMP1 = 0

      vaesenc     %%FSM_R3, %%FSM_R2, %%TEMP1      ;; R3 = AESR(R2) (encryption round key C1 = 0)
      vaesenc     %%FSM_R2, %%FSM_R1, %%TEMP1      ;; R2 = AESR(R1) (encryption round key C2 = 0)
      vpshufb     %%FSM_R1, %%TEMP2, [rel sigma]   ;; R1 = sigma(TEMP2)

%endmacro ;; SNOW_V_FSM_UPDATE

;; =============================================================================
;; =============================================================================
;; Update SNOW_V LSFR
;; =============================================================================
%macro SNOW_V_LFSR_UPDATE 11

;; all input is expected to be xmm registers

%define %%LFSR_A_LDQ    %1   ;; [in/out]    128 bit LFSR_A_LDQ (a7, ..., a0)
%define %%LFSR_A_HDQ    %2   ;; [in/out]    128 bit LFSR_A_HDQ (a15, ..., a8)
%define %%LFSR_B_LDQ    %3   ;; [in/out]    128 bit LFSR_B_LDQ (b7, ..., b0)
%define %%LFSR_B_HDQ    %4   ;; [in/out]    128 bit LFSR_B_HDQ (b15, ..., b8)
%define %%inv_gB        %5   ;; [in]        128 bit inv_gB
%define %%T2            %6   ;; [clobbered] 128 bit T2 tap register
%define %%TEMP1         %7   ;; [clobbered] 128 bit register
%define %%TEMP2         %8   ;; [clobbered] 128 bit register
%define %%gA            %9   ;; [in]        128 bit gA
%define %%gB            %10  ;; [in]        128 bit gB
%define %%inv_gA        %11  ;; [in]        128 bit inv_gA

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
      vpsraw      %%TEMP1, %%LFSR_A_LDQ, 15  ;; 16-bit mask with sign bits preserved
      vpand       %%TEMP1, %%TEMP1, %%gA
      vpsllw      %%TEMP2, %%LFSR_A_LDQ, 1
      vpxor       %%TEMP2, %%TEMP2, %%TEMP1   ;; TEMP2 = mulx_A

      ;; calculate invx_A = (alpha^-1*a15, ..., alpha^-1*a8)
      vpsllw      %%TEMP1, %%LFSR_A_HDQ, 15
      vpsignw     %%TEMP1, %%inv_gA, %%TEMP1    ;; negate bits in inv_gA depending on LFSR_A_HDQ << 15
      vpxor       %%TEMP2, %%TEMP1, %%TEMP2
      vpsrlw      %%TEMP1, %%LFSR_A_HDQ, 1
      vpxor       %%TEMP1, %%TEMP1, %%TEMP2  ;; TEMP1 = invx_A xor mulx_A

      ;; LFSR_A_HDQ = mulx_A XOR invx_A XOR (b7, ..., b0) XOR (a8, ..., a1)
      vpalignr    %%TEMP2, %%LFSR_A_HDQ, %%LFSR_A_LDQ, 2  ;; T2 = (tmpa_8, ..., tmpa_1)
      vpxor       %%TEMP2, %%TEMP2, %%LFSR_B_LDQ
      vpxor       %%T2, %%TEMP2, %%TEMP1

      ;; calculate mulx_B
      vpsraw      %%TEMP1, %%LFSR_B_LDQ, 15
      vpand       %%TEMP1, %%TEMP1, %%gB
      vpsllw      %%TEMP2, %%LFSR_B_LDQ, 1
      vpxor       %%TEMP1, %%TEMP1, %%TEMP2

      ;; T2 = mulx_B XOR (a7, ..., a0) XOR (b10, ..., b3)
      vpxor       %%TEMP1, %%TEMP1, %%LFSR_A_LDQ
      vmovdqa     %%LFSR_A_LDQ, %%LFSR_A_HDQ           ;; LFSR_A_LDQ = LFSR_A_HDQ
      vmovdqa     %%LFSR_A_HDQ, %%T2

      vpalignr    %%TEMP2, %%LFSR_B_HDQ, %%LFSR_B_LDQ, 6    ;; (b10, ..., b3)
      vmovdqa     %%LFSR_B_LDQ, %%LFSR_B_HDQ                 ;; LFSR_B_LDQ = LFSR_B_HDQ
      vpxor       %%TEMP2, %%TEMP2, %%TEMP1

      ;; calculate invx_B
      vpsllw      %%TEMP1, %%LFSR_B_HDQ, 15
      vpsrlw      %%LFSR_B_HDQ, %%LFSR_B_HDQ, 1
      vpsignw     %%TEMP1,inv_gB, %%TEMP1

      ;; LFSR_B_HDQ = mulx_B XOR invx_B XOR (a7, ..., a0) XOR (b10, ..., b3)
      vpxor       %%LFSR_B_HDQ, %%LFSR_B_HDQ, %%TEMP1
      vpxor       %%LFSR_B_HDQ, %%LFSR_B_HDQ, %%TEMP2

%endmacro ;; SNOW_V_LFSR_UPDATE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

MKGLOBAL(SNOW_V_AEAD_INIT,function,)
SNOW_V_AEAD_INIT:
      endbranch64
      ;; use offset to indicate AEAD mode
      mov         DWORD(offset), 1
      vmovdqa     LFSR_B_LDQ, [rel aead_lsfr_b_lo]
      jmp         snow_v_common_init

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

MKGLOBAL(SNOW_V,function,)
SNOW_V:
      endbranch64
      ;; use offset to indicate AEAD mode
      xor         DWORD(offset), DWORD(offset)
      vpxor       LFSR_B_LDQ, LFSR_B_LDQ, LFSR_B_LDQ

snow_v_common_init:

      ;; Init LSFR
      mov         rax, [job + _enc_keys]
      vmovdqu     LFSR_A_HDQ, [rax]
      vmovdqu     LFSR_B_HDQ, [rax + 16]
      mov         rax, [job + _iv]
      vmovdqu     LFSR_A_LDQ, [rax]

      ;; Init FSM: R1 = R2 = R3 = 0
      vpxor       FSM_R1, FSM_R1, FSM_R1
      vpxor       FSM_R2, FSM_R2, FSM_R2
      vpxor       FSM_R3, FSM_R3, FSM_R3

      vmovdqa     gA, [rel alpha]
      vmovdqa     gB, [rel beta]
      vmovdqa     inv_gA, [rel alpha_inv]
      vmovdqa     inv_gB, [rel beta_inv]

      mov   eax, 15

init_fsm_lfsr_loop:

      SNOW_V_KEYSTREAM   KEYSTREAM, LFSR_B_HDQ, FSM_R1, FSM_R2
      SNOW_V_FSM_UPDATE  FSM_R1, FSM_R2, FSM_R3, LFSR_A_LDQ, temp1, temp2
      SNOW_V_LFSR_UPDATE LFSR_A_LDQ, LFSR_A_HDQ, LFSR_B_LDQ, LFSR_B_HDQ, \
                         inv_gB, temp1, temp2, temp3, gA, gB, inv_gA
      vpxor LFSR_A_HDQ, LFSR_A_HDQ, KEYSTREAM
      dec         eax
      jnz         init_fsm_lfsr_loop

      mov         rax, [job + _enc_keys]
      vmovdqu     temp4, [rax]
      vpxor       FSM_R1, FSM_R1, temp4

      SNOW_V_KEYSTREAM   KEYSTREAM, LFSR_B_HDQ, FSM_R1, FSM_R2
      SNOW_V_FSM_UPDATE  FSM_R1, FSM_R2, FSM_R3, LFSR_A_LDQ, temp1, temp2
      SNOW_V_LFSR_UPDATE LFSR_A_LDQ, LFSR_A_HDQ,LFSR_B_LDQ, LFSR_B_HDQ, \
                         inv_gB, temp1, temp2, temp3, gA, gB, inv_gA
      vpxor       LFSR_A_HDQ, LFSR_A_HDQ, KEYSTREAM
      vmovdqu     temp4, [rax + 16]
      vpxor       FSM_R1, FSM_R1, temp4

      ;; At this point FSM and LSFR are initialized

      or          DWORD(offset), DWORD(offset)
      jz          no_aead

      ;; in AEAD mode hkey = keystream_0 and endpad = keystream_1
      mov         r11,    [job + _snow_v_reserved]

      ;; generate hkey
      SNOW_V_KEYSTREAM KEYSTREAM, LFSR_B_HDQ, FSM_R1, FSM_R2
      vmovdqu     [r11], KEYSTREAM

      ;; generate endpad
      SNOW_V_FSM_UPDATE  FSM_R1, FSM_R2, FSM_R3, LFSR_A_LDQ, temp1, temp2
      SNOW_V_LFSR_UPDATE LFSR_A_LDQ, LFSR_A_HDQ,LFSR_B_LDQ, LFSR_B_HDQ, \
                         inv_gB, temp1, temp2, temp3, gA, gB, inv_gA
      SNOW_V_KEYSTREAM   KEYSTREAM, LFSR_B_HDQ, FSM_R1, FSM_R2

      SNOW_V_FSM_UPDATE  FSM_R1, FSM_R2, FSM_R3, LFSR_A_LDQ, temp1, temp2
      SNOW_V_LFSR_UPDATE LFSR_A_LDQ, LFSR_A_HDQ,LFSR_B_LDQ, LFSR_B_HDQ, \
                         inv_gB, temp1, temp2, temp3, gA, gB, inv_gA

      mov         offset, [r11 + 24]
      vmovdqu     [r11 + 16], KEYSTREAM
      or          offset, offset
      ;; if last 8 bytes endpad are not 0 skip encrypt/decrypt operation
      ;; option used to calculate auth tag for decrypt and not overwrite
      ;; cipher by plain when the same src/dst pointer is used
      jnz         no_partial_block_left

no_aead:

      ;; Process input
      mov         r10, [job + _src]
      add         r10, [job + _cipher_start_src_offset_in_bytes]
      mov         r11, [job + _dst]
      mov         rax, [job + _msg_len_to_cipher_in_bytes]
      xor         offset, offset
      ;; deal with partial block less than 16b outside main loop
      and         rax, 0xfffffffffffffff0
      jz          final_bytes

encrypt_loop:

      vmovdqu     temp4, [r10 + offset]

      SNOW_V_KEYSTREAM   KEYSTREAM, LFSR_B_HDQ, FSM_R1, FSM_R2
      SNOW_V_FSM_UPDATE  FSM_R1, FSM_R2, FSM_R3, LFSR_A_LDQ, temp1, temp2
      SNOW_V_LFSR_UPDATE LFSR_A_LDQ, LFSR_A_HDQ, LFSR_B_LDQ, LFSR_B_HDQ, \
                         inv_gB, temp1, temp2, temp3, gA, gB, inv_gA

      vpxor       temp4, temp4, KEYSTREAM
      vmovdqu     [r11 + offset], temp4
      add         offset, 16
      sub         rax, 16
      jnz         encrypt_loop

final_bytes:
      mov         rax, [job + _msg_len_to_cipher_in_bytes]
      and         rax, 0xf
      jz          no_partial_block_left

      ;; load partial block into XMM register
      add         r10, offset
      simd_load_avx_15_1 temp4, r10, rax
      SNOW_V_KEYSTREAM KEYSTREAM, LFSR_B_HDQ, FSM_R1, FSM_R2
      vpxor       temp4, temp4, KEYSTREAM
      add         r11, offset

      ;; use r10 and offset as temp [clobbered]
      simd_store_avx_15 r11, temp4, rax, r10, offset

no_partial_block_left:
      ;; Clear registers and return data
%ifdef SAFE_DATA
      clear_scratch_xmms_avx_asm
%endif

      mov         rax, job
      or          dword [rax + _status], IMB_STATUS_COMPLETED_CIPHER

      ret

mksection stack-noexec

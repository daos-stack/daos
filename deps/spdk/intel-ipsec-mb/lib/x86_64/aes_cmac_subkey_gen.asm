;;
;; Copyright (c) 2018-2021, Intel Corporation
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
%define NO_AESNI_RENAME
%include "include/aesni_emu.inc"
%include "include/clear_regs.asm"
%include "include/cet.inc"
%include "include/error.inc"
;;; Routines to generate subkeys for AES-CMAC.
;;; See RFC 4493 for more details.

;; In System V AMD64 ABI
;;      callee saves: RBX, RBP, R12-R15
;; Windows x64 ABI
;;      callee saves: RBX, RBP, RDI, RSI, RSP, R12-R15
;;
;; Registers:           RAX RBX RCX RDX RBP RSI RDI R8  R9  R10 R11 R12 R13 R14 R15
;;                      -----------------------------------------------------------
;; Windows clobbers:
;; Windows preserves:   RAX RBX RCX RDX RBP RSI RDI R8  R9  R10 R11 R12 R13 R14 R15
;;                      -----------------------------------------------------------
;; Linux clobbers:
;; Linux preserves:     RAX RBX RCX RDX RBP RSI RDI R8  R9  R10 R11 R12 R13 R14 R15
;;                      -----------------------------------------------------------
;;
;; Linux/Windows clobbers: xmm0, xmm1, xmm2
;;

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
%define arg5    [rsp + 5*8]
%endif

%define KEY_EXP arg1
%define KEY1    arg2
%define KEY2    arg3

%define XL      xmm0
%define XKEY1   xmm1
%define XKEY2   xmm2

mksection .rodata
default rel

align 16
xmm_bit127:
        ;ddq 0x80000000000000000000000000000000
        dq 0x0000000000000000, 0x8000000000000000

align 16
xmm_bit63:
        ;ddq 0x00000000000000008000000000000000
        dq 0x8000000000000000, 0x0000000000000000

align 16
xmm_bit64:
        ;ddq 0x00000000000000010000000000000000
        dq 0x0000000000000000, 0x0000000000000001

align 16
const_Rb:
        ;ddq 0x00000000000000000000000000000087
        dq 0x0000000000000087, 0x0000000000000000

align 16
byteswap_const:
        ;DDQ 0x000102030405060708090A0B0C0D0E0F
        dq 0x08090A0B0C0D0E0F, 0x0001020304050607

mksection .text

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; void aes_cmac_subkey_gen(const void *key_exp, void *key1, void *key2)
;;;
;;; key_exp : IN  : address of expanded encryption key structure
;;; key1    : OUT : address to store subkey 1 (16 bytes)
;;; key2    : OUT : address to store subkey 2 (16 bytes)
;;;
;;; RFC 4493 Figure 2.2 describing function operations at highlevel
;;;
;;; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;;; +                    Algorithm Generate_Subkey                       +
;;; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;;; +                                                                    +
;;; +   Input    : K  (128/256-bit key)                                  +
;;; +   Output   : K1 (128-bit first subkey)                             +
;;; +              K2 (128-bit second subkey)                            +
;;; +--------------------------------------------------------------------+
;;; +                                                                    +
;;; +   Constants: const_Zero is 0x00000000000000000000000000000000      +
;;; +              const_Rb   is 0x00000000000000000000000000000087      +
;;; +   Variables: L          for output of AES-128/256 applied to 0^128 +
;;; +                                                                    +
;;; +   Step 1.  L := AES-128/256(K, const_Zero) ;                       +
;;; +   Step 2.  if MSB(L) is equal to 0                                 +
;;; +            then    K1 := L << 1 ;                                  +
;;; +            else    K1 := (L << 1) XOR const_Rb ;                   +
;;; +   Step 3.  if MSB(K1) is equal to 0                                +
;;; +            then    K2 := K1 << 1 ;                                 +
;;; +            else    K2 := (K1 << 1) XOR const_Rb ;                  +
;;; +   Step 4.  return K1, K2                        ;                  +
;;; +                                                                    +
;;; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

%macro AES_CMAC_SUBKEY_GEN_SSE 1-2
%define %%NROUNDS       %1
%define %%ARCH          %2

%ifdef SAFE_PARAM
        IMB_ERR_CHECK_RESET

        cmp     KEY_EXP, 0
        jz      %%_cmac_subkey_error
        cmp     KEY1, 0
        jz      %%_cmac_subkey_error
        cmp     KEY2, 0
        jz      %%_cmac_subkey_error
        jmp     %%_cmac_subkey_no_error
%%_cmac_subkey_error:
        IMB_ERR_CHECK_START rax
        IMB_ERR_CHECK_NULL KEY_EXP, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_NULL KEY1, rax, IMB_ERR_NULL_KEY
        IMB_ERR_CHECK_NULL KEY2, rax, IMB_ERR_NULL_KEY
        IMB_ERR_CHECK_END rax

        jmp %%_aes_cmac_subkey_gen_sse_return

%%_cmac_subkey_no_error:
%endif

%ifidn %%ARCH, no_aesni
%define AESENC          EMULATE_AESENC
%define AESENCLAST      EMULATE_AESENCLAST
%else
%define AESENC          aesenc
%define AESENCLAST      aesenclast
%endif

        ;; Step 1.  L := AES-128(K, const_Zero) ;
        movdqa          XL, [KEY_EXP + 16*0]    ; 0. ARK xor const_Zero
        AESENC          XL, [KEY_EXP + 16*1]    ; 1. ENC
        AESENC          XL, [KEY_EXP + 16*2]    ; 2. ENC
        AESENC          XL, [KEY_EXP + 16*3]    ; 3. ENC
        AESENC          XL, [KEY_EXP + 16*4]    ; 4. ENC
        AESENC          XL, [KEY_EXP + 16*5]    ; 5. ENC
        AESENC          XL, [KEY_EXP + 16*6]    ; 6. ENC
        AESENC          XL, [KEY_EXP + 16*7]    ; 7. ENC
        AESENC          XL, [KEY_EXP + 16*8]    ; 8. ENC
        AESENC          XL, [KEY_EXP + 16*9]    ; 9. ENC
%if %%NROUNDS == 13     ;; CMAC-256
        AESENC          XL, [KEY_EXP + 16*10]   ; 10. ENC
        AESENC          XL, [KEY_EXP + 16*11]   ; 11. ENC
        AESENC          XL, [KEY_EXP + 16*12]   ; 12. ENC
        AESENC          XL, [KEY_EXP + 16*13]   ; 13. ENC
        AESENCLAST      XL, [KEY_EXP + 16*14]   ; 14. ENC
%else                   ;; CMAC-128
        AESENCLAST      XL, [KEY_EXP + 16*10]   ; 10. ENC
%endif

        ;; Step 2.  if MSB(L) is equal to 0
        ;;          then    K1 := L << 1 ;
        ;;          else    K1 := (L << 1) XOR const_Rb ;
        pshufb          XL, [rel byteswap_const]
        movdqa          XKEY1, XL
        psllq           XKEY1, 1
        ptest           XL, [rel xmm_bit63]
        jz              %%_K1_no_carry_bit_sse
        ;; set carry bit
        por             XKEY1, [rel xmm_bit64]
%%_K1_no_carry_bit_sse:
        ptest           XL, [rel xmm_bit127]
        jz              %%_K1_msb_is_zero_sse
        ;; XOR const_Rb
        pxor            XKEY1, [rel const_Rb]
%%_K1_msb_is_zero_sse:

        ;; Step 3.  if MSB(K1) is equal to 0
        ;;          then    K2 := K1 << 1 ;
        ;;          else    K2 := (K1 << 1) XOR const_Rb ;
        movdqa          XKEY2, XKEY1
        psllq           XKEY2, 1
        ptest           XKEY1, [rel xmm_bit63]
        jz              %%_K2_no_carry_bit_sse
        ;; set carry bit
        por             XKEY2, [rel xmm_bit64]
%%_K2_no_carry_bit_sse:
        ptest           XKEY1, [rel xmm_bit127]
        jz              %%_K2_msb_is_zero_sse
        ;; XOR const_Rb
        pxor            XKEY2, [rel const_Rb]
%%_K2_msb_is_zero_sse:

        ;; Step 4.  return K1, K2
        pshufb          XKEY1, [rel byteswap_const]
        pshufb          XKEY2, [rel byteswap_const]
        movdqu          [KEY1], XKEY1
        movdqu          [KEY2], XKEY2

%%_aes_cmac_subkey_gen_sse_return:

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_sse_asm
%endif
%endmacro

%macro AES_CMAC_SUBKEY_GEN_AVX 1
%define %%NROUNDS       %1

%ifdef SAFE_PARAM
        IMB_ERR_CHECK_RESET

        cmp     KEY_EXP, 0
        jz      %%_cmac_subkey_error_avx
        cmp     KEY1, 0
        jz      %%_cmac_subkey_error_avx
        cmp     KEY2, 0
        jz      %%_cmac_subkey_error_avx

        jmp     %%_cmac_subkey_no_error_avx
%%_cmac_subkey_error_avx:
        IMB_ERR_CHECK_START rax
        IMB_ERR_CHECK_NULL KEY_EXP, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_NULL KEY1, rax, IMB_ERR_NULL_KEY
        IMB_ERR_CHECK_NULL KEY2, rax, IMB_ERR_NULL_KEY
        IMB_ERR_CHECK_END rax

        jmp     %%_aes_cmac_subkey_gen_avx_return

%%_cmac_subkey_no_error_avx:
%endif
        ;; Step 1.  L := AES-128(K, const_Zero) ;
        vmovdqa         XL, [KEY_EXP + 16*0]        ; 0. ARK xor const_Zero
        vaesenc         XL, [KEY_EXP + 16*1]        ; 1. ENC
        vaesenc         XL, [KEY_EXP + 16*2]        ; 2. ENC
        vaesenc         XL, [KEY_EXP + 16*3]        ; 3. ENC
        vaesenc         XL, [KEY_EXP + 16*4]        ; 4. ENC
        vaesenc         XL, [KEY_EXP + 16*5]        ; 5. ENC
        vaesenc         XL, [KEY_EXP + 16*6]        ; 6. ENC
        vaesenc         XL, [KEY_EXP + 16*7]        ; 7. ENC
        vaesenc         XL, [KEY_EXP + 16*8]        ; 8. ENC
        vaesenc         XL, [KEY_EXP + 16*9]        ; 9. ENC
%if %%NROUNDS == 13     ;; CMAC-256
        vaesenc         XL, [KEY_EXP + 16*10]       ; 10. ENC
        vaesenc         XL, [KEY_EXP + 16*11]       ; 11. ENC
        vaesenc         XL, [KEY_EXP + 16*12]       ; 12. ENC
        vaesenc         XL, [KEY_EXP + 16*13]       ; 13. ENC
        vaesenclast     XL, [KEY_EXP + 16*14]       ; 14. ENC
%else                   ;; CMAC-128
        vaesenclast     XL, [KEY_EXP + 16*10]       ; 10. ENC
%endif

        ;; Step 2.  if MSB(L) is equal to 0
        ;;          then    K1 := L << 1 ;
        ;;          else    K1 := (L << 1) XOR const_Rb ;
        vpshufb         XL, [rel byteswap_const]
        vmovdqa         XKEY1, XL
        vpsllq          XKEY1, 1
        vptest          XL, [rel xmm_bit63]
        jz              %%_K1_no_carry_bit_avx
        ;; set carry bit
        vpor            XKEY1, [rel xmm_bit64]
%%_K1_no_carry_bit_avx:
        vptest          XL, [rel xmm_bit127]
        jz              %%_K1_msb_is_zero_avx
        ;; XOR const_Rb
        vpxor           XKEY1, [rel const_Rb]
%%_K1_msb_is_zero_avx:

        ;; Step 3.  if MSB(K1) is equal to 0
        ;;          then    K2 := K1 << 1 ;
        ;;          else    K2 := (K1 << 1) XOR const_Rb ;
        vmovdqa         XKEY2, XKEY1
        vpsllq          XKEY2, 1
        vptest          XKEY1, [rel xmm_bit63]
        jz              %%_K2_no_carry_bit_avx
        ;; set carry bit
        vpor            XKEY2, [rel xmm_bit64]
%%_K2_no_carry_bit_avx:
        vptest          XKEY1, [rel xmm_bit127]
        jz              %%_K2_msb_is_zero_avx
        ;; XOR const_Rb
        vpxor           XKEY2, [rel const_Rb]
%%_K2_msb_is_zero_avx:

        ;; Step 4.  return K1, K2
        vpshufb         XKEY1, [rel byteswap_const]
        vpshufb         XKEY2, [rel byteswap_const]
        vmovdqu         [KEY1], XKEY1
        vmovdqu         [KEY2], XKEY2

%%_aes_cmac_subkey_gen_avx_return:

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_avx_asm
%endif
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; void aes_cmac_subkey_gen_sse(const void *key_exp, void *key1, void *key2)
;;;
;;; key_exp : IN  : address of expanded encryption key structure (AES 128)
;;; key1    : OUT : address to store subkey 1 (AES128 - 16 bytes)
;;; key2    : OUT : address to store subkey 2 (AES128 - 16 bytes)
;;;
;;; See aes_cmac_subkey_gen() above for operation details
MKGLOBAL(aes_cmac_subkey_gen_sse,function,)
align 32
aes_cmac_subkey_gen_sse:
        endbranch64
        AES_CMAC_SUBKEY_GEN_SSE 9
        ret

MKGLOBAL(aes_cmac_subkey_gen_sse_no_aesni,function,)
align 32
aes_cmac_subkey_gen_sse_no_aesni:
        endbranch64
        AES_CMAC_SUBKEY_GEN_SSE 9, no_aesni
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; void aes_cmac_256_subkey_gen_sse(const void *key_exp,
;;;                                  void *key1,
;;;                                  void *key2)
;;;
;;; key_exp : IN  : address of expanded encryption key structure (AES 256)
;;; key1    : OUT : address to store subkey 1 (AES256 - 16 bytes)
;;; key2    : OUT : address to store subkey 2 (AES256 - 16 bytes)
;;;
;;; See aes_cmac_subkey_gen() above for operation details
MKGLOBAL(aes_cmac_256_subkey_gen_sse,function,)
align 32
aes_cmac_256_subkey_gen_sse:
        endbranch64
        AES_CMAC_SUBKEY_GEN_SSE 13
        ret

MKGLOBAL(aes_cmac_256_subkey_gen_sse_no_aesni,function,)
align 32
aes_cmac_256_subkey_gen_sse_no_aesni:
        endbranch64
        AES_CMAC_SUBKEY_GEN_SSE 13, no_aesni
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; void aes_cmac_subkey_gen_avx(const void *key_exp, void *key1, void *key2)
;;;
;;; key_exp : IN  : address of expanded encryption key structure (AES 128)
;;; key1    : OUT : address to store subkey 1 (AES128 - 16 bytes)
;;; key2    : OUT : address to store subkey 2 (AES128 - 16 bytes)
;;;
;;; See aes_cmac_subkey_gen() above for operation details
MKGLOBAL(aes_cmac_subkey_gen_avx,function,)
MKGLOBAL(aes_cmac_subkey_gen_avx2,function,)
MKGLOBAL(aes_cmac_subkey_gen_avx512,function,)
align 32
aes_cmac_subkey_gen_avx:
aes_cmac_subkey_gen_avx2:
aes_cmac_subkey_gen_avx512:
        endbranch64
        AES_CMAC_SUBKEY_GEN_AVX 9
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; void aes_cmac_256_subkey_gen_avx(const void *key_exp,
;;;                                  void *key1,
;;;                                  void *key2)
;;;
;;; key_exp : IN  : address of expanded encryption key structure (AES 256)
;;; key1    : OUT : address to store subkey 1 (AES256 - 16 bytes)
;;; key2    : OUT : address to store subkey 2 (AES256 - 16 bytes)
;;;
;;; See aes_cmac_subkey_gen() above for operation details
MKGLOBAL(aes_cmac_256_subkey_gen_avx,function,)
MKGLOBAL(aes_cmac_256_subkey_gen_avx2,function,)
MKGLOBAL(aes_cmac_256_subkey_gen_avx512,function,)
align 32
aes_cmac_256_subkey_gen_avx:
aes_cmac_256_subkey_gen_avx2:
aes_cmac_256_subkey_gen_avx512:
        endbranch64
        AES_CMAC_SUBKEY_GEN_AVX 13
        ret

mksection stack-noexec

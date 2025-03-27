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

mksection .rodata
default rel
EK_d:
dw	0x44D7, 0x26BC, 0x626B, 0x135E, 0x5789, 0x35E2, 0x7135, 0x09AF,
dw	0x4D78, 0x2F13, 0x6BC4, 0x1AF1, 0x5E26, 0x3C4D, 0x789A, 0x47AC

align 16
mask_S0:
dq      0xff00ff00ff00ff00

align 16
mask_S1:
dq      0x00ff00ff00ff00ff

mksection stack-noexec

mksection .text

%define OFFSET_FR1      (16*4)
%define OFFSET_FR2      (17*4)
%define OFFSET_BRC_X0   (18*4)
%define OFFSET_BRC_X1   (19*4)
%define OFFSET_BRC_X2   (20*4)
%define OFFSET_BRC_X3   (21*4)

;
;   BITS_REORG()
;
;   params
;       %1 - round number
;   uses
;       eax, ebx, ecx, edx
;   return
;       updates r12d, r13d, r14d, r15d
;
%macro  BITS_REORG  1
    ;
    ; r12d = LFSR_S15
    ; eax  = LFSR_S14
    ; r13d = LFSR_S11
    ; ebx  = LFSR_S9
    ; r14d = LFSR_S7
    ; ecx  = LFSR_S5
    ; r15d = LFSR_S2
    ; edx  = LFSR_S0

    mov         r12d, [rsi + ((15 + %1) % 16)*4]
    mov          eax, [rsi + ((14 + %1) % 16)*4]
    mov         r13d, [rsi + ((11 + %1) % 16)*4]
    mov          ebx, [rsi + (( 9 + %1) % 16)*4]
    mov         r14d, [rsi + (( 7 + %1) % 16)*4]
    mov          ecx, [rsi + (( 5 + %1) % 16)*4]
    mov         r15d, [rsi + (( 2 + %1) % 16)*4]
    mov          edx, [rsi + (( 0 + %1) % 16)*4]

    shr         r12d, 15
    shl         eax, 16
    shl         ebx, 1
    shl         ecx, 1
    shl         edx, 1
    shld        r12d, eax, 16   ; BRC_X0
    shld        r13d, ebx, 16   ; BRC_X1
    shld        r14d, ecx, 16   ; BRC_X2
    shld        r15d, edx, 16   ; BRC_X3
%endmacro

;
;   NONLIN_FUN()
;
;   uses
;           rdi rsi eax rdx edx
;           r8d r9d ebx
;   return
;       eax  = W value
;       r10d = F_R1
;       r11d = F_R2
;
%macro NONLIN_FUN   2
%define %%CALC_W %1 ; [in] Calculate W if 1
%define %%ARCH   %2 ; [in] SSE/SSE_NO_AESNI/AVX

%if (%%CALC_W == 1)
    mov         eax, r12d
    xor         eax, r10d
    add         eax, r11d   ; W = (BRC_X0 ^ F_R1) + F_R2
%endif

    add         r10d, r13d  ; W1= F_R1 + BRC_X1
    xor         r11d, r14d  ; W2= F_R2 ^ BRC_X2

    mov         rdx, r10
    shld        edx, r11d, 16   ; P = (W1 << 16) | (W2 >> 16)
    shld        r11d, r10d, 16  ; Q = (W2 << 16) | (W1 >> 16)

    mov         ebx, edx
    mov         ecx, edx
    mov         r8d, edx
    mov         r9d, edx

    rol         ebx, 2
    rol         ecx, 10
    rol         r8d, 18
    rol         r9d, 24
    xor         edx, ebx
    xor         edx, ecx
    xor         edx, r8d
    xor         edx, r9d    ; U = L1(P) = EDX, hi(RDX)=0

    mov         ebx, r11d
    mov         ecx, r11d
    mov         r8d, r11d
    mov         r9d, r11d
    rol         ebx, 8
    rol         ecx, 14
    rol         r8d, 22
    rol         r9d, 30
    xor         r11d, ebx
    xor         r11d, ecx
    xor         r11d, r8d
    xor         r11d, r9d   ; V = L2(Q) = R11D, hi(R11)=0

    shl         r11, 32
    xor         rdx, r11 ; V || U
%ifidn %%ARCH, SSE
    movq        xmm0, rdx
    movdqa      xmm1, xmm0
    S0_comput_SSE xmm1, xmm2, xmm3, 0
    S1_comput_SSE xmm0, xmm2, xmm3, xmm4, 0

    pand        xmm0, [rel mask_S1]
    pand        xmm1, [rel mask_S0]

    pxor        xmm0, xmm1
    movd        r10d, xmm0      ; F_R1
    pextrd      r11d, xmm0, 1   ; F_R2
%elifidn %%ARCH, SSE_NO_AESNI
    movq        xmm0, rdx
    movdqa      xmm1, xmm0
    S0_comput_SSE xmm1, xmm2, xmm3, 0
    S1_comput_SSE_NO_AESNI xmm0, xmm2, xmm3, xmm4

    pand        xmm0, [rel mask_S1]
    pand        xmm1, [rel mask_S0]

    pxor        xmm0, xmm1
    movd        r10d, xmm0      ; F_R1
    pextrd      r11d, xmm0, 1   ; F_R2
%else
    vmovq       xmm0, rdx
    vmovdqa     xmm1, xmm0
    S0_comput_AVX xmm1, xmm2, xmm3
    S1_comput_AVX xmm0, xmm2, xmm3, xmm4
    vpand        xmm0, [rel mask_S1]
    vpand        xmm1, [rel mask_S0]

    vpxor       xmm0, xmm0, xmm1
    vmovd       r10d, xmm0      ; F_R1
    vpextrd     r11d, xmm0, 1   ; F_R2

%endif

%endmacro

;
;   LFSR_UPDT()
;
;   params
;       %1 - round number
;   uses
;       rax as input (ZERO or W)
;   return
;
%macro  LFSR_UPDT   1
    ;
    ; ebx = LFSR_S0
    ; ecx = LFSR_S4
    ; edx = LFSR_S10
    ; r8d = LFSR_S13
    ; r9d = LFSR_S15
    ;lea         rsi, [LFSR_STA] ; moved to calling function

    mov         ebx, [rsi + (( 0 + %1) % 16)*4]
    mov         ecx, [rsi + (( 4 + %1) % 16)*4]
    mov         edx, [rsi + ((10 + %1) % 16)*4]
    mov         r8d, [rsi + ((13 + %1) % 16)*4]
    mov         r9d, [rsi + ((15 + %1) % 16)*4]

    ; Calculate 64-bit LFSR feedback
    add         rax, rbx
    shl         rbx, 8
    shl         rcx, 20
    shl         rdx, 21
    shl         r8, 17
    shl         r9, 15
    add         rax, rbx
    add         rax, rcx
    add         rax, rdx
    add         rax, r8
    add         rax, r9

    ; Reduce it to 31-bit value
    mov         rbx, rax
    and         rax, 0x7FFFFFFF
    shr         rbx, 31
    add         rax, rbx

    mov rbx, rax
    sub rbx, 0x7FFFFFFF
    cmovns rax, rbx

    ; LFSR_S16 = (LFSR_S15++) = eax
    mov         [rsi + (( 0 + %1) % 16)*4], eax
%endmacro

;
;   make_u31()
;
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
;	key_expand()
;
%macro	key_expand	1
	movzx		r8d, byte [pKe +  (%1 + 0)]
	movzx		r9d, word [rbx + ((%1 + 0)*2)]
	movzx		r10d, byte [pIv + (%1 + 0)]
	make_u31	r11d, r8d, r9d, r10d
	mov 		[rax +  ((%1 + 0)*4)], r11d

	movzx		r12d, byte [pKe +  (%1 + 1)]
	movzx		r13d, word [rbx + ((%1 + 1)*2)]
	movzx		r14d, byte [pIv +  (%1 + 1)]
	make_u31	r15d, r12d, r13d, r14d
	mov 		[rax +  ((%1 + 1)*4)], r15d
%endmacro

;
; Initialize internal LFSR
;
%macro ZUC_INIT 1
%define %%ARCH  %1 ; [in] SSE/SSE_NO_AESNI/AVX

%ifdef LINUX
	%define		pKe	rdi
	%define		pIv	rsi
	%define		pState	rdx
%else
	%define		pKe	rcx
	%define		pIv	rdx
	%define		pState	r8
%endif

    ; save the base pointer
    push rbp

    ;load stack pointer to rbp and reserve memory in the red zone
    mov rbp, rsp
    sub rsp, 64

    ; Save non-volatile registers
    mov [rbp - 8],  rbx
    mov [rbp - 16], r12
    mov [rbp - 24], r13
    mov [rbp - 32], r14
    mov [rbp - 40], r15
%ifndef LINUX
    mov [rbp - 48], rdi
    mov [rbp - 56], rsi
%endif

    lea rbx, [rel EK_d]     ; load pointer to D
    lea rax, [pState]      ; load pointer to pState
    mov [rbp - 64], pState   ; save pointer to pState

    ; Expand key
    key_expand  0
    key_expand  2
    key_expand  4
    key_expand  6
    key_expand  8
    key_expand  10
    key_expand  12
    key_expand  14

    ; Set R1 and R2 to zero
    xor         r10, r10
    xor         r11, r11

    ; Shift LFSR 32-times, update state variables
%assign N 0
%rep 32
    mov rdx, [rbp - 64]   ; load pointer to pState
    lea rsi, [rdx]

    BITS_REORG  N

    NONLIN_FUN  1, %%ARCH
    shr         eax, 1

    mov rdx, [rbp - 64]   ; re-load pointer to pState
    lea rsi, [rdx]

    LFSR_UPDT   N

%assign N N+1
%endrep

    ; And once more, initial round from keygen phase = 33 times
    mov         rdx, [rbp - 64]   ; load pointer to pState
    lea         rsi, [rdx]

    BITS_REORG  0
    NONLIN_FUN  0, %%ARCH
    xor         rax, rax

    mov         rdx, [rbp - 64]   ; load pointer to pState
    lea         rsi, [rdx]

    LFSR_UPDT   0

    mov         rdx, [rbp - 64]   ; load pointer to pState
    lea         rsi, [rdx]

    ; Save ZUC's state variables
    mov         [rsi + (16*4)],r10d  ;F_R1
    mov         [rsi + (17*4)],r11d  ;F_R2
    mov         [rsi + (18*4)],r12d  ;BRC_X0
    mov         [rsi + (19*4)],r13d  ;BRC_X1
    mov         [rsi + (20*4)],r14d  ;BRC_X2
    mov         [rsi + (21*4)],r15d  ;BRC_X3

    ; Restore non-volatile registers
    mov rbx, [rbp - 8]
    mov r12, [rbp - 16]
    mov r13, [rbp - 24]
    mov r14, [rbp - 32]
    mov r15, [rbp - 40]
%ifndef LINUX
    mov rdi, [rbp - 48]
    mov rsi, [rbp - 56]
%endif

    ; restore base pointer
    mov rsp, rbp
    pop rbp

%endmacro

;
; Generate N*4 bytes of keystream
; for a single buffer (where N is number of rounds)
;
%macro ZUC_KEYGEN 2
%define %%ARCH          %1 ; [in] SSE/SSE_NO_AESNI/AVX
%define %%NUM_ROUNDS    %2 ; [in] Number of 4-byte rounds

%ifdef LINUX
	%define		pKS	rdi
	%define		pState	rsi
%else
	%define		pKS	rcx
	%define		pState	rdx
%endif

%ifidn %%ARCH, AVX
%define %%MOVDQA vmovdqa
%else
%define %%MOVDQA movdqa
%endif

    ; save the base pointer
    push rbp

    ;load stack pointer to rbp and reserve memory in the red zone
    mov rbp, rsp
    sub rsp, 72

    ; Save non-volatile registers
    mov [rbp - 8], rbx
    mov [rbp - 16], r12
    mov [rbp - 24], r13
    mov [rbp - 32], r14
    mov [rbp - 40], r15
%ifndef LINUX
    mov [rbp - 48], rdi
    mov [rbp - 56], rsi
%endif

    ; Load input keystream pointer parameter in RAX
    mov         rax, pKS

    ; Restore ZUC's state variables
    mov         r10d, [pState + OFFSET_FR1]
    mov         r11d, [pState + OFFSET_FR2]
    mov         r12d, [pState + OFFSET_BRC_X0]
    mov         r13d, [pState + OFFSET_BRC_X1]
    mov         r14d, [pState + OFFSET_BRC_X2]
    mov         r15d, [pState + OFFSET_BRC_X3]

    ; Store keystream pointer
    mov [rbp - 64], rax

    ; Store ZUC State Pointer
    mov [rbp - 72], pState

    ; Generate N*4B of keystream in N rounds
%assign N 1
%rep %%NUM_ROUNDS

    mov rdx, [rbp - 72]       ; load *pState
    lea rsi, [rdx]

    BITS_REORG  N
    NONLIN_FUN  1, %%ARCH

    ;Store the keystream
    mov rbx, [rbp - 64]  ; load *pkeystream
    xor eax, r15d
    mov [rbx], eax
    add rbx, 4          ; increment the pointer
    mov [rbp - 64], rbx   ; save pkeystream

    xor         rax, rax

    mov rdx, [rbp - 72]     ; load *pState
    lea rsi, [rdx]

    LFSR_UPDT   N

%assign N N+1
%endrep

;; Reorder LFSR registers, as not all 16 rounds have been completed
;; (if number of rounds is not 4, 8 or 16, the only possible case is 2,
;; and in that case, we don't have to update the states, as that function
;; call is done at the end the algorithm).
%if (%%NUM_ROUNDS == 8)
    %%MOVDQA xmm0, [rsi]
    %%MOVDQA xmm1, [rsi+16]
    %%MOVDQA xmm2, [rsi+32]
    %%MOVDQA xmm3, [rsi+48]

    %%MOVDQA [rsi],    xmm2
    %%MOVDQA [rsi+16], xmm3
    %%MOVDQA [rsi+32], xmm0
    %%MOVDQA [rsi+48], xmm1
%elif (%%NUM_ROUNDS == 4)
    %%MOVDQA xmm0, [rsi]
    %%MOVDQA xmm1, [rsi+16]
    %%MOVDQA xmm2, [rsi+32]
    %%MOVDQA xmm3, [rsi+48]

    %%MOVDQA [rsi],    xmm1
    %%MOVDQA [rsi+16], xmm2
    %%MOVDQA [rsi+32], xmm3
    %%MOVDQA [rsi+48], xmm0
%endif

    mov rsi, [rbp - 72]   ; load pState

    ; Save ZUC's state variables
    mov         [rsi + OFFSET_FR1], r10d
    mov         [rsi + OFFSET_FR2], r11d
    mov         [rsi + OFFSET_BRC_X0], r12d
    mov         [rsi + OFFSET_BRC_X1], r13d
    mov         [rsi + OFFSET_BRC_X2], r14d
    mov         [rsi + OFFSET_BRC_X3], r15d

    ; Restore non-volatile registers
    mov rbx, [rbp - 8]
    mov r12, [rbp - 16]
    mov r13, [rbp - 24]
    mov r14, [rbp - 32]
    mov r15, [rbp - 40]
%ifndef LINUX
    mov rdi, [rbp - 48]
    mov rsi, [rbp - 56]
%endif

    mov rsp, rbp
    pop rbp

%endmacro

;
; Generate N*4 bytes of keystream for a single buffer
; (where N is number of rounds, being 16 rounds the maximum)
;
%macro ZUC_KEYGEN_VAR 1
%define %%ARCH          %1 ; [in] SSE/SSE_NO_AESNI/AVX

%ifdef LINUX
	%define		pKS	rdi
	%define		pState	rsi
        %define         nRounds rdx
%else
	%define		pKS	rcx
	%define		pState	rdx
        %define         nRounds r8
%endif

%define MAX_ROUNDS 16
    ; save the base pointer
    push rbp

    ;load stack pointer to rbp and reserve memory in the red zone
    mov rbp, rsp
    sub rsp, 80

    ; Save non-volatile registers
    mov [rbp - 8], rbx
    mov [rbp - 16], r12
    mov [rbp - 24], r13
    mov [rbp - 32], r14
    mov [rbp - 40], r15
%ifndef LINUX
    mov [rbp - 48], rdi
    mov [rbp - 56], rsi
%endif

    mov [rbp - 80], nRounds

    ; Load input keystream pointer parameter in RAX
    mov         rax, pKS

    ; Restore ZUC's state variables
    mov         r10d, [pState + OFFSET_FR1]
    mov         r11d, [pState + OFFSET_FR2]
    mov         r12d, [pState + OFFSET_BRC_X0]
    mov         r13d, [pState + OFFSET_BRC_X1]
    mov         r14d, [pState + OFFSET_BRC_X2]
    mov         r15d, [pState + OFFSET_BRC_X3]

    ; Store keystream pointer
    mov [rbp - 64], rax

    ; Store ZUC State Pointer
    mov [rbp - 72], pState

    ; Generate N*4B of keystream in N rounds
%assign N 1
%rep MAX_ROUNDS

    mov rdx, [rbp - 72]       ; load *pState
    lea rsi, [rdx]

    BITS_REORG  N
    NONLIN_FUN  1, %%ARCH

    ;Store the keystream
    mov rbx, [rbp - 64]  ; load *pkeystream
    xor eax, r15d
    mov [rbx], eax
    add rbx, 4          ; increment the pointer
    mov [rbp - 64], rbx   ; save pkeystream

    xor rax, rax

    mov rdx, [rbp - 72]     ; load *pState
    lea rsi, [rdx]

    LFSR_UPDT   N

    dec qword [rbp - 80] ; numRounds - 1
    jz %%exit_loop
%assign N N+1
%endrep

%%exit_loop:
    mov rsi, [rbp - 72]   ; load pState

    ; Save ZUC's state variables
    mov         [rsi + OFFSET_FR1], r10d
    mov         [rsi + OFFSET_FR2], r11d
    mov         [rsi + OFFSET_BRC_X0], r12d
    mov         [rsi + OFFSET_BRC_X1], r13d
    mov         [rsi + OFFSET_BRC_X2], r14d
    mov         [rsi + OFFSET_BRC_X3], r15d

    ; Restore non-volatile registers
    mov rbx, [rbp - 8]
    mov r12, [rbp - 16]
    mov r13, [rbp - 24]
    mov r14, [rbp - 32]
    mov r15, [rbp - 40]
%ifndef LINUX
    mov rdi, [rbp - 48]
    mov rsi, [rbp - 56]
%endif

    mov rsp, rbp
    pop rbp

%endmacro

;;
;;extern void Zuc_Initialization_sse(uint8_t* pKey, uint8_t* pIV, uint32_t * pState)
;;
;; WIN64
;;	RCX - pKey
;;	RDX - pIV
;;      R8  - pState
;; LIN64
;;	RDI - pKey
;;	RSI - pIV
;;      RDX - pState
;;
align 16
MKGLOBAL(asm_ZucInitialization_sse,function,internal)
asm_ZucInitialization_sse:

    ZUC_INIT SSE

    ret

;;
;;extern void Zuc_Initialization_sse_no_aesni(uint8_t* pKey, uint8_t* pIV,
;;                                            uint32_t * pState)
;;
;; WIN64
;;	RCX - pKey
;;	RDX - pIV
;;      R8  - pState
;; LIN64
;;	RDI - pKey
;;	RSI - pIV
;;      RDX - pState
;;
align 16
MKGLOBAL(asm_ZucInitialization_sse_no_aesni,function,internal)
asm_ZucInitialization_sse_no_aesni:

    ZUC_INIT SSE_NO_AESNI

    ret

;;
;;extern void Zuc_Initialization_avx(uint8_t* pKey, uint8_t* pIV, uint32_t * pState)
;;
;; WIN64
;;	RCX - pKey
;;	RDX - pIV
;;      R8  - pState
;; LIN64
;;	RDI - pKey
;;	RSI - pIV
;;      RDX - pState
;;
align 16
MKGLOBAL(asm_ZucInitialization_avx,function,internal)
asm_ZucInitialization_avx:

    ZUC_INIT AVX

    ret

;;
;; void asm_ZucGenKeystream8B_sse(void *pKeystream, ZucState_t *pState);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream8B_sse,function,internal)
asm_ZucGenKeystream8B_sse:

    ZUC_KEYGEN SSE, 2

    ret

;;
;; void asm_ZucGenKeystream8B_sse_no_aesni(void *pKeystream, ZucState_t *pState);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream8B_sse_no_aesni,function,internal)
asm_ZucGenKeystream8B_sse_no_aesni:

    ZUC_KEYGEN SSE_NO_AESNI, 2

    ret

;;
;; void asm_ZucGenKeystream8B_avx(void *pKeystream, ZucState_t *pState);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream8B_avx,function,internal)
asm_ZucGenKeystream8B_avx:

    ZUC_KEYGEN AVX, 2

    ret

;;
;; void asm_ZucGenKeystream16B_sse(uint32_t * pKeystream, uint32_t * pState);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream16B_sse,function,internal)
asm_ZucGenKeystream16B_sse:

    ZUC_KEYGEN SSE, 4

    ret

;;
;; void asm_ZucGenKeystream16B_sse_no_aesni(uint32_t * pKeystream, uint32_t * pState);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream16B_sse_no_aesni,function,internal)
asm_ZucGenKeystream16B_sse_no_aesni:

    ZUC_KEYGEN SSE_NO_AESNI, 4

    ret

;;
;; void asm_ZucGenKeystream64B_avx(uint32_t * pKeystream, uint32_t * pState);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream64B_avx,function,internal)
asm_ZucGenKeystream64B_avx:

    ZUC_KEYGEN AVX, 16

    ret

;;
;; void asm_ZucGenKeystream32B_avx(uint32_t * pKeystream, uint32_t * pState);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream32B_avx,function,internal)
asm_ZucGenKeystream32B_avx:

    ZUC_KEYGEN AVX, 8

    ret

;;
;; void asm_ZucGenKeystream16B_avx(uint32_t * pKeystream, uint32_t * pState);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream16B_avx,function,internal)
asm_ZucGenKeystream16B_avx:

    ZUC_KEYGEN AVX, 4

    ret

;;
;; void asm_ZucGenKeystream_sse(uint32_t * pKeystream, uint32_t * pState,
;;                              uint64_t numRounds);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; 	R8  - NROUNDS (number of 4B rounds)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;; 	RDX - NROUNDS (number of 4B rounds)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream_sse,function,internal)
asm_ZucGenKeystream_sse:

    ZUC_KEYGEN_VAR SSE

    ret

;;
;; void asm_ZucGenKeystream_sse_no_aesni(uint32_t * pKeystream, uint32_t * pState,
;;                              uint64_t numRounds);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; 	R8  - NROUNDS (number of 4B rounds)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;; 	RDX - NROUNDS (number of 4B rounds)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream_sse_no_aesni,function,internal)
asm_ZucGenKeystream_sse_no_aesni:

    ZUC_KEYGEN_VAR SSE_NO_AESNI

    ret

;;
;; void asm_ZucGenKeystream_avx(uint32_t * pKeystream, uint32_t * pState);
;;                              uint64_t numRounds);
;;
;; WIN64
;;	RCX - KS (key stream pointer)
;; 	RDX - STATE (state pointer)
;; 	R8  - NROUNDS (number of 4B rounds)
;; LIN64
;;	RDI - KS (key stream pointer)
;;	RSI - STATE (state pointer)
;; 	RDX - NROUNDS (number of 4B rounds)
;;
align 16
MKGLOBAL(asm_ZucGenKeystream_avx,function,internal)
asm_ZucGenKeystream_avx:

    ZUC_KEYGEN_VAR AVX

    ret

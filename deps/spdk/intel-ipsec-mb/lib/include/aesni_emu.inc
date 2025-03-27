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

%ifndef _AESNI_EMU_INC_
%define _AESNI_EMU_INC_

%include "include/reg_sizes.asm"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Utility macros and defines to assist AESNI translation macros
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%define GP0  rax
%define GP1  rbx
%define GP2  rcx
%define GP3  rdx
%define GP4  rbp
%define GP5  rsi
%define GP6  rdi
%define GP7  r8
%define GP8  r9
%define GP9  r10
%define GP10 r11
%define GP11 r12
%define GP12 r13
%define GP13 r14
%define GP14 r15
%define NUM_GP_REGS  15
%define NUM_XMM_REGS 16

%define GP_SZ   8
%define XMM_SZ  16
%define ARG_SZ  16

;; 8 extra bytes added to align to 16 bytes
%define XMM_OFFSET ((NUM_GP_REGS + 1) * GP_SZ)
;; ARG1 placed in the stack after all GP and XMM registers
%define ARG1_OFFSET (XMM_OFFSET + (NUM_XMM_REGS * XMM_SZ))
;; ARG2 placed in the stack after all GP and XMM registers and ARG1
%define ARG2_OFFSET (ARG1_OFFSET + ARG_SZ)

%define GP(x) GP %+ x
%define XMM(x) xmm  %+ x

;; Reserve enough stack space to store all GP and XMM
;; registers and emulation function arguments
;; e.g. void emulate_AESXXX(xmm_reg *dst, xmm_reg *src);
%define RES_STACK_SZ (ARG2_OFFSET + ARG_SZ)

;; Allocate stack space and save GP registers
%macro SAVE_GP_REGS 0
        push    rax
        mov     rax, rsp
        sub	rsp, RES_STACK_SZ
        and     rsp, -16
%assign gp_regs_i 0
%rep    NUM_GP_REGS
        mov	[rsp + 8*gp_regs_i], GP(gp_regs_i)
%assign gp_regs_i gp_regs_i+1
%endrep
%endmacro

;; Restore GP registers and stack pointer
%macro RESTORE_GP_REGS 0
%assign gp_regs_i 0
%rep    NUM_GP_REGS
        mov	GP(gp_regs_i), [rsp + 8*gp_regs_i]
%assign gp_regs_i gp_regs_i+1
%endrep
        mov     rsp, rax
        pop     rax
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Generic macro to translate AESNI instructions to AESNI emulation functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%macro EMULATE_AESNI 4
%define %%func    %1
%define %%src_dst %2
%define %%key     %3
%define %%imm     %4

%ifdef LINUX
%define %%arg1 rdi
%define %%arg2 rsi
%define %%arg3 rdx
%else
%define %%arg1 rcx
%define %%arg2 rdx
%define %%arg3 r8
%endif

;; Check if key is reg or ptr
%assign IS_REG 0
%assign x 0
%rep    NUM_XMM_REGS
%ifidni %%key, XMM(x)
       %assign IS_REG 1
       %exitrep
%endif
%assign x x+1
%endrep
        ;; save GP registers to stack
        SAVE_GP_REGS

        ;; move function args onto stack before function call
        movdqa  [rsp + ARG1_OFFSET], %%src_dst
%if IS_REG
        movdqa  [rsp + ARG2_OFFSET], %%key
%else
        movdqu  %%src_dst, %%key
        movdqa  [rsp + ARG2_OFFSET], %%src_dst
%endif
        lea     %%arg1, [rsp + ARG1_OFFSET]
        lea     %%arg2, [rsp + ARG2_OFFSET]

        ;; move 8 bit imm rcon for aeskeygenassist
%ifnum  %%imm
        mov     BYTE(%%arg3), %%imm
%endif

;; save XMM registers to stack, as some compilers may use them in "func"
%assign reg_idx 0
%rep NUM_XMM_REGS
	movdqa	[rsp + XMM_OFFSET + (reg_idx * XMM_SZ)], XMM(reg_idx)
%assign reg_idx reg_idx + 1
%endrep

;; reserve space on stack for up to 4 arguments on the stack (windows only)
%ifndef LINUX
        sub     rsp, 32
%endif
        ;; call emulation function
        call    %%func
%ifndef LINUX
        add     rsp, 32
%endif

;; restore XMM registers from stack
%assign reg_idx 0
%rep NUM_XMM_REGS
	movdqa	XMM(reg_idx), [rsp + XMM_OFFSET + (reg_idx * XMM_SZ)]
%ifdef SAFE_DATA
        mov     qword [rsp + XMM_OFFSET + (reg_idx * XMM_SZ)], 0
        mov     qword [rsp + XMM_OFFSET + (reg_idx * XMM_SZ) + 8], 0
%endif
%assign reg_idx reg_idx + 1
%endrep

	;; Destination XMM gets overwritten with result from func
        movdqa  %%src_dst, [rsp + ARG1_OFFSET]

%ifdef SAFE_DATA
        mov     qword [rsp + ARG1_OFFSET], 0
        mov     qword [rsp + ARG1_OFFSET + 8], 0
        mov     qword [rsp + ARG2_OFFSET], 0
        mov     qword [rsp + ARG2_OFFSET + 8], 0
%endif
        RESTORE_GP_REGS
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Macros to translate AESNI instructions to AESNI emulation functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; AESENC translation macro
%macro EMULATE_AESENC 2
%define %%src_dst %1
%define %%key     %2
        EMULATE_AESNI emulate_AESENC, %%src_dst, %%key, ""
%endmacro

;; AESENCLAST translation macro
%macro EMULATE_AESENCLAST 2
%define %%src_dst %1
%define %%key     %2
        EMULATE_AESNI emulate_AESENCLAST, %%src_dst, %%key, ""
%endmacro

;; AESDEC translation macro
%macro EMULATE_AESDEC 2
%define %%src_dst %1
%define %%key     %2
        EMULATE_AESNI emulate_AESDEC, %%src_dst, %%key, ""
%endmacro

;; AESDECLAST translation macro
%macro EMULATE_AESDECLAST 2
%define %%src_dst %1
%define %%key     %2
        EMULATE_AESNI emulate_AESDECLAST, %%src_dst, %%key, ""
%endmacro

;; AESIMC translation macro
%macro EMULATE_AESIMC 2
%define %%src_dst %1
%define %%key     %2
        EMULATE_AESNI emulate_AESIMC, %%src_dst, %%key, ""
%endmacro

;; AESKEYGENASSIST translation macro
%macro EMULATE_AESKEYGENASSIST 3
%define %%src_dst %1
%define %%key     %2
%define %%imm     %3
        EMULATE_AESNI emulate_AESKEYGENASSIST, %%src_dst, %%key, %%imm
%endmacro

;; PCLMULQDQ translation macro
%macro EMULATE_PCLMULQDQ 3
%define %%src1_dst %1
%define %%src2     %2
%define %%imm      %3
        EMULATE_AESNI emulate_PCLMULQDQ, %%src1_dst, %%src2, %%imm
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AESNI defines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%ifndef NO_AESNI_RENAME
%define aesenc          EMULATE_AESENC
%define aesenclast      EMULATE_AESENCLAST
%define aesdec          EMULATE_AESDEC
%define aesdeclast      EMULATE_AESDECLAST
%define aesimc          EMULATE_AESIMC
%define aeskeygenassist EMULATE_AESKEYGENASSIST
%define pclmulqdq       EMULATE_PCLMULQDQ
%endif

extern emulate_AESENC
extern emulate_AESENCLAST
extern emulate_AESDEC
extern emulate_AESDECLAST
extern emulate_AESIMC
extern emulate_AESKEYGENASSIST
extern emulate_PCLMULQDQ

%endif ; end ifndef _AESNI_EMU_INC_

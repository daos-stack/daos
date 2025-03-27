;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2019 Intel Corporation All rights reserved.
;
;  Redistribution and use in source and binary forms, with or without
;  modification, are permitted provided that the following conditions
;  are met:
;    * Redistributions of source code must retain the above copyright
;      notice, this list of conditions and the following disclaimer.
;    * Redistributions in binary form must reproduce the above copyright
;      notice, this list of conditions and the following disclaimer in
;      the documentation and/or other materials provided with the
;      distribution.
;    * Neither the name of Intel Corporation nor the names of its
;      contributors may be used to endorse or promote products derived
;      from this software without specific prior written permission.
;
;  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
;  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
;  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
;  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
;  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%ifndef _CLEAR_REGS_ASM_
%define _CLEAR_REGS_ASM_

%ifndef LINUX
%ifidn __OUTPUT_FORMAT__, elf64
%define LINUX
%endif
%endif

;
; This macro clears any GP registers passed
;
%macro clear_gps 1-16
%define %%NUM_REGS %0
%rep %%NUM_REGS
        xor %1, %1
%rotate 1
%endrep
%endmacro

;
; This macro clears any XMM registers passed on SSE
;
%macro clear_xmms_sse 1-16
%define %%NUM_REGS %0
%rep %%NUM_REGS
        pxor    %1, %1
%rotate 1
%endrep
%endmacro

;
; This macro clears any XMM registers passed on AVX
;
%macro clear_xmms_avx 1-16
%define %%NUM_REGS %0
%rep %%NUM_REGS
        vpxor   %1, %1
%rotate 1
%endrep
%endmacro

;
; This macro clears any YMM registers passed
;
%macro clear_ymms 1-16
%define %%NUM_REGS %0
%rep %%NUM_REGS
        vpxor   %1, %1
%rotate 1
%endrep
%endmacro

;
; This macro clears any ZMM registers passed
;
%macro clear_zmms 1-32
%define %%NUM_REGS %0
%rep %%NUM_REGS
        vpxorq  %1, %1
%rotate 1
%endrep
%endmacro

;
; This macro clears all scratch GP registers
; for Windows or Linux
;
%macro clear_scratch_gps_asm 0
        clear_gps rax, rcx, rdx, r8, r9, r10, r11
%ifdef LINUX
        clear_gps rdi, rsi
%endif
%endmacro

;
; This macro clears all scratch XMM registers on SSE
;
%macro clear_scratch_xmms_sse_asm 0
%ifdef LINUX
%assign i 0
%rep 16
        pxor    xmm %+ i, xmm %+ i
%assign i (i+1)
%endrep
; On Windows, XMM0-XMM5 registers are scratch registers
%else
%assign i 0
%rep 6
        pxor    xmm %+ i, xmm %+ i
%assign i (i+1)
%endrep
%endif ; LINUX
%endmacro

;
; This macro clears all scratch XMM registers on AVX
;
%macro clear_scratch_xmms_avx_asm 0
%ifdef LINUX
        vzeroall
; On Windows, XMM0-XMM5 registers are scratch registers
%else
%assign i 0
%rep 6
        vpxor   xmm %+ i, xmm %+ i
%assign i (i+1)
%endrep
%endif ; LINUX
%endmacro

;
; This macro clears all scratch YMM registers
;
; It should be called before restoring the XMM registers
; for Windows (XMM6-XMM15)
;
%macro clear_scratch_ymms_asm 0
; On Linux, all YMM registers are scratch registers
%ifdef LINUX
        vzeroall
; On Windows, YMM0-YMM5 registers are scratch registers.
; YMM6-YMM15 upper 128 bits are scratch registers too, but
; the lower 128 bits are to be restored after calling these function
; which clears the upper bits too.
%else
%assign i 0
%rep 6
        vpxor   ymm %+ i, ymm %+ i
%assign i (i+1)
%endrep
%endif ; LINUX
%endmacro

;
; This macro clears all scratch ZMM registers
;
; It should be called before restoring the XMM registers
; for Windows (XMM6-XMM15). YMM registers are used
; on purpose, since XOR'ing YMM registers is faster
; than XOR'ing ZMM registers, and the operation clears
; also the upper 256 bits
;
%macro clear_scratch_zmms_asm 0
; On Linux, all ZMM registers are scratch registers
%ifdef LINUX
        vzeroall
        ;; vzeroall only clears the first 16 ZMM registers
%assign i 16
%rep 16
        vpxorq  ymm %+ i, ymm %+ i
%assign i (i+1)
%endrep
; On Windows, ZMM0-ZMM5 and ZMM16-ZMM31 registers are scratch registers.
; ZMM6-ZMM15 upper 384 bits are scratch registers too, but
; the lower 128 bits are to be restored after calling these function
; which clears the upper bits too.
%else
%assign i 0
%rep 6
        vpxorq  ymm %+ i, ymm %+ i
%assign i (i+1)
%endrep

%assign i 16
%rep 16
        vpxorq  ymm %+ i, ymm %+ i
%assign i (i+1)
%endrep
%endif ; LINUX
%endmacro

%endif ;; _CLEAR_REGS_ASM

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2021, Intel Corporation All rights reserved.
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


%ifdef LINUX
;;; macro to declare global symbols
;;;  - name : symbol name
;;;  - type : function or data
;;;  - scope : internal, private, default
%define MKGLOBAL(name,type,scope) global name %+ : %+ type scope
%else
;;; macro to declare global symbols
;;;  - name : symbol name
;;;  - type : function or data
;;;  - scope : internal, private, default (ignored in win64 coff format)
%define MKGLOBAL(name,type,scope) global name
%endif

%ifdef WIN_ABI
%define arg1 rcx
%define arg2 rdx
%else
%define arg1 rdi
%define arg2 rsi
%endif

%define GP0 r8
%define GP1 r9
%define GP2 r10

;; macro to read TSC into GP register
%macro RDTSCP 1
%define %%TSC   %1      ; GP reg to store TSC value (cannot be rax, rdx or rcx)
        rdtscp
        shl     rdx, 32
        or      rax, rdx
        mov     %%TSC, rax
%endmacro

section .text

;; uint64_t measure_tsc(const uint64_t cycles);
MKGLOBAL(measure_tsc,function,)
align 16
measure_tsc:
        ;; store arg1 (clobbered in RDTSCP on Windows)
        mov     GP0, arg1

        ;; get start ts
        RDTSCP  GP1

        ;; loop with fixed_overhead number of cycles due to
        ;; 1-cycle latency dependency on all non-ancient CPUs
        mov     rax, GP0 ; arg1 (cycles)
fixed_loop:
        dec     eax
        dec     eax
        jg      fixed_loop

        ;; get end ts
        RDTSCP  rax

        sub     rax, GP1

        ret


%ifdef LINUX
section .note.GNU-stack noalloc noexec nowrite progbits
%endif

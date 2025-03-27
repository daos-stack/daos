;;
;; Copyright (c) 2019-2021, Intel Corporation
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
%include "include/clear_regs.asm"
%include "include/cet.inc"
mksection .text
;
; This function clears all scratch GP registers
;
; void clear_scratch_gps(void)
MKGLOBAL(clear_scratch_gps,function,internal)
clear_scratch_gps:

        clear_scratch_gps_asm

        ret

;
; This function clears all scratch XMM registers
;
; void clear_scratch_xmms_sse(void)
MKGLOBAL(clear_scratch_xmms_sse,function,internal)
clear_scratch_xmms_sse:

        clear_scratch_xmms_sse_asm

        ret

;
; This function clears all scratch XMM registers
;
; It should be called before restoring the XMM registers
; for Windows (XMM6-XMM15)
;
; void clear_scratch_xmms_avx(void)
MKGLOBAL(clear_scratch_xmms_avx,function,internal)
clear_scratch_xmms_avx:

        clear_scratch_xmms_avx_asm

        ret

;
; This function clears all scratch YMM registers
;
; It should be called before restoring the XMM registers
; for Windows (XMM6-XMM15)
;
; void clear_scratch_ymms(void)
MKGLOBAL(clear_scratch_ymms,function,internal)
clear_scratch_ymms:

        clear_scratch_ymms_asm

        ret

;
; This function clears all scratch ZMM registers
;
; It should be called before restoring the XMM registers
; for Windows (XMM6-XMM15). YMM registers are used
; on purpose, since XOR'ing YMM registers is faster
; than XOR'ing ZMM registers, and the operation clears
; also the upper 256 bits
;
; void clear_scratch_zmms(void)
MKGLOBAL(clear_scratch_zmms,function,internal)
clear_scratch_zmms:

        clear_scratch_zmms_asm

        ret

;
; This function clears all memory passed
;
; void force_memset_zero(void *mem, const size_t size)
MKGLOBAL(force_memset_zero,function,internal)
force_memset_zero:

%ifdef LINUX
        mov rcx, rsi
%else
        push rdi
        mov rdi, rcx
        mov rcx, rdx
%endif
        xor eax, eax
        cld
        rep stosb

%ifndef LINUX
        pop rdi
%endif
        ret

MKGLOBAL(imb_clear_mem,function,)
imb_clear_mem:
        endbranch64
%ifdef LINUX
        cmp rdi, 0
%else
        cmp rcx, 0
%endif
        jz  clr_mem_return

        pushfq ;; save flags
%ifdef LINUX
        mov rcx, rsi
%else
        push rdi
        mov rdi, rcx
        mov rcx, rdx
%endif
        xor eax, eax
        cld
        rep stosb

%ifndef LINUX
        pop rdi
%endif
        sfence  ;; ensure stores complete
        popfq   ;; restore flags

clr_mem_return:
        ret

mksection stack-noexec

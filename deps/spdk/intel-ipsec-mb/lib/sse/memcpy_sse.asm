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
%include "include/memcpy.asm"

%ifdef LINUX
%define arg1    rdi
%define arg2    rsi
%define arg3    rdx
%else
%define arg1    rcx
%define arg2    rdx
%define arg3    r8
%endif

mksection .text

; void memcpy_fn_sse_16(void *dst, const void *src, const size_t size)
MKGLOBAL(memcpy_fn_sse_16,function,internal)
memcpy_fn_sse_16:
        memcpy_sse_16 arg1, arg2, arg3, r10, r11

        ret

MKGLOBAL(memcpy_fn_sse_128,function,internal)
memcpy_fn_sse_128:
        movdqu  xmm0, [arg2]
        movdqu  xmm1, [arg2 + 16]
        movdqu  xmm2, [arg2 + 16*2]
        movdqu  xmm3, [arg2 + 16*3]
        movdqu  [arg1], xmm0
        movdqu  [arg1 + 16], xmm1
        movdqu  [arg1 + 16*2], xmm2
        movdqu  [arg1 + 16*3], xmm3
        movdqu  xmm0, [arg2 + 16*4]
        movdqu  xmm1, [arg2 + 16*5]
        movdqu  xmm2, [arg2 + 16*6]
        movdqu  xmm3, [arg2 + 16*7]
        movdqu  [arg1 + 16*4], xmm0
        movdqu  [arg1 + 16*5], xmm1
        movdqu  [arg1 + 16*6], xmm2
        movdqu  [arg1 + 16*7], xmm3

        ret

mksection stack-noexec

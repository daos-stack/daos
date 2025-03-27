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

%define CBCS
%include "avx512/aes_cbc_dec_by16_vaes_avx512.asm"
%include "include/cet.inc"
%define len     rax

mksection .text

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; aes_cbcs_1_9_dec_128_vaes_avx512(void *in, void *IV, void *keys, void *out, UINT64 num_bytes)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(aes_cbcs_1_9_dec_128_vaes_avx512,function,internal)
aes_cbcs_1_9_dec_128_vaes_avx512:
        endbranch64
%ifndef LINUX
        mov     len,     [rsp + 8*5]
%else
        mov     len, num_bytes
%endif

        ;; convert CBCS length to standard number of CBC blocks
        ;; ((num_bytes + 9 blocks) / 160) = num blocks to decrypt
        mov     tmp2, rdx
        xor     rdx, rdx        ;; store and zero rdx for div
        add     len, 9*16
        mov     tmp, 160
        div     tmp             ;; divide by 160
        shl     len, 4    ;; multiply by 16 to get num bytes
        mov     rdx, tmp2

        AES_CBC_DEC p_in, p_out, p_keys, p_IV, len, 9, tmp

%ifndef LINUX
        mov     next_iv, [rsp + 8*6]
%endif
        ;; store last cipher block as next iv
        vextracti64x2 [next_iv], zIV, 3

%ifdef SAFE_DATA
	clear_all_zmms_asm
%endif ;; SAFE_DATA

        ret

mksection stack-noexec

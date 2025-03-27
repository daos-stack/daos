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
%include "include/reg_sizes.asm"
%include "include/crc32_const.inc"
%include "include/clear_regs.asm"
%include "include/cet.inc"
%include "include/error.inc"

[bits 64]
default rel

%ifndef CRC16_FP_DATA_FN
%define CRC16_FP_DATA_FN crc16_fp_data_sse
%endif

%ifndef CRC11_FP_HEADER_FN
%define CRC11_FP_HEADER_FN crc11_fp_header_sse
%endif

%ifndef CRC7_FP_HEADER_FN
%define CRC7_FP_HEADER_FN crc7_fp_header_sse
%endif

%ifndef CRC32_FN
%define CRC32_FN crc32_by8_sse
%endif

%ifdef LINUX
%define arg1            rdi
%define arg2            rsi
%define arg3            rdx
%define arg4            rcx
%else
%define arg1            rcx
%define arg2            rdx
%define arg3            r8
%define arg4            r9
%endif

struc STACK_FRAME
_xmm_save:      resq    8 * 2
_rsp_save:      resq    1
endstruc

mksection .text

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; arg1 - buffer pointer
;; arg2 - buffer size in bytes
;; Returns CRC value through RAX
align 32
MKGLOBAL(CRC16_FP_DATA_FN, function,)
CRC16_FP_DATA_FN:
        endbranch64
%ifdef SAFE_PARAM

        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check len == 0
        or              arg2, arg2
        jz              .end_param_check

        ;; Check in == NULL (invalid if len != 0)
        or              arg1, arg1
        jz              .wrong_param

.end_param_check:
%endif
%ifndef LINUX
        mov             rax, rsp
        sub             rsp, STACK_FRAME_size
        and             rsp, -16
        mov             [rsp + _rsp_save], rax
        movdqa          [rsp + _xmm_save + 16*0], xmm6
        movdqa          [rsp + _xmm_save + 16*1], xmm7
        movdqa          [rsp + _xmm_save + 16*2], xmm8
        movdqa          [rsp + _xmm_save + 16*3], xmm9
        movdqa          [rsp + _xmm_save + 16*4], xmm10
        movdqa          [rsp + _xmm_save + 16*5], xmm11
        movdqa          [rsp + _xmm_save + 16*6], xmm12
        movdqa          [rsp + _xmm_save + 16*7], xmm13
%endif
        lea             arg4, [rel crc32_fp_data_crc16_const]
        mov             arg3, arg2
        mov             arg2, arg1
        xor             DWORD(arg1), DWORD(arg1)

        call            CRC32_FN

        shr             eax, 16  ; adjust to 16-bit poly

%ifdef SAFE_DATA
        clear_scratch_xmms_sse_asm
%endif
%ifndef LINUX
        movdqa          xmm6,  [rsp + _xmm_save + 16*0]
        movdqa          xmm7,  [rsp + _xmm_save + 16*1]
        movdqa          xmm8,  [rsp + _xmm_save + 16*2]
        movdqa          xmm9,  [rsp + _xmm_save + 16*3]
        movdqa          xmm10, [rsp + _xmm_save + 16*4]
        movdqa          xmm11, [rsp + _xmm_save + 16*5]
        movdqa          xmm12, [rsp + _xmm_save + 16*6]
        movdqa          xmm13, [rsp + _xmm_save + 16*7]
        mov             rsp, [rsp + _rsp_save]
%endif
        ret

%ifdef SAFE_PARAM
.wrong_param:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check in != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_SRC

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax

        ret
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; arg1 - buffer pointer
;; arg2 - buffer size in bytes
;; Returns CRC value through RAX
align 32
MKGLOBAL(CRC11_FP_HEADER_FN, function,)
CRC11_FP_HEADER_FN:
        endbranch64
%ifdef SAFE_PARAM

        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check len == 0
        or              arg2, arg2
        jz              .end_param_check

        ;; Check in == NULL (invalid if len != 0)
        or              arg1, arg1
        jz              .wrong_param

.end_param_check:
%endif
%ifndef LINUX
        mov             rax, rsp
        sub             rsp, STACK_FRAME_size
        and             rsp, -16
        mov             [rsp + _rsp_save], rax
        movdqa          [rsp + _xmm_save + 16*0], xmm6
        movdqa          [rsp + _xmm_save + 16*1], xmm7
        movdqa          [rsp + _xmm_save + 16*2], xmm8
        movdqa          [rsp + _xmm_save + 16*3], xmm9
        movdqa          [rsp + _xmm_save + 16*4], xmm10
        movdqa          [rsp + _xmm_save + 16*5], xmm11
        movdqa          [rsp + _xmm_save + 16*6], xmm12
        movdqa          [rsp + _xmm_save + 16*7], xmm13
%endif
        lea             arg4, [rel crc32_fp_header_crc11_const]
        mov             arg3, arg2
        mov             arg2, arg1
        xor             DWORD(arg1), DWORD(arg1)

        call            CRC32_FN

        shr             eax, 21  ; adjust to 11-bit poly

%ifdef SAFE_DATA
        clear_scratch_xmms_sse_asm
%endif
%ifndef LINUX
        movdqa          xmm6,  [rsp + _xmm_save + 16*0]
        movdqa          xmm7,  [rsp + _xmm_save + 16*1]
        movdqa          xmm8,  [rsp + _xmm_save + 16*2]
        movdqa          xmm9,  [rsp + _xmm_save + 16*3]
        movdqa          xmm10, [rsp + _xmm_save + 16*4]
        movdqa          xmm11, [rsp + _xmm_save + 16*5]
        movdqa          xmm12, [rsp + _xmm_save + 16*6]
        movdqa          xmm13, [rsp + _xmm_save + 16*7]
        mov             rsp, [rsp + _rsp_save]
%endif
        ret

%ifdef SAFE_PARAM
.wrong_param:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check in != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_SRC

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax

        ret
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; arg1 - buffer pointer
;; arg2 - buffer size in bytes
;; Returns CRC value through RAX
align 32
MKGLOBAL(CRC7_FP_HEADER_FN, function,)
CRC7_FP_HEADER_FN:
        endbranch64
%ifdef SAFE_PARAM

        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check len == 0
        or              arg2, arg2
        jz              .end_param_check

        ;; Check in == NULL (invalid if len != 0)
        or              arg1, arg1
        jz              .wrong_param

.end_param_check:
%endif
%ifndef LINUX
        mov             rax, rsp
        sub             rsp, STACK_FRAME_size
        and             rsp, -16
        mov             [rsp + _rsp_save], rax
        movdqa          [rsp + _xmm_save + 16*0], xmm6
        movdqa          [rsp + _xmm_save + 16*1], xmm7
        movdqa          [rsp + _xmm_save + 16*2], xmm8
        movdqa          [rsp + _xmm_save + 16*3], xmm9
        movdqa          [rsp + _xmm_save + 16*4], xmm10
        movdqa          [rsp + _xmm_save + 16*5], xmm11
        movdqa          [rsp + _xmm_save + 16*6], xmm12
        movdqa          [rsp + _xmm_save + 16*7], xmm13
%endif
        lea             arg4, [rel crc32_fp_header_crc7_const]
        mov             arg3, arg2
        mov             arg2, arg1
        xor             DWORD(arg1), DWORD(arg1)

        call            CRC32_FN

        shr             eax, 25  ; adjust to 7-bit poly

%ifdef SAFE_DATA
        clear_scratch_xmms_sse_asm
%endif
%ifndef LINUX
        movdqa          xmm6,  [rsp + _xmm_save + 16*0]
        movdqa          xmm7,  [rsp + _xmm_save + 16*1]
        movdqa          xmm8,  [rsp + _xmm_save + 16*2]
        movdqa          xmm9,  [rsp + _xmm_save + 16*3]
        movdqa          xmm10, [rsp + _xmm_save + 16*4]
        movdqa          xmm11, [rsp + _xmm_save + 16*5]
        movdqa          xmm12, [rsp + _xmm_save + 16*6]
        movdqa          xmm13, [rsp + _xmm_save + 16*7]
        mov             rsp, [rsp + _rsp_save]
%endif
        ret

%ifdef SAFE_PARAM
.wrong_param:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check in != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_SRC

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax

        ret
%endif

mksection stack-noexec

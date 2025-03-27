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

%ifndef ERROR_ASM_INCLUDED
%define ERROR_ASM_INCLUDED

extern imb_errno
extern imb_errno_types

;; Macro to define error types
%macro SET_ERRNO_TYPES 1-*

%assign i 0
%rep  %0
;; set address of next error type in imb_errno_type array
%xdefine %1 [rel imb_errno_types + i]
%rotate 1
%assign i (i + 4)
%endrep

%endmacro

SET_ERRNO_TYPES \
    IMB_ERR_NULL_MBMGR,         \
    IMB_ERR_JOB_NULL_SRC,       \
    IMB_ERR_JOB_NULL_DST,       \
    IMB_ERR_JOB_NULL_KEY,       \
    IMB_ERR_JOB_NULL_IV,        \
    IMB_ERR_JOB_NULL_AUTH,      \
    IMB_ERR_JOB_NULL_AAD,       \
    IMB_ERR_JOB_CIPH_LEN,       \
    IMB_ERR_JOB_AUTH_LEN,       \
    IMB_ERR_JOB_IV_LEN,         \
    IMB_ERR_JOB_KEY_LEN,        \
    IMB_ERR_JOB_AUTH_TAG_LEN,   \
    IMB_ERR_JOB_AAD_LEN,        \
    IMB_ERR_JOB_SRC_OFFSET,     \
    IMB_ERR_JOB_CHAIN_ORDER,    \
    IMB_ERR_CIPH_MODE,          \
    IMB_ERR_HASH_ALGO,          \
    IMB_ERR_JOB_NULL_AUTH_KEY,  \
    IMB_ERR_JOB_NULL_SGL_CTX,   \
    IMB_ERR_JOB_NULL_NEXT_IV,   \
    IMB_ERR_JOB_PON_PLI,        \
    IMB_ERR_NULL_SRC,           \
    IMB_ERR_NULL_DST,           \
    IMB_ERR_NULL_KEY,           \
    IMB_ERR_NULL_EXP_KEY,       \
    IMB_ERR_NULL_IV,            \
    IMB_ERR_NULL_AUTH,          \
    IMB_ERR_NULL_AAD,           \
    IMB_ERR_CIPH_LEN,           \
    IMB_ERR_AUTH_LEN,           \
    IMB_ERR_IV_LEN,             \
    IMB_ERR_KEY_LEN,            \
    IMB_ERR_AUTH_TAG_LEN,       \
    IMB_ERR_AAD_LEN,            \
    IMB_ERR_SRC_OFFSET,         \
    IMB_ERR_NULL_AUTH_KEY,      \
    IMB_ERR_NULL_CTX

;; Reset global imb_errno to 0
%macro IMB_ERR_CHECK_RESET 0

        mov     dword [rel imb_errno], 0
%endmacro

;; Set GP reg and global imb_errno to 0
%macro IMB_ERR_CHECK_START 1
%define %%REG   %1 ;; [clobbered] 64 bit GP reg

        xor     %%REG, %%REG
        mov     [rel imb_errno], DWORD(%%REG)
%endmacro

;; Set global imb_errno to specified error type
%macro IMB_ERR_CHECK_END 1
%define %%ERR   %1 ;; [in] 64 bit GP reg containing error type

        mov     [rel imb_errno], DWORD(%%ERR)
%endmacro

;; Set error type if value is 0
%macro IMB_ERR_CHECK_ZERO 3
%define %%VAL   %1 ;; [in] value to check for 0
%define %%REG   %2 ;; [out] GP reg to store error type
%define %%ERR   %3 ;; [in] error type

        cmp     %%VAL, 0
        cmovz   DWORD(%%REG), %%ERR
%endmacro

;; Set error type if value is null
%macro IMB_ERR_CHECK_NULL 3
%define %%VAL   %1 ;; [in] value to check for null
%define %%REG   %2 ;; [out] GP reg to store error type
%define %%ERR   %3 ;; [in] error type

        IMB_ERR_CHECK_ZERO %%VAL, %%REG, %%ERR
%endmacro

;; Set error type if value is above compare value
%macro IMB_ERR_CHECK_ABOVE 4
%define %%VAL   %1 ;; [in] value to check
%define %%CVAL  %2 ;; [in] value to compare against
%define %%REG   %3 ;; [out] GP reg to store error type
%define %%ERR   %4 ;; [in] error type

        cmp     %%VAL, %%CVAL
        cmova   DWORD(%%REG), %%ERR
%endmacro

;; Set error type if value is below compare value
%macro IMB_ERR_CHECK_BELOW 4
%define %%VAL   %1 ;; [in] value to check
%define %%CVAL  %2 ;; [in] value to compare against
%define %%REG   %3 ;; [out] GP reg to store error type
%define %%ERR   %4 ;; [in] error type

        cmp     %%VAL, %%CVAL
        cmovb   DWORD(%%REG), %%ERR
%endmacro

%endif ;; ERROR_ASM_INCLUDED

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

mksection .rodata
default rel

MKGLOBAL(len_shift_tab,data,internal)
MKGLOBAL(len_mask_tab,data,internal)
MKGLOBAL(padding_0x80_tab16,data,internal)
MKGLOBAL(shift_tab_16,data,internal)
MKGLOBAL(idx_rows_avx512,data,internal)
MKGLOBAL(all_7fs,data,internal)
MKGLOBAL(all_80s,data,internal)

;;; The following tables are used to insert a word into
;;; a SIMD register and must be defined together.
;;; If resized, update len_tab_diff definition in const.inc module.
;;; Other modifications may require updates to dependent modules.

;;; Table used to shuffle word to correct index
;;; Used by macros:
;;;    - PINSRW_COMMON
;;;    - XPINSRW
;;;    - XVPINSRW
align 16
len_shift_tab:
        db 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01

;;; Table used to zero index
align 16
len_mask_tab:
        dw 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        dw 0xffff, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        dw 0xffff, 0xffff, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        dw 0xffff, 0xffff, 0xffff, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff,
        dw 0xffff, 0xffff, 0xffff, 0xffff, 0x0000, 0xffff, 0xffff, 0xffff,
        dw 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0x0000, 0xffff, 0xffff,
        dw 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0x0000, 0xffff,
        dw 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0x0000

;;; Table to do 0x80 byte shift for padding prefix
align 16
padding_0x80_tab16:
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        db 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

;;; Table for shifting bytes in 128 bit SIMD register
align 16
shift_tab_16:
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        db 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        db 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff

align 64
idx_rows_avx512:
times 16 dd 0x00000000
times 16 dd 0x10101010
times 16 dd 0x20202020
times 16 dd 0x30303030
times 16 dd 0x40404040
times 16 dd 0x50505050
times 16 dd 0x60606060
times 16 dd 0x70707070
times 16 dd 0x80808080
times 16 dd 0x90909090
times 16 dd 0xa0a0a0a0
times 16 dd 0xb0b0b0b0
times 16 dd 0xc0c0c0c0
times 16 dd 0xd0d0d0d0
times 16 dd 0xe0e0e0e0
times 16 dd 0xf0f0f0f0

align 64
all_7fs:
times 64 db 0x7f

align 64
all_80s:
times 64 db 0x80

mksection stack-noexec

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

%ifndef crc32_inc_included
%define crc32_inc_included

struc CRC32_CONSTANTS
crc32_const_fold_16x128b:
                resq    2       ; fold by 16 x 128-bits
crc32_const_fold_8x128b:
                resq    2       ; fold by 8 x 128-bits
crc32_const_fold_7x128b:
                resq    2       ; fold by 7 x 128-bits
crc32_const_fold_6x128b:
                resq    2       ; fold by 6 x 128-bits
crc32_const_fold_5x128b:
                resq    2       ; fold by 5 x 128-bits
crc32_const_fold_4x128b:
                resq    2       ; fold by 4 x 128-bits
crc32_const_fold_3x128b:
                resq    2       ; fold by 3 x 128-bits
crc32_const_fold_2x128b:
                resq    2       ; fold by 2 x 128-bits
crc32_const_fold_1x128b:
                resq    2       ; fold by 1 x 128-bits
crc32_const_null_padding:
                resq    2
crc32_const_fold_128b_to_64b:
                resq    2       ; 128-bits to 64-bits fold
crc32_const_reduce_64b_to_32b:
                resq    2       ; barrett reduction 64-bits to 32-bits
endstruc

%endif ;; crc32_inc_included

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
%include "include/zuc_sbox.inc"
%include "include/transpose_avx512.asm"
%include "include/const.inc"
%include "include/mb_mgr_datastruct.asm"
%include "include/cet.inc"
%define APPEND(a,b) a %+ b
%define APPEND3(a,b,c) a %+ b %+ c

%ifndef CIPHER_16
%define USE_GFNI_VAES_VPCLMUL 0
%define CIPHER_16 asm_ZucCipher_16_avx512
%define ZUC128_INIT asm_ZucInitialization_16_avx512
%define ZUC256_INIT asm_Zuc256Initialization_16_avx512
%define ZUC128_REMAINDER_16 asm_Eia3RemainderAVX512_16
%define ZUC256_REMAINDER_16 asm_Eia3_256_RemainderAVX512_16
%define ZUC_KEYGEN64B_16 asm_ZucGenKeystream64B_16_avx512
%define ZUC_KEYGEN8B_16 asm_ZucGenKeystream8B_16_avx512
%define ZUC_KEYGEN4B_16 asm_ZucGenKeystream4B_16_avx512
%define ZUC_KEYGEN_16 asm_ZucGenKeystream_16_avx512
%define ZUC_KEYGEN64B_SKIP8_16 asm_ZucGenKeystream64B_16_skip8_avx512
%define ZUC_KEYGEN8B_SKIP8_16 asm_ZucGenKeystream8B_16_skip8_avx512
%define ZUC_KEYGEN_SKIP8_16 asm_ZucGenKeystream_16_skip8_avx512
%define ZUC_ROUND64B_16 asm_Eia3Round64BAVX512_16
%define ZUC_EIA3_N64B asm_Eia3_Nx64B_AVX512_16
%endif

mksection .rodata
default rel

align 64
EK_d64:
dd	0x0044D700, 0x0026BC00, 0x00626B00, 0x00135E00, 0x00578900, 0x0035E200, 0x00713500, 0x0009AF00
dd	0x004D7800, 0x002F1300, 0x006BC400, 0x001AF100, 0x005E2600, 0x003C4D00, 0x00789A00, 0x0047AC00

align 64
EK256_d64:
dd      0x00220000, 0x002F0000, 0x00240000, 0x002A0000, 0x006D0000, 0x00400000, 0x00400000, 0x00400000
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000, 0x00400000, 0x00520000, 0x00100000, 0x00300000

align 64
EK256_EIA3_4:
dd      0x00220000, 0x002F0000, 0x00250000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000

align 64
EK256_EIA3_8:
dd      0x00230000, 0x002F0000, 0x00240000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000

align 64
EK256_EIA3_16:
dd      0x00230000, 0x002F0000, 0x00250000, 0x002A0000,
dd      0x006D0000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00400000, 0x00400000, 0x00400000,
dd      0x00400000, 0x00520000, 0x00100000, 0x00300000

align 64
shuf_mask_key:
dd      0x00FFFFFF, 0x01FFFFFF, 0x02FFFFFF, 0x03FFFFFF, 0x04FFFFFF, 0x05FFFFFF, 0x06FFFFFF, 0x07FFFFFF,
dd      0x08FFFFFF, 0x09FFFFFF, 0x0AFFFFFF, 0x0BFFFFFF, 0x0CFFFFFF, 0x0DFFFFFF, 0x0EFFFFFF, 0x0FFFFFFF,

align 64
shuf_mask_iv:
dd      0xFFFFFF00, 0xFFFFFF01, 0xFFFFFF02, 0xFFFFFF03, 0xFFFFFF04, 0xFFFFFF05, 0xFFFFFF06, 0xFFFFFF07,
dd      0xFFFFFF08, 0xFFFFFF09, 0xFFFFFF0A, 0xFFFFFF0B, 0xFFFFFF0C, 0xFFFFFF0D, 0xFFFFFF0E, 0xFFFFFF0F,

align 64
shuf_mask_key256_first_high:
dd      0x00FFFFFF, 0x01FFFFFF, 0x02FFFFFF, 0x03FFFFFF, 0x04FFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
dd      0x08FFFFFF, 0x09FFFFFF, 0xFFFFFFFF, 0x0BFFFFFF, 0x0CFFFFFF, 0x0DFFFFFF, 0x0EFFFFFF, 0x0FFFFFFF,

align 64
shuf_mask_key256_first_low:
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF05FF, 0xFFFF06FF, 0xFFFF07FF,
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF0AFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,

align 64
shuf_mask_key256_second:
dd      0xFFFF0500, 0xFFFF0601, 0xFFFF0702, 0xFFFF0803, 0xFFFF0904, 0xFFFFFF0A, 0xFFFFFF0B, 0xFFFFFFFF,
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF0C, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF0FFFFF, 0xFF0F0E0D,

align 64
shuf_mask_iv256_first_high:
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00FFFFFF, 0x01FFFFFF, 0x0AFFFFFF,
dd      0xFFFFFFFF, 0xFFFFFFFF, 0x05FFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,

align 64
shuf_mask_iv256_first_low:
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF02,
dd      0xFFFF030B, 0xFFFF0C04, 0xFFFFFFFF, 0xFFFF060D, 0xFFFF070E, 0xFFFF0F08, 0xFFFFFF09, 0xFFFFFFFF,

align 64
shuf_mask_iv256_second:
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF01FFFF, 0xFF02FFFF, 0xFF03FFFF,
dd      0xFF04FFFF, 0xFF05FFFF, 0xFF06FFFF, 0xFF07FFFF, 0xFF08FFFF, 0xFFFFFFFF, 0xFFFF00FF, 0xFFFFFFFF,

align 64
key_mask_low_4:
dq      0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff
dq      0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xff0fffffffff0fff

align 64
iv_mask_low_6:
dq      0x3f3f3f3f3f3f3fff, 0x000000000000003f

align 64
mask31:
dd	0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
dd	0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
dd	0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
dd	0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,

align 64
swap_mask:
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c

align 64
S1_S0_shuf:
db      0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F
db      0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F
db      0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F
db      0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F

align 64
S0_S1_shuf:
db      0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
db      0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
db      0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
db      0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,

align 64
rev_S1_S0_shuf:
db      0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F
db      0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F
db      0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F
db      0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F

align 64
rev_S0_S1_shuf:
db      0x08, 0x00, 0x09, 0x01, 0x0A, 0x02, 0x0B, 0x03, 0x0C, 0x04, 0x0D, 0x05, 0x0E, 0x06, 0x0F, 0x07
db      0x08, 0x00, 0x09, 0x01, 0x0A, 0x02, 0x0B, 0x03, 0x0C, 0x04, 0x0D, 0x05, 0x0E, 0x06, 0x0F, 0x07
db      0x08, 0x00, 0x09, 0x01, 0x0A, 0x02, 0x0B, 0x03, 0x0C, 0x04, 0x0D, 0x05, 0x0E, 0x06, 0x0F, 0x07
db      0x08, 0x00, 0x09, 0x01, 0x0A, 0x02, 0x0B, 0x03, 0x0C, 0x04, 0x0D, 0x05, 0x0E, 0x06, 0x0F, 0x07

align 64
bit_reverse_table_l:
db	0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e, 0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f
db	0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e, 0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f
db	0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e, 0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f
db	0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e, 0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f

align 64
bit_reverse_table_h:
db	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0
db	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0
db	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0
db	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0

align 64
bit_reverse_and_table:
db	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f
db	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f
db	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f
db	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f

align 64
bit_reverse_table:
times 8 db      0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80

align 64
shuf_mask_tags_0_1_2_3:
dd      0x01, 0x05, 0x09, 0x0D, 0x11, 0x15, 0x19, 0x1D, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
dd      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x05, 0x09, 0x0D, 0x11, 0x15, 0x19, 0x1D

align 64
shuf_mask_tags_0_4_8_12:
dd      0x01, 0x11, 0xFF, 0xFF, 0x05, 0x15, 0xFF, 0xFF, 0x09, 0x19, 0xFF, 0xFF, 0x0D, 0x1D, 0xFF, 0xFF
dd      0xFF, 0xFF, 0x01, 0x11, 0xFF, 0xFF, 0x05, 0x15, 0xFF, 0xFF, 0x09, 0x19, 0xFF, 0xFF, 0x0D, 0x1D

align 64
all_ffs:
dw      0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
dw      0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
dw      0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
dw      0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff

align 64
all_threes:
dw      0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003
dw      0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003

align 64
all_fffcs:
dw      0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc
dw      0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc
dw      0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc
dw      0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc, 0xfffc

align 64
all_3fs:
dw      0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f
dw      0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f

align 16
bit_mask_table:
db      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
db      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80
db      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0
db      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0
db      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0
db      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8
db      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc
db      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe

byte64_len_to_mask_table:
        dq      0xffffffffffffffff, 0x0000000000000001
        dq      0x0000000000000003, 0x0000000000000007
        dq      0x000000000000000f, 0x000000000000001f
        dq      0x000000000000003f, 0x000000000000007f
        dq      0x00000000000000ff, 0x00000000000001ff
        dq      0x00000000000003ff, 0x00000000000007ff
        dq      0x0000000000000fff, 0x0000000000001fff
        dq      0x0000000000003fff, 0x0000000000007fff
        dq      0x000000000000ffff, 0x000000000001ffff
        dq      0x000000000003ffff, 0x000000000007ffff
        dq      0x00000000000fffff, 0x00000000001fffff
        dq      0x00000000003fffff, 0x00000000007fffff
        dq      0x0000000000ffffff, 0x0000000001ffffff
        dq      0x0000000003ffffff, 0x0000000007ffffff
        dq      0x000000000fffffff, 0x000000001fffffff
        dq      0x000000003fffffff, 0x000000007fffffff
        dq      0x00000000ffffffff, 0x00000001ffffffff
        dq      0x00000003ffffffff, 0x00000007ffffffff
        dq      0x0000000fffffffff, 0x0000001fffffffff
        dq      0x0000003fffffffff, 0x0000007fffffffff
        dq      0x000000ffffffffff, 0x000001ffffffffff
        dq      0x000003ffffffffff, 0x000007ffffffffff
        dq      0x00000fffffffffff, 0x00001fffffffffff
        dq      0x00003fffffffffff, 0x00007fffffffffff
        dq      0x0000ffffffffffff, 0x0001ffffffffffff
        dq      0x0003ffffffffffff, 0x0007ffffffffffff
        dq      0x000fffffffffffff, 0x001fffffffffffff
        dq      0x003fffffffffffff, 0x007fffffffffffff
        dq      0x00ffffffffffffff, 0x01ffffffffffffff
        dq      0x03ffffffffffffff, 0x07ffffffffffffff
        dq      0x0fffffffffffffff, 0x1fffffffffffffff
        dq      0x3fffffffffffffff, 0x7fffffffffffffff
        dq      0xffffffffffffffff

align 64
add_64:
dq      64, 64, 64, 64, 64, 64, 64, 64

align 32
all_512w:
dw      512, 512, 512, 512, 512, 512, 512, 512
dw      512, 512, 512, 512, 512, 512, 512, 512

align 64
bswap_mask:
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c
db      0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04
db      0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c

align 64
all_31w:
dw      31, 31, 31, 31, 31, 31, 31, 31
dw      31, 31, 31, 31, 31, 31, 31, 31

align 64
all_ffe0w:
dw      0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0
dw      0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0

align 32
permw_mask:
dw      0, 4, 8, 12, 1, 5, 8, 13, 2, 6, 10, 14, 3, 7, 11, 15

extr_bits_0_4_8_12:
db      00010001b, 00010001b, 00000000b, 00000000b

extr_bits_1_5_9_13:
db      00100010b, 00100010b, 00000000b, 00000000b

extr_bits_2_6_10_14:
db      01000100b, 01000100b, 00000000b, 00000000b

extr_bits_3_7_11_15:
db      10001000b, 10001000b, 00000000b, 00000000b

alignr_mask:
dw      0xffff, 0xffff, 0xffff, 0xffff
dw      0x0000, 0xffff, 0xffff, 0xffff
dw      0xffff, 0x0000, 0xffff, 0xffff
dw      0x0000, 0x0000, 0xffff, 0xffff
dw      0xffff, 0xffff, 0x0000, 0xffff
dw      0x0000, 0xffff, 0x0000, 0xffff
dw      0xffff, 0x0000, 0x0000, 0xffff
dw      0x0000, 0x0000, 0x0000, 0xffff
dw      0xffff, 0xffff, 0xffff, 0x0000
dw      0x0000, 0xffff, 0xffff, 0x0000
dw      0xffff, 0x0000, 0xffff, 0x0000
dw      0x0000, 0x0000, 0xffff, 0x0000
dw      0xffff, 0xffff, 0x0000, 0x0000
dw      0x0000, 0xffff, 0x0000, 0x0000
dw      0xffff, 0x0000, 0x0000, 0x0000
dw      0x0000, 0x0000, 0x0000, 0x0000

mov_mask:
db      10101010b, 10101011b, 10101110b, 10101111b
db      10111010b, 10111011b, 10111110b, 10111111b
db      11101010b, 11101011b, 11101110b, 11101111b
db      11111010b, 11111011b, 11111110b, 11111111b

;; Calculate address for next bytes of keystream (KS)
;; Memory for KS is laid out in the following way:
;; - There are 128 bytes of KS for each buffer spread in chunks of 16 bytes,
;;   interleaving with KS from other 3 buffers, every 512 bytes
;; - There are 16 bytes of KS every 64 bytes, for every buffer

;; - To access the 512-byte chunk, containing the 128 bytes of KS for the 4 buffers,
;;   lane4_idx
;; - To access the next 16 bytes of KS for a buffer, bytes16_idx is used
;; - To access a 16-byte chunk inside a 64-byte chunk, ks_idx is used
%define GET_KS(base, lane4_idx, bytes16_idx, ks_idx) (base + lane4_idx * 512 + bytes16_idx * 64 + ks_idx * 16)

mksection .text
align 64

%ifdef LINUX
%define arg1 rdi
%define arg2 rsi
%define arg3 rdx
%define arg4 rcx
%define arg5 r8
%define arg6 r9d
%else
%define arg1 rcx
%define arg2 rdx
%define arg3 r8
%define arg4 r9
%define arg5 [rsp + 40]
%define arg6 [rsp + 48]
%endif

%define OFS_R1  (16*(4*16))
%define OFS_R2  (OFS_R1 + (4*16))

%ifidn __OUTPUT_FORMAT__, win64
        %define XMM_STORAGE     16*10
        %define GP_STORAGE      8*8
%else
        %define XMM_STORAGE     0
        %define GP_STORAGE      6*8
%endif
%define LANE_STORAGE    64

%define VARIABLE_OFFSET XMM_STORAGE + GP_STORAGE + LANE_STORAGE
%define GP_OFFSET XMM_STORAGE

%macro FUNC_SAVE 0
        mov     rax, rsp
        sub     rsp, VARIABLE_OFFSET
        and     rsp, ~15

%ifidn __OUTPUT_FORMAT__, win64
        ; xmm6:xmm15 need to be maintained for Windows
        vmovdqa [rsp + 0*16], xmm6
        vmovdqa [rsp + 1*16], xmm7
        vmovdqa [rsp + 2*16], xmm8
        vmovdqa [rsp + 3*16], xmm9
        vmovdqa [rsp + 4*16], xmm10
        vmovdqa [rsp + 5*16], xmm11
        vmovdqa [rsp + 6*16], xmm12
        vmovdqa [rsp + 7*16], xmm13
        vmovdqa [rsp + 8*16], xmm14
        vmovdqa [rsp + 9*16], xmm15
        mov     [rsp + GP_OFFSET + 48], rdi
        mov     [rsp + GP_OFFSET + 56], rsi
%endif
        mov     [rsp + GP_OFFSET],      r12
        mov     [rsp + GP_OFFSET + 8],  r13
        mov     [rsp + GP_OFFSET + 16], r14
        mov     [rsp + GP_OFFSET + 24], r15
        mov     [rsp + GP_OFFSET + 32], rbx
        mov     [rsp + GP_OFFSET + 40], rax ;; rsp pointer
%endmacro

%macro FUNC_RESTORE 0

%ifidn __OUTPUT_FORMAT__, win64
        vmovdqa xmm6,  [rsp + 0*16]
        vmovdqa xmm7,  [rsp + 1*16]
        vmovdqa xmm8,  [rsp + 2*16]
        vmovdqa xmm9,  [rsp + 3*16]
        vmovdqa xmm10, [rsp + 4*16]
        vmovdqa xmm11, [rsp + 5*16]
        vmovdqa xmm12, [rsp + 6*16]
        vmovdqa xmm13, [rsp + 7*16]
        vmovdqa xmm14, [rsp + 8*16]
        vmovdqa xmm15, [rsp + 9*16]
        mov     rdi, [rsp + GP_OFFSET + 48]
        mov     rsi, [rsp + GP_OFFSET + 56]
%endif
        mov     r12, [rsp + GP_OFFSET]
        mov     r13, [rsp + GP_OFFSET + 8]
        mov     r14, [rsp + GP_OFFSET + 16]
        mov     r15, [rsp + GP_OFFSET + 24]
        mov     rbx, [rsp + GP_OFFSET + 32]
        mov     rsp, [rsp + GP_OFFSET + 40]
%endmacro

; This macro reorder the LFSR registers
; after N rounds (1 <= N <= 15), since the registers
; are shifted every round
;
; The macro clobbers ZMM0-15
;
%macro REORDER_LFSR 3
%define %%STATE      %1
%define %%NUM_ROUNDS %2
%define %%LANE_MASK  %3

%if %%NUM_ROUNDS != 16
%assign i 0
%rep 16
    vmovdqa32 APPEND(zmm,i){%%LANE_MASK}, [%%STATE + 64*i]
%assign i (i+1)
%endrep

%assign i 0
%assign j %%NUM_ROUNDS
%rep 16
    vmovdqa32 [%%STATE + 64*i]{%%LANE_MASK}, APPEND(zmm,j)
%assign i (i+1)
%assign j ((j+1) % 16)
%endrep
%endif ;; %%NUM_ROUNDS != 16

%endmacro

;
; Perform a partial 16x16 transpose (as opposed to a full 16x16 transpose),
; where the output is chunks of 16 bytes from 4 different buffers interleaved
; in each register (all ZMM registers)
;
; Input:
; a0 a1 a2 a3 a4 a5 a6 a7 .... a15
; b0 b1 b2 b3 b4 b5 b6 b7 .... b15
; c0 c1 c2 c3 c4 c5 c6 c7 .... c15
; d0 d1 d2 d3 d4 d5 d6 d7 .... d15
;
; Output:
; a0 b0 c0 d0 a4 b4 c4 d4 .... d12
; a1 b1 c1 d1 a5 b5 c5 d5 .... d13
; a2 b2 c2 d2 a6 b6 c6 d6 .... d14
; a3 b3 c3 d3 a7 b7 c7 d7 .... d15
;
%macro TRANSPOSE16_U32_INTERLEAVED 26
%define %%IN00  %1 ; [in/out] Bytes 0-3 for all buffers (in) / Bytes 0-15 for buffers 3,7,11,15 (out)
%define %%IN01  %2 ; [in/out] Bytes 4-7 for all buffers (in) / Bytes 16-31 for buffers 3,7,11,15 (out)
%define %%IN02  %3 ; [in/out] Bytes 8-11 for all buffers (in) / Bytes 32-47 for buffers 3,7,11,15 (out)
%define %%IN03  %4 ; [in/out] Bytes 12-15 for all buffers (in) / Bytes 48-63 for buffers 3,7,11,15 (out)
%define %%IN04  %5 ; [in/clobbered] Bytes 16-19 for all buffers (in)
%define %%IN05  %6 ; [in/clobbered] Bytes 20-23 for all buffers (in)
%define %%IN06  %7 ; [in/clobbered] Bytes 24-27 for all buffers (in)
%define %%IN07  %8 ; [in/clobbered] Bytes 28-31 for all buffers (in)
%define %%IN08  %9 ; [in/clobbered] Bytes 32-35 for all buffers (in)
%define %%IN09 %10 ; [in/clobbered] Bytes 36-39 for all buffers (in)
%define %%IN10 %11 ; [in/clobbered] Bytes 40-43 for all buffers (in)
%define %%IN11 %12 ; [in/clobbered] Bytes 44-47 for all buffers (in)
%define %%IN12 %13 ; [in/out] Bytes 48-51 for all buffers (in) / Bytes 0-15 for buffers 2,6,10,14 (out)
%define %%IN13 %14 ; [in/out] Bytes 52-55 for all buffers (in) / Bytes 16-31 for buffers 2,6,10,14 (out)
%define %%IN14 %15 ; [in/out] Bytes 56-59 for all buffers (in) / Bytes 32-47 for buffers 2,6,10,14 (out)
%define %%IN15 %16 ; [in/out] Bytes 60-63 for all buffers (in) / Bytes 48-63 for buffers 2,6,10,14 (out)
%define %%T0   %17 ; [out] Bytes 32-47 for buffers 1,5,9,13 (out)
%define %%T1   %18 ; [out] Bytes 48-63 for buffers 1,5,9,13 (out)
%define %%T2   %19 ; [out] Bytes 32-47 for buffers 0,4,8,12 (out)
%define %%T3   %20 ; [out] Bytes 48-63 for buffers 0,4,8,12 (out)
%define %%K0   %21 ; [out] Bytes 0-15 for buffers 1,5,9,13 (out)
%define %%K1   %22 ; [out] Bytes 16-31for buffers 1,5,9,13 (out)
%define %%K2   %23 ; [out] Bytes 0-15 for buffers 0,4,8,12 (out)
%define %%K3   %24 ; [out] Bytes 16-31 for buffers 0,4,8,12 (out)
%define %%K4   %25 ; [clobbered] Temporary register
%define %%K5   %26 ; [clobbered] Temporary register

        vpunpckldq      %%K0, %%IN00, %%IN01
        vpunpckhdq      %%K1, %%IN00, %%IN01
        vpunpckldq      %%T0, %%IN02, %%IN03
        vpunpckhdq      %%T1, %%IN02, %%IN03

        vpunpckldq      %%IN00, %%IN04, %%IN05
        vpunpckhdq      %%IN01, %%IN04, %%IN05
        vpunpckldq      %%IN02, %%IN06, %%IN07
        vpunpckhdq      %%IN03, %%IN06, %%IN07

        vpunpcklqdq     %%K2, %%K0, %%T0
        vpunpckhqdq     %%K3, %%K0, %%T0
        vpunpcklqdq     %%T2, %%K1, %%T1
        vpunpckhqdq     %%T3, %%K1, %%T1

        vpunpcklqdq     %%K0, %%IN00, %%IN02
        vpunpckhqdq     %%K1, %%IN00, %%IN02
        vpunpcklqdq     %%T0, %%IN01, %%IN03
        vpunpckhqdq     %%T1, %%IN01, %%IN03

        vpunpckldq      %%K4, %%IN08, %%IN09
        vpunpckhdq      %%K5, %%IN08, %%IN09
        vpunpckldq      %%IN04, %%IN10, %%IN11
        vpunpckhdq      %%IN05, %%IN10, %%IN11
        vpunpckldq      %%IN06, %%IN12, %%IN13
        vpunpckhdq      %%IN07, %%IN12, %%IN13
        vpunpckldq      %%IN10, %%IN14, %%IN15
        vpunpckhdq      %%IN11, %%IN14, %%IN15

        vpunpcklqdq     %%IN12, %%K4, %%IN04
        vpunpckhqdq     %%IN13, %%K4, %%IN04
        vpunpcklqdq     %%IN14, %%K5, %%IN05
        vpunpckhqdq     %%IN15, %%K5, %%IN05
        vpunpcklqdq     %%IN00, %%IN06, %%IN10
        vpunpckhqdq     %%IN01, %%IN06, %%IN10
        vpunpcklqdq     %%IN02, %%IN07, %%IN11
        vpunpckhqdq     %%IN03, %%IN07, %%IN11
%endmacro

;
; Perform a partial 4x16 transpose
; where the output is chunks of 16 bytes from 4 different buffers interleaved
; in each register (all ZMM registers)
;
; Input:
; a0 a1 a2 a3 a4 a5 a6 a7 .... a15
; b0 b1 b2 b3 b4 b5 b6 b7 .... b15
; c0 c1 c2 c3 c4 c5 c6 c7 .... c15
; d0 d1 d2 d3 d4 d5 d6 d7 .... d15
;
; Output:
; a0 b0 c0 d0 a4 b4 c4 d4 .... d12
; a1 b1 c1 d1 a5 b5 c5 d5 .... d13
; a2 b2 c2 d2 a6 b6 c6 d6 .... d14
; a3 b3 c3 d3 a7 b7 c7 d7 .... d15
;
%macro TRANSPOSE4_U32_INTERLEAVED 8
%define %%IN00  %1 ; [in/out] Bytes 0-3 for all buffers (in) / Bytes 0-15 for buffers 0,4,8,12 (out)
%define %%IN01  %2 ; [in/out] Bytes 4-7 for all buffers (in) / Bytes 0-15 for buffers 1,5,9,13 (out)
%define %%IN02  %3 ; [in/out] Bytes 8-11 for all buffers (in) / Bytes 0-15 for buffers 2,6,10,14 (out)
%define %%IN03  %4 ; [in/out] Bytes 12-15 for all buffers (in) / Bytes 0-15 for buffers 3,7,11,15 (out)
%define %%T0   %5 ; [clobbered] Temporary ZMM register
%define %%T1   %6 ; [clobbered] Temporary ZMM register
%define %%K0   %7 ; [clobbered] Temporary ZMM register
%define %%K1   %8 ; [clobbered] Temporary ZMM register

        vpunpckldq      %%K0, %%IN00, %%IN01
        vpunpckhdq      %%K1, %%IN00, %%IN01
        vpunpckldq      %%T0, %%IN02, %%IN03
        vpunpckhdq      %%T1, %%IN02, %%IN03

        vpunpcklqdq     %%IN00, %%K0, %%T0
        vpunpckhqdq     %%IN01, %%K0, %%T0
        vpunpcklqdq     %%IN02, %%K1, %%T1
        vpunpckhqdq     %%IN03, %%K1, %%T1

%endmacro

;
; Calculates X0-X3 from LFSR registers
;
%macro  BITS_REORG16 16-17
%define %%STATE         %1  ; [in] ZUC state
%define %%ROUND_NUM     %2  ; [in] Round number
%define %%LANE_MASK     %3  ; [in] Mask register with lanes to update
%define %%LFSR_0        %4  ; [clobbered] LFSR_0
%define %%LFSR_2        %5  ; [clobbered] LFSR_2
%define %%LFSR_5        %6  ; [clobbered] LFSR_5
%define %%LFSR_7        %7  ; [clobbered] LFSR_7
%define %%LFSR_9        %8  ; [clobbered] LFSR_9
%define %%LFSR_11       %9  ; [clobbered] LFSR_11
%define %%LFSR_14       %10 ; [clobbered] LFSR_14
%define %%LFSR_15       %11 ; [clobbered] LFSR_15
%define %%ZTMP          %12 ; [clobbered] Temporary ZMM register
%define %%BLEND_KMASK   %13 ; [in] Blend K-mask
%define %%X0            %14 ; [out] ZMM register containing X0 of all lanes
%define %%X1            %15 ; [out] ZMM register containing X1 of all lanes
%define %%X2            %16 ; [out] ZMM register containing X2 of all lanes
%define %%X3            %17 ; [out] ZMM register containing X3 of all lanes (only for work mode)

        vmovdqa64   %%LFSR_15, [%%STATE + ((15 + %%ROUND_NUM) % 16)*64]
        vmovdqa64   %%LFSR_14, [%%STATE + ((14 + %%ROUND_NUM) % 16)*64]
        vmovdqa64   %%LFSR_11, [%%STATE + ((11 + %%ROUND_NUM) % 16)*64]
        vmovdqa64   %%LFSR_9,  [%%STATE + (( 9 + %%ROUND_NUM) % 16)*64]
        vmovdqa64   %%LFSR_7,  [%%STATE + (( 7 + %%ROUND_NUM) % 16)*64]
        vmovdqa64   %%LFSR_5,  [%%STATE + (( 5 + %%ROUND_NUM) % 16)*64]
%if (%0 == 17) ; Only needed when generating X3 (for "working" mode)
        vmovdqa64   %%LFSR_2,  [%%STATE + (( 2 + %%ROUND_NUM) % 16)*64]
        vmovdqa64   %%LFSR_0,  [%%STATE + (( 0 + %%ROUND_NUM) % 16)*64]
%endif

%if USE_GFNI_VAES_VPCLMUL == 1
        vpsrld  %%LFSR_15, 15
        vpslld  %%LFSR_14, 16
        vpslld  %%LFSR_9, 1
        vpslld  %%LFSR_5, 1
        vpshldd %%X0, %%LFSR_15, %%LFSR_14, 16
        vpshldd %%X1, %%LFSR_11, %%LFSR_9, 16
        vpshldd %%X2, %%LFSR_7, %%LFSR_5, 16
%if (%0 == 17)
        vpslld  %%LFSR_0, 1
        vpshldd %%X3, %%LFSR_2, %%LFSR_0, 16
%endif
%else ; USE_GFNI_VAES_VPCLMUL == 1
    vpxorq      %%ZTMP, %%ZTMP
    vpslld      %%LFSR_15, 1
    vpblendmw   %%ZTMP{%%BLEND_KMASK}, %%LFSR_14, %%ZTMP
    vpblendmw   %%X0{%%BLEND_KMASK}, %%ZTMP, %%LFSR_15
    vpslld      %%LFSR_11, 16
    vpsrld      %%LFSR_9, 15
    vporq       %%X1, %%LFSR_11, %%LFSR_9
    vpslld      %%LFSR_7, 16
    vpsrld      %%LFSR_5, 15
    vporq       %%X2, %%LFSR_7, %%LFSR_5
%if (%0 == 17)
    vpslld      %%LFSR_2, 16
    vpsrld      %%LFSR_0, 15
    vporq       %%X3, %%LFSR_2, %%LFSR_0 ; Store BRC_X3 in ZMM register
%endif ; %0 == 17
%endif ; USE_GFNI_VAES_VPCLMUL == 1
%endmacro

;
; Updates R1-R2, using X0-X3 and generates W (if needed)
;
%macro NONLIN_FUN16  13-14
%define %%STATE     %1  ; [in] ZUC state
%define %%LANE_MASK %2  ; [in] Mask register with lanes to update
%define %%X0        %3  ; [in] ZMM register containing X0 of all lanes
%define %%X1        %4  ; [in] ZMM register containing X1 of all lanes
%define %%X2        %5  ; [in] ZMM register containing X2 of all lanes
%define %%R1        %6  ; [in/out] ZMM register to contain R1 for all lanes
%define %%R2        %7  ; [in/out] ZMM register to contain R2 for all lanes
%define %%ZTMP1     %8  ; [clobbered] Temporary ZMM register
%define %%ZTMP2     %9  ; [clobbered] Temporary ZMM register
%define %%ZTMP3     %10 ; [clobbered] Temporary ZMM register
%define %%ZTMP4     %11 ; [clobbered] Temporary ZMM register
%define %%ZTMP5     %12 ; [clobbered] Temporary ZMM register
%define %%ZTMP6     %13 ; [clobbered] Temporary ZMM register
%define %%W         %14 ; [out] ZMM register to contain W for all lanes

%define %%W1 %%ZTMP5
%define %%W2 %%ZTMP6

%if (%0 == 14)
    vpxorq      %%W, %%X0, %%R1
    vpaddd      %%W, %%R2    ; W = (BRC_X0 ^ F_R1) + F_R2
%endif

    vpaddd      %%W1, %%R1, %%X1    ; W1 = F_R1 + BRC_X1
    vpxorq      %%W2, %%R2, %%X2    ; W2 = F_R2 ^ BRC_X2

%if USE_GFNI_VAES_VPCLMUL == 1
    vpshldd     %%ZTMP1, %%W1, %%W2, 16
    vpshldd     %%ZTMP2, %%W2, %%W1, 16
%else
    vpslld      %%ZTMP3, %%W1, 16
    vpsrld      %%ZTMP4, %%W1, 16
    vpslld      %%ZTMP5, %%W2, 16
    vpsrld      %%ZTMP6, %%W2, 16
    vporq       %%ZTMP1, %%ZTMP3, %%ZTMP6
    vporq       %%ZTMP2, %%ZTMP4, %%ZTMP5
%endif

    vprold   %%ZTMP3, %%ZTMP1, 10
    vprold   %%ZTMP4, %%ZTMP1, 18
    vprold   %%ZTMP5, %%ZTMP1, 24
    vprold   %%ZTMP6, %%ZTMP1, 2
    ; ZMM1 = U = L1(P)
    vpternlogq  %%ZTMP1, %%ZTMP3, %%ZTMP4, 0x96 ; (A ^ B) ^ C
    vpternlogq  %%ZTMP1, %%ZTMP5, %%ZTMP6, 0x96 ; (A ^ B) ^ C

    vprold   %%ZTMP3, %%ZTMP2, 8
    vprold   %%ZTMP4, %%ZTMP2, 14
    vprold   %%ZTMP5, %%ZTMP2, 22
    vprold   %%ZTMP6, %%ZTMP2, 30
    ; ZMM2 = V = L2(Q)
    vpternlogq  %%ZTMP2, %%ZTMP3, %%ZTMP4, 0x96 ; (A ^ B) ^ C
    vpternlogq  %%ZTMP2, %%ZTMP5, %%ZTMP6, 0x96 ; (A ^ B) ^ C

    ; Shuffle U and V to have all S0 lookups in XMM1 and all S1 lookups in XMM2

    ; Compress all S0 and S1 input values in each register
    ; S0: Bytes 0-7,16-23,32-39,48-55 S1: Bytes 8-15,24-31,40-47,56-63
    vpshufb     %%ZTMP1, [rel S0_S1_shuf]
    ; S1: Bytes 0-7,16-23,32-39,48-55 S0: Bytes 8-15,24-31,40-47,56-63
    vpshufb     %%ZTMP2, [rel S1_S0_shuf]

    vshufpd     %%ZTMP3, %%ZTMP1, %%ZTMP2, 0xAA ; All S0 input values
    vshufpd     %%ZTMP4, %%ZTMP2, %%ZTMP1, 0xAA ; All S1 input values

    ; Compute S0 and S1 values
    S0_comput_AVX512  %%ZTMP3, %%ZTMP1, %%ZTMP2, USE_GFNI_VAES_VPCLMUL
    S1_comput_AVX512  %%ZTMP4, %%ZTMP1, %%ZTMP2, %%ZTMP5, %%ZTMP6, USE_GFNI_VAES_VPCLMUL

    ; Need to shuffle back %%ZTMP1 & %%ZTMP2 before storing output
    ; (revert what was done before S0 and S1 computations)
    vshufpd     %%ZTMP1, %%ZTMP3, %%ZTMP4, 0xAA
    vshufpd     %%ZTMP2, %%ZTMP4, %%ZTMP3, 0xAA

    vpshufb     %%R1, %%ZTMP1, [rel rev_S0_S1_shuf]
    vpshufb     %%R2, %%ZTMP2, [rel rev_S1_S0_shuf]
%endmacro

;
; Function to store 64 bytes of keystream for 16 buffers
; Note: all the 64*16 bytes are not store contiguously,
;       the first 256 bytes (containing 64 bytes from 4 buffers)
;       are stored in the first half of the first 512 bytes,
;       then there is a gap of 256 bytes and then the next 256 bytes
;       are written, and so on.
;
%macro  STORE_KSTR16 18-24
%define %%KS          %1  ; [in] Pointer to keystream
%define %%DATA64B_L0  %2  ; [in] 64 bytes of keystream for lane 0
%define %%DATA64B_L1  %3  ; [in] 64 bytes of keystream for lane 1
%define %%DATA64B_L2  %4  ; [in] 64 bytes of keystream for lane 2
%define %%DATA64B_L3  %5  ; [in] 64 bytes of keystream for lane 3
%define %%DATA64B_L4  %6  ; [in] 64 bytes of keystream for lane 4
%define %%DATA64B_L5  %7  ; [in] 64 bytes of keystream for lane 5
%define %%DATA64B_L6  %8  ; [in] 64 bytes of keystream for lane 6
%define %%DATA64B_L7  %9  ; [in] 64 bytes of keystream for lane 7
%define %%DATA64B_L8  %10 ; [in] 64 bytes of keystream for lane 8
%define %%DATA64B_L9  %11 ; [in] 64 bytes of keystream for lane 9
%define %%DATA64B_L10 %12 ; [in] 64 bytes of keystream for lane 10
%define %%DATA64B_L11 %13 ; [in] 64 bytes of keystream for lane 11
%define %%DATA64B_L12 %14 ; [in] 64 bytes of keystream for lane 12
%define %%DATA64B_L13 %15 ; [in] 64 bytes of keystream for lane 13
%define %%DATA64B_L14 %16 ; [in] 64 bytes of keystream for lane 14
%define %%DATA64B_L15 %17 ; [in] 64 bytes of keystream for lane 15
%define %%KEY_OFF     %18 ; [in] Offset to start writing Keystream
%define %%LANE_MASK   %19 ; [in] Lane mask with lanes to generate all keystream words
%define %%ALIGN_MASK  %20 ; [in] Address with alignr masks
%define %%MOV_MASK    %21 ; [in] Address with move masks
%define %%TMP         %22 ; [in] Temporary GP register
%define %%KMASK1      %23 ; [clobbered] Temporary K mask
%define %%KMASK2      %24 ; [clobbered] Temporary K mask

%if (%0 == 18)
    vmovdqu64   [%%KS + %%KEY_OFF*4], %%DATA64B_L0
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 64], %%DATA64B_L1
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 2*64], %%DATA64B_L2
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 3*64], %%DATA64B_L3

    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512], %%DATA64B_L4
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512 + 64], %%DATA64B_L5
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512 + 2*64], %%DATA64B_L6
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512 + 3*64], %%DATA64B_L7

    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*2], %%DATA64B_L8
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*2 + 64], %%DATA64B_L9
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*2 + 64*2], %%DATA64B_L10
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*2 + 64*3], %%DATA64B_L11

    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*3], %%DATA64B_L12
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*3 + 64], %%DATA64B_L13
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*3 + 64*2], %%DATA64B_L14
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*3 + 64*3], %%DATA64B_L15
%else
    pext        DWORD(%%TMP), DWORD(%%LANE_MASK), [rel extr_bits_0_4_8_12]
    kmovq       %%KMASK1, [%%ALIGN_MASK + 8*%%TMP]
    kmovb       %%KMASK2, [%%MOV_MASK + %%TMP]
    ; Shifting left 8 bytes of KS for lanes which first 8 bytes are skipped
    vpalignr    %%DATA64B_L3{%%KMASK1}, %%DATA64B_L3, %%DATA64B_L2, 8
    vpalignr    %%DATA64B_L2{%%KMASK1}, %%DATA64B_L2, %%DATA64B_L1, 8
    vpalignr    %%DATA64B_L1{%%KMASK1}, %%DATA64B_L1, %%DATA64B_L0, 8
    vpalignr    %%DATA64B_L0{%%KMASK1}, %%DATA64B_L0, %%DATA64B_L3, 8
    vmovdqu64   [%%KS + %%KEY_OFF*4]{%%KMASK2}, %%DATA64B_L0
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 64], %%DATA64B_L1
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 2*64], %%DATA64B_L2
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 3*64], %%DATA64B_L3

    pext        DWORD(%%TMP), DWORD(%%LANE_MASK), [rel extr_bits_1_5_9_13]
    kmovq       %%KMASK1, [%%ALIGN_MASK + 8*%%TMP]
    kmovb       %%KMASK2, [%%MOV_MASK + %%TMP]
    vpalignr    %%DATA64B_L7{%%KMASK1}, %%DATA64B_L7, %%DATA64B_L6, 8
    vpalignr    %%DATA64B_L6{%%KMASK1}, %%DATA64B_L6, %%DATA64B_L5, 8
    vpalignr    %%DATA64B_L5{%%KMASK1}, %%DATA64B_L5, %%DATA64B_L4, 8
    vpalignr    %%DATA64B_L4{%%KMASK1}, %%DATA64B_L4, %%DATA64B_L7, 8
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512]{%%KMASK2}, %%DATA64B_L4
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512 + 64], %%DATA64B_L5
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512 + 64*2], %%DATA64B_L6
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512 + 64*3], %%DATA64B_L7

    pext        DWORD(%%TMP), DWORD(%%LANE_MASK), [rel extr_bits_2_6_10_14]
    kmovq       %%KMASK1, [%%ALIGN_MASK + 8*%%TMP]
    kmovb       %%KMASK2, [%%MOV_MASK + %%TMP]
    vpalignr    %%DATA64B_L11{%%KMASK1}, %%DATA64B_L11, %%DATA64B_L10, 8
    vpalignr    %%DATA64B_L10{%%KMASK1}, %%DATA64B_L10, %%DATA64B_L9, 8
    vpalignr    %%DATA64B_L9{%%KMASK1}, %%DATA64B_L9, %%DATA64B_L8, 8
    vpalignr    %%DATA64B_L8{%%KMASK1}, %%DATA64B_L8, %%DATA64B_L11, 8
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*2]{%%KMASK2}, %%DATA64B_L8
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*2 + 64], %%DATA64B_L9
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*2 + 64*2], %%DATA64B_L10
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*2 + 64*3], %%DATA64B_L11

    pext        DWORD(%%TMP), DWORD(%%LANE_MASK), [rel extr_bits_3_7_11_15]
    kmovq       %%KMASK1, [%%ALIGN_MASK + 8*%%TMP]
    kmovb       %%KMASK2, [%%MOV_MASK + %%TMP]
    vpalignr    %%DATA64B_L15{%%KMASK1}, %%DATA64B_L15, %%DATA64B_L14, 8
    vpalignr    %%DATA64B_L14{%%KMASK1}, %%DATA64B_L14, %%DATA64B_L13, 8
    vpalignr    %%DATA64B_L13{%%KMASK1}, %%DATA64B_L13, %%DATA64B_L12, 8
    vpalignr    %%DATA64B_L12{%%KMASK1}, %%DATA64B_L12, %%DATA64B_L15, 8
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*3]{%%KMASK2}, %%DATA64B_L12
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*3 + 64], %%DATA64B_L13
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*3 + 64*2], %%DATA64B_L14
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 512*3 + 64*3], %%DATA64B_L15
%endif
%endmacro

;
; Function to store 64 bytes of keystream for 4 buffers
; Note: all the 64*4 bytes are not store contiguously.
;       Each 64 bytes are stored every 512 bytes, being written in
;       qword index 0, 1, 2 or 3 inside the 512 bytes, depending on the lane.
%macro  STORE_KSTR4 7
%define %%KS          %1  ; [in] Pointer to keystream
%define %%DATA64B_L0  %2  ; [in] 64 bytes of keystream for lane 0
%define %%DATA64B_L1  %3  ; [in] 64 bytes of keystream for lane 1
%define %%DATA64B_L2  %4  ; [in] 64 bytes of keystream for lane 2
%define %%DATA64B_L3  %5  ; [in] 64 bytes of keystream for lane 3
%define %%KEY_OFF     %6  ; [in] Offset to start writing Keystream
%define %%LANE_GROUP  %7  ; [immediate] 0, 1, 2 or 3

    vmovdqu64   [%%KS + %%KEY_OFF*4 + 64*%%LANE_GROUP], %%DATA64B_L0
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 64*%%LANE_GROUP + 512], %%DATA64B_L1
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 64*%%LANE_GROUP + 512*2], %%DATA64B_L2
    vmovdqu64   [%%KS + %%KEY_OFF*4 + 64*%%LANE_GROUP + 512*3], %%DATA64B_L3
%endmacro

;
; Add two 32-bit args and reduce mod (2^31-1)
;
%macro  ADD_MOD31 4
%define %%IN_OUT        %1 ; [in/out] ZMM register with first input and output
%define %%IN2           %2 ; [in] ZMM register with second input
%define %%ZTMP          %3 ; [clobbered] Temporary ZMM register
%define %%MASK31        %4 ; [in] ZMM register containing 0x7FFFFFFF's in all dwords

    vpaddd      %%IN_OUT, %%IN2
    vpsrld      %%ZTMP, %%IN_OUT, 31
    vpandq      %%IN_OUT, %%MASK31
    vpaddd      %%IN_OUT, %%ZTMP
%endmacro

;
; Rotate (mult by pow of 2) 32-bit arg and reduce mod (2^31-1)
;
%macro  ROT_MOD31   4
%define %%IN_OUT        %1 ; [in/out] ZMM register with input and output
%define %%ZTMP          %2 ; [clobbered] Temporary ZMM register
%define %%MASK31        %3 ; [in] ZMM register containing 0x7FFFFFFF's in all dwords
%define %%N_BITS        %4 ; [immediate] Number of bits to rotate for each dword

    vpslld      %%ZTMP, %%IN_OUT, %%N_BITS
    vpsrld      %%IN_OUT, %%IN_OUT, (31 - %%N_BITS)
    vpternlogq  %%IN_OUT, %%ZTMP, %%MASK31, 0xA8 ; (A | B) & C
%endmacro

;
; Update LFSR registers, calculating S_16
;
; S_16 = [ 2^15*S_15 + 2^17*S_13 + 2^21*S_10 + 2^20*S_4 + (1 + 2^8)*S_0 ] mod (2^31 - 1)
; If init mode, add W to the calculation above.
; S_16 -> S_15 for next round
;
%macro  LFSR_UPDT16  13
%define %%STATE     %1  ; [in] ZUC state
%define %%ROUND_NUM %2  ; [in] Round number
%define %%LANE_MASK %3  ; [in] Mask register with lanes to update
%define %%LFSR_0    %4  ; [clobbered] LFSR_0
%define %%LFSR_4    %5  ; [clobbered] LFSR_2
%define %%LFSR_10   %6  ; [clobbered] LFSR_5
%define %%LFSR_13   %7  ; [clobbered] LFSR_7
%define %%LFSR_15   %8  ; [clobbered] LFSR_9
%define %%ZTMP      %9  ; [clobbered] Temporary ZMM register
%define %%MASK_31   %10 ; [in] Mask_31
%define %%W         %11 ; [in/clobbered] In init mode, contains W for all 16 lanes
%define %%KTMP      %12 ; [clobbered] Temporary K mask
%define %%MODE      %13 ; [constant] "init" / "work" mode

    vmovdqa64   %%LFSR_0,  [%%STATE + (( 0 + %%ROUND_NUM) % 16)*64]
    vmovdqa64   %%LFSR_4,  [%%STATE + (( 4 + %%ROUND_NUM) % 16)*64]
    vmovdqa64   %%LFSR_10, [%%STATE + ((10 + %%ROUND_NUM) % 16)*64]
    vmovdqa64   %%LFSR_13, [%%STATE + ((13 + %%ROUND_NUM) % 16)*64]
    vmovdqa64   %%LFSR_15, [%%STATE + ((15 + %%ROUND_NUM) % 16)*64]

    ; Calculate LFSR feedback (S_16)

    ; In Init mode, W is added to the S_16 calculation
%ifidn %%MODE, init
    ADD_MOD31   %%W, %%LFSR_0, %%ZTMP, %%MASK_31
%else
    vmovdqa64   %%W, %%LFSR_0
%endif
    ROT_MOD31   %%LFSR_0, %%ZTMP, %%MASK_31, 8
    ADD_MOD31   %%W, %%LFSR_0, %%ZTMP, %%MASK_31
    ROT_MOD31   %%LFSR_4, %%ZTMP, %%MASK_31, 20
    ADD_MOD31   %%W, %%LFSR_4, %%ZTMP, %%MASK_31
    ROT_MOD31   %%LFSR_10, %%ZTMP, %%MASK_31, 21
    ADD_MOD31   %%W, %%LFSR_10, %%ZTMP, %%MASK_31
    ROT_MOD31   %%LFSR_13, %%ZTMP, %%MASK_31, 17
    ADD_MOD31   %%W, %%LFSR_13, %%ZTMP, %%MASK_31
    ROT_MOD31   %%LFSR_15, %%ZTMP, %%MASK_31, 15
    ADD_MOD31   %%W, %%LFSR_15, %%ZTMP, %%MASK_31

    vmovdqa32   [%%STATE + (( 0 + %%ROUND_NUM) % 16)*64]{%%LANE_MASK}, %%W

    ; LFSR_S16 = (LFSR_S15++) = eax
%endmacro

;
; Initialize LFSR registers for a single lane, for ZUC-128
;
; From spec, s_i (LFSR) registers need to be loaded as follows:
;
; For 0 <= i <= 15, let s_i= k_i || d_i || iv_i.
; Where k_i is each byte of the key, d_i is a 15-bit constant
; and iv_i is each byte of the IV.
;
%macro INIT_LFSR_128 4
%define %%KEY  %1 ;; [in] Key pointer
%define %%IV   %2 ;; [in] IV pointer
%define %%LFSR %3 ;; [out] ZMM register to contain initialized LFSR regs
%define %%ZTMP %4 ;; [clobbered] ZMM temporary register

    vbroadcasti64x2 %%LFSR, [%%KEY]
    vbroadcasti64x2 %%ZTMP, [%%IV]
    vpshufb         %%LFSR, [rel shuf_mask_key]
    vpsrld          %%LFSR, 1
    vpshufb         %%ZTMP, [rel shuf_mask_iv]
    vpternlogq      %%LFSR, %%ZTMP, [rel EK_d64], 0xFE ; A OR B OR C

%endmacro

;
; Initialize LFSR registers for a single lane, for ZUC-256
;
%macro INIT_LFSR_256 11
%define %%KEY         %1 ;; [in] Key pointer
%define %%IV          %2 ;; [in] IV pointer
%define %%LFSR        %3 ;; [out] ZMM register to contain initialized LFSR regs
%define %%ZTMP1       %4 ;; [clobbered] ZMM temporary register
%define %%ZTMP2       %5 ;; [clobbered] ZMM temporary register
%define %%ZTMP3       %6 ;; [clobbered] ZMM temporary register
%define %%ZTMP4       %7 ;; [clobbered] ZMM temporary register
%define %%ZTMP5       %8 ;; [clobbered] ZMM temporary register
%define %%CONSTANTS   %9 ;; [in] Address to constants
%define %%SHIFT_MASK %10 ;; [in] Mask register to shift K_31
%define %%IV_MASK    %11 ;; [in] Mask register to read IV (last 10 bytes)

        vmovdqu8        XWORD(%%ZTMP4){%%IV_MASK}, [%%IV + 16]
        ; Zero out first 2 bits of IV bytes 17-24
        vpandq          XWORD(%%ZTMP4), [rel iv_mask_low_6]
        vshufi32x4      %%ZTMP4, %%ZTMP4, 0
        vbroadcasti64x2 %%ZTMP1, [%%KEY]
        vbroadcasti64x2 %%ZTMP2, [%%KEY + 16]
        vbroadcasti64x2 %%ZTMP3, [%%IV]

        vpshufb         %%ZTMP5, %%ZTMP1, [rel shuf_mask_key256_first_high]
        vpshufb         %%LFSR, %%ZTMP3, [rel shuf_mask_iv256_first_high]
        vporq           %%LFSR, %%ZTMP5
        vpsrld          %%LFSR, 1

        vpshufb         %%ZTMP5, %%ZTMP2, [rel shuf_mask_key256_second]
        vpsrld          %%ZTMP5{%%SHIFT_MASK}, 4
        vpandq          %%ZTMP5, [rel key_mask_low_4]

        vpshufb         %%ZTMP1, [rel shuf_mask_key256_first_low]
        vpshufb         %%ZTMP3, [rel shuf_mask_iv256_first_low]
        vpshufb         %%ZTMP4, [rel shuf_mask_iv256_second]

        vpternlogq      %%LFSR, %%ZTMP5, %%ZTMP1, 0xFE
        vpternlogq      %%LFSR, %%ZTMP3, %%ZTMP4, 0xFE

        vporq           %%LFSR, [%%CONSTANTS]
%endmacro

%macro INIT_16_AVX512 1
%define %%KEY_SIZE   %1 ; [in] Key size (128 or 256)

%ifdef LINUX
	%define		pKe	  rdi
	%define		pIv	  rsi
	%define		pState	  rdx
        %define         lane_mask ecx
%else
	%define		pKe	  rcx
	%define		pIv	  rdx
	%define		pState	  r8
        %define         lane_mask r9d
%endif
%define	tag_sz	  r10d ; Only used in ZUC-256 (caller written in assembly, so using a hardcoded register)
%define tag_sz_q  r10

%define         %%X0    zmm10
%define         %%X1    zmm11
%define         %%X2    zmm12
%define         %%W     zmm13
%define         %%R1    zmm14
%define         %%R2    zmm15

    FUNC_SAVE

    mov rax, pState

    kmovw   k2, lane_mask

%if %%KEY_SIZE == 256
    ; Get pointer to constants (depending on tag size, this will point at
    ; constants for encryption, authentication with 4-byte, 8-byte or 16-byte tags)
    lea    r13, [rel EK256_d64]
    bsf    tag_sz, tag_sz
    dec    tag_sz
    shl    tag_sz, 6
    add    r13, tag_sz_q
    mov    r11, 0x4000 ; Mask to shift 4 bits only in the 15th dword
    kmovq  k1, r11
    mov    r11, 0x3ff ; Mask to read 10 bytes of IV
    kmovq  k3, r11
%endif

    ; Set LFSR registers for Packet 1
    mov    r9, [pKe]   ; Load Key 1 pointer
    lea    r10, [pIv]  ; Load IV 1 pointer

%if %%KEY_SIZE == 128
    INIT_LFSR_128 r9, r10, zmm0, zmm1
%else
    INIT_LFSR_256 r9, r10, zmm0, zmm3, zmm5, zmm7, zmm9, zmm11, r13, k1, k3
%endif
    ; Set LFSR registers for Packets 2-15
%assign idx 1
%assign reg_lfsr 2
%assign reg_tmp 3
%rep 14
    mov     r9, [pKe + 8*idx]  ; Load Key N pointer
    lea     r10, [pIv + 32*idx] ; Load IV N pointer
%if %%KEY_SIZE == 128
    INIT_LFSR_128 r9, r10, APPEND(zmm, reg_lfsr), APPEND(zmm, reg_tmp)
%else
    INIT_LFSR_256 r9, r10, APPEND(zmm, reg_lfsr), zmm3, zmm5, zmm7, zmm9, zmm11, r13, k1, k3
%endif
%assign idx (idx + 1)
%assign reg_lfsr (reg_lfsr + 2)
%assign reg_tmp (reg_tmp + 2)
%endrep

    ; Set LFSR registers for Packet 16
    mov     r9, [pKe + 8*15]      ; Load Key 16 pointer
    lea     r10, [pIv + 32*15]     ; Load IV 16 pointer
%if %%KEY_SIZE == 128
    INIT_LFSR_128 r9, r10, zmm30, zmm31
%else
    INIT_LFSR_256 r9, r10, zmm30, zmm3, zmm5, zmm7, zmm9, zmm11, r13, k1, k3
%endif
    ; Store LFSR registers in memory (reordering first, so all S0 regs
    ; are together, then all S1 regs... until S15)
    TRANSPOSE16_U32 zmm0, zmm2, zmm4, zmm6, zmm8, zmm10, zmm12, zmm14, \
                    zmm16, zmm18, zmm20, zmm22, zmm24, zmm26, zmm28, zmm30, \
                    zmm1, zmm3, zmm5, zmm7, zmm9, zmm11, zmm13, zmm15, \
                    zmm17, zmm19, zmm21, zmm23, zmm25, zmm27

%assign i 0
%assign j 0
%rep 16
    vmovdqa32 [pState + 64*i]{k2}, APPEND(zmm, j)
%assign i (i+1)
%assign j (j+2)
%endrep

    ; Load read-only registers
    vmovdqa64  zmm0, [rel mask31]
    mov        edx, 0xAAAAAAAA
    kmovd      k1, edx

    ; Zero out R1, R2
    vpxorq %%R1, %%R1
    vpxorq %%R2, %%R2

    ; Shift LFSR 32-times, update state variables
%assign N 0
%rep 32
    BITS_REORG16 rax, N, k2, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, \
                 zmm7, zmm8, zmm9, k1, %%X0, %%X1, %%X2
    NONLIN_FUN16 rax, k2, %%X0, %%X1, %%X2, %%R1, %%R2, \
                 zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, %%W
    vpsrld  %%W,1         ; Shift out LSB of W

    LFSR_UPDT16  rax, N, k2, zmm1, zmm2, zmm3, zmm4, zmm5, \
                 zmm6, zmm0, %%W, k7, init  ; W used in LFSR update
%assign N N+1
%endrep

    ; And once more, initial round from keygen phase = 33 times
    BITS_REORG16 rax, 0, k2, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, \
                 zmm8, zmm9, k1, %%X0, %%X1, %%X2
    NONLIN_FUN16 rax, k2, %%X0, %%X1, %%X2, %%R1, %%R2, \
                 zmm1, zmm2, zmm3, zmm4, zmm5, zmm6

    LFSR_UPDT16  rax, 0, k2, zmm1, zmm2, zmm3, zmm4, zmm5, \
                 zmm6, zmm0, %%W, k7, work

    ; Update R1, R2
    vmovdqa32   [rax + OFS_R1]{k2}, %%R1
    vmovdqa32   [rax + OFS_R2]{k2}, %%R2
    FUNC_RESTORE

%endmacro

;;
;; void asm_ZucInitialization_16_avx512(ZucKey16_t *pKeys, ZucIv16_t *pIvs,
;;                                      ZucState16_t *pState)
;;
MKGLOBAL(ZUC128_INIT,function,internal)
ZUC128_INIT:
    endbranch64
    INIT_16_AVX512 128

    ret

;;
;; void asm_Zuc256Initialization_16_avx512(ZucKey16_t *pKeys, ZucIv16_t *pIvs,
;;                                         ZucState16_t *pState, uint32_t tag_sz)
;;
MKGLOBAL(ZUC256_INIT,function,internal)
ZUC256_INIT:
    endbranch64
    INIT_16_AVX512 256

    ret

;
; Generate N*4 bytes of keystream
; for 16 buffers (where N is number of rounds)
;
%macro KEYGEN_16_AVX512 3-4
%define %%NUM_ROUNDS    %1 ; [in] Number of 4-byte rounds
%define %%STORE_SINGLE  %2 ; [in] If 1, KS will be stored continuously in a single buffer
%define %%KEY_OFF       %3 ; [in] Offset to start writing Keystream
%define %%LANE_MASK     %4 ; [in] Lane mask with lanes to generate all keystream words

    %define     pState  arg1
    %define     pKS     arg2

%define         %%X0    zmm10
%define         %%X1    zmm11
%define         %%X2    zmm12
%define         %%W     zmm13
%define         %%R1    zmm14
%define         %%R2    zmm15

    FUNC_SAVE

    ; Load read-only registers
    vmovdqa64   zmm0, [rel mask31]
    mov         r10d, 0xAAAAAAAA
    kmovd       k1, r10d

%if (%0 == 4)
    kmovd       k2, DWORD(%%LANE_MASK)
    knotd       k4, k2
    mov         r10d, 0x0000FFFF
    kmovd       k3, r10d
%else
    mov         r10d, 0x0000FFFF
    kmovd       k2, r10d
    kmovd       k3, k2
%endif

    ; Read R1/R2
    vmovdqa32   %%R1, [pState + OFS_R1]
    vmovdqa32   %%R2, [pState + OFS_R2]
; Store all 4 bytes of keystream in a single 64-byte buffer
%if (%%NUM_ROUNDS == 1)
    BITS_REORG16 pState, 1, k2, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, \
                 zmm7, zmm8, zmm9, k1, %%X0, %%X1, %%X2, zmm16
    NONLIN_FUN16 pState, k2, %%X0, %%X1, %%X2, %%R1, %%R2, \
                 zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7
    ; OFS_X3 XOR W (zmm7)
    vpxorq      zmm16, zmm7
    LFSR_UPDT16  pState, 1, k2, zmm1, zmm2, zmm3, zmm4, zmm5, \
                 zmm6, zmm0, zmm7, k7, work
    vmovdqa32   [pState + OFS_R1]{k2}, %%R1
    vmovdqa32   [pState + OFS_R2]{k2}, %%R2
%else ;; %%NUM_ROUNDS != 1
    ; Generate N*4B of keystream in N rounds
    ; Generate first bytes of KS for all lanes
%assign N 1
%assign idx 16
%rep (%%NUM_ROUNDS-2)
    BITS_REORG16 pState, N, k3, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, \
                 zmm7, zmm8, zmm9, k1, %%X0, %%X1, %%X2, APPEND(zmm, idx)
    NONLIN_FUN16 pState, k3, %%X0, %%X1, %%X2, %%R1, %%R2, \
                 zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7
    ; OFS_X3 XOR W (zmm7)
    vpxorq      APPEND(zmm, idx), zmm7
    LFSR_UPDT16  pState, N, k3, zmm1, zmm2, zmm3, zmm4, zmm5, \
                 zmm6, zmm0, zmm7, k7, work
%assign N N+1
%assign idx (idx + 1)
%endrep
%if (%%NUM_ROUNDS > 2)
    vmovdqa32   [pState + OFS_R1]{k3}, %%R1
    vmovdqa32   [pState + OFS_R2]{k3}, %%R2
%endif

    ; Generate rest of the KS bytes (last 8 bytes) for selected lanes
%rep 2
    BITS_REORG16 pState, N, k2, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, \
                 zmm7, zmm8, zmm9, k1, %%X0, %%X1, %%X2, APPEND(zmm, idx)
    NONLIN_FUN16 pState, k2, %%X0, %%X1, %%X2, %%R1, %%R2, \
                 zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7
    ; OFS_X3 XOR W (zmm7)
    vpxorq      APPEND(zmm, idx), zmm7
    LFSR_UPDT16  pState, N, k2, zmm1, zmm2, zmm3, zmm4, zmm5, \
                 zmm6, zmm0, zmm7, k7, work
%assign N N+1
%assign idx (idx + 1)
%endrep
    vmovdqa32   [pState + OFS_R1]{k2}, %%R1
    vmovdqa32   [pState + OFS_R2]{k2}, %%R2
%endif ;; (%%NUM_ROUNDS == 1)

%if (%%STORE_SINGLE == 1)
    vmovdqa32 [pKS]{k2}, zmm16
%else
    ; ZMM16-31 contain the keystreams for each round
    ; Perform a 32-bit 16x16 transpose to have up to 64 bytes
    ; (NUM_ROUNDS * 4B) of each lane in a different register
    TRANSPOSE16_U32_INTERLEAVED zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                    zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                    zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, \
                    zmm8, zmm9

%if (%0 == 4)
    lea         r12, [rel alignr_mask]
    lea         r13, [rel mov_mask]
    STORE_KSTR16 pKS, zmm6, zmm4, zmm28, zmm16, zmm7, zmm5, zmm29, zmm17, \
                 zmm2, zmm0, zmm30, zmm18, zmm3, zmm1, zmm31, zmm19, %%KEY_OFF, \
                 %%LANE_MASK, r12, r13, r10, k3, k5
%else
    STORE_KSTR16 pKS, zmm6, zmm4, zmm28, zmm16, zmm7, zmm5, zmm29, zmm17, \
                 zmm2, zmm0, zmm30, zmm18, zmm3, zmm1, zmm31, zmm19, %%KEY_OFF
%endif
%endif ;; %%STORE_SINGLE == 1

   ; Reorder LFSR registers
%if (%0 == 4)
    REORDER_LFSR pState, %%NUM_ROUNDS, k2
%if (%%NUM_ROUNDS >= 2)
    REORDER_LFSR pState, (%%NUM_ROUNDS - 2), k4 ; 2 less rounds for "old" buffers
%endif
%else
    REORDER_LFSR pState, %%NUM_ROUNDS, k2
%endif

    FUNC_RESTORE

%endmacro

;;
;; Reverse bits of each byte of a XMM register
;;
%macro REVERSE_BITS 7
%define %%DATA_IN       %1 ; [in] Input data
%define %%DATA_OUT      %2 ; [out] Output data
%define %%TABLE_L       %3 ; [in] Table to shuffle low nibbles
%define %%TABLE_H       %4 ; [in] Table to shuffle high nibbles
%define %%REV_AND_TABLE %5 ; [in] Mask to keep low nibble of each byte
%define %%XTMP1         %6 ; [clobbered] Temporary XMM register
%define %%XTMP2         %7 ; [clobbered] Temporary XMM register

        vpandq   %%XTMP1, %%DATA_IN, %%REV_AND_TABLE

        vpandnq  %%XTMP2, %%REV_AND_TABLE, %%DATA_IN
        vpsrld   %%XTMP2, 4

        vpshufb  %%DATA_OUT, %%TABLE_H, %%XTMP1 ; bit reverse low nibbles (use high table)
        vpshufb  %%XTMP2, %%TABLE_L, %%XTMP2 ; bit reverse high nibbles (use low table)

        vporq    %%DATA_OUT, %%XTMP2
%endmacro

;;
;; Set up data and KS bytes and use PCLMUL to digest data,
;; then the result gets XOR'ed with the previous digest.
;; This macro can be used with XMM (for 1 buffer),
;; YMM (for 2 buffers) or ZMM registers (for 4 buffers).
;; To use it with YMM and ZMM registers, VPCMULQDQ must be
;; supported.
;;
%macro DIGEST_DATA 11
%define %%DATA          %1 ; [in] Input data (16 bytes)
%define %%KS_L          %2 ; [in] Lower 16 bytes of KS
%define %%KS_H          %3 ; [in] Higher 16 bytes of KS
%define %%IN_OUT        %4 ; [in/out] Accumulated digest
%define %%KMASK         %5 ; [in] Shuffle mask register
%define %%TMP1          %6 ; [clobbered] Temporary XMM/YMM/ZMM register
%define %%TMP2          %7 ; [clobbered] Temporary XMM/YMM/ZMM register
%define %%TMP3          %8 ; [clobbered] Temporary XMM/YMM/ZMM register
%define %%TMP4          %9 ; [clobbered] Temporary XMM/YMM/ZMM register
%define %%TMP5          %10 ; [clobbered] Temporary XMM/YMM/ZMM register
%define %%TMP6          %11 ; [clobbered] Temporary XMM/YMM/ZMM register

        ;; Set up KS
        ;;
        ;; KS_L contains bytes 15:0 of KS (for 1, 2 or 4 buffers)
        ;; KS_H contains bytes 31:16 of KS (for 1, 2 or 4 buffers)
        ;; TMP1 to contain bytes in the following order [7:4 11:8 3:0 7:4]
        ;; TMP2 to contain bytes in the following order [15:12 19:16 11:8 15:12]
        vpalignr        %%TMP1, %%KS_H, %%KS_L, 8
        vpshufd         %%TMP2, %%KS_L, 0x61
        vpshufd         %%TMP1, %%TMP1, 0x61

        ;; Set up DATA
        ;;
        ;; DATA contains 16 bytes of input data (for 1, 2 or 4 buffers)
        ;; TMP3 to contain bytes in the following order [4*0's 7:4 4*0's 3:0]
        ;; TMP3 to contain bytes in the following order [4*0's 15:12 4*0's 11:8]
        vpshufd         %%TMP3{%%KMASK}{z}, %%DATA, 0x10
        vpshufd         %%TMP4{%%KMASK}{z}, %%DATA, 0x32

        ;; PCMUL the KS's with the DATA
        ;; XOR the results from 4 32-bit words together
        vpclmulqdq      %%TMP5, %%TMP3, %%TMP2, 0x00
        vpclmulqdq      %%TMP3, %%TMP3, %%TMP2, 0x11
        vpclmulqdq      %%TMP6, %%TMP4, %%TMP1, 0x00
        vpclmulqdq      %%TMP4, %%TMP4, %%TMP1, 0x11
        vpternlogq      %%TMP5, %%TMP3, %%TMP6, 0x96
        vpternlogq      %%IN_OUT, %%TMP5, %%TMP4, 0x96
%endmacro

;
; Generate 64 bytes of keystream
; for 16 buffers and authenticate 64 bytes of data
;
%macro ZUC_EIA3_16_64B_AVX512 6
%define %%STATE         %1 ; [in] ZUC state
%define %%KS            %2 ; [in] Pointer to keystream (128x16 bytes)
%define %%T             %3 ; [in] Pointer to digests
%define %%DATA          %4 ; [in] Pointer to array of pointers to data buffers
%define %%LEN           %5 ; [in] Pointer to array of remaining length to digest
%define %%NROUNDS       %6 ; [in/clobbered] Number of rounds of 64 bytes of data to digest

%define %%DATA_ADDR0    rbx
%define %%DATA_ADDR1    r12
%define %%DATA_ADDR2    r13
%define %%DATA_ADDR3    r14
%define %%OFFSET        r15

%define %%DIGEST_0      zmm28
%define %%DIGEST_1      zmm29
%define %%DIGEST_2      zmm30
%define %%DIGEST_3      zmm31

%define %%ZTMP1         zmm1
%define %%ZTMP2         zmm2
%define %%ZTMP3         zmm3
%define %%ZTMP4         zmm4
%define %%ZTMP5         zmm5
%define %%ZTMP6         zmm6
%define %%ZTMP7         zmm7
%define %%ZTMP8         zmm8
%define %%ZTMP9         zmm9

%define %%ZKS_L         %%ZTMP9
%define %%ZKS_H         zmm21

%define %%XTMP1         xmm1
%define %%XTMP2         xmm2
%define %%XTMP3         xmm3
%define %%XTMP4         xmm4
%define %%XTMP5         xmm5
%define %%XTMP6         xmm6
%define %%XTMP7         xmm7
%define %%XTMP9         xmm9
%define %%KS_L          %%XTMP9
%define %%KS_H          xmm21
%define %%XDIGEST_0     xmm13
%define %%XDIGEST_1     xmm14
%define %%XDIGEST_2     xmm19
%define %%XDIGEST_3     xmm20
%define %%Z_TEMP_DIGEST zmm15
%define %%REV_TABLE_L   xmm16
%define %%REV_TABLE_H   xmm17
%define %%REV_AND_TABLE xmm18

; Defines used in KEYGEN
%define %%MASK31        zmm0

%define %%X0            zmm10
%define %%X1            zmm11
%define %%X2            zmm12
%define %%R1            zmm22
%define %%R2            zmm23

%define %%KS_0          zmm24
%define %%KS_1          zmm25
%define %%KS_2          zmm26
%define %%KS_3          zmm27

        xor     %%OFFSET, %%OFFSET

        mov     r12d, 0xAAAAAAAA
        kmovd   k1, r12d

        mov     r12d, 0x0000FFFF
        kmovd   k2, r12d

        mov     r12d, 0x55555555
        kmovd   k3, r12d

        mov     r12d, 0x3333
        kmovd   k4, r12d
        mov     r12d, 0xCCCC
        kmovd   k5, r12d

        vpxorq     %%DIGEST_0, %%DIGEST_0
        vpxorq     %%DIGEST_1, %%DIGEST_1
        vpxorq     %%DIGEST_2, %%DIGEST_2
        vpxorq     %%DIGEST_3, %%DIGEST_3

        ; Load read-only registers
        vmovdqa64   %%MASK31, [rel mask31]

%if USE_GFNI_VAES_VPCLMUL == 0
        vmovdqa64  %%REV_TABLE_L, [bit_reverse_table_l]
        vmovdqa64  %%REV_TABLE_H, [bit_reverse_table_h]
        vmovdqa64  %%REV_AND_TABLE, [bit_reverse_and_table]
%endif

        ; Read R1/R2
        vmovdqa32   %%R1, [%%STATE + OFS_R1]
        vmovdqa32   %%R2, [%%STATE + OFS_R2]

        ;;
        ;; Generate keystream and digest 64 bytes on each iteration
        ;;
%%_loop:
        ;; Generate 64B of keystream in 16 (4x4) rounds
        ;; N goes from 1 to 16, within two nested reps of 4 iterations
        ;; The outer "rep" loop iterates through 4 groups of lanes (4 buffers each),
        ;; the inner "rep" loop iterates through the data for each group:
        ;; each iteration digests 16 bytes of data (in case of having VPCLMUL
        ;; data from the 4 buffers is digested in one go (using ZMM registers), otherwise,
        ;; data is digested in 4 iterations (using XMM registers)
%assign %%N 1
%assign %%LANE_GROUP 0
%rep 4
        mov             %%DATA_ADDR0, [%%DATA + %%LANE_GROUP*8 + 0*32]
        mov             %%DATA_ADDR1, [%%DATA + %%LANE_GROUP*8 + 1*32]
        mov             %%DATA_ADDR2, [%%DATA + %%LANE_GROUP*8 + 2*32]
        mov             %%DATA_ADDR3, [%%DATA + %%LANE_GROUP*8 + 3*32]

%assign %%idx 0
%rep 4
        BITS_REORG16 %%STATE, %%N, k2, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, %%ZTMP6, \
                     %%ZTMP7, %%ZTMP8, %%ZTMP9, k1, %%X0, %%X1, %%X2, APPEND(%%KS_, %%idx)
        NONLIN_FUN16 %%STATE, k2, %%X0, %%X1, %%X2, %%R1, %%R2, \
                     %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, %%ZTMP6, %%ZTMP7
        ; OFS_X3 XOR W (%%ZTMP7)
        vpxorq  APPEND(%%KS_, %%idx), %%ZTMP7
        LFSR_UPDT16  %%STATE, %%N, k2, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, \
                      %%ZTMP6, %%MASK31, %%ZTMP7, k7, work

        ;; Transpose and store KS every 16 bytes
%if %%idx == 3
        TRANSPOSE4_U32_INTERLEAVED %%KS_0, %%KS_1, %%KS_2, %%KS_3, %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4

        STORE_KSTR4 %%KS, %%KS_0, %%KS_1, %%KS_2, %%KS_3, 64, %%LANE_GROUP
%endif

        ;; Digest next 16 bytes of data for 4 buffers
%if USE_GFNI_VAES_VPCLMUL == 1
        ;; If VPCMUL is available, read chunks of 16x4 bytes of data
        ;; and digest them with 24x4 bytes of KS, then XOR their digest
        ;; with previous digest (with DIGEST_DATA)

        ; Read 4 blocks of 16 bytes of data and put them in a register
        vmovdqu64       %%XTMP1, [%%DATA_ADDR0 + 16*%%idx + %%OFFSET]
        vinserti32x4    %%ZTMP1, [%%DATA_ADDR1 + 16*%%idx + %%OFFSET], 1
        vinserti32x4    %%ZTMP1, [%%DATA_ADDR2 + 16*%%idx + %%OFFSET], 2
        vinserti32x4    %%ZTMP1, [%%DATA_ADDR3 + 16*%%idx + %%OFFSET], 3

        ; Read 8 blocks of 16 bytes of KS
        vmovdqa64       %%ZKS_L, [GET_KS(%%KS, %%LANE_GROUP, %%idx, 0)]
        vmovdqa64       %%ZKS_H, [GET_KS(%%KS, %%LANE_GROUP, (%%idx + 1), 0)]

        ; Reverse bits of next 16 bytes from all 4 buffers
        vgf2p8affineqb  %%ZTMP7, %%ZTMP1, [rel bit_reverse_table], 0x00

        ; Digest 16 bytes of data with 24 bytes of KS, for 4 buffers
        DIGEST_DATA %%ZTMP7, %%ZKS_L, %%ZKS_H, APPEND(%%DIGEST_, %%LANE_GROUP), k3, \
                    %%ZTMP1, %%ZTMP2, %%ZTMP3, %%ZTMP4, %%ZTMP5, %%ZTMP6

%else ; USE_GFNI_VAES_VPCLMUL == 1
        ;; If VPCMUL is NOT available, read chunks of 16 bytes of data
        ;; and digest them with 24 bytes of KS, and repeat this for 4 different buffers
        ;; then insert these digests into a ZMM register and XOR with previous digest

%assign %%J 0
%rep 4
%if %%idx == 0
        ; Reset temporary digests (for the first 16 bytes)
        vpxorq  APPEND(%%XDIGEST_, %%J), APPEND(%%XDIGEST_, %%J)
%endif
        ; Read the next 2 blocks of 16 bytes of KS
        vmovdqa64  %%KS_L, [GET_KS(%%KS, %%LANE_GROUP, %%idx, %%J)]
        vmovdqa64  %%KS_H, [GET_KS(%%KS, %%LANE_GROUP, (%%idx + 1), %%J)]

        ;; read 16 bytes and reverse bits
        vmovdqu64  %%XTMP1, [APPEND(%%DATA_ADDR, %%J) + %%idx*16 + %%OFFSET]
        REVERSE_BITS %%XTMP1, %%XTMP7, %%REV_TABLE_L, %%REV_TABLE_H, \
                     %%REV_AND_TABLE, %%XTMP2, %%XTMP3

        ; Digest 16 bytes of data with 24 bytes of KS, for one buffer
        DIGEST_DATA %%XTMP7, %%KS_L, %%KS_H, APPEND(%%XDIGEST_, %%J), k3, \
                    %%XTMP1, %%XTMP2, %%XTMP3, %%XTMP4, %%XTMP5, %%XTMP6

        ; Once all 64 bytes of data have been digested, insert them in temporary ZMM register
%if %%idx == 3
        vinserti32x4 %%Z_TEMP_DIGEST, APPEND(%%XDIGEST_, %%J), %%J
%endif
%assign %%J (%%J + 1)
%endrep ; %rep 4 %%J

        ; XOR with previous digest
%if %%idx == 3
        vpxorq  APPEND(%%DIGEST_, %%LANE_GROUP), %%Z_TEMP_DIGEST
%endif
%endif ;; USE_GFNI_VAES_VPCLMUL == 0
%assign %%idx (%%idx + 1)
%assign %%N %%N+1
%endrep ; %rep 4 %%idx

%assign %%LANE_GROUP (%%LANE_GROUP + 1)
%endrep ; %rep 4 %%LANE_GROUP

%assign %%LANE_GROUP 0
%rep 4
        ; Memcpy KS 64-127 bytes to 0-63 bytes
        vmovdqa64       %%ZTMP3, [%%KS + %%LANE_GROUP*512 + 64*4]
        vmovdqa64       %%ZTMP4, [%%KS + %%LANE_GROUP*512 + 64*5]
        vmovdqa64       %%ZTMP5, [%%KS + %%LANE_GROUP*512 + 64*6]
        vmovdqa64       %%ZTMP6, [%%KS + %%LANE_GROUP*512 + 64*7]
        vmovdqa64       [%%KS + %%LANE_GROUP*512], %%ZTMP3
        vmovdqa64       [%%KS + %%LANE_GROUP*512 + 64], %%ZTMP4
        vmovdqa64       [%%KS + %%LANE_GROUP*512 + 64*2], %%ZTMP5
        vmovdqa64       [%%KS + %%LANE_GROUP*512 + 64*3], %%ZTMP6
%assign %%LANE_GROUP (%%LANE_GROUP + 1)
%endrep ; %rep 4 %%LANE_GROUP

        add     %%OFFSET, 64

        dec     %%NROUNDS
        jnz     %%_loop

        ;; - update tags
        vmovdqu64       %%ZTMP1, [%%T] ; Input tags
        vmovdqa64       %%ZTMP2, [rel shuf_mask_tags_0_4_8_12]
        vmovdqa64       %%ZTMP3, [rel shuf_mask_tags_0_4_8_12 + 64]
        ; Get result tags for 16 buffers in different position in each lane
        ; and blend these tags into an ZMM register.
        ; Then, XOR the results with the previous tags and write out the result.
        vpermt2d        %%DIGEST_0{k4}{z}, %%ZTMP2, %%DIGEST_1
        vpermt2d        %%DIGEST_2{k5}{z}, %%ZTMP3, %%DIGEST_3
        vpternlogq      %%ZTMP1, %%DIGEST_0, %%DIGEST_2, 0x96 ; A XOR B XOR C
        vmovdqu64       [%%T], %%ZTMP1

        ; Update R1/R2
        vmovdqa64   [%%STATE + OFS_R1], %%R1
        vmovdqa64   [%%STATE + OFS_R2], %%R2

        ; Update data pointers
        vmovdqu64       %%ZTMP1, [%%DATA]
        vmovdqu64       %%ZTMP2, [%%DATA + 64]
        vpbroadcastq    %%ZTMP3, %%OFFSET
        vpaddq          %%ZTMP1, %%ZTMP3
        vpaddq          %%ZTMP2, %%ZTMP3
        vmovdqu64       [%%DATA], %%ZTMP1
        vmovdqu64       [%%DATA + 64], %%ZTMP2

        ; Update array of lengths (if lane is valid, so length < UINT16_MAX)
        vmovdqa64       YWORD(%%ZTMP2), [%%LEN]
        vpcmpw          k1, YWORD(%%ZTMP2), [rel all_ffs], 4 ; k1 -> valid lanes
        shl             %%OFFSET, 3 ; Convert to bits
        vpbroadcastw    YWORD(%%ZTMP1), DWORD(%%OFFSET)
        vpsubw          YWORD(%%ZTMP2){k1}, YWORD(%%ZTMP1)
        vmovdqa64       [%%LEN], YWORD(%%ZTMP2)

%endmacro

;;
;; void asm_ZucGenKeystream64B_16_avx512(state16_t *pSta, u32* pKeyStr[16],
;;                                       const u32 key_off)
;;
MKGLOBAL(ZUC_KEYGEN64B_16,function,internal)
ZUC_KEYGEN64B_16:
    endbranch64
    KEYGEN_16_AVX512 16, 0, arg3

    ret
;;
;; void asm_Eia3_Nx64B_AVX512_16(ZucState16_t *pState,
;;                               uint32_t *pKeyStr,
;;                               uint32_t *T,
;;                               const void **data,
;;                               uint16_t *len);
MKGLOBAL(ZUC_EIA3_N64B,function,internal)
ZUC_EIA3_N64B:
%define STATE         arg1
%define KS            arg2
%define T             arg3
%define DATA          arg4

%ifdef LINUX
%define LEN           arg5
%define NROUNDS       arg6
%else
%define LEN           r10
%define NROUNDS       r11
%endif
    endbranch64

%ifndef LINUX
    mov         LEN, arg5
    mov         NROUNDS, arg6
%endif

    FUNC_SAVE

    ZUC_EIA3_16_64B_AVX512 STATE, KS, T, DATA, LEN, NROUNDS

    FUNC_RESTORE

    ret

;
;; void asm_ZucGenKeystream64B_16_skip8_avx512(state16_t *pSta, u32* pKeyStr[16],
;;                                             const u32 key_off,
;;                                             const u16 lane_mask)
;;
MKGLOBAL(ZUC_KEYGEN64B_SKIP8_16,function,internal)
ZUC_KEYGEN64B_SKIP8_16:
    endbranch64
    KEYGEN_16_AVX512 16, 0, arg3, arg4

    ret

;;
;; void asm_ZucGenKeystream8B_16_avx512(state16_t *pSta, u32* pKeyStr[16],
;;                                      const u32 key_off)
;;
MKGLOBAL(ZUC_KEYGEN8B_16,function,internal)
ZUC_KEYGEN8B_16:
    endbranch64
    KEYGEN_16_AVX512 2, 0, arg3

    ret

;;
;; void asm_ZucGenKeystream4B_16_avx512(state16_t *pSta, u32 pKeyStr[16],
;;                                      const u32 lane_mask)
;;
MKGLOBAL(ZUC_KEYGEN4B_16,function,internal)
ZUC_KEYGEN4B_16:
    endbranch64
    KEYGEN_16_AVX512 1, 1, 0, arg3

    ret

%macro KEYGEN_VAR_16_AVX512 2-3
%define %%NUM_ROUNDS    %1 ; [in] Number of 4-byte rounds (GP dowrd register)
%define %%KEY_OFF       %2 ; [in] Offset to start writing Keystream
%define %%LANE_MASK     %3 ; [in] Lane mask with lanes to generate full keystream (rest 2 words less)

    cmp     %%NUM_ROUNDS, 16
    je      %%_num_rounds_is_16
    cmp     %%NUM_ROUNDS, 8
    je      %%_num_rounds_is_8
    jb      %%_rounds_is_1_7

    ; Final blocks 9-16
    cmp     %%NUM_ROUNDS, 12
    je      %%_num_rounds_is_12
    jb      %%_rounds_is_9_11

    ; Final blocks 13-15
    cmp     %%NUM_ROUNDS, 14
    je      %%_num_rounds_is_14
    ja      %%_num_rounds_is_15
    jb      %%_num_rounds_is_13

%%_rounds_is_9_11:
    cmp     %%NUM_ROUNDS, 10
    je      %%_num_rounds_is_10
    ja      %%_num_rounds_is_11
    jb      %%_num_rounds_is_9

%%_rounds_is_1_7:
    cmp     %%NUM_ROUNDS, 4
    je      %%_num_rounds_is_4
    jb      %%_rounds_is_1_3

    ; Final blocks 5-7
    cmp     %%NUM_ROUNDS, 6
    je      %%_num_rounds_is_6
    ja      %%_num_rounds_is_7
    jb      %%_num_rounds_is_5

%%_rounds_is_1_3:
    cmp     %%NUM_ROUNDS, 2
    je      %%_num_rounds_is_2
    ja      %%_num_rounds_is_3

    ; Rounds = 1 if fall-through
%assign I 1
%rep 16
APPEND(%%_num_rounds_is_,I):
%if (%0 == 3)
    KEYGEN_16_AVX512 I, 0, %%KEY_OFF, %%LANE_MASK
%else
    KEYGEN_16_AVX512 I, 0, %%KEY_OFF
%endif
    jmp     %%_done

%assign I (I + 1)
%endrep

%%_done:
%endmacro

;;
;; void asm_ZucGenKeystream_16_avx512(state16_t *pSta, u32* pKeyStr[16],
;;                                    const u32 key_off,
;;                                    const u32 numRounds)
;;
MKGLOBAL(ZUC_KEYGEN_16,function,internal)
ZUC_KEYGEN_16:
    endbranch64

    KEYGEN_VAR_16_AVX512 arg4, arg3

    ret

;;
;; void asm_ZucGenKeystream_16_skip8_avx512(state16_t *pSta, u32* pKeyStr[16],
;;                                          const u32 key_off,
;;                                          const u16 lane_mask,
;;                                          u32 numRounds)
;;
MKGLOBAL(ZUC_KEYGEN_SKIP8_16,function,internal)
ZUC_KEYGEN_SKIP8_16:
%ifdef LINUX
        %define	        arg5    r8d
%else
        %define         arg5    [rsp + 40]
%endif
    endbranch64

    mov     r10d, arg5
    KEYGEN_VAR_16_AVX512 r10d, arg3, arg4

    ret

;;
;; Encrypts up to 64 bytes of data
;;
;; 1 - Reads R1 & R2
;; 2 - Generates up to 64 bytes of keystream (16 rounds of 4 bytes)
;; 3 - Writes R1 & R2
;; 4 - Transposes the registers containing chunks of 4 bytes of KS for each buffer
;; 5 - ZMM16-31 will contain 64 bytes of KS for each buffer
;; 6 - Reads 64 bytes of data for each buffer, XOR with KS and writes the ciphertext
;;
%macro CIPHER64B 12
%define %%NROUNDS    %1
%define %%BYTE_MASK  %2
%define %%LANE_MASK  %3
%define %%OFFSET     %4
%define %%LAST_ROUND %5
%define %%MASK_31    %6
%define %%X0         %7
%define %%X1         %8
%define %%X2         %9
%define %%W          %10
%define %%R1         %11
%define %%R2         %12

        ; Read R1/R2
        vmovdqa32   %%R1, [rax + OFS_R1]
        vmovdqa32   %%R2, [rax + OFS_R2]

        ; Generate N*4B of keystream in N rounds
%assign N 1
%assign idx 16
%rep %%NROUNDS
        BITS_REORG16 rax, N, %%LANE_MASK, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, \
                     zmm7, zmm8, zmm9, k1, %%X0, %%X1, %%X2, APPEND(zmm, idx)
        NONLIN_FUN16 rax, %%LANE_MASK, %%X0, %%X1, %%X2, %%R1, %%R2, \
                     zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7
        ; OFS_X3 XOR W (zmm7)
        vpxorq      APPEND(zmm, idx), zmm7
        ; Shuffle bytes within KS words to XOR with plaintext later
        vpshufb APPEND(zmm, idx), [rel swap_mask]
        LFSR_UPDT16  rax, N, %%LANE_MASK, zmm1, zmm2, zmm3, zmm4, zmm5, \
                     zmm6, %%MASK_31, zmm7, k7, work
%assign N (N + 1)
%assign idx (idx + 1)
%endrep
        vmovdqa32   [rax + OFS_R1]{%%LANE_MASK}, %%R1
        vmovdqa32   [rax + OFS_R2]{%%LANE_MASK}, %%R2

        ; ZMM16-31 contain the keystreams for each round
        ; Perform a 32-bit 16x16 transpose to have the 64 bytes
        ; of each lane in a different register
        TRANSPOSE16_U32 zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                        zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                        zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, \
                        zmm8, zmm9, zmm10, zmm11, zmm12, zmm13

        ;; XOR Input buffer with keystream
%if %%LAST_ROUND == 1
        lea     rbx, [rel byte64_len_to_mask_table]
%endif
        ;; Read all 16 streams using registers r12-15 into registers zmm0-15
%assign i 0
%assign j 0
%assign k 12
%rep 16
%if %%LAST_ROUND == 1
        ;; Read number of bytes left to encrypt for the lane stored in stack
        ;; and construct byte mask to read from input pointer
        movzx   r12d, word [rsp + j*2]
        kmovq   %%BYTE_MASK, [rbx + r12*8]
%endif
        mov     APPEND(r, k), [pIn + i]
        vmovdqu8 APPEND(zmm, j){%%BYTE_MASK}{z}, [APPEND(r, k) + %%OFFSET]
%assign k 12 + ((j + 1) % 4)
%assign j (j + 1)
%assign i (i + 8)
%endrep

        ;; XOR Input (zmm0-15) with Keystreams (zmm16-31)
%assign i 0
%assign j 16
%rep 16
        vpxorq zmm %+j, zmm %+i
%assign i (i + 1)
%assign j (j + 1)
%endrep

        ;; Write output for all 16 buffers (zmm16-31) using registers r12-15
%assign i 0
%assign j 16
%assign k 12
%rep 16
%if %%LAST_ROUND == 1
        ;; Read length to encrypt for the lane stored in stack
        ;; and construct byte mask to write to output pointer
        movzx   r12d, word [rsp + (j-16)*2]
        kmovq   %%BYTE_MASK, [rbx + r12*8]
%endif
        mov     APPEND(r, k), [pOut + i]
        vmovdqu8 [APPEND(r, k) + %%OFFSET]{%%BYTE_MASK}, APPEND(zmm, j)
%assign k 12 + ((j + 1) % 4)
%assign j (j + 1)
%assign i (i + 8)
%endrep

%endmacro

;;
;; void asm_ZucCipher_16_avx512(state16_t *pSta, u64 *pIn[16],
;;                              u64 *pOut[16], u16 lengths[16],
;;                              u64 min_length);
MKGLOBAL(CIPHER_16,function,internal)
CIPHER_16:

%ifdef LINUX
        %define         pState  rdi
        %define         pIn     rsi
        %define         pOut    rdx
        %define         lengths rcx
        %define         arg5    r8
%else
        %define         pState  rcx
        %define         pIn     rdx
        %define         pOut    r8
        %define         lengths r9
        %define         arg5    [rsp + 40]
%endif

%define min_length r10
%define buf_idx    r11

        mov     min_length, arg5

        FUNC_SAVE

        ; Convert all lengths set to UINT16_MAX (indicating that lane is not valid) to min length
        vpbroadcastw ymm0, min_length
        vmovdqa ymm1, [lengths]
        vpcmpw k1, ymm1, [rel all_ffs], 0
        vmovdqu16 ymm1{k1}, ymm0 ; YMM1 contain updated lengths

        ; Round up to nearest multiple of 4 bytes
        vpaddw  ymm0, [rel all_threes]
        vpandq  ymm0, [rel all_fffcs]

        ; Calculate remaining bytes to encrypt after function call
        vpsubw  ymm2, ymm1, ymm0
        vpxorq  ymm3, ymm3
        vpcmpw  k1, ymm2, ymm3, 1 ; Get mask of lengths < 0
        ; Set to zero the lengths of the lanes which are going to be completed
        vmovdqu16 ymm2{k1}, ymm3 ; YMM2 contain final lengths
        vmovdqa [lengths], ymm2 ; Update in memory the final updated lengths

        ; Calculate number of bytes to encrypt after round of 64 bytes (up to 63 bytes),
        ; for each lane, and store it in stack to be used in the last round
        vpsubw  ymm1, ymm2 ; Bytes to encrypt in all lanes
        vpandq  ymm1, [rel all_3fs] ; Number of final bytes (up to 63 bytes) for each lane
        sub     rsp, 32
        vmovdqu [rsp], ymm1

        ; Load state pointer in RAX
        mov     rax, pState

        ; Load read-only registers
        mov     r12d, 0xAAAAAAAA
        kmovd   k1, r12d
        mov     r12, 0xFFFFFFFFFFFFFFFF
        kmovq   k2, r12
        mov     r12d, 0x0000FFFF
        kmovd   k3, r12d

        xor     buf_idx, buf_idx

        ;; Perform rounds of 64 bytes, where LFSR reordering is not needed
loop:
        cmp     min_length, 64
        jl      exit_loop

        vmovdqa64 zmm0, [rel mask31]

        CIPHER64B 16, k2, k3, buf_idx, 0, zmm0, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15

        sub     min_length, 64
        add     buf_idx, 64
        jmp     loop

exit_loop:

        mov     r15, min_length
        add     r15, 3
        shr     r15, 2 ;; numbers of rounds left (round up length to nearest multiple of 4B)
        jz      _no_final_rounds

        vmovdqa64 zmm0, [rel mask31]

        cmp     r15, 8
        je      _num_final_rounds_is_8
        jl      _final_rounds_is_1_7

        ; Final blocks 9-16
        cmp     r15, 12
        je      _num_final_rounds_is_12
        jl      _final_rounds_is_9_11

        ; Final blocks 13-16
        cmp     r15, 16
        je      _num_final_rounds_is_16
        cmp     r15, 15
        je      _num_final_rounds_is_15
        cmp     r15, 14
        je      _num_final_rounds_is_14
        cmp     r15, 13
        je      _num_final_rounds_is_13

_final_rounds_is_9_11:
        cmp     r15, 11
        je      _num_final_rounds_is_11
        cmp     r15, 10
        je      _num_final_rounds_is_10
        cmp     r15, 9
        je      _num_final_rounds_is_9

_final_rounds_is_1_7:
        cmp     r15, 4
        je      _num_final_rounds_is_4
        jl      _final_rounds_is_1_3

        ; Final blocks 5-7
        cmp     r15, 7
        je      _num_final_rounds_is_7
        cmp     r15, 6
        je      _num_final_rounds_is_6
        cmp     r15, 5
        je      _num_final_rounds_is_5

_final_rounds_is_1_3:
        cmp     r15, 3
        je      _num_final_rounds_is_3
        cmp     r15, 2
        je      _num_final_rounds_is_2

        jmp     _num_final_rounds_is_1

        ; Perform encryption of last bytes (<= 64 bytes) and reorder LFSR registers
        ; if needed (if not all 16 rounds of 4 bytes are done)
%assign I 1
%rep 16
APPEND(_num_final_rounds_is_,I):
        CIPHER64B I, k2, k3, buf_idx, 1, zmm0, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15
        REORDER_LFSR rax, I, k3
        add     buf_idx, min_length
        jmp     _no_final_rounds
%assign I (I + 1)
%endrep

_no_final_rounds:
        add             rsp, 32
        ;; update in/out pointers
        add             buf_idx, 3
        and             buf_idx, 0xfffffffffffffffc
        vpbroadcastq    zmm0, buf_idx
        vpaddq          zmm1, zmm0, [pIn]
        vpaddq          zmm2, zmm0, [pIn + 64]
        vmovdqa64       [pIn], zmm1
        vmovdqa64       [pIn + 64], zmm2
        vpaddq          zmm1, zmm0, [pOut]
        vpaddq          zmm2, zmm0, [pOut + 64]
        vmovdqa64       [pOut], zmm1
        vmovdqa64       [pOut + 64], zmm2

        FUNC_RESTORE

        ret

;;
;;extern void asm_Eia3Round64B_16(uint32_t *T, const void *KS,
;;                                const void **DATA, uint16_t *LEN);
;;
;; Updates authentication tag T of 16 buffers based on keystream KS and DATA.
;; - it processes 64 bytes of DATA of buffers
;; - reads data in 16 byte chunks from different buffers
;;   (first buffers 0,4,8,12; then 1,5,9,13; etc) and bit reverses them
;; - reads KS (when utilizing VPCLMUL instructions, it reads 64 bytes directly,
;;   containing 16 bytes of KS for 4 different buffers)
;; - employs clmul for the XOR & ROL part
;; - copies top 64 bytes of KS to bottom (for the next round)
;; - Updates Data pointers for next rounds
;; - Updates array of lengths
;;
;;  @param [in] T: Array of digests for all 16 buffers
;;  @param [in] KS: Pointer to 128 bytes of keystream for all 16 buffers (2048 bytes in total)
;;  @param [in] DATA: Array of pointers to data for all 16 buffers
;;  @param [in] LEN: Array of lengths for all 16 buffers
;;
align 64
MKGLOBAL(ZUC_ROUND64B_16,function,internal)
ZUC_ROUND64B_16:
        endbranch64
%ifdef LINUX
	%define		T	rdi
	%define		KS	rsi
	%define		DATA	rdx
	%define		LEN	rcx
%else
	%define		T	rcx
	%define		KS	rdx
	%define		DATA	r8
	%define		LEN	r9
%endif

%if USE_GFNI_VAES_VPCLMUL == 1
%define         DATA_ADDR0      rbx
%define         DATA_ADDR1      r10
%define         DATA_ADDR2      r11
%define         DATA_ADDR3      r12

%define         DATA_TRANS0     zmm19
%define         DATA_TRANS1     zmm20
%define         DATA_TRANS2     zmm21
%define         DATA_TRANS3     zmm22
%define         DATA_TRANS0x    xmm19
%define         DATA_TRANS1x    xmm20
%define         DATA_TRANS2x    xmm21
%define         DATA_TRANS3x    xmm22

%define         KS_TRANS0       zmm23
%define         KS_TRANS1       zmm24
%define         KS_TRANS2       zmm25
%define         KS_TRANS3       zmm26
%define         KS_TRANS4       zmm27
%define         KS_TRANS0x      xmm23
%define         KS_TRANS1x      xmm24
%define         KS_TRANS2x      xmm25
%define         KS_TRANS3x      xmm26
%define         KS_TRANS4x      xmm27

%define         DIGEST_0        zmm28
%define         DIGEST_1        zmm29
%define         DIGEST_2        zmm30
%define         DIGEST_3        zmm31

%define         ZTMP1           zmm0
%define         ZTMP2           zmm1
%define         ZTMP3           zmm2
%define         ZTMP4           zmm3
%define         ZTMP5           zmm4
%define         ZTMP6           zmm5
%define         ZTMP7           zmm6
%define         ZTMP8           zmm7

%define         YTMP1           YWORD(ZTMP1)

        FUNC_SAVE

        mov             r12d, 0x55555555
        kmovd           k1, r12d
        ;; Read first buffers 0,4,8,12; then 1,5,9,13, and so on,
        ;; since the keystream is laid out this way, which chunks of
        ;; 16 bytes interleved. First the 128 bytes for
        ;; buffers 0,4,8,12 (total of 512 bytes), then the 128 bytes
        ;; for buffers 1,5,9,13, and so on
%assign IDX 0
%rep 4
        vpxorq          APPEND(DIGEST_, IDX), APPEND(DIGEST_, IDX)

        mov             DATA_ADDR0, [DATA + IDX*8 + 0*32]
        mov             DATA_ADDR1, [DATA + IDX*8 + 1*32]
        mov             DATA_ADDR2, [DATA + IDX*8 + 2*32]
        mov             DATA_ADDR3, [DATA + IDX*8 + 3*32]

        vmovdqu64       KS_TRANS0, [KS + IDX*64*2*4]

%assign I 0
%assign J 1
%rep 4
        vmovdqu64       XWORD(APPEND(DATA_TRANS, I)), [DATA_ADDR0 + 16*I]
        vinserti32x4    APPEND(DATA_TRANS, I), [DATA_ADDR1 + 16*I], 1
        vinserti32x4    APPEND(DATA_TRANS, I), [DATA_ADDR2 + 16*I], 2
        vinserti32x4    APPEND(DATA_TRANS, I), [DATA_ADDR3 + 16*I], 3

        vmovdqu64       APPEND(KS_TRANS, J), [KS + IDX*64*2*4 + 64*J]

        ;; Reverse bits of next 16 bytes from all 4 buffers
        vgf2p8affineqb  ZTMP1, APPEND(DATA_TRANS,I), [rel bit_reverse_table], 0x00

        ;; ZUC authentication part
        ;; - 4x32 data bits
        ;; - set up KS
        vpalignr        ZTMP2, APPEND(KS_TRANS, J), APPEND(KS_TRANS, I), 8
        vpshufd         ZTMP3, APPEND(KS_TRANS, I), 0x61
        vpshufd         ZTMP4, ZTMP2, 0x61

        ;;  - set up DATA
        vpshufd         APPEND(DATA_TRANS, I){k1}{z}, ZTMP1, 0x10
        vpshufd         ZTMP2{k1}{z}, ZTMP1, 0x32

        ;; - clmul
        ;; - xor the results from 4 32-bit words together
        vpclmulqdq      ZTMP5, APPEND(DATA_TRANS, I), ZTMP3, 0x00
        vpclmulqdq      ZTMP6, APPEND(DATA_TRANS, I), ZTMP3, 0x11
        vpclmulqdq      ZTMP7, ZTMP2, ZTMP4, 0x00
        vpclmulqdq      ZTMP8, ZTMP2, ZTMP4, 0x11

        vpternlogq      ZTMP5, ZTMP6, ZTMP8, 0x96
        vpternlogq      APPEND(DIGEST_, IDX), ZTMP5, ZTMP7, 0x96

%assign J (J + 1)
%assign I (I + 1)
%endrep

        ; Memcpy KS 64-127 bytes to 0-63 bytes
        vmovdqa64       ZTMP4, [KS + IDX*4*64*2 + 64*4]
        vmovdqa64       ZTMP1, [KS + IDX*4*64*2 + 64*5]
        vmovdqa64       ZTMP2, [KS + IDX*4*64*2 + 64*6]
        vmovdqa64       ZTMP3, [KS + IDX*4*64*2 + 64*7]
        vmovdqa64       [KS + IDX*4*64*2], ZTMP4
        vmovdqa64       [KS + IDX*4*64*2 + 64], ZTMP1
        vmovdqa64       [KS + IDX*4*64*2 + 64*2], ZTMP2
        vmovdqa64       [KS + IDX*4*64*2 + 64*3], ZTMP3

%assign IDX (IDX + 1)
%endrep

        ;; - update tags
        mov             r12, 0x3333
        mov             r13, 0xCCCC
        kmovq           k1, r12
        kmovq           k2, r13

        vmovdqu64       ZTMP1, [T] ; Input tags
        vmovdqa64       ZTMP2, [rel shuf_mask_tags_0_4_8_12]
        vmovdqa64       ZTMP3, [rel shuf_mask_tags_0_4_8_12 + 64]
        ; Get result tags for 16 buffers in different position in each lane
        ; and blend these tags into an ZMM register.
        ; Then, XOR the results with the previous tags and write out the result.
        vpermt2d        DIGEST_0{k1}{z}, ZTMP2, DIGEST_1
        vpermt2d        DIGEST_2{k2}{z}, ZTMP3, DIGEST_3
        vpternlogq      ZTMP1, DIGEST_0, DIGEST_2, 0x96 ; A XOR B XOR C
        vmovdqu64       [T], ZTMP1

        ; Update data pointers
        vmovdqu64       ZTMP1, [DATA]
        vmovdqu64       ZTMP2, [DATA + 64]
        vpaddq          ZTMP1, [rel add_64]
        vpaddq          ZTMP2, [rel add_64]
        vmovdqu64       [DATA], ZTMP1
        vmovdqu64       [DATA + 64], ZTMP2

        ; Update array of lengths (subtract 512 bits from all lengths if valid lane)
        vmovdqa         YTMP1, [LEN]
        vpcmpw          k1, YTMP1, [rel all_ffs], 4
        vpsubw          YTMP1{k1}, [rel all_512w]
        vmovdqa         [LEN], YTMP1

%else ; USE_GFNI_VAES_VPCLMUL == 1

%define         DIGEST_0        zmm28
%define         DIGEST_1        zmm29
%define         DIGEST_2        zmm30
%define         DIGEST_3        zmm31

%define         DATA_ADDR       r10

        FUNC_SAVE

        vmovdqa  xmm5, [bit_reverse_table_l]
        vmovdqa  xmm6, [bit_reverse_table_h]
        vmovdqa  xmm7, [bit_reverse_and_table]

        mov             r12d, 0x55555555
        kmovd           k1, r12d

        ;; Read first buffers 0,4,8,12; then 1,5,9,13, and so on,
        ;; since the keystream is laid out this way, which chunks of
        ;; 16 bytes interleved. First the 128 bytes for
        ;; buffers 0,4,8,12 (total of 512 bytes), then the 128 bytes
        ;; for buffers 1,5,9,13, and so on
%assign I 0
%rep 4
%assign J 0
%rep 4

        vpxor   xmm9, xmm9
        mov     DATA_ADDR, [DATA + 8*(J*4 + I)]

%assign K 0
%rep 4
        ;; read 16 bytes and reverse bits
        vmovdqu  xmm0, [DATA_ADDR + 16*K]
        vpand    xmm1, xmm0, xmm7

        vpandn   xmm2, xmm7, xmm0
        vpsrld   xmm2, 4

        vpshufb  xmm8, xmm6, xmm1 ; bit reverse low nibbles (use high table)
        vpshufb  xmm4, xmm5, xmm2 ; bit reverse high nibbles (use low table)

        vpor     xmm8, xmm4
        ; xmm8 - bit reversed data bytes

        ;; ZUC authentication part
        ;; - 4x32 data bits
        ;; - set up KS
%if K != 0
        vmovdqa  xmm11, xmm12
        vmovdqu  xmm12, [KS + (16*J + I*512) + (K + 1)*(16*4)]
%else
        vmovdqu  xmm11, [KS + (16*J + I*512)]
        vmovdqu  xmm12, [KS + (16*J + I*512) + (16*4)]
%endif
        vpalignr xmm13, xmm12, xmm11, 8
        vpshufd  xmm2, xmm11, 0x61
        vpshufd  xmm3, xmm13, 0x61

        ;;  - set up DATA
        vpshufd xmm0{k1}{z}, xmm8, 0x10
        vpshufd xmm1{k1}{z}, xmm8, 0x32

        ;; - clmul
        ;; - xor the results from 4 32-bit words together
        vpclmulqdq xmm13, xmm0, xmm2, 0x00
        vpclmulqdq xmm14, xmm0, xmm2, 0x11
        vpclmulqdq xmm15, xmm1, xmm3, 0x00
        vpclmulqdq xmm8,  xmm1, xmm3, 0x11

        vpternlogq xmm13, xmm14, xmm8, 0x96
        vpternlogq xmm9, xmm13, xmm15, 0x96

%assign K (K + 1)
%endrep

        vinserti32x4 APPEND(DIGEST_, I), xmm9, J
%assign J (J + 1)
%endrep
        ; Memcpy KS 64-127 bytes to 0-63 bytes
        vmovdqa64       zmm23, [KS + I*4*64*2 + 64*4]
        vmovdqa64       zmm24, [KS + I*4*64*2 + 64*5]
        vmovdqa64       zmm25, [KS + I*4*64*2 + 64*6]
        vmovdqa64       zmm26, [KS + I*4*64*2 + 64*7]
        vmovdqa64       [KS + I*4*64*2], zmm23
        vmovdqa64       [KS + I*4*64*2 + 64], zmm24
        vmovdqa64       [KS + I*4*64*2 + 64*2], zmm25
        vmovdqa64       [KS + I*4*64*2 + 64*3], zmm26
%assign I (I + 1)
%endrep

        ;; - update tags
        mov             r12, 0x3333
        mov             r13, 0xCCCC
        kmovq           k1, r12
        kmovq           k2, r13

        vmovdqu64       zmm4, [T] ; Input tags
        vmovdqa64       zmm0, [rel shuf_mask_tags_0_4_8_12]
        vmovdqa64       zmm1, [rel shuf_mask_tags_0_4_8_12 + 64]
        ; Get result tags for 16 buffers in different position in each lane
        ; and blend these tags into an ZMM register.
        ; Then, XOR the results with the previous tags and write out the result.
        vpermt2d        DIGEST_0{k1}{z}, zmm0, DIGEST_1
        vpermt2d        DIGEST_2{k2}{z}, zmm1, DIGEST_3
        vpternlogq      zmm4, DIGEST_0, DIGEST_2, 0x96 ; A XOR B XOR C
        vmovdqu64       [T], zmm4

        ; Update data pointers
        vmovdqu64       zmm0, [DATA]
        vmovdqu64       zmm1, [DATA + 64]
        vpaddq          zmm0, [rel add_64]
        vpaddq          zmm1, [rel add_64]
        vmovdqu64       [DATA], zmm0
        vmovdqu64       [DATA + 64], zmm1

        ; Update array of lengths (if lane is valid, so length < UINT16_MAX)
        vmovdqa         ymm2, [LEN]
        vpcmpw          k1, ymm2, [rel all_ffs], 4 ; k1 -> valid lanes
        vpsubw          ymm2{k1}, [rel all_512w]
        vmovdqa         [LEN], ymm2

%endif ;; USE_GFNI_VAES_VPCLMUL == 0
        FUNC_RESTORE

        ret

%macro REMAINDER_16 1
%define %%KEY_SIZE      %1 ; [constant] Key size (128 or 256)

%ifdef LINUX
        %define         T       rdi
        %define	        KS      rsi
        %define	        DATA    rdx
        %define         LEN     rcx
        %define	        arg5    r8d
%else
        %define         T       rcx
        %define	        KS      rdx
        %define	        DATA    r8
        %define	        LEN     r9
        %define         arg5    [rsp + 40]
%endif

%define DIGEST_0        zmm28
%define DIGEST_1        zmm29
%define DIGEST_2        zmm30
%define DIGEST_3        zmm31

%define DATA_ADDR       r12
%define KS_ADDR         r13

%define N_BYTES         r14
%define OFFSET          r15

%define MIN_LEN         r10d
%define MIN_LEN_Q       r10
%define IDX             rax
%define TMP             rbx

        mov     MIN_LEN, arg5

        FUNC_SAVE

        vpbroadcastw ymm0, MIN_LEN
        ; Get mask of non-NULL lanes (lengths not set to UINT16_MAX, indicating that lane is not valid)
        vmovdqa ymm1, [LEN]
        vpcmpw k1, ymm1, [rel all_ffs], 4 ; NEQ

        ; Round up to nearest multiple of 32 bits
        vpaddw  ymm0{k1}, [rel all_31w]
        vpandq  ymm0, [rel all_ffe0w]

        ; Calculate remaining bits to authenticate after function call
        vpcmpuw k2, ymm1, ymm0, 1 ; Get mask of lengths that will be < 0 after subtracting
        vpsubw  ymm2{k1}, ymm1, ymm0
        vpxorq  ymm3, ymm3
        ; Set to zero the lengths of the lanes which are going to be completed
        vmovdqu16 ymm2{k2}, ymm3 ; YMM2 contain final lengths
        vmovdqu16 [LEN]{k1}, ymm2 ; Update in memory the final updated lengths

        ; Calculate number of bits to authenticate (up to 511 bits),
        ; for each lane, and store it in stack to be used later
        vpsubw  ymm1{k1}{z}, ymm2 ; Bits to authenticate in all lanes (zero out length of NULL lanes)
        sub     rsp, 32
        vmovdqu [rsp], ymm1

        xor     OFFSET, OFFSET

%if USE_GFNI_VAES_VPCLMUL != 1
        vmovdqa  xmm5, [bit_reverse_table_l]
        vmovdqa  xmm6, [bit_reverse_table_h]
        vmovdqa  xmm7, [bit_reverse_and_table]
%endif

        mov             r12d, 0x55555555
        kmovd           k2, r12d

        ;; Read first buffers 0,4,8,12; then 1,5,9,13, and so on,
        ;; since the keystream is laid out this way, which chunks of
        ;; 16 bytes interleved. First the 128 bytes for
        ;; buffers 0,4,8,12 (total of 512 bytes), then the 128 bytes
        ;; for buffers 1,5,9,13, and so on
%assign I 0
%rep 4
%assign J 0
%rep 4

        ; Read  length to authenticate for each buffer
        movzx   TMP, word [rsp + 2*(I*4 + J)]

        vpxor   xmm9, xmm9

        xor     OFFSET, OFFSET
        mov     DATA_ADDR, [DATA + 8*(I*4 + J)]

%assign K 0
%rep 4
        cmp     TMP, 128
        jb      APPEND3(%%Eia3RoundsAVX512_dq_end,I,J)

        ;; read 16 bytes and reverse bits
        vmovdqu xmm0, [DATA_ADDR + OFFSET]
%if USE_GFNI_VAES_VPCLMUL == 1
        vgf2p8affineqb  xmm8, xmm0, [rel bit_reverse_table], 0x00
%else
        vpand   xmm1, xmm0, xmm7

        vpandn  xmm2, xmm7, xmm0
        vpsrld  xmm2, 4

        vpshufb xmm8, xmm6, xmm1 ; bit reverse low nibbles (use high table)
        vpshufb xmm4, xmm5, xmm2 ; bit reverse high nibbles (use low table)

        vpor    xmm8, xmm4
%endif
        ; xmm8 - bit reversed data bytes

        ;; ZUC authentication part
        ;; - 4x32 data bits
        ;; - set up KS
%if K != 0
        vmovdqa  xmm11, xmm12
        vmovdqu  xmm12, [KS + (16*I + J*512) + OFFSET*4 + (16*4)]
%else
        vmovdqu  xmm11, [KS + (16*I + J*512) + (0*4)]
        vmovdqu  xmm12, [KS + (16*I + J*512) + (16*4)]
%endif
        vpalignr xmm13, xmm12, xmm11, 8
        vpshufd  xmm2, xmm11, 0x61
        vpshufd  xmm3, xmm13, 0x61

        ;;  - set up DATA
        vpshufd xmm0{k2}{z}, xmm8, 0x10
        vpshufd xmm1{k2}{z}, xmm8, 0x32

        ;; - clmul
        ;; - xor the results from 4 32-bit words together
        vpclmulqdq xmm13, xmm0, xmm2, 0x00
        vpclmulqdq xmm14, xmm0, xmm2, 0x11
        vpclmulqdq xmm15, xmm1, xmm3, 0x00
        vpclmulqdq xmm8,  xmm1, xmm3, 0x11

        vpternlogq xmm13, xmm14, xmm8, 0x96
        vpternlogq xmm9, xmm13, xmm15, 0x96
        add     OFFSET, 16
        sub     TMP, 128
%assign K (K + 1)
%endrep
APPEND3(%%Eia3RoundsAVX512_dq_end,I,J):

        or      TMP, TMP
        jz      APPEND3(%%Eia3RoundsAVX_end,I,J)

        ; Get number of bytes
        mov     N_BYTES, TMP
        add     N_BYTES, 7
        shr     N_BYTES, 3

        lea     r11, [rel byte64_len_to_mask_table]
        kmovq   k1, [r11 + N_BYTES*8]

        ;; Set up KS
        shl     OFFSET, 2
        vmovdqu xmm1, [KS + (16*I + J*512) + OFFSET]
        vmovdqu xmm2, [KS + (16*I + J*512) + OFFSET + 16*4]
        shr     OFFSET, 2
        vpalignr xmm13, xmm2, xmm1, 8
        vpshufd xmm11, xmm1, 0x61
        vpshufd xmm12, xmm13, 0x61

        ;; read up to 16 bytes of data, zero bits not needed if partial byte and bit-reverse
        vmovdqu8 xmm0{k1}{z}, [DATA_ADDR + OFFSET]
        ; check if there is a partial byte (less than 8 bits in last byte)
        mov     rax, TMP
        and     rax, 0x7
        shl     rax, 4
        lea     r11, [rel bit_mask_table]
        add     r11, rax

        ; Get mask to clear last bits
        vmovdqa xmm3, [r11]

        ; Shift left 16-N bytes to have the last byte always at the end of the XMM register
        ; to apply mask, then restore by shifting right same amount of bytes
        mov     r11, 16
        sub     r11, N_BYTES
        ; r13 = DATA_ADDR can be used at this stage
        XVPSLLB xmm0, r11, xmm4, r13
        vpandq  xmm0, xmm3
        XVPSRLB xmm0, r11, xmm4, r13

%if USE_GFNI_VAES_VPCLMUL == 1
        vgf2p8affineqb  xmm8, xmm0, [rel bit_reverse_table], 0x00
%else
        ; Bit reverse input data
        vpand   xmm1, xmm0, xmm7

        vpandn  xmm2, xmm7, xmm0
        vpsrld  xmm2, 4

        vpshufb xmm8, xmm6, xmm1 ; bit reverse low nibbles (use high table)
        vpshufb xmm3, xmm5, xmm2 ; bit reverse high nibbles (use low table)

        vpor    xmm8, xmm3
%endif

        ;;  - set up DATA
        vpshufd xmm0{k2}{z}, xmm8, 0x10 ; D 0-3 || Os || D 4-7 || 0s
        vpshufd xmm1{k2}{z}, xmm8, 0x32 ; D 8-11 || 0s || D 12-15 || 0s

        ;; - clmul
        ;; - xor the results from 4 32-bit words together
        vpclmulqdq xmm13, xmm0, xmm11, 0x00
        vpclmulqdq xmm14, xmm0, xmm11, 0x11
        vpclmulqdq xmm15, xmm1, xmm12, 0x00
        vpclmulqdq xmm8, xmm1, xmm12, 0x11
        vpternlogq xmm9, xmm14, xmm13, 0x96
        vpternlogq xmm9, xmm15, xmm8, 0x96

APPEND3(%%Eia3RoundsAVX_end,I,J):
        vinserti32x4 APPEND(DIGEST_, I), xmm9, J
%assign J (J + 1)
%endrep
%assign I (I + 1)
%endrep

        ;; - update tags
        mov             TMP, 0x00FF
        kmovq           k1, TMP
        mov             TMP, 0xFF00
        kmovq           k2, TMP

        vmovdqu64       zmm4, [T] ; Input tags
        vmovdqa64       zmm0, [rel shuf_mask_tags_0_1_2_3]
        vmovdqa64       zmm1, [rel shuf_mask_tags_0_1_2_3 + 64]
        ; Get result tags for 16 buffers in different position in each lane
        ; and blend these tags into an ZMM register.
        ; Then, XOR the results with the previous tags and write out the result.
        vpermt2d        DIGEST_0{k1}{z}, zmm0, DIGEST_1
        vpermt2d        DIGEST_2{k2}{z}, zmm1, DIGEST_3
        vpternlogq      zmm4, DIGEST_0, DIGEST_2, 0x96 ; A XOR B XOR C

        vmovdqa64       [T], zmm4 ; Store temporary digests

        ; These last steps should be done only for the buffers that
        ; have no more data to authenticate
        xor     IDX, IDX
%%start_loop:
        ; Update data pointer
        movzx   r11d, word [rsp + IDX*2]
        shr     r11d, 3 ; length authenticated in bytes
        add     [DATA + IDX*8], r11

        cmp     word [LEN + 2*IDX], 0
        jnz     %%skip_comput

        mov     r11, IDX
        and     r11, 0x3
        shl     r11, 9 ; * 512

        mov     r12, IDX
        shr     r12, 2
        shl     r12, 4 ; * 16
        add     r11, r12
        lea     KS_ADDR, [KS + r11]

        ; Read digest
        mov     r12d, [T + 4*IDX]

        ; Read keyStr[MIN_LEN / 32]
        movzx   TMP, word [rsp + 2*IDX]
        mov     r15, TMP
        shr     r15, 5
        mov     r11, r15
        shr     r15, 2
        shl     r15, (4+2)
        and     r11, 0x3
        shl     r11, 2
        add     r15, r11
        mov     r11, r15
        and     r11, 0xf
        cmp     r11, 12
        je      %%_read_2dwords
        mov     r11, [KS_ADDR + r15]
        jmp     %%_ks_qword_read

        ;; The 8 bytes of KS are separated
%%_read_2dwords:
        mov     r11d, [KS_ADDR + r15]
        mov     r15d, [KS_ADDR + r15 + (4+48)]
        shl     r15, 32
        or      r11, r15
%%_ks_qword_read:
        ; Rotate left by MIN_LEN % 32
        mov     r15, rcx
        mov     rcx, TMP
        and     rcx, 0x1F
        rol     r11, cl
        mov     rcx, r15
        ; XOR with current digest
        xor     r12d, r11d

%if %%KEY_SIZE == 128
        ; Read keystr[L - 1] (last dword of keyStr)
        add     TMP, (31 + 64)
        shr     TMP, 5 ; L
        dec     TMP
        mov     r11, TMP
        shr     r11, 2
        shl     r11, (4+2)
        and     TMP, 0x3
        shl     TMP, 2
        add     TMP, r11
        mov     r11d, [KS_ADDR + TMP]
        ; XOR with current digest
        xor     r12d, r11d
%endif

        ; byte swap and write digest out
        bswap   r12d
        mov     [T + 4*IDX], r12d

%%skip_comput:
        inc     IDX
        cmp     IDX, 16
        jne     %%start_loop

        add     rsp, 32

        ; Memcpy last 8 bytes of KS into start
        add     MIN_LEN, 31
        shr     MIN_LEN, 5
        shl     MIN_LEN, 2 ; Offset where to copy the last 8 bytes from

        mov     r12d, MIN_LEN
        shr     MIN_LEN, 4
        shl     MIN_LEN, (4+2)
        and     r12d, 0xf
        add     MIN_LEN, r12d
        cmp     r12d, 12
        je      %%_copy_2dwords

%assign %%i 0
%rep 4
%assign %%j 0
%rep 4
        mov     TMP, [KS + 512*%%i + 16*%%j + MIN_LEN_Q]
        mov     [KS + 512*%%i + 16*%%j], TMP
%assign %%j (%%j + 1)
%endrep
%assign %%i (%%i + 1)
%endrep
        jmp     %%_ks_copied

        ;; The 8 bytes of KS are separated
%%_copy_2dwords:
%assign %%i 0
%rep 4
%assign %%j 0
%rep 4
        mov     DWORD(TMP), [KS + 512*%%i + 16*%%j + MIN_LEN_Q]
        mov     [KS + 512*%%i + 16*%%j], DWORD(TMP)
        mov     DWORD(TMP), [KS + 512*%%i + 16*%%j + (48+4) + MIN_LEN_Q]
        mov     [KS + 512*%%i + 16*%%j + 4], DWORD(TMP)
%assign %%j (%%j + 1)
%endrep
%assign %%i (%%i + 1)
%endrep
%%_ks_copied:
        vzeroupper
        FUNC_RESTORE
        ret
%endmacro

;;
;; extern void asm_Eia3RemainderAVX512_16(uint32_t *T, const void **ks, const void **data, uint64_t n_bits)
;;
;;  @param [in] T: Array of digests for all 16 buffers
;;  @param [in] KS : Array of pointers to key stream for all 16 buffers
;;  @param [in] DATA : Array of pointers to data for all 16 buffers
;;  @param [in] N_BITS (number data bits to process)
;;
align 64
MKGLOBAL(ZUC128_REMAINDER_16,function,internal)
ZUC128_REMAINDER_16:
        endbranch64
        REMAINDER_16 128

;;
;; extern void asm_Eia3_256_RemainderAVX512_16(uint32_t *T, const void **ks, const void **data, uint64_t n_bits)
;;
;;  @param [in] T: Array of digests for all 16 buffers
;;  @param [in] KS : Array of pointers to key stream for all 16 buffers
;;  @param [in] DATA : Array of pointers to data for all 16 buffers
;;  @param [in] N_BITS (number data bits to process)
;;
align 64
MKGLOBAL(ZUC256_REMAINDER_16,function,internal)
ZUC256_REMAINDER_16:
        endbranch64
        REMAINDER_16 256

; Following functions only need AVX512 instructions (no VAES, GFNI, etc.)
%if USE_GFNI_VAES_VPCLMUL == 0
;;
;; extern void asm_Eia3RemainderAVX512(uint32_t *T, const void *ks,
;;                                     const void *data, uint64_t n_bits)
;;
;; Returns authentication update value to be XOR'ed with current authentication tag
;;
;;  @param [in] T (digest pointer)
;;  @param [in] KS (key stream pointer)
;;  @param [in] DATA (data pointer)
;;  @param [in] N_BITS (number data bits to process)
;;
align 64
MKGLOBAL(asm_Eia3RemainderAVX512,function,internal)
asm_Eia3RemainderAVX512:
        endbranch64
%ifdef LINUX
	%define		T	rdi
	%define		KS	rsi
	%define		DATA	rdx
	%define		N_BITS	rcx
%else
        %define         T       rcx
	%define		KS	rdx
	%define		DATA	r8
	%define		N_BITS	r9
%endif

%define N_BYTES rbx
%define OFFSET  r15

        FUNC_SAVE

        vmovdqa  xmm5, [bit_reverse_table_l]
        vmovdqa  xmm6, [bit_reverse_table_h]
        vmovdqa  xmm7, [bit_reverse_and_table]
        vpxor    xmm9, xmm9
        mov      r12d, 0x55555555
        kmovd    k2, r12d

        xor     OFFSET, OFFSET
%assign I 0
%rep 3
        cmp     N_BITS, 128
        jb      Eia3RoundsAVX512_dq_end

        ;; read 16 bytes and reverse bits
        vmovdqu xmm0, [DATA + OFFSET]
        vpand   xmm1, xmm0, xmm7

        vpandn  xmm2, xmm7, xmm0
        vpsrld  xmm2, 4

        vpshufb xmm8, xmm6, xmm1 ; bit reverse low nibbles (use high table)
        vpshufb xmm4, xmm5, xmm2 ; bit reverse high nibbles (use low table)

        vpor    xmm8, xmm4
        ; xmm8 - bit reversed data bytes

        ;; ZUC authentication part
        ;; - 4x32 data bits
        ;; - set up KS
%if I != 0
        vmovdqa  xmm11, xmm12
        vmovdqu  xmm12, [KS + OFFSET + (4*4)]
%else
        vmovdqu  xmm11, [KS + (0*4)]
        vmovdqu  xmm12, [KS + (4*4)]
%endif
        vpalignr xmm13, xmm12, xmm11, 8
        vpshufd  xmm2, xmm11, 0x61
        vpshufd  xmm3, xmm13, 0x61

        ;;  - set up DATA
        vpshufd xmm0{k2}{z}, xmm8, 0x10
        vpshufd xmm1{k2}{z}, xmm8, 0x32

        ;; - clmul
        ;; - xor the results from 4 32-bit words together
        vpclmulqdq xmm13, xmm0, xmm2, 0x00
        vpclmulqdq xmm14, xmm0, xmm2, 0x11
        vpclmulqdq xmm15, xmm1, xmm3, 0x00
        vpclmulqdq xmm8,  xmm1, xmm3, 0x11

        vpternlogq xmm13, xmm14, xmm8, 0x96
        vpternlogq xmm9, xmm13, xmm15, 0x96

        add     OFFSET, 16
        sub     N_BITS, 128
%assign I (I + 1)
%endrep
Eia3RoundsAVX512_dq_end:

        or      N_BITS, N_BITS
        jz      Eia3RoundsAVX_end

        ; Get number of bytes
        mov     N_BYTES, N_BITS
        add     N_BYTES, 7
        shr     N_BYTES, 3

        lea     r10, [rel byte64_len_to_mask_table]
        kmovq   k1, [r10 + N_BYTES*8]

        ;; Set up KS
        vmovdqu xmm1, [KS + OFFSET]
        vmovdqu xmm2, [KS + OFFSET + 16]
        vpalignr xmm13, xmm2, xmm1, 8
        vpshufd xmm11, xmm1, 0x61
        vpshufd xmm12, xmm13, 0x61

        ;; read up to 16 bytes of data, zero bits not needed if partial byte and bit-reverse
        vmovdqu8 xmm0{k1}{z}, [DATA + OFFSET]
        ; check if there is a partial byte (less than 8 bits in last byte)
        mov     rax, N_BITS
        and     rax, 0x7
        shl     rax, 4
        lea     r10, [rel bit_mask_table]
        add     r10, rax

        ; Get mask to clear last bits
        vmovdqa xmm3, [r10]

        ; Shift left 16-N bytes to have the last byte always at the end of the XMM register
        ; to apply mask, then restore by shifting right same amount of bytes
        mov     r10, 16
        sub     r10, N_BYTES
        XVPSLLB xmm0, r10, xmm4, r11
        vpandq  xmm0, xmm3
        XVPSRLB xmm0, r10, xmm4, r11

        ; Bit reverse input data
        vpand   xmm1, xmm0, xmm7

        vpandn  xmm2, xmm7, xmm0
        vpsrld  xmm2, 4

        vpshufb xmm8, xmm6, xmm1 ; bit reverse low nibbles (use high table)
        vpshufb xmm3, xmm5, xmm2 ; bit reverse high nibbles (use low table)

        vpor    xmm8, xmm3

        ;; Set up DATA
        vpshufd xmm0{k2}{z}, xmm8, 0x10 ; D 0-3 || Os || D 4-7 || 0s
        vpshufd xmm1{k2}{z}, xmm8, 0x32 ; D 8-11 || 0s || D 12-15 || 0s

        ;; - clmul
        ;; - xor the results from 4 32-bit words together
        vpclmulqdq xmm13, xmm0, xmm11, 0x00
        vpclmulqdq xmm14, xmm0, xmm11, 0x11
        vpclmulqdq xmm15, xmm1, xmm12, 0x00
        vpclmulqdq xmm8, xmm1, xmm12, 0x11
        vpternlogq xmm9, xmm14, xmm13, 0x96
        vpternlogq xmm9, xmm15, xmm8, 0x96

Eia3RoundsAVX_end:
        mov     r11d, [T]
        vmovq   rax, xmm9
        shr     rax, 32
        xor     eax, r11d

        ; Read keyStr[N_BITS / 32]
        lea     r10, [N_BITS + OFFSET*8] ; Restore original N_BITS
        shr     r10, 5
        mov     r11, [KS + r10*4]

        ; Rotate left by N_BITS % 32
        mov     r12, rcx ; Save RCX
        mov     rcx, N_BITS
        and     rcx, 0x1F
        rol     r11, cl
        mov     rcx, r12 ; Restore RCX

        ; XOR with previous digest calculation
        xor     eax, r11d

       ; Read keyStr[L - 1] (last double word of keyStr)
        lea     r10, [N_BITS + OFFSET*8] ; Restore original N_BITS
        add     r10, (31 + 64)
        shr     r10, 5 ; L
        dec     r10
        mov     r11d, [KS + r10 * 4]

        ; XOR with previous digest calculation and bswap it
        xor     eax, r11d
        bswap   eax
        mov     [T], eax

        FUNC_RESTORE

        ret

;;
;;extern void asm_Eia3Round64BAVX512(uint32_t *T, const void *KS, const void *DATA)
;;
;; Updates authentication tag T based on keystream KS and DATA.
;; - it processes 64 bytes of DATA
;; - reads data in 16 byte chunks and bit reverses them
;; - reads and re-arranges KS
;; - employs clmul for the XOR & ROL part
;;
;;  @param [in] T (digest pointer)
;;  @param [in] KS (key stream pointer)
;;  @param [in] DATA (data pointer)
;;
align 64
MKGLOBAL(asm_Eia3Round64BAVX512,function,internal)
asm_Eia3Round64BAVX512:
        endbranch64
%ifdef LINUX
	%define		T	rdi
	%define		KS	rsi
	%define		DATA	rdx
%else
	%define		T	rcx
	%define		KS	rdx
	%define		DATA	r8
%endif

        FUNC_SAVE

        vmovdqa  xmm5, [bit_reverse_table_l]
        vmovdqa  xmm6, [bit_reverse_table_h]
        vmovdqa  xmm7, [bit_reverse_and_table]
        vpxor    xmm9, xmm9

        mov      r12d, 0x55555555
        kmovd    k1, r12d
%assign I 0
%rep 4
        ;; read 16 bytes and reverse bits
        vmovdqu  xmm0, [DATA + 16*I]
        vpand    xmm1, xmm0, xmm7

        vpandn   xmm2, xmm7, xmm0
        vpsrld   xmm2, 4

        vpshufb  xmm8, xmm6, xmm1 ; bit reverse low nibbles (use high table)
        vpshufb  xmm4, xmm5, xmm2 ; bit reverse high nibbles (use low table)

        vpor     xmm8, xmm4
        ; xmm8 - bit reversed data bytes

        ;; ZUC authentication part
        ;; - 4x32 data bits
        ;; - set up KS
%if I != 0
        vmovdqa  xmm11, xmm12
        vmovdqu  xmm12, [KS + (I*16) + (4*4)]
%else
        vmovdqu  xmm11, [KS + (I*16) + (0*4)]
        vmovdqu  xmm12, [KS + (I*16) + (4*4)]
%endif
        vpalignr xmm13, xmm12, xmm11, 8
        vpshufd  xmm2, xmm11, 0x61
        vpshufd  xmm3, xmm13, 0x61

        ;;  - set up DATA
        vpshufd xmm0{k1}{z}, xmm8, 0x10
        vpshufd xmm1{k1}{z}, xmm8, 0x32

        ;; - clmul
        ;; - xor the results from 4 32-bit words together
        vpclmulqdq xmm13, xmm0, xmm2, 0x00
        vpclmulqdq xmm14, xmm0, xmm2, 0x11
        vpclmulqdq xmm15, xmm1, xmm3, 0x00
        vpclmulqdq xmm8,  xmm1, xmm3, 0x11

        vpternlogq xmm13, xmm14, xmm8, 0x96
        vpternlogq xmm9, xmm13, xmm15, 0x96

%assign I (I + 1)
%endrep

        ;; - update T
        vmovq   rax, xmm9
        shr     rax, 32
        mov     r10d, [T]
        xor     eax, r10d
        mov     [T], eax

        FUNC_RESTORE

        ret

%endif ; USE_GFNI_VAES_VPCLMUL == 0

;----------------------------------------------------------------------------------------
;----------------------------------------------------------------------------------------

mksection stack-noexec

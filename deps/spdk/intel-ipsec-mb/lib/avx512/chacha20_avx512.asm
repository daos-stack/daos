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
%include "include/imb_job.asm"
%include "include/clear_regs.asm"
%include "include/const.inc"
%include "include/reg_sizes.asm"
%include "include/transpose_avx512.asm"
%include "include/aes_common.asm"
%include "include/chacha_poly_defines.asm"
%include "include/cet.inc"
mksection .rodata
default rel

align 16
constants:
dd      0x61707865, 0x3320646e, 0x79622d32, 0x6b206574

align 64
add_0_3:
dd      0x00000000, 0x00000000, 0x00000000, 0x00000000
dd      0x00000001, 0x00000000, 0x00000000, 0x00000000
dd      0x00000002, 0x00000000, 0x00000000, 0x00000000
dd      0x00000003, 0x00000000, 0x00000000, 0x00000000

align 64
add_4_7:
dd      0x00000004, 0x00000000, 0x00000000, 0x00000000
dd      0x00000005, 0x00000000, 0x00000000, 0x00000000
dd      0x00000006, 0x00000000, 0x00000000, 0x00000000
dd      0x00000007, 0x00000000, 0x00000000, 0x00000000

align 64
add_1_4:
dd      0x00000001, 0x00000000, 0x00000000, 0x00000000
dd      0x00000002, 0x00000000, 0x00000000, 0x00000000
dd      0x00000003, 0x00000000, 0x00000000, 0x00000000
dd      0x00000004, 0x00000000, 0x00000000, 0x00000000

align 64
add_5_8:
dd      0x00000005, 0x00000000, 0x00000000, 0x00000000
dd      0x00000006, 0x00000000, 0x00000000, 0x00000000
dd      0x00000007, 0x00000000, 0x00000000, 0x00000000
dd      0x00000008, 0x00000000, 0x00000000, 0x00000000

align 64
add_16:
dd      0x00000010, 0x00000010, 0x00000010, 0x00000010
dd      0x00000010, 0x00000010, 0x00000010, 0x00000010
dd      0x00000010, 0x00000010, 0x00000010, 0x00000010
dd      0x00000010, 0x00000010, 0x00000010, 0x00000010

align 64
set_1_16:
dd      0x00000001, 0x00000002, 0x00000003, 0x00000004
dd      0x00000005, 0x00000006, 0x00000007, 0x00000008
dd      0x00000009, 0x0000000a, 0x0000000b, 0x0000000c
dd      0x0000000d, 0x0000000e, 0x0000000f, 0x00000010

align 64
set_0_15:
dd      0x00000000, 0x00000001, 0x00000002, 0x00000003
dd      0x00000004, 0x00000005, 0x00000006, 0x00000007
dd      0x00000008, 0x00000009, 0x0000000a, 0x0000000b
dd      0x0000000c, 0x0000000d, 0x0000000e, 0x0000000f

align 64
len_to_mask:
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

align 32
poly_clamp_r:
dq      0x0ffffffc0fffffff, 0x0ffffffc0ffffffc
dq      0xffffffffffffffff, 0xffffffffffffffff

%define APPEND(a,b) a %+ b
%define APPEND3(a,b,c) a %+ b %+ c

%ifdef LINUX
%define arg1    rdi
%define arg2    rsi
%define arg3    rdx
%define arg4    rcx
%define arg5    r8
%else
%define arg1    rcx
%define arg2    rdx
%define arg3    r8
%define arg4    r9
%define arg5    [rsp + 40]
%endif

%define job     arg1

%define added_len r12

mksection .text

%macro ZMM_OP_X4 9
        ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16 16, %1,%2,%3,%4,%5,%2,%3,%4,%5,%6,%7,%8,%9
%endmacro

%macro ZMM_ROLS_X4 5
%define %%ZMM_OP1_1      %1
%define %%ZMM_OP1_2      %2
%define %%ZMM_OP1_3      %3
%define %%ZMM_OP1_4      %4
%define %%BITS_TO_ROTATE %5

        vprold  %%ZMM_OP1_1, %%BITS_TO_ROTATE
        vprold  %%ZMM_OP1_2, %%BITS_TO_ROTATE
        vprold  %%ZMM_OP1_3, %%BITS_TO_ROTATE
        vprold  %%ZMM_OP1_4, %%BITS_TO_ROTATE

%endmacro

%macro GEN_POLY_KEY 2
%define %%AKEY_PTR      %1
%define %%CHACHA_STATE  %2

        vpandq  YWORD(%%CHACHA_STATE), [rel poly_clamp_r]
        vmovdqa64 [%%AKEY_PTR], YWORD(%%CHACHA_STATE)

%endmacro

;
; Macro adding original state values to processed state values
; and transposing 16x16 u32 from first 16 ZMM registers,
; creating keystreams.
; Note that the registers are tranposed in a different
; order, so first register (IN00) containing row 0
; will not contain the first column of the matrix, but
; row 1 and same with other registers.
; This is done to minimize the number of registers clobbered.
; Once transposition is done, keystream is XOR'd with the plaintext
; and output buffer is written.
;
%macro GENERATE_1K_KS_AND_ENCRYPT 36
%define %%IN00_KS01  %1 ; [in/clobbered] Input row 0 of state, bytes 64-127 of keystream
%define %%IN01_KS02  %2 ; [in/clobbered] Input row 1 of state, bytes 128-191 of keystream
%define %%IN02_KS15  %3 ; [in/clobbered] Input row 2 of state, bytes 960-1023 of keystream
%define %%IN03_KS04  %4 ; [in/clobbered] Input row 3 of state, bytes 256-319 of keystream
%define %%IN04_KS08  %5 ; [in/clobbered] Input row 4 of state, bytes 512-575 of keystream
%define %%IN05       %6 ; [in/clobbered] Input row 5 of state, bytes 576-639 of keystream
%define %%IN06_KS13  %7 ; [in/clobbered] Input row 6 of state, bytes 832-895 of keystream
%define %%IN07_KS07  %8 ; [in/clobbered] Input row 7 of state, bytes 448-511 of keystream
%define %%IN08_KS05  %9 ; [in/clobbered] Input row 8 of state, bytes 320-383 of keystream
%define %%IN09_KS00 %10 ; [in/clobbered] Input row 9 of state, bytes 0-63 of keystream
%define %%IN10_KS06 %11 ; [in/clobbered] Input row 10 of state, bytes 384-447 of keystream
%define %%IN11_KS11 %12 ; [in/clobbered] Input row 11 of state, bytes 704-767 of keystream
%define %%IN12_KS12 %13 ; [in/clobbered] Input row 12 of state, bytes 768-831 of keystream
%define %%IN13_KS03 %14 ; [in/clobbered] Input row 13 of state, bytes 192-255 of keystream
%define %%IN14_KS14 %15 ; [in/clobbered] Input row 14 of state, bytes 896-959 of keystream
%define %%IN15      %16 ; [in/clobbered] Input row 15 of state, bytes 640-703 of keystream
%define %%IN_ORIG00_KS09  %17 ; [in/clobbered] Original input row 0, bytes 576-639 of keystream
%define %%IN_ORIG01_KS10  %18 ; [in/clobbered] Original input row 1, bytes 640-703 of keystream
%define %%IN_ORIG02  %19 ; [in] Original input row 2
%define %%IN_ORIG03  %20 ; [in] Original input row 3
%define %%IN_ORIG04  %21 ; [in] Original input row 4
%define %%IN_ORIG05  %22 ; [in] Original input row 5
%define %%IN_ORIG06  %23 ; [in] Original input row 6
%define %%IN_ORIG07  %24 ; [in] Original input row 7
%define %%IN_ORIG08  %25 ; [in] Original input row 8
%define %%IN_ORIG09  %26 ; [in] Original input row 9
%define %%IN_ORIG10  %27 ; [in] Original input row 10
%define %%IN_ORIG11  %28 ; [in] Original input row 11
%define %%IN_ORIG12  %29 ; [in] Original input row 12
%define %%IN_ORIG13  %30 ; [in] Original input row 13
%define %%IN_ORIG14  %31 ; [in] Original input row 14
%define %%IN_ORIG15  %32 ; [in] Original input row 15
%define %%SRC        %33 ; [in] Source pointer
%define %%DST        %34 ; [in] Destination pointer
%define %%OFF        %35 ; [in] Offset into src/dst pointers
%define %%GEN_KEY    %36 ; [in] Generate poly key

        vpaddd %%IN00_KS01, %%IN_ORIG00_KS09
        vpaddd %%IN01_KS02, %%IN_ORIG01_KS10
        vpaddd %%IN02_KS15, %%IN_ORIG02
        vpaddd %%IN03_KS04, %%IN_ORIG03

        ;; Deal with first lanes 0-7
        ; T0, T1 free
        vpunpckldq      %%IN_ORIG00_KS09, %%IN00_KS01, %%IN01_KS02
        vpunpckhdq      %%IN00_KS01, %%IN00_KS01, %%IN01_KS02
        vpunpckldq      %%IN_ORIG01_KS10, %%IN02_KS15, %%IN03_KS04
        vpunpckhdq      %%IN02_KS15, %%IN02_KS15, %%IN03_KS04

        ; IN01_KS02, IN03_KS04 free
        vpunpcklqdq     %%IN03_KS04, %%IN_ORIG00_KS09, %%IN_ORIG01_KS10
        vpunpckhqdq     %%IN01_KS02, %%IN_ORIG00_KS09, %%IN_ORIG01_KS10
        vpunpcklqdq     %%IN_ORIG00_KS09, %%IN00_KS01, %%IN02_KS15
        vpunpckhqdq     %%IN00_KS01, %%IN00_KS01, %%IN02_KS15

        vpaddd %%IN04_KS08, %%IN_ORIG04
        vpaddd %%IN05, %%IN_ORIG05
        vpaddd %%IN06_KS13, %%IN_ORIG06
        vpaddd %%IN07_KS07, %%IN_ORIG07

        ; IN02_KS15, T1 free
        vpunpckldq      %%IN_ORIG01_KS10, %%IN04_KS08, %%IN05
        vpunpckhdq      %%IN04_KS08, %%IN04_KS08, %%IN05
        vpunpckldq      %%IN02_KS15, %%IN06_KS13, %%IN07_KS07
        vpunpckhdq      %%IN06_KS13, %%IN06_KS13, %%IN07_KS07

        ; IN07_KS07, IN05 free
        vpunpcklqdq     %%IN07_KS07, %%IN_ORIG01_KS10, %%IN02_KS15
        vpunpckhqdq     %%IN05, %%IN_ORIG01_KS10, %%IN02_KS15
        vpunpcklqdq     %%IN02_KS15, %%IN04_KS08, %%IN06_KS13
        vpunpckhqdq     %%IN04_KS08, %%IN04_KS08, %%IN06_KS13

        ; T1, IN06_KS13 free
        vshufi64x2      %%IN_ORIG01_KS10, %%IN03_KS04, %%IN07_KS07, 0x44
        vshufi64x2      %%IN03_KS04, %%IN03_KS04, %%IN07_KS07, 0xee
        vshufi64x2      %%IN06_KS13, %%IN01_KS02, %%IN05, 0x44
        vshufi64x2      %%IN01_KS02, %%IN01_KS02, %%IN05, 0xee
        vshufi64x2      %%IN07_KS07, %%IN_ORIG00_KS09, %%IN02_KS15, 0x44
        vshufi64x2      %%IN02_KS15, %%IN_ORIG00_KS09, %%IN02_KS15, 0xee
        vshufi64x2      %%IN05, %%IN00_KS01, %%IN04_KS08, 0x44
        vshufi64x2      %%IN00_KS01, %%IN00_KS01, %%IN04_KS08, 0xee

        ;; Deal with lanes 8-15
        vpaddd %%IN08_KS05, %%IN_ORIG08
        vpaddd %%IN09_KS00, %%IN_ORIG09
        vpaddd %%IN10_KS06, %%IN_ORIG10
        vpaddd %%IN11_KS11, %%IN_ORIG11

        vpunpckldq      %%IN_ORIG00_KS09, %%IN08_KS05, %%IN09_KS00
        vpunpckhdq      %%IN08_KS05, %%IN08_KS05, %%IN09_KS00
        vpunpckldq      %%IN04_KS08, %%IN10_KS06, %%IN11_KS11
        vpunpckhdq      %%IN10_KS06, %%IN10_KS06, %%IN11_KS11

        vpunpcklqdq     %%IN09_KS00, %%IN_ORIG00_KS09, %%IN04_KS08
        vpunpckhqdq     %%IN04_KS08, %%IN_ORIG00_KS09, %%IN04_KS08
        vpunpcklqdq     %%IN11_KS11, %%IN08_KS05, %%IN10_KS06
        vpunpckhqdq     %%IN08_KS05, %%IN08_KS05, %%IN10_KS06

        vpaddd %%IN12_KS12, %%IN_ORIG12
        vpaddd %%IN13_KS03, %%IN_ORIG13
        vpaddd %%IN14_KS14, %%IN_ORIG14
        vpaddd %%IN15, %%IN_ORIG15

        vpunpckldq      %%IN_ORIG00_KS09, %%IN12_KS12, %%IN13_KS03
        vpunpckhdq      %%IN12_KS12, %%IN12_KS12, %%IN13_KS03
        vpunpckldq      %%IN10_KS06, %%IN14_KS14, %%IN15
        vpunpckhdq      %%IN14_KS14, %%IN14_KS14, %%IN15

        vpunpcklqdq     %%IN13_KS03, %%IN_ORIG00_KS09, %%IN10_KS06
        vpunpckhqdq     %%IN10_KS06, %%IN_ORIG00_KS09, %%IN10_KS06
        vpunpcklqdq     %%IN15, %%IN12_KS12, %%IN14_KS14
        vpunpckhqdq     %%IN12_KS12, %%IN12_KS12, %%IN14_KS14

        vshufi64x2      %%IN14_KS14, %%IN09_KS00, %%IN13_KS03, 0x44
        vshufi64x2      %%IN09_KS00, %%IN09_KS00, %%IN13_KS03, 0xee
        vshufi64x2      %%IN_ORIG00_KS09, %%IN04_KS08, %%IN10_KS06, 0x44
        vshufi64x2      %%IN10_KS06, %%IN04_KS08, %%IN10_KS06, 0xee
        vshufi64x2      %%IN13_KS03, %%IN11_KS11, %%IN15, 0x44
        vshufi64x2      %%IN11_KS11, %%IN11_KS11, %%IN15, 0xee
        vshufi64x2      %%IN15, %%IN08_KS05, %%IN12_KS12, 0x44
        vshufi64x2      %%IN08_KS05, %%IN08_KS05, %%IN12_KS12, 0xee

%ifidn %%GEN_KEY, gen_poly_key
        vshufi64x2      %%IN12_KS12, %%IN03_KS04, %%IN09_KS00, 0xdd
        vpxorq          %%IN12_KS12, [%%SRC + %%OFF + 64*11]
        vmovdqu64       [%%DST + %%OFF + 64*11], %%IN12_KS12

        vshufi64x2      %%IN04_KS08, %%IN03_KS04, %%IN09_KS00, 0x88
        vpxorq          %%IN04_KS08, [%%SRC + %%OFF + 64*7]
        vmovdqu64       [%%DST + %%OFF + 64*7], %%IN04_KS08

        vshufi64x2      %%IN09_KS00, %%IN_ORIG01_KS10, %%IN14_KS14, 0x88
        GEN_POLY_KEY    arg2, %%IN09_KS00

        vshufi64x2      %%IN03_KS04, %%IN_ORIG01_KS10, %%IN14_KS14, 0xdd
        vpxorq          %%IN03_KS04, [%%SRC + %%OFF + 64*3]
        vmovdqu64       [%%DST + %%OFF + 64*3], %%IN03_KS04

        vshufi64x2      %%IN14_KS14, %%IN02_KS15, %%IN11_KS11, 0xdd
        vpxorq          %%IN14_KS14, [%%SRC + %%OFF + 64*13]
        vmovdqu64       [%%DST + %%OFF + 64*13], %%IN14_KS14

        vshufi64x2      %%IN_ORIG01_KS10, %%IN02_KS15, %%IN11_KS11, 0x88
        vpxorq          %%IN_ORIG01_KS10, [%%SRC + %%OFF + 64*9]
        vmovdqu64       [%%DST + %%OFF + 64*9], %%IN_ORIG01_KS10

        vshufi64x2      %%IN11_KS11, %%IN00_KS01, %%IN08_KS05, 0x88
        vpxorq          %%IN11_KS11, [%%SRC + %%OFF + 64*10]
        vmovdqu64       [%%DST + %%OFF + 64*10], %%IN11_KS11

        vshufi64x2      %%IN02_KS15, %%IN00_KS01, %%IN08_KS05, 0xdd
        vpxorq          %%IN02_KS15, [%%SRC + %%OFF + 64*14]
        vmovdqu64       [%%DST + %%OFF + 64*14], %%IN02_KS15

        vshufi64x2      %%IN00_KS01, %%IN06_KS13, %%IN_ORIG00_KS09, 0x88
        vpxorq          %%IN00_KS01, [%%SRC + %%OFF]
        vmovdqu64       [%%DST + %%OFF], %%IN00_KS01

        vshufi64x2      %%IN08_KS05, %%IN06_KS13, %%IN_ORIG00_KS09, 0xdd
        vpxorq          %%IN08_KS05, [%%SRC + %%OFF + 64*4]
        vmovdqu64       [%%DST + %%OFF + 64*4], %%IN08_KS05

        vshufi64x2      %%IN_ORIG00_KS09, %%IN01_KS02, %%IN10_KS06, 0x88
        vpxorq          %%IN_ORIG00_KS09, [%%SRC + %%OFF + 64*8]
        vmovdqu64       [%%DST + %%OFF + 64*8], %%IN_ORIG00_KS09

        vshufi64x2      %%IN06_KS13, %%IN01_KS02, %%IN10_KS06, 0xdd
        vpxorq          %%IN06_KS13, [%%SRC + %%OFF + 64*12]
        vmovdqu64       [%%DST + %%OFF + 64*12], %%IN06_KS13

        vshufi64x2      %%IN01_KS02, %%IN07_KS07, %%IN13_KS03, 0x88
        vpxorq          %%IN01_KS02, [%%SRC + %%OFF + 64]
        vmovdqu64       [%%DST + %%OFF + 64], %%IN01_KS02

        vshufi64x2      %%IN10_KS06, %%IN07_KS07, %%IN13_KS03, 0xdd
        vpxorq          %%IN10_KS06, [%%SRC + %%OFF + 64*5]
        vmovdqu64       [%%DST + %%OFF + 64*5], %%IN10_KS06

        vshufi64x2      %%IN13_KS03, %%IN05, %%IN15, 0x88
        vpxorq          %%IN13_KS03, [%%SRC + %%OFF + 64*2]
        vmovdqu64       [%%DST + %%OFF + 64*2], %%IN13_KS03

        vshufi64x2      %%IN07_KS07, %%IN05, %%IN15, 0xdd
        vpxorq          %%IN07_KS07, [%%SRC + %%OFF + 64*6]
        vmovdqu64       [%%DST + %%OFF + 64*6], %%IN07_KS07
%else ; GEN_KEY != gen_poly_key
        vshufi64x2      %%IN12_KS12, %%IN03_KS04, %%IN09_KS00, 0xdd
        vpxorq          %%IN12_KS12, [%%SRC + %%OFF + 64*12]
        vmovdqu64       [%%DST + %%OFF + 64*12], %%IN12_KS12

        vshufi64x2      %%IN04_KS08, %%IN03_KS04, %%IN09_KS00, 0x88
        vpxorq          %%IN04_KS08, [%%SRC + %%OFF + 64*8]
        vmovdqu64       [%%DST + %%OFF + 64*8], %%IN04_KS08

        vshufi64x2      %%IN09_KS00, %%IN_ORIG01_KS10, %%IN14_KS14, 0x88
        vpxorq          %%IN09_KS00, [%%SRC + %%OFF]
        vmovdqu64       [%%DST + %%OFF], %%IN09_KS00

        vshufi64x2      %%IN03_KS04, %%IN_ORIG01_KS10, %%IN14_KS14, 0xdd
        vpxorq          %%IN03_KS04, [%%SRC + %%OFF + 64*4]
        vmovdqu64       [%%DST + %%OFF + 64*4], %%IN03_KS04

        vshufi64x2      %%IN14_KS14, %%IN02_KS15, %%IN11_KS11, 0xdd
        vpxorq          %%IN14_KS14, [%%SRC + %%OFF + 64*14]
        vmovdqu64       [%%DST + %%OFF + 64*14], %%IN14_KS14

        vshufi64x2      %%IN_ORIG01_KS10, %%IN02_KS15, %%IN11_KS11, 0x88
        vpxorq          %%IN_ORIG01_KS10, [%%SRC + %%OFF + 64*10]
        vmovdqu64       [%%DST + %%OFF + 64*10], %%IN_ORIG01_KS10

        vshufi64x2      %%IN11_KS11, %%IN00_KS01, %%IN08_KS05, 0x88
        vpxorq          %%IN11_KS11, [%%SRC + %%OFF + 64*11]
        vmovdqu64       [%%DST + %%OFF + 64*11], %%IN11_KS11

        vshufi64x2      %%IN02_KS15, %%IN00_KS01, %%IN08_KS05, 0xdd
        vpxorq          %%IN02_KS15, [%%SRC + %%OFF + 64*15]
        vmovdqu64       [%%DST + %%OFF + 64*15], %%IN02_KS15

        vshufi64x2      %%IN00_KS01, %%IN06_KS13, %%IN_ORIG00_KS09, 0x88
        vpxorq          %%IN00_KS01, [%%SRC + %%OFF + 64*1]
        vmovdqu64       [%%DST + %%OFF + 64*1], %%IN00_KS01

        vshufi64x2      %%IN08_KS05, %%IN06_KS13, %%IN_ORIG00_KS09, 0xdd
        vpxorq          %%IN08_KS05, [%%SRC + %%OFF + 64*5]
        vmovdqu64       [%%DST + %%OFF + 64*5], %%IN08_KS05

        vshufi64x2      %%IN_ORIG00_KS09, %%IN01_KS02, %%IN10_KS06, 0x88
        vpxorq          %%IN_ORIG00_KS09, [%%SRC + %%OFF + 64*9]
        vmovdqu64       [%%DST + %%OFF + 64*9], %%IN_ORIG00_KS09

        vshufi64x2      %%IN06_KS13, %%IN01_KS02, %%IN10_KS06, 0xdd
        vpxorq          %%IN06_KS13, [%%SRC + %%OFF + 64*13]
        vmovdqu64       [%%DST + %%OFF + 64*13], %%IN06_KS13

        vshufi64x2      %%IN01_KS02, %%IN07_KS07, %%IN13_KS03, 0x88
        vpxorq          %%IN01_KS02, [%%SRC + %%OFF + 64*2]
        vmovdqu64       [%%DST + %%OFF + 64*2], %%IN01_KS02

        vshufi64x2      %%IN10_KS06, %%IN07_KS07, %%IN13_KS03, 0xdd
        vpxorq          %%IN10_KS06, [%%SRC + %%OFF + 64*6]
        vmovdqu64       [%%DST + %%OFF + 64*6], %%IN10_KS06

        vshufi64x2      %%IN13_KS03, %%IN05, %%IN15, 0x88
        vpxorq          %%IN13_KS03, [%%SRC + %%OFF + 64*3]
        vmovdqu64       [%%DST + %%OFF + 64*3], %%IN13_KS03

        vshufi64x2      %%IN07_KS07, %%IN05, %%IN15, 0xdd
        vpxorq          %%IN07_KS07, [%%SRC + %%OFF + 64*7]
        vmovdqu64       [%%DST + %%OFF + 64*7], %%IN07_KS07
%endif

%endmacro

;;
;; Performs a quarter round on all 4 columns,
;; resulting in a full round
;;
%macro QUARTER_ROUND_X4 4
%define %%A %1 ;; [in/out] ZMM register containing value A of all 4 columns
%define %%B %2 ;; [in/out] ZMM register containing value B of all 4 columns
%define %%C %3 ;; [in/out] ZMM register containing value C of all 4 columns
%define %%D %4 ;; [in/out] ZMM register containing value D of all 4 columns

        vpaddd          %%A, %%B
        vpxorq          %%D, %%A
        vprold          %%D, 16
        vpaddd          %%C, %%D
        vpxorq          %%B, %%C
        vprold          %%B, 12
        vpaddd          %%A, %%B
        vpxorq          %%D, %%A
        vprold          %%D, 8
        vpaddd          %%C, %%D
        vpxorq          %%B, %%C
        vprold          %%B, 7

%endmacro

;;
;; Rotates the registers to prepare the data
;; from column round to diagonal round
;;
%macro COLUMN_TO_DIAG 3
%define %%B %1 ;; [in/out] ZMM register containing value B of all 4 columns
%define %%C %2 ;; [in/out] ZMM register containing value C of all 4 columns
%define %%D %3 ;; [in/out] ZMM register containing value D of all 4 columns

        vpshufd         %%B, %%B, 0x39 ; 0b00111001 ;; 0,3,2,1
        vpshufd         %%C, %%C, 0x4E ; 0b01001110 ;; 1,0,3,2
        vpshufd         %%D, %%D, 0x93 ; 0b10010011 ;; 2,1,0,3

%endmacro

;;
;; Rotates the registers to prepare the data
;; from diagonal round to column round
;;
%macro DIAG_TO_COLUMN 3
%define %%B %1 ;; [in/out] ZMM register containing value B of all 4 columns
%define %%C %2 ;; [in/out] ZMM register containing value C of all 4 columns
%define %%D %3 ;; [in/out] ZMM register containing value D of all 4 columns

        vpshufd         %%B, %%B, 0x93 ; 0b10010011 ; 2,1,0,3
        vpshufd         %%C, %%C, 0x4E ; 0b01001110 ;  1,0,3,2
        vpshufd         %%D, %%D, 0x39 ; 0b00111001 ;  0,3,2,1

%endmacro

;;
;; Generates up to 64*8 bytes of keystream
;;
%macro GENERATE_512_KS 18
%define %%A_L_KS0        %1  ;; [out] ZMM A / Bytes 0-63    of KS
%define %%B_L_KS1        %2  ;; [out] ZMM B / Bytes 64-127  of KS
%define %%C_L_KS2        %3  ;; [out] ZMM C / Bytes 128-191 of KS
%define %%D_L_KS3        %4  ;; [out] ZMM D / Bytes 192-255 of KS
%define %%A_H_KS4        %5  ;; [out] ZMM A / Bytes 256-319 of KS (or "none" in NUM_BLOCKS == 4)
%define %%B_H_KS5        %6  ;; [out] ZMM B / Bytes 320-383 of KS (or "none" in NUM_BLOCKS == 4)
%define %%C_H_KS6        %7  ;; [out] ZMM C / Bytes 384-447 of KS (or "none" in NUM_BLOCKS == 4)
%define %%D_H_KS7        %8  ;; [out] ZMM D / Bytes 448-511 of KS (or "none" in NUM_BLOCKS == 4)
%define %%STATE_IN_A_L   %9  ;; [in] ZMM containing state "A" part
%define %%STATE_IN_B_L   %10 ;; [in] ZMM containing state "B" part
%define %%STATE_IN_C_L   %11 ;; [in] ZMM containing state "C" part
%define %%STATE_IN_D_L   %12 ;; [in] ZMM containing state "D" part
%define %%STATE_IN_D_H   %13 ;; [in] ZMM containing state "D" part (or "none" in NUM_BLOCKS == 4)
%define %%ZTMP0          %14 ;; [clobbered] Temp ZMM reg
%define %%ZTMP1          %15 ;; [clobbered] Temp ZMM reg
%define %%ZTMP2          %16 ;; [clobbered] Temp ZMM reg
%define %%ZTMP3          %17 ;; [clobbered] Temp ZMM reg
%define %%NUM_BLOCKS     %18 ;; [in] Num blocks to encrypt (4 or 8)

        vmovdqa64       %%A_L_KS0, %%STATE_IN_A_L
        vmovdqa64       %%B_L_KS1, %%STATE_IN_B_L
        vmovdqa64       %%C_L_KS2, %%STATE_IN_C_L
        vmovdqa64       %%D_L_KS3, %%STATE_IN_D_L
%if %%NUM_BLOCKS == 8
        vmovdqa64       %%A_H_KS4, %%STATE_IN_A_L
        vmovdqa64       %%B_H_KS5, %%STATE_IN_B_L
        vmovdqa64       %%C_H_KS6, %%STATE_IN_C_L
        vmovdqa64       %%D_H_KS7, %%STATE_IN_D_H
%endif

%rep 10
%if %%NUM_BLOCKS == 4
        QUARTER_ROUND_X4 %%A_L_KS0, %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
        COLUMN_TO_DIAG %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
        QUARTER_ROUND_X4 %%A_L_KS0, %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
        DIAG_TO_COLUMN %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
%else
        QUARTER_ROUND_X4 %%A_L_KS0, %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
        QUARTER_ROUND_X4 %%A_H_KS4, %%B_H_KS5, %%C_H_KS6, %%D_H_KS7
        COLUMN_TO_DIAG %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
        COLUMN_TO_DIAG %%B_H_KS5, %%C_H_KS6, %%D_H_KS7
        QUARTER_ROUND_X4 %%A_L_KS0, %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
        QUARTER_ROUND_X4 %%A_H_KS4, %%B_H_KS5, %%C_H_KS6, %%D_H_KS7
        DIAG_TO_COLUMN %%B_L_KS1, %%C_L_KS2, %%D_L_KS3
        DIAG_TO_COLUMN %%B_H_KS5, %%C_H_KS6, %%D_H_KS7
%endif ;; %%NUM_BLOCKS == 4
%endrep

        vpaddd %%A_L_KS0, %%STATE_IN_A_L
        vpaddd %%B_L_KS1, %%STATE_IN_B_L
        vpaddd %%C_L_KS2, %%STATE_IN_C_L
        vpaddd %%D_L_KS3, %%STATE_IN_D_L

        TRANSPOSE4_U128_INPLACE %%A_L_KS0, %%B_L_KS1, %%C_L_KS2, %%D_L_KS3, \
                                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3
%if %%NUM_BLOCKS == 8
        vpaddd %%A_H_KS4, %%STATE_IN_A_L
        vpaddd %%B_H_KS5, %%STATE_IN_B_L
        vpaddd %%C_H_KS6, %%STATE_IN_C_L
        vpaddd %%D_H_KS7, %%STATE_IN_D_H

        TRANSPOSE4_U128_INPLACE %%A_H_KS4, %%B_H_KS5, %%C_H_KS6, %%D_H_KS7, \
                                %%ZTMP0, %%ZTMP1, %%ZTMP2, %%ZTMP3
%endif
%endmacro

;;
;; Performs a full chacha20 round on 16 states,
;; consisting of 4 quarter rounds, which are done in parallel
;;
%macro CHACHA20_ROUND 16
%define %%ZMM_DWORD_A1  %1  ;; [in/out] ZMM register containing dword A for first quarter round
%define %%ZMM_DWORD_A2  %2  ;; [in/out] ZMM register containing dword A for second quarter round
%define %%ZMM_DWORD_A3  %3  ;; [in/out] ZMM register containing dword A for third quarter round
%define %%ZMM_DWORD_A4  %4  ;; [in/out] ZMM register containing dword A for fourth quarter round
%define %%ZMM_DWORD_B1  %5  ;; [in/out] ZMM register containing dword B for first quarter round
%define %%ZMM_DWORD_B2  %6  ;; [in/out] ZMM register containing dword B for second quarter round
%define %%ZMM_DWORD_B3  %7  ;; [in/out] ZMM register containing dword B for third quarter round
%define %%ZMM_DWORD_B4  %8  ;; [in/out] ZMM register containing dword B for fourth quarter round
%define %%ZMM_DWORD_C1  %9  ;; [in/out] ZMM register containing dword C for first quarter round
%define %%ZMM_DWORD_C2 %10  ;; [in/out] ZMM register containing dword C for second quarter round
%define %%ZMM_DWORD_C3 %11  ;; [in/out] ZMM register containing dword C for third quarter round
%define %%ZMM_DWORD_C4 %12  ;; [in/out] ZMM register containing dword C for fourth quarter round
%define %%ZMM_DWORD_D1 %13  ;; [in/out] ZMM register containing dword D for first quarter round
%define %%ZMM_DWORD_D2 %14  ;; [in/out] ZMM register containing dword D for second quarter round
%define %%ZMM_DWORD_D3 %15  ;; [in/out] ZMM register containing dword D for third quarter round
%define %%ZMM_DWORD_D4 %16  ;; [in/out] ZMM register containing dword D for fourth quarter round

        ; A += B
        ZMM_OP_X4 vpaddd, %%ZMM_DWORD_A1, %%ZMM_DWORD_A2, %%ZMM_DWORD_A3, %%ZMM_DWORD_A4, \
                          %%ZMM_DWORD_B1, %%ZMM_DWORD_B2, %%ZMM_DWORD_B3, %%ZMM_DWORD_B4
        ; D ^= A
        ZMM_OP_X4 vpxorq, %%ZMM_DWORD_D1, %%ZMM_DWORD_D2, %%ZMM_DWORD_D3, %%ZMM_DWORD_D4, \
                          %%ZMM_DWORD_A1, %%ZMM_DWORD_A2, %%ZMM_DWORD_A3, %%ZMM_DWORD_A4

        ; D <<< 16
        ZMM_ROLS_X4 %%ZMM_DWORD_D1, %%ZMM_DWORD_D2, %%ZMM_DWORD_D3, %%ZMM_DWORD_D4, 16

        ; C += D
        ZMM_OP_X4 vpaddd, %%ZMM_DWORD_C1, %%ZMM_DWORD_C2, %%ZMM_DWORD_C3, %%ZMM_DWORD_C4, \
                          %%ZMM_DWORD_D1, %%ZMM_DWORD_D2, %%ZMM_DWORD_D3, %%ZMM_DWORD_D4
        ; B ^= C
        ZMM_OP_X4 vpxorq, %%ZMM_DWORD_B1, %%ZMM_DWORD_B2, %%ZMM_DWORD_B3, %%ZMM_DWORD_B4, \
                          %%ZMM_DWORD_C1, %%ZMM_DWORD_C2, %%ZMM_DWORD_C3, %%ZMM_DWORD_C4

        ; B <<< 12
        ZMM_ROLS_X4 %%ZMM_DWORD_B1, %%ZMM_DWORD_B2, %%ZMM_DWORD_B3, %%ZMM_DWORD_B4, 12

        ; A += B
        ZMM_OP_X4 vpaddd, %%ZMM_DWORD_A1, %%ZMM_DWORD_A2, %%ZMM_DWORD_A3, %%ZMM_DWORD_A4, \
                          %%ZMM_DWORD_B1, %%ZMM_DWORD_B2, %%ZMM_DWORD_B3, %%ZMM_DWORD_B4
        ; D ^= A
        ZMM_OP_X4 vpxorq, %%ZMM_DWORD_D1, %%ZMM_DWORD_D2, %%ZMM_DWORD_D3, %%ZMM_DWORD_D4, \
                          %%ZMM_DWORD_A1, %%ZMM_DWORD_A2, %%ZMM_DWORD_A3, %%ZMM_DWORD_A4

        ; D <<< 8
        ZMM_ROLS_X4 %%ZMM_DWORD_D1, %%ZMM_DWORD_D2, %%ZMM_DWORD_D3, %%ZMM_DWORD_D4, 8

        ; C += D
        ZMM_OP_X4 vpaddd, %%ZMM_DWORD_C1, %%ZMM_DWORD_C2, %%ZMM_DWORD_C3, %%ZMM_DWORD_C4, \
                          %%ZMM_DWORD_D1, %%ZMM_DWORD_D2, %%ZMM_DWORD_D3, %%ZMM_DWORD_D4
        ; B ^= C
        ZMM_OP_X4 vpxorq, %%ZMM_DWORD_B1, %%ZMM_DWORD_B2, %%ZMM_DWORD_B3, %%ZMM_DWORD_B4, \
                          %%ZMM_DWORD_C1, %%ZMM_DWORD_C2, %%ZMM_DWORD_C3, %%ZMM_DWORD_C4

        ; B <<< 7
        ZMM_ROLS_X4 %%ZMM_DWORD_B1, %%ZMM_DWORD_B2, %%ZMM_DWORD_B3, %%ZMM_DWORD_B4, 7
%endmacro

;;
;; Generates 64*16 bytes of keystream and encrypt up to 1KB of input data
;;
%macro ENCRYPT_1K 36
%define %%ZMM_DWORD0       %1   ;; [clobbered] ZMM to contain dword 0 of all states
%define %%ZMM_DWORD1       %2   ;; [clobbered] ZMM to contain dword 1 of all states
%define %%ZMM_DWORD2       %3   ;; [clobbered] ZMM to contain dword 2 of all states
%define %%ZMM_DWORD3       %4   ;; [clobbered] ZMM to contain dword 3 of all states
%define %%ZMM_DWORD4       %5   ;; [clobbered] ZMM to contain dword 4 of all states
%define %%ZMM_DWORD5       %6   ;; [clobbered] ZMM to contain dword 5 of all states
%define %%ZMM_DWORD6       %7   ;; [clobbered] ZMM to contain dword 6 of all states
%define %%ZMM_DWORD7       %8   ;; [clobbered] ZMM to contain dword 7 of all states
%define %%ZMM_DWORD8       %9   ;; [clobbered] ZMM to contain dword 8 of all states
%define %%ZMM_DWORD9       %10  ;; [clobbered] ZMM to contain dword 9 of all states
%define %%ZMM_DWORD10      %11  ;; [clobbered] ZMM to contain dword 10 of all states
%define %%ZMM_DWORD11      %12  ;; [clobbered] ZMM to contain dword 11 of all states
%define %%ZMM_DWORD12      %13  ;; [clobbered] ZMM to contain dword 12 of all states
%define %%ZMM_DWORD13      %14  ;; [clobbered] ZMM to contain dword 13 of all states
%define %%ZMM_DWORD14      %15  ;; [clobbered] ZMM to contain dword 14 of all states
%define %%ZMM_DWORD15      %16  ;; [clobbered] ZMM to contain dword 15 of all states
%define %%ZMM_DWORD_ORIG0  %17  ;; [in/clobbered] ZMM containing dword 0 of all states / Temp ZMM register
%define %%ZMM_DWORD_ORIG1  %18  ;; [in/clobbered] ZMM containing dword 1 of all states / Temp ZMM register
%define %%ZMM_DWORD_ORIG2  %19  ;; [in] ZMM containing dword 2 of all states
%define %%ZMM_DWORD_ORIG3  %20  ;; [in] ZMM containing dword 3 of all states
%define %%ZMM_DWORD_ORIG4  %21  ;; [in] ZMM containing dword 4 of all states
%define %%ZMM_DWORD_ORIG5  %22  ;; [in] ZMM containing dword 5 of all states
%define %%ZMM_DWORD_ORIG6  %23  ;; [in] ZMM containing dword 6 of all states
%define %%ZMM_DWORD_ORIG7  %24  ;; [in] ZMM containing dword 7 of all states
%define %%ZMM_DWORD_ORIG8  %25  ;; [in] ZMM containing dword 8 of all states
%define %%ZMM_DWORD_ORIG9  %26  ;; [in] ZMM containing dword 9 of all states
%define %%ZMM_DWORD_ORIG10 %27  ;; [in] ZMM containing dword 10 of all states
%define %%ZMM_DWORD_ORIG11 %28  ;; [in] ZMM containing dword 11 of all states
%define %%ZMM_DWORD_ORIG12 %29  ;; [in] ZMM containing dword 12 of all states
%define %%ZMM_DWORD_ORIG13 %30  ;; [in] ZMM containing dword 13 of all states
%define %%ZMM_DWORD_ORIG14 %31  ;; [in] ZMM containing dword 14 of all states
%define %%ZMM_DWORD_ORIG15 %32  ;; [in] ZMM containing dword 15 of all states
%define %%SRC              %33  ;; [in] Source pointer
%define %%DST              %34  ;; [in] Destination pointer
%define %%OFF              %35  ;; [in] Offset into src/dst pointers
%define %%GEN_KEY          %36  ;; [in] Generate poly key

%assign i 0
%rep 16
        vmovdqa64 APPEND(%%ZMM_DWORD, i), APPEND(%%ZMM_DWORD_ORIG, i)
%assign i (i + 1)
%endrep

%rep 10

        ;;; Each full round consists of 8 quarter rounds, 4 column rounds and 4 diagonal rounds
        ;;; For first 4 column rounds:
        ;;; A = 0, 1, 2, 3;   B = 4, 5, 6, 7;
        ;;; C = 8, 9, 10, 11; D = 12, 13, 14, 15
        CHACHA20_ROUND %%ZMM_DWORD0, %%ZMM_DWORD1, %%ZMM_DWORD2, %%ZMM_DWORD3, \
                       %%ZMM_DWORD4, %%ZMM_DWORD5, %%ZMM_DWORD6, %%ZMM_DWORD7, \
                       %%ZMM_DWORD8, %%ZMM_DWORD9, %%ZMM_DWORD10, %%ZMM_DWORD11, \
                       %%ZMM_DWORD12, %%ZMM_DWORD13, %%ZMM_DWORD14, %%ZMM_DWORD15
        ;;; For 4 diagonal rounds:
        ;;; A = 0, 1, 2, 3;   B = 5, 6, 7, 4;
        ;;; C = 10, 11, 8, 9; D = 15, 12, 13, 14
        CHACHA20_ROUND %%ZMM_DWORD0, %%ZMM_DWORD1, %%ZMM_DWORD2, %%ZMM_DWORD3, \
                       %%ZMM_DWORD5, %%ZMM_DWORD6, %%ZMM_DWORD7, %%ZMM_DWORD4, \
                       %%ZMM_DWORD10, %%ZMM_DWORD11, %%ZMM_DWORD8, %%ZMM_DWORD9, \
                       %%ZMM_DWORD15, %%ZMM_DWORD12, %%ZMM_DWORD13, %%ZMM_DWORD14
%endrep

        ;; Add original states to processed states, transpose
        ;; these states to form the 64*16 bytes of keystream,
        ;; XOR with plaintext and write ciphertext out
        GENERATE_1K_KS_AND_ENCRYPT %%ZMM_DWORD0, %%ZMM_DWORD1, %%ZMM_DWORD2, %%ZMM_DWORD3, \
                                   %%ZMM_DWORD4, %%ZMM_DWORD5, %%ZMM_DWORD6, %%ZMM_DWORD7, \
                                   %%ZMM_DWORD8, %%ZMM_DWORD9, %%ZMM_DWORD10, %%ZMM_DWORD11, \
                                   %%ZMM_DWORD12, %%ZMM_DWORD13, %%ZMM_DWORD14, %%ZMM_DWORD15, \
                                   %%ZMM_DWORD_ORIG0, %%ZMM_DWORD_ORIG1, %%ZMM_DWORD_ORIG2, \
                                   %%ZMM_DWORD_ORIG3,%%ZMM_DWORD_ORIG4, %%ZMM_DWORD_ORIG5, \
                                   %%ZMM_DWORD_ORIG6, %%ZMM_DWORD_ORIG7, %%ZMM_DWORD_ORIG8, \
                                   %%ZMM_DWORD_ORIG9, %%ZMM_DWORD_ORIG10, %%ZMM_DWORD_ORIG11, \
                                   %%ZMM_DWORD_ORIG12, %%ZMM_DWORD_ORIG13, %%ZMM_DWORD_ORIG14, \
                                   %%ZMM_DWORD_ORIG15, %%SRC, %%DST, %%OFF, %%GEN_KEY
%endmacro

;
; Macro adding original state values to processed state values
; and transposing 16x16 u32 from first 16 ZMM registers,
; creating keystreams.
; Note that the registers are tranposed in a different
; order, so first register (IN00) containing row 0
; will not contain the first column of the matrix, but
; row 1 and same with other registers.
; This is done to minimize the number of registers clobbered.
;
%macro ADD_TRANSPOSE_STATE_KS 32
%define %%IN00_OUT01  %1 ; [in/out] Input row 0, Output column 1
%define %%IN01_OUT02  %2 ; [in/out] Input row 1, Output column 2
%define %%IN02_OUT15  %3 ; [in/out] Input row 2, Output column 15
%define %%IN03_OUT04  %4 ; [in/out] Input row 3, Output column 4
%define %%IN04_OUT08  %5 ; [in/out] Input row 4, Output column 8
%define %%IN05_OUT09  %6 ; [in/out] Input row 5, Output column 9
%define %%IN06_OUT13  %7 ; [in/out] Input row 6, Output column 13
%define %%IN07_OUT07  %8 ; [in/out] Input row 7, Output column 7
%define %%IN08_OUT05  %9 ; [in/out] Input row 8, Output column 5
%define %%IN09_OUT00 %10 ; [in/out] Input row 9, Output column 0
%define %%IN10_OUT06 %11 ; [in/out] Input row 10, Output column 6
%define %%IN11_OUT11 %12 ; [in/out] Input row 11, Output column 11
%define %%IN12_OUT12 %13 ; [in/out] Input row 12, Output column 12
%define %%IN13_OUT03 %14 ; [in/out] Input row 13, Output column 3
%define %%IN14_OUT14 %15 ; [in/out] Input row 14, Output column 14
%define %%IN15_OUT10 %16 ; [in/out] Input row 15, Output column 10
%define %%IN_ORIG00  %17 ; [in/clobbered] Original input row 0
%define %%IN_ORIG01  %18 ; [in/clobbered] Original input row 1
%define %%IN_ORIG02  %19 ; [in] Original input row 2
%define %%IN_ORIG03  %20 ; [in] Original input row 3
%define %%IN_ORIG04  %21 ; [in] Original input row 4
%define %%IN_ORIG05  %22 ; [in] Original input row 5
%define %%IN_ORIG06  %23 ; [in] Original input row 6
%define %%IN_ORIG07  %24 ; [in] Original input row 7
%define %%IN_ORIG08  %25 ; [in] Original input row 8
%define %%IN_ORIG09  %26 ; [in] Original input row 9
%define %%IN_ORIG10  %27 ; [in] Original input row 10
%define %%IN_ORIG11  %28 ; [in] Original input row 11
%define %%IN_ORIG12  %29 ; [in] Original input row 12
%define %%IN_ORIG13  %30 ; [in] Original input row 13
%define %%IN_ORIG14  %31 ; [in] Original input row 14
%define %%IN_ORIG15  %32 ; [in] Original input row 15

        vpaddd %%IN00_OUT01, %%IN_ORIG00
        vpaddd %%IN01_OUT02, %%IN_ORIG01
        vpaddd %%IN02_OUT15, %%IN_ORIG02
        vpaddd %%IN03_OUT04, %%IN_ORIG03

        ;; Deal with first lanes 0-7
        ; T0, T1 free
        vpunpckldq      %%IN_ORIG00, %%IN00_OUT01, %%IN01_OUT02
        vpunpckhdq      %%IN00_OUT01, %%IN00_OUT01, %%IN01_OUT02
        vpunpckldq      %%IN_ORIG01, %%IN02_OUT15, %%IN03_OUT04
        vpunpckhdq      %%IN02_OUT15, %%IN02_OUT15, %%IN03_OUT04

        ; IN01_OUT02, IN03_OUT04 free
        vpunpcklqdq     %%IN03_OUT04, %%IN_ORIG00, %%IN_ORIG01
        vpunpckhqdq     %%IN01_OUT02, %%IN_ORIG00, %%IN_ORIG01
        vpunpcklqdq     %%IN_ORIG00, %%IN00_OUT01, %%IN02_OUT15
        vpunpckhqdq     %%IN00_OUT01, %%IN00_OUT01, %%IN02_OUT15

        vpaddd %%IN04_OUT08, %%IN_ORIG04
        vpaddd %%IN05_OUT09, %%IN_ORIG05
        vpaddd %%IN06_OUT13, %%IN_ORIG06
        vpaddd %%IN07_OUT07, %%IN_ORIG07

        ; IN02_OUT15, T1 free
        vpunpckldq      %%IN_ORIG01, %%IN04_OUT08, %%IN05_OUT09
        vpunpckhdq      %%IN04_OUT08, %%IN04_OUT08, %%IN05_OUT09
        vpunpckldq      %%IN02_OUT15, %%IN06_OUT13, %%IN07_OUT07
        vpunpckhdq      %%IN06_OUT13, %%IN06_OUT13, %%IN07_OUT07

        ; IN07_OUT07, IN05_OUT09 free
        vpunpcklqdq     %%IN07_OUT07, %%IN_ORIG01, %%IN02_OUT15
        vpunpckhqdq     %%IN05_OUT09, %%IN_ORIG01, %%IN02_OUT15
        vpunpcklqdq     %%IN02_OUT15, %%IN04_OUT08, %%IN06_OUT13
        vpunpckhqdq     %%IN04_OUT08, %%IN04_OUT08, %%IN06_OUT13

        ; T1, IN06_OUT13 free
        vshufi64x2      %%IN_ORIG01, %%IN03_OUT04, %%IN07_OUT07, 0x44
        vshufi64x2      %%IN03_OUT04, %%IN03_OUT04, %%IN07_OUT07, 0xee
        vshufi64x2      %%IN06_OUT13, %%IN01_OUT02, %%IN05_OUT09, 0x44
        vshufi64x2      %%IN01_OUT02, %%IN01_OUT02, %%IN05_OUT09, 0xee
        vshufi64x2      %%IN07_OUT07, %%IN_ORIG00, %%IN02_OUT15, 0x44
        vshufi64x2      %%IN02_OUT15, %%IN_ORIG00, %%IN02_OUT15, 0xee
        vshufi64x2      %%IN05_OUT09, %%IN00_OUT01, %%IN04_OUT08, 0x44
        vshufi64x2      %%IN00_OUT01, %%IN00_OUT01, %%IN04_OUT08, 0xee

        ;; Deal with lanes 8-15
        vpaddd %%IN08_OUT05, %%IN_ORIG08
        vpaddd %%IN09_OUT00, %%IN_ORIG09
        vpaddd %%IN10_OUT06, %%IN_ORIG10
        vpaddd %%IN11_OUT11, %%IN_ORIG11

        vpunpckldq      %%IN_ORIG00, %%IN08_OUT05, %%IN09_OUT00
        vpunpckhdq      %%IN08_OUT05, %%IN08_OUT05, %%IN09_OUT00
        vpunpckldq      %%IN04_OUT08, %%IN10_OUT06, %%IN11_OUT11
        vpunpckhdq      %%IN10_OUT06, %%IN10_OUT06, %%IN11_OUT11

        vpunpcklqdq     %%IN09_OUT00, %%IN_ORIG00, %%IN04_OUT08
        vpunpckhqdq     %%IN04_OUT08, %%IN_ORIG00, %%IN04_OUT08
        vpunpcklqdq     %%IN11_OUT11, %%IN08_OUT05, %%IN10_OUT06
        vpunpckhqdq     %%IN08_OUT05, %%IN08_OUT05, %%IN10_OUT06

        vpaddd %%IN12_OUT12, %%IN_ORIG12
        vpaddd %%IN13_OUT03, %%IN_ORIG13
        vpaddd %%IN14_OUT14, %%IN_ORIG14
        vpaddd %%IN15_OUT10, %%IN_ORIG15

        vpunpckldq      %%IN_ORIG00, %%IN12_OUT12, %%IN13_OUT03
        vpunpckhdq      %%IN12_OUT12, %%IN12_OUT12, %%IN13_OUT03
        vpunpckldq      %%IN10_OUT06, %%IN14_OUT14, %%IN15_OUT10
        vpunpckhdq      %%IN14_OUT14, %%IN14_OUT14, %%IN15_OUT10

        vpunpcklqdq     %%IN13_OUT03, %%IN_ORIG00, %%IN10_OUT06
        vpunpckhqdq     %%IN10_OUT06, %%IN_ORIG00, %%IN10_OUT06
        vpunpcklqdq     %%IN15_OUT10, %%IN12_OUT12, %%IN14_OUT14
        vpunpckhqdq     %%IN12_OUT12, %%IN12_OUT12, %%IN14_OUT14

        vshufi64x2      %%IN14_OUT14, %%IN09_OUT00, %%IN13_OUT03, 0x44
        vshufi64x2      %%IN09_OUT00, %%IN09_OUT00, %%IN13_OUT03, 0xee
        vshufi64x2      %%IN_ORIG00, %%IN04_OUT08, %%IN10_OUT06, 0x44
        vshufi64x2      %%IN10_OUT06, %%IN04_OUT08, %%IN10_OUT06, 0xee
        vshufi64x2      %%IN13_OUT03, %%IN11_OUT11, %%IN15_OUT10, 0x44
        vshufi64x2      %%IN11_OUT11, %%IN11_OUT11, %%IN15_OUT10, 0xee
        vshufi64x2      %%IN15_OUT10, %%IN08_OUT05, %%IN12_OUT12, 0x44
        vshufi64x2      %%IN08_OUT05, %%IN08_OUT05, %%IN12_OUT12, 0xee

        vshufi64x2      %%IN12_OUT12, %%IN03_OUT04, %%IN09_OUT00, 0xdd
        vshufi64x2      %%IN04_OUT08, %%IN03_OUT04, %%IN09_OUT00, 0x88
        vshufi64x2      %%IN03_OUT04, %%IN_ORIG01, %%IN14_OUT14, 0xdd
        vshufi64x2      %%IN09_OUT00, %%IN_ORIG01, %%IN14_OUT14, 0x88
        vshufi64x2      %%IN14_OUT14, %%IN02_OUT15, %%IN11_OUT11, 0xdd
        vshufi64x2      %%IN_ORIG01, %%IN02_OUT15, %%IN11_OUT11, 0x88
        vshufi64x2      %%IN11_OUT11, %%IN00_OUT01, %%IN08_OUT05, 0x88
        vshufi64x2      %%IN02_OUT15, %%IN00_OUT01, %%IN08_OUT05, 0xdd
        vshufi64x2      %%IN00_OUT01, %%IN06_OUT13, %%IN_ORIG00, 0x88
        vshufi64x2      %%IN08_OUT05, %%IN06_OUT13, %%IN_ORIG00, 0xdd
        vshufi64x2      %%IN_ORIG00, %%IN01_OUT02, %%IN10_OUT06, 0x88
        vshufi64x2      %%IN06_OUT13, %%IN01_OUT02, %%IN10_OUT06, 0xdd
        vshufi64x2      %%IN01_OUT02, %%IN07_OUT07, %%IN13_OUT03, 0x88
        vshufi64x2      %%IN10_OUT06, %%IN07_OUT07, %%IN13_OUT03, 0xdd
        vshufi64x2      %%IN13_OUT03, %%IN05_OUT09, %%IN15_OUT10, 0x88
        vshufi64x2      %%IN07_OUT07, %%IN05_OUT09, %%IN15_OUT10, 0xdd

        vmovdqa64       %%IN05_OUT09, %%IN_ORIG00
        vmovdqa64       %%IN15_OUT10, %%IN_ORIG01
%endmacro

;;
;; Generates 64*16 bytes of keystream
;;
%macro GENERATE_1K_KS 32
%define %%ZMM_DWORD0       %1   ;; [out] ZMM containing dword 0 of all states and bytes 64-127  of keystream
%define %%ZMM_DWORD1       %2   ;; [out] ZMM containing dword 1 of all states and bytes 128-191 of keystream
%define %%ZMM_DWORD2       %3   ;; [out] ZMM containing dword 2 of all states and bytes 960-1023 of keystream
%define %%ZMM_DWORD3       %4   ;; [out] ZMM containing dword 3 of all states and bytes 256-319 of keystream
%define %%ZMM_DWORD4       %5   ;; [out] ZMM containing dword 4 of all states and bytes 512-575 of keystream
%define %%ZMM_DWORD5       %6   ;; [out] ZMM containing dword 5 of all states and bytes 576-639 of keystream
%define %%ZMM_DWORD6       %7   ;; [out] ZMM containing dword 6 of all states and bytes 832-895 of keystream
%define %%ZMM_DWORD7       %8   ;; [out] ZMM containing dword 7 of all states and bytes 448-511 of keystream
%define %%ZMM_DWORD8       %9   ;; [out] ZMM containing dword 8 of all states and bytes 320-383 of keystream
%define %%ZMM_DWORD9       %10  ;; [out] ZMM containing dword 9 of all states and bytes 0-63 of keystream
%define %%ZMM_DWORD10      %11  ;; [out] ZMM containing dword 10 of all states and bytes 384-447 of keystream
%define %%ZMM_DWORD11      %12  ;; [out] ZMM containing dword 11 of all states and bytes 704-767 of keystream
%define %%ZMM_DWORD12      %13  ;; [out] ZMM containing dword 12 of all states and bytes 768-831 of keystream
%define %%ZMM_DWORD13      %14  ;; [out] ZMM containing dword 13 of all states and bytes 192-255 of keystream
%define %%ZMM_DWORD14      %15  ;; [out] ZMM containing dword 14 of all states and bytes 896-959 of keystream
%define %%ZMM_DWORD15      %16  ;; [out] ZMM containing dword 15 of all states and bytes 640-703 of keystream
%define %%ZMM_DWORD_ORIG0  %17  ;; [in/clobbered] ZMM containing dword 0 of all states / Temp ZMM register
%define %%ZMM_DWORD_ORIG1  %18  ;; [in/clobbered] ZMM containing dword 1 of all states / Temp ZMM register
%define %%ZMM_DWORD_ORIG2  %19  ;; [in] ZMM containing dword 2 of all states
%define %%ZMM_DWORD_ORIG3  %20  ;; [in] ZMM containing dword 3 of all states
%define %%ZMM_DWORD_ORIG4  %21  ;; [in] ZMM containing dword 4 of all states
%define %%ZMM_DWORD_ORIG5  %22  ;; [in] ZMM containing dword 5 of all states
%define %%ZMM_DWORD_ORIG6  %23  ;; [in] ZMM containing dword 6 of all states
%define %%ZMM_DWORD_ORIG7  %24  ;; [in] ZMM containing dword 7 of all states
%define %%ZMM_DWORD_ORIG8  %25  ;; [in] ZMM containing dword 8 of all states
%define %%ZMM_DWORD_ORIG9  %26  ;; [in] ZMM containing dword 9 of all states
%define %%ZMM_DWORD_ORIG10 %27  ;; [in] ZMM containing dword 10 of all states
%define %%ZMM_DWORD_ORIG11 %28  ;; [in] ZMM containing dword 11 of all states
%define %%ZMM_DWORD_ORIG12 %29  ;; [in] ZMM containing dword 12 of all states
%define %%ZMM_DWORD_ORIG13 %30  ;; [in] ZMM containing dword 13 of all states
%define %%ZMM_DWORD_ORIG14 %31  ;; [in] ZMM containing dword 14 of all states
%define %%ZMM_DWORD_ORIG15 %32  ;; [in] ZMM containing dword 15 of all states

%assign i 0
%rep 16
        vmovdqa64 APPEND(%%ZMM_DWORD, i), APPEND(%%ZMM_DWORD_ORIG, i)
%assign i (i + 1)
%endrep

%rep 10

        ;;; Each full round consists of 8 quarter rounds, 4 column rounds and 4 diagonal rounds
        ;;; For first 4 column rounds:
        ;;; A = 0, 1, 2, 3;   B = 4, 5, 6, 7;
        ;;; C = 8, 9, 10, 11; D = 12, 13, 14, 15
        CHACHA20_ROUND %%ZMM_DWORD0, %%ZMM_DWORD1, %%ZMM_DWORD2, %%ZMM_DWORD3, \
                       %%ZMM_DWORD4, %%ZMM_DWORD5, %%ZMM_DWORD6, %%ZMM_DWORD7, \
                       %%ZMM_DWORD8, %%ZMM_DWORD9, %%ZMM_DWORD10, %%ZMM_DWORD11, \
                       %%ZMM_DWORD12, %%ZMM_DWORD13, %%ZMM_DWORD14, %%ZMM_DWORD15
        ;;; For 4 diagonal rounds:
        ;;; A = 0, 1, 2, 3;   B = 5, 6, 7, 4;
        ;;; C = 10, 11, 8, 9; D = 15, 12, 13, 14
        CHACHA20_ROUND %%ZMM_DWORD0, %%ZMM_DWORD1, %%ZMM_DWORD2, %%ZMM_DWORD3, \
                       %%ZMM_DWORD5, %%ZMM_DWORD6, %%ZMM_DWORD7, %%ZMM_DWORD4, \
                       %%ZMM_DWORD10, %%ZMM_DWORD11, %%ZMM_DWORD8, %%ZMM_DWORD9, \
                       %%ZMM_DWORD15, %%ZMM_DWORD12, %%ZMM_DWORD13, %%ZMM_DWORD14
%endrep

        ;; Add original states to processed states and transpose
        ;; these states to form the 64*16 bytes of keystream
        ADD_TRANSPOSE_STATE_KS %%ZMM_DWORD0, %%ZMM_DWORD1, %%ZMM_DWORD2, %%ZMM_DWORD3, \
                               %%ZMM_DWORD4, %%ZMM_DWORD5, %%ZMM_DWORD6, %%ZMM_DWORD7, \
                               %%ZMM_DWORD8, %%ZMM_DWORD9, %%ZMM_DWORD10, %%ZMM_DWORD11, \
                               %%ZMM_DWORD12, %%ZMM_DWORD13, %%ZMM_DWORD14, %%ZMM_DWORD15, \
                               %%ZMM_DWORD_ORIG0, %%ZMM_DWORD_ORIG1, %%ZMM_DWORD_ORIG2, \
                               %%ZMM_DWORD_ORIG3,%%ZMM_DWORD_ORIG4, %%ZMM_DWORD_ORIG5, \
                               %%ZMM_DWORD_ORIG6, %%ZMM_DWORD_ORIG7, %%ZMM_DWORD_ORIG8, \
                               %%ZMM_DWORD_ORIG9, %%ZMM_DWORD_ORIG10, %%ZMM_DWORD_ORIG11, \
                               %%ZMM_DWORD_ORIG12, %%ZMM_DWORD_ORIG13, %%ZMM_DWORD_ORIG14, \
                               %%ZMM_DWORD_ORIG15
%endmacro

%macro ENCRYPT_1_16_BLOCKS 23-24
%define %%KS0         %1 ; [in/clobbered] Bytes 0-63 of keystream
%define %%KS1         %2 ; [in/clobbered] Bytes 64-127 of keystream
%define %%KS2         %3 ; [in/clobbered] Bytes 128-191 of keystream
%define %%KS3         %4 ; [in/clobbered] Bytes 192-255 of keystream
%define %%KS4         %5 ; [in/clobbered] Bytes 256-319 of keystream
%define %%KS5         %6 ; [in/clobbered] Bytes 320-383 of keystream
%define %%KS6         %7 ; [in/clobbered] Bytes 384-447 of keystream
%define %%KS7         %8 ; [in/clobbered] Bytes 448-511 of keystream
%define %%KS8         %9 ; [in/clobbered] Bytes 512-575 of keystream
%define %%KS9        %10 ; [in/clobbered] Bytes 576-639 of keystream
%define %%KS10       %11 ; [in/clobbered] Bytes 640-703 of keystream
%define %%KS11       %12 ; [in/clobbered] Bytes 704-767 of keystream
%define %%KS12       %13 ; [in/clobbered] Bytes 768-831 of keystream
%define %%KS13       %14 ; [in/clobbered] Bytes 832-895 of keystream
%define %%KS14       %15 ; [in/clobbered] Bytes 896-959 of keystream
%define %%KS15       %16 ; [in/clobbered] Bytes 960-1023 of keystream
%define %%ZTMP       %17 ; [clobbered] Temporary ZMM register
%define %%SRC        %18 ; [in] Source pointer
%define %%DST        %19 ; [in] Destination pointer
%define %%OFF        %20 ; [in] Offset into src/dst pointers
%define %%KMASK      %21 ; [in] Mask register for final block
%define %%NUM_BLOCKS %22 ; [in] Number of blocks to encrypt
%define %%GEN_KEY    %23 ; [in] Generate poly key
%define %%LAST_KS    %24 ; [in] Pointer to last KS pointer

%ifidn %%GEN_KEY, gen_poly_key
%if %%NUM_BLOCKS != 16
        ; Check if Poly key has been generated
        or      added_len, added_len
        jz      %%encrypt_key_calculated

        ; Generate Poly key
        GEN_POLY_KEY    arg2, %%KS0
        ; XOR Keystreams with blocks of input data
%assign %%I 0
%assign %%J 1
%rep (%%NUM_BLOCKS - 1)
        vpxorq    APPEND(%%KS, %%J), [%%SRC + %%OFF + 64*%%I]
%assign %%I (%%I + 1)
%assign %%J (%%J + 1)
%endrep
        ; Final block which might have less than 64 bytes, so mask register is used
        vmovdqu8 %%ZTMP{%%KMASK}, [%%SRC + %%OFF + 64*%%I]
        vpxorq  APPEND(%%KS, %%J), %%ZTMP

        ; Write out blocks of ciphertext
%assign %%I 0
%assign %%J 1
%rep (%%NUM_BLOCKS - 1)
        vmovdqu8 [%%DST + %%OFF + 64*%%I], APPEND(%%KS, %%J)
%assign %%I (%%I + 1)
%assign %%J (%%J + 1)
%endrep
        vmovdqu8 [%%DST + %%OFF + 64*%%I]{%%KMASK}, APPEND(%%KS, %%J)

        jmp     %%encrypt_done
%%encrypt_key_calculated:
%endif ; %%GEN_KEY == gen_poly_key
%endif ; %%NUM_BLOCKS != 16

%assign %%I 0
%rep (%%NUM_BLOCKS - 1)
        vpxorq  APPEND(%%KS, %%I), [%%SRC + %%OFF + 64*%%I]
%assign %%I (%%I + 1)
%endrep
        ; Final block which might have less than 64 bytes, so mask register is used
        vmovdqu8 %%ZTMP{%%KMASK}, [%%SRC + %%OFF + 64*%%I]
%if %0 == 24
        vmovdqu64 [%%LAST_KS], APPEND(%%KS, %%I)
%endif
        vpxorq  APPEND(%%KS, %%I), %%ZTMP

        ; Write out blocks of ciphertext
%assign %%I 0
%rep (%%NUM_BLOCKS - 1)
        vmovdqu8 [%%DST + %%OFF + 64*%%I], APPEND(%%KS, %%I)
%assign %%I (%%I + 1)
%endrep
        vmovdqu8 [%%DST + %%OFF + 64*%%I]{%%KMASK}, APPEND(%%KS, %%I)
%%encrypt_done:

%endmacro

%macro PREPARE_NEXT_STATES_4_TO_8 13
%define %%STATE_IN_A_L   %1  ;; [out] ZMM containing state "A" part for states 1-4
%define %%STATE_IN_B_L   %2  ;; [out] ZMM containing state "B" part for states 1-4
%define %%STATE_IN_C_L   %3  ;; [out] ZMM containing state "C" part for states 1-4
%define %%STATE_IN_D_L   %4  ;; [out] ZMM containing state "D" part for states 1-4
%define %%STATE_IN_D_H   %5  ;; [out] ZMM containing state "D" part for states 5-8 (or "none" in NUM_BLOCKS == 4)
%define %%ZTMP0          %6  ;; [clobbered] ZMM temp reg
%define %%ZTMP1          %7  ;; [clobbered] ZMM temp reg
%define %%LAST_BLK_CNT   %8  ;; [in] Last block counter
%define %%IV             %9  ;; [in] Pointer to IV
%define %%KEYS           %10 ;; [in/clobbered] Pointer to keys
%define %%KMASK          %11 ;; [clobbered] Mask register
%define %%NUM_BLOCKS     %12 ;; [in] Number of state blocks to prepare (numerical)
%define %%GEN_KEY        %13 ;; [in] Generate poly key

        ;; Prepare next 8 states (or 4, if 4 or less blocks left)
        vbroadcastf64x2  %%STATE_IN_B_L, [%%KEYS]            ; Load key bytes 0-15
        vbroadcastf64x2  %%STATE_IN_C_L, [%%KEYS + 16]       ; Load key bytes 16-31
        mov       %%KEYS, 0xfff ; Reuse %%KEYS register, as it is not going to be used again
        kmovq     %%KMASK, %%KEYS
        vmovdqu8  XWORD(%%STATE_IN_D_L){%%KMASK}, [%%IV] ; Load Nonce (12 bytes)
        vpslldq   XWORD(%%STATE_IN_D_L), 4
        vshufi64x2 %%STATE_IN_D_L, %%STATE_IN_D_L, 0 ; Broadcast 128 bits to 512 bits
        vbroadcastf64x2 %%STATE_IN_A_L, [rel constants]

%if %%NUM_BLOCKS == 8
        ;; Prepare chacha states 4-7 (A-C same as states 0-3)
        vmovdqa64 %%STATE_IN_D_H, %%STATE_IN_D_L
%endif

        ; Broadcast last block counter
        vmovq   XWORD(%%ZTMP0), %%LAST_BLK_CNT
        vshufi32x4 %%ZTMP0, %%ZTMP0, 0x00

%ifidn %%GEN_KEY, gen_poly_key
%if %%NUM_BLOCKS == 4
        ; Add 0-3 to construct next block counters
        vpaddd  %%ZTMP0, [rel add_0_3]
        vporq   %%STATE_IN_D_L, %%ZTMP0
%else
        ; Add 0-7 to construct next block counters
        vpaddd  %%ZTMP1, %%ZTMP0, [rel add_4_7]
        vpaddd  %%ZTMP0, [rel add_0_3]
        vporq   %%STATE_IN_D_L, %%ZTMP0
        vporq   %%STATE_IN_D_H, %%ZTMP1
%endif

%else ; %%GEN == gen_poly_key
%if %%NUM_BLOCKS == 4
        ; Add 1-4 to construct next block counters
        vpaddd  %%ZTMP0, [rel add_1_4]
        vporq   %%STATE_IN_D_L, %%ZTMP0
%else
        ; Add 1-8 to construct next block counters
        vpaddd  %%ZTMP1, %%ZTMP0, [rel add_5_8]
        vpaddd  %%ZTMP0, [rel add_1_4]
        vporq   %%STATE_IN_D_L, %%ZTMP0
        vporq   %%STATE_IN_D_H, %%ZTMP1
%endif
%endif ; %%GEN == gen_poly_key
%endmacro

align 32
MKGLOBAL(submit_job_chacha20_enc_dec_avx512,function,internal)
submit_job_chacha20_enc_dec_avx512:
        endbranch64
%define src     r8
%define dst     r9
%define len     r10
%define iv      r11
%define keys    rdx
%define tmp     rdx
%define off     rax

        xor     off, off

        mov     tmp, 0xffffffffffffffff
        kmovq   k1, tmp

        mov     len, [job + _msg_len_to_cipher_in_bytes]
        mov     src, [job + _src]
        add     src, [job + _cipher_start_src_offset_in_bytes]
        mov     dst, [job + _dst]
        mov     keys, [job + _enc_keys]
        mov     iv, [job + _iv]

        ; If less than or equal to 64*8 bytes, prepare directly states for up to 8 blocks
        cmp     len, 64*8
        jbe     exit_loop

        ; Prepare first 16 chacha20 states from IV, key, constants and counter values
        vpbroadcastd zmm0, [rel constants]
        vpbroadcastd zmm1, [rel constants + 4]
        vpbroadcastd zmm2, [rel constants + 8]
        vpbroadcastd zmm3, [rel constants + 12]

        vpbroadcastd zmm4, [keys]
        vpbroadcastd zmm5, [keys + 4]
        vpbroadcastd zmm6, [keys + 8]
        vpbroadcastd zmm7, [keys + 12]
        vpbroadcastd zmm8, [keys + 16]
        vpbroadcastd zmm9, [keys + 20]
        vpbroadcastd zmm10, [keys + 24]
        vpbroadcastd zmm11, [keys + 28]

        vpbroadcastd zmm13, [iv]
        vpbroadcastd zmm14, [iv + 4]
        vpbroadcastd zmm15, [iv + 8]
        ;; Set first 16 counter values
        vmovdqa64 zmm12, [rel set_1_16]

        cmp     len, 64*16
        jb      exit_loop

align 32
start_loop:
        ENCRYPT_1K zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                   zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                   zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, \
                   zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15, src, dst, off, 0

        ; Update remaining length
        sub     len, 64*16
        add     off, 64*16

        ; Reload first two registers zmm0 and 1,
        ; as they have been overwritten by the previous macros
        vpbroadcastd zmm0, [rel constants]
        vpbroadcastd zmm1, [rel constants + 4]

        ; Increment counter values
        vpaddd      zmm12, [rel add_16]

        cmp     len, 64*16
        jae     start_loop

exit_loop:

        ; Check if there are partial block (less than 16*64 bytes)
        or      len, len
        jz      no_partial_block

        cmp     len, 64*8
        ja      more_than_8_blocks_left

        cmp     len, 64*4
        ja      more_than_4_blocks_left

        ;; up to 4 blocks left

        ; Get last block counter dividing offset by 64
        shr     off, 6
        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, off, iv, keys, k2, 4, no_key
        shl     off, 6 ; Restore offset

        ; Use same first 4 registers as the output of GENERATE_1K_KS,
        ; to be able to use common code later on to encrypt
        GENERATE_512_KS zmm25, zmm16, zmm17, zmm29, none, none, none, none, \
                        zmm0, zmm1, zmm2, zmm3, none, \
                        zmm8, zmm9, zmm10, zmm11, 4

        jmp ks_gen_done

more_than_4_blocks_left:
        ;; up to 8 blocks left

        ; Get last block counter dividing offset by 64
        shr     off, 6
        ;; up to 8 blocks left
        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, off, iv, keys, k2, 8, no_key
        shl     off, 6 ; Restore offset

        ; Use same first 8 registers as the output of GENERATE_1K_KS,
        ; to be able to use common code later on to encrypt
        GENERATE_512_KS zmm25, zmm16, zmm17, zmm29, zmm19, zmm24, zmm26, zmm23, \
                        zmm0, zmm1, zmm2, zmm3, zmm7, \
                        zmm8, zmm9, zmm10, zmm11, 8

        jmp ks_gen_done
more_than_8_blocks_left:
        ; Generate another 64*16 bytes of keystream and XOR only the leftover plaintext
        GENERATE_1K_KS zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                       zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                       zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, \
                       zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15

ks_gen_done:

        ; Calculate number of final blocks
        mov     tmp, len
        add     tmp, 63
        shr     tmp, 6

        cmp     tmp, 8
        je      final_num_blocks_is_8
        jb      final_num_blocks_is_1_7

        ; Final blocks 9-16
        cmp     tmp, 12
        je      final_num_blocks_is_12
        jb      final_num_blocks_is_9_11

        ; Final blocks 13-16
        cmp     tmp, 14
        je      final_num_blocks_is_14
        jb      final_num_blocks_is_13

        cmp     tmp, 15
        je      final_num_blocks_is_15
        jmp     final_num_blocks_is_16

final_num_blocks_is_9_11:
        cmp     tmp, 10
        je      final_num_blocks_is_10
        jb      final_num_blocks_is_9
        ja      final_num_blocks_is_11

final_num_blocks_is_1_7:
        ; Final blocks 1-7
        cmp     tmp, 4
        je      final_num_blocks_is_4
        jb      final_num_blocks_is_1_3

        ; Final blocks 5-7
        cmp     tmp, 6
        je      final_num_blocks_is_6
        jb      final_num_blocks_is_5
        ja      final_num_blocks_is_7

final_num_blocks_is_1_3:
        cmp     tmp, 2
        je      final_num_blocks_is_2
        ja      final_num_blocks_is_3

        ; 1 final block if no jump
%assign I 1
%rep 16
APPEND(final_num_blocks_is_, I):

        lea     tmp, [rel len_to_mask]
        and     len, 63
        kmovq   k1, [tmp + len*8]

        ENCRYPT_1_16_BLOCKS zmm25, zmm16, zmm17, zmm29, zmm19, zmm24, zmm26, zmm23, \
                            zmm20, zmm21, zmm31, zmm27, zmm28, zmm22, zmm30, zmm18, \
                            zmm0, src, dst, off, k1, I, 0
        jmp     no_partial_block

%assign I (I + 1)
%endrep

no_partial_block:

%ifdef SAFE_DATA
        clear_all_zmms_asm
%endif
        mov     rax, job
        or      dword [rax + _status], IMB_STATUS_COMPLETED_CIPHER

        ret

align 32
MKGLOBAL(submit_job_chacha20_poly_enc_avx512,function,internal)
submit_job_chacha20_poly_enc_avx512:
        endbranch64
%define src     r8
%define dst     r9
%define len     r10
%define iv      r11
%define keys    r13
%define tmp     r13
%define off     rax

        sub     rsp, 16
        mov     [rsp], r12
        mov     [rsp + 8], r13

        mov     added_len, 64
        xor     off, off

        mov     tmp, 0xffffffffffffffff
        kmovq   k1, tmp

        mov     len, [job + _msg_len_to_cipher_in_bytes]
        add     len, 64 ; 64 bytes more to generate Poly key
        mov     src, [job + _src]
        add     src, [job + _cipher_start_src_offset_in_bytes]
        mov     dst, [job + _dst]
        mov     keys, [job + _enc_keys]
        mov     iv, [job + _iv]

        ; If less than or equal to 64*8 bytes, prepare directly states for up to 8 blocks
        cmp     len, 64*8
        jbe     exit_loop_poly

        ; Prepare first 16 chacha20 states from IV, key, constants and counter values
        vpbroadcastd zmm0, [rel constants]
        vpbroadcastd zmm1, [rel constants + 4]
        vpbroadcastd zmm2, [rel constants + 8]
        vpbroadcastd zmm3, [rel constants + 12]

        vpbroadcastd zmm4, [keys]
        vpbroadcastd zmm5, [keys + 4]
        vpbroadcastd zmm6, [keys + 8]
        vpbroadcastd zmm7, [keys + 12]
        vpbroadcastd zmm8, [keys + 16]
        vpbroadcastd zmm9, [keys + 20]
        vpbroadcastd zmm10, [keys + 24]
        vpbroadcastd zmm11, [keys + 28]

        vpbroadcastd zmm13, [iv]
        vpbroadcastd zmm14, [iv + 4]
        vpbroadcastd zmm15, [iv + 8]
        ;; Set first 16 counter values (including 0 for poly key)
        vmovdqa64 zmm12, [rel set_0_15]

        cmp     len, 64*16
        jb      exit_loop_poly

        ; Generate Poly key and encrypt 15*16 bytes
        ENCRYPT_1K zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                   zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                   zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, \
                   zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15, src, dst, off, gen_poly_key

        ; Clear added_len, indicating that Poly key has been generated
        xor     added_len, added_len

        ; Update remaining length
        sub     len, 64*16
        add     off, 64*15

        ; Reload first two registers zmm0 and 1,
        ; as they have been overwritten by the previous macros
        vpbroadcastd zmm0, [rel constants]
        vpbroadcastd zmm1, [rel constants + 4]

        ; Increment counter values
        vpaddd  zmm12, [rel add_16]

        cmp     len, 64*16
        jb      exit_loop_poly

align 32
start_loop_poly:
        ENCRYPT_1K zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                   zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                   zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, \
                   zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15, src, dst, \
                   off, no_gen_poly_key

        ; Update remaining length
        sub     len, 64*16
        add     off, 64*16

        ; Reload first two registers zmm0 and 1,
        ; as they have been overwritten by the previous macros
        vpbroadcastd zmm0, [rel constants]
        vpbroadcastd zmm1, [rel constants + 4]

        ; Increment counter values
        vpaddd      zmm12, [rel add_16]

        cmp     len, 64*16
        jae     start_loop_poly

exit_loop_poly:

        ; Check if there are partial block (less than 16*64 bytes)
        or      len, len
        jz      no_partial_block_poly

        cmp     len, 64*8
        ja      more_than_8_blocks_left_poly

        cmp     len, 64*4
        ja      more_than_4_blocks_left_poly

        ;; up to 4 blocks left

        ; Get last block counter dividing offset by 64
        shr     off, 6
        ; Check if Poly key has been generated
        or      added_len, added_len
        jz      prepare_four_states

        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, off, iv, keys, k2, 4, gen_poly_key
        jmp     four_states_prepared

prepare_four_states:

        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, off, iv, keys, k2, 4, no_key

four_states_prepared:
        shl     off, 6 ; Restore offset

        ; Use same first 4 registers as the output of GENERATE_1K_KS,
        ; to be able to use common code later on to encrypt
        GENERATE_512_KS zmm25, zmm16, zmm17, zmm29, none, none, none, none, \
                        zmm0, zmm1, zmm2, zmm3, none, \
                        zmm8, zmm9, zmm10, zmm11, 4

        jmp ks_gen_done_poly

more_than_4_blocks_left_poly:
        ;; up to 8 blocks left

        ; Get last block counter dividing offset by 64
        shr     off, 6
        ;; up to 8 blocks left

        ; Check if Poly key has been generated
        or      added_len, added_len
        jz      prepare_eight_states

        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, off, iv, keys, k2, 8, gen_poly_key
        jmp     eight_states_prepared

prepare_eight_states:

        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, off, iv, keys, k2, 8, no_key
eight_states_prepared:
        shl     off, 6 ; Restore offset

        ; Use same first 8 registers as the output of GENERATE_1K_KS,
        ; to be able to use common code later on to encrypt
        GENERATE_512_KS zmm25, zmm16, zmm17, zmm29, zmm19, zmm24, zmm26, zmm23, \
                        zmm0, zmm1, zmm2, zmm3, zmm7, \
                        zmm8, zmm9, zmm10, zmm11, 8

        jmp ks_gen_done_poly
more_than_8_blocks_left_poly:
        ; Generate another 64*16 bytes of keystream and XOR only the leftover plaintext
        GENERATE_1K_KS zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                       zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                       zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, \
                       zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15

ks_gen_done_poly:

        ; Reduce number of bytes used for the Poly key
        sub     len, added_len

        ; Calculate number of final blocks
        mov     tmp, len
        add     tmp, 63
        shr     tmp, 6
        jz      final_num_blocks_is_0_poly

        cmp     tmp, 8
        je      final_num_blocks_is_8_poly
        jb      final_num_blocks_is_1_7_poly

        ; Final blocks 9-16
        cmp     tmp, 12
        je      final_num_blocks_is_12_poly
        jb      final_num_blocks_is_9_11_poly

        ; Final blocks 13-16
        cmp     tmp, 14
        je      final_num_blocks_is_14_poly
        jb      final_num_blocks_is_13_poly

        cmp     tmp, 15
        je      final_num_blocks_is_15_poly
        jmp     final_num_blocks_is_16_poly

final_num_blocks_is_9_11_poly:
        cmp     tmp, 10
        je      final_num_blocks_is_10_poly
        jb      final_num_blocks_is_9_poly
        ja      final_num_blocks_is_11_poly

final_num_blocks_is_1_7_poly:
        ; Final blocks 1-7
        cmp     tmp, 4
        je      final_num_blocks_is_4_poly
        jb      final_num_blocks_is_1_3_poly

        ; Final blocks 5-7
        cmp     tmp, 6
        je      final_num_blocks_is_6_poly
        jb      final_num_blocks_is_5_poly
        ja      final_num_blocks_is_7_poly

final_num_blocks_is_1_3_poly:
        cmp     tmp, 2
        je      final_num_blocks_is_2_poly
        ja      final_num_blocks_is_3_poly

        ; 1 final block if no jump
%assign I 1
%rep 16
APPEND3(final_num_blocks_is_, I, _poly):

        lea     tmp, [rel len_to_mask]
        and     len, 63
        kmovq   k1, [tmp + len*8]

        ENCRYPT_1_16_BLOCKS zmm25, zmm16, zmm17, zmm29, zmm19, zmm24, zmm26, zmm23, \
                            zmm20, zmm21, zmm31, zmm27, zmm28, zmm22, zmm30, zmm18, \
                            zmm0, src, dst, off, k1, I, gen_poly_key
        jmp     no_partial_block_poly

%assign I (I + 1)
%endrep

final_num_blocks_is_0_poly:
        ; Generate Poly key
        GEN_POLY_KEY    arg2, zmm25

no_partial_block_poly:

%ifdef SAFE_DATA
        clear_all_zmms_asm
%endif
        mov     rax, job
        or      dword [rax + _status], IMB_STATUS_COMPLETED_CIPHER

        mov     r12, [rsp]
        mov     r13, [rsp + 8]
        add     rsp, 16

        ret

align 32
MKGLOBAL(gen_keystr_poly_key_avx512,function,internal)
gen_keystr_poly_key_avx512:
        endbranch64
%define keys    arg1
%define iv      arg2
%define len     arg3
%define ks      arg4

%define off     rax

        ; Generate up to 1KB of keystream

        ; If less than or equal to 64*8 bytes, prepare directly states for up to 8 blocks
        cmp     len, 64*8
        jbe     less_than_512_ks

        ; Prepare first 16 chacha20 states from IV, key, constants and counter values
        vpbroadcastd zmm0, [rel constants]
        vpbroadcastd zmm1, [rel constants + 4]
        vpbroadcastd zmm2, [rel constants + 8]
        vpbroadcastd zmm3, [rel constants + 12]

        vpbroadcastd zmm4, [keys]
        vpbroadcastd zmm5, [keys + 4]
        vpbroadcastd zmm6, [keys + 8]
        vpbroadcastd zmm7, [keys + 12]
        vpbroadcastd zmm8, [keys + 16]
        vpbroadcastd zmm9, [keys + 20]
        vpbroadcastd zmm10, [keys + 24]
        vpbroadcastd zmm11, [keys + 28]

        vpbroadcastd zmm13, [iv]
        vpbroadcastd zmm14, [iv + 4]
        vpbroadcastd zmm15, [iv + 8]
        ;; Set first 16 counter values (including 0 for poly key)
        vmovdqa64 zmm12, [rel set_0_15]

        GENERATE_1K_KS zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                       zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                       zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, \
                       zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15

        ; Clamp first 16 bytes of keystream (in zmm25), for poly key
        vpandq  ymm25, [rel poly_clamp_r]

        ; Write out 1KB of KS
        vmovdqa64 [ks],        zmm25
        vmovdqa64 [ks + 64],   zmm16
        vmovdqa64 [ks + 64*2], zmm17
        vmovdqa64 [ks + 64*3], zmm29
        vmovdqa64 [ks + 64*4], zmm19
        vmovdqa64 [ks + 64*5], zmm24
        vmovdqa64 [ks + 64*6], zmm26
        vmovdqa64 [ks + 64*7], zmm23
        vmovdqa64 [ks + 64*8], zmm20
        vmovdqa64 [ks + 64*9], zmm21
        vmovdqa64 [ks + 64*10], zmm31
        vmovdqa64 [ks + 64*11], zmm27
        vmovdqa64 [ks + 64*12], zmm28
        vmovdqa64 [ks + 64*13], zmm22
        vmovdqa64 [ks + 64*14], zmm30
        vmovdqa64 [ks + 64*15], zmm18

        ret

less_than_512_ks:

        cmp     len, 64*4
        ja      more_than_256_ks

        xor     off, off
        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, off, iv, keys, k1, 4, gen_poly_key

        GENERATE_512_KS zmm25, zmm16, zmm17, zmm29, none, none, none, none, \
                        zmm0, zmm1, zmm2, zmm3, none, \
                        zmm8, zmm9, zmm10, zmm11, 4

        ; Clamp first 16 bytes of keystream (in zmm25), for poly key
        vpandq  ymm25, [rel poly_clamp_r]

        ; Write out 256B of KS
        vmovdqa64 [ks],        zmm25
        vmovdqa64 [ks + 64],   zmm16
        vmovdqa64 [ks + 64*2], zmm17
        vmovdqa64 [ks + 64*3], zmm29

        ret

more_than_256_ks:
        xor     off, off
        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, off, iv, keys, k1, 8, gen_poly_key

        GENERATE_512_KS zmm25, zmm16, zmm17, zmm29, zmm19, zmm24, zmm26, zmm23, \
                        zmm0, zmm1, zmm2, zmm3, zmm7, \
                        zmm8, zmm9, zmm10, zmm11, 8

        ; Clamp first 16 bytes of keystream (in zmm25), for poly key
        vpandq  ymm25, [rel poly_clamp_r]

        ; Write out 512B of KS
        vmovdqa64 [ks],        zmm25
        vmovdqa64 [ks + 64],   zmm16
        vmovdqa64 [ks + 64*2], zmm17
        vmovdqa64 [ks + 64*3], zmm29
        vmovdqa64 [ks + 64*4], zmm19
        vmovdqa64 [ks + 64*5], zmm24
        vmovdqa64 [ks + 64*6], zmm26
        vmovdqa64 [ks + 64*7], zmm23

        ret

align 32
MKGLOBAL(submit_job_chacha20_poly_dec_avx512,function,internal)
submit_job_chacha20_poly_dec_avx512:
        endbranch64
%define src     r8
%define dst     r9
%define len     r10
%define iv      r11
%ifdef LINUX
%define keys    rdx
%else
%define keys    rsi
%endif
%define tmp     keys
%define off     rax

%define ks      arg2
%define len_xor iv

%ifndef LINUX
        push    rsi
%endif
        mov     len_xor, arg3

        ; Check if there is ciphertext to decrypt
        or      len_xor, len_xor
        jz      no_partial_block_dec

        ; XOR ciphertext with existing keystream, generated previously
        mov     src, [job + _src]
        add     src, [job + _cipher_start_src_offset_in_bytes]
        mov     dst, [job + _dst]

        xor     off, off

        ; Calculate number of initial blocks
        mov     tmp, len_xor
        add     tmp, 63
        shr     tmp, 6

        cmp     tmp, 8
        je      initial_dec_num_blocks_is_8
        jb      initial_dec_num_blocks_is_1_7

        ; Initial blocks 9-15
        cmp     tmp, 12
        je      initial_dec_num_blocks_is_12
        jb      initial_dec_num_blocks_is_9_11

        ; Initial blocks 13-15
        cmp     tmp, 14
        je      initial_dec_num_blocks_is_14
        jb      initial_dec_num_blocks_is_13

        cmp     tmp, 15
        je      initial_dec_num_blocks_is_15

initial_dec_num_blocks_is_9_11:
        cmp     tmp, 10
        je      initial_dec_num_blocks_is_10
        jb      initial_dec_num_blocks_is_9
        ja      initial_dec_num_blocks_is_11

initial_dec_num_blocks_is_1_7:
        ; Initial blocks 1-7
        cmp     tmp, 4
        je      initial_dec_num_blocks_is_4
        jb      initial_dec_num_blocks_is_1_3

        ; Initial blocks 5-7
        cmp     tmp, 6
        je      initial_dec_num_blocks_is_6
        jb      initial_dec_num_blocks_is_5
        ja      initial_dec_num_blocks_is_7

initial_dec_num_blocks_is_1_3:
        cmp     tmp, 2
        je      initial_dec_num_blocks_is_2
        ja      initial_dec_num_blocks_is_3

        ; 1 Initial block if no jump
%assign I 1
%rep 15
APPEND(initial_dec_num_blocks_is_, I):

        lea     tmp, [rel len_to_mask]
        mov     len, len_xor
        and     len, 63
        kmovq   k1, [tmp + len*8]

        ; Read Keystream from memory
%assign J 0
%rep I
        vmovdqa64 APPEND(zmm, J), [ks + J*64]
%assign J (J+1)
%endrep
        ENCRYPT_1_16_BLOCKS zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, \
                            zmm8, zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15, \
                            zmm16, src, dst, off, k1, I, 0
        add     off, 64*I
        ; Check if less than 15*64 bytes have been decrypted (meaning there are no more to decrypt)
        cmp     len_xor, 15*64
        jb      no_partial_block_dec
        ; If there were 15*64 bytes, check if there are more bytes to decrypt
        jmp     resume_dec

%assign I (I + 1)
%endrep

resume_dec:
        ; Get remaining length to decrypt
        mov     len, [job + _msg_len_to_cipher_in_bytes]
        sub     len, off

        mov     keys, [job + _enc_keys]
        mov     iv, [job + _iv]

        ; If less than or equal to 64*8 bytes, prepare directly states for up to 8 blocks
        cmp     len, 64*8
        jbe     exit_loop_dec

        ; Prepare first 16 chacha20 states from IV, key, constants and counter values
        vpbroadcastd zmm0, [rel constants]
        vpbroadcastd zmm1, [rel constants + 4]
        vpbroadcastd zmm2, [rel constants + 8]
        vpbroadcastd zmm3, [rel constants + 12]

        vpbroadcastd zmm4, [keys]
        vpbroadcastd zmm5, [keys + 4]
        vpbroadcastd zmm6, [keys + 8]
        vpbroadcastd zmm7, [keys + 12]
        vpbroadcastd zmm8, [keys + 16]
        vpbroadcastd zmm9, [keys + 20]
        vpbroadcastd zmm10, [keys + 24]
        vpbroadcastd zmm11, [keys + 28]

        vpbroadcastd zmm13, [iv]
        vpbroadcastd zmm14, [iv + 4]
        vpbroadcastd zmm15, [iv + 8]
        ; Get last block counter dividing offset by 64
        shr     off, 6
        ;; Set next 16 counter values
        vpbroadcastd zmm12, DWORD(off)
        shl     off, 6
        vpaddd  zmm12, [rel set_1_16]

        cmp     len, 64*16
        jb      exit_loop_dec

align 32
start_loop_dec:
        ENCRYPT_1K zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                   zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                   zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, \
                   zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15, src, dst, off, 0

        ; Update remaining length
        sub     len, 64*16
        add     off, 64*16

        ; Reload first two registers zmm0 and 1,
        ; as they have been overwritten by the previous macros
        vpbroadcastd zmm0, [rel constants]
        vpbroadcastd zmm1, [rel constants + 4]

        ; Increment counter values
        vpaddd  zmm12, [rel add_16]

        cmp     len, 64*16
        jae     start_loop_dec

exit_loop_dec:

        ; Check if there are partial block (less than 16*64 bytes)
        or      len, len
        jz      no_partial_block_dec

        cmp     len, 64*8
        ja      more_than_8_blocks_left_dec

        cmp     len, 64*4
        ja      more_than_4_blocks_left_dec

        ;; up to 4 blocks left

        ; Get last block counter dividing offset by 64
        shr     off, 6
        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, off, iv, keys, k2, 4, 0
        shl     off, 6 ; Restore offset

        ; Use same first 4 registers as the output of GENERATE_1K_KS,
        ; to be able to use common code later on to encrypt
        GENERATE_512_KS zmm25, zmm16, zmm17, zmm29, none, none, none, none, \
                        zmm0, zmm1, zmm2, zmm3, none, \
                        zmm8, zmm9, zmm10, zmm11, 4

        jmp ks_gen_done_dec

more_than_4_blocks_left_dec:
        ;; up to 8 blocks left

        ; Get last block counter dividing offset by 64
        shr     off, 6
        ;; up to 8 blocks left
        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, off, iv, keys, k2, 8, 0
        shl     off, 6 ; Restore offset

        ; Use same first 8 registers as the output of GENERATE_1K_KS,
        ; to be able to use common code later on to encrypt
        GENERATE_512_KS zmm25, zmm16, zmm17, zmm29, zmm19, zmm24, zmm26, zmm23, \
                        zmm0, zmm1, zmm2, zmm3, zmm7, \
                        zmm8, zmm9, zmm10, zmm11, 8

        jmp ks_gen_done_dec
more_than_8_blocks_left_dec:
        ; Generate another 64*16 bytes of keystream and XOR only the leftover plaintext
        GENERATE_1K_KS zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                       zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                       zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, \
                       zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15

ks_gen_done_dec:

        ; Calculate number of final blocks
        mov     tmp, len
        add     tmp, 63
        shr     tmp, 6

        cmp     tmp, 8
        je      final_dec_num_blocks_is_8
        jb      final_dec_num_blocks_is_1_7

        ; Final blocks 9-16
        cmp     tmp, 12
        je      final_dec_num_blocks_is_12
        jb      final_dec_num_blocks_is_9_11

        ; Final blocks 13-16
        cmp     tmp, 14
        je      final_dec_num_blocks_is_14
        jb      final_dec_num_blocks_is_13

        cmp     tmp, 15
        je      final_dec_num_blocks_is_15
        jmp     final_dec_num_blocks_is_16

final_dec_num_blocks_is_9_11:
        cmp     tmp, 10
        je      final_dec_num_blocks_is_10
        jb      final_dec_num_blocks_is_9
        ja      final_dec_num_blocks_is_11

final_dec_num_blocks_is_1_7:
        ; Final blocks 1-7
        cmp     tmp, 4
        je      final_dec_num_blocks_is_4
        jb      final_dec_num_blocks_is_1_3

        ; Final blocks 5-7
        cmp     tmp, 6
        je      final_dec_num_blocks_is_6
        jb      final_dec_num_blocks_is_5
        ja      final_dec_num_blocks_is_7

final_dec_num_blocks_is_1_3:
        cmp     tmp, 2
        je      final_dec_num_blocks_is_2
        ja      final_dec_num_blocks_is_3

        ; 1 final block if no jump
%assign I 1
%rep 16
APPEND(final_dec_num_blocks_is_, I):

        lea     tmp, [rel len_to_mask]
        and     len, 63
        kmovq   k1, [tmp + len*8]

        ENCRYPT_1_16_BLOCKS zmm25, zmm16, zmm17, zmm29, zmm19, zmm24, zmm26, zmm23, \
                            zmm20, zmm21, zmm31, zmm27, zmm28, zmm22, zmm30, zmm18, \
                            zmm0, src, dst, off, k1, I, 0
        jmp     no_partial_block_dec

%assign I (I + 1)
%endrep

no_partial_block_dec:

%ifdef SAFE_DATA
        clear_all_zmms_asm
        ; Clear stored keystreams in stack
%assign i 0
%rep 15
        vmovdqa64 [ks + 64*i], zmm0
%assign i (i + 1)
%endrep
%endif
        mov     rax, job
        or      dword [rax + _status], IMB_STATUS_COMPLETED_CIPHER

%ifndef LINUX
        pop     rsi
%endif
        ret

align 32
MKGLOBAL(chacha20_enc_dec_ks_avx512,function,internal)
chacha20_enc_dec_ks_avx512:
        endbranch64
%define blk_cnt r10

%define prev_ks r13
%define remain_ks r12
%define ctx     r11

%define src     arg1
%define dst     arg2
%define len     arg3
%define keys    arg4

%define iv      r15
%define off     rax
%define tmp     iv
%define tmp3    r14
%define tmp4    rbp
%define tmp5    rbx
%ifdef LINUX
%define tmp2 r9
%else
%define tmp2 rdi
%endif

        ; Check if there is nothing to encrypt
        or      len, len
        jz      exit_ks

        mov     ctx, arg5

        sub     rsp, 8*7
        mov     [rsp], r12
        mov     [rsp + 8], r13
        mov     [rsp + 16], r14
        mov     [rsp + 24], r15
        mov     [rsp + 32], rbx
        mov     [rsp + 40], rbp
%ifndef LINUX
        mov     [rsp + 48], rdi
%endif

        xor     off, off
        mov     blk_cnt, [ctx + LastBlkCount]
        lea     prev_ks, [ctx + LastKs]
        mov     remain_ks, [ctx + RemainKsBytes]

        ; Check if there are any remaining bytes of keystream
        mov     tmp3, remain_ks
        or      tmp3, tmp3
        jz      no_remain_ks_bytes

        mov     tmp4, 64
        sub     tmp4, tmp3

        ; Adjust pointer of previous KS to point at start of unused KS
        add     prev_ks, tmp4

        ; Set remaining bytes to length of input segment, if lower
        cmp     len, tmp3
        cmovbe  tmp3, len

        mov     tmp5, tmp3
        lea     tmp, [rel len_to_mask]
        and     tmp3, 63
        kmovq   k1, [tmp + tmp3*8]

        ; Read up to 63 bytes of KS and XOR the first bytes of message
        ; with the previous unused bytes of keystream
        vmovdqu8 zmm0{k1}, [src]
        vmovdqu8 zmm1{k1}, [prev_ks]
        vpxorq  zmm1, zmm0
        vmovdqu8 [dst]{k1}, zmm1
        add     src, tmp3
        add     dst, tmp3

        ; Update remain bytes of KS
        sub     [ctx + RemainKsBytes], tmp5
        ; Restore pointer to previous KS
        sub     prev_ks, tmp4

        sub     len, tmp5
        jz      no_partial_block_ks

no_remain_ks_bytes:

        ; Reset remaining number of KS bytes
        mov     qword [ctx + RemainKsBytes], 0
        lea     iv, [ctx + IV]

        mov     tmp5, 0xffffffffffffffff
        kmovq   k1, tmp5

        ; If less than or equal to 64*8 bytes, prepare directly states for up to 8 blocks
        cmp     len, 64*8
        jbe     exit_loop_ks

        ; Prepare first 16 chacha20 states from IV, key, constants and counter values
        vpbroadcastd zmm0, [rel constants]
        vpbroadcastd zmm1, [rel constants + 4]
        vpbroadcastd zmm2, [rel constants + 8]
        vpbroadcastd zmm3, [rel constants + 12]

        vpbroadcastd zmm4, [keys]
        vpbroadcastd zmm5, [keys + 4]
        vpbroadcastd zmm6, [keys + 8]
        vpbroadcastd zmm7, [keys + 12]
        vpbroadcastd zmm8, [keys + 16]
        vpbroadcastd zmm9, [keys + 20]
        vpbroadcastd zmm10, [keys + 24]
        vpbroadcastd zmm11, [keys + 28]

        vpbroadcastd zmm13, [iv]
        vpbroadcastd zmm14, [iv + 4]
        vpbroadcastd zmm15, [iv + 8]
        ;; Set block counter s for the next 16 Chacha20 states
        vpbroadcastd zmm12, DWORD(blk_cnt)
        vpaddd zmm12, [rel set_1_16]

        cmp     len, 64*16
        jb      exit_loop_ks

align 32
start_loop_ks:
        ENCRYPT_1K zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                   zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                   zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, \
                   zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15, src, dst, off, 0

        ; Update remaining length
        sub     len, 64*16
        add     off, 64*16
        add     blk_cnt, 16

        ; Reload first two registers zmm0 and 1,
        ; as they have been overwritten by the previous macros
        vpbroadcastd zmm0, [rel constants]
        vpbroadcastd zmm1, [rel constants + 4]

        ; Increment counter values
        vpaddd      zmm12, [rel add_16]

        cmp     len, 64*16
        jae     start_loop_ks

exit_loop_ks:

        ; Check if there are partial block (less than 16*64 bytes)
        or      len, len
        jz      no_partial_block_ks

        cmp     len, 64*8
        ja      more_than_8_blocks_left_ks

        cmp     len, 64*4
        ja      more_than_4_blocks_left_ks

        ;; up to 4 blocks left

        ; Get last block counter dividing offset by 64
        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, blk_cnt, iv, keys, k2, 4, no_key

        ; Use same first 4 registers as the output of GENERATE_1K_KS,
        ; to be able to use common code later on to encrypt
        GENERATE_512_KS zmm25, zmm16, zmm17, zmm29, none, none, none, none, \
                        zmm0, zmm1, zmm2, zmm3, none, \
                        zmm8, zmm9, zmm10, zmm11, 4

        jmp ks_gen_done_ks

more_than_4_blocks_left_ks:
        ;; up to 8 blocks left

        ; Get last block counter dividing offset by 64
        ;; up to 8 blocks left
        PREPARE_NEXT_STATES_4_TO_8 zmm0, zmm1, zmm2, zmm3, zmm7, \
                                   zmm8, zmm9, blk_cnt, iv, keys, k2, 8, no_key

        ; Use same first 8 registers as the output of GENERATE_1K_KS,
        ; to be able to use common code later on to encrypt
        GENERATE_512_KS zmm25, zmm16, zmm17, zmm29, zmm19, zmm24, zmm26, zmm23, \
                        zmm0, zmm1, zmm2, zmm3, zmm7, \
                        zmm8, zmm9, zmm10, zmm11, 8

        jmp ks_gen_done_ks
more_than_8_blocks_left_ks:
        ; Generate another 64*16 bytes of keystream and XOR only the leftover plaintext
        GENERATE_1K_KS zmm16, zmm17, zmm18, zmm19, zmm20, zmm21, zmm22, zmm23, \
                       zmm24, zmm25, zmm26, zmm27, zmm28, zmm29, zmm30, zmm31, \
                       zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, \
                       zmm9, zmm10, zmm11, zmm12, zmm13, zmm14, zmm15

ks_gen_done_ks:

        ; Calculate number of final blocks
        mov     tmp, len
        add     tmp, 63
        shr     tmp, 6

        cmp     tmp, 8
        je      final_num_blocks_is_8_ks
        jb      final_num_blocks_is_1_7_ks

        ; Final blocks 9-16
        cmp     tmp, 12
        je      final_num_blocks_is_12_ks
        jb      final_num_blocks_is_9_11_ks

        ; Final blocks 13-16
        cmp     tmp, 14
        je      final_num_blocks_is_14_ks
        jb      final_num_blocks_is_13_ks

        cmp     tmp, 15
        je      final_num_blocks_is_15_ks
        jmp     final_num_blocks_is_16_ks

final_num_blocks_is_9_11_ks:
        cmp     tmp, 10
        je      final_num_blocks_is_10_ks
        jb      final_num_blocks_is_9_ks
        ja      final_num_blocks_is_11_ks

final_num_blocks_is_1_7_ks:
        ; Final blocks 1-7
        cmp     tmp, 4
        je      final_num_blocks_is_4_ks
        jb      final_num_blocks_is_1_3_ks

        ; Final blocks 5-7
        cmp     tmp, 6
        je      final_num_blocks_is_6_ks
        jb      final_num_blocks_is_5_ks
        ja      final_num_blocks_is_7_ks

final_num_blocks_is_1_3_ks:
        cmp     tmp, 2
        je      final_num_blocks_is_2_ks
        ja      final_num_blocks_is_3_ks

        ; 1 final block if no jump
%assign I 1
%rep 16
APPEND3(final_num_blocks_is_, I, _ks):

        lea     tmp, [rel len_to_mask]
        and     len, 63
        kmovq   k1, [tmp + len*8]

        ENCRYPT_1_16_BLOCKS zmm25, zmm16, zmm17, zmm29, zmm19, zmm24, zmm26, zmm23, \
                            zmm20, zmm21, zmm31, zmm27, zmm28, zmm22, zmm30, zmm18, \
                            zmm0, src, dst, off, k1, I, 0, prev_ks
        add     blk_cnt, I

        ; Update remain number of KS bytes
        mov     tmp, 64
        sub     tmp, len
        and     tmp, 63
        mov     [ctx + RemainKsBytes], tmp

        jmp     no_partial_block_ks

%assign I (I + 1)
%endrep

no_partial_block_ks:

        mov     [ctx + LastBlkCount], blk_cnt

        mov     r12, [rsp]
        mov     r13, [rsp + 8]
        mov     r14, [rsp + 16]
        mov     r15, [rsp + 24]
        mov     rbx, [rsp + 32]
        mov     rbp, [rsp + 40]
%ifndef LINUX
        mov     rdi, [rsp + 48]
%endif
        add     rsp, 8*7
%ifdef SAFE_DATA
        clear_all_zmms_asm
%endif

exit_ks:
        ret

mksection stack-noexec

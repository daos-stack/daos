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

;; Authors of original CRC implementation:
;;     Erdinc Ozturk
;;     Vinodh Gopal
;;     James Guilford
;;     Greg Tucker
;;
;; Reference paper titled:
;;     "Fast CRC Computation for Generic Polynomials Using PCLMULQDQ Instruction"
;;     URL: http://download.intel.com/design/intarch/papers/323102.pdf

%include "include/os.asm"
%include "include/memcpy.asm"
%include "include/reg_sizes.asm"
%include "include/crc32_refl.inc"

%ifndef CRC32_REFL_FN
%define CRC32_REFL_FN crc32_refl_by8_sse
%endif

[bits 64]
default rel

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

mksection .text

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; arg1 - initial CRC value
;; arg2 - buffer pointer
;; arg3 - buffer size
;; arg4 - pointer to CRC constants
;; Returns CRC value through EAX
align 32
MKGLOBAL(CRC32_REFL_FN,function,internal)
CRC32_REFL_FN:
        not             DWORD(arg1)

        ;; check if smaller than 256B
        cmp             arg3, 256
        jl              .less_than_256

        ;; load the initial crc value
        movd            xmm10, DWORD(arg1)      ; initial crc

        ;; load initial 128B data, xor the initial crc value
        movdqu          xmm0, [arg2 + 16 * 0]
        movdqu          xmm1, [arg2 + 16 * 1]
        movdqu          xmm2, [arg2 + 16 * 2]
        movdqu          xmm3, [arg2 + 16 * 3]
        movdqu          xmm4, [arg2 + 16 * 4]
        movdqu          xmm5, [arg2 + 16 * 5]
        movdqu          xmm6, [arg2 + 16 * 6]
        movdqu          xmm7, [arg2 + 16 * 7]

        ;; XOR the initial_crc value
        pxor            xmm0, xmm10
        movdqa          xmm10, [arg4 + crc32_const_fold_8x128b]

        ;; subtract 256 instead of 128 to save one instruction from the loop
        sub             arg3, 256

        ;; In this section of the code, there is ((128 * x) + y) bytes of buffer
        ;; where, 0 <= y < 128.
        ;; The fold_128_B_loop loop will fold 128 bytes at a time until
        ;; there is (128 + y) bytes of buffer left

        ;; Fold 128 bytes at a time.
        ;; This section of the code folds 8 xmm registers in parallel
.fold_128_B_loop:
        add             arg2, 128
        movdqu          xmm9, [arg2 + 16 * 0]
        movdqu          xmm12, [arg2 + 16 * 1]
        movdqa          xmm8, xmm0
        pclmulqdq       xmm8, xmm10, 0x10
        pclmulqdq       xmm0, xmm10 , 0x1
        movdqa          xmm13, xmm1
        pclmulqdq       xmm13, xmm10, 0x10
        pclmulqdq       xmm1, xmm10 , 0x1
        pxor            xmm0, xmm9
        xorps           xmm0, xmm8
        pxor            xmm1, xmm12
        xorps           xmm1, xmm13

        movdqu          xmm9, [arg2 + 16 * 2]
        movdqu          xmm12, [arg2 + 16 * 3]
        movdqa          xmm8, xmm2
        pclmulqdq       xmm8, xmm10, 0x10
        pclmulqdq       xmm2, xmm10 , 0x1
        movdqa          xmm13, xmm3
        pclmulqdq       xmm13, xmm10, 0x10
        pclmulqdq       xmm3, xmm10 , 0x1
        pxor            xmm2, xmm9
        xorps           xmm2, xmm8
        pxor            xmm3, xmm12
        xorps           xmm3, xmm13

        movdqu          xmm9, [arg2 + 16 * 4]
        movdqu          xmm12, [arg2 + 16 * 5]
        movdqa          xmm8, xmm4
        pclmulqdq       xmm8, xmm10, 0x10
        pclmulqdq       xmm4, xmm10 , 0x1
        movdqa          xmm13, xmm5
        pclmulqdq       xmm13, xmm10, 0x10
        pclmulqdq       xmm5, xmm10 , 0x1
        pxor            xmm4, xmm9
        xorps           xmm4, xmm8
        pxor            xmm5, xmm12
        xorps           xmm5, xmm13

        movdqu          xmm9, [arg2 + 16 * 6]
        movdqu          xmm12, [arg2 + 16 * 7]
        movdqa          xmm8, xmm6
        pclmulqdq       xmm8, xmm10, 0x10
        pclmulqdq       xmm6, xmm10 , 0x1
        movdqa          xmm13, xmm7
        pclmulqdq       xmm13, xmm10, 0x10
        pclmulqdq       xmm7, xmm10 , 0x1
        pxor            xmm6, xmm9
        xorps           xmm6, xmm8
        pxor            xmm7, xmm12
        xorps           xmm7, xmm13

        sub             arg3, 128
        jge             .fold_128_B_loop

        add             arg2, 128
        ;; At this point, the buffer pointer is pointing at the last
        ;; y bytes of the buffer, where 0 <= y < 128.
        ;; The 128B of folded data is in 8 of the xmm registers:
        ;;     xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7

        ;; fold the 8 xmm registers into 1 xmm register with different constants
        movdqa          xmm10, [arg4 + crc32_const_fold_7x128b]
        movdqa          xmm8, xmm0
        pclmulqdq       xmm8, xmm10, 0x1
        pclmulqdq       xmm0, xmm10, 0x10
        pxor            xmm7, xmm8
        xorps           xmm7, xmm0

        movdqa          xmm10, [arg4 + crc32_const_fold_6x128b]
        movdqa          xmm8, xmm1
        pclmulqdq       xmm8, xmm10, 0x1
        pclmulqdq       xmm1, xmm10, 0x10
        pxor            xmm7, xmm8
        xorps           xmm7, xmm1

        movdqa          xmm10, [arg4 + crc32_const_fold_5x128b]
        movdqa          xmm8, xmm2
        pclmulqdq       xmm8, xmm10, 0x1
        pclmulqdq       xmm2, xmm10, 0x10
        pxor            xmm7, xmm8
        pxor            xmm7, xmm2

        movdqa          xmm10, [arg4 + crc32_const_fold_4x128b]
        movdqa          xmm8, xmm3
        pclmulqdq       xmm8, xmm10, 0x1
        pclmulqdq       xmm3, xmm10, 0x10
        pxor            xmm7, xmm8
        xorps           xmm7, xmm3

        movdqa          xmm10, [arg4 + crc32_const_fold_3x128b]
        movdqa          xmm8, xmm4
        pclmulqdq       xmm8, xmm10, 0x1
        pclmulqdq       xmm4, xmm10, 0x10
        pxor            xmm7, xmm8
        pxor            xmm7, xmm4

        movdqa          xmm10, [arg4 + crc32_const_fold_2x128b]
        movdqa          xmm8, xmm5
        pclmulqdq       xmm8, xmm10, 0x1
        pclmulqdq       xmm5, xmm10, 0x10
        pxor            xmm7, xmm8
        xorps           xmm7, xmm5

        movdqa          xmm10, [arg4 + crc32_const_fold_1x128b]
        movdqa          xmm8, xmm6
        pclmulqdq       xmm8, xmm10, 0x1
        pclmulqdq       xmm6, xmm10, 0x10
        pxor            xmm7, xmm8
        pxor            xmm7, xmm6

        ;; Instead of 128, we add 128-16 to the loop counter to save 1
        ;; instruction from the loop below.
        ;; Instead of a cmp instruction, we use the negative flag with the jl instruction
        add             arg3, 128 - 16
        jl              .final_reduction_for_128

        ;; There are 16 + y bytes left to reduce.
        ;; 16 bytes is in register xmm7 and the rest is in memory
        ;; we can fold 16 bytes at a time if y>=16
        ;; continue folding 16B at a time

.16B_reduction_loop:
        movdqa          xmm8, xmm7
        pclmulqdq       xmm8, xmm10, 0x1
        pclmulqdq       xmm7, xmm10, 0x10
        pxor            xmm7, xmm8
        movdqu          xmm0, [arg2]
        pxor            xmm7, xmm0
        add             arg2, 16
        sub             arg3, 16
        ;; Instead of a cmp instruction, we utilize the flags with the jge instruction.
        ;; Equivalent of check if there is any more 16B in the buffer to be folded.
        jge             .16B_reduction_loop

        ;; Now we have 16+z bytes left to reduce, where 0<= z < 16.
        ;; First, we reduce the data in the xmm7 register

.final_reduction_for_128:
        add             arg3, 16
        je              .128_done

        ;; Here we are getting data that is less than 16 bytes.
        ;; Since we know that there was data before the pointer, we can offset
        ;; the input pointer before the actual point, to receive exactly 16 bytes.
        ;; After that the registers need to be adjusted.
.get_last_two_xmms:

        movdqa          xmm2, xmm7
        movdqu          xmm1, [arg2 - 16 + arg3]

        ;; Get rid of the extra data that was loaded before.
        ;; Load the shift constant.
        lea             rax, [rel pshufb_shf_table]
        movdqu          xmm0, [rax + arg3]

        pshufb          xmm7, xmm0
        pxor            xmm0, [rel mask3]
        pshufb          xmm2, xmm0

        pblendvb        xmm2, xmm1              ; xmm0 is implicit

        movdqa          xmm8, xmm7
        pclmulqdq       xmm8, xmm10, 0x1
        pclmulqdq       xmm7, xmm10, 0x10
        pxor            xmm7, xmm8
        pxor            xmm7, xmm2

.128_done:
        ;; compute crc of a 128-bit value
        movdqa          xmm10, [arg4 + crc32_const_fold_128b_to_64b]
        movdqa          xmm0, xmm7

        ;; 64b fold
        pclmulqdq       xmm7, xmm10, 0
        psrldq          xmm0, 8
        pxor            xmm7, xmm0

        ;; 32b fold
        movdqa          xmm0, xmm7
        pslldq          xmm7, 4
        pclmulqdq       xmm7, xmm10, 0x10
        pxor            xmm7, xmm0

        ;; barrett reduction
.barrett:
        pand            xmm7, [rel mask2]
        movdqa          xmm1, xmm7
        movdqa          xmm2, xmm7
        movdqa          xmm10, [arg4 + crc32_const_reduce_64b_to_32b]

        pclmulqdq       xmm7, xmm10, 0
        pxor            xmm7, xmm2
        pand            xmm7, [rel mask]
        movdqa          xmm2, xmm7
        pclmulqdq       xmm7, xmm10, 0x10
        pxor            xmm7, xmm2
        pxor            xmm7, xmm1
        pextrd          eax, xmm7, 2

.cleanup:
        not             eax
        ret

align 32
.less_than_256:
        ;; check if there is enough buffer to be able to fold 16B at a time
        cmp             arg3, 32
        jl              .less_than_32

        ;; if there is, load the constants
        movdqa          xmm10, [arg4 + crc32_const_fold_1x128b]

        movd            xmm0, DWORD(arg1)       ; get the initial crc value
        movdqu          xmm7, [arg2]            ; load the plaintext
        pxor            xmm7, xmm0

        ;; update the buffer pointer
        add             arg2, 16

        ;; update the counter
        ;; - subtract 32 instead of 16 to save one instruction from the loop
        sub             arg3, 32
        jmp             .16B_reduction_loop

align 32
.less_than_32:
        ;; Move initial crc to the return value.
        ;; This is necessary for zero-length buffers.
        mov             eax, DWORD(arg1)
        test            arg3, arg3
        je              .cleanup

        movd            xmm0, DWORD(arg1)       ; get the initial crc value

        cmp             arg3, 16
        je              .exact_16_left
        jl              .less_than_16_left

        movdqu          xmm7, [arg2]            ; load the plaintext
        pxor            xmm7, xmm0              ; xor the initial crc value
        add             arg2, 16
        sub             arg3, 16
        movdqa          xmm10, [arg4 + crc32_const_fold_1x128b]
        jmp             .get_last_two_xmms

align 32
.less_than_16_left:
        simd_load_sse_15_1 xmm7, arg2, arg3
        pxor            xmm7, xmm0              ; xor the initial crc value

        cmp             arg3, 4
        jl              .only_less_than_4

        lea             rax, [rel pshufb_shf_table]
        movdqu          xmm0, [rax + arg3]
        pshufb          xmm7,xmm0
        jmp             .128_done

align 32
.exact_16_left:
        movdqu          xmm7, [arg2]
        pxor            xmm7, xmm0              ; xor the initial crc value
        jmp             .128_done

.only_less_than_4:
        cmp             arg3, 3
        jl              .only_less_than_3
        pslldq          xmm7, 5
        jmp             .barrett

.only_less_than_3:
        cmp             arg3, 2
        jl              .only_less_than_2
        pslldq          xmm7, 6
        jmp             .barrett

.only_less_than_2:
        pslldq          xmm7, 7
        jmp             .barrett

mksection .rodata

align 16
mask:
        dq     0xFFFFFFFFFFFFFFFF, 0x0000000000000000

align 16
mask2:
        dq     0xFFFFFFFF00000000, 0xFFFFFFFFFFFFFFFF

align 16
mask3:
        dq     0x8080808080808080, 0x8080808080808080

align 16
pshufb_shf_table:
;; use these values for shift constants for the pshufb instruction
        dq 0x8786858483828100, 0x8f8e8d8c8b8a8988
        dq 0x0706050403020100, 0x000e0d0c0b0a0908

mksection stack-noexec

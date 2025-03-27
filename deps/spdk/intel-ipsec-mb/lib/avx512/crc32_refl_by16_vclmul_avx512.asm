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
%include "include/reg_sizes.asm"
%include "include/clear_regs.asm"
%include "include/crc32_refl.inc"
%include "include/cet.inc"
[bits 64]
default rel

%ifndef LINUX
%xdefine	arg1 rcx
%xdefine	arg2 rdx
%xdefine	arg3 r8
%xdefine        arg4 r9
%else
%xdefine	arg1 rdi
%xdefine	arg2 rsi
%xdefine	arg3 rdx
%xdefine        arg4 rcx
%endif

%define msg             arg2
%define len             arg3

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
MKGLOBAL(crc32_refl_by16_vclmul_avx512,function,internal)
crc32_refl_by16_vclmul_avx512:
        endbranch64
        not             DWORD(arg1)

	;; check if smaller than 256B
	cmp		len, 256
	jl		.less_than_256

	;; load the initial crc value
        vmovd		xmm10, DWORD(arg1)

	;; receive the initial 64B data, xor the initial crc value
	vmovdqu8	zmm0, [msg + 16*0]
	vmovdqu8	zmm4, [msg + 16*4]
	vpxorq		zmm0, zmm10
	vbroadcasti32x4	zmm10, [arg4 + crc32_const_fold_8x128b]

	sub		len, 256
	cmp		len, 256
	jl		.fold_128_B_loop

	vmovdqu8	zmm7, [msg + 16*8]
	vmovdqu8	zmm8, [msg + 16*12]
	vbroadcasti32x4 zmm16, [arg4 + crc32_const_fold_16x128b]
	sub		len, 256

.fold_256_B_loop:
	add		msg, 256
	vmovdqu8	zmm3, [msg + 16*0]
	vpclmulqdq	zmm1, zmm0, zmm16, 0x10
	vpclmulqdq	zmm0, zmm0, zmm16, 0x01
        vpternlogq      zmm0, zmm1, zmm3, 0x96

	vmovdqu8	zmm9, [msg + 16*4]
	vpclmulqdq	zmm5, zmm4, zmm16, 0x10
	vpclmulqdq	zmm4, zmm4, zmm16, 0x01
        vpternlogq      zmm4, zmm5, zmm9, 0x96

	vmovdqu8	zmm11, [msg + 16*8]
	vpclmulqdq	zmm12, zmm7, zmm16, 0x10
	vpclmulqdq	zmm7, zmm7, zmm16, 0x01
        vpternlogq      zmm7, zmm12, zmm11, 0x96

	vmovdqu8	zmm17, [msg + 16*12]
	vpclmulqdq	zmm14, zmm8, zmm16, 0x10
	vpclmulqdq	zmm8, zmm8, zmm16, 0x01
        vpternlogq      zmm8, zmm14, zmm17, 0x96

	sub		len, 256
	jge     	.fold_256_B_loop

	;; Fold 256 into 128
	add		msg, 256
	vpclmulqdq	zmm1, zmm0, zmm10, 0x01
	vpclmulqdq	zmm2, zmm0, zmm10, 0x10
	vpternlogq	zmm7, zmm1, zmm2, 0x96	; xor ABC

	vpclmulqdq	zmm5, zmm4, zmm10, 0x01
	vpclmulqdq	zmm6, zmm4, zmm10, 0x10
	vpternlogq	zmm8, zmm5, zmm6, 0x96	; xor ABC

	vmovdqa32	zmm0, zmm7
	vmovdqa32	zmm4, zmm8

	add		len, 128
	jmp		.fold_128_B_register

        ;; In this section of the code, there is ((128 * x) + y) bytes of buffer
        ;; where, 0 <= y < 128.
        ;; The fold_128_B_loop loop will fold 128 bytes at a time until
        ;; there is (128 + y) bytes of buffer left

        ;; Fold 128 bytes at a time.
        ;; This section of the code folds 8 xmm registers in parallel
.fold_128_B_loop:
	add		msg, 128
	vmovdqu8	zmm8, [msg + 16*0]
	vpclmulqdq	zmm2, zmm0, zmm10, 0x10
	vpclmulqdq	zmm0, zmm0, zmm10, 0x01
        vpternlogq      zmm0, zmm2, zmm8, 0x96

	vmovdqu8	zmm9, [msg + 16*4]
	vpclmulqdq	zmm5, zmm4, zmm10, 0x10
	vpclmulqdq	zmm4, zmm4, zmm10, 0x01
        vpternlogq      zmm4, zmm5, zmm9, 0x96

	sub		len, 128
	jge		.fold_128_B_loop

	add		msg, 128

        ;; At this point, the buffer pointer is pointing at the last
        ;; y bytes of the buffer, where 0 <= y < 128.
        ;; The 128 bytes of folded data is in 2 of the zmm registers:
        ;;     zmm0 and zmm4

.fold_128_B_register:
        ;; fold the 8x128-bits into 1x128-bits with different constants
	vmovdqu8	zmm16, [arg4 + crc32_const_fold_7x128b]
	vmovdqu8	zmm11, [arg4 + crc32_const_fold_3x128b]
	vpclmulqdq	zmm1, zmm0, zmm16, 0x01
	vpclmulqdq	zmm2, zmm0, zmm16, 0x10
	vextracti64x2	xmm7, zmm4, 3		; save last that has no multiplicand

	vpclmulqdq	zmm5, zmm4, zmm11, 0x01
	vpclmulqdq	zmm6, zmm4, zmm11, 0x10
	vmovdqa		xmm10, [arg4 + crc32_const_fold_1x128b] ; Needed later in reduction loop
	vpternlogq	zmm1, zmm2, zmm5, 0x96	; xor ABC
	vpternlogq	zmm1, zmm6, zmm7, 0x96	; xor ABC

	vshufi64x2      zmm8, zmm1, zmm1, 0x4e ; Swap 1,0,3,2 - 01 00 11 10
	vpxorq          ymm8, ymm8, ymm1
	vextracti64x2   xmm5, ymm8, 1
	vpxorq          xmm7, xmm5, xmm8

        ;; Instead of 128, we add 128-16 to the loop counter to save 1
        ;; instruction from the loop below.
        ;; Instead of a cmp instruction, we use the negative flag with the jl instruction
	add		len, 128 - 16
	jl		.final_reduction_for_128

        ;; There are 16 + y bytes left to reduce.
        ;; 16 bytes is in register xmm7 and the rest is in memory
        ;; we can fold 16 bytes at a time if y>=16
        ;; continue folding 16B at a time

.reduction_loop_16B:
	vpclmulqdq	xmm8, xmm7, xmm10, 0x1
	vpclmulqdq	xmm7, xmm7, xmm10, 0x10
	vpxor		xmm7, xmm8
	vmovdqu		xmm0, [msg]
	vpxor		xmm7, xmm0
	add		msg, 16
	sub		len, 16
        ;; Instead of a cmp instruction, we utilize the flags with the jge instruction.
        ;; Equivalent of check if there is any more 16B in the buffer to be folded.
	jge		.reduction_loop_16B

        ;; Now we have 16+z bytes left to reduce, where 0<= z < 16.
        ;; First, we reduce the data in the xmm7 register
.final_reduction_for_128:
	add		len, 16
	je		.done_128

	; here we are getting data that is less than 16 bytes.
	; since we know that there was data before the pointer, we can offset
	; the input pointer before the actual point, to receive exactly 16 bytes.
	; after that the registers need to be adjusted.
.get_last_two_xmms:

	vmovdqa		xmm2, xmm7
	vmovdqu		xmm1, [msg - 16 + len]

        ;; Get rid of the extra data that was loaded before
        ;; load the shift constant
	lea		rax, [rel pshufb_shf_table]
	vmovdqu		xmm0, [rax + len]

	vpshufb		xmm7, xmm0
	vpxor		xmm0, [rel mask3]
	vpshufb		xmm2, xmm0

	vpblendvb	xmm2, xmm2, xmm1, xmm0

	vpclmulqdq	xmm8, xmm7, xmm10, 0x1
	vpclmulqdq	xmm7, xmm7, xmm10, 0x10
        vpternlogq      zmm7, zmm2, zmm8, 0x96

.done_128:
	;; compute crc of a 128-bit value
	vmovdqa		xmm10, [arg4 + crc32_const_fold_128b_to_64b]
	vmovdqa		xmm0, xmm7

	;; 64b fold
	vpclmulqdq	xmm7, xmm10, 0
	vpsrldq		xmm0, 8
	vpxor		xmm7, xmm0

	;; 32b fold
	vmovdqa		xmm0, xmm7
	vpslldq		xmm7, 4
	vpclmulqdq	xmm7, xmm10, 0x10
	vpxor		xmm7, xmm0

	;; barrett reduction
.barrett:
	vpand		xmm7, [rel mask2]
	vmovdqa		xmm1, xmm7
	vmovdqa		xmm2, xmm7
	vmovdqa		xmm10, [arg4 + crc32_const_reduce_64b_to_32b]

	vpclmulqdq	xmm7, xmm10, 0
        vpternlogq      xmm7, xmm2, [rel mask], 0x28
	vmovdqa		xmm2, xmm7
	vpclmulqdq	xmm7, xmm10, 0x10
        vpternlogq      zmm7, zmm2, zmm1, 0x96
	vpextrd		eax, xmm7, 2

.cleanup:
	not		eax
	ret

align 32
.less_than_256:
	;; check if there is enough buffer to be able to fold 16B at a time
	cmp	len, 32
	jl	.less_than_32

	;; if there is, load the constants
	vmovdqa	xmm10, [arg4 + crc32_const_fold_1x128b]

	vmovd	xmm0, DWORD(arg1)	; get the initial crc value
	vmovdqu	xmm7, [msg]		; load the plaintext
	vpxor	xmm7, xmm0

	;; update the buffer pointer
	add	msg, 16

        ;; update the counter
        ;; - subtract 32 instead of 16 to save one instruction from the loop
	sub	len, 32
	jmp	.reduction_loop_16B

align 32
.less_than_32:
        ;; Move initial crc to the return value.
        ;; This is necessary for zero-length buffers.
        mov	eax, DWORD(arg1)
	test	len, len
	je	.cleanup

	vmovd	xmm0, DWORD(arg1)	; get the initial crc value

	cmp	len, 16
	je	.exact_16_left
	jl	.less_than_16_left

	vmovdqu	xmm7, [msg]		; load the plaintext
	vpxor	xmm7, xmm0		; xor the initial crc value
	add	msg, 16
	sub	len, 16
	vmovdqa	xmm10, [arg4 + crc32_const_fold_1x128b]
	jmp	.get_last_two_xmms

align 32
.less_than_16_left:
        lea     r11, [rel byte_len_to_mask_table]
        kmovw   k2, [r11 + len*2]
        vmovdqu8 xmm7{k2}{z}, [msg]
	vpxor	xmm7, xmm0	; xor the initial crc value

        cmp	len, 4
	jl	.only_less_than_4

	lea	r11, [rel pshufb_shf_table]
	vmovdqu	xmm0, [r11 + len]
	vpshufb	xmm7,xmm0
	jmp	.done_128

.only_less_than_4:
	cmp	len, 3
	jl	.only_less_than_3

	vpslldq	xmm7, 5
	jmp	.barrett

.only_less_than_3:
	cmp	len, 2
	jl	.only_less_than_2

	vpslldq	xmm7, 6
	jmp	.barrett

.only_less_than_2:
	vpslldq	xmm7, 7
	jmp	.barrett

align 32
.exact_16_left:
	vmovdqu	xmm7, [msg]
	vpxor	xmm7, xmm0      ; xor the initial crc value
	jmp	.done_128

mksection .rodata

align 16
pshufb_shf_table:
        ;; use these values for shift constants for the pshufb instruction
        dq 0x8786858483828100, 0x8f8e8d8c8b8a8988
        dq 0x0706050403020100, 0x000e0d0c0b0a0908

align 16
mask:   dq     0xFFFFFFFFFFFFFFFF, 0x0000000000000000
mask2:  dq     0xFFFFFFFF00000000, 0xFFFFFFFFFFFFFFFF
mask3:  dq     0x8080808080808080, 0x8080808080808080

align 64
byte_len_to_mask_table:
        dw      0x0000, 0x0001, 0x0003, 0x0007,
        dw      0x000f, 0x001f, 0x003f, 0x007f,
        dw      0x00ff, 0x01ff, 0x03ff, 0x07ff,
        dw      0x0fff, 0x1fff, 0x3fff, 0x7fff,
        dw      0xffff

mksection stack-noexec

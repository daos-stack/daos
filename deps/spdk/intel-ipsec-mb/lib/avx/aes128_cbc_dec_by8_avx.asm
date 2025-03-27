;;
;; Copyright (c) 2012-2021, Intel Corporation
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

; routine to do AES128 CBC decrypt "by8"

;; clobbers xmm0-15

%include "include/os.asm"
%include "include/clear_regs.asm"

%define CONCAT(a,b) a %+ b
%define VMOVDQ vmovdqu

%define xdata0	xmm0
%define xdata1	xmm1
%define xdata2	xmm2
%define xdata3	xmm3
%define xdata4	xmm4
%define xdata5	xmm5
%define xdata6	xmm6
%define xdata7	xmm7
%define xIV  	xmm8
%define xkey0 	xmm9
%define xkey2 	xmm10
%define xkey4 	xmm11
%define xkey6 	xmm12
%define xkey8 	xmm13
%define xkey10	xmm14
%define xkeytmp	xmm15

%ifdef LINUX
%define p_in	rdi
%define p_IV	rsi
%define p_keys	rdx
%define p_out	rcx
%define len     r8
%define next_iv r9
%else
%define p_in	rcx
%define p_IV	rdx
%define p_keys	r8
%define p_out	r9
%define next_iv r11
%endif

%define num_bytes rax
%define tmp	r10
%define tmp2    r11

%macro do_aes_load 1
	do_aes %1, 1
%endmacro

%macro do_aes_noload 1
	do_aes %1, 0
%endmacro

; do_aes num_in_par load_keys
; This increments p_in, but not p_out
%macro do_aes 2
%define %%by %1
%define %%load_keys %2

%if (%%load_keys)
	vmovdqa	xkey0, [p_keys + 0*16]
%endif

%assign i 0
%rep %%by
	VMOVDQ	CONCAT(xdata,i), [p_in  + i*OFFSET]
%assign i (i+1)
%endrep

%if (%%load_keys)
	vmovdqa	xkey2, [p_keys + 2*16]
%endif
%assign i 0
%rep %%by
	vpxor	CONCAT(xdata,i), CONCAT(xdata,i), xkey0
%assign i (i+1)
%endrep

	add	p_in, OFFSET*%%by

	vmovdqa	xkeytmp, [p_keys + 1*16]
%assign i 0
%rep %%by
	vaesdec	CONCAT(xdata,i), CONCAT(xdata,i), xkeytmp
%assign i (i+1)
%endrep

%if (%%load_keys)
	vmovdqa	xkey4, [p_keys + 4*16]
%endif
%assign i 0
%rep %%by
	vaesdec	CONCAT(xdata,i), CONCAT(xdata,i), xkey2
%assign i (i+1)
%endrep

	vmovdqa	xkeytmp, [p_keys + 3*16]
%assign i 0
%rep %%by
	vaesdec	CONCAT(xdata,i), CONCAT(xdata,i), xkeytmp
%assign i (i+1)
%endrep

%if (%%load_keys)
	vmovdqa	xkey6, [p_keys + 6*16]
%endif
%assign i 0
%rep %%by
	vaesdec	CONCAT(xdata,i), CONCAT(xdata,i), xkey4
%assign i (i+1)
%endrep

	vmovdqa	xkeytmp, [p_keys + 5*16]
%assign i 0
%rep %%by
	vaesdec	CONCAT(xdata,i), CONCAT(xdata,i), xkeytmp
%assign i (i+1)
%endrep

%if (%%load_keys)
	vmovdqa	xkey8, [p_keys + 8*16]
%endif
%assign i 0
%rep %%by
	vaesdec	CONCAT(xdata,i), CONCAT(xdata,i), xkey6
%assign i (i+1)
%endrep

	vmovdqa	xkeytmp, [p_keys + 7*16]
%assign i 0
%rep %%by
	vaesdec	CONCAT(xdata,i), CONCAT(xdata,i), xkeytmp
%assign i (i+1)
%endrep

%if (%%load_keys)
	vmovdqa	xkey10, [p_keys + 10*16]
%endif
%assign i 0
%rep %%by
	vaesdec	CONCAT(xdata,i), CONCAT(xdata,i), xkey8
%assign i (i+1)
%endrep

	vmovdqa	xkeytmp, [p_keys + 9*16]
%assign i 0
%rep %%by
	vaesdec	CONCAT(xdata,i), CONCAT(xdata,i), xkeytmp
%assign i (i+1)
%endrep

%assign i 0
%rep %%by
	vaesdeclast	CONCAT(xdata,i), CONCAT(xdata,i), xkey10
%assign i (i+1)
%endrep

	vpxor	xdata0, xdata0, xIV
%assign i 1
%if (%%by > 1)
%rep (%%by - 1)
	VMOVDQ	xIV, [p_in  + (i-1)*OFFSET - OFFSET*%%by]
	vpxor	CONCAT(xdata,i), CONCAT(xdata,i), xIV
%assign i (i+1)
%endrep
%endif
	VMOVDQ	xIV, [p_in  + (i-1)*OFFSET - OFFSET*%%by]

%assign i 0
%rep %%by
	VMOVDQ	[p_out  + i*OFFSET], CONCAT(xdata,i)
%assign i (i+1)
%endrep
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

mksection .text

%ifndef AES_CBC_DEC_128_X8
;; aes_cbc_dec_128_avx(void *in, void *IV, void *keys, void *out, UINT64 num_bytes)
%define AES_CBC_DEC_128_X8 aes_cbc_dec_128_avx
%define OFFSET 16
%endif

MKGLOBAL(AES_CBC_DEC_128_X8,function,internal)
AES_CBC_DEC_128_X8:

%ifndef LINUX
	mov	num_bytes, [rsp + 8*5]
%else
        mov     num_bytes, len
%endif

	vmovdqu	xIV, [p_IV]

%ifdef CBCS
        ;; convert CBCS length to standard number of CBC blocks
        ;; ((num_bytes + 9 blocks) / 160) = num blocks to decrypt
        mov     tmp2, rdx
        xor     rdx, rdx        ;; store and zero rdx for div
        add     num_bytes, 9*16
        mov     tmp, 160
        div     tmp             ;; divide by 160
        shl     num_bytes, 4    ;; multiply by 16 to get num bytes
        mov     rdx, tmp2
%endif

	mov	tmp, num_bytes
	and	tmp, 7*16
	jz	mult_of_8_blks

	; 1 <= tmp <= 7
	cmp	tmp, 4*16
	jg	gt4
	je	eq4

lt4:
	cmp	tmp, 2*16
	jg	eq3
	je	eq2
eq1:
	do_aes_load	1
	add	p_out, 1*OFFSET
	and	num_bytes, ~7*16
	jz	do_return2
	jmp	main_loop2

eq2:
	do_aes_load	2
	add	p_out, 2*OFFSET
	and	num_bytes, ~7*16
	jz	do_return2
	jmp	main_loop2

eq3:
	do_aes_load	3
	add	p_out, 3*OFFSET
	and	num_bytes, ~7*16
	jz	do_return2
	jmp	main_loop2

eq4:
	do_aes_load	4
	add	p_out, 4*OFFSET
	and	num_bytes, ~7*16
	jz	do_return2
	jmp	main_loop2

gt4:
	cmp	tmp, 6*16
	jg	eq7
	je	eq6

eq5:
	do_aes_load	5
	add	p_out, 5*OFFSET
	and	num_bytes, ~7*16
	jz	do_return2
	jmp	main_loop2

eq6:
	do_aes_load	6
	add	p_out, 6*OFFSET
	and	num_bytes, ~7*16
	jz	do_return2
	jmp	main_loop2

eq7:
	do_aes_load	7
	add	p_out, 7*OFFSET
	and	num_bytes, ~7*16
	jz	do_return2
	jmp	main_loop2

mult_of_8_blks:
	vmovdqa	xkey0, [p_keys + 0*16]
	vmovdqa	xkey2, [p_keys + 2*16]
	vmovdqa	xkey4, [p_keys + 4*16]
	vmovdqa	xkey6, [p_keys + 6*16]
	vmovdqa	xkey8, [p_keys + 8*16]
	vmovdqa	xkey10, [p_keys + 10*16]

main_loop2:
	; num_bytes is a multiple of 8 and >0
	do_aes_noload	8
	add	p_out,	8*OFFSET
	sub	num_bytes, 8*16
	jne	main_loop2

do_return2:
%ifdef CBCS
%ifndef LINUX
	mov	next_iv, [rsp + 8*6]
%endif
        ;; store last cipher block as next iv
        vmovdqu  [next_iv], xIV
%endif
%ifdef SAFE_DATA
	clear_all_xmms_avx_asm
%endif ;; SAFE_DATA

	ret

mksection stack-noexec

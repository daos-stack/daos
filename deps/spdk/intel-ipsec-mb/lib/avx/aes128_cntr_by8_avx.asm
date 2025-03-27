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

%include "include/os.asm"
%include "include/imb_job.asm"
%include "include/memcpy.asm"
%include "include/const.inc"
%include "include/reg_sizes.asm"
%include "include/clear_regs.asm"

; routine to do AES128 CNTR enc/decrypt "by8"
; XMM registers are clobbered. Saving/restoring must be done at a higher level
mksection .rodata
default rel

%ifndef CNTR_CCM_AVX
MKGLOBAL(byteswap_const,data,internal)
MKGLOBAL(set_byte15,data,internal)
MKGLOBAL(ddq_add_1,data,internal)
MKGLOBAL(ddq_add_2,data,internal)
MKGLOBAL(ddq_add_3,data,internal)
MKGLOBAL(ddq_add_4,data,internal)
MKGLOBAL(ddq_add_5,data,internal)
MKGLOBAL(ddq_add_6,data,internal)
MKGLOBAL(ddq_add_7,data,internal)
MKGLOBAL(ddq_add_8,data,internal)
%endif ;; CNTR_CCM_AVX

align 16
byteswap_const:	;DDQ 0x000102030405060708090A0B0C0D0E0F
		DQ 0x08090A0B0C0D0E0F, 0x0001020304050607
set_byte15:	DQ 0x0000000000000000, 0x0100000000000000

ddq_add_1:	;DDQ 0x00000000000000000000000000000001
		DQ 0x0000000000000001, 0x0000000000000000
ddq_add_2:	;DDQ 0x00000000000000000000000000000002
		DQ 0x0000000000000002, 0x0000000000000000
ddq_add_3:	;DDQ 0x00000000000000000000000000000003
		DQ 0x0000000000000003, 0x0000000000000000
ddq_add_4:	;DDQ 0x00000000000000000000000000000004
		DQ 0x0000000000000004, 0x0000000000000000
ddq_add_5:	;DDQ 0x00000000000000000000000000000005
		DQ 0x0000000000000005, 0x0000000000000000
ddq_add_6:	;DDQ 0x00000000000000000000000000000006
		DQ 0x0000000000000006, 0x0000000000000000
ddq_add_7:	;DDQ 0x00000000000000000000000000000007
		DQ 0x0000000000000007, 0x0000000000000000
ddq_add_8:	;DDQ 0x00000000000000000000000000000008
		DQ 0x0000000000000008, 0x0000000000000000

mksection .text

%define CONCAT(a,b) a %+ b
%define VMOVDQ vmovdqu

%define xdata0	xmm0
%define xdata1	xmm1
%define xpart	xmm1
%define xdata2	xmm2
%define xdata3	xmm3
%define xdata4	xmm4
%define xdata5	xmm5
%define xdata6	xmm6
%define xdata7	xmm7
%define xcounter xmm8
%define xtmp    xmm8
%define xbyteswap xmm9
%define xtmp2   xmm9
%define xkey0 	xmm10
%define xtmp3   xmm10
%define xkey3 	xmm11
%define xkey6 	xmm12
%define xkey9	xmm13
%define xkeyA	xmm14
%define xkeyB	xmm15

%ifdef CNTR_CCM_AVX
%ifdef LINUX
%define job	  rdi
%define p_in	  rsi
%define p_keys	  rdx
%define p_out	  rcx
%define num_bytes r8
%define p_ivlen   r9
%else ;; LINUX
%define job	  rcx
%define p_in	  rdx
%define p_keys	  r8
%define p_out	  r9
%define num_bytes r10
%define p_ivlen   rax
%endif ;; LINUX
%define p_IV    r11
%else ;; CNTR_CCM_AVX
%ifdef LINUX
%define p_in	  rdi
%define p_IV	  rsi
%define p_keys	  rdx
%define p_out	  rcx
%define num_bytes r8
%define num_bits  r8
%define p_ivlen   r9
%else ;; LINUX
%define p_in	  rcx
%define p_IV	  rdx
%define p_keys	  r8
%define p_out	  r9
%define num_bytes r10
%define num_bits  r10
%define p_ivlen   qword [rsp + 8*6]
%endif ;; LINUX
%endif ;; CNTR_CCM_AVX

%define tmp	r11
%define flags   r11

%define r_bits   r12
%define tmp2    r13
%define mask    r14

%macro do_aes_load 2
	do_aes %1, %2, 1
%endmacro

%macro do_aes_noload 2
	do_aes %1, %2, 0
%endmacro

; do_aes num_in_par load_keys
; This increments p_in, but not p_out
%macro do_aes 3
%define %%by %1
%define %%cntr_type %2
%define %%load_keys %3

%ifidn %%cntr_type, CNTR_BIT
%define %%VPADD vpaddq
%else
%define %%VPADD vpaddd
%endif

%if (%%load_keys)
	vmovdqa	xkey0, [p_keys + 0*16]
%endif

	vpshufb	xdata0, xcounter, xbyteswap
%assign i 1
%rep (%%by - 1)
	%%VPADD	CONCAT(xdata,i), xcounter, [rel CONCAT(ddq_add_,i)]
	vpshufb	CONCAT(xdata,i), CONCAT(xdata,i), xbyteswap
%assign i (i + 1)
%endrep

	vmovdqa	xkeyA, [p_keys + 1*16]

	vpxor	xdata0, xkey0
	%%VPADD	xcounter, xcounter, [rel CONCAT(ddq_add_,%%by)]

%assign i 1
%rep (%%by - 1)
	vpxor	CONCAT(xdata,i), xkey0
%assign i (i + 1)
%endrep

	vmovdqa	xkeyB, [p_keys + 2*16]
%assign i 0
%rep %%by
	vaesenc	CONCAT(xdata,i), CONCAT(xdata,i), xkeyA		; key 1
%assign i (i+1)
%endrep

%if (%%load_keys)
	vmovdqa	xkey3, [p_keys + 3*16]
%endif
%assign i 0
%rep %%by
	vaesenc	CONCAT(xdata,i), CONCAT(xdata,i), xkeyB		; key 2
%assign i (i+1)
%endrep

	add	p_in, 16*%%by

	vmovdqa	xkeyB, [p_keys + 4*16]
%assign i 0
%rep %%by
	vaesenc	CONCAT(xdata,i), CONCAT(xdata,i), xkey3		; key 3
%assign i (i+1)
%endrep

	vmovdqa	xkeyA, [p_keys + 5*16]
%assign i 0
%rep %%by
	vaesenc	CONCAT(xdata,i), CONCAT(xdata,i), xkeyB		; key 4
%assign i (i+1)
%endrep

%if (%%load_keys)
	vmovdqa	xkey6, [p_keys + 6*16]
%endif
%assign i 0
%rep %%by
	vaesenc	CONCAT(xdata,i), CONCAT(xdata,i), xkeyA		; key 5
%assign i (i+1)
%endrep

	vmovdqa	xkeyA, [p_keys + 7*16]
%assign i 0
%rep %%by
	vaesenc	CONCAT(xdata,i), CONCAT(xdata,i), xkey6		; key 6
%assign i (i+1)
%endrep

	vmovdqa	xkeyB, [p_keys + 8*16]
%assign i 0
%rep %%by
	vaesenc	CONCAT(xdata,i), CONCAT(xdata,i), xkeyA		; key 7
%assign i (i+1)
%endrep

%if (%%load_keys)
	vmovdqa	xkey9, [p_keys + 9*16]
%endif
%assign i 0
%rep %%by
	vaesenc	CONCAT(xdata,i), CONCAT(xdata,i), xkeyB		; key 8
%assign i (i+1)
%endrep

	vmovdqa	xkeyB, [p_keys + 10*16]
%assign i 0
%rep %%by
	vaesenc	CONCAT(xdata,i), CONCAT(xdata,i), xkey9		; key 9
%assign i (i+1)
%endrep

%assign i 0
%rep %%by
	vaesenclast	CONCAT(xdata,i), CONCAT(xdata,i), xkeyB		; key 10
%assign i (i+1)
%endrep

%assign i 0
%rep (%%by / 2)
%assign j (i+1)
	VMOVDQ	xkeyA, [p_in + i*16 - 16*%%by]
	VMOVDQ	xkeyB, [p_in + j*16 - 16*%%by]
	vpxor	CONCAT(xdata,i), CONCAT(xdata,i), xkeyA
	vpxor	CONCAT(xdata,j), CONCAT(xdata,j), xkeyB
%assign i (i+2)
%endrep
%if (i < %%by)
	VMOVDQ	xkeyA, [p_in + i*16 - 16*%%by]
	vpxor	CONCAT(xdata,i), CONCAT(xdata,i), xkeyA
%endif

%ifidn %%cntr_type, CNTR_BIT
        ;; check if this is the end of the message
        mov     tmp, num_bytes
        and     tmp, ~(%%by*16)
        jnz     %%skip_preserve
        ;; Check if there is a partial byte
        or      r_bits, r_bits
        jz      %%skip_preserve

%assign idx (%%by - 1)
        ;; Load output to get last partial byte
        vmovdqu        xtmp, [p_out + idx * 16]

        ;; Save RCX in temporary GP register
        mov             tmp, rcx
        mov             mask, 0xff
        mov             cl, BYTE(r_bits)
        shr             mask, cl ;; e.g. 3 remaining bits -> mask = 00011111
        mov             rcx, tmp

        vmovq           xtmp2, mask
        vpslldq         xtmp2, 15
        ;; At this point, xtmp2 contains a mask with all 0s, but with some ones
        ;; in the partial byte

        ;; Clear all the bits that do not need to be preserved from the output
        vpand           xtmp, xtmp, xtmp2

        ;; Clear all bits from the input that are not to be ciphered
        vpandn          CONCAT(xdata,idx), xtmp2, CONCAT(xdata,idx)
        vpor            CONCAT(xdata,idx), xtmp

%%skip_preserve:
%endif

%assign i 0
%rep %%by
	VMOVDQ	[p_out  + i*16], CONCAT(xdata,i)
%assign i (i+1)
%endrep
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Macro performing AES-CTR.
;;
%macro DO_CNTR 1
%define %%CNTR_TYPE %1 ; [in] Type of CNTR operation to do (CNTR/CNTR_BIT/CCM)

%ifidn %%CNTR_TYPE, CCM
        mov     p_in, [job + _src]
        add     p_in, [job + _cipher_start_src_offset_in_bytes]
        mov     p_ivlen, [job + _iv_len_in_bytes]
        mov	num_bytes, [job + _msg_len_to_cipher_in_bytes]
        mov     p_keys, [job + _enc_keys]
        mov     p_out, [job + _dst]

	vmovdqa	xbyteswap, [rel byteswap_const]
        ;; Prepare IV ;;

        ;; Byte 0: flags with L'
        ;; Calculate L' = 15 - Nonce length - 1 = 14 - IV length
        mov     flags, 14
        sub     flags, p_ivlen
        vmovd   xcounter, DWORD(flags)
        ;; Bytes 1 - 13: Nonce (7 - 13 bytes long)

        ;; Bytes 1 - 7 are always copied (first 7 bytes)
        mov     p_IV, [job + _iv]
        vpinsrb xcounter, [p_IV], 1
        vpinsrw xcounter, [p_IV + 1], 1
        vpinsrd xcounter, [p_IV + 3], 1

        cmp     p_ivlen, 7
        je      _finish_nonce_move

        cmp     p_ivlen, 8
        je      _iv_length_8
        cmp     p_ivlen, 9
        je      _iv_length_9
        cmp     p_ivlen, 10
        je      _iv_length_10
        cmp     p_ivlen, 11
        je      _iv_length_11
        cmp     p_ivlen, 12
        je      _iv_length_12

        ;; Bytes 8 - 13
_iv_length_13:
        vpinsrb xcounter, [p_IV + 12], 13
_iv_length_12:
        vpinsrb xcounter, [p_IV + 11], 12
_iv_length_11:
        vpinsrd xcounter, [p_IV + 7], 2
        jmp     _finish_nonce_move
_iv_length_10:
        vpinsrb xcounter, [p_IV + 9], 10
_iv_length_9:
        vpinsrb xcounter, [p_IV + 8], 9
_iv_length_8:
        vpinsrb xcounter, [p_IV + 7], 8

_finish_nonce_move:
        ; last byte = 1
        vpor    xcounter, [rel set_byte15]
%else ;; CNTR/CNTR_BIT
%ifndef LINUX
	mov	num_bytes, [rsp + 8*5] ; arg5
%endif

%ifidn %%CNTR_TYPE, CNTR_BIT
        push r12
        push r13
        push r14
%endif

	vmovdqa	xbyteswap, [rel byteswap_const]
%ifidn %%CNTR_TYPE, CNTR
        test    p_ivlen, 16
        jnz     %%iv_is_16_bytes
        ; Read 12 bytes: Nonce + ESP IV. Then pad with block counter 0x00000001
        mov     DWORD(tmp), 0x01000000
        vpinsrq xcounter, [p_IV], 0
        vpinsrd xcounter, [p_IV + 8], 2
        vpinsrd xcounter, DWORD(tmp), 3

%else ;; CNTR_BIT
        ; Read 16 byte IV: Nonce + 8-byte block counter (BE)
        vmovdqu xcounter, [p_IV]
%endif
%endif ;; CNTR/CNTR_BIT/CCM
%%bswap_iv:
	vpshufb	xcounter, xbyteswap

        ;; calculate len
        ;; convert bits to bytes (message length in bits for CNTR_BIT)
%ifidn %%CNTR_TYPE, CNTR_BIT
        mov     r_bits, num_bits
        add     num_bits, 7
        shr     num_bits, 3 ; "num_bits" and "num_bytes" registers are the same
        and     r_bits, 7   ; Check if there are remainder bits (0-7)
%endif

	mov	tmp, num_bytes
	and	tmp, 7*16
	jz	%%chk             ; x8 > or < 15 (not 7 lines)

	; 1 <= tmp <= 7
	cmp	tmp, 4*16
	jg	%%gt4
	je	%%eq4

%%lt4:
	cmp	tmp, 2*16
	jg	%%eq3
	je	%%eq2
%%eq1:
	do_aes_load	1, %%CNTR_TYPE
	add	p_out, 1*16
	jmp	%%chk

%%eq2:
	do_aes_load	2, %%CNTR_TYPE
	add	p_out, 2*16
	jmp	%%chk

%%eq3:
	do_aes_load	3, %%CNTR_TYPE
	add	p_out, 3*16
	jmp	%%chk

%%eq4:
	do_aes_load	4, %%CNTR_TYPE
	add	p_out, 4*16
	jmp	%%chk

%%gt4:
	cmp	tmp, 6*16
	jg	%%eq7
	je	%%eq6

%%eq5:
	do_aes_load	5, %%CNTR_TYPE
	add	p_out, 5*16
	jmp	%%chk

%%eq6:
	do_aes_load	6, %%CNTR_TYPE
	add	p_out, 6*16
	jmp	%%chk

%%eq7:
	do_aes_load	7, %%CNTR_TYPE
	add	p_out, 7*16
	; fall through to chk
%%chk:
	and	num_bytes, ~(7*16)
	jz	%%do_return2

        cmp	num_bytes, 16
        jb	%%last

	; process multiples of 8 blocks
	vmovdqa	xkey0, [p_keys + 0*16]
	vmovdqa	xkey3, [p_keys + 3*16]
	vmovdqa	xkey6, [p_keys + 6*16]
	vmovdqa	xkey9, [p_keys + 9*16]
	jmp	%%main_loop2

align 32
%%main_loop2:
	; num_bytes is a multiple of 8 blocks + partial bytes
	do_aes_noload	8, %%CNTR_TYPE
	add	p_out,	8*16
	sub	num_bytes, 8*16
        cmp	num_bytes, 8*16
	jae	%%main_loop2

        ; Check if there is a partial block
        or      num_bytes, num_bytes
        jnz	%%last

%%do_return2:
%ifidn %%CNTR_TYPE, CCM
	mov	rax, job
	or	dword [rax + _status], IMB_STATUS_COMPLETED_CIPHER
%endif

%ifidn %%CNTR_TYPE, CNTR_BIT
        pop r14
        pop r13
        pop r12
%endif

%ifdef SAFE_DATA
	clear_all_xmms_avx_asm
%endif ;; SAFE_DATA

	ret

%%last:

	; load partial block into XMM register
	simd_load_avx_15_1 xpart, p_in, num_bytes

%%final_ctr_enc:
	; Encryption of a single partial block
        vpshufb	xcounter, xbyteswap
        vmovdqa	xdata0, xcounter
        vpxor   xdata0, [p_keys + 16*0]
%assign i 1
%rep 9
        vaesenc xdata0, [p_keys + 16*i]
%assign i (i+1)
%endrep
	; created keystream
        vaesenclast xdata0, [p_keys + 16*i]

	; xor keystream with the message (scratch)
        vpxor   xdata0, xpart

%ifidn %%CNTR_TYPE, CNTR_BIT
        ;; Check if there is a partial byte
        or      r_bits, r_bits
        jz      %%store_output

        ;; Load output to get last partial byte
        simd_load_avx_15_1 xtmp, p_out, num_bytes

        ;; Save RCX in temporary GP register
        mov     tmp, rcx
        mov     mask, 0xff
%ifidn r_bits, rcx
%error "r_bits cannot be mapped to rcx!"
%endif
        mov     cl, BYTE(r_bits)
        shr     mask, cl ;; e.g. 3 remaining bits -> mask = 00011111
        mov     rcx, tmp

        vmovq   xtmp2, mask

        ;; Get number of full bytes in last block of 16 bytes
        mov     tmp, num_bytes
        dec     tmp
        XVPSLLB xtmp2, tmp, xtmp3, tmp2
        ;; At this point, xtmp2 contains a mask with all 0s, but with some ones
        ;; in the partial byte

        ;; Clear all the bits that do not need to be preserved from the output
        vpand   xtmp, xtmp, xtmp2

        ;; Clear the bits from the input that are not to be ciphered
        vpandn  xdata0, xtmp2, xdata0
        vpor    xdata0, xtmp
%endif

%%store_output:
        ; copy result into the output buffer
        simd_store_avx_15 p_out, xdata0, num_bytes, tmp, rax

        jmp	%%do_return2

%%iv_is_16_bytes:
        ; Read 16 byte IV: Nonce + ESP IV + block counter (BE)
        vmovdqu xcounter, [p_IV]
        jmp     %%bswap_iv
%endmacro

align 32
%ifdef CNTR_CCM_AVX
; IMB_JOB * aes_cntr_ccm_128_avx(IMB_JOB *job)
; arg 1 : job
MKGLOBAL(aes_cntr_ccm_128_avx,function,internal)
aes_cntr_ccm_128_avx:
	DO_CNTR CCM
%else
;; aes_cntr_128_avx(void *in, void *IV, void *keys, void *out, UINT64 num_bytes,
;;                  UINT64 iv_len)
MKGLOBAL(aes_cntr_128_avx,function,internal)
aes_cntr_128_avx:
	DO_CNTR CNTR

;; aes_cntr_bit_128_avx(void *in, void *IV, void *keys, void *out, UINT64 num_bits,
;;                  UINT64 iv_len)
MKGLOBAL(aes_cntr_bit_128_avx,function,internal)
aes_cntr_bit_128_avx:
	DO_CNTR CNTR_BIT
%endif ;; CNTR_CCM_AVX

mksection stack-noexec

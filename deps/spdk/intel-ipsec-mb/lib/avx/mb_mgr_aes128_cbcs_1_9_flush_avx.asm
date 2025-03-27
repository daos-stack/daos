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
%include "include/mb_mgr_datastruct.asm"
%include "include/cet.inc"
%include "include/reg_sizes.asm"

%define NUM_LANES 8

%ifndef AES_CBCS_ENC_X8
%define AES_CBCS_ENC_X8 aes_cbcs_1_9_enc_128_x8
%define FLUSH_JOB_AES_CBCS_ENC flush_job_aes128_cbcs_1_9_enc_avx
%endif

; void aes_cbcs_1_9_enc_128_x8(AES_ARGS *args, UINT64 len_in_bytes);
extern AES_CBCS_ENC_X8

mksection .text

%define APPEND(a,b) a %+ b

%ifdef LINUX
%define arg1	rdi
%define arg2	rsi
%else
%define arg1	rcx
%define arg2	rdx
%endif

%define state	arg1
%define len2	arg2

%define job_rax          rax

%define unused_lanes     rbx
%define tmp1             rbx

%define good_lane        rdx

%define tmp2             rax

; idx needs to be in rbp
%define tmp              rbp
%define idx              rbp

%define tmp3             r8

; STACK_SPACE needs to be an odd multiple of 8
; This routine and its callee clobbers all GPRs
struc STACK
_gpr_save:	resq	8
_rsp_save:	resq	1
endstruc

; JOB* flush_job_aes128_cbcs_1_9_enc_avx(MB_MGR_AES_OOO *state)
; arg 1 : state
MKGLOBAL(FLUSH_JOB_AES_CBCS_ENC,function,internal)
FLUSH_JOB_AES_CBCS_ENC:

        mov	rax, rsp
        sub	rsp, STACK_size
        and	rsp, -16

	mov	[rsp + _gpr_save + 8*0], rbx
	mov	[rsp + _gpr_save + 8*1], rbp
	mov	[rsp + _gpr_save + 8*2], r12
	mov	[rsp + _gpr_save + 8*3], r13
	mov	[rsp + _gpr_save + 8*4], r14
	mov	[rsp + _gpr_save + 8*5], r15
%ifndef LINUX
	mov	[rsp + _gpr_save + 8*6], rsi
	mov	[rsp + _gpr_save + 8*7], rdi
%endif
	mov	[rsp + _rsp_save], rax	; original SP

	; check for empty
	mov	unused_lanes, [state + _aes_unused_lanes]
	bt	unused_lanes, 32+3
	jc	return_null

	; find a lane with a non-null job
	xor	good_lane, good_lane
        mov     tmp3, 1
%assign I 1
%rep 7
	cmp	qword [state + _aes_job_in_lane + I*8], 0
	cmovne	good_lane, tmp3
%if I != 7
        inc     tmp3
%endif
%assign I (I+1)
%endrep

	; copy good_lane to empty lanes
	mov	tmp1, [state + _aes_args_in + good_lane*8]
	mov	tmp2, [state + _aes_args_out + good_lane*8]
	mov	tmp3, [state + _aes_args_keys + good_lane*8]
	shl	good_lane, 4 ; multiply by 16
	vmovdqa	xmm2, [state + _aes_args_IV + good_lane]

%assign I 0
%rep 8
	cmp	qword [state + _aes_job_in_lane + I*8], 0
	jne	APPEND(skip_,I)
	mov	[state + _aes_args_in + I*8], tmp1
	mov	[state + _aes_args_out + I*8], tmp2
	mov	[state + _aes_args_keys + I*8], tmp3
	vmovdqa	[state + _aes_args_IV + I*16], xmm2
        mov     qword [state + _aes_lens_64 + 8*I], 0xffffffffffffffff
APPEND(skip_,I):
%assign I (I+1)
%endrep

	; Find min length
        mov     len2, [state + _aes_lens_64 + 8*0]
        xor     idx, idx
        mov     tmp3, 1
%assign I 1
%rep 7
        cmp     len2, [state + _aes_lens_64 + 8*I]
        cmova   len2, [state + _aes_lens_64 + 8*I]
        cmova   idx, tmp3
%if I != 7
        inc     tmp3
%endif
%assign I (I+1)
%endrep

        or	len2, len2
	jz	len_is_0

        ; Round up to multiple of 16*10
        ; N = (length + 159) / 160 --> Number of 160-byte blocks
        mov     rax, len2
        xor     rdx, rdx ;; zero rdx for div
        add     rax, 159
        mov     tmp1, 160
        div     tmp1
        ; Number of 160-byte blocks in rax
        mov     tmp1, 160
        mul     tmp1
        ; Number of bytes to process in rax
        mov     len2, rax

        xor     tmp1, tmp1
%assign I 0
%rep NUM_LANES
        mov     tmp3, [state + _aes_lens_64 + 8*I]
        sub     tmp3, len2
        cmovs   tmp3, tmp1 ; 0 if negative number
        mov     [state + _aes_lens_64 + 8*I], tmp3
%assign I (I+1)
%endrep

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	AES_CBCS_ENC_X8
	; state and idx are intact

len_is_0:
	; process completed job "idx"
	mov	job_rax, [state + _aes_job_in_lane + idx*8]
	mov	unused_lanes, [state + _aes_unused_lanes]
	mov	qword [state + _aes_job_in_lane + idx*8], 0
	or	dword [job_rax + _status], IMB_STATUS_COMPLETED_CIPHER
	shl	unused_lanes, 4
	or	unused_lanes, idx
	mov	[state + _aes_unused_lanes], unused_lanes

        ;; store last cipher block as next iv
        shl	idx, 3 ; multiply by 8
        mov     tmp1, [job_rax + _cbcs_next_iv]
        vmovdqa xmm0, [state + _aes_args_IV + idx*2]
        vmovdqu [tmp1], xmm0

%ifdef SAFE_DATA
        ;; Clear IVs of returned job and "NULL lanes"
        vpxor   xmm0, xmm0
%assign I 0
%rep 8
	cmp	qword [state + _aes_job_in_lane + I*8], 0
	jne	APPEND(skip_clear_,I)
	vmovdqa	[state + _aes_args_IV + I*16], xmm0
APPEND(skip_clear_,I):
%assign I (I+1)
%endrep
%endif

return:

	mov	rbx, [rsp + _gpr_save + 8*0]
	mov	rbp, [rsp + _gpr_save + 8*1]
	mov	r12, [rsp + _gpr_save + 8*2]
	mov	r13, [rsp + _gpr_save + 8*3]
	mov	r14, [rsp + _gpr_save + 8*4]
	mov	r15, [rsp + _gpr_save + 8*5]
%ifndef LINUX
	mov	rsi, [rsp + _gpr_save + 8*6]
	mov	rdi, [rsp + _gpr_save + 8*7]
%endif
	mov	rsp, [rsp + _rsp_save]	; original SP

        ret

return_null:
	xor	job_rax, job_rax
	jmp	return

mksection stack-noexec

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
%include "include/const.inc"

%define NUM_LANES 4

%ifndef AES_CBCS_ENC_X4
%define AES_CBCS_ENC_X4 aes_cbcs_1_9_enc_128_x4
%define SUBMIT_JOB_AES_CBCS_ENC submit_job_aes128_cbcs_1_9_enc_sse
%endif

; void aes_cbcs_1_9_enc_128_x4(AES_ARGS *args, UINT64 len_in_bytes);
extern AES_CBCS_ENC_X4

%ifdef LINUX
%define arg1	rdi
%define arg2	rsi
%else
%define arg1	rcx
%define arg2	rdx
%endif

%define state	arg1
%define job	arg2
%define len2	arg2

%define job_rax          rax

; idx needs to be in rbp
%define len              rbp
%define idx              rbp
%define tmp              rbp

%define lane             r8
%define tmp2             r8

%define iv               r9
%define tmp3             r9

%define unused_lanes     rbx

; STACK_SPACE needs to be an odd multiple of 8
; This routine and its callee clobbers all GPRs
struc STACK
_gpr_save:	resq	8
_rsp_save:	resq	1
endstruc

mksection .text

; JOB* submit_job_aes128_cbcs_1_9_enc_sse(MB_MGR_AES_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_AES_CBCS_ENC,function,internal)
SUBMIT_JOB_AES_CBCS_ENC:

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

	mov	unused_lanes, [state + _aes_unused_lanes]
	mov	lane, unused_lanes
        and     lane, 0xf
	shr	unused_lanes, 4
	mov	iv, [job + _iv]
	mov	[state + _aes_unused_lanes], unused_lanes

	mov	[state + _aes_job_in_lane + lane*8], job
	mov	tmp, [job + _src]
	add	tmp, [job + _cipher_start_src_offset_in_bytes]
	movdqu	xmm0, [iv]
	mov	[state + _aes_args_in + lane*8], tmp
	mov	tmp, [job + _enc_keys]
	mov	[state + _aes_args_keys + lane*8], tmp
	mov	tmp, [job + _dst]
	mov	[state + _aes_args_out + lane*8], tmp
	shl	lane, 4	; multiply by 16
	movdqa	[state + _aes_args_IV + lane], xmm0

        ;; insert len into proper lane
        mov     len, [job + _msg_len_to_cipher_in_bytes]
        and     len, -16        ; Buffer might not be aligned to block size

        shr     lane, 4
        mov     [state + _aes_lens_64 + lane*8], len

	cmp	unused_lanes, 0xf
	jne	return_null

	; Find min length
        mov     len2, len
        mov     idx, lane
        xor     tmp2, tmp2
        cmp     len2, [state + _aes_lens_64 + 8*0]
        cmova   len2, [state + _aes_lens_64 + 8*0]
        cmova   idx, tmp2
        inc     tmp2

        cmp     len2, [state + _aes_lens_64 + 8*1]
        cmova   len2, [state + _aes_lens_64 + 8*1]
        cmova   idx, tmp2
        inc     tmp2

        cmp     len2, [state + _aes_lens_64 + 8*2]
        cmova   len2, [state + _aes_lens_64 + 8*2]
        cmova   idx, tmp2
        inc     tmp2

        cmp     len2, [state + _aes_lens_64 + 8*3]
        cmova   len2, [state + _aes_lens_64 + 8*3]
        cmova   idx, tmp2

        cmp	len2, 0
	je	len_is_0

        ; Round up to multiple of 16*10
        ; N = (length + 159) / 160 --> Number of 160-byte blocks
        mov     rax, len2
        xor     rdx, rdx ;; zero rdx for div
        add     rax, 159
        mov     tmp2, 160
        div     tmp2
        ; Number of 160-byte blocks in rax
        mov     tmp2, 160
        mul     tmp2
        ; Number of bytes to process in rax
        mov     len2, rax

        xor     tmp2, tmp2
%assign I 0
%rep NUM_LANES
        mov     tmp3, [state + _aes_lens_64 + 8*I]
        sub     tmp3, len2
        cmovs   tmp3, tmp2 ; 0 if negative number
        mov     [state + _aes_lens_64 + 8*I], tmp3
%assign I (I+1)
%endrep

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	AES_CBCS_ENC_X4
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

        ;; store last cipher block as next_iv
        shl     idx, 3 ; multiply by 8
        mov     tmp2, [job_rax + _cbcs_next_iv]
        movdqa  xmm0, [state + _aes_args_IV + idx*2]
        movdqu  [tmp2], xmm0

%ifdef SAFE_DATA
        ;; clear key pointers
        mov     qword [state + _aes_args_keys + idx], 0
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


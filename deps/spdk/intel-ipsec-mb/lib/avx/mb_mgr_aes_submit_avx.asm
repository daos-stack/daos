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
%include "include/mb_mgr_datastruct.asm"
%include "include/cet.inc"
%include "include/reg_sizes.asm"
%include "include/const.inc"

%ifndef AES_CBC_ENC_X8
%define AES_CBC_ENC_X8 aes_cbc_enc_128_x8
%define SUBMIT_JOB_AES_ENC submit_job_aes128_enc_avx
%endif

; void AES_CBC_ENC_X8(AES_ARGS *args, UINT64 len_in_bytes);
extern AES_CBC_ENC_X8

mksection .rodata
default rel

align 16
dupw:
	;ddq 0x01000100010001000100010001000100
	dq 0x0100010001000100, 0x0100010001000100

mksection .text

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

%if 1
; idx needs to be in rbp
%define len              rbp
%define idx              rbp
%define tmp              rbp

%define lane             r8

%define iv               r9

%define unused_lanes     rbx
%endif

; STACK_SPACE needs to be an odd multiple of 8
; This routine and its callee clobbers all GPRs
struc STACK
_gpr_save:	resq	8
_rsp_save:	resq	1
endstruc

; JOB* SUBMIT_JOB_AES_ENC(MB_MGR_AES_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_AES_ENC,function,internal)
SUBMIT_JOB_AES_ENC:

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
	and	lane, 0xF
	shr	unused_lanes, 4
	mov	len, [job + _msg_len_to_cipher_in_bytes]
	and	len, -16		; DOCSIS may pass size unaligned to block size
	mov	iv, [job + _iv]
	mov	[state + _aes_unused_lanes], unused_lanes

	mov	[state + _aes_job_in_lane + lane*8], job

        vmovdqa xmm0, [state + _aes_lens]
        XVPINSRW xmm0, xmm1, tmp, lane, len, scale_x16
        vmovdqa [state + _aes_lens], xmm0

	mov	tmp, [job + _src]
	add	tmp, [job + _cipher_start_src_offset_in_bytes]
	vmovdqu	xmm0, [iv]
	mov	[state + _aes_args_in + lane*8], tmp
	mov	tmp, [job + _enc_keys]
	mov	[state + _aes_args_keys + lane*8], tmp
	mov	tmp, [job + _dst]
	mov	[state + _aes_args_out + lane*8], tmp
	shl	lane, 4	; multiply by 16
	vmovdqa	[state + _aes_args_IV + lane], xmm0

	cmp	unused_lanes, 0xf
	jne	return_null

	; Find min length
	vmovdqa	xmm0, [state + _aes_lens]
	vphminposuw	xmm1, xmm0
	vpextrw	DWORD(len2), xmm1, 0	; min value
	vpextrw	DWORD(idx), xmm1, 1	; min index (0...7)
	cmp	len2, 0
	je	len_is_0

	vpshufb	xmm1, xmm1, [rel dupw]   ; duplicate words across all lanes
	vpsubw	xmm0, xmm0, xmm1
	vmovdqa	[state + _aes_lens], xmm0

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	AES_CBC_ENC_X8
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
%ifdef SAFE_DATA
        ;; Clear IV
        vpxor   xmm0, xmm0
        shl	idx, 3 ; multiply by 8
        vmovdqa [state + _aes_args_IV + idx*2], xmm0
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

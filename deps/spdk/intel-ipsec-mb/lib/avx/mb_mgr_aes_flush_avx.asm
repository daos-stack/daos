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

%ifndef AES_CBC_ENC_X8
%define AES_CBC_ENC_X8 aes_cbc_enc_128_x8
%define FLUSH_JOB_AES_ENC flush_job_aes128_enc_avx
%endif

; void AES_CBC_ENC_X8(AES_ARGS *args, UINT64 len_in_bytes);
extern AES_CBC_ENC_X8

mksection .rodata
default rel
align 16
len_masks:
	;ddq 0x0000000000000000000000000000FFFF
	dq 0x000000000000FFFF, 0x0000000000000000
	;ddq 0x000000000000000000000000FFFF0000
	dq 0x00000000FFFF0000, 0x0000000000000000
	;ddq 0x00000000000000000000FFFF00000000
	dq 0x0000FFFF00000000, 0x0000000000000000
	;ddq 0x0000000000000000FFFF000000000000
	dq 0xFFFF000000000000, 0x0000000000000000
	;ddq 0x000000000000FFFF0000000000000000
	dq 0x0000000000000000, 0x000000000000FFFF
	;ddq 0x00000000FFFF00000000000000000000
	dq 0x0000000000000000, 0x00000000FFFF0000
	;ddq 0x0000FFFF000000000000000000000000
	dq 0x0000000000000000, 0x0000FFFF00000000
	;ddq 0xFFFF0000000000000000000000000000
	dq 0x0000000000000000, 0xFFFF000000000000
dupw:
	;ddq 0x01000100010001000100010001000100
	dq 0x0100010001000100, 0x0100010001000100
one:	dq  1
two:	dq  2
three:	dq  3
four:	dq  4
five:	dq  5
six:	dq  6
seven:	dq  7

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
%define job	arg2
%define len2	arg2

%define job_rax          rax

%if 1
%define unused_lanes     rbx
%define tmp1             rbx

%define good_lane        rdx
%define iv               rdx

%define tmp2             rax

; idx needs to be in rbp
%define tmp              rbp
%define idx              rbp

%define tmp3             r8
%endif

; STACK_SPACE needs to be an odd multiple of 8
; This routine and its callee clobbers all GPRs
struc STACK
_gpr_save:	resq	8
_rsp_save:	resq	1
endstruc

; JOB* FLUSH_JOB_AES_ENC(MB_MGR_AES_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(FLUSH_JOB_AES_ENC,function,internal)
FLUSH_JOB_AES_ENC:

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
	cmp	qword [state + _aes_job_in_lane + 1*8], 0
	cmovne	good_lane, [rel one]
	cmp	qword [state + _aes_job_in_lane + 2*8], 0
	cmovne	good_lane, [rel two]
	cmp	qword [state + _aes_job_in_lane + 3*8], 0
	cmovne	good_lane, [rel three]
	cmp	qword [state + _aes_job_in_lane + 4*8], 0
	cmovne	good_lane, [rel four]
	cmp	qword [state + _aes_job_in_lane + 5*8], 0
	cmovne	good_lane, [rel five]
	cmp	qword [state + _aes_job_in_lane + 6*8], 0
	cmovne	good_lane, [rel six]
	cmp	qword [state + _aes_job_in_lane + 7*8], 0
	cmovne	good_lane, [rel seven]

	; copy good_lane to empty lanes
	mov	tmp1, [state + _aes_args_in + good_lane*8]
	mov	tmp2, [state + _aes_args_out + good_lane*8]
	mov	tmp3, [state + _aes_args_keys + good_lane*8]
	shl	good_lane, 4 ; multiply by 16
	vmovdqa	xmm2, [state + _aes_args_IV + good_lane]
	vmovdqa	xmm0, [state + _aes_lens]

%assign I 0
%rep 8
	cmp	qword [state + _aes_job_in_lane + I*8], 0
	jne	APPEND(skip_,I)
	mov	[state + _aes_args_in + I*8], tmp1
	mov	[state + _aes_args_out + I*8], tmp2
	mov	[state + _aes_args_keys + I*8], tmp3
	vmovdqa	[state + _aes_args_IV + I*16], xmm2
	vpor	xmm0, xmm0, [rel len_masks + 16*I]
APPEND(skip_,I):
%assign I (I+1)
%endrep

	; Find min length
	vphminposuw	xmm1, xmm0
	vpextrw	DWORD(len2), xmm1, 0	; min value
	vpextrw	DWORD(idx), xmm1, 1	; min index (0...3)
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

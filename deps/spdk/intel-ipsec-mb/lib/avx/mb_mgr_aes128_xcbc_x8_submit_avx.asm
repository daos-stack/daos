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

%include "include/memcpy.asm"
%include "include/const.inc"

%ifndef AES_XCBC_X8
%define AES_XCBC_X8 aes_xcbc_mac_128_x8
%define SUBMIT_JOB_AES_XCBC submit_job_aes_xcbc_avx
%endif

; void AES_XCBC_X8(AES_XCBC_ARGS_x16 *args, UINT64 len_in_bytes);
extern AES_XCBC_X8

mksection .rodata
default rel

align 16
dupw:	;ddq 0x01000100010001000100010001000100
	dq 0x0100010001000100, 0x0100010001000100
x80:    ;ddq 0x00000000000000000000000000000080
        dq 0x0000000000000080, 0x0000000000000000

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
%define len              r11
%define idx              rbp
%define tmp2             rbp
%define tmp              r14

%define lane             r8
%define icv              r9
%define p2               r9

%define last_len         r10

%define lane_data        r12
%define p                r13

%define unused_lanes     rbx
%endif

; STACK_SPACE needs to be an odd multiple of 8
; This routine and its callee clobbers all GPRs
struc STACK
_gpr_save:	resq	8
_rsp_save:	resq	1
endstruc

; JOB* SUBMIT_JOB_AES_XCBC(MB_MGR_AES_XCBC_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_AES_XCBC,function,internal)
SUBMIT_JOB_AES_XCBC:

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

	mov	unused_lanes, [state + _aes_xcbc_unused_lanes]
	mov	lane, unused_lanes
	and	lane, 0xF
	shr	unused_lanes, 4
	imul	lane_data, lane, _XCBC_LANE_DATA_size
	lea	lane_data, [state + _aes_xcbc_ldata + lane_data]
	mov	len, [job + _msg_len_to_hash_in_bytes]
	mov	[state + _aes_xcbc_unused_lanes], unused_lanes
	mov	[lane_data + _xcbc_job_in_lane], job
	mov	dword [lane_data + _xcbc_final_done], 0
	mov	tmp, [job + _k1_expanded]
	mov	[state + _aes_xcbc_args_keys + lane*8], tmp
	mov	p, [job + _src]
	add	p, [job + _hash_start_src_offset_in_bytes]

	mov	last_len, len

	cmp	len, 16
	jle	small_buffer

	mov	[state + _aes_xcbc_args_in + lane*8], p
	add	p, len		; set point to end of data

	and	last_len, 15	; Check lsbs of msg len
	jnz	slow_copy	; if not 16B mult, do slow copy

fast_copy:
	vmovdqu	xmm0, [p - 16]	; load last block M[n]
        mov     tmp, [job + _k2] ; load K2 address
        vmovdqu xmm1, [tmp]     ; load K2
        vpxor   xmm0, xmm0, xmm1      ; M[n] XOR K2
	vmovdqa	[lane_data + _xcbc_final_block], xmm0
	sub	len, 16		; take last block off length
end_fast_copy:
	vpxor	xmm0, xmm0, xmm0
	shl	lane, 4	; multiply by 16
	vmovdqa	[state + _aes_xcbc_args_ICV + lane], xmm0

        vmovdqa xmm0, [state + _aes_xcbc_lens]
        XVPINSRW xmm0, xmm1, tmp, lane, len, no_scale
        vmovdqa [state + _aes_xcbc_lens], xmm0

	cmp	unused_lanes, 0xf
	jne	return_null

start_loop:
	; Find min length
	vphminposuw	xmm1, xmm0
	vpextrw	DWORD(len2), xmm1, 0	; min value
	vpextrw	DWORD(idx), xmm1, 1	; min index (0...7)
	cmp	len2, 0
	je	len_is_0

	vpshufb	xmm1, xmm1, [rel dupw]   ; duplicate words across all lanes
	vpsubw	xmm0, xmm0, xmm1
	vmovdqa	[state + _aes_xcbc_lens], xmm0

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	AES_XCBC_X8
	; state and idx are intact

len_is_0:
	; process completed job "idx"
	imul	lane_data, idx, _XCBC_LANE_DATA_size
	lea	lane_data, [state + _aes_xcbc_ldata + lane_data]
	cmp	dword [lane_data + _xcbc_final_done], 0
	jne	end_loop

	mov	dword [lane_data + _xcbc_final_done], 1

        vmovdqa xmm0, [state + _aes_xcbc_lens]
        XVPINSRW xmm0, xmm1, tmp, idx, 16, scale_x16
        vmovdqa [state + _aes_xcbc_lens], xmm0

	lea	tmp, [lane_data + _xcbc_final_block]
	mov	[state + _aes_xcbc_args_in + 8*idx], tmp
	jmp	start_loop

end_loop:
	; process completed job "idx"
	mov	job_rax, [lane_data + _xcbc_job_in_lane]
	mov	icv, [job_rax + _auth_tag_output]
	mov	unused_lanes, [state + _aes_xcbc_unused_lanes]
	mov	qword [lane_data + _xcbc_job_in_lane], 0
	or	dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
	shl	unused_lanes, 4
	or	unused_lanes, idx
	shl	idx, 4 ; multiply by 16
	mov	[state + _aes_xcbc_unused_lanes], unused_lanes

	; copy 12 bytes
	vmovdqa	xmm0, [state + _aes_xcbc_args_ICV + idx]
	vmovq	[icv], xmm0
	vpextrd	[icv + 8], xmm0, 2

%ifdef SAFE_DATA
        ;; Clear ICV
        vpxor   xmm0, xmm0
        vmovdqa [state + _aes_xcbc_args_ICV + idx], xmm0

        ;; Clear final block (32 bytes)
        vmovdqa [lane_data + _xcbc_final_block], xmm0
        vmovdqa [lane_data + _xcbc_final_block + 16], xmm0
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

small_buffer:
	; For buffers <= 16 Bytes
	; The input data is set to final block
	lea	tmp, [lane_data + _xcbc_final_block] ; final block
	mov	[state + _aes_xcbc_args_in + lane*8], tmp
	add	p, len		; set point to end of data
	cmp	len, 16
	je	fast_copy

slow_copy:
	and	len, ~15	; take final block off len
	sub	p, last_len	; adjust data pointer
	lea	p2, [lane_data + _xcbc_final_block + 16] ; upper part of final
	sub	p2, last_len	; adjust data pointer backwards
	memcpy_avx_16_1 p2, p, last_len, tmp, tmp2
        vmovdqa	xmm0, [rel x80]	; fill reg with padding
	vmovdqu	[lane_data + _xcbc_final_block + 16], xmm0 ; add padding
	vmovdqu	xmm0, [p2]	; load final block to process
	mov	tmp, [job + _k3] ; load K3 address
	vmovdqu	xmm1, [tmp]	; load K3
	vpxor	xmm0, xmm0, xmm1	; M[n] XOR K3
	vmovdqu	[lane_data + _xcbc_final_block], xmm0	; write final block
	jmp	end_fast_copy

return_null:
	xor	job_rax, job_rax
	jmp	return

mksection stack-noexec

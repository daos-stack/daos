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
%include "include/reg_sizes.asm"

extern md5_x4x2_avx

mksection .rodata
default rel
align 16
dupw:	;ddq 0x01000100010001000100010001000100
	dq 0x0100010001000100, 0x0100010001000100
x80:    ;ddq 0x00000000000000000000000000000080
        dq 0x0000000000000080, 0x0000000000000000
x00:    ;ddq 0x00000000000000000000000000000000
        dq 0x0000000000000000, 0x0000000000000000
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
one:	dq  1
two:	dq  2
three:	dq  3
four:	dq  4
five:	dq  5
six:	dq  6
seven:	dq  7

mksection .text

%if 1
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

; idx needs to be in rbp
%define idx             rbp

; unused_lanes must be in rax-rdx
%define unused_lanes    rbx
%define lane_data       rbx
%define tmp2		rbx

%define job_rax         rax
%define	tmp1		rax
%define size_offset     rax
%define tmp             rax
%define start_offset    rax

%define tmp3		arg1

%define extra_blocks    arg2
%define p               arg2

%define tmp4		r8
%define tmp5		r9

%endif

; This routine and/or the called routine clobbers all GPRs
struc STACK
_gpr_save:	resq	8
_rsp_save:	resq	1
endstruc

%define APPEND(a,b) a %+ b

; JOB* flush_job_hmac_md5_avx(MB_MGR_HMAC_MD5_OOO *state)
; arg 1 : rcx : state
MKGLOBAL(flush_job_hmac_md5_avx,function,internal)
flush_job_hmac_md5_avx:

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

	mov	unused_lanes, [state + _unused_lanes_md5]
	bt	unused_lanes, 32+3
	jc	return_null

	; find a lane with a non-null job
	xor	idx, idx
	cmp	qword [state + _ldata_md5 + 1 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane],0
	cmovne	idx, [rel one]
	cmp	qword [state + _ldata_md5 + 2 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane],0
	cmovne	idx, [rel two]
	cmp	qword [state + _ldata_md5 + 3 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane],0
	cmovne	idx, [rel three]
	cmp	qword [state + _ldata_md5 + 4 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane],0
	cmovne	idx, [rel four]
	cmp	qword [state + _ldata_md5 + 5 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane],0
	cmovne	idx, [rel five]
	cmp	qword [state + _ldata_md5 + 6 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane],0
	cmovne	idx, [rel six]
	cmp	qword [state + _ldata_md5 + 7 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane],0
	cmovne	idx, [rel seven]

copy_lane_data:
	; copy good lane (idx) to empty lanes
	vmovdqa	xmm0, [state + _lens_md5]
	mov	tmp, [state + _args_data_ptr_md5 + PTR_SZ*idx]

%assign I 0
%rep 8
	cmp	qword [state + _ldata_md5 + I * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
	jne	APPEND(skip_,I)
	mov	[state + _args_data_ptr_md5 + PTR_SZ*I], tmp
	vpor	xmm0, xmm0, [rel len_masks + 16*I]
APPEND(skip_,I):
%assign I (I+1)
%endrep

	vmovdqa	[state + _lens_md5], xmm0

	vphminposuw	xmm1, xmm0
	vpextrw	DWORD(len2), xmm1, 0	; min value
	vpextrw	DWORD(idx), xmm1, 1	; min index (0...3)
	cmp	len2, 0
	je	len_is_0

	vpshufb	xmm1, [rel dupw]	; duplicate words across all lanes
	vpsubw	xmm0, xmm0, xmm1
	vmovdqa	[state + _lens_md5], xmm0

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	md5_x4x2_avx
	; state and idx are intact

len_is_0:
	; process completed job "idx"
	imul	lane_data, idx, _HMAC_SHA1_LANE_DATA_size
	lea	lane_data, [state + _ldata_md5 + lane_data]
	mov	DWORD(extra_blocks), [lane_data + _extra_blocks]
	cmp	extra_blocks, 0
	jne	proc_extra_blocks
	cmp	dword [lane_data + _outer_done], 0
	jne	end_loop

proc_outer:
	mov	dword [lane_data + _outer_done], 1
	mov	DWORD(size_offset), [lane_data + _size_offset]
	mov	qword [lane_data + _extra_block + size_offset], 0
	mov	word [state + _lens_md5 + 2*idx], 1
	lea	tmp, [lane_data + _outer_block]
	mov	job, [lane_data + _job_in_lane]
	mov	[state + _args_data_ptr_md5 + PTR_SZ*idx], tmp

	vmovd	xmm0, [state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 0*MD5_DIGEST_ROW_SIZE]
	vpinsrd	xmm0, [state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 1*MD5_DIGEST_ROW_SIZE], 1
	vpinsrd	xmm0, [state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 2*MD5_DIGEST_ROW_SIZE], 2
	vpinsrd	xmm0, [state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 3*MD5_DIGEST_ROW_SIZE], 3
	vmovdqa	[lane_data + _outer_block], xmm0

	mov	tmp, [job + _auth_key_xor_opad]
	vmovdqu	xmm0, [tmp]
	vmovd	[state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 0*MD5_DIGEST_ROW_SIZE], xmm0
	vpextrd	[state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 1*MD5_DIGEST_ROW_SIZE], xmm0, 1
	vpextrd	[state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 2*MD5_DIGEST_ROW_SIZE], xmm0, 2
	vpextrd	[state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 3*MD5_DIGEST_ROW_SIZE], xmm0, 3
	jmp	copy_lane_data

	align	16
proc_extra_blocks:
	mov	DWORD(start_offset), [lane_data + _start_offset]
	mov	[state + _lens_md5 + 2*idx], WORD(extra_blocks)
	lea	tmp, [lane_data + _extra_block + start_offset]
	mov	[state + _args_data_ptr_md5 + PTR_SZ*idx], tmp
	mov	dword [lane_data + _extra_blocks], 0
	jmp	copy_lane_data

return_null:
	xor	job_rax, job_rax
	jmp	return

	align	16
end_loop:
	mov	job_rax, [lane_data + _job_in_lane]
	mov	qword [lane_data + _job_in_lane], 0
	or	dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
	mov	unused_lanes, [state + _unused_lanes_md5]
	shl	unused_lanes, 4
	or	unused_lanes, idx
	mov	[state + _unused_lanes_md5], unused_lanes

	mov	p, [job_rax + _auth_tag_output]

	; copy 12 bytes
	mov	DWORD(tmp2), [state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 0*MD5_DIGEST_ROW_SIZE]
	mov	DWORD(tmp4), [state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 1*MD5_DIGEST_ROW_SIZE]
	mov	DWORD(tmp5), [state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 2*MD5_DIGEST_ROW_SIZE]
;	bswap	DWORD(tmp2)
;	bswap	DWORD(tmp4)
;	bswap	DWORD(tmp3)
	mov	[p + 0*4], DWORD(tmp2)
	mov	[p + 1*4], DWORD(tmp4)
	mov	[p + 2*4], DWORD(tmp5)

        cmp     DWORD [job_rax + _auth_tag_output_len_in_bytes], 12
        je      clear_ret

        ; copy 16 bytes
        mov	DWORD(tmp5), [state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 3*MD5_DIGEST_ROW_SIZE]
        mov	[p + 3*4], DWORD(tmp5)

clear_ret:

%ifdef SAFE_DATA
        vpxor   xmm0, xmm0

        ;; Clear digest (16B), outer_block (16B) and extra_block (64B)
        ;; of returned job and NULL jobs
%assign I 0
%rep 8
	cmp	qword [state + _ldata_md5 + (I*_HMAC_SHA1_LANE_DATA_size) + _job_in_lane], 0
	jne	APPEND(skip_clear_,I)

        ;; Clear digest (16 bytes)
%assign J 0
%rep 4
        mov     dword [state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*I + J*MD5_DIGEST_ROW_SIZE], 0
%assign J (J+1)
%endrep

        lea     lane_data, [state + _ldata_md5 + (I*_HMAC_SHA1_LANE_DATA_size)]
        ;; Clear first 64 bytes of extra_block
%assign offset 0
%rep 4
        vmovdqa [lane_data + _extra_block + offset], xmm0
%assign offset (offset + 16)
%endrep

        ;; Clear first 16 bytes of outer_block
        vmovdqa [lane_data + _outer_block], xmm0

APPEND(skip_clear_,I):
%assign I (I+1)
%endrep

%endif ;; SAFE_DATA

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

mksection stack-noexec

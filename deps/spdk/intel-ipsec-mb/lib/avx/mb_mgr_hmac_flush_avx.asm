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

extern sha1_mult_avx

mksection .rodata
default rel

align 16
byteswap:	;ddq 0x0c0d0e0f08090a0b0405060700010203
	dq 0x0405060700010203, 0x0c0d0e0f08090a0b
x80:		;ddq 0x00000000000000000000000000000080
	dq 0x0000000000000080, 0x0000000000000000
x00:		;ddq 0x00000000000000000000000000000000
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
one:	dq  1
two:	dq  2
three:	dq  3

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

; idx needs to be in rbx, rbp, r12-r15
%define idx             rbp

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

%endif

; This routine clobbers rbx, rbp
struc STACK
_gpr_save:	resq	2
_rsp_save:	resq	1
endstruc

%define APPEND(a,b) a %+ b

; JOB* flush_job_hmac_avx(MB_MGR_HMAC_SHA_1_OOO *state)
; arg 1 : rcx : state
MKGLOBAL(flush_job_hmac_avx,function,internal)
flush_job_hmac_avx:

        mov	rax, rsp
        sub	rsp, STACK_size
        and	rsp, -16

	mov	[rsp + _gpr_save + 8*0], rbx
	mov	[rsp + _gpr_save + 8*1], rbp
	mov	[rsp + _rsp_save], rax	; original SP

	mov	unused_lanes, [state + _unused_lanes]
	bt	unused_lanes, 32+7
	jc	return_null

	; find a lane with a non-null job
	xor	idx, idx
	cmp	qword [state + _ldata + 1 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [rel one]
	cmp	qword [state + _ldata + 2 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [rel two]
	cmp	qword [state + _ldata + 3 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [rel three]

copy_lane_data:
	; copy valid lane (idx) to empty lanes
	vmovdqa	xmm0, [state + _lens]
	mov	tmp, [state + _args_data_ptr + PTR_SZ*idx]

%assign I 0
%rep 4
	cmp	qword [state + _ldata + I * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
	jne	APPEND(skip_,I)
	mov	[state + _args_data_ptr + PTR_SZ*I], tmp
	vpor	xmm0, xmm0, [rel len_masks + 16*I]
APPEND(skip_,I):
%assign I (I+1)
%endrep

	vmovdqa	[state + _lens], xmm0

	vphminposuw	xmm1, xmm0
	vpextrw	DWORD(len2), xmm1, 0	; min value
	vpextrw	DWORD(idx), xmm1, 1	; min index (0...3)
	cmp	len2, 0
	je	len_is_0

	vpshuflw xmm1, xmm1, 0
	vpsubw	xmm0, xmm0, xmm1
	vmovdqa	[state + _lens], xmm0

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	sha1_mult_avx
	; state is intact

len_is_0:
	; process completed job "idx"
	imul	lane_data, idx, _HMAC_SHA1_LANE_DATA_size
	lea	lane_data, [state + _ldata + lane_data]
	mov	DWORD(extra_blocks), [lane_data + _extra_blocks]
	cmp	extra_blocks, 0
	jne	proc_extra_blocks
	cmp	dword [lane_data + _outer_done], 0
	jne	end_loop

proc_outer:
	mov	dword [lane_data + _outer_done], 1
	mov	DWORD(size_offset), [lane_data + _size_offset]
	mov	qword [lane_data + _extra_block + size_offset], 0
	mov	word [state + _lens + 2*idx], 1
	lea	tmp, [lane_data + _outer_block]
	mov	job, [lane_data + _job_in_lane]
	mov	[state + _args_data_ptr + PTR_SZ*idx], tmp

        ;; idx determines which column
        ;; read off from consecutive rows
	vmovd	xmm0, [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 0*SHA1_DIGEST_ROW_SIZE]
	vpinsrd	xmm0, xmm0, [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 1*SHA1_DIGEST_ROW_SIZE], 1
	vpinsrd	xmm0, xmm0, [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 2*SHA1_DIGEST_ROW_SIZE], 2
	vpinsrd	xmm0, xmm0, [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 3*SHA1_DIGEST_ROW_SIZE], 3
	vpshufb	xmm0, xmm0, [rel byteswap]
	mov	DWORD(tmp),  [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 4*SHA1_DIGEST_ROW_SIZE]
	bswap	DWORD(tmp)
	vmovdqa	[lane_data + _outer_block], xmm0
	mov	[lane_data + _outer_block + 4*4], DWORD(tmp)

	mov	tmp, [job + _auth_key_xor_opad]
	vmovdqu	xmm0, [tmp]
	mov	DWORD(tmp),  [tmp + 4*4]
	vmovd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 0*SHA1_DIGEST_ROW_SIZE], xmm0
	vpextrd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 1*SHA1_DIGEST_ROW_SIZE], xmm0, 1
	vpextrd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 2*SHA1_DIGEST_ROW_SIZE], xmm0, 2
	vpextrd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 3*SHA1_DIGEST_ROW_SIZE], xmm0, 3
	mov	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 4*SHA1_DIGEST_ROW_SIZE], DWORD(tmp)
	jmp	copy_lane_data

	align	16
proc_extra_blocks:
	mov	DWORD(start_offset), [lane_data + _start_offset]
	mov	[state + _lens + 2*idx], WORD(extra_blocks)
	lea	tmp, [lane_data + _extra_block + start_offset]
	mov	[state + _args_data_ptr + PTR_SZ*idx], tmp
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
	mov	unused_lanes, [state + _unused_lanes]
	shl	unused_lanes, 8
	or	unused_lanes, idx
	mov	[state + _unused_lanes], unused_lanes

	mov	p, [job_rax + _auth_tag_output]

	; copy 12 bytes
	mov	DWORD(tmp2), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 0*SHA1_DIGEST_ROW_SIZE]
	mov	DWORD(tmp4), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 1*SHA1_DIGEST_ROW_SIZE]
	bswap	DWORD(tmp2)
	bswap	DWORD(tmp4)
	mov	[p + 0*4], DWORD(tmp2)
	mov	[p + 1*4], DWORD(tmp4)
	mov	DWORD(tmp2), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 2*SHA1_DIGEST_ROW_SIZE]
	bswap	DWORD(tmp2)
	mov	[p + 2*4], DWORD(tmp2)

        cmp     qword [job_rax + _auth_tag_output_len_in_bytes], 12
        je      clear_ret

        ;; copy remaining 8 bytes to return 20 byte digest
        mov	DWORD(tmp2),  [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 3*SHA1_DIGEST_ROW_SIZE]
        mov	DWORD(tmp4), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 4*SHA1_DIGEST_ROW_SIZE]
        bswap	DWORD(tmp2)
        bswap	DWORD(tmp4)
        mov	[p + 3*SHA1_DIGEST_WORD_SIZE], DWORD(tmp2)
        mov	[p + 4*SHA1_DIGEST_WORD_SIZE], DWORD(tmp4)

clear_ret:

%ifdef SAFE_DATA
        vpxor   xmm0, xmm0

        ;; Clear digest (20B), outer_block (20B) and extra_block (64B)
        ;; of returned job and NULL jobs
%assign I 0
%rep 4
	cmp	qword [state + _ldata + (I*_HMAC_SHA1_LANE_DATA_size) + _job_in_lane], 0
	jne	APPEND(skip_clear_,I)

        ;; Clear digest
        mov     dword [state + _args_digest + SHA1_DIGEST_WORD_SIZE*I + 0*SHA1_DIGEST_ROW_SIZE], 0
        mov     dword [state + _args_digest + SHA1_DIGEST_WORD_SIZE*I + 1*SHA1_DIGEST_ROW_SIZE], 0
        mov     dword [state + _args_digest + SHA1_DIGEST_WORD_SIZE*I + 2*SHA1_DIGEST_ROW_SIZE], 0
        mov     dword [state + _args_digest + SHA1_DIGEST_WORD_SIZE*I + 3*SHA1_DIGEST_ROW_SIZE], 0
        mov     dword [state + _args_digest + SHA1_DIGEST_WORD_SIZE*I + 4*SHA1_DIGEST_ROW_SIZE], 0

        lea     lane_data, [state + _ldata + (I*_HMAC_SHA1_LANE_DATA_size)]
        ;; Clear first 64 bytes of extra_block
%assign offset 0
%rep 4
        vmovdqa [lane_data + _extra_block + offset], xmm0
%assign offset (offset + 16)
%endrep

        ;; Clear first 20 bytes of outer_block
        vmovdqa [lane_data + _outer_block], xmm0
        mov     dword [lane_data + _outer_block + 16], 0

APPEND(skip_clear_,I):
%assign I (I+1)
%endrep

%endif ;; SAFE_DATA

return:

	mov	rbx, [rsp + _gpr_save + 8*0]
	mov	rbp, [rsp + _gpr_save + 8*1]
	mov	rsp, [rsp + _rsp_save]	; original SP

	ret

mksection stack-noexec

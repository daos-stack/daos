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
%include "include/memcpy.asm"
%include "include/const.inc"

extern sha512_x2_sse

mksection .rodata
default rel
align 16
byteswap:	;ddq 0x08090a0b0c0d0e0f0001020304050607
	dq 0x0001020304050607, 0x08090a0b0c0d0e0f

mksection .text

%ifndef FUNC
%define FUNC submit_job_hmac_sha_512_sse
%define SHA_X_DIGEST_SIZE 512
%endif

%if 1
%ifdef LINUX
%define arg1	rdi
%define arg2	rsi
%define reg3	rcx
%define reg4	rdx
%else
%define arg1	rcx
%define arg2	rdx
%define reg3	rdi
%define reg4	rsi
%endif

%define state	arg1
%define job	arg2
%define len2	arg2

; idx needs to be in rbx, rbp, r12-r15
%define last_len	rbp
%define idx		rbp

%define p		r11
%define start_offset	r11

%define unused_lanes	rbx
%define tmp4		rbx

%define job_rax		rax
%define len		rax

%define size_offset	reg3
%define tmp2		reg3

%define lane		reg4
%define tmp3		reg4

%define extra_blocks	r8

%define tmp		r9
%define p2		r9

%define lane_data	r10

%endif

; This routine clobbers rbx, rbp, rsi, rdi
struc STACK
_gpr_save:	resq	4
_rsp_save:	resq	1
endstruc

; JOB* FUNC(MB_MGR_HMAC_SHA_512_OOO *state, IMB_JOB *job)
; arg 1 : rcx : state
; arg 2 : rdx : job
MKGLOBAL(FUNC,function,internal)
FUNC:

	mov	rax, rsp
	sub	rsp, STACK_size
	and	rsp, -16

	mov	[rsp + _gpr_save + 8*0], rbx
	mov	[rsp + _gpr_save + 8*1], rbp
%ifndef LINUX
	mov	[rsp + _gpr_save + 8*2], rsi
	mov	[rsp + _gpr_save + 8*3], rdi
%endif
	mov	[rsp + _rsp_save], rax	; original SP

	mov	unused_lanes, [state + _unused_lanes_sha512]
	movzx	lane, BYTE(unused_lanes)
	shr	unused_lanes, 8
	imul	lane_data, lane, _SHA512_LANE_DATA_size
	lea	lane_data, [state + _ldata_sha512+ lane_data]
	mov	[state + _unused_lanes_sha512], unused_lanes
	mov	len, [job + _msg_len_to_hash_in_bytes]
	mov	tmp, len
	shr	tmp, 7	; divide by 128, len in terms of sha512 blocks

	mov	[lane_data + _job_in_lane_sha512], job
	mov	dword [lane_data + _outer_done_sha512], 0

        movdqa  xmm0, [state + _lens_sha512]
        XPINSRW xmm0, xmm1, p, lane, tmp, scale_x16
        movdqa  [state + _lens_sha512], xmm0

	mov	last_len, len
	and	last_len, 127
	lea	extra_blocks, [last_len + 17 + 127]
	shr	extra_blocks, 7
	mov	[lane_data + _extra_blocks_sha512], DWORD(extra_blocks)

	mov	p, [job + _src]
	add	p, [job + _hash_start_src_offset_in_bytes]
	mov	[state + _args_data_ptr_sha512 + PTR_SZ*lane], p

	cmp	len, 128
	jb	copy_lt128

fast_copy:
	add	p, len
%assign I 0
%rep 2
	movdqu	xmm0, [p - 128 + I*4*16 + 0*16]
	movdqu	xmm1, [p - 128 + I*4*16 + 1*16]
	movdqu	xmm2, [p - 128 + I*4*16 + 2*16]
	movdqu	xmm3, [p - 128 + I*4*16 + 3*16]
	movdqa	[lane_data + _extra_block_sha512 + I*4*16 + 0*16], xmm0
	movdqa	[lane_data + _extra_block_sha512 + I*4*16 + 1*16], xmm1
	movdqa	[lane_data + _extra_block_sha512 + I*4*16 + 2*16], xmm2
	movdqa	[lane_data + _extra_block_sha512 + I*4*16 + 3*16], xmm3
%assign I (I+1)
%endrep
end_fast_copy:

	mov	size_offset, extra_blocks
	shl	size_offset, 7
	sub	size_offset, last_len
	add	size_offset, 128-8
	mov	[lane_data + _size_offset_sha512], DWORD(size_offset)
	mov	start_offset, 128
	sub	start_offset, last_len
	mov	[lane_data + _start_offset_sha512], DWORD(start_offset)

	lea	tmp, [8*128 + 8*len]
	bswap	tmp
	mov	[lane_data + _extra_block_sha512 + size_offset], tmp

	mov	tmp, [job + _auth_key_xor_ipad]
 %assign I 0
 %rep 4
	movdqu	xmm0, [tmp + I * 2 * SHA512_DIGEST_WORD_SIZE]
	movq	[state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*lane + (2*I)*SHA512_DIGEST_ROW_SIZE], xmm0
	pextrq	[state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*lane + (2*I + 1)*SHA512_DIGEST_ROW_SIZE], xmm0, 1
 %assign I (I+1)
 %endrep
	test	len, ~127
	jnz	ge128_bytes

lt128_bytes:
        movdqa  xmm0, [state + _lens_sha512]
        XPINSRW xmm0, xmm1, tmp, lane, extra_blocks, scale_x16
        movdqa  [state + _lens_sha512], xmm0

	lea	tmp, [lane_data + _extra_block_sha512 + start_offset]
	mov	[state + _args_data_ptr_sha512 + PTR_SZ*lane], tmp ;; 8 to hold a UINT8
	mov	dword [lane_data + _extra_blocks_sha512], 0

ge128_bytes:
	cmp	unused_lanes, 0xff
	jne	return_null
	jmp	start_loop

	align	16
start_loop:
	; Find min length
	movdqa	xmm0, [state + _lens_sha512]
	phminposuw	xmm1, xmm0
	pextrw	DWORD(len2), xmm1, 0	; min value
	pextrw	DWORD(idx), xmm1, 1	; min index (0...1)
	cmp	len2, 0
	je	len_is_0

	pshuflw	xmm1, xmm1, 0XA0
	psubw	xmm0, xmm1
	movdqa	[state + _lens_sha512], xmm0

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	sha512_x2_sse
	; state and idx are intact

len_is_0:
	; process completed job "idx"
	imul	lane_data, idx, _SHA512_LANE_DATA_size
	lea	lane_data, [state + _ldata_sha512 + lane_data]
	mov	DWORD(extra_blocks), [lane_data + _extra_blocks_sha512]
	cmp	extra_blocks, 0
	jne	proc_extra_blocks
	cmp	dword [lane_data + _outer_done_sha512], 0
	jne	end_loop

proc_outer:
	mov	dword [lane_data + _outer_done_sha512], 1
	mov	DWORD(size_offset), [lane_data + _size_offset_sha512]
	mov	qword [lane_data + _extra_block_sha512 + size_offset], 0

        movdqa  xmm0, [state + _lens_sha512]
        XPINSRW xmm0, xmm1, tmp, idx, 1, scale_x16
        movdqa  [state + _lens_sha512], xmm0

	lea	tmp, [lane_data + _outer_block_sha512]
	mov	job, [lane_data + _job_in_lane_sha512]
	mov	[state + _args_data_ptr_sha512 + PTR_SZ*idx], tmp

%assign I 0
%rep (SHA_X_DIGEST_SIZE / (8 * 16))
	movq	xmm0, [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + (2*I)*SHA512_DIGEST_ROW_SIZE]
	pinsrq	xmm0, [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + (2*I + 1)*SHA512_DIGEST_ROW_SIZE], 1
	pshufb	xmm0, [rel byteswap]
	movdqa	[lane_data + _outer_block_sha512 + I*16], xmm0
%assign I (I+1)
%endrep

	mov	tmp, [job + _auth_key_xor_opad]
%assign I 0
%rep 4
	movdqu	xmm0, [tmp + I*16]
	movq	[state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 2*I*SHA512_DIGEST_ROW_SIZE], xmm0
	pextrq	[state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + (2*I + 1)*SHA512_DIGEST_ROW_SIZE], xmm0, 1
%assign I (I+1)
%endrep
	jmp	start_loop

	align	16
proc_extra_blocks:
	mov	DWORD(start_offset), [lane_data + _start_offset_sha512]

        movdqa  xmm0, [state + _lens_sha512]
        XPINSRW xmm0, xmm1, tmp, idx, extra_blocks, scale_x16
        movdqa  [state + _lens_sha512], xmm0

	lea	tmp, [lane_data + _extra_block_sha512 + start_offset]
	mov	[state + _args_data_ptr_sha512 + PTR_SZ*idx], tmp
	mov	dword [lane_data + _extra_blocks_sha512], 0
	jmp	start_loop

	align	16
copy_lt128:
	;; less than one message block of data
	;; beginning of source block
	;; destination extra block but backwards by len from where 0x80 pre-populated
	lea	p2, [lane_data + _extra_block  + 128]
	sub	p2, len
	memcpy_sse_128_1 p2, p, len, tmp4, tmp2, xmm0, xmm1, xmm2, xmm3
	mov	unused_lanes, [state + _unused_lanes_sha512]
	jmp	end_fast_copy

return_null:
	xor	job_rax, job_rax
	jmp	return

	align	16
end_loop:
	mov	job_rax, [lane_data + _job_in_lane_sha512]
	mov	unused_lanes, [state + _unused_lanes_sha512]
	mov	qword [lane_data + _job_in_lane_sha512], 0
	or	dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
	shl	unused_lanes, 8
	or	unused_lanes, idx
	mov	[state + _unused_lanes_sha512], unused_lanes

	mov	p, [job_rax + _auth_tag_output]

%if (SHA_X_DIGEST_SIZE != 384)
        cmp     qword [job_rax + _auth_tag_output_len_in_bytes], 32
        jne     copy_full_digest
%else
        cmp     qword [job_rax + _auth_tag_output_len_in_bytes], 24
        jne     copy_full_digest
%endif

	;; copy 32 bytes for SHA512 / 24 bytes for SHA384
	mov	QWORD(tmp),  [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 0*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp2), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 1*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp3), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 2*SHA512_DIGEST_ROW_SIZE]
%if (SHA_X_DIGEST_SIZE != 384)
	mov	QWORD(tmp4), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 3*SHA512_DIGEST_ROW_SIZE] ; this line of code will run only for SHA512
%endif
	bswap	QWORD(tmp)
	bswap	QWORD(tmp2)
	bswap	QWORD(tmp3)
%if (SHA_X_DIGEST_SIZE != 384)
	bswap	QWORD(tmp4)
%endif
	mov	[p + 0*8], QWORD(tmp)
	mov	[p + 1*8], QWORD(tmp2)
	mov	[p + 2*8], QWORD(tmp3)
%if (SHA_X_DIGEST_SIZE != 384)
	mov	[p + 3*8], QWORD(tmp4)
%endif
        jmp     clear_ret

copy_full_digest:
	;; copy 64 bytes for SHA512 / 48 bytes for SHA384
	mov	QWORD(tmp),  [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 0*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp2), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 1*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp3), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 2*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp4), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 3*SHA512_DIGEST_ROW_SIZE] ; this line of code will run only for SHA512
	bswap	QWORD(tmp)
	bswap	QWORD(tmp2)
	bswap	QWORD(tmp3)
	bswap	QWORD(tmp4)
	mov	[p + 0*8], QWORD(tmp)
	mov	[p + 1*8], QWORD(tmp2)
	mov	[p + 2*8], QWORD(tmp3)
	mov	[p + 3*8], QWORD(tmp4)
	mov	QWORD(tmp),  [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 4*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp2), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 5*SHA512_DIGEST_ROW_SIZE]
%if (SHA_X_DIGEST_SIZE != 384)
	mov	QWORD(tmp3), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 6*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp4), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 7*SHA512_DIGEST_ROW_SIZE] ; this line of code will run only for SHA512
%endif
	bswap	QWORD(tmp)
	bswap	QWORD(tmp2)
%if (SHA_X_DIGEST_SIZE != 384)
	bswap	QWORD(tmp3)
	bswap	QWORD(tmp4)
%endif
	mov	[p + 4*8], QWORD(tmp)
	mov	[p + 5*8], QWORD(tmp2)
%if (SHA_X_DIGEST_SIZE != 384)
	mov	[p + 6*8], QWORD(tmp3)
	mov	[p + 7*8], QWORD(tmp4)
%endif

clear_ret:

%ifdef SAFE_DATA
        ;; Clear digest (48B/64B), outer_block (48B/64B) and extra_block (128B) of returned job
%assign J 0
%rep 6
        mov     qword [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + J*SHA512_DIGEST_ROW_SIZE], 0
%assign J (J+1)
%endrep
%if (SHA_X_DIGEST_SIZE != 384)
        mov     qword [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 6*SHA256_DIGEST_ROW_SIZE], 0
        mov     qword [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 7*SHA256_DIGEST_ROW_SIZE], 0
%endif

        pxor    xmm0, xmm0
        imul	lane_data, idx, _SHA512_LANE_DATA_size
        lea	lane_data, [state + _ldata_sha512 + lane_data]
        ;; Clear first 128 bytes of extra_block
%assign offset 0
%rep 8
        movdqa  [lane_data + _extra_block + offset], xmm0
%assign offset (offset + 16)
%endrep

        ;; Clear first 48 bytes (SHA-384) or 64 bytes (SHA-512) of outer_block
        movdqa  [lane_data + _outer_block], xmm0
        movdqa  [lane_data + _outer_block + 16], xmm0
        movdqa  [lane_data + _outer_block + 32], xmm0
%if (SHA_X_DIGEST_SIZE != 384)
        movdqa  [lane_data + _outer_block + 48], xmm0
%endif
%endif ;; SAFE_DATA

return:
	mov	rbx, [rsp + _gpr_save + 8*0]
	mov	rbp, [rsp + _gpr_save + 8*1]
%ifndef LINUX
	mov	rsi, [rsp + _gpr_save + 8*2]
	mov	rdi, [rsp + _gpr_save + 8*3]
%endif
	mov	rsp, [rsp + _rsp_save]	; original SP
	ret

mksection stack-noexec

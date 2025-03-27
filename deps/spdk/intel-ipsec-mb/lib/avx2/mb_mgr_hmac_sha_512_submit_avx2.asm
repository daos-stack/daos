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
%include "include/cet.inc"
extern sha512_x4_avx2

mksection .rodata
default rel
align 16
byteswap:	;ddq 0x08090a0b0c0d0e0f0001020304050607
	dq 0x0001020304050607, 0x08090a0b0c0d0e0f

mksection .text

%ifndef FUNC
%define FUNC submit_job_hmac_sha_512_avx2
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

; idx needs to be in rbp, r13, r14, r16
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

; Define stack usage

; we clobber rbx, rsi, rdi, rbp; called routine also clobbers r12
struc STACK
_gpr_save:	resq	5
_rsp_save:	resq	1
endstruc

; JOB* FUNC(MB_MGR_HMAC_sha_512_OOO *state, IMB_JOB *job)
; arg 1 : rcx : state
; arg 2 : rdx : job
MKGLOBAL(FUNC,function,internal)
FUNC:
        endbranch64
	mov	rax, rsp
	sub	rsp, STACK_size
	and	rsp, -32
	mov	[rsp + _gpr_save + 8*0], rbx
	mov	[rsp + _gpr_save + 8*1], rbp
	mov	[rsp + _gpr_save + 8*2], r12
%ifndef LINUX
	mov	[rsp + _gpr_save + 8*3], rsi
	mov	[rsp + _gpr_save + 8*4], rdi
%endif
	mov	[rsp + _rsp_save], rax	; original SP

	mov	unused_lanes, [state + _unused_lanes_sha512]
	movzx	lane, BYTE(unused_lanes)
	shr	unused_lanes, 8
	imul	lane_data, lane, _SHA512_LANE_DATA_size
	lea	lane_data, [state + _ldata_sha512 + lane_data]
	mov	[state + _unused_lanes_sha512], unused_lanes
	mov	len, [job + _msg_len_to_hash_in_bytes]
	mov	tmp, len
	shr	tmp, 7	; divide by 128, len in terms of blocks

	mov	[lane_data + _job_in_lane_sha512], job
	mov	dword [lane_data + _outer_done_sha512], 0

        vmovdqa xmm0, [state + _lens_sha512]
        XVPINSRW xmm0, xmm1, extra_blocks, lane, tmp, scale_x16
        vmovdqa [state + _lens_sha512], xmm0

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
	vmovdqu	ymm0, [p - 128 + 0*32]
	vmovdqu	ymm1, [p - 128 + 1*32]
	vmovdqu	ymm2, [p - 128 + 2*32]
	vmovdqu	ymm3, [p - 128 + 3*32]
	vmovdqu	[lane_data + _extra_block_sha512 + 0*32], ymm0
	vmovdqu	[lane_data + _extra_block_sha512 + 1*32], ymm1
	vmovdqu	[lane_data + _extra_block_sha512 + 2*32], ymm2
	vmovdqu	[lane_data + _extra_block_sha512 + 3*32], ymm3
end_fast_copy:
        endbranch64
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
     vmovdqu	xmm0, [tmp + I * 2 * SHA512_DIGEST_WORD_SIZE]
     vmovq	[state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*lane + (2*I + 0)*SHA512_DIGEST_ROW_SIZE], xmm0
     vpextrq	[state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*lane + (2*I + 1)*SHA512_DIGEST_ROW_SIZE], xmm0, 1
%assign I (I+1)
%endrep

	test	len, ~127
	jnz	ge128_bytes

lt128_bytes:
        vmovdqa xmm0, [state + _lens_sha512]
        XVPINSRW xmm0, xmm1, tmp, lane, extra_blocks, scale_x16
        vmovdqa [state + _lens_sha512], xmm0

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
        endbranch64
	vmovdqa	xmm0, [state + _lens_sha512]
	vphminposuw	xmm1, xmm0
	vpextrw	DWORD(len2), xmm1, 0	; min value
	vpextrw	DWORD(idx), xmm1, 1	; min index (0...1)
	cmp	len2, 0
	je	len_is_0

	vpshuflw xmm1, xmm1, 0x00
	vpsubw	xmm0, xmm0, xmm1
	vmovdqa	[state + _lens_sha512], xmm0

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	sha512_x4_avx2
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

        vmovdqa xmm0, [state + _lens_sha512]
        XVPINSRW xmm0, xmm1, tmp, idx, 1, scale_x16
        vmovdqa [state + _lens_sha512], xmm0

	lea	tmp, [lane_data + _outer_block_sha512]
	mov	job, [lane_data + _job_in_lane_sha512]
	mov	[state + _args_data_ptr_sha512 + PTR_SZ*idx], tmp

%assign I 0
%rep (SHA_X_DIGEST_SIZE / (8 * 16))
	vmovq	xmm0, [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + (2*I + 0)*SHA512_DIGEST_ROW_SIZE]
	vpinsrq	xmm0, [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + (2*I + 1)*SHA512_DIGEST_ROW_SIZE], 1
	vpshufb	xmm0, [rel byteswap]
	vmovdqa	[lane_data + _outer_block_sha512 + I * 2 * SHA512_DIGEST_WORD_SIZE], xmm0
%assign I (I+1)
%endrep

	mov	tmp, [job + _auth_key_xor_opad]
%assign I 0
%rep 4
	vmovdqu	xmm0, [tmp + I * 16]
	vmovq	[state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + (2*I+0)*SHA512_DIGEST_ROW_SIZE], xmm0
	vpextrq	[state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + (2*I + 1)*SHA512_DIGEST_ROW_SIZE], xmm0, 1
%assign I (I+1)
%endrep

	jmp	start_loop

	align	16
proc_extra_blocks:
	mov	DWORD(start_offset), [lane_data + _start_offset_sha512]

        vmovdqa xmm0, [state + _lens_sha512]
        XVPINSRW xmm0, xmm1, tmp, idx, extra_blocks, scale_x16
        vmovdqa [state + _lens_sha512], xmm0

	lea	tmp, [lane_data + _extra_block_sha512 + start_offset]
	mov	[state + _args_data_ptr_sha512 + PTR_SZ*idx], tmp ;;  idx is index of shortest length message
	mov	dword [lane_data + _extra_blocks_sha512], 0
	jmp	start_loop

	align	16
copy_lt128:
	;; less than one message block of data
	;; destination extra block but backwards by len from where 0x80 pre-populated
	lea	p2, [lane_data + _extra_block  + 128]
	sub	p2, len
	memcpy_avx2_128_1 p2, p, len, tmp4, tmp2, ymm0, ymm1, ymm2, ymm3
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
	;; copy 32 bytes for  SHA512 / 24 bytes for SHA384
	mov	QWORD(tmp),  [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 0*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp2), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 1*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp3), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 2*SHA512_DIGEST_ROW_SIZE]
%if (SHA_X_DIGEST_SIZE != 384)
	mov	QWORD(tmp4), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 3*SHA512_DIGEST_ROW_SIZE]
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
	;; copy 64 bytes for  SHA512 / 48 bytes for SHA384
	mov	QWORD(tmp),  [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 0*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp2), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 1*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp3), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 2*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp4), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 3*SHA512_DIGEST_ROW_SIZE]
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
	mov	QWORD(tmp4), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 7*SHA512_DIGEST_ROW_SIZE]
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
        endbranch64
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

        vpxor   ymm0, ymm0
        imul	lane_data, idx, _SHA512_LANE_DATA_size
        lea	lane_data, [state + _ldata_sha512 + lane_data]
        ;; Clear first 128 bytes of extra_block
%assign offset 0
%rep 4
        vmovdqa [lane_data + _extra_block + offset], ymm0
%assign offset (offset + 32)
%endrep

        ;; Clear first 48 bytes (SHA-384) or 64 bytes (SHA-512) of outer_block
        vmovdqu [lane_data + _outer_block], ymm0
%if (SHA_X_DIGEST_SIZE == 384)
        vmovdqa [lane_data + _outer_block + 32], xmm0
%else
        vmovdqu [lane_data + _outer_block + 32], ymm0
%endif
%endif ;; SAFE_DATA

return:
        endbranch64
        vzeroupper

	mov	rbx, [rsp + _gpr_save + 8*0]
	mov	rbp, [rsp + _gpr_save + 8*1]
	mov	r12, [rsp + _gpr_save + 8*2]
%ifndef LINUX
	mov	rsi, [rsp + _gpr_save + 8*3]
	mov	rdi, [rsp + _gpr_save + 8*4]
%endif
	mov	rsp, [rsp + _rsp_save]	; original SP
	ret

mksection stack-noexec

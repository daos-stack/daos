;;
;; Copyright (c) 2017-2021, Intel Corporation
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

;; In System V AMD64 ABI
;;	callee saves: RBX, RBP, R12-R15
;; Windows x64 ABI
;;	callee saves: RBX, RBP, RDI, RSI, R12-R15
;;
;; Clobbers ZMM0-31

%include "include/os.asm"
%include "include/imb_job.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/reg_sizes.asm"
%include "include/cet.inc"
extern sha512_x8_avx512

mksection .rodata
default rel

align 16
dupw:	;ddq 0x01000100010001000100010001000100
	dq 0x0100010001000100, 0x0100010001000100

align 16
byteswap:	;ddq 0x08090a0b0c0d0e0f0001020304050607
	dq 0x0001020304050607, 0x08090a0b0c0d0e0f

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

lane_1: dq  1
lane_2: dq  2
lane_3: dq  3
lane_4: dq  4
lane_5: dq  5
lane_6: dq  6
lane_7: dq  7

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

; idx needs to be in rbp
%define idx		rbp

%define unused_lanes	rbx
%define lane_data	rbx
%define tmp2		rbx

%define job_rax		rax

%define size_offset	rax
%define tmp		rax
%define start_offset	rax

%define extra_blocks	arg2
%define p		arg2

%define tmp4		r8
%define tmp5		r9
%define tmp6		r10

struc STACK
_gpr_save:	resq	7 ; rbx, rbp, r12-r15, rdi (windows)
_rsp_save:	resq	1
endstruc

%define APPEND(a,b) a %+ b

%ifndef SHA384
; JOB* flush_job_hmac_sha_384_avx512(MB_MGR_HMAC_SHA_512_OOO *state)
; arg 1 : state
%define SHA_X_DIGEST_SIZE 512
MKGLOBAL(flush_job_hmac_sha_512_avx512,function,internal)
align 64
flush_job_hmac_sha_512_avx512:
        endbranch64
%else
; JOB* flush_job_hmac_sha_512_avx512(MB_MGR_HMAC_SHA_512_OOO *state)
; arg 1 : state
%define SHA_X_DIGEST_SIZE 384
MKGLOBAL(flush_job_hmac_sha_384_avx512,function,internal)
align 64
flush_job_hmac_sha_384_avx512:
        endbranch64
%endif
	mov	rax, rsp
	sub	rsp, STACK_size
	and	rsp, -32
	mov	[rsp + _gpr_save + 8*0], rbx
	mov	[rsp + _gpr_save + 8*1], rbp
	mov	[rsp + _gpr_save + 8*2], r12
	mov	[rsp + _gpr_save + 8*3], r13
	mov	[rsp + _gpr_save + 8*4], r14
	mov	[rsp + _gpr_save + 8*5], r15
%ifndef LINUX
	mov	[rsp + _gpr_save + 8*6], rdi
%endif
	mov	[rsp + _rsp_save], rax	; original SP

	mov	unused_lanes, [state + _unused_lanes_sha512]
	bt	unused_lanes, 32+3
	jc	return_null

	; find a lane with a non-null job
	xor	idx, idx
%assign I 1
%rep 7
	cmp	qword [state + _ldata_sha512 + I * _SHA512_LANE_DATA_size + _job_in_lane_sha512], 0
	cmovne	idx, [rel APPEND(lane_, I)]
%assign I (I+1)
%endrep

copy_lane_data:
	; copy good lane (idx) to empty lanes
	vmovdqa	xmm0, [state + _lens_sha512]
	mov	tmp, [state + _args_sha512 + _data_ptr_sha512 + PTR_SZ*idx]

%assign I 0
%rep 8
	cmp	qword [state + _ldata_sha512 + I * _SHA512_LANE_DATA_size + _job_in_lane_sha512], 0
	jne	APPEND(skip_,I)
	mov	[state + _args_sha512 + _data_ptr_sha512 + PTR_SZ*I], tmp
	vpor	xmm0, xmm0, [rel len_masks + 16*I]
APPEND(skip_,I):
%assign I (I+1)
%endrep

	vmovdqa	[state + _lens_sha512], xmm0

	vphminposuw	xmm1, xmm0
	vpextrw	DWORD(len2), xmm1, 0	; min value
	vpextrw	DWORD(idx), xmm1, 1	; min index (0...7)
	cmp	len2, 0
	je	len_is_0

	vpshufb	xmm1, [rel dupw]	; duplicate words across all 8 lanes
	vpsubw	xmm0, xmm0, xmm1
	vmovdqa	[state + _lens_sha512], xmm0

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	sha512_x8_avx512
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
	mov	word [state + _lens_sha512 + 2*idx], 1
	lea	tmp, [lane_data + _outer_block_sha512]
	mov	job, [lane_data + _job_in_lane_sha512]
	mov	[state + _args_data_ptr_sha512 + PTR_SZ*idx], tmp

	; move digest into data location
	%assign I 0
	%rep (SHA_X_DIGEST_SIZE / (8*16))
	vmovq	xmm0, [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 2*I*SHA512_DIGEST_ROW_SIZE]
	vpinsrq	xmm0, [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + (2*I + 1)*SHA512_DIGEST_ROW_SIZE], 1
	vpshufb	xmm0, [rel byteswap]
	vmovdqa	[lane_data + _outer_block_sha512 + I*2*SHA512_DIGEST_WORD_SIZE], xmm0
	%assign I (I+1)
	%endrep

	; move the opad key into digest
	mov	tmp, [job + _auth_key_xor_opad]

	%assign I 0
	%rep 4
	vmovdqu	xmm0, [tmp + I * 16]
	vmovq	[state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + (2*I + 0)*SHA512_DIGEST_ROW_SIZE], xmm0
	vpextrq	[state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + (2*I + 1)*SHA512_DIGEST_ROW_SIZE], xmm0, 1
	%assign I (I+1)
	%endrep

	jmp	copy_lane_data

	align	32
proc_extra_blocks:
	mov	DWORD(start_offset), [lane_data + _start_offset_sha512]
	mov	[state + _lens_sha512 + 2*idx], WORD(extra_blocks)
	lea	tmp, [lane_data + _extra_block_sha512 + start_offset]
	mov	[state + _args_data_ptr_sha512 + PTR_SZ*idx], tmp
	mov	dword [lane_data + _extra_blocks_sha512], 0
	jmp	copy_lane_data

return_null:
	xor	job_rax, job_rax
	jmp	return

	align	32
end_loop:
	mov	job_rax, [lane_data + _job_in_lane_sha512]
	mov	qword [lane_data + _job_in_lane_sha512], 0
	or	dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
	mov	unused_lanes, [state + _unused_lanes_sha512]
	shl	unused_lanes, 4
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
	mov	QWORD(tmp2), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 0*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp4), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 1*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp6), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 2*SHA512_DIGEST_ROW_SIZE]
%if (SHA_X_DIGEST_SIZE != 384)
	mov	QWORD(tmp5), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 3*SHA512_DIGEST_ROW_SIZE]
%endif
	bswap	QWORD(tmp2)
	bswap	QWORD(tmp4)
	bswap	QWORD(tmp6)
%if (SHA_X_DIGEST_SIZE != 384)
	bswap	QWORD(tmp5)
%endif
	mov	[p + 0*8], QWORD(tmp2)
	mov	[p + 1*8], QWORD(tmp4)
	mov	[p + 2*8], QWORD(tmp6)
%if (SHA_X_DIGEST_SIZE != 384)
	mov	[p + 3*8], QWORD(tmp5)
%endif
        jmp     clear_ret

copy_full_digest:
	;; copy 64 bytes for SHA512 / 48 bytes for SHA384
	mov	QWORD(tmp2), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 0*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp4), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 1*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp6), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 2*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp5), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 3*SHA512_DIGEST_ROW_SIZE]
	bswap	QWORD(tmp2)
	bswap	QWORD(tmp4)
	bswap	QWORD(tmp6)
	bswap	QWORD(tmp5)
	mov	[p + 0*8], QWORD(tmp2)
	mov	[p + 1*8], QWORD(tmp4)
	mov	[p + 2*8], QWORD(tmp6)
	mov	[p + 3*8], QWORD(tmp5)

	mov	QWORD(tmp2), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 4*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp4), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 5*SHA512_DIGEST_ROW_SIZE]
%if (SHA_X_DIGEST_SIZE != 384)
	mov	QWORD(tmp6), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 6*SHA512_DIGEST_ROW_SIZE]
	mov	QWORD(tmp5), [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*idx + 7*SHA512_DIGEST_ROW_SIZE]
%endif
	bswap	QWORD(tmp2)
	bswap	QWORD(tmp4)
%if (SHA_X_DIGEST_SIZE != 384)
	bswap	QWORD(tmp6)
	bswap	QWORD(tmp5)
%endif
	mov	[p + 4*8], QWORD(tmp2)
	mov	[p + 5*8], QWORD(tmp4)
%if (SHA_X_DIGEST_SIZE != 384)
	mov	[p + 6*8], QWORD(tmp6)
	mov	[p + 7*8], QWORD(tmp5)
%endif

clear_ret:

%ifdef SAFE_DATA
        vpxorq   zmm0, zmm0

        ;; Clear digest (48B/64B), outer_block (48B/64B) and extra_block (128B) of returned job
%assign I 0
%rep 8
	cmp	qword [state + _ldata_sha512 + (I*_SHA512_LANE_DATA_size) + _job_in_lane_sha512], 0
	jne	APPEND(skip_clear_,I)

        ;; Clear digest (48 bytes for SHA-384, 64 bytes for SHA-512 bytes)
%assign J 0
%rep 6
        mov     qword [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*I + J*SHA512_DIGEST_ROW_SIZE], 0
%assign J (J+1)
%endrep
%if (SHA_X_DIGEST_SIZE != 384)
        mov     qword [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*I + 6*SHA512_DIGEST_ROW_SIZE], 0
        mov     qword [state + _args_digest_sha512 + SHA512_DIGEST_WORD_SIZE*I + 7*SHA512_DIGEST_ROW_SIZE], 0
%endif

        lea     lane_data, [state + _ldata_sha512 + (I*_SHA512_LANE_DATA_size)]
        ;; Clear first 128 bytes of extra_block
        vmovdqu64 [lane_data + _extra_block], zmm0
        vmovdqu64 [lane_data + _extra_block + 64], zmm0

        ;; Clear first 48 bytes (SHA-384) or 64 bytes (SHA-512) of outer_block
%if (SHA_X_DIGEST_SIZE == 384)
        vmovdqu64 [lane_data + _outer_block], ymm0
        vmovdqa64 [lane_data + _outer_block + 32], xmm0
%else
        vmovdqu64 [lane_data + _outer_block], zmm0
%endif

APPEND(skip_clear_,I):
%assign I (I+1)
%endrep

%endif ;; SAFE_DATA

return:
        vzeroupper

	mov	rbx, [rsp + _gpr_save + 8*0]
	mov	rbp, [rsp + _gpr_save + 8*1]
	mov	r12, [rsp + _gpr_save + 8*2]
	mov	r13, [rsp + _gpr_save + 8*3]
	mov	r14, [rsp + _gpr_save + 8*4]
	mov	r15, [rsp + _gpr_save + 8*5]
%ifndef LINUX
	mov	rdi, [rsp + _gpr_save + 8*6]
%endif
	mov	rsp, [rsp + _rsp_save]	; original SP

	ret

mksection stack-noexec

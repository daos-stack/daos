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

;; In System V AMD64 ABI
;;	callee saves: RBX, RBP, R12-R15
;; Windows x64 ABI
;;	callee saves: RBX, RBP, RDI, RSI, RSP, R12-R15
;;
;; Linux/Windows clobbers: xmm0 - xmm15
;;

%include "include/os.asm"
%include "include/imb_job.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/reg_sizes.asm"

;%define DO_DBGPRINT
%include "include/dbgprint.asm"

extern sha256_ni

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

; idx needs to be in rbx, rbp, r13-r15
%define idx		rbp

%define unused_lanes	rbx
%define lane_data	rbx
%define tmp2		rbx

%define job_rax		rax
%define	tmp1		rax
%define size_offset	rax
%define tmp		rax
%define start_offset	rax

%define tmp3		arg1

%define extra_blocks	arg2
%define p		arg2

%define tmp4		r8

%define tmp5	        r9

%define tmp6	        r10

%define bswap_xmm4	xmm4

struc STACK
_gpr_save:	resq	4 ;rbx, rbp, rsi (win), rdi (win)
_rsp_save:	resq	1
endstruc

%define APPEND(a,b) a %+ b

mksection .rodata
default rel

align 16
byteswap:
	dq 0x0405060700010203
	dq 0x0c0d0e0f08090a0b

one:	dq  1

mksection .text

%ifdef SHA224
;; JOB* flush_job_hmac_sha_224_ni_sse(MB_MGR_HMAC_SHA_256_OOO *state)
;; arg1 : state
MKGLOBAL(flush_job_hmac_sha_224_ni_sse,function,internal)
flush_job_hmac_sha_224_ni_sse:
%else
;; JOB* flush_job_hmac_sha_256_ni_sse(MB_MGR_HMAC_SHA_256_OOO *state)
;; arg1 : state
MKGLOBAL(flush_job_hmac_sha_256_ni_sse,function,internal)
flush_job_hmac_sha_256_ni_sse:
%endif
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

        DBGPRINTL "enter sha256-ni-sse flush"

	mov	unused_lanes, [state + _unused_lanes_sha256]
	bt	unused_lanes, 16+7
	jc	return_null

	; find a lane with a non-null job, assume it is 0 then check 1
	xor	idx, idx
	cmp	qword [state + _ldata_sha256 + 1 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [rel one]
	DBGPRINTL64 "idx:", idx

copy_lane_data:
	; copy idx to empty lanes
	mov	tmp, [state + _args_data_ptr_sha256 + PTR_SZ*idx]
	xor	len2, len2
	mov	WORD(len2), word [state + _lens_sha256 + idx*2]

	; there are only two lanes so if one is empty it is easy to determine which one
	xor	idx, 1
	mov	[state + _args_data_ptr_sha256 + PTR_SZ*idx], tmp
	xor	idx, 1

	; No need to find min length - only two lanes available
        cmp	len2, 0
        je	len_is_0

	; set length on both lanes to 0
	mov	dword [state + _lens_sha256], 0

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	sha256_ni
	; state and idx are intact

len_is_0:
	; process completed job "idx"
	imul	lane_data, idx, _HMAC_SHA1_LANE_DATA_size
	lea	lane_data, [state + _ldata_sha256 + lane_data]
	mov	DWORD(extra_blocks), [lane_data + _extra_blocks]
	cmp	extra_blocks, 0
	jne	proc_extra_blocks
	movdqa	bswap_xmm4, [rel byteswap]
	cmp	dword [lane_data + _outer_done], 0
	jne	end_loop

proc_outer:
	mov	dword [lane_data + _outer_done], 1
	mov	DWORD(size_offset), [lane_data + _size_offset]
	mov	qword [lane_data + _extra_block + size_offset], 0
	mov	word [state + _lens_sha256 + 2*idx], 1
	lea	tmp, [lane_data + _outer_block]
	mov	job, [lane_data + _job_in_lane]
	mov	[state + _args_data_ptr_sha256 + PTR_SZ*idx], tmp

%if SHA256NI_DIGEST_ROW_SIZE != 32
%error "Below code has been optimized for SHA256NI_DIGEST_ROW_SIZE = 32!"
%endif
	lea	tmp4, [idx*8]	 ; x8 here + scale factor x4 below give x32
	movdqu	xmm0, [state + _args_digest_sha256 + tmp4*4]
	movdqu	xmm1, [state + _args_digest_sha256 + tmp4*4 + 4*4]
	pshufb	xmm0, bswap_xmm4
	pshufb	xmm1, bswap_xmm4
	movdqa	[lane_data + _outer_block], xmm0
	movdqa	[lane_data + _outer_block + 4*4], xmm1
%ifdef SHA224
	;; overwrite top 4 bytes with 0x80
	mov	dword [lane_data + _outer_block + 7*4], 0x80
%endif
        DBGPRINTL	"sha256 outer hash input words:"
        DBGPRINT_XMM xmm0
        DBGPRINT_XMM xmm1

	mov	tmp, [job + _auth_key_xor_opad]
	movdqu	xmm0, [tmp]
	movdqu	xmm1, [tmp + 4*4]
	DBGPRINTL64 "auth_key_xor_opad", tmp
	movdqu	[state + _args_digest_sha256 + tmp4*4], xmm0
	movdqu	[state + _args_digest_sha256 + tmp4*4 + 4*4], xmm1
        DBGPRINTL	"new digest args"
        DBGPRINT_XMM xmm0
        DBGPRINT_XMM xmm1
	jmp	copy_lane_data

	align	16
proc_extra_blocks:
	mov	DWORD(start_offset), [lane_data + _start_offset]
	mov	[state + _lens_sha256 + 2*idx], WORD(extra_blocks)
	lea	tmp, [lane_data + _extra_block + start_offset]
	mov	[state + _args_data_ptr_sha256 + PTR_SZ*idx], tmp
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
	mov	unused_lanes, [state + _unused_lanes_sha256]
	shl	unused_lanes, 8
	or	unused_lanes, idx
	mov	[state + _unused_lanes_sha256], unused_lanes

	mov	p, [job_rax + _auth_tag_output]

	; copy 16 bytes for SHA256, 14 bytes for SHA224
%if SHA256NI_DIGEST_ROW_SIZE != 32
%error "Below code has been optimized for SHA256NI_DIGEST_ROW_SIZE = 32!"
%endif
	shl	idx, 5

%ifdef SHA224
        cmp     qword [job_rax + _auth_tag_output_len_in_bytes], 14
        jne     copy_full_digest
%else
        cmp     qword [job_rax + _auth_tag_output_len_in_bytes], 16
        jne     copy_full_digest
%endif
	movdqu	xmm0, [state + _args_digest_sha256 + idx]
	pshufb	xmm0, bswap_xmm4
%ifdef SHA224
	;; SHA224
	movq	[p + 0*4], xmm0
	pextrd	[p + 2*4], xmm0, 2
	pextrw	[p + 3*4], xmm0, 6
%else
	;; SHA256
	movdqu	[p], xmm0
%endif
	DBGPRINTL	"auth_tag_output:"
        DBGPRINT_XMM	xmm0
        jmp     clear_ret

copy_full_digest:
	movdqu	xmm0,  [state + _args_digest_sha256 + idx]
	movdqu	xmm1,  [state + _args_digest_sha256 + idx + 16]
	pshufb	xmm0, bswap_xmm4
	pshufb	xmm1, bswap_xmm4
%ifdef SHA224
	;; SHA224
	movdqu	[p], xmm0
	movq	[p + 16], xmm1
	pextrd	[p + 16 + 8], xmm1, 2
%else
	;; SHA256
	movdqu	[p], xmm0
	movdqu	[p + 16], xmm1
%endif

clear_ret:

%ifdef SAFE_DATA
        pxor    xmm0, xmm0

        ;; Clear digest, outer_block (28B/32B) and extra_block (64B)
        ;; of returned job and NULL jobs
%assign I 0
%rep 2
	cmp	qword [state + _ldata_sha256 + (I*_HMAC_SHA1_LANE_DATA_size) + _job_in_lane], 0
	jne	APPEND(skip_clear_,I)

        ;; Clear digest
        movdqa  [state + _args_digest_sha256 + I*32], xmm0
        movdqa  [state + _args_digest_sha256 + I*32 + 16], xmm0

        lea     lane_data, [state + _ldata_sha256 + (I*_HMAC_SHA1_LANE_DATA_size)]
        ;; Clear first 64 bytes of extra_block
%assign offset 0
%rep 4
        movdqa  [lane_data + _extra_block + offset], xmm0
%assign offset (offset + 16)
%endrep

        ;; Clear first 28 bytes (SHA-224) or 32 bytes (SHA-256) of outer_block
        movdqa  [lane_data + _outer_block], xmm0
%ifdef SHA224
        mov     qword [lane_data + _outer_block + 16], 0
        mov     dword [lane_data + _outer_block + 24], 0
%else
        movdqa  [lane_data + _outer_block + 16], xmm0
%endif

APPEND(skip_clear_,I):
%assign I (I+1)
%endrep

%endif ;; SAFE_DATA

return:
        DBGPRINTL "exit sha256-ni-sse flush"

	mov	rbx, [rsp + _gpr_save + 8*0]
	mov	rbp, [rsp + _gpr_save + 8*1]
%ifndef LINUX
	mov	rsi, [rsp + _gpr_save + 8*2]
	mov	rdi, [rsp + _gpr_save + 8*3]
%endif
	mov	rsp, [rsp + _rsp_save]	; original SP
	ret

mksection stack-noexec

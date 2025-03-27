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
;; Registers:		RAX RBX RCX RDX RBP RSI RDI R8  R9  R10 R11 R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Windows clobbers:	RAX     RCX RDX             R8  R9  R10 R11
;; Windows preserves:	    RBX         RBP RSI RDI                 R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Linux clobbers:	RAX                 RSI RDI R8  R9  R10 R11
;; Linux preserves:	    RBX RCX RDX RBP                         R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Clobbers ZMM0-31

%include "include/os.asm"
%include "include/imb_job.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/reg_sizes.asm"

;; %define DO_DBGPRINT
%include "include/dbgprint.asm"

extern sha1_x16_avx512

mksection .rodata
default rel

align 16
byteswap:
	dq 0x0405060700010203
	dq 0x0c0d0e0f08090a0b

align 32
len_masks:
	dq 0x000000000000FFFF, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000
	dq 0x00000000FFFF0000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000
	dq 0x0000FFFF00000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000
	dq 0xFFFF000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000
	dq 0x0000000000000000, 0x000000000000FFFF, 0x0000000000000000, 0x0000000000000000
	dq 0x0000000000000000, 0x00000000FFFF0000, 0x0000000000000000, 0x0000000000000000
	dq 0x0000000000000000, 0x0000FFFF00000000, 0x0000000000000000, 0x0000000000000000
	dq 0x0000000000000000, 0xFFFF000000000000, 0x0000000000000000, 0x0000000000000000
	dq 0x0000000000000000, 0x0000000000000000, 0x000000000000FFFF, 0x0000000000000000
	dq 0x0000000000000000, 0x0000000000000000, 0x00000000FFFF0000, 0x0000000000000000
	dq 0x0000000000000000, 0x0000000000000000, 0x0000FFFF00000000, 0x0000000000000000
	dq 0x0000000000000000, 0x0000000000000000, 0xFFFF000000000000, 0x0000000000000000
	dq 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x000000000000FFFF
	dq 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x00000000FFFF0000
	dq 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000FFFF00000000
	dq 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0xFFFF000000000000

lane_1: dq  1
lane_2: dq  2
lane_3: dq  3
lane_4: dq  4
lane_5: dq  5
lane_6: dq  6
lane_7: dq  7
lane_8: dq  8
lane_9: dq  9
lane_10: dq  10
lane_11: dq  11
lane_12: dq  12
lane_13: dq  13
lane_14: dq  14
lane_15: dq  15

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

; idx needs to be in rbx, rdi, rbp
%define idx		rbp

%define unused_lanes	r9
%define lane_data	r9
%define tmp2		r9
%define num_lanes_inuse r12
%define len_upper	r13
%define idx_upper	r14

%define job_rax		rax
%define	tmp1		rax
%define size_offset	rax
%define tmp		rax
%define start_offset	rax

%define tmp3		arg1

%define extra_blocks	arg2
%define p		arg2

%define tmp4		r8

%endif

; we clobber rbp, called routine clobbers r12-r15
struc STACK
_gpr_save:	resq	5
_rsp_save:	resq	1
endstruc

%define APPEND(a,b) a %+ b

; JOB* flush_job_hmac_avx(MB_MGR_HMAC_SHA_1_OOO *state)
; arg 1 : rcx : state
MKGLOBAL(flush_job_hmac_avx512,function,internal)
flush_job_hmac_avx512:

	mov	rax, rsp
	sub	rsp, STACK_size
	and	rsp, -32		; align stack to 32 byte boundary
	mov	[rsp + _gpr_save + 8*0], rbp
	mov	[rsp + _gpr_save + 8*1], r12
	mov	[rsp + _gpr_save + 8*2], r13
	mov	[rsp + _gpr_save + 8*3], r14
	mov	[rsp + _gpr_save + 8*4], r15
	mov	[rsp + _rsp_save], rax

        DBGPRINTL "---------- start hmac flush avx512 -----------"

	mov	DWORD(num_lanes_inuse), [state + _num_lanes_inuse_sha1] ;empty?
	cmp	num_lanes_inuse, 0
	jz	return_null

	; find a lane with a non-null job
	xor	idx, idx
%assign I 1
%rep 15
	cmp	qword [state + _ldata + (I * _HMAC_SHA1_LANE_DATA_size) + _job_in_lane], 0
	cmovne	idx, [rel APPEND(lane_,I)]
%assign I (I+1)
%endrep

copy_lane_data:
	; copy valid lane (idx) to empty lanes
	vmovdqa	ymm0, [state + _lens]
	mov	tmp, [state + _args_data_ptr + PTR_SZ*idx]

%assign I 0
%rep 16
	cmp	qword [state + _ldata + I * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
	jne	APPEND(skip_,I)
	mov	[state + _args_data_ptr + PTR_SZ*I], tmp
	vpor	ymm0, ymm0, [rel len_masks + 32*I] ; 32 for ymm, 16 for xmm
APPEND(skip_,I):
%assign I (I+1)
%endrep
	vmovdqa	[state + _lens], ymm0

	vphminposuw	xmm1, xmm0
	vpextrw	DWORD(len2), xmm1, 0	; min value
	vpextrw	DWORD(idx), xmm1, 1	; min index (0...7)

        vmovdqa	xmm2, [state + _lens + 8*2]
        vphminposuw	xmm3, xmm2
        vpextrw	DWORD(len_upper), xmm3, 0	; min value
        vpextrw	DWORD(idx_upper), xmm3, 1	; min index (8...F)

	cmp	len2, len_upper
	jle	use_min

	vmovdqa	xmm1, xmm3
	mov	len2, len_upper
	mov	idx, idx_upper	; idx would be in range 0..7
	add	idx, 8		; to reflect that index is in 8..F range

use_min:
	DBGPRINTL64 "FLUSH min_length", len2
	DBGPRINTL64 "FLUSH min_length index ", idx
	cmp	len2, 0
	je	len_is_0

	vpbroadcastw	xmm1, xmm1
	DBGPRINTL_XMM "FLUSH lens after shuffle", xmm1

	vpsubw	xmm0, xmm0, xmm1
	vmovdqa	[state + _lens], xmm0
	vpsubw	xmm2, xmm2, xmm1
	vmovdqa	[state + _lens + 8*2], xmm2
	DBGPRINTL_XMM "FLUSH lens immediately after min subtraction (0..7)", xmm0
	DBGPRINTL_XMM "FLUSH lens immediately after min subtraction (8..F)", xmm2

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	sha1_x16_avx512
	; state and idx are intact

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
        DBGPRINTL "FLUSH *** ---------- return null"
	xor	job_rax, job_rax
	jmp	return

	align	16
end_loop:
	mov	job_rax, [lane_data + _job_in_lane]
	mov	qword [lane_data + _job_in_lane], 0
	or	dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH

	mov	unused_lanes, [state + _unused_lanes]
	shl	unused_lanes, 4	 ;; a nibble
	or	unused_lanes, idx
	mov	[state + _unused_lanes], unused_lanes

	sub	dword [state + _num_lanes_inuse_sha1], 1

	mov	p, [job_rax + _auth_tag_output]

	; copy 12 bytes
	mov	DWORD(tmp2), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 0*SHA1_DIGEST_ROW_SIZE]
	mov	DWORD(tmp4), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 1*SHA1_DIGEST_ROW_SIZE]
	mov	DWORD(r12), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 2*SHA1_DIGEST_ROW_SIZE]
	bswap	DWORD(tmp2)
	bswap	DWORD(tmp4)
	bswap	DWORD(r12)
	mov	[p + 0*4], DWORD(tmp2)
	mov	[p + 1*4], DWORD(tmp4)
	mov	[p + 2*4], DWORD(r12)

        cmp     qword [job_rax + _auth_tag_output_len_in_bytes], 12
        je      clear_ret

        ;; copy remaining 8 bytes to return 20 byte digest
        mov	DWORD(r13), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 3*SHA1_DIGEST_ROW_SIZE]
        mov	DWORD(r14), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 4*SHA1_DIGEST_ROW_SIZE]
        bswap	DWORD(r13)
        bswap	DWORD(r14)
        mov	[p + 3*SHA1_DIGEST_WORD_SIZE], DWORD(r13)
        mov	[p + 4*SHA1_DIGEST_WORD_SIZE], DWORD(r14)

clear_ret:

%ifdef SAFE_DATA
        vpxorq  zmm0, zmm0

        ;; Clear digest (20B), outer_block (20B) and extra_block (64B)
        ;; of returned job and NULL jobs
%assign I 0
%rep 16
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
        vmovdqu64 [lane_data + _extra_block], zmm0

        ;; Clear first 20 bytes of outer_block
        vmovdqu64 [lane_data + _outer_block], xmm0
        mov     dword [lane_data + _outer_block + 16], 0

APPEND(skip_clear_,I):
%assign I (I+1)
%endrep

%endif ;; SAFE_DATA

return:
        DBGPRINTL "---------- exit hmac flush avx512 -----------"
        vzeroupper

        mov	rbp, [rsp + _gpr_save + 8*0]
        mov	r12, [rsp + _gpr_save + 8*1]
        mov	r13, [rsp + _gpr_save + 8*2]
        mov	r14, [rsp + _gpr_save + 8*3]
        mov	r15, [rsp + _gpr_save + 8*4]
	mov	rsp, [rsp + _rsp_save]
	ret

mksection stack-noexec

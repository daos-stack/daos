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
;; Linux clobbers:	RAX     RCX RDX     RSI RDI R8  R9  R10 R11
;; Linux preserves:	    RBX         RBP                         R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Clobbers ZMM0-31

%include "include/os.asm"
%include "include/imb_job.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/reg_sizes.asm"
%include "include/memcpy.asm"
%include "include/const.inc"
%include "include/cet.inc"
;; %define DO_DBGPRINT
%include "include/dbgprint.asm"

extern sha1_x16_avx512

mksection .rodata
default rel

align 16
byteswap:
	dq 0x0405060700010203
	dq 0x0c0d0e0f08090a0b

mksection .text

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

; idx needs to be in rbx, rdi, rbp
%define last_len        rbp
%define idx             rbp

%define p               r11
%define start_offset    r11

%define unused_lanes    r12
%define tmp4            r12

%define job_rax         rax
%define len             rax

%define size_offset     reg3
%define tmp2		reg3

%define lane            reg4
%define tmp3		reg4

%define extra_blocks    r8

%define tmp             r9
%define p2              r9

%define lane_data       r10
%define num_lanes_inuse r12
%define len_upper	r13
%define idx_upper	r14
%endif

; we clobber rsi, rdi, rbp, r12; called routine clobbers also r9-r15
struc STACK
_gpr_save:	resq	7
_rsp_save:	resq	1
endstruc

; JOB* submit_job_hmac_avx(MB_MGR_HMAC_SHA_1_OOO *state, IMB_JOB *job)
; arg 1 : rcx : state
; arg 2 : rdx : job
MKGLOBAL(submit_job_hmac_avx512,function,internal)
submit_job_hmac_avx512:
        endbranch64
        mov	rax, rsp
        sub	rsp, STACK_size
        and	rsp, -32		; align to 32 byte boundary
        mov	[rsp + _gpr_save + 8*0], rbp
        mov	[rsp + _gpr_save + 8*1], r12
        mov	[rsp + _gpr_save + 8*2], r13
        mov	[rsp + _gpr_save + 8*3], r14
        mov	[rsp + _gpr_save + 8*4], r15
%ifndef LINUX
        mov	[rsp + _gpr_save + 8*5], rsi
        mov	[rsp + _gpr_save + 8*6], rdi
%endif
        mov	[rsp + _rsp_save], rax
        DBGPRINTL "---------- enter sha1 submit -----------"

        mov	unused_lanes, [state + _unused_lanes]
        mov	lane, unused_lanes
        and	lane, 0xF           ;; just a nibble
        shr	unused_lanes, 4
        imul	lane_data, lane, _HMAC_SHA1_LANE_DATA_size
        lea	lane_data, [state + _ldata + lane_data]
        mov	[state + _unused_lanes], unused_lanes
	DBGPRINTL64 "lane", lane
	DBGPRINTL64 "unused_lanes", unused_lanes

	add	dword [state + _num_lanes_inuse_sha1], 1

        mov	len, [job + _msg_len_to_hash_in_bytes]
        mov	tmp, len
        shr	tmp, 6	; divide by 64, len in terms of blocks

        mov	[lane_data + _job_in_lane], job
        mov	dword [lane_data + _outer_done], 0
        VPINSRW_M256 state + _lens, xmm0, xmm1, p, lane, tmp, scale_x16

        mov	last_len, len
	DBGPRINTL64 "last_len", last_len
        and	last_len, 63
        lea	extra_blocks, [last_len + 9 + 63]
        shr	extra_blocks, 6
	DBGPRINTL64 "extra_blocks", extra_blocks
        mov	[lane_data + _extra_blocks], DWORD(extra_blocks)

        mov	p, [job + _src]
        add	p, [job + _hash_start_src_offset_in_bytes]
        mov	[state + _args_data_ptr + PTR_SZ*lane], p
        cmp	len, 64
        jb	copy_lt64

fast_copy:
	vmovdqu32	zmm0, [p - 64 + len]
	vmovdqu32	[lane_data + _extra_block], zmm0

end_fast_copy:
        mov	size_offset, extra_blocks
        shl	size_offset, 6
        sub	size_offset, last_len
        add	size_offset, 64-8
        mov	[lane_data + _size_offset], DWORD(size_offset)
        mov	start_offset, 64
        sub	start_offset, last_len
        mov	[lane_data + _start_offset], DWORD(start_offset)

        lea	tmp, [8*64 + 8*len]
        bswap	tmp
        mov	[lane_data + _extra_block + size_offset], tmp

        mov	tmp, [job + _auth_key_xor_ipad]
        vmovdqu	xmm0, [tmp]
        mov	DWORD(tmp),  [tmp + 4*4]
        vmovd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*lane + 0*SHA1_DIGEST_ROW_SIZE], xmm0
        vpextrd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*lane + 1*SHA1_DIGEST_ROW_SIZE], xmm0, 1
        vpextrd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*lane + 2*SHA1_DIGEST_ROW_SIZE], xmm0, 2
        vpextrd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*lane + 3*SHA1_DIGEST_ROW_SIZE], xmm0, 3
        mov	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*lane + 4*SHA1_DIGEST_ROW_SIZE], DWORD(tmp)

        test	len, ~63
        jnz	ge64_bytes

lt64_bytes:
        DBGPRINTL64 "lt64_bytes extra_blocks", extra_blocks
        DBGPRINTL64 "lt64_bytes start_offset", start_offset
        mov	[state + _lens + 2*lane], WORD(extra_blocks)
        lea	tmp, [lane_data + _extra_block + start_offset]
        mov	[state + _args_data_ptr + PTR_SZ*lane], tmp
        mov	dword [lane_data + _extra_blocks], 0

ge64_bytes:
	mov	DWORD(num_lanes_inuse), [state + _num_lanes_inuse_sha1]
        cmp	num_lanes_inuse, 0x10 ; all 16 lanes used?
        jne	return_null
        jmp	start_loop

        align	16
start_loop:
        ; Find min length
        vmovdqa	xmm0, [state + _lens]
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
        cmp	len2, 0
        je	len_is_0

        DBGPRINTL64 "min_length", len2
        DBGPRINTL64 "min_length index ", idx

        vpbroadcastw	xmm1, xmm1
        DBGPRINTL_XMM "SUBMIT lens after shuffle", xmm1

        vpsubw	xmm0, xmm0, xmm1
        vmovdqa	[state + _lens + 0*2], xmm0
        vpsubw	xmm2, xmm2, xmm1
        vmovdqa	[state + _lens + 8*2], xmm2
        DBGPRINTL_XMM "lengths after subtraction (0..7)", xmm0
        DBGPRINTL_XMM "lengths after subtraction (8..F)", xmm2

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
        VPINSRW_M256 state + _lens, xmm0, xmm1, p, idx, 1, scale_x16
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
        mov	[lane_data + _outer_block + 4*SHA1_DIGEST_WORD_SIZE], DWORD(tmp)

        mov	tmp, [job + _auth_key_xor_opad]
        vmovdqu	xmm0, [tmp]
        mov	DWORD(tmp),  [tmp + 4*4]
        vmovd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 0*SHA1_DIGEST_ROW_SIZE], xmm0
        vpextrd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 1*SHA1_DIGEST_ROW_SIZE], xmm0, 1
        vpextrd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 2*SHA1_DIGEST_ROW_SIZE], xmm0, 2
        vpextrd	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 3*SHA1_DIGEST_ROW_SIZE], xmm0, 3
        mov	[state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 4*SHA1_DIGEST_ROW_SIZE], DWORD(tmp)

	jmp	start_loop

        align	16
proc_extra_blocks:
        mov	DWORD(start_offset), [lane_data + _start_offset]
        VPINSRW_M256 state + _lens, xmm0, xmm1, p2, idx, extra_blocks, scale_x16
        lea	tmp, [lane_data + _extra_block + start_offset]
        mov	[state + _args_data_ptr + PTR_SZ*idx], tmp
        mov	dword [lane_data + _extra_blocks], 0
        jmp	start_loop

        align	16
copy_lt64:
        ;; less than one message block of data
        ;; beginning of source block
        ;; destination extrablock but backwards by len from where 0x80 pre-populated
        lea	p2, [lane_data + _extra_block  + 64]
        sub     p2, len
        memcpy_avx2_64_1 p2, p, len, tmp4, tmp2, ymm0, ymm1
        mov	unused_lanes, [state + _unused_lanes]
        jmp	end_fast_copy

return_null:
        xor	job_rax, job_rax
        jmp	return

        align	16
end_loop:
        mov	job_rax, [lane_data + _job_in_lane]
        or	dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
        mov	qword [lane_data + _job_in_lane], 0

        mov	unused_lanes, [state + _unused_lanes]
        shl	unused_lanes, 4
        or	unused_lanes, idx
        mov	[state + _unused_lanes], unused_lanes

	sub	dword [state + _num_lanes_inuse_sha1], 1

	mov	p, [job_rax + _auth_tag_output]

        ; copy 12 bytes
        mov	DWORD(tmp),  [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 0*SHA1_DIGEST_ROW_SIZE]
        mov	DWORD(tmp2), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 1*SHA1_DIGEST_ROW_SIZE]
        mov	DWORD(tmp3), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 2*SHA1_DIGEST_ROW_SIZE]
        bswap	DWORD(tmp)
        bswap	DWORD(tmp2)
        bswap	DWORD(tmp3)
        mov	[p + 0*SHA1_DIGEST_WORD_SIZE], DWORD(tmp)
        mov	[p + 1*SHA1_DIGEST_WORD_SIZE], DWORD(tmp2)
        mov	[p + 2*SHA1_DIGEST_WORD_SIZE], DWORD(tmp3)

        cmp     qword [job_rax + _auth_tag_output_len_in_bytes], 12
        je      clear_ret

        ;; copy remaining 8 bytes to return 20 byte digest
        mov	DWORD(tmp),  [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 3*SHA1_DIGEST_ROW_SIZE]
        mov	DWORD(tmp2), [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 4*SHA1_DIGEST_ROW_SIZE]
        bswap	DWORD(tmp)
        bswap	DWORD(tmp2)
        mov	[p + 3*SHA1_DIGEST_WORD_SIZE], DWORD(tmp)
        mov	[p + 4*SHA1_DIGEST_WORD_SIZE], DWORD(tmp2)

clear_ret:

%ifdef SAFE_DATA
        ;; Clear digest (20B), outer_block (20B) and extra_block (64B) of returned job
        mov     dword [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 0*SHA1_DIGEST_ROW_SIZE], 0
        mov     dword [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 1*SHA1_DIGEST_ROW_SIZE], 0
        mov     dword [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 2*SHA1_DIGEST_ROW_SIZE], 0
        mov     dword [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 3*SHA1_DIGEST_ROW_SIZE], 0
        mov     dword [state + _args_digest + SHA1_DIGEST_WORD_SIZE*idx + 4*SHA1_DIGEST_ROW_SIZE], 0

        vpxorq  zmm0, zmm0
        imul    lane_data, idx, _HMAC_SHA1_LANE_DATA_size
        lea     lane_data, [state + _ldata + lane_data]

        ;; Clear first 64 bytes of extra_block
        vmovdqu64 [lane_data + _extra_block], zmm0

        ;; Clear first 20 bytes of outer_block
        vmovdqu64 [lane_data + _outer_block], xmm0
        mov     dword [lane_data + _outer_block + 16], 0
%endif

return:
        vzeroupper

        DBGPRINTL "---------- exit sha1 submit -----------"
        mov	rbp, [rsp + _gpr_save + 8*0]
        mov	r12, [rsp + _gpr_save + 8*1]
        mov	r13, [rsp + _gpr_save + 8*2]
        mov	r14, [rsp + _gpr_save + 8*3]
        mov	r15, [rsp + _gpr_save + 8*4]
%ifndef LINUX
        mov	rsi, [rsp + _gpr_save + 8*5]
        mov	rdi, [rsp + _gpr_save + 8*6]
%endif
        mov	rsp, [rsp + _rsp_save]

        ret

mksection stack-noexec

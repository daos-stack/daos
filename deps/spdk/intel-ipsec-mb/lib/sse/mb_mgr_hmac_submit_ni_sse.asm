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
;;
;; Linux/Windows clobbers: xmm0 - xmm15
;;

%include "include/os.asm"
%include "include/imb_job.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/reg_sizes.asm"
%include "include/memcpy.asm"

;%define DO_DBGPRINT
%include "include/dbgprint.asm"

extern sha1_ni

mksection .rodata
default rel

align 16
byteswap:
	dq 0x0405060700010203
	dq 0x0c0d0e0f08090a0b

mksection .text

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
%define last_len        rbp
%define idx             rbp
%define p4              rbp

%define p               r11
%define start_offset    r11

%define unused_lanes    rbx
%define tmp4            rbx
%define p3		rbx

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

struc STACK
_gpr_save:	resq	4
_rsp_save:	resq	1
endstruc

; JOB* submit_job_hmac_ni_sse(MB_MGR_HMAC_SHA_1_OOO *state, IMB_JOB *job)
; arg 1 : rcx : state
; arg 2 : rdx : job
MKGLOBAL(submit_job_hmac_ni_sse,function,internal)
submit_job_hmac_ni_sse:

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

        DBGPRINTL "enter sha1-ni-sse submit"
        mov	unused_lanes, [state + _unused_lanes]
        movzx	lane, BYTE(unused_lanes)
	DBGPRINTL64 "lane: ", lane
        shr	unused_lanes, 8
        imul	lane_data, lane, _HMAC_SHA1_LANE_DATA_size
        lea	lane_data, [state + _ldata + lane_data]
        mov	[state + _unused_lanes], unused_lanes
        mov	len, [job + _msg_len_to_hash_in_bytes]
	DBGPRINTL64 "length: ", len
        mov	tmp, len
        shr	tmp, 6	; divide by 64, len in terms of blocks

        mov	[lane_data + _job_in_lane], job
        mov	dword [lane_data + _outer_done], 0
        mov	[state + _lens + 2*lane], WORD(tmp)

        mov	last_len, len
        and	last_len, 63
        lea	extra_blocks, [last_len + 9 + 63]
        shr	extra_blocks, 6
        mov	[lane_data + _extra_blocks], DWORD(extra_blocks)

        mov	p, [job + _src]
        add	p, [job + _hash_start_src_offset_in_bytes]
	DBGPRINTL64 "src pointer + offset:", p
        mov	[state + _args_data_ptr + PTR_SZ*lane], p
        cmp	len, 64
        jb	copy_lt64

fast_copy:
        add	p, len
        movdqu	xmm0, [p - 64 + 0*16]
        movdqu	xmm1, [p - 64 + 1*16]
        movdqu	xmm2, [p - 64 + 2*16]
        movdqu	xmm3, [p - 64 + 3*16]
        movdqa	[lane_data + _extra_block + 0*16], xmm0
        movdqa	[lane_data + _extra_block + 1*16], xmm1
        movdqa	[lane_data + _extra_block + 2*16], xmm2
        movdqa	[lane_data + _extra_block + 3*16], xmm3
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
        movdqu	xmm0, [tmp]
        mov	DWORD(tmp),  [tmp + 4*SHA1_DIGEST_WORD_SIZE]
%if SHA1NI_DIGEST_ROW_SIZE != 20
%error "Below code has been optimized for SHA1NI_DIGEST_ROW_SIZE = 20!"
%endif
	lea	p4, [lane + lane*4]
        movdqu	[state + _args_digest + p4*4 + 0*SHA1_DIGEST_WORD_SIZE], xmm0
        mov	[state + _args_digest + p4*4 + 4*SHA1_DIGEST_WORD_SIZE], DWORD(tmp)
        test	len, ~63
        jnz	ge64_bytes

lt64_bytes:
        mov	[state + _lens + 2*lane], WORD(extra_blocks)
        lea	tmp, [lane_data + _extra_block + start_offset]
        mov	[state + _args_data_ptr + PTR_SZ*lane], tmp
        mov	dword [lane_data + _extra_blocks], 0

ge64_bytes:
        cmp	unused_lanes, 0xff
        jne	return_null
        jmp	start_loop

        align	16
start_loop:
	; Find min length - only two lanes available
	xor     len2, len2
	mov	p3, 0x10000
	mov	WORD(len2), word [state + _lens + 0*2]	; [0:15] - lane 0 length, [16:31] - lane index (0)
	mov	WORD(p3), word [state + _lens + 1*2]	; [0:15] - lane 1 length, [16:31] - lane index (1)
	cmp     WORD(len2), WORD(p3)
	cmovg	DWORD(len2), DWORD(p3)	; move if lane 0 length is greater than lane 1 length

	mov	idx, len2		; retrieve index & length from [16:31] and [0:15] bit fields
	shr	DWORD(idx), 16
	and	DWORD(len2), 0xffff
	je	len_is_0

	sub	word [state + _lens + 0*2], WORD(len2)
	sub	word [state + _lens + 1*2], WORD(len2)

	; "state" and "args" are the same address, arg1
	; len is arg2
        call	sha1_ni
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

%if SHA1NI_DIGEST_ROW_SIZE != 20
%error	"Below code has been optimized for SHA1NI_DIGEST_ROW_SIZE = 20!"
%endif
	lea	p3, [idx + idx*4]
        movdqu	xmm0, [state + _args_digest + p3*4 + 0*SHA1_DIGEST_WORD_SIZE]
        pshufb	xmm0, [rel byteswap]
        mov	DWORD(tmp),  [state + _args_digest + p3*4 + 4*SHA1_DIGEST_WORD_SIZE]
        bswap	DWORD(tmp)
        movdqa	[lane_data + _outer_block], xmm0
        mov	[lane_data + _outer_block + 4*SHA1_DIGEST_WORD_SIZE], DWORD(tmp)

        mov	tmp, [job + _auth_key_xor_opad]
        movdqu	xmm0, [tmp]
        mov	DWORD(tmp),  [tmp + 4*SHA1_DIGEST_WORD_SIZE]
        movdqu	[state + _args_digest + p3*4 + 0*SHA1_DIGEST_WORD_SIZE], xmm0
        mov	[state + _args_digest + p3*4 + 4*SHA1_DIGEST_WORD_SIZE], DWORD(tmp)
        jmp	start_loop

        align	16
proc_extra_blocks:
        mov	DWORD(start_offset), [lane_data + _start_offset]
        mov	[state + _lens + 2*idx], WORD(extra_blocks)
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
        memcpy_sse_64_1 p2, p, len, tmp4, tmp2, xmm0, xmm1, xmm2, xmm3
        mov	unused_lanes, [state + _unused_lanes]
        jmp	end_fast_copy

return_null:
        xor	job_rax, job_rax
        jmp	return

        align	16
end_loop:
        mov	job_rax, [lane_data + _job_in_lane]
        mov	unused_lanes, [state + _unused_lanes]
        mov	qword [lane_data + _job_in_lane], 0
        or	dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
        shl	unused_lanes, 8
        or	unused_lanes, idx
        mov	[state + _unused_lanes], unused_lanes

        mov	p, [job_rax + _auth_tag_output]

        ; copy 12 bytes
%if SHA1NI_DIGEST_ROW_SIZE != 20
%error	"Below code has been optimized for SHA1NI_DIGEST_ROW_SIZE = 20!"
%endif
	lea	idx, [idx + 4*idx]
        mov	DWORD(tmp),  [state + _args_digest + idx*4 + 0*SHA1_DIGEST_WORD_SIZE]
        mov	DWORD(tmp2), [state + _args_digest + idx*4 + 1*SHA1_DIGEST_WORD_SIZE]
        mov	DWORD(tmp3), [state + _args_digest + idx*4 + 2*SHA1_DIGEST_WORD_SIZE]
        bswap	DWORD(tmp)
        bswap	DWORD(tmp2)
        bswap	DWORD(tmp3)
        mov	[p + 0*SHA1_DIGEST_WORD_SIZE], DWORD(tmp)
        mov	[p + 1*SHA1_DIGEST_WORD_SIZE], DWORD(tmp2)
        mov	[p + 2*SHA1_DIGEST_WORD_SIZE], DWORD(tmp3)

        cmp     qword [job_rax + _auth_tag_output_len_in_bytes], 12
        je      clear_ret

        ;; copy remaining 8 bytes to return 20 byte digest
        mov	DWORD(tmp),  [state + _args_digest + idx*4 + 3*SHA1_DIGEST_WORD_SIZE]
        mov	DWORD(tmp2), [state + _args_digest + idx*4 + 4*SHA1_DIGEST_WORD_SIZE]
        bswap	DWORD(tmp)
        bswap	DWORD(tmp2)
        mov	[p + 3*4], DWORD(tmp)
        mov	[p + 4*4], DWORD(tmp2)

clear_ret:

%ifdef SAFE_DATA
        pxor    xmm0, xmm0
        ;; Clear digest (20B), outer_block (20B) and extra_block (64B)
        ;; idx = 0 or 5 (depending on lane)
        movdqu  [state + _args_digest + idx*4], xmm0
        mov     dword [state + _args_digest + idx*4 + 16], 0

        shr     idx, 2 ;; idx == 5 ? 1 : 0
        imul    lane_data, idx, _HMAC_SHA1_LANE_DATA_size
        lea     lane_data, [state + _ldata + lane_data]
        ;; Clear first 64 bytes of extra_block
%assign offset 0
%rep 4
        movdqa  [lane_data + _extra_block + offset], xmm0
%assign offset (offset + 16)
%endrep

        ;; Clear 20 bytes of outer_block
        movdqa  [lane_data + _outer_block], xmm0
        mov     dword [lane_data + _outer_block + 16], 0
%endif

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

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
;; Windows clobbers:	RAX     RCX RDX             R8
;; Windows preserves:	    RBX         RBP RSI RDI     R9  R10 R11 R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Linux clobbers:	RAX                 RSI RDI R8
;; Linux preserves:	    RBX RCX RDX RBP             R9  R10 R11 R12 R13 R14 R15
;;			-----------------------------------------------------------
;;
;; Linux/Windows clobbers: xmm0 - xmm15
;;

%include "include/os.asm"
%include "include/imb_job.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/reg_sizes.asm"

;%define DO_DBGPRINT
%include "include/dbgprint.asm"

extern sha1_ni

mksection .rodata
default rel

align 16
byteswap:	;ddq 0x0c0d0e0f08090a0b0405060700010203
	dq 0x0405060700010203, 0x0c0d0e0f08090a0b
one:
	dq 1

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
%define p2		r8

; This routine clobbers rbx, rbp
struc STACK
_gpr_save:	resq	4
_rsp_save:	resq	1
endstruc

%define APPEND(a,b) a %+ b

; JOB* flush_job_hmac_ni_sse(MB_MGR_HMAC_SHA_1_OOO *state)
; arg 1 : state
MKGLOBAL(flush_job_hmac_ni_sse,function,internal)
flush_job_hmac_ni_sse:

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

        DBGPRINTL "enter sha1-ni-sse flush"
	mov	unused_lanes, [state + _unused_lanes]
	bt	unused_lanes, 16+7
	jc	return_null

	; find a lane with a non-null job, assume it is 0 then check 1
	xor	idx, idx
	cmp	qword [state + _ldata + 1 * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [rel one]
	DBGPRINTL64 "idx:", idx

copy_lane_data:
	; copy valid lane (idx) to empty lanes
	mov	tmp, [state + _args_data_ptr + PTR_SZ*idx]
	movzx	len2, word [state + _lens + idx*2]

	DBGPRINTL64 "ptr", tmp

	; there are only two lanes so if one is empty it is easy to determine which one
	xor	idx, 1
	mov	[state + _args_data_ptr + PTR_SZ*idx], tmp
	xor	idx, 1

	; No need to find min length - only two lanes available
        cmp	len2, 0
        je	len_is_0

	; Set length on both lanes to 0
	mov	dword [state + _lens], 0

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
	DBGPRINTL64 "outer-block-index", idx
	lea	tmp, [lane_data + _outer_block]
	DBGPRINTL64 "outer block ptr:", tmp
	mov	[state + _args_data_ptr + PTR_SZ*idx], tmp

        ;; idx determines which column
        ;; read off from consecutive rows
%if SHA1NI_DIGEST_ROW_SIZE != 20
%error "Below code has been optimized for SHA1NI_DIGEST_ROW_SIZE = 20!"
%endif
	lea	p2, [idx + idx*4]
	movdqu	xmm0, [state + _args_digest + p2*4]
	pshufb	xmm0, [rel byteswap]
	mov	DWORD(tmp),  [state + _args_digest + p2*4 + 4*SHA1_DIGEST_WORD_SIZE]
	bswap	DWORD(tmp)
	movdqa	[lane_data + _outer_block], xmm0
	mov	[lane_data + _outer_block + 4*SHA1_DIGEST_WORD_SIZE], DWORD(tmp)
        DBGPRINTL_XMM "sha1 outer hash input words[0-3]", xmm0
        DBGPRINTL64 "sha1 outer hash input word 4", tmp
	mov	job, [lane_data + _job_in_lane]
	mov	tmp, [job + _auth_key_xor_opad]
	movdqu	xmm0, [tmp]
	mov	DWORD(tmp),  [tmp + 4*SHA1_DIGEST_WORD_SIZE]
	movdqu	[state + _args_digest + p2*4], xmm0
	mov	[state + _args_digest + p2*4 + 4*SHA1_DIGEST_WORD_SIZE], DWORD(tmp)

	jmp	copy_lane_data

	align	16
proc_extra_blocks:
	mov	DWORD(start_offset), [lane_data + _start_offset]
	DBGPRINTL64 "extra blocks-start offset", start_offset
	mov	[state + _lens + 2*idx], WORD(extra_blocks)
	DBGPRINTL64 "extra blocks-len", extra_blocks
	lea	tmp, [lane_data + _extra_block + start_offset]
	DBGPRINTL64 "extra block ptr", tmp
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
%if SHA1NI_DIGEST_ROW_SIZE != 20
%error "Below code has been optimized for SHA1NI_DIGEST_ROW_SIZE = 20!"
%endif
	lea	idx, [idx + idx*4]
	mov	DWORD(tmp2), [state + _args_digest + idx*4 + 0*SHA1_DIGEST_WORD_SIZE]
	mov	DWORD(tmp4), [state + _args_digest + idx*4 + 1*SHA1_DIGEST_WORD_SIZE]
	bswap	DWORD(tmp2)
	bswap	DWORD(tmp4)
	mov	[p + 0*4], DWORD(tmp2)
	mov	[p + 1*4], DWORD(tmp4)
	mov	DWORD(tmp2), [state + _args_digest + idx*4 + 2*SHA1_DIGEST_WORD_SIZE]
	bswap	DWORD(tmp2)
	mov	[p + 2*4], DWORD(tmp2)

        cmp     qword [job_rax + _auth_tag_output_len_in_bytes], 12
        je      clear_ret

        ;; copy remaining 8 bytes to return 20 byte digest
        mov	DWORD(tmp2), [state + _args_digest + idx*4 + 3*SHA1_DIGEST_WORD_SIZE]
        mov	DWORD(tmp4), [state + _args_digest + idx*4 + 4*SHA1_DIGEST_WORD_SIZE]
        bswap	DWORD(tmp2)
        bswap	DWORD(tmp4)
        mov	[p + 3*4], DWORD(tmp2)
        mov	[p + 4*4], DWORD(tmp4)

clear_ret:

%ifdef SAFE_DATA
        pxor    xmm0, xmm0

        ;; Clear digest (20B), outer_block (20B) and extra_block (64B)
        ;; of returned job and NULL jobs
%assign I 0
%rep 2
	cmp	qword [state + _ldata + (I*_HMAC_SHA1_LANE_DATA_size) + _job_in_lane], 0
	jne	APPEND(skip_clear_,I)

        ;; Clear digest
        movdqu  [state + _args_digest + I*20], xmm0
        mov     dword [state + _args_digest + I*20 + 16], 0

        lea     lane_data, [state + _ldata + (I*_HMAC_SHA1_LANE_DATA_size)]
        ;; Clear first 64 bytes of extra_block
%assign offset 0
%rep 4
        movdqa  [lane_data + _extra_block + offset], xmm0
%assign offset (offset + 16)
%endrep

        ;; Clear first 20 bytes of outer_block
        movdqa  [lane_data + _outer_block], xmm0
        mov     dword [lane_data + _outer_block + 16], 0

APPEND(skip_clear_,I):
%assign I (I+1)
%endrep

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

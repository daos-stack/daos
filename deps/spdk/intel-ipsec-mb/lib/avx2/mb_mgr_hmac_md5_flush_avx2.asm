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
%include "include/cet.inc"
;%define DO_DBGPRINT
%include "include/dbgprint.asm"
extern md5_x8x2_avx2

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

lane_1:     dq  1
lane_2:     dq  2
lane_3:     dq  3
lane_4:     dq  4
lane_5:     dq  5
lane_6:     dq  6
lane_7:     dq  7
lane_8:     dq  8
lane_9:     dq  9
lane_10:    dq  10
lane_11:    dq  11
lane_12:    dq  12
lane_13:    dq  13
lane_14:    dq  14
lane_15:    dq  15

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

%define tmp4		    r8
%define tmp5		    r9
%define num_lanes_inuse     r12
%define len_upper           r13
%define idx_upper           r14
%endif

; This routine and/or the called routine clobbers all GPRs
struc STACK
_gpr_save:	resq	8
_rsp_save:	resq	1
endstruc

%define APPEND(a,b) a %+ b

; JOB* flush_job_hmac_md5_avx(MB_MGR_HMAC_MD5_OOO *state)
; arg 1 : rcx : state
MKGLOBAL(flush_job_hmac_md5_avx2,function,internal)
flush_job_hmac_md5_avx2:
        endbranch64
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
	mov	[rsp + _gpr_save + 8*6], rsi
	mov	[rsp + _gpr_save + 8*7], rdi
%endif
	mov	[rsp + _rsp_save], rax	; original SP

        DBGPRINTL "---------- enter md5 flush -----------"
        mov DWORD(num_lanes_inuse), [state + _num_lanes_inuse_md5]  ;; empty?
	cmp	num_lanes_inuse, 0
        jz  return_null

        ; find a lane with a non-null job -- flush does not have to be efficient!
        mov     idx, 0
        %assign I 1
%rep 15
        cmp     qword [state + _ldata_md5 + I * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
        cmovne  idx, [rel APPEND(lane_,I)]
%assign I (I+1)
%endrep

copy_lane_data:
        endbranch64
        ; copy good lane (idx) to empty lanes
        mov	tmp, [state + _args_data_ptr_md5 + PTR_SZ*idx]
        ;; tackle lower 8 lanes
	vmovdqa	xmm0, [state + _lens_md5 + 0*16]  ;; lower 8 lengths
%assign I 0
%rep 8
        cmp	qword [state + _ldata_md5 + I * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
        jne	APPEND(lower_skip_,I)
        mov	[state + _args_data_ptr_md5 + PTR_SZ*I], tmp
        vpor	xmm0, xmm0, [rel len_masks + 16*I]
APPEND(lower_skip_,I):
%assign I (I+1)
%endrep
        ;; tackle upper lanes
        vmovdqa	xmm1, [state + _lens_md5 + 1*16]  ;; upper 8 lengths
%assign  I 0
%rep 8
        cmp	qword [state + _ldata_md5 + (8 + I) * _HMAC_SHA1_LANE_DATA_size + _job_in_lane], 0
        jne	APPEND(upper_skip_,I)
        mov	[state + _args_data_ptr_md5 + PTR_SZ*(8+I)], tmp
        vpor xmm1, xmm1, [rel len_masks + 16*I]
APPEND(upper_skip_,I):
%assign I (I+1)
%endrep
        jmp	start_loop0

        align	32
start_loop0:
        endbranch64
        ; Find min length
        vphminposuw	xmm2, xmm0
        vpextrw	DWORD(len2), xmm2, 0	; min value
        vpextrw	DWORD(idx), xmm2, 1	; min index (0...7)

        vphminposuw	xmm3, xmm1
        vpextrw	DWORD(len_upper), xmm3, 0	; min value
        vpextrw	DWORD(idx_upper), xmm3, 1	; min index (8...F)

        cmp len2, len_upper
        jle use_min

min_in_high:
        vmovdqa xmm2, xmm3
        mov len2, len_upper
        mov idx, idx_upper
        or  idx, 0x8  ; to reflect that index in 8-F
use_min:
        and len2, len2   ; to set flags
        jz  len_is_0
        DBGPRINTL64 "min_length min_index ", len2, idx
        DBGPRINTL_XMM "FLUSH md5 lens before sub lower", xmm0
        vpbroadcastw	xmm2, xmm2 ; duplicate words across all lanes
        vpsubw	xmm0, xmm0, xmm2
        DBGPRINTL_XMM "FLUSH md5 lens after sub lower", xmm0
        vmovdqa	[state + _lens_md5 + 0*16], xmm0

        vpsubw	xmm1, xmm1, xmm2
        DBGPRINTL_XMM "FLUSH md5 lens after sub upper", xmm1
        vmovdqa	[state + _lens_md5 + 1*16], xmm1

        ; "state" and "args" are the same address, arg1
        ; len is arg2
        call	md5_x8x2_avx2
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

	mov     DWORD(num_lanes_inuse), [state + _num_lanes_inuse_md5]  ;; update lanes inuse
	sub     num_lanes_inuse, 1
	mov     [state + _num_lanes_inuse_md5], DWORD(num_lanes_inuse)

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

	cmp	DWORD [job_rax + _auth_tag_output_len_in_bytes], 12
	je 	clear_ret

	; copy 16 bytes
	mov	DWORD(tmp5), [state + _args_digest_md5 + MD5_DIGEST_WORD_SIZE*idx + 3*MD5_DIGEST_ROW_SIZE]
	mov	[p + 3*4], DWORD(tmp5)

clear_ret:

%ifdef SAFE_DATA
        vpxor   ymm0, ymm0

        ;; Clear digest (16B), outer_block (16B) and extra_block (64B)
        ;; of returned job and NULL jobs
%assign I 0
%rep 16
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
        vmovdqa [lane_data + _extra_block], ymm0
        vmovdqa [lane_data + _extra_block + 32], ymm0

        ;; Clear first 16 bytes of outer_block
        vmovdqa [lane_data + _outer_block], xmm0

APPEND(skip_clear_,I):
%assign I (I+1)
%endrep

%endif ;; SAFE_DATA

return:
        endbranch64
        DBGPRINTL "---------- exit md5 flush -----------"
        vzeroupper

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

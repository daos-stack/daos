;;
;; Copyright (c) 2018-2021, Intel Corporation
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
;%define DO_DBGPRINT
%include "include/dbgprint.asm"

%ifndef NUM_LANES
%define NUM_LANES 4
%endif

%ifndef AES_CBC_MAC
%define AES_CBC_MAC aes128_cbc_mac_x4
%define SUBMIT_JOB_AES_CMAC_AUTH submit_job_aes128_cmac_auth_sse
%define FLUSH_JOB_AES_CMAC_AUTH flush_job_aes128_cmac_auth_sse
%endif

extern AES_CBC_MAC

mksection .rodata
default rel

align 16
len_masks:
	dq 0x000000000000FFFF, 0x0000000000000000
	dq 0x00000000FFFF0000, 0x0000000000000000
	dq 0x0000FFFF00000000, 0x0000000000000000
	dq 0xFFFF000000000000, 0x0000000000000000
%if NUM_LANES > 4
        dq 0x0000000000000000, 0x000000000000FFFF
	dq 0x0000000000000000, 0x00000000FFFF0000
	dq 0x0000000000000000, 0x0000FFFF00000000
	dq 0x0000000000000000, 0xFFFF000000000000
%endif

%if NUM_LANES > 4
align 16
dupw:
	dq 0x0100010001000100, 0x0100010001000100
%endif

one:	dq  1
two:	dq  2
three:	dq  3
%if NUM_LANES > 4
four:	dq  4
five:	dq  5
six:	dq  6
seven:	dq  7
%endif

align 16
len_shuf_masks:
        dq 0XFFFFFFFF09080100, 0XFFFFFFFFFFFFFFFF
        dq 0X09080100FFFFFFFF, 0XFFFFFFFFFFFFFFFF
        dq 0XFFFFFFFFFFFFFFFF, 0XFFFFFFFF09080100
        dq 0XFFFFFFFFFFFFFFFF, 0X09080100FFFFFFFF

mksection .text

%define APPEND(a,b) a %+ b

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

%define job_rax          rax

; idx needs to be in rbp
%define len              rbp
%define idx              rbp
%define tmp              rbp

%define lane             r8

%define iv               r9
%define m_last           r10
%define n                r11

%define unused_lanes     rbx
%define r                rbx

%define tmp3             r12
%define tmp4             r13
%define tmp2             r14

%define good_lane        r15
%define rbits            r15

; STACK_SPACE needs to be an odd multiple of 8
; This routine and its callee clobbers all GPRs
struc STACK
_gpr_save:	resq	8
_rsp_save:	resq	1
endstruc

;;; ===========================================================================
;;; ===========================================================================
;;; MACROS
;;; ===========================================================================
;;; ===========================================================================

;;; ===========================================================================
;;; AES CMAC job submit & flush
;;; ===========================================================================
;;; SUBMIT_FLUSH [in] - SUBMIT, FLUSH job selection
%macro GENERIC_SUBMIT_FLUSH_JOB_AES_CMAC_SSE 1
%define %%SUBMIT_FLUSH %1
        mov	rax, rsp
        sub	rsp, STACK_size
        and	rsp, -16

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

        ;; Find free lane
 	mov	unused_lanes, [state + _aes_cmac_unused_lanes]

%ifidn %%SUBMIT_FLUSH, SUBMIT

 	mov	lane, unused_lanes
        and	lane, 0xF
 	shr	unused_lanes, 4
 	mov	[state + _aes_cmac_unused_lanes], unused_lanes

        ;; Copy job info into lane
 	mov	[state + _aes_cmac_job_in_lane + lane*8], job
        ;; Copy keys into lane args
 	mov	tmp, [job + _key_expanded]
 	mov	[state + _aes_cmac_args_keys + lane*8], tmp
        mov     tmp, lane
        shl     tmp, 4  ; lane*16

        ;; Zero IV to store digest
        pxor    xmm0, xmm0
        movdqa  [state + _aes_cmac_args_IV + tmp], xmm0

        lea     m_last, [state + _aes_cmac_scratch + tmp]

        ;; calculate len
        ;; convert bits to bytes (message length in bits for CMAC)
        mov     len, [job + _msg_len_to_hash_in_bits]
        mov     rbits, len
        add     len, 7      ; inc len if there are remainder bits
        shr     len, 3
        and     rbits, 7

        ;; Check at least 1 or more blocks (get n)
        mov     n, len
        add     n, 0xf
        shr     n, 4

        ;; Check for partial block
        mov     r, len
        and     r, 0xf

        or      n, n   ; check one or more blocks?
        jz      %%_lt_one_block

        ;; One or more blocks, potentially partial
        mov     word [state + _aes_cmac_init_done + lane*2], 0

        mov     tmp2, [job + _src]
        add     tmp2, [job + _hash_start_src_offset_in_bytes]
        mov     [state + _aes_cmac_args_in + lane*8], tmp2

        ;; len = (n-1)*16
        lea     tmp2, [n - 1]
        shl     tmp2, 4
        movdqa  xmm0, [state + _aes_cmac_lens]
        XPINSRW xmm0, xmm1, tmp, lane, tmp2, scale_x16
        movdqa  [state + _aes_cmac_lens], xmm0

        ;; check remainder bits
        or      rbits, rbits
        jnz     %%_not_complete_block_3gpp

        ;; check if complete block
        or      r, r
        jz      %%_complete_block

%%_not_complete_block:
        ;; M_last = padding(M_n) XOR K2
        lea     tmp, [rel padding_0x80_tab16 + 16]
        sub     tmp, r
        movdqu  xmm0, [tmp]
        movdqa  [m_last], xmm0

        mov     tmp, [job + _src]
        add     tmp, [job + _hash_start_src_offset_in_bytes]
        lea     tmp3, [n - 1]
        shl     tmp3, 4
        add     tmp, tmp3

        memcpy_sse_16 m_last, tmp, r, tmp4, iv

        ;; src + n + r
        mov     tmp3, [job + _skey2]
        movdqa  xmm1, [m_last]
        movdqu  xmm0, [tmp3]
        pxor    xmm0, xmm1
        movdqa  [m_last], xmm0

%%_step_5:
        ;; Find min length
        movdqa  xmm0, [state + _aes_cmac_lens]
        phminposuw xmm1, xmm0

        cmp	byte [state + _aes_cmac_unused_lanes], 0xf
        jne	%%_return_null

%else ; end SUBMIT

        ;; Check at least one job
        bt      unused_lanes, ((NUM_LANES * 4) + 3)
	jc      %%_return_null

      	;; Find a lane with a non-null job
	xor	good_lane, good_lane
	cmp	qword [state + _aes_cmac_job_in_lane + 1*8], 0
	cmovne	good_lane, [rel one]
	cmp	qword [state + _aes_cmac_job_in_lane + 2*8], 0
	cmovne	good_lane, [rel two]
	cmp	qword [state + _aes_cmac_job_in_lane + 3*8], 0
	cmovne	good_lane, [rel three]
%if NUM_LANES > 4
	cmp	qword [state + _aes_cmac_job_in_lane + 4*8], 0
	cmovne	good_lane, [rel four]
	cmp	qword [state + _aes_cmac_job_in_lane + 5*8], 0
	cmovne	good_lane, [rel five]
	cmp	qword [state + _aes_cmac_job_in_lane + 6*8], 0
	cmovne	good_lane, [rel six]
	cmp	qword [state + _aes_cmac_job_in_lane + 7*8], 0
	cmovne	good_lane, [rel seven]
%endif

	; Copy good_lane to empty lanes
	mov	tmp2, [state + _aes_cmac_args_in + good_lane*8]
	mov	tmp3, [state + _aes_cmac_args_keys + good_lane*8]
	shl	good_lane, 4 ; multiply by 16
	movdqa	xmm2, [state + _aes_cmac_args_IV + good_lane]
	movdqa	xmm0, [state + _aes_cmac_lens]

%assign I 0
%rep NUM_LANES
	cmp	qword [state + _aes_cmac_job_in_lane + I*8], 0
	jne	APPEND(skip_,I)
	mov	[state + _aes_cmac_args_in + I*8], tmp2
	mov	[state + _aes_cmac_args_keys + I*8], tmp3
	movdqa	[state + _aes_cmac_args_IV + I*16], xmm2
	por	xmm0, [rel len_masks + 16*I]
APPEND(skip_,I):
%assign I (I+1)
%endrep
        ;; Find min length
        phminposuw xmm1, xmm0

%endif ; end FLUSH

%%_cmac_round:
	pextrw	len2, xmm1, 0	; min value
	pextrw	idx, xmm1, 1	; min index (0...3)
        or	len2, len2
	je	%%_len_is_0

%if NUM_LANES > 4
	pshufb	xmm1, [rel dupw]        ; duplicate words across all lanes
%else
        pshuflw	xmm1, xmm1, 0
%endif
        psubw	xmm0, xmm1
	movdqa	[state + _aes_cmac_lens], xmm0

        ; "state" and "args" are the same address, arg1
	; len2 is arg2
	call    AES_CBC_MAC
	; state and idx are intact

        movdqa  xmm0, [state + _aes_cmac_lens]  ; preload lens
%%_len_is_0:
        ; Check if job complete
        test    word [state + _aes_cmac_init_done + idx*2], 0xffff
        jnz     %%_copy_complete_digest

        ; Finish step 6
        mov     word [state + _aes_cmac_init_done + idx*2], 1

        ; Reset NULL lane lens to UINT16_MAX
%ifidn %%SUBMIT_FLUSH, FLUSH
        pxor    xmm1, xmm1
        pcmpeqq xmm1, [state + _aes_cmac_job_in_lane + 0]
        pshufb  xmm1, [rel len_shuf_masks + 0]

        pxor    xmm2, xmm2
        pcmpeqq xmm2, [state + _aes_cmac_job_in_lane + 16]
        pshufb  xmm2, [rel len_shuf_masks + 16]

        por     xmm1, xmm2
        por     xmm0, xmm1

%if NUM_LANES > 4
        pxor    xmm3, xmm3
        pcmpeqq xmm3, [state + _aes_cmac_job_in_lane + 32]
        pshufb  xmm3, [rel len_shuf_masks + 32]

        pxor    xmm4, xmm4
        pcmpeqq xmm4, [state + _aes_cmac_job_in_lane + 48]
        pshufb  xmm4, [rel len_shuf_masks + 48]

        por     xmm3, xmm4
        por     xmm0, xmm3
%endif ; %%NUM_LANES > 4
%endif ; %%SUBMIT_FLUSH == FLUSH

        XPINSRW xmm0, xmm1, tmp3, idx, 16, scale_x16
        movdqa  [state + _aes_cmac_lens], xmm0

        phminposuw xmm1, xmm0 ; find min length

        mov     tmp3, idx
        shl     tmp3, 4  ; idx*16
        lea     m_last, [state + _aes_cmac_scratch + tmp3]
        mov     [state + _aes_cmac_args_in + idx*8], m_last

        jmp     %%_cmac_round

%%_copy_complete_digest:
        ; Job complete, copy digest to AT output
 	mov	job_rax, [state + _aes_cmac_job_in_lane + idx*8]

        mov     tmp4, idx
        shl     tmp4, 4
        lea     tmp3, [state + _aes_cmac_args_IV + tmp4]
        mov     tmp4, [job_rax + _auth_tag_output_len_in_bytes]
        mov     tmp2, [job_rax + _auth_tag_output]

        cmp     tmp4, 16
        jne     %%_ne_16_copy

        ;; 16 byte AT copy
        movdqu  xmm0, [tmp3]
        movdqu  [tmp2], xmm0
        jmp     %%_update_lanes

%%_ne_16_copy:
        memcpy_sse_16 tmp2, tmp3, tmp4, lane, iv

%%_update_lanes:
        ; Update unused lanes
        mov	unused_lanes, [state + _aes_cmac_unused_lanes]
        shl	unused_lanes, 4
 	or	unused_lanes, idx
 	mov	[state + _aes_cmac_unused_lanes], unused_lanes

        ; Set return job
        mov	job_rax, [state + _aes_cmac_job_in_lane + idx*8]

 	mov	qword [state + _aes_cmac_job_in_lane + idx*8], 0
 	or	dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH

%ifdef SAFE_DATA
        pxor    xmm0, xmm0
%ifidn %%SUBMIT_FLUSH, SUBMIT
        ;; Clear digest (in memory for IV) and scratch memory of returned job
        movdqa  [tmp3], xmm0

        shl     idx, 4
        movdqa  [state + _aes_cmac_scratch + idx], xmm0

%else
        ;; Clear digest and scratch memory of returned job and "NULL lanes"
%assign I 0
%rep NUM_LANES
        cmp     qword [state + _aes_cmac_job_in_lane + I*8], 0
        jne     APPEND(skip_clear_,I)
        movdqa  [state + _aes_cmac_args_IV + I*16], xmm0
        movdqa  [state + _aes_cmac_scratch + I*16], xmm0
APPEND(skip_clear_,I):
%assign I (I+1)
%endrep
%endif ;; SUBMIT

%endif ;; SAFE_DATA

%%_return:
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

%%_return_null:
	xor	job_rax, job_rax
	jmp	%%_return

%ifidn %%SUBMIT_FLUSH, SUBMIT
%%_complete_block:

        ;; Block size aligned
        mov     tmp2, [job + _src]
        add     tmp2, [job + _hash_start_src_offset_in_bytes]
        lea     tmp3, [n - 1]
        shl     tmp3, 4
        add     tmp2, tmp3

        ;; M_last = M_n XOR K1
        mov     tmp3, [job + _skey1]
        movdqu  xmm0, [tmp3]
        movdqu  xmm1, [tmp2]
        pxor    xmm0, xmm1
        movdqa  [m_last], xmm0

        jmp     %%_step_5

%%_lt_one_block:
        ;; Single partial block
        mov     word [state + _aes_cmac_init_done + lane*2], 1
        mov     [state + _aes_cmac_args_in + lane*8], m_last

        movdqa  xmm0, [state + _aes_cmac_lens]
        XPINSRW xmm0, xmm1, tmp2, lane, 16, scale_x16
        movdqa  [state + _aes_cmac_lens], xmm0

        mov     n, 1
        jmp     %%_not_complete_block

%%_not_complete_block_3gpp:
        ;; bit pad last block
        ;; xor with skey2
        ;; copy to m_last

        ;; load pointer to src
        mov     tmp, [job + _src]
        add     tmp, [job + _hash_start_src_offset_in_bytes]
        lea     tmp3, [n - 1]
        shl     tmp3, 4
        add     tmp, tmp3

        ;; check if partial block
        or      r, r
        jz      %%_load_full_block_3gpp

        simd_load_sse_15_1 xmm0, tmp, r
        dec     r

%%_update_mlast_3gpp:
        ;; set last byte padding mask
        ;; shift into correct xmm idx

        ;; save and restore rcx on windows
%ifndef LINUX
	mov	tmp, rcx
%endif
        mov     rcx, rbits
        mov     tmp3, 0xff
        shr     tmp3, cl
        movq    xmm2, tmp3
        XPSLLB  xmm2, r, xmm1, tmp2

        ;; pad final byte
        pandn   xmm2, xmm0
%ifndef LINUX
	mov	rcx, tmp
%endif
        ;; set OR mask to pad final bit
        mov     tmp2, tmp3
        shr     tmp2, 1
        xor     tmp2, tmp3 ; XOR to get OR mask
        movq    xmm3, tmp2
        ;; xmm1 contains shift table from previous shift
        pshufb  xmm3, xmm1

        ;; load skey2 address
        mov     tmp3, [job + _skey2]
        movdqu  xmm1, [tmp3]

        ;; set final padding bit
        por     xmm2, xmm3

        ;; XOR last partial block with skey2
        ;; update mlast
        pxor    xmm2, xmm1
        movdqa  [m_last], xmm2

        jmp     %%_step_5

%%_load_full_block_3gpp:
        movdqu  xmm0, [tmp]
        mov     r, 0xf
        jmp     %%_update_mlast_3gpp
%endif
%endmacro

align 64
; IMB_JOB * submit_job_aes_cmac_auth_sse(MB_MGR_CMAC_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_AES_CMAC_AUTH,function,internal)
SUBMIT_JOB_AES_CMAC_AUTH:
        endbranch64
        GENERIC_SUBMIT_FLUSH_JOB_AES_CMAC_SSE SUBMIT

; IMB_JOB * flush_job_aes_cmac_auth_sse(MB_MGR_CMAC_OOO *state)
; arg 1 : state
MKGLOBAL(FLUSH_JOB_AES_CMAC_AUTH,function,internal)
FLUSH_JOB_AES_CMAC_AUTH:
        endbranch64
        GENERIC_SUBMIT_FLUSH_JOB_AES_CMAC_SSE FLUSH

mksection stack-noexec

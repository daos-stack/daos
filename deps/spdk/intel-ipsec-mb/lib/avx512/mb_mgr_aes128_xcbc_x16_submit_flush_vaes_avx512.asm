;;
;; Copyright (c) 2020-2021, Intel Corporation
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
%include "include/cet.inc"
%include "include/reg_sizes.asm"
%include "include/memcpy.asm"
%include "include/const.inc"

%ifndef AES_XCBC_X16
%define AES_XCBC_X16 aes_xcbc_mac_128_vaes_avx512
%define SUBMIT_JOB_AES_XCBC submit_job_aes_xcbc_vaes_avx512
%define FLUSH_JOB_AES_XCBC flush_job_aes_xcbc_vaes_avx512
%define NUM_KEYS 11
%endif

extern AES_XCBC_X16

mksection .rodata
default rel

align 64
byte_len_to_mask_table:
        dw      0x0000, 0x0001, 0x0003, 0x0007,
        dw      0x000f, 0x001f, 0x003f, 0x007f,
        dw      0x00ff, 0x01ff, 0x03ff, 0x07ff,
        dw      0x0fff, 0x1fff, 0x3fff, 0x7fff,
        dw      0xffff

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

%define job_rax          rax

%if 1
; idx needs to be in rbp
%define len              r11
%define idx              rbp
%define tmp2             rbp
%define tmp              r14

%define lane             r8
%define icv              r9
%define p2               r9

%define last_len         r10

%define lane_data        r12
%define p                r13

%define unused_lanes     rbx
%define tmp3             r15
%endif

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

; transpose keys and insert into key table
%macro INSERT_KEYS 5
%define %%KP    %1 ; [in] GP reg with pointer to expanded keys
%define %%LANE  %2 ; [in] GP reg with lane number
%define %%COL   %3 ; [clobbered] GP reg
%define %%ZTMP  %4 ; [clobbered] ZMM reg
%define %%IA0   %5 ; [clobbered] GP reg

%assign ROW (16*16)

        mov             %%COL, %%LANE
        shl             %%COL, 4
        lea             %%IA0, [state + _aes_xcbc_args_key_tab]
        add             %%COL, %%IA0

        vmovdqu64       %%ZTMP, [%%KP]
        vextracti64x2   [%%COL + ROW*0], %%ZTMP, 0
        vextracti64x2   [%%COL + ROW*1], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*2], %%ZTMP, 2
        vextracti64x2   [%%COL + ROW*3], %%ZTMP, 3

        vmovdqu64       %%ZTMP, [%%KP + 64]
        vextracti64x2   [%%COL + ROW*4], %%ZTMP, 0
        vextracti64x2   [%%COL + ROW*5], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*6], %%ZTMP, 2
        vextracti64x2   [%%COL + ROW*7], %%ZTMP, 3

        mov             %%IA0, 0x3f
        kmovq           k1, %%IA0
        vmovdqu64       %%ZTMP{k1}{z}, [%%KP + 128]

        vextracti64x2   [%%COL + ROW*8], %%ZTMP, 0
        vextracti64x2   [%%COL + ROW*9], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*10], %%ZTMP, 2
%endmacro

; copy IV's and round keys into NULL lanes
%macro COPY_IV_KEYS_TO_NULL_LANES 6
%define %%IDX           %1 ; [in] GP with good lane idx (scaled x16)
%define %%NULL_MASK     %2 ; [clobbered] GP to store NULL lane mask
%define %%KEY_TAB       %3 ; [clobbered] GP to store key table pointer
%define %%XTMP1         %4 ; [clobbered] temp XMM reg
%define %%XTMP2         %5 ; [clobbered] temp XMM reg
%define %%MASK_REG      %6 ; [in] mask register

        vmovdqa64       %%XTMP1, [state + _aes_xcbc_args_ICV + %%IDX]
        lea             %%KEY_TAB, [state + _aes_xcbc_args_key_tab]
        kmovw           DWORD(%%NULL_MASK), %%MASK_REG

%assign j 0 ; outer loop to iterate through round keys
%rep NUM_KEYS
        vmovdqa64       %%XTMP2, [%%KEY_TAB + j + %%IDX]

%assign k 0 ; inner loop to iterate through lanes
%rep 16
        bt              %%NULL_MASK, k
        jnc             %%_skip_copy %+ j %+ _ %+ k

%if j == 0 ;; copy IVs for each lane just once
        vmovdqa64       [state + _aes_xcbc_args_ICV + (k*16)], %%XTMP1
%endif
        ;; copy key for each lane
        vmovdqa64       [%%KEY_TAB + j + (k*16)], %%XTMP2
%%_skip_copy %+ j %+ _ %+ k:
%assign k (k + 1)
%endrep

%assign j (j + 256)
%endrep

%endmacro

; clear final block buffers and round key's in NULL lanes
%macro CLEAR_KEYS_FINAL_BLK_IN_NULL_LANES 3
%define %%NULL_MASK     %1 ; [clobbered] GP to store NULL lane mask
%define %%YTMP          %2 ; [clobbered] temp YMM reg
%define %%MASK_REG      %3 ; [in] mask register

        vpxor           %%YTMP, %%YTMP
        kmovw           DWORD(%%NULL_MASK), %%MASK_REG
%assign k 0 ; outer loop to iterate through lanes
%rep 16
        bt              %%NULL_MASK, k
        jnc             %%_skip_clear %+ k

        ;; clear final blocks and ICV buffers
        vmovdqa         [state + _aes_xcbc_ldata + k * _XCBC_LANE_DATA_size + _xcbc_final_block], %%YTMP
        vmovdqa         [state + _aes_xcbc_args_ICV + k * 16], XWORD(%%YTMP)
%assign j 0 ; inner loop to iterate through round keys
%rep NUM_KEYS
        vmovdqa         [state + _aes_xcbc_args_key_tab + j + (k*16)], XWORD(%%YTMP)
%assign j (j + 256)

%endrep
%%_skip_clear %+ k:
%assign k (k + 1)
%endrep

%endmacro

;;; ===========================================================================
;;; AES XCBC job submit & flush
;;; ===========================================================================
;;; SUBMIT_FLUSH [in] - SUBMIT, FLUSH job selection
%macro GENERIC_SUBMIT_FLUSH_JOB_AES_XCBC_VAES_AVX512 1
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
 	mov	unused_lanes, [state + _aes_xcbc_unused_lanes]

%ifidn %%SUBMIT_FLUSH, SUBMIT
	mov	lane, unused_lanes
	and	lane, 0xF
	shr	unused_lanes, 4
	imul	lane_data, lane, _XCBC_LANE_DATA_size
	lea	lane_data, [state + _aes_xcbc_ldata + lane_data]
	mov	len, [job + _msg_len_to_hash_in_bytes]
	mov	[state + _aes_xcbc_unused_lanes], unused_lanes
        add     qword [state + _aes_xcbc_num_lanes_inuse], 1
	mov	[lane_data + _xcbc_job_in_lane], job
	mov	dword [lane_data + _xcbc_final_done], 0
	mov	tmp, [job + _k1_expanded]
        INSERT_KEYS tmp, lane, tmp2, zmm4, p
	mov	p, [job + _src]
	add	p, [job + _hash_start_src_offset_in_bytes]

	mov	last_len, len

        cmp	len, 16
	jle	%%_small_buffer

	mov	[state + _aes_xcbc_args_in + lane*8], p
	add	p, len		; set point to end of data

	and	last_len, 15	; Check lsbs of msg len
	jnz	%%_slow_copy	; if not 16B mult, do slow copy

%%_fast_copy:
	vmovdqu	xmm0, [p - 16]	; load last block M[n]
        mov     tmp, [job + _k2] ; load K2 address
        vmovdqu xmm1, [tmp]     ; load K2
        vpxor   xmm0, xmm0, xmm1      ; M[n] XOR K2
	vmovdqa	[lane_data + _xcbc_final_block], xmm0
	sub	len, 16		; take last block off length
%%_end_fast_copy:
        ;; Update lane len
        vmovdqa64 ymm0, [state + _aes_xcbc_lens]

        SHIFT_GP 1, lane, tmp, tmp3, left
        kmovq   k1, tmp

        vpbroadcastw    ymm1, WORD(len)
        vmovdqu16       ymm0{k1}, ymm1
        vmovdqa64       [state + _aes_xcbc_lens], ymm0

	vpxor	xmm1, xmm1, xmm1
	shl	lane, 4	; multiply by 16
	vmovdqa	[state + _aes_xcbc_args_ICV + lane], xmm1

        cmp     qword [state + _aes_xcbc_num_lanes_inuse], 16
	jne	%%_return_null

        ;; Find min length for lanes 0-7
        vphminposuw xmm2, xmm0

%else ;; FLUSH
        ;; Check at least one job
        cmp     qword [state + _aes_xcbc_num_lanes_inuse], 0
        je      %%_return_null

        ; find a lane with a non-null job
        xor     tmp2, tmp2
        xor     tmp, tmp
%assign i 15 ; iterate through lanes
%rep 16
        cmp     qword [state + _aes_xcbc_ldata + i * _XCBC_LANE_DATA_size + _xcbc_job_in_lane], 0
        jne     %%_skip_copy %+ i

        bts     tmp, i ;; set bit in null lane mask
        jmp     %%_end_loop %+ i
%%_skip_copy %+ i:
        mov     DWORD(tmp2), i ;; store good lane idx
%%_end_loop %+ i:
%assign i (i - 1)
%endrep
        kmovw           k6, DWORD(tmp)
        movzx           tmp3, BYTE(tmp)
        kmovw           k4, DWORD(tmp3)
        shr             tmp, 8
        kmovw           k5, DWORD(tmp)

        mov             tmp, [state + _aes_xcbc_args_in + tmp2*8]
        vpbroadcastq    zmm1, tmp
        vmovdqa64       [state + _aes_xcbc_args_in + (0*PTR_SZ)]{k4}, zmm1
        vmovdqa64       [state + _aes_xcbc_args_in + (8*PTR_SZ)]{k5}, zmm1

        ;; - set len to UINT16_MAX
        mov             WORD(tmp), 0xffff
        vpbroadcastw    ymm3, WORD(tmp)
        vmovdqa64       ymm0, [state + _aes_xcbc_lens]
        vmovdqu16       ymm0{k6}, ymm3
        vmovdqa64       [state + _aes_xcbc_lens], ymm0

        ;; scale up good lane idx before copying IV and keys
        shl             tmp2, 4

        ;; - copy IV and round keys to null lanes
        COPY_IV_KEYS_TO_NULL_LANES tmp2, tmp, tmp3, xmm4, xmm5, k6

        ;; Find min length for lanes 0-7
        vphminposuw xmm2, xmm0

%endif ; SUBMIT_FLUSH

%%_start_loop:

        ; Find min length for lanes 8-15
        vpextrw         DWORD(len2), xmm2, 0   ; min value
        vpextrw         DWORD(idx), xmm2, 1   ; min index
        vextracti128    xmm1, ymm0, 1
        vphminposuw     xmm2, xmm1
        vpextrw         DWORD(tmp3), xmm2, 0       ; min value
        cmp             DWORD(len2), DWORD(tmp3)
        jle             %%_use_min
        vpextrw         DWORD(idx), xmm2, 1   ; min index
        add             DWORD(idx), 8               ; but index +8
        mov             len2, tmp3                    ; min len
%%_use_min:
        cmp             len2, 0
        je              %%_len_is_0

        vpbroadcastw    ymm3, WORD(len2)
        vpsubw          ymm0, ymm0, ymm3
        vmovdqa         [state + _aes_xcbc_lens], ymm0

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	AES_XCBC_X16
	; state and idx are intact

%%_len_is_0:
	; process completed job "idx"
	imul	lane_data, idx, _XCBC_LANE_DATA_size
	lea	lane_data, [state + _aes_xcbc_ldata + lane_data]
	cmp	dword [lane_data + _xcbc_final_done], 0
	jne	%%_end_loop

	mov	dword [lane_data + _xcbc_final_done], 1

        ;; Update lane len
        vmovdqa64 ymm0, [state + _aes_xcbc_lens]

        SHIFT_GP 1, idx, tmp, tmp3, left
        kmovq   k1, tmp

        mov             tmp3, 16
        vpbroadcastw    ymm1, WORD(tmp3)
        vmovdqu16       ymm0{k1}, ymm1

%ifidn %%SUBMIT_FLUSH, FLUSH
        ;; reset null lane lens to UINT16_MAX on flush
        mov             WORD(tmp3), 0xffff
        vpbroadcastw    ymm3, WORD(tmp3)
        vmovdqu16       ymm0{k6}, ymm3
%endif

        vmovdqa64       [state + _aes_xcbc_lens], ymm0

        ;; Find min length for lanes 0-7
        vphminposuw xmm2, xmm0

        lea	tmp, [lane_data + _xcbc_final_block]

%ifidn %%SUBMIT_FLUSH, FLUSH
        ;; update input pointers for idx (processed) lane
        ;; and null lanes to point to idx lane final block
        vpbroadcastq    zmm1, tmp
        korw            k4, k6, k1 ;; create mask with all lanes to be updated (k4)
        kshiftrw        k5, k4, 8  ;; lanes 8-15 mask in k5
        vmovdqa64       [state + _aes_xcbc_args_in + (0*PTR_SZ)]{k4}, zmm1
        vmovdqa64       [state + _aes_xcbc_args_in + (8*PTR_SZ)]{k5}, zmm1
%else
        ;; only update processed lane input pointer on submit
        mov	[state + _aes_xcbc_args_in + 8*idx], tmp
%endif

	jmp	%%_start_loop

%%_end_loop:
	; process completed job "idx"
	mov	job_rax, [lane_data + _xcbc_job_in_lane]
	mov	icv, [job_rax + _auth_tag_output]
	mov	unused_lanes, [state + _aes_xcbc_unused_lanes]
	mov	qword [lane_data + _xcbc_job_in_lane], 0
	or	dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
	shl	unused_lanes, 4
	or	unused_lanes, idx
	shl	idx, 4 ; multiply by 16
	mov	[state + _aes_xcbc_unused_lanes], unused_lanes
        sub     qword [state + _aes_xcbc_num_lanes_inuse], 1

	; copy 12 bytes
	vmovdqa	xmm0, [state + _aes_xcbc_args_ICV + idx]
        mov     tmp, 0xfff
        kmovw   k1, DWORD(tmp)
        vmovdqu8 [icv]{k1}, xmm0

%ifdef SAFE_DATA
        vpxor   ymm0, ymm0
%ifidn %%SUBMIT_FLUSH, SUBMIT
        ;; Clear final block (32 bytes)
        vmovdqa [lane_data + _xcbc_final_block], ymm0

        ;; Clear expanded keys
%assign round 0
%rep NUM_KEYS
        vmovdqa [state + _aes_xcbc_args_key_tab + round * (16*16) + idx], xmm0
%assign round (round + 1)
%endrep

%else ;; FLUSH
        ;; Clear keys and final blocks of returned job and "NULL lanes"
        shr     idx, 4 ;; divide by 16 to restore lane idx
        xor     DWORD(tmp), DWORD(tmp)
        bts     DWORD(tmp), DWORD(idx)
        kmovw   k1, DWORD(tmp)
        korw    k6, k1, k6
        ;; k6 contains the mask of the jobs
        CLEAR_KEYS_FINAL_BLK_IN_NULL_LANES tmp, ymm0, k6
%endif
%else
        vzeroupper
%endif

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

%ifidn %%SUBMIT_FLUSH, SUBMIT
%%_small_buffer:
	; For buffers <= 16 Bytes
	; The input data is set to final block
	lea	tmp, [lane_data + _xcbc_final_block] ; final block
	mov	[state + _aes_xcbc_args_in + lane*8], tmp
	add	p, len		; set point to end of data
	cmp	len, 16
	je	%%_fast_copy

%%_slow_copy:
	and	len, ~15	; take final block off len
	sub	p, last_len	; adjust data pointer
	lea	p2, [lane_data + _xcbc_final_block + 16] ; upper part of final
	sub	p2, last_len	; adjust data pointer backwards

        lea     tmp, [rel byte_len_to_mask_table]
        kmovw   k1, word [tmp + last_len*2]

        vmovdqu8 xmm0{k1}, [p]
        vmovdqu8 [p2]{k1}, xmm0

        mov     tmp, 0x80
        vmovq   xmm0, tmp
	vmovdqu	[lane_data + _xcbc_final_block + 16], xmm0 ; add padding
	vmovdqu	xmm0, [p2]	; load final block to process
	mov	tmp, [job + _k3] ; load K3 address
	vmovdqu	xmm1, [tmp]	; load K3
	vpxor	xmm0, xmm0, xmm1	; M[n] XOR K3
	vmovdqu	[lane_data + _xcbc_final_block], xmm0	; write final block
	jmp	%%_end_fast_copy

%endif ; SUBMIT

%%_return_null:
	xor	job_rax, job_rax
	jmp	%%_return

%endmacro

align 64
; IMB_JOB * submit_job_aes_xcbc_vaes_avx512(MB_MGR_XCBC_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_AES_XCBC,function,internal)
SUBMIT_JOB_AES_XCBC:
        endbranch64
        GENERIC_SUBMIT_FLUSH_JOB_AES_XCBC_VAES_AVX512 SUBMIT

; IMB_JOB * flush_job_aes_xcbc_vaes_avx512(MB_MGR_XCBC_OOO *state)
; arg 1 : state
MKGLOBAL(FLUSH_JOB_AES_XCBC,function,internal)
FLUSH_JOB_AES_XCBC:
        endbranch64
        GENERIC_SUBMIT_FLUSH_JOB_AES_XCBC_VAES_AVX512 FLUSH

mksection stack-noexec

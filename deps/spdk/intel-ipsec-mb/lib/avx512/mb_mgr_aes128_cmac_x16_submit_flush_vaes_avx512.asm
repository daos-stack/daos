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
%include "include/const.inc"

%ifndef AES_CBC_MAC
%define AES_CBC_MAC aes128_cbc_mac_vaes_avx512
%define SUBMIT_JOB_AES_CMAC_AUTH submit_job_aes128_cmac_auth_vaes_avx512
%define FLUSH_JOB_AES_CMAC_AUTH flush_job_aes128_cmac_auth_vaes_avx512
%define NUM_KEYS 11
%endif

extern AES_CBC_MAC

mksection .rodata
default rel

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

%define tmp5             r9
%define m_last           r10
%define n                r11

%define unused_lanes     rbx
%define r                rbx

%define tmp3             r12
%define tmp4             r13
%define tmp2             r14

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
        lea             %%IA0, [state + _aes_cmac_args_key_tab]
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

%if NUM_KEYS == 11
        mov             %%IA0, 0x3f
        kmovq           k1, %%IA0
        vmovdqu64       %%ZTMP{k1}{z}, [%%KP + 128]

        vextracti64x2   [%%COL + ROW*8], %%ZTMP, 0
        vextracti64x2   [%%COL + ROW*9], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*10], %%ZTMP, 2

%else ;; assume 15 keys for CMAC 256
        vmovdqu64       %%ZTMP, [%%KP + 128]
        vextracti64x2   [%%COL + ROW*8], %%ZTMP, 0
        vextracti64x2   [%%COL + ROW*9], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*10], %%ZTMP, 2
        vextracti64x2   [%%COL + ROW*11], %%ZTMP, 3

        mov             %%IA0, 0x3f
        kmovq           k1, %%IA0
        vmovdqu64       %%ZTMP{k1}{z}, [%%KP + 192]

        vextracti64x2   [%%COL + ROW*12], %%ZTMP, 0
        vextracti64x2   [%%COL + ROW*13], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*14], %%ZTMP, 2
%endif
%endmacro

; copy IV's and round keys into NULL lanes
%macro COPY_IV_KEYS_TO_NULL_LANES 6
%define %%IDX           %1 ; [in] GP with good lane idx (scaled x16)
%define %%NULL_MASK     %2 ; [clobbered] GP to store NULL lane mask
%define %%KEY_TAB       %3 ; [clobbered] GP to store key table pointer
%define %%XTMP1         %4 ; [clobbered] temp XMM reg
%define %%XTMP2         %5 ; [clobbered] temp XMM reg
%define %%MASK_REG      %6 ; [in] mask register

        vmovdqa64       %%XTMP1, [state + _aes_cmac_args_IV + %%IDX]
        lea             %%KEY_TAB, [state + _aes_cmac_args_key_tab]
        kmovw           DWORD(%%NULL_MASK), %%MASK_REG

%assign j 0 ; outer loop to iterate through round keys
%rep 15
        vmovdqa64       %%XTMP2, [%%KEY_TAB + j + %%IDX]

%assign k 0 ; inner loop to iterate through lanes
%rep 16
        bt              %%NULL_MASK, k
        jnc             %%_skip_copy %+ j %+ _ %+ k

%if j == 0 ;; copy IVs for each lane just once
        vmovdqa64       [state + _aes_cmac_args_IV + (k*16)], %%XTMP1
%endif
        ;; copy key for each lane
        vmovdqa64       [%%KEY_TAB + j + (k*16)], %%XTMP2
%%_skip_copy %+ j %+ _ %+ k:
%assign k (k + 1)
%endrep

%assign j (j + 256)
%endrep

%endmacro

; clear IVs, scratch buffers and round key's in NULL lanes
%macro CLEAR_IV_KEYS_SCRATCH_IN_NULL_LANES 3
%define %%NULL_MASK     %1 ; [clobbered] GP to store NULL lane mask
%define %%XTMP          %2 ; [clobbered] temp XMM reg
%define %%MASK_REG      %3 ; [in] mask register

        vpxorq          %%XTMP, %%XTMP
        kmovw           DWORD(%%NULL_MASK), %%MASK_REG
%assign k 0 ; outer loop to iterate through lanes
%rep 16
        bt              %%NULL_MASK, k
        jnc             %%_skip_clear %+ k

        ;; clean lane scratch and IV buffers
        vmovdqa64       [state + _aes_cmac_scratch + (k*16)], %%XTMP
        vmovdqa64       [state + _aes_cmac_args_IV + (k*16)], %%XTMP

%assign j 0 ; inner loop to iterate through round keys
%rep NUM_KEYS
        vmovdqa64       [state + _aes_cmac_args_key_tab + j + (k*16)], %%XTMP
%assign j (j + 256)

%endrep
%%_skip_clear %+ k:
%assign k (k + 1)
%endrep

%endmacro

;;; ===========================================================================
;;; AES CMAC job submit & flush
;;; ===========================================================================
;;; SUBMIT_FLUSH [in] - SUBMIT, FLUSH job selection
%macro GENERIC_SUBMIT_FLUSH_JOB_AES_CMAC_VAES_AVX512 1
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
        add     qword [state + _aes_cmac_num_lanes_inuse], 1

        ;; Copy job info into lane
 	mov	[state + _aes_cmac_job_in_lane + lane*8], job

        mov     tmp, lane
        shl     tmp, 4  ; lane*16

        ;; Zero IV to store digest
        vpxor   xmm0, xmm0
        vmovdqa [state + _aes_cmac_args_IV + tmp], xmm0

        lea     m_last, [state + _aes_cmac_scratch + tmp]

        ;; Insert expanded keys
        mov     tmp, [job + _key_expanded]
        INSERT_KEYS tmp, lane, tmp2, zmm4, tmp3

        ;; Calculate len
        ;; Convert bits to bytes (message length in bits for CMAC)
        mov     len, [job + _msg_len_to_hash_in_bits]
        mov     rbits, len
        add     len, 7      ; inc len if there are remainder bits
        shr     len, 3
        and     rbits, 7

        ;; Check number of blocks and for partial block
        mov     r, len  ; set remainder
        and     r, 0xf

        lea     n, [len + 0xf] ; set num blocks
        shr     n, 4

        jz      %%_lt_one_block ; check one or more blocks?

        ;; One or more blocks, potentially partial
        mov     word [state + _aes_cmac_init_done + lane*2], 0

        mov     tmp2, [job + _src]
        add     tmp2, [job + _hash_start_src_offset_in_bytes]
        mov     [state + _aes_cmac_args_in + lane*8], tmp2

        ;; len = (n-1)*16
        lea     tmp2, [n - 1]
        shl     tmp2, 4

        ;; Update lane len
        vmovdqa64 ymm0, [state + _aes_cmac_lens]
%ifndef LINUX
        mov     tmp3, rcx       ; save rcx
%endif
        mov     rcx, lane
        mov     tmp, 1
        shl     tmp, cl
%ifndef LINUX
        mov     rcx, tmp3       ; restore rcx
%endif
        kmovq   k1, tmp

        vpbroadcastw    ymm1, WORD(tmp2)
        vmovdqu16       ymm0{k1}, ymm1
        vmovdqa64       [state + _aes_cmac_lens], ymm0

        ;; check remainder bits
        or      rbits, rbits
        jnz     %%_not_complete_block_3gpp

        ;; check if complete block
        or      r, r
        jz      %%_complete_block

%%_not_complete_block:
        ;; M_last = padding(M_n) XOR K2

        mov     tmp, [job + _src]
        add     tmp, [job + _hash_start_src_offset_in_bytes]
        lea     tmp3, [n - 1]
        shl     tmp3, 4
        add     tmp, tmp3
        vmovdqu xmm1, [tmp] ;; load last block

        ;; get mask for padding
%ifndef LINUX
        mov     tmp3, rcx       ; save rcx
%endif
        mov     rcx, r
        mov     tmp, 0xffff
        shl     tmp, cl
%ifndef LINUX
        mov     rcx, tmp3       ; restore rcx
%endif
        kmovq   k1, tmp

        lea     tmp, [rel padding_0x80_tab16 + 16]
        sub     tmp, r
        vmovdqu8 xmm1{k1}, [tmp] ;; merge last block and padding

        ;; src + n + r
        mov     tmp3, [job + _skey2]
        vmovdqu xmm2, [tmp3]
        vpxor   xmm2, xmm1
        vmovdqa [m_last], xmm2

%%_step_5:
        ;; Find min length for lanes 0-7
        vphminposuw     xmm2, xmm0

        cmp     qword [state + _aes_cmac_num_lanes_inuse], 16
        jne     %%_return_null
%else ; end SUBMIT

        ;; Check at least one job
        cmp     qword [state + _aes_cmac_num_lanes_inuse], 0
        je      %%_return_null

        ; find a lane with a non-null job
        vpxord          zmm0, zmm0, zmm0
        vmovdqu64       zmm1, [state + _aes_cmac_job_in_lane + (0*PTR_SZ)]
        vmovdqu64       zmm2, [state + _aes_cmac_job_in_lane + (8*PTR_SZ)]
        vpcmpq          k1, zmm1, zmm0, 4 ; NEQ
        vpcmpq          k2, zmm2, zmm0, 4 ; NEQ
        kmovw           DWORD(tmp), k1
        kmovw           DWORD(tmp4), k2
        mov             DWORD(tmp2), DWORD(tmp4)
        shl             DWORD(tmp2), 8
        or              DWORD(tmp2), DWORD(tmp) ; mask of non-null jobs in tmp2
        not             BYTE(tmp)
        kmovw           k4, DWORD(tmp)
        not             BYTE(tmp4)
        kmovw           k5, DWORD(tmp4)
        mov             DWORD(tmp), DWORD(tmp2)
        not             WORD(tmp)
        kmovw           k6, DWORD(tmp)         ; mask of NULL jobs in k4, k5 and k6
        mov             DWORD(tmp), DWORD(tmp2)
        xor             tmp2, tmp2
        bsf             WORD(tmp2), WORD(tmp)   ; index of the 1st set bit in tmp2

        ;; copy good lane data into NULL lanes
        mov             tmp, [state + _aes_cmac_args_in + tmp2*8]
        vpbroadcastq    zmm1, tmp
        vmovdqa64       [state + _aes_cmac_args_in + (0*PTR_SZ)]{k4}, zmm1
        vmovdqa64       [state + _aes_cmac_args_in + (8*PTR_SZ)]{k5}, zmm1

        ;; - set len to UINT16_MAX
        mov             WORD(tmp), 0xffff
        vpbroadcastw    ymm3, WORD(tmp)
        vmovdqa64       ymm0, [state + _aes_cmac_lens]
        vmovdqu16       ymm0{k6}, ymm3
        vmovdqa64       [state + _aes_cmac_lens], ymm0

        ;; scale up good lane idx before copying IV and keys
        shl             tmp2, 4

        ;; - copy IV and round keys to null lanes
        COPY_IV_KEYS_TO_NULL_LANES tmp2, tmp4, tmp3, xmm4, xmm5, k6

        ;; Find min length for lanes 0-7
        vphminposuw xmm2, xmm0

%endif ; end FLUSH

%%_cmac_round:
        ; Find min length for lanes 8-15
        vpextrw         DWORD(len2), xmm2, 0   ; min value
        vpextrw         DWORD(idx), xmm2, 1   ; min index
        vextracti128    xmm1, ymm0, 1
        vphminposuw     xmm2, xmm1
        vpextrw         DWORD(tmp4), xmm2, 0       ; min value
        cmp             DWORD(len2), DWORD(tmp4)
        jle             %%_use_min
        vpextrw         DWORD(idx), xmm2, 1   ; min index
        add             DWORD(idx), 8               ; but index +8
        mov             len2, tmp4                    ; min len
%%_use_min:
        cmp             len2, 0
        je              %%_len_is_0

        vpbroadcastw    ymm3, WORD(len2)
        vpsubw          ymm0, ymm0, ymm3
        vmovdqa         [state + _aes_cmac_lens], ymm0

        ; "state" and "args" are the same address, arg1
        ; len2 is arg2
        call    AES_CBC_MAC
        ; state and idx are intact

        vmovdqa ymm0, [state + _aes_cmac_lens]  ; preload lens
%%_len_is_0:
        ; Check if job complete
        test    word [state + _aes_cmac_init_done + idx*2], 0xffff
        jnz     %%_copy_complete_digest

        ; Finish step 6
        mov     word [state + _aes_cmac_init_done + idx*2], 1

        ; Set len to 16
        mov             tmp3, 16
%ifndef LINUX
        mov             tmp2, rcx       ; save rcx
%endif
        mov             rcx, idx
        mov             DWORD(tmp4), 1
        shl             DWORD(tmp4), cl
%ifndef LINUX
        mov             rcx, tmp2       ; restore rcx
%endif
        kmovq           k1, tmp4

        vpbroadcastw    ymm1, WORD(tmp3)
        vmovdqu16       ymm0{k1}, ymm1

%ifidn %%SUBMIT_FLUSH, FLUSH
        ;; reset null lane lens to UINT16_MAX on flush
        mov             WORD(tmp3), 0xffff
        vpbroadcastw    ymm3, WORD(tmp3)
        vmovdqu16       ymm0{k6}, ymm3
%endif
        vmovdqa64       [state + _aes_cmac_lens], ymm0

        vphminposuw xmm2, xmm0 ; find min length for lanes 0-7

        mov     tmp3, idx
        shl     tmp3, 4  ; idx*16
        lea     m_last, [state + _aes_cmac_scratch + tmp3]

%ifidn %%SUBMIT_FLUSH, FLUSH
        ;; update input pointers for idx (processed) lane
        ;; and null lanes to point to idx lane final block
        vpbroadcastq    zmm1, m_last
        korw            k4, k6, k1 ;; create mask with all lanes to be updated (k4)
        kshiftrw        k5, k4, 8  ;; lanes 8-15 mask in k5
        vmovdqa64       [state + _aes_cmac_args_in + (0*PTR_SZ)]{k4}, zmm1
        vmovdqa64       [state + _aes_cmac_args_in + (8*PTR_SZ)]{k5}, zmm1
%else
        ;; only update processed lane input pointer on submit
        mov     [state + _aes_cmac_args_in + idx*8], m_last
%endif

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
        vmovdqa xmm0, [tmp3]
        vmovdqu [tmp2], xmm0
        jmp     %%_update_lanes

%%_ne_16_copy:
        vmovdqa xmm0, [tmp3]

        ;; get mask for padding
%ifndef LINUX
        mov     tmp3, rcx       ; save rcx
%endif
        mov     rcx, tmp4
        mov     DWORD(tmp5), 0xffff
        shl     DWORD(tmp5), cl
        not     DWORD(tmp5)
%ifndef LINUX
        mov     rcx, tmp3       ; restore rcx
%endif
        kmovq   k1, tmp5

        vmovdqu8 [tmp2]{k1}, xmm0

%%_update_lanes:
        ; Update unused lanes
        mov	unused_lanes, [state + _aes_cmac_unused_lanes]
        shl	unused_lanes, 4
 	or	unused_lanes, idx
 	mov	[state + _aes_cmac_unused_lanes], unused_lanes
        sub     qword [state + _aes_cmac_num_lanes_inuse], 1

        ; Set return job
        mov	job_rax, [state + _aes_cmac_job_in_lane + idx*8]

 	mov	qword [state + _aes_cmac_job_in_lane + idx*8], 0
 	or	dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH

%ifdef SAFE_DATA
        vpxor   xmm0, xmm0
%ifidn %%SUBMIT_FLUSH, SUBMIT
        ;; Clear IV and scratch memory of returned job
        shl     idx, 4
        vmovdqa [state + _aes_cmac_scratch + idx], xmm0
        vmovdqa [state + _aes_cmac_args_IV + idx], xmm0

        ;; Clear expanded keys
%assign round 0
%rep NUM_KEYS
        vmovdqa [state + _aes_cmac_args_key_tab + round * (16*16) + idx], xmm0
%assign round (round + 1)
%endrep

%else
        ;; Clear keys and scratch memory of returned job and "NULL lanes"
        xor     DWORD(tmp2), DWORD(tmp2)
        bts     DWORD(tmp2), DWORD(idx)
        kmovw   k1, DWORD(tmp2)
        korw    k6, k1, k6

        ;; Clear IVs, keys and scratch buffers of returned job and "NULL lanes"
        ;; (k6 contains the mask of the jobs)
        CLEAR_IV_KEYS_SCRATCH_IN_NULL_LANES tmp4, xmm0, k6
%endif ;; SUBMIT

%else
        vzeroupper
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
        vmovdqu xmm4, [tmp3]
        vmovdqu xmm5, [tmp2]
        vpxor   xmm4, xmm5
        vmovdqa [m_last], xmm4

        jmp     %%_step_5

%%_lt_one_block:
        ;; Single partial block
        mov     word [state + _aes_cmac_init_done + lane*2], 1
        mov     [state + _aes_cmac_args_in + lane*8], m_last

        ;; Set len to 16
        vmovdqa64       ymm0, [state + _aes_cmac_lens]
        mov             tmp2, 16
%ifndef LINUX
        mov             tmp3, rcx       ; save rcx
%endif
        mov             rcx, lane
        mov             tmp, 1
        shl             tmp, cl
%ifndef LINUX
        mov             rcx, tmp3       ; restore rcx
%endif
        kmovq           k1, tmp

        vpbroadcastw    ymm1, WORD(tmp2)
        vmovdqu16       ymm0{k1}, ymm1
        vmovdqa64       [state + _aes_cmac_lens], ymm0

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

        ;; load remainder bytes from last block
%ifndef LINUX
        mov     tmp3, rcx       ; save rcx
%endif
        mov     rcx, r
        mov     DWORD(tmp5), 0xffff
        shl     DWORD(tmp5), cl
        not     tmp5
%ifndef LINUX
        mov     rcx, tmp3       ; restore rcx
%endif
        kmovq   k1, tmp5
        vmovdqu8 xmm4{k1}{z}, [tmp]

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
        vmovq   xmm2, tmp3
        XVPSLLB xmm2, r, xmm1, tmp2

        ;; pad final byte
        vpandn  xmm2, xmm4
%ifndef LINUX
	mov	rcx, tmp
%endif
        ;; set OR mask to pad final bit
        mov     tmp2, tmp3
        shr     tmp2, 1
        xor     tmp2, tmp3 ; XOR to get OR mask
        vmovq   xmm3, tmp2
        ;; xmm1 contains shift table from previous shift
        vpshufb xmm3, xmm1

        ;; load skey2 address
        mov     tmp3, [job + _skey2]
        vmovdqu xmm1, [tmp3]

        ;; set final padding bit
        vpor    xmm2, xmm3

        ;; XOR last partial block with skey2
        ;; update mlast
        vpxor   xmm2, xmm1
        vmovdqa [m_last], xmm2

        jmp     %%_step_5

%%_load_full_block_3gpp:
        vmovdqu xmm4, [tmp]
        mov     r, 0xf
        jmp     %%_update_mlast_3gpp
%endif
%endmacro

align 64
; IMB_JOB * submit_job_aes_cmac_auth_vaes_avx512(MB_MGR_CMAC_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_AES_CMAC_AUTH,function,internal)
SUBMIT_JOB_AES_CMAC_AUTH:
        endbranch64
        GENERIC_SUBMIT_FLUSH_JOB_AES_CMAC_VAES_AVX512 SUBMIT

; IMB_JOB * flush_job_aes_cmac_auth_vaes_avx512(MB_MGR_CMAC_OOO *state)
; arg 1 : state
MKGLOBAL(FLUSH_JOB_AES_CMAC_AUTH,function,internal)
FLUSH_JOB_AES_CMAC_AUTH:
        endbranch64
        GENERIC_SUBMIT_FLUSH_JOB_AES_CMAC_VAES_AVX512 FLUSH

mksection stack-noexec

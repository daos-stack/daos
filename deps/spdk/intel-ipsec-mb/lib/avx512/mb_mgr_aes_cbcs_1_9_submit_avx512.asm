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

%define CBCS
%include "avx512/mb_mgr_aes_submit_avx512.asm"
%include "include/cet.inc"

%define AES_CBCS_ENC_X16 aes_cbcs_1_9_enc_128_vaes_avx512
%define NUM_KEYS 11
%define SUBMIT_JOB_AES_CBCS_ENC submit_job_aes128_cbcs_1_9_enc_vaes_avx512

; void AES_CBCS_ENC_X16(AES_ARGS_x16 *args, UINT64 len_in_bytes);
extern AES_CBCS_ENC_X16

; JOB* SUBMIT_JOB_AES_ENC(MB_MGR_AES_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_AES_CBCS_ENC,function,internal)
SUBMIT_JOB_AES_CBCS_ENC:
        endbranch64
        mov     rax, rsp
        sub     rsp, STACK_size
        and     rsp, -16

        mov     [rsp + _gpr_save + 8*0], rbx
        mov     [rsp + _gpr_save + 8*1], rbp
        mov     [rsp + _gpr_save + 8*2], r12
        mov     [rsp + _gpr_save + 8*3], r13
        mov     [rsp + _gpr_save + 8*4], r14
        mov     [rsp + _gpr_save + 8*5], r15
%ifndef LINUX
        mov     [rsp + _gpr_save + 8*6], rsi
        mov     [rsp + _gpr_save + 8*7], rdi
%endif
        mov     [rsp + _rsp_save], rax  ; original SP

        mov     unused_lanes, [state + _aes_unused_lanes]
        mov     lane, unused_lanes
        and     lane, 0xF
        shr     unused_lanes, 4
        mov     len, [job + _msg_len_to_cipher_in_bytes]
        and     len, -16                ; Length needs to be multiple of block size
        mov     iv, [job + _iv]
        mov     [state + _aes_unused_lanes], unused_lanes
        add     qword [state + _aes_lanes_in_use], 1

        mov     [state + _aes_job_in_lane + lane*8], job

        mov     tmp, 0xfff
        kmovq   k3, tmp

        ;; Update lane len
        vpbroadcastq    zmm5, len
        lea             tmp, [rel index_to_lane16_mask]
        mov             WORD(tmp), [tmp + lane*2]
        kmovb           k1, DWORD(tmp)
        shr             tmp, 8
        kmovb           k2, DWORD(tmp)

        vmovdqa64 zmm6, [state + _aes_lens_64]
        vmovdqa64 zmm7, [state + _aes_lens_64 + 64]

        vmovdqa64 zmm6{k1}, zmm5
        vmovdqa64 zmm7{k2}, zmm5

        vmovdqa64 [state + _aes_lens_64]{k3}, zmm6
        vmovdqa64 [state + _aes_lens_64 + 64]{k3}, zmm7

        ;; Update input pointer
        mov     tmp, [job + _src]
        add     tmp, [job + _cipher_start_src_offset_in_bytes]
        vmovdqu xmm1, [iv]
        mov     [state + _aes_args_in + lane*8], tmp

        ;; Insert expanded keys
        mov     tmp, [job + _enc_keys]
        INSERT_KEYS tmp, lane, NUM_KEYS, tmp2, zmm4, tmp3

        ;; Update output pointer
        mov     tmp, [job + _dst]
        mov     [state + _aes_args_out + lane*8], tmp
        shl     lane, 4 ; multiply by 16
        vmovdqa [state + _aes_args_IV + lane], xmm1

        cmp     qword [state + _aes_lanes_in_use], 12
        jne     return_null

	; Find min length
        vpsllq          zmm8, zmm6, 4
        vpsllq          zmm9, zmm7, 4
        vporq           zmm8, zmm8, [rel index_to_lane16]
        vporq           zmm9, zmm9, [rel index_to_lane16 + 64]
        vpminuq         zmm8, zmm8, zmm9
        vextracti64x4   ymm9, zmm8, 1
        vpminuq         ymm8, ymm9, ymm8
        vextracti32x4   xmm9, ymm8, 1
        vpminuq         xmm8, xmm9, xmm8
        vpsrldq         xmm9, xmm8, 8
        vpminuq         xmm8, xmm9, xmm8
        vmovq           len2, xmm8
        mov             idx, len2
        and             idx, 0xf
        shr             len2, 4

        or	len2, len2
	jz	len_is_0

        vpbroadcastq    zmm5, len2
        vpsubq          zmm6, zmm6, zmm5
        vpsubq          zmm7, zmm7, zmm5
        vmovdqa64       [state + _aes_lens_64]{k3}, zmm6
        vmovdqa64       [state + _aes_lens_64 + 64]{k3}, zmm7

        ; "state" and "args" are the same address, arg1
        ; len is arg2
        call    AES_CBCS_ENC_X16
        ; state and idx are intact

len_is_0:
        ; process completed job "idx"
        mov     job_rax, [state + _aes_job_in_lane + idx*8]

        mov     unused_lanes, [state + _aes_unused_lanes]
        mov     qword [state + _aes_job_in_lane + idx*8], 0
        or      dword [job_rax + _status], IMB_STATUS_COMPLETED_CIPHER
        shl     unused_lanes, 4
        or      unused_lanes, idx

        mov     [state + _aes_unused_lanes], unused_lanes
        sub     qword [state + _aes_lanes_in_use], 1

        ;; store last cipher block as next_iv
        shl	idx, 3 ; multiply by 8
        mov     tmp2, [job_rax + _cbcs_next_iv]
        vmovdqa xmm0, [state + _aes_args_IV + idx*2]
        vmovdqu [tmp2], xmm0

%ifdef SAFE_DATA
        ;; Clear IV
        vpxorq  xmm0, xmm0
        shl     idx, 1 ; multiply by 2
        vmovdqa [state + _aes_args_IV + idx], xmm0

        ;; Clear expanded keys
%assign round 0
%rep NUM_KEYS
        vmovdqa [state + _aesarg_key_tab + round * (16*16) + idx], xmm0
%assign round (round + 1)
%endrep

%endif

return:

        mov     rbx, [rsp + _gpr_save + 8*0]
        mov     rbp, [rsp + _gpr_save + 8*1]
        mov     r12, [rsp + _gpr_save + 8*2]
        mov     r13, [rsp + _gpr_save + 8*3]
        mov     r14, [rsp + _gpr_save + 8*4]
        mov     r15, [rsp + _gpr_save + 8*5]
%ifndef LINUX
        mov     rsi, [rsp + _gpr_save + 8*6]
        mov     rdi, [rsp + _gpr_save + 8*7]
%endif
        mov     rsp, [rsp + _rsp_save]  ; original SP

        ret

return_null:
        xor     job_rax, job_rax
        jmp     return

mksection .rodata
default rel

align 64
index_to_lane16:
        dq      0x0000000000000000, 0x0000000000000001
        dq      0x0000000000000002, 0x0000000000000003
        dq      0x0000000000000004, 0x0000000000000005
        dq      0x0000000000000006, 0x0000000000000007
        dq      0x0000000000000008, 0x0000000000000009
        dq      0x000000000000000a, 0x000000000000000b
        dq      0x000000000000000c, 0x000000000000000d
        dq      0x000000000000000e, 0x000000000000000f

align 16
index_to_lane16_mask:
        dw      0x0001, 0x0002, 0x0004, 0x0008,
        dw      0x0010, 0x0020, 0x0040, 0x0080,
        dw      0x0100, 0x0200, 0x0400, 0x0800,
        dw      0x1000, 0x2000, 0x4000, 0x8000,

mksection stack-noexec

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
%include "avx512/mb_mgr_aes_flush_avx512.asm"
%include "include/cet.inc"
%define AES_CBCS_ENC_X16 aes_cbcs_1_9_enc_128_vaes_avx512
%define FLUSH_JOB_AES_CBCS_ENC flush_job_aes128_cbcs_1_9_enc_vaes_avx512

; void AES_CBCS_ENC_X16(AES_ARGS *args, UINT64 len_in_bytes);
extern AES_CBCS_ENC_X16

; JOB* FLUSH_JOB_AES_CBCS_ENC(MB_MGR_AES_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(FLUSH_JOB_AES_CBCS_ENC,function,internal)
FLUSH_JOB_AES_CBCS_ENC:
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

        ; check for empty
        cmp     qword [state + _aes_lanes_in_use], 0
        je      return_null

        ; find a lane with a non-null job
        vpxord          zmm0, zmm0, zmm0
        vmovdqu64       zmm1, [state + _aes_job_in_lane + (0*PTR_SZ)]
        vmovdqu64       zmm2, [state + _aes_job_in_lane + (8*PTR_SZ)]
        vpcmpq          k1, zmm1, zmm0, 4 ; NEQ
        vpcmpq          k2, zmm2, zmm0, 4 ; NEQ
        kmovw           DWORD(tmp), k1
        kmovw           DWORD(tmp1), k2
        mov             DWORD(tmp2), DWORD(tmp1)
        shl             DWORD(tmp2), 8
        or              DWORD(tmp2), DWORD(tmp) ; mask of non-null jobs in tmp2
        not             BYTE(tmp)
        kmovw           k4, DWORD(tmp)
        not             BYTE(tmp1)
        kmovw           k5, DWORD(tmp1)
        mov             DWORD(tmp), DWORD(tmp2)
        not             WORD(tmp)
        kmovw           k6, DWORD(tmp)         ; mask of NULL jobs in k4, k5 and k6
        mov             DWORD(tmp), DWORD(tmp2)
        xor             tmp2, tmp2
        bsf             WORD(tmp2), WORD(tmp)   ; index of the 1st set bit in tmp2

        ;; copy good lane data into NULL lanes
        mov             tmp, [state + _aes_args_in + tmp2*8]
        vpbroadcastq    zmm1, tmp
        vmovdqa64       [state + _aes_args_in + (0*PTR_SZ)]{k4}, zmm1
        vmovdqa64       [state + _aes_args_in + (8*PTR_SZ)]{k5}, zmm1
        ;; - out pointer
        mov             tmp, [state + _aes_args_out + tmp2*8]
        vpbroadcastq    zmm1, tmp
        vmovdqa64       [state + _aes_args_out + (0*PTR_SZ)]{k4}, zmm1
        vmovdqa64       [state + _aes_args_out + (8*PTR_SZ)]{k5}, zmm1

        mov             tmp, 0xfff
        kmovq           k3, tmp

        ;; - set len to MAX
        vmovdqa64 zmm6, [state + _aes_lens_64]
        vmovdqa64 zmm7, [state + _aes_lens_64 + 64]

        mov             tmp, 0xffffffffffffffff
        vpbroadcastq    zmm1, tmp

        vmovdqa64 zmm6{k4}, zmm1
        vmovdqa64 zmm7{k5}, zmm1

        vmovdqa64 [state + _aes_lens_64]{k3}, zmm6
        vmovdqa64 [state + _aes_lens_64 + 64]{k3}, zmm7

        ;; scale up good lane idx before copying IV and keys
        shl             tmp2, 4

        ;; - copy IV and round keys to null lanes
        COPY_IV_KEYS_TO_NULL_LANES tmp2, tmp1, tmp3, xmm4, xmm5, k6

        ;; Update lens and find min for lanes 8-15
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
        lea     tmp3, [idx*8]
        mov     tmp1, [job_rax + _cbcs_next_iv]
        vmovdqa xmm0, [state + _aes_args_IV + tmp3*2]
        vmovdqu [tmp1], xmm0

%ifdef SAFE_DATA
        ; Set bit of lane of returned job
        xor     DWORD(tmp3), DWORD(tmp3)
        bts     DWORD(tmp3), DWORD(idx)
        kmovw   k1, DWORD(tmp3)
        korw    k6, k1, k6

        ;; Clear IV and expanded keys of returned job and "NULL lanes"
        ;; (k6 contains the mask of the jobs)
        CLEAR_IV_KEYS_IN_NULL_LANES tmp1, xmm0, k6
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

mksection stack-noexec

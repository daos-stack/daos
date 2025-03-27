;;
;; Copyright (c) 2019-2021, Intel Corporation
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
%ifndef AES_CBC_ENC_X16
%define AES_CBC_ENC_X16 aes_cbc_enc_128_vaes_avx512
%define NUM_KEYS 11
%define SUBMIT_JOB_AES_ENC submit_job_aes128_enc_vaes_avx512
%endif

; void AES_CBC_ENC_X16(AES_ARGS_x16 *args, UINT64 len_in_bytes);
extern AES_CBC_ENC_X16

mksection .text

%ifdef LINUX
%define arg1    rdi
%define arg2    rsi
%else
%define arg1    rcx
%define arg2    rdx
%endif

%define state   arg1
%define job     arg2
%define len2    arg2

%define job_rax          rax

%if 1
; idx needs to be in rbp
%define len              rbp
%define idx              rbp
%define tmp              r10
%define tmp2             r11
%define tmp3             r12

%define lane             r8

%define iv               r9

%define unused_lanes     rbx
%endif

; STACK_SPACE needs to be an odd multiple of 8
; This routine and its callee clobbers all GPRs
struc STACK
_gpr_save:      resq    8
_rsp_save:      resq    1
endstruc

%macro INSERT_KEYS 6
%define %%KP    %1 ; [in] GP reg with pointer to expanded keys
%define %%LANE  %2 ; [in] GP reg with lane number
%define %%NKEYS %3 ; [in] number of round keys (numerical value)
%define %%COL   %4 ; [clobbered] GP reg
%define %%ZTMP  %5 ; [clobbered] ZMM reg
%define %%IA0   %6 ; [clobbered] GP reg

%assign ROW (16*16)

        mov             %%COL, %%LANE
        shl             %%COL, 4
        lea             %%IA0, [state + _aes_args_key_tab]
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

%if %%NKEYS > 11 ; 192 or 256 - copy 4 more keys
        vmovdqu64       %%ZTMP, [%%KP + 128]
        vextracti64x2   [%%COL + ROW*11], %%ZTMP, 3
%else   ; 128 - copy 3 more keys
        mov             %%IA0, 0x3f
        kmovq           k1, %%IA0
        vmovdqu64       %%ZTMP{k1}{z}, [%%KP + 128]
%endif
        vextracti64x2   [%%COL + ROW*8], %%ZTMP, 0
        vextracti64x2   [%%COL + ROW*9], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*10], %%ZTMP, 2

%if %%NKEYS == 15 ; 256 - 3 more keys
        mov             %%IA0, 0x3f
        kmovq           k1, %%IA0
        vmovdqu64       %%ZTMP{k1}{z}, [%%KP + 192]
        vextracti64x2   [%%COL + ROW*12], %%ZTMP, 0
        vextracti64x2   [%%COL + ROW*13], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*14], %%ZTMP, 2
%elif %%NKEYS == 13   ; 192 - 1 more key
        mov             %%IA0, 0x3
        kmovq           k1, %%IA0
        vmovdqu64       %%ZTMP{k1}{z}, [%%KP + 192]
        vextracti64x2   [%%COL + ROW*12], %%ZTMP, 0
%endif
%endmacro

%ifndef CBCS

; JOB* SUBMIT_JOB_AES_ENC(MB_MGR_AES_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_AES_ENC,function,internal)
SUBMIT_JOB_AES_ENC:
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
        and     len, -16                ; DOCSIS may pass size unaligned to block size
        mov     iv, [job + _iv]
        mov     [state + _aes_unused_lanes], unused_lanes
        add     qword [state + _aes_lanes_in_use], 1

        mov     [state + _aes_job_in_lane + lane*8], job

        ;; Update lane len
        vmovdqa64       ymm0, [state + _aes_lens]
        mov             tmp2, rcx       ; save rcx
        mov             rcx, lane
        mov             tmp, 1
        shl             tmp, cl
        mov             rcx, tmp2       ; restore rcx
        kmovq           k1, tmp

        vpbroadcastw    ymm1, WORD(len)
        vmovdqu16       ymm0{k1}, ymm1
        vmovdqa64       [state + _aes_lens], ymm0

        ;; Find min length for lanes 0-7
        vphminposuw     xmm2, xmm0

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

        cmp     qword [state + _aes_lanes_in_use], 16
        jne     return_null

        ; Find min length for lanes 8-15
        vpextrw         DWORD(len2), xmm2, 0   ; min value
        vpextrw         DWORD(idx), xmm2, 1   ; min index
        vextracti128    xmm1, ymm0, 1
        vphminposuw     xmm2, xmm1
        vpextrw         DWORD(tmp), xmm2, 0       ; min value
        cmp             DWORD(len2), DWORD(tmp)
        jle             use_min
        vpextrw         DWORD(idx), xmm2, 1   ; min index
        add             DWORD(idx), 8               ; but index +8
        mov             len2, tmp                    ; min len
use_min:
        cmp             len2, 0
        je              len_is_0

        vpbroadcastw    ymm3, WORD(len2)
        vpsubw          ymm0, ymm0, ymm3
        vmovdqa         [state + _aes_lens], ymm0

        ; "state" and "args" are the same address, arg1
        ; len is arg2
        call    AES_CBC_ENC_X16
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

%ifdef SAFE_DATA
        ;; Clear IV
        vpxorq  xmm0, xmm0
        shl     idx, 4 ; multiply by 16
        vmovdqa [state + _aes_args_IV + idx], xmm0

        ;; Clear expanded keys
%assign round 0
%rep NUM_KEYS
        vmovdqa [state + _aesarg_key_tab + round * (16*16) + idx], xmm0
%assign round (round + 1)
%endrep

%endif

return:
%ifndef SAFE_DATA
        vzeroupper
%endif
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

mksection stack-noexec

%endif ;; CBCS

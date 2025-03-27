;;
;; Copyright (c) 2021, Intel Corporation
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
%include "include/constants.asm"
%include "include/cet.inc"
%include "include/reg_sizes.asm"
%include "include/const.inc"
%include "include/clear_regs.asm"
%include "avx512/snow3g_uea2_by16_vaes_avx512.asm"

%ifndef SUBMIT_JOB_SNOW3G_UIA2
%define SUBMIT_JOB_SNOW3G_UIA2_GEN2     submit_job_snow3g_uia2_vaes_avx512
%define FLUSH_JOB_SNOW3G_UIA2_GEN2      flush_job_snow3g_uia2_vaes_avx512
%define SNOW3G_F9_1_BUFFER_INT_GEN2     snow3g_f9_1_buffer_internal_vaes_avx512

%define SUBMIT_JOB_SNOW3G_UIA2          submit_job_snow3g_uia2_avx512
%define FLUSH_JOB_SNOW3G_UIA2           flush_job_snow3g_uia2_avx512
%define SNOW3G_F9_1_BUFFER_INT          snow3g_f9_1_buffer_internal_avx
%endif
mksection .rodata
default rel

extern snow3g_f9_1_buffer_internal_vaes_avx512
extern snow3g_f9_1_buffer_internal_avx

%ifdef LINUX
%define arg1    rdi
%define arg2    rsi
%define arg3    rdx
%define arg4    rcx
%else
%define arg1    rcx
%define arg2    rdx
%define arg3    r8
%define arg4    r9
%endif

%define state   arg1
%define job     arg2

%define job_rax          rax

mksection .text

%define APPEND(a,b) a %+ b

%macro SUBMIT_FLUSH_JOB_SNOW3G_UIA2 2
%define %%SUBMIT_FLUSH  %1
%define %%GEN           %2 ;; [in] avx512_gen1/avx512_gen2

; idx needs to be in rbp
%define len              rbp
%define idx              rbp

%define lane             r8
%define unused_lanes     rbx
%define tmp              r12
%define tmp2             r13
%define tmp3             r14
%define init_lanes       r15

%xdefine tmp_state      tmp2

        SNOW3G_FUNC_START

%ifidn %%SUBMIT_FLUSH, submit

        mov     unused_lanes, [state + _snow3g_unused_lanes]
        mov     lane, unused_lanes
        and     lane, 0xF ;; just a nibble
        shr     unused_lanes, 4
        mov     [state + _snow3g_unused_lanes], unused_lanes
        add	qword [state + _snow3g_lanes_in_use], 1

        mov     [state + _snow3g_job_in_lane + lane*8], job

        ;; set lane mask
        xor     DWORD(tmp), DWORD(tmp)
        bts     DWORD(tmp), DWORD(lane)
        kmovw   k1, DWORD(tmp)

        ;; copy input, key and iv pointers to OOO mgr
        mov     tmp, [job + _src]
        add     tmp, [job + _hash_start_src_offset_in_bytes]
        mov     [state + _snow3g_args_in + lane*8], tmp
        mov     tmp, [job + _snow3g_uia2_key]
        mov     [state + _snow3g_args_keys + lane*8], tmp
        mov     tmp, [job + _snow3g_uia2_iv]
        mov     [state + _snow3g_args_IV + lane*8], tmp

        ;; insert len into proper lane
        mov     len, [job + _msg_len_to_hash_in_bits]

        ;; Update lane len
        vpbroadcastd    zmm1, DWORD(len)
        vmovdqa32       [state + _snow3g_lens]{k1}, zmm1

        cmp     qword [state + _snow3g_lanes_in_use], 16
        jne     %%return_null_uia2

        ;;      all lanes full and no jobs initialized - do init
        ;;      otherwise process next job
        cmp     word [state + _snow3g_init_done], 0
        jz      %%init_all_lanes_uia2

        ;; find next initialized job index
        xor     DWORD(idx), DWORD(idx)
        bsf     WORD(idx), word [state + _snow3g_init_done]

%else ;; FLUSH

        ; check ooo mgr empty
        cmp     qword [state + _snow3g_lanes_in_use], 0
        jz      %%return_null_uia2

        ; check for initialized jobs
        movzx   DWORD(tmp), word [state + _snow3g_init_done]
        bsf     DWORD(idx), DWORD(tmp)
        jnz     %%process_job_uia2

        ; no initialized jobs found
        ; - find valid job
        ; - copy valid job fields to empty lanes
        ; - initialize all lanes

        ; find null lanes
        vpxorq          zmm0, zmm0
        vmovdqa64       zmm1, [state + _snow3g_job_in_lane]
        vmovdqa64       zmm2, [state + _snow3g_job_in_lane + (8*8)]
        vpcmpq          k1, zmm1, zmm0, 0 ; EQ ; mask of null jobs (L8)
        vpcmpq          k2, zmm2, zmm0, 0 ; EQ ; mask of null jobs (H8)

        kshiftlw        k3, k2, 8
        korw            k3, k3, k1 ; mask of NULL jobs for all lanes

        ;; find first valid job
        kmovw           DWORD(init_lanes), k3
        not             WORD(init_lanes)
        bsf             DWORD(idx), DWORD(init_lanes)

        ;; copy input pointers
        mov             tmp3, [state + _snow3g_args_in + idx*8]
        vpbroadcastq    zmm1, tmp3
        vmovdqa64       [state + _snow3g_args_in + (0*8)]{k1}, zmm1
        vmovdqa64       [state + _snow3g_args_in + (8*8)]{k2}, zmm1
        ;; - copy key pointers
        mov             tmp3, [state + _snow3g_args_keys + idx*8]
        vpbroadcastq    zmm1, tmp3
        vmovdqa64       [state + _snow3g_args_keys + (0*8)]{k1}, zmm1
        vmovdqa64       [state + _snow3g_args_keys + (8*8)]{k2}, zmm1
        ;; - copy IV pointers
        mov             tmp3, [state + _snow3g_args_IV + idx*8]
        vpbroadcastq    zmm1, tmp3
        vmovdqa64       [state + _snow3g_args_IV + (0*8)]{k1}, zmm1
        vmovdqa64       [state + _snow3g_args_IV + (8*8)]{k2}, zmm1

        jmp             %%init_lanes_uia2
%endif

%%process_job_uia2:
        ;; preserve state for function call
        mov     tmp_state, state

        mov     arg1, [tmp_state + _snow3g_args_in + idx*8]
        lea     arg2, [idx*8]
        lea     arg2, [tmp_state + _snow3g_ks + arg2*4]   ;; arg2*4 = idx*32
        mov     DWORD(arg3), dword [tmp_state + _snow3g_lens + idx*4]
%ifidn %%GEN, avx512_gen2
        call    SNOW3G_F9_1_BUFFER_INT_GEN2
%else
        call    SNOW3G_F9_1_BUFFER_INT
%endif

        ;; restore state
        mov     state, tmp_state

        ;; copy digest temporarily
        mov     DWORD(tmp), eax

%%process_completed_job_submit_uia2:
        ; process completed job "idx"
        ;; - decrement number of jobs in use
        sub     qword [state + _snow3g_lanes_in_use], 1
        mov     job_rax, [state + _snow3g_job_in_lane + idx*8]
        mov     unused_lanes, [state + _snow3g_unused_lanes]
        mov     qword [state + _snow3g_job_in_lane + idx*8], 0
        or      dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
        ; Copy digest to auth tag output
        mov     tmp2, [job_rax + _auth_tag_output]
        mov     [tmp2], DWORD(tmp)
        shl     unused_lanes, 4
        or      unused_lanes, idx
        mov     [state + _snow3g_unused_lanes], unused_lanes
        btr     [state + _snow3g_init_done], WORD(idx)

%ifdef SAFE_DATA
        ;; clear keystream for processed job
        vpxorq          ymm0, ymm0
        shl             WORD(idx), 5 ;; ks stored at 32 byte offsets
        vmovdqa32       [state + _snow3g_ks + idx], ymm0
%endif

        jmp     %%return_uia2

%%init_all_lanes_uia2:
        ;; set initialized lanes mask for all 16 lanes
        ;; this is used to update OOO MGR after initialization
        mov     DWORD(init_lanes), 0xffff

%%init_lanes_uia2:

        SNOW3G_AUTH_INIT_5 {state + _snow3g_args_keys}, \
                           {state + _snow3g_args_IV}, \
                           {state + _snow3g_ks}, \
                           tmp, tmp2, k1, k2, k3, k4, k5, k6, \
                           %%GEN

        ;; update init_done for valid initialized lanes
        mov     [state + _snow3g_init_done], WORD(init_lanes)
        bsf     DWORD(idx), DWORD(init_lanes)

        ;; process first job
        jmp     %%process_job_uia2

%%return_uia2:
%ifndef SAFE_DATA
        vzeroupper
%else
        clear_scratch_zmms_asm
%endif

        SNOW3G_FUNC_END

        ret

%%return_null_uia2:
        xor     job_rax, job_rax
        jmp     %%return_uia2
%endmacro

; JOB* SUBMIT_JOB_SNOW3G_UIA2(MB_MGR_SNOW3G_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_SNOW3G_UIA2_GEN2,function,internal)
SUBMIT_JOB_SNOW3G_UIA2_GEN2:
        endbranch64
        SUBMIT_FLUSH_JOB_SNOW3G_UIA2 submit, avx512_gen2

MKGLOBAL(SUBMIT_JOB_SNOW3G_UIA2,function,internal)
SUBMIT_JOB_SNOW3G_UIA2:
        endbranch64
        SUBMIT_FLUSH_JOB_SNOW3G_UIA2 submit, avx512_gen1

; JOB* FLUSH_JOB_SNOW3G_UIA2(MB_MGR_SNOW3G_OOO *state)
; arg 1 : state
MKGLOBAL(FLUSH_JOB_SNOW3G_UIA2_GEN2,function,internal)
FLUSH_JOB_SNOW3G_UIA2_GEN2:
        endbranch64
        SUBMIT_FLUSH_JOB_SNOW3G_UIA2 flush, avx512_gen2

MKGLOBAL(FLUSH_JOB_SNOW3G_UIA2,function,internal)
FLUSH_JOB_SNOW3G_UIA2:
        endbranch64
        SUBMIT_FLUSH_JOB_SNOW3G_UIA2 flush, avx512_gen1

mksection stack-noexec

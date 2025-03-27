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

%include "include/mb_mgr_datastruct.asm"
%include "include/datastruct.asm"
%include "include/transpose_avx512.asm"
%include "include/imb_job.asm"
%include "include/os.asm"
%include "include/clear_regs.asm"

%include "include/cet.inc"
%include "avx512/snow3g_uea2_by16_vaes_avx512.asm"

%ifndef SUBMIT_JOB_SNOW3G_UEA2
%define SUBMIT_JOB_SNOW3G_UEA2_GEN2 submit_job_snow3g_uea2_vaes_avx512
%define FLUSH_JOB_SNOW3G_UEA2_GEN2 flush_job_snow3g_uea2_vaes_avx512
%define SUBMIT_JOB_SNOW3G_UEA2 submit_job_snow3g_uea2_avx512
%define FLUSH_JOB_SNOW3G_UEA2 flush_job_snow3g_uea2_avx512
%endif

%ifdef LINUX
%define arg1    rdi
%define arg2    rsi
%define tmp_gp1 rcx
%define tmp_gp2 rdx
%else
%define arg1    rcx
%define arg2    rdx
%define tmp_gp1 rdi
%define tmp_gp2 rsi
%endif

%define state    arg1
%define job      arg2

%define job_rax  rax

%define tmp_gp3  rbx
%define tmp_gp4  rbp
%define tmp_gp5  r9
%define tmp_gp6  r10
%define tmp_gp7  r11
%define tmp_gp8  r12
%define tmp_gp9  r13
%define tmp_gp10 r14
%define tmp_gp11 r15

mksection .rodata
default rel

align 64
dd_0_to_15:
        dd 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15

mksection .text

%macro SUBMIT_FLUSH_JOB_SNOW3G_UEA2 13
%define %%SUBMIT_FLUSH    %1  ;; [in] submit/flush selector
%define %%INIT_FLAG       %2  ;; [clobbered] GP register
%define %%UNUSED_LANES    %3  ;; [clobbered] GP register
%define %%LANE            %4  ;; [clobbered] GP register
%define %%TGP0            %5  ;; [clobbered] GP register
%define %%TGP1            %6  ;; [clobbered] GP register
%define %%TGP2            %7  ;; [clobbered] GP register
%define %%TGP3            %8  ;; [clobbered] GP register
%define %%TGP4            %9  ;; [clobbered] GP register
%define %%TGP5            %10 ;; [clobbered] GP register
%define %%MIN_COMMON_LEN  %11 ;; [clobbered] GP register
%define %%OFFSET          %12 ;; [clobbered] GP register
%define %%GEN             %13 ;; [in] avx512_gen1/avx512_gen2

        xor     job_rax, job_rax        ;; assume NULL return job

%ifidn %%SUBMIT_FLUSH, submit
        ;; unused lanes is a list of all unused lane ids (0-15)
        mov     %%UNUSED_LANES, [state + _snow3g_unused_lanes]
        mov     %%LANE, %%UNUSED_LANES
        and     %%LANE, 0xF ; max 16 lanes
        shr     %%UNUSED_LANES, 4
        mov     [state + _snow3g_unused_lanes], %%UNUSED_LANES
        add	qword [state + _snow3g_lanes_in_use], 1
        mov     [state + _snow3g_job_in_lane + %%LANE*8], job

        ;; set lane mask
        xor             %%TGP0, %%TGP0
        bts             DWORD(%%TGP0), DWORD(%%LANE)
        kmovd           k1, DWORD(%%TGP0)

        ;; Initialize LFSR and FSM registers
        mov             %%TGP1, [job + _enc_keys]
        mov             %%TGP2, [job + _iv]

        LFSR_FSM_INIT_SUBMIT state, k1, %%TGP1, %%TGP2, \
                                TEMP_29, TEMP_30, TEMP_31, %%TGP3, %%TGP4

        ;; _INIT_MASK is common mask for clocking loop
        kmovw           k6, [state + _snow3g_INIT_MASK]
        korw            k6, k1, k6
        kmovw           [state + _snow3g_INIT_MASK], k6

        ;; 32 iterations of FSM and LFSR clock are needed
        ;; LD_ST_MASK is used to determine if any data should
        ;; be read from src and written to dst
        ;; When set to 0 so no reads/writes occur
        mov             qword [state + _snow3g_args_LD_ST_MASK + %%LANE*8], 0

        vmovdqa32       zmm0, [state + _snow3g_lens_dw]
        mov             DWORD(%%TGP0), 32
        vpbroadcastd    zmm0{k1}, DWORD(%%TGP0)
        vmovdqa32       [state + _snow3g_lens_dw], zmm0

        ;; insert length into proper lane
        ;; - convert from bits to bytes (round up)
        mov             %%TGP0, [job + _msg_len_to_cipher_in_bits]
        add             %%TGP0, 7
        shr             %%TGP0, 3
        mov             [state + _snow3g_args_byte_length + %%LANE*8], %%TGP0

        cmp             qword [state + _snow3g_lanes_in_use], 16
        jne             %%return_uea2   ;; RAX is NULL
        ;; if all lanes are busy fall through to %%process_job_uea2
%else   ;; FLUSH
        cmp             qword [state + _snow3g_lanes_in_use], 0
        je              %%return_uea2   ;; RAX is NULL
%endif

        ;; ---------------------------------------------------------------------
        ;; All lanes are busy or flush is called - process used lanes until
        ;; one job is done.
        ;; ---------------------------------------------------------------------
        ;; State of the job is identified with:
        ;;   _snow3g_INIT_MASK - if bit set then init xor
        ;;   _snow3g_args_LD_ST_MASK - no output written
        ;;   _snow3g_args_byte_length - message lengths to be processed (bytes)
        ;;
        ;; START -> INIT1 -> INIT2 -> WORK1 -> COMPLETE <-+
        ;;                              |                 |
        ;;                              +-> WORK2 --------+
        ;;
        ;; Each of the lanes can be in any of 4 states (INIT1, INIT2, WORK1 or
        ;; WORK2) and they can be processed in parallel by the algorithmic code.
        ;; ---------------------------------------------------------------------

%%_find_min:
        ;; Find minimum length
        vmovdqa32       zmm0, [state + _snow3g_lens_dw]
        vpslld          zmm0, zmm0, 4
        vpord           zmm0, zmm0, [rel dd_0_to_15]
        vextracti64x4   ymm1, zmm0, 1

        vpminud         ymm0, ymm0, ymm1
        vextracti32x4   xmm1, ymm0, 1
        vpminud         xmm0, xmm0, xmm1
        vpsrldq         xmm1, xmm0, 8
        vpminud         xmm0, xmm0, xmm1
        vpsrldq         xmm1, xmm0, 4
        vpminud         xmm0, xmm0, xmm1

        vmovd           DWORD(%%MIN_COMMON_LEN), xmm0
        mov             DWORD(%%LANE), DWORD(%%MIN_COMMON_LEN)
        and             DWORD(%%LANE), 15               ;; keep lane index on 4 least significant bits
        xor             %%TGP0, %%TGP0
        bts             DWORD(%%TGP0), DWORD(%%LANE)
        kmovd           k7, DWORD(%%TGP0)               ;; k7 holds mask of the min LANE
        shr             DWORD(%%MIN_COMMON_LEN), 4      ;; remove index from 4 least significant bits
        jz              %%_len_is_0

        ;; subtract common minimum length from all lanes lengths
        vmovdqa32       zmm0, [state + _snow3g_lens_dw]
        vpbroadcastd    zmm1, DWORD(%%MIN_COMMON_LEN)
%ifidn %%SUBMIT_FLUSH, submit
        vpsubd          zmm0, zmm0, zmm1
%else ; FLUSH
        vpcmpeqd        k1, zmm0, [rel all_fs]
        knotw           k1, k1
        vpsubd          zmm0{k1}, zmm0, zmm1
%endif
        vmovdqa32       [state + _snow3g_lens_dw], zmm0

        ;; Do cipher / clock operation for all lanes and given common length
        SNOW_3G_KEYSTREAM state, %%MIN_COMMON_LEN, {state + _snow3g_args_in}, \
                          {state + _snow3g_args_out}, %%OFFSET, \
                          %%TGP0, %%TGP1, %%TGP2, \
                          k1, k2, k3, k4, k5, k6, %%GEN

        ;; save DST[i] = DST[i] + %%OFFSET
        ;; save SRC[i] = SRC[i] + %%OFFSET
        ;; for not initialized lanes this will create invalid pointers
        ;; but no loads/stores take place thanks to masking (LD_ST_MASK)
        vpbroadcastq    zmm0, %%OFFSET
        vpaddq          zmm1, zmm0, [state + _snow3g_args_in + 0*8]
        vpaddq          zmm2, zmm0, [state + _snow3g_args_in + 8*8]
        vpaddq          zmm3, zmm0, [state + _snow3g_args_out + 0*8]
        vpaddq          zmm4, zmm0, [state + _snow3g_args_out + 8*8]
        vmovdqa32       [state + _snow3g_args_in + 0*8], zmm1
        vmovdqa32       [state + _snow3g_args_in + 8*8], zmm2
        vmovdqa32       [state + _snow3g_args_out + 0*8], zmm3
        vmovdqa32       [state + _snow3g_args_out + 8*8], zmm4

%%_len_is_0:
        ;; Four states are possible here:
        ;;   INIT1) initialization phase is complete
        ;;   INIT2) 1st key stream word after initialization was discarded
        ;;   WORK1) message processed for the size aligned to double words
        ;;   WORK2) message processed for the trailing bytes below one double word

        ;; check if the job is in one of INIT1 or INIT2
        test            qword [state + _snow3g_args_LD_ST_MASK + %%LANE*8], 0xffffffff_ffffffff
        jz              %%_init_phase_in_progress

        ;; The job is in WORK1 or WORK2 state
        ;; - This is determined by content of _snow3g_args_byte_length.
        ;;   If it is zero then this is WORK2 state and the job processing is complete
        ;; - Non-zero content with odd bytes requiring processing => WORK1
        mov             %%TGP0, [state + _snow3g_args_byte_length + %%LANE*8]
        and             %%TGP0, 0x3
        jz              %%process_completed_job_submit_uea2

        ;; Outstanding bytes to process (less than 32-bits)
        ;; - depending on number of bytes set flag to 1/3/7
        xor             DWORD(%%TGP1), DWORD(%%TGP1)
        bts             DWORD(%%TGP1), DWORD(%%TGP0)
        dec             DWORD(%%TGP1)
        mov             [state + _snow3g_args_LD_ST_MASK + %%LANE*8], %%TGP1

        ;; set length in double words to 1
        vmovdqa32       zmm0, [state + _snow3g_lens_dw]
        mov             DWORD(%%TGP0), 1
        vpbroadcastd    zmm0{k7}, DWORD(%%TGP0)
        vmovdqa32       [state + _snow3g_lens_dw], zmm0

        ;; clear the length so that the job can transition to completion
        mov             qword [state + _snow3g_args_byte_length + %%LANE*8], 0
        jmp             %%_find_min

%%_init_phase_in_progress:
        ;; This is INIT1 or INIT2 state
        bt              word [state + _snow3g_INIT_MASK], WORD(%%LANE)
        jnc             %%_init_done

        ;; The lane is INIT1
        ;; - the job finished first phase of initialization (32 iterations)
        ;; - it can transition to INIT2 (1 iteration)
        btr             word [state + _snow3g_INIT_MASK], WORD(%%LANE)  ;; INIT_MASK[LANE] = 0

        vmovdqa32       zmm0, [state + _snow3g_lens_dw]
        mov             DWORD(%%TGP0), 1
        vpbroadcastd    zmm0{k7}, DWORD(%%TGP0)
        vmovdqa32       [state + _snow3g_lens_dw], zmm0
        jmp             %%_find_min

%%_init_done:
        ;; The lane is in INIT2 state
        ;; - just finished 2 phase of initialization (1 iteration)
        ;; - it can transition to WORK1 state
        mov             qword [state + _snow3g_args_LD_ST_MASK + %%LANE*8], 0xffffffff_ffffffff

        ;; length in double words = original length in bytes / 4
        ;; - odd bytes are processed later
        mov             %%TGP0, [state + _snow3g_args_byte_length + %%LANE*8]
        shr             %%TGP0, 2
        vmovdqa32       zmm0, [state + _snow3g_lens_dw]
        vpbroadcastd    zmm0{k7}, DWORD(%%TGP0)
        vmovdqa32       [state + _snow3g_lens_dw], zmm0

        ;; set the correct in & out pointers
        mov             %%TGP0, [state + _snow3g_job_in_lane + %%LANE*8]
        mov             %%TGP1, [%%TGP0 + _cipher_start_offset_in_bits]
        shr             %%TGP1, 3               ;; convert from bits to bytes (src & dst)

        mov             %%TGP2, [%%TGP0 + _dst]
        add             %%TGP2, %%TGP1
        mov             [state + _snow3g_args_out + %%LANE*8], %%TGP2

        add             %%TGP1, [%%TGP0 + _src]
        mov             [state + _snow3g_args_in + %%LANE*8], %%TGP1

        jmp             %%_find_min

%%process_completed_job_submit_uea2:
        ;; COMPLETE: return job, change job length to UINT32_MAX
        vmovdqa32       zmm0, [state + _snow3g_lens_dw]
        mov             DWORD(%%TGP0), 0xffffffff
        vpbroadcastd    zmm0{k7}, DWORD(%%TGP0)
        vmovdqa32       [state + _snow3g_lens_dw], zmm0

        ;; required in case of flush
        mov             qword [state + _snow3g_args_LD_ST_MASK + %%LANE*8], 0

        ;; decrement number of jobs in use
        dec             qword [state + _snow3g_lanes_in_use]

        mov             job_rax, [state + _snow3g_job_in_lane + %%LANE*8]
        or              qword [job_rax + _status], IMB_STATUS_COMPLETED_CIPHER

        mov             %%UNUSED_LANES, [state + _snow3g_unused_lanes]
        mov             qword [state + _snow3g_job_in_lane + %%LANE*8], 0
        shl             %%UNUSED_LANES, 4
        or              %%UNUSED_LANES, %%LANE
        mov             [state + _snow3g_unused_lanes], %%UNUSED_LANES

%ifdef SAFE_DATA
        ;; clear finished job lane, %%LANE is an index of finished job
        ;; - LFSR
        ;; - FSM
        vpxorq          zmm0, zmm0, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_0]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_1]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_2]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_3]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_4]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_5]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_6]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_7]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_8]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_9]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_10]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_11]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_12]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_13]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_14]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_LFSR_15]{k7}, zmm0

        vmovdqa32       [state + _snow3g_args_FSM_1]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_FSM_2]{k7}, zmm0
        vmovdqa32       [state + _snow3g_args_FSM_3]{k7}, zmm0

        ;; clear key stream stack frame
        vmovdqa64        [rsp + _keystream + 0 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 1 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 2 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 3 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 4 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 5 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 6 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 7 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 8 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 9 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 10 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 11 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 12 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 13 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 14 * 64], zmm0
        vmovdqa64        [rsp + _keystream + 15 * 64], zmm0

        ;; clear register contents
        clear_scratch_zmms_asm
%endif

%%return_uea2:

%endmacro

;; JOB* SUBMIT_JOB_SNOW3G_UEA2(MB_MGR_SNOW3G_OOO *state, IMB_JOB *job)
;; arg 1 : state
;; arg 2 : job
MKGLOBAL(SUBMIT_JOB_SNOW3G_UEA2_GEN2,function,internal)
SUBMIT_JOB_SNOW3G_UEA2_GEN2:
        endbranch64
        SNOW3G_FUNC_START
        SUBMIT_FLUSH_JOB_SNOW3G_UEA2 submit, tmp_gp1, tmp_gp2, tmp_gp3, tmp_gp4, tmp_gp5, tmp_gp6, tmp_gp7, tmp_gp8, tmp_gp9, tmp_gp10, tmp_gp11, avx512_gen2
        SNOW3G_FUNC_END
        ret
MKGLOBAL(SUBMIT_JOB_SNOW3G_UEA2,function,internal)
SUBMIT_JOB_SNOW3G_UEA2:
        endbranch64
        SNOW3G_FUNC_START
        SUBMIT_FLUSH_JOB_SNOW3G_UEA2 submit, tmp_gp1, tmp_gp2, tmp_gp3, tmp_gp4, tmp_gp5, tmp_gp6, tmp_gp7, tmp_gp8, tmp_gp9, tmp_gp10, tmp_gp11, avx512_gen1
        SNOW3G_FUNC_END
        ret

;; JOB* FLUSH_JOB_SNOW3G_UEA2(MB_MGR_SNOW3G_OOO *state)
;; arg 1 : state
MKGLOBAL(FLUSH_JOB_SNOW3G_UEA2_GEN2,function,internal)
FLUSH_JOB_SNOW3G_UEA2_GEN2:
        endbranch64
        SNOW3G_FUNC_START
        SUBMIT_FLUSH_JOB_SNOW3G_UEA2 flush, tmp_gp1, tmp_gp2, tmp_gp3, tmp_gp4, tmp_gp5, tmp_gp6, tmp_gp7, tmp_gp8, tmp_gp9, tmp_gp10, tmp_gp11, avx512_gen2
        SNOW3G_FUNC_END
        ret
MKGLOBAL(FLUSH_JOB_SNOW3G_UEA2,function,internal)
FLUSH_JOB_SNOW3G_UEA2:
        endbranch64
        SNOW3G_FUNC_START
        SUBMIT_FLUSH_JOB_SNOW3G_UEA2 flush, tmp_gp1, tmp_gp2, tmp_gp3, tmp_gp4, tmp_gp5, tmp_gp6, tmp_gp7, tmp_gp8, tmp_gp9, tmp_gp10, tmp_gp11, avx512_gen1
        SNOW3G_FUNC_END
        ret
mksection stack-noexec

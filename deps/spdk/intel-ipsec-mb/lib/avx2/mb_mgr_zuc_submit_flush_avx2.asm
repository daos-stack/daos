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

%define SUBMIT_JOB_ZUC128_EEA3 submit_job_zuc_eea3_avx2
%define FLUSH_JOB_ZUC128_EEA3 flush_job_zuc_eea3_avx2
%define SUBMIT_JOB_ZUC256_EEA3 submit_job_zuc256_eea3_avx2
%define FLUSH_JOB_ZUC256_EEA3 flush_job_zuc256_eea3_avx2
%define SUBMIT_JOB_ZUC128_EIA3 submit_job_zuc_eia3_avx2
%define FLUSH_JOB_ZUC128_EIA3 flush_job_zuc_eia3_avx2
%define SUBMIT_JOB_ZUC256_EIA3 submit_job_zuc256_eia3_avx2
%define FLUSH_JOB_ZUC256_EIA3 flush_job_zuc256_eia3_avx2
%define ZUC128_INIT_8        asm_ZucInitialization_8_avx2
%define ZUC256_INIT_8        asm_Zuc256Initialization_8_avx2

mksection .rodata
default rel

align 16
broadcast_word:
db      0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01
db      0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01

align 32
clear_lane_mask_tab:
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,

clear_lane_mask_tab_start:
dd      0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,

align 16
bitmask_to_dword_tab:
dd      0x00000000, 0x00000000, 0x00000000, 0x00000000
dd      0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000
dd      0x00000000, 0xFFFFFFFF, 0x00000000, 0x00000000
dd      0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000
dd      0x00000000, 0x00000000, 0xFFFFFFFF, 0x00000000
dd      0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000
dd      0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000
dd      0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF
dd      0xFFFFFFFF, 0x00000000, 0x00000000, 0xFFFFFFFF
dd      0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF
dd      0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF
dd      0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF
dd      0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF
dd      0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
dd      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF

extern zuc_eia3_8_buffer_job_avx2
extern zuc256_eia3_8_buffer_job_avx2
extern asm_ZucInitialization_8_avx2
extern asm_Zuc256Initialization_8_avx2
extern asm_ZucCipher_8_avx2

%ifdef LINUX
%define arg1    rdi
%define arg2    rsi
%define arg3    rdx
%define arg4    rcx
%define arg5    r8
%define arg6    r9
%else
%define arg1    rcx
%define arg2    rdx
%define arg3    r8
%define arg4    r9
%define arg5    [rsp + 32]
%define arg6    [rsp + 40]
%endif

%define state   arg1
%define job     arg2

%define job_rax          rax

; This routine and its callee clobbers all GPRs
struc STACK
_gpr_save:      resq    10
_null_len_save: resq    2
_rsp_save:      resq    1
endstruc

mksection .text

%define APPEND(a,b) a %+ b
%define APPEND3(a,b,c) a %+ b %+ c

;; Clear state for multiple lanes in the OOO managers
%macro CLEAR_ZUC_STATE 4
%define %%STATE         %1 ;; [in] ZUC OOO manager pointer
%define %%LANE_MASK     %2 ;; [in/clobbered] bitmask with lanes to clear
%define %%TMP           %3 ;; [clobbered] Temporary GP register
%define %%TMP2          %4 ;; [clobbered] Temporary GP register

        ; Load mask for lanes 0-3
        mov     %%TMP, %%LANE_MASK
        and     %%TMP, 0xf
        lea     %%TMP2, [rel bitmask_to_dword_tab]
        shl     %%TMP, 4 ; Multiply by 16 to move through the table
        add     %%TMP2, %%TMP
        vmovdqa xmm0, [%%TMP2] ; Mask for first 4 lanes

        ; Load mask for lanes 4-7
        and     %%LANE_MASK, 0xf0
        lea     %%TMP2, [rel bitmask_to_dword_tab]
        add     %%TMP2, %%LANE_MASK ; lane_mask already multipied by 16 to move through the table
        vmovdqa xmm1, [%%TMP2]

        ;; Clear state for lanes
%assign I 0
%rep (16 + 6)
        ; First 4 lanes
        vmovdqa xmm2, [%%STATE + _zuc_state + I*32]
        vpandn  xmm2, xmm0, xmm2
        vmovdqa [%%STATE + _zuc_state + I*32], xmm2

        ; Next 4 lanes
        vmovdqa xmm3, [%%STATE + _zuc_state + I*32 + 16]
        vpandn  xmm3, xmm1, xmm3
        vmovdqa [%%STATE + _zuc_state + I*32 + 16], xmm3
%assign I (I + 1)
%endrep
%endmacro

;; Clear state for a specified lane in the OOO manager
%macro CLEAR_ZUC_LANE_STATE 5
%define %%STATE         %1 ;; [in] ZUC OOO manager pointer
%define %%LANE          %2 ;; [in/clobbered] lane index
%define %%TMP           %3 ;; [clobbered] Temporary GP register
%define %%YTMP1         %4 ;; [clobbered] Temporary YMM register
%define %%YTMP2         %5 ;; [clobbered] Temporary YMM register

        shl     %%LANE, 2
        lea     %%TMP, [rel clear_lane_mask_tab_start]
        sub     %%TMP, %%LANE
        vmovdqu %%YTMP1, [%%TMP]
%assign I 0
%rep (16 + 6)
        vmovdqa %%YTMP2, [%%STATE + _zuc_state + I*32]
        vpand   %%YTMP2, %%YTMP1
        vmovdqa [%%STATE + _zuc_state + I*32], %%YTMP2
%assign I (I + 1)
%endrep

%endmacro

%macro SUBMIT_JOB_ZUC_EEA3 1
%define %%KEY_SIZE      %1 ; [constant] Key size (128 or 256)

; idx needs to be in rbp
%define len              rbp
%define idx              rbp

%define lane             r8
%define unused_lanes     rbx
%define tmp              r11
%define tmp2             r13
%define tmp3             r14
%define min_len          r15

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
        mov     [rsp + _gpr_save + 8*8], state
        mov     [rsp + _gpr_save + 8*9], job
        mov     [rsp + _rsp_save], rax  ; original SP

        mov     unused_lanes, [state + _zuc_unused_lanes]
        mov     lane, unused_lanes
        and     lane, 0xF ;; just a nibble
        shr     unused_lanes, 4
        mov     tmp, [job + _iv]
        shl     lane, 5
%if %%KEY_SIZE == 128
        ; Read first 16 bytes of IV
        vmovdqu xmm0, [tmp]
        vmovdqa [state + _zuc_args_IV + lane], xmm0
%else ;; %%KEY_SIZE == 256
        cmp     qword [job + _iv_len_in_bytes], 25
        je      %%_iv_size_25
%%_iv_size_23:
        ; Read 23 bytes of IV and expand to 25 bytes
        ; then expand the last 6 bytes to 8 bytes

        ; Read and write first 16 bytes
        vmovdqu xmm0, [tmp]
        vmovdqa [state + _zuc_args_IV + lane], xmm0
        ; Read and write next byte
        mov     al, [tmp + 16]
        mov     [state + _zuc_args_IV + lane + 16], al
        ; Read next 6 bytes
        movzx   DWORD(tmp2), word [tmp + 17]
        mov     DWORD(tmp3), [tmp + 19]
        shl     tmp2, 32
        or      tmp2, tmp3
        ; Expand to 8 bytes and write
        mov     tmp3, 0x3f3f3f3f3f3f3f3f
        pdep    tmp2, tmp2, tmp3
        mov     [state + _zuc_args_IV + lane + 17], tmp2

        jmp     %%_iv_read
%%_iv_size_25:
        ; Read 25 bytes of IV
        vmovdqu xmm0, [tmp]
        vmovdqa [state + _zuc_args_IV + lane], xmm0
        vmovq   xmm0, [tmp + 16]
        vpinsrb xmm0, [tmp + 24], 8
        vmovdqa [state + _zuc_args_IV + lane + 16], xmm0
%%_iv_read:
%endif
        shr     lane, 5
        mov     [state + _zuc_unused_lanes], unused_lanes

        mov     [state + _zuc_job_in_lane + lane*8], job
        ; New job that needs init (update bit in zuc_init_not_done bitmask)
        SHIFT_GP        1, lane, tmp, tmp2, left
        or      [state + _zuc_init_not_done], WORD(tmp)
        not     tmp
        and     [state + _zuc_unused_lane_bitmask], BYTE(tmp)

        mov     tmp, [job + _src]
        add     tmp, [job + _cipher_start_src_offset_in_bytes]
        mov     [state + _zuc_args_in + lane*8], tmp
        mov     tmp, [job + _enc_keys]
        mov     [state + _zuc_args_keys + lane*8], tmp
        mov     tmp, [job + _dst]
        mov     [state + _zuc_args_out + lane*8], tmp

        ;; insert len into proper lane
        mov     len, [job + _msg_len_to_cipher_in_bytes]

        vmovdqa xmm0, [state + _zuc_lens]
        XVPINSRW xmm0, xmm1, tmp, lane, len, scale_x16
        vmovdqa [state + _zuc_lens], xmm0

        cmp     unused_lanes, 0xf
        jne     %%return_null_submit_eea3

        ; Find minimum length (searching for zero length,
        ; to retrieve already encrypted buffers)
        vphminposuw     xmm1, xmm0
        vpextrw min_len, xmm1, 0   ; min value
        vpextrw idx, xmm1, 1    ; min index (0...7)
        cmp     min_len, 0
        je      %%len_is_0_submit_eea3

        ; Move state into r12, as register for state will be used
        ; to pass parameter to next function
        mov     r12, state

        ;; Save state into stack
        sub     rsp, (16*32 + 2*32) ; LFSR registers + R1-R2

%assign I 0
%rep (16 + 2)
        vmovdqa ymm0, [r12 + _zuc_state + 32*I]
        vmovdqu [rsp + 32*I], ymm0
%assign I (I + 1)
%endrep

        ;; If Windows, reserve memory in stack for parameter transferring
%ifndef LINUX
        ;; 32 bytes for 4 parameters
        sub     rsp, 32
%endif
        lea     arg1, [r12 + _zuc_args_keys]
        lea     arg2, [r12 + _zuc_args_IV]
        lea     arg3, [r12 + _zuc_state]
%if %%KEY_SIZE == 256
        ;; Setting "tag size" to 2 in case of ciphering
        ;; (dummy size, just for constant selecion at Initialization)
        mov     arg4, 2
%endif

%if %%KEY_SIZE == 128
        call    ZUC128_INIT_8
%else
        call    ZUC256_INIT_8
%endif

%ifndef LINUX
        add     rsp, 32
%endif

        cmp     word [r12 + _zuc_init_not_done], 0xff ; Init done for all lanes
        je      %%skip_submit_restoring_state

        ; Load mask for lanes 0-3
        movzx   DWORD(tmp), word [r12 + _zuc_init_not_done]
        mov     tmp2, tmp
        and     tmp2, 0xf
        lea     tmp3, [rel bitmask_to_dword_tab]
        shl     tmp2, 4 ; Multiply by 16 to move through the table
        add     tmp3, tmp2
        vmovdqa xmm2, [tmp3] ; Mask for first 4 lanes

        ; Load mask for lanes 4-7
        and     tmp, 0xf0
        lea     tmp3, [rel bitmask_to_dword_tab]
        add     tmp3, tmp ; tmp already multipied by 16 to move through the table
        vmovdqa xmm5, [tmp3]

        ;; Restore state from stack for lanes that did not need init
%assign I 0
%rep (16 + 2)
        ; First 4 lanes
        vmovdqu xmm0, [rsp + 32*I] ; State before init
        vmovdqa xmm1, [r12 + _zuc_state + 32*I] ; State after init

        vpand   xmm3, xmm2, xmm1 ;; Zero out lanes that need to be restored in current state
        vpandn  xmm4, xmm2, xmm0 ;; Zero out lanes that do not need to be restored in saved state
        vpor    xmm4, xmm3       ;; Combine both states
        vmovdqa [r12 + _zuc_state + 32*I], xmm4 ; Save new state

        ; Next 4 lanes
        vmovdqu xmm0, [rsp + 32*I + 16] ; State before init
        vmovdqa xmm1, [r12 + _zuc_state + 32*I + 16] ; State after init
        vpand   xmm3, xmm5, xmm1 ;; Zero out lanes that need to be restored in current state
        vpandn  xmm4, xmm5, xmm0 ;; Zero out lanes that do not need to be restored in saved state
        vpor    xmm4, xmm3       ;; Combine both states
        vmovdqa [r12 + _zuc_state + 32*I + 16], xmm4 ; Save new state
%assign I (I + 1)
%endrep

%%skip_submit_restoring_state:
%ifdef SAFE_DATA
        ;; Clear stack containing state info
        vpxor   ymm0, ymm0
%assign I 0
%rep (16 + 2)
        vmovdqu [rsp + 32*I], ymm0
%assign I (I + 1)
%endrep
%endif
        add     rsp, (16*32 + 2*32) ; Restore stack pointer

        mov     word [r12 + _zuc_init_not_done], 0 ; Init done for all lanes

        ;; If Windows, reserve memory in stack for parameter transferring
%ifndef LINUX
        ;; 40 bytes for 5 parameters
        sub     rsp, 40
%endif
        lea     arg1, [r12 + _zuc_state]
        lea     arg2, [r12 + _zuc_args_in]
        lea     arg3, [r12 + _zuc_args_out]
        lea     arg4, [r12 + _zuc_lens]
        mov     arg5, min_len

        call    asm_ZucCipher_8_avx2

%ifndef LINUX
        add     rsp, 40
%endif
        mov     state, [rsp + _gpr_save + 8*8]
        mov     job,   [rsp + _gpr_save + 8*9]

%%len_is_0_submit_eea3:
        ; process completed job "idx"
        mov     job_rax, [state + _zuc_job_in_lane + idx*8]
        mov     unused_lanes, [state + _zuc_unused_lanes]
        mov     qword [state + _zuc_job_in_lane + idx*8], 0
        or      dword [job_rax + _status], IMB_STATUS_COMPLETED_CIPHER
        shl     unused_lanes, 4
        or      unused_lanes, idx
        mov     [state + _zuc_unused_lanes], unused_lanes
        SHIFT_GP        1, idx, tmp, tmp2, left
        or      [state + _zuc_unused_lane_bitmask], BYTE(tmp)

%ifdef SAFE_DATA
        ; Clear ZUC state of the lane that is returned
        CLEAR_ZUC_LANE_STATE state, idx, tmp, ymm0, ymm1
%endif

%%return_submit_eea3:

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

%%return_null_submit_eea3:
        xor     job_rax, job_rax
        jmp     %%return_submit_eea3
%endmacro

%macro FLUSH_JOB_ZUC_EEA3 1
%define %%KEY_SIZE      %1 ; [constant] Key size (128 or 256)

%define unused_lanes     rbx
%define tmp1             rbx

%define tmp2             rax

; idx needs to be in rbp
%define tmp              rbp
%define idx              rbp

%define tmp3             r8
%define tmp4             r9
%define tmp5             r10
%define min_len          r14 ; Will be maintained after function calls

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
        mov     [rsp + _gpr_save + 8*8], state
        mov     [rsp + _rsp_save], rax  ; original SP

        ; check for empty
        mov     unused_lanes, [state + _zuc_unused_lanes]
        bt      unused_lanes, 32+3
        jc      %%return_null_flush_eea3

        ; Set length = 0xFFFF in NULL jobs
        vmovdqa xmm0, [state + _zuc_lens]
%assign I 0
%rep 8
        cmp     qword [state + _zuc_job_in_lane + I*8], 0
        jne     APPEND(%%skip_copy_ffs_,I)
        mov     idx, (I << 4)
        XVPINSRW xmm0, xmm1, tmp3, idx, 0xffff, no_scale
APPEND(%%skip_copy_ffs_,I):
%assign I (I+1)
%endrep

        vmovdqa [state + _zuc_lens], xmm0

        ; Find minimum length (searching for zero length,
        ; to retrieve already encrypted buffers)
        vphminposuw     xmm1, xmm0
        vpextrw min_len, xmm1, 0   ; min value
        vpextrw idx, xmm1, 1    ; min index (0...7)
        cmp     min_len, 0
        je      %%len_is_0_flush_eea3

        ; copy good_lane to empty lanes
        mov     tmp1, [state + _zuc_args_in + idx*8]
        mov     tmp2, [state + _zuc_args_out + idx*8]
        mov     tmp3, [state + _zuc_args_keys + idx*8]

%assign I 0
%rep 8
        cmp     qword [state + _zuc_job_in_lane + I*8], 0
        jne     APPEND(%%skip_eea3_,I)
        mov     [state + _zuc_args_in + I*8], tmp1
        mov     [state + _zuc_args_out + I*8], tmp2
        mov     [state + _zuc_args_keys + I*8], tmp3
APPEND(%%skip_eea3_,I):
%assign I (I+1)
%endrep

        ; Move state into r12, as register for state will be used
        ; to pass parameter to next function
        mov     r12, state

        cmp     word [r12 + _zuc_init_not_done], 0
        je      %%skip_flush_init

        ;; Save state into stack
        sub     rsp, (16*32 + 2*32) ; LFSR registers + R1-R2

%assign I 0
%rep (16 + 2)
        vmovdqa ymm0, [r12 + _zuc_state + 32*I]
        vmovdqu [rsp + 32*I], ymm0
%assign I (I + 1)
%endrep

        ;; If Windows, reserve memory in stack for parameter transferring
%ifndef LINUX
        ;; 32 bytes for 4 parameters
        sub     rsp, 32
%endif
        lea     arg1, [r12 + _zuc_args_keys]
        lea     arg2, [r12 + _zuc_args_IV]
        lea     arg3, [r12 + _zuc_state]
%if %%KEY_SIZE == 256
        ;; Setting "tag size" to 2 in case of ciphering
        ;; (dummy size, just for constant selecion at Initialization)
        mov     arg4, 2
%endif

%if %%KEY_SIZE == 128
        call    ZUC128_INIT_8
%else
        call    ZUC256_INIT_8
%endif

%ifndef LINUX
        add     rsp, 32
%endif
        cmp     word [r12 + _zuc_init_not_done], 0xff ; Init done for all lanes
        je      %%skip_flush_restoring_state

        ;; Restore state from stack for lanes that did not need init
%assign I 0
%rep (16 + 2)
        ; First 4 lanes
        vmovdqu xmm0, [rsp + 32*I] ; State before init
        vmovdqa xmm1, [r12 + _zuc_state + 32*I] ; State after init
%assign J 0
%rep 4
        test    word [r12 + _zuc_init_not_done], (1 << J)
        jnz     APPEND3(%%skip_flush_lane_,I,J)
        ;; Extract dword from ymm0
        vpextrd r15, xmm0, J ; value
        mov     r8, (J << 4) ; index

        XVPINSRD xmm1, xmm2, r13, r8, r15, no_scale

APPEND3(%%skip_flush_lane_,I,J):
%assign J (J+1)
%endrep
        vmovdqa [r12 + _zuc_state + 32*I], xmm1 ; Save new state

        ; Next 4 lanes
        vmovdqu xmm0, [rsp + 32*I + 16] ; State before init
        vmovdqa xmm1, [r12 + _zuc_state + 32*I + 16] ; State after init
%assign J 4
%assign K 0
%rep 4
        test    word [r12 + _zuc_init_not_done], (1 << J)
        jnz     APPEND3(%%skip_flush_lane_,I,J)
        ;; Extract dword from ymm0
        vpextrd r15, xmm0, K ; value
        mov     r8, (K << 4) ; index

        XVPINSRD xmm1, xmm2, r13, r8, r15, no_scale

APPEND3(%%skip_flush_lane_,I,J):
%assign J (J+1)
%assign K (K+1)
%endrep
        vmovdqa [r12 + _zuc_state + 32*I + 16], xmm1 ; Save new state
%assign I (I + 1)
%endrep

%%skip_flush_restoring_state:
%ifdef SAFE_DATA
        ;; Clear stack containing state info
        vpxor   ymm0, ymm0
%assign I 0
%rep (16 + 2)
        vmovdqu [rsp + 32*I], ymm0
%assign I (I + 1)
%endrep
%endif
        add     rsp, (16*32 + 2*32) ; Restore stack pointer

        mov     word [r12 + _zuc_init_not_done], 0 ; Init done for all lanes

%%skip_flush_init:

        ;; Copy state from good lane to NULL lanes
%assign I 0
%rep (16 + 2)
        ; Read dword from good lane and broadcast to NULL lanes
        mov     r13d, [r12 + _zuc_state + 32*I + idx*4]

        ; First 4 lanes
        vmovdqa xmm1, [r12 + _zuc_state + 32*I] ; State after init
%assign J 0
%rep 4
        cmp     qword [r12 + _zuc_job_in_lane + J*8], 0
        jne     APPEND3(%%skip_eea3_copy_,I,J)
        mov     r8, (J << 4) ; index

        XVPINSRD xmm1, xmm2, r15, r8, r13, no_scale

APPEND3(%%skip_eea3_copy_,I,J):
%assign J (J+1)
%endrep
        vmovdqa [r12 + _zuc_state + 32*I], xmm1 ; Save new state

        ; Next 4 lanes
        vmovdqa xmm1, [r12 + _zuc_state + 32*I + 16] ; State after init
%assign J 4
%assign K 0
%rep 4
        cmp     qword [r12 + _zuc_job_in_lane + J*8], 0
        jne     APPEND3(%%skip_eea3_copy_,I,J)
        mov     r8, (K << 4) ; index

        XVPINSRD xmm1, xmm2, r15, r8, r13, no_scale

APPEND3(%%skip_eea3_copy_,I,J):
%assign J (J+1)
%assign K (K+1)
%endrep
        vmovdqa [r12 + _zuc_state + 32*I + 16], xmm1 ; Save new state
%assign I (I+1)
%endrep

        ;; If Windows, reserve memory in stack for parameter transferring
%ifndef LINUX
        ;; 40 bytes for 5 parameters
        sub     rsp, 40
%endif
        lea     arg1, [r12 + _zuc_state]
        lea     arg2, [r12 + _zuc_args_in]
        lea     arg3, [r12 + _zuc_args_out]
        lea     arg4, [r12 + _zuc_lens]
        mov     arg5, min_len

        call    asm_ZucCipher_8_avx2

%ifndef LINUX
        add     rsp, 40
%endif
        mov     state, [rsp + _gpr_save + 8*8]

        ; Clear ZUC state of the lane that is returned and NULL lanes
%ifdef SAFE_DATA
        SHIFT_GP        1, idx, tmp1, tmp2, left
        movzx   DWORD(tmp3), byte [state + _zuc_unused_lane_bitmask]
        or      tmp3, tmp1 ;; bitmask with NULL lanes and job to return

        CLEAR_ZUC_STATE state, tmp3, tmp2, tmp4
        jmp     %%skip_flush_clear_state
%endif

%%len_is_0_flush_eea3:
%ifdef SAFE_DATA
        ; Clear ZUC state of the lane that is returned
        mov     tmp2, idx
        CLEAR_ZUC_LANE_STATE state, tmp2, tmp3, ymm0, ymm1

%%skip_flush_clear_state:
%endif
        ; process completed job "idx"
        mov     job_rax, [state + _zuc_job_in_lane + idx*8]
        mov     unused_lanes, [state + _zuc_unused_lanes]
        mov     qword [state + _zuc_job_in_lane + idx*8], 0
        or      dword [job_rax + _status], IMB_STATUS_COMPLETED_CIPHER
        shl     unused_lanes, 4
        or      unused_lanes, idx
        mov     [state + _zuc_unused_lanes], unused_lanes

        SHIFT_GP        1, idx, tmp3, tmp4, left
        or      [state + _zuc_unused_lane_bitmask], BYTE(tmp3)
%%return_flush_eea3:

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

%%return_null_flush_eea3:
        xor     job_rax, job_rax
        jmp     %%return_flush_eea3
%endmacro

; JOB* SUBMIT_JOB_ZUC128_EEA3(MB_MGR_ZUC_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_ZUC128_EEA3,function,internal)
SUBMIT_JOB_ZUC128_EEA3:
        endbranch64
        SUBMIT_JOB_ZUC_EEA3 128

; JOB* SUBMIT_JOB_ZUC256_EEA3(MB_MGR_ZUC_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_ZUC256_EEA3,function,internal)
SUBMIT_JOB_ZUC256_EEA3:
        endbranch64
        SUBMIT_JOB_ZUC_EEA3 256

; JOB* FLUSH_JOB_ZUC128_EEA3(MB_MGR_ZUC_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(FLUSH_JOB_ZUC128_EEA3,function,internal)
FLUSH_JOB_ZUC128_EEA3:
        endbranch64
        FLUSH_JOB_ZUC_EEA3 128

; JOB* FLUSH_JOB_ZUC256_EEA3(MB_MGR_ZUC_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(FLUSH_JOB_ZUC256_EEA3,function,internal)
FLUSH_JOB_ZUC256_EEA3:
        endbranch64
        FLUSH_JOB_ZUC_EEA3 256

%macro SUBMIT_JOB_ZUC_EIA3 1
%define %%KEY_SIZE      %1 ; [constant] Key size (128 or 256)

; idx needs to be in rbp
%define len              rbp
%define idx              rbp
%define tmp              rbp
%define tmp2             r14
%define tmp3             r15

%define lane             r8
%define unused_lanes     rbx
%define len2             r13

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
        mov     [rsp + _gpr_save + 8*8], state
        mov     [rsp + _gpr_save + 8*9], job
        mov     [rsp + _rsp_save], rax  ; original SP

        mov     unused_lanes, [state + _zuc_unused_lanes]
        mov     lane, unused_lanes
        and	lane, 0xF           ;; just a nibble
        shr     unused_lanes, 4
        mov     tmp, [job + _zuc_eia3_iv]
        shl     lane, 5
%if %%KEY_SIZE == 128
        ; Read first 16 bytes of IV
        vmovdqu xmm0, [tmp]
        vmovdqa [state + _zuc_args_IV + lane], xmm0
%else ;; %%KEY_SIZE == 256
        ; Check if ZUC_EIA3._iv is not NULL, meaning a 25-byte IV can be parsed
        or      tmp, tmp
        jnz     %%_iv_size_25
%%_iv_size_23:
        ; Read 23 bytes of IV and expand to 25 bytes
        ; then expand the last 6 bytes to 8 bytes
        mov     tmp, [job + _zuc_eia3_iv23]
        ; Read and write first 16 bytes
        vmovdqu xmm0, [tmp]
        vmovdqa [state + _zuc_args_IV + lane], xmm0
        ; Read and write next byte
        mov     al, [tmp + 16]
        mov     [state + _zuc_args_IV + lane + 16], al
        ; Read next 6 bytes
        movzx   DWORD(tmp2), word [tmp + 17]
        mov     DWORD(tmp3), [tmp + 19]
        shl     tmp2, 32
        or      tmp2, tmp3
        ; Expand to 8 bytes and write
        mov     tmp3, 0x3f3f3f3f3f3f3f3f
        pdep    tmp2, tmp2, tmp3
        mov     [state + _zuc_args_IV + lane + 17], tmp2

        jmp     %%_iv_read
%%_iv_size_25:
        ; Read 25 bytes of IV
        vmovdqu xmm0, [tmp]
        vmovdqa [state + _zuc_args_IV + lane], xmm0
        vmovq   xmm0, [tmp + 16]
        vpinsrb xmm0, [tmp + 24], 8
        vmovdqa [state + _zuc_args_IV + lane + 16], xmm0
%%_iv_read:
%endif
        shr     lane, 5
        mov     [state + _zuc_unused_lanes], unused_lanes

        mov     [state + _zuc_job_in_lane + lane*8], job
        mov     tmp, [job + _src]
        add     tmp, [job + _hash_start_src_offset_in_bytes]
        mov     [state + _zuc_args_in + lane*8], tmp
        mov     tmp, [job + _zuc_eia3_key]
        mov     [state + _zuc_args_keys + lane*8], tmp
        mov     tmp, [job + _auth_tag_output]
        mov     [state + _zuc_args_out + lane*8], tmp

        ;; insert len into proper lane
        mov     len, [job + _msg_len_to_hash_in_bits]

        vmovdqa xmm0, [state + _zuc_lens]
        XVPINSRW xmm0, xmm1, tmp, lane, len, scale_x16
        vmovdqa [state + _zuc_lens], xmm0

        cmp     unused_lanes, 0xf
        jne     %%return_null_submit_eia3

        ; Find minimum length (searching for zero length,
        ; to retrieve already encrypted buffers)
        vphminposuw     xmm1, xmm0
        vpextrw len2, xmm1, 0   ; min value
        vpextrw idx, xmm1, 1    ; min index (0...7)
        cmp     len2, 0
        je      %%len_is_0_submit_eia3

        ; Move state into r11, as register for state will be used
        ; to pass parameter to next function
        mov     r11, state

        ;; If Windows, reserve memory in stack for parameter transferring
%ifndef LINUX
        ;; 48 bytes for 6 parameters (already aligned to 16 bytes)
        sub     rsp, 48
%endif
        lea     arg1, [r11 + _zuc_args_keys]
        lea     arg2, [r11 + _zuc_args_IV]
        lea     arg3, [r11 + _zuc_args_in]
        lea     arg4, [r11 + _zuc_args_out]
%ifdef LINUX
        lea     arg5, [r11 + _zuc_lens]
        lea     arg6, [r11 + _zuc_job_in_lane]
%else
        lea     r12, [r11 + _zuc_lens]
        mov     arg5, r12
        lea     r12, [r11 + _zuc_job_in_lane]
        mov     arg6, r12
%endif

%if %%KEY_SIZE == 128
        call    zuc_eia3_8_buffer_job_avx2
%else
        call    zuc256_eia3_8_buffer_job_avx2
%endif

%ifndef LINUX
        add     rsp, 48
%endif
        mov     state, [rsp + _gpr_save + 8*8]
        mov     job,   [rsp + _gpr_save + 8*9]

        ;; Clear all lengths (function will authenticate all buffers)
        vpxor   xmm0, xmm0
        vmovdqu [state + _zuc_lens], xmm0

%%len_is_0_submit_eia3:
        ; process completed job "idx"
        mov     job_rax, [state + _zuc_job_in_lane + idx*8]
        mov     unused_lanes, [state + _zuc_unused_lanes]
        mov     qword [state + _zuc_job_in_lane + idx*8], 0
        or      dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
        ;; TODO: fix double store (above setting the length to 0 and now setting to FFFFF)
        mov     word [state + _zuc_lens + idx*2], 0xFFFF
        shl     unused_lanes, 4
        or      unused_lanes, idx
        mov     [state + _zuc_unused_lanes], unused_lanes

%%return_submit_eia3:

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

%%return_null_submit_eia3:
        xor     job_rax, job_rax
        jmp     %%return_submit_eia3
%endmacro

%macro FLUSH_JOB_ZUC_EIA3 1
%define %%KEY_SIZE      %1 ; [constant] Key size (128 or 256)

%define unused_lanes     rbx
%define tmp1             rbx

%define tmp2             rax

; idx needs to be in rbp
%define tmp              rbp
%define idx              rbp

%define tmp3             r8
%define tmp4             r9
%define tmp5             r10

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
        mov     [rsp + _gpr_save + 8*8], state
        mov     [rsp + _rsp_save], rax  ; original SP

        ; check for empty
        mov     unused_lanes, [state + _zuc_unused_lanes]
        bt      unused_lanes, 32+3
        jc      %%return_null_flush_eia3

        ; Find minimum length (searching for zero length,
        ; to retrieve already authenticated buffers)
        vmovdqa xmm0, [state + _zuc_lens]
        vphminposuw     xmm1, xmm0
        vpextrw len2, xmm1, 0   ; min value
        vpextrw idx, xmm1, 1    ; min index (0...7)
        cmp     len2, 0
        je      %%len_is_0_flush_eia3

        ; copy good_lane to empty lanes
        mov     tmp1, [state + _zuc_args_in + idx*8]
        mov     tmp2, [state + _zuc_args_out + idx*8]
        mov     tmp3, [state + _zuc_args_keys + idx*8]
        mov     WORD(tmp5), [state + _zuc_lens + idx*2]

        ; Set valid length in NULL jobs
        vmovd   xmm0, DWORD(tmp5)
        vpshufb xmm0, xmm0, [rel broadcast_word]
        vmovdqa xmm1, [state + _zuc_lens]

        vpcmpeqw xmm2, xmm2 ;; Get all ff's in XMM register
        vpcmpeqw xmm3, xmm1, xmm2 ;; Mask with FFFF in NULL jobs
        vmovdqa [rsp + _null_len_save], xmm3 ;; Save lengths with FFFF in NULL jobs

        vpand   xmm4, xmm3, xmm0 ;; Length of valid job in all NULL jobs

        vpxor   xmm2, xmm3 ;; Mask with 0000 in NULL jobs
        vpand   xmm1, xmm2 ;; Zero out lengths of NULL jobs

        vpor    xmm1, xmm4
        vmovdqa [state + _zuc_lens], xmm1

%assign I 0
%rep 8
        cmp     qword [state + _zuc_job_in_lane + I*8], 0
        jne     APPEND(%%skip_eia3_,I)
        mov     [state + _zuc_args_in + I*8], tmp1
        mov     [state + _zuc_args_out + I*8], tmp2
        mov     [state + _zuc_args_keys + I*8], tmp3
APPEND(%%skip_eia3_,I):
%assign I (I+1)
%endrep

        ; Move state into r11, as register for state will be used
        ; to pass parameter to next function
        mov     r11, state

%ifndef LINUX
        ;; 48 bytes for 6 parameters (already aligned to 16 bytes)
        sub     rsp, 48
%endif
        lea     arg1, [r11 + _zuc_args_keys]
        lea     arg2, [r11 + _zuc_args_IV]
        lea     arg3, [r11 + _zuc_args_in]
        lea     arg4, [r11 + _zuc_args_out]
%ifdef LINUX
        lea     arg5, [r11 + _zuc_lens]
        lea     arg6, [r11 + _zuc_job_in_lane]
%else
        lea     r12, [r11 + _zuc_lens]
        mov     arg5, r12
        lea     r12, [r11 + _zuc_job_in_lane]
        mov     arg6, r12
%endif

%if %%KEY_SIZE == 128
        call    zuc_eia3_8_buffer_job_avx2
%else
        call    zuc256_eia3_8_buffer_job_avx2
%endif

%ifndef LINUX
        add     rsp, 48
%endif
        vmovdqa xmm2, [rsp + _null_len_save]

        mov     state, [rsp + _gpr_save + 8*8]

        ;; Clear all lengths of valid jobs and set to FFFF to NULL jobs
        vmovdqu [state + _zuc_lens], xmm2

%%len_is_0_flush_eia3:
        ; process completed job "idx"
        mov     job_rax, [state + _zuc_job_in_lane + idx*8]
        mov     unused_lanes, [state + _zuc_unused_lanes]
        mov     qword [state + _zuc_job_in_lane + idx*8], 0
        or      dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
        ;; TODO: fix double store (above setting the length to 0 and now setting to FFFFF)
        mov     word [state + _zuc_lens + idx*2], 0xFFFF
        shl     unused_lanes, 4
        or      unused_lanes, idx
        mov     [state + _zuc_unused_lanes], unused_lanes

%%return_flush_eia3:

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

%%return_null_flush_eia3:
        xor     job_rax, job_rax
        jmp     %%return_flush_eia3
%endmacro

; JOB* SUBMIT_JOB_ZUC128_EIA3(MB_MGR_ZUC_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_ZUC128_EIA3,function,internal)
SUBMIT_JOB_ZUC128_EIA3:
        endbranch64
        SUBMIT_JOB_ZUC_EIA3 128

; JOB* SUBMIT_JOB_ZUC256_EIA3(MB_MGR_ZUC_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_ZUC256_EIA3,function,internal)
SUBMIT_JOB_ZUC256_EIA3:
        endbranch64
        SUBMIT_JOB_ZUC_EIA3 256

; JOB* FLUSH_JOB_ZUC128_EIA3(MB_MGR_ZUC_OOO *state)
; arg 1 : state
MKGLOBAL(FLUSH_JOB_ZUC128_EIA3,function,internal)
FLUSH_JOB_ZUC128_EIA3:
        endbranch64
        FLUSH_JOB_ZUC_EIA3 128

; JOB* FLUSH_JOB_ZUC256_EIA3(MB_MGR_ZUC_OOO *state)
; arg 1 : state
MKGLOBAL(FLUSH_JOB_ZUC256_EIA3,function,internal)
FLUSH_JOB_ZUC256_EIA3:
        endbranch64
        FLUSH_JOB_ZUC_EIA3 256

mksection stack-noexec

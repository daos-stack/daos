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

%include "include/reg_sizes.asm"
%include "include/const.inc"
%include "include/cet.inc"
%ifndef SUBMIT_JOB_ZUC128_EEA3
%define SUBMIT_JOB_ZUC128_EEA3 submit_job_zuc_eea3_no_gfni_sse
%define FLUSH_JOB_ZUC128_EEA3 flush_job_zuc_eea3_no_gfni_sse
%define SUBMIT_JOB_ZUC256_EEA3 submit_job_zuc256_eea3_no_gfni_sse
%define FLUSH_JOB_ZUC256_EEA3 flush_job_zuc256_eea3_no_gfni_sse
%define SUBMIT_JOB_ZUC128_EIA3 submit_job_zuc_eia3_no_gfni_sse
%define FLUSH_JOB_ZUC128_EIA3 flush_job_zuc_eia3_no_gfni_sse
%define FLUSH_JOB_ZUC256_EIA3 flush_job_zuc256_eia3_no_gfni_sse
%define SUBMIT_JOB_ZUC256_EIA3 submit_job_zuc256_eia3_no_gfni_sse
%define ZUC_EIA3_4_BUFFER zuc_eia3_4_buffer_job_no_gfni_sse
%define ZUC256_EIA3_4_BUFFER zuc256_eia3_4_buffer_job_no_gfni_sse
%define ZUC128_INIT_4        asm_ZucInitialization_4_sse
%define ZUC256_INIT_4     asm_Zuc256Initialization_4_sse
%define ZUC_CIPHER_4      asm_ZucCipher_4_sse
%endif

mksection .rodata
default rel

align 16
broadcast_word:
db      0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01
db      0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01

align 16
all_ffs_top_64bits:
db      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
db      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF

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

select_bits_mask:
dq      0x3f
dq      0xfc0
dq      0x3f000
dq      0xfc0000
dq      0x3f000000
dq      0xfc0000000
dq      0x3f000000000
dq      0xfc0000000000

extern ZUC_EIA3_4_BUFFER
extern ZUC256_EIA3_4_BUFFER
extern ZUC128_INIT_4
extern ZUC256_INIT_4
extern ZUC_CIPHER_4

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
_state_save    resq     2*(16+2) ; Space for ZUC LFSR + R1-2
_gpr_save:      resq    10
_null_len_save: resq    1
_rsp_save:      resq    1
endstruc

mksection .text

%define APPEND(a,b) a %+ b
%define APPEND3(a,b,c) a %+ b %+ c

;; Clear state for multiple lanes in the OOO managers
%macro CLEAR_ZUC_STATE 5
%define %%STATE         %1 ;; [in] ZUC OOO manager pointer
%define %%LANE_MASK     %2 ;; [in/clobbered] bitmask with lanes to clear
%define %%TMP           %3 ;; [clobbered] Temporary GP register
%define %%XTMP1         %4 ;; [clobbered] Temporary XMM register
%define %%XTMP2         %5 ;; [clobbered] Temporary XMM register

        lea     %%TMP, [rel bitmask_to_dword_tab]
        shl     %%LANE_MASK, 4 ; Multiply by 16 to move through the table
        movdqa  %%XTMP1, [%%TMP + %%LANE_MASK]
        pxor    %%XTMP1, [rel clear_lane_mask_tab] ; NOT mask

        ;; Clear state for lanes
%assign I 0
%rep (16 + 6)
        movdqa  %%XTMP2, [%%STATE + _zuc_state + I*16]
        pand    %%XTMP2, %%XTMP1
        movdqa  [%%STATE + _zuc_state + I*16], %%XTMP2

%assign I (I + 1)
%endrep
%endmacro

;; Clear state for a specified lane in the OOO manager
%macro CLEAR_ZUC_LANE_STATE 5
%define %%STATE         %1 ;; [in] ZUC OOO manager pointer
%define %%LANE          %2 ;; [in/clobbered] lane index
%define %%TMP           %3 ;; [clobbered] Temporary GP register
%define %%XTMP1         %4 ;; [clobbered] Temporary YMM register
%define %%XTMP2         %5 ;; [clobbered] Temporary YMM register

        shl     %%LANE, 2
        lea     %%TMP, [rel clear_lane_mask_tab_start]
        sub     %%TMP, %%LANE
        movdqu  %%XTMP1, [%%TMP]
%assign I 0
%rep (16 + 6)
        movdqa  %%XTMP2, [%%STATE + _zuc_state + I*16]
        pand    %%XTMP2, %%XTMP1
        movdqa  [%%STATE + _zuc_state + I*16], %%XTMP2
%assign I (I + 1)
%endrep

%endmacro

;; Read 8x6 bits and store them as 8 partial bytes
;; (using 6 least significant bits)
%macro EXPAND_FROM_6_TO_8_BYTES 3
%define %%IN_OUT        %1 ; [in/out] Input/output GP register
%define %%INPUT_COPY    %2 ; [clobbered] Temporary GP register
%define %%TMP           %3 ; [clobbered] Temporary GP register

        mov     %%INPUT_COPY, %%IN_OUT
        mov     %%TMP, %%INPUT_COPY
        ; Read bits [5:0] from input
        and     %%TMP, [rel select_bits_mask]
        ; Store bits [7:0] in output
        mov     %%IN_OUT, %%TMP

%assign %%i 1
%rep 7
        mov     %%TMP, %%INPUT_COPY
        ; Read bits [6*i + 5 : 6*i] from input
        and     %%TMP, [rel select_bits_mask + 8*%%i]
        ; Store bits [8*i + 7 : 8*i] in output
        shl     %%TMP, (2*%%i)
        or      %%IN_OUT, %%TMP
%assign %%i (%%i + 1)
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
        movzx   lane, BYTE(unused_lanes)
        shr     unused_lanes, 8
        mov     tmp, [job + _iv]
        shl     lane, 5
%if %%KEY_SIZE == 128
        ; Read first 16 bytes of IV
        movdqu  xmm0, [tmp]
        movdqa  [state + _zuc_args_IV + lane], xmm0
%else ;; %%KEY_SIZE == 256
        cmp     qword [job + _iv_len_in_bytes], 25
        je      %%_iv_size_25
%%_iv_size_23:
        ; Read 23 bytes of IV and expand to 25 bytes
        ; then expand the last 6 bytes to 8 bytes

        ; Read and write first 16 bytes
        movdqu  xmm0, [tmp]
        movdqa  [state + _zuc_args_IV + lane], xmm0
        ; Read and write next byte
        mov     al, [tmp + 16]
        mov     [state + _zuc_args_IV + lane + 16], al
        ; Read next 6 bytes and write as 8 bytes
        movzx   DWORD(tmp2), word [tmp + 17]
        mov     DWORD(tmp3), [tmp + 19]
        shl     tmp2, 32
        or      tmp2, tmp3
        EXPAND_FROM_6_TO_8_BYTES tmp2, tmp, tmp3
        mov     [state + _zuc_args_IV + lane + 17], tmp2

        jmp     %%_iv_read
%%_iv_size_25:
        ; Read 25 bytes of IV
        movdqu  xmm0, [tmp]
        movdqa  [state + _zuc_args_IV + lane], xmm0
        movq    xmm0, [tmp + 16]
        pinsrb  xmm0, [tmp + 24], 8
        movdqa  [state + _zuc_args_IV + lane + 16], xmm0
%%_iv_read:
%endif
        shr     lane, 5
        mov     [state + _zuc_unused_lanes], unused_lanes

        mov     [state + _zuc_job_in_lane + lane*8], job
        ; New job that needs init (update bit in zuc_init_not_done bitmask)
        SHIFT_GP        1, lane, tmp, tmp2, left
        or      [state + _zuc_init_not_done], BYTE(tmp)
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

        movq    xmm0, [state + _zuc_lens]
        XPINSRW xmm0, xmm1, tmp, lane, len, scale_x16
        movq    [state + _zuc_lens], xmm0

        cmp     unused_lanes, 0xff
        jne     %%return_null_submit_eea3

        ; Set all ffs in top 64 bits to invalid them
        por     xmm0, [rel all_ffs_top_64bits]

        ; Find minimum length (searching for zero length,
        ; to retrieve already encrypted buffers)
        phminposuw      xmm1, xmm0
        pextrw  min_len, xmm1, 0   ; min value
        pextrw  idx, xmm1, 1    ; min index (0...3)
        cmp     min_len, 0
        je      %%len_is_0_submit_eea3

        ; Move state into r12, as register for state will be used
        ; to pass parameter to next function
        mov     r12, state

%assign I 0
%rep (16 + 2)
        movdqa  xmm0, [r12 + _zuc_state + 16*I]
        movdqa  [rsp + _state_save + 16*I], xmm0
%assign I (I + 1)
%endrep

        ;; If Windows, reserve memory in stack for parameter transferring
%ifndef LINUX
        ;; 24 bytes for 3 parameters
        sub     rsp, 24
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
        call    ZUC128_INIT_4
%else
        call    ZUC256_INIT_4
%endif

%ifndef LINUX
        add     rsp, 24
%endif

        cmp     byte [r12 + _zuc_init_not_done], 0x0f ; Init done for all lanes
        je      %%skip_submit_restoring_state

        ;; Load mask containing FF's in lanes which init has just been done
        movzx   DWORD(tmp3), byte [r12 + _zuc_init_not_done]
        lea     tmp2, [rel bitmask_to_dword_tab]
        shl     tmp3, 4 ; Multiply by 16 to move through the table
        movdqa  xmm2, [tmp3 + tmp2]

        ;; Restore state from stack for lanes that did not need init
%assign I 0
%rep (16 + 2)
        movdqa  xmm0, [rsp + _state_save + 16*I] ; State before init
        movdqa  xmm1, [r12 + _zuc_state + 16*I] ; State after init

        movdqa  xmm3, xmm2
        ; Zero out lanes that need to be restored in current state
        pand    xmm1, xmm3
        ; Zero out lanes that do not need to be restored in saved state
        pandn   xmm3, xmm0
        por     xmm1, xmm3

        movdqa  [r12 + _zuc_state + 16*I], xmm1 ; Save new state

%assign I (I + 1)
%endrep

%%skip_submit_restoring_state:
%ifdef SAFE_DATA
        ;; Clear stack containing state info
        pxor    xmm0, xmm0
%assign I 0
%rep (16 + 2)
        movdqa  [rsp + _state_save + 16*I], xmm0
%assign I (I + 1)
%endrep
%endif
        mov     byte [r12 + _zuc_init_not_done], 0 ; Init done for all lanes

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

        call    ZUC_CIPHER_4

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
        shl     unused_lanes, 8
        or      unused_lanes, idx
        mov     [state + _zuc_unused_lanes], unused_lanes
        SHIFT_GP        1, idx, tmp, tmp2, left
        or      [state + _zuc_unused_lane_bitmask], BYTE(tmp)

%ifdef SAFE_DATA
        ; Clear ZUC state of the lane that is returned
        CLEAR_ZUC_LANE_STATE state, idx, tmp, xmm0, xmm1
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
        bt      unused_lanes, 32+7
        jc      %%return_null_flush_eea3

        ; Set length = 0xFFFF in NULL jobs
        movq    xmm0, [state + _zuc_lens]
        mov     DWORD(tmp3), 0xffff
%assign I 0
%rep 4
        cmp     qword [state + _zuc_job_in_lane + I*8], 0
        jne     APPEND(%%skip_copy_ffs_,I)
        pinsrw  xmm0, DWORD(tmp3), I
APPEND(%%skip_copy_ffs_,I):
%assign I (I+1)
%endrep

        movq    [state + _zuc_lens], xmm0

        ; Set all ffs in top 64 bits to invalid them
        por     xmm0, [rel all_ffs_top_64bits]

        ; Find minimum length (searching for zero length,
        ; to retrieve already encrypted buffers)
        phminposuw     xmm1, xmm0
        pextrw  min_len, xmm1, 0   ; min value
        pextrw  idx, xmm1, 1    ; min index (0...3)
        cmp     min_len, 0
        je      %%len_is_0_flush_eea3

        ; copy good_lane to empty lanes
        mov     tmp1, [state + _zuc_args_in + idx*8]
        mov     tmp2, [state + _zuc_args_out + idx*8]
        mov     tmp3, [state + _zuc_args_keys + idx*8]

%assign I 0
%rep 4
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

%assign I 0
%rep (16 + 2)
        movdqa  xmm0, [r12 + _zuc_state + 16*I]
        movdqa  [rsp + _state_save + 16*I], xmm0
%assign I (I + 1)
%endrep

        ;; If Windows, reserve memory in stack for parameter transferring
%ifndef LINUX
        ;; 24 bytes for 3 parameters
        sub     rsp, 24
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
        call    ZUC128_INIT_4
%else
        call    ZUC256_INIT_4
%endif

%ifndef LINUX
        add     rsp, 24
%endif
        cmp     word [r12 + _zuc_init_not_done], 0x0f ; Init done for all lanes
        je      %%skip_flush_restoring_state

        ;; Load mask containing FF's in lanes which init has just been done
        movzx   DWORD(tmp3), byte [r12 + _zuc_init_not_done]
        lea     tmp2, [rel bitmask_to_dword_tab]
        shl     tmp3, 4 ; Multiply by 16 to move through the table
        movdqa  xmm2, [tmp3 + tmp2]

        ;; Restore state from stack for lanes that did not need init
%assign I 0
%rep (16 + 2)
        movdqa  xmm0, [rsp + _state_save + 16*I] ; State before init
        movdqa  xmm1, [r12 + _zuc_state + 16*I] ; State after init

        movdqa  xmm3, xmm2
        ; Zero out lanes that need to be restored in current state
        pand    xmm1, xmm3
        ; Zero out lanes that do not need to be restored in saved state
        pandn   xmm3, xmm0
        por     xmm1, xmm3

        movdqa  [r12 + _zuc_state + 16*I], xmm1 ; Save new state
%assign I (I + 1)
%endrep

%%skip_flush_restoring_state:
%ifdef SAFE_DATA
        ;; Clear stack containing state info
        pxor    xmm0, xmm0
%assign I 0
%rep (16 + 2)
        movdqa  [rsp + _state_save + 16*I], xmm0
%assign I (I + 1)
%endrep
%endif
        mov     word [r12 + _zuc_init_not_done], 0 ; Init done for all lanes

%%skip_flush_init:

        ;; Copy state from good lane to NULL lanes
%assign I 0
%rep (16 + 2)
        ; Read dword from good lane and broadcast to NULL lanes
        mov     r13d, [r12 + _zuc_state + 16*I + idx*4]
        movdqa  xmm1, [r12 + _zuc_state + 16*I] ; State after init
%assign J 0
%rep 4
        cmp     qword [r12 + _zuc_job_in_lane + J*8], 0
        jne     APPEND3(%%skip_eea3_copy_,I,J)
        pinsrd  xmm1, r13d, J

APPEND3(%%skip_eea3_copy_,I,J):
%assign J (J+1)
%endrep
        movdqa  [r12 + _zuc_state + 16*I], xmm1 ; Save new state
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

        call    ZUC_CIPHER_4

%ifndef LINUX
        add     rsp, 40
%endif
        mov     state, [rsp + _gpr_save + 8*8]

        ; Clear ZUC state of the lane that is returned and NULL lanes
%ifdef SAFE_DATA
        SHIFT_GP        1, idx, tmp1, tmp2, left
        movzx   DWORD(tmp3), byte [state + _zuc_unused_lane_bitmask]
        or      tmp3, tmp1 ;; bitmask with NULL lanes and job to return

        CLEAR_ZUC_STATE state, tmp3, tmp2, xmm0, xmm1
        jmp     %%skip_flush_clear_state
%endif

%%len_is_0_flush_eea3:
%ifdef SAFE_DATA
        ; Clear ZUC state of the lane that is returned
        mov     tmp2, idx
        CLEAR_ZUC_LANE_STATE state, tmp2, tmp3, xmm0, xmm1

%%skip_flush_clear_state:
%endif
        ; process completed job "idx"
        mov     job_rax, [state + _zuc_job_in_lane + idx*8]
        mov     unused_lanes, [state + _zuc_unused_lanes]
        mov     qword [state + _zuc_job_in_lane + idx*8], 0
        or      dword [job_rax + _status], IMB_STATUS_COMPLETED_CIPHER
        shl     unused_lanes, 8
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

; JOB* FLUSH_JOB_ZUC128_EEA3(MB_MGR_ZUC_OOO *state)
; arg 1 : state
MKGLOBAL(FLUSH_JOB_ZUC128_EEA3,function,internal)
FLUSH_JOB_ZUC128_EEA3:
        endbranch64
        FLUSH_JOB_ZUC_EEA3 128

; JOB* FLUSH_JOB_ZUC256_EEA3(MB_MGR_ZUC_OOO *state)
; arg 1 : state
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
        movzx   lane, BYTE(unused_lanes)
        shr     unused_lanes, 8
        mov     tmp, [job + _zuc_eia3_iv]
        shl     lane, 5
%if %%KEY_SIZE == 128
        ; Read first 16 bytes of IV
        movdqu  xmm0, [tmp]
        movdqa  [state + _zuc_args_IV + lane], xmm0
%else ;; %%KEY_SIZE == 256
        or      tmp, tmp
        jnz     %%_iv_size_25
%%_iv_size_23:
        ; Read 23 bytes of IV and expand to 25 bytes
        ; then expand the last 6 bytes to 8 bytes
        mov     tmp, [job + _zuc_eia3_iv23]
        ; Read and write first 16 bytes
        movdqu  xmm0, [tmp]
        movdqa  [state + _zuc_args_IV + lane], xmm0
        ; Read and write next byte
        mov     al, [tmp + 16]
        mov     [state + _zuc_args_IV + lane + 16], al
        ; Read next 6 bytes and write as 8 bytes
        movzx   DWORD(tmp2), word [tmp + 17]
        mov     DWORD(tmp3), [tmp + 19]
        shl     tmp2, 32
        or      tmp2, tmp3
        EXPAND_FROM_6_TO_8_BYTES tmp2, tmp, tmp3
        mov     [state + _zuc_args_IV + lane + 17], tmp2

        jmp     %%_iv_read
%%_iv_size_25:
        ; Read 25 bytes of IV
        movdqu  xmm0, [tmp]
        movdqa  [state + _zuc_args_IV + lane], xmm0
        movq    xmm0, [tmp + 16]
        pinsrb  xmm0, [tmp + 24], 8
        movdqa  [state + _zuc_args_IV + lane + 16], xmm0
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

        movdqa  xmm0, [state + _zuc_lens]
        XPINSRW xmm0, xmm1, tmp, lane, len, scale_x16
        movdqa  [state + _zuc_lens], xmm0

        cmp     unused_lanes, 0xff
        jne     %%return_null_submit_eia3

        ; Find minimum length (searching for zero length,
        ; to retrieve already encrypted buffers)
        phminposuw      xmm1, xmm0
        pextrw  len2, xmm1, 0   ; min value
        pextrw  idx, xmm1, 1    ; min index (0...3)
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
        call    ZUC_EIA3_4_BUFFER
%else
        call    ZUC256_EIA3_4_BUFFER
%endif

%ifndef LINUX
        add     rsp, 48
%endif
        mov     state, [rsp + _gpr_save + 8*8]
        mov     job,   [rsp + _gpr_save + 8*9]

        ;; Clear all lengths (function will authenticate all buffers)
        mov     qword [state + _zuc_lens], 0

%%len_is_0_submit_eia3:
        ; process completed job "idx"
        mov     job_rax, [state + _zuc_job_in_lane + idx*8]
        mov     unused_lanes, [state + _zuc_unused_lanes]
        mov     qword [state + _zuc_job_in_lane + idx*8], 0
        or      dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
        ;; TODO: fix double store (above setting the length to 0 and now setting to FFFFF)
        mov     word [state + _zuc_lens + idx*2], 0xFFFF
        shl     unused_lanes, 8
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
        bt      unused_lanes, 32+7
        jc      %%return_null_flush_eia3

        ; Find minimum length (searching for zero length,
        ; to retrieve already authenticated buffers)
        movdqa  xmm0, [state + _zuc_lens]
        phminposuw     xmm1, xmm0
        pextrw  len2, xmm1, 0   ; min value
        pextrw  idx, xmm1, 1    ; min index (0...3)
        cmp     len2, 0
        je      %%len_is_0_flush_eia3

        ; copy good_lane to empty lanes
        mov     tmp1, [state + _zuc_args_in + idx*8]
        mov     tmp2, [state + _zuc_args_out + idx*8]
        mov     tmp3, [state + _zuc_args_keys + idx*8]
        mov     WORD(tmp5), [state + _zuc_lens + idx*2]

        ; Set valid length in NULL jobs
        movd    xmm0, DWORD(tmp5)
        pshufb  xmm0, [rel broadcast_word]
        movdqa  xmm1, [state + _zuc_lens]
        movdqa	xmm2, xmm1

        pcmpeqw xmm3, xmm3 ;; Get all ff's in XMM register
        pcmpeqw xmm1, xmm3 ;; Mask with FFFF in NULL jobs
        movq	tmp5, xmm1
        mov     [rsp + _null_len_save], tmp5 ;; Save lengths with FFFF in NULL jobs

        pand    xmm0, xmm1 ;; Length of valid job in all NULL jobs

        pxor    xmm3, xmm1 ;; Mask with 0000 in NULL jobs
        pand    xmm2, xmm3 ;; Zero out lengths of NULL jobs

        por     xmm2, xmm0
        movq    tmp5, xmm2
        mov     [state + _zuc_lens], tmp5

%assign I 0
%rep 4
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
        call    ZUC_EIA3_4_BUFFER
%else
        call    ZUC256_EIA3_4_BUFFER
%endif

%ifndef LINUX
        add     rsp, 48
%endif

        mov	tmp5, [rsp + _null_len_save]
        mov     state, [rsp + _gpr_save + 8*8]

        ;; Clear all lengths of valid jobs and set to FFFF to NULL jobs
        mov     qword [state + _zuc_lens], tmp5

%%len_is_0_flush_eia3:
        ; process completed job "idx"
        mov     job_rax, [state + _zuc_job_in_lane + idx*8]
        mov     unused_lanes, [state + _zuc_unused_lanes]
        mov     qword [state + _zuc_job_in_lane + idx*8], 0
        or      dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH
        ;; TODO: fix double store (above setting the length to 0 and now setting to FFFFF)
        mov     word [state + _zuc_lens + idx*2], 0xFFFF
        shl     unused_lanes, 8
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

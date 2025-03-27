;;
;; Copyright (c) 2017-2021, Intel Corporation
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

;; In System V AMD64 ABI
;;	callee saves: RBX, RBP, R12-R15
;; Windows x64 ABI
;;	callee saves: RBX, RBP, RDI, RSI, RSP, R12-R15

;;
;; Registers:		RAX RBX RCX RDX RBP RSI RDI R8  R9  R10 R11 R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Windows clobbers:	RAX                         R8  R9  R10 R11
;; Windows preserves:	    RBX RCX RDX RBP RSI RDI                 R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Linux clobbers:	RAX     RCX RDX                     R10 R11
;; Linux preserves:	    RBX         RBP RSI RDI R8  R9          R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Clobbers ZMM0-31, K1-7 (K1-2 and K4-6 here but DES underneath clobbers K1-7).

%include "include/os.asm"
%include "include/reg_sizes.asm"
%include "include/imb_job.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/constants.asm"
;%define DO_DBGPRINT
%include "include/dbgprint.asm"
%include "include/const.inc"
%include "include/cet.inc"
extern docsis_des_x16_enc_avx512
extern docsis_des_x16_dec_avx512
extern des_x16_cbc_enc_avx512
extern des_x16_cbc_dec_avx512
extern des3_x16_cbc_enc_avx512
extern des3_x16_cbc_dec_avx512

%ifdef LINUX
%define arg1	rdi
%define arg2	rsi
%define arg3	rdx
%define arg4	rcx
%else
%define arg1	rcx
%define arg2	rdx
%define arg3	r8
%define arg4	r9
%endif

%define STATE	arg1
%define JOB	arg2

%define IA0     arg3
%define IA1     arg4
%define IA2     r10

%define MIN_IDX	r11
%define MIN_LEN rax
%define LANE	r11

%define AVX512_NUM_DES_LANES 16

%define ZTMP0   zmm0
%define ZTMP1   zmm1
%define ZTMP2   zmm2
%define ZTMP3   zmm3
%define ZTMP4   zmm4
%define ZTMP5   zmm5
%define ZTMP6   zmm6
%define ZTMP7   zmm7
%define ZTMP8   zmm8
%define ZTMP9   zmm9

;;; ===========================================================================
;;; ===========================================================================
;;; MACROS
;;; ===========================================================================
;;; ===========================================================================

;;; ===========================================================================
;;; DES/DOCSIS DES job submit
;;; ===========================================================================
;;; DES_DOCSIS [in] - DES, DOCSIS or 3DES cipher selection
;;; ENC_DEC    [in] - ENCrypt or DECrypt seection
%macro GENERIC_DES_SUBMIT 2
%define %%DES_DOCSIS %1
%define %%ENC_DEC %2

        ;; get unused lane and increment number of lanes in use
        mov	IA0, [STATE + _des_unused_lanes]
        mov     LANE, IA0
        and	LANE, 0xF           ;; just a nibble
        shr	IA0, 4
        mov	[STATE + _des_unused_lanes], IA0
	add	qword [STATE + _des_lanes_in_use], 1

        ;; store job info in OOO structure
        ;; - job pointer
        mov     [STATE + _des_job_in_lane + LANE*8], JOB
        ;; - key schedule
%ifidn %%ENC_DEC, ENC
        mov     IA2, [JOB + _enc_keys]
%else
        mov     IA2, [JOB + _dec_keys]
%endif
        mov     [STATE + _des_args_keys + LANE*8], IA2
        ;; - IV
        mov     IA2, [JOB + _iv]
        mov     DWORD(IA0), [IA2]
        mov     DWORD(IA1), [IA2 + 4]
        mov     [STATE + _des_args_IV + LANE*4], DWORD(IA0)
        mov     [STATE + _des_args_IV + LANE*4 + (AVX512_NUM_DES_LANES*4)], DWORD(IA1)
        ;; - src pointer
        mov     IA0, [JOB + _src]
        add     IA0, [JOB + _cipher_start_src_offset_in_bytes]
        mov     [STATE + _des_args_in + LANE*8], IA0
        ;; - destination pointer
        mov     IA1, [JOB + _dst]
        mov     [STATE + _des_args_out + LANE*8], IA1
        ;; - length in bytes (block aligned)
        mov     IA2, [JOB + _msg_len_to_cipher_in_bytes]
        and     IA2, -8
        VPINSRW_M256 STATE + _des_lens, XWORD(ZTMP0), XWORD(ZTMP1), MIN_LEN, LANE, IA2, scale_x16

%ifidn %%DES_DOCSIS, DOCSIS
        ;; - block length
        mov     [STATE + _des_args_BLen + LANE*4], DWORD(IA2)
        ;; - last in
        add     IA0, IA2
        mov     [STATE + _des_args_LIn + LANE*8], IA0
        ;; - last out
        add     IA1, IA2
        mov     [STATE + _des_args_LOut + LANE*8], IA1
        ;; - partial length
        mov     IA2, [JOB + _msg_len_to_cipher_in_bytes]
        and     IA2, 7
        mov     [STATE + _des_args_PLen + LANE*4], DWORD(IA2)
%endif                          ; DOCSIS
        ;; is there enough jobs to process them in parallel?
        cmp     qword [STATE + _des_lanes_in_use], AVX512_NUM_DES_LANES
        jb      %%_des_submit_null_end
        ;; schedule the processing
        ;; - find min job size
        vmovdqa         XWORD(ZTMP0), [STATE + _des_lens + 2*0]
        vphminposuw	XWORD(ZTMP2), XWORD(ZTMP0)
        vpextrw         DWORD(MIN_LEN), XWORD(ZTMP2), 0   ; min value
        vpextrw         DWORD(MIN_IDX), XWORD(ZTMP2), 1   ; min index
        vmovdqa         XWORD(ZTMP1), [STATE + _des_lens + 2*8]
        vphminposuw	XWORD(ZTMP2), XWORD(ZTMP1)
        vpextrw         DWORD(IA2), XWORD(ZTMP2), 0       ; min value
        cmp             DWORD(MIN_LEN), DWORD(IA2)
        jle             %%_use_min
        vpextrw         DWORD(MIN_IDX), XWORD(ZTMP2), 1   ; min index
        add             DWORD(MIN_IDX), 8               ; but index +8
        mov             MIN_LEN, IA2                    ; min len
%%_use_min:
        cmp             MIN_LEN, 0
        je              %%_len_is_0

        vpbroadcastw    XWORD(ZTMP3), WORD(MIN_LEN)
        vpsubw          XWORD(ZTMP0), XWORD(ZTMP0), XWORD(ZTMP3)
        vmovdqa         [STATE + _des_lens + 2*0], XWORD(ZTMP0)
        vpsubw          XWORD(ZTMP1), XWORD(ZTMP1), XWORD(ZTMP3)
        vmovdqa         [STATE + _des_lens + 2*8], XWORD(ZTMP1)

        push            MIN_IDX
        mov             arg2, MIN_LEN
%ifidn %%ENC_DEC, ENC
        ;; encrypt
%ifidn %%DES_DOCSIS, DOCSIS
        call            docsis_des_x16_enc_avx512
%endif
%ifidn %%DES_DOCSIS, DES
        call            des_x16_cbc_enc_avx512
%endif
%ifidn %%DES_DOCSIS, 3DES
        call            des3_x16_cbc_enc_avx512
%endif
%else                           ; ENC
        ;; decrypt
%ifidn %%DES_DOCSIS, DOCSIS
        call            docsis_des_x16_dec_avx512
%endif
%ifidn %%DES_DOCSIS, DES
        call            des_x16_cbc_dec_avx512
%endif
%ifidn %%DES_DOCSIS, 3DES
        call            des3_x16_cbc_dec_avx512
%endif
%endif                          ; DEC
        pop             MIN_IDX
        jmp             %%_des_submit_end

%%_des_submit_null_end:
        xor     rax, rax
        jmp     %%_des_submit_return

%%_len_is_0:
%ifidn %%DES_DOCSIS, DOCSIS
        cmp             dword [STATE + _des_args_PLen + MIN_IDX*4], 0
        jz              %%_des_submit_end
        push            MIN_IDX
        xor             arg2, arg2 ; len is 0
%ifidn %%ENC_DEC, ENC
        call            docsis_des_x16_enc_avx512
%else                           ; ENC
        call            docsis_des_x16_dec_avx512
%endif                          ; DEC
        pop            MIN_IDX
%endif                          ; DOCSIS
        ;; fall through
%%_des_submit_end:
        ;; return a job
        ;; - decrement number of jobs in use
	sub	qword [STATE + _des_lanes_in_use], 1
        ;; - put the lane back to free lanes pool
        mov	IA0, [STATE + _des_unused_lanes]
        shl	IA0, 4
        or      IA0, MIN_IDX
        mov	[STATE + _des_unused_lanes], IA0
        ;; - mark job as complete
        ;; - clear job pointer
        mov     rax, [STATE + _des_job_in_lane + MIN_IDX*8]
        mov     qword [STATE + _des_job_in_lane + MIN_IDX*8], 0
        or	dword [rax + _status], IMB_STATUS_COMPLETED_CIPHER

%ifdef SAFE_DATA
        ;; Clear IV
        mov     dword [STATE + _des_args_IV + MIN_IDX*4], 0
        mov     dword [STATE + _des_args_IV + MIN_IDX*4 + (AVX512_NUM_DES_LANES*4)], 0
%endif
        vzeroupper
%%_des_submit_return:
%endmacro

;;; ===========================================================================
;;; DES/DOCSIS DES flush
;;; ===========================================================================
;;; DES_DOCSIS [in] - DES, DOCSIS or 3DES cipher selection
;;; ENC_DEC    [in] - ENCrypt or DECrypt selection
;;;
;;; Clobbers k1, k2, k4, k5 and k6
%macro GENERIC_DES_FLUSH 2
%define %%DES_DOCSIS %1
%define %%ENC_DEC %2

        cmp     qword [STATE + _des_lanes_in_use], 0
        je      %%_des_flush_null_end

        ;; find non-null job
        vpxord          ZTMP0, ZTMP0, ZTMP0
        vmovdqu64       ZTMP1, [STATE + _des_job_in_lane + (0*PTR_SZ)]
        vmovdqu64       ZTMP2, [STATE + _des_job_in_lane + (8*PTR_SZ)]
        vpcmpq          k1, ZTMP1, ZTMP0, 4 ; NEQ
        vpcmpq          k2, ZTMP2, ZTMP0, 4 ; NEQ
        xor             IA0, IA0
        xor             IA1, IA1
        kmovw           DWORD(IA0), k1
        kmovw           DWORD(IA1), k2
        mov             DWORD(IA2), DWORD(IA1)
        shl             DWORD(IA2), 8
        or              DWORD(IA2), DWORD(IA0) ; mask of non-null jobs in IA2
        not             BYTE(IA0)
        kmovw           k4, DWORD(IA0)
        not             BYTE(IA1)
        kmovw           k5, DWORD(IA1)
        mov             DWORD(IA0), DWORD(IA2)
        not             WORD(IA0)
        kmovw           k6, DWORD(IA0)         ; mask of NULL jobs in k4, k5 and k6
        mov             DWORD(IA0), DWORD(IA2)
        xor             IA2, IA2
        bsf             WORD(IA2), WORD(IA0)   ; index of the 1st set bit in IA2

        ;; copy good lane data into NULL lanes
        ;; - k1(L8)/k2(H8)    - masks of non-null jobs
        ;; - k4(L8)/k5(H8)/k6 - masks of NULL jobs
        ;; - IA2 index of 1st non-null job

        ;; - in pointer
        mov             IA0, [STATE + _des_args_in + IA2*8]
        vpbroadcastq    ZTMP1, IA0
        vmovdqu64       [STATE + _des_args_in + (0*PTR_SZ)]{k4}, ZTMP1
        vmovdqu64       [STATE + _des_args_in + (8*PTR_SZ)]{k5}, ZTMP1
        ;; - out pointer
        mov             IA0, [STATE + _des_args_out + IA2*8]
        vpbroadcastq    ZTMP1, IA0
        vmovdqu64       [STATE + _des_args_out + (0*PTR_SZ)]{k4}, ZTMP1
        vmovdqu64       [STATE + _des_args_out + (8*PTR_SZ)]{k5}, ZTMP1
        ;; - key schedule
        mov             IA0, [STATE + _des_args_keys + IA2*8]
        vpbroadcastq    ZTMP1, IA0
        vmovdqu64       [STATE + _des_args_keys + (0*PTR_SZ)]{k4}, ZTMP1
        vmovdqu64       [STATE + _des_args_keys + (8*PTR_SZ)]{k5}, ZTMP1
        ;; - zero partial len
        vmovdqu32       [STATE + _des_args_PLen]{k6}, ZTMP0
        ;; - set len to UINT16_MAX
        mov             WORD(IA0), 0xffff
        vpbroadcastw    ZTMP1, WORD(IA0)
        vmovdqu16       [STATE + _des_lens]{k6}, ZTMP1

        ;; - IV
        mov             DWORD(IA0), [STATE + _des_args_IV + IA2*4]
        mov             DWORD(IA1), [STATE + _des_args_IV + IA2*4 + (16*4)]
        vpbroadcastd    ZTMP1, DWORD(IA0)
        vpbroadcastd    ZTMP2, DWORD(IA1)
        vmovdqu32       [STATE + _des_args_IV]{k6}, ZTMP1
        vmovdqu32       [STATE + _des_args_IV + (16*4)]{k6}, ZTMP2

        ;; schedule the processing
        ;; - find min job size
        vmovdqa         XWORD(ZTMP0), [STATE + _des_lens + 2*0]
        vphminposuw	XWORD(ZTMP2), XWORD(ZTMP0)
        vpextrw         DWORD(MIN_LEN), XWORD(ZTMP2), 0   ; min value
        vpextrw         DWORD(MIN_IDX), XWORD(ZTMP2), 1   ; min index
        vmovdqa         XWORD(ZTMP1), [STATE + _des_lens + 2*8]
        vphminposuw	XWORD(ZTMP2), XWORD(ZTMP1)
        vpextrw         DWORD(IA2), XWORD(ZTMP2), 0       ; min value
        cmp             DWORD(MIN_LEN), DWORD(IA2)
        jle             %%_use_min
        vpextrw         DWORD(MIN_IDX), XWORD(ZTMP2), 1   ; min index
        add             DWORD(MIN_IDX), 8               ; but index +8
        mov             MIN_LEN, IA2                    ; min len
%%_use_min:
        vpbroadcastw    XWORD(ZTMP3), WORD(MIN_LEN)
        vpsubw          XWORD(ZTMP0), XWORD(ZTMP0), XWORD(ZTMP3)
        vmovdqa         [STATE + _des_lens + 2*0], XWORD(ZTMP0)
        vpsubw          XWORD(ZTMP1), XWORD(ZTMP1), XWORD(ZTMP3)
        vmovdqa         [STATE + _des_lens + 2*8], XWORD(ZTMP1)

        push            MIN_IDX
%ifdef SAFE_DATA
        ;; Save k6, which may be clobbered by following functions
        kmovq           IA0, k6
        push            IA0
%endif

        mov             arg2, MIN_LEN
%ifidn %%ENC_DEC, ENC
        ;; encrypt
%ifidn %%DES_DOCSIS, DOCSIS
        call            docsis_des_x16_enc_avx512
%endif
%ifidn %%DES_DOCSIS, DES
        call            des_x16_cbc_enc_avx512
%endif
%ifidn %%DES_DOCSIS, 3DES
        call            des3_x16_cbc_enc_avx512
%endif
%else                           ; ENC
        ;; decrypt
%ifidn %%DES_DOCSIS, DOCSIS
        call            docsis_des_x16_dec_avx512
%endif
%ifidn %%DES_DOCSIS, DES
        call            des_x16_cbc_dec_avx512
%endif
%ifidn %%DES_DOCSIS, 3DES
        call            des3_x16_cbc_dec_avx512
%endif
%endif                          ; DEC
%ifdef SAFE_DATA
        ;; Restore k6, which may have been clobbered by previous functions
        pop             IA0
        kmovq           k6, IA0
%endif
        pop             MIN_IDX
        jmp             %%_des_flush_end

%%_des_flush_null_end:
        xor     rax, rax
        jmp     %%_des_flush_return
%%_des_flush_end:
        ;; return a job
        ;; - decrement number of jobs in use
	sub	qword [STATE + _des_lanes_in_use], 1
        ;; - put the lane back to free lanes pool
        mov	IA0, [STATE + _des_unused_lanes]
        shl	IA0, 4
        or      IA0, MIN_IDX
        mov	[STATE + _des_unused_lanes], IA0
        ;; - mark job as complete
        mov     rax, [STATE + _des_job_in_lane + MIN_IDX*8]
        or	dword [rax + _status], IMB_STATUS_COMPLETED_CIPHER
        ;; - clear job pointer
        mov     qword [STATE + _des_job_in_lane + MIN_IDX*8], 0
%ifdef SAFE_DATA
        ; Set bit of lane of returned job
        xor     DWORD(IA0), DWORD(IA0)
        bts     DWORD(IA0), DWORD(MIN_IDX)
        kmovd   k1, DWORD(IA0)
        kord    k6, k1, k6

        ;; Clear IV of returned job and "NULL lanes" (k6 contains the mask of the jobs)
        vpxorq  ZTMP1, ZTMP1
        vmovdqa32       [STATE + _des_args_IV]{k6}, ZTMP1
        vmovdqa32       [STATE + _des_args_IV + (16*4)]{k6}, ZTMP1
%endif
%%_des_flush_return:
        vzeroupper
%endmacro

;;; ========================================================
;;; DATA

mksection .rodata
default rel

;;; ========================================================
;;; CODE
mksection .text

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : job
align 64
MKGLOBAL(submit_job_des_cbc_enc_avx512,function,internal)
submit_job_des_cbc_enc_avx512:
        endbranch64
        GENERIC_DES_SUBMIT DES, ENC
        ret

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : job
align 64
MKGLOBAL(submit_job_des_cbc_dec_avx512,function,internal)
submit_job_des_cbc_dec_avx512:
        endbranch64
        GENERIC_DES_SUBMIT DES, DEC
        ret

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : job
align 64
MKGLOBAL(submit_job_docsis_des_enc_avx512,function,internal)
submit_job_docsis_des_enc_avx512:
        endbranch64
        GENERIC_DES_SUBMIT DOCSIS, ENC
        ret

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : job
align 64
MKGLOBAL(submit_job_docsis_des_dec_avx512,function,internal)
submit_job_docsis_des_dec_avx512:
        endbranch64
        GENERIC_DES_SUBMIT DOCSIS, DEC
        ret

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : job
align 64
MKGLOBAL(submit_job_3des_cbc_enc_avx512,function,internal)
submit_job_3des_cbc_enc_avx512:
        endbranch64
        GENERIC_DES_SUBMIT 3DES, ENC
        ret

;;; arg 1 : pointer to DES OOO structure
;;; arg 2 : job
align 64
MKGLOBAL(submit_job_3des_cbc_dec_avx512,function,internal)
submit_job_3des_cbc_dec_avx512:
        endbranch64
        GENERIC_DES_SUBMIT 3DES, DEC
        ret

;;; arg 1 : pointer to DES OOO structure
align 64
MKGLOBAL(flush_job_des_cbc_enc_avx512,function,internal)
flush_job_des_cbc_enc_avx512:
        endbranch64
        GENERIC_DES_FLUSH DES, ENC
        ret

;;; arg 1 : pointer to DES OOO structure
align 64
MKGLOBAL(flush_job_des_cbc_dec_avx512,function,internal)
flush_job_des_cbc_dec_avx512:
        endbranch64
        GENERIC_DES_FLUSH DES, DEC
        ret

;;; arg 1 : pointer to DES OOO structure
align 64
MKGLOBAL(flush_job_docsis_des_enc_avx512,function,internal)
flush_job_docsis_des_enc_avx512:
        endbranch64
        GENERIC_DES_FLUSH DOCSIS, ENC
        ret

;;; arg 1 : pointer to DES OOO structure
align 64
MKGLOBAL(flush_job_docsis_des_dec_avx512,function,internal)
flush_job_docsis_des_dec_avx512:
        endbranch64
        GENERIC_DES_FLUSH DOCSIS, DEC
        ret

;;; arg 1 : pointer to DES OOO structure
align 64
MKGLOBAL(flush_job_3des_cbc_enc_avx512,function,internal)
flush_job_3des_cbc_enc_avx512:
        endbranch64
        GENERIC_DES_FLUSH 3DES, ENC
        ret

;;; arg 1 : pointer to DES OOO structure
align 64
MKGLOBAL(flush_job_3des_cbc_dec_avx512,function,internal)
flush_job_3des_cbc_dec_avx512:
        endbranch64
        GENERIC_DES_FLUSH 3DES, DEC
        ret

mksection stack-noexec

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

%use smartalign

%include "include/imb_job.asm"
%include "include/os.asm"
%include "include/memcpy.asm"
%include "include/clear_regs.asm"
%include "include/cet.inc"

extern aes_cntr_pon_enc_128_vaes_avx512
extern aes_cntr_pon_dec_128_vaes_avx512
extern ethernet_fcs_avx512_local

;;; This is implementation of algorithms: AES128-CTR + CRC32 + BIP
;;; This combination is required by PON/xPON/gPON standard.
;;; Note: BIP is running XOR of double words
;;; Order of operations:
;;; - encrypt: HEC update (XGEM header), CRC32 (Ethernet FCS), AES-CTR and BIP
;;; - decrypt: BIP, AES-CTR and CRC32 (Ethernet FCS)

;; Precomputed constants for HEC calculation (XGEM header)
;; POLY 0x53900000:
;;         k1    = 0xf9800000
;;         k2    = 0xa0900000
;;         k3    = 0x7cc00000
;;         q     = 0x46b927ec
;;         p_res = 0x53900000

mksection .rodata
default rel

align 16
k3_q:
        dq 0x7cc00000, 0x46b927ec

align 16
p_res:
        dq 0x53900000, 0

align 16
mask_out_top_64bits:
        dq 0xffffffff_ffffffff, 0

byte64_len_to_mask_table:
        dq      0xffffffffffffffff, 0x0000000000000001
        dq      0x0000000000000003, 0x0000000000000007
        dq      0x000000000000000f, 0x000000000000001f
        dq      0x000000000000003f, 0x000000000000007f
        dq      0x00000000000000ff, 0x00000000000001ff
        dq      0x00000000000003ff, 0x00000000000007ff
        dq      0x0000000000000fff, 0x0000000000001fff
        dq      0x0000000000003fff, 0x0000000000007fff
        dq      0x000000000000ffff, 0x000000000001ffff
        dq      0x000000000003ffff, 0x000000000007ffff
        dq      0x00000000000fffff, 0x00000000001fffff
        dq      0x00000000003fffff, 0x00000000007fffff
        dq      0x0000000000ffffff, 0x0000000001ffffff
        dq      0x0000000003ffffff, 0x0000000007ffffff
        dq      0x000000000fffffff, 0x000000001fffffff
        dq      0x000000003fffffff, 0x000000007fffffff
        dq      0x00000000ffffffff, 0x00000001ffffffff
        dq      0x00000003ffffffff, 0x00000007ffffffff
        dq      0x0000000fffffffff, 0x0000001fffffffff
        dq      0x0000003fffffffff, 0x0000007fffffffff
        dq      0x000000ffffffffff, 0x000001ffffffffff
        dq      0x000003ffffffffff, 0x000007ffffffffff
        dq      0x00000fffffffffff, 0x00001fffffffffff
        dq      0x00003fffffffffff, 0x00007fffffffffff
        dq      0x0000ffffffffffff, 0x0001ffffffffffff
        dq      0x0003ffffffffffff, 0x0007ffffffffffff
        dq      0x000fffffffffffff, 0x001fffffffffffff
        dq      0x003fffffffffffff, 0x007fffffffffffff
        dq      0x00ffffffffffffff, 0x01ffffffffffffff
        dq      0x03ffffffffffffff, 0x07ffffffffffffff
        dq      0x0fffffffffffffff, 0x1fffffffffffffff
        dq      0x3fffffffffffffff, 0x7fffffffffffffff
        dq      0xffffffffffffffff

mksection .text

%define xtmp1   xmm4
%define xtmp2   xmm5

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
%define arg5    qword [rsp + 32]
%define arg6    qword [rsp + 40]
%endif

%define job     arg1

%define tmp_1           r10
%define tmp_2           r11
%define bytes_to_crc    r12
%define bip             r13 ; Needs to be in preserved register
%define src             r14 ; Needs to be in preserved register
%define dst             r15 ; Needs to be in preserved register

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Stack frame definition
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
struc STACKFRAME
_gpr_save:      resq    4  ; Memory to save GP registers
_job_save:      resq    1  ; Memory to save job pointer
endstruc

;; =============================================================================
;; Barrett reduction from 128-bits to 32-bits modulo 0x53900000 polynomial

%macro HEC_REDUCE_128_TO_32 2
%define %%XMM_IN_OUT %1         ; [in/out] xmm register with data in and out
%define %%XTMP       %2         ; [clobbered] temporary xmm register

        ;; 128 to 64 bit reduction
        vpclmulqdq      %%XTMP, %%XMM_IN_OUT, [rel k3_q], 0x01 ; K3
        vpxor           %%XTMP, %%XTMP, %%XMM_IN_OUT

        vpclmulqdq      %%XTMP, %%XTMP, [rel k3_q], 0x01 ; K3
        vpxor           %%XMM_IN_OUT, %%XTMP, %%XMM_IN_OUT

        vpand           %%XMM_IN_OUT, [rel mask_out_top_64bits]

        ;; 64 to 32 bit reduction
        vpsrldq         %%XTMP, %%XMM_IN_OUT, 4
        vpclmulqdq      %%XTMP, %%XTMP, [rel k3_q], 0x10 ; Q
        vpxor           %%XTMP, %%XTMP, %%XMM_IN_OUT
        vpsrldq         %%XTMP, %%XTMP, 4

        vpclmulqdq      %%XTMP, %%XTMP, [p_res], 0x00 ; P
        vpxor           %%XMM_IN_OUT, %%XTMP, %%XMM_IN_OUT
%endmacro

;; =============================================================================
;; HEC compute and header update for 64-bit XGEM headers
%macro HEC_COMPUTE_64 4
%define %%HEC_IN_OUT %1         ; [in/out] GP register with HEC in LE format
%define %%GT1        %2         ; [clobbered] temporary GP register
%define %%XT1        %3         ; [clobbered] temporary xmm register
%define %%XT2        %4         ; [clobbered] temporary xmm register

        mov     %%GT1, %%HEC_IN_OUT
        ;; shift out 13 bits of HEC value for CRC computation
        shr     %%GT1, 13

        ;; mask out current HEC value to merge with an updated HEC at the end
        and     %%HEC_IN_OUT, 0xffff_ffff_ffff_e000

        ;; prepare the message for CRC computation
        vmovq   %%XT1, %%GT1
        vpslldq %%XT1, 4         ; shift left by 32-bits

        HEC_REDUCE_128_TO_32 %%XT1, %%XT2

        ;; extract 32-bit value
        ;; - normally perform 20 bit shift right but bit 0 is a parity bit
        vmovd   DWORD(%%GT1), %%XT1
        shr     DWORD(%%GT1), (20 - 1)

        ;; merge header bytes with updated 12-bit CRC value and
        ;; compute parity
        or      %%GT1, %%HEC_IN_OUT
        popcnt  %%HEC_IN_OUT, %%GT1
        and     %%HEC_IN_OUT, 1
        or      %%HEC_IN_OUT, %%GT1
%endmacro

%macro AES128_CTR_PON_ENC 1
%define %%CIPH  %1              ; [in] cipher "CTR" or "NO_CTR"

        sub     rsp, STACKFRAME_size

        mov     [rsp + _gpr_save], r12
        mov     [rsp + _gpr_save + 8], r13
        mov     [rsp + _gpr_save + 8*2], r14
        mov     [rsp + _gpr_save + 8*3], r15

        ;; START BIP and update HEC if encrypt direction
        ;; - load XGEM header (8 bytes) for BIP (not part of encrypted payload)
        ;; - convert it into LE
        ;; - update HEC field in the header
        ;; - convert it into BE
        ;; - store back the header (with updated HEC)
        ;; - start BIP
        ;; (free to use tmp_1, tmp2 and at this stage)
        mov     tmp_2, [job + _src]
        add     tmp_2, [job + _hash_start_src_offset_in_bytes]
        mov     bip, [tmp_2]
        bswap   bip                   ; go to LE
        HEC_COMPUTE_64 bip, tmp_1, xtmp1, xtmp2
        mov     bytes_to_crc, bip
        shr     bytes_to_crc, (48 + 2)  ; PLI = MSB 14 bits
        bswap   bip                   ; go back to BE
        mov     [tmp_2], bip

        ;; get input buffer (after XGEM header)
        mov     src, [job + _src]
        add     src, [job + _cipher_start_src_offset_in_bytes]

        ; Save job pointer
        mov     [rsp + _job_save], job

        cmp     bytes_to_crc, 4
        jle     %%_skip_crc
        sub     bytes_to_crc, 4         ; subtract size of the CRC itself

        ; Calculate CRC
%ifndef LINUX
        ;; If Windows, reserve memory in stack for parameter transferring
        sub     rsp, 8*4
%endif
        mov     arg1, src
        mov     arg2, bytes_to_crc
        lea     arg3, [src + bytes_to_crc]
        call    ethernet_fcs_avx512_local

%ifndef LINUX
        add     rsp, 8*4
%endif
        ; Restore job pointer
        mov    job, [rsp + _job_save]

        mov     tmp_1, [job + _auth_tag_output]
        mov     [tmp_1 + 4], eax
%%_skip_crc:
        ; get output buffer
        mov     dst, [job + _dst]

%ifidn %%CIPH, CTR
        ; Encrypt buffer and calculate BIP in the same function
        mov     arg2, dst

        mov     arg3, [job + _iv]
        mov     arg4, [job + _enc_keys]

%ifndef LINUX
        ;; If Windows, reserve memory in stack for parameter transferring
        sub     rsp, 8*6
        mov     tmp_1, [job + _msg_len_to_cipher_in_bytes]
        mov     arg5, tmp_1 ; arg5 in stack, not register
%else
        mov     arg5, [job + _msg_len_to_cipher_in_bytes]
%endif

        mov     arg6, bip
        mov     arg1, src

        call    aes_cntr_pon_enc_128_vaes_avx512

        mov     bip, arg6

%ifndef LINUX
        add     rsp, 8*6
%endif
        ; Restore job pointer
        mov     job, [rsp + _job_save]
%else
        ; Calculate BIP (XOR message)
        vmovq   xmm1, bip

        ;; Message length to cipher is 0
        ;; - length is obtained from message length to hash (BIP) minus XGEM header size
        mov     tmp_2, [job + _msg_len_to_hash_in_bytes]
        sub     tmp_2, 8

%%start_bip:
        cmp     tmp_2, 64
        jle     %%finish_bip

        vpxorq  zmm1, [dst]
        sub     tmp_2, 64
        add     dst, 64

        jmp     %%start_bip
%%finish_bip:

        lea     tmp_1, [rel byte64_len_to_mask_table]
        mov     tmp_1, [tmp_1 + 8*tmp_2]
        kmovq   k1, tmp_1

        vmovdqu8 zmm0{k1}{z}, [dst]
        vpxorq  zmm1, zmm0

        vextracti64x4   ymm0, zmm1, 1
        vpxorq  ymm1, ymm0
        vextracti32x4 xmm0, ymm1, 1
        vpxorq  xmm1, xmm0
        vpsrldq xmm0, xmm1, 8
        vpxorq  xmm1, xmm0
        vpsrldq xmm0, xmm1, 4
        vpxord  xmm1, xmm0

        vmovq   bip, xmm1
%endif ; CIPH = CTR

        mov     tmp_1, [job + _auth_tag_output]
        mov     [tmp_1], DWORD(bip)

        ;; set job status
        or      dword [job + _status], IMB_STATUS_COMPLETED

        ;;  return job
        mov     rax, job

        mov     r12, [rsp + _gpr_save]
        mov     r13, [rsp + _gpr_save + 8*1]
        mov     r14, [rsp + _gpr_save + 8*2]
        mov     r15, [rsp + _gpr_save + 8*3]
        add     rsp, STACKFRAME_size

%ifdef SAFE_DATA
	clear_all_zmms_asm
%endif ;; SAFE_DATA

%endmacro

%macro AES128_CTR_PON_DEC 1
%define %%CIPH  %1              ; [in] cipher "CTR" or "NO_CTR"

        sub      rsp, STACKFRAME_size

        mov     [rsp + _gpr_save], r12
        mov     [rsp + _gpr_save + 8], r13
        mov     [rsp + _gpr_save + 8*2], r14
        mov     [rsp + _gpr_save + 8*3], r15

        ;; START BIP
        ;; - load XGEM header (8 bytes) for BIP (not part of encrypted payload)
        ;; - convert it into LE
        ;; - update HEC field in the header
        ;; - convert it into BE
        ;; - store back the header (with updated HEC)
        ;; - start BIP
        ;; (free to use tmp_1, tmp2 and at this stage)
        mov     tmp_2, [job + _src]
        add     tmp_2, [job + _hash_start_src_offset_in_bytes]
        mov     bip, [tmp_2]
        mov     bytes_to_crc, bip
        bswap   bytes_to_crc            ; go to LE
        shr     bytes_to_crc, (48 + 2)  ; PLI = MSB 14 bits

        ;; get input buffer (after XGEM header)
        mov     src, [job + _src]
        add     src, [job + _cipher_start_src_offset_in_bytes]
        ; get output buffer
        mov     dst, [job + _dst]

        ; Save job pointer
        mov     [rsp + _job_save], job

%ifidn %%CIPH, CTR
        ;; Decrypt message and calculate BIP in same function
        mov     arg2, [job + _dst]
        mov     arg3, [job + _iv]
        mov     arg4, [job + _enc_keys]

%ifndef LINUX
        ;; If Windows, reserve memory in stack for parameter transferring
        sub     rsp, 8*6
        mov     tmp_1, [job + _msg_len_to_cipher_in_bytes]
        mov     arg5, tmp_1 ; arg5 in stack, not register
%else
        mov     arg5, [job + _msg_len_to_cipher_in_bytes]
%endif

        mov     arg6, bip
        mov     arg1, src

        ; Decrypt buffer
        call    aes_cntr_pon_dec_128_vaes_avx512

        mov     bip, arg6

%ifndef LINUX
        add     rsp, 8*6
%endif
%else ; %%CIPH == CTR

        ; Calculate BIP (XOR message)
        vmovq   xmm1, bip

        ;; Message length to cipher is 0
        ;; - length is obtained from message length to hash (BIP) minus XGEM header size
        mov     tmp_2, [job + _msg_len_to_hash_in_bytes]
        sub     tmp_2, 8

%%start_bip:
        cmp     tmp_2, 64
        jle     %%finish_bip

        vpxorq  zmm1, [dst]
        sub     tmp_2, 64
        add     dst, 64

        jmp     %%start_bip
%%finish_bip:

        lea     tmp_1, [rel byte64_len_to_mask_table]
        mov     tmp_1, [tmp_1 + 8*tmp_2]
        kmovq   k1, tmp_1

        vmovdqu8 zmm0{k1}{z}, [dst]
        vpxorq  zmm1, zmm0

        vextracti64x4   ymm0, zmm1, 1
        vpxorq  ymm1, ymm0
        vextracti32x4 xmm0, ymm1, 1
        vpxorq  xmm1, xmm0
        vpsrldq xmm0, xmm1, 8
        vpxorq  xmm1, xmm0
        vpsrldq xmm0, xmm1, 4
        vpxord  xmm1, xmm0

        vmovd   DWORD(bip), xmm1

%endif ; CIPH == CTR

        cmp     bytes_to_crc, 4
        jle     %%_skip_crc
        sub     bytes_to_crc, 4         ; subtract size of the CRC itself

        ; Calculate CRC
%ifndef LINUX
        ;; If Windows, reserve memory in stack for parameter transferring
        sub     rsp, 8*4
%endif
        mov     arg1, src
        mov     arg2, bytes_to_crc
        xor     arg3, arg3 ; Do not write CRC in buffer
        call    ethernet_fcs_avx512_local

%ifndef LINUX
        add     rsp, 8*4
%endif
        ; Restore job pointer
        mov    job, [rsp + _job_save]

        mov     tmp_1, [job + _auth_tag_output]
        mov     [tmp_1 + 4], eax
%%_skip_crc:
        ; Restore job pointer
        mov    job, [rsp + _job_save]

        mov     tmp_1, [job + _auth_tag_output]
        mov     [tmp_1], DWORD(bip)

        ;; set job status
        or      dword [job + _status], IMB_STATUS_COMPLETED

        ;;  return job
        mov     rax, job

        mov     r12, [rsp + _gpr_save]
        mov     r13, [rsp + _gpr_save + 8*1]
        mov     r14, [rsp + _gpr_save + 8*2]
        mov     r15, [rsp + _gpr_save + 8*3]
        add     rsp, STACKFRAME_size

%ifdef SAFE_DATA
	clear_all_zmms_asm
%endif ;; SAFE_DATA

%endmacro

;;; submit_job_pon_enc_vaes_avx512(IMB_JOB *job)
align 64
MKGLOBAL(submit_job_pon_enc_vaes_avx512,function,internal)
submit_job_pon_enc_vaes_avx512:
        endbranch64
        AES128_CTR_PON_ENC CTR
        ret

;;; submit_job_pon_dec_vaes_avx512(IMB_JOB *job)
align 64
MKGLOBAL(submit_job_pon_dec_vaes_avx512,function,internal)
submit_job_pon_dec_vaes_avx512:
        endbranch64
        AES128_CTR_PON_DEC CTR
        ret

;;; submit_job_pon_enc_no_ctr_vaes_avx512(IMB_JOB *job)
align 64
MKGLOBAL(submit_job_pon_enc_no_ctr_vaes_avx512,function,internal)
submit_job_pon_enc_no_ctr_vaes_avx512:
        endbranch64
        AES128_CTR_PON_ENC NO_CTR
        ret

;;; submit_job_pon_dec_no_ctr_vaes_avx512(IMB_JOB *job)
align 64
MKGLOBAL(submit_job_pon_dec_no_ctr_vaes_avx512,function,internal)
submit_job_pon_dec_no_ctr_vaes_avx512:
        endbranch64
        AES128_CTR_PON_DEC NO_CTR
        ret

mksection stack-noexec

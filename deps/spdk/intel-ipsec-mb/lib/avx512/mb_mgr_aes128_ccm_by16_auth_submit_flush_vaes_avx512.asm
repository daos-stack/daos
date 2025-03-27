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
%include "include/memcpy.asm"

%ifndef AES_CBC_MAC
%define AES_CBC_MAC aes128_cbc_mac_vaes_avx512
%define SUBMIT_JOB_AES_CCM_AUTH submit_job_aes128_ccm_auth_vaes_avx512
%define FLUSH_JOB_AES_CCM_AUTH flush_job_aes128_ccm_auth_vaes_avx512
%endif

extern AES_CBC_MAC

mksection .rodata
default rel

align 64
byte_len_to_mask_table:
        dw      0x0000, 0x0001, 0x0003, 0x0007,
        dw      0x000f, 0x001f, 0x003f, 0x007f,
        dw      0x00ff, 0x01ff, 0x03ff, 0x07ff,
        dw      0x0fff, 0x1fff, 0x3fff, 0x7fff,
        dw      0xffff

align 64
byte64_len_to_mask_table:
        dq      0x0000000000000000, 0x0000000000000001
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

align 16
len_mask:
        dq 0xFFFFFFFFFFFFFFF0
align 16
len_masks:
        dq 0x000000000000FFFF, 0x0000000000000000
        dq 0x00000000FFFF0000, 0x0000000000000000
        dq 0x0000FFFF00000000, 0x0000000000000000
        dq 0xFFFF000000000000, 0x0000000000000000
        dq 0x0000000000000000, 0x000000000000FFFF
        dq 0x0000000000000000, 0x00000000FFFF0000
        dq 0x0000000000000000, 0x0000FFFF00000000
        dq 0x0000000000000000, 0xFFFF000000000000
dupw:
	dq 0x0100010001000100, 0x0100010001000100
counter_mask:
	dq 0xFFFFFFFFFFFFFF07, 0x0000FFFFFFFFFFFF
one:    dq  1
two:    dq  2
three:  dq  3
four:   dq  4
five:   dq  5
six:    dq  6
seven:  dq  7

mksection .text

%define APPEND(a,b) a %+ b

%ifndef NROUNDS
%define NROUNDS 9 ; AES-CCM-128
%endif
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
%define tmp4             rax
%define auth_len_aad     rax

%define min_idx          rbp
%define flags            rbp

%define lane             r8

%define iv_len           r9
%define auth_len         r9

%define aad_len          r10
%define init_block_addr  r11

%define unused_lanes     rbx
%define r                rbx

%define tmp              r12
%define tmp2             r13
%define tmp3             r14

%define good_lane        r15
%define min_job          r15

%define init_block0      xmm0
%define ccm_lens         ymm1
%define min_len_idx      xmm2
%define xtmp0            xmm3
%define xtmp1            xmm4
%define xtmp2            xmm5
%define ytmp3            ymm6
%define ytmp0            ymm3
%define ytmp1            ymm4
%define ytmp2            ymm5
%define ytmp3            ymm6

; STACK_SPACE needs to be an odd multiple of 8
; This routine and its callee clobbers all GPRs
struc STACK
_gpr_save:      resq    8
_rsp_save:      resq    1
endstruc

;;; ===========================================================================
;;; ===========================================================================
;;; MACROS
;;; ===========================================================================
;;; ===========================================================================

%macro ENCRYPT_SINGLE_BLOCK 2
%define %%KP   %1
%define %%XDATA %2

                vpxor           %%XDATA, [%%KP + 0*(16*16)]
%assign i 1
%rep NROUNDS
                vaesenc         %%XDATA, [%%KP + i*(16*16)]
%assign i (i+1)
%endrep
                vaesenclast     %%XDATA, [%%KP + i*(16*16)]
%endmacro

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
        lea             %%IA0, [state + _aes_ccm_args_key_tab]
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

%if NROUNDS == 9 ;; 11 round keys
        mov             %%IA0, 0x3f
        kmovq           k1, %%IA0
        vmovdqu64       %%ZTMP{k1}{z}, [%%KP + 128]

        vextracti64x2   [%%COL + ROW*8], %%ZTMP, 0
        vextracti64x2   [%%COL + ROW*9], %%ZTMP, 1
        vextracti64x2   [%%COL + ROW*10], %%ZTMP, 2

%else ;; assume 15 keys for CCM 256
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

        vmovdqa64       %%XTMP1, [state + _aes_ccm_args_IV + %%IDX]
        lea             %%KEY_TAB, [state + _aes_ccm_args_key_tab]
        kmovw           DWORD(%%NULL_MASK), %%MASK_REG

%assign j 0 ; outer loop to iterate through round keys
%rep 15
        vmovdqa64       %%XTMP2, [%%KEY_TAB + j + %%IDX]

%assign k 0 ; inner loop to iterate through lanes
%rep 16
        bt              %%NULL_MASK, k
        jnc             %%_skip_copy %+ j %+ _ %+ k

%if j == 0 ;; copy IVs for each lane just once
        vmovdqa64       [state + _aes_ccm_args_IV + (k*16)], %%XTMP1
%endif
        ;; copy key for each lane
        vmovdqa64       [%%KEY_TAB + j + (k*16)], %%XTMP2
%%_skip_copy %+ j %+ _ %+ k:
%assign k (k + 1)
%endrep

%assign j (j + 256)
%endrep

%endmacro

; clear IVs, block 0 and round key's in NULL lanes
%macro CLEAR_IV_KEYS_BLK0_IN_NULL_LANES 3
%define %%NULL_MASK     %1 ; [clobbered] GP to store NULL lane mask
%define %%XTMP          %2 ; [clobbered] temp XMM reg
%define %%MASK_REG      %3 ; [in] mask register

        vpxorq          ZWORD(%%XTMP), ZWORD(%%XTMP)
        kmovw           DWORD(%%NULL_MASK), %%MASK_REG
%assign k 0 ; outer loop to iterate through lanes
%rep 16
        bt              %%NULL_MASK, k
        jnc             %%_skip_clear %+ k

        ;; clean lane block 0 and IV buffers
        vmovdqa64       [state + _aes_ccm_init_blocks + (k*64)], ZWORD(%%XTMP)
        vmovdqa64       [state + _aes_ccm_args_IV + (k*16)], %%XTMP

%assign j 0 ; inner loop to iterate through round keys
%rep NROUNDS + 2
        vmovdqa64       [state + _aes_ccm_args_key_tab + j + (k*16)], %%XTMP
%assign j (j + 256)

%endrep
%%_skip_clear %+ k:
%assign k (k + 1)
%endrep

%endmacro

;;; ===========================================================================
;;; AES CCM auth job submit & flush
;;; ===========================================================================
;;; SUBMIT_FLUSH [in] - SUBMIT, FLUSH job selection
%macro GENERIC_SUBMIT_FLUSH_JOB_AES_CCM_AUTH_AVX 1
%define %%SUBMIT_FLUSH %1

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

        ;; Find free lane
        mov     unused_lanes, [state + _aes_ccm_unused_lanes]

%ifidn %%SUBMIT_FLUSH, SUBMIT

        mov     lane, unused_lanes
        and     lane, 15
        shr     unused_lanes, 4
        mov     [state + _aes_ccm_unused_lanes], unused_lanes
        add     qword [state + _aes_ccm_num_lanes_inuse], 1

        ;; Copy job info into lane
        mov     [state + _aes_ccm_job_in_lane + lane*8], job

        ;; Insert expanded keys
        mov     tmp, [job + _enc_keys]
        INSERT_KEYS tmp, lane, tmp2, zmm4, tmp3

        ;; init_done = 0
        mov     word [state + _aes_ccm_init_done + lane*2], 0
        lea     tmp, [lane * 8]

        vpxor   init_block0, init_block0
        vmovdqa [state + _aes_ccm_args_IV + tmp*2], init_block0

        ;; Prepare initial Block 0 for CBC-MAC-128

        ;; Byte 0: flags with L' and M' (AAD later)
        ;; Calculate L' = 15 - IV length - 1 = 14 - IV length
        mov     flags, 14
        mov     iv_len, [job + _iv_len_in_bytes]
        sub     flags, iv_len
        ;; Calculate M' = (Digest length - 2) / 2
        mov     tmp, [job + _auth_tag_output_len_in_bytes]
        sub     tmp, 2

        shl     tmp, 2 ; M' << 3 (combine 1xshr, to div by 2, and 3xshl)
        or      flags, tmp

        ;; Bytes 1 - 13: Nonce (7 - 13 bytes long)
        ;; Bytes 1 - 7 are always copied (first 7 bytes)
        mov     tmp, [job + _iv]

        lea     tmp2, [rel byte_len_to_mask_table]
        kmovw   k1, [tmp2 + iv_len*2]

        vmovdqu8 init_block0{k1}, [tmp]
        vpslldq init_block0, init_block0, 1

        ;; Bytes 14 & 15 (message length), in Big Endian
        mov     ax, [job + _msg_len_to_hash_in_bytes]
        xchg    al, ah
        vpinsrw init_block0, ax, 7

        mov     aad_len, [job + _cbcmac_aad_len]
        ;; Initial length to authenticate (Block 0)
        mov     auth_len, 16
        ;; Length to authenticate (Block 0 + len(AAD) (2B) + AAD padded,
        ;; so length is multiple of 64B)
        lea     auth_len_aad, [aad_len + (2 + 15) + 16]
        and     auth_len_aad, -16

        or      aad_len, aad_len
        cmovne  auth_len, auth_len_aad

        ;; Update lengths to authenticate and find min length
        vmovdqa ccm_lens, [state + _aes_ccm_lens]
        xor     DWORD(tmp), DWORD(tmp)
        bts     DWORD(tmp), DWORD(lane)
        kmovw   k1, DWORD(tmp)

        vpbroadcastw    ccm_lens{k1}, WORD(auth_len)
        vmovdqa64       [state + _aes_cmac_lens], ccm_lens

        vphminposuw min_len_idx, XWORD(ccm_lens)

        mov     tmp, lane
        shl     tmp, 6
        lea     init_block_addr, [state + _aes_ccm_init_blocks + tmp]
        or      aad_len, aad_len
        je      %%_aad_complete

        or      flags, (1 << 6) ; Set Adata bit in flags

        ;; Copy AAD
        ;; Set all 0s in last block (padding)
        lea     tmp, [init_block_addr + auth_len]
        sub     tmp, 16
        vpxor   xtmp0, xtmp0
        vmovdqa [tmp], xtmp0

        ;; Start copying from second block
        lea     tmp, [init_block_addr+16]
        mov     rax, aad_len
        xchg    al, ah
        mov     [tmp], ax
        add     tmp, 2

        lea     tmp2, [rel byte64_len_to_mask_table]
        kmovq   k1, [tmp2 + aad_len*8]

        mov     tmp2, [job + _cbcmac_aad]
        vmovdqu8 ZWORD(xtmp0){k1}, [tmp2]
        vmovdqu8 [tmp]{k1}, ZWORD(xtmp0)

%%_aad_complete:

        ;; Finish Block 0 with Byte 0
        vpinsrb init_block0, BYTE(flags), 0
        vmovdqa [init_block_addr], init_block0

        mov     [state + _aes_ccm_args_in + lane * 8], init_block_addr

        cmp     qword [state + _aes_ccm_num_lanes_inuse], 16
        jne     %%_return_null

%else ; end SUBMIT

        ;; Check at least one job
        cmp     qword [state + _aes_ccm_num_lanes_inuse], 0
        je      %%_return_null

        ; find a lane with a non-null job
        vpxorq          zmm7, zmm7, zmm7
        vmovdqu64       zmm1, [state + _aes_ccm_job_in_lane + (0*PTR_SZ)]
        vmovdqu64       zmm2, [state + _aes_ccm_job_in_lane + (8*PTR_SZ)]
        vpcmpq          k4, zmm1, zmm7, 0 ; EQ
        vpcmpq          k5, zmm2, zmm7, 0 ; EQ
        kshiftlw        k6, k5, 8
        korw            k6, k6, k4              ; masks of NULL jobs in k4 (8), k5 (8) and k6 (16)
        knotw           k7, k6                  ; mask of non-NULL jobs
        kmovw           DWORD(tmp), k7
        bsf             DWORD(tmp2), DWORD(tmp) ; index of the 1st set bit in tmp

        ;; copy good lane data into NULL lanes
        mov             tmp, [state + _aes_ccm_args_in + tmp2*8]
        vpbroadcastq    zmm1, tmp
        vmovdqa64       [state + _aes_ccm_args_in + (0*PTR_SZ)]{k4}, zmm1
        vmovdqa64       [state + _aes_ccm_args_in + (8*PTR_SZ)]{k5}, zmm1

        ;; - set len to UINT16_MAX
        mov             WORD(tmp), 0xffff
        vmovdqa64       ccm_lens, [state + _aes_ccm_lens]
        vpbroadcastw    ccm_lens{k6}, WORD(tmp)
        vmovdqa64       [state + _aes_ccm_lens], ccm_lens

        ;; - copy init done
        movzx           tmp,  word [state + _aes_ccm_init_done + tmp2*2]
        vmovdqa64       ytmp1, [state + _aes_ccm_init_done]
        vpbroadcastw    ytmp1{k6}, WORD(tmp)
        vmovdqa64       [state + _aes_ccm_init_done], ytmp1

        ;; scale up good lane idx before copying IV and keys
        shl             tmp2, 4

        ;; - copy IV and round keys to null lanes
        COPY_IV_KEYS_TO_NULL_LANES tmp2, tmp4, tmp3, xmm4, xmm5, k6

        ;; Find min length for lanes 0-7
        vphminposuw min_len_idx, XWORD(ccm_lens)
%endif ; end FLUSH

%%_ccm_round:

        ; Find min length for lanes 8-15
        vpextrw         DWORD(len2), min_len_idx, 0   ; min value
        vpextrw         DWORD(min_idx), min_len_idx, 1   ; min index
        vextracti128    xtmp1, ccm_lens, 1
        vphminposuw     min_len_idx, xtmp1
        vpextrw         DWORD(tmp4), min_len_idx, 0       ; min value
        cmp             DWORD(len2), DWORD(tmp4)
        jle             %%_use_min
        vpextrw         DWORD(min_idx), min_len_idx, 1   ; min index
        add             DWORD(min_idx), 8               ; but index +8
        mov             len2, tmp4                    ; min len
%%_use_min:
        mov             min_job, [state + _aes_ccm_job_in_lane + min_idx*8]
        or              len2, len2
        jz              %%_len_is_0

        vpbroadcastw    ytmp0, WORD(len2)
%ifidn %%SUBMIT_FLUSH, SUBMIT
        vpsubw          ccm_lens, ccm_lens, ytmp0
%else
        vpsubw          ccm_lens{k7}, ccm_lens, ytmp0
%endif
        vmovdqa         [state + _aes_cmac_lens], ccm_lens

        ; "state" and "args" are the same address, arg1
        ; len2 is arg2
        call    AES_CBC_MAC
        ; state and min_idx are intact

%%_len_is_0:

        movzx   tmp, WORD [state + _aes_ccm_init_done + min_idx*2]
        cmp     WORD(tmp), 0
        je      %%_prepare_full_blocks_to_auth
        cmp     WORD(tmp), 1
        je      %%_prepare_partial_block_to_auth

%%_encrypt_digest:

        ;; Set counter block 0 (reusing previous initial block 0)
        mov     tmp, min_idx
        shl     tmp, 3
        vmovdqa init_block0, [state + _aes_ccm_init_blocks + tmp * 8]

        vpand   init_block0, [rel counter_mask]

        lea     tmp2, [state + _aes_ccm_args_key_tab + tmp*2]
        ENCRYPT_SINGLE_BLOCK tmp2, init_block0
        vpxor   init_block0, [state + _aes_ccm_args_IV + tmp*2]

        ;; Copy Mlen bytes into auth_tag_output (Mlen = 4,6,8,10,12,14,16)
        mov     min_job, [state + _aes_ccm_job_in_lane + tmp]
        mov     tmp3, [min_job + _auth_tag_output_len_in_bytes]
        mov     tmp2, [min_job + _auth_tag_output]

        simd_store_avx tmp2, init_block0, tmp3, tmp, tmp4
%%_update_lanes:
        ; Update unused lanes
        mov     unused_lanes, [state + _aes_ccm_unused_lanes]
        shl     unused_lanes, 4
        or      unused_lanes, min_idx
        mov     [state + _aes_ccm_unused_lanes], unused_lanes
        sub     qword [state + _aes_ccm_num_lanes_inuse], 1

        ; Set return job
        mov     job_rax, min_job

        mov     qword [state + _aes_ccm_job_in_lane + min_idx*8], 0
        or      dword [job_rax + _status], IMB_STATUS_COMPLETED_AUTH

%ifdef SAFE_DATA
       vpxorq   ZWORD(xtmp0), ZWORD(xtmp0)
%ifidn %%SUBMIT_FLUSH, SUBMIT
       shl     min_idx, 4

       ;; Clear digest (in memory for CBC IV), counter block 0 and AAD of returned job
       vmovdqa   [state + _aes_ccm_args_IV + min_idx],              xtmp0
       vmovdqa64 [state + _aes_ccm_init_blocks + min_idx * 4],      ZWORD(xtmp0)

       ;; Clear expanded keys
%assign round 0
%rep NROUNDS + 2
        vmovdqa [state + _aes_ccm_args_key_tab + round * (16*16) + min_idx], xtmp0
%assign round (round + 1)
%endrep

%else ;; FLUSH
        ;; Clear digest (in memory for CBC IV), counter block 0 and AAD
        ;; of returned job and "NULL lanes"
        xor     DWORD(tmp2), DWORD(tmp2)
        bts     DWORD(tmp2), DWORD(min_idx)
        kmovw   k1, DWORD(tmp2)
        korw    k6, k1, k6

        ;; Clear IVs, keys and counter block 0 of returned job and "NULL lanes"
        ;; (k6 contains the mask of the jobs)
        CLEAR_IV_KEYS_BLK0_IN_NULL_LANES tmp2, xtmp0, k6

%endif ;; SUBMIT
%endif ;; SAFE_DATA

%%_return:
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

%%_return_null:
        xor     job_rax, job_rax
        jmp     %%_return

%%_prepare_full_blocks_to_auth:

        cmp     dword [min_job + _cipher_direction], 2 ; DECRYPT
        je      %%_decrypt

%%_encrypt:
        mov     tmp, [min_job + _src]
        add     tmp, [min_job + _hash_start_src_offset_in_bytes]
        jmp     %%_set_init_done_1

%%_decrypt:
        mov     tmp, [min_job + _dst]

%%_set_init_done_1:
        mov     [state + _aes_ccm_args_in + min_idx*8], tmp
        mov     word [state + _aes_ccm_init_done + min_idx*2], 1

        ; Check if there are full blocks to hash
        mov     tmp, [min_job + _msg_len_to_hash_in_bytes]
        and     tmp, -16
        je      %%_prepare_partial_block_to_auth

        ;; Update lengths to authenticate and find min length
        vmovdqa ccm_lens, [state + _aes_ccm_lens]
        xor     DWORD(tmp2), DWORD(tmp2)
        bts     DWORD(tmp2), DWORD(min_idx)
        kmovw   k1, DWORD(tmp2)

        vpbroadcastw    ccm_lens{k1}, WORD(tmp)
        vmovdqa64       [state + _aes_cmac_lens], ccm_lens
        vphminposuw     min_len_idx, XWORD(ccm_lens)

        jmp     %%_ccm_round

%%_prepare_partial_block_to_auth:
        ; Check if partial block needs to be hashed
        mov     auth_len, [min_job + _msg_len_to_hash_in_bytes]
        and     auth_len, 15
        je      %%_encrypt_digest

        mov     word [state + _aes_ccm_init_done + min_idx * 2], 2
        ;; Update lengths to authenticate and find min length
        vmovdqa ccm_lens, [state + _aes_ccm_lens]
        xor     DWORD(tmp2), DWORD(tmp2)
        bts     DWORD(tmp2), DWORD(min_idx)
        kmovw   k1, DWORD(tmp2)

        mov             DWORD(tmp2), 16
        vpbroadcastw    ccm_lens{k1}, WORD(tmp2)
        vmovdqa64       [state + _aes_cmac_lens], ccm_lens
        vphminposuw     min_len_idx, XWORD(ccm_lens)

        mov     tmp2, min_idx
        shl     tmp2, 6
        add     tmp2, 16 ; pb[AES_BLOCK_SIZE]
        lea     init_block_addr, [state + _aes_ccm_init_blocks + tmp2]
        mov     tmp2, [state + _aes_ccm_args_in + min_idx * 8]

        simd_load_avx_15_1 xtmp0, tmp2, auth_len

%%_finish_partial_block_copy:
        vmovdqa [init_block_addr], xtmp0
        mov     [state + _aes_ccm_args_in + min_idx * 8], init_block_addr

        jmp     %%_ccm_round
%endmacro

align 64
; IMB_JOB * submit_job_aes128/256_ccm_auth_vaes_avx512(MB_MGR_CCM_OOO *state, IMB_JOB *job)
; arg 1 : state
; arg 2 : job
MKGLOBAL(SUBMIT_JOB_AES_CCM_AUTH,function,internal)
SUBMIT_JOB_AES_CCM_AUTH:
        endbranch64
        GENERIC_SUBMIT_FLUSH_JOB_AES_CCM_AUTH_AVX SUBMIT

; IMB_JOB * flush_job_aes128/256_ccm_auth_vaes_avx512(MB_MGR_CCM_OOO *state)
; arg 1 : state
MKGLOBAL(FLUSH_JOB_AES_CCM_AUTH,function,internal)
FLUSH_JOB_AES_CCM_AUTH:
        endbranch64
        GENERIC_SUBMIT_FLUSH_JOB_AES_CCM_AUTH_AVX FLUSH

mksection stack-noexec

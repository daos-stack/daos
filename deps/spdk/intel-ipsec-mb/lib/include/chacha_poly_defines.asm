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

%ifndef CHACHA_POLY_DEFINES_ASM_INCLUDED
%define CHACHA_POLY_DEFINES_ASM_INCLUDED

;;define the fields of gcm_context_data struct
;; struct chacha20_poly1305_context_data {
;;        uint64_t hash[3];
;;        uint64_t aad_len;
;;        uint64_t hash_len;
;;        uint8_t last_ks[64];
;;        uint8_t poly_key[32];
;;        uint8_t poly_scratch[16];
;;        uint64_t last_block_count;
;;        uint64_t remain_ks_bytes;
;;        uint64_t remain_ct_bytes;
;;        uint8_t IV[12];
;; };

%define Hash		(0)	  ; Intermediate computation of hash value (24 bytes)
%define AadLen		(8*3)	  ; Total AAD length (8 bytes)
%define HashLen         (8*4)     ; Total length to digest (excluding AAD) (8 bytes)
%define LastKs		(8*5)     ; Last 64 bytes of KS
%define PolyKey	        (8*13)    ; Poly Key (32 bytes)
%define PolyScratch	(8*17)	  ; Scratchpad to compute Poly on 16 bytes
%define LastBlkCount	(8*19)	  ; Last block count used in last segment (8 bytes)
; Amount of bytes still to use of keystream (8 bytes)
%define RemainKsBytes	(8*20)
; Amount of ciphertext bytes still to use of previous segment to authenticate (8 bytes)
%define RemainCtBytes   (8*21)
%define IV              (8*22)

%endif ; CHACHA_POLY_DEFINES_ASM_INCLUDED

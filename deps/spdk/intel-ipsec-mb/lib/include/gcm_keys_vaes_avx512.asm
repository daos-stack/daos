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

%ifndef GCM_KEYS_VAES_AVX512_INCLUDED
%define GCM_KEYS_VAES_AVX512_INCLUDED

;; Define the fields of gcm_key_data struct:
;; uint8_t expanded_keys[GCM_ENC_KEY_LEN * GCM_KEY_SETS];
;; uint8_t shifted_hkey_9_128[GCM_ENC_KEY_LEN * (128 - 8)];
;; uint8_t shifted_hkey_8[GCM_ENC_KEY_LEN]; // HashKey^8 <<1 mod poly
;; uint8_t shifted_hkey_7[GCM_ENC_KEY_LEN]; // HashKey^7 <<1 mod poly
;; uint8_t shifted_hkey_6[GCM_ENC_KEY_LEN]; // HashKey^6 <<1 mod poly
;; uint8_t shifted_hkey_5[GCM_ENC_KEY_LEN]; // HashKey^5 <<1 mod poly
;; uint8_t shifted_hkey_4[GCM_ENC_KEY_LEN]; // HashKey^4 <<1 mod poly
;; uint8_t shifted_hkey_3[GCM_ENC_KEY_LEN]; // HashKey^3 <<1 mod poly
;; uint8_t shifted_hkey_2[GCM_ENC_KEY_LEN]; // HashKey^2 <<1 mod poly
;; uint8_t shifted_hkey_1[GCM_ENC_KEY_LEN]; // HashKey   <<1 mod poly

;;
;; Key structure holds up to 48 ghash keys
;;
%define HashKey_48      (16*15)   ; HashKey^48 <<1 mod poly
%define HashKey_47      (16*16)   ; HashKey^47 <<1 mod poly
%define HashKey_46      (16*17)   ; HashKey^46 <<1 mod poly
%define HashKey_45      (16*18)   ; HashKey^45 <<1 mod poly
%define HashKey_44      (16*19)   ; HashKey^44 <<1 mod poly
%define HashKey_43      (16*20)   ; HashKey^43 <<1 mod poly
%define HashKey_42      (16*21)   ; HashKey^42 <<1 mod poly
%define HashKey_41      (16*22)   ; HashKey^41 <<1 mod poly
%define HashKey_40      (16*23)   ; HashKey^40 <<1 mod poly
%define HashKey_39      (16*24)   ; HashKey^39 <<1 mod poly
%define HashKey_38      (16*25)   ; HashKey^38 <<1 mod poly
%define HashKey_37      (16*26)   ; HashKey^37 <<1 mod poly
%define HashKey_36      (16*27)   ; HashKey^36 <<1 mod poly
%define HashKey_35      (16*28)   ; HashKey^35 <<1 mod poly
%define HashKey_34      (16*29)   ; HashKey^34 <<1 mod poly
%define HashKey_33      (16*30)   ; HashKey^33 <<1 mod poly
%define HashKey_32      (16*31)   ; HashKey^32 <<1 mod poly
%define HashKey_31      (16*32)   ; HashKey^31 <<1 mod poly
%define HashKey_30      (16*33)   ; HashKey^30 <<1 mod poly
%define HashKey_29      (16*34)   ; HashKey^29 <<1 mod poly
%define HashKey_28      (16*35)   ; HashKey^28 <<1 mod poly
%define HashKey_27      (16*36)   ; HashKey^27 <<1 mod poly
%define HashKey_26      (16*37)   ; HashKey^26 <<1 mod poly
%define HashKey_25      (16*38)   ; HashKey^25 <<1 mod poly
%define HashKey_24      (16*39)   ; HashKey^24 <<1 mod poly
%define HashKey_23      (16*40)   ; HashKey^23 <<1 mod poly
%define HashKey_22      (16*41)   ; HashKey^22 <<1 mod poly
%define HashKey_21      (16*42)   ; HashKey^21 <<1 mod poly
%define HashKey_20      (16*43)   ; HashKey^20 <<1 mod poly
%define HashKey_19      (16*44)   ; HashKey^19 <<1 mod poly
%define HashKey_18      (16*45)   ; HashKey^18 <<1 mod poly
%define HashKey_17      (16*46)   ; HashKey^17 <<1 mod poly
%define HashKey_16      (16*47)   ; HashKey^16 <<1 mod poly
%define HashKey_15      (16*48)   ; HashKey^15 <<1 mod poly
%define HashKey_14      (16*49)   ; HashKey^14 <<1 mod poly
%define HashKey_13      (16*50)   ; HashKey^13 <<1 mod poly
%define HashKey_12      (16*51)   ; HashKey^12 <<1 mod poly
%define HashKey_11      (16*52)   ; HashKey^11 <<1 mod poly
%define HashKey_10      (16*53)   ; HashKey^10 <<1 mod poly
%define HashKey_9       (16*54)   ; HashKey^9 <<1 mod poly
%define HashKey_8       (16*55)   ; HashKey^8 <<1 mod poly
%define HashKey_7       (16*56)   ; HashKey^7 <<1 mod poly
%define HashKey_6       (16*57)   ; HashKey^6 <<1 mod poly
%define HashKey_5       (16*58)   ; HashKey^5 <<1 mod poly
%define HashKey_4       (16*59)   ; HashKey^4 <<1 mod poly
%define HashKey_3       (16*60)   ; HashKey^3 <<1 mod poly
%define HashKey_2       (16*61)   ; HashKey^2 <<1 mod poly
%define HashKey_1       (16*62)   ; HashKey <<1 mod poly
%define HashKey         (16*62)   ; HashKey <<1 mod poly

%endif ; GCM_KEYS_VAES_AVX512_INCLUDED

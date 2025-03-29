;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2016 Intel Corporation All rights reserved.
;
;  Redistribution and use in source and binary forms, with or without
;  modification, are permitted provided that the following conditions
;  are met:
;    * Redistributions of source code must retain the above copyright
;      notice, this list of conditions and the following disclaimer.
;    * Redistributions in binary form must reproduce the above copyright
;      notice, this list of conditions and the following disclaimer in
;      the documentation and/or other materials provided with the
;      distribution.
;    * Neither the name of Intel Corporation nor the names of its
;      contributors may be used to endorse or promote products derived
;      from this software without specific prior written permission.
;
;  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
;  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
;  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
;  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
;  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%include "datastruct.asm"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;; Define MD5 Out Of Order Data Structures
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

START_FIELDS    ; LANE_DATA
;;;     name            size    align
FIELD   _job_in_lane,   8,      8       ; pointer to job object
END_FIELDS

%assign _LANE_DATA_size 	_FIELD_OFFSET
%assign _LANE_DATA_align	_STRUCT_ALIGN

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

START_FIELDS    ; MD5_ARGS_X32
;;;     name            size    align
FIELD   _digest,        4*4*32, 16      ; transposed digest
FIELD   _data_ptr,      8*32,   8       ; array of pointers to data
END_FIELDS

%assign _MD5_ARGS_X8_size       _FIELD_OFFSET
%assign _MD5_ARGS_X8_align      _STRUCT_ALIGN
%assign _MD5_ARGS_X16_size	_FIELD_OFFSET
%assign _MD5_ARGS_X16_align	_STRUCT_ALIGN
%assign _MD5_ARGS_X32_size	_FIELD_OFFSET
%assign _MD5_ARGS_X32_align	_STRUCT_ALIGN
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

START_FIELDS    ; MB_MGR
;;;     name            size    align
FIELD   _args,          _MD5_ARGS_X8_size, _MD5_ARGS_X8_align
FIELD   _lens,          4*32,   8
FIELD   _unused_lanes,  8*4,    8
FIELD   _ldata,         _LANE_DATA_size*32, _LANE_DATA_align
FIELD   _num_lanes_inuse, 4,    4
END_FIELDS

%assign _MB_MGR_size    _FIELD_OFFSET
%assign _MB_MGR_align   _STRUCT_ALIGN

_args_digest    equ     _args + _digest
_args_data_ptr  equ     _args + _data_ptr

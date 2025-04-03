/**********************************************************************
  Copyright(c) 2020 Arm Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Arm Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/
	.arch armv8-a+crypto
	.text
	.align	6

	.global	mh_sha256_block_ce
	.type	mh_sha256_block_ce, %function

/*
Macros
*/

.macro	declare_vector_reg name:req,reg:req,default:req
	\name		.req	\default\reg
	q_\name		.req	q\reg
	v_\name		.req	v\reg
	s_\name		.req	s\reg
.endm

declare_vector_reg	lane0_msg0,	0,v
declare_vector_reg	lane1_msg0,	1,v
declare_vector_reg	lane2_msg0,	2,v
declare_vector_reg	lane3_msg0,	3,v

declare_vector_reg	lane0_msg1,	4,v
declare_vector_reg	lane1_msg1,	5,v
declare_vector_reg	lane2_msg1,	6,v
declare_vector_reg	lane3_msg1,	7,v

declare_vector_reg	lane0_msg2,	8,v
declare_vector_reg	lane1_msg2,	9,v
declare_vector_reg	lane2_msg2,	10,v
declare_vector_reg	lane3_msg2,	11,v

declare_vector_reg	lane0_msg3,	12,v
declare_vector_reg	lane1_msg3,	13,v
declare_vector_reg	lane2_msg3,	14,v
declare_vector_reg	lane3_msg3,	15,v

declare_vector_reg	lane0_state0,	16,v
declare_vector_reg	lane1_state0,	17,v
declare_vector_reg	lane2_state0,	18,v
declare_vector_reg	lane3_state0,	19,v

declare_vector_reg	lane0_state1,	20,v
declare_vector_reg	lane1_state1,	21,v
declare_vector_reg	lane2_state1,	22,v
declare_vector_reg	lane3_state1,	23,v

declare_vector_reg	lane0_tmp0,	24,v
declare_vector_reg	lane1_tmp0,	25,v
declare_vector_reg	lane2_tmp0,	26,v
declare_vector_reg	lane3_tmp0,	27,v

declare_vector_reg	lane0_tmp2,	28,v
declare_vector_reg	lane1_tmp2,	29,v
declare_vector_reg	lane2_tmp2,	30,v
declare_vector_reg	lane3_tmp2,	31,v

declare_vector_reg	key,		27,v
declare_vector_reg	tmp,		29,v

/*
void mh_sha256_block_ce(const uint8_t * input_data,
			uint32_t digests[SHA256_DIGEST_WORDS][HASH_SEGS],
			uint8_t frame_buffer[MH_SHA256_BLOCK_SIZE],
			uint32_t num_blocks);
*/
	x_input_data		.req	x0
	x_digests		.req	x1
	x_frame_buffer		.req	x2
	w_num_blocks		.req	w3

	x_digest_addr		.req	x4
	x_key_addr		.req	x5
	x_msg_addr		.req	x6
	x_lane_offs		.req	x7
	x_offs			.req	x9
	w_input_data_end	.req	w10
	x_input_data_end	.req	x10
	x_tmp			.req	x11
mh_sha256_block_ce:
	cbz		w_num_blocks, .exit
	mov		w_input_data_end, w_num_blocks

	ubfiz		x_input_data_end, x_input_data_end, 10, 32
	add		x_input_data_end, x_input_data, x_input_data_end

	adrp		x_key_addr, .key_addr
	add		x_key_addr, x_key_addr, :lo12:.key_addr

	stp		d8, d9, [sp, -192]!

	stp		d10, d11, [sp, 16]
	stp		d12, d13, [sp, 32]
	stp		d14, d15, [sp, 48]

	.p2align 3,,7
.start_loop:
	mov		x_lane_offs, 0
	mov		x_digest_addr, x_digests

.lane_loop:
	add		x_msg_addr, x_input_data, x_lane_offs, lsl 2

	.p2align 3,,7
	mov		x_offs, 64
	mov		x_tmp, x_digest_addr
	ld4		{v_lane0_state0.S-v_lane3_state0.S}[0], [x_tmp], x_offs
	ld4		{v_lane0_state0.S-v_lane3_state0.S}[1], [x_tmp], x_offs
	ld4		{v_lane0_state0.S-v_lane3_state0.S}[2], [x_tmp], x_offs
	ld4		{v_lane0_state0.S-v_lane3_state0.S}[3], [x_tmp], x_offs

	add		x_tmp, x_digest_addr, 256
	ld4		{v_lane0_state1.S-v_lane3_state1.S}[0], [x_tmp], x_offs
	ld4		{v_lane0_state1.S-v_lane3_state1.S}[1], [x_tmp], x_offs
	ld4		{v_lane0_state1.S-v_lane3_state1.S}[2], [x_tmp], x_offs
	ld4		{v_lane0_state1.S-v_lane3_state1.S}[3], [x_tmp], x_offs

	ld4		{v_lane0_msg0.S-v_lane3_msg0.S}[0], [x_msg_addr], x_offs
	ld4		{v_lane0_msg0.S-v_lane3_msg0.S}[1], [x_msg_addr], x_offs
	ld4		{v_lane0_msg0.S-v_lane3_msg0.S}[2], [x_msg_addr], x_offs
	ld4		{v_lane0_msg0.S-v_lane3_msg0.S}[3], [x_msg_addr], x_offs

	ld4		{v_lane0_msg1.S-v_lane3_msg1.S}[0], [x_msg_addr], x_offs
	ld4		{v_lane0_msg1.S-v_lane3_msg1.S}[1], [x_msg_addr], x_offs
	ld4		{v_lane0_msg1.S-v_lane3_msg1.S}[2], [x_msg_addr], x_offs
	ld4		{v_lane0_msg1.S-v_lane3_msg1.S}[3], [x_msg_addr], x_offs

	ld4		{v_lane0_msg2.S-v_lane3_msg2.S}[0], [x_msg_addr], x_offs
	ld4		{v_lane0_msg2.S-v_lane3_msg2.S}[1], [x_msg_addr], x_offs
	ld4		{v_lane0_msg2.S-v_lane3_msg2.S}[2], [x_msg_addr], x_offs
	ld4		{v_lane0_msg2.S-v_lane3_msg2.S}[3], [x_msg_addr], x_offs

	ld4		{v_lane0_msg3.S-v_lane3_msg3.S}[0], [x_msg_addr], x_offs
	ld4		{v_lane0_msg3.S-v_lane3_msg3.S}[1], [x_msg_addr], x_offs
	ld4		{v_lane0_msg3.S-v_lane3_msg3.S}[2], [x_msg_addr], x_offs
	ld4		{v_lane0_msg3.S-v_lane3_msg3.S}[3], [x_msg_addr], x_offs

	// reverse for little endian
	rev32		v_lane0_msg0.16b, v_lane0_msg0.16b
	rev32		v_lane1_msg0.16b, v_lane1_msg0.16b
	rev32		v_lane2_msg0.16b, v_lane2_msg0.16b
	rev32		v_lane3_msg0.16b, v_lane3_msg0.16b

	rev32		v_lane0_msg1.16b, v_lane0_msg1.16b
	rev32		v_lane1_msg1.16b, v_lane1_msg1.16b
	rev32		v_lane2_msg1.16b, v_lane2_msg1.16b
	rev32		v_lane3_msg1.16b, v_lane3_msg1.16b

	rev32		v_lane0_msg2.16b, v_lane0_msg2.16b
	rev32		v_lane1_msg2.16b, v_lane1_msg2.16b
	rev32		v_lane2_msg2.16b, v_lane2_msg2.16b
	rev32		v_lane3_msg2.16b, v_lane3_msg2.16b

	rev32		v_lane0_msg3.16b, v_lane0_msg3.16b
	rev32		v_lane1_msg3.16b, v_lane1_msg3.16b
	rev32		v_lane2_msg3.16b, v_lane2_msg3.16b
	rev32		v_lane3_msg3.16b, v_lane3_msg3.16b

	// rounds 0-3
	ldr		q_key, [x_key_addr]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg0.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg0.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg0.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg0.4s

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	str		q_lane0_state1, [sp, 64]
	str		q_lane1_state1, [sp, 80]
	str		q_lane2_state1, [sp, 96]
	str		q_lane3_state1, [sp, 112]

	mov		x_offs, 64
	mov		x_tmp, x_digest_addr
	ld4		{v_lane0_tmp2.S-v_lane3_tmp2.S}[0], [x_tmp], x_offs
	ld4		{v_lane0_tmp2.S-v_lane3_tmp2.S}[1], [x_tmp], x_offs
	ld4		{v_lane0_tmp2.S-v_lane3_tmp2.S}[2], [x_tmp], x_offs
	ld4		{v_lane0_tmp2.S-v_lane3_tmp2.S}[3], [x_tmp], x_offs

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg0.4s, v_lane0_msg1.4s
	sha256su0	v_lane1_msg0.4s, v_lane1_msg1.4s
	sha256su0	v_lane2_msg0.4s, v_lane2_msg1.4s
	sha256su0	v_lane3_msg0.4s, v_lane3_msg1.4s

	sha256su1	v_lane0_msg0.4s, v_lane0_msg2.4s, v_lane0_msg3.4s
	sha256su1	v_lane1_msg0.4s, v_lane1_msg2.4s, v_lane1_msg3.4s
	sha256su1	v_lane2_msg0.4s, v_lane2_msg2.4s, v_lane2_msg3.4s
	sha256su1	v_lane3_msg0.4s, v_lane3_msg2.4s, v_lane3_msg3.4s

	// rounds 4-7
	ldr		q_key, [x_key_addr, 16]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg1.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg1.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg1.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg1.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg1.4s, v_lane0_msg2.4s
	sha256su0	v_lane1_msg1.4s, v_lane1_msg2.4s
	sha256su0	v_lane2_msg1.4s, v_lane2_msg2.4s
	sha256su0	v_lane3_msg1.4s, v_lane3_msg2.4s

	sha256su1	v_lane0_msg1.4s, v_lane0_msg3.4s, v_lane0_msg0.4s
	sha256su1	v_lane1_msg1.4s, v_lane1_msg3.4s, v_lane1_msg0.4s
	sha256su1	v_lane2_msg1.4s, v_lane2_msg3.4s, v_lane2_msg0.4s
	sha256su1	v_lane3_msg1.4s, v_lane3_msg3.4s, v_lane3_msg0.4s

	// rounds 8-11
	ldr		q_key, [x_key_addr, 32]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg2.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg2.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg2.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg2.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg2.4s, v_lane0_msg3.4s
	sha256su0	v_lane1_msg2.4s, v_lane1_msg3.4s
	sha256su0	v_lane2_msg2.4s, v_lane2_msg3.4s
	sha256su0	v_lane3_msg2.4s, v_lane3_msg3.4s

	sha256su1	v_lane0_msg2.4s, v_lane0_msg0.4s, v_lane0_msg1.4s
	sha256su1	v_lane1_msg2.4s, v_lane1_msg0.4s, v_lane1_msg1.4s
	sha256su1	v_lane2_msg2.4s, v_lane2_msg0.4s, v_lane2_msg1.4s
	sha256su1	v_lane3_msg2.4s, v_lane3_msg0.4s, v_lane3_msg1.4s

	// rounds 12-15
	ldr		q_key, [x_key_addr, 48]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg3.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg3.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg3.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg3.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg3.4s, v_lane0_msg0.4s
	sha256su0	v_lane1_msg3.4s, v_lane1_msg0.4s
	sha256su0	v_lane2_msg3.4s, v_lane2_msg0.4s
	sha256su0	v_lane3_msg3.4s, v_lane3_msg0.4s

	sha256su1	v_lane0_msg3.4s, v_lane0_msg1.4s, v_lane0_msg2.4s
	sha256su1	v_lane1_msg3.4s, v_lane1_msg1.4s, v_lane1_msg2.4s
	sha256su1	v_lane2_msg3.4s, v_lane2_msg1.4s, v_lane2_msg2.4s
	sha256su1	v_lane3_msg3.4s, v_lane3_msg1.4s, v_lane3_msg2.4s

	// rounds 16-19
	ldr		q_key, [x_key_addr, 64]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg0.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg0.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg0.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg0.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg0.4s, v_lane0_msg1.4s
	sha256su0	v_lane1_msg0.4s, v_lane1_msg1.4s
	sha256su0	v_lane2_msg0.4s, v_lane2_msg1.4s
	sha256su0	v_lane3_msg0.4s, v_lane3_msg1.4s

	sha256su1	v_lane0_msg0.4s, v_lane0_msg2.4s, v_lane0_msg3.4s
	sha256su1	v_lane1_msg0.4s, v_lane1_msg2.4s, v_lane1_msg3.4s
	sha256su1	v_lane2_msg0.4s, v_lane2_msg2.4s, v_lane2_msg3.4s
	sha256su1	v_lane3_msg0.4s, v_lane3_msg2.4s, v_lane3_msg3.4s

	// rounds 20-23
	ldr		q_key, [x_key_addr, 80]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg1.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg1.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg1.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg1.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg1.4s, v_lane0_msg2.4s
	sha256su0	v_lane1_msg1.4s, v_lane1_msg2.4s
	sha256su0	v_lane2_msg1.4s, v_lane2_msg2.4s
	sha256su0	v_lane3_msg1.4s, v_lane3_msg2.4s

	sha256su1	v_lane0_msg1.4s, v_lane0_msg3.4s, v_lane0_msg0.4s
	sha256su1	v_lane1_msg1.4s, v_lane1_msg3.4s, v_lane1_msg0.4s
	sha256su1	v_lane2_msg1.4s, v_lane2_msg3.4s, v_lane2_msg0.4s
	sha256su1	v_lane3_msg1.4s, v_lane3_msg3.4s, v_lane3_msg0.4s

	// rounds 24-27
	ldr		q_key, [x_key_addr, 96]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg2.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg2.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg2.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg2.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg2.4s, v_lane0_msg3.4s
	sha256su0	v_lane1_msg2.4s, v_lane1_msg3.4s
	sha256su0	v_lane2_msg2.4s, v_lane2_msg3.4s
	sha256su0	v_lane3_msg2.4s, v_lane3_msg3.4s

	sha256su1	v_lane0_msg2.4s, v_lane0_msg0.4s, v_lane0_msg1.4s
	sha256su1	v_lane1_msg2.4s, v_lane1_msg0.4s, v_lane1_msg1.4s
	sha256su1	v_lane2_msg2.4s, v_lane2_msg0.4s, v_lane2_msg1.4s
	sha256su1	v_lane3_msg2.4s, v_lane3_msg0.4s, v_lane3_msg1.4s

	// rounds 28-31
	ldr		q_key, [x_key_addr, 112]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg3.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg3.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg3.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg3.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg3.4s, v_lane0_msg0.4s
	sha256su0	v_lane1_msg3.4s, v_lane1_msg0.4s
	sha256su0	v_lane2_msg3.4s, v_lane2_msg0.4s
	sha256su0	v_lane3_msg3.4s, v_lane3_msg0.4s

	sha256su1	v_lane0_msg3.4s, v_lane0_msg1.4s, v_lane0_msg2.4s
	sha256su1	v_lane1_msg3.4s, v_lane1_msg1.4s, v_lane1_msg2.4s
	sha256su1	v_lane2_msg3.4s, v_lane2_msg1.4s, v_lane2_msg2.4s
	sha256su1	v_lane3_msg3.4s, v_lane3_msg1.4s, v_lane3_msg2.4s

	// rounds 32-35
	ldr		q_key, [x_key_addr, 128]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg0.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg0.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg0.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg0.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg0.4s, v_lane0_msg1.4s
	sha256su0	v_lane1_msg0.4s, v_lane1_msg1.4s
	sha256su0	v_lane2_msg0.4s, v_lane2_msg1.4s
	sha256su0	v_lane3_msg0.4s, v_lane3_msg1.4s

	sha256su1	v_lane0_msg0.4s, v_lane0_msg2.4s, v_lane0_msg3.4s
	sha256su1	v_lane1_msg0.4s, v_lane1_msg2.4s, v_lane1_msg3.4s
	sha256su1	v_lane2_msg0.4s, v_lane2_msg2.4s, v_lane2_msg3.4s
	sha256su1	v_lane3_msg0.4s, v_lane3_msg2.4s, v_lane3_msg3.4s

	// rounds 36-39
	ldr		q_key, [x_key_addr, 144]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg1.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg1.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg1.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg1.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg1.4s, v_lane0_msg2.4s
	sha256su0	v_lane1_msg1.4s, v_lane1_msg2.4s
	sha256su0	v_lane2_msg1.4s, v_lane2_msg2.4s
	sha256su0	v_lane3_msg1.4s, v_lane3_msg2.4s

	sha256su1	v_lane0_msg1.4s, v_lane0_msg3.4s, v_lane0_msg0.4s
	sha256su1	v_lane1_msg1.4s, v_lane1_msg3.4s, v_lane1_msg0.4s
	sha256su1	v_lane2_msg1.4s, v_lane2_msg3.4s, v_lane2_msg0.4s
	sha256su1	v_lane3_msg1.4s, v_lane3_msg3.4s, v_lane3_msg0.4s

	// rounds 40-43
	ldr		q_key, [x_key_addr, 160]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg2.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg2.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg2.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg2.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg2.4s, v_lane0_msg3.4s
	sha256su0	v_lane1_msg2.4s, v_lane1_msg3.4s
	sha256su0	v_lane2_msg2.4s, v_lane2_msg3.4s
	sha256su0	v_lane3_msg2.4s, v_lane3_msg3.4s

	sha256su1	v_lane0_msg2.4s, v_lane0_msg0.4s, v_lane0_msg1.4s
	sha256su1	v_lane1_msg2.4s, v_lane1_msg0.4s, v_lane1_msg1.4s
	sha256su1	v_lane2_msg2.4s, v_lane2_msg0.4s, v_lane2_msg1.4s
	sha256su1	v_lane3_msg2.4s, v_lane3_msg0.4s, v_lane3_msg1.4s

	// rounds 44-47
	ldr		q_key, [x_key_addr, 176]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg3.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg3.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg3.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg3.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	sha256su0	v_lane0_msg3.4s, v_lane0_msg0.4s
	sha256su0	v_lane1_msg3.4s, v_lane1_msg0.4s
	sha256su0	v_lane2_msg3.4s, v_lane2_msg0.4s
	sha256su0	v_lane3_msg3.4s, v_lane3_msg0.4s

	sha256su1	v_lane0_msg3.4s, v_lane0_msg1.4s, v_lane0_msg2.4s
	sha256su1	v_lane1_msg3.4s, v_lane1_msg1.4s, v_lane1_msg2.4s
	sha256su1	v_lane2_msg3.4s, v_lane2_msg1.4s, v_lane2_msg2.4s
	sha256su1	v_lane3_msg3.4s, v_lane3_msg1.4s, v_lane3_msg2.4s

	// rounds 48-51
	ldr		q_key, [x_key_addr, 192]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg0.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg0.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg0.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg0.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	// rounds 52-55
	ldr		q_key, [x_key_addr, 208]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg1.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg1.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg1.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg1.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	// rounds 56-59
	ldr		q_key, [x_key_addr, 224]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg2.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg2.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg2.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg2.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	// rounds 60-63
	ldr		q_key, [x_key_addr, 240]
	add		v_lane0_tmp0.4s, v_key.4s, v_lane0_msg3.4s
	add		v_lane1_tmp0.4s, v_key.4s, v_lane1_msg3.4s
	add		v_lane2_tmp0.4s, v_key.4s, v_lane2_msg3.4s
	add		v_lane3_tmp0.4s, v_key.4s, v_lane3_msg3.4s

	mov		v_lane0_tmp2.16b, v_lane0_state0.16b
	mov		v_lane1_tmp2.16b, v_lane1_state0.16b
	mov		v_lane2_tmp2.16b, v_lane2_state0.16b
	mov		v_lane3_tmp2.16b, v_lane3_state0.16b

	sha256h		q_lane0_state0, q_lane0_state1, v_lane0_tmp0.4s
	sha256h		q_lane1_state0, q_lane1_state1, v_lane1_tmp0.4s
	sha256h		q_lane2_state0, q_lane2_state1, v_lane2_tmp0.4s
	sha256h		q_lane3_state0, q_lane3_state1, v_lane3_tmp0.4s

	sha256h2	q_lane0_state1, q_lane0_tmp2, v_lane0_tmp0.4s
	sha256h2	q_lane1_state1, q_lane1_tmp2, v_lane1_tmp0.4s
	sha256h2	q_lane2_state1, q_lane2_tmp2, v_lane2_tmp0.4s
	sha256h2	q_lane3_state1, q_lane3_tmp2, v_lane3_tmp0.4s

	mov		x_offs, 64
	mov		x_tmp, x_digest_addr
	ld4		{v_lane0_tmp0.S-v_lane3_tmp0.S}[0], [x_tmp], x_offs
	ld4		{v_lane0_tmp0.S-v_lane3_tmp0.S}[1], [x_tmp], x_offs
	ld4		{v_lane0_tmp0.S-v_lane3_tmp0.S}[2], [x_tmp], x_offs
	ld4		{v_lane0_tmp0.S-v_lane3_tmp0.S}[3], [x_tmp], x_offs

	add		v_lane0_state0.4s, v_lane0_tmp0.4s, v_lane0_state0.4s
	add		v_lane1_state0.4s, v_lane1_tmp0.4s, v_lane1_state0.4s
	add		v_lane2_state0.4s, v_lane2_tmp0.4s, v_lane2_state0.4s
	add		v_lane3_state0.4s, v_lane3_tmp0.4s, v_lane3_state0.4s

	mov		x_offs, 64
	mov		x_tmp, x_digest_addr
	st4		{v_lane0_state0.S-v_lane3_state0.S}[0], [x_tmp], x_offs
	st4		{v_lane0_state0.S-v_lane3_state0.S}[1], [x_tmp], x_offs
	st4		{v_lane0_state0.S-v_lane3_state0.S}[2], [x_tmp], x_offs
	st4		{v_lane0_state0.S-v_lane3_state0.S}[3], [x_tmp], x_offs

	ldp		q_lane0_tmp2, q_lane1_tmp2, [sp, 64]
	ldp		q_lane2_tmp2, q_lane3_tmp2, [sp, 96]

	add		v_lane0_state1.4s, v_lane0_tmp2.4s, v_lane0_state1.4s
	add		v_lane1_state1.4s, v_lane1_tmp2.4s, v_lane1_state1.4s
	add		v_lane2_state1.4s, v_lane2_tmp2.4s, v_lane2_state1.4s
	add		v_lane3_state1.4s, v_lane3_tmp2.4s, v_lane3_state1.4s

	mov		x_offs, 64
	add		x_tmp, x_digest_addr, 256
	st4		{v_lane0_state1.S-v_lane3_state1.S}[0], [x_tmp], x_offs
	st4		{v_lane0_state1.S-v_lane3_state1.S}[1], [x_tmp], x_offs
	st4		{v_lane0_state1.S-v_lane3_state1.S}[2], [x_tmp], x_offs
	st4		{v_lane0_state1.S-v_lane3_state1.S}[3], [x_tmp], x_offs

	add		x_digest_addr, x_digest_addr, 16
	add		x_lane_offs, x_lane_offs, 4
	cmp		x_lane_offs, 16
	bne		.lane_loop

	add		x_input_data, x_input_data, 1024
	cmp		x_input_data, x_input_data_end
	bne		.start_loop

	ldp		d10, d11, [sp, 16]
	ldp		d12, d13, [sp, 32]
	ldp		d14, d15, [sp, 48]
	ldp		d8, d9, [sp], 192
.exit:
	ret
	.size	mh_sha256_block_ce, .-mh_sha256_block_ce

	.section	.rodata
	.align	4
	.set	.key_addr,. + 0
	.type	K, %object
	.size	K, 256
K:
	.word	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
	.word	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
	.word	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
	.word	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
	.word	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
	.word	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
	.word	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
	.word	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2

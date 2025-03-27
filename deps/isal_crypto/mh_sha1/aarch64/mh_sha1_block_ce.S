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
	.align	2
	.p2align 3,,7

/*
Macros
*/

.macro	declare_var_vector_reg name:req,reg:req
	\name\()_q	.req	q\reg
	\name\()_v	.req	v\reg
	\name\()_s	.req	s\reg
.endm



/*
Variable list
*/

	declare_var_vector_reg	lane0_msg_0, 0
	declare_var_vector_reg	lane1_msg_0, 1
	declare_var_vector_reg	lane2_msg_0, 2
	declare_var_vector_reg	lane3_msg_0, 3
	declare_var_vector_reg	lane0_msg_1, 4
	declare_var_vector_reg	lane1_msg_1, 5
	declare_var_vector_reg	lane2_msg_1, 6
	declare_var_vector_reg	lane3_msg_1, 7
	declare_var_vector_reg	lane0_msg_2, 8
	declare_var_vector_reg	lane1_msg_2, 9
	declare_var_vector_reg	lane2_msg_2,10
	declare_var_vector_reg	lane3_msg_2,11
	declare_var_vector_reg	lane0_msg_3,12
	declare_var_vector_reg	lane1_msg_3,13
	declare_var_vector_reg	lane2_msg_3,14
	declare_var_vector_reg	lane3_msg_3,15

	declare_var_vector_reg	lane0_abcd ,16
	declare_var_vector_reg	lane1_abcd ,17
	declare_var_vector_reg	lane2_abcd ,18
	declare_var_vector_reg	lane3_abcd ,19
	declare_var_vector_reg	lane0_tmp0 ,20
	declare_var_vector_reg	lane1_tmp0 ,21
	declare_var_vector_reg	lane2_tmp0 ,22
	declare_var_vector_reg	lane3_tmp0 ,23
	declare_var_vector_reg	lane0_tmp1 ,24
	declare_var_vector_reg	lane1_tmp1 ,25
	declare_var_vector_reg	lane2_tmp1 ,26
	declare_var_vector_reg	lane3_tmp1 ,27


	declare_var_vector_reg	e0	   ,28
	declare_var_vector_reg	e1	   ,29
	declare_var_vector_reg	key	   ,30
	declare_var_vector_reg	tmp	   ,31

	key_adr		.req	x4
	msg_adr		.req	x5
	block_cnt	.req	x6
	offs		.req	x7
	digest_adr	.req	x16
	tmp0_adr	.req	x17
	tmp1_adr	.req	x18

/**
maros for round 4-67
*/
.macro sha1_4_rounds inst:req,msg0:req,msg1:req,msg2:req,msg3:req,abcd:req,e0:req,tmp0:req,e1:req,tmp1:req
	sha1h	lane0_\tmp0\()_s, lane0_\abcd\()_s
	sha1h	lane1_\tmp0\()_s, lane1_\abcd\()_s
	sha1h	lane2_\tmp0\()_s, lane2_\abcd\()_s
	sha1h	lane3_\tmp0\()_s, lane3_\abcd\()_s
	mov	\e0\()_v.S[0],lane0_\tmp0\()_v.S[0]
	mov	\e0\()_v.S[1],lane1_\tmp0\()_v.S[0]
	mov	\e0\()_v.S[2],lane2_\tmp0\()_v.S[0]
	mov	\e0\()_v.S[3],lane3_\tmp0\()_v.S[0]
	mov	lane0_\tmp0\()_v.S[0],\e1\()_v.S[0]
	mov	lane1_\tmp0\()_v.S[0],\e1\()_v.S[1]
	mov	lane2_\tmp0\()_v.S[0],\e1\()_v.S[2]
	mov	lane3_\tmp0\()_v.S[0],\e1\()_v.S[3]
	\inst	lane0_\abcd\()_q,lane0_\tmp0\()_s,lane0_\tmp1\()_v.4s
	\inst	lane1_\abcd\()_q,lane1_\tmp0\()_s,lane1_\tmp1\()_v.4s
	\inst	lane2_\abcd\()_q,lane2_\tmp0\()_s,lane2_\tmp1\()_v.4s
	\inst	lane3_\abcd\()_q,lane3_\tmp0\()_s,lane3_\tmp1\()_v.4s
	ld1	{lane0_\tmp0\()_v.4s-lane3_\tmp0\()_v.4s},[\tmp0\()_adr]
	add 	lane0_\tmp1\()_v.4s,lane0_\msg3\()_v.4s,key_v.4s
	add 	lane1_\tmp1\()_v.4s,lane1_\msg3\()_v.4s,key_v.4s
	add 	lane2_\tmp1\()_v.4s,lane2_\msg3\()_v.4s,key_v.4s
	add 	lane3_\tmp1\()_v.4s,lane3_\msg3\()_v.4s,key_v.4s
	st1	{lane0_\tmp1\()_v.4s-lane3_\tmp1\()_v.4s},[\tmp1\()_adr]
	sha1su1	lane0_\msg0\()_v.4s,lane0_\msg3\()_v.4s
	sha1su1	lane1_\msg0\()_v.4s,lane1_\msg3\()_v.4s
	sha1su1	lane2_\msg0\()_v.4s,lane2_\msg3\()_v.4s
	sha1su1	lane3_\msg0\()_v.4s,lane3_\msg3\()_v.4s
	sha1su0	lane0_\msg1\()_v.4s,lane0_\msg2\()_v.4s,lane0_\msg3\()_v.4s
	sha1su0	lane1_\msg1\()_v.4s,lane1_\msg2\()_v.4s,lane1_\msg3\()_v.4s
	sha1su0	lane2_\msg1\()_v.4s,lane2_\msg2\()_v.4s,lane2_\msg3\()_v.4s
	sha1su0	lane3_\msg1\()_v.4s,lane3_\msg2\()_v.4s,lane3_\msg3\()_v.4s

.endm


/*
	void mh_sha1_block_ce(const uint8_t * input_data,
	      uint32_t digests[SHA1_DIGEST_WORDS][HASH_SEGS],
	      uint8_t frame_buffer[MH_SHA1_BLOCK_SIZE], uint32_t num_blocks)
*/
/*
Arguements list
*/
	input_data 	.req	x0
	digests		.req	x1
	frame_buffer	.req	x2
	num_blocks	.req	w3

	.global	mh_sha1_block_ce
	.type	mh_sha1_block_ce, %function
mh_sha1_block_ce:
	//save temp vector registers
	stp     d8, d9, [sp, -128]!

	stp     d10, d11, [sp, 16]
        stp     d12, d13, [sp, 32]
	stp     d14, d15, [sp, 48]
	mov	tmp0_adr,frame_buffer
	add	tmp1_adr,tmp0_adr,128


start_loop:
	mov	block_cnt,0
	mov	msg_adr,input_data
lane_loop:
	mov	offs,64
	adr	key_adr,KEY_0
	//load msg 0
	ld4	{lane0_msg_0_v.S-lane3_msg_0_v.S}[0],[msg_adr],offs
	ld4	{lane0_msg_0_v.S-lane3_msg_0_v.S}[1],[msg_adr],offs
	ld4	{lane0_msg_0_v.S-lane3_msg_0_v.S}[2],[msg_adr],offs
	ld4	{lane0_msg_0_v.S-lane3_msg_0_v.S}[3],[msg_adr],offs

	ld4	{lane0_msg_1_v.S-lane3_msg_1_v.S}[0],[msg_adr],offs
	ld4	{lane0_msg_1_v.S-lane3_msg_1_v.S}[1],[msg_adr],offs
	ld4	{lane0_msg_1_v.S-lane3_msg_1_v.S}[2],[msg_adr],offs
	ld4	{lane0_msg_1_v.S-lane3_msg_1_v.S}[3],[msg_adr],offs

	ld4	{lane0_msg_2_v.S-lane3_msg_2_v.S}[0],[msg_adr],offs
	ld4	{lane0_msg_2_v.S-lane3_msg_2_v.S}[1],[msg_adr],offs
	ld4	{lane0_msg_2_v.S-lane3_msg_2_v.S}[2],[msg_adr],offs
	ld4	{lane0_msg_2_v.S-lane3_msg_2_v.S}[3],[msg_adr],offs

	ld4	{lane0_msg_3_v.S-lane3_msg_3_v.S}[0],[msg_adr],offs
	ld4	{lane0_msg_3_v.S-lane3_msg_3_v.S}[1],[msg_adr],offs
	ld4	{lane0_msg_3_v.S-lane3_msg_3_v.S}[2],[msg_adr],offs
	ld4	{lane0_msg_3_v.S-lane3_msg_3_v.S}[3],[msg_adr],offs

	add	digest_adr,digests,block_cnt
	ld4	{lane0_abcd_v.S-lane3_abcd_v.S}[0],[digest_adr],offs
	ld4	{lane0_abcd_v.S-lane3_abcd_v.S}[1],[digest_adr],offs
	ld4	{lane0_abcd_v.S-lane3_abcd_v.S}[2],[digest_adr],offs
	ld4	{lane0_abcd_v.S-lane3_abcd_v.S}[3],[digest_adr],offs
	ldr	e0_q,[digest_adr]

	//load key_0
	ldr	key_q,[key_adr]

	rev32	lane0_msg_0_v.16b,lane0_msg_0_v.16b
	rev32	lane1_msg_0_v.16b,lane1_msg_0_v.16b
	rev32	lane2_msg_0_v.16b,lane2_msg_0_v.16b
	rev32	lane3_msg_0_v.16b,lane3_msg_0_v.16b
	rev32	lane0_msg_1_v.16b,lane0_msg_1_v.16b
	rev32	lane1_msg_1_v.16b,lane1_msg_1_v.16b
	rev32	lane2_msg_1_v.16b,lane2_msg_1_v.16b
	rev32	lane3_msg_1_v.16b,lane3_msg_1_v.16b
	rev32	lane0_msg_2_v.16b,lane0_msg_2_v.16b
	rev32	lane1_msg_2_v.16b,lane1_msg_2_v.16b
	rev32	lane2_msg_2_v.16b,lane2_msg_2_v.16b
	rev32	lane3_msg_2_v.16b,lane3_msg_2_v.16b
	rev32	lane0_msg_3_v.16b,lane0_msg_3_v.16b
	rev32	lane1_msg_3_v.16b,lane1_msg_3_v.16b
	rev32	lane2_msg_3_v.16b,lane2_msg_3_v.16b
	rev32	lane3_msg_3_v.16b,lane3_msg_3_v.16b

	add	lane0_tmp1_v.4s,lane0_msg_1_v.4s,key_v.4s
	add	lane1_tmp1_v.4s,lane1_msg_1_v.4s,key_v.4s
	add	lane2_tmp1_v.4s,lane2_msg_1_v.4s,key_v.4s
	add	lane3_tmp1_v.4s,lane3_msg_1_v.4s,key_v.4s
	st1	{lane0_tmp1_v.4s-lane3_tmp1_v.4s},[tmp1_adr]

	add	lane0_tmp0_v.4s,lane0_msg_0_v.4s,key_v.4s
	add	lane1_tmp0_v.4s,lane1_msg_0_v.4s,key_v.4s
	add	lane2_tmp0_v.4s,lane2_msg_0_v.4s,key_v.4s
	add	lane3_tmp0_v.4s,lane3_msg_0_v.4s,key_v.4s

	/* rounds 0-3 */
	sha1h	lane0_tmp1_s,lane0_abcd_s
	sha1h	lane1_tmp1_s,lane1_abcd_s
	sha1h	lane2_tmp1_s,lane2_abcd_s
	sha1h	lane3_tmp1_s,lane3_abcd_s
	mov	e1_v.S[0],lane0_tmp1_v.S[0]
	mov	e1_v.S[1],lane1_tmp1_v.S[0]
	mov	e1_v.S[2],lane2_tmp1_v.S[0]
	mov	e1_v.S[3],lane3_tmp1_v.S[0]
	mov	lane0_tmp1_v.S[0],e0_v.S[0]
	mov	lane1_tmp1_v.S[0],e0_v.S[1]
	mov	lane2_tmp1_v.S[0],e0_v.S[2]
	mov	lane3_tmp1_v.S[0],e0_v.S[3]
	sha1c	lane0_abcd_q,lane0_tmp1_s,lane0_tmp0_v.4s
	sha1c	lane1_abcd_q,lane1_tmp1_s,lane1_tmp0_v.4s
	sha1c	lane2_abcd_q,lane2_tmp1_s,lane2_tmp0_v.4s
	sha1c	lane3_abcd_q,lane3_tmp1_s,lane3_tmp0_v.4s
	ld1	{lane0_tmp1_v.4s-lane3_tmp1_v.4s},[tmp1_adr]
	add	lane0_tmp0_v.4s,lane0_msg_2_v.4s,key_v.4s
	sha1su0	lane0_msg_0_v.4s,lane0_msg_1_v.4s,lane0_msg_2_v.4s
	add	lane1_tmp0_v.4s,lane1_msg_2_v.4s,key_v.4s
	sha1su0	lane1_msg_0_v.4s,lane1_msg_1_v.4s,lane1_msg_2_v.4s
	add	lane2_tmp0_v.4s,lane2_msg_2_v.4s,key_v.4s
	sha1su0	lane2_msg_0_v.4s,lane2_msg_1_v.4s,lane2_msg_2_v.4s
	add	lane3_tmp0_v.4s,lane3_msg_2_v.4s,key_v.4s
	sha1su0	lane3_msg_0_v.4s,lane3_msg_1_v.4s,lane3_msg_2_v.4s
	st1	{lane0_tmp0_v.4s-lane3_tmp0_v.4s},[tmp0_adr]

	sha1_4_rounds	sha1c,msg_0,msg_1,msg_2,msg_3,abcd,e0,tmp0,e1,tmp1     /* rounds 4-7 */
	sha1_4_rounds	sha1c,msg_1,msg_2,msg_3,msg_0,abcd,e1,tmp1,e0,tmp0


	adr	key_adr,KEY_1
	ldr	key_q,[key_adr]
	sha1_4_rounds	sha1c,msg_2,msg_3,msg_0,msg_1,abcd,e0,tmp0,e1,tmp1     /* rounds 12-15 */
	sha1_4_rounds	sha1c,msg_3,msg_0,msg_1,msg_2,abcd,e1,tmp1,e0,tmp0
	sha1_4_rounds	sha1p,msg_0,msg_1,msg_2,msg_3,abcd,e0,tmp0,e1,tmp1     /* rounds 20-23 */
	sha1_4_rounds	sha1p,msg_1,msg_2,msg_3,msg_0,abcd,e1,tmp1,e0,tmp0
	sha1_4_rounds	sha1p,msg_2,msg_3,msg_0,msg_1,abcd,e0,tmp0,e1,tmp1

	adr	key_adr,KEY_2
	ldr	key_q,[key_adr]
	sha1_4_rounds	sha1p,msg_3,msg_0,msg_1,msg_2,abcd,e1,tmp1,e0,tmp0
	sha1_4_rounds	sha1p,msg_0,msg_1,msg_2,msg_3,abcd,e0,tmp0,e1,tmp1     /* rounds 36-39 */
	sha1_4_rounds	sha1m,msg_1,msg_2,msg_3,msg_0,abcd,e1,tmp1,e0,tmp0
	sha1_4_rounds	sha1m,msg_2,msg_3,msg_0,msg_1,abcd,e0,tmp0,e1,tmp1
	sha1_4_rounds	sha1m,msg_3,msg_0,msg_1,msg_2,abcd,e1,tmp1,e0,tmp0

	adr	key_adr,KEY_3
	ldr	key_q,[key_adr]
	sha1_4_rounds	sha1m,msg_0,msg_1,msg_2,msg_3,abcd,e0,tmp0,e1,tmp1     /* rounds 52-55 */
	sha1_4_rounds	sha1m,msg_1,msg_2,msg_3,msg_0,abcd,e1,tmp1,e0,tmp0
	sha1_4_rounds	sha1p,msg_2,msg_3,msg_0,msg_1,abcd,e0,tmp0,e1,tmp1
	sha1_4_rounds	sha1p,msg_3,msg_0,msg_1,msg_2,abcd,e1,tmp1,e0,tmp0

	//msg2 and msg1 are free
	mov	lane0_msg_2_v.S[0],e1_v.S[0]
	mov	lane1_msg_2_v.S[0],e1_v.S[1]
	mov	lane2_msg_2_v.S[0],e1_v.S[2]
	mov	lane3_msg_2_v.S[0],e1_v.S[3]

	/* rounds 68-71 */
	sha1h	lane0_msg_1_s,lane0_abcd_s
	sha1h	lane1_msg_1_s,lane1_abcd_s
	sha1h	lane2_msg_1_s,lane2_abcd_s
	sha1h	lane3_msg_1_s,lane3_abcd_s
	sha1p	lane0_abcd_q,lane0_msg_2_s,lane0_tmp1_v.4s
	sha1p	lane1_abcd_q,lane1_msg_2_s,lane1_tmp1_v.4s
	sha1p	lane2_abcd_q,lane2_msg_2_s,lane2_tmp1_v.4s
	sha1p	lane3_abcd_q,lane3_msg_2_s,lane3_tmp1_v.4s
	add	lane0_tmp1_v.4s,lane0_msg_3_v.4s,key_v.4s
	add	lane1_tmp1_v.4s,lane1_msg_3_v.4s,key_v.4s
	add	lane2_tmp1_v.4s,lane2_msg_3_v.4s,key_v.4s
	add	lane3_tmp1_v.4s,lane3_msg_3_v.4s,key_v.4s
	sha1su1	lane0_msg_0_v.4s,lane0_msg_3_v.4s
	sha1su1	lane1_msg_0_v.4s,lane1_msg_3_v.4s
	sha1su1	lane2_msg_0_v.4s,lane2_msg_3_v.4s
	sha1su1	lane3_msg_0_v.4s,lane3_msg_3_v.4s

	/* rounds 72-75 */
	sha1h	lane0_msg_2_s,lane0_abcd_s
	sha1h	lane1_msg_2_s,lane1_abcd_s
	sha1h	lane2_msg_2_s,lane2_abcd_s
	sha1h	lane3_msg_2_s,lane3_abcd_s
	sha1p	lane0_abcd_q,lane0_msg_1_s,lane0_tmp0_v.4s
	sha1p	lane1_abcd_q,lane1_msg_1_s,lane1_tmp0_v.4s
	sha1p	lane2_abcd_q,lane2_msg_1_s,lane2_tmp0_v.4s
	sha1p	lane3_abcd_q,lane3_msg_1_s,lane3_tmp0_v.4s

	/* rounds 76-79 */
	sha1h	lane0_msg_1_s,lane0_abcd_s
	sha1h	lane1_msg_1_s,lane1_abcd_s
	sha1h	lane2_msg_1_s,lane2_abcd_s
	sha1h	lane3_msg_1_s,lane3_abcd_s
	sha1p	lane0_abcd_q,lane0_msg_2_s,lane0_tmp1_v.4s
	sha1p	lane1_abcd_q,lane1_msg_2_s,lane1_tmp1_v.4s
	sha1p	lane2_abcd_q,lane2_msg_2_s,lane2_tmp1_v.4s
	sha1p	lane3_abcd_q,lane3_msg_2_s,lane3_tmp1_v.4s
	add	digest_adr,digests,block_cnt
	ld4	{lane0_msg_0_v.S-lane3_msg_0_v.S}[0],[digest_adr],offs
	ld4	{lane0_msg_0_v.S-lane3_msg_0_v.S}[1],[digest_adr],offs
	ld4	{lane0_msg_0_v.S-lane3_msg_0_v.S}[2],[digest_adr],offs
	ld4	{lane0_msg_0_v.S-lane3_msg_0_v.S}[3],[digest_adr],offs
	ld4	{lane0_msg_3_v.S-lane3_msg_3_v.S}[0],[digest_adr]

	add	lane0_abcd_v.4S,lane0_abcd_v.4S,lane0_msg_0_v.4S
	add	lane1_abcd_v.4S,lane1_abcd_v.4S,lane1_msg_0_v.4S
	add	lane2_abcd_v.4S,lane2_abcd_v.4S,lane2_msg_0_v.4S
	add	lane3_abcd_v.4S,lane3_abcd_v.4S,lane3_msg_0_v.4S

	add	lane0_msg_1_v.4S,lane0_msg_1_v.4S,lane0_msg_3_v.4S
        add	lane1_msg_1_v.4S,lane1_msg_1_v.4S,lane1_msg_3_v.4S
        add	lane2_msg_1_v.4S,lane2_msg_1_v.4S,lane2_msg_3_v.4S
        add	lane3_msg_1_v.4S,lane3_msg_1_v.4S,lane3_msg_3_v.4S

	add	digest_adr,digests,block_cnt
	st4	{lane0_abcd_v.S-lane3_abcd_v.S}[0],[digest_adr],offs
	st4	{lane0_abcd_v.S-lane3_abcd_v.S}[1],[digest_adr],offs
	st4	{lane0_abcd_v.S-lane3_abcd_v.S}[2],[digest_adr],offs
	st4	{lane0_abcd_v.S-lane3_abcd_v.S}[3],[digest_adr],offs
	st4	{lane0_msg_1_v.S-lane3_msg_1_v.S}[0],[digest_adr]

	add	block_cnt,block_cnt,16
	cmp	block_cnt,64
	add	msg_adr,input_data,block_cnt
	add	digest_adr,digests,block_cnt
	bcc	lane_loop

	subs	num_blocks,num_blocks,1
	add	input_data,input_data,1024
	bhi	start_loop
exit_func:
	//restore temp register
	ldp     d10, d11, [sp, 16]
        ldp     d12, d13, [sp, 32]
        ldp     d14, d15, [sp, 48]
        ldp     d8, d9, [sp], 128
	ret

	.size	mh_sha1_block_ce, .-mh_sha1_block_ce
	.section	.rodata.cst16,"aM",@progbits,16
	.align	4
KEY_0:
	.word	0x5a827999
	.word	0x5a827999
	.word	0x5a827999
	.word	0x5a827999
KEY_1:
	.word	0x6ed9eba1
	.word	0x6ed9eba1
	.word	0x6ed9eba1
	.word	0x6ed9eba1
KEY_2:
	.word	0x8f1bbcdc
	.word	0x8f1bbcdc
	.word	0x8f1bbcdc
	.word	0x8f1bbcdc
KEY_3:
	.word	0xca62c1d6
	.word	0xca62c1d6
	.word	0xca62c1d6
	.word	0xca62c1d6

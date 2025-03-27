/**********************************************************************
  Copyright(c) 2019 Arm Corporation All rights reserved.

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

/**
maros for round 4-67
*/
.macro sha1_4_rounds inst:req,msg0:req,msg1:req,msg2:req,msg3:req,abcd:req,e0:req,tmp0:req,e1:req,tmp1:req,k:req
	sha1h	l0_\e0\()_s, l0_\abcd\()_s
	sha1h	l1_\e0\()_s, l1_\abcd\()_s

	\inst	l0_\abcd\()_q,l0_\e1\()_s,l0_\tmp1\()_v.4s
	\inst	l1_\abcd\()_q,l1_\e1\()_s,l1_\tmp1\()_v.4s

	add 	l0_\tmp1\()_v.4s,l0_\msg3\()_v.4s,\k\()_v.4s
	add 	l1_\tmp1\()_v.4s,l1_\msg3\()_v.4s,\k\()_v.4s

	sha1su1	l0_\msg0\()_v.4s,l0_\msg3\()_v.4s
	sha1su1	l1_\msg0\()_v.4s,l1_\msg3\()_v.4s

	sha1su0	l0_\msg1\()_v.4s,l0_\msg2\()_v.4s,l0_\msg3\()_v.4s
	sha1su0	l1_\msg1\()_v.4s,l1_\msg2\()_v.4s,l1_\msg3\()_v.4s
.endm


/*
Variable list
*/

	declare_var_vector_reg	key_0,28
	declare_var_vector_reg	key_1,29
	declare_var_vector_reg	key_2,30
	declare_var_vector_reg	key_3,31


/*
lane variables
*/
	declare_var_vector_reg	l0_abcd,0
	declare_var_vector_reg	l0_e0,1
	declare_var_vector_reg	l0_e1,2
	declare_var_vector_reg	l0_abcd_saved,3
	declare_var_vector_reg	l0_e0_saved,4
	declare_var_vector_reg	l0_tmp_0,5
	declare_var_vector_reg	l0_tmp_1,6
	declare_var_vector_reg	l0_msg_0,16
	declare_var_vector_reg	l0_msg_1,17
	declare_var_vector_reg	l0_msg_2,18
	declare_var_vector_reg	l0_msg_3,19

	declare_var_vector_reg	l1_abcd,7
	declare_var_vector_reg	l1_e0,8
	declare_var_vector_reg	l1_e1,9
	declare_var_vector_reg	l1_abcd_saved,24
	declare_var_vector_reg	l1_e0_saved,25
	declare_var_vector_reg	l1_tmp_0,26
	declare_var_vector_reg	l1_tmp_1,27
	declare_var_vector_reg	l1_msg_0,20
	declare_var_vector_reg	l1_msg_1,21
	declare_var_vector_reg	l1_msg_2,22
	declare_var_vector_reg	l1_msg_3,23

/*
	void sha1_mb_ce_x2(SHA1_JOB * job_0, SHA1_JOB * job_1,int len);
*/
	l0_job .req x0
	l1_job .req x1
	len .req w2

	l0_data		.req	x3
	l1_data		.req	x4
	tmp		.req	x5
	.global	sha1_mb_ce_x2
	.type	sha1_mb_ce_x2, %function
sha1_mb_ce_x2:
	//push 	d8,d9 to stack
	stp	d8, d9, [sp, -256]!

	adr	tmp, KEY
	ld1	{key_0_v.4s-key_3_v.4s},[tmp]
	ldr	l0_data,	[l0_job]
	ldr	l1_data,	[l1_job]
	ldr	l0_abcd_q,	[l0_job, 64]
	ldr	l0_e0_s,	[l0_job, 80]
	ldr	l1_abcd_q,	[l1_job, 64]
	ldr	l1_e0_s,	[l1_job, 80]

start_loop:

	//load msgs
	ld1	{l0_msg_0_v.4s-l0_msg_3_v.4s},[l0_data]
	ld1	{l1_msg_0_v.4s-l1_msg_3_v.4s},[l1_data]

	//adjust loop parameter
	add	l0_data,l0_data,64
	add	l1_data,l1_data,64
	sub	len, len, #1
	cmp	len, 0
	//backup digest
	mov	l0_abcd_saved_v.16b,	l0_abcd_v.16b
	mov	l0_e0_saved_v.16b,	l0_e0_v.16b
	mov	l1_abcd_saved_v.16b,	l1_abcd_v.16b
	mov	l1_e0_saved_v.16b,	l1_e0_v.16b

	rev32	l0_msg_0_v.16b,	l0_msg_0_v.16b
	rev32	l0_msg_1_v.16b,	l0_msg_1_v.16b
	add	l0_tmp_0_v.4s,	l0_msg_0_v.4s,	key_0_v.4s
	rev32	l0_msg_2_v.16b,	l0_msg_2_v.16b
	add	l0_tmp_1_v.4s,	l0_msg_1_v.4s,	key_0_v.4s
	rev32	l0_msg_3_v.16b,	l0_msg_3_v.16b

	rev32	l1_msg_0_v.16b,	l1_msg_0_v.16b
	rev32	l1_msg_1_v.16b,	l1_msg_1_v.16b
	add	l1_tmp_0_v.4s,	l1_msg_0_v.4s,	key_0_v.4s
	rev32	l1_msg_2_v.16b,	l1_msg_2_v.16b
	add	l1_tmp_1_v.4s,	l1_msg_1_v.4s,	key_0_v.4s
	rev32	l1_msg_3_v.16b,	l1_msg_3_v.16b

	/* rounds 0-3 */
	sha1h	l0_e1_s,	l0_abcd_s
	sha1c	l0_abcd_q,	l0_e0_s,	l0_tmp_0_v.4s
	add	l0_tmp_0_v.4s,	l0_msg_2_v.4s,	key_0_v.4s
	sha1su0	l0_msg_0_v.4s,	l0_msg_1_v.4s,	l0_msg_2_v.4s

	sha1h	l1_e1_s,	l1_abcd_s
	sha1c	l1_abcd_q,	l1_e0_s,	l1_tmp_0_v.4s
	add	l1_tmp_0_v.4s,	l1_msg_2_v.4s,	key_0_v.4s
	sha1su0	l1_msg_0_v.4s,	l1_msg_1_v.4s,	l1_msg_2_v.4s

	sha1_4_rounds	sha1c,msg_0,msg_1,msg_2,msg_3,abcd,e0,tmp_0,e1,tmp_1,key_0     /* rounds 4-7 */
	sha1_4_rounds	sha1c,msg_1,msg_2,msg_3,msg_0,abcd,e1,tmp_1,e0,tmp_0,key_0
	sha1_4_rounds	sha1c,msg_2,msg_3,msg_0,msg_1,abcd,e0,tmp_0,e1,tmp_1,key_1     /* rounds 12-15 */
	sha1_4_rounds	sha1c,msg_3,msg_0,msg_1,msg_2,abcd,e1,tmp_1,e0,tmp_0,key_1
	sha1_4_rounds	sha1p,msg_0,msg_1,msg_2,msg_3,abcd,e0,tmp_0,e1,tmp_1,key_1     /* rounds 20-23 */
	sha1_4_rounds	sha1p,msg_1,msg_2,msg_3,msg_0,abcd,e1,tmp_1,e0,tmp_0,key_1
	sha1_4_rounds	sha1p,msg_2,msg_3,msg_0,msg_1,abcd,e0,tmp_0,e1,tmp_1,key_1
	sha1_4_rounds	sha1p,msg_3,msg_0,msg_1,msg_2,abcd,e1,tmp_1,e0,tmp_0,key_2
	sha1_4_rounds	sha1p,msg_0,msg_1,msg_2,msg_3,abcd,e0,tmp_0,e1,tmp_1,key_2     /* rounds 36-39 */
	sha1_4_rounds	sha1m,msg_1,msg_2,msg_3,msg_0,abcd,e1,tmp_1,e0,tmp_0,key_2
	sha1_4_rounds	sha1m,msg_2,msg_3,msg_0,msg_1,abcd,e0,tmp_0,e1,tmp_1,key_2
	sha1_4_rounds	sha1m,msg_3,msg_0,msg_1,msg_2,abcd,e1,tmp_1,e0,tmp_0,key_2
	sha1_4_rounds	sha1m,msg_0,msg_1,msg_2,msg_3,abcd,e0,tmp_0,e1,tmp_1,key_3     /* rounds 52-55 */
	sha1_4_rounds	sha1m,msg_1,msg_2,msg_3,msg_0,abcd,e1,tmp_1,e0,tmp_0,key_3
	sha1_4_rounds	sha1p,msg_2,msg_3,msg_0,msg_1,abcd,e0,tmp_0,e1,tmp_1,key_3
	sha1_4_rounds	sha1p,msg_3,msg_0,msg_1,msg_2,abcd,e1,tmp_1,e0,tmp_0,key_3

	/* rounds 68-71 */
	sha1h	l0_e0_s,	l0_abcd_s
	sha1p	l0_abcd_q,	l0_e1_s,	l0_tmp_1_v.4s
	add	l0_tmp_1_v.4s,	l0_msg_3_v.4s,	key_3_v.4s
	sha1su1	l0_msg_0_v.4s,	l0_msg_3_v.4s

	sha1h	l1_e0_s,	l1_abcd_s
	sha1p	l1_abcd_q,	l1_e1_s,	l1_tmp_1_v.4s
	add	l1_tmp_1_v.4s,	l1_msg_3_v.4s,	key_3_v.4s
	sha1su1	l1_msg_0_v.4s,	l1_msg_3_v.4s

	/* rounds 72-75 */
	sha1h   l0_e1_s,	l0_abcd_s
	sha1p   l0_abcd_q,	l0_e0_s,	l0_tmp_0_v.4s

	sha1h   l1_e1_s,	l1_abcd_s
	sha1p   l1_abcd_q,	l1_e0_s,	l1_tmp_0_v.4s

	/* rounds 76-79 */
	sha1h   l0_e0_s,	l0_abcd_s
	sha1p   l0_abcd_q,	l0_e1_s,	l0_tmp_1_v.4s

	sha1h   l1_e0_s,	l1_abcd_s
	sha1p   l1_abcd_q,	l1_e1_s,	l1_tmp_1_v.4s



	add     l0_abcd_v.4s,	l0_abcd_v.4s,	l0_abcd_saved_v.4s
	add     l0_e0_v.2s,	l0_e0_v.2s,	l0_e0_saved_v.2s
	add     l1_abcd_v.4s,	l1_abcd_v.4s,	l1_abcd_saved_v.4s
	add     l1_e0_v.2s,	l1_e0_v.2s,	l1_e0_saved_v.2s




	bgt	start_loop

	str	l0_abcd_q,	[l0_job, 64]
	str	l0_e0_s, 	[l0_job, 80]


	str	l1_abcd_q,	[l1_job, 64]
	str	l1_e0_s, 	[l1_job, 80]

	//pop d8,d9 from stack
	ldp	d8, d9, [sp], 256
	ret

	.size	sha1_mb_ce_x2, .-sha1_mb_ce_x2
	.section	.rodata.cst16,"aM",@progbits,16
	.align	4
KEY:
	.word	0x5a827999
	.word	0x5a827999
	.word	0x5a827999
	.word	0x5a827999
	.word	0x6ed9eba1
	.word	0x6ed9eba1
	.word	0x6ed9eba1
	.word	0x6ed9eba1
	.word	0x8f1bbcdc
	.word	0x8f1bbcdc
	.word	0x8f1bbcdc
	.word	0x8f1bbcdc
	.word	0xca62c1d6
	.word	0xca62c1d6
	.word	0xca62c1d6
	.word	0xca62c1d6

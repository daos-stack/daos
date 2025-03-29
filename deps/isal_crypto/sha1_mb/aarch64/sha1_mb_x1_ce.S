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
	sha1h	\e0\()_s, \abcd\()_s
	\inst	\abcd\()_q,\e1\()_s,\tmp1\()_v.4s
	add 	\tmp1\()_v.4s,\msg3\()_v.4s,\k\()_v.4s
	sha1su1	\msg0\()_v.4s,\msg3\()_v.4s
	sha1su0	\msg1\()_v.4s,\msg2\()_v.4s,\msg3\()_v.4s
.endm


/*
Variable list
*/

	declare_var_vector_reg	key_0,28
	declare_var_vector_reg	key_1,29
	declare_var_vector_reg	key_2,30
	declare_var_vector_reg	key_3,31


/*
digest variables
*/
	declare_var_vector_reg	abcd,0
	declare_var_vector_reg	e0,1
	declare_var_vector_reg	e1,2
	declare_var_vector_reg	abcd_saved,3
	declare_var_vector_reg	e0_saved,4
/*
Message variables
*/
	declare_var_vector_reg	msg_0,16
	declare_var_vector_reg	msg_1,17
	declare_var_vector_reg	msg_2,18
	declare_var_vector_reg	msg_3,19
/*
Temporay variables
*/
	declare_var_vector_reg	tmp_0,5
	declare_var_vector_reg	tmp_1,6

/*
	void sha1_mb_ce_x1(SHA1_JOB * job, int len);
*/
/*
Arguements list
*/
	job 	.req	x0
	len	.req	w1
	data	.req	x2
	tmp	.req	x3
	.global	sha1_mb_ce_x1
	.type	sha1_mb_ce_x1, %function
sha1_mb_ce_x1:
	ldr	data, [job]
	ldr	abcd_q, [job, 64]
	ldr	e0_s, [job, 80]
	adr	tmp, KEY
	ld1	{key_0_v.4s-key_3_v.4s},[tmp]

start_loop:

	//load msgs
	ld1	{msg_0_v.4s-msg_3_v.4s},[data]

	//adjust loop parameter
	add	data,data,64
	sub	len, len, #1
	cmp	len, 0
	//backup digest
	mov	abcd_saved_v.16b,abcd_v.16b
	mov	e0_saved_v.16b,e0_v.16b

	rev32	msg_0_v.16b,msg_0_v.16b
	rev32	msg_1_v.16b,msg_1_v.16b
	add	tmp_0_v.4s,msg_0_v.4s,key_0_v.4s
	rev32	msg_2_v.16b,msg_2_v.16b
	add	tmp_1_v.4s,msg_1_v.4s,key_0_v.4s
	rev32	msg_3_v.16b,msg_3_v.16b

	/* rounds 0-3 */
	sha1h	e1_s,abcd_s
	sha1c	abcd_q,e0_s,tmp_0_v.4s
	add	tmp_0_v.4s,msg_2_v.4s,key_0_v.4s
	sha1su0	msg_0_v.4s,msg_1_v.4s,msg_2_v.4s

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
	sha1h   e0_s,abcd_s
	sha1p   abcd_q,e1_s,tmp_1_v.4s
	add     tmp_1_v.4s,msg_3_v.4s,key_3_v.4s
	sha1su1 msg_0_v.4s,msg_3_v.4s

	/* rounds 72-75 */
	sha1h   e1_s,abcd_s
	sha1p   abcd_q,e0_s,tmp_0_v.4s

	/* rounds 76-79 */
	sha1h   e0_s,abcd_s
	sha1p   abcd_q,e1_s,tmp_1_v.4s



	add     abcd_v.4s,abcd_v.4s,abcd_saved_v.4s
	add     e0_v.2s,e0_v.2s,e0_saved_v.2s


	bgt	start_loop
	str	abcd_q,	[job, 64]
	str	e0_s, 	[job, 80]

	ret

	.size	sha1_mb_ce_x1, .-sha1_mb_ce_x1
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

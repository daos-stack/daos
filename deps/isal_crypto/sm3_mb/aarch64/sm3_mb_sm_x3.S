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
	//dsdf
	.arch armv8.2-a+sm4
	.text
	.align	2
	.p2align 3,,7

.macro	declare_var_vector_reg name:req,reg:req
	q\name\()	.req	q\reg
	v\name\()	.req	v\reg
	s\name\()	.req	s\reg
.endm

.macro	do_ext	job,arg0,arg1,arg2,arg3
	ext	vjob\job\()_\arg0\().16b,vjob\job\()_\arg1\().16b,vjob\job\()_\arg2\().16b,\arg3
.endm
.macro	do_sm3partw1	job,msg4,msg0,msg3
	sm3partw1	vjob\job\()_\msg4\().4s, vjob\job\()_\msg0\().4s, vjob\job\()_\msg3\().4s
.endm
.macro	do_sm3partw2	job,msg4,tmp1,tmp0
	sm3partw2	vjob\job\()_\msg4\().4s, vjob\job\()_\tmp1\().4s, vjob\job\()_\tmp0\().4s
.endm

.macro message_expand	msg0:req,msg1:req,msg2:req,msg3:req,msg4:req,tmp0:req,tmp1:req
	.irp	j,0,1,2
		do_ext	\j,\msg4,\msg1,\msg2,#12
	.endr
	.irp	j,0,1,2
		do_ext	\j,\tmp0,\msg0,\msg1,#12
	.endr
	.irp	j,0,1,2
		do_ext	\j,\tmp1,\msg2,\msg3,#8
	.endr

	.irp	j,0,1,2
		do_sm3partw1	\j,\msg4, \msg0, \msg3
	.endr
	.irp	j,0,1,2
		do_sm3partw2	\j,\msg4, \tmp1, \tmp0
	.endr

.endm

.macro do_eor	job,arg0,arg1,arg2
	eor	v\job\()_\arg0\().16b,v\job\()_\arg1\().16b,v\job\()_\arg2\().16b
.endm
.macro	do_sm3ss1	job,tmp1,dig0,dig1,const
	sm3ss1		v\job\()_\tmp1\().4s, v\job\()_\dig0\().4s, v\job\()_\dig1\().4s, v\const\().4s
.endm

.macro do_sm3tt1	job,ab,dig0,tmp1,tmp0,lane
	sm3tt1\ab	v\job\()_\dig0\().4s, v\job\()_\tmp1\().4s, v\job\()_\tmp0\().4s[\lane]

.endm
.macro do_sm3tt2	job,ab,dig1,tmp1,msg0,lane
	sm3tt2\ab	v\job\()_\dig1\().4s, v\job\()_\tmp1\().4s, v\job\()_\msg0\().4s[\lane]
.endm
.macro do_ld_backup_digest	job
	ldp	qjob\job\()_backup_dig0,qjob\job\()_backup_dig1,[sp,job\job\()_dig_off]
.endm

.macro	do_st_digest	job
	stp	qjob\job\()_dig0,qjob\job\()_dig1,[job\job\()_digest]
.endm
.macro	quad_round	ab:req,const:req,dig0:req,dig1:req,msg0:req,msg1:req,tmp0:req,tmp1:req,load_digest
	.irp	j,0,1,2
		do_eor	job\j,\tmp0,\msg0,\msg1
		.ifnb	\load_digest
			do_ld_backup_digest \j
		.endif
	.endr
	.irp	lane,0,1,2,3
		.irp	j,0,1,2
			do_sm3ss1	job\j,\tmp1,\dig0,\dig1,\const
		.endr

		ext		v\const\().16b,v\const\().16b,v\const\().16b,12
		.irp	j,0,1,2
			do_sm3tt1	job\j,\ab,\dig0,\tmp1,\tmp0,\lane
		.endr
		.irp	j,0,1,2
			do_sm3tt2	job\j,\ab,\dig1,\tmp1,\msg0,\lane
		.endr

	.endr
.endm

.macro quad_round_expand	ab:req,const:req,dig0:req,dig1:req,msg0:req,msg1:req,msg2:req,msg3:req,msg4:req,tmp0:req,tmp1:req
	message_expand	\msg0,\msg1,\msg2,\msg3,\msg4,\tmp0,\tmp1
	quad_round	\ab,\const,\dig0,\dig1,\msg0,\msg1,\tmp0,\tmp1
.endm

/*
	Variables
*/
	job0		.req	x0
	job1		.req	x1
	job2		.req	x2
	len		.req	x3

	job0_data	.req	x4
	job1_data	.req	x5
	job2_data	.req	x6
	job0_digest	.req	x0
	job1_digest	.req	x1
	job2_digest	.req	x2

	const_adr	.req	x7
	end_ptr		.req	x3

	declare_var_vector_reg	job0_msg0,          0
	declare_var_vector_reg	job0_msg1,          1
	declare_var_vector_reg	job0_msg2,          2
	declare_var_vector_reg	job0_msg3,          3
	declare_var_vector_reg	job0_msg4,          4
	declare_var_vector_reg	job0_dig0,          5
	declare_var_vector_reg	job0_dig1,          6
	declare_var_vector_reg	job0_tmp0,          7
	declare_var_vector_reg	job0_tmp1,          8
	.set			job0_dig_off,      64
	declare_var_vector_reg	job0_backup_dig0,   2
	declare_var_vector_reg	job0_backup_dig1,   3

	declare_var_vector_reg	job1_msg0,          9
	declare_var_vector_reg	job1_msg1,         10
	declare_var_vector_reg	job1_msg2,         11
	declare_var_vector_reg	job1_msg3,         12
	declare_var_vector_reg	job1_msg4,         13
	declare_var_vector_reg	job1_dig0,         14
	declare_var_vector_reg	job1_dig1,         15
	declare_var_vector_reg	job1_tmp0,         16
	declare_var_vector_reg	job1_tmp1,         17
	.set			job1_dig_off,      96
	declare_var_vector_reg	job1_backup_dig0,  11
	declare_var_vector_reg	job1_backup_dig1,  12

	declare_var_vector_reg	job2_msg0,         18
	declare_var_vector_reg	job2_msg1,         19
	declare_var_vector_reg	job2_msg2,         20
	declare_var_vector_reg	job2_msg3,         21
	declare_var_vector_reg	job2_msg4,         22
	declare_var_vector_reg	job2_dig0,         23
	declare_var_vector_reg	job2_dig1,         24
	declare_var_vector_reg	job2_tmp0,         25
	declare_var_vector_reg	job2_tmp1,         26
	.set			job2_dig_off,     128
	declare_var_vector_reg	job2_backup_dig0,  20
	declare_var_vector_reg	job2_backup_dig1,  21


	declare_var_vector_reg	const0,            27
	declare_var_vector_reg	const1,            28
	declare_var_vector_reg	const2,            29
	declare_var_vector_reg	const3,            30
	declare_var_vector_reg	const4,            27
	declare_var_vector_reg	const5,            28
	declare_var_vector_reg	const6,            29
	declare_var_vector_reg	const7,            30
	declare_var_vector_reg	const8,            27
	declare_var_vector_reg	const9,            28
	declare_var_vector_reg	const10,           29
	declare_var_vector_reg	const11,           30

.macro do_rev32_msg	job:req,msg:req
	rev32	v\job\()_\msg\().16b,v\job\()_\msg\().16b
.endm
.macro do_rev32_job	job:req
	.irp	m,0,1,2,3
	do_rev32_msg	\job,msg\m
	.endr
.endm
.macro rev32_msgs
	.irp	j,0,1,2
	do_rev32_job	job\j
	.endr
.endm

.macro do_rev64		job,regd,regn
	rev64		vjob\job\()_\regd\().16b,vjob\job\()_\regd\().16b
.endm

	.global	sm3_mb_sm_x3
	.type	sm3_mb_sm_x3, %function
sm3_mb_sm_x3:
	//push d8~d15
	stp 	d8,d9,[sp,-192]!
	stp 	d10,d11,[sp,16]
	stp 	d12,d13,[sp,32]
	stp 	d14,d15,[sp,48]


	adrp	const_adr,.consts
	ldr	job0_data, [job0],64
	add	const_adr,const_adr,:lo12:.consts
	ldr	job1_data, [job1],64
	ldr	job2_data, [job2],64

	ldp	qjob0_dig0,qjob0_dig1,[job0_digest]
	ldp	qjob1_dig0,qjob1_dig1,[job1_digest]
	ldp	qjob2_dig0,qjob2_dig1,[job2_digest]
	ld1	{vconst0.16b-vconst3.16b},[const_adr]
	add	end_ptr,job0_data,len,lsl 6

	//rev128
	.irp	j,0,1,2
		do_ext		\j,dig0,dig0,dig0,#8
		do_ext		\j,dig1,dig1,dig1,#8
		do_rev64	\j,dig0,dig0
		do_rev64	\j,dig1,dig1
	.endr





start_loop:

	ld1	{vjob0_msg0.16b-vjob0_msg3.16b},[job0_data],64
	stp	qjob0_dig0,qjob0_dig1,[sp,job0_dig_off]
	ld1	{vjob1_msg0.16b-vjob1_msg3.16b},[job1_data],64
	stp	qjob1_dig0,qjob1_dig1,[sp,job1_dig_off]
	ld1	{vjob2_msg0.16b-vjob2_msg3.16b},[job2_data],64
	stp	qjob2_dig0,qjob2_dig1,[sp,job2_dig_off]

	cmp			job0_data,end_ptr

	// big-endian to little-endian
	rev32_msgs

	quad_round_expand	a, const0 , dig0, dig1,  msg0, msg1, msg2, msg3, msg4, tmp0, tmp1
	quad_round_expand	a, const1 , dig0, dig1,  msg1, msg2, msg3, msg4, msg0, tmp0, tmp1

	ldp			qconst4,qconst5,[const_adr,4*16]
	quad_round_expand	a, const2 , dig0, dig1,  msg2, msg3, msg4, msg0, msg1, tmp0, tmp1
	quad_round_expand	a, const3 , dig0, dig1,  msg3, msg4, msg0, msg1, msg2, tmp0, tmp1

	ldp			qconst6,qconst7,[const_adr,6*16]
	quad_round_expand	b, const4 , dig0, dig1,  msg4, msg0, msg1, msg2, msg3, tmp0, tmp1
	quad_round_expand	b, const5 , dig0, dig1,  msg0, msg1, msg2, msg3, msg4, tmp0, tmp1
	ldp			qconst8,qconst9,[const_adr,8*16]
	quad_round_expand	b, const6 , dig0, dig1,  msg1, msg2, msg3, msg4, msg0, tmp0, tmp1
	quad_round_expand	b, const7 , dig0, dig1,  msg2, msg3, msg4, msg0, msg1, tmp0, tmp1
	ldp			qconst10,qconst11,[const_adr,10*16]
	quad_round_expand	b, const8 , dig0, dig1,  msg3, msg4, msg0, msg1, msg2, tmp0, tmp1
	quad_round_expand	b, const9 , dig0, dig1,  msg4, msg0, msg1, msg2, msg3, tmp0, tmp1

	ldp			qconst4,qconst5,[const_adr,4*16]
	quad_round_expand	b, const10, dig0, dig1,  msg0, msg1, msg2, msg3, msg4, tmp0, tmp1
	quad_round_expand	b, const11, dig0, dig1,  msg1, msg2, msg3, msg4, msg0, tmp0, tmp1
	ldp			qconst6,qconst7,[const_adr,6*16]
	quad_round_expand	b, const4 , dig0, dig1,  msg2, msg3, msg4, msg0, msg1, tmp0, tmp1

	quad_round		b, const5, dig0, dig1, msg3, msg4, tmp0, tmp1
	ldp			qconst0,qconst1,[const_adr]
	quad_round		b, const6, dig0, dig1, msg4, msg0, tmp0, tmp1

	quad_round		b, const7, dig0, dig1, msg0, msg1, tmp0, tmp1,1
	ldp			qconst2,qconst3,[const_adr,2*16]

	.irp	j,0,1,2
		do_eor			job\j,dig0,dig0,backup_dig0
		do_eor			job\j,dig1,dig1,backup_dig1
	.endr

	bcc	start_loop

	//rev128
	.irp	j,0,1,2
		do_ext		\j,dig0,dig0,dig0,#8
		do_ext		\j,dig1,dig1,dig1,#8
		do_rev64	\j,dig0,dig0
		do_rev64	\j,dig1,dig1
		do_st_digest	\j
	.endr



exit_ret:
	ldp 	d10,d11,[sp,16]
	ldp 	d12,d13,[sp,32]
	ldp 	d14,d15,[sp,48]
	ldp     d8, d9, [sp], 192
	ret

	.align	2
.consts:
	.word	0xce6228cb	// 3
	.word	0xe7311465	// 2
	.word	0xf3988a32	// 1
	.word	0x79cc4519	// 0
	.word	0xe6228cbc	// 7
	.word	0x7311465e	// 6
	.word	0x3988a32f	// 5
	.word	0x9cc45197	// 4
	.word	0x6228cbce	//11
	.word	0x311465e7	//10
	.word	0x988a32f3	// 9
	.word	0xcc451979	// 8
	.word	0x228cbce6	//15
	.word	0x11465e73	//14
	.word	0x88a32f39	//13
	.word	0xc451979c	//12
	.word	0xec53d43c	//19
	.word	0x7629ea1e	//18
	.word	0x3b14f50f	//17
	.word	0x9d8a7a87	//16
	.word	0xc53d43ce	//23
	.word	0x629ea1e7	//22
	.word	0xb14f50f3	//21
	.word	0xd8a7a879	//20
	.word	0x53d43cec	//27
	.word	0x29ea1e76	//26
	.word	0x14f50f3b	//25
	.word	0x8a7a879d	//24
	.word	0x3d43cec5	//31
	.word	0x9ea1e762	//30
	.word	0x4f50f3b1	//29
	.word	0xa7a879d8	//28
	.word	0xd43cec53	//35
	.word	0xea1e7629	//34
	.word	0xf50f3b14	//33
	.word	0x7a879d8a	//32
	.word	0x43cec53d	//39
	.word	0xa1e7629e	//38
	.word	0x50f3b14f	//37
	.word	0xa879d8a7	//36
	.word	0x3cec53d4	//43
	.word	0x1e7629ea	//42
	.word	0x0f3b14f5	//41
	.word	0x879d8a7a	//40
	.word	0xcec53d43	//47
	.word	0xe7629ea1	//46
	.word	0xf3b14f50	//45
	.word	0x79d8a7a8	//44
	.word	0xec53d43c	//51
	.word	0x7629ea1e	//50
	.word	0x3b14f50f	//49


	.size	sm3_mb_sm_x3, .-sm3_mb_sm_x3


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
	.irp	j,0,1,2,3
		do_ext	\j,\msg4,\msg1,\msg2,#12
	.endr
	.irp	j,0,1,2,3
		do_ext	\j,\tmp0,\msg0,\msg1,#12
	.endr
	.irp	j,0,1,2,3
		do_ext	\j,\tmp1,\msg2,\msg3,#8
	.endr

	.irp	j,0,1,2,3
		do_sm3partw1	\j,\msg4, \msg0, \msg3
	.endr
	.irp	j,0,1,2,3
		do_sm3partw2	\j,\msg4, \tmp1, \tmp0
	.endr
	st1	{vjob0_\msg4\().16b-vjob3_\msg4\().16b},[data_buf],64
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

.macro	quad_round	ab:req,const:req,dig0:req,dig1:req,msg0:req,msg1:req,tmp0:req,tmp1:req,is_last
	.ifnb	\is_last
		ld1		{vjob0_backup_dig0.16b-vjob3_backup_dig0.16b},[dig_buf],64
	.endif

	.irp	j,0,1,2,3
		do_eor	job\j,\tmp0,\msg0,\msg1

	.endr

	.irp	lane,0,1,2
		.irp	j,0,1,2,3
			do_sm3ss1	job\j,\tmp1,\dig0,\dig1,\const
		.endr
		ext		v\const\().16b,v\const\().16b,v\const\().16b,12
		.irp	j,0,1,2,3
			do_sm3tt2	job\j,\ab,\dig1,\tmp1,\msg0,\lane
		.endr
		.irp	j,0,1,2,3
			do_sm3tt1	job\j,\ab,\dig0,\tmp1,\tmp0,\lane
		.endr


	.endr
	.irp	j,0,1,2,3
		do_sm3ss1	job\j,\tmp1,\dig0,\dig1,\const
	.endr
	.ifnb	\is_last

		ld1	{vjob0_backup_dig1.16b-vjob3_backup_dig1.16b},[dig_buf]
	.else
		ext		v\const\().16b,v\const\().16b,v\const\().16b,12
	.endif
	.irp	j,0,1,2,3
		do_sm3tt2	job\j,\ab,\dig1,\tmp1,\msg0,3
	.endr

	.irp	j,0,1,2,3
		do_sm3tt1	job\j,\ab,\dig0,\tmp1,\tmp0,3
		.ifnb	\is_last
			do_eor	job\j,dig1,dig1,backup_dig1
			do_eor	job\j,dig0,dig0,backup_dig0
		.endif
	.endr

	.ifb	\is_last
		ld1	{vjob0_\msg0\().16b-vjob3_\msg0\().16b},[data_buf],64
	.endif

.endm



/*
	Variables
*/
	.set		temp_buf_size,(68*4+32)*4
	.set		dig_buf_off,64
	.set		data_buf_off,64+32*4
	job0		.req	x0
	job1		.req	x1
	job2		.req	x2
	job3		.req	x3
	len		.req	x4

	job0_data	.req	x5
	job1_data	.req	x6
	job2_data	.req	x7
	job3_data	.req	x9

	job0_digest	.req	x0
	job1_digest	.req	x1
	job2_digest	.req	x2
	job3_digest	.req	x3

	const_adr	.req	x10
	end_ptr		.req	x4
	data_buf	.req	x11
	dig_buf		.req	x12

	declare_var_vector_reg	job0_msg0,          0
	declare_var_vector_reg	job1_msg0,          1
	declare_var_vector_reg	job2_msg0,          2
	declare_var_vector_reg	job3_msg0,          3
	declare_var_vector_reg	job0_msg1,          4
	declare_var_vector_reg	job1_msg1,          5
	declare_var_vector_reg	job2_msg1,          6
	declare_var_vector_reg	job3_msg1,          7
	declare_var_vector_reg	job0_msg2,          8
	declare_var_vector_reg	job1_msg2,          9
	declare_var_vector_reg	job2_msg2,         10
	declare_var_vector_reg	job3_msg2,         11
	declare_var_vector_reg	job0_msg3,         12
	declare_var_vector_reg	job1_msg3,         13
	declare_var_vector_reg	job2_msg3,         14
	declare_var_vector_reg	job3_msg3,         15
	declare_var_vector_reg	job0_tmp0,         16
	declare_var_vector_reg	job1_tmp0,         17
	declare_var_vector_reg	job2_tmp0,         18
	declare_var_vector_reg	job3_tmp0,         19
	declare_var_vector_reg	job0_tmp1,         20
	declare_var_vector_reg	job1_tmp1,         21
	declare_var_vector_reg	job2_tmp1,         22
	declare_var_vector_reg	job3_tmp1,         23
	declare_var_vector_reg	job0_msg4,         24
	declare_var_vector_reg	job1_msg4,         25
	declare_var_vector_reg	job2_msg4,         26
	declare_var_vector_reg	job3_msg4,         27
	declare_var_vector_reg	job0_dig0,          8
	declare_var_vector_reg	job1_dig0,          9
	declare_var_vector_reg	job2_dig0,         10
	declare_var_vector_reg	job3_dig0,         11
	declare_var_vector_reg	job0_dig1,         12
	declare_var_vector_reg	job1_dig1,         13
	declare_var_vector_reg	job2_dig1,         14
	declare_var_vector_reg	job3_dig1,         15

	declare_var_vector_reg	job0_backup_dig0,  24
	declare_var_vector_reg	job1_backup_dig0,  25
	declare_var_vector_reg	job2_backup_dig0,  26
	declare_var_vector_reg	job3_backup_dig0,  27
	declare_var_vector_reg	job0_backup_dig1,  28
	declare_var_vector_reg	job1_backup_dig1,  29
	declare_var_vector_reg	job2_backup_dig1,  30
	declare_var_vector_reg	job3_backup_dig1,  31

	declare_var_vector_reg	const0,            24
	declare_var_vector_reg	const1,            25
	declare_var_vector_reg	const2,            26
	declare_var_vector_reg	const3,            27
	declare_var_vector_reg	const4,            28
	declare_var_vector_reg	const5,            29
	declare_var_vector_reg	const6,            30
	declare_var_vector_reg	const7,            31
	declare_var_vector_reg	const8,            24
	declare_var_vector_reg	const9,            25
	declare_var_vector_reg	const10,           26
	declare_var_vector_reg	const11,           27

.macro do_rev32_msg	job:req,msg:req
	rev32	v\job\()_\msg\().16b,v\job\()_\msg\().16b
.endm

.macro do_rev32_job	job:req
	.irp	m,0,1,2,3
	do_rev32_msg	\job,msg\m
	.endr
.endm

.macro rev32_msgs
	.irp	j,0,1,2,3
	do_rev32_job	job\j
	.endr
.endm

.macro do_rev64		job,regd,regn
	rev64		vjob\job\()_\regd\().16b,vjob\job\()_\regd\().16b
.endm

.macro do_ldp_msg23	job
	ldp	qjob\job\()_msg2,qjob\job\()_msg3,[job\job\()_data],32
.endm

	.global	sm3_mb_sm_x4
	.type	sm3_mb_sm_x4, %function
sm3_mb_sm_x4:
	//push d8~d15
	sub	sp,sp,temp_buf_size
	stp 	d8,d9,[sp,-64]!
	stp 	d10,d11,[sp,16]
	stp 	d12,d13,[sp,32]
	stp 	d14,d15,[sp,48]



	ldr	job0_data, [job0],64
	ldr	job1_data, [job1],64
	ldr	job2_data, [job2],64
	ldr	job3_data, [job3],64

	ldp	qjob0_dig0,qjob0_dig1,[job0_digest]
	ldp	qjob1_dig0,qjob1_dig1,[job1_digest]
	ldp	qjob2_dig0,qjob2_dig1,[job2_digest]
	ldp	qjob3_dig0,qjob3_dig1,[job3_digest]
	add	end_ptr,job0_data,len,lsl 6
	//rev128,change digest endian
	.irp	j,0,1,2,3
		do_ext		\j,dig0,dig0,dig0,#8
		do_ext		\j,dig1,dig1,dig1,#8
		do_rev64	\j,dig0,dig0
		do_rev64	\j,dig1,dig1
	.endr




start_loop:
	add	dig_buf,sp,dig_buf_off
	ldp	qjob0_msg0,qjob0_msg1,[job0_data],32
	add	data_buf,sp,data_buf_off
	ldp	qjob1_msg0,qjob1_msg1,[job1_data],32
	st1	{vjob0_dig0.16b-vjob3_dig0.16b},[dig_buf],64
	ldp	qjob2_msg0,qjob2_msg1,[job2_data],32
	st1	{vjob0_dig1.16b-vjob3_dig1.16b},[dig_buf]
	ldp	qjob3_msg0,qjob3_msg1,[job3_data],32

	.irp	j,0,1,2,3
		do_ldp_msg23	\j
		do_rev32_msg	job\j,msg0
		do_rev32_msg	job\j,msg1
	.endr
	st1	{vjob0_msg0.16b-vjob3_msg0.16b},[data_buf],64
	st1	{vjob0_msg1.16b-vjob3_msg1.16b},[data_buf],64
	.irp	j,0,1,2,3
		do_rev32_msg	job\j,msg2
		do_rev32_msg	job\j,msg3
	.endr
	st1	{vjob0_msg2.16b-vjob3_msg2.16b},[data_buf],64
	st1	{vjob0_msg3.16b-vjob3_msg3.16b},[data_buf],64

	cmp			job0_data,end_ptr

	/** message expand **/
	message_expand	msg0, msg1, msg2, msg3, msg4, tmp0, tmp1
	message_expand	msg1, msg2, msg3, msg4, msg0, tmp0, tmp1
	message_expand	msg2, msg3, msg4, msg0, msg1, tmp0, tmp1
	message_expand	msg3, msg4, msg0, msg1, msg2, tmp0, tmp1
	message_expand	msg4, msg0, msg1, msg2, msg3, tmp0, tmp1
	message_expand	msg0, msg1, msg2, msg3, msg4, tmp0, tmp1
	message_expand	msg1, msg2, msg3, msg4, msg0, tmp0, tmp1
	message_expand	msg2, msg3, msg4, msg0, msg1, tmp0, tmp1
	message_expand	msg3, msg4, msg0, msg1, msg2, tmp0, tmp1
	message_expand	msg4, msg0, msg1, msg2, msg3, tmp0, tmp1
	message_expand	msg0, msg1, msg2, msg3, msg4, tmp0, tmp1
	message_expand	msg1, msg2, msg3, msg4, msg0, tmp0, tmp1
	message_expand	msg2, msg3, msg4, msg0, msg1, tmp0, tmp1

	/** re-init variables for sm3 rounds **/
	add		dig_buf,sp,dig_buf_off
	ld1		{vjob0_dig0.16b-vjob3_dig0.16b},[dig_buf],64
	add		data_buf,sp,data_buf_off
	ld1		{vjob0_dig1.16b-vjob3_dig1.16b},[dig_buf]
	add		dig_buf,sp,dig_buf_off
	adrp		const_adr,.consts
	ld1		{vjob0_msg0.16b-vjob3_msg0.16b},[data_buf],64
	add		const_adr,const_adr,:lo12:.consts
	ld1		{vjob0_msg1.16b-vjob3_msg1.16b},[data_buf],64
	ld1		{vconst0.16b-vconst3.16b},[const_adr],64
	ld1		{vconst4.16b-vconst7.16b},[const_adr],64
	/** digests rounds **/
	quad_round	a, const0 , dig0, dig1,  msg0, msg1, tmp0, tmp1
	quad_round	a, const1 , dig0, dig1,  msg1, msg0, tmp0, tmp1
	quad_round	a, const2 , dig0, dig1,  msg0, msg1, tmp0, tmp1
	quad_round	a, const3 , dig0, dig1,  msg1, msg0, tmp0, tmp1

	/** share registers with vconst0-vconst3 **/
	ld1		{vconst8.16b-vconst11.16b},[const_adr]

	quad_round	b, const4 , dig0, dig1,  msg0, msg1, tmp0, tmp1
	quad_round	b, const5 , dig0, dig1,  msg1, msg0, tmp0, tmp1
	quad_round	b, const6 , dig0, dig1,  msg0, msg1, tmp0, tmp1
	quad_round	b, const7 , dig0, dig1,  msg1, msg0, tmp0, tmp1
	quad_round	b, const8 , dig0, dig1,  msg0, msg1, tmp0, tmp1
	quad_round	b, const9 , dig0, dig1,  msg1, msg0, tmp0, tmp1
	quad_round	b, const10, dig0, dig1,  msg0, msg1, tmp0, tmp1
	quad_round	b, const11, dig0, dig1,  msg1, msg0, tmp0, tmp1
	quad_round	b, const4 , dig0, dig1,  msg0, msg1, tmp0, tmp1
	quad_round	b, const5 , dig0, dig1,  msg1, msg0, tmp0, tmp1
	quad_round	b, const6 , dig0, dig1,  msg0, msg1, tmp0, tmp1
	quad_round	b, const7 , dig0, dig1,  msg1, msg0, tmp0, tmp1,1

	bcc	start_loop

	//rev128
	.irp	j,0,1,2,3
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
	ldp     d8, d9, [sp], 64
	add	sp,sp,temp_buf_size
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


	.size	sm3_mb_sm_x4, .-sm3_mb_sm_x4


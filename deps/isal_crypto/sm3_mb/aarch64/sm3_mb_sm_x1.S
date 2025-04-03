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

.macro message_expand	msg0:req,msg1:req,msg2:req,msg3:req,msg4:req,tmp0:req,tmp1:req
	ext		v\msg4\().16b, v\msg1\().16b, v\msg2\().16b, #12
	ext		v\tmp0\().16b, v\msg0\().16b, v\msg1\().16b, #12
	ext		v\tmp1\().16b, v\msg2\().16b, v\msg3\().16b, #8
	sm3partw1	v\msg4\().4s, v\msg0\().4s, v\msg3\().4s
	sm3partw2	v\msg4\().4s, v\tmp1\().4s, v\tmp0\().4s

.endm

.macro	quad_round	ab:req,const:req,dig0:req,dig1:req,msg0:req,msg1:req,tmp0:req,tmp1:req
	eor		v\tmp0\().16b, v\msg0\().16b, v\msg1\().16b


	sm3ss1		v\tmp1\().4s, v\dig0\().4s, v\dig1\().4s, v\const\().4s
	ext		v\const\().16b,v\const\().16b,v\const\().16b,12
	sm3tt1\ab	v\dig0\().4s, v\tmp1\().4s, v\tmp0\().4s[0]
	sm3tt2\ab	v\dig1\().4s, v\tmp1\().4s, v\msg0\().4s[0]

	sm3ss1		v\tmp1\().4s, v\dig0\().4s, v\dig1\().4s, v\const\().4s
	ext		v\const\().16b,v\const\().16b,v\const\().16b,12
	sm3tt1\ab	v\dig0\().4s, v\tmp1\().4s, v\tmp0\().4s[1]
	sm3tt2\ab	v\dig1\().4s, v\tmp1\().4s, v\msg0\().4s[1]

	sm3ss1		v\tmp1\().4s, v\dig0\().4s, v\dig1\().4s, v\const\().4s
	ext		v\const\().16b,v\const\().16b,v\const\().16b,12
	sm3tt1\ab	v\dig0\().4s, v\tmp1\().4s, v\tmp0\().4s[2]
	sm3tt2\ab	v\dig1\().4s, v\tmp1\().4s, v\msg0\().4s[2]

	sm3ss1		v\tmp1\().4s, v\dig0\().4s, v\dig1\().4s, v\const\().4s
	ext		v\const\().16b,v\const\().16b,v\const\().16b,12
	sm3tt1\ab	v\dig0\().4s, v\tmp1\().4s, v\tmp0\().4s[3]
	sm3tt2\ab	v\dig1\().4s, v\tmp1\().4s, v\msg0\().4s[3]

.endm

.macro quad_round_expand	ab:req,const:req,dig0:req,dig1:req,msg0:req,msg1:req,msg2:req,msg3:req,msg4:req,tmp0:req,tmp1:req
	message_expand	\msg0,\msg1,\msg2,\msg3,\msg4,\tmp0,\tmp1
	quad_round	\ab,\const,\dig0,\dig1,\msg0,\msg1,\tmp0,\tmp1
.endm
	job	.req	x0
	len	.req	x1
	data	.req	x2
	digest	.req	x0
	end_ptr	.req	x1


	declare_var_vector_reg	msg0,0
	declare_var_vector_reg	msg1,1
	declare_var_vector_reg	msg2,2
	declare_var_vector_reg	msg3,3
	declare_var_vector_reg	msg4,4
	declare_var_vector_reg	dig0,5
	declare_var_vector_reg	dig1,6
	declare_var_vector_reg	backup_dig0,  7

	declare_var_vector_reg	tmp0,16
	declare_var_vector_reg	tmp1,17
	declare_var_vector_reg	backup_dig1,  18

	declare_var_vector_reg	const0,19
	declare_var_vector_reg	const1,20
	declare_var_vector_reg	const2,21
	declare_var_vector_reg	const3,22
	declare_var_vector_reg	const4,23
	declare_var_vector_reg	const5,24
	declare_var_vector_reg	const6,25
	declare_var_vector_reg	const7,26
	declare_var_vector_reg	const8,27
	declare_var_vector_reg	const9,28
	declare_var_vector_reg	const10,29
	declare_var_vector_reg	const11,30




	.global	sm3_mb_sm_x1
	.type	sm3_mb_sm_x1, %function
sm3_mb_sm_x1:
	adrp	x3,.consts
	ldr	data, [job],64
	add	x3,x3,:lo12:.consts
	ldp	qdig0,qdig1,[digest]
	ld1	{vconst0.16b-vconst3.16b},[x3],64
	add	end_ptr,data,len,lsl 6
	ld1	{vconst4.16b-vconst7.16b},[x3],64
	//rev128
	ext	vdig0.16b,vdig0.16b,vdig0.16b,#8
	ext	vdig1.16b,vdig1.16b,vdig1.16b,#8
	ld1	{vconst8.16b-vconst11.16b},[x3],64
	rev64	vdig0.16b,vdig0.16b
	rev64	vdig1.16b,vdig1.16b


start_loop:
	mov	vbackup_dig0.16b,vdig0.16b
	mov	vbackup_dig1.16b,vdig1.16b
	ldp	qmsg0,qmsg1,[data],32
	ldp	qmsg2,qmsg3,[data],32

	// big-endian to little-endian
	rev32	vmsg0.16b,vmsg0.16b
	rev32	vmsg1.16b,vmsg1.16b
	rev32	vmsg2.16b,vmsg2.16b
	rev32	vmsg3.16b,vmsg3.16b

	quad_round_expand	a, const0, dig0, dig1,  msg0, msg1, msg2, msg3, msg4, tmp0, tmp1
	quad_round_expand	a, const1, dig0, dig1,  msg1, msg2, msg3, msg4, msg0, tmp0, tmp1
	quad_round_expand	a, const2, dig0, dig1,  msg2, msg3, msg4, msg0, msg1, tmp0, tmp1
	quad_round_expand	a, const3, dig0, dig1,  msg3, msg4, msg0, msg1, msg2, tmp0, tmp1
	quad_round_expand	b, const4, dig0, dig1,  msg4, msg0, msg1, msg2, msg3, tmp0, tmp1
	quad_round_expand	b, const5, dig0, dig1,  msg0, msg1, msg2, msg3, msg4, tmp0, tmp1
	quad_round_expand	b, const6, dig0, dig1,  msg1, msg2, msg3, msg4, msg0, tmp0, tmp1
	quad_round_expand	b, const7, dig0, dig1,  msg2, msg3, msg4, msg0, msg1, tmp0, tmp1
	quad_round_expand	b, const8, dig0, dig1,  msg3, msg4, msg0, msg1, msg2, tmp0, tmp1
	quad_round_expand	b, const9, dig0, dig1,  msg4, msg0, msg1, msg2, msg3, tmp0, tmp1
	quad_round_expand	b, const10, dig0, dig1,  msg0, msg1, msg2, msg3, msg4, tmp0, tmp1
	quad_round_expand	b, const11, dig0, dig1,  msg1, msg2, msg3, msg4, msg0, tmp0, tmp1
	quad_round_expand	b, const4, dig0, dig1,  msg2, msg3, msg4, msg0, msg1, tmp0, tmp1


	quad_round		b, const5, dig0, dig1, msg3, msg4, tmp0, tmp1
	cmp			data,end_ptr
	quad_round		b, const6, dig0, dig1, msg4, msg0, tmp0, tmp1
	quad_round		b, const7, dig0, dig1, msg0, msg1, tmp0, tmp1

	eor			vdig0.16b,vdig0.16b,vbackup_dig0.16b
	eor			vdig1.16b,vdig1.16b,vbackup_dig1.16b


	bcc	start_loop

	//rev128
	ext	vdig0.16b,vdig0.16b,vdig0.16b,#8
	ext	vdig1.16b,vdig1.16b,vdig1.16b,#8
	rev64	vdig0.16b,vdig0.16b
	rev64	vdig1.16b,vdig1.16b
	str	qdig0,[digest]
	str	qdig1,[digest,16]
	ret
	dsb	ish
	isb
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


	.size	sm3_mb_sm_x1, .-sm3_mb_sm_x1


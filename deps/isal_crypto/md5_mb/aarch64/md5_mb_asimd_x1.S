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
	.arch armv8-a

/*
Macros
*/

.macro	declare_var_vector_reg name:req,reg:req
	q_\name	.req	q\reg
	v_\name	.req	v\reg
	s_\name	.req	s\reg
.endm


.macro	round_0_15	d_a:req,d_b:req,d_c:req,d_d:req,kh:req,kl:req,w:req,r:req
	eor	tmp0,\d_c,\d_d
	mov	k,\kl
	and	tmp0,tmp0,\d_b
	movk	k,\kh,lsl 16
	eor	tmp0,tmp0,\d_d
	add	tmp1,k,\w
	add	tmp0,tmp1,tmp0
	add	tmp0,\d_a,tmp0
	ror	tmp0,tmp0,32 - \r
	add	\d_a,\d_b,tmp0
.endm

.macro	round_16_31	d_a:req,d_b:req,d_c:req,d_d:req,kh:req,kl:req,w:req,r:req
	eor	tmp0,\d_b,\d_c
	mov	k,\kl
	and	tmp0,tmp0,\d_d
	movk	k,\kh,lsl 16
	eor	tmp0,tmp0,\d_c
	add	tmp1,k,\w
	add	tmp0,tmp1,tmp0
	add	tmp0,\d_a,tmp0
	ror	tmp0,tmp0,32 - \r
	add	\d_a,\d_b,tmp0
.endm

.macro	round_32_47	d_a:req,d_b:req,d_c:req,d_d:req,kh:req,kl:req,w:req,r:req
	eor	tmp0,\d_b,\d_c
	mov	k,\kl
	eor	tmp0,tmp0,\d_d
	movk	k,\kh,lsl 16
	add	tmp1,k,\w
	add	tmp0,tmp1,tmp0
	add	tmp0,\d_a,tmp0
	ror	tmp0,tmp0,32 - \r
	add	\d_a,\d_b,tmp0
.endm

.macro	round_48_63	d_a:req,d_b:req,d_c:req,d_d:req,kh:req,kl:req,w:req,r:req
	orn	tmp0,\d_b,\d_d
	mov	k,\kl
	eor	tmp0,tmp0,\d_c
	movk	k,\kh,lsl 16
	add	tmp1,k,\w
	add	tmp0,tmp1,tmp0
	add	tmp0,\d_a,tmp0
	ror	tmp0,tmp0,32 - \r
	add	\d_a,\d_b,tmp0
.endm
/*
	variables
*/
	job0		.req	x0
	digest_addr	.req	x0
	len		.req	w1
	end		.req	x1

	buf_adr		.req	x2
	d_a		.req	w3
	d_b		.req	w4
	d_c		.req	w5
	d_d		.req	w6
	k		.req	w7
	m0		.req	w8
	m1		.req	w9
	m2		.req	w10
	m3		.req	w11
	m4		.req	w12
	m5		.req	w13
	m6		.req	w14
	m7		.req	w15
	m8		.req	w19
	m9		.req	w20
	m10		.req	w21
	m11		.req	w22
	m12		.req	w23
	m13		.req	w24
	m14		.req	w25
	m15		.req	w26

	tmp0		.req	w27
	tmp1		.req	w28

	d_a1		.req	w8
	d_b1		.req	w9
	d_c1		.req	w15
	d_d1		.req	w19

/*
	void md5_mb_asimd_x1(MD5_JOB * job0,int len)
*/
	.global md5_mb_asimd_x1
	.type md5_mb_asimd_x1, %function
md5_mb_asimd_x1:
	cmp	len,0
	stp	x29, x30, [sp,-96]!
	ldr	buf_adr,[job0],64
	stp	x19, x20, [sp, 16]
	add	end,buf_adr,end,lsl 6
	stp	x21, x22, [sp, 32]
	ldp	d_a,d_b,[digest_addr]
	stp	x23, x24, [sp, 48]
	ldp	d_c,d_d,[digest_addr,8]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ble	.exit

.loop_start:
	ldp		m0,m1,[buf_adr],8
	ldp		m2,m3,[buf_adr],8
	round_0_15	d_a,d_b,d_c,d_d,0xd76a,0xa478,m0,7

	ldp		m4,m5,[buf_adr],8
	round_0_15	d_d,d_a,d_b,d_c,0xe8c7,0xb756,m1,12
	ldp		m6,m7,[buf_adr],8
	round_0_15	d_c,d_d,d_a,d_b,0x2420,0x70db,m2,17
	ldp		m8,m9,[buf_adr],8
	round_0_15	d_b,d_c,d_d,d_a,0xc1bd,0xceee,m3,22
	ldp		m10,m11,[buf_adr],8
	round_0_15	d_a,d_b,d_c,d_d,0xf57c,0xfaf,m4,7
	ldp		m12,m13,[buf_adr],8
	round_0_15	d_d,d_a,d_b,d_c,0x4787,0xc62a,m5,12
	ldp		m14,m15,[buf_adr],8
	round_0_15	d_c,d_d,d_a,d_b,0xa830,0x4613,m6,17
	round_0_15	d_b,d_c,d_d,d_a,0xfd46,0x9501,m7,22
	round_0_15	d_a,d_b,d_c,d_d,0x6980,0x98d8,m8,7
	round_0_15	d_d,d_a,d_b,d_c,0x8b44,0xf7af,m9,12
	round_0_15	d_c,d_d,d_a,d_b,0xffff,0x5bb1,m10,17
	round_0_15	d_b,d_c,d_d,d_a,0x895c,0xd7be,m11,22
	round_0_15	d_a,d_b,d_c,d_d,0x6b90,0x1122,m12,7
	round_0_15	d_d,d_a,d_b,d_c,0xfd98,0x7193,m13,12
	round_0_15	d_c,d_d,d_a,d_b,0xa679,0x438e,m14,17
	round_0_15	d_b,d_c,d_d,d_a,0x49b4,0x821,m15,22

	round_16_31	d_a,d_b,d_c,d_d,0xf61e,0x2562,m1,5
	round_16_31	d_d,d_a,d_b,d_c,0xc040,0xb340,m6,9
	round_16_31	d_c,d_d,d_a,d_b,0x265e,0x5a51,m11,14
	round_16_31	d_b,d_c,d_d,d_a,0xe9b6,0xc7aa,m0,20
	round_16_31	d_a,d_b,d_c,d_d,0xd62f,0x105d,m5,5
	round_16_31	d_d,d_a,d_b,d_c,0x244,0x1453,m10,9
	round_16_31	d_c,d_d,d_a,d_b,0xd8a1,0xe681,m15,14
	round_16_31	d_b,d_c,d_d,d_a,0xe7d3,0xfbc8,m4,20
	round_16_31	d_a,d_b,d_c,d_d,0x21e1,0xcde6,m9,5
	round_16_31	d_d,d_a,d_b,d_c,0xc337,0x7d6,m14,9
	round_16_31	d_c,d_d,d_a,d_b,0xf4d5,0xd87,m3,14
	round_16_31	d_b,d_c,d_d,d_a,0x455a,0x14ed,m8,20
	round_16_31	d_a,d_b,d_c,d_d,0xa9e3,0xe905,m13,5
	round_16_31	d_d,d_a,d_b,d_c,0xfcef,0xa3f8,m2,9
	round_16_31	d_c,d_d,d_a,d_b,0x676f,0x2d9,m7,14
	round_16_31	d_b,d_c,d_d,d_a,0x8d2a,0x4c8a,m12,20

	round_32_47	d_a,d_b,d_c,d_d,0xfffa,0x3942,m5,4
	round_32_47	d_d,d_a,d_b,d_c,0x8771,0xf681,m8,11
	round_32_47	d_c,d_d,d_a,d_b,0x6d9d,0x6122,m11,16
	round_32_47	d_b,d_c,d_d,d_a,0xfde5,0x380c,m14,23
	round_32_47	d_a,d_b,d_c,d_d,0xa4be,0xea44,m1,4
	round_32_47	d_d,d_a,d_b,d_c,0x4bde,0xcfa9,m4,11
	round_32_47	d_c,d_d,d_a,d_b,0xf6bb,0x4b60,m7,16
	round_32_47	d_b,d_c,d_d,d_a,0xbebf,0xbc70,m10,23
	round_32_47	d_a,d_b,d_c,d_d,0x289b,0x7ec6,m13,4
	round_32_47	d_d,d_a,d_b,d_c,0xeaa1,0x27fa,m0,11
	round_32_47	d_c,d_d,d_a,d_b,0xd4ef,0x3085,m3,16
	round_32_47	d_b,d_c,d_d,d_a,0x488,0x1d05,m6,23
	round_32_47	d_a,d_b,d_c,d_d,0xd9d4,0xd039,m9,4
	round_32_47	d_d,d_a,d_b,d_c,0xe6db,0x99e5,m12,11
	round_32_47	d_c,d_d,d_a,d_b,0x1fa2,0x7cf8,m15,16
	round_32_47	d_b,d_c,d_d,d_a,0xc4ac,0x5665,m2,23

	round_48_63	d_a,d_b,d_c,d_d,0xf429,0x2244,m0,6
	round_48_63	d_d,d_a,d_b,d_c,0x432a,0xff97,m7,10
	round_48_63	d_c,d_d,d_a,d_b,0xab94,0x23a7,m14,15
	round_48_63	d_b,d_c,d_d,d_a,0xfc93,0xa039,m5,21
	round_48_63	d_a,d_b,d_c,d_d,0x655b,0x59c3,m12,6
	round_48_63	d_d,d_a,d_b,d_c,0x8f0c,0xcc92,m3,10
	round_48_63	d_c,d_d,d_a,d_b,0xffef,0xf47d,m10,15
	round_48_63	d_b,d_c,d_d,d_a,0x8584,0x5dd1,m1,21
	round_48_63	d_a,d_b,d_c,d_d,0x6fa8,0x7e4f,m8,6
	round_48_63	d_d,d_a,d_b,d_c,0xfe2c,0xe6e0,m15,10
	round_48_63	d_c,d_d,d_a,d_b,0xa301,0x4314,m6,15
	round_48_63	d_b,d_c,d_d,d_a,0x4e08,0x11a1,m13,21
	round_48_63	d_a,d_b,d_c,d_d,0xf753,0x7e82,m4,6
	ldp		d_a1,d_b1,[digest_addr]
	round_48_63	d_d,d_a,d_b,d_c,0xbd3a,0xf235,m11,10
	ldp		d_c1,d_d1,[digest_addr,8]
	round_48_63	d_c,d_d,d_a,d_b,0x2ad7,0xd2bb,m2,15
	round_48_63	d_b,d_c,d_d,d_a,0xeb86,0xd391,m9,21

	cmp	buf_adr,end
	add	d_a,d_a1 ,d_a
	str	d_a,[digest_addr]
	add	d_b,d_b1 ,d_b
	str	d_b,[digest_addr,4]
	add	d_c,d_c1 ,d_c
	str	d_c,[digest_addr,8]
	add	d_d,d_d1 ,d_d
	str	d_d,[digest_addr,12]
	bne	.loop_start

.exit:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], 96
	ret
	.size md5_mb_asimd_x1, .-md5_mb_asimd_x1

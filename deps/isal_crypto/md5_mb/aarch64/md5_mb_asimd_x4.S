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

.macro	add_key_rol	a:req,b:req,k:req,w:req,r:req
	add	v_tmp0.4s,v_\k\().4s,v_\w\().4s
	add	v_tmp1.4s,v_tmp1.4s,v_\a\().4s
	add	v_tmp1.4s,v_tmp1.4s,v_tmp0.4s
	shl	v_tmp0.4s,v_tmp1.4s,\r
	ushr	v_tmp1.4s,v_tmp1.4s,32-\r
	orr	v_tmp0.16b,v_tmp1.16b,v_tmp0.16b

	add	v_\a\().4s,v_\b\().4s,v_tmp0.4s
.endm
.macro	round_0_15	a:req,b:req,c:req,d:req,k:req,k1:req,w:req,r:req
	mov	v_tmp1.16b, v_\b\().16b
	bsl	v_tmp1.16b, v_\c\().16b, v_\d\().16b
	ldr	q_\k1,[key_adr],16
	add_key_rol	\a,\b,\k,\w,\r
.endm

.macro	round_16_31	a:req,b:req,c:req,d:req,k:req,k1:req,w:req,r:req
	mov	v_tmp1.16b, v_\d\().16b
	bsl	v_tmp1.16b, v_\b\().16b, v_\c\().16b
	ldr	q_\k1,[key_adr],16
	add_key_rol	\a,\b,\k,\w,\r
.endm

.macro	round_32_47	a:req,b:req,c:req,d:req,k:req,k1:req,w:req,r:req
	eor	v_tmp1.16b,v_\b\().16b,v_\c\().16b
	eor	v_tmp1.16b,v_tmp1.16b,v_\d\().16b
	ldr	q_\k1,[key_adr],16
	add_key_rol	\a,\b,\k,\w,\r
.endm

.macro	round_48_63	a:req,b:req,c:req,d:req,k:req,k1,w:req,r:req
	orn	v_tmp1.16b,v_\b\().16b,v_\d\().16b
	eor	v_tmp1.16b,v_tmp1.16b,v_\c\().16b
	.ifnb	\k1
	ldr	q_\k1,[key_adr],16
	.endif
	add_key_rol	\a,\b,\k,\w,\r
.endm
/*
	variables
*/
	declare_var_vector_reg	tmp0, 0
	declare_var_vector_reg	tmp1, 1
	declare_var_vector_reg	k,    2
	declare_var_vector_reg	k1,   3
	declare_var_vector_reg	a,    4
	declare_var_vector_reg	b,    5
	declare_var_vector_reg	c,    6
	declare_var_vector_reg	d,    7
	declare_var_vector_reg	a1,   8
	declare_var_vector_reg	b1,   9
	declare_var_vector_reg	c1,  10
	declare_var_vector_reg	d1,  11

	declare_var_vector_reg	w0,  16
	declare_var_vector_reg	w1,  17
	declare_var_vector_reg	w2,  18
	declare_var_vector_reg	w3,  19
	declare_var_vector_reg	w4,  20
	declare_var_vector_reg	w5,  21
	declare_var_vector_reg	w6,  22
	declare_var_vector_reg	w7,  23
	declare_var_vector_reg	w8,  24
	declare_var_vector_reg	w9,  25
	declare_var_vector_reg	w10, 26
	declare_var_vector_reg	w11, 27
	declare_var_vector_reg	w12, 28
	declare_var_vector_reg	w13, 29
	declare_var_vector_reg	w14, 30
	declare_var_vector_reg	w15, 31

	len	.req	w4
	len_x	.req	x4
	lane0	.req	x5
	lane1	.req	x6
	lane2	.req	x7
	lane3	.req	x9
	end	.req	x4
	job0	.req	x0
	job1	.req	x1
	job2	.req	x2
	job3	.req	x3
	key_adr	.req	x10

/*
	void md5_mb_asimd_x4(MD5_JOB * job0, MD5_JOB * job1,
		MD5_JOB * job2, MD5_JOB * job3, int len)
*/
	.global md5_mb_asimd_x4
	.type md5_mb_asimd_x4, %function
md5_mb_asimd_x4:
	stp	x29,x30,[sp,-48]!
	ldr	lane0,[job0],64
	stp	d8,d9,[sp,16]
	ldr	lane1,[job1],64
	stp	d10,d11,[sp,32]
	ldr	lane2,[job2],64
	cmp	len,0
	ldr	lane3,[job3],64
	ble	.exit

	//load digests
	ld4	{v_a.s-v_d.s}[0],[job0]
	add	end,lane0,len_x,lsl 6
	ld4	{v_a.s-v_d.s}[1],[job1]
	ld4	{v_a.s-v_d.s}[2],[job2]
	ld4	{v_a.s-v_d.s}[3],[job3]
.loop_start:
	ld1	{v_w0.s}[0],[lane0],4
	mov	v_a1.16b,v_a.16b
	ld1	{v_w0.s}[1],[lane1],4
	mov	v_b1.16b,v_b.16b
	ld1	{v_w0.s}[2],[lane2],4
	mov	v_c1.16b,v_c.16b
	ld1	{v_w0.s}[3],[lane3],4
	mov	v_d1.16b,v_d.16b

	ld3	{v_w1.s-v_w3.s}[0],[lane0],12
	adrp	key_adr,.key_consts
	ld3	{v_w1.s-v_w3.s}[1],[lane1],12
	add	key_adr,key_adr,#:lo12:.key_consts
	ld3	{v_w1.s-v_w3.s}[2],[lane2],12
	ldr	q_k,[key_adr],16
	ld3	{v_w1.s-v_w3.s}[3],[lane3],12


	ld4		{v_w4.s-v_w7.s}[0], [lane0],16

	round_0_15	a,b,c,d,k,k1,w0,7

	ld4		{v_w4.s-v_w7.s}[1], [lane1],16
	round_0_15	d,a,b,c,k1,k,w1,12
	ld4		{v_w4.s-v_w7.s}[2], [lane2],16
	round_0_15	c,d,a,b,k,k1,w2,17
	ld4		{v_w4.s-v_w7.s}[3], [lane3],16
	round_0_15	b,c,d,a,k1,k,w3,22
	ld4		{v_w8.s-v_w11.s}[0],[lane0],16
	round_0_15	a,b,c,d,k,k1,w4,7
	ld4		{v_w8.s-v_w11.s}[1],[lane1],16
	round_0_15	d,a,b,c,k1,k,w5,12
	ld4		{v_w8.s-v_w11.s}[2],[lane2],16
	round_0_15	c,d,a,b,k,k1,w6,17
	ld4		{v_w8.s-v_w11.s}[3],[lane3],16
	round_0_15	b,c,d,a,k1,k,w7,22
	ld4		{v_w12.s-v_w15.s}[0],[lane0],16
	round_0_15	a,b,c,d,k,k1,w8,7
	ld4		{v_w12.s-v_w15.s}[1],[lane1],16
	round_0_15	d,a,b,c,k1,k,w9,12
	ld4		{v_w12.s-v_w15.s}[2],[lane2],16
	round_0_15	c,d,a,b,k,k1,w10,17
	ld4		{v_w12.s-v_w15.s}[3],[lane3],16
	round_0_15	b,c,d,a,k1,k,w11,22
	round_0_15	a,b,c,d,k,k1,w12,7
	round_0_15	d,a,b,c,k1,k,w13,12
	round_0_15	c,d,a,b,k,k1,w14,17
	round_0_15	b,c,d,a,k1,k,w15,22

	round_16_31	a,b,c,d,k,k1,w1,5
	round_16_31	d,a,b,c,k1,k,w6,9
	round_16_31	c,d,a,b,k,k1,w11,14
	round_16_31	b,c,d,a,k1,k,w0,20
	round_16_31	a,b,c,d,k,k1,w5,5
	round_16_31	d,a,b,c,k1,k,w10,9
	round_16_31	c,d,a,b,k,k1,w15,14
	round_16_31	b,c,d,a,k1,k,w4,20
	round_16_31	a,b,c,d,k,k1,w9,5
	round_16_31	d,a,b,c,k1,k,w14,9
	round_16_31	c,d,a,b,k,k1,w3,14
	round_16_31	b,c,d,a,k1,k,w8,20
	round_16_31	a,b,c,d,k,k1,w13,5
	round_16_31	d,a,b,c,k1,k,w2,9
	round_16_31	c,d,a,b,k,k1,w7,14
	round_16_31	b,c,d,a,k1,k,w12,20

	round_32_47	a,b,c,d,k,k1,w5,4
	round_32_47	d,a,b,c,k1,k,w8,11
	round_32_47	c,d,a,b,k,k1,w11,16
	round_32_47	b,c,d,a,k1,k,w14,23
	round_32_47	a,b,c,d,k,k1,w1,4
	round_32_47	d,a,b,c,k1,k,w4,11
	round_32_47	c,d,a,b,k,k1,w7,16
	round_32_47	b,c,d,a,k1,k,w10,23
	round_32_47	a,b,c,d,k,k1,w13,4
	round_32_47	d,a,b,c,k1,k,w0,11
	round_32_47	c,d,a,b,k,k1,w3,16
	round_32_47	b,c,d,a,k1,k,w6,23
	round_32_47	a,b,c,d,k,k1,w9,4
	round_32_47	d,a,b,c,k1,k,w12,11
	round_32_47	c,d,a,b,k,k1,w15,16
	round_32_47	b,c,d,a,k1,k,w2,23

	round_48_63	a,b,c,d,k,k1,w0,6
	round_48_63	d,a,b,c,k1,k,w7,10
	round_48_63	c,d,a,b,k,k1,w14,15
	round_48_63	b,c,d,a,k1,k,w5,21
	round_48_63	a,b,c,d,k,k1,w12,6
	round_48_63	d,a,b,c,k1,k,w3,10
	round_48_63	c,d,a,b,k,k1,w10,15
	round_48_63	b,c,d,a,k1,k,w1,21
	round_48_63	a,b,c,d,k,k1,w8,6
	round_48_63	d,a,b,c,k1,k,w15,10
	round_48_63	c,d,a,b,k,k1,w6,15
	round_48_63	b,c,d,a,k1,k,w13,21
	round_48_63	a,b,c,d,k,k1,w4,6
	round_48_63	d,a,b,c,k1,k,w11,10
	round_48_63	c,d,a,b,k,k1,w2,15
	round_48_63	b,c,d,a,k1, ,w9,21




	cmp	lane0,end
	add	v_a.4s,v_a1.4s,v_a.4s
	add	v_b.4s,v_b1.4s,v_b.4s
	add	v_c.4s,v_c1.4s,v_c.4s
	add	v_d.4s,v_d1.4s,v_d.4s
	bne	.loop_start

	st4	{v_a.s-v_d.s}[0],[job0]
	st4	{v_a.s-v_d.s}[1],[job1]
	st4	{v_a.s-v_d.s}[2],[job2]
	st4	{v_a.s-v_d.s}[3],[job3]
.exit:
	ldp	d8,d9,[sp,16]
	ldp	d10,d11,[sp,32]
	ldp	x29,x30,[sp],48
	ret
.key_consts:
	.word	0xd76aa478
	.word	0xd76aa478
	.word	0xd76aa478
	.word	0xd76aa478
	.word	0xe8c7b756
	.word	0xe8c7b756
	.word	0xe8c7b756
	.word	0xe8c7b756
	.word	0x242070db
	.word	0x242070db
	.word	0x242070db
	.word	0x242070db
	.word	0xc1bdceee
	.word	0xc1bdceee
	.word	0xc1bdceee
	.word	0xc1bdceee
	.word	0xf57c0faf
	.word	0xf57c0faf
	.word	0xf57c0faf
	.word	0xf57c0faf
	.word	0x4787c62a
	.word	0x4787c62a
	.word	0x4787c62a
	.word	0x4787c62a
	.word	0xa8304613
	.word	0xa8304613
	.word	0xa8304613
	.word	0xa8304613
	.word	0xfd469501
	.word	0xfd469501
	.word	0xfd469501
	.word	0xfd469501
	.word	0x698098d8
	.word	0x698098d8
	.word	0x698098d8
	.word	0x698098d8
	.word	0x8b44f7af
	.word	0x8b44f7af
	.word	0x8b44f7af
	.word	0x8b44f7af
	.word	0xffff5bb1
	.word	0xffff5bb1
	.word	0xffff5bb1
	.word	0xffff5bb1
	.word	0x895cd7be
	.word	0x895cd7be
	.word	0x895cd7be
	.word	0x895cd7be
	.word	0x6b901122
	.word	0x6b901122
	.word	0x6b901122
	.word	0x6b901122
	.word	0xfd987193
	.word	0xfd987193
	.word	0xfd987193
	.word	0xfd987193
	.word	0xa679438e
	.word	0xa679438e
	.word	0xa679438e
	.word	0xa679438e
	.word	0x49b40821
	.word	0x49b40821
	.word	0x49b40821
	.word	0x49b40821
	.word	0xf61e2562
	.word	0xf61e2562
	.word	0xf61e2562
	.word	0xf61e2562
	.word	0xc040b340
	.word	0xc040b340
	.word	0xc040b340
	.word	0xc040b340
	.word	0x265e5a51
	.word	0x265e5a51
	.word	0x265e5a51
	.word	0x265e5a51
	.word	0xe9b6c7aa
	.word	0xe9b6c7aa
	.word	0xe9b6c7aa
	.word	0xe9b6c7aa
	.word	0xd62f105d
	.word	0xd62f105d
	.word	0xd62f105d
	.word	0xd62f105d
	.word	0x02441453
	.word	0x02441453
	.word	0x02441453
	.word	0x02441453
	.word	0xd8a1e681
	.word	0xd8a1e681
	.word	0xd8a1e681
	.word	0xd8a1e681
	.word	0xe7d3fbc8
	.word	0xe7d3fbc8
	.word	0xe7d3fbc8
	.word	0xe7d3fbc8
	.word	0x21e1cde6
	.word	0x21e1cde6
	.word	0x21e1cde6
	.word	0x21e1cde6
	.word	0xc33707d6
	.word	0xc33707d6
	.word	0xc33707d6
	.word	0xc33707d6
	.word	0xf4d50d87
	.word	0xf4d50d87
	.word	0xf4d50d87
	.word	0xf4d50d87
	.word	0x455a14ed
	.word	0x455a14ed
	.word	0x455a14ed
	.word	0x455a14ed
	.word	0xa9e3e905
	.word	0xa9e3e905
	.word	0xa9e3e905
	.word	0xa9e3e905
	.word	0xfcefa3f8
	.word	0xfcefa3f8
	.word	0xfcefa3f8
	.word	0xfcefa3f8
	.word	0x676f02d9
	.word	0x676f02d9
	.word	0x676f02d9
	.word	0x676f02d9
	.word	0x8d2a4c8a
	.word	0x8d2a4c8a
	.word	0x8d2a4c8a
	.word	0x8d2a4c8a
	.word	0xfffa3942
	.word	0xfffa3942
	.word	0xfffa3942
	.word	0xfffa3942
	.word	0x8771f681
	.word	0x8771f681
	.word	0x8771f681
	.word	0x8771f681
	.word	0x6d9d6122
	.word	0x6d9d6122
	.word	0x6d9d6122
	.word	0x6d9d6122
	.word	0xfde5380c
	.word	0xfde5380c
	.word	0xfde5380c
	.word	0xfde5380c
	.word	0xa4beea44
	.word	0xa4beea44
	.word	0xa4beea44
	.word	0xa4beea44
	.word	0x4bdecfa9
	.word	0x4bdecfa9
	.word	0x4bdecfa9
	.word	0x4bdecfa9
	.word	0xf6bb4b60
	.word	0xf6bb4b60
	.word	0xf6bb4b60
	.word	0xf6bb4b60
	.word	0xbebfbc70
	.word	0xbebfbc70
	.word	0xbebfbc70
	.word	0xbebfbc70
	.word	0x289b7ec6
	.word	0x289b7ec6
	.word	0x289b7ec6
	.word	0x289b7ec6
	.word	0xeaa127fa
	.word	0xeaa127fa
	.word	0xeaa127fa
	.word	0xeaa127fa
	.word	0xd4ef3085
	.word	0xd4ef3085
	.word	0xd4ef3085
	.word	0xd4ef3085
	.word	0x04881d05
	.word	0x04881d05
	.word	0x04881d05
	.word	0x04881d05
	.word	0xd9d4d039
	.word	0xd9d4d039
	.word	0xd9d4d039
	.word	0xd9d4d039
	.word	0xe6db99e5
	.word	0xe6db99e5
	.word	0xe6db99e5
	.word	0xe6db99e5
	.word	0x1fa27cf8
	.word	0x1fa27cf8
	.word	0x1fa27cf8
	.word	0x1fa27cf8
	.word	0xc4ac5665
	.word	0xc4ac5665
	.word	0xc4ac5665
	.word	0xc4ac5665
	.word	0xf4292244
	.word	0xf4292244
	.word	0xf4292244
	.word	0xf4292244
	.word	0x432aff97
	.word	0x432aff97
	.word	0x432aff97
	.word	0x432aff97
	.word	0xab9423a7
	.word	0xab9423a7
	.word	0xab9423a7
	.word	0xab9423a7
	.word	0xfc93a039
	.word	0xfc93a039
	.word	0xfc93a039
	.word	0xfc93a039
	.word	0x655b59c3
	.word	0x655b59c3
	.word	0x655b59c3
	.word	0x655b59c3
	.word	0x8f0ccc92
	.word	0x8f0ccc92
	.word	0x8f0ccc92
	.word	0x8f0ccc92
	.word	0xffeff47d
	.word	0xffeff47d
	.word	0xffeff47d
	.word	0xffeff47d
	.word	0x85845dd1
	.word	0x85845dd1
	.word	0x85845dd1
	.word	0x85845dd1
	.word	0x6fa87e4f
	.word	0x6fa87e4f
	.word	0x6fa87e4f
	.word	0x6fa87e4f
	.word	0xfe2ce6e0
	.word	0xfe2ce6e0
	.word	0xfe2ce6e0
	.word	0xfe2ce6e0
	.word	0xa3014314
	.word	0xa3014314
	.word	0xa3014314
	.word	0xa3014314
	.word	0x4e0811a1
	.word	0x4e0811a1
	.word	0x4e0811a1
	.word	0x4e0811a1
	.word	0xf7537e82
	.word	0xf7537e82
	.word	0xf7537e82
	.word	0xf7537e82
	.word	0xbd3af235
	.word	0xbd3af235
	.word	0xbd3af235
	.word	0xbd3af235
	.word	0x2ad7d2bb
	.word	0x2ad7d2bb
	.word	0x2ad7d2bb
	.word	0x2ad7d2bb
	.word	0xeb86d391
	.word	0xeb86d391
	.word	0xeb86d391
	.word	0xeb86d391
	.size md5_mb_asimd_x4, .-md5_mb_asimd_x4

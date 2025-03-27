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

  THIS SOFTmsgARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED msgARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED msgARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  dig_A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OmsgNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOmsgEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, msgHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERmsgISE) ARISING IN ANY msgAY OUT OF THE USE
  OF THIS SOFTmsgARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/
	.arch armv8.2-a
	.text
	.align	2
	.p2align 3,,7

.macro	declare_var_vector_reg name:req,reg:req
	q\name\()	.req	q\reg
	v\name\()	.req	v\reg
	s\name\()	.req	s\reg
.endm

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
	job0_tmp	.req	x10
	job1_tmp	.req	x11
	job2_tmp	.req	x12
	job3_tmp	.req	x13
	const_adr	.req	x14


	declare_var_vector_reg	msg0,0
	declare_var_vector_reg	msg1,1
	declare_var_vector_reg	msg2,2
	declare_var_vector_reg	msg3,3
	declare_var_vector_reg	msg4,4
	declare_var_vector_reg	msg5,5
	declare_var_vector_reg	msg6,6
	declare_var_vector_reg	msg7,7
	declare_var_vector_reg	msg8,8
	declare_var_vector_reg	msg9,9
	declare_var_vector_reg	msg10,10
	declare_var_vector_reg	msg11,11
	declare_var_vector_reg	msg12,12
	declare_var_vector_reg	msg13,13
	declare_var_vector_reg	msg14,14
	declare_var_vector_reg	msg15,15
	declare_var_vector_reg	msg16,16


	declare_var_vector_reg	dig_A,24
	declare_var_vector_reg	dig_B,25
	declare_var_vector_reg	dig_C,26
	declare_var_vector_reg	dig_D,27
	declare_var_vector_reg	dig_E,28
	declare_var_vector_reg	dig_F,29
	declare_var_vector_reg	dig_G,30
	declare_var_vector_reg	dig_H,31

	declare_var_vector_reg	TT1,17
	declare_var_vector_reg	TT2,18
	declare_var_vector_reg	SS1,19
	declare_var_vector_reg	SS2,20
	declare_var_vector_reg	tmp0,21
	declare_var_vector_reg	word_pair,23
	declare_var_vector_reg	Tj,22


.macro rol32	target:req,reg:req,bit:req
	ushr	v\target\().4s,v\reg\().4s,32 - \bit
	sli	v\target\().4s,v\reg\().4s,\bit
.endm

// round 0-11
.macro sm3_round_0	round:req,wp:req

	ushr	vtmp0.4s,vdig_A.4s,32 - 12

	add	vSS1.4s,vdig_E.4s,vTj.4s
	sli	vtmp0.4s,vdig_A.4s,12
	rev32	vmsg\round\().16b,vmsg\round\().16b
	rev32	vmsg\wp\().16b,vmsg\wp\().16b
	add	vTT1.4s,vSS1.4s,vtmp0.4s	//SS1 Done
	rol32	SS1,TT1,7
	eor	vSS2.16b,vSS1.16b,vtmp0.16b	//SS2 Done
	eor	vword_pair.16b,vmsg\round\().16b,vmsg\wp\().16b

	eor	vTT1.16b,vdig_A.16b,vdig_B.16b
	eor	vTT2.16b,vdig_E.16b,vdig_F.16b
	eor	vTT1.16b,vTT1.16b,vdig_C.16b
	eor	vTT2.16b,vTT2.16b,vdig_G.16b

	add	vSS1.4s,vSS1.4s,vmsg\round\().4s
	add	vSS2.4s,vSS2.4s,vword_pair.4s
	add	vTT1.4s,vTT1.4s,vdig_D.4s
	add	vTT2.4s,vTT2.4s,vdig_H.4s
	ushr	vtmp0.4s,vTj.4s,32-1
	add	vTT1.4s,vTT1.4s,vSS2.4s	//TT1 Done
	sli	vtmp0.4s,vTj.4s,1
	add	vTT2.4s,vTT2.4s,vSS1.4s //TT2 Done
	mov	vTj.16b,vtmp0.16b
	//D=C
	mov	vdig_D.16b,vdig_C.16b
	//C = ROTL32(B, 9);
	ushr	vdig_C.4s,vdig_B.4s,32 - 9
	sli	vdig_C.4s,vdig_B.4s,9
	//B=A
	mov	vdig_B.16b,vdig_A.16b
	//A=TT1
	mov	vdig_A.16b,vTT1.16b
	//	H=G
	mov	vdig_H.16b,vdig_G.16b
	//G = ROTL32(F,19)
	rol32	dig_G,dig_F,19
	//F = E
	mov	vdig_F.16b,vdig_E.16b
	// E=Target, TT2=src, TT1,SS1,SS2 is free
	// E = P0(TT2);
	ushr	vSS2.4s, vTT2.4s, 32 - 9
	ushr	vSS1.4s, vTT2.4s, 32 - 17
	sli	vSS2.4s, vTT2.4s, 9
	sli	vSS1.4s, vTT2.4s, 17
	eor	vdig_E.16b, vTT2.16b, vSS1.16b
	eor	vdig_E.16b, vdig_E.16b, vSS2.16b

.endm


.macro sm3_round_4	round:req,wp:req

	ushr	vtmp0.4s,vdig_A.4s,32 - 12
	add	vSS1.4s,vdig_E.4s,vTj.4s
	sli	vtmp0.4s,vdig_A.4s,12
	rev32	vmsg\wp\().16b,vmsg\wp\().16b
	add	vTT1.4s,vSS1.4s,vtmp0.4s	//SS1 Done
	rol32	SS1,TT1,7
	eor	vSS2.16b,vSS1.16b,vtmp0.16b	//SS2 Done
	eor	vword_pair.16b,vmsg\round\().16b,vmsg\wp\().16b
	eor	vTT1.16b,vdig_A.16b,vdig_B.16b
	eor	vTT2.16b,vdig_E.16b,vdig_F.16b
	eor	vTT1.16b,vTT1.16b,vdig_C.16b
	eor	vTT2.16b,vTT2.16b,vdig_G.16b
	add	vSS1.4s,vSS1.4s,vmsg\round\().4s
	add	vSS2.4s,vSS2.4s,vword_pair.4s
	add	vTT1.4s,vTT1.4s,vdig_D.4s
	add	vTT2.4s,vTT2.4s,vdig_H.4s
	ushr	vtmp0.4s,vTj.4s,32-1
	add	vTT1.4s,vTT1.4s,vSS2.4s	//TT1 Done
	sli	vtmp0.4s,vTj.4s,1
	add	vTT2.4s,vTT2.4s,vSS1.4s //TT2 Done
	mov	vTj.16b,vtmp0.16b
	//D=C
	mov	vdig_D.16b,vdig_C.16b
	//C = ROTL32(B, 9);
	ushr	vdig_C.4s,vdig_B.4s,32 - 9
	sli	vdig_C.4s,vdig_B.4s,9
	//B=A
	mov	vdig_B.16b,vdig_A.16b
	//A=TT1
	mov	vdig_A.16b,vTT1.16b
	//	H=G
	mov	vdig_H.16b,vdig_G.16b
	//G = ROTL32(F,19)
	rol32	dig_G,dig_F,19
	//F = E
	mov	vdig_F.16b,vdig_E.16b
	// E=Target, TT2=src, TT1,SS1,SS2 is free
	// E = P0(TT2);
	ushr	vSS2.4s, vTT2.4s, 32 - 9
	ushr	vSS1.4s, vTT2.4s, 32 - 17
	sli	vSS2.4s, vTT2.4s, 9
	sli	vSS1.4s, vTT2.4s, 17
	eor	vdig_E.16b, vTT2.16b, vSS1.16b
	eor	vdig_E.16b, vdig_E.16b, vSS2.16b

.endm

//round 12-15
.macro sm3_round_12	round:req,plus_4:req,m0,m1,m2,m3,m4
	rol32	msg\plus_4,msg\m2,15
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vmsg\m0\().16b
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vmsg\m1\().16b
	rol32	tmp0,msg\plus_4,15
	rol32	word_pair,msg\plus_4,23
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vtmp0.16b
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vword_pair.16b
	rol32	tmp0,msg\m3,7
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vmsg\m4\().16b
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vtmp0.16b
	ushr	vtmp0.4s,vdig_A.4s,32 - 12
	sli	vtmp0.4s,vdig_A.4s,12
	add	vSS1.4s,vdig_E.4s,vTj.4s
	add	vSS2.4s,vSS1.4s,vtmp0.4s	//SS1 Done
	rol32	SS1,SS2,7
	eor	vSS2.16b,vSS1.16b,vtmp0.16b	//SS2 Done
	eor	vword_pair.16b,vmsg\round\().16b,vmsg\plus_4\().16b
	eor	vTT1.16b,vdig_A.16b,vdig_B.16b
	eor	vTT1.16b,vTT1.16b,vdig_C.16b
	eor	vTT2.16b,vdig_E.16b,vdig_F.16b
	eor	vTT2.16b,vTT2.16b,vdig_G.16b
	add	vSS1.4s,vSS1.4s,vmsg\round\().4s
	add	vSS2.4s,vSS2.4s,vword_pair.4s
	add	vTT1.4s,vTT1.4s,vdig_D.4s
	add	vTT2.4s,vTT2.4s,vdig_H.4s
	ushr	vtmp0.4s,vTj.4s,32-1
	add	vTT1.4s,vTT1.4s,vSS2.4s	//TT1 Done
	sli	vtmp0.4s,vTj.4s,1
	add	vTT2.4s,vTT2.4s,vSS1.4s //TT2 Done
	mov	vTj.16b,vtmp0.16b
	//D=C
	mov	vdig_D.16b,vdig_C.16b
	//C = ROTL32(B, 9);
	ushr	vdig_C.4s,vdig_B.4s,32 - 9
	sli	vdig_C.4s,vdig_B.4s,9
	//B=A
	mov	vdig_B.16b,vdig_A.16b
	//A=TT1
	mov	vdig_A.16b,vTT1.16b
	//	H=G
	mov	vdig_H.16b,vdig_G.16b
	//G = ROTL32(F,19)
	rol32	dig_G,dig_F,19
	//F = E
	mov	vdig_F.16b,vdig_E.16b
	// E=Target, TT2=src, TT1,SS1,SS2 is free
	// E = P0(TT2);
	ushr	vSS2.4s, vTT2.4s, 32 - 9
	ushr	vSS1.4s, vTT2.4s, 32 - 17
	sli	vSS2.4s, vTT2.4s, 9
	sli	vSS1.4s, vTT2.4s, 17
	eor	vdig_E.16b, vTT2.16b, vSS1.16b
	eor	vdig_E.16b, vdig_E.16b, vSS2.16b
.endm

// round 16-62
.macro sm3_round_16	round:req,plus_4:req,m0,m1,m2,m3,m4
	rol32	msg\plus_4,msg\m2,15
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vmsg\m0\().16b
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vmsg\m1\().16b
	rol32	tmp0,msg\plus_4,15
	rol32	word_pair,msg\plus_4,23
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vtmp0.16b
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vword_pair.16b
	rol32	tmp0,msg\m3,7
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vmsg\m4\().16b
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vtmp0.16b
	ushr	vtmp0.4s,vdig_A.4s,32 - 12
	sli	vtmp0.4s,vdig_A.4s,12
	add	vSS1.4s,vdig_E.4s,vTj.4s
	add	vSS2.4s,vSS1.4s,vtmp0.4s	//SS1 Done
	rol32	SS1,SS2,7
	eor	vSS2.16b,vSS1.16b,vtmp0.16b	//SS2 Done
	eor	vword_pair.16b,vmsg\round\().16b,vmsg\plus_4\().16b
	mov	vTT2.16b,vdig_E.16b
	orr	vTT1.16b,vdig_B.16b,vdig_C.16b
	and	vtmp0.16b,vdig_B.16b,vdig_C.16b
	bsl	vTT2.16b,vdig_F.16b,vdig_G.16b
	and	vTT1.16b,vTT1.16b,vdig_A.16b
	add	vSS1.4s,vSS1.4s,vmsg\round\().4s
	orr	vTT1.16b,vTT1.16b,vtmp0.16b
	add	vSS2.4s,vSS2.4s,vword_pair.4s
	add	vTT1.4s,vTT1.4s,vdig_D.4s
	add	vTT2.4s,vTT2.4s,vdig_H.4s
	ushr	vtmp0.4s,vTj.4s,32-1
	add	vTT1.4s,vTT1.4s,vSS2.4s	//TT1 Done
	sli	vtmp0.4s,vTj.4s,1
	add	vTT2.4s,vTT2.4s,vSS1.4s //TT2 Done
	mov	vTj.16b,vtmp0.16b
	//D=C
	mov	vdig_D.16b,vdig_C.16b
	//C = ROTL32(B, 9);
	ushr	vdig_C.4s,vdig_B.4s,32 - 9
	sli	vdig_C.4s,vdig_B.4s,9
	//B=A
	mov	vdig_B.16b,vdig_A.16b
	//A=TT1
	mov	vdig_A.16b,vTT1.16b
	//	H=G
	mov	vdig_H.16b,vdig_G.16b
	//G = ROTL32(F,19)
	rol32	dig_G,dig_F,19
	//F = E
	mov	vdig_F.16b,vdig_E.16b
	// E=Target, TT2=src, TT1,SS1,SS2 is free
	// E = P0(TT2);
	ushr	vSS2.4s, vTT2.4s, 32 - 9
	ushr	vSS1.4s, vTT2.4s, 32 - 17
	sli	vSS2.4s, vTT2.4s, 9
	sli	vSS1.4s, vTT2.4s, 17
	eor	vdig_E.16b, vTT2.16b, vSS1.16b
	eor	vdig_E.16b, vdig_E.16b, vSS2.16b
.endm

//round 63
.macro sm3_round_63	round:req,plus_4:req,m0,m1,m2,m3,m4
	rol32	msg\plus_4,msg\m2,15
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vmsg\m0\().16b
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vmsg\m1\().16b
	rol32	tmp0,msg\plus_4,15
	rol32	word_pair,msg\plus_4,23
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vtmp0.16b
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vword_pair.16b
	rol32	tmp0,msg\m3,7
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vmsg\m4\().16b
	eor	vmsg\plus_4\().16b,vmsg\plus_4\().16b,vtmp0.16b
	ushr	vtmp0.4s,vdig_A.4s,32 - 12
	sli	vtmp0.4s,vdig_A.4s,12
	add	vSS1.4s,vdig_E.4s,vTj.4s
	add	vSS2.4s,vSS1.4s,vtmp0.4s	//SS1 Done
	rol32	SS1,SS2,7
	eor	vSS2.16b,vSS1.16b,vtmp0.16b	//SS2 Done
	eor	vword_pair.16b,vmsg\round\().16b,vmsg\plus_4\().16b

	ldp	qmsg0,qmsg1,[sp,dig_off+   0]
	mov	vTT2.16b,vdig_E.16b
	ldp	qmsg2,qmsg3,[sp,dig_off+  32]
	orr	vTT1.16b,vdig_B.16b,vdig_C.16b
	ldp	qmsg4,qmsg5,[sp,dig_off+  64]
	and	vtmp0.16b,vdig_B.16b,vdig_C.16b
	bsl	vTT2.16b,vdig_F.16b,vdig_G.16b
	ldp	qmsg6,qmsg7,[sp,dig_off+  96]
	and	vTT1.16b,vTT1.16b,vdig_A.16b
	add	vSS1.4s,vSS1.4s,vmsg\round\().4s
	orr	vTT1.16b,vTT1.16b,vtmp0.16b
	add	vSS2.4s,vSS2.4s,vword_pair.4s
	add	vTT1.4s,vTT1.4s,vdig_D.4s
	add	vTT2.4s,vTT2.4s,vdig_H.4s
	add	vTT1.4s,vTT1.4s,vSS2.4s	//TT1 Done
	add	vTT2.4s,vTT2.4s,vSS1.4s //TT2 Done
	//D=C
	eor	vdig_D.16b,vdig_C.16b,vmsg3.16b
	//C = ROTL32(B, 9);
	ushr	vdig_C.4s,vdig_B.4s,32 - 9
	sli	vdig_C.4s,vdig_B.4s,9
	eor	vdig_C.16b,vdig_C.16b,vmsg2.16b
	//B=A
	eor	vdig_B.16b,vdig_A.16b,vmsg1.16b
	stp	qdig_C,qdig_D,[sp,dig_off+  32]
	//A=TT1
	eor	vdig_A.16b,vTT1.16b,vmsg0.16b
	//	H=G
	eor	vdig_H.16b,vdig_G.16b,vmsg7.16b
	stp	qdig_A,qdig_B,[sp,dig_off+   0]
	//G = ROTL32(F,19)
	rol32	dig_G,dig_F,19
	eor	vdig_G.16b,vdig_G.16b,vmsg6.16b
	//F = E
	eor	vdig_F.16b,vdig_E.16b,vmsg5.16b
	stp	qdig_G,qdig_H,[sp,dig_off+  96]
	// E=Target, TT2=src, TT1,SS1,SS2 is free
	// E = P0(TT2);
	ushr	vSS2.4s, vTT2.4s, 32 - 9
	ushr	vSS1.4s, vTT2.4s, 32 - 17
	sli	vSS2.4s, vTT2.4s, 9
	sli	vSS1.4s, vTT2.4s, 17
	eor	vdig_E.16b, vTT2.16b, vSS1.16b
	eor	vdig_E.16b, vdig_E.16b, vSS2.16b
	eor	vdig_E.16b, vdig_E.16b, vmsg4.16b
	stp	qdig_E,qdig_F,[sp,dig_off+  64]
.endm

	.set	dig_off , 80

#define STACK_SIZE	224
	.global	sm3_mb_asimd_x4
	.type	sm3_mb_asimd_x4, %function
sm3_mb_asimd_x4:
	stp	x29,x30, [sp,-STACK_SIZE]!
	cmp	len,0
	//push d8~d15
	ldr	job0_data, [job0],64
	stp 	d8,d9,  [sp,16]
	ldr	job1_data, [job1],64
	stp 	d10,d11,[sp,32]
	ldr	job2_data, [job2],64
	stp 	d12,d13,[sp,48]
	ldr	job3_data, [job3],64
	stp 	d14,d15,[sp,64]
	ble	.exit_func

	mov	job0_tmp,job0_digest
	mov	job1_tmp,job1_digest
	mov	job2_tmp,job2_digest
	mov	job3_tmp,job3_digest
	//load digests
	ld4	{vdig_A.s-vdig_D.s}[0],[job0_tmp],16
	ld4	{vdig_A.s-vdig_D.s}[1],[job1_tmp],16
	ld4	{vdig_A.s-vdig_D.s}[2],[job2_tmp],16
	adrp	const_adr, .consts
	ld4	{vdig_A.s-vdig_D.s}[3],[job3_tmp],16
	add	const_adr, const_adr, #:lo12:.consts
	ld4	{vdig_E.s-vdig_H.s}[0],[job0_tmp]
	rev32	vdig_A.16b,vdig_A.16b
	ld4	{vdig_E.s-vdig_H.s}[1],[job1_tmp]
	rev32	vdig_B.16b,vdig_B.16b
	ld4	{vdig_E.s-vdig_H.s}[2],[job2_tmp]
	rev32	vdig_C.16b,vdig_C.16b
	ld4	{vdig_E.s-vdig_H.s}[3],[job3_tmp]
	rev32	vdig_D.16b,vdig_D.16b
	stp	qdig_A,qdig_B,[sp,dig_off+  0]
	rev32	vdig_E.16b,vdig_E.16b
	rev32	vdig_F.16b,vdig_F.16b
	stp	qdig_C,qdig_D,[sp,dig_off+ 32]
	rev32	vdig_G.16b,vdig_G.16b
	rev32	vdig_H.16b,vdig_H.16b
	stp	qdig_E,qdig_F,[sp,dig_off+ 64]
	stp	qdig_G,qdig_H,[sp,dig_off+ 96]

.start_loop:
	ld4	{vmsg0.s-vmsg3.s}[0],[job0_data],16
	ld4	{vmsg0.s-vmsg3.s}[1],[job1_data],16
	ld4	{vmsg0.s-vmsg3.s}[2],[job2_data],16
	ld4	{vmsg0.s-vmsg3.s}[3],[job3_data],16
	ld4	{vmsg4.s-vmsg7.s}[0],[job0_data],16
	ld4	{vmsg4.s-vmsg7.s}[1],[job1_data],16
	ld4	{vmsg4.s-vmsg7.s}[2],[job2_data],16
	ld4	{vmsg4.s-vmsg7.s}[3],[job3_data],16
	ld4	{vmsg8.s-vmsg11.16b}[0],[job0_data],16
	ldr	qTj,[const_adr]

	sm3_round_0	 0, 4

	ld4	{vmsg8.s-vmsg11.s}[1],[job1_data],16
	sm3_round_0	 1, 5

	ld4	{vmsg8.s-vmsg11.s}[2],[job2_data],16
	sm3_round_0	 2, 6
	ld4	{vmsg8.s-vmsg11.s}[3],[job3_data],16
	sm3_round_0	 3, 7

	ld4	{vmsg12.s-vmsg15.s}[0],[job0_data],16

	sm3_round_4	 4, 8
	ld4	{vmsg12.s-vmsg15.s}[1],[job1_data],16
	sm3_round_4	 5, 9
	ld4	{vmsg12.s-vmsg15.s}[2],[job2_data],16
	sm3_round_4	 6,10
	ld4	{vmsg12.s-vmsg15.s}[3],[job3_data],16
	sm3_round_4	 7,11
	sm3_round_4	 8,12
	sm3_round_4	 9,13
	sm3_round_4	10,14
	sm3_round_4	11,15

	sm3_round_12	12,16, 0, 7,13, 3,10 //12
	sm3_round_12	13, 0, 1, 8,14, 4,11 //13
	sm3_round_12	14, 1, 2, 9,15, 5,12 //14
	sm3_round_12	15, 2, 3,10,16, 6,13 //15

	ldr	qTj,[const_adr,16]
	sm3_round_16	16, 3, 4,11, 0, 7,14 //16
#if 0
	stp	sdig_A,sdig_B,[job0_digest]
	stp	sdig_C,sdig_D,[job0_digest,8]
	stp	sdig_E,sdig_F,[job0_digest,16]
	stp	sdig_G,sdig_H,[job0_digest,24]
	b .exit_func
#endif
	sm3_round_16	 0, 4, 5,12, 1, 8,15 //17

	sm3_round_16	 1, 5, 6,13, 2, 9,16 //18
	sm3_round_16	 2, 6, 7,14, 3,10, 0 //19
	sm3_round_16	 3, 7, 8,15, 4,11, 1 //20
	sm3_round_16	 4, 8, 9,16, 5,12, 2 //21
	sm3_round_16	 5, 9,10, 0, 6,13, 3 //22
	sm3_round_16	 6,10,11, 1, 7,14, 4 //23
	sm3_round_16	 7,11,12, 2, 8,15, 5 //24
	sm3_round_16	 8,12,13, 3, 9,16, 6 //25
	sm3_round_16	 9,13,14, 4,10, 0, 7 //26
	sm3_round_16	10,14,15, 5,11, 1, 8 //27
	sm3_round_16	11,15,16, 6,12, 2, 9 //28
	sm3_round_16	12,16, 0, 7,13, 3,10 //29
	sm3_round_16	13, 0, 1, 8,14, 4,11 //30
	sm3_round_16	14, 1, 2, 9,15, 5,12 //31
	sm3_round_16	15, 2, 3,10,16, 6,13 //32
	sm3_round_16	16, 3, 4,11, 0, 7,14 //33
	sm3_round_16	 0, 4, 5,12, 1, 8,15 //34
	sm3_round_16	 1, 5, 6,13, 2, 9,16 //35
	sm3_round_16	 2, 6, 7,14, 3,10, 0 //36
	sm3_round_16	 3, 7, 8,15, 4,11, 1 //37
	sm3_round_16	 4, 8, 9,16, 5,12, 2 //38
	sm3_round_16	 5, 9,10, 0, 6,13, 3 //39
	sm3_round_16	 6,10,11, 1, 7,14, 4 //40
	sm3_round_16	 7,11,12, 2, 8,15, 5 //41
	sm3_round_16	 8,12,13, 3, 9,16, 6 //42
	sm3_round_16	 9,13,14, 4,10, 0, 7 //43
	sm3_round_16	10,14,15, 5,11, 1, 8 //44
	sm3_round_16	11,15,16, 6,12, 2, 9 //45
	sm3_round_16	12,16, 0, 7,13, 3,10 //46
	sm3_round_16	13, 0, 1, 8,14, 4,11 //47
	sm3_round_16	14, 1, 2, 9,15, 5,12 //48
	sm3_round_16	15, 2, 3,10,16, 6,13 //49
	sm3_round_16	16, 3, 4,11, 0, 7,14 //50
	sm3_round_16	 0, 4, 5,12, 1, 8,15 //51
	sm3_round_16	 1, 5, 6,13, 2, 9,16 //52
	sm3_round_16	 2, 6, 7,14, 3,10, 0 //53
	sm3_round_16	 3, 7, 8,15, 4,11, 1 //54
	sm3_round_16	 4, 8, 9,16, 5,12, 2 //55
	sm3_round_16	 5, 9,10, 0, 6,13, 3 //56
	sm3_round_16	 6,10,11, 1, 7,14, 4 //57
	sm3_round_16	 7,11,12, 2, 8,15, 5 //58
	sm3_round_16	 8,12,13, 3, 9,16, 6 //59
	sm3_round_16	 9,13,14, 4,10, 0, 7 //60
	sm3_round_16	10,14,15, 5,11, 1, 8 //61
	sm3_round_16	11,15,16, 6,12, 2, 9 //62
	sm3_round_63	12,16, 0, 7,13, 3,10 //63

	subs		len,len,1
	bne		.start_loop

	//save digests with big endian
	rev32	vdig_A.16b,vdig_A.16b
	rev32	vdig_B.16b,vdig_B.16b
	rev32	vdig_C.16b,vdig_C.16b
	rev32	vdig_D.16b,vdig_D.16b
	st4	{vdig_A.s-vdig_D.s}[0],[job0_digest],16
	rev32	vdig_E.16b,vdig_E.16b
	rev32	vdig_F.16b,vdig_F.16b
	st4	{vdig_A.s-vdig_D.s}[1],[job1_digest],16
	rev32	vdig_G.16b,vdig_G.16b
	rev32	vdig_H.16b,vdig_H.16b
	st4	{vdig_A.s-vdig_D.s}[2],[job2_digest],16
	st4	{vdig_A.s-vdig_D.s}[3],[job3_digest],16
	st4	{vdig_E.s-vdig_H.s}[0],[job0_digest]
	st4	{vdig_E.s-vdig_H.s}[1],[job1_digest]
	st4	{vdig_E.s-vdig_H.s}[2],[job2_digest]
	st4	{vdig_E.s-vdig_H.s}[3],[job3_digest]

.exit_func:
	ldp     d8, d9, [sp,16]
	ldp 	d10,d11,[sp,32]
	ldp 	d12,d13,[sp,48]
	ldp 	d14,d15,[sp,64]
	ldp	x29, x30, [sp], STACK_SIZE
	ret
.consts:
	.word	0x79cc4519
	.word	0x79cc4519
	.word	0x79cc4519
	.word	0x79cc4519
	.word	0x9d8a7a87
	.word	0x9d8a7a87
	.word	0x9d8a7a87
	.word	0x9d8a7a87
	.size	sm3_mb_asimd_x4, .-sm3_mb_asimd_x4


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

	job	.req	x0
	len	.req	x1
	data	.req	x2
	digest	.req	x0

	msg0	.req	w3
	msg1	.req	w4
	msg2	.req	w5
	msg3	.req	w6
	msg4	.req	w7

	msg	.req	w9
	msgP	.req	w10
	SS1	.req	w11
	SS2	.req	w12
	TT1	.req	w13
	TT2	.req	w14
	Tj	.req	w15
	tmp0	.req	w19
	tmp1	.req	w20
	dig_A	.req	w21
	dig_B	.req	w22
	dig_C	.req	w23
	dig_D	.req	w24
	dig_E	.req	w25
	dig_F	.req	w26
	dig_G	.req	w27
	dig_H	.req	w28

	declare_var_vector_reg	dig0,0
	declare_var_vector_reg	dig1,1
	declare_var_vector_reg	dig0_bak,2
	declare_var_vector_reg	dig1_bak,3
	declare_var_vector_reg	vect_msg0,4
	declare_var_vector_reg	vect_msg1,5
	declare_var_vector_reg	vect_msg2,6
	declare_var_vector_reg	vect_msg3,7

	declare_var_vector_reg	vect_msgP0,16
	declare_var_vector_reg	vect_msgP1,17
	declare_var_vector_reg	vect_msgP2,18






// round 0-11
.macro sm3_round_0	round:req
	ldr	msg, [sp,msg_off+4*\round\()]
	ldr	msgP,[sp,wp_off +4*\round\()]
	add	SS1,dig_E,Tj
	ror	TT1,dig_A,32-12
	add	SS1,SS1,TT1
	ror	SS1,SS1,32-7	//SS1 done
	eor	SS2,SS1,TT1	//SS2 done
	eor	TT1,dig_A,dig_B
	eor	TT2,dig_E,dig_F
	add	SS2,SS2,msgP
	eor	TT2,TT2,dig_G
	add	SS1,SS1,msg
	eor	TT1,TT1,dig_C
	add	SS2,SS2,dig_D
	add	SS1,SS1,dig_H
	add	TT1,TT1,SS2
	add	TT2,TT2,SS1
	mov	dig_D,dig_C
	ror	dig_C,dig_B,32-9
	mov	dig_B,dig_A
	mov	dig_A,TT1
	eor	TT1,TT2,TT2,ror (32-17)
	mov	dig_H,dig_G
	ror	dig_G,dig_F,32-19
	mov	dig_F,dig_E
	eor	dig_E,TT1,TT2,ror(32-9)
	ror	Tj,Tj,(32-1)
.endm

//round 12-15
.macro sm3_round_12	round:req
	ldr	msg, [sp,msg_off+4*((\round\())%17)]
	ldr	msg0,[sp,msg_off+4*((\round\()+4 - 16)%17)]
	ldr	msg1,[sp,msg_off+4*((\round\()+4 - 9)%17)]
	add	SS1,dig_E,Tj
	ror	TT1,dig_A,32-12
	add	SS1,SS1,TT1
	ror	SS1,SS1,32-7	//SS1 done
	eor	SS2,SS1,TT1	//SS2 done

	eor	msg0,msg0,msg1
	ldr	msg2,[sp,msg_off+4*((\round\()+4 - 3)%17)]
	eor	TT1,dig_A,dig_B
	eor	TT2,dig_E,dig_F
	add	SS2,SS2,dig_D
	eor	TT2,TT2,dig_G
	add	SS1,SS1,msg
	eor	msg0,msg0,msg2,ror (32-15)
	ldr	msg3,[sp,msg_off+4*((\round\()+4 - 13)%17)]
	ldr	msg4,[sp,msg_off+4*((\round\()+4 -  6)%17)]
	eor	msg1,msg0,msg0,ror (32 -15)
	eor	TT1,TT1,dig_C
	add	TT1,TT1,SS2
	eor	msg4,msg4,msg3, ror (32-7)
	eor	msg0,msg1,msg0, ror (32-23)
	add	SS1,SS1,dig_H
	eor	msg0,msg0,msg4
	add	TT2,TT2,SS1
	mov	dig_D,dig_C
	str	msg0,[sp,msg_off+4*((\round\()+4)%17)]
	eor	msgP,msg,msg0
	add	TT1,TT1,msgP
	ror	dig_C,dig_B,32-9
	mov	dig_B,dig_A
	mov	dig_A,TT1
	eor	TT1,TT2,TT2,ror (32-17)
	mov	dig_H,dig_G
	ror	dig_G,dig_F,32-19
	mov	dig_F,dig_E
	eor	dig_E,TT1,TT2,ror(32-9)
	ror	Tj,Tj,32-1
.endm

// round 16-62
.macro sm3_round_16	round:req
	ldr	msg, [sp,msg_off+4*((\round\())%17)]
	ldr	msg0,[sp,msg_off+4*((\round\()+4 - 16)%17)]
	ldr	msg1,[sp,msg_off+4*((\round\()+4 - 9)%17)]
	add	SS1,dig_E,Tj
	ror	TT1,dig_A,32-12
	add	SS1,SS1,TT1
	ror	SS1,SS1,32-7	//SS1 done
	eor	SS2,SS1,TT1	//SS2 done

	eor	msg0,msg0,msg1
	ldr	msg2,[sp,msg_off+4*((\round\()+4 - 3)%17)]
	orr	TT1,dig_B,dig_C
	and	tmp0,dig_B,dig_C

	eor	TT2,dig_F,dig_G
	and	TT1,TT1,dig_A
	add	SS2,SS2,dig_D
	orr	TT1,TT1,tmp0
	and	TT2,TT2,dig_E
	add	SS1,SS1,msg
	eor	TT2,TT2,dig_G

	eor	msg0,msg0,msg2,ror (32-15)
	ldr	msg3,[sp,msg_off+4*((\round\()+4 - 13)%17)]
	ldr	msg4,[sp,msg_off+4*((\round\()+4 -  6)%17)]
	eor	msg1,msg0,msg0,ror (32 -15)
	add	TT1,TT1,SS2
	eor	msg4,msg4,msg3, ror (32-7)
	eor	msg0,msg1,msg0, ror (32-23)
	add	SS1,SS1,dig_H
	eor	msg0,msg0,msg4
	add	TT2,TT2,SS1
	mov	dig_D,dig_C
	str	msg0,[sp,msg_off+4*((\round\()+4)%17)]
	eor	msgP,msg,msg0
	add	TT1,TT1,msgP
	ror	dig_C,dig_B,32-9
	mov	dig_B,dig_A
	mov	dig_A,TT1
	eor	TT1,TT2,TT2,ror (32-17)
	mov	dig_H,dig_G
	ror	dig_G,dig_F,32-19
	mov	dig_F,dig_E
	eor	dig_E,TT1,TT2,ror(32-9)
	ror	Tj,Tj,32-1
.endm

//round 63
.macro sm3_round_63	round:req
	ldr	msg, [sp,msg_off+4*((\round\())%17)]
	ldr	msg0,[sp,msg_off+4*((\round\()+4 - 16)%17)]
	ldr	msg1,[sp,msg_off+4*((\round\()+4 - 9)%17)]
	add	SS1,dig_E,Tj
	ror	TT1,dig_A,32-12
	add	SS1,SS1,TT1
	ror	SS1,SS1,32-7	//SS1 done
	eor	SS2,SS1,TT1	//SS2 done
	eor	msg0,msg0,msg1
	ldr	msg2,[sp,msg_off+4*((\round\()+4 - 3)%17)]
	orr	TT1,dig_B,dig_C
	and	tmp0,dig_B,dig_C
	eor	TT2,dig_F,dig_G
	and	TT1,TT1,dig_A
	add	SS2,SS2,dig_D
	orr	TT1,TT1,tmp0
	and	TT2,TT2,dig_E
	add	SS1,SS1,msg
	eor	TT2,TT2,dig_G
	eor	msg0,msg0,msg2,ror (32-15)
	ldr	msg3,[sp,msg_off+4*((\round\()+4 - 13)%17)]
	ldr	msg4,[sp,msg_off+4*((\round\()+4 -  6)%17)]
	eor	msg1,msg0,msg0,ror (32 -15)
	add	TT1,TT1,SS2
	eor	msg4,msg4,msg3, ror (32-7)
	eor	msg0,msg1,msg0, ror (32-23)
	add	SS1,SS1,dig_H
	eor	msg0,msg0,msg4
	add	TT2,TT2,SS1
	str	msg0,[sp,msg_off+4*((\round\()+4)%17)]
	eor	msgP,msg,msg0
	add	TT1,TT1,msgP
	ins	vdig0_bak.s[3],dig_C
	ror	dig_C,dig_B,32-9
	ins	vdig0_bak.s[1],dig_A
	ins	vdig0_bak.s[0],TT1
	ins	vdig0_bak.s[2],dig_C
	eor	TT1,TT2,TT2,ror (32-17)
	ins	vdig1_bak.s[3],dig_G
	ror	dig_G,dig_F,32-19
	ins	vdig1_bak.s[1],dig_E
	ins	vdig1_bak.s[2],dig_G
	eor	dig_E,TT1,TT2,ror(32-9)
	ins	vdig1_bak.s[0],dig_E
.endm

	.set	wp_off , 96
	.set	msg_off, 96 + 12*4
#define STACK_SIZE	224
	.global	sm3_mb_asimd_x1
	.type	sm3_mb_asimd_x1, %function
sm3_mb_asimd_x1:
	stp	x29,x30, [sp,-STACK_SIZE]!
	cmp	len,0
	ldr	data,[job],64
	ldp	qdig0,qdig1,[digest]
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	rev32	vdig0.16b,vdig0.16b
	stp	x23, x24, [sp, 48]
	rev32	vdig1.16b,vdig1.16b
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	ble	.exit_func

.start_loop:

	/** prepare first 12 round data **/
	ld1	{vvect_msg0.16b-vvect_msg3.16b},[data],64
	mov 	Tj, 17689
	umov	dig_A,vdig0.s[0]
	movk	Tj, 0x79cc, lsl 16
	rev32	vvect_msg0.16b,vvect_msg0.16b
	umov	dig_B,vdig0.s[1]
	rev32	vvect_msg1.16b,vvect_msg1.16b
	umov	dig_C,vdig0.s[2]
	rev32	vvect_msg2.16b,vvect_msg2.16b
	umov	dig_D,vdig0.s[3]
	rev32	vvect_msg3.16b,vvect_msg3.16b
	umov	dig_E,vdig1.s[0]
	stp	qvect_msg0,qvect_msg1,[sp,msg_off]
	umov	dig_F,vdig1.s[1]
	stp	qvect_msg2,qvect_msg3,[sp,msg_off+32]
	umov	dig_G,vdig1.s[2]
	eor	vvect_msgP0.16b,vvect_msg0.16b,vvect_msg1.16b
	eor	vvect_msgP1.16b,vvect_msg1.16b,vvect_msg2.16b
	umov	dig_H,vdig1.s[3]
	stp	qvect_msgP0,qvect_msgP1,[sp,wp_off]
	eor	vvect_msgP2.16b,vvect_msg2.16b,vvect_msg3.16b
	str	qvect_msgP2,[sp,wp_off+32]

	sm3_round_0	 0
	sm3_round_0	 1
	sm3_round_0	 2
	sm3_round_0	 3
	sm3_round_0	 4
	sm3_round_0	 5
	sm3_round_0	 6
	sm3_round_0	 7
	sm3_round_0	 8
	sm3_round_0	 9
	sm3_round_0	10
	sm3_round_0	11

	sm3_round_12	12
	sm3_round_12	13
	sm3_round_12	14
	sm3_round_12	15
	mov	Tj, 0x7a87
	movk	Tj, 0x9d8a, lsl 16
	sm3_round_16	16
	sm3_round_16	17
	sm3_round_16	18
	sm3_round_16	19
	sm3_round_16	20
	sm3_round_16	21
	sm3_round_16	22
	sm3_round_16	23
	sm3_round_16	24
	sm3_round_16	25
	sm3_round_16	26
	sm3_round_16	27
	sm3_round_16	28
	sm3_round_16	29
	sm3_round_16	30
	sm3_round_16	31
	sm3_round_16	32
	sm3_round_16	33
	sm3_round_16	34
	sm3_round_16	35
	sm3_round_16	36
	sm3_round_16	37
	sm3_round_16	38
	sm3_round_16	39
	sm3_round_16	40
	sm3_round_16	41
	sm3_round_16	42
	sm3_round_16	43
	sm3_round_16	44
	sm3_round_16	45
	sm3_round_16	46
	sm3_round_16	47
	sm3_round_16	48
	sm3_round_16	49
	sm3_round_16	50
	sm3_round_16	51
	sm3_round_16	52
	sm3_round_16	53
	sm3_round_16	54
	sm3_round_16	55
	sm3_round_16	56
	sm3_round_16	57
	sm3_round_16	58
	sm3_round_16	59
	sm3_round_16	60
	sm3_round_16	61
	sm3_round_16	62
	sm3_round_63	63
	subs		len,len,1
	eor		vdig0.16b,vdig0.16b,vdig0_bak.16b
	eor		vdig1.16b,vdig1.16b,vdig1_bak.16b
	bne	.start_loop
.exit_func:
	ldp	x19, x20, [sp, 16]
	rev32	vdig0.16b,vdig0.16b
	ldp	x21, x22, [sp, 32]
	rev32	vdig1.16b,vdig1.16b
	ldp	x23, x24, [sp, 48]
	stp	qdig0,qdig1,[digest]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	ldp	x29, x30, [sp], STACK_SIZE
	ret
	.size	sm3_mb_asimd_x1, .-sm3_mb_asimd_x1


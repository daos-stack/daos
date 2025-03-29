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
	uint64_t rolling_hash2_run_until_unroll(
		uint32_t * idx, int max_idx, uint64_t * t1,
		uint64_t * t2, uint8_t * b1, uint8_t * b2, uint64_t h,
		uint64_t mask, uint64_t trigger)
*/
	idx_addr	.req	x0
	max_idx		.req	w1	//signed int
	t1_addr		.req	x2
	t2_addr		.req	x3
	b1_addr		.req	x4
	b2_addr		.req	x5
	h		.req	x6
	mask		.req	x7
	trigger		.req	x12

	idx		.req	w8

	dat1		.req	x9
	dat2		.req	x10
	wdat1		.req	w9
	wdat2		.req	w10
	tmp_loop	.req	w11

	t1		.req	x13
	t2		.req	x14

.macro	round	off:req
	ubfx	t1,dat1,8*\off\(),8
	ubfx	t2,dat2,8*\off\(),8
	ldr	t1,[t1_addr,t1,lsl 3]
	ldr	t2,[t2_addr,t2,lsl 3]
	eor	t1,t2,t1
	eor	h,t1,h,ror 63
	and	t2,h,mask
	cmp	t2,trigger
	beq	exit_ret
	add 	idx, idx,1
.endm
	.global	rolling_hash2_run_until_unroll
	.type	rolling_hash2_run_until_unroll, %function
rolling_hash2_run_until_unroll:
	ldr	trigger,[sp]
	ldr	idx,[idx_addr]
	sub	tmp_loop,max_idx,8
	cmp	idx,tmp_loop
	bge	unroll_loop_end
unroll_loop:
	ldr	dat1,[b1_addr,idx,sxtw]
	ldr	dat2,[b2_addr,idx,sxtw]

	round	0
	round	1
	round	2
	round	3
	round	4
	round	5
	round	6
	round	7
	cmp	tmp_loop,idx
	bgt	unroll_loop
unroll_loop_end:
	cmp	idx,max_idx
	bge	exit_ret
loop:
	ldrb	wdat1,[b1_addr,idx,sxtw]
	ldrb	wdat2,[b2_addr,idx,sxtw]
	ldr	t1,[t1_addr,dat1,lsl 3]
	ldr	t2,[t2_addr,dat2,lsl 3]
	eor	t1,t2,t1
	eor	h,t1,h,ror 63
	and	t2,h,mask
	cmp	t2,trigger
	beq	exit_ret
	add	idx,idx,1
	cmp	max_idx,idx
	bgt	loop
exit_ret:
	str	idx,[idx_addr]
	mov	x0,h
	ret
	.size	rolling_hash2_run_until_unroll, .-rolling_hash2_run_until_unroll


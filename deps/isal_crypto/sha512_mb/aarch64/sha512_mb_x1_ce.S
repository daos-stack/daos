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
	.arch armv8.2-a+crypto+sha3
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
maros for round 0-63
*/
.macro sha512_rounds_low ab,cd,ef,gh,tmp,msg0,msg1,msg4,msg5,msg7
	ldr		key_q , [key_adr]
	add		l0_tmp0_v.2d,l0_\msg0\()_v.2d,key_v.2d
	add		key_adr,key_adr,16
	ext		l0_tmp1_v.16b,l0_\ef\()_v.16b,l0_\gh\()_v.16b,#8
	ext		l0_tmp0_v.16b,l0_tmp0_v.16b,l0_tmp0_v.16b,#8
	ext		l0_tmp2_v.16b,l0_\cd\()_v.16b,l0_\ef\()_v.16b,#8
	add		l0_\gh\()_v.2d,l0_\gh\()_v.2d,l0_tmp0_v.2d
	ext		l0_tmp0_v.16b,l0_\msg4\()_v.16b,l0_\msg5\()_v.16b,#8
	sha512su0	l0_\msg0\()_v.2d,l0_\msg1\()_v.2d
	sha512h		l0_\gh\()_q,l0_tmp1_q,l0_tmp2_v.2d
	sha512su1	l0_\msg0\()_v.2d,l0_\msg7\()_v.2d,l0_tmp0_v.2d
	add		l0_\tmp\()_v.2d,l0_\cd\()_v.2d,l0_\gh\()_v.2d
	sha512h2	l0_\gh\()_q,l0_\cd\()_q,l0_\ab\()_v.2d
.endm
/**
maros for round 64-79
*/
.macro sha512_rounds_high	ab,cd,ef,gh,tmp,msg0
	ldr		key_q , [key_adr]
	add		l0_tmp0_v.2d,l0_\msg0\()_v.2d,key_v.2d
	add		key_adr,key_adr,16
	ext		l0_tmp1_v.16b,l0_\ef\()_v.16b,l0_\gh\()_v.16b,#8
	ext		l0_tmp0_v.16b,l0_tmp0_v.16b,l0_tmp0_v.16b,#8
	ext		l0_tmp2_v.16b,l0_\cd\()_v.16b,l0_\ef\()_v.16b,#8
	add		l0_\gh\()_v.2d,l0_\gh\()_v.2d,l0_tmp0_v.2d
	sha512h		l0_\gh\()_q,l0_tmp1_q,l0_tmp2_v.2d
	add		l0_\tmp\()_v.2d,l0_\cd\()_v.2d,l0_\gh\()_v.2d
	sha512h2	l0_\gh\()_q,l0_\cd\()_q,l0_\ab\()_v.2d
.endm


/*
Variable list
*/

	declare_var_vector_reg	key,31


/*
digest variables
*/
	declare_var_vector_reg	l0_ab,0
	declare_var_vector_reg	l0_cd,1
	declare_var_vector_reg	l0_ef,2
	declare_var_vector_reg	l0_gh,3

	declare_var_vector_reg	l0_tmp,4
	declare_var_vector_reg	l0_ab_saved,24
	declare_var_vector_reg	l0_cd_saved,25
	declare_var_vector_reg	l0_ef_saved,26
	declare_var_vector_reg	l0_gh_saved,27
/*
Temporay variables
*/
	declare_var_vector_reg	l0_tmp0,5
	declare_var_vector_reg	l0_tmp1,6
	declare_var_vector_reg	l0_tmp2,7

/*
Message variables
*/
	declare_var_vector_reg	l0_msg0,16
	declare_var_vector_reg	l0_msg1,17
	declare_var_vector_reg	l0_msg2,18
	declare_var_vector_reg	l0_msg3,19
	declare_var_vector_reg	l0_msg4,20
	declare_var_vector_reg	l0_msg5,21
	declare_var_vector_reg	l0_msg6,22
	declare_var_vector_reg	l0_msg7,23



/*
	void sha512_mb_ce_x1(SHA1_JOB * l0_job, int len);
*/
/*
Arguements list
*/
	l0_job 	.req	x0
	len	.req	w1
	l0_data	.req	x2
	key_adr	.req	x3
	.global	sha512_mb_ce_x1
	.type	sha512_mb_ce_x1, %function
sha512_mb_ce_x1:
	ldr	l0_data, [l0_job]
	// load initial digest
	add	x4,l0_job,64
	ld1	{l0_ab_v.4s-l0_gh_v.4s},[x4]



start_loop:
	adr	key_adr, KEY
	//load msgs
	ld1	{l0_msg0_v.4s-l0_msg3_v.4s},[l0_data]
	add	l0_data,l0_data,64
	ld1	{l0_msg4_v.4s-l0_msg7_v.4s},[l0_data]
	add	l0_data,l0_data,64
	//adjust loop parameter

	sub	len, len, #1
	cmp	len, 0

	//save state
	mov	l0_ab_saved_v.16b,l0_ab_v.16b
	mov	l0_cd_saved_v.16b,l0_cd_v.16b
	mov	l0_ef_saved_v.16b,l0_ef_v.16b
	mov	l0_gh_saved_v.16b,l0_gh_v.16b

	//rev endian
	rev64	l0_msg0_v.16b,l0_msg0_v.16b
	rev64	l0_msg1_v.16b,l0_msg1_v.16b
	rev64	l0_msg2_v.16b,l0_msg2_v.16b
	rev64	l0_msg3_v.16b,l0_msg3_v.16b
	rev64	l0_msg4_v.16b,l0_msg4_v.16b
	rev64	l0_msg5_v.16b,l0_msg5_v.16b
	rev64	l0_msg6_v.16b,l0_msg6_v.16b
	rev64	l0_msg7_v.16b,l0_msg7_v.16b



	sha512_rounds_low	 ab, cd, ef, gh,tmp,msg0,msg1,msg4,msg5,msg7	/* rounds  0- 1 */
	sha512_rounds_low	 gh, ab,tmp, ef, cd,msg1,msg2,msg5,msg6,msg0	/* rounds  2- 3 */
	sha512_rounds_low	 ef, gh, cd,tmp, ab,msg2,msg3,msg6,msg7,msg1	/* rounds  4- 5 */
	sha512_rounds_low	tmp, ef, ab, cd, gh,msg3,msg4,msg7,msg0,msg2	/* rounds  6- 7 */
	sha512_rounds_low	 cd,tmp, gh, ab, ef,msg4,msg5,msg0,msg1,msg3	/* rounds  8- 9 */
	sha512_rounds_low	 ab, cd, ef, gh,tmp,msg5,msg6,msg1,msg2,msg4	/* rounds 10-11 */
	sha512_rounds_low	 gh, ab,tmp, ef, cd,msg6,msg7,msg2,msg3,msg5	/* rounds 12-13 */
	sha512_rounds_low	 ef, gh, cd,tmp, ab,msg7,msg0,msg3,msg4,msg6	/* rounds 14-15 */
	sha512_rounds_low	tmp, ef, ab, cd, gh,msg0,msg1,msg4,msg5,msg7	/* rounds 16-17 */
	sha512_rounds_low	 cd,tmp, gh, ab, ef,msg1,msg2,msg5,msg6,msg0	/* rounds 18-19 */
	sha512_rounds_low	 ab, cd, ef, gh,tmp,msg2,msg3,msg6,msg7,msg1	/* rounds 20-21 */
	sha512_rounds_low	 gh, ab,tmp, ef, cd,msg3,msg4,msg7,msg0,msg2	/* rounds 22-23 */
	sha512_rounds_low	 ef, gh, cd,tmp, ab,msg4,msg5,msg0,msg1,msg3	/* rounds 24-25 */
	sha512_rounds_low	tmp, ef, ab, cd, gh,msg5,msg6,msg1,msg2,msg4	/* rounds 26-27 */
	sha512_rounds_low	 cd,tmp, gh, ab, ef,msg6,msg7,msg2,msg3,msg5	/* rounds 28-29 */
	sha512_rounds_low	 ab, cd, ef, gh,tmp,msg7,msg0,msg3,msg4,msg6	/* rounds 30-31 */
	sha512_rounds_low	 gh, ab,tmp, ef, cd,msg0,msg1,msg4,msg5,msg7	/* rounds 32-33 */
	sha512_rounds_low	 ef, gh, cd,tmp, ab,msg1,msg2,msg5,msg6,msg0	/* rounds 34-35 */
	sha512_rounds_low	tmp, ef, ab, cd, gh,msg2,msg3,msg6,msg7,msg1	/* rounds 36-37 */
	sha512_rounds_low	 cd,tmp, gh, ab, ef,msg3,msg4,msg7,msg0,msg2	/* rounds 38-39 */
	sha512_rounds_low	 ab, cd, ef, gh,tmp,msg4,msg5,msg0,msg1,msg3	/* rounds 40-41 */
	sha512_rounds_low	 gh, ab,tmp, ef, cd,msg5,msg6,msg1,msg2,msg4	/* rounds 42-43 */
	sha512_rounds_low	 ef, gh, cd,tmp, ab,msg6,msg7,msg2,msg3,msg5	/* rounds 44-45 */
	sha512_rounds_low	tmp, ef, ab, cd, gh,msg7,msg0,msg3,msg4,msg6	/* rounds 46-47 */
	sha512_rounds_low	 cd,tmp, gh, ab, ef,msg0,msg1,msg4,msg5,msg7	/* rounds 48-49 */
	sha512_rounds_low	 ab, cd, ef, gh,tmp,msg1,msg2,msg5,msg6,msg0	/* rounds 50-51 */
	sha512_rounds_low	 gh, ab,tmp, ef, cd,msg2,msg3,msg6,msg7,msg1	/* rounds 52-53 */
	sha512_rounds_low	 ef, gh, cd,tmp, ab,msg3,msg4,msg7,msg0,msg2	/* rounds 54-55 */
	sha512_rounds_low	tmp, ef, ab, cd, gh,msg4,msg5,msg0,msg1,msg3	/* rounds 56-57 */
	sha512_rounds_low	 cd,tmp, gh, ab, ef,msg5,msg6,msg1,msg2,msg4	/* rounds 58-59 */
	sha512_rounds_low	 ab, cd, ef, gh,tmp,msg6,msg7,msg2,msg3,msg5	/* rounds 60-61 */
	sha512_rounds_low	 gh, ab,tmp, ef, cd,msg7,msg0,msg3,msg4,msg6	/* rounds 62-63 */
	sha512_rounds_high	 ef, gh, cd,tmp, ab,msg0			/* rounds 64-65 */
	sha512_rounds_high	tmp, ef, ab, cd, gh,msg1			/* rounds 66-67 */
	sha512_rounds_high	 cd,tmp, gh, ab, ef,msg2			/* rounds 68-69 */
	sha512_rounds_high	 ab, cd, ef, gh,tmp,msg3			/* rounds 70-71 */
	sha512_rounds_high	 gh, ab,tmp, ef, cd,msg4			/* rounds 72-73 */
	sha512_rounds_high	 ef, gh, cd,tmp, ab,msg5			/* rounds 74-75 */
	sha512_rounds_high	tmp, ef, ab, cd, gh,msg6			/* rounds 76-77 */
	sha512_rounds_high	 cd,tmp, gh, ab, ef,msg7			/* rounds 78-79 */



	add	l0_ab_v.2d,l0_ab_v.2d,l0_ab_saved_v.2d
	add	l0_cd_v.2d,l0_cd_v.2d,l0_cd_saved_v.2d
	add	l0_ef_v.2d,l0_ef_v.2d,l0_ef_saved_v.2d
	add	l0_gh_v.2d,l0_gh_v.2d,l0_gh_saved_v.2d


	bgt	start_loop

	add	x4,l0_job,64
	st1	{l0_ab_v.4s-l0_gh_v.4s},[x4]


	ret

	.size	sha512_mb_ce_x1, .-sha512_mb_ce_x1
	.section	.rol0_data.cst16,"aM",@progbits,16
	.align	4
KEY:
	.quad		0x428a2f98d728ae22, 0x7137449123ef65cd
	.quad		0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc
	.quad		0x3956c25bf348b538, 0x59f111f1b605d019
	.quad		0x923f82a4af194f9b, 0xab1c5ed5da6d8118
	.quad		0xd807aa98a3030242, 0x12835b0145706fbe
	.quad		0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2
	.quad		0x72be5d74f27b896f, 0x80deb1fe3b1696b1
	.quad		0x9bdc06a725c71235, 0xc19bf174cf692694
	.quad		0xe49b69c19ef14ad2, 0xefbe4786384f25e3
	.quad		0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65
	.quad		0x2de92c6f592b0275, 0x4a7484aa6ea6e483
	.quad		0x5cb0a9dcbd41fbd4, 0x76f988da831153b5
	.quad		0x983e5152ee66dfab, 0xa831c66d2db43210
	.quad		0xb00327c898fb213f, 0xbf597fc7beef0ee4
	.quad		0xc6e00bf33da88fc2, 0xd5a79147930aa725
	.quad		0x06ca6351e003826f, 0x142929670a0e6e70
	.quad		0x27b70a8546d22ffc, 0x2e1b21385c26c926
	.quad		0x4d2c6dfc5ac42aed, 0x53380d139d95b3df
	.quad		0x650a73548baf63de, 0x766a0abb3c77b2a8
	.quad		0x81c2c92e47edaee6, 0x92722c851482353b
	.quad		0xa2bfe8a14cf10364, 0xa81a664bbc423001
	.quad		0xc24b8b70d0f89791, 0xc76c51a30654be30
	.quad		0xd192e819d6ef5218, 0xd69906245565a910
	.quad		0xf40e35855771202a, 0x106aa07032bbd1b8
	.quad		0x19a4c116b8d2d0c8, 0x1e376c085141ab53
	.quad		0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8
	.quad		0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb
	.quad		0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3
	.quad		0x748f82ee5defb2fc, 0x78a5636f43172f60
	.quad		0x84c87814a1f0ab72, 0x8cc702081a6439ec
	.quad		0x90befffa23631e28, 0xa4506cebde82bde9
	.quad		0xbef9a3f7b2c67915, 0xc67178f2e372532b
	.quad		0xca273eceea26619c, 0xd186b8c721c0c207
	.quad		0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178
	.quad		0x06f067aa72176fba, 0x0a637dc5a2c898a6
	.quad		0x113f9804bef90dae, 0x1b710b35131c471b
	.quad		0x28db77f523047d84, 0x32caab7b40c72493
	.quad		0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c
	.quad		0x4cc5d4becb3e42b6, 0x597f299cfc657e2a
	.quad		0x5fcb6fab3ad6faec, 0x6c44198c4a475817

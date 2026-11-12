/**
 * (C) Copyright 2025 <COPYRIGHT HOLDER - to be set by the author>
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Instruction helpers for the aarch64 flavour of the function hooking framework in hook.c.
 *
 * To hook a function we overwrite ONE instruction at its entry with a direct "b" to a bounce
 * stub, so only the first instruction (or the second one when the first is a BTI landing pad) has
 * to be displaced into a trampoline. That instruction is relocated if it is PC-relative. This
 * avoids a general purpose disassembler: only the few PC-relative instruction classes of the A64
 * ISA need decoding.
 *
 * Everything here is pure computation (no libc/system calls) so it can be unit tested anywhere.
 */

#ifndef __HOOK_AARCH64_H
#define __HOOK_AARCH64_H

#include <stdint.h>
#include <stdbool.h>

/* The longest sequence a64_relocate() emits (LDR literal: 4 mov + ldr + b). */
#define A64_MAX_RELOC_WORDS (8)

/* "ldr x16, #8" followed by "br x16": jump through the 64-bit literal that follows. */
#define A64_LDR_X16_LIT8    (0x58000050u)
#define A64_BR_X16          (0xd61f0200u)
#define A64_NOP             (0xd503201fu)

static inline int64_t
a64_sext(uint64_t val, unsigned int bits)
{
	uint64_t sign = 1ULL << (bits - 1);

	return (int64_t)((val ^ sign) - sign);
}

/* BTI, BTI c, BTI j and BTI jc are HINT #32, #34, #36 and #38. */
static inline bool
a64_is_bti(uint32_t insn)
{
	return (insn & 0xffffff1fu) == 0xd503241fu && (insn & 0x20u) == 0;
}

/* Can a direct B/BL placed at "from" reach "to"? (+-128 MiB, 4 byte aligned) */
static inline bool
a64_branch_in_range(uint64_t from, uint64_t to)
{
	int64_t off = (int64_t)(to - from);

	return (off & 3) == 0 && off >= -(1LL << 27) && off < (1LL << 27);
}

static inline uint32_t
a64_b(uint64_t from, uint64_t to)
{
	return 0x14000000u | (((uint32_t)((int64_t)(to - from) >> 2)) & 0x03ffffffu);
}

static inline uint32_t
a64_bl(uint64_t from, uint64_t to)
{
	return 0x94000000u | (((uint32_t)((int64_t)(to - from) >> 2)) & 0x03ffffffu);
}

/* movz/movk x<reg>, <val>: always four instructions so the length does not depend on val. */
static inline void
a64_mov_imm64(uint32_t *out, unsigned int reg, uint64_t val)
{
	out[0] = 0xd2800000u | ((uint32_t)(val & 0xffff) << 5) | reg;
	out[1] = 0xf2a00000u | ((uint32_t)((val >> 16) & 0xffff) << 5) | reg;
	out[2] = 0xf2c00000u | ((uint32_t)((val >> 32) & 0xffff) << 5) | reg;
	out[3] = 0xf2e00000u | ((uint32_t)((val >> 48) & 0xffff) << 5) | reg;
}

/**
 * Relocate the instruction "insn", located at address "pc", so that it can be executed from a
 * trampoline placed at "out_pc" and then continue at "resume" (normally pc + 4).
 *
 * Emits the complete trampoline: the relocated instruction plus the branch back to "resume".
 * Branches are direct, since they must not be subject to BTI checks when entering the middle of a
 * function.
 *
 * Returns the number of 32-bit words written to out[] (at most max_words), or -1 when the
 * instruction cannot be relocated (target out of branch range, unsupported encoding).
 */
static inline int
a64_relocate(uint32_t insn, uint64_t pc, uint32_t *out, int max_words, uint64_t out_pc,
	     uint64_t resume)
{
	int n = 0;

#define A64_EMIT(w)                                                                                \
	do {                                                                                       \
		if (n >= max_words)                                                                \
			return -1;                                                                 \
		out[n++] = (w);                                                                    \
	} while (0)
#define A64_EMIT_B_TO(dest)                                                                        \
	do {                                                                                       \
		uint64_t _here = out_pc + 4 * (uint64_t)n;                                         \
		if (!a64_branch_in_range(_here, (dest)))                                           \
			return -1;                                                                 \
		A64_EMIT(a64_b(_here, (dest)));                                                    \
	} while (0)

	if ((insn & 0x7c000000u) == 0x14000000u) {
		/* B / BL imm26 */
		uint64_t target = pc + (uint64_t)(a64_sext(insn & 0x03ffffffu, 26) * 4);
		uint64_t here   = out_pc;

		if (!a64_branch_in_range(here, target))
			return -1;
		if (insn & 0x80000000u) {
			A64_EMIT(a64_bl(here, target));
			/* the callee returns here, then continues in the original function */
			A64_EMIT_B_TO(resume);
		} else {
			A64_EMIT(a64_b(here, target));
		}
		return n;
	}

	if ((insn & 0xff000000u) == 0x54000000u || (insn & 0x7e000000u) == 0x34000000u ||
	    (insn & 0x7e000000u) == 0x36000000u) {
		/* B.cond / BC.cond, CBZ / CBNZ (imm19), TBZ / TBNZ (imm14).
		 * These reach only +-1 MiB / +-32 KiB, so keep the conditional branch local:
		 *   0: <insn> with offset +8  (taken: goes to 2)
		 *   1: b resume               (not taken: next instruction of the original function)
		 *   2: b target               (taken)
		 */
		uint32_t newinsn;
		uint64_t target;

		if ((insn & 0x7e000000u) == 0x36000000u) {
			target  = pc + (uint64_t)(a64_sext((insn >> 5) & 0x3fffu, 14) * 4);
			newinsn = (insn & ~0x0007ffe0u) | (2u << 5);
		} else {
			target  = pc + (uint64_t)(a64_sext((insn >> 5) & 0x7ffffu, 19) * 4);
			newinsn = (insn & ~0x00ffffe0u) | (2u << 5);
		}
		A64_EMIT(newinsn);
		A64_EMIT_B_TO(resume);
		A64_EMIT_B_TO(target);
		return n;
	}

	if ((insn & 0x1f000000u) == 0x10000000u) {
		/* ADR / ADRP: the result is a constant, materialize it. */
		uint64_t imm = (((insn >> 5) & 0x7ffffu) << 2) | ((insn >> 29) & 3u);
		uint64_t val;

		imm = (uint64_t)a64_sext(imm, 21);

		if (insn & 0x80000000u)
			val = (pc & ~0xfffULL) + (imm << 12);
		else
			val = pc + imm;
		if (n + 4 > max_words)
			return -1;
		a64_mov_imm64(&out[n], insn & 0x1fu, val);
		n += 4;
		A64_EMIT_B_TO(resume);
		return n;
	}

	if ((insn & 0x3b000000u) == 0x18000000u) {
		/* LDR (literal), LDRSW (literal), PRFM (literal), SIMD&FP LDR (literal) */
		unsigned int opc = (insn >> 30) & 3u;
		unsigned int v   = (insn >> 26) & 1u;
		unsigned int rt  = insn & 0x1fu;
		uint64_t     val = pc + (uint64_t)(a64_sext((insn >> 5) & 0x7ffffu, 19) * 4);
		uint32_t     base;

		if (v == 0 && opc == 3) {
			/* PRFM: a prefetch hint, dropping it is harmless */
			A64_EMIT(A64_NOP);
			A64_EMIT_B_TO(resume);
			return n;
		}
		if (v == 0)
			base = opc == 0 ? 0xb9400000u : (opc == 1 ? 0xf9400000u : 0xb9800000u);
		else if (opc == 3)
			return -1; /* reserved */
		else
			base = opc == 0 ? 0xbd400000u : (opc == 1 ? 0xfd400000u : 0x3dc00000u);

		/* x16 (IP0) is a scratch register at function entry */
		if (n + 4 > max_words)
			return -1;
		a64_mov_imm64(&out[n], 16, val);
		n += 4;
		A64_EMIT(base | (16u << 5) | rt);
		A64_EMIT_B_TO(resume);
		return n;
	}

	/* Position independent: copy as is. For ret/br the branch back is simply never reached. */
	A64_EMIT(insn);
	A64_EMIT_B_TO(resume);
	return n;

#undef A64_EMIT_B_TO
#undef A64_EMIT
}

#endif /* __HOOK_AARCH64_H */

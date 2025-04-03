/*******************************************************
 *                                                     *
 *  -------------------------------------------------  *
 *  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  *
 *  -------------------------------------------------  *
 *  | 0x0 | 0x4 | 0x8 | 0xc | 0x10| 0x14| 0x18| 0x1c|  *
 *  -------------------------------------------------  *
 *  |    d8     |    d9     |    d10    |    d11    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  8  |  9  |  10 |  11 |  12 |  13 |  14 |  15 |  *
 *  -------------------------------------------------  *
 *  | 0x20| 0x24| 0x28| 0x2c| 0x30| 0x34| 0x38| 0x3c|  *
 *  -------------------------------------------------  *
 *  |    d12    |    d13    |    d14    |    d15    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  16 |  17 |  18 |  19 |  20 |  21 |  22 |  23 |  *
 *  -------------------------------------------------  *
 *  | 0x40| 0x44| 0x48| 0x4c| 0x50| 0x54| 0x58| 0x5c|  *
 *  -------------------------------------------------  *
 *  |    x19    |    x20    |    x21    |    x22    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  24 |  25 |  26 |  27 |  28 |  29 |  30 |  31 |  *
 *  -------------------------------------------------  *
 *  | 0x60| 0x64| 0x68| 0x6c| 0x70| 0x74| 0x78| 0x7c|  *
 *  -------------------------------------------------  *
 *  |    x23    |    x24    |    x25    |    x26    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  32 |  33 |  34 |  35 |  36 |  37 |  38 |  39 |  *
 *  -------------------------------------------------  *
 *  | 0x80| 0x84| 0x88| 0x8c| 0x90| 0x94| 0x98| 0x9c|  *
 *  -------------------------------------------------  *
 *  |    x27    |    x28    |    FP     |     LR    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  40 |  41 |  42 | 43  |           |           |  *
 *  -------------------------------------------------  *
 *  | 0xa0| 0xa4| 0xa8| 0xac|           |           |  *
 *  -------------------------------------------------  *
 *  |     PC    |   align   |           |           |  *
 *  -------------------------------------------------  *
 *                                                     *
 *******************************************************/

#include "abt_config.h"

/*
void switch_fcontext(fcontext_t *p_new_ctx, fcontext_t *p_old_ctx);
*/
.text
.globl  _switch_fcontext
.balign 16
_switch_fcontext:
    /* prepare stack for GP + FPU */
    sub  sp, sp, #0xb0

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save d8 - d15 */
    stp  d8,  d9,  [sp, #0x00]
    stp  d10, d11, [sp, #0x10]
    stp  d12, d13, [sp, #0x20]
    stp  d14, d15, [sp, #0x30]
#endif
    /* save x19 - x30 */
    stp  x19, x20, [sp, #0x40]
    stp  x21, x22, [sp, #0x50]
    stp  x23, x24, [sp, #0x60]
    stp  x25, x26, [sp, #0x70]
    stp  x27, x28, [sp, #0x80]
    stp  x29, x30, [sp, #0x90]

    /* save LR as PC */
    str  x30, [sp, #0xa0]

    /* store SP (pointing to context-data) in p_old_ctx (x1). */
    /* STR cannot have sp as a target register */
    mov  x4, sp
    str  x4, [x1]

    /* restore SP (pointing to context-data) from p_new_ctx (x0) */
    /* LDR cannot have sp as a target register */
    ldr  x3, [x0]
    mov  sp, x3

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* load d8 - d15 */
    ldp  d8,  d9,  [sp, #0x00]
    ldp  d10, d11, [sp, #0x10]
    ldp  d12, d13, [sp, #0x20]
    ldp  d14, d15, [sp, #0x30]
#endif
    /* load x19 - x30 */
    ldp  x19, x20, [sp, #0x40]
    ldp  x21, x22, [sp, #0x50]
    ldp  x23, x24, [sp, #0x60]
    ldp  x25, x26, [sp, #0x70]
    ldp  x27, x28, [sp, #0x80]
    ldp  x29, x30, [sp, #0x90]

    /* load pc */
    ldr  x4, [sp, #0xa0]

    /* restore stack from GP + FPU */
    add  sp, sp, #0xb0

    ret x4

/*
void jump_fcontext(fcontext_t *p_new_ctx);
*/
.text
.globl  _jump_fcontext
.balign 16
_jump_fcontext:
    /* restore SP (pointing to context-data) from p_new_ctx (x0) */
    /* LDR cannot have sp as a target register */
    ldr  x3, [x0]
    mov  sp, x3

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* load d8 - d15 */
    ldp  d8,  d9,  [sp, #0x00]
    ldp  d10, d11, [sp, #0x10]
    ldp  d12, d13, [sp, #0x20]
    ldp  d14, d15, [sp, #0x30]
#endif
    /* load x19 - x30 */
    ldp  x19, x20, [sp, #0x40]
    ldp  x21, x22, [sp, #0x50]
    ldp  x23, x24, [sp, #0x60]
    ldp  x25, x26, [sp, #0x70]
    ldp  x27, x28, [sp, #0x80]
    ldp  x29, x30, [sp, #0x90]

    /* load pc */
    ldr  x4, [sp, #0xa0]

    /* restore stack from GP + FPU */
    add  sp, sp, #0xb0

    ret  x4

/*
void init_and_switch_fcontext(fcontext_t *p_new_ctx,
                              void (*f_thread)(fcontext_t *),
                              void *p_stacktop, fcontext_t *p_old_ctx);
*/
.text
.globl  _init_and_switch_fcontext
.balign 16
_init_and_switch_fcontext:
    /* prepare stack for GP + FPU */
    sub  sp, sp, #0xb0

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save d8 - d15 */
    stp  d8,  d9,  [sp, #0x00]
    stp  d10, d11, [sp, #0x10]
    stp  d12, d13, [sp, #0x20]
    stp  d14, d15, [sp, #0x30]
#endif
    /* save x19 - x30 */
    stp  x19, x20, [sp, #0x40]
    stp  x21, x22, [sp, #0x50]
    stp  x23, x24, [sp, #0x60]
    stp  x25, x26, [sp, #0x70]
    stp  x27, x28, [sp, #0x80]
    stp  x29, x30, [sp, #0x90]

    /* save LR as PC */
    str  x30, [sp, #0xa0]

    /* store SP (pointing to context-data) in p_old_ctx (x3). */
    /* STR cannot have sp as a target register */
    mov  x4, sp
    str  x4, [x3]

    /* shift address in p_stacktop (x2) to lower 16 byte boundary */
    and  x2, x2, ~0xF
    /* restore SP (pointing to context-data) from p_stacktop (x2) */
    sub  sp, x2, #0x10

    /* call f_thread (x1). p_new_ctx (x0) has been already set */
    /* sp is 16-byte aligned (the ABI spec requires it) */
    blr  x1

/*
void init_and_jump_fcontext(fcontext_t *p_new_ctx,
                            void (*f_thread)(fcontext_t *), void *p_stacktop);
*/
.text
.globl  _init_and_jump_fcontext
.balign 16
_init_and_jump_fcontext:
    /* shift address in p_stacktop (x2) to lower 16 byte boundary */
    and  x2, x2, ~0xF
    /* restore SP (pointing to context-data) from p_stacktop (x2) */
    sub  sp, x2, #0x10

    /* call f_thread (x1). p_new_ctx (x0) has been already set */
    /* sp is 16-byte aligned (the ABI spec requires it) */
    blr  x1

/*
void switch_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                               fcontext_t *p_new_ctx, fcontext_t *p_old_ctx);
*/
.text
.globl  _switch_with_call_fcontext
.balign 16
_switch_with_call_fcontext:
    /* prepare stack for GP + FPU */
    sub  sp, sp, #0xb0

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save d8 - d15 */
    stp  d8,  d9,  [sp, #0x00]
    stp  d10, d11, [sp, #0x10]
    stp  d12, d13, [sp, #0x20]
    stp  d14, d15, [sp, #0x30]
#endif
    /* save x19 - x30 */
    stp  x19, x20, [sp, #0x40]
    stp  x21, x22, [sp, #0x50]
    stp  x23, x24, [sp, #0x60]
    stp  x25, x26, [sp, #0x70]
    stp  x27, x28, [sp, #0x80]
    stp  x29, x30, [sp, #0x90]

    /* save LR as PC */
    str  x30, [sp, #0xa0]

    /* store SP (pointing to context-data) in p_old_ctx (x3). */
    /* STR cannot have sp as a target register */
    mov  x4, sp
    str  x4, [x3]

    /* restore SP (pointing to context-data) from p_new_ctx (x2) */
    /* LDR cannot have sp as a target register */
    ldr  x5, [x2]
    mov  sp, x5

    /* call f_cb (x1).  cb_arg (x0) has already been set.
     * all the caller-saved registers will be discarded. */
    blr  x1

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* load d8 - d15 */
    ldp  d8,  d9,  [sp, #0x00]
    ldp  d10, d11, [sp, #0x10]
    ldp  d12, d13, [sp, #0x20]
    ldp  d14, d15, [sp, #0x30]
#endif
    /* load x19 - x30 */
    ldp  x19, x20, [sp, #0x40]
    ldp  x21, x22, [sp, #0x50]
    ldp  x23, x24, [sp, #0x60]
    ldp  x25, x26, [sp, #0x70]
    ldp  x27, x28, [sp, #0x80]
    ldp  x29, x30, [sp, #0x90]

    /* load pc */
    ldr  x4, [sp, #0xa0]

    /* restore stack from GP + FPU */
    add  sp, sp, #0xb0

    ret  x4

/*
void jump_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                             fcontext_t *p_new_ctx);
*/
.text
.globl  _jump_with_call_fcontext
.balign 16
_jump_with_call_fcontext:
    /* restore SP (pointing to context-data) from p_new_ctx (x2) */
    /* LDR cannot have sp as a target register */
    ldr  x3, [x2]
    mov  sp, x3

    /* call f_cb (x1).  cb_arg (x0) has already been set.
     * all the caller-saved registers will be discarded. */
    blr  x1

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* load d8 - d15 */
    ldp  d8,  d9,  [sp, #0x00]
    ldp  d10, d11, [sp, #0x10]
    ldp  d12, d13, [sp, #0x20]
    ldp  d14, d15, [sp, #0x30]
#endif
    /* load x19 - x30 */
    ldp  x19, x20, [sp, #0x40]
    ldp  x21, x22, [sp, #0x50]
    ldp  x23, x24, [sp, #0x60]
    ldp  x25, x26, [sp, #0x70]
    ldp  x27, x28, [sp, #0x80]
    ldp  x29, x30, [sp, #0x90]

    /* load pc */
    ldr  x4, [sp, #0xa0]

    /* restore stack from GP + FPU */
    add  sp, sp, #0xb0

    ret  x4

/*
void init_and_switch_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                                        fcontext_t *p_new_ctx,
                                        void (*f_thread)(fcontext_t *),
                                        void *p_stacktop,
                                        fcontext_t *p_old_ctx);
*/
.text
.globl  _init_and_switch_with_call_fcontext
.balign 16
_init_and_switch_with_call_fcontext:
    /* prepare stack for GP + FPU */
    sub  sp, sp, #0xb0

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save d8 - d15 */
    stp  d8,  d9,  [sp, #0x00]
    stp  d10, d11, [sp, #0x10]
    stp  d12, d13, [sp, #0x20]
    stp  d14, d15, [sp, #0x30]
#endif
    /* save x19 - x30 */
    stp  x19, x20, [sp, #0x40]
    stp  x21, x22, [sp, #0x50]
    stp  x23, x24, [sp, #0x60]
    stp  x25, x26, [sp, #0x70]
    stp  x27, x28, [sp, #0x80]
    stp  x29, x30, [sp, #0x90]

    /* save LR as PC */
    str  x30, [sp, #0xa0]

    /* store SP (pointing to context-data) in p_old_ctx (x5). */
    /* STR cannot have sp as a target register */
    mov  x6, sp
    str  x6, [x5]

    /* shift address in p_stacktop (x4) to lower 16 byte boundary */
    and  x4, x4, ~0xF
    /* restore SP (pointing to context-data) from p_stacktop (x4) */
    sub  sp, x4, #0x10

    /* save p_new_ctx (x2) in x19 (callee-saved) */
    mov  x19, x2
    /* save f_thread (x3) in x20 (callee-saved) */
    mov  x20, x3

    /* call f_cb (x1).  cb_arg (x0) has already been set.
     * all the caller-saved registers will be discarded. */
    blr  x1

    /* set the first argument (x0) to p_new_ctx (x19) */
    mov  x0, x19

    /* call f_thread (x20). p_new_ctx (x0) has been already set */
    /* sp is 16-byte aligned (the ABI spec requires it) */
    blr  x20

/*
void init_and_jump_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                                      fcontext_t *p_new_ctx,
                                      void (*f_thread)(fcontext_t *),
                                      void *p_stacktop);
*/
.text
.globl  _init_and_jump_with_call_fcontext
.balign 16
_init_and_jump_with_call_fcontext:
    /* shift address in p_stacktop (x4) to lower 16 byte boundary */
    and  x4, x4, ~0xF
    /* restore SP (pointing to context-data) from p_stacktop (x4) */
    sub  sp, x4, #0x10

    /* save p_new_ctx (x2) in x19 (callee-saved) */
    mov  x19, x2
    /* save f_thread (x3) in x20 (callee-saved) */
    mov  x20, x3

    /* call f_cb (x1).  cb_arg (x0) has already been set.
     * all the caller-saved registers will be discarded. */
    blr  x1

    /* set the first argument (x0) to p_new_ctx (x19) */
    mov  x0, x19

    /* call f_thread (x20). p_new_ctx (x0) has been already set */
    /* sp is 16-byte aligned (the ABI spec requires it) */
    blr  x20

/*
void peek_fcontext(void *arg, void (*f_peek)(void *), fcontext_t *p_target_ctx);
*/
.text
.globl  _peek_fcontext
.balign 16
_peek_fcontext:
    sub  sp, sp, #0x20
    /* save a frame pointer and LR */
    stp  x29, x30, [sp, #0x10]

    /* save SP to x20 (callee-saved register) */
    str  x20, [sp]
    mov  x20, sp

    /* restore SP (pointing to context-data) from p_target_ctx (x2) */
    /* LDR cannot have sp as a target register */
    ldr  x3, [x2]
    mov  sp, x3

    /* call f_peek (x1). arg (x0) has been already set */
    /* sp is 16-byte aligned (the ABI spec requires it) */
    blr  x1

    /* restore original x20 and sp */
    mov  sp, x20
    ldr  x20, [sp]

    /* restore a frame pointer and LR */
    ldp  x29, x30, [sp, #0x10]

    /* return */
    add  sp, sp, #0x20
    ret

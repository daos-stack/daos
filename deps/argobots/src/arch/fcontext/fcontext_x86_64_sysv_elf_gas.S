/*
            Copyright Oliver Kowalke 2009.
   Distributed under the Boost Software License, Version 1.0.
      (See accompanying file LICENSE_1_0.txt or copy at
            http://www.boost.org/LICENSE_1_0.txt)
*/

/****************************************************************************************
 *                                                                                      *
 *  ----------------------------------------------------------------------------------  *
 *  |    0    |    1    |    2    |    3    |    4     |    5    |    6    |    7    |  *
 *  ----------------------------------------------------------------------------------  *
 *  |   0x0   |   0x4   |   0x8   |   0xc   |   0x10   |   0x14  |   0x18  |   0x1c  |  *
 *  ----------------------------------------------------------------------------------  *
 *  | fc_mxcsr|fc_x87_cw|        R12        |         R13        |        R14        |  *
 *  ----------------------------------------------------------------------------------  *
 *  ----------------------------------------------------------------------------------  *
 *  |    8    |    9    |   10    |   11    |    12    |    13   |    14   |    15   |  *
 *  ----------------------------------------------------------------------------------  *
 *  |   0x20  |   0x24  |   0x28  |  0x2c   |   0x30   |   0x34  |   0x38  |   0x3c  |  *
 *  ----------------------------------------------------------------------------------  *
 *  |        R15        |        RBX        |         RBP        |        RIP        |  *
 *  ----------------------------------------------------------------------------------  *
 *  ----------------------------------------------------------------------------------  *
 *  |    16   |   17    |                                                            |  *
 *  ----------------------------------------------------------------------------------  *
 *  |   0x40  |   0x44  |                                                            |  *
 *  ----------------------------------------------------------------------------------  *
 *  |        EXIT       |                                                            |  *
 *  ----------------------------------------------------------------------------------  *
 *                                                                                      *
 ****************************************************************************************/

#include "abt_config.h"

/*
void switch_fcontext(fcontext_t *p_new_ctx, fcontext_t *p_old_ctx);
*/
.text
.globl switch_fcontext
.type switch_fcontext,@function
.align 16
switch_fcontext:
    pushq  %rbp  /* save RBP */
    pushq  %rbx  /* save RBX */
    pushq  %r15  /* save R15 */
    pushq  %r14  /* save R14 */
    pushq  %r13  /* save R13 */
    pushq  %r12  /* save R12 */

    /* prepare stack for FPU */
    leaq  -0x8(%rsp), %rsp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save MMX control- and status-word */
    stmxcsr  (%rsp)
    /* save x87 control-word */
    fnstcw   0x4(%rsp)
#endif

    /* store RSP (pointing to context-data) in p_old_ctx (RSI) */
    movq  %rsp, (%rsi)

    /* restore RSP which is stored in p_new_ctx (RDI) */
    movq  (%rdi), %rsp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore MMX control- and status-word */
    ldmxcsr  (%rsp)
    /* restore x87 control-word */
    fldcw  0x4(%rsp)
#endif

    /* prepare stack for FPU */
    leaq  0x8(%rsp), %rsp

    popq  %r12  /* restrore R12 */
    popq  %r13  /* restrore R13 */
    popq  %r14  /* restrore R14 */
    popq  %r15  /* restrore R15 */
    popq  %rbx  /* restrore RBX */
    popq  %rbp  /* restrore RBP */

    /* restore return-address */
    popq  %r8

    /* indirect jump to context */
    jmpq  *%r8
.size switch_fcontext,.-switch_fcontext

/*
void jump_fcontext(fcontext_t *p_new_ctx);
*/
.text
.globl jump_fcontext
.type jump_fcontext,@function
.align 16
jump_fcontext:
    /* restore RSP which is stored in p_new_ctx (RDI) */
    movq  (%rdi), %rsp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore MMX control- and status-word */
    ldmxcsr  (%rsp)
    /* restore x87 control-word */
    fldcw  0x4(%rsp)
#endif

    /* prepare stack for FPU */
    leaq  0x8(%rsp), %rsp

    popq  %r12  /* restrore R12 */
    popq  %r13  /* restrore R13 */
    popq  %r14  /* restrore R14 */
    popq  %r15  /* restrore R15 */
    popq  %rbx  /* restrore RBX */
    popq  %rbp  /* restrore RBP */

    /* restore return-address */
    popq  %r8

    /* indirect jump to context */
    jmpq  *%r8
.size jump_fcontext,.-jump_fcontext

/*
void init_and_switch_fcontext(fcontext_t *p_new_ctx,
                              void (*f_thread)(fcontext_t *),
                              void *p_stacktop, fcontext_t *p_old_ctx);
*/
.text
.globl init_and_switch_fcontext
.type init_and_switch_fcontext,@function
.align 16
init_and_switch_fcontext:
    pushq  %rbp  /* save RBP */
    pushq  %rbx  /* save RBX */
    pushq  %r15  /* save R15 */
    pushq  %r14  /* save R14 */
    pushq  %r13  /* save R13 */
    pushq  %r12  /* save R12 */

    /* prepare stack for FPU */
    leaq  -0x8(%rsp), %rsp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save MMX control- and status-word */
    stmxcsr  (%rsp)
    /* save x87 control-word */
    fnstcw   0x4(%rsp)
#endif

    /* store RSP (pointing to context-data) in p_old_ctx (RCX) */
    movq  %rsp, (%rcx)

    /* shift address in p_stacktop (RDX) to lower 16 byte boundary */
    andq  $-16, %rdx

    /* set p_stacktop (RDX) to RSP.  For 16-byte function stack alignment,
     * subtract 8 bytes */
    leaq -0x8(%rdx), %rsp

    /* p_new_ctx (RDI) is passed as the first argument of f_thread. */
    /* jump to a f_thread (RSI) */
    jmpq *%rsi
.size init_and_switch_fcontext,.-init_and_switch_fcontext

/*
void init_and_jump_fcontext(fcontext_t *p_new_ctx,
                            void (*f_thread)(fcontext_t *), void *p_stacktop);
*/
.text
.globl init_and_jump_fcontext
.type init_and_jump_fcontext,@function
.align 16
init_and_jump_fcontext:
    /* shift address in p_stacktop (RDX) to lower 16 byte boundary */
    andq  $-16, %rdx

    /* set p_stacktop(RDX) to RSP.  For 16-byte function stack alignment,
     * subtract 8 bytes */
    leaq -0x8(%rdx), %rsp

    /* p_new_ctx (RDI) is passed as the first argument (RDI) of f_thread. */
    /* jump to a f_thread (RSI) */
    jmpq *%rsi
.size init_and_jump_fcontext,.-init_and_jump_fcontext


/*
void switch_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                               fcontext_t *p_new_ctx, fcontext_t *p_old_ctx);
*/
.text
.globl switch_with_call_fcontext
.type switch_with_call_fcontext,@function
.align 16
switch_with_call_fcontext:
    pushq  %rbp  /* save RBP */
    pushq  %rbx  /* save RBX */
    pushq  %r15  /* save R15 */
    pushq  %r14  /* save R14 */
    pushq  %r13  /* save R13 */
    pushq  %r12  /* save R12 */

    /* prepare stack for FPU */
    leaq  -0x8(%rsp), %rsp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save MMX control- and status-word */
    stmxcsr  (%rsp)
    /* save x87 control-word */
    fnstcw   0x4(%rsp)
#endif

    /* store RSP (pointing to context-data) in p_old_ctx (RCX) */
    movq  %rsp, (%rcx)

    /* restore RSP which is stored in p_new_ctx (RDX) */
    movq  (%rdx), %rsp

    /* call f_cb (RSI).  cb_arg (RDI) has already been set.
     * all the caller-saved registers will be discarded */
    callq *%rsi

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore MMX control- and status-word */
    ldmxcsr  (%rsp)
    /* restore x87 control-word */
    fldcw  0x4(%rsp)
#endif

    /* prepare stack for FPU */
    leaq  0x8(%rsp), %rsp

    popq  %r12  /* restrore R12 */
    popq  %r13  /* restrore R13 */
    popq  %r14  /* restrore R14 */
    popq  %r15  /* restrore R15 */
    popq  %rbx  /* restrore RBX */
    popq  %rbp  /* restrore RBP */

    /* restore return-address */
    popq  %r8

    /* indirect jump to context */
    jmpq  *%r8
.size switch_with_call_fcontext,.-switch_with_call_fcontext

/*
void jump_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                             fcontext_t *p_new_ctx);
*/
.text
.globl jump_with_call_fcontext
.type jump_with_call_fcontext,@function
.align 16
jump_with_call_fcontext:
    /* restore RSP which is stored in p_new_ctx (RDX) */
    movq  (%rdx), %rsp

    /* call f_cb (RSI).  cb_arg (RDI) has already been set.
     * all the caller-saved registers will be discarded */
    callq *%rsi

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore MMX control- and status-word */
    ldmxcsr  (%rsp)
    /* restore x87 control-word */
    fldcw  0x4(%rsp)
#endif

    /* prepare stack for FPU */
    leaq  0x8(%rsp), %rsp

    popq  %r12  /* restrore R12 */
    popq  %r13  /* restrore R13 */
    popq  %r14  /* restrore R14 */
    popq  %r15  /* restrore R15 */
    popq  %rbx  /* restrore RBX */
    popq  %rbp  /* restrore RBP */

    /* restore return-address */
    popq  %r8

    /* indirect jump to context */
    jmpq  *%r8
.size jump_with_call_fcontext,.-jump_with_call_fcontext

/*
void init_and_switch_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                                        fcontext_t *p_new_ctx,
                                        void (*f_thread)(fcontext_t *),
                                        void *p_stacktop,
                                        fcontext_t *p_old_ctx);
*/
.text
.globl init_and_switch_with_call_fcontext
.type init_and_switch_with_call_fcontext,@function
.align 16
init_and_switch_with_call_fcontext:
    pushq  %rbp  /* save RBP */
    pushq  %rbx  /* save RBX */
    pushq  %r15  /* save R15 */
    pushq  %r14  /* save R14 */
    pushq  %r13  /* save R13 */
    pushq  %r12  /* save R12 */

    /* prepare stack for FPU */
    leaq  -0x8(%rsp), %rsp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save MMX control- and status-word */
    stmxcsr  (%rsp)
    /* save x87 control-word */
    fnstcw   0x4(%rsp)
#endif

    /* store RSP (pointing to context-data) in p_old_ctx (R9) */
    movq  %rsp, (%r9)

    /* shift address in p_stacktop (R8) to lower 16 byte boundary */
    andq  $-16, %r8

    /* save p_new_ctx (RDX) in R12 (callee-saved) */
    movq  %rdx, %r12
    /* save f_thread (RCX) in R13 (callee-saved) */
    movq  %rcx, %r13
    /* set p_stacktop (R8) */
    movq  %r8, %rsp

    /* call f_cb (RSI).  cb_arg (RDI) has already been set.
     * all the caller-saved registers will be discarded */
    callq *%rsi

    /* For 16-byte function stack alignment, subtract 8 bytes */
    leaq -0x8(%rsp), %rsp

    /* set the first argument (RDI) to p_new_ctx (R12) */
    movq  %r12, %rdi

    /* p_new_ctx (RDI) is passed as the first argument of f_thread. */
    /* jump to a f_thread (R13) */
    jmpq *%r13
.size init_and_switch_with_call_fcontext,.-init_and_switch_with_call_fcontext

/*
void init_and_jump_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                                      fcontext_t *p_new_ctx,
                                      void (*f_thread)(fcontext_t *),
                                      void *p_stacktop);
*/
.text
.globl init_and_jump_with_call_fcontext
.type init_and_jump_with_call_fcontext,@function
.align 16
init_and_jump_with_call_fcontext:
    /* shift address in p_stacktop (R8) to lower 16 byte boundary */
    andq  $-16, %r8

    /* save p_new_ctx (RDX) in R12 (callee-saved) */
    movq  %rdx, %r12
    /* save f_thread (RCX) in R13 (callee-saved) */
    movq  %rcx, %r13
    /* set p_stacktop (R8) */
    movq  %r8, %rsp

    /* call f_cb (RSI).  cb_arg (RDI) has already been set.
     * all the caller-saved registers will be discarded */
    callq *%rsi

    /* For 16-byte function stack alignment, subtract 8 bytes */
    leaq -0x8(%rsp), %rsp

    /* set the first argument (RDI) to p_new_ctx (R12) */
    movq  %r12, %rdi

    /* p_new_ctx (RDI) is passed as the first argument of f_thread. */
    /* jump to a f_thread (R13) */
    jmpq *%r13
.size init_and_jump_with_call_fcontext,.-init_and_jump_with_call_fcontext

/*
void peek_fcontext(void *arg, void (*f_peek)(void *), fcontext_t *p_target_ctx);
*/
.text
.globl peek_fcontext
.type peek_fcontext,@function
.align 16
peek_fcontext:
    /* temporarily move RSP (pointing to context-data) to R12 (callee-saved) */
    pushq  %r12
    movq   %rsp, %r12
    /* restore RSP (pointing to context-data) from p_target_ctx (RDX) */
    movq   (%rdx), %rsp
    /* RSP is already 16-byte aligned, so we can call f_peek (RSI) here.
     * arg is passed as the first argument (RDI). */
    callq *%rsi
    /* restore callee-saved registers. */
    movq   %r12, %rsp
    popq   %r12
    /* return */
    ret
.size peek_fcontext,.-peek_fcontext

/* mark that we don't need executable stack. */
#ifndef __SUNPRO_C
.section .note.GNU-stack,"",%progbits
#else
/* skip this since Solaris assembler does not recognize it */
#endif

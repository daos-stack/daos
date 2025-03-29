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
 *  | fc_mxcsr|fc_x87_cw|   EDI   |   ESI   |   EBX    |   EBP   |   EIP   |   EXIT  |  *
 *  ----------------------------------------------------------------------------------  *
 *                                                                                      *
 ****************************************************************************************/

#include "abt_config.h"

/*
void switch_fcontext(fcontext_t *p_new_ctx, fcontext_t *p_old_ctx);
*/
.text
.globl switch_fcontext
.align 2
.type switch_fcontext,@function
switch_fcontext:
    pushl  %ebp  /* save EBP */
    pushl  %ebx  /* save EBX */
    pushl  %esi  /* save ESI */
    pushl  %edi  /* save EDI */

    /* prepare stack for FPU */
    leal  -0x8(%esp), %esp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save MMX control- and status-word */
    stmxcsr  (%esp)
    /* save x87 control-word */
    fnstcw  0x4(%esp)
#endif

    /* load p_old_ctx (the second arg) to EAX */
    movl  0x20(%esp), %eax

    /* store ESP in p_old_ctx (EAX) */
    movl  %esp, (%eax)

    /* load p_new_ctx (the first arg) to EDX */
    movl  0x1c(%esp), %edx

    /* restore ESP (pointing to context-data) from p_new_ctx (EDX) */
    movl  (%edx), %esp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore MMX control- and status-word */
    ldmxcsr  (%esp)
    /* restore x87 control-word */
    fldcw  0x4(%esp)
#endif

    /* prepare stack for FPU */
    leal  0x8(%esp), %esp

    popl  %edi  /* restore EDI */
    popl  %esi  /* restore ESI */
    popl  %ebx  /* restore EBX */
    popl  %ebp  /* restore EBP */

    /* restore return-address */
    popl  %edx

    /* indirect jump to context */
    jmp  *%edx
.size switch_fcontext,.-switch_fcontext

/*
void jump_fcontext(fcontext_t *p_new_ctx);
*/
.text
.globl jump_fcontext
.align 2
.type jump_fcontext,@function
jump_fcontext:
    /* load p_new_ctx (the first arg) to EDX */
    movl  0x4(%esp), %edx

    /* restore ESP (pointing to context-data) from p_new_ctx (EDX) */
    movl  (%edx), %esp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore MMX control- and status-word */
    ldmxcsr  (%esp)
    /* restore x87 control-word */
    fldcw  0x4(%esp)
#endif

    /* prepare stack for FPU */
    leal  0x8(%esp), %esp

    popl  %edi  /* restore EDI */
    popl  %esi  /* restore ESI */
    popl  %ebx  /* restore EBX */
    popl  %ebp  /* restore EBP */

    /* restore return-address */
    popl  %edx

    /* indirect jump to context */
    jmp  *%edx
.size jump_fcontext,.-jump_fcontext

/*
void init_and_switch_fcontext(fcontext_t *p_new_ctx,
                              void (*f_thread)(fcontext_t *),
                              void *p_stacktop, fcontext_t *p_old_ctx);
*/
.text
.globl init_and_switch_fcontext
.align 2
.type init_and_switch_fcontext,@function
init_and_switch_fcontext:
    pushl  %ebp  /* save EBP */
    pushl  %ebx  /* save EBX */
    pushl  %esi  /* save ESI */
    pushl  %edi  /* save EDI */

    /* prepare stack for FPU */
    leal  -0x8(%esp), %esp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save MMX control- and status-word */
    stmxcsr  (%esp)
    /* save x87 control-word */
    fnstcw  0x4(%esp)
#endif

    /* load p_old_ctx (the fourth arg) to EDX */
    movl  0x28(%esp), %edx

    /* store ESP in p_old_ctx (EDX) */
    movl  %esp, (%edx)

    /* load p_new_ctx (the first arg) to EAX */
    movl  0x1c(%esp), %eax

    /* set f_thread (the second arg) to EDX */
    movl  0x20(%esp), %edx

    /* set p_stacktop (the third arg) to ESP */
    movl  0x24(%esp), %esp

    /* shift address in ESP to lower 16 byte boundary */
    andl  $-16, %esp

    /* for 16-byte function stack alignment, subtract 4 bytes */
    leal -0x4(%esp), %esp

    /* set p_new_ctx (EAX) as the first argument. */
    movl  %eax, 0x4(%esp)

    /* indirect jump to context */
    jmp  *%edx
.size init_and_switch_fcontext,.-init_and_switch_fcontext

/*
void init_and_jump_fcontext(fcontext_t *p_new_ctx,
                            void (*f_thread)(fcontext_t *), void *p_stacktop);
*/
.text
.globl init_and_jump_fcontext
.align 2
.type init_and_jump_fcontext,@function
init_and_jump_fcontext:
    /* load p_new_ctx (the first arg) to EAX */
    movl  0x4(%esp), %eax

    /* set f_thread (the second arg) to EDX */
    movl  0x8(%esp), %edx

    /* set p_stacktop (the third arg) to ESP */
    movl  0xc(%esp), %esp

    /* shift address in ESP to lower 16 byte boundary */
    andl  $-16, %esp

    /* for 16-byte function stack alignment, subtract 4 bytes */
    leal -0x4(%esp), %esp

    /* set p_new_ctx (EAX) as the first argument. */
    movl  %eax, 0x4(%esp)

    /* indirect jump to context */
    jmp  *%edx
.size init_and_jump_fcontext,.-init_and_jump_fcontext

/*
void switch_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                               fcontext_t *p_new_ctx, fcontext_t *p_old_ctx);
*/
.text
.globl switch_with_call_fcontext
.align 2
.type switch_with_call_fcontext,@function
switch_with_call_fcontext:
    pushl  %ebp  /* save EBP */
    pushl  %ebx  /* save EBX */
    pushl  %esi  /* save ESI */
    pushl  %edi  /* save EDI */

    /* prepare stack for FPU */
    leal  -0x8(%esp), %esp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save MMX control- and status-word */
    stmxcsr  (%esp)
    /* save x87 control-word */
    fnstcw  0x4(%esp)
#endif

    /* load p_old_ctx (the fourth arg) to EAX */
    movl  0x28(%esp), %eax

    /* store ESP in p_old_ctx (EAX) */
    movl  %esp, (%eax)

    /* load p_new_ctx (the third arg) to EDX */
    movl  0x24(%esp), %edx

    /* load f_cb (the second arg) to ESI */
    movl  0x20(%esp), %esi

    /* load cb_arg (the first arg) to EDI */
    movl  0x1c(%esp), %edi

    /* restore ESP (pointing to context-data) from p_new_ctx (EDX) */
    movl  (%edx), %esp

    /* set the first arg to cb_arg (EDI).  Be aware of the alignment. */
    leal   -0xc(%esp), %esp
    pushl  %edi
    /* call f_cb (ESI).  All the caller-saved registers will be discarded */
    call  *%esi
    /* restore ESP */
    leal   0x10(%esp), %esp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore MMX control- and status-word */
    ldmxcsr  (%esp)
    /* restore x87 control-word */
    fldcw  0x4(%esp)
#endif

    /* prepare stack for FPU */
    leal  0x8(%esp), %esp

    popl  %edi  /* restore EDI */
    popl  %esi  /* restore ESI */
    popl  %ebx  /* restore EBX */
    popl  %ebp  /* restore EBP */

    /* restore return-address */
    popl  %edx

    /* indirect jump to context */
    jmp  *%edx
.size switch_with_call_fcontext,.-switch_with_call_fcontext

/*
void jump_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                             fcontext_t *p_new_ctx);
*/
.text
.globl jump_with_call_fcontext
.align 2
.type jump_with_call_fcontext,@function
jump_with_call_fcontext:
    /* load cb_arg (the first arg) to EDI */
    movl  0x4(%esp), %edi

    /* load f_cb (the second arg) to ESI */
    movl  0x8(%esp), %esi

    /* load p_new_ctx (the third arg) to EDX */
    movl  0xc(%esp), %edx

    /* restore ESP (pointing to context-data) from p_new_ctx (EDX) */
    movl  (%edx), %esp

    /* set the first arg to cb_arg (EDI).  Be aware of the alignment. */
    leal   -0xc(%esp), %esp
    pushl  %edi
    /* call f_cb (ESI).  All the caller-saved registers will be discarded */
    call  *%esi
    /* restore ESP */
    leal   0x10(%esp), %esp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore MMX control- and status-word */
    ldmxcsr  (%esp)
    /* restore x87 control-word */
    fldcw  0x4(%esp)
#endif

    /* prepare stack for FPU */
    leal  0x8(%esp), %esp

    popl  %edi  /* restore EDI */
    popl  %esi  /* restore ESI */
    popl  %ebx  /* restore EBX */
    popl  %ebp  /* restore EBP */

    /* restore return-address */
    popl  %edx

    /* indirect jump to context */
    jmp  *%edx
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
.align 2
.type init_and_switch_with_call_fcontext,@function
init_and_switch_with_call_fcontext:
    pushl  %ebp  /* save EBP */
    pushl  %ebx  /* save EBX */
    pushl  %esi  /* save ESI */
    pushl  %edi  /* save EDI */

    /* prepare stack for FPU */
    leal  -0x8(%esp), %esp

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save MMX control- and status-word */
    stmxcsr  (%esp)
    /* save x87 control-word */
    fnstcw  0x4(%esp)
#endif

    /* load p_old_ctx (the sixth arg) to EDX */
    movl  0x30(%esp), %edx

    /* store ESP in p_old_ctx (EDX) */
    movl  %esp, (%edx)

    /* load cb_arg (the first arg) to EDX */
    movl  0x1c(%esp), %edx

    /* load f_cb (the second arg) to EAX */
    movl  0x20(%esp), %eax

    /* load p_new_ctx (the third arg) to EDI (callee-saved) */
    movl  0x24(%esp), %edi

    /* set f_thread (the fourth arg) to ESI (callee-saved) */
    movl  0x28(%esp), %esi

    /* set p_stacktop (the fifth arg) to ESP */
    movl  0x2c(%esp), %esp

    /* shift address in ESP to lower 16 byte boundary */
    andl  $-16, %esp

    /* set the first arg to cb_arg (EDX).  Be aware of the alignment. */
    leal   -0xc(%esp), %esp
    pushl  %edx
    /* call f_cb (EAX).  All the caller-saved registers will be discarded */
    call  *%eax

    /* for 16-byte function stack alignment, subtract 4 bytes */
    leal  -0x4(%esp), %esp

    /* set p_new_ctx (EDI) as the first argument. */
    movl  %edi, 0x4(%esp)

    /* indirect jump to context */
    jmp  *%esi
.size init_and_switch_with_call_fcontext,.-init_and_switch_with_call_fcontext

/*
void init_and_jump_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                                      fcontext_t *p_new_ctx,
                                      void (*f_thread)(fcontext_t *),
                                      void *p_stacktop);
*/
.text
.globl init_and_jump_with_call_fcontext
.align 2
.type init_and_jump_with_call_fcontext,@function
init_and_jump_with_call_fcontext:
    /* load cb_arg (the first arg) to EDX */
    movl  0x4(%esp), %edx

    /* load f_cb (the second arg) to EAX */
    movl  0x8(%esp), %eax

    /* load p_new_ctx (the third arg) to EDI (callee-saved) */
    movl  0xc(%esp), %edi

    /* set f_thread (the fourth arg) to ESI (callee-saved) */
    movl  0x10(%esp), %esi

    /* set p_stacktop (the fifth arg) to ESP */
    movl  0x14(%esp), %esp

    /* shift address in ESP to lower 16 byte boundary */
    andl  $-16, %esp

    /* set the first arg to cb_arg (EDX).  Be aware of the alignment. */
    leal   -0xc(%esp), %esp
    pushl  %edx
    /* call f_cb (EAX).  All the caller-saved registers will be discarded */
    call  *%eax

    /* for 16-byte function stack alignment, subtract 4 bytes */
    leal  -0x4(%esp), %esp

    /* set p_new_ctx (EDI) as the first argument. */
    movl  %edi, 0x4(%esp)

    /* indirect jump to context */
    jmp  *%esi
.size init_and_jump_with_call_fcontext,.-init_and_jump_with_call_fcontext

/*
void peek_fcontext(void *arg, void (*f_peek)(void *), fcontext_t *p_target_ctx);
*/
.text
.globl peek_fcontext
.align 2
.type peek_fcontext,@function
peek_fcontext:
    /* load arg (the first arg) to EAX */
    movl  0x4(%esp), %eax

    /* load f_peek (the second arg) to ECX */
    movl  0x8(%esp), %ecx

    /* load p_target_ctx (the third arg) to EDX */
    movl  0xc(%esp), %edx

    /* temporarily move ESP to ESI (callee-saved) */
    pushl  %esi
    movl   %esp, %esi

    /* restore ESP from p_target_ctx (EDX) */
    movl   (%edx), %esp

    /* set p_new_ctx (EAX) as the first argument */
    pushl %eax

    /* ESP is 16-byte aligned, so we can call f_peek (ECX) here. */
    call *%ecx

    /* restore callee-saved registers. */
    movl   %esi, %esp
    popl   %esi

    /* return */
    ret
.size peek_fcontext,.-peek_fcontext


/* Mark that we don't need executable stack.  */
.section .note.GNU-stack,"",%progbits

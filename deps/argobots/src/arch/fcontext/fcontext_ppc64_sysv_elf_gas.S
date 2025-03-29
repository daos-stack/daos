/*
            Copyright Oliver Kowalke 2009.
   Distributed under the Boost Software License, Version 1.0.
      (See accompanying file LICENSE_1_0.txt or copy at
          http://www.boost.org/LICENSE_1_0.txt)
*/

/*******************************************************
 *                                                     *
 *  -------------------------------------------------  *
 *  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  *
 *  -------------------------------------------------  *
 *  |  0  |  4  |  8  |  12 |  16 |  20 |  24 |  28 |  *
 *  -------------------------------------------------  *
 *  |    F14    |    F15    |    F16    |    F17    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  8  |  9  |  10 |  11 |  12 |  13 |  14 |  15 |  *
 *  -------------------------------------------------  *
 *  |  32 |  36 |  40 |  44 |  48 |  52 |  56 |  60 |  *
 *  -------------------------------------------------  *
 *  |    F18    |    F19    |    F20    |    F21    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  16 |  17 |  18 |  19 |  20 |  21 |  22 |  23 |  *
 *  -------------------------------------------------  *
 *  |  64 |  68 |  72 |  76 |  80 |  84 |  88 |  92 |  *
 *  -------------------------------------------------  *
 *  |    F22    |    F23    |    F24    |    F25    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  24 |  25 |  26 |  27 |  28 |  29 |  30 |  31 |  *
 *  -------------------------------------------------  *
 *  |  96 | 100 | 104 | 108 | 112 | 116 | 120 | 124 |  *
 *  -------------------------------------------------  *
 *  |    F26    |    F27    |    F28    |    F29    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  32 |  33 |  34 |  35 |  36 |  37 |  38 |  39 |  *
 *  -------------------------------------------------  *
 *  | 128 | 132 | 136 | 140 | 144 | 148 | 152 | 156 |  *
 *  -------------------------------------------------  *
 *  |    F30    |    F31    |   fpscr   |    vscr   |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  40 |  41 |  42 |  43 |  44 |  45 |  46 |  47 |  *
 *  -------------------------------------------------  *
 *  | 160 | 164 | 168 | 172 | 176 | 180 | 184 | 188 |  *
 *  -------------------------------------------------  *
 *  |          V20          |          V21          |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  48 |  49 |  50 |  51 |  52 |  53 |  54 |  55 |  *
 *  -------------------------------------------------  *
 *  | 192 | 196 | 200 | 204 | 208 | 212 | 216 | 220 |  *
 *  -------------------------------------------------  *
 *  |          V22          |          V23          |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  56 |  57 |  58 |  59 |  60 |  61 |  62 |  63 |  *
 *  -------------------------------------------------  *
 *  | 224 | 228 | 232 | 236 | 240 | 244 | 248 | 252 |  *
 *  -------------------------------------------------  *
 *  |          V24          |          V25          |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  64 |  65 |  66 |  67 |  68 |  69 |  70 |  71 |  *
 *  -------------------------------------------------  *
 *  | 256 | 260 | 264 | 268 | 272 | 276 | 280 | 284 |  *
 *  -------------------------------------------------  *
 *  |          V26          |          V27          |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  72 |  73 |  74 |  75 |  76 |  77 |  78 |  79 |  *
 *  -------------------------------------------------  *
 *  | 288 | 292 | 296 | 300 | 304 | 308 | 312 | 316 |  *
 *  -------------------------------------------------  *
 *  |          V28          |          V29          |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  80 |  81 |  82 |  83 |  84 |  85 |  86 |  87 |  *
 *  -------------------------------------------------  *
 *  | 320 | 324 | 328 | 332 | 336 | 340 | 344 | 348 |  *
 *  -------------------------------------------------  *
 *  |          V30          |          V31          |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  88 |  89 |  90 |  91 |  92 |  93 |  94 |  95 |  *
 *  -------------------------------------------------  *
 *  | 352 | 356 | 360 | 364 | 368 | 372 | 376 | 380 |  *
 *  -------------------------------------------------  *
 *  |    R14    |    R15    |     R16   |    R17    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |  96 |  97 |  98 |  99 | 100 | 101 | 102 | 103 |  *
 *  -------------------------------------------------  *
 *  | 384 | 388 | 392 | 396 | 400 | 404 | 408 | 412 |  *
 *  -------------------------------------------------  *
 *  |    R18    |    R19    |    R20    |    R21    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  | 104 | 105 | 106 | 107 | 108 | 109 | 110 | 111 |  *
 *  -------------------------------------------------  *
 *  | 416 | 420 | 424 | 428 | 432 | 436 | 440 | 444 |  *
 *  -------------------------------------------------  *
 *  |    R22    |    R23    |    R24    |    R25    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  | 112 | 113 | 114 | 115 | 116 | 117 | 118 | 119 |  *
 *  -------------------------------------------------  *
 *  | 448 | 452 | 456 | 460 | 464 | 468 | 472 | 476 |  *
 *  -------------------------------------------------  *
 *  |    R26    |    R27    |    R28    |    R29    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  | 120 | 121 | 122 | 123 | 124 | 125 | 126 | 127 |  *
 *  -------------------------------------------------  *
 *  | 480 | 484 | 488 | 492 | 496 | 500 | 504 | 508 |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  |    R30    |    R31    |    TOC    |     CR    |  *
 *  -------------------------------------------------  *
 *  -------------------------------------------------  *
 *  | 128 | 129 | 130 | 131 |                       |  *
 *  -------------------------------------------------  *
 *  | 512 | 516 | 520 | 524 |                       |  *
 *  -------------------------------------------------  *
 *  |    LR     |    PC     |                       |  *
 *  -------------------------------------------------  *
 *                                                     *
 *******************************************************/

#include "abt_config.h"

/*
void switch_fcontext(fcontext_t *p_new_ctx, fcontext_t *p_old_ctx);
*/
.globl switch_fcontext
#if _CALL_ELF == 2
    .text
    .align 2
switch_fcontext:
        addis   %r2, %r12, .TOC.-switch_fcontext@ha
        addi    %r2, %r2, .TOC.-switch_fcontext@l
        .localentry switch_fcontext, . - switch_fcontext
#else
    .section ".opd","aw"
    .align 3
switch_fcontext:
# ifdef _CALL_LINUX
        .quad   .L.switch_fcontext,.TOC.@tocbase,0
        .type   switch_fcontext,@function
        .text
        .align 2
.L.switch_fcontext:
# else
        .hidden .switch_fcontext
        .globl  .switch_fcontext
        .quad   .switch_fcontext,.TOC.@tocbase,0
        .size   switch_fcontext,24
        .type   .switch_fcontext,@function
        .text
        .align 2
.switch_fcontext:
# endif
#endif
    /* reserve space on stack */
    subi  %r1, %r1, 528
    /* save R14 - R31 */
    std  %r14, 352(%r1)
    std  %r15, 360(%r1)
    std  %r16, 368(%r1)
    std  %r17, 376(%r1)
    std  %r18, 384(%r1)
    std  %r19, 392(%r1)
    std  %r20, 400(%r1)
    std  %r21, 408(%r1)
    std  %r22, 416(%r1)
    std  %r23, 424(%r1)
    std  %r24, 432(%r1)
    std  %r25, 440(%r1)
    std  %r26, 448(%r1)
    std  %r27, 456(%r1)
    std  %r28, 464(%r1)
    std  %r29, 472(%r1)
    std  %r30, 480(%r1)
    std  %r31, 488(%r1)
#if _CALL_ELF != 2
    /* save TOC */
    std  %r2,  496(%r1)  # save TOC
#endif

    /* save CR */
    mfcr  %r0
    std  %r0, 504(%r1)
    /* save LR */
    mflr  %r0
    std  %r0, 512(%r1)
    /* save LR as PC */
    std  %r0, 520(%r1)

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save F14 - F32 */
    stfd  %f14, 0(%r1)
    stfd  %f15, 8(%r1)
    stfd  %f16, 16(%r1)
    stfd  %f17, 24(%r1)
    stfd  %f18, 32(%r1)
    stfd  %f19, 40(%r1)
    stfd  %f20, 48(%r1)
    stfd  %f21, 56(%r1)
    stfd  %f22, 64(%r1)
    stfd  %f23, 72(%r1)
    stfd  %f24, 80(%r1)
    stfd  %f25, 88(%r1)
    stfd  %f26, 96(%r1)
    stfd  %f27, 104(%r1)
    stfd  %f28, 112(%r1)
    stfd  %f29, 120(%r1)
    stfd  %f30, 128(%r1)
    stfd  %f31, 136(%r1)
#ifdef __VSX__
    /* VSCR can be loaded only to Vn.  To store VSCR as it is a vector, */
    /* it must be written before saving FPSCR. */
    /* load VSCR. */
    mfvscr %v19
    li    %r10, 144
    /* save VSCR.  Only the last 32 bits are used */
    stvx  %v19, %r10, %r1
#endif
    /* load FPSCR */
    mffs  %f0
    /* save FPSCR */
    stfd  %f0, 144(%r1)
#ifdef __VSX__
    /* OpenPOWER saves V20 - V31 (vector units) */
    /* Note stvx cannot take an immediate value as an offset. */
    li    %r14, 160
    stvx  %v20, %r14, %r1
    li    %r15, 176
    stvx  %v21, %r15, %r1
    li    %r16, 192
    stvx  %v22, %r16, %r1
    li    %r17, 208
    stvx  %v23, %r17, %r1
    li    %r18, 224
    stvx  %v24, %r18, %r1
    li    %r19, 240
    stvx  %v25, %r19, %r1
    li    %r20, 256
    stvx  %v26, %r20, %r1
    li    %r21, 272
    stvx  %v27, %r21, %r1
    li    %r22, 288
    stvx  %v28, %r22, %r1
    li    %r23, 304
    stvx  %v29, %r23, %r1
    li    %r24, 320
    stvx  %v30, %r24, %r1
    li    %r25, 336
    stvx  %v31, %r25, %r1
#endif
#endif

    /* store RSP (pointing to context-data) in p_old_ctx (R4) */
    std  %r1, 0(%r4)

    /* restore RSP (pointing to context-data) from p_new_ctx (R3) */
    ld   %r1, 0(%r3)

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore F14 - F31 */
    lfd  %f14, 0(%r1)
    lfd  %f15, 8(%r1)
    lfd  %f16, 16(%r1)
    lfd  %f17, 24(%r1)
    lfd  %f18, 32(%r1)
    lfd  %f19, 40(%r1)
    lfd  %f20, 48(%r1)
    lfd  %f21, 56(%r1)
    lfd  %f22, 64(%r1)
    lfd  %f23, 72(%r1)
    lfd  %f24, 80(%r1)
    lfd  %f25, 88(%r1)
    lfd  %f26, 96(%r1)
    lfd  %f27, 104(%r1)
    lfd  %f28, 112(%r1)
    lfd  %f29, 120(%r1)
    lfd  %f30, 128(%r1)
    lfd  %f31, 136(%r1)
    /* restore FPSCR */
    lfd  %f0,  144(%r1)
    mtfsf  0xff, %f0
#ifdef __VSX__
    li    %r10, 144
    /* Restore VSCR.  Only the last 32 bits are used */
    lvx   %v19, %r10, %r1
    mtvscr %v19
    /* restore V20 - V21 */
    li    %r14, 160
    lvx  %v20, %r14, %r1
    li    %r15, 176
    lvx  %v21, %r15, %r1
    li    %r16, 192
    lvx  %v22, %r16, %r1
    li    %r17, 208
    lvx  %v23, %r17, %r1
    li    %r18, 224
    lvx  %v24, %r18, %r1
    li    %r19, 240
    lvx  %v25, %r19, %r1
    li    %r20, 256
    lvx  %v26, %r20, %r1
    li    %r21, 272
    lvx  %v27, %r21, %r1
    li    %r22, 288
    lvx  %v28, %r22, %r1
    li    %r23, 304
    lvx  %v29, %r23, %r1
    li    %r24, 320
    lvx  %v30, %r24, %r1
    li    %r25, 336
    lvx  %v31, %r25, %r1
#endif
#endif
    /* restore R14 - R31 */
    ld  %r14, 352(%r1)
    ld  %r15, 360(%r1)
    ld  %r16, 368(%r1)
    ld  %r17, 376(%r1)
    ld  %r18, 384(%r1)
    ld  %r19, 392(%r1)
    ld  %r20, 400(%r1)
    ld  %r21, 408(%r1)
    ld  %r22, 416(%r1)
    ld  %r23, 424(%r1)
    ld  %r24, 432(%r1)
    ld  %r25, 440(%r1)
    ld  %r26, 448(%r1)
    ld  %r27, 456(%r1)
    ld  %r28, 464(%r1)
    ld  %r29, 472(%r1)
    ld  %r30, 480(%r1)
    ld  %r31, 488(%r1)
#if _CALL_ELF != 2
    /* restore TOC */
    ld  %r2,  496(%r1)
#endif

    /* restore CR */
    ld  %r0, 504(%r1)
    mtcr  %r0
    /* restore LR */
    ld  %r0, 512(%r1)
    mtlr  %r0

    /* load PC */
    ld  %r12, 520(%r1)
    /* restore CTR */
    mtctr  %r12

    /* adjust stack */
    addi  %r1, %r1, 528

    /* jump to context */
    bctr
#if _CALL_ELF == 2
    .size switch_fcontext, .-switch_fcontext
#else
# ifdef _CALL_LINUX
    .size .switch_fcontext, .-.L.switch_fcontext
# else
    .size .switch_fcontext, .-.switch_fcontext
# endif
#endif

/*
void jump_fcontext(fcontext_t *p_new_ctx);
*/
.globl jump_fcontext
#if _CALL_ELF == 2
    .text
    .align 2
jump_fcontext:
        addis   %r2, %r12, .TOC.-jump_fcontext@ha
        addi    %r2, %r2, .TOC.-jump_fcontext@l
        .localentry jump_fcontext, . - jump_fcontext
#else
    .section ".opd","aw"
    .align 3
jump_fcontext:
# ifdef _CALL_LINUX
        .quad   .L.jump_fcontext,.TOC.@tocbase,0
        .type   jump_fcontext,@function
        .text
        .align 2
.L.jump_fcontext:
# else
        .hidden .jump_fcontext
        .globl  .jump_fcontext
        .quad   .jump_fcontext,.TOC.@tocbase,0
        .size   jump_fcontext,24
        .type   .jump_fcontext,@function
        .text
        .align 2
.jump_fcontext:
# endif
#endif
    /* restore RSP (pointing to context-data) from p_new_ctx (R3) */
    ld  %r1, 0(%r3)

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore F14 - F31 */
    lfd  %f14, 0(%r1)
    lfd  %f15, 8(%r1)
    lfd  %f16, 16(%r1)
    lfd  %f17, 24(%r1)
    lfd  %f18, 32(%r1)
    lfd  %f19, 40(%r1)
    lfd  %f20, 48(%r1)
    lfd  %f21, 56(%r1)
    lfd  %f22, 64(%r1)
    lfd  %f23, 72(%r1)
    lfd  %f24, 80(%r1)
    lfd  %f25, 88(%r1)
    lfd  %f26, 96(%r1)
    lfd  %f27, 104(%r1)
    lfd  %f28, 112(%r1)
    lfd  %f29, 120(%r1)
    lfd  %f30, 128(%r1)
    lfd  %f31, 136(%r1)
    /* restore FPSCR */
    lfd  %f0,  144(%r1)
    mtfsf  0xff, %f0
#ifdef __VSX__
    li    %r10, 144
    /* Restore VSCR.  Only the last 32 bits are used */
    lvx   %v19, %r10, %r1
    mtvscr %v19
    /* restore V20 - V21 */
    li    %r14, 160
    lvx  %v20, %r14, %r1
    li    %r15, 176
    lvx  %v21, %r15, %r1
    li    %r16, 192
    lvx  %v22, %r16, %r1
    li    %r17, 208
    lvx  %v23, %r17, %r1
    li    %r18, 224
    lvx  %v24, %r18, %r1
    li    %r19, 240
    lvx  %v25, %r19, %r1
    li    %r20, 256
    lvx  %v26, %r20, %r1
    li    %r21, 272
    lvx  %v27, %r21, %r1
    li    %r22, 288
    lvx  %v28, %r22, %r1
    li    %r23, 304
    lvx  %v29, %r23, %r1
    li    %r24, 320
    lvx  %v30, %r24, %r1
    li    %r25, 336
    lvx  %v31, %r25, %r1
#endif
#endif
    /* restore R14 - R31 */
    ld  %r14, 352(%r1)
    ld  %r15, 360(%r1)
    ld  %r16, 368(%r1)
    ld  %r17, 376(%r1)
    ld  %r18, 384(%r1)
    ld  %r19, 392(%r1)
    ld  %r20, 400(%r1)
    ld  %r21, 408(%r1)
    ld  %r22, 416(%r1)
    ld  %r23, 424(%r1)
    ld  %r24, 432(%r1)
    ld  %r25, 440(%r1)
    ld  %r26, 448(%r1)
    ld  %r27, 456(%r1)
    ld  %r28, 464(%r1)
    ld  %r29, 472(%r1)
    ld  %r30, 480(%r1)
    ld  %r31, 488(%r1)
#if _CALL_ELF != 2
    /* restore TOC */
    ld  %r2,  496(%r1)
#endif

    /* restore CR */
    ld  %r0, 504(%r1)
    mtcr  %r0
    /* restore LR */
    ld  %r0, 512(%r1)
    mtlr  %r0

    /* load PC */
    ld  %r12, 520(%r1)
    /* restore CTR */
    mtctr  %r12

    /* adjust stack */
    addi  %r1, %r1, 528

    /* jump to context */
    bctr
#if _CALL_ELF == 2
    .size jump_fcontext, .-jump_fcontext
#else
# ifdef _CALL_LINUX
    .size .jump_fcontext, .-.L.jump_fcontext
# else
    .size .jump_fcontext, .-.jump_fcontext
# endif
#endif


/*
void init_and_switch_fcontext(fcontext_t *p_new_ctx,
                              void (*f_thread)(fcontext_t *),
                              void *p_stacktop, fcontext_t *p_old_ctx);
*/
.globl init_and_switch_fcontext
#if _CALL_ELF == 2
    .text
    .align 2
init_and_switch_fcontext:
        addis   %r2, %r12, .TOC.-init_and_switch_fcontext@ha
        addi    %r2, %r2, .TOC.-init_and_switch_fcontext@l
        .localentry init_and_switch_fcontext, . - init_and_switch_fcontext
#else
    .section ".opd","aw"
    .align 3
init_and_switch_fcontext:
# ifdef _CALL_LINUX
        .quad   .L.init_and_switch_fcontext,.TOC.@tocbase,0
        .type   init_and_switch_fcontext,@function
        .text
        .align 2
.L.init_and_switch_fcontext:
# else
        .hidden .init_and_switch_fcontext
        .globl  .init_and_switch_fcontext
        .quad   .init_and_switch_fcontext,.TOC.@tocbase,0
        .size   init_and_switch_fcontext,24
        .type   .init_and_switch_fcontext,@function
        .text
        .align 2
.init_and_switch_fcontext:
# endif
#endif
    /* shift address in p_stacktop (R5) to lower 16 byte boundary */
    clrrdi  %r5, %r5, 4

    /* save TOC in the target stack (p_stacktop, R5) */
    /* TOC must be saved when an external function is called. */
    std  %r2, -24(%r5)

    /* reserve space on stack */
    subi  %r1, %r1, 528
    /* save R14 - R31 */
    std  %r14, 352(%r1)
    std  %r15, 360(%r1)
    std  %r16, 368(%r1)
    std  %r17, 376(%r1)
    std  %r18, 384(%r1)
    std  %r19, 392(%r1)
    std  %r20, 400(%r1)
    std  %r21, 408(%r1)
    std  %r22, 416(%r1)
    std  %r23, 424(%r1)
    std  %r24, 432(%r1)
    std  %r25, 440(%r1)
    std  %r26, 448(%r1)
    std  %r27, 456(%r1)
    std  %r28, 464(%r1)
    std  %r29, 472(%r1)
    std  %r30, 480(%r1)
    std  %r31, 488(%r1)
#if _CALL_ELF != 2
    /* save TOC */
    std  %r2,  496(%r1)  # save TOC
#endif

    /* save CR */
    mfcr  %r0
    std  %r0, 504(%r1)
    /* save LR */
    mflr  %r0
    std  %r0, 512(%r1)
    /* save LR as PC */
    std  %r0, 520(%r1)

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save F14 - F32 */
    stfd  %f14, 0(%r1)
    stfd  %f15, 8(%r1)
    stfd  %f16, 16(%r1)
    stfd  %f17, 24(%r1)
    stfd  %f18, 32(%r1)
    stfd  %f19, 40(%r1)
    stfd  %f20, 48(%r1)
    stfd  %f21, 56(%r1)
    stfd  %f22, 64(%r1)
    stfd  %f23, 72(%r1)
    stfd  %f24, 80(%r1)
    stfd  %f25, 88(%r1)
    stfd  %f26, 96(%r1)
    stfd  %f27, 104(%r1)
    stfd  %f28, 112(%r1)
    stfd  %f29, 120(%r1)
    stfd  %f30, 128(%r1)
    stfd  %f31, 136(%r1)
#ifdef __VSX__
    /* VSCR can be loaded only to Vn.  To store VSCR as it is a vector, */
    /* it must be written before saving FPSCR. */
    /* load VSCR. */
    mfvscr %v19
    li    %r10, 144
    /* save VSCR.  Only the last 32 bits are used */
    stvx  %v19, %r10, %r1
#endif
    /* load FPSCR */
    mffs  %f0
    /* save FPSCR */
    stfd  %f0, 144(%r1)
#ifdef __VSX__
    /* OpenPOWER saves V20 - V31 (vector units) */
    /* Note stvx cannot take an immediate value as an offset. */
    li    %r14, 160
    stvx  %v20, %r14, %r1
    li    %r15, 176
    stvx  %v21, %r15, %r1
    li    %r16, 192
    stvx  %v22, %r16, %r1
    li    %r17, 208
    stvx  %v23, %r17, %r1
    li    %r18, 224
    stvx  %v24, %r18, %r1
    li    %r19, 240
    stvx  %v25, %r19, %r1
    li    %r20, 256
    stvx  %v26, %r20, %r1
    li    %r21, 272
    stvx  %v27, %r21, %r1
    li    %r22, 288
    stvx  %v28, %r22, %r1
    li    %r23, 304
    stvx  %v29, %r23, %r1
    li    %r24, 320
    stvx  %v30, %r24, %r1
    li    %r25, 336
    stvx  %v31, %r25, %r1
#endif
#endif

    /* store RSP (pointing to context-data) in p_old_ctx (R6) */
    std  %r1, 0(%r6)

    /* set RSP (pointing to context-data) from p_stacktop (R5) */
    /* R5 must be 16-byte aligned. */
    subi  %r1, %r5, 48

    /* set f_thread (R4) to CTR. */
    /* f_thread can be a global entry point, so R12 must be set as well */
    mr    %r12, %r4
    mtctr %r12
    /* call CTR (=f_thread) */
    /* note: p_new_ctx (R3) has been already set (as the first argument). */
    /* note: TOC has been saved at the very beginning of the function */
    bctrl
    /* unreachable. */
#if _CALL_ELF == 2
    .size init_and_switch_fcontext, .-init_and_switch_fcontext
#else
# ifdef _CALL_LINUX
    .size .init_and_switch_fcontext, .-.L.init_and_switch_fcontext
# else
    .size .init_and_switch_fcontext, .-.init_and_switch_fcontext
# endif
#endif


/*
void init_and_jump_fcontext(fcontext_t *p_new_ctx,
                            void (*f_thread)(fcontext_t *), void *p_stacktop);
*/
.globl init_and_jump_fcontext
#if _CALL_ELF == 2
    .text
    .align 2
init_and_jump_fcontext:
        addis   %r2, %r12, .TOC.-init_and_jump_fcontext@ha
        addi    %r2, %r2, .TOC.-init_and_jump_fcontext@l
        .localentry init_and_jump_fcontext, . - init_and_jump_fcontext
#else
    .section ".opd","aw"
    .align 3
init_and_jump_fcontext:
# ifdef _CALL_LINUX
        .quad   .L.init_and_jump_fcontext,.TOC.@tocbase,0
        .type   init_and_jump_fcontext,@function
        .text
        .align 2
.L.init_and_jump_fcontext:
# else
        .hidden .init_and_jump_fcontext
        .globl  .init_and_jump_fcontext
        .quad   .init_and_jump_fcontext,.TOC.@tocbase,0
        .size   init_and_jump_fcontext,24
        .type   .init_and_jump_fcontext,@function
        .text
        .align 2
.init_and_jump_fcontext:
# endif
#endif
    /* shift address in p_stacktop (R5) to lower 16 byte boundary */
    clrrdi  %r5, %r5, 4

    /* save TOC in the target stack (p_stacktop, R5) */
    /* TOC must be saved when an external function is called. */
    std  %r2, -24(%r5)

    /* set RSP (pointing to context-data) from p_stacktop (R5) */
    /* R5 must be 16-byte aligned. */
    subi  %r1, %r5, 48

    /* set f_thread (R4) to CTR. */
    /* f_thread can be a global entry point, so R12 must be set as well */
    mr    %r12, %r4
    mtctr %r12
    /* call CTR (=f_thread) */
    /* note: p_new_ctx (R3) has been already set (as the first argument). */
    /* note: TOC has been saved at the very beginning of the function */
    bctrl
    /* unreachable. */
#if _CALL_ELF == 2
    .size init_and_jump_fcontext, .-init_and_jump_fcontext
#else
# ifdef _CALL_LINUX
    .size .init_and_jump_fcontext, .-.L.init_and_jump_fcontext
# else
    .size .init_and_jump_fcontext, .-.init_and_jump_fcontext
# endif
#endif

/*
void switch_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                               fcontext_t *p_new_ctx, fcontext_t *p_old_ctx);
*/
.globl switch_with_call_fcontext
#if _CALL_ELF == 2
    .text
    .align 2
switch_with_call_fcontext:
        addis   %r2, %r12, .TOC.-switch_with_call_fcontext@ha
        addi    %r2, %r2, .TOC.-switch_with_call_fcontext@l
        .localentry switch_with_call_fcontext, . - switch_with_call_fcontext
#else
    .section ".opd","aw"
    .align 3
switch_with_call_fcontext:
# ifdef _CALL_LINUX
        .quad   .L.switch_with_call_fcontext,.TOC.@tocbase,0
        .type   switch_with_call_fcontext,@function
        .text
        .align 2
.L.switch_with_call_fcontext:
# else
        .hidden .switch_with_call_fcontext
        .globl  .switch_with_call_fcontext
        .quad   .switch_with_call_fcontext,.TOC.@tocbase,0
        .size   switch_with_call_fcontext,24
        .type   .switch_with_call_fcontext,@function
        .text
        .align 2
.switch_with_call_fcontext:
# endif
#endif
    /* reserve space on stack */
    subi  %r1, %r1, 528
    /* save R14 - R31 */
    std  %r14, 352(%r1)
    std  %r15, 360(%r1)
    std  %r16, 368(%r1)
    std  %r17, 376(%r1)
    std  %r18, 384(%r1)
    std  %r19, 392(%r1)
    std  %r20, 400(%r1)
    std  %r21, 408(%r1)
    std  %r22, 416(%r1)
    std  %r23, 424(%r1)
    std  %r24, 432(%r1)
    std  %r25, 440(%r1)
    std  %r26, 448(%r1)
    std  %r27, 456(%r1)
    std  %r28, 464(%r1)
    std  %r29, 472(%r1)
    std  %r30, 480(%r1)
    std  %r31, 488(%r1)
#if _CALL_ELF != 2
    /* save TOC */
    std  %r2,  496(%r1)  # save TOC
#endif

    /* save CR */
    mfcr  %r0
    std  %r0, 504(%r1)
    /* save LR */
    mflr  %r0
    std  %r0, 512(%r1)
    /* save LR as PC */
    std  %r0, 520(%r1)

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save F14 - F32 */
    stfd  %f14, 0(%r1)
    stfd  %f15, 8(%r1)
    stfd  %f16, 16(%r1)
    stfd  %f17, 24(%r1)
    stfd  %f18, 32(%r1)
    stfd  %f19, 40(%r1)
    stfd  %f20, 48(%r1)
    stfd  %f21, 56(%r1)
    stfd  %f22, 64(%r1)
    stfd  %f23, 72(%r1)
    stfd  %f24, 80(%r1)
    stfd  %f25, 88(%r1)
    stfd  %f26, 96(%r1)
    stfd  %f27, 104(%r1)
    stfd  %f28, 112(%r1)
    stfd  %f29, 120(%r1)
    stfd  %f30, 128(%r1)
    stfd  %f31, 136(%r1)
#ifdef __VSX__
    /* VSCR can be loaded only to Vn.  To store VSCR as it is a vector, */
    /* it must be written before saving FPSCR. */
    /* load VSCR. */
    mfvscr %v19
    li    %r10, 144
    /* save VSCR.  Only the last 32 bits are used */
    stvx  %v19, %r10, %r1
#endif
    /* load FPSCR */
    mffs  %f0
    /* save FPSCR */
    stfd  %f0, 144(%r1)
#ifdef __VSX__
    /* OpenPOWER saves V20 - V31 (vector units) */
    /* Note stvx cannot take an immediate value as an offset. */
    li    %r14, 160
    stvx  %v20, %r14, %r1
    li    %r15, 176
    stvx  %v21, %r15, %r1
    li    %r16, 192
    stvx  %v22, %r16, %r1
    li    %r17, 208
    stvx  %v23, %r17, %r1
    li    %r18, 224
    stvx  %v24, %r18, %r1
    li    %r19, 240
    stvx  %v25, %r19, %r1
    li    %r20, 256
    stvx  %v26, %r20, %r1
    li    %r21, 272
    stvx  %v27, %r21, %r1
    li    %r22, 288
    stvx  %v28, %r22, %r1
    li    %r23, 304
    stvx  %v29, %r23, %r1
    li    %r24, 320
    stvx  %v30, %r24, %r1
    li    %r25, 336
    stvx  %v31, %r25, %r1
#endif
#endif

    /* store RSP (pointing to context-data) in p_old_ctx (R6) */
    std  %r1, 0(%r6)

    /* restore RSP (pointing to context-data) from p_new_ctx (R5) */
    ld   %r1, 0(%r5)

    /* set f_cb (R4) to CTR.
     * f_cb can be a global entry point, so R12 must be set as well */
    mr    %r12, %r4
    mtctr %r12
    /* save necessary things in a stack, including TOC. */
    subi  %r1, %r1, 48
    std   %r2, 24(%r1)
    /* call f_cb.  cb_arg (R3) has already been set.
     * all the caller-saved registers will be discarded */
    bctrl
    /* restore a stack. TOC will be restored below. */
    addi  %r1, %r1, 48

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore F14 - F31 */
    lfd  %f14, 0(%r1)
    lfd  %f15, 8(%r1)
    lfd  %f16, 16(%r1)
    lfd  %f17, 24(%r1)
    lfd  %f18, 32(%r1)
    lfd  %f19, 40(%r1)
    lfd  %f20, 48(%r1)
    lfd  %f21, 56(%r1)
    lfd  %f22, 64(%r1)
    lfd  %f23, 72(%r1)
    lfd  %f24, 80(%r1)
    lfd  %f25, 88(%r1)
    lfd  %f26, 96(%r1)
    lfd  %f27, 104(%r1)
    lfd  %f28, 112(%r1)
    lfd  %f29, 120(%r1)
    lfd  %f30, 128(%r1)
    lfd  %f31, 136(%r1)
    /* restore FPSCR */
    lfd  %f0,  144(%r1)
    mtfsf  0xff, %f0
#ifdef __VSX__
    li    %r10, 144
    /* Restore VSCR.  Only the last 32 bits are used */
    lvx   %v19, %r10, %r1
    mtvscr %v19
    /* restore V20 - V21 */
    li    %r14, 160
    lvx  %v20, %r14, %r1
    li    %r15, 176
    lvx  %v21, %r15, %r1
    li    %r16, 192
    lvx  %v22, %r16, %r1
    li    %r17, 208
    lvx  %v23, %r17, %r1
    li    %r18, 224
    lvx  %v24, %r18, %r1
    li    %r19, 240
    lvx  %v25, %r19, %r1
    li    %r20, 256
    lvx  %v26, %r20, %r1
    li    %r21, 272
    lvx  %v27, %r21, %r1
    li    %r22, 288
    lvx  %v28, %r22, %r1
    li    %r23, 304
    lvx  %v29, %r23, %r1
    li    %r24, 320
    lvx  %v30, %r24, %r1
    li    %r25, 336
    lvx  %v31, %r25, %r1
#endif
#endif
    /* restore R14 - R31 */
    ld  %r14, 352(%r1)
    ld  %r15, 360(%r1)
    ld  %r16, 368(%r1)
    ld  %r17, 376(%r1)
    ld  %r18, 384(%r1)
    ld  %r19, 392(%r1)
    ld  %r20, 400(%r1)
    ld  %r21, 408(%r1)
    ld  %r22, 416(%r1)
    ld  %r23, 424(%r1)
    ld  %r24, 432(%r1)
    ld  %r25, 440(%r1)
    ld  %r26, 448(%r1)
    ld  %r27, 456(%r1)
    ld  %r28, 464(%r1)
    ld  %r29, 472(%r1)
    ld  %r30, 480(%r1)
    ld  %r31, 488(%r1)
#if _CALL_ELF != 2
    /* restore TOC */
    ld  %r2,  496(%r1)
#endif

    /* restore CR */
    ld  %r0, 504(%r1)
    mtcr  %r0
    /* restore LR */
    ld  %r0, 512(%r1)
    mtlr  %r0

    /* load PC */
    ld  %r12, 520(%r1)
    /* restore CTR */
    mtctr  %r12

    /* adjust stack */
    addi  %r1, %r1, 528

    /* jump to context */
    bctr
#if _CALL_ELF == 2
    .size switch_with_call_fcontext, .-switch_with_call_fcontext
#else
# ifdef _CALL_LINUX
    .size .switch_with_call_fcontext, .-.L.switch_with_call_fcontext
# else
    .size .switch_with_call_fcontext, .-.switch_with_call_fcontext
# endif
#endif

/*
void jump_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                             fcontext_t *p_new_ctx);
*/
.globl jump_with_call_fcontext
#if _CALL_ELF == 2
    .text
    .align 2
jump_with_call_fcontext:
        addis   %r2, %r12, .TOC.-jump_with_call_fcontext@ha
        addi    %r2, %r2, .TOC.-jump_with_call_fcontext@l
        .localentry jump_with_call_fcontext, . - jump_with_call_fcontext
#else
    .section ".opd","aw"
    .align 3
jump_with_call_fcontext:
# ifdef _CALL_LINUX
        .quad   .L.jump_with_call_fcontext,.TOC.@tocbase,0
        .type   jump_with_call_fcontext,@function
        .text
        .align 2
.L.jump_with_call_fcontext:
# else
        .hidden .jump_with_call_fcontext
        .globl  .jump_with_call_fcontext
        .quad   .jump_with_call_fcontext,.TOC.@tocbase,0
        .size   jump_with_call_fcontext,24
        .type   .jump_with_call_fcontext,@function
        .text
        .align 2
.jump_with_call_fcontext:
# endif
#endif
    /* restore RSP (pointing to context-data) from p_new_ctx (R5) */
    ld  %r1, 0(%r5)

    /* set f_cb (R4) to CTR.
     * f_cb can be a global entry point, so R12 must be set as well */
    mr    %r12, %r4
    mtctr %r12
    /* save necessary things in a stack, including TOC. */
    subi  %r1, %r1, 48
    std   %r2, 24(%r1)
    /* call f_cb.  cb_arg (R3) has already been set.
     * all the caller-saved registers will be discarded */
    bctrl
    /* restore a stack. TOC will be restored below. */
    addi  %r1, %r1, 48

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* restore F14 - F31 */
    lfd  %f14, 0(%r1)
    lfd  %f15, 8(%r1)
    lfd  %f16, 16(%r1)
    lfd  %f17, 24(%r1)
    lfd  %f18, 32(%r1)
    lfd  %f19, 40(%r1)
    lfd  %f20, 48(%r1)
    lfd  %f21, 56(%r1)
    lfd  %f22, 64(%r1)
    lfd  %f23, 72(%r1)
    lfd  %f24, 80(%r1)
    lfd  %f25, 88(%r1)
    lfd  %f26, 96(%r1)
    lfd  %f27, 104(%r1)
    lfd  %f28, 112(%r1)
    lfd  %f29, 120(%r1)
    lfd  %f30, 128(%r1)
    lfd  %f31, 136(%r1)
    /* restore FPSCR */
    lfd  %f0,  144(%r1)
    mtfsf  0xff, %f0
#ifdef __VSX__
    li    %r10, 144
    /* Restore VSCR.  Only the last 32 bits are used */
    lvx   %v19, %r10, %r1
    mtvscr %v19
    /* restore V20 - V21 */
    li    %r14, 160
    lvx  %v20, %r14, %r1
    li    %r15, 176
    lvx  %v21, %r15, %r1
    li    %r16, 192
    lvx  %v22, %r16, %r1
    li    %r17, 208
    lvx  %v23, %r17, %r1
    li    %r18, 224
    lvx  %v24, %r18, %r1
    li    %r19, 240
    lvx  %v25, %r19, %r1
    li    %r20, 256
    lvx  %v26, %r20, %r1
    li    %r21, 272
    lvx  %v27, %r21, %r1
    li    %r22, 288
    lvx  %v28, %r22, %r1
    li    %r23, 304
    lvx  %v29, %r23, %r1
    li    %r24, 320
    lvx  %v30, %r24, %r1
    li    %r25, 336
    lvx  %v31, %r25, %r1
#endif
#endif
    /* restore R14 - R31 */
    ld  %r14, 352(%r1)
    ld  %r15, 360(%r1)
    ld  %r16, 368(%r1)
    ld  %r17, 376(%r1)
    ld  %r18, 384(%r1)
    ld  %r19, 392(%r1)
    ld  %r20, 400(%r1)
    ld  %r21, 408(%r1)
    ld  %r22, 416(%r1)
    ld  %r23, 424(%r1)
    ld  %r24, 432(%r1)
    ld  %r25, 440(%r1)
    ld  %r26, 448(%r1)
    ld  %r27, 456(%r1)
    ld  %r28, 464(%r1)
    ld  %r29, 472(%r1)
    ld  %r30, 480(%r1)
    ld  %r31, 488(%r1)
#if _CALL_ELF != 2
    /* restore TOC */
    ld  %r2,  496(%r1)
#endif

    /* restore CR */
    ld  %r0, 504(%r1)
    mtcr  %r0
    /* restore LR */
    ld  %r0, 512(%r1)
    mtlr  %r0

    /* load PC */
    ld  %r12, 520(%r1)
    /* restore CTR */
    mtctr  %r12

    /* adjust stack */
    addi  %r1, %r1, 528

    /* jump to context */
    bctr
#if _CALL_ELF == 2
    .size jump_with_call_fcontext, .-jump_with_call_fcontext
#else
# ifdef _CALL_LINUX
    .size .jump_with_call_fcontext, .-.L.jump_with_call_fcontext
# else
    .size .jump_with_call_fcontext, .-.jump_with_call_fcontext
# endif
#endif


/*
void init_and_switch_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                                        fcontext_t *p_new_ctx,
                                        void (*f_thread)(fcontext_t *),
                                        void *p_stacktop,
                                        fcontext_t *p_old_ctx);
*/
.globl init_and_switch_with_call_fcontext
#if _CALL_ELF == 2
    .text
    .align 2
init_and_switch_with_call_fcontext:
        addis   %r2, %r12, .TOC.-init_and_switch_with_call_fcontext@ha
        addi    %r2, %r2, .TOC.-init_and_switch_with_call_fcontext@l
        .localentry init_and_switch_with_call_fcontext, . - init_and_switch_with_call_fcontext
#else
    .section ".opd","aw"
    .align 3
init_and_switch_with_call_fcontext:
# ifdef _CALL_LINUX
        .quad   .L.init_and_switch_with_call_fcontext,.TOC.@tocbase,0
        .type   init_and_switch_with_call_fcontext,@function
        .text
        .align 2
.L.init_and_switch_with_call_fcontext:
# else
        .hidden .init_and_switch_with_call_fcontext
        .globl  .init_and_switch_with_call_fcontext
        .quad   .init_and_switch_with_call_fcontext,.TOC.@tocbase,0
        .size   init_and_switch_with_call_fcontext,24
        .type   .init_and_switch_with_call_fcontext,@function
        .text
        .align 2
.init_and_switch_with_call_fcontext:
# endif
#endif
    /* shift address in p_stacktop (R7) to lower 16 byte boundary */
    clrrdi  %r7, %r7, 4

    /* save TOC in the target stack (p_stacktop, R7) */
    /* TOC must be saved when an external function is called. */
    std  %r2, -24(%r7)

    /* reserve space on stack */
    subi  %r1, %r1, 528
    /* save R14 - R31 */
    std  %r14, 352(%r1)
    std  %r15, 360(%r1)
    std  %r16, 368(%r1)
    std  %r17, 376(%r1)
    std  %r18, 384(%r1)
    std  %r19, 392(%r1)
    std  %r20, 400(%r1)
    std  %r21, 408(%r1)
    std  %r22, 416(%r1)
    std  %r23, 424(%r1)
    std  %r24, 432(%r1)
    std  %r25, 440(%r1)
    std  %r26, 448(%r1)
    std  %r27, 456(%r1)
    std  %r28, 464(%r1)
    std  %r29, 472(%r1)
    std  %r30, 480(%r1)
    std  %r31, 488(%r1)
#if _CALL_ELF != 2
    /* save TOC */
    std  %r2,  496(%r1)  # save TOC
#endif

    /* save CR */
    mfcr  %r0
    std  %r0, 504(%r1)
    /* save LR */
    mflr  %r0
    std  %r0, 512(%r1)
    /* save LR as PC */
    std  %r0, 520(%r1)

#if ABTD_FCONTEXT_PRESERVE_FPU
    /* save F14 - F32 */
    stfd  %f14, 0(%r1)
    stfd  %f15, 8(%r1)
    stfd  %f16, 16(%r1)
    stfd  %f17, 24(%r1)
    stfd  %f18, 32(%r1)
    stfd  %f19, 40(%r1)
    stfd  %f20, 48(%r1)
    stfd  %f21, 56(%r1)
    stfd  %f22, 64(%r1)
    stfd  %f23, 72(%r1)
    stfd  %f24, 80(%r1)
    stfd  %f25, 88(%r1)
    stfd  %f26, 96(%r1)
    stfd  %f27, 104(%r1)
    stfd  %f28, 112(%r1)
    stfd  %f29, 120(%r1)
    stfd  %f30, 128(%r1)
    stfd  %f31, 136(%r1)
#ifdef __VSX__
    /* VSCR can be loaded only to Vn.  To store VSCR as it is a vector, */
    /* it must be written before saving FPSCR. */
    /* load VSCR. */
    mfvscr %v19
    li    %r10, 144
    /* save VSCR.  Only the last 32 bits are used */
    stvx  %v19, %r10, %r1
#endif
    /* load FPSCR */
    mffs  %f0
    /* save FPSCR */
    stfd  %f0, 144(%r1)
#ifdef __VSX__
    /* OpenPOWER saves V20 - V31 (vector units) */
    /* Note stvx cannot take an immediate value as an offset. */
    li    %r14, 160
    stvx  %v20, %r14, %r1
    li    %r15, 176
    stvx  %v21, %r15, %r1
    li    %r16, 192
    stvx  %v22, %r16, %r1
    li    %r17, 208
    stvx  %v23, %r17, %r1
    li    %r18, 224
    stvx  %v24, %r18, %r1
    li    %r19, 240
    stvx  %v25, %r19, %r1
    li    %r20, 256
    stvx  %v26, %r20, %r1
    li    %r21, 272
    stvx  %v27, %r21, %r1
    li    %r22, 288
    stvx  %v28, %r22, %r1
    li    %r23, 304
    stvx  %v29, %r23, %r1
    li    %r24, 320
    stvx  %v30, %r24, %r1
    li    %r25, 336
    stvx  %v31, %r25, %r1
#endif
#endif

    /* store RSP (pointing to context-data) in p_old_ctx (R8) */
    std  %r1, 0(%r8)

    /* set RSP (pointing to context-data) from p_stacktop (R7) */
    /* R7 must be 16-byte aligned. */
    subi  %r1, %r7, 48

    /* save p_new_ctx (R5) in R19 (callee-saved) */
    mr    %r19, %r5
    /* save f_thread (R6) in R20 (callee-saved) */
    mr    %r20, %r6

    /* set f_cb (R4) to CTR. */
    /* f_cb can be a global entry point, so R12 must be set as well */
    mr    %r12, %r4
    mtctr %r12
    /* call CTR (=f_cb) */
    /* note: cb_arg (R3) has been already set (as the first argument). */
    /* note: TOC has been saved at the very beginning of the function */
    /* all the caller-saved registers will be discarded. */
    bctrl

    /* set the first argument (R3) to p_new_ctx (R19) */
    mr    %r3, %r19

    /* set f_thread (R20) to CTR. */
    /* f_thread can be a global entry point, so R12 must be set as well */
    mr    %r12, %r20
    mtctr %r12

    /* call CTR (=f_thread) */
    bctrl
    /* unreachable. */
#if _CALL_ELF == 2
    .size init_and_switch_with_call_fcontext, .-init_and_switch_with_call_fcontext
#else
# ifdef _CALL_LINUX
    .size .init_and_switch_with_call_fcontext, .-.L.init_and_switch_with_call_fcontext
# else
    .size .init_and_switch_with_call_fcontext, .-.init_and_switch_with_call_fcontext
# endif
#endif


/*
void init_and_jump_with_call_fcontext(void *cb_arg, void (*f_cb)(void *),
                                      fcontext_t *p_new_ctx,
                                      void (*f_thread)(fcontext_t *),
                                      void *p_stacktop);
*/
.globl init_and_jump_with_call_fcontext
#if _CALL_ELF == 2
    .text
    .align 2
init_and_jump_with_call_fcontext:
        addis   %r2, %r12, .TOC.-init_and_jump_with_call_fcontext@ha
        addi    %r2, %r2, .TOC.-init_and_jump_with_call_fcontext@l
        .localentry init_and_jump_with_call_fcontext, . - init_and_jump_with_call_fcontext
#else
    .section ".opd","aw"
    .align 3
init_and_jump_with_call_fcontext:
# ifdef _CALL_LINUX
        .quad   .L.init_and_jump_with_call_fcontext,.TOC.@tocbase,0
        .type   init_and_jump_with_call_fcontext,@function
        .text
        .align 2
.L.init_and_jump_with_call_fcontext:
# else
        .hidden .init_and_jump_with_call_fcontext
        .globl  .init_and_jump_with_call_fcontext
        .quad   .init_and_jump_with_call_fcontext,.TOC.@tocbase,0
        .size   init_and_jump_with_call_fcontext,24
        .type   .init_and_jump_with_call_fcontext,@function
        .text
        .align 2
.init_and_jump_with_call_fcontext:
# endif
#endif
    /* shift address in p_stacktop (R7) to lower 16 byte boundary */
    clrrdi  %r7, %r7, 4

    /* save TOC in the target stack (p_stacktop, R7) */
    /* TOC must be saved when an external function is called. */
    std  %r2, -24(%r7)

    /* set RSP (pointing to context-data) from p_stacktop (R7) */
    /* R5 must be 16-byte aligned. */
    subi  %r1, %r7, 48

    /* save p_new_ctx (R5) in R19 (callee-saved) */
    mr    %r19, %r5
    /* save f_thread (R6) in R20 (callee-saved) */
    mr    %r20, %r6

    /* set f_cb (R4) to CTR. */
    /* f_cb can be a global entry point, so R12 must be set as well */
    mr    %r12, %r4
    mtctr %r12
    /* call CTR (=f_cb) */
    /* note: cb_arg (R3) has been already set (as the first argument). */
    /* note: TOC has been saved at the very beginning of the function */
    /* all the caller-saved registers will be discarded. */
    bctrl

    /* set the first argument (R3) to p_new_ctx (R19) */
    mr    %r3, %r19

    /* set f_thread (R20) to CTR. */
    /* f_thread can be a global entry point, so R12 must be set as well */
    mr    %r12, %r20
    mtctr %r12

    /* call CTR (=f_thread) */
    bctrl
    /* unreachable. */
#if _CALL_ELF == 2
    .size init_and_jump_with_call_fcontext, .-init_and_jump_with_call_fcontext
#else
# ifdef _CALL_LINUX
    .size .init_and_jump_with_call_fcontext, .-.L.init_and_jump_with_call_fcontext
# else
    .size .init_and_jump_with_call_fcontext, .-.init_and_jump_with_call_fcontext
# endif
#endif


/*
void peek_fcontext(void *arg, void (*f_peek)(void *), fcontext_t *p_target_ctx);
*/
.globl peek_fcontext
#if _CALL_ELF == 2
    .text
    .align 2
peek_fcontext:
        addis   %r2, %r12, .TOC.-peek_fcontext@ha
        addi    %r2, %r2, .TOC.-peek_fcontext@l
        .localentry peek_fcontext, . - peek_fcontext
#else
    .section ".opd","aw"
    .align 3
peek_fcontext:
# ifdef _CALL_LINUX
        .quad   .L.peek_fcontext,.TOC.@tocbase,0
        .type   peek_fcontext,@function
        .text
        .align 2
.L.peek_fcontext:
# else
        .hidden .peek_fcontext
        .globl  .peek_fcontext
        .quad   .peek_fcontext,.TOC.@tocbase,0
        .size   peek_fcontext,24
        .type   .peek_fcontext,@function
        .text
        .align 2
.peek_fcontext:
# endif
#endif
    /* get the target stack pointer from p_target_ctx (R5) and set it to R6 */
    /* the stack pointer must be 16-byte aligned. */
    ld   %r6, 0(%r5)
    /* save LR in the target stack (R6) */
    mflr %r0
    std  %r0, -16(%r6)
    /* save RSP in the target stack (R6) */
    std  %r1, -8(%r6)
    /* save TOC in the target stack (R6) */
    /* TOC must be saved when an external function is called. */
    std  %r2, -24(%r6)

    /* set RSP (pointing to context-data) from p_target_ctx (R6) */
    mr    %r1, %r6
    stdu  %r1, -48(%r1)

    /* set f_peek (R4) to CTR. */
    /* f_peek) can be a global entry point, so R12 must be set as well */
    mr    %r12, %r4
    mtctr %r12
    /* call CTR (=f_peek) */
    /* note: arg (R3) has been already set as the first argument. */
    /* note: TOC has been saved at the very beginning of the function */
    bctrl

    /* restore LR from the stack */
    /* because it might call an external function, set R12 as well */
    ld    %r12, 32(%r1)
    mtlr  %r12
    /* restore TOC from the stack */
    ld    %r2, 24(%r1)
    /* restore RSP from the stack */
    ld    %r1, 40(%r1)
    /* return */
    blr
#if _CALL_ELF == 2
    .size peek_fcontext, .-peek_fcontext
#else
# ifdef _CALL_LINUX
    .size .peek_fcontext, .-.L.peek_fcontext
# else
    .size .peek_fcontext, .-.peek_fcontext
# endif
#endif


/* Mark that we don't need executable stack.  */
.section .note.GNU-stack,"",%progbits

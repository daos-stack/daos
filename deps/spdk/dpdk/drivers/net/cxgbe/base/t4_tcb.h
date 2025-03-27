/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Chelsio Communications.
 * All rights reserved.
 */

#ifndef _T4_TCB_DEFS_H
#define _T4_TCB_DEFS_H

/* 31:24 */
#define W_TCB_SMAC_SEL    0
#define S_TCB_SMAC_SEL    24
#define M_TCB_SMAC_SEL    0xffULL
#define V_TCB_SMAC_SEL(x) ((x) << S_TCB_SMAC_SEL)

/* 95:32 */
#define W_TCB_T_FLAGS    1

/* 105:96 */
#define W_TCB_RSS_INFO    3
#define S_TCB_RSS_INFO    0
#define M_TCB_RSS_INFO    0x3ffULL
#define V_TCB_RSS_INFO(x) ((x) << S_TCB_RSS_INFO)

/* 191:160 */
#define W_TCB_TIMESTAMP    5
#define S_TCB_TIMESTAMP    0
#define M_TCB_TIMESTAMP    0xffffffffULL
#define V_TCB_TIMESTAMP(x) ((x) << S_TCB_TIMESTAMP)

/* 223:192 */
#define W_TCB_T_RTT_TS_RECENT_AGE    6
#define S_TCB_T_RTT_TS_RECENT_AGE    0
#define M_TCB_T_RTT_TS_RECENT_AGE    0xffffffffULL
#define V_TCB_T_RTT_TS_RECENT_AGE(x) ((x) << S_TCB_T_RTT_TS_RECENT_AGE)

/* 255:224 */
#define S_TCB_T_RTSEQ_RECENT    0
#define M_TCB_T_RTSEQ_RECENT    0xffffffffULL
#define V_TCB_T_RTSEQ_RECENT(x) ((x) << S_TCB_T_RTSEQ_RECENT)

#define S_TF_CCTRL_ECE    60

#define S_TF_CCTRL_CWR    61

#define S_TF_CCTRL_RFR    62

#endif /* _T4_TCB_DEFS_H */

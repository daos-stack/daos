/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (C) 2019 Marvell International Ltd.
 */

#ifndef _OTX2_CRYPTODEV_OPS_H_
#define _OTX2_CRYPTODEV_OPS_H_

#include <cryptodev_pmd.h>

#define OTX2_CPT_MIN_HEADROOM_REQ	48
#define OTX2_CPT_MIN_TAILROOM_REQ	208

extern struct rte_cryptodev_ops otx2_cpt_ops;

#endif /* _OTX2_CRYPTODEV_OPS_H_ */

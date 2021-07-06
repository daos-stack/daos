/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_OBJ_CTL_H__
#define __DAOS_OBJ_CTL_H__

#include "daos_hdlr.h"

/**
 * Interactive function testing shell for DAOS
 *
 * Provides a shell to test VOS and DAOS commands.
 *
 * \param[in]     argc   number of arguments
 * \param[in,out] argv   array of character pointers listing the arguments.
 * \return               0 on success, daos_errno code on failure.
 */
int obj_ctl_shell(struct cmd_args_s *ap);

#endif /* __DAOS_OBJ_CTL_H__ */

/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "../utils/utils_refcnt.h"

void ocf_refcnt_init(struct ocf_refcnt *rc)
{
	env_atomic_set(&rc->counter, 0);
	env_atomic_set(&rc->freeze, 0);
	env_atomic_set(&rc->callback, 0);
	rc->cb = NULL;
}

int ocf_refcnt_dec(struct ocf_refcnt *rc)
{
	int val = env_atomic_dec_return(&rc->counter);
	ENV_BUG_ON(val < 0);

	if (!val && env_atomic_cmpxchg(&rc->callback, 1, 0))
		rc->cb(rc->priv);

	return val;
}

int ocf_refcnt_inc(struct ocf_refcnt  *rc)
{
	int val;

	if (!env_atomic_read(&rc->freeze)) {
		val = env_atomic_inc_return(&rc->counter);
		if (!env_atomic_read(&rc->freeze))
			return  val;
		else
			ocf_refcnt_dec(rc);
	}

	return 0;
}


void ocf_refcnt_freeze(struct ocf_refcnt *rc)
{
	env_atomic_inc(&rc->freeze);
}

void ocf_refcnt_register_zero_cb(struct ocf_refcnt *rc, ocf_refcnt_cb_t cb,
		void *priv)
{
	ENV_BUG_ON(!env_atomic_read(&rc->freeze));
	ENV_BUG_ON(env_atomic_read(&rc->callback));

	env_atomic_inc(&rc->counter);
	rc->cb = cb;
	rc->priv = priv;
	env_atomic_set(&rc->callback, 1);
	ocf_refcnt_dec(rc);
}

void ocf_refcnt_unfreeze(struct ocf_refcnt *rc)
{
	int val = env_atomic_dec_return(&rc->freeze);
	ENV_BUG_ON(val < 0);
}

bool ocf_refcnt_frozen(struct ocf_refcnt *rc)
{
	return !!env_atomic_read(&rc->freeze);
}

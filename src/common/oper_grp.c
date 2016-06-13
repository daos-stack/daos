/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#include <daos_event.h>
#include <daos/event.h>
#include <daos/common.h>

struct daos_oper {
	daos_list_t		 op_link;
	daos_event_t		 op_ev;
	/* NB: callback for each event? */
};

struct daos_oper_grp {
	pthread_mutex_t		 gp_lock;
	daos_list_t		 gp_opers;
	daos_oper_grp_comp_t	 gp_comp;
	/** the group event */
	daos_event_t		 gp_ev;
	/** the upper level event */
	daos_event_t		*gp_ev_up;
	void			*gp_args;
};

static int daos_oper_grp_complete(struct daos_oper_grp *grp, int rc);

/**
 * Create an asynchronous operation group.
 * If the operation group has been launched, it will be automatically freed
 * within the event completion callback. Otherwise t should be destroyed
 * explicitly by calling daos_oper_grp_destroy().
 *
 * \param ev_up	[IN]	The upper layer event.
 * \param com	[IN]	Completion callback for the operation group.
 * \param arg	[IN]	Arguments for the callback.
 * \param grpp	[OUT]	The returned operation group.
 */
int
daos_oper_grp_create(daos_event_t *ev_up, daos_oper_grp_comp_t comp,
		     void *args, struct daos_oper_grp **grpp)
{
	struct daos_oper_grp *grp;
	int		      rc;

	D_ALLOC_PTR(grp);
	if (grp == NULL)
		return -DER_NOMEM;

	if (ev_up != NULL) {
		rc = daos_event_init_adv(&grp->gp_ev,
					 DAOS_EVF_NO_POLL |
					 DAOS_EVF_NEED_LAUNCH,
					 daos_ev2eqh(ev_up), NULL);
		if (rc != 0)
			goto failed;
	}

	pthread_mutex_init(&grp->gp_lock, NULL);
	DAOS_INIT_LIST_HEAD(&grp->gp_opers);

	grp->gp_ev_up = ev_up;
	grp->gp_comp  = comp;
	grp->gp_args  = args;
	*grpp = grp;
	return 0;
 failed:
	D_FREE_PTR(grp);
	return rc;
}

/**
 * destroy a operation group
 */
void
daos_oper_grp_destroy(struct daos_oper_grp *grp, int rc)
{
	D_DEBUG(DF_MISC, "Destroy operation group.\n");
	daos_oper_grp_complete(grp, rc);
}

/**
 * Complete a operation group.
 */
int
daos_oper_grp_complete(struct daos_oper_grp *grp, int rc)
{
	struct daos_oper *oper;

	while (!daos_list_empty(&grp->gp_opers)) {
		oper = daos_list_entry(grp->gp_opers.next,
				       struct daos_oper, op_link);
		daos_list_del(&oper->op_link);
		daos_event_fini(&oper->op_ev);

		D_FREE_PTR(oper);
	}

	if (grp->gp_comp)
		grp->gp_comp(grp->gp_args, rc);

	if (grp->gp_ev_up != NULL) {
		daos_event_fini(&grp->gp_ev);

		D_DEBUG(DF_MISC, "Completing upper level event\n");
		daos_event_launch(grp->gp_ev_up, NULL, NULL);
		daos_event_complete(grp->gp_ev_up, rc);
	}

	pthread_mutex_destroy(&grp->gp_lock);
	D_FREE_PTR(grp);
	return rc;
}

static int
daos_oper_grp_comp_cb(struct daos_op_sp *esp, daos_event_t *ev, int rc)
{
	struct daos_oper_grp *grp = (struct daos_oper_grp *)esp->sp_arg;

	D_DEBUG(DF_MISC, "Completing operation group %p\n", grp);
	daos_oper_grp_complete(grp, rc);
	return 0;
}

/**
 * Launch an asynchronous operation group, the group does not require to
 * be explicitly destroyed after this.
 */
int
daos_oper_grp_launch(struct daos_oper_grp *grp)
{
	if (grp->gp_ev_up == NULL) {
		daos_oper_grp_complete(grp, 0);
		return 0;
	}

	daos_ev2sp(&grp->gp_ev)->sp_arg = grp;
	return daos_event_launch(&grp->gp_ev, NULL, daos_oper_grp_comp_cb);
}

/**
 * Allocate a new event
 *
 * TODO:
 * - add per operation callback and parameter
 * - allow to relaunch an event (new EQ API)
 */
int
daos_oper_grp_new_ev(struct daos_oper_grp *grp, struct daos_event **ev_pp)
{
	struct daos_oper	*oper;
	int		 rc;

	if (grp->gp_ev_up == NULL)
		return 0;

	D_ALLOC_PTR(oper);
	if (oper == NULL)
		return -DER_NOMEM;

	rc = daos_event_init_adv(&oper->op_ev, DAOS_EVF_NO_POLL,
				 DAOS_HDL_INVAL, &grp->gp_ev);
	if (rc != 0)
		goto failed;

	pthread_mutex_lock(&grp->gp_lock);
	daos_list_add(&oper->op_link, &grp->gp_opers);
	pthread_mutex_unlock(&grp->gp_lock);

	*ev_pp = &oper->op_ev;
	return 0;
 failed:
	D_FREE_PTR(oper);
	return rc;
}

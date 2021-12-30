/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_rsvc: Replicated Service Server
 *
 * This server module implements a generic framework for different classes of
 * replicated service servers.
 */

#ifndef DAOS_SRV_RSVC_H
#define DAOS_SRV_RSVC_H

#include <abt.h>
#include <daos/rsvc.h>
#include <daos_srv/rdb.h>

/** List of all replicated service classes */
enum ds_rsvc_class_id {
	DS_RSVC_CLASS_MGMT,
	DS_RSVC_CLASS_POOL,
	DS_RSVC_CLASS_TEST,
	DS_RSVC_CLASS_COUNT
};

struct ds_rsvc;

/** Replicated service class */
struct ds_rsvc_class {
	/**
	 * Name the service identified by the generic \a id. The returned name
	 * string will later be passed to D_FREE.
	 */
	int (*sc_name)(d_iov_t *id, char **name);

	/**
	 * Locate the DB of the service identified by \a id. The returned DB
	 * path will later be passed to D_FREE.
	 */
	int (*sc_locate)(d_iov_t *id, char **path);

	/** Allocate a ds_rsvc object and initialize its s_id member. */
	int (*sc_alloc)(d_iov_t *id, struct ds_rsvc **svc);

	/**
	 * Free the ds_rsvc object, after finalizing its s_id member (if
	 * necessary).
	 */
	void (*sc_free)(struct ds_rsvc *svc);

	/**
	 * Bootstrap (i.e., initialize) the DB with the argument passed to
	 * ds_rsvc_start. If supplied, this is called on a self-only service.
	 * See bootstrap_self.
	 */
	int (*sc_bootstrap)(struct ds_rsvc *svc, void *arg);

	/**
	 * Step up to be the new leader. If the DB is new (i.e., has not been
	 * bootstrapped), return +DER_UNINIT.
	 */
	int (*sc_step_up)(struct ds_rsvc *svc);

	/** Step down from the current leadership. */
	void (*sc_step_down)(struct ds_rsvc *svc);

	/**
	 * Drain the leader activities, if any. This is called when stepping
	 * down but before sc_step_down. See rsvc_step_down_cb.
	 */
	void (*sc_drain)(struct ds_rsvc *svc);

	/**
	 * Distribute the system/pool map in the system/pool. This callback is
	 * optional.
	 */
	int (*sc_map_dist)(struct ds_rsvc *svc);
};

void ds_rsvc_class_register(enum ds_rsvc_class_id id,
			    struct ds_rsvc_class *class);
void ds_rsvc_class_unregister(enum ds_rsvc_class_id id);

/** Replicated service state in ds_rsvc.s_term */
enum ds_rsvc_state {
	DS_RSVC_UP_EMPTY,	/**< up but DB newly-created and empty */
	DS_RSVC_UP,		/**< up and ready to serve */
	DS_RSVC_DRAINING,	/**< stepping down */
	DS_RSVC_DOWN		/**< down */
};

/** Replicated service */
struct ds_rsvc {
	d_list_t		s_entry;	/* in rsvc_hash */
	enum ds_rsvc_class_id	s_class;
	d_iov_t			s_id;		/**< for lookups */
	char		       *s_name;		/**< for printing */
	struct rdb	       *s_db;		/**< DB handle */
	char		       *s_db_path;
	uuid_t			s_db_uuid;
	int			s_ref;
	ABT_mutex		s_mutex;	/* for the following members */
	bool			s_stop;
	uint64_t		s_term;		/**< leader term */
	enum ds_rsvc_state	s_state;
	ABT_cond		s_state_cv;
	int			s_leader_ref;	/* on leader state */
	ABT_cond		s_leader_ref_cv;
	bool			s_map_dist;	/* has a map dist request? */
	ABT_cond		s_map_dist_cv;
	ABT_thread		s_map_distd;
	bool			s_map_distd_stop;
};

int ds_rsvc_start_nodb(enum ds_rsvc_class_id class, d_iov_t *id,
		       uuid_t db_uuid);
int ds_rsvc_stop_nodb(enum ds_rsvc_class_id class, d_iov_t *id);

int ds_rsvc_start(enum ds_rsvc_class_id class, d_iov_t *id, uuid_t db_uuid,
		  bool create, size_t size, d_rank_list_t *replicas, void *arg);
int ds_rsvc_stop(enum ds_rsvc_class_id class, d_iov_t *id, bool destroy);
int ds_rsvc_stop_all(enum ds_rsvc_class_id class);
int ds_rsvc_stop_leader(enum ds_rsvc_class_id class, d_iov_t *id,
			struct rsvc_hint *hint);
int ds_rsvc_dist_start(enum ds_rsvc_class_id class, d_iov_t *id,
		       const uuid_t dbid, const d_rank_list_t *ranks,
		       bool create, bool bootstrap, size_t size);
int ds_rsvc_dist_stop(enum ds_rsvc_class_id class, d_iov_t *id,
		      const d_rank_list_t *ranks, d_rank_list_t *excluded,
		      bool destroy);
int ds_rsvc_add_replicas_s(struct ds_rsvc *svc, d_rank_list_t *ranks,
			   size_t size);
int ds_rsvc_add_replicas(enum ds_rsvc_class_id class, d_iov_t *id,
			 d_rank_list_t *ranks, size_t size,
			 struct rsvc_hint *hint);
int ds_rsvc_remove_replicas_s(struct ds_rsvc *svc, d_rank_list_t *ranks,
			      bool stop);
int ds_rsvc_remove_replicas(enum ds_rsvc_class_id class, d_iov_t *id,
			    d_rank_list_t *ranks, bool stop,
			    struct rsvc_hint *hint);
int ds_rsvc_lookup(enum ds_rsvc_class_id class, d_iov_t *id,
		   struct ds_rsvc **svc);
int ds_rsvc_lookup_leader(enum ds_rsvc_class_id class, d_iov_t *id,
			  struct ds_rsvc **svcp, struct rsvc_hint *hint);
void ds_rsvc_get(struct ds_rsvc *svc);
void ds_rsvc_put(struct ds_rsvc *svc);
void ds_rsvc_get_leader(struct ds_rsvc *svc);
void ds_rsvc_put_leader(struct ds_rsvc *svc);
void ds_rsvc_set_hint(struct ds_rsvc *svc, struct rsvc_hint *hint);

int ds_rsvc_set_attr(struct ds_rsvc *svc, struct rdb_tx *tx, rdb_path_t *path,
		     crt_bulk_t remote_bulk, crt_rpc_t *rpc, uint64_t count);
int ds_rsvc_del_attr(struct ds_rsvc *svc, struct rdb_tx *tx, rdb_path_t *path,
		     crt_bulk_t remote_bulk, crt_rpc_t *rpc, uint64_t count);
int ds_rsvc_get_attr(struct ds_rsvc *svc, struct rdb_tx *tx, rdb_path_t *path,
		     crt_bulk_t remote_bulk, crt_rpc_t *rpc, uint64_t count,
		     uint64_t key_length);
int ds_rsvc_list_attr(struct ds_rsvc *svc, struct rdb_tx *tx, rdb_path_t *path,
		      crt_bulk_t remote_bulk, crt_rpc_t *rpc, uint64_t *size);

size_t ds_rsvc_get_md_cap(void);

void ds_rsvc_request_map_dist(struct ds_rsvc *svc);

#endif /* DAOS_SRV_RSVC_H */

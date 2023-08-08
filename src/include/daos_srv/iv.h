/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * daos iv tree definition.
 */
#ifndef __DAOS_SRV_IV_H__
#define __DAOS_SRV_IV_H__

#include <abt.h>

/* DAOS iv cache provide a general interface for daos to use cart IV.
 * Each pool has one iv namespace, which is created when the  pool is
 * connected, and destroyed when pool is disconnected. Each DAOS IV
 * user will attach its entries to the iv namespace, and each user will
 * have a constant key id to locate its entry.
 */
struct ds_iv_ns {
	d_rank_t	iv_master_rank;
	/* Different pool will use different ns id */
	unsigned int	iv_ns_id;
	uint64_t	iv_master_term;
	/* Link to global ns list (ds_iv_list) */
	d_list_t	iv_ns_link;
	/* all of entries under the ns links here */
	d_list_t	iv_entry_list;
	/* Cart IV namespace */
	crt_iv_namespace_t	iv_ns;
	/* pool uuid */
	uuid_t		iv_pool_uuid;

	ABT_eventual	iv_done_eventual;
	int		iv_refcount;
	/**
	 * iv_fini: the IV namespace will be stopped, usually happens
	 * the pool will be destroyed.
	 */
	uint32_t	iv_stop:1;
};

struct ds_iv_class_ops;
/* This structure defines the DAOS IV class type. Each IV user
 * should register its class type during module load by unique
 * predefined class ID. There is a predefined CART IV callback,
 * iv_cache_ops, and some users can share this callbacks, but
 * provides different iv_class_ops, which will be called inside
 */
struct ds_iv_class {
	/* link the class to the ds_iv_class_list */
	d_list_t		iv_class_list;

	/* operations for cart IV */
	struct crt_iv_ops	*iv_class_crt_cbs;

	/* Class id */
	int			iv_class_id;

	/* class id for cart */
	int			iv_cart_class_id;
	/* operations for this IV class */
	struct ds_iv_class_ops	*iv_class_ops;
};

#define IV_KEY_BUF_SIZE 48
/*
 * Those callbacks uses ds_iv_key to locate the iv cache entry and class
 * type.
 *
 * When IV callback arrives, it will locate the cache entry in namespace
 * by the key. If there is only one entry for the class, then only using
 * class_id can locate the entry, otherwise using key + key_cmp callback.
 */
struct ds_iv_key {
	d_rank_t	rank;
	int		class_id;
	char		key_buf[IV_KEY_BUF_SIZE];
};

/**
 * Each IV user will create one or multiple entries attached to ds iv
 * namespace, which can be located by ds_iv_key.
 */
struct ds_iv_entry {
	/* Back pointer to NS */
	struct ds_iv_ns *ns;
	/* Cache management ops for the key */
	struct ds_iv_class	*iv_class;
	/* key of the IV entry */
	struct ds_iv_key	iv_key;
	/* value of the IV entry */
	d_sg_list_t		iv_value;
	/* link to the namespace */
	d_list_t		iv_link;
	unsigned int		iv_ref;
	unsigned int		iv_valid:1,
				iv_to_delete:1;
};

/**
 * Pack(serialize) the ds_key into the iov_key, so it
 * can be used by cart IV rpc.
 *
 * \param class [IN]	ds_iv_class for packing.
 * \param iv_key [IN]	ds_iv key structure.
 * \param iov_key [OUT]	iov buf for the key.
 *
 * \return		0 if succeeds, error code otherwise.
 */
typedef int (*ds_iv_key_pack_t)(struct ds_iv_class *iv_class,
				struct ds_iv_key *iv_key,
				crt_iv_key_t *iov_key);

/**
 * Unpack(unserialize) the iov_key from CART IV req into the
 * ds_key.
 *
 * \param class [IN]	ds_iv_class for unpacking.
 * \param iov_key [IN]	iov buf for the key.
 * \param iv_key [OUT]	ds_iv key structure.
 *
 * \return		0 if succeeds, error code otherwise.
 */
typedef int (*ds_iv_key_unpack_t)(struct ds_iv_class *iv_class,
				  crt_iv_key_t *iov_key,
				  struct ds_iv_key *iv_key);

/**
 * Compare the key for the entry if there are multiple entry.
 *
 * \param key1 [IN]	key1 to compare.
 * \param key2 [IN]	key2 to compare.
 *
 * \return		true if equal, false if non-equal.
 */
typedef bool (*ds_iv_key_cmp_t)(void *key1, void *key2);

/**
 * Init class entry.
 *
 * \param iv_key [IN]	iv_key of the class to be init.
 * \param data [IN]	data to help allocate class entry.
 * \param entry [IN/OUT] class entry to be initialized.
 *
 * \return		0 if succeeds, error code otherwise.
 */
typedef int (*ds_iv_ent_init_t)(struct ds_iv_key *iv_key, void *data,
				struct ds_iv_entry *entry);

/**
 * Called from IV cart ivo_on_get callback.
 *
 * \param ent [IN]	iv class entry to get.
 * \param priv [IN]	private ptr from crt IV callback.
 *
 * \return		0 if succeeds, error code otherwise.
 */
typedef int (*ds_iv_ent_get_t)(struct ds_iv_entry *ent, void **priv);

/**
 * Called IV cart ivo_on_put callback.
 *
 * \param ent [IN]	iv class entry to get.
 * \param priv [IN]	private ptr from crt IV callback.
 *
 */
typedef void (*ds_iv_ent_put_t)(struct ds_iv_entry *ent, void *priv);

/**
 * Destroy the data attached to the entry.
 *
 * \param sgl [IN|OUT]	sgl to be destroyed.
 *
 * \return		0 if succeeds, error code otherwise.
 */
typedef int (*ds_iv_ent_destroy_t)(d_sg_list_t *sgl);

/**
 * Fetch data from the iv_class entry.
 *
 * \param entry [IN]	class entry.
 * \param key [IN]	key to locate the entry.
 * \param dst [OUT]	destination buffer.
 * \param priv [OUT]	private buffer from IV callback.
 *
 * \return		0 if succeeds, error code otherwise.
 */
typedef int (*ds_iv_ent_fetch_t)(struct ds_iv_entry *entry,
				 struct ds_iv_key *key,
				 d_sg_list_t *dst, void **priv);

/**
 * Update data to the iv_class entry.
 *
 * \param entry [IN]	class entry.
 * \param key [IN]	key to locate entry.
 * \param src [IN]	source update buffer.
 * \param priv [IN]	private buffer from IV callback.
 *
 * \return		0 if succeeds, error code otherwise.
 */
typedef int (*ds_iv_ent_update_t)(struct ds_iv_entry *entry,
				  struct ds_iv_key *key,
				  d_sg_list_t *src, void **priv);

/**
 * Refresh the data to the iv_class entry.
 *
 * \param entry[IN]	class entry
 * \param key [IN]	key to locate the entry.
 * \param src [IN]	source refresh buffer.
 * \param priv [IN]	private buffer from IV callback.
 *
 * \return		0 if succeeds, error code otherwise.
 */
typedef int (*ds_iv_ent_refresh_t)(struct ds_iv_entry *entry,
				   struct ds_iv_key *key,
				   d_sg_list_t *src,
				   int ref_rc, void **priv);

/**
 * allocate the value for cart IV.
 *
 * \param ent [IN]	entry to allocate iv_value.
 * \param key [IN]	key of the IV call.
 * \param src [OUT]	buffer to be allocated.
 *
 * \return		0 if succeeds, error code otherwise.
 */
typedef int (*ds_iv_value_alloc_t)(struct ds_iv_entry *ent,
				   struct ds_iv_key *key,
				   d_sg_list_t *sgl);

/**
 * Check whether the entry is valid
 *
 * \param ent [IN]	entry to be check
 * \param key [IN]	key to help checking
 *
 * \return		true if it is valid
 *                      false if it is not valid
 */
typedef bool (*ds_iv_ent_valid_t)(struct ds_iv_entry *ent,
				 struct ds_iv_key *key);

typedef int (*ds_iv_pre_sync_t)(struct ds_iv_entry *entry,
				struct ds_iv_key *key, d_sg_list_t *value);

struct ds_iv_class_ops {
	ds_iv_key_pack_t	ivc_key_pack;
	ds_iv_key_unpack_t	ivc_key_unpack;
	ds_iv_key_cmp_t		ivc_key_cmp;
	ds_iv_ent_init_t	ivc_ent_init;
	ds_iv_ent_get_t		ivc_ent_get;
	ds_iv_ent_put_t		ivc_ent_put;
	ds_iv_ent_destroy_t	ivc_ent_destroy;
	ds_iv_ent_fetch_t	ivc_ent_fetch;
	ds_iv_ent_update_t	ivc_ent_update;
	ds_iv_ent_refresh_t	ivc_ent_refresh;
	ds_iv_value_alloc_t	ivc_value_alloc;
	ds_iv_ent_valid_t	ivc_ent_valid;
	ds_iv_pre_sync_t	ivc_pre_sync;
};

extern struct crt_iv_ops iv_cache_ops;

int ds_iv_class_register(unsigned int class_id, struct crt_iv_ops *ops,
			 struct ds_iv_class_ops *class_ops);

int ds_iv_class_unregister(unsigned int class_id);

enum iv_key {
	IV_POOL_MAP = 1,
	IV_POOL_PROP,
	IV_POOL_CONN,
	IV_REBUILD,
	IV_OID,
	IV_CONT_SNAP,
	IV_CONT_CAPA,
	/* Container properties */
	IV_CONT_PROP,
	IV_POOL_HDL,
	/* Each server report its own EC aggregation epoch to the container
	 * service leader
	 */
	IV_CONT_AGG_EPOCH_REPORT,
	/* leader sync the minimum epoch(VOS aggregate epoch boundary) to all
	 * other servers
	 */
	IV_CONT_AGG_EPOCH_BOUNDRY,
};

int ds_iv_fetch(struct ds_iv_ns *ns, struct ds_iv_key *key, d_sg_list_t *value,
		bool retry);
int ds_iv_update(struct ds_iv_ns *ns, struct ds_iv_key *key,
		 d_sg_list_t *value, unsigned int shortcut,
		 unsigned int sync_mode, unsigned int sync_flags, bool retry);
int ds_iv_invalidate(struct ds_iv_ns *ns, struct ds_iv_key *key,
		     unsigned int shortcut, unsigned int sync_mode,
		     unsigned int sync_flags, bool retry);

int ds_iv_ns_create(crt_context_t ctx, uuid_t pool_uuid, crt_group_t *grp,
		    unsigned int *ns_id, struct ds_iv_ns **p_iv_ns);

void ds_iv_ns_update(struct ds_iv_ns *ns, unsigned int master_rank, uint64_t term);
void ds_iv_ns_stop(struct ds_iv_ns *ns);
void ds_iv_ns_leader_stop(struct ds_iv_ns *ns);
void ds_iv_ns_start(struct ds_iv_ns *ns);
void ds_iv_ns_put(struct ds_iv_ns *ns);

unsigned int ds_iv_ns_id_get(void *ns);

#endif /* __DAOS_SRV_IV_H__ */

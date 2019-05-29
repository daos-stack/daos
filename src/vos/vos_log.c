#include <daos_srv/vos.h>
#include <daos/btree.h>
#include "vos_internal.h"
#include "vos_layout.h"
#include "vos_log.h"

struct vos_log_root {
	/** Zero means multiple log entries are present */
	uint64_t	lr_epoch;
	/** If epoch != 0 then dtx entry
	 *  else offset to multientry log
	 */
	umem_off_t	lr_off;
};
D_CASSERT(sizeof(struct vos_log_root) == sizeof(struct vos_log_root_df));

struct vos_log_entry_df {
	daos_epoch_t	le_epoch;
	uint64_t	le_value;
};

/** Fit the log chunk in < 128 bytes */
#define VOS_LOG_CHUNK_SIZE	7

struct vos_log {
	umem_off_t		l_next;
	/** Log entries */
	struct vos_log_entry_df	l_log[];
};

int vos_log_upsert(struct vos_pool *pool, struct vos_log_entry_df *rootp,
		   daos_epoch_t epoch, uint64_t value)
{
	struct vos_log_root	*root = (struct vos_log_root *)rootp;

	if (root->lr_epoch == 0 && root->lr_value == 0) {
	}

}

int vos_log_iter_prepare(struct vos_pool *pool, struct vos_log_root_df *root,
			 struct vos_log_iter *iter);
int vos_log_iter_probe(struct vos_pool *pool, struct vos_log_iter *iter,
		       daos_epoch_t epoch, int opc);
int vos_log_iter_fetch(struct vos_pool *pool, struct vos_log_iter *iter,
		       struct vos_log_iter_entry *entry);
int vos_log_iter_next(struct vos_pool *pool, struct vos_log_iter *iter);
int vos_log_iter_prev(struct vos_pool *pool, struct vos_log_iter *iter);
int vos_log_iter_delete(struct vos_pool *pool, struct vos_log_iter *iter);
int vos_log_iter_fini(struct vos_pool *pool, struct vos_log_iter *iter);

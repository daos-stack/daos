#ifndef __DAOS_SYS_LMDB_H__
#define __DAOS_SYS_LMDB_H__

#include <daos_srv/daos_engine.h>

int lmm_db_init(const char *db_path);
int lmm_db_init_ex(const char *db_path, const char *db_name, bool force_create, bool destroy_db_on_fini);
void lmm_db_fini(void);
struct sys_db *lmm_db_get(void);

#endif /* __DAOS_SYS_LMDB_H__ */

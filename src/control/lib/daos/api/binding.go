package api

import "context"

/*
#cgo LDFLAGS: -ldaos -ldfs -lduns

#include <daos.h>
#include <daos/debug.h>
#include <daos_fs.h>
*/
import "C"

type (
	apiClient interface {
		daos_init() C.int
		daos_fini() C.int
		dfuse_open(path *C.char, oflag C.int) (C.int, error)
		dfuse_ioctl(fd C.int, reply *C.struct_dfuse_il_reply) (C.int, error)
		close_fd(fd C.int) C.int
		duns_resolve_path(path *C.char, dattr *C.struct_duns_attr_t) C.int
		daos_pool_connect(poolID *C.char, sys *C.char, flags C.uint, poolHdl *C.daos_handle_t, poolInfo *C.daos_pool_info_t, ev *C.struct_daos_event) C.int
		daos_pool_disconnect(poolHdl C.daos_handle_t) C.int
		daos_cont_create(poolHdl C.daos_handle_t, cUUID *C.uuid_t, props *C.daos_prop_t, ev *C.struct_daos_event) C.int
		dfs_cont_create(poolHdl C.daos_handle_t, cUUID *C.uuid_t, attr *C.dfs_attr_t, contHdl *C.daos_handle_t, dfs **C.dfs_t) C.int
		duns_create_path(poolHdl C.daos_handle_t, path *C.char, attr *C.struct_duns_attr_t) C.int
		daos_cont_destroy(poolHdl C.daos_handle_t, contID *C.char, force C.int, ev *C.struct_daos_event) C.int
		daos_cont_open(poolHdl C.daos_handle_t, contID *C.char, flags C.uint, contHdl *C.daos_handle_t, info *C.daos_cont_info_t, ev *C.struct_daos_event) C.int
		daos_cont_close(contHdl C.daos_handle_t, ev *C.struct_daos_event) C.int
		daos_cont_query(contHdl C.daos_handle_t, dci *C.daos_cont_info_t, props *C.daos_prop_t, ev *C.struct_daos_event) C.int
	}

	daosClientBinding struct {
		cancelCtx context.CancelFunc
	}
)

func (b *daosClientBinding) daos_init() C.int {
	return C.daos_init()
}

func (b *daosClientBinding) daos_fini() C.int {
	b.cancelCtx()
	return C.daos_fini()
}

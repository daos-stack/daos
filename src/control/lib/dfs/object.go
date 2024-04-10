package dfs

import "io/fs"

/*
#include <daos.h>
#include <daos_fs.h>
#include <daos_fs_sys.h>
*/
import "C"

type dfsObject struct {
	dfs  *C.struct_dfs_sys
	obj  *C.dfs_obj_t
	name string
}

func (o *dfsObject) Close() error {
	return dfsError(C.dfs_release(o.obj))
}

func (o *dfsObject) Stat() (fs.FileInfo, error) {
	var stat dfsStat_t
	var dfs *C.dfs_t

	if err := dfsError(C.dfs_sys2base(o.dfs, &dfs)); err != nil {
		return nil, err
	}

	if err := dfsError(C.dfs_ostat(dfs, o.obj, &stat.cst)); err != nil {
		return nil, err
	}
	stat.name = o.name
	stat.fillMode()

	return &stat, nil
}

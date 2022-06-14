//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"strconv"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#cgo CFLAGS: -I${SRCDIR}/../../../../utils
#cgo LDFLAGS: -ldaos -ldaos_common

#include <daos.h>

#include "daos_hdlr.h"

// typedefs to work around cgo's limitations
typedef const char const_char;
typedef char const *const * char_const_in_arr;
typedef void *const * void_const_out_arr;
typedef void const *const * void_const_in_arr;
typedef size_t const * size_t_in_arr;
typedef size_t * size_t_out_arr;
*/
import "C"

const (
	DefaultNumAttrs = 3
)

func main() {}

func getEnvNumVal(envKey string, defVal ...int) int {
	if len(defVal) == 0 {
		defVal = append(defVal, 0)
	}

	if strNumVal := os.Getenv(envKey); strNumVal != "" {
		numVal, err := strconv.Atoi(strNumVal)
		if err != nil {
			panic(err)
		}
		fmt.Printf("%s: %d\n", envKey, numVal)
		return numVal
	}
	return defVal[0]
}

func getEnvRc(envKey string) C.int {
	return -C.int(getEnvNumVal(envKey))
}

func fillCUuid(src uuid.UUID, dst *C.uuid_t) {
	for i, v := range src {
		dst[i] = C.uchar(v)
	}
}

func fillPoolContStr(src string, dst [128]C.char) {
	for i, v := range src {
		dst[i] = C.char(v)
	}
}

//export daos_init
func daos_init() C.int {
	return getEnvRc("DAOS_INIT_RC")
}

//export daos_fini
func daos_fini() C.int {
	return getEnvRc("DAOS_FINI_RC")
}

//export daos_debug_init
func daos_debug_init(*C.char) C.int {
	return getEnvRc("DAOS_DEBUG_INIT_RC")
}

//export daos_debug_fini
func daos_debug_fini() {}

//export daos_pool_connect2
func daos_pool_connect2(pool *C.const_char, sys *C.const_char, flags C.uint, poh *C.daos_handle_t, info *C.daos_pool_info_t, ev *C.daos_event_t) C.int {
	return getEnvRc("DAOS_POOL_CONNECT_RC")
}

//export daos_pool_disconnect
func daos_pool_disconnect(poh C.daos_handle_t, ev *C.daos_event_t) C.int {
	return getEnvRc("DAOS_POOL_DISCONNECT_RC")
}

//export daos_pool_query
func daos_pool_query(coh C.daos_handle_t, ranks **C.d_rank_list_t, info *C.daos_pool_info_t, props *C.daos_prop_t, ev *C.daos_event_t) C.int {
	return getEnvRc("DAOS_POOL_QUERY_RC")
}

//export pool_autotest_hdlr
func pool_autotest_hdlr(ap *C.struct_cmd_args_s) C.int {
	return getEnvRc("POOL_AUTOTEST_HDLR_RC")
}

//export duns_destroy_path
func duns_destroy_path(poh C.daos_handle_t, path *C.const_char) C.int {
	return getEnvRc("DUNS_DESTROY_PATH_RC")
}

//export fs_dfs_get_attr_hdlr
func fs_dfs_get_attr_hdlr(ap *C.struct_cmd_args_s, attrs *C.dfs_obj_info_t) C.int {
	return getEnvRc("FS_DFS_GET_ATTR_HDLR_RC")
}

//export fs_dfs_hdlr
func fs_dfs_hdlr(ap *C.struct_cmd_args_s) C.int {
	return getEnvRc("FS_DFS_HDLR_RC")
}

//export fs_dfs_resolve_pool
func fs_dfs_resolve_pool(ap *C.struct_cmd_args_s) C.int {
	rc := getEnvRc("FS_DFS_RESOLVE_POOL_RC")
	if rc != 0 {
		return rc
	}

	if ap == nil || ap.path == nil {
		return C.int(daos.InvalidInput)
	}

	fillCUuid(uuid.New(), &ap.p_uuid)

	return rc
}

//export fs_dfs_resolve_path
func fs_dfs_resolve_path(ap *C.struct_cmd_args_s) C.int {
	rc := getEnvRc("FS_DFS_RESOLVE_PATH_RC")
	if rc != 0 {
		return rc
	}

	if ap == nil || ap.path == nil {
		return C.int(daos.InvalidInput)
	}

	poolUUID := uuid.New()
	fillCUuid(poolUUID, &ap.p_uuid)
	for i, v := range poolUUID.String() {
		ap.pool_str[i] = C.char(v)
	}

	contUUID := uuid.New()
	fillCUuid(contUUID, &ap.c_uuid)
	for i, v := range contUUID.String() {
		ap.cont_str[i] = C.char(v)
	}

	return rc
}

//export cont_create_hdlr
func cont_create_hdlr(ap *C.struct_cmd_args_s) C.int {
	rc := getEnvRc("CONT_CREATE_HDLR_RC")
	if rc != 0 {
		return rc
	}

	if ap == nil {
		return C.int(daos.InvalidInput)
	}

	fillCUuid(uuid.New(), &ap.c_uuid)

	return rc
}

//export cont_create_uns_hdlr
func cont_create_uns_hdlr(ap *C.struct_cmd_args_s) C.int {
	rc := getEnvRc("CONT_CREATE_UNS_HDLR_RC")
	if rc != 0 {
		return rc
	}

	if ap == nil {
		return C.int(daos.InvalidInput)
	}

	fillCUuid(uuid.New(), &ap.c_uuid)

	return rc
}

//export daos_cont_open2
func daos_cont_open2(poh C.daos_handle_t, cont *C.const_char, flags C.uint, coh *C.daos_handle_t, info *C.daos_cont_info_t, ev *C.daos_event_t) C.int {
	return getEnvRc("DAOS_CONT_OPEN_RC")
}

//export daos_cont_close
func daos_cont_close(coh C.daos_handle_t, ev *C.daos_event_t) C.int {
	return getEnvRc("DAOS_CONT_CLOSE_RC")
}

//export daos_cont_query
func daos_cont_query(coh C.daos_handle_t, info *C.daos_cont_info_t, props *C.daos_prop_t, ev *C.daos_event_t) C.int {
	return getEnvRc("DAOS_CONT_QUERY_RC")
}

//export daos_cont_set_prop
func daos_cont_set_prop(coh C.daos_handle_t, props *C.daos_prop_t, ev *C.daos_event_t) C.int {
	return getEnvRc("DAOS_CONT_SET_PROP_RC")
}

//export daos_cont_get_acl
func daos_cont_get_acl(coh C.daos_handle_t, acl **C.daos_prop_t, ev *C.daos_event_t) C.int {
	rc := getEnvRc("DAOS_CONT_GET_ACL_RC")
	if rc != 0 {
		return rc
	}

	numProps := 3 // daos_cont_get_acl returns 3 props
	props := C.daos_prop_alloc(C.uint(3))
	if props == nil {
		return -C.int(daos.NoMemory)
	}

	props.dpp_nr = C.uint(numProps)
	entries := (*[1 << 30]C.struct_daos_prop_entry)(
		unsafe.Pointer(props.dpp_entries))[:numProps:numProps]

	for i := 0; i < numProps; i++ {
		switch i {
		case 0:
			entries[i].dpe_type = C.DAOS_PROP_CO_ACL
		case 1:
			entries[i].dpe_type = C.DAOS_PROP_CO_OWNER
		case 2:
			entries[i].dpe_type = C.DAOS_PROP_CO_OWNER_GROUP
		}
	}

	*acl = props

	return 0
}

//export daos_cont_destroy2
func daos_cont_destroy2(coh C.daos_handle_t, cont *C.const_char, force C.int, ev *C.daos_event_t) C.int {
	return getEnvRc("DAOS_CONT_DESTROY_RC")
}

type attrList map[string]string

func (al attrList) keys() []string {
	keys := make([]string, 0, len(al))
	for k := range al {
		keys = append(keys, k)
	}
	return keys
}

func genAttrList(envKey string) attrList {
	numAttrs := getEnvNumVal(envKey, DefaultNumAttrs)
	if numAttrs == 0 {
		return nil
	}

	attrs := make(attrList)
	for i := 0; i < numAttrs; i++ {
		attrs[fmt.Sprintf("test-%d", i)] = fmt.Sprintf("test-val-%d", i)
	}

	return attrs
}

func fillAttrList(envKey string, buf *C.char, size *C.size_t) C.int {
	if size == nil {
		return C.int(daos.InvalidInput)
	}

	attrs := genAttrList(envKey)
	totalSize := 0
	for key := range attrs {
		totalSize += len(key) + 1
	}

	if buf != nil {
		offset := 0
		bufSlice := (*[1 << 30]C.char)((unsafe.Pointer)(buf))[:totalSize:totalSize]
		for _, key := range attrs.keys() {
			for i, v := range key {
				bufSlice[offset+i] = C.char(v)
			}
			bufSlice[offset+len(key)] = 0
			offset += len(key) + 1
		}
	}

	*size = C.size_t(totalSize)

	return 0
}

func delAttrList(envKey string, n C.int, names C.char_const_in_arr) C.int {
	knownAttrs := genAttrList(envKey)
	if n == 0 && len(knownAttrs) == 0 {
		return 0
	}

	if names == nil {
		return C.int(daos.InvalidInput)
	}

	nameSlice := (*[1 << 30]*C.char)((unsafe.Pointer)(names))[:n:n]
	for i := 0; C.int(i) < n; i++ {
		name := C.GoString(nameSlice[i])
		_, found := knownAttrs[name]
		if !found {
			return C.int(daos.Nonexistent)
		}
	}

	return 0
}

func getAttrList(envKey string, n C.int, names C.char_const_in_arr, buffers C.void_const_out_arr, sizes C.size_t_out_arr) C.int {
	knownAttrs := genAttrList(envKey)
	if n == 0 && len(knownAttrs) == 0 {
		return 0
	}

	if names == nil || sizes == nil {
		return C.int(daos.InvalidInput)
	}

	vals := make([]string, int(n))
	nameSlice := (*[1 << 30]*C.char)((unsafe.Pointer)(names))[:n:n]
	sizeSlice := (*[1 << 30]C.size_t)((unsafe.Pointer)(sizes))[:n:n]
	for i := 0; C.int(i) < n; i++ {
		name := C.GoString(nameSlice[i])
		val, found := knownAttrs[name]
		if !found {
			return C.int(daos.Nonexistent)
		}
		vals[i] = val

		sizeSlice[i] = C.size_t(len(vals[i]) + 1)
	}

	if buffers != nil {
		bufSlice := (*[1 << 30]*C.void)((unsafe.Pointer)(buffers))[:n:n]
		for i := 0; C.int(i) < n; i++ {
			bufSlice[i] = (*C.void)(unsafe.Pointer(C.CString(vals[i])))
		}
	}

	return 0
}

//export daos_pool_list_attr
func daos_pool_list_attr(poh C.daos_handle_t, buf *C.char, size *C.size_t, ev *C.daos_event_t) C.int {
	rc := getEnvRc("DAOS_POOL_LIST_ATTR_RC")
	if rc != 0 {
		return rc
	}

	return fillAttrList("DAOS_POOL_NUM_ATTRS", buf, size)
}

//export daos_pool_get_attr
func daos_pool_get_attr(poh C.daos_handle_t, n C.int, names C.char_const_in_arr, buffers C.void_const_out_arr, sizes C.size_t_out_arr, ev *C.daos_event_t) C.int {
	rc := getEnvRc("DAOS_POOL_GET_ATTR_RC")
	if rc != 0 {
		return rc
	}

	return getAttrList("DAOS_POOL_NUM_ATTRS", n, names, buffers, sizes)
}

//export daos_pool_del_attr
func daos_pool_del_attr(poh C.daos_handle_t, n C.int, names C.char_const_in_arr, ev *C.daos_event_t) C.int {
	rc := getEnvRc("DAOS_POOL_DEL_ATTR_RC")
	if rc != 0 {
		return rc
	}

	return delAttrList("DAOS_POOL_NUM_ATTRS", n, names)
}

//export daos_pool_set_attr
func daos_pool_set_attr(poh C.daos_handle_t, n C.int, names C.char_const_in_arr, values C.void_const_in_arr, sizes C.size_t_in_arr, ev *C.daos_event_t) C.int {
	return getEnvRc("DAOS_POOL_SET_ATTR_RC")
}

//export daos_cont_list_attr
func daos_cont_list_attr(poh C.daos_handle_t, buf *C.char, size *C.size_t, ev *C.daos_event_t) C.int {
	rc := getEnvRc("DAOS_CONT_LIST_ATTR_RC")
	if rc != 0 {
		return rc
	}

	return fillAttrList("DAOS_CONT_NUM_ATTRS", buf, size)
}

//export daos_cont_get_attr
func daos_cont_get_attr(poh C.daos_handle_t, n C.int, names C.char_const_in_arr, buffers C.void_const_out_arr, sizes C.size_t_out_arr, ev *C.daos_event_t) C.int {
	rc := getEnvRc("DAOS_CONT_GET_ATTR_RC")
	if rc != 0 {
		return rc
	}

	return getAttrList("DAOS_CONT_NUM_ATTRS", n, names, buffers, sizes)
}

//export daos_cont_del_attr
func daos_cont_del_attr(poh C.daos_handle_t, n C.int, names C.char_const_in_arr, ev *C.daos_event_t) C.int {
	rc := getEnvRc("DAOS_CONT_DEL_ATTR_RC")
	if rc != 0 {
		return rc
	}

	return delAttrList("DAOS_CONT_NUM_ATTRS", n, names)
}

//export daos_cont_set_attr
func daos_cont_set_attr(poh C.daos_handle_t, n C.int, names C.char_const_in_arr, values C.void_const_in_arr, sizes C.size_t_in_arr, ev *C.daos_event_t) C.int {
	return getEnvRc("DAOS_CONT_SET_ATTR_RC")
}

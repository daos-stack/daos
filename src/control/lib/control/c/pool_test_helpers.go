//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

// cgo drivers for pool_test.go (see test_helpers.go for why these can't
// live in the test file).

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos_prop.h>
#include <daos_mgmt.h>
#include <daos_security.h>
#include <gurt/common.h>

#include <daos/control_types.h>

// Build a minimal ACL with one Allow entry for OWNER@ with the given perms.
// Returns a newly allocated struct daos_acl * (caller must daos_acl_free).
static struct daos_acl *
make_pool_test_acl(uint64_t allow_perms)
{
	struct daos_ace *ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	if (ace == NULL)
		return NULL;
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms  = allow_perms;

	struct daos_ace *aces[] = { ace };
	struct daos_acl *acl = daos_acl_create(aces, 1);
	daos_ace_free(ace);
	return acl;
}

static void
set_pool_prop_entry_acl(daos_prop_t *prop, uint32_t idx, struct daos_acl *acl)
{
	prop->dpp_entries[idx].dpe_type    = DAOS_PROP_PO_ACL;
	prop->dpp_entries[idx].dpe_val_ptr = acl;
}

static void
set_pool_prop_entry_str(daos_prop_t *prop, uint32_t idx, uint32_t type, const char *str)
{
	prop->dpp_entries[idx].dpe_type = type;
	D_STRNDUP(prop->dpp_entries[idx].dpe_str, str, DAOS_PROP_LABEL_MAX_LEN);
}

static void
set_pool_prop_entry_val(daos_prop_t *prop, uint32_t idx, uint32_t type, uint64_t val)
{
	prop->dpp_entries[idx].dpe_type = type;
	prop->dpp_entries[idx].dpe_val  = val;
}
*/
import "C"
import (
	"runtime/cgo"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

type poolCreateResult struct {
	rc       int
	poolUUID uuid.UUID
	svcRanks []uint32
}

func doPoolCreate(handle cgo.Handle, args C.struct_daos_control_pool_create_args) *poolCreateResult {
	var cUUIDOut C.uuid_t
	var svcOut *C.d_rank_list_t

	rc := int(daos_control_pool_create(C.uintptr_t(handle), &args, &svcOut, &cUUIDOut))
	res := &poolCreateResult{rc: rc}
	if rc == 0 {
		res.poolUUID = uuidFromC(&cUUIDOut)
	}
	if svcOut != nil {
		n := int(svcOut.rl_nr)
		if n > 0 {
			cRanks := unsafe.Slice(svcOut.rl_ranks, n)
			res.svcRanks = make([]uint32, n)
			for i, r := range cRanks {
				res.svcRanks[i] = uint32(r)
			}
		}
		C.d_rank_list_free(svcOut)
	}
	return res
}

func callPoolCreate(handle cgo.Handle, uid, gid uint32, scmSize, nvmeSize uint64, nsvc uint32) *poolCreateResult {
	return doPoolCreate(handle, C.struct_daos_control_pool_create_args{
		dcpa_uid:       C.uid_t(uid),
		dcpa_gid:       C.gid_t(gid),
		dcpa_scm_size:  C.daos_size_t(scmSize),
		dcpa_nvme_size: C.daos_size_t(nvmeSize),
		dcpa_nsvc:      C.uint32_t(nsvc),
	})
}

func callPoolCreateWithProp(handle cgo.Handle, uid, gid uint32, scmSize, nvmeSize uint64, nsvc uint32, entries []testPropEntry) *poolCreateResult {
	args := C.struct_daos_control_pool_create_args{
		dcpa_uid:       C.uid_t(uid),
		dcpa_gid:       C.gid_t(gid),
		dcpa_scm_size:  C.daos_size_t(scmSize),
		dcpa_nvme_size: C.daos_size_t(nvmeSize),
		dcpa_nsvc:      C.uint32_t(nsvc),
	}
	if entries != nil {
		args.dcpa_prop = buildCProp(entries)
		defer C.daos_prop_free(args.dcpa_prop)
	}
	return doPoolCreate(handle, args)
}

func callPoolCreateInvalidHandle() int {
	var poolUUID C.uuid_t
	args := C.struct_daos_control_pool_create_args{
		dcpa_uid:      C.uid_t(1000),
		dcpa_gid:      C.gid_t(1000),
		dcpa_scm_size: C.daos_size_t(1 << 30),
	}
	return int(daos_control_pool_create(C.uintptr_t(0), &args, nil, &poolUUID))
}

type validatePoolCreateArgsSpec struct {
	nilArgs      bool
	omitPoolUUID bool
	nonNilSvc    bool
	scmSize      uint64
	nvmeSize     uint64
}

func callValidatePoolCreateArgs(spec *validatePoolCreateArgsSpec) int {
	var poolUUID C.uuid_t
	if spec == nil || spec.nilArgs {
		return errorToRC(validatePoolCreateArgs(nil, nil, &poolUUID))
	}
	args := C.struct_daos_control_pool_create_args{
		dcpa_scm_size:  C.daos_size_t(spec.scmSize),
		dcpa_nvme_size: C.daos_size_t(spec.nvmeSize),
	}
	poolUUIDOut := &poolUUID
	if spec.omitPoolUUID {
		poolUUIDOut = nil
	}
	var svcOut *C.d_rank_list_t
	if spec.nonNilSvc {
		svcOut = C.d_rank_list_alloc(1)
		defer C.d_rank_list_free(svcOut)
	}
	return errorToRC(validatePoolCreateArgs(&args, &svcOut, poolUUIDOut))
}

func callPoolDestroy(handle cgo.Handle, poolUUID uuid.UUID, force bool) int {
	cu := cUUID(poolUUID)
	var fi C.int
	if force {
		fi = 1
	}
	return int(daos_control_pool_destroy(C.uintptr_t(handle), &cu, nil, fi))
}

func callPoolEvict(handle cgo.Handle, poolUUID uuid.UUID) int {
	cu := cUUID(poolUUID)
	return int(daos_control_pool_evict(C.uintptr_t(handle), &cu, nil))
}

func callPoolExclude(handle cgo.Handle, poolUUID uuid.UUID, rank uint32, tgtIdx int) int {
	cu := cUUID(poolUUID)
	return int(daos_control_pool_exclude(C.uintptr_t(handle), &cu, nil, C.d_rank_t(rank), C.int(tgtIdx)))
}

func callPoolDrain(handle cgo.Handle, poolUUID uuid.UUID, rank uint32, tgtIdx int) int {
	cu := cUUID(poolUUID)
	return int(daos_control_pool_drain(C.uintptr_t(handle), &cu, nil, C.d_rank_t(rank), C.int(tgtIdx)))
}

func callPoolReintegrate(handle cgo.Handle, poolUUID uuid.UUID, rank uint32, tgtIdx int) int {
	cu := cUUID(poolUUID)
	return int(daos_control_pool_reintegrate(C.uintptr_t(handle), &cu, nil, C.d_rank_t(rank), C.int(tgtIdx)))
}

func callPoolExtend(handle cgo.Handle, poolUUID uuid.UUID, ranks []uint32) int {
	cu := cUUID(poolUUID)
	if len(ranks) == 0 {
		return int(daos_control_pool_extend(C.uintptr_t(handle), &cu, nil, nil, 0))
	}
	cRanks := make([]C.d_rank_t, len(ranks))
	for i, r := range ranks {
		cRanks[i] = C.d_rank_t(r)
	}
	return int(daos_control_pool_extend(C.uintptr_t(handle), &cu, nil, &cRanks[0], C.int(len(ranks))))
}

func callPoolSetProp(handle cgo.Handle, label string, poolUUID uuid.UUID, name, value string) int {
	cu := cUUID(poolUUID)
	cLabel, lFree := cString(label)
	defer lFree()
	cName, nFree := cString(name)
	defer nFree()
	cValue, vFree := cString(value)
	defer vFree()
	return int(daos_control_pool_set_prop(C.uintptr_t(handle), cLabel, &cu, cName, cValue))
}

func callPoolGetProp(handle cgo.Handle, label string, poolUUID uuid.UUID, name string) (string, int) {
	cu := cUUID(poolUUID)
	cLabel, lFree := cString(label)
	defer lFree()
	cName, nFree := cString(name)
	defer nFree()
	var cValue *C.char
	rc := int(daos_control_pool_get_prop(C.uintptr_t(handle), cLabel, &cu, cName, &cValue))
	var value string
	if cValue != nil {
		value = C.GoString(cValue)
		C.free(unsafe.Pointer(cValue))
	}
	return value, rc
}

func callPoolUpdateACE(handle cgo.Handle, poolUUID uuid.UUID, ace string) int {
	cu := cUUID(poolUUID)
	lustreACE, f := cString(ace)
	defer f()
	return int(daos_control_pool_update_ace(C.uintptr_t(handle), &cu, nil, lustreACE))
}

func callPoolDeleteACE(handle cgo.Handle, poolUUID uuid.UUID, principal string) int {
	cu := cUUID(poolUUID)
	cP, f := cString(principal)
	defer f()
	return int(daos_control_pool_delete_ace(C.uintptr_t(handle), &cu, nil, cP))
}

func callPoolRebuildStop(handle cgo.Handle, poolUUID uuid.UUID, force bool) int {
	cu := cUUID(poolUUID)
	var fi C.int
	if force {
		fi = 1
	}
	return int(daos_control_pool_rebuild_stop(C.uintptr_t(handle), &cu, nil, fi))
}

func callPoolRebuildStart(handle cgo.Handle, poolUUID uuid.UUID) int {
	cu := cUUID(poolUUID)
	return int(daos_control_pool_rebuild_start(C.uintptr_t(handle), &cu, nil))
}

// testPoolInfo is a Go-native snapshot of one entry in the pool_list output.
type testPoolInfo struct {
	UUID  uuid.UUID
	Label string
}

// callPoolListCount runs pool_list in count-only mode (pools=NULL). inputCap
// is the caller-side *npools value before the call.
func callPoolListCount(handle cgo.Handle, inputCap uint64) (outCount uint64, rc int) {
	cCount := C.daos_size_t(inputCap)
	rc = int(daos_control_pool_list(C.uintptr_t(handle), nil, &cCount, nil))
	return uint64(cCount), rc
}

// callPoolList runs pool_list with a caller-allocated buffer of size cap. On
// success returns the populated entries. On BufTooSmall, entries is nil and
// outCount reports the required capacity.
func callPoolList(handle cgo.Handle, cap uint64) (entries []testPoolInfo, outCount uint64, rc int) {
	var cPools *C.daos_mgmt_pool_info_t
	var buf []C.daos_mgmt_pool_info_t
	if cap > 0 {
		buf = make([]C.daos_mgmt_pool_info_t, cap)
		cPools = &buf[0]
	}
	cCount := C.daos_size_t(cap)

	rc = int(daos_control_pool_list(C.uintptr_t(handle), nil, &cCount, cPools))
	outCount = uint64(cCount)

	if rc != 0 || cap == 0 {
		return nil, outCount, rc
	}

	n := outCount
	if n > cap {
		n = cap
	}
	entries = make([]testPoolInfo, n)
	for i := uint64(0); i < n; i++ {
		entries[i] = testPoolInfo{
			UUID:  uuidFromC(&buf[i].mgpi_uuid),
			Label: goCString(buf[i].mgpi_label),
		}
	}
	daos_control_pool_list_free(&buf[0], C.daos_size_t(outCount))
	return entries, outCount, rc
}

// callPoolListDoubleFree exercises the pool_list_free double-free guard.
// Populates a buffer via pool_list, then calls the free helper twice.
func callPoolListDoubleFree(handle cgo.Handle) int {
	buf := make([]C.daos_mgmt_pool_info_t, 1)
	cCount := C.daos_size_t(1)
	rc := int(daos_control_pool_list(C.uintptr_t(handle), nil, &cCount, &buf[0]))
	if rc != 0 {
		return rc
	}
	daos_control_pool_list_free(&buf[0], cCount)
	daos_control_pool_list_free(&buf[0], cCount)
	return 0
}

// callPoolCreateWithACL builds a pool-create prop containing a single
// DAOS_PROP_PO_ACL entry with one OWNER@ Allow ACE set to allowPerms, then
// calls pool create. Returns the poolCreateResult.
func callPoolCreateWithACL(handle cgo.Handle, uid, gid uint32, scmSize uint64, allowPerms uint64) *poolCreateResult {
	acl := C.make_pool_test_acl(C.uint64_t(allowPerms))
	if acl == nil {
		return &poolCreateResult{rc: int(daos.NoMemory)}
	}

	prop := C.daos_prop_alloc(1)
	// Transfer ACL ownership to the prop; daos_prop_free will call
	// daos_acl_free on dpe_val_ptr, so we must not free acl separately.
	C.set_pool_prop_entry_acl(prop, 0, acl)
	defer C.daos_prop_free(prop)

	return doPoolCreate(handle, C.struct_daos_control_pool_create_args{
		dcpa_uid:      C.uid_t(uid),
		dcpa_gid:      C.gid_t(gid),
		dcpa_scm_size: C.daos_size_t(scmSize),
		dcpa_prop:     prop,
		dcpa_nsvc:     1,
	})
}

// callPoolCreateWithOwnerProp builds a pool-create prop containing
// DAOS_PROP_PO_OWNER and DAOS_PROP_PO_OWNER_GROUP string entries and calls
// pool create.
func callPoolCreateWithOwnerProp(handle cgo.Handle, uid, gid uint32, scmSize uint64, owner, ownerGroup string) *poolCreateResult {
	prop := C.daos_prop_alloc(2)
	defer C.daos_prop_free(prop)

	cOwner := C.CString(owner)
	defer C.free(unsafe.Pointer(cOwner))
	C.set_pool_prop_entry_str(prop, 0, C.DAOS_PROP_PO_OWNER, cOwner)

	cGroup := C.CString(ownerGroup)
	defer C.free(unsafe.Pointer(cGroup))
	C.set_pool_prop_entry_str(prop, 1, C.DAOS_PROP_PO_OWNER_GROUP, cGroup)

	return doPoolCreate(handle, C.struct_daos_control_pool_create_args{
		dcpa_uid:      C.uid_t(uid),
		dcpa_gid:      C.gid_t(gid),
		dcpa_scm_size: C.daos_size_t(scmSize),
		dcpa_prop:     prop,
		dcpa_nsvc:     1,
	})
}

// callPoolCreateWithSvcListProp builds a prop containing DAOS_PROP_PO_SVC_LIST
// (a read-only type) and calls pool create. propsFromC must skip it silently.
func callPoolCreateWithSvcListProp(handle cgo.Handle, uid, gid uint32, scmSize uint64) *poolCreateResult {
	prop := C.daos_prop_alloc(1)
	defer C.daos_prop_free(prop)
	C.set_pool_prop_entry_val(prop, 0, C.DAOS_PROP_PO_SVC_LIST, 0)

	return doPoolCreate(handle, C.struct_daos_control_pool_create_args{
		dcpa_uid:      C.uid_t(uid),
		dcpa_gid:      C.gid_t(gid),
		dcpa_scm_size: C.daos_size_t(scmSize),
		dcpa_prop:     prop,
		dcpa_nsvc:     1,
	})
}

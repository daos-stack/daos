//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build !release

// Test shim: _test.go files in this package can't `import "C"` (Go disallows
// cgo in tests of a c-shared main package), so every cgo-touching test helper
// lives here.

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos_prop.h>
#include <daos_mgmt.h>
#include <gurt/common.h>

// cgo can't write into the dpe_type/dpe_val/dpe_str union directly.
static void
set_prop_entry_val(daos_prop_t *prop, uint32_t idx, uint32_t type, uint64_t val)
{
	prop->dpp_entries[idx].dpe_type = type;
	prop->dpp_entries[idx].dpe_val  = val;
}

static void
set_prop_entry_str(daos_prop_t *prop, uint32_t idx, uint32_t type, const char *str)
{
	prop->dpp_entries[idx].dpe_type = type;
	D_STRNDUP(prop->dpp_entries[idx].dpe_str, str, DAOS_PROP_LABEL_MAX_LEN);
}
*/
import "C"
import (
	"runtime/cgo"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
)

// makeTestHandle creates a cgo.Handle for a test context with the given mock invoker.
func makeTestHandle(mi control.UnaryInvoker, log *logging.LeveledLogger) cgo.Handle {
	return cgo.NewHandle(newTestContext(mi, log))
}

// testPropEntry describes one daos_prop_t entry for a test. Exactly one of
// NumVal or StrVal is used depending on the property's storage kind (see
// testPropNum / testPropStr).
type testPropEntry struct {
	Type   uint32
	NumVal uint64
	StrVal string
	isStr  bool
}

func testPropNum(propType uint32, val uint64) testPropEntry {
	return testPropEntry{Type: propType, NumVal: val}
}

func testPropStr(propType uint32, val string) testPropEntry {
	return testPropEntry{Type: propType, StrVal: val, isStr: true}
}

// Re-exported from cgo so _test.go files can refer to these constants.
const (
	testPropPoLabel      = uint32(C.DAOS_PROP_PO_LABEL)
	testPropPoOwner      = uint32(C.DAOS_PROP_PO_OWNER)
	testPropPoOwnerGroup = uint32(C.DAOS_PROP_PO_OWNER_GROUP)
	testPropPoRedunFac   = uint32(C.DAOS_PROP_PO_REDUN_FAC)
	testPropPoScrubMode  = uint32(C.DAOS_PROP_PO_SCRUB_MODE)
	testPropPoACL        = uint32(C.DAOS_PROP_PO_ACL)
	testPropPoSvcList    = uint32(C.DAOS_PROP_PO_SVC_LIST)
)

// buildCProp allocates a daos_prop_t from entries. Caller must C.daos_prop_free
// the result when done.
func buildCProp(entries []testPropEntry) *C.daos_prop_t {
	prop := C.daos_prop_alloc(C.uint32_t(len(entries)))
	for i, e := range entries {
		if e.isStr {
			cs := C.CString(e.StrVal)
			C.set_prop_entry_str(prop, C.uint32_t(i), C.uint32_t(e.Type), cs)
			C.free(unsafe.Pointer(cs))
		} else {
			C.set_prop_entry_val(prop, C.uint32_t(i), C.uint32_t(e.Type), C.uint64_t(e.NumVal))
		}
	}
	return prop
}

// testPropsFromC drives propsFromC end-to-end: builds a C prop from entries,
// converts it back through the library, and frees the C allocation.
// Passing nilInput=true runs propsFromC(nil).
func testPropsFromC(entries []testPropEntry, nilInput bool) ([]*daos.PoolProperty, error) {
	var cProp *C.daos_prop_t
	if !nilInput {
		cProp = buildCProp(entries)
		defer C.daos_prop_free(cProp)
	}
	return propsFromC(cProp)
}

// cUUID builds a C uuid_t from a Go UUID. Returns the value; callers take its
// address via &... when passing to a function that wants *C.uuid_t.
func cUUID(u uuid.UUID) C.uuid_t {
	var out C.uuid_t
	copyUUIDToC(u, &out)
	return out
}

// cString wraps C.CString with a NULL-on-empty convention and a small closure
// for deferred freeing.
func cString(s string) (*C.char, func()) {
	if s == "" {
		return nil, func() {}
	}
	cs := C.CString(s)
	return cs, func() { C.free(unsafe.Pointer(cs)) }
}

// testCopyStringToCharArray exercises copyStringToCharArray with a Go input
// and the given buffer size, returning the resulting Go string.
func testCopyStringToCharArray(s string, bufSize int) string {
	if bufSize <= 0 {
		copyStringToCharArray(s, nil, bufSize)
		return ""
	}
	buf := make([]C.char, bufSize)
	copyStringToCharArray(s, &buf[0], bufSize)
	return goCString(&buf[0])
}

// testCopyStringToNil exercises the nil-dest fast path.
func testCopyStringToNil() {
	copyStringToCharArray("hello", nil, 10)
}

// goCString reads a null-terminated C string pointer into a Go string.
func goCString(p *C.char) string {
	if p == nil {
		return ""
	}
	return C.GoString(p)
}

// testRankListRoundTrip builds a C rank list from the Go input, converts it
// back via rankListFromC, and returns the resulting slice.
func testRankListRoundTrip(ranks []uint32) []uint32 {
	crl := C.d_rank_list_alloc(C.uint32_t(len(ranks)))
	defer C.d_rank_list_free(crl)
	if len(ranks) > 0 {
		cRanks := unsafe.Slice(crl.rl_ranks, len(ranks))
		for i, r := range ranks {
			cRanks[i] = C.d_rank_t(r)
		}
	}
	got := rankListFromC(crl)
	out := make([]uint32, len(got))
	for i, r := range got {
		out[i] = uint32(r)
	}
	return out
}

// testCopyRankListTo exercises copyRankListToC with the given input and
// destination capacity; returns the ranks written and rl_nr after the call.
func testCopyRankListTo(input []uint32, outCap int) (got []uint32, rlNr uint32) {
	if outCap <= 0 {
		return nil, 0
	}
	dst := C.d_rank_list_alloc(C.uint32_t(outCap))
	defer C.d_rank_list_free(dst)

	goRanks := make([]ranklist.Rank, len(input))
	for i, r := range input {
		goRanks[i] = ranklist.Rank(r)
	}
	copyRankListToC(goRanks, dst, outCap)

	rlNr = uint32(dst.rl_nr)
	got = make([]uint32, rlNr)
	if rlNr > 0 {
		cRanks := unsafe.Slice(dst.rl_ranks, rlNr)
		for i, r := range cRanks {
			got[i] = uint32(r)
		}
	}
	return got, rlNr
}

func callInit(configFile, logFile, logLevel string) (cgo.Handle, int) {
	cfg, cfgFree := cString(configFile)
	defer cfgFree()
	lf, lfFree := cString(logFile)
	defer lfFree()
	ll, llFree := cString(logLevel)
	defer llFree()

	args := C.struct_daos_control_init_args{
		config_file: cfg,
		log_file:    lf,
		log_level:   ll,
	}
	var handle C.uintptr_t
	rc := int(daos_control_init(&args, &handle))
	return cgo.Handle(handle), rc
}

func callInitNilHandleOut() int {
	var args C.struct_daos_control_init_args
	return int(daos_control_init(&args, nil))
}

func callFini(handle cgo.Handle) { daos_control_fini(C.uintptr_t(handle)) }

type poolCreateResult struct {
	rc       int
	poolUUID uuid.UUID
	svcRanks []uint32
}

func doPoolCreate(handle cgo.Handle, args C.struct_daos_control_pool_create_args) *poolCreateResult {
	var cUUIDOut C.uuid_t
	args.pool_uuid = &cUUIDOut

	rc := int(daos_control_pool_create(C.uintptr_t(handle), &args))
	res := &poolCreateResult{rc: rc}
	if rc == 0 {
		res.poolUUID = uuidFromC(&cUUIDOut)
	}
	if args.svc != nil {
		n := int(args.svc.rl_nr)
		if n > 0 {
			cRanks := unsafe.Slice(args.svc.rl_ranks, n)
			res.svcRanks = make([]uint32, n)
			for i, r := range cRanks {
				res.svcRanks[i] = uint32(r)
			}
		}
		C.d_rank_list_free(args.svc)
	}
	return res
}

func callPoolCreate(handle cgo.Handle, uid, gid uint32, scmSize, nvmeSize uint64, nsvc uint32) *poolCreateResult {
	return doPoolCreate(handle, C.struct_daos_control_pool_create_args{
		uid:       C.uid_t(uid),
		gid:       C.gid_t(gid),
		scm_size:  C.daos_size_t(scmSize),
		nvme_size: C.daos_size_t(nvmeSize),
		nsvc:      C.uint32_t(nsvc),
	})
}

func callPoolCreateWithProp(handle cgo.Handle, uid, gid uint32, scmSize, nvmeSize uint64, nsvc uint32, entries []testPropEntry) *poolCreateResult {
	args := C.struct_daos_control_pool_create_args{
		uid:       C.uid_t(uid),
		gid:       C.gid_t(gid),
		scm_size:  C.daos_size_t(scmSize),
		nvme_size: C.daos_size_t(nvmeSize),
		nsvc:      C.uint32_t(nsvc),
	}
	if entries != nil {
		args.prop = buildCProp(entries)
		defer C.daos_prop_free(args.prop)
	}
	return doPoolCreate(handle, args)
}

func callPoolCreateInvalidHandle() int {
	var poolUUID C.uuid_t
	args := C.struct_daos_control_pool_create_args{
		uid:       C.uid_t(1000),
		gid:       C.gid_t(1000),
		scm_size:  C.daos_size_t(1 << 30),
		pool_uuid: &poolUUID,
	}
	return int(daos_control_pool_create(C.uintptr_t(0), &args))
}

type validatePoolCreateArgsSpec struct {
	nilArgs      bool
	omitPoolUUID bool
	nonNilSvc    bool
	scmSize      uint64
	nvmeSize     uint64
}

func callValidatePoolCreateArgs(spec *validatePoolCreateArgsSpec) int {
	if spec == nil || spec.nilArgs {
		return errorToRC(validatePoolCreateArgs(nil))
	}
	args := C.struct_daos_control_pool_create_args{
		scm_size:  C.daos_size_t(spec.scmSize),
		nvme_size: C.daos_size_t(spec.nvmeSize),
	}
	var poolUUID C.uuid_t
	if !spec.omitPoolUUID {
		args.pool_uuid = &poolUUID
	}
	var cSvc *C.d_rank_list_t
	if spec.nonNilSvc {
		cSvc = C.d_rank_list_alloc(1)
		defer C.d_rank_list_free(cSvc)
		args.svc = cSvc
	}
	return errorToRC(validatePoolCreateArgs(&args))
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

func callCheckSwitch(handle cgo.Handle, enable bool) int {
	var e C.int
	if enable {
		e = 1
	}
	return int(daos_control_check_switch(C.uintptr_t(handle), e))
}

func uuidArrayToC(uuids []uuid.UUID) []C.uuid_t {
	if len(uuids) == 0 {
		return nil
	}
	out := make([]C.uuid_t, len(uuids))
	for i, u := range uuids {
		copyUUIDToC(u, &out[i])
	}
	return out
}

func callCheckStart(handle cgo.Handle, flags uint32, poolUUIDs []uuid.UUID, policies string) int {
	arr := uuidArrayToC(poolUUIDs)
	var uuids *C.uuid_t
	if len(arr) > 0 {
		uuids = &arr[0]
	}
	cPol, f := cString(policies)
	defer f()
	return int(daos_control_check_start(C.uintptr_t(handle), C.uint32_t(flags), C.uint32_t(len(poolUUIDs)), uuids, cPol))
}

func callCheckStop(handle cgo.Handle, poolUUIDs []uuid.UUID) int {
	arr := uuidArrayToC(poolUUIDs)
	var uuids *C.uuid_t
	if len(arr) > 0 {
		uuids = &arr[0]
	}
	return int(daos_control_check_stop(C.uintptr_t(handle), C.uint32_t(len(poolUUIDs)), uuids))
}

func callCheckQuery(handle cgo.Handle, poolUUIDs []uuid.UUID) int {
	arr := uuidArrayToC(poolUUIDs)
	var uuids *C.uuid_t
	if len(arr) > 0 {
		uuids = &arr[0]
	}
	return int(daos_control_check_query(C.uintptr_t(handle), C.uint32_t(len(poolUUIDs)), uuids, nil))
}

func callCheckRepair(handle cgo.Handle, seq uint64, action uint32) int {
	return int(daos_control_check_repair(C.uintptr_t(handle), C.uint64_t(seq), C.uint32_t(action)))
}

func callCheckSetPolicy(handle cgo.Handle, flags uint32, policies string) int {
	cPol, f := cString(policies)
	defer f()
	return int(daos_control_check_set_policy(C.uintptr_t(handle), C.uint32_t(flags), cPol))
}

type testCheckPoolInfo struct {
	UUID   uuid.UUID
	Status string
	Phase  string
}

type testCheckReportInfo struct {
	UUID    uuid.UUID
	Seq     uint64
	Class   uint32
	Action  uint32
	Result  int
	Options []int
}

type testCheckInfo struct {
	Status         string
	Phase          string
	Pools          []testCheckPoolInfo
	Reports        []testCheckReportInfo
	PostFreeZeroed bool
}

// callCheckQueryWithInfo populates a daos_check_info, snapshots it into Go
// values, frees the C allocation, and asserts (via PostFreeZeroed) that the
// free zeroed out the struct's pointers and counters.
func callCheckQueryWithInfo(handle cgo.Handle, poolUUIDs []uuid.UUID) (testCheckInfo, int) {
	arr := uuidArrayToC(poolUUIDs)
	var uuids *C.uuid_t
	if len(arr) > 0 {
		uuids = &arr[0]
	}

	var dci C.struct_daos_check_info
	rc := int(daos_control_check_query(C.uintptr_t(handle), C.uint32_t(len(poolUUIDs)), uuids, &dci))

	info := testCheckInfo{
		Status: goCString(dci.dci_status),
		Phase:  goCString(dci.dci_phase),
	}

	if dci.dci_pools != nil && dci.dci_pool_nr > 0 {
		pools := unsafe.Slice(dci.dci_pools, int(dci.dci_pool_nr))
		info.Pools = make([]testCheckPoolInfo, len(pools))
		for i := range pools {
			info.Pools[i] = testCheckPoolInfo{
				UUID:   uuidFromC(&pools[i].dcpi_uuid),
				Status: goCString(pools[i].dcpi_status),
				Phase:  goCString(pools[i].dcpi_phase),
			}
		}
	}
	if dci.dci_reports != nil && dci.dci_report_nr > 0 {
		reports := unsafe.Slice(dci.dci_reports, int(dci.dci_report_nr))
		info.Reports = make([]testCheckReportInfo, len(reports))
		for i := range reports {
			r := testCheckReportInfo{
				UUID:   uuidFromC(&reports[i].dcri_uuid),
				Seq:    uint64(reports[i].dcri_seq),
				Class:  uint32(reports[i].dcri_class),
				Action: uint32(reports[i].dcri_act),
				Result: int(reports[i].dcri_result),
			}
			nOpts := int(reports[i].dcri_option_nr)
			r.Options = make([]int, nOpts)
			for j := 0; j < nOpts; j++ {
				r.Options[j] = int(reports[i].dcri_options[j])
			}
			info.Reports[i] = r
		}
	}

	// Free once, assert post-free zeroed, free again (must be a no-op).
	daos_control_check_info_free(&dci)
	info.PostFreeZeroed = dci.dci_pool_nr == 0 && dci.dci_report_nr == 0 &&
		dci.dci_pools == nil && dci.dci_reports == nil &&
		dci.dci_status == nil && dci.dci_phase == nil
	daos_control_check_info_free(&dci)

	return info, rc
}

// callCheckInfoFreeNil exercises the nil-safe path of the free helper.
func callCheckInfoFreeNil() { daos_control_check_info_free(nil) }

func callServerSetLogmasks(handle cgo.Handle, host, masks, streams, subsystems string) int {
	cHost, hF := cString(host)
	defer hF()
	cMasks, mF := cString(masks)
	defer mF()
	cStreams, strF := cString(streams)
	defer strF()
	cSubs, sF := cString(subsystems)
	defer sF()
	return int(daos_control_server_set_logmasks(C.uintptr_t(handle), cHost, cMasks, cStreams, cSubs))
}

func callStorageDeviceList(handle cgo.Handle, host string) (ndisks int, rc int) {
	cHost, f := cString(host)
	defer f()
	var n C.int
	rc = int(daos_control_storage_device_list(C.uintptr_t(handle), cHost, &n, nil))
	return int(n), rc
}

// callStorageDeviceListCount runs device_list in count-only mode (devices=NULL)
// with the caller-side *ndisks set to inputCount on entry, returning the value
// the library writes back.
func callStorageDeviceListCount(handle cgo.Handle, host string, inputCount int) (ndisks int, rc int) {
	cHost, f := cString(host)
	defer f()
	n := C.int(inputCount)
	rc = int(daos_control_storage_device_list(C.uintptr_t(handle), cHost, &n, nil))
	return int(n), rc
}

type testDeviceInfo struct {
	UUID    uuid.UUID
	State   string
	Rank    uint32
	Host    string
	Targets []int32
}

func callStorageDeviceListPopulated(handle cgo.Handle, host string, cap int) ([]testDeviceInfo, int, int) {
	cHost, f := cString(host)
	defer f()

	devices := make([]C.struct_device_list, cap)
	n := C.int(cap)
	var devPtr *C.struct_device_list
	if cap > 0 {
		devPtr = &devices[0]
	}
	rc := int(daos_control_storage_device_list(C.uintptr_t(handle), cHost, &n, devPtr))
	if rc != 0 {
		return nil, int(n), rc
	}
	got := int(n)
	if got > cap {
		got = cap
	}
	out := make([]testDeviceInfo, got)
	for i := 0; i < got; i++ {
		tgtCount := int(devices[i].n_tgtidx)
		out[i] = testDeviceInfo{
			UUID:    uuidFromC(&devices[i].device_id),
			State:   C.GoString(&devices[i].state[0]),
			Rank:    uint32(devices[i].rank),
			Host:    C.GoString(&devices[i].host[0]),
			Targets: make([]int32, tgtCount),
		}
		for j := 0; j < tgtCount; j++ {
			out[i].Targets[j] = int32(devices[i].tgtidx[j])
		}
	}
	return out, int(n), rc
}

func callStorageSetNVMeFault(handle cgo.Handle, host string, devUUID uuid.UUID) int {
	cHost, f := cString(host)
	defer f()
	cu := cUUID(devUUID)
	return int(daos_control_storage_set_nvme_fault(C.uintptr_t(handle), cHost, &cu))
}

func callStorageQueryDeviceHealth(handle cgo.Handle, host, statsKey string, devUUID uuid.UUID) (string, int) {
	return callStorageQueryDeviceHealthSized(handle, host, statsKey, devUUID, 256)
}

func callStorageQueryDeviceHealthSized(handle cgo.Handle, host, statsKey string, devUUID uuid.UUID, bufSize int) (string, int) {
	cHost, hF := cString(host)
	defer hF()
	cKey := C.CString(statsKey)
	defer C.free(unsafe.Pointer(cKey))
	cu := cUUID(devUUID)

	out := make([]C.char, bufSize)
	rc := int(daos_control_storage_query_device_health(C.uintptr_t(handle), cHost, cKey, &out[0], C.int(bufSize), &cu))

	var result []byte
	for _, c := range out {
		if c == 0 {
			break
		}
		result = append(result, byte(c))
	}
	return string(result), rc
}

func callSystemStopRank(handle cgo.Handle, rank uint32, force bool) int {
	var fi C.int
	if force {
		fi = 1
	}
	return int(daos_control_system_stop_rank(C.uintptr_t(handle), C.d_rank_t(rank), fi))
}

func callSystemStartRank(handle cgo.Handle, rank uint32) int {
	return int(daos_control_system_start_rank(C.uintptr_t(handle), C.d_rank_t(rank)))
}

func callSystemReintRank(handle cgo.Handle, rank uint32) int {
	return int(daos_control_system_reint_rank(C.uintptr_t(handle), C.d_rank_t(rank)))
}

func callSystemExcludeRank(handle cgo.Handle, rank uint32) int {
	return int(daos_control_system_exclude_rank(C.uintptr_t(handle), C.d_rank_t(rank)))
}

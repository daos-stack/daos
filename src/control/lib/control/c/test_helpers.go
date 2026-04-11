//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build !release

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos_prop.h>
#include <daos_mgmt.h>
#include <gurt/common.h>

// Helper to allocate a rank list for testing
static d_rank_list_t *alloc_rank_list(uint32_t nr) {
	return d_rank_list_alloc(nr);
}

// Helper to free a rank list
static void free_rank_list(d_rank_list_t *rl) {
	d_rank_list_free(rl);
}

// Helper to set a rank in a rank list
static void set_rank(d_rank_list_t *rl, uint32_t idx, d_rank_t rank) {
	if (rl != NULL && rl->rl_ranks != NULL && idx < rl->rl_nr) {
		rl->rl_ranks[idx] = rank;
	}
}

// Helper to get a rank from a rank list
static d_rank_t get_rank(d_rank_list_t *rl, uint32_t idx) {
	if (rl != NULL && rl->rl_ranks != NULL && idx < rl->rl_nr) {
		return rl->rl_ranks[idx];
	}
	return 0;
}
*/
import "C"
import (
	"runtime/cgo"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
)

// testRankList wraps a C rank list for testing.
type testRankList struct {
	crl *C.d_rank_list_t
}

// newTestRankList allocates a rank list with the given capacity.
func newTestRankList(capacity uint32) *testRankList {
	return &testRankList{crl: C.alloc_rank_list(C.uint32_t(capacity))}
}

// free releases the rank list memory.
func (t *testRankList) free() {
	if t.crl != nil {
		C.free_rank_list(t.crl)
		t.crl = nil
	}
}

// setRank sets a rank at the given index.
func (t *testRankList) setRank(idx, rank uint32) {
	C.set_rank(t.crl, C.uint32_t(idx), C.d_rank_t(rank))
}

// getRank gets a rank at the given index.
func (t *testRankList) getRank(idx uint32) uint32 {
	return uint32(C.get_rank(t.crl, C.uint32_t(idx)))
}

// nr returns the number of ranks.
func (t *testRankList) nr() uint32 {
	if t.crl == nil {
		return 0
	}
	return uint32(t.crl.rl_nr)
}

// ptr returns the underlying C pointer for passing to functions.
func (t *testRankList) ptr() *C.d_rank_list_t {
	return t.crl
}

// testUUID wraps a C uuid_t for testing.
type testUUID struct {
	cuuid C.uuid_t
}

// newTestUUID creates a new test UUID wrapper.
func newTestUUID() *testUUID {
	return &testUUID{}
}

// set copies a Go UUID into the C uuid.
func (t *testUUID) set(u uuid.UUID) {
	copyUUIDToC(u, &t.cuuid)
}

// get returns the UUID as a Go uuid.UUID.
func (t *testUUID) get() uuid.UUID {
	return uuidFromC(&t.cuuid)
}

// ptr returns a pointer to the C uuid for passing to functions.
func (t *testUUID) ptr() *C.uuid_t {
	return &t.cuuid
}

// testCString wraps a C string for testing.
type testCString struct {
	cstr *C.char
}

// newTestCString creates a C string from a Go string.
func newTestCString(s string) *testCString {
	if s == "" {
		return &testCString{cstr: nil}
	}
	return &testCString{cstr: C.CString(s)}
}

// free releases the C string memory.
func (t *testCString) free() {
	if t.cstr != nil {
		C.free(unsafe.Pointer(t.cstr))
		t.cstr = nil
	}
}

// ptr returns the C string pointer.
func (t *testCString) ptr() *C.char {
	return t.cstr
}

// toGo converts to Go string using the library's goString function.
func (t *testCString) toGo() string {
	return goString(t.cstr)
}

// callPoolCreate is a test helper that calls daos_control_pool_create
// with the given parameters and returns the result.
func callPoolCreate(
	handle cgo.Handle,
	uid, gid uint32,
	scmSize, nvmeSize uint64,
	svc *testRankList,
	poolUUID *testUUID,
) int {
	return int(daos_control_pool_create(
		C.uintptr_t(handle),
		C.uid_t(uid),
		C.gid_t(gid),
		nil, // grp
		nil, // tgts
		C.daos_size_t(scmSize),
		C.daos_size_t(nvmeSize),
		nil, // prop
		svc.ptr(),
		poolUUID.ptr(),
	))
}

// callPoolCreateInvalidHandle calls pool create with a zero handle.
func callPoolCreateInvalidHandle(poolUUID *testUUID) int {
	return int(daos_control_pool_create(
		C.uintptr_t(0),
		C.uid_t(1000),
		C.gid_t(1000),
		nil,
		nil,
		C.daos_size_t(1<<30),
		C.daos_size_t(0),
		nil,
		nil,
		poolUUID.ptr(),
	))
}

// testConvertRankListFromC tests the rankListFromC conversion.
func testConvertRankListFromC(rl *testRankList) []uint32 {
	if rl == nil {
		return nil
	}
	ranks := rankListFromC(rl.ptr())
	result := make([]uint32, len(ranks))
	for i, r := range ranks {
		result[i] = uint32(r)
	}
	return result
}

// testConvertRankListToC tests the copyRankListToC conversion.
// It populates the source rank list with the given ranks, converts to Go,
// then copies back to the destination C rank list.
func testConvertRankListToC(ranks []uint32, rl *testRankList) {
	if rl == nil || len(ranks) == 0 {
		return
	}
	// Create a source rank list with the input ranks
	from := newTestRankList(uint32(len(ranks)))
	defer from.free()
	for i, r := range ranks {
		from.setRank(uint32(i), r)
	}
	// Convert to Go slice of ranklist.Rank
	goRanks := rankListFromC(from.ptr())
	// Copy back to the destination C rank list
	copyRankListToC(goRanks, rl.ptr())
}

// makeTestHandle creates a cgo.Handle for a test context with the given mock invoker.
func makeTestHandle(mi control.UnaryInvoker, log *logging.LeveledLogger) cgo.Handle {
	ctx := newTestContext(mi, log)
	return cgo.NewHandle(ctx)
}

// callPoolDestroy is a test helper for daos_control_pool_destroy.
func callPoolDestroy(handle cgo.Handle, poolUUID *testUUID, force bool) int {
	var forceInt C.int
	if force {
		forceInt = 1
	}
	return int(daos_control_pool_destroy(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		nil, // grp
		forceInt,
	))
}

// callPoolEvict is a test helper for daos_control_pool_evict.
func callPoolEvict(handle cgo.Handle, poolUUID *testUUID) int {
	return int(daos_control_pool_evict(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		nil, // grp
	))
}

// callPoolExclude is a test helper for daos_control_pool_exclude.
func callPoolExclude(handle cgo.Handle, poolUUID *testUUID, rank uint32, tgtIdx int) int {
	return int(daos_control_pool_exclude(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		nil, // grp
		C.d_rank_t(rank),
		C.int(tgtIdx),
	))
}

// callPoolDrain is a test helper for daos_control_pool_drain.
func callPoolDrain(handle cgo.Handle, poolUUID *testUUID, rank uint32, tgtIdx int) int {
	return int(daos_control_pool_drain(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		nil, // grp
		C.d_rank_t(rank),
		C.int(tgtIdx),
	))
}

// callPoolReintegrate is a test helper for daos_control_pool_reintegrate.
func callPoolReintegrate(handle cgo.Handle, poolUUID *testUUID, rank uint32, tgtIdx int) int {
	return int(daos_control_pool_reintegrate(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		nil, // grp
		C.d_rank_t(rank),
		C.int(tgtIdx),
	))
}

// callSystemStopRank is a test helper for daos_control_system_stop_rank.
func callSystemStopRank(handle cgo.Handle, rank uint32, force bool) int {
	var forceInt C.int
	if force {
		forceInt = 1
	}
	return int(daos_control_system_stop_rank(
		C.uintptr_t(handle),
		C.d_rank_t(rank),
		forceInt,
	))
}

// callSystemStartRank is a test helper for daos_control_system_start_rank.
func callSystemStartRank(handle cgo.Handle, rank uint32) int {
	return int(daos_control_system_start_rank(
		C.uintptr_t(handle),
		C.d_rank_t(rank),
	))
}

// testCharArray wraps a C char array for testing copyStringToCharArray.
type testCharArray struct {
	data []C.char
}

// newTestCharArray creates a char array of the given size.
func newTestCharArray(size int) *testCharArray {
	return &testCharArray{data: make([]C.char, size)}
}

// ptr returns a pointer to the first element.
func (t *testCharArray) ptr() *C.char {
	if len(t.data) == 0 {
		return nil
	}
	return &t.data[0]
}

// toString converts the char array to a Go string (up to null terminator).
func (t *testCharArray) toString() string {
	var result []byte
	for _, c := range t.data {
		if c == 0 {
			break
		}
		result = append(result, byte(c))
	}
	return string(result)
}

// testCopyStringToCharArray tests the copyStringToCharArray helper.
func testCopyStringToCharArray(s string, dest *testCharArray, maxLen int) {
	if dest == nil {
		return
	}
	copyStringToCharArray(s, dest.ptr(), maxLen)
}

// testParseUUID tests UUID parsing using google/uuid.
func testParseUUID(s string) ([16]byte, error) {
	return uuid.Parse(s)
}

// testErrorToRC tests the errorToRC function.
func testErrorToRC(err error) int {
	return errorToRC(err)
}

// callInit is a test helper for daos_control_init with default (insecure) config.
func callInit(configFile, logFile, logLevel string) (cgo.Handle, int) {
	var args C.struct_daos_control_init_args

	if configFile != "" {
		args.config_file = C.CString(configFile)
		defer C.free(unsafe.Pointer(args.config_file))
	}
	if logFile != "" {
		args.log_file = C.CString(logFile)
		defer C.free(unsafe.Pointer(args.log_file))
	}
	if logLevel != "" {
		args.log_level = C.CString(logLevel)
		defer C.free(unsafe.Pointer(args.log_level))
	}

	var handle C.uintptr_t
	rc := int(daos_control_init(&args, &handle))
	return cgo.Handle(handle), rc
}

// callInitNilHandleOut tests daos_control_init with a nil handleOut.
func callInitNilHandleOut() int {
	var args C.struct_daos_control_init_args
	return int(daos_control_init(&args, nil))
}

// callFini is a test helper for daos_control_fini.
func callFini(handle cgo.Handle) {
	daos_control_fini(C.uintptr_t(handle))
}

// callCheckSwitch is a test helper for daos_control_check_switch.
func callCheckSwitch(handle cgo.Handle, enable bool) int {
	var enableInt C.int
	if enable {
		enableInt = 1
	}
	return int(daos_control_check_switch(C.uintptr_t(handle), enableInt))
}

// callCheckStart is a test helper for daos_control_check_start.
func callCheckStart(handle cgo.Handle, flags uint32, poolUUIDs []uuid.UUID, policies string) int {
	var uuids *C.uuid_t
	var cPolicies *C.char

	if len(poolUUIDs) > 0 {
		uuidArray := make([]C.uuid_t, len(poolUUIDs))
		for i, u := range poolUUIDs {
			copyUUIDToC(u, &uuidArray[i])
		}
		uuids = &uuidArray[0]
	}

	if policies != "" {
		cPolicies = C.CString(policies)
		defer C.free(unsafe.Pointer(cPolicies))
	}

	return int(daos_control_check_start(
		C.uintptr_t(handle),
		C.uint32_t(flags),
		C.uint32_t(len(poolUUIDs)),
		uuids,
		cPolicies,
	))
}

// callCheckStop is a test helper for daos_control_check_stop.
func callCheckStop(handle cgo.Handle, poolUUIDs []uuid.UUID) int {
	var uuids *C.uuid_t

	if len(poolUUIDs) > 0 {
		uuidArray := make([]C.uuid_t, len(poolUUIDs))
		for i, u := range poolUUIDs {
			copyUUIDToC(u, &uuidArray[i])
		}
		uuids = &uuidArray[0]
	}

	return int(daos_control_check_stop(
		C.uintptr_t(handle),
		C.uint32_t(len(poolUUIDs)),
		uuids,
	))
}

// callCheckRepair is a test helper for daos_control_check_repair.
func callCheckRepair(handle cgo.Handle, seq uint64, action uint32) int {
	return int(daos_control_check_repair(
		C.uintptr_t(handle),
		C.uint64_t(seq),
		C.uint32_t(action),
	))
}

// callCheckQuery is a test helper for daos_control_check_query.
func callCheckQuery(handle cgo.Handle, poolUUIDs []uuid.UUID) int {
	var uuids *C.uuid_t

	if len(poolUUIDs) > 0 {
		uuidArray := make([]C.uuid_t, len(poolUUIDs))
		for i, u := range poolUUIDs {
			copyUUIDToC(u, &uuidArray[i])
		}
		uuids = &uuidArray[0]
	}

	// Call with nil dci to just test the query path
	return int(daos_control_check_query(
		C.uintptr_t(handle),
		C.uint32_t(len(poolUUIDs)),
		uuids,
		nil,
	))
}

// callCheckQueryWithInfo is a test helper for daos_control_check_query that
// populates a daos_check_info struct and returns it for verification.
func callCheckQueryWithInfo(handle cgo.Handle, poolUUIDs []uuid.UUID) (*C.struct_daos_check_info, int) {
	var uuids *C.uuid_t

	if len(poolUUIDs) > 0 {
		uuidArray := make([]C.uuid_t, len(poolUUIDs))
		for i, u := range poolUUIDs {
			copyUUIDToC(u, &uuidArray[i])
		}
		uuids = &uuidArray[0]
	}

	var dci C.struct_daos_check_info
	rc := int(daos_control_check_query(
		C.uintptr_t(handle),
		C.uint32_t(len(poolUUIDs)),
		uuids,
		&dci,
	))

	return &dci, rc
}

// callCheckInfoFree is a test helper for daos_control_check_info_free.
func callCheckInfoFree(dci *C.struct_daos_check_info) {
	daos_control_check_info_free(dci)
}

// callCheckSetPolicy is a test helper for daos_control_check_set_policy.
func callCheckSetPolicy(handle cgo.Handle, flags uint32, policies string) int {
	var cPolicies *C.char
	if policies != "" {
		cPolicies = C.CString(policies)
		defer C.free(unsafe.Pointer(cPolicies))
	}

	return int(daos_control_check_set_policy(
		C.uintptr_t(handle),
		C.uint32_t(flags),
		cPolicies,
	))
}

// callServerSetLogmasks is a test helper for daos_control_server_set_logmasks.
func callServerSetLogmasks(handle cgo.Handle, masks, streams, subsystems string) int {
	var cMasks, cStreams, cSubsystems *C.char

	if masks != "" {
		cMasks = C.CString(masks)
		defer C.free(unsafe.Pointer(cMasks))
	}
	if streams != "" {
		cStreams = C.CString(streams)
		defer C.free(unsafe.Pointer(cStreams))
	}
	if subsystems != "" {
		cSubsystems = C.CString(subsystems)
		defer C.free(unsafe.Pointer(cSubsystems))
	}

	return int(daos_control_server_set_logmasks(
		C.uintptr_t(handle),
		cMasks,
		cStreams,
		cSubsystems,
	))
}

// callStorageDeviceList is a test helper for daos_control_storage_device_list.
func callStorageDeviceList(handle cgo.Handle) (int, int) {
	var ndisks C.int
	rc := int(daos_control_storage_device_list(
		C.uintptr_t(handle),
		&ndisks,
		nil,
	))
	return int(ndisks), rc
}

// callStorageSetNVMeFault is a test helper for daos_control_storage_set_nvme_fault.
func callStorageSetNVMeFault(handle cgo.Handle, host string, devUUID *testUUID, force bool) int {
	var cHost *C.char
	if host != "" {
		cHost = C.CString(host)
		defer C.free(unsafe.Pointer(cHost))
	}

	var forceInt C.int
	if force {
		forceInt = 1
	}

	return int(daos_control_storage_set_nvme_fault(
		C.uintptr_t(handle),
		cHost,
		devUUID.ptr(),
		forceInt,
	))
}

// callStorageQueryDeviceHealth is a test helper for daos_control_storage_query_device_health.
func callStorageQueryDeviceHealth(handle cgo.Handle, host, statsKey string, devUUID *testUUID) (string, int) {
	var cHost *C.char
	if host != "" {
		cHost = C.CString(host)
		defer C.free(unsafe.Pointer(cHost))
	}

	cStatsKey := C.CString(statsKey)
	defer C.free(unsafe.Pointer(cStatsKey))

	// Allocate output buffer
	statsOut := make([]C.char, 256)
	rc := int(daos_control_storage_query_device_health(
		C.uintptr_t(handle),
		cHost,
		cStatsKey,
		&statsOut[0],
		C.int(len(statsOut)),
		devUUID.ptr(),
	))

	// Convert output to string
	var result []byte
	for _, c := range statsOut {
		if c == 0 {
			break
		}
		result = append(result, byte(c))
	}

	return string(result), rc
}

// callSystemReintRank is a test helper for daos_control_system_reint_rank.
func callSystemReintRank(handle cgo.Handle, rank uint32) int {
	return int(daos_control_system_reint_rank(
		C.uintptr_t(handle),
		C.d_rank_t(rank),
	))
}

// callSystemExcludeRank is a test helper for daos_control_system_exclude_rank.
func callSystemExcludeRank(handle cgo.Handle, rank uint32) int {
	return int(daos_control_system_exclude_rank(
		C.uintptr_t(handle),
		C.d_rank_t(rank),
	))
}

// callPoolExtend is a test helper for daos_control_pool_extend.
func callPoolExtend(handle cgo.Handle, poolUUID *testUUID, ranks []uint32) int {
	if len(ranks) == 0 {
		return int(daos_control_pool_extend(
			C.uintptr_t(handle),
			poolUUID.ptr(),
			nil,
			nil,
			0,
		))
	}
	cRanks := make([]C.d_rank_t, len(ranks))
	for i, r := range ranks {
		cRanks[i] = C.d_rank_t(r)
	}
	return int(daos_control_pool_extend(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		nil,
		&cRanks[0],
		C.int(len(ranks)),
	))
}

// callPoolSetProp is a test helper for daos_control_pool_set_prop.
func callPoolSetProp(handle cgo.Handle, poolUUID *testUUID, name, value string) int {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))
	cValue := C.CString(value)
	defer C.free(unsafe.Pointer(cValue))

	return int(daos_control_pool_set_prop(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		cName,
		cValue,
	))
}

// callPoolGetProp is a test helper for daos_control_pool_get_prop.
func callPoolGetProp(handle cgo.Handle, label string, poolUUID *testUUID, name string) (string, int) {
	var cLabel *C.char
	if label != "" {
		cLabel = C.CString(label)
		defer C.free(unsafe.Pointer(cLabel))
	}
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	var cValue *C.char
	rc := int(daos_control_pool_get_prop(
		C.uintptr_t(handle),
		cLabel,
		poolUUID.ptr(),
		cName,
		&cValue,
	))

	var value string
	if cValue != nil {
		value = C.GoString(cValue)
		C.free(unsafe.Pointer(cValue))
	}
	return value, rc
}

// callPoolUpdateACE is a test helper for daos_control_pool_update_ace.
func callPoolUpdateACE(handle cgo.Handle, poolUUID *testUUID, ace string) int {
	aceStr := C.CString(ace)
	defer C.free(unsafe.Pointer(aceStr))

	return int(daos_control_pool_update_ace(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		nil,
		aceStr,
	))
}

// callPoolDeleteACE is a test helper for daos_control_pool_delete_ace.
func callPoolDeleteACE(handle cgo.Handle, poolUUID *testUUID, principal string) int {
	cPrincipal := C.CString(principal)
	defer C.free(unsafe.Pointer(cPrincipal))

	return int(daos_control_pool_delete_ace(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		nil,
		cPrincipal,
	))
}

// callPoolRebuildStop is a test helper for daos_control_pool_rebuild_stop.
func callPoolRebuildStop(handle cgo.Handle, poolUUID *testUUID, force bool) int {
	var forceInt C.int
	if force {
		forceInt = 1
	}
	return int(daos_control_pool_rebuild_stop(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		nil,
		forceInt,
	))
}

// callPoolRebuildStart is a test helper for daos_control_pool_rebuild_start.
func callPoolRebuildStart(handle cgo.Handle, poolUUID *testUUID) int {
	return int(daos_control_pool_rebuild_start(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		nil,
	))
}

// testPoolListInfo wraps pool list info for testing.
type testPoolListInfo struct {
	data []C.daos_mgmt_pool_info_t
}

// newTestPoolListInfo creates a pool list info array.
func newTestPoolListInfo(count int) *testPoolListInfo {
	if count == 0 {
		return &testPoolListInfo{}
	}
	return &testPoolListInfo{data: make([]C.daos_mgmt_pool_info_t, count)}
}

// ptr returns a pointer to the first element.
func (t *testPoolListInfo) ptr() *C.daos_mgmt_pool_info_t {
	if len(t.data) == 0 {
		return nil
	}
	return &t.data[0]
}

// callPoolList is a test helper for daos_control_pool_list.
func callPoolList(handle cgo.Handle, npools *uint64, pools *testPoolListInfo) int {
	var cNpools C.daos_size_t
	if npools != nil {
		cNpools = C.daos_size_t(*npools)
	}

	var poolsPtr *C.daos_mgmt_pool_info_t
	if pools != nil {
		poolsPtr = pools.ptr()
	}

	rc := int(daos_control_pool_list(
		C.uintptr_t(handle),
		nil,
		&cNpools,
		poolsPtr,
	))

	if npools != nil {
		*npools = uint64(cNpools)
	}
	return rc
}

// getPoolListLabel returns the label from a pool list entry.
func (t *testPoolListInfo) getLabel(idx int) string {
	if idx >= len(t.data) {
		return ""
	}
	if t.data[idx].mgpi_label == nil {
		return ""
	}
	return C.GoString(t.data[idx].mgpi_label)
}

// getPoolListUUID returns the UUID from a pool list entry.
func (t *testPoolListInfo) getUUID(idx int) uuid.UUID {
	if idx >= len(t.data) {
		return uuid.Nil
	}
	return uuidFromC(&t.data[idx].mgpi_uuid)
}

// free releases memory allocated by pool list using the exported free function.
func (t *testPoolListInfo) free() {
	if len(t.data) > 0 {
		daos_control_pool_list_free(&t.data[0], C.daos_size_t(len(t.data)))
	}
}

package client

import (
	"context"
	"strconv"
	"strings"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

/*
#include <gurt/common.h>
#include <daos.h>
#include <daos_pool.h>

static inline uint32_t
get_rebuild_state(struct daos_rebuild_status *drs)
{
	if (drs == NULL)
		return 0;

	return drs->rs_state;
}
*/
import "C"

type (
	PoolConnectFlag uint
)

const (
	PoolConnectFlagReadOnly  PoolConnectFlag = C.DAOS_PC_RO
	PoolConnectFlagReadWrite PoolConnectFlag = C.DAOS_PC_RW
	PoolConnectFlagExclusive PoolConnectFlag = C.DAOS_PC_EX
)

func newPoolSpaceInfo(dps *C.struct_daos_pool_space, mt C.uint) *daos.StorageUsageStats {
	if dps == nil {
		return nil
	}

	return &daos.StorageUsageStats{
		Total:     uint64(dps.ps_space.s_total[mt]),
		Free:      uint64(dps.ps_space.s_free[mt]),
		Min:       uint64(dps.ps_free_min[mt]),
		Max:       uint64(dps.ps_free_max[mt]),
		Mean:      uint64(dps.ps_free_mean[mt]),
		MediaType: daos.StorageMediaType(mt),
	}
}

func newPoolRebuildStatus(drs *C.struct_daos_rebuild_status) *daos.PoolRebuildStatus {
	if drs == nil {
		return nil
	}

	compatRebuildState := func() daos.PoolRebuildState {
		switch {
		case drs.rs_version == 0:
			return daos.PoolRebuildStateIdle
		case C.get_rebuild_state(drs) == C.DRS_COMPLETED:
			return daos.PoolRebuildStateDone
		default:
			return daos.PoolRebuildStateBusy
		}
	}

	return &daos.PoolRebuildStatus{
		Status:  int32(drs.rs_errno),
		Objects: uint64(drs.rs_obj_nr),
		Records: uint64(drs.rs_rec_nr),
		State:   compatRebuildState(),
	}
}

func newPoolInfo(cpi *C.daos_pool_info_t) *daos.PoolInfo {
	return &daos.PoolInfo{
		UUID:            uuid.Must(uuidFromC(cpi.pi_uuid)),
		TotalTargets:    uint32(cpi.pi_ntargets),
		ActiveTargets:   uint32(cpi.pi_ntargets - cpi.pi_ndisabled),
		TotalEngines:    uint32(cpi.pi_nnodes),
		DisabledTargets: uint32(cpi.pi_ndisabled),
		Version:         uint32(cpi.pi_map_ver),
		Leader:          uint32(cpi.pi_leader),
		Rebuild:         newPoolRebuildStatus(&cpi.pi_rebuild_st),
		TierStats: []*daos.StorageUsageStats{
			newPoolSpaceInfo(&cpi.pi_space, C.DAOS_MEDIA_SCM),
			newPoolSpaceInfo(&cpi.pi_space, C.DAOS_MEDIA_NVME),
		},
	}
}

type (
	PoolConnectResp struct {
		PoolConnection *PoolHandle
		PoolInfo       *daos.PoolInfo
	}
)

func (ph *PoolHandle) CreateContainer(ctx context.Context, req ContainerCreateReq) (*ContainerInfo, error) {
	return ContainerCreate(ctx, ph, req)
}

func (ph *PoolHandle) DestroyContainer(ctx context.Context, contID string, force bool) error {
	return ContainerDestroy(ctx, ph, contID, force)
}

func (ph *PoolHandle) OpenContainer(ctx context.Context, contID string, flags ...ContainerOpenFlag) (*ContainerHandle, error) {
	return ContainerOpen(ctx, ph, contID, flags...)
}

func (ph *PoolHandle) Query(ctx context.Context, req PoolQueryReq) (*daos.PoolInfo, error) {
	return PoolQuery(ctx, ph, req)
}

func (ph *PoolHandle) Disconnect(ctx context.Context) error {
	return PoolDisconnect(ctx, ph)
}

func (ph *PoolHandle) ListContainers(ctx context.Context, query bool) ([]*ContainerInfo, error) {
	return PoolListContainers(ctx, ph, query)
}

func (ph *PoolHandle) ListAttributes(ctx context.Context) ([]string, error) {
	return PoolListAttributes(ctx, ph)
}

func (ph *PoolHandle) GetAttributes(ctx context.Context, names ...string) ([]*daos.Attribute, error) {
	return PoolGetAttributes(ctx, ph, names...)
}

func (ph *PoolHandle) SetAttributes(ctx context.Context, attrs []*daos.Attribute) error {
	return PoolSetAttributes(ctx, ph, attrs)
}

func (ph *PoolHandle) DeleteAttribute(ctx context.Context, attrName string) error {
	return PoolDeleteAttribute(ctx, ph, attrName)
}

func (b *daosClientBinding) daos_pool_connect(poolID *C.char, sys *C.char, flags C.uint, poolHdl *C.daos_handle_t, poolInfo *C.daos_pool_info_t, ev *C.struct_daos_event) C.int {
	return C.daos_pool_connect(poolID, sys, flags, poolHdl, poolInfo, ev)
}

func (m *mockApiClient) daos_pool_connect(poolID *C.char, sys *C.char, flags C.uint, poolHdl *C.daos_handle_t, poolInfo *C.daos_pool_info_t, ev *C.struct_daos_event) C.int {
	if rc := m.getRc("daos_pool_connect", 0); rc != 0 {
		return rc
	}

	poolHdl.cookie = 42
	poolInfo.pi_uuid = uuidToC(m.cfg.ConnectedPool)
	return 0
}

func PoolConnect(ctx context.Context, poolID, sysID string, flags PoolConnectFlag) (*PoolConnectResp, error) {
	log.Debugf("PoolConnect(%s)", poolID)

	client, err := getApiClient(ctx)
	if err != nil {
		return nil, err
	}

	if poolID == "" {
		return nil, errors.Wrap(daos.InvalidInput, "no pool ID provided")
	}
	if sysID == "" {
		sysID = build.DefaultSystemName
	}
	if flags == 0 {
		flags = PoolConnectFlagReadOnly
	}

	var dpi C.daos_pool_info_t
	var poolConn PoolHandle

	cPoolID := C.CString(poolID)
	defer freeString(cPoolID)
	cSys := C.CString(sysID)
	defer freeString(cSys)

	if err := daosError(client.daos_pool_connect(cPoolID, cSys, C.uint(flags), &poolConn.daosHandle, &dpi, nil)); err != nil {
		return nil, errors.Wrap(err, "failed to connect to pool")
	}

	poolInfo := newPoolInfo(&dpi)
	poolConn.UUID = poolInfo.UUID
	return &PoolConnectResp{
		PoolConnection: &poolConn,
		PoolInfo:       poolInfo,
	}, nil
}

func generateRankSet(ranklist *C.d_rank_list_t) string {
	if ranklist.rl_nr == 0 {
		return ""
	}

	rankSlice := unsafe.Slice((*C.d_rank_t)(unsafe.Pointer(ranklist.rl_ranks)), ranklist.rl_nr)
	ranks := make([]string, len(rankSlice))
	for i, rank := range rankSlice {
		ranks[i] = strconv.Itoa(int(rank))
	}
	return "[" + strings.Join(ranks, ",") + "]"
}

func (b *daosClientBinding) daos_pool_query(poolHdl C.daos_handle_t, rankList **C.d_rank_list_t, poolInfo *C.daos_pool_info_t, props *C.daos_prop_t, ev *C.struct_daos_event) C.int {
	return C.daos_pool_query(poolHdl, rankList, poolInfo, props, ev)
}

func (m *mockApiClient) daos_pool_query(poolHdl C.daos_handle_t, rankList **C.d_rank_list_t, poolInfo *C.daos_pool_info_t, props *C.daos_prop_t, ev *C.struct_daos_event) C.int {
	return m.getRc("daos_pool_query", 0)
}

const (
	dpiQuerySpace   = C.DPI_SPACE
	dpiQueryRebuild = C.DPI_REBUILD_STATUS
	dpiQueryAll     = C.uint64_t(^uint64(0)) // DPI_ALL is -1
)

type (
	PoolQueryReq struct {
		IncludeEnabled  bool
		IncludeDisabled bool
	}
)

func PoolQuery(ctx context.Context, poolConn *PoolHandle, req PoolQueryReq) (*daos.PoolInfo, error) {
	log.Debugf("PoolQuery(%s:%+v)", poolConn, req)

	client, err := getApiClient(ctx)
	if err != nil {
		return nil, err
	}

	var rlPtr **C.d_rank_list_t = nil
	var rl *C.d_rank_list_t = nil
	defer C.d_rank_list_free(rl)

	if req.IncludeEnabled || req.IncludeDisabled {
		rlPtr = &rl
	}

	cpi := &C.daos_pool_info_t{
		pi_bits: dpiQueryAll,
	}
	if req.IncludeDisabled {
		cpi.pi_bits &= C.uint64_t(^(uint64(C.DPI_ENGINES_ENABLED)))
	}

	rc := client.daos_pool_query(poolConn.daosHandle, rlPtr, cpi, nil, nil)
	if err := daosError(rc); err != nil {
		return nil, err
	}

	dpi := newPoolInfo(cpi)
	if rlPtr != nil {
		if req.IncludeEnabled {
			dpi.EnabledRanks, err = ranklist.CreateRankSet(generateRankSet(rl))
			if err != nil {
				return nil, err
			}
		}
		if req.IncludeDisabled {
			dpi.DisabledRanks, err = ranklist.CreateRankSet(generateRankSet(rl))
			if err != nil {
				return nil, err
			}
		}
	}

	return dpi, nil
}

func (b *daosClientBinding) daos_pool_disconnect(poolHdl C.daos_handle_t) C.int {
	// Hack for NLT fault injection testing: If the rc
	// is -DER_NOMEM, retry once in order to actually
	// shut down and release resources.
	rc := C.daos_pool_disconnect(poolHdl, nil)
	if rc == -C.DER_NOMEM {
		rc = C.daos_pool_disconnect(poolHdl, nil)
		// DAOS-8866, daos_pool_disconnect() might have failed, but worked anyway.
		if rc == -C.DER_NO_HDL {
			rc = -C.DER_SUCCESS
		}
	}

	return rc
}

func (m *mockApiClient) daos_pool_disconnect(poolHdl C.daos_handle_t) C.int {
	return m.getRc("daos_pool_disconnect", 0)
}

func PoolDisconnect(ctx context.Context, poolConn *PoolHandle) error {
	log.Debugf("PoolDisconnect(%s)", poolConn)

	client, err := getApiClient(ctx)
	if err != nil {
		return err
	}

	if err := daosError(client.daos_pool_disconnect(poolConn.daosHandle)); err != nil {
		return errors.Wrap(err, "failed to disconnect from pool")
	}
	poolConn.invalidate()

	return nil
}

func PoolListContainers(ctx context.Context, poolConn *PoolHandle, query bool) ([]*ContainerInfo, error) {
	log.Debugf("PoolListContainers(%s:%t)", poolConn, query)

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	extra_cont_margin := C.size_t(16)

	// First call gets the current number of containers.
	var ncont C.daos_size_t
	rc := C.daos_pool_list_cont(poolConn.daosHandle, &ncont, nil, nil)
	if err := daosError(rc); err != nil {
		return nil, errors.Wrap(err, "pool list containers failed")
	}

	// No containers.
	if ncont == 0 {
		return nil, nil
	}

	var cConts *C.struct_daos_pool_cont_info
	// Extend ncont with a safety margin to account for containers
	// that might have been created since the first API call.
	ncont += extra_cont_margin
	cConts = (*C.struct_daos_pool_cont_info)(C.calloc(C.sizeof_struct_daos_pool_cont_info, ncont))
	if cConts == nil {
		return nil, errors.New("calloc() for containers failed")
	}
	dpciSlice := unsafe.Slice(cConts, ncont)
	cleanup := func() {
		C.free(unsafe.Pointer(cConts))
	}

	rc = C.daos_pool_list_cont(poolConn.daosHandle, &ncont, cConts, nil)
	if err := daosError(rc); err != nil {
		cleanup()
		return nil, err
	}
	defer C.free(unsafe.Pointer(cConts))

	out := make([]*ContainerInfo, ncont)
	for i := range out {
		out[i] = new(ContainerInfo)
		out[i].PoolUUID = poolConn.UUID
		out[i].UUID = uuid.Must(uuidFromC(dpciSlice[i].pci_uuid))
		out[i].Label = C.GoString(&dpciSlice[i].pci_label[0])
		contLabel := out[i].Label

		if query {
			contConn, err := ContainerOpen(ctx, poolConn, contLabel, ContainerOpenFlagReadOnly)
			if err != nil {
				log.Debugf("unable to open container %q for query: %s", contLabel, err)
				continue
			}

			closeCont := func() {
				if err := ContainerClose(ctx, contConn); err != nil {
					log.Debugf("unable to close container %q: %s", out[i].Label, err)
				}
			}

			info, err := ContainerQuery(ctx, contConn)
			if err != nil {
				log.Debugf("unable to query container %q: %s", contLabel, err)
				closeCont()
				continue
			}

			out[i] = info
			closeCont()
		}
	}

	return out, nil
}

func PoolListAttributes(ctx context.Context, poolConn *PoolHandle) ([]string, error) {
	log.Debugf("PoolListAttributes(%s)", poolConn)

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	return listDaosAttributes(poolConn.daosHandle, poolAttr)
}

func PoolGetAttributes(ctx context.Context, poolConn *PoolHandle, names ...string) ([]*daos.Attribute, error) {
	log.Debugf("PoolGetAttributes(%s:%v)", poolConn, names)

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	return getDaosAttributes(poolConn.daosHandle, poolAttr, names)
}

func PoolSetAttributes(ctx context.Context, poolConn *PoolHandle, attrs []*daos.Attribute) error {
	log.Debugf("PoolSetAttributes(%s:%v)", poolConn, attrs)

	if err := ctx.Err(); err != nil {
		return ctxErr(err)
	}

	return setDaosAttributes(poolConn.daosHandle, poolAttr, attrs)
}

func PoolDeleteAttribute(ctx context.Context, poolConn *PoolHandle, attrName string) error {
	log.Debugf("PoolDeleteAttribute(%s:%s)", poolConn, attrName)

	if err := ctx.Err(); err != nil {
		return ctxErr(err)
	}

	return delDaosAttribute(poolConn.daosHandle, poolAttr, attrName)
}

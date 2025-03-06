//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"context"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
#include <gurt/common.h>
#include <daos.h>
#include <daos_pool.h>

#include "util.h"

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
	// PoolHandle is an opaque type used to represent a DAOS Pool connection.
	PoolHandle struct {
		connHandle
	}
)

const (
	poolHandleKey ctxHdlKey = "poolHandle"
)

// phFromContext retrieves the PoolHandle from the supplied context, if available.
func phFromCtx(ctx context.Context) (*PoolHandle, error) {
	if ctx == nil {
		return nil, errNilCtx
	}

	ph, ok := ctx.Value(poolHandleKey).(*PoolHandle)
	if !ok {
		return nil, errNoCtxHdl
	}
	ph.fromCtx = true

	return ph, nil
}

// IsValid returns true if the pool handle is valid.
func (ph *PoolHandle) IsValid() bool {
	if ph == nil {
		return false
	}
	return ph.connHandle.IsValid()
}

// toCtx returns a new context with the PoolHandle stashed in it.
// NB: Will panic if the context already has a different PoolHandle stashed.
func (ph *PoolHandle) toCtx(ctx context.Context) context.Context {
	if ph == nil {
		return ctx
	}

	stashed, _ := phFromCtx(ctx)
	if stashed != nil {
		if stashed.UUID() == ph.UUID() {
			return ctx
		}
		panic("attempt to stash different PoolHandle in context")
	}

	return context.WithValue(ctx, poolHandleKey, ph)
}

// newPoolSpaceInfo constructs a Go type from the underlying C type.
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

// newPoolRebuildStatus constructs a Go type from the underlying C type.
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

// newPoolInfo constructs a Go type from the underlying C type.
func newPoolInfo(cpi *C.daos_pool_info_t) *daos.PoolInfo {
	if cpi == nil {
		return nil
	}

	poolInfo := new(daos.PoolInfo)

	poolInfo.QueryMask = daos.PoolQueryMask(cpi.pi_bits)
	poolInfo.UUID = uuid.Must(uuidFromC(cpi.pi_uuid))
	poolInfo.TotalTargets = uint32(cpi.pi_ntargets)
	poolInfo.DisabledTargets = uint32(cpi.pi_ndisabled)
	poolInfo.ActiveTargets = uint32(cpi.pi_space.ps_ntargets)
	poolInfo.TotalEngines = uint32(cpi.pi_nnodes)
	poolInfo.ServiceLeader = uint32(cpi.pi_leader)
	poolInfo.Version = uint32(cpi.pi_map_ver)
	poolInfo.State = daos.PoolServiceStateReady
	if poolInfo.DisabledTargets > 0 {
		poolInfo.State = daos.PoolServiceStateDegraded
	}

	poolInfo.Rebuild = newPoolRebuildStatus(&cpi.pi_rebuild_st)
	if poolInfo.QueryMask.HasOption(daos.PoolQueryOptionSpace) {
		poolInfo.TierStats = []*daos.StorageUsageStats{
			newPoolSpaceInfo(&cpi.pi_space, C.DAOS_MEDIA_SCM),
			newPoolSpaceInfo(&cpi.pi_space, C.DAOS_MEDIA_NVME),
		}
	}

	return poolInfo
}

func poolInfoFromProps(pi *daos.PoolInfo, propEntries []C.struct_daos_prop_entry) {
	if pi == nil || len(propEntries) == 0 {
		return
	}

	for _, entry := range propEntries {
		switch entry.dpe_type {
		case C.DAOS_PROP_PO_LABEL:
			pi.Label = C.GoString(C.get_dpe_str(&entry))
		case C.DAOS_PROP_PO_SVC_LIST:
			rlPtr := C.get_dpe_val_ptr(&entry)
			if rlPtr == nil {
				return
			}
			rs, err := rankSetFromC((*C.d_rank_list_t)(rlPtr))
			if err != nil {
				return
			}
			pi.ServiceReplicas = rs.Ranks()
		}
	}
}

// Disconnect signals that the client no longer needs the DAOS pool
// connection and that it is safe to release resources allocated for
// the connection.
func (ph *PoolHandle) Disconnect(ctx context.Context) error {
	if !ph.IsValid() {
		return ErrInvalidPoolHandle
	}
	logging.FromContext(ctx).Debugf("PoolHandle.Disconnect(%s)", ph)

	if err := daosError(daos_pool_disconnect(ph.daosHandle)); err != nil {
		return errors.Wrap(err, "failed to disconnect from pool")
	}
	ph.invalidate()

	return nil
}

// UUID returns the DAOS pool's UUID.
func (ph *PoolHandle) UUID() uuid.UUID {
	if ph == nil {
		return uuid.Nil
	}
	return ph.connHandle.UUID
}

type (
	// PoolConnectReq defines the parameters for a PoolConnect request.
	PoolConnectReq struct {
		SysName string
		ID      string
		Flags   daos.PoolConnectFlag
		Query   bool
	}

	// PoolConnectResp contains the response to a PoolConnect request.
	PoolConnectResp struct {
		Connection *PoolHandle
		Info       *daos.PoolInfo
	}
)

// PoolConnect establishes a connection to the specified DAOS pool.
// NB: The caller is responsible for disconnecting from the pool when
// finished.
func PoolConnect(ctx context.Context, req PoolConnectReq) (*PoolConnectResp, error) {
	if ctx == nil {
		return nil, errNilCtx
	}
	logging.FromContext(ctx).Debugf("PoolConnect(%+v)", req)

	if _, err := phFromCtx(ctx); err == nil {
		return nil, ErrContextHandleConflict
	}

	if req.ID == "" {
		return nil, errors.Wrap(daos.InvalidInput, "no pool ID provided")
	}
	if req.Flags == 0 {
		req.Flags = daos.PoolConnectFlagReadOnly
	}

	var dpi C.daos_pool_info_t
	if req.Query {
		dpi.pi_bits = C.ulong(daos.DefaultPoolQueryMask)
	}
	var poolConn PoolHandle

	cPoolID := C.CString(req.ID)
	defer freeString(cPoolID)
	var cSys *C.char
	if req.SysName != "" {
		cSys = C.CString(req.SysName)
		defer freeString(cSys)
	}

	if err := daosError(daos_pool_connect(cPoolID, cSys, C.uint(req.Flags), &poolConn.daosHandle, &dpi, nil)); err != nil {
		return nil, errors.Wrap(err, "failed to connect to pool")
	}

	poolInfo := newPoolInfo(&dpi)
	poolConn.connHandle.UUID = poolInfo.UUID
	if req.ID != poolInfo.UUID.String() {
		poolInfo.Label = req.ID
	} else {
		// If the connection was made with a UUID, then we don't know the label without
		// a query. This should be a rare scenario. If the request allows it, try a query.
		poolInfo.Label = MissingPoolLabel
		if req.Query {
			qpi, err := poolConn.Query(ctx, daos.HealthOnlyPoolQueryMask)
			if err != nil {
				if dcErr := poolConn.Disconnect(ctx); dcErr != nil {
					logging.FromContext(ctx).Error(dcErr.Error())
				}
				return nil, errors.Wrap(err, "failed to query pool for label")
			}
			poolInfo.Label = qpi.Label
		}
	}
	// Set the label on the connection for convenience.
	poolConn.connHandle.Label = poolInfo.Label

	logging.FromContext(ctx).Debugf("Connected to Pool %s", &poolConn)
	return &PoolConnectResp{
		Connection: &poolConn,
		Info:       poolInfo,
	}, nil
}

// getPoolConn retrieves the PoolHandle set in the context, if available,
// or tries to establish a new connection to the specified pool.
func getPoolConn(ctx context.Context, sysName, poolID string, flags daos.PoolConnectFlag) (*PoolHandle, func(), error) {
	nulCleanup := func() {}
	ph, err := phFromCtx(ctx)
	if err == nil {
		if poolID != "" {
			return nil, nulCleanup, errors.Wrap(daos.InvalidInput, "PoolHandle found in context with non-empty poolID")
		}
		return ph, nulCleanup, nil
	}

	resp, err := PoolConnect(ctx, PoolConnectReq{
		ID:      poolID,
		SysName: sysName,
		Flags:   flags,
		Query:   false,
	})
	if err != nil {
		return nil, nulCleanup, err
	}

	cleanup := func() {
		if err := resp.Connection.Disconnect(ctx); err != nil {
			logging.FromContext(ctx).Error(err.Error())
		}
	}
	return resp.Connection, cleanup, nil
}

// Query is a convenience wrapper around the PoolQuery() function.
func (ph *PoolHandle) Query(ctx context.Context, mask daos.PoolQueryMask) (*daos.PoolInfo, error) {
	if !ph.IsValid() {
		return nil, ErrInvalidPoolHandle
	}
	return PoolQuery(ph.toCtx(ctx), "", "", mask)
}

// PoolQuery retrieves information about the DAOS Pool, including health and rebuild status,
// storage usage, and other details.
func PoolQuery(ctx context.Context, sysName, poolID string, queryMask daos.PoolQueryMask) (*daos.PoolInfo, error) {
	if queryMask == 0 {
		queryMask = daos.DefaultPoolQueryMask
	}
	poolConn, disconnect, err := getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadOnly)
	if err != nil {
		return nil, err
	}
	defer disconnect()
	logging.FromContext(ctx).Debugf("PoolQuery(%s:%s)", poolConn, queryMask)

	var enabledRanks *C.d_rank_list_t
	var disabledRanks *C.d_rank_list_t
	defer func() {
		C.d_rank_list_free(enabledRanks)
		C.d_rank_list_free(disabledRanks)
	}()

	// Query for some additional information stored as properties.
	queryProps := C.daos_prop_alloc(2)
	if queryProps == nil {
		return nil, errors.Wrap(daos.NoMemory, "failed to allocate property list")
	}
	propEntries := unsafe.Slice(queryProps.dpp_entries, queryProps.dpp_nr)
	propEntries[0].dpe_type = C.DAOS_PROP_PO_LABEL
	propEntries[1].dpe_type = C.DAOS_PROP_PO_SVC_LIST
	defer func() {
		C.daos_prop_free(queryProps)
	}()

	var rc C.int
	cPoolInfo := C.daos_pool_info_t{
		pi_bits: C.uint64_t(queryMask),
	}
	if queryMask.HasOption(daos.PoolQueryOptionEnabledEngines) && queryMask.HasOption(daos.PoolQueryOptionDisabledEngines) {
		enaQm := queryMask
		enaQm.ClearOptions(daos.PoolQueryOptionDisabledEngines)
		cPoolInfo.pi_bits = C.uint64_t(enaQm)
		rc = daos_pool_query(poolConn.daosHandle, &enabledRanks, &cPoolInfo, queryProps, nil)
		if err := daosError(rc); err != nil {
			return nil, errors.Wrap(err, "failed to query pool")
		}

		/* second query to just get disabled ranks */
		rc = daos_pool_query(poolConn.daosHandle, &disabledRanks, nil, nil, nil)
	} else if queryMask.HasOption(daos.PoolQueryOptionEnabledEngines) {
		rc = daos_pool_query(poolConn.daosHandle, &enabledRanks, &cPoolInfo, queryProps, nil)
	} else if queryMask.HasOption(daos.PoolQueryOptionDisabledEngines) {
		rc = daos_pool_query(poolConn.daosHandle, &disabledRanks, &cPoolInfo, queryProps, nil)
	} else {
		rc = daos_pool_query(poolConn.daosHandle, nil, &cPoolInfo, queryProps, nil)
	}

	if err := daosError(rc); err != nil {
		return nil, errors.Wrap(err, "failed to query pool")
	}

	poolInfo := newPoolInfo(&cPoolInfo)
	poolInfo.QueryMask = queryMask
	poolInfoFromProps(poolInfo, propEntries)

	if enabledRanks != nil {
		poolInfo.EnabledRanks, err = rankSetFromC(enabledRanks)
		if err != nil {
			return nil, err
		}
	}
	if disabledRanks != nil {
		poolInfo.DisabledRanks, err = rankSetFromC(disabledRanks)
		if err != nil {
			return nil, err
		}
	}

	return poolInfo, nil
}

func newPoolTargetInfo(ptinfo *C.daos_target_info_t) *daos.PoolQueryTargetInfo {
	return &daos.PoolQueryTargetInfo{
		Type:  daos.PoolQueryTargetType(ptinfo.ta_type),
		State: daos.PoolQueryTargetState(ptinfo.ta_state),
		Space: []*daos.StorageUsageStats{
			{
				Total:     uint64(ptinfo.ta_space.s_total[C.DAOS_MEDIA_SCM]),
				Free:      uint64(ptinfo.ta_space.s_free[C.DAOS_MEDIA_SCM]),
				MediaType: C.DAOS_MEDIA_SCM,
			},
			{
				Total:     uint64(ptinfo.ta_space.s_total[C.DAOS_MEDIA_NVME]),
				Free:      uint64(ptinfo.ta_space.s_free[C.DAOS_MEDIA_NVME]),
				MediaType: C.DAOS_MEDIA_NVME,
			},
		},
	}
}

// QueryTargets is a convenience wrapper around the PoolQueryTargets() function.
func (ph *PoolHandle) QueryTargets(ctx context.Context, rank ranklist.Rank, targets *ranklist.RankSet) ([]*daos.PoolQueryTargetInfo, error) {
	if !ph.IsValid() {
		return nil, ErrInvalidPoolHandle
	}
	return PoolQueryTargets(ph.toCtx(ctx), "", "", rank, targets)
}

// PoolQueryTargets retrieves information about storage targets in the DAOS Pool.
func PoolQueryTargets(ctx context.Context, sysName, poolID string, rank ranklist.Rank, reqTargets *ranklist.RankSet) ([]*daos.PoolQueryTargetInfo, error) {
	targets := ranklist.NewRankSet()
	targets.Replace(reqTargets)

	if targets.Count() == 0 {
		pi, err := PoolQuery(ctx, sysName, poolID, daos.HealthOnlyPoolQueryMask)
		if err != nil || (pi.TotalTargets == 0 || pi.TotalEngines == 0) {
			if err != nil {
				return nil, errors.Wrap(err, "pool query failed")
			}
			return nil, errors.New("failed to derive target count from pool query")
		}
		tgtCount := pi.TotalTargets / pi.TotalEngines
		for i := uint32(0); i < tgtCount; i++ {
			targets.Add(ranklist.Rank(i))
		}
	}
	poolConn, disconnect, err := getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadOnly)
	if err != nil {
		return nil, err
	}
	defer disconnect()
	logging.FromContext(ctx).Debugf("PoolQueryTargets(%s:%d:[%s])", poolConn, rank, targets)

	ptInfo := C.daos_target_info_t{}
	var rc C.int

	infos := make([]*daos.PoolQueryTargetInfo, 0, targets.Count())
	for _, tgt := range targets.Ranks() {
		rc = daos_pool_query_target(poolConn.daosHandle, C.uint32_t(tgt), C.uint32_t(rank), &ptInfo, nil)
		if err := daosError(rc); err != nil {
			return nil, errors.Wrapf(err, "failed to query pool %s rank:target %d:%d", poolID, rank, tgt)
		}

		infos = append(infos, newPoolTargetInfo(&ptInfo))
	}

	return infos, nil
}

// ListAttributes is a convenience wrapper around the PoolListAttributes() function.
func (ph *PoolHandle) ListAttributes(ctx context.Context) ([]string, error) {
	if !ph.IsValid() {
		return nil, ErrInvalidPoolHandle
	}
	return PoolListAttributes(ph.toCtx(ctx), "", "")
}

// PoolListAttributes returns a list of user-definable pool attribute names.
func PoolListAttributes(ctx context.Context, sysName, poolID string) ([]string, error) {
	poolConn, disconnect, err := getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadOnly)
	if err != nil {
		return nil, err
	}
	defer disconnect()
	logging.FromContext(ctx).Debugf("PoolListAttributes(%s)", poolConn)

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	return listDaosAttributes(poolConn.daosHandle, poolAttr)
}

// GetAttributes is a convenience wrapper around the PoolGetAttributes() function.
func (ph *PoolHandle) GetAttributes(ctx context.Context, attrNames ...string) (daos.AttributeList, error) {
	if !ph.IsValid() {
		return nil, ErrInvalidPoolHandle
	}
	return PoolGetAttributes(ph.toCtx(ctx), "", "", attrNames...)
}

// PoolGetAttributes fetches the specified pool attributes. If no
// attribute names are provided, all attributes are fetched.
func PoolGetAttributes(ctx context.Context, sysName, poolID string, names ...string) (daos.AttributeList, error) {
	poolConn, disconnect, err := getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadOnly)
	if err != nil {
		return nil, err
	}
	defer disconnect()
	logging.FromContext(ctx).Debugf("PoolGetAttributes(%s:%v)", poolConn, names)

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	return getDaosAttributes(poolConn.daosHandle, poolAttr, names)
}

// SetAttributes is a convenience wrapper around the PoolSetAttributes() function.
func (ph *PoolHandle) SetAttributes(ctx context.Context, attrs ...*daos.Attribute) error {
	if !ph.IsValid() {
		return ErrInvalidPoolHandle
	}
	return PoolSetAttributes(ph.toCtx(ctx), "", "", attrs...)
}

// PoolSetAttributes sets the specified pool attributes.
func PoolSetAttributes(ctx context.Context, sysName, poolID string, attrs ...*daos.Attribute) error {
	poolConn, disconnect, err := getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadOnly)
	if err != nil {
		return err
	}
	defer disconnect()
	logging.FromContext(ctx).Debugf("PoolSetAttributes(%s:%v)", poolConn, attrs)

	if err := ctx.Err(); err != nil {
		return ctxErr(err)
	}

	return setDaosAttributes(poolConn.daosHandle, poolAttr, attrs)
}

// DeleteAttributes is a convenience wrapper around the PoolDeleteAttributes() function.
func (ph *PoolHandle) DeleteAttributes(ctx context.Context, attrNames ...string) error {
	if !ph.IsValid() {
		return ErrInvalidPoolHandle
	}
	return PoolDeleteAttributes(ph.toCtx(ctx), "", "", attrNames...)
}

// PoolDeleteAttributes deletes the specified pool attributes.
func PoolDeleteAttributes(ctx context.Context, sysName, poolID string, attrNames ...string) error {
	poolConn, disconnect, err := getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadOnly)
	if err != nil {
		return err
	}
	defer disconnect()
	logging.FromContext(ctx).Debugf("PoolDeleteAttributes(%s:%+v)", poolConn, attrNames)

	if err := ctx.Err(); err != nil {
		return ctxErr(err)
	}

	return delDaosAttributes(poolConn.daosHandle, poolAttr, attrNames)
}

func (ph *PoolHandle) ListContainers(ctx context.Context, query bool) ([]*daos.ContainerInfo, error) {
	if !ph.IsValid() {
		return nil, ErrInvalidPoolHandle
	}

	return PoolListContainers(ph.toCtx(ctx), "", "", query)
}

// PoolListContainers returns a list of information about containers in the pool.
func PoolListContainers(ctx context.Context, sysName, poolID string, query bool) ([]*daos.ContainerInfo, error) {
	poolConn, disconnect, err := getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadOnly)
	if err != nil {
		return nil, err
	}
	defer disconnect()
	logging.FromContext(ctx).Debugf("PoolListContainers(%s:%t)", poolConn, query)

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	extra_cont_margin := C.size_t(16)

	// First call gets the current number of containers.
	var ncont C.daos_size_t
	rc := daos_pool_list_cont(poolConn.daosHandle, &ncont, nil, nil)
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
	defer func() {
		C.free(unsafe.Pointer(cConts))
	}()

	rc = daos_pool_list_cont(poolConn.daosHandle, &ncont, cConts, nil)
	if err := daosError(rc); err != nil {
		return nil, err
	}

	out := make([]*daos.ContainerInfo, ncont)
	for i := range out {
		out[i] = new(daos.ContainerInfo)
		out[i].ContainerUUID = uuid.Must(uuidFromC(dpciSlice[i].pci_uuid))
		out[i].ContainerLabel = C.GoString(&dpciSlice[i].pci_label[0])
	}

	if query {
		for i := range out {
			qc, err := poolConn.QueryContainer(ctx, out[i].ContainerUUID.String())
			if err != nil {
				logging.FromContext(ctx).Errorf("failed to query container %s: %s", out[i].Name(), err)
				continue
			}
			out[i] = qc
		}
	}

	return out, nil
}

type (
	// GetPoolListReq defines the parameters for a GetPoolList request.
	GetPoolListReq struct {
		SysName string
		Query   bool
	}
)

// GetPoolList returns a list of DAOS pools in the system.
func GetPoolList(ctx context.Context, req GetPoolListReq) ([]*daos.PoolInfo, error) {
	if ctx == nil {
		return nil, errNilCtx
	}

	log := logging.FromContext(ctx)
	log.Debugf("GetPoolList(%+v)", req)

	if req.SysName == "" {
		req.SysName = build.DefaultSystemName
	}
	cSysName := C.CString(req.SysName)
	defer freeString(cSysName)

	var cPools []C.daos_mgmt_pool_info_t
	for {
		var rc C.int
		var poolCount C.size_t

		// First, fetch the total number of pools in the system.
		// We may not have access to all of them, so this is an upper bound.
		rc = daos_mgmt_list_pools(cSysName, &poolCount, nil, nil)
		if err := daosError(rc); err != nil {
			return nil, errors.Wrap(err, "failed to list pools")
		}
		log.Debugf("pools in system: %d", poolCount)

		if poolCount < 1 {
			return nil, nil
		}

		// Now, fetch the pools into a buffer sized for the number of pools found.
		cPools = make([]C.daos_mgmt_pool_info_t, poolCount)
		rc = daos_mgmt_list_pools(cSysName, &poolCount, &cPools[0], nil)
		err := daosError(rc)
		if err == nil {
			cPools = cPools[:poolCount] // adjust the slice to the number of pools retrieved
			log.Debugf("fetched %d pools", len(cPools))
			break
		}
		if err == daos.StructTooSmall {
			log.Notice("server-side pool list changed; re-fetching")
			continue
		}
		log.Errorf("failed to fetch pool list: %s", err)
		return nil, errors.Wrap(err, "failed to list pools")
	}

	pools := make([]*daos.PoolInfo, 0, len(cPools))
	for i := 0; i < len(cPools); i++ {
		cPool := &cPools[i]

		svcLdr := uint32(cPool.mgpi_ldr)
		svcRanks, err := rankSetFromC(cPool.mgpi_svc)
		if err != nil {
			return nil, err
		}
		defer func() {
			C.d_rank_list_free(cPool.mgpi_svc)
		}()
		poolUUID, err := uuidFromC(cPool.mgpi_uuid)
		if err != nil {
			return nil, err
		}
		poolLabel := C.GoString(cPool.mgpi_label)

		var pool *daos.PoolInfo
		if req.Query {
			pcResp, err := PoolConnect(ctx, PoolConnectReq{
				ID:      poolUUID.String(),
				SysName: req.SysName,
				Flags:   daos.PoolConnectFlagReadOnly,
				Query:   true,
			})
			if err != nil {
				log.Errorf("failed to connect to pool %q: %s", poolLabel, err)
				continue
			}
			if err := pcResp.Connection.Disconnect(ctx); err != nil {
				log.Errorf("failed to disconnect from pool %q: %s", poolLabel, err)
			}
			pool = pcResp.Info

			// Add a few missing pieces that the query doesn't fill in.
			pool.Label = poolLabel
			pool.ServiceLeader = svcLdr
			pool.ServiceReplicas = svcRanks.Ranks()
		} else {
			// Just populate the basic info.
			pool = &daos.PoolInfo{
				UUID:            poolUUID,
				Label:           poolLabel,
				ServiceLeader:   svcLdr,
				ServiceReplicas: svcRanks.Ranks(),
				State:           daos.PoolServiceStateReady,
			}
		}

		pools = append(pools, pool)
	}

	log.Debugf("fetched %d/%d pools", len(pools), len(cPools))
	return pools, nil
}

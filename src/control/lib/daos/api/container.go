//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"context"
	"fmt"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
#include <fcntl.h>

#include <gurt/common.h>
#include <daos.h>
#include <daos_cont.h>
#include <daos_fs.h>

#include "util.h"

#cgo LDFLAGS: -ldaos_common
*/
import "C"

type (
	// ContainerHandle is an opaque type used to represent a DAOS Container connection.
	// NB: A ContainerHandle contains the PoolHandle used to open the container.
	ContainerHandle struct {
		connHandle
		poolConnCleanup func()
		PoolHandle      *PoolHandle
	}
)

const (
	contHandleKey ctxHdlKey = "contHandle"
)

// chFromCtx retrieves the ContainerHandle from the supplied context, if available.
func chFromCtx(ctx context.Context) (*ContainerHandle, error) {
	if ctx == nil {
		return nil, errNilCtx
	}

	ch, ok := ctx.Value(contHandleKey).(*ContainerHandle)
	if !ok {
		return nil, errNoCtxHdl
	}

	if !ch.IsValid() {
		return nil, ErrInvalidContainerHandle
	}

	return ch, nil
}

// toCtx returns a new context with the ContainerHandle stashed in it.
// NB: Will panic if the context already has a different ContainerHandle stashed.
func (ch *ContainerHandle) toCtx(ctx context.Context) context.Context {
	if ch == nil {
		return ctx
	}

	stashed, _ := chFromCtx(ctx)
	if stashed != nil {
		if stashed.UUID() == ch.UUID() {
			return ctx
		}
		panic("attempt to stash different ContainerHandle in context")
	}

	return context.WithValue(ctx, contHandleKey, ch)
}

// IsValid returns true if the handle is valid.
func (ch *ContainerHandle) IsValid() bool {
	return ch != nil && ch.connHandle.IsValid() && ch.PoolHandle.IsValid()
}

// UUID returns the DAOS container's UUID.
func (ch *ContainerHandle) UUID() uuid.UUID {
	if ch == nil {
		return uuid.Nil
	}
	return ch.connHandle.UUID
}

func newContainerInfo(poolUUID, contUUID uuid.UUID, cInfo *C.daos_cont_info_t, props *daos.ContainerPropertyList) *daos.ContainerInfo {
	if cInfo == nil {
		return nil
	}

	ci := &daos.ContainerInfo{
		PoolUUID:        poolUUID,
		ContainerUUID:   contUUID,
		LatestSnapshot:  daos.HLC(cInfo.ci_lsnapshot),
		NumHandles:      uint32(cInfo.ci_nhandles),
		NumSnapshots:    uint32(cInfo.ci_nsnapshots),
		OpenTime:        daos.HLC(cInfo.ci_md_otime),
		CloseModifyTime: daos.HLC(cInfo.ci_md_mtime),
	}

	if props != nil {
		for _, prop := range props.Properties() {
			switch prop.Type {
			case daos.ContainerPropLayoutType:
				ci.Type = daos.ContainerLayout(prop.GetValue())
			case daos.ContainerPropLabel:
				ci.ContainerLabel = prop.GetString()
			case daos.ContainerPropRedunFactor:
				ci.RedundancyFactor = uint32(prop.GetValue())
			case daos.ContainerPropStatus:
				// Temporary; once we migrate the container properties into the API
				// we can use the prop stringer.
				statusInt := prop.GetValue()
				coStatus := C.struct_daos_co_status{}

				C.daos_prop_val_2_co_status(C.uint64_t(statusInt), &coStatus)
				switch coStatus.dcs_status {
				case C.DAOS_PROP_CO_HEALTHY:
					ci.Health = "HEALTHY"
				case C.DAOS_PROP_CO_UNCLEAN:
					ci.Health = "UNCLEAN"
				}
			}
		}
	}

	return ci
}

func (ch *ContainerHandle) String() string {
	return ch.PoolHandle.String() + fmt.Sprintf(":%s", ch.connHandle.String())
}

// Close performs a container close operation to release resources associated
// with the container handle.
func (ch *ContainerHandle) Close(ctx context.Context) error {
	if !ch.IsValid() {
		return ErrInvalidContainerHandle
	}
	logging.FromContext(ctx).Debugf("ContainerHandle.Close(%s)", ch)

	if err := daosError(daos_cont_close(ch.daosHandle)); err != nil {
		return errors.Wrap(err, "failed to close container")
	}
	ch.invalidate()

	// If a pool connection was made as part of the container open operation,
	// then we should disconnect from the pool now. This should be a no-op if
	// the pool connection was already established and stashed in the context.
	if ch.poolConnCleanup != nil {
		ch.poolConnCleanup()
	}

	return nil
}

// DestroyContainer calls ContainerDestroy() for the specified container, which must
// be served by the pool opened in the handle.
func (ph *PoolHandle) DestroyContainer(ctx context.Context, contID string, force bool) error {
	if !ph.IsValid() {
		return ErrInvalidPoolHandle
	}
	logging.FromContext(ctx).Debugf("PoolHandle.DestroyContainer(%s:%s:%t)", ph, contID, force)

	return ContainerDestroy(ph.toCtx(ctx), "", "", contID, force)
}

// ContainerDestroy destroys the specified container. Setting the force flag
// to true will destroy the container even if it has open handles.
func ContainerDestroy(ctx context.Context, sysName, poolID, contID string, force bool) error {
	var poolConn *PoolHandle
	var cleanup func()
	var err error

	poolConn, cleanup, err = getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadWrite)
	if err != nil {
		if errors.Is(err, daos.NoPermission) {
			// Even if we don't have pool-level write permissions, we may
			// have delete permissions at the container level.
			poolConn, cleanup, err = getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadOnly)
		}
		if err != nil {
			return err
		}
	}
	defer cleanup()
	logging.FromContext(ctx).Debugf("ContainerDestroy(%s:%s:%t)", poolConn, contID, force)

	cContID := C.CString(contID)
	defer freeString(cContID)
	if err := daosError(daos_cont_destroy(poolConn.daosHandle, cContID, goBool2int(force), nil)); err != nil {
		return errors.Wrapf(err, "failed to destroy container %q", contID)
	}

	return nil
}

func contFlags2PoolFlags(contFlags daos.ContainerOpenFlag) daos.PoolConnectFlag {
	var poolFlags daos.PoolConnectFlag

	if contFlags&daos.ContainerOpenFlagReadOnly != 0 {
		poolFlags |= daos.PoolConnectFlagReadOnly
	}
	if contFlags&daos.ContainerOpenFlagReadWrite != 0 {
		poolFlags |= daos.PoolConnectFlagReadWrite
	}
	if contFlags&daos.ContainerOpenFlagExclusive != 0 {
		poolFlags |= daos.PoolConnectFlagExclusive
	}

	return poolFlags
}

// OpenContainer calls ContainerOpen() for the specified container, which must
// be served by the pool opened in the handle.
func (ph *PoolHandle) OpenContainer(ctx context.Context, req ContainerOpenReq) (*ContainerOpenResp, error) {
	if !ph.IsValid() {
		return nil, ErrInvalidPoolHandle
	}

	return ContainerOpen(ph.toCtx(ctx), req)
}

type (
	// ContainerOpenReq specifies the container open parameters.
	ContainerOpenReq struct {
		ID      string
		Flags   daos.ContainerOpenFlag
		Query   bool
		SysName string
		PoolID  string
	}

	// ContainerOpenResp contains the handle and optional container information.
	ContainerOpenResp struct {
		Connection *ContainerHandle
		Info       *daos.ContainerInfo
	}
)

// ContainerOpen opens the container specified in the open request.
func ContainerOpen(ctx context.Context, req ContainerOpenReq) (_ *ContainerOpenResp, exitErr error) {
	if _, err := chFromCtx(ctx); err == nil {
		return nil, ErrContextHandleConflict
	}

	poolHdl, poolDisc, err := getPoolConn(ctx, req.SysName, req.PoolID, daos.PoolConnectFlagReadOnly)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to connect to pool %q", req.PoolID)
	}
	defer func() {
		// If the pool connection was made for this Open operation (i.e. was not already set in the context),
		// then we need to disconnect from the pool here in order to avoid leaking the pool handle.
		if exitErr != nil && !poolHdl.fromCtx {
			poolDisc()
		}
	}()
	logging.FromContext(ctx).Debugf("ContainerOpen(%s:%+v)", poolHdl, req)

	if req.ID == "" {
		return nil, errors.Wrap(daos.InvalidInput, "no container ID provided")
	}
	cContID := C.CString(req.ID)
	defer freeString(cContID)
	if req.Flags == 0 {
		req.Flags = daos.ContainerOpenFlagReadOnly
	}

	contHdl := &ContainerHandle{
		poolConnCleanup: poolDisc,
		PoolHandle:      poolHdl,
	}
	cContInfo := C.daos_cont_info_t{}

	if err := daosError(daos_cont_open(poolHdl.daosHandle, cContID, C.uint(req.Flags), &contHdl.connHandle.daosHandle, &cContInfo, nil)); err != nil {
		return nil, errors.Wrapf(err, "failed to open container %q", req.ID)
	}

	contHdl.connHandle.UUID, err = uuidFromC(cContInfo.ci_uuid)
	if err != nil {
		contHdl.poolConnCleanup = func() {} // Handled in defer.
		if dcErr := contHdl.Close(ctx); dcErr != nil {
			logging.FromContext(ctx).Error(dcErr.Error())
		}
		return nil, errors.New("unable to parse container UUID from open")
	}

	var contInfo *daos.ContainerInfo
	if req.Query {
		contInfo, err = contHdl.Query(ctx)
		if err != nil {
			contHdl.poolConnCleanup = func() {} // Handled in defer.
			if dcErr := contHdl.Close(ctx); dcErr != nil {
				logging.FromContext(ctx).Error(dcErr.Error())
			}
			return nil, errors.Wrapf(err, "failed to query container %q", req.ID)
		}
		contHdl.connHandle.Label = contInfo.ContainerLabel
	} else {
		if _, err := uuid.Parse(req.ID); err != nil {
			contHdl.connHandle.Label = req.ID
		}
		contInfo = newContainerInfo(poolHdl.UUID(), contHdl.UUID(), &cContInfo, nil)
	}

	logging.FromContext(ctx).Debugf("Opened Container %s)", contHdl)
	return &ContainerOpenResp{
		Connection: contHdl,
		Info:       contInfo,
	}, nil
}

// getContConn retrieves the ContainerHandle set in the context, if available,
// or tries to establish a new connection to the specified container.
func getContConn(ctx context.Context, sysName, poolID, contID string, flags daos.ContainerOpenFlag) (*ContainerHandle, func(), error) {
	nulCleanup := func() {}
	ch, err := chFromCtx(ctx)
	if err == nil {
		if contID != "" {
			return nil, nulCleanup, errors.Wrap(daos.InvalidInput, "ContainerHandle found in context with non-empty contID")
		}
		return ch, nulCleanup, nil
	}

	resp, err := ContainerOpen(ctx, ContainerOpenReq{
		ID:      contID,
		Flags:   flags,
		Query:   false,
		SysName: sysName,
		PoolID:  poolID,
	})
	if err != nil {
		return nil, nulCleanup, err
	}

	cleanup := func() {
		if err := resp.Connection.Close(ctx); err != nil {
			logging.FromContext(ctx).Error(err.Error())
		}
		resp.Connection.poolConnCleanup()
	}
	return resp.Connection, cleanup, nil
}

func containerQueryDFSAttrs(contConn *ContainerHandle) (*daos.POSIXAttributes, error) {
	var pa daos.POSIXAttributes
	var dfs *C.dfs_t
	var attr C.dfs_attr_t

	if !contConn.IsValid() {
		return nil, ErrInvalidContainerHandle
	}

	// NB: A relaxed-mode container may be mounted with the DFS_BALANCED
	// flag, but the reverse is not true. So we use DFS_BALANCED as the
	// default for querying the DFS attributes.
	var mountFlags C.int = C.O_RDONLY | C.DFS_BALANCED
	rc := dfs_mount(contConn.PoolHandle.daosHandle, contConn.daosHandle, mountFlags, &dfs)
	if err := dfsError(rc); err != nil {
		return nil, errors.Wrap(err, "failed to mount container")
	}

	rc = dfs_query(dfs, &attr)
	if err := dfsError(rc); err != nil {
		return nil, errors.Wrap(err, "failed to query container")
	}
	pa.ChunkSize = uint64(attr.da_chunk_size)
	pa.ObjectClass = daos.ObjectClass(attr.da_oclass_id)
	pa.DirObjectClass = daos.ObjectClass(attr.da_dir_oclass_id)
	pa.FileObjectClass = daos.ObjectClass(attr.da_file_oclass_id)
	pa.ConsistencyMode = uint32(attr.da_mode)
	pa.Hints = C.GoString(&attr.da_hints[0])

	if err := dfsError(dfs_umount(dfs)); err != nil {
		return nil, errors.Wrap(err, "failed to unmount container")
	}

	return &pa, nil
}

// QueryContainer calls ContainerQuery() for the specified container, which must
// be served by the pool opened in the handle.
func (ph *PoolHandle) QueryContainer(ctx context.Context, contID string) (*daos.ContainerInfo, error) {
	if !ph.IsValid() {
		return nil, ErrInvalidPoolHandle
	}
	logging.FromContext(ctx).Debugf("PoolHandle.QueryContainer(%s:%s)", ph, contID)

	return ContainerQuery(ph.toCtx(ctx), "", "", contID)
}

// Query calls ContainerQuery() for the container in the handle.
func (ch *ContainerHandle) Query(ctx context.Context) (*daos.ContainerInfo, error) {
	if !ch.IsValid() {
		return nil, ErrInvalidContainerHandle
	}
	logging.FromContext(ctx).Debugf("ContainerHandle.Query(%s)", ch)

	return ContainerQuery(ch.toCtx(ctx), "", "", "")
}

func contQuery(contConn *ContainerHandle, propList *daos.ContainerPropertyList) (*daos.ContainerInfo, error) {
	var cProps *C.daos_prop_t
	if propList != nil {
		cProps = (*C.daos_prop_t)(propList.ToPtr())
	}

	var dci C.daos_cont_info_t
	if err := daosError(daos_cont_query(contConn.daosHandle, &dci, cProps, nil)); err != nil {
		return nil, errors.Wrap(err, "failed to query container")
	}

	ciUUID, err := uuidFromC(dci.ci_uuid)
	if err != nil {
		return nil, errors.New("unable to parse container UUID from query")
	}
	if contConn.connHandle.UUID == uuid.Nil {
		contConn.connHandle.UUID = ciUUID
	} else if contConn.connHandle.UUID != ciUUID {
		return nil, errors.Errorf("queried container UUID != handle UUID: %s != %s", ciUUID, contConn.connHandle.UUID)
	}

	info := newContainerInfo(contConn.PoolHandle.UUID(), contConn.UUID(), &dci, propList)
	if info.Type == daos.ContainerLayoutPOSIX {
		posixAttrs, err := containerQueryDFSAttrs(contConn)
		if err != nil {
			return nil, errors.Wrap(err, "failed to query DFS attributes")
		}
		info.POSIXAttributes = posixAttrs
	}

	return info, nil
}

// ContainerQuery queries the specified container and returns its information.
func ContainerQuery(ctx context.Context, sysName, poolID, contID string) (*daos.ContainerInfo, error) {
	queryOpenFlags := daos.ContainerOpenFlagReadOnly | daos.ContainerOpenFlagForce | daos.ContainerOpenFlagReadOnlyMetadata
	contConn, cleanup, err := getContConn(ctx, sysName, poolID, contID, queryOpenFlags)
	if err != nil {
		return nil, err
	}
	defer cleanup()
	logging.FromContext(ctx).Debugf("ContainerQuery(%s)", contConn)

	propList, err := daos.AllocateContainerPropertyList(4)
	if err != nil {
		return nil, err
	}
	defer propList.Free()

	propList.MustAddEntryByType(daos.ContainerPropLayoutType)
	propList.MustAddEntryByType(daos.ContainerPropLabel)
	propList.MustAddEntryByType(daos.ContainerPropRedunFactor)
	propList.MustAddEntryByType(daos.ContainerPropStatus)

	return contQuery(contConn, propList)
}

// ListAttributes calls ContainerListAttributes() for the container in the handle.
func (ch *ContainerHandle) ListAttributes(ctx context.Context) ([]string, error) {
	if !ch.IsValid() {
		return nil, ErrInvalidContainerHandle
	}
	return ContainerListAttributes(ch.toCtx(ctx), "", "", "")
}

// ContainerListAttributes returns a list of user-definable container attribute names.
func ContainerListAttributes(ctx context.Context, sysName, poolID, contID string) ([]string, error) {
	contConn, cleanup, err := getContConn(ctx, sysName, poolID, contID, daos.ContainerOpenFlagReadOnlyMetadata)
	if err != nil {
		return nil, err
	}
	defer cleanup()
	logging.FromContext(ctx).Debugf("ContainerListAttributes(%s)", contConn)

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	return listDaosAttributes(contConn.daosHandle, contAttr)
}

// GetAttributes calls ContainerGetAttributes() for the container in the handle.
func (ch *ContainerHandle) GetAttributes(ctx context.Context, attrNames ...string) (daos.AttributeList, error) {
	if !ch.IsValid() {
		return nil, ErrInvalidContainerHandle
	}
	return ContainerGetAttributes(ch.toCtx(ctx), "", "", "", attrNames...)
}

// ContainerGetAttributes fetches the specified container attributes. If no
// attribute names are provided, all attributes are fetched.
func ContainerGetAttributes(ctx context.Context, sysName, poolID, contID string, names ...string) (daos.AttributeList, error) {
	contConn, cleanup, err := getContConn(ctx, sysName, poolID, contID, daos.ContainerOpenFlagReadOnlyMetadata)
	if err != nil {
		return nil, err
	}
	defer cleanup()
	logging.FromContext(ctx).Debugf("ContainerGetAttributes(%s:%v)", contConn, names)

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	return getDaosAttributes(contConn.daosHandle, contAttr, names)
}

// SetAttributes calls ContainerSetAttributes() for the container in the handle.
func (ch *ContainerHandle) SetAttributes(ctx context.Context, attrs ...*daos.Attribute) error {
	if !ch.IsValid() {
		return ErrInvalidContainerHandle
	}
	return ContainerSetAttributes(ch.toCtx(ctx), "", "", "", attrs...)
}

// ContainerSetAttributes sets the specified container attributes.
func ContainerSetAttributes(ctx context.Context, sysName, poolID, contID string, attrs ...*daos.Attribute) error {
	contConn, cleanup, err := getContConn(ctx, sysName, poolID, contID, daos.ContainerOpenFlagReadWrite)
	if err != nil {
		return err
	}
	defer cleanup()
	logging.FromContext(ctx).Debugf("ContainerSetAttributes(%s:%v)", contConn, attrs)

	if err := ctx.Err(); err != nil {
		return ctxErr(err)
	}

	return setDaosAttributes(contConn.daosHandle, contAttr, attrs)
}

// DeleteAttributes calls ContainerDeleteAttributes() for the container in the handle.
func (ch *ContainerHandle) DeleteAttributes(ctx context.Context, attrNames ...string) error {
	if !ch.IsValid() {
		return ErrInvalidContainerHandle
	}
	return ContainerDeleteAttributes(ch.toCtx(ctx), "", "", "", attrNames...)
}

// ContainerDeleteAttributes deletes the specified pool attributes.
func ContainerDeleteAttributes(ctx context.Context, sysName, poolID, contID string, attrNames ...string) error {
	contConn, cleanup, err := getContConn(ctx, sysName, poolID, contID, daos.ContainerOpenFlagReadWrite)
	if err != nil {
		return err
	}
	defer cleanup()
	logging.FromContext(ctx).Debugf("ContainerDeleteAttributes(%s:%+v)", contConn, attrNames)

	if err := ctx.Err(); err != nil {
		return ctxErr(err)
	}

	return delDaosAttributes(contConn.daosHandle, contAttr, attrNames)
}

// GetProperties calls ContainerGetProperties() for the container in the handle.
func (ch *ContainerHandle) GetProperties(ctx context.Context, propNames ...string) (*daos.ContainerPropertyList, error) {
	if !ch.IsValid() {
		return nil, ErrInvalidContainerHandle
	}
	return ContainerGetProperties(ch.toCtx(ctx), "", "", "", propNames...)
}

// ContainerGetProperties fetches the specified container properties. If no
// property names are provided, all properties are fetched.
func ContainerGetProperties(ctx context.Context, sysName, poolID, contID string, propNames ...string) (*daos.ContainerPropertyList, error) {
	contConn, cleanup, err := getContConn(ctx, sysName, poolID, contID, daos.ContainerOpenFlagReadOnlyMetadata)
	if err != nil {
		return nil, err
	}
	defer cleanup()

	logging.FromContext(ctx).Debugf("ContainerGetProperties(%s:%+v)", contConn, propNames)
	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	propList, err := daos.NewContainerPropertyList(propNames...)
	if err != nil {
		return nil, err
	}

	if _, err = contQuery(contConn, propList); err != nil {
		if !(len(propNames) == 0 && errors.Is(err, daos.NoPermission)) {
			propList.Free()
			return nil, err
		}

		// Special-case: If the request is for all properties, and it fails with -DER_NO_PERM,
		// retry the request without the acl property. Maintains backward-compatible behavior with
		// the previous daos tool implementation.
		newPropList, err := daos.NewContainerPropertyList()
		if err != nil {
			propList.Free()
			return nil, err
		}
		if err := newPropList.DelEntryByType(daos.ContainerPropACL); err != nil {
			propList.Free()
			newPropList.Free()
			return nil, err
		}
		propList.Free()
		propList = newPropList

		if _, err = contQuery(contConn, propList); err != nil {
			propList.Free()
			return nil, err
		}
	}

	return propList, nil
}

// SetProperties calls ContainerSetProperties() for the container in the handle.
func (ch *ContainerHandle) SetProperties(ctx context.Context, propList *daos.ContainerPropertyList) error {
	if !ch.IsValid() {
		return ErrInvalidContainerHandle
	}
	return ContainerSetProperties(ch.toCtx(ctx), "", "", "", propList)
}

// ContainerSetProperties sets the specified container properties.
func ContainerSetProperties(ctx context.Context, sysName, poolID, contID string, propList *daos.ContainerPropertyList) error {
	if propList == nil {
		return errors.New("nil property list")
	}

	contConn, cleanup, err := getContConn(ctx, sysName, poolID, contID, daos.ContainerOpenFlagReadOnlyMetadata)
	if err != nil {
		return err
	}
	defer cleanup()

	logging.FromContext(ctx).Debugf("ContainerSetProperties(%s:%+v)", contConn, propList)
	if err := ctx.Err(); err != nil {
		return ctxErr(err)
	}

	if err := daosError(daos_cont_set_prop(contConn.daosHandle, (*C.daos_prop_t)(propList.ToPtr()), nil)); err != nil {
		return errors.Wrap(err, "failed to set container properties")
	}

	return nil
}

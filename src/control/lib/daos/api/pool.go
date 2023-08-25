package api

import (
	"context"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <daos.h>
#include <daos_pool.h>
*/
import "C"

func newPoolInfo(cpi *C.daos_pool_info_t) *daos.PoolInfo {
	return &daos.PoolInfo{
		UUID:            uuid.Must(uuidFromC(cpi.pi_uuid)),
		TotalTargets:    uint32(cpi.pi_ntargets),
		ActiveTargets:   uint32(cpi.pi_ntargets - cpi.pi_ndisabled),
		TotalEngines:    uint32(cpi.pi_nnodes),
		DisabledTargets: uint32(cpi.pi_ndisabled),
		Version:         uint32(cpi.pi_map_ver),
		Leader:          uint32(cpi.pi_leader),
	}
}

type (
	PoolConnectReq struct {
		PoolID     string
		SystemName string
		Flags      uint
	}

	PoolConnectResp struct {
		PoolConnection *PoolHandle
		PoolInfo       *daos.PoolInfo
	}
)

func (ph *PoolHandle) CreateContainer(ctx context.Context, req ContainerCreateReq) (*daos.ContainerInfo, error) {
	return ContainerCreate(ctx, ph, req)
}

func (ph *PoolHandle) DestroyContainer(ctx context.Context, contID string, force bool) error {
	return ContainerDestroy(ctx, ph, contID, force)
}

func (ph *PoolHandle) OpenContainer(ctx context.Context, contID string, flags daos.ContainerOpenFlag) (*ContainerHandle, error) {
	return ContainerOpen(ctx, ph, contID, flags)
}

func (ph *PoolHandle) Query(ctx context.Context) (*daos.PoolInfo, error) {
	return PoolQuery(ctx, ph)
}

func (ph *PoolHandle) Disconnect(ctx context.Context) error {
	return PoolDisconnect(ctx, ph)
}

func (ph *PoolHandle) ListContainers(ctx context.Context, query bool) ([]*daos.ContainerInfo, error) {
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

func PoolConnect(ctx context.Context, req PoolConnectReq) (*PoolConnectResp, error) {
	log.Debugf("PoolConnect(%+v)", req)

	client, err := getApiClient(ctx)
	if err != nil {
		return nil, err
	}

	if req.PoolID == "" {
		return nil, errors.Wrap(daos.InvalidInput, "no pool ID provided")
	}

	var dpi C.daos_pool_info_t
	var poolConn PoolHandle

	cPoolID := C.CString(req.PoolID)
	defer freeString(cPoolID)
	cSys := C.CString(req.SystemName)
	defer freeString(cSys)

	if err := daosError(client.daos_pool_connect(cPoolID, cSys, C.uint(req.Flags), &poolConn.daosHandle, &dpi, nil)); err != nil {
		return nil, errors.Wrap(err, "failed to connect to pool")
	}

	poolInfo := newPoolInfo(&dpi)
	poolConn.UUID = poolInfo.UUID
	return &PoolConnectResp{
		PoolConnection: &poolConn,
		PoolInfo:       poolInfo,
	}, nil
}

func PoolQuery(ctx context.Context, poolConn *PoolHandle) (*daos.PoolInfo, error) {
	log.Debugf("PoolQuery(%s)", poolConn)

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	var cpi C.daos_pool_info_t
	if err := daosError(C.daos_pool_query(poolConn.daosHandle, nil, &cpi, nil, nil)); err != nil {
		return nil, err
	}

	return newPoolInfo(&cpi), nil
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
	if rc := m.getRc("daos_pool_disconnect", 0); rc != 0 {
		return rc
	}

	return 0
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

func PoolListContainers(ctx context.Context, poolConn *PoolHandle, query bool) ([]*daos.ContainerInfo, error) {
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

	out := make([]*daos.ContainerInfo, ncont)
	for i := range out {
		out[i] = new(daos.ContainerInfo)
		out[i].PoolUUID = poolConn.UUID
		out[i].UUID = uuid.Must(uuidFromC(dpciSlice[i].pci_uuid))
		out[i].Label = C.GoString(&dpciSlice[i].pci_label[0])
		contLabel := out[i].Label

		if query {
			contConn, err := ContainerOpen(ctx, poolConn, contLabel, daos.ContainerOpenFlagReadOnly)
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

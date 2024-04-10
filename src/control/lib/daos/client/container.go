package client

import (
	"context"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <fcntl.h>

#include <gurt/common.h>
#include <daos/common.h>
#include <daos_prop.h>
#include <daos_cont.h>
#include <daos_array.h>
#include <daos_fs.h>
#include <daos_uns.h>

#include "util.h"
*/
import "C"

type (
	ContainerLayout   uint16
	ContainerOpenFlag uint

	POSIXAttributes struct {
		ChunkSize       uint64      `json:"chunk_size,omitempty"`
		ObjectClass     ObjectClass `json:"object_class,omitempty"`
		DirObjectClass  ObjectClass `json:"dir_object_class,omitempty"`
		FileObjectClass ObjectClass `json:"file_object_class,omitempty"`
		ConsistencyMode uint32      `json:"cons_mode,omitempty"`
		Hints           string      `json:"hints,omitempty"`
	}

	ContainerInfo struct {
		PoolUUID         uuid.UUID        `json:"pool_uuid"`
		UUID             uuid.UUID        `json:"container_uuid"`
		Label            string           `json:"container_label,omitempty"`
		LatestSnapshot   uint64           `json:"latest_snapshot"`
		RedundancyFactor uint32           `json:"redundancy_factor"`
		NumHandles       uint32           `json:"num_handles"`
		NumSnapshots     uint32           `json:"num_snapshots"`
		OpenTime         uint64           `json:"open_time"`
		CloseModifyTime  uint64           `json:"close_modify_time"`
		Type             ContainerLayout  `json:"container_type"`
		POSIXAttributes  *POSIXAttributes `json:"posix_attributes,omitempty"`
	}
)

const (
	ContainerLayoutUnknown  ContainerLayout = C.DAOS_PROP_CO_LAYOUT_UNKNOWN
	ContainerLayoutPOSIX    ContainerLayout = C.DAOS_PROP_CO_LAYOUT_POSIX
	ContainerLayoutHDF5     ContainerLayout = C.DAOS_PROP_CO_LAYOUT_HDF5
	ContainerLayoutPython   ContainerLayout = C.DAOS_PROP_CO_LAYOUT_PYTHON
	ContainerLayoutSpark    ContainerLayout = C.DAOS_PROP_CO_LAYOUT_SPARK
	ContainerLayoutDatabase ContainerLayout = C.DAOS_PROP_CO_LAYOUT_DATABASE
	ContainerLayoutRoot     ContainerLayout = C.DAOS_PROP_CO_LAYOUT_ROOT
	ContainerLayoutSeismic  ContainerLayout = C.DAOS_PROP_CO_LAYOUT_SEISMIC
	ContainerLayoutMeteo    ContainerLayout = C.DAOS_PROP_CO_LAYOUT_METEO

	ContainerOpenFlagReadOnly  ContainerOpenFlag = C.DAOS_COO_RO
	ContainerOpenFlagReadWrite ContainerOpenFlag = C.DAOS_COO_RW
	ContainerOpenFlagExclusive ContainerOpenFlag = C.DAOS_COO_EX
	ContainerOpenFlagForce     ContainerOpenFlag = C.DAOS_COO_FORCE
	ContainerOpenFlagMdStats   ContainerOpenFlag = C.DAOS_COO_RO_MDSTATS
	ContainerOpenFlagEvict     ContainerOpenFlag = C.DAOS_COO_EVICT
	ContainerOpenFlagEvictAll  ContainerOpenFlag = C.DAOS_COO_EVICT_ALL
)

func (l *ContainerLayout) FromString(in string) error {
	cStr := C.CString(in)
	defer freeString(cStr)
	C.daos_parse_ctype(cStr, (*C.uint16_t)(l))

	if *l == ContainerLayoutUnknown {
		return errors.Errorf("unknown container layout %q", in)
	}

	return nil
}

func (l ContainerLayout) String() string {
	var cType [10]C.char
	C.daos_unparse_ctype(C.ushort(l), &cType[0])
	return C.GoString(&cType[0])
}

func newContainerInfo(cUUID C.uuid_t, cInfo *C.daos_cont_info_t, props propSlice) *ContainerInfo {
	contInfo := &ContainerInfo{
		UUID: uuid.Must(uuidFromC(cUUID)),
	}

	if cInfo != nil {
		contInfo.LatestSnapshot = uint64(cInfo.ci_lsnapshot)
		contInfo.NumHandles = uint32(cInfo.ci_nhandles)
		contInfo.NumSnapshots = uint32(cInfo.ci_nsnapshots)
		contInfo.OpenTime = uint64(cInfo.ci_md_otime)
		contInfo.CloseModifyTime = uint64(cInfo.ci_md_mtime)
	}

	for _, p := range props {
		switch p.dpe_type {
		case C.DAOS_PROP_CO_LAYOUT_TYPE:
			contInfo.Type = ContainerLayout(C.get_dpe_val(&p))
		case C.DAOS_PROP_CO_LABEL:
			contInfo.Label = C.GoString(C.get_dpe_str(&p))
		case C.DAOS_PROP_CO_REDUN_FAC:
			contInfo.RedundancyFactor = uint32(C.get_dpe_val(&p))
		}
	}

	return contInfo
}

func (b *daosClientBinding) duns_create_path(poolHdl C.daos_handle_t, path *C.char, attr *C.struct_duns_attr_t) C.int {
	return C.duns_create_path(poolHdl, path, attr)
}

func (m *mockApiClient) duns_create_path(_ C.daos_handle_t, _ *C.char, attr *C.struct_duns_attr_t) C.int {
	if rc := m.getRc("duns_create_path", 0); rc != 0 {
		return rc
	}

	contStr := m.cfg.ConnectedCont.String()
	for i := 0; i <= len(contStr); i++ {
		if i == len(contStr) {
			attr.da_cont[i] = C.char('\x00')
			break
		}
		attr.da_cont[i] = C.char(contStr[i])
	}
	return 0
}

func dunsContainerCreate(client apiClient, poolConn *PoolHandle, req ContainerCreateReq, cInfo *ContainerInfo) error {
	var attr C.struct_duns_attr_t

	if req.UNSPath == "" {
		return errors.Wrap(daos.InvalidInput, "UNS Path is required")
	}
	cPath := C.CString(req.UNSPath)
	defer freeString(cPath)

	if req.Type == ContainerLayoutUnknown {
		return errors.Wrap(daos.InvalidInput, "container type is required for UNS")
	}
	attr.da_type = C.ushort(req.Type)

	if req.POSIXAttributes != nil {
		if req.POSIXAttributes.ChunkSize > 0 {
			attr.da_chunk_size = C.uint64_t(req.POSIXAttributes.ChunkSize)
		}
		if req.POSIXAttributes.ObjectClass != 0 {
			attr.da_oclass_id = C.uint32_t(req.POSIXAttributes.ObjectClass)
		}
		if req.POSIXAttributes.DirObjectClass != 0 {
			attr.da_dir_oclass_id = C.uint32_t(req.POSIXAttributes.DirObjectClass)
		}
		if req.POSIXAttributes.FileObjectClass != 0 {
			attr.da_file_oclass_id = C.uint32_t(req.POSIXAttributes.FileObjectClass)
		}
		if req.POSIXAttributes.Hints != "" {
			cHint := C.CString(req.POSIXAttributes.Hints)
			defer freeString(cHint)
			C.strncpy(&attr.da_hints[0], cHint, C.DAOS_CONT_HINT_MAX_LEN-1)
		}
	}

	props, cleanup, err := req.Properties.cArrPtr()
	if err != nil {
		return err
	}
	defer cleanup()
	attr.da_props = props

	rc := client.duns_create_path(poolConn.daosHandle, cPath, &attr)
	if err := daosError(C.daos_errno2der(rc)); err != nil {
		return err
	}
	idStr := C.GoString(&attr.da_cont[0])
	if contUUID, err := uuid.Parse(idStr); err == nil {
		cInfo.UUID = contUUID
	} else {
		cInfo.Label = idStr
	}

	return err
}

func (b *daosClientBinding) dfs_cont_create(poolHdl C.daos_handle_t, cUUID *C.uuid_t, attr *C.dfs_attr_t, contHdl *C.daos_handle_t, dfs **C.dfs_t) C.int {
	return C.dfs_cont_create(poolHdl, cUUID, attr, contHdl, dfs)
}

func (m *mockApiClient) dfs_cont_create(_ C.daos_handle_t, cUUID *C.uuid_t, _ *C.dfs_attr_t, _ *C.daos_handle_t, _ **C.dfs_t) C.int {
	if rc := m.getRc("dfs_cont_create", 0); rc != 0 {
		return rc
	}

	if err := copyUUID(cUUID, m.cfg.ConnectedCont); err != nil {
		panic(err)
	}
	return 0
}

func posixContainerCreate(client apiClient, poolConn *PoolHandle, req ContainerCreateReq, cInfo *ContainerInfo) error {
	var attr C.dfs_attr_t

	if req.POSIXAttributes == nil {
		return errors.Wrap(daos.InvalidInput, "POSIX attributes are required if container type is POSIX")
	}
	if req.POSIXAttributes.ChunkSize > 0 {
		attr.da_chunk_size = C.uint64_t(req.POSIXAttributes.ChunkSize)
	}
	if req.POSIXAttributes.ObjectClass != 0 {
		attr.da_oclass_id = C.uint32_t(req.POSIXAttributes.ObjectClass)
	}
	if req.POSIXAttributes.DirObjectClass != 0 {
		attr.da_dir_oclass_id = C.uint32_t(req.POSIXAttributes.DirObjectClass)
	}
	if req.POSIXAttributes.FileObjectClass != 0 {
		attr.da_file_oclass_id = C.uint32_t(req.POSIXAttributes.FileObjectClass)
	}
	if req.POSIXAttributes.ConsistencyMode != 0 {
		attr.da_mode = C.uint32_t(req.POSIXAttributes.ConsistencyMode)
	}
	if req.POSIXAttributes.Hints != "" {
		cHint := C.CString(req.POSIXAttributes.Hints)
		defer freeString(cHint)
		C.strncpy(&attr.da_hints[0], cHint, C.DAOS_CONT_HINT_MAX_LEN-1)
	}

	props, cleanup, err := req.Properties.cArrPtr()
	if err != nil {
		return err
	}
	defer cleanup()
	attr.da_props = props

	var cUUID C.uuid_t
	rc := client.dfs_cont_create(poolConn.daosHandle, &cUUID, &attr, nil, nil)
	if err := daosError(C.daos_errno2der(rc)); err != nil {
		return err
	}
	cInfo.UUID = uuid.Must(uuidFromC(cUUID))

	return nil
}

func (b *daosClientBinding) daos_cont_create(poolHdl C.daos_handle_t, cUUID *C.uuid_t, props *C.daos_prop_t, ev *C.struct_daos_event) C.int {
	return C.daos_cont_create(poolHdl, cUUID, props, ev)
}

func (m *mockApiClient) daos_cont_create(_ C.daos_handle_t, cUUID *C.uuid_t, _ *C.daos_prop_t, _ *C.struct_daos_event) C.int {
	if rc := m.getRc("daos_cont_create", 0); rc != 0 {
		return rc
	}

	if err := copyUUID(cUUID, m.cfg.ConnectedCont); err != nil {
		panic(err)
	}
	return 0
}

func defaultContainerCreate(client apiClient, poolConn *PoolHandle, req ContainerCreateReq, cInfo *ContainerInfo) error {
	props, cleanup, err := req.Properties.cArrPtr()
	if err != nil {
		return err
	}
	defer cleanup()

	var cUUID C.uuid_t
	rc := client.daos_cont_create(poolConn.daosHandle, &cUUID, props, nil)
	if err := daosError(rc); err != nil {
		return err
	}
	cInfo.UUID = uuid.Must(uuidFromC(cUUID))

	return nil
}

type ContainerCreateReq struct {
	Label           string
	UNSPath         string
	ACL             *control.AccessControlList
	Type            ContainerLayout
	POSIXAttributes *POSIXAttributes
	Properties      ContainerPropertySet
}

func ContainerCreate(ctx context.Context, poolConn *PoolHandle, req ContainerCreateReq) (*ContainerInfo, error) {
	log.Debugf("ContainerCreate(%+v)", req)

	if poolConn == nil {
		return nil, errInvalidPoolHandle
	}

	client, err := getApiClient(ctx)
	if err != nil {
		return nil, err
	}

	if req.Properties == nil {
		req.Properties = make(ContainerPropertySet)
	}

	if req.ACL != nil {
		if len(req.ACL.Entries) > 0 {
			cACL, cleanup, err := aclToC(req.ACL)
			if err != nil {
				return nil, err
			}
			defer cleanup()

			var aclEntry C.struct_daos_prop_entry
			aclEntry.dpe_type = C.DAOS_PROP_CO_ACL
			C.set_dpe_val_ptr(&aclEntry, unsafe.Pointer(cACL))
			req.Properties.addEntry("acl", &aclEntry)
		}

		if req.ACL.OwnerGroup != "" {
			var groupEntry C.struct_daos_prop_entry
			groupEntry.dpe_type = C.DAOS_PROP_CO_OWNER_GROUP
			cGrpStr := C.CString(req.ACL.OwnerGroup)
			defer freeString(cGrpStr)
			C.set_dpe_str(&groupEntry, cGrpStr)
			req.Properties.addEntry("ownerGroup", &groupEntry)
		}
	}

	if req.Type != ContainerLayoutUnknown {
		var typeEntry C.struct_daos_prop_entry
		typeEntry.dpe_type = C.DAOS_PROP_CO_LAYOUT_TYPE
		C.set_dpe_val(&typeEntry, C.ulong(req.Type))
		req.Properties.addEntry("type", &typeEntry)
	}

	var cInfo ContainerInfo
	var createErr error
	if req.UNSPath != "" {
		createErr = dunsContainerCreate(client, poolConn, req, &cInfo)
	} else {
		switch req.Type {
		case ContainerLayoutPOSIX:
			createErr = posixContainerCreate(client, poolConn, req, &cInfo)
		default:
			createErr = defaultContainerCreate(client, poolConn, req, &cInfo)
		}
	}

	if createErr != nil {
		return nil, errors.Wrap(createErr, "failed to create container")
	}
	if cInfo.Label == "" {
		cInfo.Label = req.Label
	}
	if cInfo.Type == ContainerLayoutUnknown {
		cInfo.Type = req.Type
	}
	cInfo.POSIXAttributes = req.POSIXAttributes

	log.Debugf("created container %s", cInfo.UUID)

	return &cInfo, nil
}

func (ch *ContainerHandle) Destroy(ctx context.Context, force bool) error {
	return ContainerDestroy(ctx, ch.PoolHandle, ch.UUID.String(), force)
}

func (b *daosClientBinding) daos_cont_destroy(poolHdl C.daos_handle_t, contID *C.char, force C.int, ev *C.struct_daos_event) C.int {
	return C.daos_cont_destroy(poolHdl, contID, force, ev)
}

func (m *mockApiClient) daos_cont_destroy(poolHdl C.daos_handle_t, contID *C.char, force C.int, ev *C.struct_daos_event) C.int {
	return m.getRc("daos_cont_destroy", 0)
}

func ContainerDestroy(ctx context.Context, poolConn *PoolHandle, contID string, force bool) error {
	log.Debugf("ContainerDestroy(%s:%s:%t)", poolConn, contID, force)

	if poolConn == nil {
		return errInvalidPoolHandle
	}

	client, err := getApiClient(ctx)
	if err != nil {
		return err
	}

	/*if req.UNSPath != "" {
		if req.PoolConn != nil || req.ContainerID != "" {
			return errors.Wrap(InvalidInput, "UNSPath should not be used with PoolConn or ContainerID")
		}

		var err error
		poolID, contID, err = ResolveUNSPath(req.UNSPath)
		if err != nil {
			return errors.Wrapf(err, "failed to resolve UNS path %q", req.UNSPath)
		}

		// If we're resolving via a mounted container, then we can
		// reasonably assume that the caller wants to destroy it
		// even though it's busy.
		req.Force = true
	}*/

	/*} else if err == nil {
		if !req.Force {
			return errors.Wrap(Busy, "container is open and Force not set")
		}
		if err := daosError(C.daos_cont_close(contConn.daosHandle, nil)); err != nil {
			return errors.Wrap(err, "failed to close container before destroy")
		}
	}*/

	cID := C.CString(contID)
	defer freeString(cID)

	rc := client.daos_cont_destroy(poolConn.daosHandle, cID, goBool2int(force), nil)
	if err := daosError(rc); err != nil {
		return errors.Wrap(err, "failed to destroy container")
	}

	return nil
}

func (b *daosClientBinding) daos_cont_open(poolHdl C.daos_handle_t, contID *C.char, flags C.uint, contHdl *C.daos_handle_t, info *C.daos_cont_info_t, ev *C.struct_daos_event) C.int {
	return C.daos_cont_open(poolHdl, contID, flags, contHdl, info, ev)
}

func (m *mockApiClient) daos_cont_open(_ C.daos_handle_t, _ *C.char, _ C.uint, contHdl *C.daos_handle_t, info *C.daos_cont_info_t, _ *C.struct_daos_event) C.int {
	if rc := m.getRc("daos_cont_open", 0); rc != 0 {
		return rc
	}

	contHdl.cookie = 42
	if err := copyUUID(&info.ci_uuid, m.cfg.ConnectedCont); err != nil {
		panic(err)
	}
	return 0
}

func ContainerOpen(ctx context.Context, poolConn *PoolHandle, contID string, flags ...ContainerOpenFlag) (*ContainerHandle, error) {
	log.Debugf("ContainerOpen(%s:%s)", poolConn, contID)

	client, err := getApiClient(ctx)
	if err != nil {
		return nil, err
	}

	cID := C.CString(contID)
	defer freeString(cID)

	var contConn ContainerHandle
	var contInfo C.daos_cont_info_t

	if len(flags) == 0 {
		flags = append(flags, ContainerOpenFlagReadOnly)
	}
	var cFlags C.uint
	for _, flag := range flags {
		cFlags |= C.uint(flag)
	}
	err = daosError(client.daos_cont_open(poolConn.daosHandle, cID,
		cFlags, &contConn.daosHandle, &contInfo, nil))
	if err != nil {
		return nil, err
	}

	contConn.PoolHandle = poolConn
	contConn.UUID, err = uuidFromC(contInfo.ci_uuid)
	if err != nil {
		if dErr := daosError(C.daos_cont_close(contConn.daosHandle, nil)); dErr != nil {
			return nil, errors.Wrapf(dErr, "while handling %s", err)
		}
		return nil, err
	}

	return &contConn, nil
}

func (b *daosClientBinding) daos_cont_close(contHdl C.daos_handle_t, ev *C.struct_daos_event) C.int {
	// Hack for NLT fault injection testing: If the rc
	// is -DER_NOMEM, retry once in order to actually
	// shut down and release resources.
	rc := C.daos_cont_close(contHdl, ev)
	if rc == -C.DER_NOMEM {
		rc = C.daos_cont_close(contHdl, ev)
	}

	return rc
}

func (m *mockApiClient) daos_cont_close(contHdl C.daos_handle_t, _ *C.struct_daos_event) C.int {
	if rc := m.getRc("daos_cont_close", 0); rc != 0 {
		return rc
	}

	contHdl.cookie = 0
	return 0
}

func (ch *ContainerHandle) Close(ctx context.Context) error {
	return ContainerClose(ctx, ch)
}

func ContainerClose(ctx context.Context, contConn *ContainerHandle) error {
	log.Debugf("ContainerClose(%s)", contConn)

	client, err := getApiClient(ctx)
	if err != nil {
		return err
	}

	if err := daosError(client.daos_cont_close(contConn.daosHandle, nil)); err != nil {
		return err
	}
	contConn.invalidate()

	return nil
}

func containerQueryDFSAttrs(ctx context.Context, poolConn *PoolHandle, contConn *ContainerHandle) (*POSIXAttributes, error) {
	var pa POSIXAttributes
	var dfs *C.dfs_t
	var attr C.dfs_attr_t

	rc := C.dfs_mount(poolConn.daosHandle, contConn.daosHandle, C.O_RDONLY, &dfs)
	if err := dfsError(rc); err != nil {
		return nil, errors.Wrap(err, "failed to mount container")
	}

	rc = C.dfs_query(dfs, &attr)
	if err := dfsError(rc); err != nil {
		return nil, errors.Wrap(err, "failed to query container")
	}
	pa.ObjectClass = ObjectClass(attr.da_oclass_id)
	pa.DirObjectClass = ObjectClass(attr.da_dir_oclass_id)
	pa.FileObjectClass = ObjectClass(attr.da_file_oclass_id)
	pa.Hints = C.GoString(&attr.da_hints[0])
	pa.ChunkSize = uint64(attr.da_chunk_size)

	if err := dfsError(C.dfs_umount(dfs)); err != nil {
		return nil, errors.Wrap(err, "failed to unmount container")
	}

	return &pa, nil
}

func (b *daosClientBinding) daos_cont_query(contHdl C.daos_handle_t, dci *C.daos_cont_info_t, props *C.daos_prop_t, ev *C.struct_daos_event) C.int {
	return C.daos_cont_query(contHdl, dci, props, ev)
}

func (m *mockApiClient) daos_cont_query(contHdl C.daos_handle_t, dci *C.daos_cont_info_t, props *C.daos_prop_t, ev *C.struct_daos_event) C.int {
	if rc := m.getRc("daos_cont_query", 0); rc != 0 {
		return rc
	}

	return 0
}

func (ch *ContainerHandle) Query(ctx context.Context) (*ContainerInfo, error) {
	return ContainerQuery(ctx, ch)
}

func ContainerQuery(ctx context.Context, contConn *ContainerHandle) (*ContainerInfo, error) {
	log.Debugf("ContainerQuery(%s)", contConn)

	client, err := getApiClient(ctx)
	if err != nil {
		return nil, err
	}

	props, entries, err := allocProps(3)
	if err != nil {
		return nil, err
	}
	defer freeProps(props)

	entries[0].dpe_type = C.DAOS_PROP_CO_LAYOUT_TYPE
	props.dpp_nr++
	entries[1].dpe_type = C.DAOS_PROP_CO_LABEL
	props.dpp_nr++
	entries[2].dpe_type = C.DAOS_PROP_CO_REDUN_FAC
	props.dpp_nr++

	var dci C.daos_cont_info_t
	if err := daosError(client.daos_cont_query(contConn.daosHandle, &dci, props, nil)); err != nil {
		return nil, errors.Wrap(err, "failed to query container")
	}

	info := newContainerInfo(dci.ci_uuid, &dci, entries)
	info.PoolUUID = contConn.PoolHandle.UUID

	if info.Type == ContainerLayoutPOSIX {
		posixAttrs, err := containerQueryDFSAttrs(ctx, contConn.PoolHandle, contConn)
		if err != nil {
			return nil, errors.Wrap(err, "failed to query DFS attributes")
		}
		info.POSIXAttributes = posixAttrs
	}

	return info, nil
}

func containerGetACLProps(contConn *ContainerHandle, propSet ContainerPropertySet) (func(), error) {
	expPropNr := 3 // ACL, user, group

	var props *C.daos_prop_t
	rc := C.daos_cont_get_acl(contConn.daosHandle, &props, nil)
	if err := daosError(rc); err != nil {
		return nil, err
	}
	if props.dpp_nr != C.uint(expPropNr) {
		return nil, errors.Errorf("invalid number of ACL props (%d != %d)",
			props.dpp_nr, expPropNr)
	}
	cleanup := func() { C.daos_prop_free(props) }

	entries := createPropSlice(props, expPropNr)
	for i, entry := range entries {
		switch entry.dpe_type {
		case C.DAOS_PROP_CO_ACL:
			propSet.addProp("acl", &ContainerProperty{
				entry:       &entries[i],
				handler:     &contPropHandler{toString: aclStringer},
				Name:        "acl",
				Description: "Access Control List",
			})
		case C.DAOS_PROP_CO_OWNER:
			propSet.addProp("user", &ContainerProperty{
				entry:       &entries[i],
				handler:     &contPropHandler{toString: strValStringer},
				Name:        "user",
				Description: "User",
			})
		case C.DAOS_PROP_CO_OWNER_GROUP:
			propSet.addProp("group", &ContainerProperty{
				entry:       &entries[i],
				handler:     &contPropHandler{toString: strValStringer},
				Name:        "group",
				Description: "Group",
			})
		}
	}

	return cleanup, nil
}

func (ch *ContainerHandle) GetProperties(ctx context.Context, names ...string) (ContainerPropertySet, error) {
	return ContainerGetProperties(ctx, ch, names...)
}

func ContainerGetProperties(ctx context.Context, contConn *ContainerHandle, names ...string) (ContainerPropertySet, error) {
	log.Debugf("ContainerGetProperties(%s:%s)", contConn, names)

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	if len(names) == 0 {
		names = ContainerProperties.Keys()
	}
	props, entries, err := allocProps(len(names))
	if err != nil {
		return nil, err
	}
	defer func() { C.daos_prop_free(props) }()

	propSet := make(ContainerPropertySet)
	for _, name := range names {
		var hdlr *contPropHandler
		hdlr, err = ContainerProperties.get(name)
		if err != nil {
			return nil, err
		}
		entries[props.dpp_nr].dpe_type = C.uint(hdlr.dpeType)

		propSet[name] = &ContainerProperty{
			entry:       &entries[props.dpp_nr],
			handler:     hdlr,
			Name:        name,
			Description: hdlr.shortDesc,
		}

		props.dpp_nr++
	}

	rc := C.daos_cont_query(contConn.daosHandle, nil, props, nil)
	if err = daosError(rc); err != nil {
		return nil, err
	}

	// FIXME: These ACL properties are synthesized on the fly and added
	// into the results for an all-properties request. There is no way
	// to query for them directly, however, as they do not have handlers
	// configured in the property handler map.
	//
	// TODO (mjmac): Why can't/shouldn't these be set/get like other
	// properties? I suspect that they could, and we could phase out
	// direct use of the dedicated ACL management APIs in favor of calling
	// them from the property management logic.
	if len(names) == len(ContainerProperties) {
		aclCleanup, err := containerGetACLProps(contConn, propSet)
		if err != nil {
			// Not a fatal error if we don't have permission to read the ACL.
			if err != daos.NoPermission {
				return nil, err
			}
		} else {
			defer aclCleanup()
		}
	}

	// Copy the entries to Go-managed memory so that we can free the C memory here.
	for _, prop := range propSet {
		var tmp C.struct_daos_prop_entry
		if err := dupePropEntry(prop.entry, &tmp); err != nil {
			return nil, err
		}
		prop.entry = &tmp
	}

	return propSet, nil
}

func (ch *ContainerHandle) SetProperties(ctx context.Context, propSet ContainerPropertySet) error {
	return ContainerSetProperties(ctx, ch, propSet)
}

func ContainerSetProperties(ctx context.Context, contConn *ContainerHandle, propSet ContainerPropertySet) error {
	log.Debugf("ContainerSetProperties(%s:%v)", contConn, propSet)

	if err := ctx.Err(); err != nil {
		return ctxErr(err)
	}

	if len(propSet) == 0 {
		return errors.Wrap(daos.InvalidInput, "no properties to set")
	}

	cProps, cleanup, err := propSet.cArrPtr()
	if err != nil {
		return err
	}
	defer cleanup()

	if err := daosError(C.daos_cont_set_prop(contConn.daosHandle, cProps, nil)); err != nil {
		return err
	}

	return nil
}

func (ch *ContainerHandle) ListAttributes(ctx context.Context) ([]string, error) {
	return ContainerListAttributes(ctx, ch)
}

func ContainerListAttributes(ctx context.Context, contConn *ContainerHandle) ([]string, error) {
	log.Debugf("ContainerListAttributes(%s)", contConn)

	if contConn == nil {
		return nil, errInvalidContainerHandle
	}

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	return listDaosAttributes(contConn.daosHandle, contAttr)
}

func (ch *ContainerHandle) GetAttributes(ctx context.Context, names ...string) ([]*daos.Attribute, error) {
	return ContainerGetAttributes(ctx, ch, names...)
}

func ContainerGetAttributes(ctx context.Context, contConn *ContainerHandle, names ...string) ([]*daos.Attribute, error) {
	log.Debugf("ContainerGetAttributes(%s:%v)", contConn, names)

	if contConn == nil {
		return nil, errInvalidContainerHandle
	}

	if err := ctx.Err(); err != nil {
		return nil, ctxErr(err)
	}

	return getDaosAttributes(contConn.daosHandle, contAttr, names)
}

func (ch *ContainerHandle) SetAttributes(ctx context.Context, attrs []*daos.Attribute) error {
	return ContainerSetAttributes(ctx, ch, attrs)
}

func ContainerSetAttributes(ctx context.Context, contConn *ContainerHandle, attrs []*daos.Attribute) error {
	log.Debugf("ContainerSetAttributes(%s:%v)", contConn, attrs)

	if contConn == nil {
		return errInvalidContainerHandle
	}

	if err := ctx.Err(); err != nil {
		return ctxErr(err)
	}

	return setDaosAttributes(contConn.daosHandle, contAttr, attrs)
}

func (ch *ContainerHandle) DeleteAttribute(ctx context.Context, attrName string) error {
	return ContainerDeleteAttribute(ctx, ch, attrName)
}

func ContainerDeleteAttribute(ctx context.Context, contConn *ContainerHandle, attrName string) error {
	log.Debugf("ContainerDeleteAttribute(%s:%s)", contConn, attrName)

	if contConn == nil {
		return errInvalidContainerHandle
	}

	if err := ctx.Err(); err != nil {
		return ctxErr(err)
	}

	return delDaosAttribute(contConn.daosHandle, contAttr, attrName)
}

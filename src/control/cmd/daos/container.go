//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"unsafe"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
	apiMocks "github.com/daos-stack/daos/src/control/lib/daos/client/mocks"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
#include "util.h"

static int
set_dfs_path(struct cmd_args_s *ap, char *name, char *rel_path)
{
	if (ap == NULL)
		return -DER_INVAL;

	if (name) {
		if (rel_path)
			asprintf(&ap->dfs_path, "%s/%s", rel_path, name);
		else
			asprintf(&ap->dfs_path, "/%s", name);
	} else {
		if (rel_path)
			ap->dfs_path = strndup(rel_path, PATH_MAX);
		else
			ap->dfs_path = strndup("/", 1);
	}
	if (ap->dfs_path == NULL)
		return -DER_NOMEM;

	return 0;
}
*/
import "C"

type (
	wrappedContHandle struct {
		*daosAPI.ContainerHandle
	}

	wrappedContMock struct {
		*apiMocks.ContConn
	}

	contConnection interface {
		UUID() uuid.UUID
		PoolUUID() uuid.UUID
		Pointer() unsafe.Pointer
		Close(context.Context) error
		Query(context.Context) (*daosAPI.ContainerInfo, error)
		ListAttributes(context.Context) ([]string, error)
		GetAttributes(context.Context, ...string) ([]*daos.Attribute, error)
		SetAttributes(context.Context, []*daos.Attribute) error
		DeleteAttribute(context.Context, string) error
		GetProperties(context.Context, ...string) (daosAPI.ContainerPropertySet, error)
		SetProperties(context.Context, daosAPI.ContainerPropertySet) error
	}
)

func (wph *wrappedContHandle) UUID() uuid.UUID {
	return wph.ContainerHandle.UUID
}

func (wph *wrappedContHandle) PoolUUID() uuid.UUID {
	return wph.ContainerHandle.PoolHandle.UUID
}

func (wcm *wrappedContMock) Pointer() unsafe.Pointer {
	return unsafe.Pointer(&C.daos_handle_t{})
}

var (
	_ contConnection = (*wrappedContHandle)(nil)
)

type containerCmd struct {
	Create      containerCreateCmd      `command:"create" description:"create a container"`
	Link        containerLinkCmd        `command:"link" description:"create a UNS link to an existing container"`
	List        containerListCmd        `command:"list" alias:"ls" description:"list all containers in pool"`
	Destroy     containerDestroyCmd     `command:"destroy" description:"destroy a container"`
	ListObjects containerListObjectsCmd `command:"list-objects" alias:"list-obj" description:"list all objects in container"`
	Query       containerQueryCmd       `command:"query" description:"query a container"`
	Stat        containerStatCmd        `command:"stat" description:"get container statistics"`
	Clone       containerCloneCmd       `command:"clone" description:"clone a container"`
	Check       containerCheckCmd       `command:"check" description:"check objects' consistency in a container"`
	Evict       containerEvictCmd       `command:"evict" description:"Evict open handles on a container"`

	ListAttributes  containerListAttrsCmd `command:"list-attr" alias:"list-attrs" alias:"lsattr" description:"list container user-defined attributes"`
	DeleteAttribute containerDelAttrCmd   `command:"del-attr" alias:"delattr" description:"delete container user-defined attribute"`
	GetAttribute    containerGetAttrCmd   `command:"get-attr" alias:"getattr" description:"get container user-defined attribute"`
	SetAttribute    containerSetAttrCmd   `command:"set-attr" alias:"setattr" description:"set container user-defined attribute"`

	GetProperty containerGetPropCmd `command:"get-prop" alias:"getprop" description:"get container properties"`
	SetProperty containerSetPropCmd `command:"set-prop" alias:"setprop" description:"set container properties"`

	GetACL       containerGetACLCmd       `command:"get-acl" description:"get a container's ACL"`
	OverwriteACL containerOverwriteACLCmd `command:"overwrite-acl" alias:"replace" description:"replace a container's ACL"`
	UpdateACL    containerUpdateACLCmd    `command:"update-acl" description:"update a container's ACL"`
	DeleteACL    containerDeleteACLCmd    `command:"delete-acl" description:"delete a container's ACL"`
	SetOwner     containerSetOwnerCmd     `command:"set-owner" alias:"chown" description:"change ownership for a container"`

	CreateSnapshot  containerSnapCreateCmd       `command:"create-snap" alias:"snap" description:"create container snapshot"`
	DestroySnapshot containerSnapDestroyCmd      `command:"destroy-snap" description:"destroy container snapshot"`
	ListSnapshots   containerSnapListCmd         `command:"list-snap" alias:"list-snaps" description:"list container snapshots"`
	Rollback        containerSnapshotRollbackCmd `command:"rollback" description:"roll back container to specified snapshot"`
}

type containerBaseCmd struct {
	poolBaseCmd

	contConn    contConnection
	cContHandle C.daos_handle_t
}

func (cmd *containerBaseCmd) contUUIDPtr() *C.uchar {
	contUUID := cmd.contUUID()
	return (*C.uchar)(unsafe.Pointer(&contUUID[0]))
}

func (cmd *containerBaseCmd) contUUID() uuid.UUID {
	if cmd.contConn == nil || cmd.contConn.UUID() == uuid.Nil {
		return uuid.Nil
	}

	return cmd.contConn.UUID()
}

func (cmd *containerBaseCmd) openContainer(contID string, openFlags daosAPI.ContainerOpenFlag) error {
	openFlags |= C.DAOS_COO_FORCE
	if (openFlags & C.DAOS_COO_RO) != 0 {
		openFlags |= C.DAOS_COO_RO_MDSTATS
	}

	cmd.Debugf("opening container: %s", contID)
	if mpc := apiMocks.GetPoolConn(cmd.daosCtx); mpc != nil {
		mcc, err := mpc.OpenContainer(cmd.daosCtx, contID, openFlags)
		if err != nil {
			return err
		}
		cmd.contConn = &wrappedContMock{mcc}
	} else {
		conn, err := cmd.poolConn.OpenContainer(cmd.daosCtx, contID, openFlags)
		if err != nil {
			return err
		}
		cmd.contConn = conn
	}

	// NB: This is somewhat dodgy... Phase out external handles ASAP!
	cmd.cContHandle = *conn2HdlPtr(cmd.contConn)

	return nil
}

func (cmd *containerBaseCmd) closeContainer() {
	cmd.Debugf("closing container: %s", cmd.contUUID())
	if err := cmd.contConn.Close(cmd.daosCtx); err != nil {
		cmd.Errorf("container close failed: %s", err)
	}
}

func (cmd *containerBaseCmd) queryContainer() (*daosAPI.ContainerInfo, error) {
	return cmd.contConn.Query(cmd.daosCtx)
}

func (cmd *containerBaseCmd) connectPool(flags daosAPI.PoolConnectFlag, ap *C.struct_cmd_args_s) (func(), error) {
	if err := cmd.poolBaseCmd.connectPool(flags); err != nil {
		return nil, err
	}

	if ap != nil {
		ap.pool = cmd.poolBaseCmd.cPoolHandle
		if err := copyUUID(&ap.p_uuid, cmd.poolUUID()); err != nil {
			cmd.disconnectPool()
			return nil, err
		}
	}

	return cmd.disconnectPool, nil
}

type containerCreateCmd struct {
	containerBaseCmd

	Type            ContTypeFlag         `long:"type" short:"t" description:"container type"`
	Path            string               `long:"path" short:"p" description:"container namespace path"`
	ChunkSize       ChunkSizeFlag        `long:"chunk-size" short:"z" description:"container chunk size"`
	ObjectClass     ObjClassFlag         `long:"oclass" short:"o" description:"default object class"`
	DirObjectClass  ObjClassFlag         `long:"dir-oclass" short:"d" description:"default directory object class"`
	FileObjectClass ObjClassFlag         `long:"file-oclass" short:"f" description:"default file object class"`
	CHints          string               `long:"hints" short:"H" description:"container hints"`
	Properties      CreatePropertiesFlag `long:"properties" description:"container properties"`
	Mode            ConsModeFlag         `long:"mode" short:"M" description:"DFS consistency mode"`
	ACLFile         string               `long:"acl-file" short:"A" description:"input file containing ACL"`
	Group           ui.ACLPrincipalFlag  `long:"group" short:"g" description:"group who will own the container (group@[domain])"`
	Args            struct {
		Label string `positional-arg-name:"label"`
	} `positional-args:"yes"`
}

func (cmd *containerCreateCmd) parseArgs() (*daosAPI.ContainerCreateReq, error) {
	if cmd.Args.Label != "" {
		for key := range cmd.Properties.ParsedProps {
			if key == "label" {
				return nil, errors.New("can't supply label arg and --properties label:")
			}
		}
		if err := cmd.Properties.AddPropVal("label", cmd.Args.Label); err != nil {
			return nil, err
		}
	}

	if cmd.Path != "" {
		// If path was specified, then the next positional argument must be a container label.
		if !cmd.PoolID().Empty() {
			if cmd.Args.Label != "" {
				return nil, errors.New("either pool ID or path must be specified, not both")
			}
			cmd.Args.Label = cmd.PoolID().String()
			cmd.poolBaseCmd.Args.Pool.Clear()
		}

		poolID, err := daosAPI.ResolvePoolPath(cmd.daosCtx, cmd.Path)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to resolve pool id from %q", filepath.Dir(cmd.Path))
		}

		poolUUID, err := uuid.Parse(poolID)
		if err == nil {
			cmd.poolBaseCmd.Args.Pool.UUID = poolUUID
		} else {
			cmd.poolBaseCmd.Args.Pool.Label = poolID
		}
	}

	if cmd.PoolID().Empty() {
		return nil, errors.New("no pool ID or dfs path supplied")
	}

	contCreateReq := daosAPI.ContainerCreateReq{
		Label:      cmd.Args.Label,
		UNSPath:    cmd.Path,
		Type:       daosAPI.ContainerLayout(cmd.Type.Type),
		Properties: cmd.Properties.props,
	}
	if cmd.ACLFile != "" {
		acl, err := control.ReadACLFile(cmd.ACLFile)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to read ACL file %s", cmd.ACLFile)
		}
		contCreateReq.ACL = acl
	}
	if contCreateReq.ACL == nil {
		contCreateReq.ACL = &control.AccessControlList{}
	}
	contCreateReq.ACL.OwnerGroup = cmd.Group.String()

	if cmd.Type.Type == daosAPI.ContainerLayoutPOSIX {
		var posixAttrs daosAPI.POSIXAttributes

		// POSIX containers have extra attributes
		if cmd.ObjectClass.Set {
			posixAttrs.ObjectClass = cmd.ObjectClass.Class
		}
		if cmd.ChunkSize.Set {
			posixAttrs.ChunkSize = uint64(cmd.ChunkSize.Size)
		}
		if cmd.DirObjectClass.Set {
			posixAttrs.DirObjectClass = cmd.DirObjectClass.Class
		}
		if cmd.FileObjectClass.Set {
			posixAttrs.FileObjectClass = cmd.FileObjectClass.Class
		}
		if cmd.Mode.Set {
			posixAttrs.ConsistencyMode = uint32(cmd.Mode.Mode)
		}
		posixAttrs.Hints = cmd.CHints

		contCreateReq.POSIXAttributes = &posixAttrs
	}

	return &contCreateReq, nil
}

func (cmd *containerCreateCmd) Execute(_ []string) (err error) {
	createReq, err := cmd.parseArgs()
	if err != nil {
		return err
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	disconnectPool, err := cmd.connectPool(C.DAOS_PC_RW, ap)
	if err != nil {
		return err
	}
	defer disconnectPool()

	info, err := cmd.poolConn.CreateContainer(cmd.daosCtx, *createReq)
	if err != nil {
		return errors.Wrap(err, "container create failed")
	}

	contID := info.Label
	if contID == "" {
		contID = info.UUID.String()
	}

	cmd.Infof("Successfully created container %s", contID)

	var ci *daosAPI.ContainerInfo
	if err := cmd.openContainer(contID, C.DAOS_COO_RO); err != nil {
		if errors.Cause(err) != daos.NoPermission {
			return errors.Wrapf(err, "failed to open new container %s", contID)
		}

		// Special case for creating a container without permission to query it.
		cmd.Errorf("container %s was created, but query failed", contID)

		ci = new(daosAPI.ContainerInfo)
		ci.Type = cmd.Type.Type
		ci.UUID = cmd.contUUID()
		ci.Label = cmd.Args.Label
	} else {
		defer cmd.closeContainer()
		ci, err = cmd.queryContainer()
		if err != nil {
			return errors.Wrapf(err, "failed to query new container %s", contID)
		}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(ci, nil)
	}

	var bld strings.Builder
	if err := printContainerInfo(&bld, ci, false); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return nil
}

type existingContainerCmd struct {
	containerBaseCmd

	Path string `long:"path" short:"p" description:"unified namespace path"`
	Args struct {
		Container ContainerID `positional-arg-name:"container name or UUID" description:"required if --path is not used"`
	} `positional-args:"yes"`
}

func (cmd *existingContainerCmd) ContainerID() ui.LabelOrUUIDFlag {
	return cmd.Args.Container.LabelOrUUIDFlag
}

// parseContPathArgs is a gross hack to support the --path flag with positional
// arguments. It's necessary because the positional arguments are parsed at the
// same time as the flags, so we can't tell if the user is trying to use a path
// with positional arguments (e.g. --path=/tmp/dfuse set-attr foo:bar) until after
// the flags have been parsed. If --path was used, any consumed positional arguments
// are appended in reverse order to the end of the args slice so that the command
// handler (which must be aware of this convention) may parse them again. Otherwise,
// the positional arguments are parsed as normal pool/container IDs.
func (cmd *existingContainerCmd) parseContPathArgs(args []string) ([]string, error) {
	if contArg := cmd.Args.Container.String(); contArg != "" {
		if cmd.Path != "" {
			args = append(args, contArg)
			cmd.Args.Container.Clear()
		} else {
			if err := cmd.Args.Container.LabelOrUUIDFlag.UnmarshalFlag(contArg); err != nil {
				return nil, err
			}
		}
	}
	if poolArg := cmd.poolBaseCmd.Args.Pool.String(); poolArg != "" {
		if cmd.Path != "" {
			args = append(args, poolArg)
			cmd.poolBaseCmd.Args.Pool.Clear()
		} else {
			if err := cmd.poolBaseCmd.Args.Pool.LabelOrUUIDFlag.UnmarshalFlag(poolArg); err != nil {
				return nil, err
			}
		}
	}

	return args, nil
}

func resolveDfsPath(ctx context.Context, path string, ap *C.struct_cmd_args_s) (poolID, contID string, err error) {
	var name *C.char
	var dirName *C.char
	var dAttr C.struct_duns_attr_t

	cPath := C.CString(path)
	defer freeString(cPath)

	rc := C.parse_filename_dfs(cPath, &name, &dirName)
	if err = daosError(rc); err != nil {
		return
	}
	defer freeString(name)
	defer freeString(dirName)

	poolID, contID, err = daosAPI.ResolveContainerPath(ctx, C.GoString(dirName))
	if err != nil {
		return
	}

	// A little inefficient since we already did this, but it keeps the other
	// implementation simpler.
	rc = C.duns_resolve_path(dirName, &dAttr)
	if err = daosError(rc); err != nil {
		return
	}

	ap._type = dAttr.da_type

	if ap.fs_op == 1<<32-1 { // unset
		return
	}

	if err = daosError(C.set_dfs_path(ap, dAttr.da_rel_path, name)); err != nil {
		return
	}

	return
}

func (cmd *existingContainerCmd) resolveContainerPath(ap *C.struct_cmd_args_s) (err error) {
	if cmd.Path == "" {
		return errors.New("path cannot be empty")
	}
	if ap == nil {
		return errors.New("ap cannot be nil")
	}

	poolID, contID, err := daosAPI.ResolveContainerPath(cmd.daosCtx, cmd.Path)
	if err != nil {
		if !(errors.Is(err, daos.Nonexistent) && ap.fs_op == C.FS_SET_ATTR) {
			return err
		}
		// we could be creating a new file, so try resolving via dfs
		poolID, contID, err = resolveDfsPath(cmd.daosCtx, cmd.Path, ap)
		if err != nil {
			return err
		}
	}

	cPoolID := C.CString(poolID)
	defer freeString(cPoolID)
	C.strncpy(&ap.pool_str[0], cPoolID, C.DAOS_PROP_LABEL_MAX_LEN+1)
	cContID := C.CString(contID)
	defer freeString(cContID)
	C.strncpy(&ap.cont_str[0], cContID, C.DAOS_PROP_LABEL_MAX_LEN+1)

	if poolUUID, err := uuid.Parse(poolID); err == nil {
		cmd.poolBaseCmd.Args.Pool.UUID = poolUUID
		if err := copyUUID(&ap.p_uuid, poolUUID); err != nil {
			return err
		}
	} else {
		cmd.poolBaseCmd.Args.Pool.Label = poolID
	}

	if contUUID, err := uuid.Parse(contID); err == nil {
		cmd.Args.Container.UUID = contUUID
		if err := copyUUID(&ap.c_uuid, contUUID); err != nil {
			return err
		}
	} else {
		cmd.Args.Container.Label = contID
	}

	cmd.Debugf("resolved %s into pool %s and container %s", cmd.Path, cmd.poolBaseCmd.Args.Pool, cmd.Args.Container)

	return
}

func (cmd *existingContainerCmd) resolveContainer(ap *C.struct_cmd_args_s) (err error) {
	if cmd.Path != "" {
		return cmd.resolveContainerPath(ap)
	}

	switch {
	case cmd.ContainerID().HasLabel():
		if ap != nil {
			cLabel := C.CString(cmd.ContainerID().Label)
			defer freeString(cLabel)
			C.strncpy(&ap.cont_str[0], cLabel, C.DAOS_PROP_LABEL_MAX_LEN)
		}
	case cmd.ContainerID().HasUUID():
		if ap != nil {
			cUUIDstr := C.CString(cmd.ContainerID().UUID.String())
			defer freeString(cUUIDstr)
			C.strncpy(&ap.cont_str[0], cUUIDstr, C.DAOS_PROP_LABEL_MAX_LEN)
		}
	default:
		return errors.New("no container label or UUID supplied")
	}

	cmd.Debugf("pool ID: %s, container ID: %s", cmd.PoolID(), cmd.ContainerID())

	return nil
}

func (cmd *existingContainerCmd) resolveAndConnect(contFlags daosAPI.ContainerOpenFlag, ap *C.struct_cmd_args_s) (cleanFn func(), err error) {
	if err = cmd.resolveContainer(ap); err != nil {
		return
	}

	var cleanupPool func()
	cleanupPool, err = cmd.connectPool(C.DAOS_PC_RO, ap)
	if err != nil {
		return
	}

	if err = cmd.openContainer(cmd.ContainerID().String(), contFlags); err != nil {
		cleanupPool()
		return
	}

	if ap != nil {
		if err = copyUUID(&ap.c_uuid, cmd.contUUID()); err != nil {
			cleanupPool()
			return
		}
		ap.cont = cmd.cContHandle
	}

	return func() {
		cmd.closeContainer()
		cleanupPool()
	}, nil
}

type containerListCmd struct {
	poolBaseCmd
}

func listContainers(ctx context.Context, poolConn poolConnection) ([]*daosAPI.ContainerInfo, error) {
	return poolConn.ListContainers(ctx, false)
}

func printContainers(out io.Writer, containers []*daosAPI.ContainerInfo) {
	if len(containers) == 0 {
		fmt.Fprintf(out, "No containers.\n")
		return
	}

	uuidTitle := "UUID"
	labelTitle := "Label"
	titles := []string{uuidTitle, labelTitle}

	table := []txtfmt.TableRow{}
	for _, id := range containers {
		table = append(table,
			txtfmt.TableRow{
				uuidTitle:  id.UUID.String(),
				labelTitle: id.Label,
			})
	}

	tf := txtfmt.NewTableFormatter(titles...)
	tf.InitWriter(out)
	tf.Format(table)
}

func (cmd *containerListCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	containers, err := listContainers(cmd.daosCtx, cmd.poolConn)
	if err != nil {
		return errors.Wrapf(err,
			"unable to list containers for pool %q", cmd.PoolID())
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(containers, nil)
	}

	var bld strings.Builder
	printContainers(&bld, containers)
	cmd.Info(bld.String())

	return nil
}

type containerDestroyCmd struct {
	existingContainerCmd

	Force bool `long:"force" short:"f" description:"force the container destroy"`
}

func (cmd *containerDestroyCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	if err := cmd.resolveContainer(ap); err != nil {
		return err
	}

	var cleanup func()
	cleanup, err = cmd.connectPool(C.DAOS_COO_RW, ap)
	if err != nil {
		// Even if we don't have pool-level write permissions, we may
		// have delete permissions at the container level.
		cleanup, err = cmd.connectPool(C.DAOS_COO_RO, ap)
		if err != nil {
			return err
		}
	}
	defer cleanup()

	cmd.Debugf("destroying container %s (force: %t)",
		cmd.ContainerID(), cmd.Force)

	if err := cmd.poolConn.DestroyContainer(cmd.daosCtx, cmd.ContainerID().String(), cmd.Force); err != nil {
		return errors.Wrapf(err, "failed to destroy container %s",
			cmd.ContainerID())
	}

	if cmd.ContainerID().Empty() {
		cmd.Infof("Successfully destroyed container %s", cmd.Path)
	} else {
		cmd.Infof("Successfully destroyed container %s", cmd.ContainerID())
	}

	return nil
}

type containerListObjectsCmd struct {
	existingContainerCmd

	Epoch uint64 `long:"epc" short:"e" description:"container epoch"`
}

func (cmd *containerListObjectsCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if cmd.Epoch > 0 {
		ap.epc = C.uint64_t(cmd.Epoch)
	}

	snapOpts := uint32(C.DAOS_SNAP_OPT_CR | C.DAOS_SNAP_OPT_OIT)
	rc := C.daos_cont_create_snap_opt(ap.cont, &ap.epc, nil, snapOpts, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to create snapshot for container %s", cmd.ContainerID())
	}
	defer func() {
		var epr C.daos_epoch_range_t
		epr.epr_lo = ap.epc
		epr.epr_hi = ap.epc
		if err := daosError(C.daos_cont_destroy_snap(ap.cont, epr, nil)); err != nil {
			cmd.Errorf("failed to destroy snapshot in cleanup: %v", err)
		}
	}()

	oit := C.daos_handle_t{}
	rc = C.daos_oit_open(ap.cont, ap.epc, &oit, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to open OIT for container %s", cmd.ContainerID())
	}
	defer func() {
		rc = C.daos_cont_snap_oit_destroy(ap.cont, oit, nil)
		if err := daosError(rc); err != nil {
			cmd.Errorf("failed to destroy OIT in cleanup: %v", err)
		}
		rc = C.daos_oit_close(oit, nil)
		if err := daosError(rc); err != nil {
			cmd.Errorf("failed to close OIT in cleanup: %v", err)
		}
	}()

	// NB: It is somewhat inefficient to build up a slice of OID strings for
	// the JSON output format, but it is simple. If it turns out to be a problem,
	// we can implement a custom JSON output handler that streams the OID
	// strings directly to output.
	var oids []string
	var readOids C.uint32_t
	oidArr := [C.OID_ARR_SIZE]C.daos_obj_id_t{}
	anchor := C.daos_anchor_t{}
	for {
		if C.daos_anchor_is_eof(&anchor) {
			break
		}

		readOids = C.OID_ARR_SIZE
		rc = C.daos_oit_list(oit, &oidArr[0], &readOids, &anchor, nil)
		if err := daosError(rc); err != nil {
			return errors.Wrapf(err, "failed to list objects for container %s", cmd.ContainerID())
		}

		for i := C.uint32_t(0); i < readOids; i++ {
			oid := fmt.Sprintf("%d.%d", oidArr[i].hi, oidArr[i].lo)

			if !cmd.JSONOutputEnabled() {
				cmd.Infof("%s", oid)
				continue
			}
			oids = append(oids, oid)
		}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(oids, nil)
	}

	return nil
}

type containerStatCmd struct {
	existingContainerCmd
}

func (cmd *containerStatCmd) Execute(_ []string) error {
	return nil
}

func printContainerInfo(out io.Writer, ci *daosAPI.ContainerInfo, verbose bool) error {
	rows := []txtfmt.TableRow{
		{"Container UUID": ci.UUID.String()},
	}
	if ci.Label != "" {
		rows = append(rows, txtfmt.TableRow{"Container Label": ci.Label})
	}
	rows = append(rows, txtfmt.TableRow{"Container Type": ci.Type.String()})

	if verbose {
		rows = append(rows, []txtfmt.TableRow{
			{"Pool UUID": ci.PoolUUID.String()},
			{"Container redundancy factor": fmt.Sprintf("%d", ci.RedundancyFactor)},
			{"Number of open handles": fmt.Sprintf("%d", ci.NumHandles)},
			{"Latest open time": fmt.Sprintf("%#x (%s)", ci.OpenTime, daos.HLC(ci.OpenTime))},
			{"Latest close/modify time": fmt.Sprintf("%#x (%s)", ci.CloseModifyTime, daos.HLC(ci.CloseModifyTime))},
			{"Number of snapshots": fmt.Sprintf("%d", ci.NumSnapshots)},
		}...)

		if ci.LatestSnapshot != 0 {
			rows = append(rows, txtfmt.TableRow{"Latest Persistent Snapshot": fmt.Sprintf("%#x (%s)", ci.LatestSnapshot, daos.HLC(ci.LatestSnapshot))})
		}
		if pa := ci.POSIXAttributes; pa != nil {
			if pa.ObjectClass != 0 {
				rows = append(rows, txtfmt.TableRow{"Object Class": pa.ObjectClass.String()})
			}
			if pa.DirObjectClass != 0 {
				rows = append(rows, txtfmt.TableRow{"Dir Object Class": pa.DirObjectClass.String()})
			}
			if pa.FileObjectClass != 0 {
				rows = append(rows, txtfmt.TableRow{"File Object Class": pa.FileObjectClass.String()})
			}
			if pa.Hints != "" {
				rows = append(rows, txtfmt.TableRow{"Hints": pa.Hints})
			}
			if pa.ChunkSize > 0 {
				rows = append(rows, txtfmt.TableRow{"Chunk Size": humanize.IBytes(pa.ChunkSize)})
			}
		}
	}
	_, err := fmt.Fprintln(out, txtfmt.FormatEntity("", rows))
	return err
}

type containerQueryCmd struct {
	existingContainerCmd
}

func (cmd *containerQueryCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	ci, err := cmd.queryContainer()
	if err != nil {
		return errors.Wrapf(err,
			"failed to query container %s",
			cmd.contUUID())
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(ci, nil)
	}

	var bld strings.Builder
	if err := printContainerInfo(&bld, ci, true); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return nil
}

type containerCloneCmd struct {
	daosCmd

	Source      string `long:"src" short:"S" description:"source container" required:"1"`
	Destination string `long:"dst" short:"D" description:"destination container" required:"1"`
}

func (cmd *containerCloneCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	disconnect, err := cmd.initDAOS(cmd.daosCtx)
	if err != nil {
		return err
	}
	defer disconnect()

	ap.src = C.CString(cmd.Source)
	defer freeString(ap.src)
	ap.dst = C.CString(cmd.Destination)
	defer freeString(ap.dst)

	ap.c_op = C.CONT_CLONE
	rc := C.cont_clone_hdlr(ap)

	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to clone container %s", cmd.Source)
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(struct {
			SourcePool string `json:"src_pool"`
			SourceCont string `json:"src_cont"`
			DestPool   string `json:"dst_pool"`
			DestCont   string `json:"dst_cont"`
		}{
			C.GoString(&ap.dm_args.src_pool[0]),
			C.GoString(&ap.dm_args.src_cont[0]),
			C.GoString(&ap.dm_args.dst_pool[0]),
			C.GoString(&ap.dm_args.dst_cont[0]),
		}, nil)
	}

	// Compat with old-style output
	cmd.Infof("Successfully copied to destination container %s", C.GoString(&ap.dm_args.dst_cont[0]))

	return nil
}

type containerCheckCmd struct {
	existingContainerCmd

	Epoch uint64 `long:"epc" short:"e" description:"container epoch"`
}

func (cmd *containerCheckCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if cmd.Epoch > 0 {
		ap.epc = C.uint64_t(cmd.Epoch)
	}

	rc := C.cont_check_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to check container %s",
			cmd.ContainerID())
	}

	return nil
}

func contListAttrs(ctx context.Context, contConn *daosAPI.ContainerHandle) ([]string, error) {
	return contConn.ListAttributes(ctx)
}

type containerGetOrListAttrsCmd struct {
	existingContainerCmd
}

func (cmd *containerGetOrListAttrsCmd) listAttributes() error {
	names, err := cmd.contConn.ListAttributes(cmd.daosCtx)
	if err != nil {
		return errors.Wrapf(err,
			"failed to list attributes for container %s",
			cmd.ContainerID())
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(names, nil)
	}

	attrList := make([]*daos.Attribute, len(names))
	for i, name := range names {
		attrList[i] = &daos.Attribute{Name: name}
	}
	var bld strings.Builder
	title := fmt.Sprintf("Attributes for container %s:", cmd.ContainerID())
	printAttributes(&bld, title, attrList...)

	cmd.Info(bld.String())

	return nil
}

func (cmd *containerGetOrListAttrsCmd) getAttributes(names ...string) error {
	attrs, err := cmd.contConn.GetAttributes(cmd.daosCtx, names...)
	if err != nil {
		return errors.Wrapf(err,
			"failed to get attributes for container %s",
			cmd.ContainerID())
	}

	if cmd.JSONOutputEnabled() {
		// Maintain compatibility with older behavior.
		if len(names) == 1 && len(attrs) == 1 {
			return cmd.OutputJSON(attrs[0], nil)
		}
		return cmd.OutputJSON(attrs, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for container %s:", cmd.ContainerID())
	printAttributes(&bld, title, attrs...)

	cmd.Info(bld.String())

	return nil
}

type containerListAttrsCmd struct {
	containerGetOrListAttrsCmd

	Verbose bool `long:"verbose" short:"V" description:"Include values"`
}

func (cmd *containerListAttrsCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if cmd.Verbose {
		return cmd.getAttributes()
	}

	return cmd.listAttributes()
}

type containerDelAttrCmd struct {
	existingContainerCmd

	FlagAttr string `long:"attr" short:"a" description:"attribute name (deprecated; use positional argument)"`
	Args     struct {
		Attr string `positional-arg-name:"<attribute name>"`
	} `positional-args:"yes"`
}

func (cmd *containerDelAttrCmd) Execute(args []string) error {
	if cmd.FlagAttr != "" {
		cmd.Args.Attr = cmd.FlagAttr
	}
	if cmd.Args.Attr == "" {
		return errors.New("attribute name is required")
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := cmd.contConn.DeleteAttribute(cmd.daosCtx, cmd.Args.Attr); err != nil {
		return errors.Wrapf(err,
			"failed to delete attribute %q on container %s",
			cmd.Args.Attr, cmd.ContainerID())
	}

	return nil
}

type containerGetAttrCmd struct {
	containerGetOrListAttrsCmd

	FlagAttr string `long:"attr" short:"a" description:"single attribute name (deprecated; use positional argument)"`
	Args     struct {
		Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]"`
	} `positional-args:"yes"`
}

func (cmd *containerGetAttrCmd) Execute(args []string) error {
	if cmd.FlagAttr != "" {
		if len(cmd.Args.Attrs.ParsedProps) > 0 {
			return errors.New("cannot specify both --attr and positional arguments")
		}
		cmd.Args.Attrs.ParsedProps.Add(cmd.FlagAttr)
	} else if len(args) > 0 {
		if err := cmd.Args.Attrs.UnmarshalFlag(args[len(args)-1]); err != nil {
			return err
		}
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if len(cmd.Args.Attrs.ParsedProps) == 0 {
		return cmd.listAttributes()
	}

	cmd.Debugf("getting attributes: %s", cmd.Args.Attrs.ParsedProps)
	return cmd.getAttributes(cmd.Args.Attrs.ParsedProps.ToSlice()...)
}

type containerSetAttrCmd struct {
	existingContainerCmd

	FlagAttr  string `long:"attr" short:"a" description:"attribute name (deprecated; use positional argument)"`
	FlagValue string `long:"value" short:"v" description:"attribute value (deprecated; use positional argument)"`
	Args      struct {
		Attrs ui.SetPropertiesFlag `positional-arg-name:"key:val[,key:val...]"`
	} `positional-args:"yes"`
}

func (cmd *containerSetAttrCmd) Execute(args []string) error {
	if cmd.FlagAttr != "" || cmd.FlagValue != "" {
		if cmd.FlagAttr == "" || cmd.FlagValue == "" {
			return errors.New("both --attr and --value are required if either are used")
		}
		if len(cmd.Args.Attrs.ParsedProps) > 0 {
			return errors.New("cannot specify both --attr and positional arguments")
		}
		cmd.Args.Attrs.ParsedProps = make(map[string]string)
		cmd.Args.Attrs.ParsedProps[cmd.FlagAttr] = cmd.FlagValue
	} else if len(args) > 0 {
		if err := cmd.Args.Attrs.UnmarshalFlag(args[len(args)-1]); err != nil {
			return err
		}
	}

	if len(cmd.Args.Attrs.ParsedProps) == 0 {
		return errors.New("attribute name and value are required")
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	attrs := make([]*daos.Attribute, 0, len(cmd.Args.Attrs.ParsedProps))
	for key, val := range cmd.Args.Attrs.ParsedProps {
		attrs = append(attrs, &daos.Attribute{
			Name:  key,
			Value: []byte(val),
		})
	}

	if err := cmd.contConn.SetAttributes(cmd.daosCtx, attrs); err != nil {
		return errors.Wrapf(err, "failed to set attributes on container %s", cmd.ContainerID())
	}

	return nil
}

type containerGetPropCmd struct {
	existingContainerCmd

	PropsFlag GetPropertiesFlag `long:"properties" description:"container properties to get (deprecated: use positional argument)"`
	Args      struct {
		Props GetPropertiesFlag `positional-arg-name:"key[,key...]"`
	} `positional-args:"yes"`
}

func (cmd *containerGetPropCmd) Execute(args []string) error {
	if len(args) > 0 {
		if err := cmd.Args.Props.UnmarshalFlag(args[len(args)-1]); err != nil {
			return err
		}
	}
	if len(cmd.PropsFlag.names) > 0 {
		if len(cmd.Args.Props.names) > 0 {
			return errors.New("cannot specify both --properties and positional arguments")
		}
		cmd.Args.Props = cmd.PropsFlag
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	props, err := cmd.contConn.GetProperties(cmd.daosCtx, cmd.Args.Props.names...)
	if err != nil {
		return errors.Wrapf(err, "failed to fetch properties for container %s", cmd.ContainerID())
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(props, nil)
	}

	title := fmt.Sprintf("Properties for container %s", cmd.ContainerID())
	var bld strings.Builder
	printProperties(&bld, title, props)

	cmd.Info(bld.String())

	return nil
}

type containerSetPropCmd struct {
	existingContainerCmd

	PropsFlag SetPropertiesFlag `long:"properties" description:"container properties to set (deprecated: use positional argument)"`
	Args      struct {
		Props SetPropertiesFlag `positional-arg-name:"key:val[,key:val...]"`
	} `positional-args:"yes"`
}

func (cmd *containerSetPropCmd) Execute(args []string) error {
	if len(args) > 0 {
		if err := cmd.Args.Props.UnmarshalFlag(args[len(args)-1]); err != nil {
			return err
		}
	}
	if len(cmd.PropsFlag.ParsedProps) > 0 {
		if len(cmd.Args.Props.ParsedProps) > 0 {
			return errors.New("cannot specify both --properties and positional arguments")
		}
		cmd.Args.Props = cmd.PropsFlag
	}
	if len(cmd.Args.Props.ParsedProps) == 0 {
		return errors.New("property name and value are required")
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := cmd.contConn.SetProperties(cmd.daosCtx, cmd.Args.Props.props); err != nil {
		return errors.Wrap(err, "failed to set container properties")
	}

	cmd.Info("Properties were successfully set")

	return nil
}

// Experiment below with container ID completion.

type cmplCmd struct {
	cliOptions

	// Consume these to leave the pool id as the only
	// positional argument.
	Args struct {
		Object  string
		Command string
	} `positional-args:"yes"`
}

type poolFlagCmd struct {
	cmplCmd
	poolBaseCmd
}

func parsePoolFlag() *poolFlagCmd {
	// Avoid recursion when doing completion resolution.
	gfcVal, gfcActive := os.LookupEnv("GO_FLAGS_COMPLETION")
	os.Unsetenv("GO_FLAGS_COMPLETION")
	defer func() {
		if gfcActive {
			os.Setenv("GO_FLAGS_COMPLETION", gfcVal)
		}
	}()

	var pfc poolFlagCmd
	parser := flags.NewParser(&pfc, flags.HelpFlag|flags.PassDoubleDash|flags.IgnoreUnknown)
	parser.ParseArgs(os.Args[1:])

	return &pfc
}

type ContainerID struct {
	argOrID
}

// Implement the completion handler to provide a list of container IDs
// as completion items.
func (f *ContainerID) Complete(match string) (comps []flags.Completion) {
	pf := parsePoolFlag()
	pf.Logger = &logging.LeveledLogger{}

	fini, err := pf.initDAOS(pf.daosCtx)
	if err != nil {
		return
	}
	defer fini()

	cleanup, err := pf.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return
	}
	defer cleanup()

	containers, err := listContainers(pf.daosCtx, pf.poolConn)
	if err != nil {
		return
	}

	for _, cont := range containers {
		id := cont.Label
		if id == "" {
			id = cont.UUID.String()
		}
		if strings.HasPrefix(id, match) {
			comps = append(comps, flags.Completion{
				Item: id,
			})
		}
	}

	return
}

type containerLinkCmd struct {
	containerBaseCmd

	Path string `long:"path" short:"p" description:"container namespace path to create"`
	Args struct {
		Container ContainerID `positional-arg-name:"container name or UUID" description:"Container to link in the namespace"`
	} `positional-args:"yes"`
}

func (cmd *containerLinkCmd) ContainerID() ui.LabelOrUUIDFlag {
	return cmd.Args.Container.LabelOrUUIDFlag
}

func (cmd *containerLinkCmd) Execute(_ []string) (err error) {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	var cleanup func()
	cleanup, err = cmd.connectPool(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	cmd.Debugf("Creating a namespace link %s for container %s", cmd.Path, cmd.ContainerID())

	cPath := C.CString(cmd.Path)
	defer freeString(cPath)

	var rc C.int
	switch {
	case cmd.ContainerID().HasUUID():
		//cmd.contConn.ContainerUUID = cmd.ContainerID().UUID
		cUUIDstr := C.CString(cmd.ContainerID().UUID.String())
		defer freeString(cUUIDstr)
		rc = C.duns_link_cont(cmd.cPoolHandle, cUUIDstr, cPath)
	case cmd.ContainerID().Label != "":
		cLabel := C.CString(cmd.ContainerID().Label)
		defer freeString(cLabel)
		rc = C.duns_link_cont(cmd.cPoolHandle, cLabel, cPath)
	default:
		return errors.New("no UUID or label for container")
	}

	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to link container %s in the namespace",
			cmd.ContainerID())
	}

	return nil
}

type containerEvictCmd struct {
	existingContainerCmd

	All bool `long:"all" short:"a" description:"evict all handles from all users"`
}

func (cmd *containerEvictCmd) Execute(_ []string) (err error) {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	var co_flags daosAPI.ContainerOpenFlag
	if cmd.All {
		co_flags = C.DAOS_COO_EVICT_ALL | C.DAOS_COO_EX
	} else {
		co_flags = C.DAOS_COO_EVICT | C.DAOS_COO_RO
	}

	cleanup, err := cmd.resolveAndConnect(co_flags, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	return nil
}

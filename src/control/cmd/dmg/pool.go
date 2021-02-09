//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

// PoolCmd is the struct representing the top-level pool subcommand.
type PoolCmd struct {
	Create       PoolCreateCmd       `command:"create" alias:"c" description:"Create a DAOS pool"`
	Destroy      PoolDestroyCmd      `command:"destroy" alias:"d" description:"Destroy a DAOS pool"`
	Evict        PoolEvictCmd        `command:"evict" alias:"ev" description:"Evict all pool connections to a DAOS pool"`
	List         systemListPoolsCmd  `command:"list" alias:"l" description:"List DAOS pools"`
	Extend       PoolExtendCmd       `command:"extend" alias:"ext" description:"Extend a DAOS pool to include new ranks."`
	Exclude      PoolExcludeCmd      `command:"exclude" alias:"e" description:"Exclude targets from a rank"`
	Drain        PoolDrainCmd        `command:"drain" alias:"d" description:"Drain targets from a rank"`
	Reintegrate  PoolReintegrateCmd  `command:"reintegrate" alias:"r" description:"Reintegrate targets for a rank"`
	Query        PoolQueryCmd        `command:"query" alias:"q" description:"Query a DAOS pool"`
	GetACL       PoolGetACLCmd       `command:"get-acl" alias:"ga" description:"Get a DAOS pool's Access Control List"`
	OverwriteACL PoolOverwriteACLCmd `command:"overwrite-acl" alias:"oa" description:"Overwrite a DAOS pool's Access Control List"`
	UpdateACL    PoolUpdateACLCmd    `command:"update-acl" alias:"ua" description:"Update entries in a DAOS pool's Access Control List"`
	DeleteACL    PoolDeleteACLCmd    `command:"delete-acl" alias:"da" description:"Delete an entry from a DAOS pool's Access Control List"`
	SetProp      PoolSetPropCmd      `command:"set-prop" alias:"sp" description:"Set pool property"`
}

// PoolCreateCmd is the struct representing the command to create a DAOS pool.
type PoolCreateCmd struct {
	logCmd
	ctlInvokerCmd
	jsonOutputCmd
	GroupName  string  `short:"g" long:"group" description:"DAOS pool to be owned by given group, format name@domain"`
	UserName   string  `short:"u" long:"user" description:"DAOS pool to be owned by given user, format name@domain"`
	PoolName   string  `short:"p" long:"name" description:"Unique name for pool (set as label)"`
	ACLFile    string  `short:"a" long:"acl-file" description:"Access Control List file path for DAOS pool"`
	Size       string  `short:"z" long:"size" description:"Total size of DAOS pool (auto)"`
	ScmRatio   float64 `short:"t" long:"scm-ratio" default:"6" description:"Percentage of SCM:NVMe for pool storage (auto)"`
	NumRanks   uint32  `short:"k" long:"nranks" description:"Number of ranks to use (auto)"`
	NumSvcReps uint32  `short:"v" long:"nsvc" description:"Number of pool service replicas"`
	ScmSize    string  `short:"s" long:"scm-size" description:"Per-server SCM allocation for DAOS pool (manual)"`
	NVMeSize   string  `short:"n" long:"nvme-size" description:"Per-server NVMe allocation for DAOS pool (manual)"`
	RankList   string  `short:"r" long:"ranks" description:"Storage server unique identifiers (ranks) for DAOS pool"`
	Sys        string  `short:"S" long:"sys" default:"daos_server" description:"DAOS system that pool is to be a part of"`
}

// Execute is run when PoolCreateCmd subcommand is activated
func (cmd *PoolCreateCmd) Execute(args []string) error {
	if cmd.Size != "" && (cmd.ScmSize != "" || cmd.NVMeSize != "") {
		return errIncompatFlags("size", "scm-size", "nvme-size")
	}
	if cmd.Size == "" && cmd.ScmSize == "" {
		return errors.New("either --size or --scm-size must be supplied")
	}

	var err error
	req := &control.PoolCreateReq{
		User:       cmd.UserName,
		UserGroup:  cmd.GroupName,
		Name:       cmd.PoolName,
		NumSvcReps: cmd.NumSvcReps,
	}

	if cmd.ACLFile != "" {
		req.ACL, err = control.ReadACLFile(cmd.ACLFile)
		if err != nil {
			return err
		}
	}

	req.Ranks, err = system.ParseRanks(cmd.RankList)
	if err != nil {
		return errors.Wrap(err, "parsing rank list")
	}

	if cmd.Size != "" {
		// auto-selection of storage values
		req.TotalBytes, err = humanize.ParseBytes(cmd.Size)
		if err != nil {
			return errors.Wrap(err, "failed to parse pool size")
		}

		if cmd.NumRanks > 0 && cmd.RankList != "" {
			return errIncompatFlags("num-ranks", "ranks")
		}
		req.NumRanks = cmd.NumRanks

		if cmd.ScmRatio < 1 || cmd.ScmRatio > 100 {
			return errors.New("SCM:NVMe ratio must be a value between 1-100")
		}
		req.ScmRatio = cmd.ScmRatio / 100
		cmd.log.Infof("Creating DAOS pool with automatic storage allocation: "+
			"%s NVMe + %0.2f%% SCM", humanize.Bytes(req.TotalBytes), req.ScmRatio*100)
	} else {
		// manual selection of storage values
		if cmd.NumRanks > 0 {
			return errIncompatFlags("nranks", "scm-size")
		}

		req.ScmBytes, err = humanize.ParseBytes(cmd.ScmSize)
		if err != nil {
			return errors.Wrap(err, "failed to parse pool SCM size")
		}

		if cmd.NVMeSize != "" {
			req.NvmeBytes, err = humanize.ParseBytes(cmd.NVMeSize)
			if err != nil {
				return errors.Wrap(err, "failed to parse pool NVMe size")
			}
		}

		ratio := 1.0
		if req.NvmeBytes > 0 {
			ratio = float64(req.ScmBytes) / float64(req.NvmeBytes)
		}
		if ratio < storage.MinScmToNVMeRatio {
			cmd.log.Infof("SCM:NVMe ratio is less than %0.2f %%, DAOS "+
				"performance will suffer!\n", storage.MinScmToNVMeRatio*100)
		}
		cmd.log.Infof("Creating DAOS pool with manual per-server storage allocation: "+
			"%s SCM, %s NVMe (%0.2f%% ratio)", humanize.Bytes(req.ScmBytes),
			humanize.Bytes(req.NvmeBytes), ratio*100)
	}
	req.SetSystem(cmd.Sys)

	ctx := context.Background()
	resp, err := control.PoolCreate(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	var bld strings.Builder
	if err := pretty.PrintPoolCreateResponse(resp, &bld); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return nil
}

// poolCmd is the base struct for all pool commands that work with existing pools.
type poolCmd struct {
	logCmd
	jsonOutputCmd
	ctlInvokerCmd
	ID   string `long:"pool" required:"1" description:"Unique ID of DAOS pool"`
	UUID string
}

// resolveID attempts to resolve the supplied pool ID into a UUID.
func (cmd *poolCmd) resolveID() error {
	if cmd.ID == "" {
		return errors.New("no pool ID supplied")
	}

	if _, err := uuid.Parse(cmd.ID); err == nil {
		cmd.UUID = cmd.ID
		return nil
	}

	ctx := context.Background()
	resp, err := control.PoolResolveID(ctx, cmd.ctlInvoker, &control.PoolResolveIDReq{
		HumanID: cmd.ID,
	})
	if err != nil {
		return errors.Wrap(err, "failed to resolve pool ID into UUID")
	}
	cmd.UUID = resp.UUID

	return nil
}

// PoolDestroyCmd is the struct representing the command to destroy a DAOS pool.
type PoolDestroyCmd struct {
	poolCmd
	// TODO: implement --sys & --svc options (currently unsupported server side)
	Force bool `short:"f" long:"force" description:"Force removal of DAOS pool"`
}

// Execute is run when PoolDestroyCmd subcommand is activated
func (cmd *PoolDestroyCmd) Execute(args []string) error {
	msg := "succeeded"

	if err := cmd.resolveID(); err != nil {
		return err
	}

	req := &control.PoolDestroyReq{UUID: cmd.UUID, Force: cmd.Force}

	ctx := context.Background()
	err := control.PoolDestroy(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.errorJSON(err)
	}

	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.log.Infof("Pool-destroy command %s\n", msg)

	return err
}

// PoolEvictCmd is the struct representing the command to evict a DAOS pool.
type PoolEvictCmd struct {
	poolCmd
	Sys string `short:"S" long:"sys" default:"daos_server" description:"DAOS system that the pools connections be evicted from."`
}

// Execute is run when PoolEvictCmd subcommand is activated
func (cmd *PoolEvictCmd) Execute(args []string) error {
	msg := "succeeded"

	if err := cmd.resolveID(); err != nil {
		return err
	}

	req := &control.PoolEvictReq{UUID: cmd.UUID}
	req.SetSystem(cmd.Sys)

	ctx := context.Background()
	err := control.PoolEvict(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.errorJSON(err)
	}

	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.log.Infof("Pool-evict command %s\n", msg)

	return err
}

// PoolExcludeCmd is the struct representing the command to exclude a DAOS target.
type PoolExcludeCmd struct {
	poolCmd
	Rank      uint32 `long:"rank" required:"1" description:"Rank of the targets to be excluded"`
	Targetidx string `long:"target-idx" description:"Comma-separated list of target idx(s) to be excluded from the rank"`
}

// Execute is run when PoolExcludeCmd subcommand is activated
func (cmd *PoolExcludeCmd) Execute(args []string) error {
	msg := "succeeded"

	if err := cmd.resolveID(); err != nil {
		return err
	}

	var idxlist []uint32
	if err := common.ParseNumberList(cmd.Targetidx, &idxlist); err != nil {
		return errors.WithMessage(err, "parsing rank list")
	}

	req := &control.PoolExcludeReq{UUID: cmd.UUID, Rank: system.Rank(cmd.Rank), Targetidx: idxlist}

	ctx := context.Background()
	err := control.PoolExclude(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.errorJSON(err)
	}

	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.log.Infof("Exclude command %s\n", msg)

	return err
}

// PoolDrainCmd is the struct representing the command to Drain a DAOS target.
type PoolDrainCmd struct {
	poolCmd
	Rank      uint32 `long:"rank" required:"1" description:"Rank of the targets to be drained"`
	Targetidx string `long:"target-idx" description:"Comma-separated list of target idx(s) to be drained on the rank"`
}

// Execute is run when PoolDrainCmd subcommand is activated
func (cmd *PoolDrainCmd) Execute(args []string) error {
	msg := "succeeded"

	if err := cmd.resolveID(); err != nil {
		return err
	}

	var idxlist []uint32
	if err := common.ParseNumberList(cmd.Targetidx, &idxlist); err != nil {
		err = errors.WithMessage(err, "parsing rank list")
		if cmd.jsonOutputEnabled() {
			return cmd.errorJSON(err)
		}
		return err
	}

	req := &control.PoolDrainReq{UUID: cmd.UUID, Rank: system.Rank(cmd.Rank), Targetidx: idxlist}

	ctx := context.Background()
	err := control.PoolDrain(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.errorJSON(err)
	}

	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.log.Infof("Drain command %s\n", msg)

	return err
}

// PoolExtendCmd is the struct representing the command to Extend a DAOS pool.
type PoolExtendCmd struct {
	poolCmd
	RankList string `long:"ranks" required:"1" description:"Comma-separated list of ranks to add to the pool"`
	// Everything after this needs to be removed when pool info can be fetched
	ScmSize  string `short:"s" long:"scm-size" required:"1" description:"Size of SCM component of the original DAOS pool being extended"`
	NVMeSize string `short:"n" long:"nvme-size" description:"Size of NVMe component of the original DAOS pool being extended, or none if not originally supplied to pool create."`
	// END TEMPORARY SECTION
}

// Execute is run when PoolExtendCmd subcommand is activated
func (cmd *PoolExtendCmd) Execute(args []string) error {
	msg := "succeeded"

	if err := cmd.resolveID(); err != nil {
		return err
	}

	ranks, err := system.ParseRanks(cmd.RankList)
	if err != nil {
		err = errors.Wrap(err, "parsing rank list")
		if cmd.jsonOutputEnabled() {
			return cmd.errorJSON(err)
		}
		return err
	}

	// Everything below this needs to be removed once Pool Info can be fetched

	scmBytes, err := humanize.ParseBytes(cmd.ScmSize)
	if err != nil {
		return errors.Wrap(err, "pool SCM size")
	}

	var nvmeBytes uint64
	if cmd.NVMeSize != "" {
		nvmeBytes, err = humanize.ParseBytes(cmd.NVMeSize)
		if err != nil {
			return errors.Wrap(err, "pool NVMe size")
		}
	}

	req := &control.PoolExtendReq{
		UUID: cmd.UUID, Ranks: ranks,
		ScmBytes: scmBytes, NvmeBytes: nvmeBytes,
	}
	// END TEMP SECTION

	ctx := context.Background()
	err = control.PoolExtend(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.errorJSON(err)
	}

	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.log.Infof("Extend command %s\n", msg)

	return err
}

// PoolReintegrateCmd is the struct representing the command to Add a DAOS target.
type PoolReintegrateCmd struct {
	poolCmd
	Rank      uint32 `long:"rank" required:"1" description:"Rank of the targets to be reintegrated"`
	Targetidx string `long:"target-idx" description:"Comma-separated list of target idx(s) to be reintegrated into the rank"`
}

// Execute is run when PoolReintegrateCmd subcommand is activated
func (cmd *PoolReintegrateCmd) Execute(args []string) error {
	msg := "succeeded"

	if err := cmd.resolveID(); err != nil {
		return err
	}

	var idxlist []uint32
	if err := common.ParseNumberList(cmd.Targetidx, &idxlist); err != nil {
		err = errors.WithMessage(err, "parsing rank list")
		if cmd.jsonOutputEnabled() {
			return cmd.errorJSON(err)
		}
		return err
	}

	req := &control.PoolReintegrateReq{UUID: cmd.UUID, Rank: system.Rank(cmd.Rank), Targetidx: idxlist}

	ctx := context.Background()
	err := control.PoolReintegrate(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.errorJSON(err)
	}

	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.log.Infof("Reintegration command %s\n", msg)

	return err
}

// PoolQueryCmd is the struct representing the command to query a DAOS pool.
type PoolQueryCmd struct {
	poolCmd
}

// Execute is run when PoolQueryCmd subcommand is activated
func (cmd *PoolQueryCmd) Execute(args []string) error {
	if err := cmd.resolveID(); err != nil {
		return err
	}

	req := &control.PoolQueryReq{
		UUID: cmd.UUID,
	}

	ctx := context.Background()
	resp, err := control.PoolQuery(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "pool query failed")
	}

	var bld strings.Builder
	if err := pretty.PrintPoolQueryResponse(resp, &bld); err != nil {
		return err
	}
	cmd.log.Info(bld.String())
	return nil
}

// PoolSetPropCmd represents the command to set a property on a pool.
type PoolSetPropCmd struct {
	poolCmd
	Property string `short:"n" long:"name" required:"1" description:"Name of property to be set"`
	Value    string `short:"v" long:"value" required:"1" description:"Value of property to be set"`
}

// Execute is run when PoolSetPropCmd subcommand is activatecmd.
func (cmd *PoolSetPropCmd) Execute(_ []string) error {
	if err := cmd.resolveID(); err != nil {
		return err
	}

	req := &control.PoolSetPropReq{
		UUID:     cmd.UUID,
		Property: cmd.Property,
	}

	req.SetString(cmd.Value)
	if numVal, err := strconv.ParseUint(cmd.Value, 10, 64); err == nil {
		req.SetNumber(numVal)
	}

	ctx := context.Background()
	resp, err := control.PoolSetProp(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "pool set-prop failed")
	}

	cmd.log.Infof("pool set-prop succeeded (%s=%q)", resp.Property, resp.Value)
	return nil
}

// PoolGetACLCmd represents the command to fetch an Access Control List of a
// DAOS pool.
type PoolGetACLCmd struct {
	poolCmd
	File    string `short:"o" long:"outfile" required:"0" description:"Output ACL to file"`
	Force   bool   `short:"f" long:"force" required:"0" description:"Allow to clobber output file"`
	Verbose bool   `short:"v" long:"verbose" required:"0" description:"Add descriptive comments to ACL entries"`
}

// Execute is run when the PoolGetACLCmd subcommand is activated
func (cmd *PoolGetACLCmd) Execute(args []string) error {
	if err := cmd.resolveID(); err != nil {
		return err
	}

	req := &control.PoolGetACLReq{UUID: cmd.UUID}

	ctx := context.Background()
	resp, err := control.PoolGetACL(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp.ACL, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-get-ACL command failed")
	}

	cmd.log.Debugf("Pool-get-ACL command succeeded, UUID: %s\n", cmd.UUID)

	acl := control.FormatACL(resp.ACL, cmd.Verbose)

	if cmd.File != "" {
		err = cmd.writeACLToFile(acl)
		if err != nil {
			return err
		}
		cmd.log.Infof("Wrote ACL to output file: %s", cmd.File)
	} else {
		cmd.log.Info(acl)
	}

	return nil
}

func (cmd *PoolGetACLCmd) writeACLToFile(acl string) error {
	if err := cmd.resolveID(); err != nil {
		return err
	}

	if !cmd.Force {
		// Keep the user from clobbering existing files
		_, err := os.Stat(cmd.File)
		if err == nil {
			return errors.New(fmt.Sprintf("file already exists: %s", cmd.File))
		}
	}

	f, err := os.Create(cmd.File)
	if err != nil {
		cmd.log.Errorf("Unable to create file: %s", cmd.File)
		return err
	}
	defer f.Close()

	_, err = f.WriteString(acl)
	if err != nil {
		cmd.log.Errorf("Failed to write to file: %s", cmd.File)
		return err
	}

	return nil
}

// PoolOverwriteACLCmd represents the command to overwrite the Access Control
// List of a DAOS pool.
type PoolOverwriteACLCmd struct {
	poolCmd
	ACLFile string `short:"a" long:"acl-file" required:"1" description:"Path for new Access Control List file"`
}

// Execute is run when the PoolOverwriteACLCmd subcommand is activated
func (cmd *PoolOverwriteACLCmd) Execute(args []string) error {
	if err := cmd.resolveID(); err != nil {
		return err
	}

	acl, err := control.ReadACLFile(cmd.ACLFile)
	if err != nil {
		if cmd.jsonOutputEnabled() {
			return cmd.errorJSON(err)
		}
		return err
	}

	req := &control.PoolOverwriteACLReq{
		UUID: cmd.UUID,
		ACL:  acl,
	}

	ctx := context.Background()
	resp, err := control.PoolOverwriteACL(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp.ACL, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-overwrite-ACL command failed")
	}

	cmd.log.Infof("Pool-overwrite-ACL command succeeded, UUID: %s\n", cmd.UUID)

	cmd.log.Info(control.FormatACLDefault(resp.ACL))

	return nil
}

// PoolUpdateACLCmd represents the command to update the Access Control List of
// a DAOS pool.
type PoolUpdateACLCmd struct {
	poolCmd
	ACLFile string `short:"a" long:"acl-file" required:"0" description:"Path for new Access Control List file"`
	Entry   string `short:"e" long:"entry" required:"0" description:"Single Access Control Entry to add or update"`
}

// Execute is run when the PoolUpdateACLCmd subcommand is activated
func (cmd *PoolUpdateACLCmd) Execute(args []string) error {
	if err := cmd.resolveID(); err != nil {
		return err
	}

	if (cmd.ACLFile == "" && cmd.Entry == "") || (cmd.ACLFile != "" && cmd.Entry != "") {
		return errors.New("either ACL file or entry parameter is required")
	}

	var acl *control.AccessControlList
	if cmd.ACLFile != "" {
		aclFileResult, err := control.ReadACLFile(cmd.ACLFile)
		if err != nil {
			if cmd.jsonOutputEnabled() {
				return cmd.errorJSON(err)
			}
			return err
		}
		acl = aclFileResult
	} else {
		acl = &control.AccessControlList{
			Entries: []string{cmd.Entry},
		}
	}

	req := &control.PoolUpdateACLReq{
		UUID: cmd.UUID,
		ACL:  acl,
	}

	ctx := context.Background()
	resp, err := control.PoolUpdateACL(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp.ACL, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-update-ACL command failed")
	}

	cmd.log.Infof("Pool-update-ACL command succeeded, UUID: %s\n", cmd.UUID)

	cmd.log.Info(control.FormatACLDefault(resp.ACL))

	return nil
}

// PoolDeleteACLCmd represents the command to delete an entry from the Access
// Control List of a DAOS pool.
type PoolDeleteACLCmd struct {
	poolCmd
	Principal string `short:"p" long:"principal" required:"1" description:"Principal whose entry should be removed"`
}

// Execute is run when the PoolDeleteACLCmd subcommand is activated
func (cmd *PoolDeleteACLCmd) Execute(args []string) error {
	if err := cmd.resolveID(); err != nil {
		return err
	}

	req := &control.PoolDeleteACLReq{
		UUID:      cmd.UUID,
		Principal: cmd.Principal,
	}

	ctx := context.Background()
	resp, err := control.PoolDeleteACL(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp.ACL, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-delete-ACL command failed")
	}

	cmd.log.Infof("Pool-delete-ACL command succeeded, UUID: %s\n", cmd.UUID)

	cmd.log.Info(control.FormatACLDefault(resp.ACL))

	return nil
}

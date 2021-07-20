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
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

// PoolCmd is the struct representing the top-level pool subcommand.
type PoolCmd struct {
	Create       PoolCreateCmd       `command:"create" alias:"c" description:"Create a DAOS pool"`
	Destroy      PoolDestroyCmd      `command:"destroy" alias:"d" description:"Destroy a DAOS pool"`
	Evict        PoolEvictCmd        `command:"evict" alias:"ev" description:"Evict all pool connections to a DAOS pool"`
	List         PoolListCmd         `command:"list" alias:"ls" description:"List DAOS pools"`
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
	GetProp      PoolGetPropCmd      `command:"get-prop" alias:"gp" description:"Get pool properties"`
}

// PoolCreateCmd is the struct representing the command to create a DAOS pool.
type PoolCreateCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd
	GroupName  string           `short:"g" long:"group" description:"DAOS pool to be owned by given group, format name@domain"`
	UserName   string           `short:"u" long:"user" description:"DAOS pool to be owned by given user, format name@domain"`
	PoolLabel  string           `short:"p" long:"label" description:"Unique label for pool"`
	Properties PoolSetPropsFlag `short:"P" long:"properties" description:"Pool properties to be set"`
	ACLFile    string           `short:"a" long:"acl-file" description:"Access Control List file path for DAOS pool"`
	Size       string           `short:"z" long:"size" description:"Total size of DAOS pool (auto)"`
	TierRatio  string           `short:"t" long:"tier-ratio" default:"6,94" description:"Percentage of storage tiers for pool storage (auto)"`
	NumRanks   uint32           `short:"k" long:"nranks" description:"Number of ranks to use (auto)"`
	NumSvcReps uint32           `short:"v" long:"nsvc" description:"Number of pool service replicas"`
	ScmSize    string           `short:"s" long:"scm-size" description:"Per-server SCM allocation for DAOS pool (manual)"`
	NVMeSize   string           `short:"n" long:"nvme-size" description:"Per-server NVMe allocation for DAOS pool (manual)"`
	RankList   string           `short:"r" long:"ranks" description:"Storage server unique identifiers (ranks) for DAOS pool"`
	Policy     string           `short:"P" long:"policy" default:"io_size" description:"Pool tiering policy"`
}

// Execute is run when PoolCreateCmd subcommand is activated
func (cmd *PoolCreateCmd) Execute(args []string) error {
	if cmd.Size != "" && (cmd.ScmSize != "" || cmd.NVMeSize != "") {
		return errIncompatFlags("size", "scm-size", "nvme-size")
	}
	if cmd.Size == "" && cmd.ScmSize == "" {
		return errors.New("either --size or --scm-size must be supplied")
	}

	if cmd.PoolLabel != "" {
		for _, prop := range cmd.Properties.ToSet {
			if prop.Name == "label" {
				return errors.New("can't use both --label and --properties label:")
			}
		}
		if err := cmd.Properties.UnmarshalFlag(fmt.Sprintf("label:%s", cmd.PoolLabel)); err != nil {
			return err
		}
	}

	var err error
	req := &control.PoolCreateReq{
		User:         cmd.UserName,
		UserGroup:    cmd.GroupName,
		NumSvcReps:   cmd.NumSvcReps,
		Properties:   cmd.Properties.ToSet,
		PolicyString: cmd.Policy,
		PolicyArgs:   args,
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

		tierRatio, err := parseUint64Array(cmd.TierRatio)
		if err != nil {
			return errors.Wrap(err, "failed to parse tier ratios")
		}

		// Handle single tier ratio as a special case and fill
		// second tier with remainder (-t 6 will assign 6% of total
		// storage to tier0 and 94% to tier1).
		if len(tierRatio) == 1 && tierRatio[0] < 100 {
			tierRatio = append(tierRatio, 100-tierRatio[0])
		}

		req.TierRatio = make([]float64, len(tierRatio))
		var totalRatios uint64
		for tierIdx, ratio := range tierRatio {
			if ratio > 100 {
				return errors.New("Storage tier ratio must be a value between 0-100")
			}
			totalRatios += ratio
			req.TierRatio[tierIdx] = float64(ratio) / 100
		}
		if totalRatios != 100 {
			return errors.New("Storage tier ratios must add up to 100")
		}
		cmd.log.Infof("Creating DAOS pool with automatic storage allocation: "+
			"%s total, %s tier ratio", humanize.Bytes(req.TotalBytes), cmd.TierRatio)
	} else {
		// manual selection of storage values
		if cmd.NumRanks > 0 {
			return errIncompatFlags("nranks", "scm-size")
		}

		ScmBytes, err := humanize.ParseBytes(cmd.ScmSize)
		if err != nil {
			return errors.Wrap(err, "failed to parse pool SCM size")
		}

		var NvmeBytes uint64
		if cmd.NVMeSize != "" {
			NvmeBytes, err = humanize.ParseBytes(cmd.NVMeSize)
			if err != nil {
				return errors.Wrap(err, "failed to parse pool NVMe size")
			}
		}

		req.TierBytes = []uint64{ScmBytes, NvmeBytes}
		req.TotalBytes = 0
		req.TierRatio = nil

		scmRatio := float64(ScmBytes) / float64(NvmeBytes)

		if scmRatio < storage.MinScmToNVMeRatio {
			cmd.log.Infof("SCM:NVMe ratio is less than %0.2f %%, DAOS "+
				"performance will suffer!\n", storage.MinScmToNVMeRatio*100)
		}
		cmd.log.Infof("Creating DAOS pool with manual per-server storage allocation: "+
			"%s SCM, %s NVMe (%0.2f%% ratio)", humanize.Bytes(ScmBytes),
			humanize.Bytes(NvmeBytes), scmRatio*100)
	}

	resp, err := control.PoolCreate(context.Background(), cmd.ctlInvoker, req)

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

// PoolListCmd represents the command to fetch a list of all DAOS pools in the system.
type PoolListCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd
	Verbose bool `short:"v" long:"verbose" required:"0" description:"Add pool UUIDs and service replica lists to display."`
}

// Execute is run when PoolListCmd activates
func (cmd *PoolListCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "list pools failed")
	}()

	if cmd.config == nil {
		return errors.New("no configuration loaded")
	}

	req := new(control.ListPoolsReq)

	resp, err := control.ListPools(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, nil)
	}

	var out, outErr strings.Builder
	if err := pretty.PrintListPoolsResponse(&out, &outErr, resp, cmd.Verbose); err != nil {
		return err
	}
	if outErr.String() != "" {
		cmd.log.Error(outErr.String())
	}
	// Infof prints raw string and doesn't try to expand "%"
	// preserving column formatting in txtfmt table
	cmd.log.Infof("%s", out.String())

	return resp.Errors()
}

type PoolID struct {
	ui.LabelOrUUIDFlag
}

// poolCmd is the base struct for all pool commands that work with existing pools.
type poolCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd
	uuidStr string

	Args struct {
		Pool PoolID `positional-arg-name:"<pool name or UUID>"`
	} `positional-args:"yes"`
}

func (cmd *poolCmd) PoolID() *PoolID {
	return &cmd.Args.Pool
}

// resolveID attempts to resolve the supplied pool ID into a UUID.
func (cmd *poolCmd) resolveID() error {
	if cmd.PoolID().Empty() {
		return errors.New("no pool ID supplied")
	}

	if cmd.PoolID().HasUUID() {
		cmd.uuidStr = cmd.PoolID().UUID.String()
		return nil
	}

	req := &control.PoolResolveIDReq{
		HumanID: cmd.PoolID().Label,
	}

	resp, err := control.PoolResolveID(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return errors.Wrap(err, "failed to resolve pool ID into UUID")
	}
	cmd.PoolID().UUID, err = uuid.Parse(resp.UUID)
	if err != nil {
		return errors.Wrapf(err, "failed to parse response uuid %q", resp.UUID)
	}
	cmd.PoolID().Label = ""
	cmd.uuidStr = resp.UUID

	return nil
}

// PoolDestroyCmd is the struct representing the command to destroy a DAOS pool.
type PoolDestroyCmd struct {
	poolCmd
	Force bool `short:"f" long:"force" description:"Force removal of DAOS pool"`
}

// Execute is run when PoolDestroyCmd subcommand is activated
func (cmd *PoolDestroyCmd) Execute(args []string) error {
	msg := "succeeded"

	if err := cmd.resolveID(); err != nil {
		return err
	}

	req := &control.PoolDestroyReq{UUID: cmd.uuidStr, Force: cmd.Force}

	err := control.PoolDestroy(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.log.Infof("Pool-destroy command %s\n", msg)

	return err
}

// PoolEvictCmd is the struct representing the command to evict a DAOS pool.
type PoolEvictCmd struct {
	poolCmd
}

// Execute is run when PoolEvictCmd subcommand is activated
func (cmd *PoolEvictCmd) Execute(args []string) error {
	msg := "succeeded"

	if err := cmd.resolveID(); err != nil {
		return err
	}

	req := &control.PoolEvictReq{UUID: cmd.uuidStr}

	err := control.PoolEvict(context.Background(), cmd.ctlInvoker, req)
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

	req := &control.PoolExcludeReq{UUID: cmd.uuidStr, Rank: system.Rank(cmd.Rank), Targetidx: idxlist}

	err := control.PoolExclude(context.Background(), cmd.ctlInvoker, req)
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
		return err
	}

	req := &control.PoolDrainReq{UUID: cmd.uuidStr, Rank: system.Rank(cmd.Rank), Targetidx: idxlist}

	err := control.PoolDrain(context.Background(), cmd.ctlInvoker, req)
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
		return err
	}

	req := &control.PoolExtendReq{
		UUID: cmd.uuidStr, Ranks: ranks,
	}

	err = control.PoolExtend(context.Background(), cmd.ctlInvoker, req)
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
		return err
	}

	req := &control.PoolReintegrateReq{UUID: cmd.uuidStr, Rank: system.Rank(cmd.Rank), Targetidx: idxlist}

	err := control.PoolReintegrate(context.Background(), cmd.ctlInvoker, req)
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
		UUID: cmd.uuidStr,
	}

	resp, err := control.PoolQuery(context.Background(), cmd.ctlInvoker, req)

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
	Property string `short:"n" long:"name" description:"Name of property to be set (deprecated; use positional argument)"`
	Value    string `short:"v" long:"value" description:"Value of property to be set (deprecated; use positional argument)"`

	Args struct {
		Props PoolSetPropsFlag `positional-arg-name:"pool properties to set (key:val[,key:val...])"`
	} `positional-args:"yes"`
}

// Execute is run when PoolSetPropCmd subcommand is activatecmd.
func (cmd *PoolSetPropCmd) Execute(_ []string) error {
	if err := cmd.resolveID(); err != nil {
		return err
	}

	// TODO (DAOS-7964): Remove support for --name/--value flags.
	if cmd.Property != "" || cmd.Value != "" {
		if len(cmd.Args.Props.ToSet) > 0 {
			return errors.New("cannot mix flags and positional arguments")
		}
		if cmd.Property == "" || cmd.Value == "" {
			return errors.New("both --name and --value must be supplied if either are supplied")
		}

		propName := strings.ToLower(cmd.Property)
		p, err := control.PoolProperties().GetProperty(propName)
		if err != nil {
			return err
		}
		if err := p.SetValue(cmd.Value); err != nil {
			return err
		}
		cmd.Args.Props.ToSet = []*control.PoolProperty{p}
	}

	req := &control.PoolSetPropReq{
		UUID:       cmd.uuidStr,
		Properties: cmd.Args.Props.ToSet,
	}

	err := control.PoolSetProp(context.Background(), cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(nil, err)
	}

	if err != nil {
		return errors.Wrap(err, "pool set-prop failed")
	}
	cmd.log.Info("pool set-prop succeeded")

	return nil
}

// PoolGetPropCmd represents the command to set a property on a pool.
type PoolGetPropCmd struct {
	poolCmd
	Args struct {
		Props PoolGetPropsFlag `positional-arg-name:"pool properties to get (key[,key...])"`
	} `positional-args:"yes"`
}

// Execute is run when PoolGetPropCmd subcommand is activatecmd.
func (cmd *PoolGetPropCmd) Execute(_ []string) error {
	if err := cmd.resolveID(); err != nil {
		return err
	}

	req := &control.PoolGetPropReq{
		UUID:       cmd.uuidStr,
		Properties: cmd.Args.Props.ToGet,
	}

	resp, err := control.PoolGetProp(context.Background(), cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "pool get-prop failed")
	}

	var bld strings.Builder
	pretty.PrintPoolProperties(cmd.PoolID().String(), &bld, resp...)
	cmd.log.Infof("%s", bld.String())

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

	req := &control.PoolGetACLReq{UUID: cmd.uuidStr}

	resp, err := control.PoolGetACL(context.Background(), cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-get-ACL command failed")
	}

	cmd.log.Debugf("Pool-get-ACL command succeeded, UUID: %s\n", cmd.uuidStr)

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
		return err
	}

	req := &control.PoolOverwriteACLReq{
		UUID: cmd.uuidStr,
		ACL:  acl,
	}

	resp, err := control.PoolOverwriteACL(context.Background(), cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-overwrite-ACL command failed")
	}

	cmd.log.Infof("Pool-overwrite-ACL command succeeded, UUID: %s\n", cmd.uuidStr)

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
			return err
		}
		acl = aclFileResult
	} else {
		acl = &control.AccessControlList{
			Entries: []string{cmd.Entry},
		}
	}

	req := &control.PoolUpdateACLReq{
		UUID: cmd.uuidStr,
		ACL:  acl,
	}

	resp, err := control.PoolUpdateACL(context.Background(), cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-update-ACL command failed")
	}

	cmd.log.Infof("Pool-update-ACL command succeeded, UUID: %s\n", cmd.uuidStr)

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
		UUID:      cmd.uuidStr,
		Principal: cmd.Principal,
	}

	resp, err := control.PoolDeleteACL(context.Background(), cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-delete-ACL command failed")
	}

	cmd.log.Infof("Pool-delete-ACL command succeeded, UUID: %s\n", cmd.uuidStr)

	cmd.log.Info(control.FormatACLDefault(resp.ACL))

	return nil
}

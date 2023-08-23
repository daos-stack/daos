//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"math"
	"os"
	"strconv"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// PoolCmd is the struct representing the top-level pool subcommand.
type PoolCmd struct {
	Create       PoolCreateCmd       `command:"create" description:"Create a DAOS pool"`
	Destroy      PoolDestroyCmd      `command:"destroy" description:"Destroy a DAOS pool"`
	Evict        PoolEvictCmd        `command:"evict" description:"Evict all pool connections to a DAOS pool"`
	List         PoolListCmd         `command:"list" alias:"ls" description:"List DAOS pools"`
	Extend       PoolExtendCmd       `command:"extend" description:"Extend a DAOS pool to include new ranks."`
	Exclude      PoolExcludeCmd      `command:"exclude" description:"Exclude targets from a rank"`
	Drain        PoolDrainCmd        `command:"drain" description:"Drain targets from a rank"`
	Reintegrate  PoolReintegrateCmd  `command:"reintegrate" alias:"reint" description:"Reintegrate targets for a rank"`
	Query        PoolQueryCmd        `command:"query" description:"Query a DAOS pool"`
	QueryTargets PoolQueryTargetsCmd `command:"query-targets" description:"Query pool target info"`
	GetACL       PoolGetACLCmd       `command:"get-acl" description:"Get a DAOS pool's Access Control List"`
	OverwriteACL PoolOverwriteACLCmd `command:"overwrite-acl" description:"Overwrite a DAOS pool's Access Control List"`
	UpdateACL    PoolUpdateACLCmd    `command:"update-acl" description:"Update entries in a DAOS pool's Access Control List"`
	DeleteACL    PoolDeleteACLCmd    `command:"delete-acl" description:"Delete an entry from a DAOS pool's Access Control List"`
	SetProp      PoolSetPropCmd      `command:"set-prop" description:"Set pool property"`
	GetProp      PoolGetPropCmd      `command:"get-prop" description:"Get pool properties"`
	Upgrade      PoolUpgradeCmd      `command:"upgrade" description:"Upgrade pool to latest format"`
}

var (
	// Default to 6% SCM:94% NVMe
	defaultTierRatios = []float64{0.06, 0.94}
)

type tierRatioFlag struct {
	ratios []float64
}

func (trf *tierRatioFlag) IsSet() bool {
	return len(trf.ratios) > 0
}

func (trf tierRatioFlag) Ratios() []float64 {
	if trf.IsSet() {
		return trf.ratios
	}

	return defaultTierRatios
}

func (trf tierRatioFlag) String() string {
	var ratioStrs []string
	for _, ratio := range trf.Ratios() {
		ratioStrs = append(ratioStrs, pretty.PrintTierRatio(ratio))
	}
	return strings.Join(ratioStrs, ",")
}

func (trf *tierRatioFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("no tier ratio specified")
	}

	roundFloatTo := func(f float64, places int) float64 {
		if f <= 0 && f >= 100 {
			return f
		}
		shift := math.Pow(10, float64(places))
		return math.Round(f*shift) / shift
	}

	for _, trStr := range strings.Split(fv, ",") {
		tr, err := strconv.ParseFloat(strings.TrimSpace(strings.Trim(trStr, "%")), 64)
		if err != nil {
			return errors.Wrapf(err, "invalid tier ratio %s", trStr)
		}
		trf.ratios = append(trf.ratios, roundFloatTo(tr, 2)/100)
	}

	// Handle single tier ratio as a special case and fill
	// second tier with remainder (-t 6 will assign 6% of total
	// storage to tier0 and 94% to tier1).
	if len(trf.ratios) == 1 && trf.ratios[0] < 1 {
		trf.ratios = append(trf.ratios, 1-trf.ratios[0])
	}

	var totalRatios float64
	for _, ratio := range trf.ratios {
		if ratio < 0 || ratio > 1 {
			return errors.New("Storage tier ratio must be a value between 0-100")
		}
		totalRatios += ratio
	}
	if math.Abs(totalRatios-1) > 0.01 {
		return errors.Errorf("Storage tier ratios must add up to 100 (got %f)", totalRatios*100)
	}

	return nil
}

type sizeFlag struct {
	bytes uint64
}

func (sf sizeFlag) IsSet() bool {
	return sf.bytes > 0
}

func (sf sizeFlag) String() string {
	return humanize.Bytes(sf.bytes)
}

func (sf *sizeFlag) UnmarshalFlag(fv string) (err error) {
	if fv == "" {
		return errors.New("no size specified")
	}

	sf.bytes, err = humanize.ParseBytes(fv)
	if err != nil {
		return errors.Wrapf(err, "invalid size %q", fv)
	}

	return nil
}

type poolSizeFlag struct {
	sizeFlag
	availRatio uint64
}

func (psf poolSizeFlag) IsRatio() bool {
	return psf.availRatio > 0
}

func (psf poolSizeFlag) IsSet() bool {
	return psf.sizeFlag.IsSet() || psf.IsRatio()
}

func (psf poolSizeFlag) String() string {
	if psf.IsRatio() {
		return fmt.Sprintf("%d%%", psf.availRatio)
	}

	return psf.sizeFlag.String()
}

func (psf *poolSizeFlag) UnmarshalFlag(fv string) error {
	trimmed := strings.TrimSpace(fv)
	if strings.HasSuffix(trimmed, "%") {
		ratioStr := strings.TrimSpace(strings.TrimSuffix(trimmed, "%"))
		ratio, err := strconv.ParseUint(ratioStr, 10, 64)
		if err != nil {
			return errors.Wrapf(err, "invalid pool size ratio %q", fv)
		}
		if ratio <= 0 || ratio > 100 {
			return errors.Errorf("Creating DAOS pool with invalid full size ratio %s:"+
				" allowed range 0 < ratio <= 100", fv)
		}
		psf.availRatio = ratio
		return nil
	}

	return psf.sizeFlag.UnmarshalFlag(fv)
}

// PoolCreateCmd is the struct representing the command to create a DAOS pool.
type PoolCreateCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	cmdutil.JSONOutputCmd
	GroupName  ui.ACLPrincipalFlag `short:"g" long:"group" description:"DAOS pool to be owned by given group, format name@domain"`
	UserName   ui.ACLPrincipalFlag `short:"u" long:"user" description:"DAOS pool to be owned by given user, format name@domain"`
	Properties PoolSetPropsFlag    `short:"P" long:"properties" description:"Pool properties to be set"`
	ACLFile    string              `short:"a" long:"acl-file" description:"Access Control List file path for DAOS pool"`
	Size       poolSizeFlag        `short:"z" long:"size" description:"Total size of DAOS pool or its percentage ratio (auto)"`
	TierRatio  tierRatioFlag       `short:"t" long:"tier-ratio" description:"Percentage of storage tiers for pool storage (auto; default: 6,94)"`
	NumRanks   uint32              `short:"k" long:"nranks" description:"Number of ranks to use (auto)"`
	NumSvcReps uint32              `short:"v" long:"nsvc" description:"Number of pool service replicas"`
	ScmSize    sizeFlag            `short:"s" long:"scm-size" description:"Per-engine SCM allocation for DAOS pool (manual)"`
	NVMeSize   sizeFlag            `short:"n" long:"nvme-size" description:"Per-engine NVMe allocation for DAOS pool (manual)"`
	RankList   ui.RankSetFlag      `short:"r" long:"ranks" description:"Storage engine unique identifiers (ranks) for DAOS pool"`

	Args struct {
		PoolLabel string `positional-arg-name:"<pool label>" required:"1"`
	} `positional-args:"yes"`
}

// Execute is run when PoolCreateCmd subcommand is activated
func (cmd *PoolCreateCmd) Execute(args []string) error {
	if cmd.Size.IsSet() && (cmd.ScmSize.IsSet() || cmd.NVMeSize.IsSet()) {
		return errIncompatFlags("size", "scm-size", "nvme-size")
	}
	if !cmd.Size.IsSet() && !cmd.ScmSize.IsSet() {
		return errors.New("either --size or --scm-size must be supplied")
	}

	if cmd.Args.PoolLabel != "" {
		for _, prop := range cmd.Properties.ToSet {
			if prop.Name == "label" {
				return errors.New("can't set label property with label argument")
			}
		}
		if err := cmd.Properties.UnmarshalFlag(fmt.Sprintf("label:%s", cmd.Args.PoolLabel)); err != nil {
			return err
		}
	}

	var err error
	req := &control.PoolCreateReq{
		User:       cmd.UserName.String(),
		UserGroup:  cmd.GroupName.String(),
		NumSvcReps: cmd.NumSvcReps,
		Properties: cmd.Properties.ToSet,
		Ranks:      cmd.RankList.Ranks(),
	}

	if cmd.ACLFile != "" {
		req.ACL, err = control.ReadACLFile(cmd.ACLFile)
		if err != nil {
			return err
		}
	}

	switch {
	case cmd.Size.IsRatio():
		if cmd.NumRanks > 0 {
			return errIncompatFlags("size", "nranks")
		}

		if cmd.TierRatio.IsSet() {
			return errIncompatFlags("size=%", "tier-ratio")
		}

		// TODO (DAOS-9556) Update the protocol with a new message allowing to perform the
		// queries of storage request and the pool creation from the management server
		scmBytes, nvmeBytes, err := control.GetMaxPoolSize(
			context.Background(),
			cmd.Logger,
			cmd.ctlInvoker,
			ranklist.RankList(req.Ranks))
		if err != nil {
			return err
		}

		if cmd.Size.availRatio < 100 {
			scmBytes = cmd.Size.availRatio * scmBytes / 100
			nvmeBytes = cmd.Size.availRatio * nvmeBytes / 100
		}

		if scmBytes == 0 {
			return errors.Errorf("Not enough SCM storage available with ratio %s: "+
				"SCM storage capacity or ratio should be increased", cmd.Size)
		}

		cmd.updateRequest(req, scmBytes, nvmeBytes)

		cmd.Infof("Creating DAOS pool with %s of all storage", cmd.Size)
	case !cmd.Size.IsRatio() && cmd.Size.IsSet():
		// auto-selection of storage values
		req.TotalBytes = cmd.Size.bytes

		if cmd.NumRanks > 0 && !cmd.RankList.Empty() {
			return errIncompatFlags("nranks", "ranks")
		}
		req.NumRanks = cmd.NumRanks

		req.TierRatio = cmd.TierRatio.Ratios()
		cmd.Infof("Creating DAOS pool with automatic storage allocation: "+
			"%s total, %s tier ratio", humanize.Bytes(req.TotalBytes), cmd.TierRatio)
	default:
		// manual selection of storage values
		if cmd.NumRanks > 0 {
			return errIncompatFlags("nranks", "scm-size")
		}
		if cmd.TierRatio.IsSet() {
			return errIncompatFlags("tier-ratio", "scm-size")
		}

		scmBytes := cmd.ScmSize.bytes
		nvmeBytes := cmd.NVMeSize.bytes
		scmRatio := cmd.updateRequest(req, scmBytes, nvmeBytes)

		cmd.Infof("Creating DAOS pool with manual per-engine storage allocation: "+
			"%s SCM, %s NVMe (%0.2f%% ratio)",
			humanize.Bytes(scmBytes),
			humanize.Bytes(nvmeBytes),
			scmRatio*100)
	}

	resp, err := control.PoolCreate(context.Background(), cmd.ctlInvoker, req)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	var bld strings.Builder
	if err := pretty.PrintPoolCreateResponse(resp, &bld); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return nil
}

func (cmd *PoolCreateCmd) updateRequest(req *control.PoolCreateReq,
	scmBytes uint64,
	nvmeBytes uint64) float64 {
	if nvmeBytes == 0 {
		cmd.Info("Creating DAOS pool without NVME storage")
	}

	scmRatio := 1.0
	if nvmeBytes > 0 {
		scmRatio = float64(scmBytes) / float64(nvmeBytes)
	}

	if scmRatio < storage.MinScmToNVMeRatio {
		cmd.Infof("SCM:NVMe ratio is less than %0.2f %%, DAOS "+
			"performance will suffer!\n", storage.MinScmToNVMeRatio*100)
	}

	req.TierBytes = []uint64{scmBytes, nvmeBytes}
	req.TotalBytes = 0
	req.TierRatio = nil

	return scmRatio
}

// PoolListCmd represents the command to fetch a list of all DAOS pools in the system.
type PoolListCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	cmdutil.JSONOutputCmd
	Verbose bool `short:"v" long:"verbose" description:"Add pool UUIDs and service replica lists to display"`
	NoQuery bool `short:"n" long:"no-query" description:"Disable query of listed pools"`
}

// Execute is run when PoolListCmd activates
func (cmd *PoolListCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "list pools failed")
	}()

	if cmd.config == nil {
		return errors.New("no configuration loaded")
	}

	req := &control.ListPoolsReq{
		NoQuery: cmd.NoQuery,
	}

	resp, err := control.ListPools(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, nil)
	}

	var out, outErr strings.Builder
	if err := pretty.PrintListPoolsResponse(&out, &outErr, resp, cmd.Verbose); err != nil {
		return err
	}
	if outErr.String() != "" {
		cmd.Error(outErr.String())
	}
	// Infof prints raw string and doesn't try to expand "%"
	// preserving column formatting in txtfmt table
	cmd.Infof("%s", out.String())

	return resp.Errors()
}

type PoolID struct {
	ui.LabelOrUUIDFlag
}

// poolCmd is the base struct for all pool commands that work with existing pools.
type poolCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	cmdutil.JSONOutputCmd

	Args struct {
		Pool PoolID `positional-arg-name:"<pool label or UUID>" required:"1"`
	} `positional-args:"yes"`
}

func (cmd *poolCmd) PoolID() *PoolID {
	return &cmd.Args.Pool
}

// PoolDestroyCmd is the struct representing the command to destroy a DAOS pool.
type PoolDestroyCmd struct {
	poolCmd
	Recursive bool `short:"r" long:"recursive" description:"Remove pool with existing containers"`
	Force     bool `short:"f" long:"force" description:"Forcibly remove pool with active client connections"`
}

// Execute is run when PoolDestroyCmd subcommand is activated
func (cmd *PoolDestroyCmd) Execute(args []string) error {
	msg := "succeeded"

	req := &control.PoolDestroyReq{
		ID:        cmd.PoolID().String(),
		Force:     cmd.Force,
		Recursive: cmd.Recursive,
	}

	err := control.PoolDestroy(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.ctlInvoker.Debugf("Pool-destroy command %s", msg)
	cmd.Infof("Pool-destroy command %s\n", msg)

	return err
}

// PoolEvictCmd is the struct representing the command to evict a DAOS pool.
type PoolEvictCmd struct {
	poolCmd
}

// Execute is run when PoolEvictCmd subcommand is activated
func (cmd *PoolEvictCmd) Execute(args []string) error {
	msg := "succeeded"

	req := &control.PoolEvictReq{ID: cmd.PoolID().String()}

	err := control.PoolEvict(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.Infof("Pool-evict command %s\n", msg)

	return err
}

// PoolExcludeCmd is the struct representing the command to exclude a DAOS target.
type PoolExcludeCmd struct {
	poolCmd
	Rank      uint32 `long:"rank" required:"1" description:"Engine rank of the targets to be excluded"`
	Targetidx string `long:"target-idx" description:"Comma-separated list of target idx(s) to be excluded from the rank"`
}

// Execute is run when PoolExcludeCmd subcommand is activated
func (cmd *PoolExcludeCmd) Execute(args []string) error {
	msg := "succeeded"

	var idxlist []uint32
	if err := common.ParseNumberList(cmd.Targetidx, &idxlist); err != nil {
		return errors.WithMessage(err, "parsing target list")
	}

	req := &control.PoolExcludeReq{ID: cmd.PoolID().String(), Rank: ranklist.Rank(cmd.Rank), Targetidx: idxlist}

	err := control.PoolExclude(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.Infof("Exclude command %s\n", msg)

	return err
}

// PoolDrainCmd is the struct representing the command to Drain a DAOS target.
type PoolDrainCmd struct {
	poolCmd
	Rank      uint32 `long:"rank" required:"1" description:"Engine rank of the targets to be drained"`
	Targetidx string `long:"target-idx" description:"Comma-separated list of target idx(s) to be drained on the rank"`
}

// Execute is run when PoolDrainCmd subcommand is activated
func (cmd *PoolDrainCmd) Execute(args []string) error {
	msg := "succeeded"

	var idxlist []uint32
	if err := common.ParseNumberList(cmd.Targetidx, &idxlist); err != nil {
		err = errors.WithMessage(err, "parsing target list")
		return err
	}

	req := &control.PoolDrainReq{ID: cmd.PoolID().String(), Rank: ranklist.Rank(cmd.Rank), Targetidx: idxlist}

	err := control.PoolDrain(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.Infof("Drain command %s\n", msg)

	return err
}

// PoolExtendCmd is the struct representing the command to Extend a DAOS pool.
type PoolExtendCmd struct {
	poolCmd
	RankList ui.RankSetFlag `long:"ranks" required:"1" description:"Comma-separated list of ranks to add to the pool"`
}

// Execute is run when PoolExtendCmd subcommand is activated
func (cmd *PoolExtendCmd) Execute(args []string) error {
	msg := "succeeded"

	req := &control.PoolExtendReq{
		ID: cmd.PoolID().String(), Ranks: cmd.RankList.Ranks(),
	}

	err := control.PoolExtend(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.Infof("Extend command %s\n", msg)

	return err
}

// PoolReintegrateCmd is the struct representing the command to Add a DAOS target.
type PoolReintegrateCmd struct {
	poolCmd
	Rank      uint32 `long:"rank" required:"1" description:"Engine rank of the targets to be reintegrated"`
	Targetidx string `long:"target-idx" description:"Comma-separated list of target idx(s) to be reintegrated into the rank"`
}

// Execute is run when PoolReintegrateCmd subcommand is activated
func (cmd *PoolReintegrateCmd) Execute(args []string) error {
	msg := "succeeded"

	var idxlist []uint32
	if err := common.ParseNumberList(cmd.Targetidx, &idxlist); err != nil {
		err = errors.WithMessage(err, "parsing target list")
		return err
	}

	req := &control.PoolReintegrateReq{ID: cmd.PoolID().String(), Rank: ranklist.Rank(cmd.Rank), Targetidx: idxlist}

	err := control.PoolReintegrate(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	cmd.Infof("Reintegration command %s\n", msg)

	return err
}

// PoolQueryCmd is the struct representing the command to query a DAOS pool.
type PoolQueryCmd struct {
	poolCmd
	ShowEnabledRanks  bool `short:"e" long:"show-enabled" description:"Show engine unique identifiers (ranks) which are enabled"`
	ShowDisabledRanks bool `short:"b" long:"show-disabled" description:"Show engine unique identifiers (ranks) which are disabled"`
}

// Execute is run when PoolQueryCmd subcommand is activated
func (cmd *PoolQueryCmd) Execute(args []string) error {
	req := &control.PoolQueryReq{
		ID: cmd.PoolID().String(),
	}

	// TODO (DAOS-10250) The two options should not be incompatible (i.e. engine limitation)
	if cmd.ShowEnabledRanks && cmd.ShowDisabledRanks {
		return errIncompatFlags("show-enabled-ranks", "show-disabled-ranks")
	}
	req.IncludeEnabledRanks = cmd.ShowEnabledRanks
	req.IncludeDisabledRanks = cmd.ShowDisabledRanks

	resp, err := control.PoolQuery(context.Background(), cmd.ctlInvoker, req)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "pool query failed")
	}

	var bld strings.Builder
	if err := pretty.PrintPoolQueryResponse(resp, &bld); err != nil {
		return err
	}
	cmd.Info(bld.String())
	return nil
}

// PoolQueryTargetsCmd is the struct representing the command to query a DAOS pool engine's targets
type PoolQueryTargetsCmd struct {
	poolCmd

	Rank    uint32 `long:"rank" required:"1" description:"Engine rank of the targets to be queried"`
	Targets string `long:"target-idx" required:"1" description:"Comma-separated list of target idx(s) to be queried"`
}

// Execute is run when PoolQueryTargetsCmd subcommand is activated
func (cmd *PoolQueryTargetsCmd) Execute(args []string) error {

	var tgtsList []uint32
	if err := common.ParseNumberList(cmd.Targets, &tgtsList); err != nil {
		return errors.WithMessage(err, "parsing target list")
	}

	req := &control.PoolQueryTargetReq{
		ID:      cmd.PoolID().String(),
		Rank:    ranklist.Rank(cmd.Rank),
		Targets: tgtsList,
	}

	resp, err := control.PoolQueryTargets(context.Background(), cmd.ctlInvoker, req)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "pool query targets failed")
	}

	var bld strings.Builder
	if err := pretty.PrintPoolQueryTargetResponse(resp, &bld); err != nil {
		return err
	}
	cmd.Info(bld.String())
	return nil
}

// PoolUpgradeCmd is the struct representing the command to update a DAOS pool.
type PoolUpgradeCmd struct {
	poolCmd
}

// Execute is run when PoolUpgradeCmd subcommand is activated
func (cmd *PoolUpgradeCmd) Execute(args []string) error {
	req := &control.PoolUpgradeReq{
		ID: cmd.PoolID().String(),
	}

	err := control.PoolUpgrade(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return errors.Wrap(err, "pool upgrade failed")
	}

	cmd.Info("Pool-upgrade command succeeded")
	return nil
}

// PoolSetPropCmd represents the command to set a property on a pool.
type PoolSetPropCmd struct {
	poolCmd

	Args struct {
		Props PoolSetPropsFlag `positional-arg-name:"<key:val[,key:val...]>" required:"1"`
	} `positional-args:"yes"`
}

// Execute is run when PoolSetPropCmd subcommand is activatecmd.
func (cmd *PoolSetPropCmd) Execute(_ []string) error {
	for _, prop := range cmd.Args.Props.ToSet {
		if prop.Name == "perf_domain" {
			return errors.New("can't set perf_domain on existing pool.")
		}
		if prop.Name == "rd_fac" {
			return errors.New("can't set redundancy factor on existing pool.")
		}
		if prop.Name == "ec_pda" {
			return errors.New("can't set EC performance domain affinity on existing pool.")
		}
		if prop.Name == "rp_pda" {
			return errors.New("can't set RP performance domain affinity on existing pool.")
		}
	}

	req := &control.PoolSetPropReq{
		ID:         cmd.PoolID().String(),
		Properties: cmd.Args.Props.ToSet,
	}

	err := control.PoolSetProp(context.Background(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(nil, err)
	}

	if err != nil {
		return errors.Wrap(err, "pool set-prop failed")
	}
	cmd.Info("pool set-prop succeeded")

	return nil
}

// PoolGetPropCmd represents the command to set a property on a pool.
type PoolGetPropCmd struct {
	poolCmd
	Args struct {
		Props PoolGetPropsFlag `positional-arg-name:"[key[,key...]]"`
	} `positional-args:"yes"`
}

// Execute is run when PoolGetPropCmd subcommand is activatecmd.
func (cmd *PoolGetPropCmd) Execute(_ []string) error {
	req := &control.PoolGetPropReq{
		ID:         cmd.PoolID().String(),
		Properties: cmd.Args.Props.ToGet,
	}

	resp, err := control.PoolGetProp(context.Background(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "pool get-prop failed")
	}

	var bld strings.Builder
	pretty.PrintPoolProperties(cmd.PoolID().String(), &bld, resp...)
	cmd.Infof("%s", bld.String())

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
	req := &control.PoolGetACLReq{ID: cmd.PoolID().String()}

	resp, err := control.PoolGetACL(context.Background(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-get-ACL command failed")
	}

	cmd.Debugf("Pool-get-ACL command succeeded, ID: %s\n", cmd.PoolID())

	acl := control.FormatACL(resp.ACL, cmd.Verbose)

	if cmd.File != "" {
		err = cmd.writeACLToFile(acl)
		if err != nil {
			return err
		}
		cmd.Infof("Wrote ACL to output file: %s", cmd.File)
	} else {
		cmd.Info(acl)
	}

	return nil
}

func (cmd *PoolGetACLCmd) writeACLToFile(acl string) error {
	if !cmd.Force {
		// Keep the user from clobbering existing files
		_, err := os.Stat(cmd.File)
		if err == nil {
			return errors.New(fmt.Sprintf("file already exists: %s", cmd.File))
		}
	}

	f, err := os.Create(cmd.File)
	if err != nil {
		cmd.Errorf("Unable to create file: %s", cmd.File)
		return err
	}
	defer f.Close()

	_, err = f.WriteString(acl)
	if err != nil {
		cmd.Errorf("Failed to write to file: %s", cmd.File)
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
	acl, err := control.ReadACLFile(cmd.ACLFile)
	if err != nil {
		return err
	}

	req := &control.PoolOverwriteACLReq{
		ID:  cmd.PoolID().String(),
		ACL: acl,
	}

	resp, err := control.PoolOverwriteACL(context.Background(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-overwrite-ACL command failed")
	}

	cmd.Infof("Pool-overwrite-ACL command succeeded, ID: %s\n", cmd.PoolID())

	cmd.Info(control.FormatACLDefault(resp.ACL))

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
		ID:  cmd.PoolID().String(),
		ACL: acl,
	}

	resp, err := control.PoolUpdateACL(context.Background(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-update-ACL command failed")
	}

	cmd.Infof("Pool-update-ACL command succeeded, ID: %s\n", cmd.PoolID())

	cmd.Info(control.FormatACLDefault(resp.ACL))

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
	req := &control.PoolDeleteACLReq{
		ID:        cmd.PoolID().String(),
		Principal: cmd.Principal,
	}

	resp, err := control.PoolDeleteACL(context.Background(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "Pool-delete-ACL command failed")
	}

	cmd.Infof("Pool-delete-ACL command succeeded, ID: %s\n", cmd.PoolID())

	cmd.Info(control.FormatACLDefault(resp.ACL))

	return nil
}

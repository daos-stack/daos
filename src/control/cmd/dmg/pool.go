//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"context"
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
)

const (
	maxNumSvcReps = 13
)

// PoolCmd is the struct representing the top-level pool subcommand.
type PoolCmd struct {
	Create       PoolCreateCmd       `command:"create" alias:"c" description:"Create a DAOS pool"`
	Destroy      PoolDestroyCmd      `command:"destroy" alias:"d" description:"Destroy a DAOS pool"`
	List         systemListPoolsCmd  `command:"list" alias:"l" description:"List DAOS pools"`
	Reintegrate  PoolReintegrateCmd  `command:"reintegrate" alias:"r" description:"Reintegrate a list of targets for a rank"`
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
	ctlClientCmd
	jsonOutputCmd
	GroupName  string `short:"g" long:"group" description:"DAOS pool to be owned by given group, format name@domain"`
	UserName   string `short:"u" long:"user" description:"DAOS pool to be owned by given user, format name@domain"`
	ACLFile    string `short:"a" long:"acl-file" description:"Access Control List file path for DAOS pool"`
	ScmSize    string `short:"s" long:"scm-size" required:"1" description:"Size of SCM component of DAOS pool"`
	NVMeSize   string `short:"n" long:"nvme-size" description:"Size of NVMe component of DAOS pool"`
	RankList   string `short:"r" long:"ranks" description:"Storage server unique identifiers (ranks) for DAOS pool"`
	NumSvcReps uint32 `short:"v" long:"nsvc" default:"1" description:"Number of pool service replicas"`
	Sys        string `short:"S" long:"sys" default:"daos_server" description:"DAOS system that pool is to be a part of"`
	UUID       string `short:"p" long:"pool" description:"UUID to be used when creating the pool, randomly generated if not specified"`
}

// Execute is run when PoolCreateCmd subcommand is activated
func (c *PoolCreateCmd) Execute(args []string) error {
	msg := "SUCCEEDED: "

	scmBytes, err := humanize.ParseBytes(c.ScmSize)
	if err != nil {
		return errors.Wrap(err, "pool SCM size")
	}

	var nvmeBytes uint64
	if c.NVMeSize != "" {
		nvmeBytes, err = humanize.ParseBytes(c.NVMeSize)
		if err != nil {
			return errors.Wrap(err, "pool NVMe size")
		}
	}

	var acl *control.AccessControlList
	if c.ACLFile != "" {
		acl, err = control.ReadACLFile(c.ACLFile)
		if err != nil {
			return err
		}
	}

	if c.NumSvcReps > maxNumSvcReps {
		return errors.Errorf("max number of service replicas is %d, got %d",
			maxNumSvcReps, c.NumSvcReps)
	}

	var ranks []uint32
	if err := common.ParseNumberList(c.RankList, &ranks); err != nil {
		return errors.WithMessage(err, "parsing rank list")
	}

	req := &control.PoolCreateReq{
		ScmBytes: scmBytes, NvmeBytes: nvmeBytes, Ranks: ranks,
		NumSvcReps: c.NumSvcReps, Sys: c.Sys,
		User: c.UserName, UserGroup: c.GroupName, ACL: acl,
		UUID: c.UUID,
	}
	// FIXME (DAOS-4546): Pool requests should not set the hostlist.
	req.SetHostList(c.hostlist)

	ctx := context.Background()
	resp, err := control.PoolCreate(ctx, c.ctlClient, req)

	if c.jsonOutputEnabled() {
		return c.outputJSON(os.Stdout, resp)
	}

	if err != nil {
		msg = errors.WithMessage(err, "FAILED").Error()
	} else {
		msg += fmt.Sprintf("UUID: %s, Service replicas: %s",
			resp.UUID, formatPoolSvcReps(resp.SvcReps))
	}

	c.log.Infof("Pool-create command %s\n", msg)

	return err
}

// PoolDestroyCmd is the struct representing the command to destroy a DAOS pool.
type PoolDestroyCmd struct {
	logCmd
	ctlClientCmd
	// TODO: implement --sys & --svc options (currently unsupported server side)
	UUID  string `long:"pool" required:"1" description:"UUID of DAOS pool to destroy"`
	Force bool   `short:"f" long:"force" description:"Force removal of DAOS pool"`
}

// Execute is run when PoolDestroyCmd subcommand is activated
func (d *PoolDestroyCmd) Execute(args []string) error {
	msg := "succeeded"

	req := &control.PoolDestroyReq{UUID: d.UUID, Force: d.Force}
	// FIXME (DAOS-4546): Pool requests should not set the hostlist.
	req.SetHostList(d.hostlist)

	ctx := context.Background()
	err := control.PoolDestroy(ctx, d.ctlClient, req)
	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	d.log.Infof("Pool-destroy command %s\n", msg)

	return err
}

// PoolReintegrateCmd is the struct representing the command to Add a DAOS target.
type PoolReintegrateCmd struct {
	logCmd
	ctlClientCmd
	UUID      string `long:"pool" required:"1" description:"UUID of the DAOS pool to start reintegration in"`
	Rank      uint32 `long:"rank" required:"1" description:"Rank of the targets to be reintegrated"`
	Targetidx string `long:"target-idx" required:"1" description:"Comma-seperated list of target idx(s) to be reintegrated into the rank"`
}

// Execute is run when PoolReintegrateCmd subcommand is activated
func (r *PoolReintegrateCmd) Execute(args []string) error {
	msg := "succeeded"

	var idxlist []uint32
	if err := common.ParseNumberList(r.Targetidx, &idxlist); err != nil {
		return errors.WithMessage(err, "parsing rank list")
	}

	req := &control.PoolReintegrateReq{UUID: r.UUID, Rank: r.Rank, Targetidx: idxlist}

	ctx := context.Background()
	err := control.PoolReintegrate(ctx, r.ctlClient, req)
	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	r.log.Infof("Reintegration command %s\n", msg)

	return err
}

// PoolQueryCmd is the struct representing the command to query a DAOS pool.
type PoolQueryCmd struct {
	logCmd
	ctlClientCmd
	jsonOutputCmd
	UUID string `long:"pool" required:"1" description:"UUID of DAOS pool to query"`
}

// Execute is run when PoolQueryCmd subcommand is activated
func (c *PoolQueryCmd) Execute(args []string) error {
	req := &control.PoolQueryReq{
		UUID: c.UUID,
	}
	// FIXME (DAOS-4546): Pool requests should not set the hostlist.
	req.SetHostList(c.hostlist)

	ctx := context.Background()
	resp, err := control.PoolQuery(ctx, c.ctlClient, req)
	if err != nil {
		return errors.Wrap(err, "pool query failed")
	}

	if c.jsonOutputEnabled() {
		return c.outputJSON(os.Stdout, resp)
	}

	var bld strings.Builder
	if err := pretty.PrintPoolQueryResponse(resp, &bld); err != nil {
		return err
	}
	c.log.Info(bld.String())
	return nil
}

// PoolSetPropCmd represents the command to set a property on a pool.
type PoolSetPropCmd struct {
	logCmd
	ctlClientCmd
	jsonOutputCmd
	UUID     string `long:"pool" required:"1" description:"UUID of DAOS pool"`
	Property string `short:"n" long:"name" required:"1" description:"Name of property to be set"`
	Value    string `short:"v" long:"value" required:"1" description:"Value of property to be set"`
}

// Execute is run when PoolSetPropCmd subcommand is activated.
func (c *PoolSetPropCmd) Execute(_ []string) error {
	req := &control.PoolSetPropReq{
		UUID:     c.UUID,
		Property: c.Property,
	}

	req.SetString(c.Value)
	if numVal, err := strconv.ParseUint(c.Value, 10, 64); err == nil {
		req.SetNumber(numVal)
	}

	ctx := context.Background()
	resp, err := control.PoolSetProp(ctx, c.ctlClient, req)
	if err != nil {
		return errors.Wrap(err, "pool set-prop failed")
	}

	if c.jsonOutputEnabled() {
		return c.outputJSON(os.Stdout, resp)
	}

	c.log.Infof("pool set-prop succeeded (%s=%q)", resp.Property, resp.Value)
	return nil
}

// PoolGetACLCmd represents the command to fetch an Access Control List of a
// DAOS pool.
type PoolGetACLCmd struct {
	logCmd
	ctlClientCmd
	jsonOutputCmd
	UUID    string `long:"pool" required:"1" description:"UUID of DAOS pool"`
	File    string `short:"o" long:"outfile" required:"0" description:"Output ACL to file"`
	Force   bool   `short:"f" long:"force" required:"0" description:"Allow to clobber output file"`
	Verbose bool   `short:"v" long:"verbose" required:"0" description:"Add descriptive comments to ACL entries"`
}

// Execute is run when the PoolGetACLCmd subcommand is activated
func (d *PoolGetACLCmd) Execute(args []string) error {
	req := &control.PoolGetACLReq{UUID: d.UUID}

	ctx := context.Background()
	resp, err := control.PoolGetACL(ctx, d.ctlClient, req)
	if err != nil {
		return errors.Wrap(err, "Pool-get-ACL command failed")
	}

	d.log.Debugf("Pool-get-ACL command succeeded, UUID: %s\n", d.UUID)

	if d.jsonOutputEnabled() {
		return d.outputJSON(os.Stdout, resp.ACL)
	}

	acl := control.FormatACL(resp.ACL, d.Verbose)

	if d.File != "" {
		err = d.writeACLToFile(acl)
		if err != nil {
			return err
		}
		d.log.Infof("Wrote ACL to output file: %s", d.File)
	} else {
		d.log.Info(acl)
	}

	return nil
}

func (d *PoolGetACLCmd) writeACLToFile(acl string) error {
	if !d.Force {
		// Keep the user from clobbering existing files
		_, err := os.Stat(d.File)
		if err == nil {
			return errors.New(fmt.Sprintf("file already exists: %s", d.File))
		}
	}

	f, err := os.Create(d.File)
	if err != nil {
		d.log.Errorf("Unable to create file: %s", d.File)
		return err
	}
	defer f.Close()

	_, err = f.WriteString(acl)
	if err != nil {
		d.log.Errorf("Failed to write to file: %s", d.File)
		return err
	}

	return nil
}

// PoolOverwriteACLCmd represents the command to overwrite the Access Control
// List of a DAOS pool.
type PoolOverwriteACLCmd struct {
	logCmd
	ctlClientCmd
	jsonOutputCmd
	UUID    string `long:"pool" required:"1" description:"UUID of DAOS pool"`
	ACLFile string `short:"a" long:"acl-file" required:"1" description:"Path for new Access Control List file"`
}

// Execute is run when the PoolOverwriteACLCmd subcommand is activated
func (d *PoolOverwriteACLCmd) Execute(args []string) error {
	acl, err := control.ReadACLFile(d.ACLFile)
	if err != nil {
		return err
	}

	req := &control.PoolOverwriteACLReq{
		UUID: d.UUID,
		ACL:  acl,
	}

	ctx := context.Background()
	resp, err := control.PoolOverwriteACL(ctx, d.ctlClient, req)
	if err != nil {
		return errors.Wrap(err, "Pool-overwrite-ACL command failed")
	}

	d.log.Infof("Pool-overwrite-ACL command succeeded, UUID: %s\n", d.UUID)

	if d.jsonOutputEnabled() {
		return d.outputJSON(os.Stdout, resp.ACL)
	}

	d.log.Info(control.FormatACLDefault(resp.ACL))

	return nil
}

// PoolUpdateACLCmd represents the command to update the Access Control List of
// a DAOS pool.
type PoolUpdateACLCmd struct {
	logCmd
	ctlClientCmd
	jsonOutputCmd
	UUID    string `long:"pool" required:"1" description:"UUID of DAOS pool"`
	ACLFile string `short:"a" long:"acl-file" required:"0" description:"Path for new Access Control List file"`
	Entry   string `short:"e" long:"entry" required:"0" description:"Single Access Control Entry to add or update"`
}

// Execute is run when the PoolUpdateACLCmd subcommand is activated
func (d *PoolUpdateACLCmd) Execute(args []string) error {
	if (d.ACLFile == "" && d.Entry == "") || (d.ACLFile != "" && d.Entry != "") {
		return errors.New("either ACL file or entry parameter is required")
	}

	var acl *control.AccessControlList
	if d.ACLFile != "" {
		aclFileResult, err := control.ReadACLFile(d.ACLFile)
		if err != nil {
			return err
		}
		acl = aclFileResult
	} else {
		acl = &control.AccessControlList{
			Entries: []string{d.Entry},
		}
	}

	req := &control.PoolUpdateACLReq{
		UUID: d.UUID,
		ACL:  acl,
	}

	ctx := context.Background()
	resp, err := control.PoolUpdateACL(ctx, d.ctlClient, req)
	if err != nil {
		return errors.Wrap(err, "Pool-update-ACL command failed")
	}

	d.log.Infof("Pool-update-ACL command succeeded, UUID: %s\n", d.UUID)

	if d.jsonOutputEnabled() {
		return d.outputJSON(os.Stdout, resp.ACL)
	}

	d.log.Info(control.FormatACLDefault(resp.ACL))

	return nil
}

// PoolDeleteACLCmd represents the command to delete an entry from the Access
// Control List of a DAOS pool.
type PoolDeleteACLCmd struct {
	logCmd
	ctlClientCmd
	jsonOutputCmd
	UUID      string `long:"pool" required:"1" description:"UUID of DAOS pool"`
	Principal string `short:"p" long:"principal" required:"1" description:"Principal whose entry should be removed"`
}

// Execute is run when the PoolDeleteACLCmd subcommand is activated
func (d *PoolDeleteACLCmd) Execute(args []string) error {
	req := &control.PoolDeleteACLReq{
		UUID:      d.UUID,
		Principal: d.Principal,
	}

	ctx := context.Background()
	resp, err := control.PoolDeleteACL(ctx, d.ctlClient, req)
	if err != nil {
		return errors.Wrap(err, "Pool-delete-ACL command failed")
	}

	d.log.Infof("Pool-delete-ACL command succeeded, UUID: %s\n", d.UUID)

	if d.jsonOutputEnabled() {
		return d.outputJSON(os.Stdout, resp.ACL)
	}

	d.log.Info(control.FormatACLDefault(resp.ACL))

	return nil
}

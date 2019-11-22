//
// (C) Copyright 2019 Intel Corporation.
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
	"fmt"
	"os/user"
	"strings"

	"github.com/inhies/go-bytesize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	msgSizeNoNumber = "size string doesn't specify a number"
	msgSizeZeroScm  = "non-zero scm size is required"
	maxNumSvcReps   = 13
)

// PoolCmd is the struct representing the top-level pool subcommand.
type PoolCmd struct {
	Create       PoolCreateCmd       `command:"create" alias:"c" description:"Create a DAOS pool"`
	Destroy      PoolDestroyCmd      `command:"destroy" alias:"d" description:"Destroy a DAOS pool"`
	GetACL       PoolGetACLCmd       `command:"get-acl" alias:"g" description:"Get a DAOS pool's Access Control List"`
	OverwriteACL PoolOverwriteACLCmd `command:"overwrite-acl" alias:"o" description:"Overwrite a DAOS pool's Access Control List"`
}

// PoolCreateCmd is the struct representing the command to create a DAOS pool.
type PoolCreateCmd struct {
	logCmd
	connectedCmd
	GroupName  string `short:"g" long:"group" description:"DAOS pool to be owned by given group, format name@domain"`
	UserName   string `short:"u" long:"user" description:"DAOS pool to be owned by given user, format name@domain"`
	ACLFile    string `short:"a" long:"acl-file" description:"Access Control List file path for DAOS pool"`
	ScmSize    string `short:"s" long:"scm-size" required:"1" description:"Size of SCM component of DAOS pool"`
	NVMeSize   string `short:"n" long:"nvme-size" description:"Size of NVMe component of DAOS pool"`
	RankList   string `short:"r" long:"ranks" description:"Storage server unique identifiers (ranks) for DAOS pool"`
	NumSvcReps uint32 `short:"v" long:"nsvc" default:"1" description:"Number of pool service replicas"`
	Sys        string `short:"S" long:"sys" default:"daos_server" description:"DAOS system that pool is to be a part of"`
}

// Execute is run when PoolCreateCmd subcommand is activated
func (c *PoolCreateCmd) Execute(args []string) error {
	return poolCreate(c.log, c.conns,
		c.ScmSize, c.NVMeSize, c.RankList, c.NumSvcReps,
		c.GroupName, c.UserName, c.Sys, c.ACLFile)
}

// PoolDestroyCmd is the struct representing the command to destroy a DAOS pool.
type PoolDestroyCmd struct {
	logCmd
	connectedCmd
	// TODO: implement --sys & --svc options (currently unsupported server side)
	Uuid  string `long:"pool" required:"1" description:"UUID of DAOS pool to destroy"`
	Force bool   `short:"f" long:"force" description:"Force removal of DAOS pool"`
}

// Execute is run when PoolDestroyCmd subcommand is activated
func (d *PoolDestroyCmd) Execute(args []string) error {
	return poolDestroy(d.log, d.conns, d.Uuid, d.Force)
}

// PoolGetACLCmd represents the command to fetch an Access Control List of a
// DAOS pool.
type PoolGetACLCmd struct {
	logCmd
	connectedCmd
	UUID string `long:"pool" required:"1" description:"UUID of DAOS pool"`
}

// Execute is run when the PoolGetACLCmd subcommand is activated
func (d *PoolGetACLCmd) Execute(args []string) error {
	return poolGetACL(d.log, d.conns, d.UUID)
}

// PoolOverwriteACLCmd represents the command to overwrite the Access Control
// List of a DAOS pool.
type PoolOverwriteACLCmd struct {
	logCmd
	connectedCmd
	UUID    string `long:"pool" required:"1" description:"UUID of DAOS pool"`
	ACLFile string `short:"a" long:"acl-file" required:"1" description:"Path for new Access Control List file"`
}

// Execute is run when the PoolOverwriteACLCmd subcommand is activated
func (d *PoolOverwriteACLCmd) Execute(args []string) error {
	return poolOverwriteACL(d.log, d.conns, d.UUID, d.ACLFile)
}

// getSize retrieves number of bytes from human readable string representation
func getSize(sizeStr string) (bytesize.ByteSize, error) {
	if sizeStr == "" {
		return bytesize.New(0.00), nil
	}
	if common.IsAlphabetic(sizeStr) {
		return bytesize.New(0.00), errors.New(msgSizeNoNumber)
	}

	// change any alphabetic characters to upper before ByteSize.parse()
	sizeStr = strings.ToUpper(sizeStr)

	// append "B" character if absent (required by ByteSize.parse())
	if !strings.HasSuffix(sizeStr, "B") {
		sizeStr += "B"
	}

	return bytesize.Parse(sizeStr)
}

// calcStorage calculates SCM & NVMe size for pool from user supplied parameters
func calcStorage(log logging.Logger, scmSize string, nvmeSize string) (
	scmBytes bytesize.ByteSize, nvmeBytes bytesize.ByteSize, err error) {

	scmBytes, err = getSize(scmSize)
	if err != nil {
		err = errors.WithMessagef(
			err, "illegal scm size: %s", scmSize)
		return
	}

	if scmBytes == 0 {
		err = errors.New(msgSizeZeroScm)
		return
	}

	nvmeBytes, err = getSize(nvmeSize)
	if err != nil {
		err = errors.WithMessagef(
			err, "illegal nvme size: %s", nvmeSize)
		return
	}

	ratio := 1.00
	if nvmeBytes > 0 {
		ratio = float64(scmBytes) / float64(nvmeBytes)
	}

	if ratio < 0.01 {
		log.Infof(
			"SCM:NVMe ratio is less than 1%%, DAOS performance " +
				"will suffer!\n")
	}
	log.Infof(
		"Creating DAOS pool with %s SCM and %s NvMe storage "+
			"(%.3f ratio)\n",
		scmBytes.Format("%.0f", "", false),
		nvmeBytes.Format("%.0f", "", false),
		ratio)

	return scmBytes, nvmeBytes, nil
}

// formatNameGroup converts system names to principal and if both user and group
// are unspecified, takes effective user name and that user's primary group.
func formatNameGroup(usr string, grp string) (string, string, error) {
	if usr == "" && grp == "" {
		eUsr, err := user.Current()
		if err != nil {
			return "", "", err
		}

		eGrp, err := user.LookupGroupId(eUsr.Gid)
		if err != nil {
			return "", "", err
		}

		usr, grp = eUsr.Username, eGrp.Name
	}

	if usr != "" && !strings.Contains(usr, "@") {
		usr += "@"
	}

	if grp != "" && !strings.Contains(grp, "@") {
		grp += "@"
	}

	return usr, grp, nil
}

// poolCreate with specified parameters.
func poolCreate(log logging.Logger, conns client.Connect, scmSize string,
	nvmeSize string, rankList string, numSvcReps uint32, groupName string,
	userName string, sys string, aclFile string) error {

	msg := "SUCCEEDED: "

	scmBytes, nvmeBytes, err := calcStorage(log, scmSize, nvmeSize)
	if err != nil {
		return errors.Wrap(err, "calculating pool storage sizes")
	}

	var acl *client.AccessControlList
	if aclFile != "" {
		acl, err = readACLFile(aclFile)
		if err != nil {
			return err
		}
	}

	if numSvcReps > maxNumSvcReps {
		return errors.Errorf("max number of service replicas is %d, got %d",
			maxNumSvcReps, numSvcReps)
	}

	usr, grp, err := formatNameGroup(userName, groupName)
	if err != nil {
		return errors.WithMessage(err, "formatting user/group strings")
	}

	req := &client.PoolCreateReq{
		ScmBytes: uint64(scmBytes), NvmeBytes: uint64(nvmeBytes),
		RankList: rankList, NumSvcReps: numSvcReps, Sys: sys,
		Usr: usr, Grp: grp, ACL: acl,
	}

	resp, err := conns.PoolCreate(req)
	if err != nil {
		msg = errors.WithMessage(err, "FAILED").Error()
	} else {
		msg += fmt.Sprintf("UUID: %s, Service replicas: %s",
			resp.UUID, resp.SvcReps)
	}

	log.Infof("Pool-create command %s\n", msg)

	return err
}

// poolDestroy identified by UUID.
func poolDestroy(log logging.Logger, conns client.Connect, poolUUID string, force bool) error {
	msg := "succeeded"

	req := &client.PoolDestroyReq{UUID: poolUUID, Force: force}

	err := conns.PoolDestroy(req)
	if err != nil {
		msg = errors.WithMessage(err, "failed").Error()
	}

	log.Infof("Pool-destroy command %s\n", msg)

	return err
}

func poolGetACL(log logging.Logger, conns client.Connect, poolUUID string) error {
	req := &client.PoolGetACLReq{UUID: poolUUID}

	resp, err := conns.PoolGetACL(req)
	if err != nil {
		log.Infof("Pool-get-ACL command failed: %s\n", err.Error())
		return err
	}

	log.Infof("Pool-get-ACL command succeeded, UUID: %s\n", poolUUID)
	log.Info(resp.ACL.String())

	return nil
}

func poolOverwriteACL(log logging.Logger, conns client.Connect, poolUUID string, aclFile string) error {
	acl, err := readACLFile(aclFile)
	if err != nil {
		return err
	}

	req := &client.PoolOverwriteACLReq{
		UUID: poolUUID,
		ACL:  acl,
	}

	resp, err := conns.PoolOverwriteACL(req)
	if err != nil {
		log.Infof("Pool-overwrite-ACL command failed: %s\n", err.Error())
		return err
	}

	log.Infof("Pool-overwrite-ACL command succeeded, UUID: %s\n", poolUUID)
	log.Info(resp.ACL.String())

	return nil
}

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
	"strings"

	"github.com/inhies/go-bytesize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

const (
	msgSizeNoNumber = "size string doesn't specify a number"
	msgSizeZeroScm  = "non-zero scm size is required"
	maxNumSvcReps   = 13
)

// PoolCmd is the struct representing the top-level pool subcommand.
type PoolCmd struct {
	Create  CreatePoolCmd  `command:"create" alias:"c" description:"Create a DAOS pool"`
	Destroy DestroyPoolCmd `command:"destroy" alias:"d" description:"Destroy a DAOS pool"`
}

// CreatePoolCmd is the struct representing the command to create a DAOS pool.
type CreatePoolCmd struct {
	connectedCmd
	GroupName  string `short:"g" long:"group" description:"DAOS pool to be owned by given group"`
	UserName   string `short:"u" long:"user" description:"DAOS pool to be owned by given user"`
	ACLFile    string `short:"a" long:"acl-file" description:"Access Control List file path for DAOS pool"`
	ScmSize    string `short:"s" long:"scm-size" required:"1" description:"Size of SCM component of DAOS pool"`
	NVMeSize   string `short:"n" long:"nvme-size" description:"Size of NVMe component of DAOS pool"`
	RankList   string `short:"r" long:"ranks" description:"Storage server unique identifiers (ranks) for DAOS pool"`
	NumSvcReps uint32 `short:"v" long:"nsvc" default:"1" description:"Number of pool service replicas"`
	Sys        string `short:"S" long:"sys" default:"daos_server" description:"DAOS system that pool is to be a part of"`
}

// Execute is run when CreatePoolCmd subcommand is activated
func (c *CreatePoolCmd) Execute(args []string) error {
	return createPool(c.conns,
		c.ScmSize, c.NVMeSize, c.RankList, c.NumSvcReps,
		c.GroupName, c.UserName, c.Sys, c.ACLFile)
}

// DestroyPoolCmd is the struct representing the command to destroy a DAOS pool.
type DestroyPoolCmd struct {
	connectedCmd
	Uuid  string `short:"u" long:"uuid" required:"1" description:"UUID of DAOS pool to destroy"`
	Force bool   `short:"f" long:"force" description:"Force removal of DAOS pool"`
}

// Execute is run when DestroyPoolCmd subcommand is activated
func (d *DestroyPoolCmd) Execute(args []string) error {
	return destroyPool(d.conns, d.Uuid, d.Force)
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
func calcStorage(scmSize string, nvmeSize string) (
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
		fmt.Printf(
			"SCM:NVMe ratio is less than 1%%, DAOS performance " +
				"will suffer!\n")
	}
	fmt.Printf(
		"Creating DAOS pool with %s SCM and %s NvMe storage "+
			"(%.3f ratio)\n",
		scmBytes.Format("%.0f", "", false),
		nvmeBytes.Format("%.0f", "", false),
		ratio)

	return scmBytes, nvmeBytes, nil
}

// createPool with specified parameters.
func createPool(conns client.Connect,
	scmSize string, nvmeSize string, rankList string, numSvcReps uint32,
	groupName string, userName string, sys string,
	aclFile string) error {

	scmBytes, nvmeBytes, err := calcStorage(scmSize, nvmeSize)
	if err != nil {
		return errors.Wrap(err, "calculating pool storage sizes")
	}

	if aclFile != "" {
		return errors.New("ACL file parsing not implemented")
	}

	if numSvcReps > maxNumSvcReps {
		return errors.Errorf(
			"max number of service replicas is %d, got %d",
			maxNumSvcReps, numSvcReps)
	}

	req := &pb.CreatePoolReq{
		Scmbytes: uint64(scmBytes), Nvmebytes: uint64(nvmeBytes),
		Ranks: rankList, Numsvcreps: numSvcReps,
		// TODO: format and populate user/group
		Sys: sys,
	}

	fmt.Printf("Creating DAOS pool: %+v\n", req)

	fmt.Printf("pool create command results:\n%s\n", conns.CreatePool(req))

	return nil
}

// destroyPool identified by UUID.
func destroyPool(conns client.Connect, uuid string, force bool) error {
	req := &pb.DestroyPoolReq{Uuid: uuid, Force: force}

	fmt.Printf("Destroying DAOS pool: %+v\n", req)

	fmt.Printf("pool destroy command results:\n%s\n", conns.DestroyPool(req))

	return nil
}

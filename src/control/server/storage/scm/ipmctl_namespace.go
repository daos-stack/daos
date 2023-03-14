//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"encoding/json"
	"fmt"
	"regexp"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/server/storage"
)

const ndctlName = "ndctl"

var (
	nsMajMinRegex = regexp.MustCompile(`namespace([0-9]).([0-9])`)

	// Cmd structs for ndctl commandline calls.

	cmdCreateNamespace = pmemCmd{
		BinaryName: ndctlName,
		Args:       []string{"create-namespace"},
	}
	// expects device name param
	cmdDisableNamespace = pmemCmd{
		BinaryName: ndctlName,
		Args:       []string{"disable-namespace"},
	}
	// expects device name param
	cmdDestroyNamespace = pmemCmd{
		BinaryName: ndctlName,
		Args:       []string{"destroy-namespace"},
	}
	// returns ns info in json
	cmdListNamespaces = pmemCmd{
		BinaryName: ndctlName,
		Args:       []string{"list", "-N", "-v"},
	}
)

func (cr *cmdRunner) checkNdctl() (errOut error) {
	cr.checkOnce.Do(func() {
		if _, err := cr.lookPath("ndctl"); err != nil {
			errOut = FaultMissingNdctl
		}
	})

	return
}

// For each region, create <nrNsPerSocket> namespaces.
func (cr *cmdRunner) createNamespaces(regionPerSocket socketRegionMap, nrNsPerSock uint) error {
	if nrNsPerSock < minNrNssPerSocket || nrNsPerSock > maxNrNssPerSocket {
		return errors.Errorf("unexpected number of namespaces requested per socket: want [%d-%d], got %d",
			minNrNssPerSocket, maxNrNssPerSocket, nrNsPerSock)
	}

	// For the moment assume 1:1 mapping of sockets to regions so nrRegions == nrSockets.
	nrRegions := len(regionPerSocket)
	if nrRegions == 0 {
		return errors.New("expected non-zero number of pmem regions")
	}
	sockIDs := regionPerSocket.keys()
	cr.log.Debugf("creating %d namespaces on each of the following sockets: %v", nrNsPerSock,
		sockIDs)

	for _, sid := range sockIDs {
		region := regionPerSocket[sid]
		cr.log.Debugf("creating namespaces on region %d, socket %d", region.ID, sid)

		// Check value is 2MiB aligned and (TODO) multiples of interleave width.
		pmemBytes := uint64(region.FreeCapacity) / uint64(nrNsPerSock)

		if pmemBytes%alignmentBoundaryBytes != 0 {
			return errors.Errorf("socket %d: free region size (%s) is not %s aligned", sid,
				humanize.Bytes(pmemBytes), humanize.Bytes(alignmentBoundaryBytes))
		}

		// Create specified number of namespaces on a single region (NUMA node).
		for j := uint(0); j < nrNsPerSock; j++ {
			// Specify socket ID for region ID in command as region parameter in ndctl
			// is zero-based, not one-based like in ipmctl.
			cmd := cmdCreateNamespace
			cmd.Args = append(cmd.Args, "--region", fmt.Sprintf("%d", sid), "--size",
				fmt.Sprintf("%d", pmemBytes))
			if _, err := cr.runCmd(cr.log, cmd); err != nil {
				return errors.WithMessagef(err, "socket %d", sid)
			}
			cr.log.Debugf("created namespace on socket %d (same region index) size %s",
				sid, humanize.Bytes(pmemBytes))
		}
	}

	return nil
}

func (cr *cmdRunner) removeNamespace(devName string) error {
	if err := cr.checkNdctl(); err != nil {
		return err
	}

	cr.log.Debugf("removing pmem namespace %q", devName)

	cmd := cmdDisableNamespace
	cmd.Args = append(cmd.Args, devName)
	if _, err := cr.runCmd(cr.log, cmd); err != nil {
		return err
	}

	cmd = cmdDestroyNamespace
	cmd.Args = append(cmd.Args, devName)
	_, err := cr.runCmd(cr.log, cmd)
	return err
}

func parseNamespaces(jsonData string) (storage.ScmNamespaces, error) {
	nss := storage.ScmNamespaces{}

	// turn single entries into arrays
	if !strings.HasPrefix(jsonData, "[") {
		jsonData = "[" + jsonData + "]"
	}

	if err := json.Unmarshal([]byte(jsonData), &nss); err != nil {
		return nil, err
	}

	return nss, nil
}

// getNamespaces calls ndctl to list pmem namespaces and returns converted
// native storage types.
func (cr *cmdRunner) getNamespaces(sockID int) (storage.ScmNamespaces, error) {
	if err := cr.checkNdctl(); err != nil {
		return nil, err
	}

	cmd := cmdListNamespaces
	if sockID != sockAny {
		cmd.Args = append(cmd.Args, "--numa-node", fmt.Sprintf("%d", sockID))
	}
	out, err := cr.runCmd(cr.log, cmd)
	if err != nil {
		return nil, err
	}

	nss, err := parseNamespaces(out)
	if err != nil {
		return nil, err
	}
	cr.log.Debugf("discovered %d pmem namespaces", len(nss))

	return nss, nil
}

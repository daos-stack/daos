//
// (C) Copyright 2022 Intel Corporation.
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

var nsMajMinRegex = regexp.MustCompile(`namespace([0-9]).([0-9])`)

// Constants for ndctl commandline calls.
const (
	cmdCreateNamespace  = "ndctl create-namespace"  // returns ns info in json
	cmdListNamespaces   = "ndctl list -N -v"        // returns ns info in json
	cmdDisableNamespace = "ndctl disable-namespace" // expect device name param
	cmdDestroyNamespace = "ndctl destroy-namespace" // expect device name param
)

func (cr *cmdRunner) checkNdctl() (errOut error) {
	cr.checkOnce.Do(func() {
		if _, err := cr.lookPath("ndctl"); err != nil {
			errOut = FaultMissingNdctl
		}
	})

	return
}

// create PMem namespaces through ndctl commandline tool, region parameter is zero-based, not
// one-based like in ipmctl.
func (cr *cmdRunner) createNamespace(regionID int, sizeBytes uint64) (string, error) {
	cmd := cmdCreateNamespace
	cmd = fmt.Sprintf("%s --region %d", cmd, regionID)
	cmd = fmt.Sprintf("%s --size %d", cmd, sizeBytes)

	out, err := cr.runCmd(cmd)

	return out, errors.Wrapf(err, cmd)
}

// For each region, create <nrNsPerSocket> namespaces.
func (cr *cmdRunner) createNamespaces(regionPerSocket socketRegionMap, nrNsPerSock uint) error {
	if nrNsPerSock < minNrNssPerSocket || nrNsPerSock > maxNrNssPerSocket {
		return errors.Errorf("unexpected number of namespaces requested per socket: want [%d-%d], got %d",
			minNrNssPerSocket, maxNrNssPerSocket, nrNsPerSock)
	}

	// 1:1 mapping of sockets to regions so nrRegions == nrSockets.
	nrRegions := len(regionPerSocket)
	if nrRegions == 0 {
		return errors.New("expected non-zero number of pmem regions")
	}
	cr.log.Debugf("creating %d namespaces per socket (%d regions)", nrNsPerSock, nrRegions)

	keys := regionPerSocket.keys()
	for sid := range keys {
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
			// Specify socket ID for region ID in command as it takes a zero-based index.
			out, err := cr.createNamespace(sid, pmemBytes)
			if err != nil {
				return errors.WithMessagef(err, "socket %d", sid)
			}
			cr.log.Debugf("createNamespace on region index %d size %s returned: %s", sid,
				humanize.Bytes(pmemBytes), out)
		}
	}

	return nil
}

func (cr *cmdRunner) listNamespaces(sockID int) (string, error) {
	cmd := cmdListNamespaces
	if sockID != sockAny {
		cmd = fmt.Sprintf("%s --numa-node %d", cmd, sockID)
	}

	return cr.runCmd(cmd)
}

func (cr *cmdRunner) disableNamespace(name string) (string, error) {
	return cr.runCmd(fmt.Sprintf("%s %s", cmdDisableNamespace, name))
}

func (cr *cmdRunner) destroyNamespace(name string) (string, error) {
	return cr.runCmd(fmt.Sprintf("%s %s", cmdDestroyNamespace, name))
}

func (cr *cmdRunner) removeNamespace(devName string) error {
	if err := cr.checkNdctl(); err != nil {
		return err
	}

	cr.log.Debugf("removing pmem namespace %q", devName)

	if _, err := cr.disableNamespace(devName); err != nil {
		return errors.WithMessagef(err, "disable namespace cmd (%s)", devName)
	}

	if _, err := cr.destroyNamespace(devName); err != nil {
		return errors.WithMessagef(err, "destroy namespace cmd (%s)", devName)
	}

	return nil
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

	out, err := cr.listNamespaces(sockID)
	if err != nil {
		return nil, errors.WithMessage(err, "list namespaces cmd")
	}

	nss, err := parseNamespaces(out)
	if err != nil {
		return nil, err
	}
	cr.log.Debugf("discovered %d pmem namespaces", len(nss))

	return nss, nil
}

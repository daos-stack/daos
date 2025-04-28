//
// (C) Copyright 2022-2024 Intel Corporation.
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

const (
	ndctlName         = "ndctl"
	ndctlRegionType   = "pmem"
	ndctlRegionDomain = "memory_controller"
)

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
	// returns region info in json
	cmdListNdctlRegions = pmemCmd{
		BinaryName: ndctlName,
		Args:       []string{"list", "-R", "-v"},
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

// For each region, create <nrNsPerSocket> namespaces. Return slice indicating which NUMA nodes the
// name spaces were created on.
func (cr *cmdRunner) createNamespaces(regionPerSocket socketRegionMap, nrNsPerSock uint) ([]int, error) {
	if nrNsPerSock < minNrNssPerSocket || nrNsPerSock > maxNrNssPerSocket {
		return nil, errors.Errorf("unexpected number of namespaces requested per socket: want [%d-%d], got %d",
			minNrNssPerSocket, maxNrNssPerSocket, nrNsPerSock)
	}

	// For the moment assume 1:1 mapping of sockets to regions
	sockIDs := regionPerSocket.keys()
	if len(sockIDs) == 0 {
		return nil, errors.New("expected non-zero number of pmem regions in input map")
	}

	// As the selector is socket, look up the ndctl region with the same ISetID as the ipmctl
	// region with specified socket ID. This may not work when the ISetID overflows in ndctl
	// output. If all sockIDs can be mapped to existent entries in ndctl regions by isetid
	// keys then use region IDs from ndctl regions, otherwise fall-back to using socket ID as
	// region index.

	regionsToPrep := NdctlRegions{}

	ndctlRegions, err := cr.getNdctlRegions(sockAny)
	if err != nil {
		var jsonErr *json.UnmarshalTypeError
		if errors.As(err, &jsonErr) {
			cr.log.Debugf("bad ndctl region unmarshal %v so fallback to use sock ID", err)
			ndctlRegions = NdctlRegions{}
		} else {
			return nil, err
		}
	}
	for _, sid := range sockIDs {
		for _, nr := range ndctlRegions {
			if nr.ISetID <= 0 {
				cr.log.Noticef("%s isetid invalid, possible overflow", nr.Dev)
				break
			}
			if nr.Type != ndctlRegionType {
				cr.log.Debugf("%s unexpected type, want %s got %s", nr.Dev,
					ndctlRegionType, nr.Type)
				break
			}
			if nr.PersistenceDomain != ndctlRegionDomain {
				cr.log.Debugf("%s unexpected persistence domain, want %s got %s",
					nr.Dev, ndctlRegionDomain, nr.PersistenceDomain)
				break
			}
			if uint64(nr.ISetID) == uint64(regionPerSocket[sid].ISetID) {
				cr.log.Debugf("%s matches requested socket %d with isetid %d",
					nr.Dev, sid, nr.ISetID)
				regionsToPrep = append(regionsToPrep, nr)
			}
			if nr.NumaNode != uint32(sid) {
				cr.log.Noticef("%s on numa node %d doesn't match socket ID %d",
					nr.Dev, nr.NumaNode, sid)
			}
		}
	}
	if len(regionsToPrep) != len(sockIDs) {
		cr.log.Debug("regions could not be mapped by isetid, using ipmctl socket ID instead")

		regionsToPrep = NdctlRegions{}
		for _, sid := range sockIDs {
			regionsToPrep = append(regionsToPrep, &NdctlRegion{
				Dev:           fmt.Sprintf("region%d", sid),
				AvailableSize: uint64(regionPerSocket[sid].FreeCapacity),
				NumaNode:      uint32(sid),
			})
		}
	}

	cr.log.Debugf("attempting to create %d namespaces on each of the following socket(s): %v",
		nrNsPerSock, sockIDs)

	var numaNodesPrepped []int
	for _, region := range regionsToPrep {
		if region.AvailableSize == 0 {
			cr.log.Tracef("skipping namespace creation on full region %q", region.Dev)
			continue
		}

		// Check value is 2MiB aligned and (TODO) multiples of interleave width.
		pmemBytes := uint64(region.AvailableSize) / uint64(nrNsPerSock)

		if pmemBytes%alignmentBoundaryBytes != 0 {
			return nil, errors.Errorf("%s: available size (%s) is not %s aligned",
				region.Dev, humanize.IBytes(pmemBytes),
				humanize.IBytes(alignmentBoundaryBytes))
		}

		// Create specified number of namespaces on a single region (NUMA node).
		for j := uint(0); j < nrNsPerSock; j++ {
			cmd := cmdCreateNamespace
			cmd.Args = append(cmd.Args, "--region", region.Dev, "--size",
				fmt.Sprintf("%d", pmemBytes))
			if _, err := cr.runCmd(cmd); err != nil {
				return nil, errors.WithMessagef(err, "%s", region.Dev)
			}
			cr.log.Debugf("created namespace on %s size %s (numa %d)", region.Dev,
				humanize.IBytes(pmemBytes), region.NumaNode)
		}

		numaNodesPrepped = append(numaNodesPrepped, int(region.NumaNode))
	}

	return numaNodesPrepped, nil
}

func (cr *cmdRunner) removeNamespace(devName string) error {
	if err := cr.checkNdctl(); err != nil {
		return err
	}

	cr.log.Debugf("removing pmem namespace %q", devName)

	cmd := cmdDisableNamespace
	cmd.Args = append(cmd.Args, devName)
	if _, err := cr.runCmd(cmd); err != nil {
		return err
	}

	cmd = cmdDestroyNamespace
	cmd.Args = append(cmd.Args, devName)
	_, err := cr.runCmd(cmd)
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
func (cr *cmdRunner) getNamespaces(numaID int) (storage.ScmNamespaces, error) {
	if err := cr.checkNdctl(); err != nil {
		return nil, err
	}

	cmd := cmdListNamespaces
	if numaID != sockAny {
		cmd.Args = append(cmd.Args, "--numa-node", fmt.Sprintf("%d", numaID))
	}
	out, err := cr.runCmd(cmd)
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

type (
	NdctlRegion struct {
		Dev               string `json:"dev"`
		Size              uint64 `json:"size"`
		AvailableSize     uint64 `json:"available_size"`
		Align             uint64 `json:"align"`
		NumaNode          uint32 `json:"numa_node"`
		ISetID            uint64 `json:"iset_id"`
		Type              string `json:"type"`
		PersistenceDomain string `json:"persistence_domain"`
	}

	NdctlRegions []*NdctlRegion
)

func parseNdctlRegions(jsonData string) (NdctlRegions, error) {
	nrs := NdctlRegions{}

	// turn single entries into arrays
	if !strings.HasPrefix(jsonData, "[") {
		jsonData = "[" + jsonData + "]"
	}

	if err := json.Unmarshal([]byte(jsonData), &nrs); err != nil {
		return nil, err
	}

	return nrs, nil
}

// getNdctlRegions calls ndctl to list pmem regions and returns NdctlRegions.
func (cr *cmdRunner) getNdctlRegions(sockID int) (NdctlRegions, error) {
	if err := cr.checkNdctl(); err != nil {
		return nil, err
	}

	cmd := cmdListNdctlRegions
	if sockID != sockAny {
		cmd.Args = append(cmd.Args, "--numa-node", fmt.Sprintf("%d", sockID))
	}
	out, err := cr.runCmd(cmd)
	if err != nil {
		return nil, err
	}

	nrs, err := parseNdctlRegions(out)
	if err != nil {
		return nil, err
	}
	cr.log.Debugf("discovered %d ndctl pmem regions", len(nrs))

	return nrs, nil
}

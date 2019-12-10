//
// (C) Copyright 2018-2019 Intel Corporation.
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

package client

import (
	"bytes"
	"fmt"
	"sort"
	"strings"

	"github.com/daos-stack/daos/src/control/common/proto"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// ControllerFactory is an interface providing capability to connect clients.
type ControllerFactory interface {
	create(string, *security.TransportConfig) (Control, error)
}

// controllerFactory as an implementation of ControllerFactory.
type controllerFactory struct {
	log logging.Logger
}

// create instantiates and connects a client to server at given address.
func (c *controllerFactory) create(address string, cfg *security.TransportConfig) (Control, error) {
	controller := &control{
		log: c.log,
	}

	err := controller.connect(address, cfg)

	return controller, err
}

// Addresses is an alias for a slice of <ipv4/hostname>:<port> addresses.
type Addresses []string

// ClientResult is a container for output of any type of client request.
type ClientResult struct {
	Address string
	Value   interface{}
	Err     error
}

func (cr ClientResult) String() string {
	if cr.Err != nil {
		return fmt.Sprintf("error: " + cr.Err.Error())
	}
	return fmt.Sprintf("%+v", cr.Value)
}

// ClientBioResult is a container for output of BIO health
// query client requests.
type ClientBioResult struct {
	Address string
	Stats   *mgmtpb.BioHealthResp
	Err     error
}

func (cr ClientBioResult) String() string {
	var buf bytes.Buffer

	if cr.Err != nil {
		return fmt.Sprintf("error: " + cr.Err.Error())
	}

	if cr.Stats.Status != 0 {
		return fmt.Sprintf("error: %v\n", cr.Stats.Status)
	}

	fmt.Fprintf(&buf, "Device UUID: %v\n", cr.Stats.DevUuid)
	fmt.Fprintf(&buf, "\tRead errors: %v\n", cr.Stats.ReadErrs)
	fmt.Fprintf(&buf, "\tWrite errors: %v\n", cr.Stats.WriteErrs)
	fmt.Fprintf(&buf, "\tUnmap errors: %v\n", cr.Stats.UnmapErrs)
	fmt.Fprintf(&buf, "\tChecksum errors: %v\n", cr.Stats.ChecksumErrs)
	fmt.Fprintf(&buf, "\tDevice Health:\n")
	fmt.Fprintf(&buf, "\t\tError log entries: %v\n", cr.Stats.ErrorCount)
	fmt.Fprintf(&buf, "\t\tMedia errors: %v\n", cr.Stats.MediaErrors)
	fmt.Fprintf(&buf, "\t\tTemperature: %v\n", cr.Stats.Temperature)
	fmt.Fprintf(&buf, "\t\tTemperature: ")
	if cr.Stats.Temp {
		fmt.Fprintf(&buf, "WARNING\n")
	} else {
		fmt.Fprintf(&buf, "OK\n")
	}
	fmt.Fprintf(&buf, "\t\tAvailable Spare: ")
	if cr.Stats.Spare {
		fmt.Fprintf(&buf, "WARNING\n")
	} else {
		fmt.Fprintf(&buf, "OK\n")
	}
	fmt.Fprintf(&buf, "\t\tDevice Reliability: ")
	if cr.Stats.DeviceReliability {
		fmt.Fprintf(&buf, "WARNING\n")
	} else {
		fmt.Fprintf(&buf, "OK\n")
	}
	fmt.Fprintf(&buf, "\t\tRead Only: ")
	if cr.Stats.Readonly {
		fmt.Fprintf(&buf, "WARNING\n")
	} else {
		fmt.Fprintf(&buf, "OK\n")
	}
	fmt.Fprintf(&buf, "\t\tVolatile Memory Backup: ")
	if cr.Stats.VolatileMemory {
		fmt.Fprintf(&buf, "WARNING\n")
	} else {
		fmt.Fprintf(&buf, "OK\n")
	}

	return buf.String()
}

// ClientSmdResult is a container for output of SMD dev list
// query client requests.
type ClientSmdResult struct {
	Address string
	Devs    *mgmtpb.SmdDevResp
	Pools   *mgmtpb.SmdPoolResp
	Err     error
}

func (cr ClientSmdResult) String() string {
	var buf bytes.Buffer

	if cr.Err != nil {
		return fmt.Sprintf("error: " + cr.Err.Error())
	}

	if cr.Devs != nil {
		if cr.Devs.Status != 0 {
			return fmt.Sprintf("error: %v\n", cr.Devs.Status)
		}

		if len(cr.Devs.Devices) == 0 {
			fmt.Fprintf(&buf, "No Devices Found\n")
		}

		for i, d := range cr.Devs.Devices {
			if i != 0 {
				fmt.Fprintf(&buf, "\n\t")
			}
			fmt.Fprintf(&buf, "Device:\n")
			fmt.Fprintf(&buf, "\t\tUUID: %+v\n", d.Uuid)
			fmt.Fprintf(&buf, "\t\tVOS Target IDs: ")
			for _, t := range d.TgtIds {
				fmt.Fprintf(&buf, "%d ", t)
			}
		}
	}

	if cr.Pools != nil {
		if cr.Pools.Status != 0 {
			return fmt.Sprintf("error: %v\n", cr.Pools.Status)
		}

		if len(cr.Pools.Pools) == 0 {
			fmt.Fprintf(&buf, "No Pools Found\n")
		}

		for i, p := range cr.Pools.Pools {
			if i != 0 {
				fmt.Fprintf(&buf, "\n\t")
			}
			fmt.Fprintf(&buf, "Pool:\n")
			fmt.Fprintf(&buf, "\t\tUUID: %+v\n", p.Uuid)
			fmt.Fprintf(&buf, "\t\tVOS Target IDs: ")
			for _, t := range p.TgtIds {
				fmt.Fprintf(&buf, "%d ", t)
			}
			fmt.Fprintf(&buf, "\n")
			fmt.Fprintf(&buf, "\t\tSPDK Blobs: ")
			for _, b := range p.Blobs {
				fmt.Fprintf(&buf, "%v ", b)
			}
		}
	}

	return buf.String()
}

// ResultMap map client addresses to method call ClientResults
type ResultMap map[string]ClientResult
type ResultQueryMap map[string]ClientBioResult
type ResultSmdMap map[string]ClientSmdResult

func (rm ResultMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(rm))

	for server := range rm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n\t%s\n", server, rm[server])
	}

	return buf.String()
}

func (rm ResultQueryMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(rm))

	for server := range rm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n\t%s\n", server, rm[server])
	}

	return buf.String()
}

func (rm ResultSmdMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(rm))

	for server := range rm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n\t%s\n", server, rm[server])
	}

	return buf.String()
}

// ClientCtrlrMap is an alias for query results of NVMe controllers (and
// any residing namespaces) on connected servers keyed on address.
type ClientCtrlrMap map[string]proto.CtrlrResults

func (ccm ClientCtrlrMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(ccm))

	for server := range ccm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n%s\n", server, ccm[server])
	}

	return buf.String()
}

// ClientMountMap is an alias for query results of SCM regions mounted
// on connected servers keyed on address.
type ClientMountMap map[string]proto.MountResults

func (cmm ClientMountMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(cmm))

	for server := range cmm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n%s\n", server, cmm[server])
	}

	return buf.String()
}

// ScmScanResult represents the result of scanning for SCM
// modules installed on a storage node and SCM namespaces.
type ScmScanResult struct {
	Modules    storage.ScmModules
	Namespaces storage.ScmNamespaces
	Err        error
}

func (result *ScmScanResult) String() string {
	switch {
	case result.Err != nil:
		return fmt.Sprintf("SCM Error: %s", result.Err)
	case len(result.Namespaces) > 0:
		return fmt.Sprintf("SCM Namespaces:\n%s", result.Namespaces)
	default:
		return fmt.Sprintf("SCM Modules:\n%s", result.Modules)
	}
}

func (result *ScmScanResult) Summary() (out string) {
	switch {
	case result.Err != nil:
		return fmt.Sprintf("Error: %s", result.Err)
	case len(result.Namespaces) > 0:
		out = result.Namespaces.Summary()
	default:
		out = result.Modules.Summary()
	}
	return fmt.Sprintf("%s", out)
}

// ScmScanMap maps ScmModuleScanResult structs to the addresses
// of remote servers identified by an address string.
type ScmScanResults map[string]*ScmScanResult

// NvmeScanResult represents the result of scanning for SCM
// modules installed on a storage node.
type NvmeScanResult struct {
	Ctrlrs proto.NvmeControllers
	Err    error
}

func (result *NvmeScanResult) String() string {
	if result.Err != nil {
		return fmt.Sprintf("NVMe Error: %s", result.Err)
	}
	return result.Ctrlrs.String()
}

func (result *NvmeScanResult) StringHealthStats() string {
	if result.Err != nil {
		return fmt.Sprintf("NVMe Error: %s", result.Err)
	}
	return result.Ctrlrs.StringHealthStats()
}

func (result *NvmeScanResult) Summary() (out string) {
	if result.Err != nil {
		return fmt.Sprintf("Error: %s", result.Err)
	}
	return fmt.Sprintf("%s", result.Ctrlrs.Summary())
}

// NvmeScanResults maps NvmeScanResult structs to the addresses
// of remote servers identified by an address string.
type NvmeScanResults map[string]*NvmeScanResult

func scmModulesFromPB(pbMms proto.ScmModules) (mms []storage.ScmModule) {
	for _, c := range pbMms {
		mms = append(mms,
			storage.ScmModule{
				ChannelID:       c.Loc.Channel,
				ChannelPosition: c.Loc.Channelpos,
				ControllerID:    c.Loc.Memctrlr,
				SocketID:        c.Loc.Socket,
				PhysicalID:      c.Physicalid,
				Capacity:        c.Capacity,
			})
	}
	return
}

func scmNamespacesFromPB(pbNss proto.ScmNamespaces) (nss []storage.ScmNamespace) {
	for _, ns := range pbNss {
		nss = append(nss,
			storage.ScmNamespace{
				UUID:        ns.Uuid,
				BlockDevice: ns.Blockdev,
				Name:        ns.Dev,
				NumaNode:    ns.Numanode,
				Size:        ns.Size,
			})
	}
	return
}

// StorageScanReq encapsulated subsystem scan parameters.
type StorageScanReq struct{}

// StorageScanResp encapsulated subsystem results.
type StorageScanResp struct {
	Servers []string
	Nvme    NvmeScanResults
	Scm     ScmScanResults
}

func (ssr *StorageScanResp) StringHealthStats() string {
	var buf bytes.Buffer

	for _, srv := range ssr.Servers {
		fmt.Fprintf(&buf, "%s\n", srv)
		fmt.Fprintf(&buf, "\t%s", ssr.Nvme[srv].StringHealthStats())
	}

	return buf.String()
}

// StorageFormatResult stores results of format operations on NVMe controllers
// and SCM mountpoints.
type StorageFormatResult struct {
	nvmeCtrlr proto.CtrlrResults
	scmMount  proto.MountResults
}

// AccessControlList is a structure for the access control list.
type AccessControlList struct {
	Entries []string // Access Control Entries in short string format
}

// String converts the AccessControlList to a human-readable string.
func (acl *AccessControlList) String() string {
	var builder strings.Builder

	builder.WriteString("# Entries:\n")
	if acl.Empty() {
		builder.WriteString("#   None\n")
		return builder.String()
	}

	for _, ace := range acl.Entries {
		fmt.Fprintf(&builder, "%s\n", ace)
	}

	return builder.String()
}

// Empty checks whether there are any entries in the AccessControlList
func (acl *AccessControlList) Empty() bool {
	if acl == nil || len(acl.Entries) == 0 {
		return true
	}
	return false
}

// PoolDiscovery represents the basic discovery information for a pool.
type PoolDiscovery struct {
	UUID        string // Unique identifier
	SvcReplicas []int  // Ranks of pool service replicas
}

// poolDiscoveriesFromPB converts the protobuf ListPoolsResp_Pool structures to
// PoolDiscovery structures.
func poolDiscoveriesFromPB(pbPools []*mgmtpb.ListPoolsResp_Pool) []*PoolDiscovery {
	pools := make([]*PoolDiscovery, 0, len(pbPools))
	for _, pbPool := range pbPools {
		svcReps := make([]int, 0, len(pbPool.Svcreps))
		for _, rep := range pbPool.Svcreps {
			svcReps = append(svcReps, int(rep))
		}

		pools = append(pools, &PoolDiscovery{
			UUID:        pbPool.Uuid,
			SvcReplicas: svcReps,
		})
	}

	return pools
}

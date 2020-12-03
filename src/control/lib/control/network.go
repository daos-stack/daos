//
// (C) Copyright 2018-2020 Intel Corporation.
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

package control

import (
	"fmt"
	"sort"
	"strings"

	"github.com/golang/protobuf/proto"
	"github.com/mitchellh/hashstructure"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

// HostFabricInterface describes a host fabric interface.
type HostFabricInterface struct {
	Provider    string
	Device      string
	NumaNode    uint32
	Priority    uint32
	NetDevClass uint32
}

func (hfi *HostFabricInterface) String() string {
	return fmt.Sprintf("%+v", *hfi)
}

// HostFabric describes a host fabric configuration.
type HostFabric struct {
	Interfaces   []*HostFabricInterface `hash:"set"`
	Providers    []string               `hash:"set"`
	NumaCount    uint32
	CoresPerNuma uint32
}

// HashKey returns a uint64 value suitable for use as a key into
// a map of HostFabric configurations.
func (hf *HostFabric) HashKey() (uint64, error) {
	return hashstructure.Hash(hf, nil)
}

// AddInterface is a helper function that populates a HostFabric.
func (hf *HostFabric) AddInterface(hfi *HostFabricInterface) {
	hf.Interfaces = append(hf.Interfaces, hfi)
	hf.Providers = append(hf.Providers, hfi.Provider)
	hf.Providers = common.DedupeStringSlice(hf.Providers)
}

// HostFabricSet contains a HostFabric configuration and the
// set of hosts matching this configuration.
type HostFabricSet struct {
	HostFabric *HostFabric
	HostSet    *hostlist.HostSet
}

// NewHostFabricSet returns an initialized HostFabricSet for the given
// host address and HostFabric configuration.
func NewHostFabricSet(hostAddr string, hf *HostFabric) (*HostFabricSet, error) {
	hostSet, err := hostlist.CreateSet(hostAddr)
	if err != nil {
		return nil, err
	}
	return &HostFabricSet{
		HostFabric: hf,
		HostSet:    hostSet,
	}, nil
}

// HostFabricMap provides a map of HostFabric keys to HostFabricSet values.
type HostFabricMap map[uint64]*HostFabricSet

// Add inserts the given host address to a matching HostFabricSet or
// creates a new one.
func (hfm HostFabricMap) Add(hostAddr string, hf *HostFabric) (err error) {
	hk, err := hf.HashKey()
	if err != nil {
		return err
	}
	if _, exists := hfm[hk]; !exists {
		hfm[hk], err = NewHostFabricSet(hostAddr, hf)
		return
	}
	_, err = hfm[hk].HostSet.Insert(hostAddr)
	return
}

// Keys returns a set of storage map keys sorted by hosts.
func (hfm HostFabricMap) Keys() []uint64 {
	sets := make([]string, 0, len(hfm))
	keys := make([]uint64, len(hfm))
	setToKeys := make(map[string]uint64)
	for key, hsf := range hfm {
		rs := hsf.HostSet.RangedString()
		sets = append(sets, rs)
		setToKeys[rs] = key
	}
	sort.Strings(sets)
	for i, set := range sets {
		keys[i] = setToKeys[set]
	}
	return keys
}

// addHostResponse is responsible for validating the given HostResponse
// and adding it to the NetworkScanResp.
func (nsr *NetworkScanResp) addHostResponse(hr *HostResponse) (err error) {
	pbResp, ok := hr.Message.(*ctlpb.NetworkScanResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	hf := new(HostFabric)
	if err := convert.Types(pbResp.GetInterfaces(), &hf.Interfaces); err != nil {
		return nsr.addHostError(hr.Addr, err)
	}

	// Populate Providers by looking at all of the interfaces.
	for _, hfi := range hf.Interfaces {
		hf.Providers = append(hf.Providers, hfi.Provider)
	}
	hf.Providers = common.DedupeStringSlice(hf.Providers)
	hf.NumaCount = uint32(pbResp.GetNumacount())
	hf.CoresPerNuma = uint32(pbResp.GetCorespernuma())

	if nsr.HostFabrics == nil {
		nsr.HostFabrics = make(HostFabricMap)
	}
	if err := nsr.HostFabrics.Add(hr.Addr, hf); err != nil {
		return err
	}

	return
}

type (
	// NetworkScanReq contains the parameters for a network scan request.
	NetworkScanReq struct {
		unaryRequest
		Provider string
	}

	// NetworkScanResp contains the results of a network scan.
	NetworkScanResp struct {
		HostErrorsResp
		HostFabrics HostFabricMap
	}
)

// NetworkScan concurrently performs network scans across all hosts
// supplied in the request's hostlist, or all configured hosts if not
// explicitly specified. The function blocks until all results (successful
// or otherwise) are received, and returns a single response structure
// containing results for all host scan operations.
func NetworkScan(ctx context.Context, rpcClient UnaryInvoker, req *NetworkScanReq) (*NetworkScanResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewMgmtCtlClient(conn).NetworkScan(ctx, &ctlpb.NetworkScanReq{
			Provider: req.Provider,
		})
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	nsr := new(NetworkScanResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := nsr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := nsr.addHostResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return nsr, nil
}

type (
	// GetAttachInfoReq defines the request parameters for GetAttachInfo.
	GetAttachInfoReq struct {
		unaryRequest
		msRequest
		System   string
		AllRanks bool
	}

	// PrimaryServiceRank provides a rank->uri mapping for a DAOS
	// Primary Service Rank (PSR).
	PrimaryServiceRank struct {
		Rank uint32
		Uri  string
	}

	GetAttachInfoResp struct {
		ServiceRanks []*PrimaryServiceRank `json:"Psrs"`
		// These CaRT settings are shared with the
		// libdaos client to aid in CaRT initialization.
		Provider        string
		Interface       string
		Domain          string
		CrtCtxShareAddr uint32
		CrtTimeout      uint32
		NetDevClass     uint32
	}
)

func (gair *GetAttachInfoResp) String() string {
	psrs := make([]string, len(gair.ServiceRanks))
	for i, psr := range gair.ServiceRanks {
		psrs[i] = fmt.Sprintf("%d:%s", psr.Rank, psr.Uri)
	}

	// Condensed format for debugging...
	return fmt.Sprintf("p=%s i=%s d=%s a=%d t=%d c=%d, psrs(%d)=%s",
		gair.Provider, gair.Interface, gair.Domain,
		gair.CrtCtxShareAddr, gair.CrtTimeout, gair.NetDevClass,
		len(psrs), strings.Join(psrs, ","),
	)
}

// GetAttachInfo makes a request to the current MS leader in order to learn
// the PSRs (rank/uri mapping) for the DAOS cluster. This information is used
// by DAOS clients in order to make connections to DAOS servers over the storage fabric.
func GetAttachInfo(ctx context.Context, rpcClient UnaryInvoker, req *GetAttachInfoReq) (*GetAttachInfoResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).GetAttachInfo(ctx, &mgmtpb.GetAttachInfoReq{
			Sys:      req.System,
			AllRanks: req.AllRanks,
		})
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	gair := new(GetAttachInfoResp)
	return gair, convertMSResponse(ur, gair)
}

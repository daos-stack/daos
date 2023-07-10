//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"fmt"
	"sort"

	"github.com/mitchellh/hashstructure/v2"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/system"
)

// HostFabricInterface describes a host fabric interface.
type HostFabricInterface struct {
	Provider    string
	Device      string
	NumaNode    uint32
	Priority    uint32
	NetDevClass hardware.NetDevClass
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
	return hashstructure.Hash(hf, hashstructure.FormatV2, nil)
}

// AddInterface is a helper function that populates a HostFabric.
func (hf *HostFabric) AddInterface(hfi *HostFabricInterface) {
	hf.Interfaces = append(hf.Interfaces, hfi)
	hf.Providers = append(hf.Providers, hfi.Provider)
	hf.Providers = common.DedupeStringSlice(hf.Providers)
	sort.Strings(hf.Providers)
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
		return ctlpb.NewCtlSvcClient(conn).NetworkScan(ctx, &ctlpb.NetworkScanReq{
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
		retryableRequest
		System   string
		AllRanks bool
	}

	// PrimaryServiceRank provides a rank->uri mapping for a DAOS
	// Primary Service Rank (PSR).
	PrimaryServiceRank struct {
		Rank uint32 `json:"rank"`
		Uri  string `json:"uri"`
	}

	ClientNetworkHint struct {
		// These CaRT settings are shared with the
		// libdaos client to aid in CaRT initialization.
		Provider        string   `json:"provider"`
		Interface       string   `json:"interface"`
		Domain          string   `json:"domain"`
		CrtCtxShareAddr uint32   `json:"crt_ctx_share_addr"`
		CrtTimeout      uint32   `json:"crt_timeout"`
		NetDevClass     uint32   `json:"net_dev_class"`
		SrvSrxSet       int32    `json:"srv_srx_set"`
		EnvVars         []string `json:"env_vars"`
	}

	GetAttachInfoResp struct {
		System        string                `json:"sys"`
		ServiceRanks  []*PrimaryServiceRank `json:"rank_uris"`
		MSRanks       []uint32              `json:"ms_ranks"`
		ClientNetHint ClientNetworkHint     `json:"client_net_hint"`
	}
)

func (gair *GetAttachInfoResp) String() string {
	// gair.ServiceRanks may contain thousands of elements. Print a few
	// (just one!) at most to avoid flooding logs.
	rankURI := fmt.Sprintf("%d:%s", gair.ServiceRanks[0].Rank, gair.ServiceRanks[0].Uri)

	// Condensed format for debugging...
	ch := gair.ClientNetHint
	return fmt.Sprintf("p=%s i=%s d=%s a=%d t=%d c=%d x=%d, rus(%d)=%s, mss=%v",
		ch.Provider, ch.Interface, ch.Domain,
		ch.CrtCtxShareAddr, ch.CrtTimeout, ch.NetDevClass, ch.SrvSrxSet,
		len(gair.ServiceRanks), rankURI, gair.MSRanks,
	)
}

// GetAttachInfo makes a request to the current MS leader in order to learn
// the PSRs (rank/uri mapping) for the DAOS cluster. This information is used
// by DAOS clients in order to make connections to DAOS servers over the storage fabric.
func GetAttachInfo(ctx context.Context, rpcClient UnaryInvoker, req *GetAttachInfoReq) (*GetAttachInfoResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).GetAttachInfo(ctx, &mgmtpb.GetAttachInfoReq{
			Sys:      req.getSystem(rpcClient),
			AllRanks: req.AllRanks,
		})
	})
	req.retryTestFn = func(err error, _ uint) bool {
		// If the MS hasn't added any members yet, retry the request.
		return system.IsEmptyGroupMap(err)
	}

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	gair := new(GetAttachInfoResp)
	return gair, convertMSResponse(ur, gair)
}

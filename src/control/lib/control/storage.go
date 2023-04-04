//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"sort"
	"strings"

	"github.com/mitchellh/hashstructure/v2"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

var storageHashOpts = hashstructure.HashOptions{
	SlicesAsSets: true,
}

// HostStorage describes a host storage configuration which
// may apply to one or more hosts.
type HostStorage struct {
	// NvmeDevices contains the set of NVMe controllers (SSDs)
	// in this configuration.
	NvmeDevices storage.NvmeControllers `json:"nvme_devices"`

	// ScmModules contains the set of SCM modules (persistent
	// memory DIMMs) in this configuration.
	ScmModules storage.ScmModules `json:"scm_modules"`

	// ScmNamespaces contains the set of prepared SCM namespaces
	// (block devices) in this configuration.
	ScmNamespaces storage.ScmNamespaces `json:"scm_namespaces"`

	// ScmMountPoints contains the set of SCM mountpoints in
	// this configuration.
	ScmMountPoints storage.ScmMountPoints `json:"scm_mount_points"`

	// SmdInfo contains information obtained by querying the
	// host's metadata table, if available.
	SmdInfo *SmdInfo `json:"smd_info"`

	// RebootRequired indicates that a host reboot is necessary in order
	// to achieve some goal (SCM prep, etc.)
	RebootRequired bool `json:"reboot_required"`

	// MemInfo contains information about the host's hugepages.
	MemInfo *common.MemInfo `json:"mem_info"`
}

// HashKey returns a uint64 value suitable for use as a key into
// a map of HostStorage configurations.
func (hs *HostStorage) HashKey() (uint64, error) {
	return hashstructure.Hash(hs, hashstructure.FormatV2, &storageHashOpts)
}

// HostStorageSet contains a HostStorage configuration and the
// set of hosts matching this configuration.
type HostStorageSet struct {
	HostStorage *HostStorage      `json:"storage"`
	HostSet     *hostlist.HostSet `json:"hosts"`
}

func (hss *HostStorageSet) String() string {
	return fmt.Sprintf("hosts %s, storage %+v", hss.HostSet, hss.HostStorage)
}

// NewHostStorageSet returns an initialized HostStorageSet for the given
// host address and HostStorage configuration.
func NewHostStorageSet(hostAddr string, hs *HostStorage) (*HostStorageSet, error) {
	hostSet, err := hostlist.CreateSet(hostAddr)
	if err != nil {
		return nil, err
	}
	return &HostStorageSet{
		HostStorage: hs,
		HostSet:     hostSet,
	}, nil
}

// HostStorageMap provides a map of HostStorage keys to HostStorageSet values.
type HostStorageMap map[uint64]*HostStorageSet

func (hsm HostStorageMap) String() string {
	var strs []string
	for _, hss := range hsm {
		strs = append(strs, hss.String())
	}
	return strings.Join(strs, "\n")
}

// Add inserts the given host address to a matching HostStorageSet or
// creates a new one.
func (hsm HostStorageMap) Add(hostAddr string, hs *HostStorage) (err error) {
	hk, err := hs.HashKey()
	if err != nil {
		return err
	}
	if _, exists := hsm[hk]; !exists {
		hsm[hk], err = NewHostStorageSet(hostAddr, hs)
		return
	}
	_, err = hsm[hk].HostSet.Insert(hostAddr)
	return
}

// Keys returns a set of storage map keys sorted by hosts.
func (hsm HostStorageMap) Keys() []uint64 {
	sets := make([]string, 0, len(hsm))
	keys := make([]uint64, len(hsm))
	setToKeys := make(map[string]uint64)
	for key, hss := range hsm {
		rs := hss.HostSet.RangedString()
		sets = append(sets, rs)
		setToKeys[rs] = key
	}
	sort.Strings(sets)
	for i, set := range sets {
		keys[i] = setToKeys[set]
	}
	return keys
}

type (
	// StorageScanReq contains the parameters for a storage scan request.
	StorageScanReq struct {
		unaryRequest
		Usage      bool
		NvmeHealth bool
		NvmeMeta   bool
		NvmeBasic  bool
	}

	// StorageScanResp contains the response from a storage scan request.
	StorageScanResp struct {
		HostErrorsResp
		HostStorage HostStorageMap
	}
)

// addHostResponse is responsible for validating the given HostResponse
// and adding it to the StorageScanResp.
//
// TODO: pass info field that is embedded in message to response receiver.
func (ssp *StorageScanResp) addHostResponse(hr *HostResponse) error {
	pbResp, ok := hr.Message.(*ctlpb.StorageScanResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	hs := new(HostStorage)

	nvmeResp := pbResp.GetNvme()
	nvmeState := nvmeResp.GetState()
	switch nvmeState.GetStatus() {
	case ctlpb.ResponseStatus_CTL_SUCCESS:
		if err := convert.Types(nvmeResp.GetCtrlrs(), &hs.NvmeDevices); err != nil {
			return err
		}
	default:
		pbErrMsg := nvmeState.GetError()
		if pbErrMsg == "" {
			pbErrMsg = "unknown error"
		}
		if nvmeState.GetInfo() != "" {
			pbErrMsg += fmt.Sprintf(" (%s)", nvmeState.GetInfo())
		}
		if err := ssp.addHostError(hr.Addr, errors.New(pbErrMsg)); err != nil {
			return err
		}
	}

	scmResp := pbResp.GetScm()
	scmState := scmResp.GetState()
	switch scmState.GetStatus() {
	case ctlpb.ResponseStatus_CTL_SUCCESS:
		if err := convert.Types(scmResp.GetModules(), &hs.ScmModules); err != nil {
			return err
		}
		if err := convert.Types(scmResp.GetNamespaces(), &hs.ScmNamespaces); err != nil {
			return err
		}
	default:
		pbErrMsg := scmState.GetError()
		if pbErrMsg == "" {
			pbErrMsg = "unknown error"
		}
		if scmState.GetInfo() != "" {
			pbErrMsg += fmt.Sprintf(" (%s)", scmState.GetInfo())
		}
		if err := ssp.addHostError(hr.Addr, errors.New(pbErrMsg)); err != nil {
			return err
		}
	}

	if err := convert.Types(pbResp.GetMemInfo(), &hs.MemInfo); err != nil {
		return err
	}

	if ssp.HostStorage == nil {
		ssp.HostStorage = make(HostStorageMap)
	}
	if err := ssp.HostStorage.Add(hr.Addr, hs); err != nil {
		return err
	}

	return nil
}

// StorageScan concurrently performs storage scans across all hosts
// supplied in the request's hostlist, or all configured hosts if not
// explicitly specified. The function blocks until all results (successful
// or otherwise) are received, and returns a single response structure
// containing results for all host scan operations.
//
// NumaHealth option requests SSD health statistics.
// NumaMeta option requests DAOS server meta data stored on SSDs.
// NumaBasic option strips SSD details down to only the most basic.
func StorageScan(ctx context.Context, rpcClient UnaryInvoker, req *StorageScanReq) (*StorageScanResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).StorageScan(ctx, &ctlpb.StorageScanReq{
			Scm: &ctlpb.ScanScmReq{
				Usage: req.Usage,
			},
			Nvme: &ctlpb.ScanNvmeReq{
				Health: req.NvmeHealth,
				// NVMe meta option will populate usage statistics
				Meta:  req.NvmeMeta || req.Usage,
				Basic: req.NvmeBasic,
			},
		})
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	ssr := new(StorageScanResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := ssr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := ssr.addHostResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return ssr, nil
}

type (
	// StorageFormatReq contains the parameters for a storage format request.
	StorageFormatReq struct {
		unaryRequest
		Reformat bool
	}

	// StorageFormatResp contains the response from a storage format request.
	StorageFormatResp struct {
		HostErrorsResp
		HostStorage HostStorageMap
	}
)

// addHostResponse is responsible for validating the given HostResponse
// and adding it to the StorageFormatResp.
func (sfr *StorageFormatResp) addHostResponse(hr *HostResponse) (err error) {
	pbResp, ok := hr.Message.(*ctlpb.StorageFormatResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	hs := new(HostStorage)
	for _, nr := range pbResp.GetCrets() {
		switch nr.GetState().GetStatus() {
		case ctlpb.ResponseStatus_CTL_SUCCESS:
			// If we didn't receive a PCI Address in the response,
			// then the device wasn't formatted.
			if nr.GetPciAddr() == "" {
				continue
			}

			info := nr.GetState().GetInfo()
			if info == "" {
				info = ctlpb.ResponseStatus_CTL_SUCCESS.String()
			}
			hs.NvmeDevices = append(hs.NvmeDevices, &storage.NvmeController{
				Info:    info,
				PciAddr: nr.GetPciAddr(),
			})
		default:
			if err := ctlStateToErr(nr.GetState()); err != nil {
				if err := sfr.addHostError(hr.Addr, err); err != nil {
					return err
				}
			}
		}
	}

	for _, sr := range pbResp.GetMrets() {
		switch sr.GetState().GetStatus() {
		case ctlpb.ResponseStatus_CTL_SUCCESS:
			info := sr.GetState().GetInfo()
			if info == "" {
				info = ctlpb.ResponseStatus_CTL_SUCCESS.String()
			}
			hs.ScmMountPoints = append(hs.ScmMountPoints, &storage.ScmMountPoint{
				Info: info,
				Path: sr.GetMntpoint(),
			})
		default:
			if err := ctlStateToErr(sr.GetState()); err != nil {
				if err := sfr.addHostError(hr.Addr, err); err != nil {
					return err
				}
			}
		}
	}

	if sfr.HostStorage == nil {
		sfr.HostStorage = make(HostStorageMap)
	}
	return sfr.HostStorage.Add(hr.Addr, hs)
}

// checkFormatReq performs some validation to determine whether or not the
// system should be erased before allowing a format request for the hosts
// in the request. The goal is to prevent reformatting a running system while
// allowing (re-)format of hosts that are not participating as MS replicas.
func checkFormatReq(ctx context.Context, rpcClient UnaryInvoker, req *StorageFormatReq) error {
	reqHosts, err := common.ParseHostList(req.HostList, build.DefaultControlPort)
	if err != nil {
		return err
	}

	checkError := func(err error) error {
		// If the call succeeded, then the MS is running and
		// we should return an error.
		if err == nil {
			return FaultFormatRunningSystem
		}

		// We expect a system unavailable error when the MS is
		// not running, so it's safe to swallow these errors. Any
		// other error should be returned.
		if !(system.IsUnavailable(err) || system.IsUninitialized(err) ||
			err == errMSConnectionFailure) {
			return err
		}

		// Safe to proceed.
		return nil
	}

	// If the request does not specify a hostlist, then it will use the
	// hostlist set in the configuration, which implies a full system
	// format. In this case, we just need to check whether or not the
	// MS is running and fail if so.
	if len(reqHosts) == 0 {
		sysReq := &SystemQueryReq{FailOnUnavailable: true}
		_, err := SystemQuery(ctx, rpcClient, sysReq)

		return checkError(err)
	}

	// Check the hosts in the format request's hostlist to see if there is
	// a MS replica running on any of them, in which case an error will
	// be returned.
	for _, host := range reqHosts {
		sysReq := &SystemQueryReq{FailOnUnavailable: true}
		sysReq.AddHost(host)
		_, err := SystemQuery(ctx, rpcClient, sysReq)
		if err := checkError(err); err != nil {
			return err
		}
	}

	return nil
}

// StorageFormat concurrently performs storage preparation steps across
// all hosts supplied in the request's hostlist, or all configured hosts
// if not explicitly specified. The function blocks until all results
// (successful or otherwise) are received, and returns a single response
// structure containing results for all host storage prepare operations.
func StorageFormat(ctx context.Context, rpcClient UnaryInvoker, req *StorageFormatReq) (*StorageFormatResp, error) {
	if err := checkFormatReq(ctx, rpcClient, req); err != nil {
		return nil, err
	}

	pbReq := new(ctlpb.StorageFormatReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).StorageFormat(ctx, pbReq)
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	sfr := new(StorageFormatResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := sfr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := sfr.addHostResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return sfr, nil
}

type (
	// NvmeRebindReq contains the parameters for a storage nvme-rebind request.
	NvmeRebindReq struct {
		unaryRequest
		PCIAddr string `json:"pci_addr"`
	}

	// NvmeRebindResp contains the response from a storage nvme-rebind request.
	NvmeRebindResp struct {
		HostErrorsResp
	}
)

// StorageNvmeRebind rebinds NVMe SSD from kernel driver and binds to user-space driver on single
// server.
func StorageNvmeRebind(ctx context.Context, rpcClient UnaryInvoker, req *NvmeRebindReq) (*NvmeRebindResp, error) {
	// validate address in request
	if _, err := hardware.NewPCIAddress(req.PCIAddr); err != nil {
		return nil, errors.Wrap(err, "invalid pci address in request")
	}

	pbReq := new(ctlpb.NvmeRebindReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).StorageNvmeRebind(ctx, pbReq)
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(NvmeRebindResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := resp.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
		}
	}

	return resp, nil
}

type (
	// NvmeAddDeviceReq contains the parameters for a storage nvme-add-device request.
	//
	// If StorageTierIndex is set to -1, this indicates that the server should add the device
	// to the first configured bdev tier.
	NvmeAddDeviceReq struct {
		unaryRequest
		PCIAddr          string `json:"pci_addr"`
		EngineIndex      uint32 `json:"engine_index"`
		StorageTierIndex int32  `json:"storage_tier_index"`
	}

	// NvmeAddDeviceResp contains the response from a storage nvme-add-device request.
	NvmeAddDeviceResp struct {
		HostErrorsResp
	}
)

// WithStorageTierIndex updates request storage tier index value and returns reference.
func (req *NvmeAddDeviceReq) WithStorageTierIndex(i int32) *NvmeAddDeviceReq {
	req.StorageTierIndex = i

	return req
}

// StorageNvmeAddDevice adds NVMe SSD transport address to an engine configuration.
func StorageNvmeAddDevice(ctx context.Context, rpcClient UnaryInvoker, req *NvmeAddDeviceReq) (*NvmeAddDeviceResp, error) {
	// validate address in request
	if _, err := hardware.NewPCIAddress(req.PCIAddr); err != nil {
		return nil, errors.Wrap(err, "invalid pci address in request")
	}

	pbReq := new(ctlpb.NvmeAddDeviceReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).StorageNvmeAddDevice(ctx, pbReq)
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(NvmeAddDeviceResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := resp.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
		}
	}

	return resp, nil
}

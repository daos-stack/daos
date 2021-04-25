//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// MockMessage implements the proto.Message
// interface, and can be used for test mocks.
type MockMessage struct{}

func (mm *MockMessage) Reset()         {}
func (mm *MockMessage) String() string { return "mock" }
func (mm *MockMessage) ProtoMessage()  {}

type (
	// MockInvokerConfig defines the configured responses
	// for a MockInvoker.
	MockInvokerConfig struct {
		Sys              string
		UnaryError       error
		UnaryResponse    *UnaryResponse
		UnaryResponseSet []*UnaryResponse
		HostResponses    HostResponseChan
	}

	// MockInvoker implements the Invoker interface in order
	// to enable unit testing of API functions.
	MockInvoker struct {
		log         debugLogger
		cfg         MockInvokerConfig
		invokeCount int
	}
)

// MockMSResponse creates a synthetic Management Service response
// from the supplied HostResponse values.
func MockMSResponse(hostAddr string, hostErr error, hostMsg proto.Message) *UnaryResponse {
	return &UnaryResponse{
		fromMS: true,
		Responses: []*HostResponse{
			{
				Addr:    hostAddr,
				Error:   hostErr,
				Message: hostMsg,
			},
		},
	}
}

func (mi *MockInvoker) Debug(msg string) {
	mi.log.Debug(msg)
}

func (mi *MockInvoker) Debugf(fmtStr string, args ...interface{}) {
	mi.log.Debugf(fmtStr, args...)
}

func (mi *MockInvoker) GetSystem() string {
	return mi.cfg.Sys
}

func (mi *MockInvoker) InvokeUnaryRPC(ctx context.Context, uReq UnaryRequest) (*UnaryResponse, error) {
	return invokeUnaryRPC(ctx, mi.log, mi, uReq, nil)
}

func (mi *MockInvoker) InvokeUnaryRPCAsync(ctx context.Context, uReq UnaryRequest) (HostResponseChan, error) {
	if mi.cfg.HostResponses != nil || mi.cfg.UnaryError != nil {
		return mi.cfg.HostResponses, mi.cfg.UnaryError
	}

	responses := make(HostResponseChan)

	ur := mi.cfg.UnaryResponse
	if len(mi.cfg.UnaryResponseSet) > mi.invokeCount {
		ur = mi.cfg.UnaryResponseSet[mi.invokeCount]
	}
	if ur == nil {
		// If the config didn't define a response, just dummy one up for
		// tests that don't care.
		ur = &UnaryResponse{
			fromMS: uReq.isMSRequest(),
			Responses: []*HostResponse{
				{
					Addr:    "dummy",
					Message: &MockMessage{},
				},
			},
		}
	}

	mi.invokeCount++
	go func() {
		for _, hr := range ur.Responses {
			select {
			case <-ctx.Done():
				return
			case responses <- hr:
			}
		}
		close(responses)
	}()

	return responses, nil
}

func (mi *MockInvoker) SetConfig(_ *Config) {}

// DefaultMockInvokerConfig returns the default MockInvoker
// configuration.
func DefaultMockInvokerConfig() *MockInvokerConfig {
	return &MockInvokerConfig{}
}

// NewMockInvoker returns a configured MockInvoker. If
// a nil config is supplied, the default config is used.
func NewMockInvoker(log debugLogger, cfg *MockInvokerConfig) *MockInvoker {
	if cfg == nil {
		cfg = DefaultMockInvokerConfig()
	}

	if log == nil {
		log = defaultLogger
	}

	return &MockInvoker{
		log: log,
		cfg: *cfg,
	}
}

// DefaultMockInvoker returns a MockInvoker that uses the default configuration.
func DefaultMockInvoker(log debugLogger) *MockInvoker {
	return NewMockInvoker(log, nil)
}

// MockHostError represents an error received from multiple hosts.
type MockHostError struct {
	Hosts string
	Error string
}

func mockHostErrorsMap(t *testing.T, hostErrors ...*MockHostError) HostErrorsMap {
	hem := make(HostErrorsMap)

	for _, he := range hostErrors {
		if hes, found := hem[he.Error]; found {
			if _, err := hes.HostSet.Insert(he.Hosts); err != nil {
				t.Fatal(err)
			}
			continue
		}
		hem[he.Error] = &HostErrorSet{
			HostError: errors.New(he.Error),
			HostSet:   mockHostSet(t, he.Hosts),
		}
	}

	return hem
}

// MockHostErrorsResp returns HostErrorsResp when provided with MockHostErrors
// from different hostsets.
func MockHostErrorsResp(t *testing.T, hostErrors ...*MockHostError) HostErrorsResp {
	if len(hostErrors) == 0 {
		return HostErrorsResp{}
	}
	return HostErrorsResp{
		HostErrors: mockHostErrorsMap(t, hostErrors...),
	}
}

func mockHostSet(t *testing.T, hosts string) *hostlist.HostSet {
	hs, err := hostlist.CreateSet(hosts)
	if err != nil {
		t.Fatal(err)
	}
	return hs
}

func mockHostStorageSet(t *testing.T, hosts string, pbResp *ctlpb.StorageScanResp) *HostStorageSet {
	hss := &HostStorageSet{
		HostStorage: &HostStorage{},
		HostSet:     mockHostSet(t, hosts),
	}

	if err := convert.Types(pbResp.GetNvme().GetCtrlrs(), &hss.HostStorage.NvmeDevices); err != nil {
		t.Fatal(err)
	}
	if err := convert.Types(pbResp.GetScm().GetModules(), &hss.HostStorage.ScmModules); err != nil {
		t.Fatal(err)
	}
	if err := convert.Types(pbResp.GetScm().GetNamespaces(), &hss.HostStorage.ScmNamespaces); err != nil {
		t.Fatal(err)
	}

	return hss
}

// MockStorageScan represents results from scan on multiple hosts.
type MockStorageScan struct {
	Hosts    string
	HostScan *ctlpb.StorageScanResp
}

// MockHostStorageMap returns HostStorageMap when provided with mock storage
// scan results from different hostsets.
func MockHostStorageMap(t *testing.T, scans ...*MockStorageScan) HostStorageMap {
	hsm := make(HostStorageMap)

	for _, scan := range scans {
		hss := mockHostStorageSet(t, scan.Hosts, scan.HostScan)
		hk, err := hss.HostStorage.HashKey()
		if err != nil {
			t.Fatal(err)
		}
		hsm[hk] = hss
	}

	return hsm
}

func standardServerScanResponse(t *testing.T) *ctlpb.StorageScanResp {
	pbSsr := &ctlpb.StorageScanResp{
		Nvme: &ctlpb.ScanNvmeResp{},
		Scm:  &ctlpb.ScanScmResp{},
	}
	nvmeControllers := storage.NvmeControllers{
		storage.MockNvmeController(),
	}
	scmModules := storage.ScmModules{
		storage.MockScmModule(),
	}
	if err := convert.Types(nvmeControllers, &pbSsr.Nvme.Ctrlrs); err != nil {
		t.Fatal(err)
	}
	if err := convert.Types(scmModules, &pbSsr.Scm.Modules); err != nil {
		t.Fatal(err)
	}

	return pbSsr
}

// MocMockServerScanResp returns protobuf storage scan response with contents
// defined by the variant input string parameter.
func MockServerScanResp(t *testing.T, variant string) *ctlpb.StorageScanResp {
	ssr := standardServerScanResponse(t)
	nss := func(idxs ...int) storage.ScmNamespaces {
		nss := make(storage.ScmNamespaces, 0, len(idxs))
		for _, i := range idxs {
			ns := storage.MockScmNamespace(int32(i))
			nss = append(nss, ns)
		}
		return nss
	}
	ctrlrs := func(idxs ...int) storage.NvmeControllers {
		ncs := make(storage.NvmeControllers, 0, len(idxs))
		for _, i := range idxs {
			nc := storage.MockNvmeController(int32(i))
			ncs = append(ncs, nc)
		}
		return ncs
	}

	switch variant {
	case "withSpaceUsage":
		snss := make(storage.ScmNamespaces, 0)
		for _, i := range []int{0, 1} {
			sm := storage.MockScmMountPoint(int32(i))
			sns := storage.MockScmNamespace(int32(i))
			sns.Mount = sm
			snss = append(snss, sns)
		}
		if err := convert.Types(snss, &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
		ncs := make(storage.NvmeControllers, 0)
		for _, i := range []int{1, 2, 3, 4, 5, 6, 7, 8} {
			nc := storage.MockNvmeController(int32(i))
			nc.SocketID = int32(i % 2)
			sd := storage.MockSmdDevice(nc.PciAddr, int32(i))
			sd.TotalBytes = uint64(humanize.TByte) * uint64(i)
			sd.AvailBytes = uint64((humanize.TByte/4)*3) * uint64(i) // 25% used
			nc.SmdDevices = append(nc.SmdDevices, sd)
			ncs = append(ncs, nc)
		}
		if err := convert.Types(ncs, &ssr.Nvme.Ctrlrs); err != nil {
			t.Fatal(err)
		}
	case "pmemSingle":
		if err := convert.Types(nss(0), &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
	case "pmemDupNuma":
		ns1 := storage.MockScmNamespace(1)
		ns1.NumaNode = 0
		scmNamespaces := storage.ScmNamespaces{
			ns1,
			storage.MockScmNamespace(0),
		}
		if err := convert.Types(scmNamespaces, &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
	case "pmemA":
		// verify out of order namespace ids
		if err := convert.Types(nss(1, 0), &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
	case "pmemB":
		ns := nss(0, 1)
		for _, n := range ns {
			n.Size += uint64(humanize.GByte * 100)
		}
		if err := convert.Types(ns, &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
	case "nvmeSingle":
		if err := convert.Types(nss(0, 1), &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
		ssr.Nvme.Ctrlrs[0].SocketId = 0
	case "nvmeA":
		if err := convert.Types(nss(0, 1), &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
		if err := convert.Types(ctrlrs(1, 2, 3, 4), &ssr.Nvme.Ctrlrs); err != nil {
			t.Fatal(err)
		}
	case "nvmeB":
		if err := convert.Types(nss(0, 1), &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
		if err := convert.Types(ctrlrs(1, 2, 5, 4), &ssr.Nvme.Ctrlrs); err != nil {
			t.Fatal(err)
		}
	case "nvmeBasicA":
		if err := convert.Types(nss(0, 1), &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
		ncs := ctrlrs(1, 4)
		for _, c := range ncs {
			c.Model = ""
			c.FwRev = ""
			c.Serial = ""
		}
		if err := convert.Types(ncs, &ssr.Nvme.Ctrlrs); err != nil {
			t.Fatal(err)
		}
	case "nvmeBasicB":
		if err := convert.Types(nss(0, 1), &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
		ncs := ctrlrs(1, 4)
		for _, c := range ncs {
			c.Model = ""
			c.FwRev = ""
			c.Serial = ""
			c.Namespaces[0].Size += uint64(humanize.GByte * 100)
		}
		if err := convert.Types(ncs, &ssr.Nvme.Ctrlrs); err != nil {
			t.Fatal(err)
		}
	case "noNvme":
		ssr.Nvme.Ctrlrs = nil
	case "noScm":
		ssr.Scm.Modules = nil
	case "noStorage":
		ssr.Nvme.Ctrlrs = nil
		ssr.Scm.Modules = nil
	case "scmFailed":
		ssr.Scm.Modules = nil
		ssr.Scm.State = &ctlpb.ResponseState{
			Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
			Error:  "scm scan failed",
		}
	case "nvmeFailed":
		ssr.Nvme.Ctrlrs = nil
		ssr.Nvme.State = &ctlpb.ResponseState{
			Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
			Error:  "nvme scan failed",
		}
	case "bothFailed":
		ssr.Scm.Modules = nil
		ssr.Scm.State = &ctlpb.ResponseState{
			Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
			Error:  "scm scan failed",
		}
		ssr.Nvme.Ctrlrs = nil
		ssr.Nvme.State = &ctlpb.ResponseState{
			Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
			Error:  "nvme scan failed",
		}
	case "standard":
	default:
		t.Fatalf("MockServerScanResp(): variant %s unrecognized", variant)
	}
	return ssr
}

// MockHostResponses returns mock host responses.
func MockHostResponses(t *testing.T, count int, fmtStr string, respMsg proto.Message) []*HostResponse {
	hrs := make([]*HostResponse, count)
	for i := 0; i < count; i++ {
		hrs[i] = &HostResponse{
			Addr:    fmt.Sprintf(fmtStr, i),
			Message: respMsg,
		}
	}
	return hrs
}

// MockFailureMap returns failure map from the given range of integers.
func MockFailureMap(idxList ...int) map[int]struct{} {
	fm := make(map[int]struct{})
	for _, i := range idxList {
		fm[i] = struct{}{}
	}
	return fm
}

// MockFormatConf configures the contents of a StorageFormatResp.
type MockFormatConf struct {
	Hosts        int
	ScmPerHost   int
	NvmePerHost  int
	ScmFailures  map[int]struct{}
	NvmeFailures map[int]struct{}
}

// MockFormatResp returns a populated StorageFormatResp based on input config.
func MockFormatResp(t *testing.T, mfc MockFormatConf) *StorageFormatResp {
	hem := make(HostErrorsMap)
	hsm := make(HostStorageMap)

	for i := 0; i < mfc.Hosts; i++ {
		hs := &HostStorage{}
		hostName := fmt.Sprintf("host%d", i+1)

		for j := 0; j < mfc.ScmPerHost; j++ {
			if _, failed := mfc.ScmFailures[j]; failed {
				if err := hem.Add(hostName, errors.Errorf("/mnt/%d format failed", j+1)); err != nil {
					t.Fatal(err)
				}
				continue
			}
			hs.ScmMountPoints = append(hs.ScmMountPoints, &storage.ScmMountPoint{
				Info: ctlpb.ResponseStatus_CTL_SUCCESS.String(),
				Path: fmt.Sprintf("/mnt/%d", j+1),
			})
		}

		for j := 0; j < mfc.NvmePerHost; j++ {
			if _, failed := mfc.NvmeFailures[j]; failed {
				if err := hem.Add(hostName, errors.Errorf("NVMe device %d format failed", j+1)); err != nil {
					t.Fatal(err)
				}
				continue
			}

			// If the SCM format/mount failed for this idx, then there shouldn't
			// be an NVMe format result.
			if _, failed := mfc.ScmFailures[j]; failed {
				continue
			}
			hs.NvmeDevices = append(hs.NvmeDevices, &storage.NvmeController{
				Info:    ctlpb.ResponseStatus_CTL_SUCCESS.String(),
				PciAddr: fmt.Sprintf("%d", j+1),
			})
		}
		if err := hsm.Add(hostName, hs); err != nil {
			t.Fatal(err)
		}
	}

	if len(hem) == 0 {
		hem = nil
	}
	return &StorageFormatResp{
		HostErrorsResp: HostErrorsResp{
			HostErrors: hem,
		},
		HostStorage: hsm,
	}
}

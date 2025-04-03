//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"os"
	"path/filepath"
	"syscall"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	commonpb "github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	sysprov "github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

var (
	// compiler check to ensure that MockInstance implements the
	// Engine interface.
	_ Engine = (*MockInstance)(nil)
)

func getTestEngineInstance(log logging.Logger) *EngineInstance {
	cfg := engine.MockConfig().WithStorage(
		storage.NewTierConfig().
			WithStorageClass("ram").
			WithScmMountPoint("/foo/bar"),
	)
	runner := engine.NewRunner(log, cfg)
	storage := storage.MockProvider(log, 0, &cfg.Storage, nil, nil, nil, nil)
	return NewEngineInstance(log, storage, nil, runner, nil)
}

func TestServer_Instance_WithHostFaultDomain(t *testing.T) {
	instance := &EngineInstance{}
	fd, err := system.NewFaultDomainFromString("/one/two")
	if err != nil {
		t.Fatalf("couldn't create fault domain: %s", err)
	}

	updatedInstance := instance.WithHostFaultDomain(fd)

	// Updated to include the fault domain
	if diff := cmp.Diff(instance.hostFaultDomain, fd); diff != "" {
		t.Fatalf("unexpected results (-want, +got):\n%s\n", diff)
	}
	// updatedInstance is the same ptr as instance
	test.AssertEqual(t, updatedInstance, instance, "not the same structure")
}

func TestServer_Instance_updateFaultDomainInSuperblock(t *testing.T) {
	for name, tc := range map[string]struct {
		superblock *Superblock
		newDomain  *system.FaultDomain
		expErr     error
		expWritten bool
	}{
		"nil superblock": {
			newDomain: system.MustCreateFaultDomain("host"),
			expErr:    errors.New("nil superblock"),
		},
		"removing domain": {
			superblock: &Superblock{
				HostFaultDomain: "/host",
			},
			expErr: errors.New("nil fault domain"),
		},
		"adding domain": {
			superblock: &Superblock{},
			newDomain:  system.MustCreateFaultDomain("host"),
			expWritten: true,
		},
		"empty domain": {
			superblock: &Superblock{
				HostFaultDomain: "/",
			},
			newDomain: system.MustCreateFaultDomain(),
		},
		"same domain": {
			superblock: &Superblock{
				HostFaultDomain: "/host1",
			},
			newDomain: system.MustCreateFaultDomain("host1"),
		},
		"different domain": {
			superblock: &Superblock{
				HostFaultDomain: "/host1",
			},
			newDomain:  system.MustCreateFaultDomain("host2"),
			expWritten: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanupDir := test.CreateTestDir(t)
			defer cleanupDir()

			// Use real os.ReadFile in MockSysProvider to test superblock logic.
			cfg := engine.MockConfig().WithStorage(
				storage.NewTierConfig().
					WithStorageClass("ram").
					WithScmMountPoint("/foo/bar"),
			)
			runner := engine.NewRunner(log, cfg)
			sysCfg := sysprov.MockSysConfig{RealReadFile: true}
			sysProv := sysprov.NewMockSysProvider(log, &sysCfg)
			scmProv := scm.NewMockProvider(log, &scm.MockBackendConfig{}, &sysCfg)
			storage := storage.MockProvider(log, 0, &cfg.Storage, sysProv, scmProv,
				nil, nil)

			ei := NewEngineInstance(log, storage, nil, runner, nil).
				WithHostFaultDomain(tc.newDomain)
			ei.fsRoot = testDir
			ei._superblock = tc.superblock

			sbPath := ei.superblockPath()
			if err := os.MkdirAll(filepath.Dir(sbPath), 0755); err != nil {
				t.Fatalf("failed to make test superblock dir: %s", err.Error())
			}

			err := ei.updateFaultDomainInSuperblock()
			test.CmpErr(t, tc.expErr, err)

			// Ensure the newer value in the instance was written to the superblock
			err = ei.ReadSuperblock()
			if tc.expWritten {
				if err != nil {
					t.Fatalf("can't read expected superblock: %s", err.Error())
				}

				newSB := ei.getSuperblock()
				if newSB == nil {
					t.Fatalf("expected non-nil superblock")
				}

				expDomainStr := ""
				if tc.newDomain != nil {
					expDomainStr = tc.newDomain.String()
				}
				test.AssertEqual(t, expDomainStr, newSB.HostFaultDomain, "")
			} else if err == nil {
				t.Fatal("expected no superblock written")
			} else {
				test.CmpErr(t, syscall.ENOENT, err)
			}
		})
	}
}

type (
	MockInstanceConfig struct {
		CallDrpcResp        *drpc.Response
		CallDrpcErr         error
		GetRankResp         ranklist.Rank
		GetRankErr          error
		TargetCount         int
		Index               uint32
		Started             atm.Bool
		Ready               atm.Bool
		CheckerMode         atm.Bool
		LocalState          system.MemberState
		RemoveSuperblockErr error
		SetupRankErr        error
		StopErr             error
		ScmTierConfig       *storage.TierConfig
		ScanBdevTiersResult []storage.BdevTierScanResult
		LastHealthStats     map[string]*ctlpb.BioHealthResp
	}

	MockInstance struct {
		cfg MockInstanceConfig
	}
)

func NewMockInstance(cfg *MockInstanceConfig) *MockInstance {
	if cfg == nil {
		cfg = &MockInstanceConfig{}
	}

	return &MockInstance{
		cfg: *cfg,
	}
}

func DefaultMockInstance() *MockInstance {
	return NewMockInstance(nil)
}

func (mi *MockInstance) SetCheckerMode(enabled bool) {
	mi.cfg.CheckerMode.Store(enabled)
}

func (mi *MockInstance) CallDrpc(_ context.Context, _ drpc.Method, _ proto.Message) (*drpc.Response, error) {
	return mi.cfg.CallDrpcResp, mi.cfg.CallDrpcErr
}

func (mi *MockInstance) GetRank() (ranklist.Rank, error) {
	return mi.cfg.GetRankResp, mi.cfg.GetRankErr
}

func (mi *MockInstance) GetTargetCount() int {
	return mi.cfg.TargetCount
}

func (mi *MockInstance) Index() uint32 {
	return mi.cfg.Index
}

func (mi *MockInstance) IsStarted() bool {
	return mi.cfg.Started.Load()
}

func (mi *MockInstance) IsReady() bool {
	return mi.cfg.Ready.Load()
}

func (mi *MockInstance) LocalState() system.MemberState {
	return mi.cfg.LocalState
}

func (mi *MockInstance) RemoveSuperblock() error {
	return mi.cfg.RemoveSuperblockErr
}

func (mi *MockInstance) Run(_ context.Context) {}

func (mi *MockInstance) SetupRank(_ context.Context, _ ranklist.Rank, _ uint32) error {
	return mi.cfg.SetupRankErr
}

func (mi *MockInstance) Stop(os.Signal) error {
	return mi.cfg.StopErr
}

func (mi *MockInstance) ScanBdevTiers() ([]storage.BdevTierScanResult, error) {
	return nil, nil
}

func (mi *MockInstance) OnReady(fns ...onReadyFn) {}

func (mi *MockInstance) OnInstanceExit(fns ...onInstanceExitFn) {}

// The rest of these methods are only to implement the interface and
// should be removed. Please do not write any new tests that rely on them.
func (mi *MockInstance) newCret(_ string, _ error) *ctlpb.NvmeControllerResult {
	return nil
}

func (mi *MockInstance) tryDrpc(_ context.Context, _ drpc.Method) *system.MemberResult {
	return nil
}

func (mi *MockInstance) requestStart(_ context.Context) {}

func (mi *MockInstance) updateInUseBdevs(_ context.Context, _ []storage.NvmeController, _ uint64, _ uint64) ([]storage.NvmeController, error) {
	return []storage.NvmeController{}, nil
}

func (mi *MockInstance) isAwaitingFormat() bool {
	return false
}

func (mi *MockInstance) NotifyDrpcReady(_ *srvpb.NotifyReadyReq) {}
func (mi *MockInstance) NotifyStorageReady(_ bool)               {}

func (mi *MockInstance) GetBioHealth(context.Context, *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
	return nil, nil
}

func (mi *MockInstance) ListSmdDevices(context.Context, *ctlpb.SmdDevReq) (*ctlpb.SmdDevResp, error) {
	return nil, nil
}

func (mi *MockInstance) StorageFormatNVMe() commonpb.NvmeControllerResults {
	return nil
}

func (mi *MockInstance) StorageFormatSCM(context.Context, bool) *ctlpb.ScmMountResult {
	return nil
}

func (mi *MockInstance) GetStorage() *storage.Provider {
	return nil
}

func (mi *MockInstance) Debugf(format string, args ...interface{}) {
	return
}

func (mi *MockInstance) Tracef(format string, args ...interface{}) {
	return
}

func (mi *MockInstance) Publish(event *events.RASEvent) {
	return
}

func (mi *MockInstance) GetLastHealthStats(pciAddr string) *ctlpb.BioHealthResp {
	return mi.cfg.LastHealthStats[pciAddr]
}

func (mi *MockInstance) SetLastHealthStats(pciAddr string, bhr *ctlpb.BioHealthResp) {
	mi.cfg.LastHealthStats[pciAddr] = bhr
}

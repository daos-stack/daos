//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"syscall"
	"testing"

	"github.com/google/go-cmp/cmp"
	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	commonpb "github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/control"
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

func TestServer_EngineInstance_updateIncarnation(t *testing.T) {
	for name, tc := range map[string]struct {
		noTestSubdirs    bool
		reqIncarnation   uint64
		startIncarnation uint64
		startSuperblock  *Superblock
		expErr           error
		expIncarnation   uint64
		expSuperblock    *Superblock
		expWritten       bool
	}{
		"nil superblock": {
			reqIncarnation: 123,
			expErr:         errors.New("nil superblock"),
			expIncarnation: 123, // set in memory even if we can't write the superblock
		},
		"superblock write failed": {
			noTestSubdirs:   true,
			reqIncarnation:  456,
			startSuperblock: &Superblock{},
			expErr:          syscall.ENOENT,
			expIncarnation:  456,
			expSuperblock:   &Superblock{Incarnation: 456},
		},
		"superblock incarnation unset": {
			reqIncarnation:  456,
			startSuperblock: &Superblock{},
			expIncarnation:  456,
			expSuperblock:   &Superblock{Incarnation: 456},
			expWritten:      true,
		},
		"superblock incarnation set": {
			reqIncarnation:  456,
			startSuperblock: &Superblock{Incarnation: 123},
			expIncarnation:  456,
			expSuperblock:   &Superblock{Incarnation: 456},
			expWritten:      true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)

			testEngineIdx := 0
			baseDir, cleanup := test.CreateTestDir(t)
			defer cleanup()
			mdDir := filepath.Join(baseDir, "daos_control", fmt.Sprintf("engine%d", testEngineIdx))

			if !tc.noTestSubdirs {
				if err := os.MkdirAll(mdDir, 0755); err != nil {
					t.Fatal(err)
				}
			}

			storageProv := storage.NewProvider(logging.FromContext(ctx), testEngineIdx, &storage.Config{
				ControlMetadata: storage.ControlMetadata{Path: baseDir},
			}, nil, nil, nil, nil)
			ei := newTestEngine(logging.FromContext(ctx), false, storageProv, &engine.Config{})
			ei.incarnation = tc.startIncarnation
			ei.setSuperblock(tc.startSuperblock)

			err := ei.updateIncarnation(&srvpb.NotifyReadyReq{
				Incarnation: tc.reqIncarnation,
			})

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expIncarnation, ei.incarnation, "")
			test.CmpAny(t, "superblock", tc.expSuperblock, ei.getSuperblock())

			sbBytes, err := os.ReadFile(filepath.Join(mdDir, "superblock"))
			if tc.expWritten {
				if err != nil {
					t.Fatal(err)
				}

				writtenSb := &Superblock{}
				if err := writtenSb.Unmarshal(sbBytes); err != nil {
					t.Fatal(err)
				}

				test.CmpAny(t, "written superblock", tc.expSuperblock, writtenSb)
			} else {
				if err == nil {
					t.Fatal("expected superblock NOT to be written")
				}

				if !os.IsNotExist(err) {
					t.Fatal(err)
				}
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
func (mi *MockInstance) NotifyStorageReady(_ *ranklist.Rank)     {}

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

func TestEngineInstance_determineRank(t *testing.T) {
	defaultUUID := uuid.MustParse("11111111-1111-1111-1111-111111111111")
	defaultReq := &srvpb.NotifyReadyReq{
		Uri:            "test-uri",
		SecondaryUris:  []string{"secondary-uri"},
		Nctxs:          8,
		SecondaryNctxs: []uint32{4},
		Incarnation:    1,
	}

	for name, tc := range map[string]struct {
		setupRank           *ranklist.Rank // Initial rank in superblock
		replaceRank         *ranklist.Rank // Replace rank pointer (nil = not replacing)
		useNotifyAPI        bool           // If true, use NotifyStorageReady() instead of direct Store()
		joinResp            *control.SystemJoinResp
		joinErr             error
		noSB                bool
		expJoinReqRank      ranklist.Rank
		expJoinReqReplace   bool
		expRank             ranklist.Rank
		expLocalJoin        bool
		expErr              error
		validateFullJoinReq bool // If true, validate all SystemJoinReq fields
	}{
		"nil superblock": {
			noSB:   true,
			expErr: errors.New("nil superblock"),
		},
		"standard join - no rank": {
			joinResp: &control.SystemJoinResp{
				Rank:       5,
				State:      system.MemberStateJoined,
				LocalJoin:  false,
				MapVersion: 1,
			},
			expJoinReqRank:    ranklist.NilRank,
			expJoinReqReplace: false,
			expRank:           5,
			expLocalJoin:      false,
		},
		"standard join - existing rank": {
			setupRank: func() *ranklist.Rank { r := ranklist.Rank(3); return &r }(),
			joinResp: &control.SystemJoinResp{
				Rank:       3,
				State:      system.MemberStateJoined,
				LocalJoin:  true,
				MapVersion: 1,
			},
			expJoinReqRank:    3,
			expJoinReqReplace: false,
			expRank:           3,
			expLocalJoin:      true,
		},
		"replace with auto-detect": {
			replaceRank: func() *ranklist.Rank { r := ranklist.NilRank; return &r }(),
			joinResp: &control.SystemJoinResp{
				Rank:       7,
				State:      system.MemberStateJoined,
				LocalJoin:  false,
				MapVersion: 2,
			},
			expJoinReqRank:    ranklist.NilRank,
			expJoinReqReplace: true,
			expRank:           7,
			expLocalJoin:      false,
		},
		"replace with explicit rank": {
			replaceRank: func() *ranklist.Rank { r := ranklist.Rank(10); return &r }(),
			joinResp: &control.SystemJoinResp{
				Rank:       10,
				State:      system.MemberStateJoined,
				LocalJoin:  false,
				MapVersion: 2,
			},
			expJoinReqRank:    10,
			expJoinReqReplace: true,
			expRank:           10,
			expLocalJoin:      false,
		},
		"replace with explicit rank 0": {
			replaceRank: func() *ranklist.Rank { r := ranklist.Rank(0); return &r }(),
			joinResp: &control.SystemJoinResp{
				Rank:       0,
				State:      system.MemberStateJoined,
				LocalJoin:  false,
				MapVersion: 2,
			},
			expJoinReqRank:    0,
			expJoinReqReplace: true,
			expRank:           0,
			expLocalJoin:      false,
		},
		"replace overrides superblock rank": {
			setupRank:   func() *ranklist.Rank { r := ranklist.Rank(5); return &r }(),
			replaceRank: func() *ranklist.Rank { r := ranklist.Rank(10); return &r }(),
			joinResp: &control.SystemJoinResp{
				Rank:       10,
				State:      system.MemberStateJoined,
				LocalJoin:  false,
				MapVersion: 2,
			},
			expJoinReqRank:    10,
			expJoinReqReplace: true,
			expRank:           10,
			expLocalJoin:      false,
		},
		"excluded rank": {
			setupRank: func() *ranklist.Rank { r := ranklist.Rank(3); return &r }(),
			joinResp: &control.SystemJoinResp{
				Rank:  3,
				State: system.MemberStateExcluded,
			},
			expJoinReqRank:    3,
			expJoinReqReplace: false,
			expErr:            errors.New("excluded"),
		},
		"admin excluded rank": {
			setupRank: func() *ranklist.Rank { r := ranklist.Rank(3); return &r }(),
			joinResp: &control.SystemJoinResp{
				Rank:  3,
				State: system.MemberStateAdminExcluded,
			},
			expJoinReqRank:    3,
			expJoinReqReplace: false,
			expErr:            errors.New("excluded"),
		},
		"join failure": {
			joinErr: errors.New("join failed"),
			expErr:  errors.New("join failed"),
		},
		"NotifyStorageReady: standard join - no replace": {
			useNotifyAPI:        true,
			replaceRank:         nil, // NotifyStorageReady(nil) = standard join
			setupRank:           nil,
			expJoinReqRank:      ranklist.NilRank,
			expJoinReqReplace:   false,
			expRank:             5,
			expLocalJoin:        false,
			validateFullJoinReq: true,
			joinResp: &control.SystemJoinResp{
				Rank:       5,
				State:      system.MemberStateJoined,
				LocalJoin:  false,
				MapVersion: 1,
			},
		},
		"NotifyStorageReady: standard join - existing rank in superblock": {
			useNotifyAPI:        true,
			replaceRank:         nil, // NotifyStorageReady(nil) = standard join
			setupRank:           func() *ranklist.Rank { r := ranklist.Rank(5); return &r }(),
			expJoinReqRank:      5,
			expJoinReqReplace:   false,
			expRank:             5,
			expLocalJoin:        true,
			validateFullJoinReq: true,
			joinResp: &control.SystemJoinResp{
				Rank:       5,
				State:      system.MemberStateJoined,
				LocalJoin:  true,
				MapVersion: 1,
			},
		},
		"NotifyStorageReady: replace with auto-detect": {
			useNotifyAPI:        true,
			replaceRank:         func() *ranklist.Rank { r := ranklist.NilRank; return &r }(),
			setupRank:           nil,
			expJoinReqRank:      ranklist.NilRank,
			expJoinReqReplace:   true,
			expRank:             7,
			expLocalJoin:        false,
			validateFullJoinReq: true,
			joinResp: &control.SystemJoinResp{
				Rank:       7,
				State:      system.MemberStateJoined,
				LocalJoin:  false,
				MapVersion: 1,
			},
		},
		"NotifyStorageReady: replace with explicit rank 7": {
			useNotifyAPI:        true,
			replaceRank:         func() *ranklist.Rank { r := ranklist.Rank(7); return &r }(),
			setupRank:           nil,
			expJoinReqRank:      7,
			expJoinReqReplace:   true,
			expRank:             7,
			expLocalJoin:        false,
			validateFullJoinReq: true,
			joinResp: &control.SystemJoinResp{
				Rank:       7,
				State:      system.MemberStateJoined,
				LocalJoin:  false,
				MapVersion: 1,
			},
		},
		"NotifyStorageReady: replace overrides superblock rank": {
			useNotifyAPI:        true,
			replaceRank:         func() *ranklist.Rank { r := ranklist.Rank(10); return &r }(),
			setupRank:           func() *ranklist.Rank { r := ranklist.Rank(5); return &r }(),
			expJoinReqRank:      10,
			expJoinReqReplace:   true,
			expRank:             10,
			expLocalJoin:        false,
			validateFullJoinReq: true,
			joinResp: &control.SystemJoinResp{
				Rank:       10,
				State:      system.MemberStateJoined,
				LocalJoin:  false,
				MapVersion: 1,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var capturedJoinReq *control.SystemJoinReq

			mockJoin := func(_ context.Context, req *control.SystemJoinReq) (*control.SystemJoinResp, error) {
				capturedJoinReq = req

				if tc.joinErr != nil {
					return nil, tc.joinErr
				}

				// Verify the replace flag is set correctly
				test.AssertEqual(t, tc.expJoinReqReplace, req.Replace, "Replace flag mismatch")

				// Verify rank in join request
				test.AssertEqual(t, tc.expJoinReqRank, req.Rank, "unexpected rank in join request")

				// Verify additional join request fields if requested
				if tc.validateFullJoinReq {
					test.AssertEqual(t, defaultUUID.String(), req.UUID, "UUID mismatch")
					test.AssertEqual(t, defaultReq.Uri, req.URI, "URI mismatch")
					test.AssertEqual(t, defaultReq.SecondaryUris, req.SecondaryURIs, "SecondaryURIs mismatch")
					test.AssertEqual(t, defaultReq.Nctxs, req.NumContexts, "NumContexts mismatch")
					test.AssertEqual(t, defaultReq.SecondaryNctxs, req.NumSecondaryContexts, "NumSecondaryContexts mismatch")
				}

				return tc.joinResp, nil
			}

			testEngineIdx := 0
			baseDir, cleanup := test.CreateTestDir(t)
			defer cleanup()
			mdDir := filepath.Join(baseDir, "daos_control", fmt.Sprintf("engine%d", testEngineIdx))
			if err := os.MkdirAll(mdDir, 0755); err != nil {
				t.Fatal(err)
			}

			cfg := engine.MockConfig().
				WithStorageControlMetadataPath(baseDir).
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("ram").
						WithScmMountPoint("/mnt/daos"),
				)
			runner := engine.NewRunner(log, cfg)
			storage := storage.MockProvider(log, 0, &cfg.Storage, nil, nil, nil, nil)
			engine := NewEngineInstance(log, storage, mockJoin, runner, nil)
			sb := &Superblock{
				UUID:      defaultUUID.String(),
				ValidRank: tc.setupRank != nil,
			}
			if tc.setupRank != nil {
				sb.Rank = tc.setupRank
			}
			if !tc.noSB {
				engine.setSuperblock(sb)
			}

			// Set replace rank via NotifyStorageReady() API or direct Store()
			if tc.useNotifyAPI {
				engine.NotifyStorageReady(tc.replaceRank)
				// Verify replaceRank was stored correctly by NotifyStorageReady()
				storedRank := engine.replaceRank.Load()
				if tc.replaceRank == nil {
					test.AssertTrue(t, storedRank == nil, "replaceRank should be nil for standard join")
				} else {
					test.AssertTrue(t, storedRank != nil, "replaceRank should be set")
					test.AssertEqual(t, *tc.replaceRank, *storedRank, "stored replaceRank mismatch")
				}
			} else if tc.replaceRank != nil {
				engine.replaceRank.Store(tc.replaceRank)
			}

			rank, localJoin, mapVersion, err := engine.determineRank(test.Context(t), defaultReq)

			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expRank, rank, "rank mismatch")
			test.AssertEqual(t, tc.expLocalJoin, localJoin, "localJoin mismatch")
			if tc.joinResp != nil {
				test.AssertEqual(t, tc.joinResp.MapVersion, mapVersion, "mapVersion mismatch")
			}

			// Verify replaceRank is cleared after join
			test.AssertTrue(t, engine.replaceRank.Load() == nil, "replaceRank should be cleared")

			// Additional validation for captured join request
			if tc.validateFullJoinReq && capturedJoinReq == nil {
				t.Fatal("join request was not captured")
			}
		})
	}
}

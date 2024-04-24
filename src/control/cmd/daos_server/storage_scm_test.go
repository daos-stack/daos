//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

var (
	zero uint = 0
	one  uint = 1
)

func TestDaosServer_setSockFromCfg(t *testing.T) {
	mockAffinitySource := func(l logging.Logger, e *engine.Config) (uint, error) {
		l.Debugf("mock affinity source: assigning engine numa to its index %d", e.Index)
		return uint(e.Index), nil
	}

	for name, tc := range map[string]struct {
		cfg       *config.Server
		affSrc    config.EngineAffinityFn
		expSockID *uint
		expErr    error
	}{
		"nil config": {},
		"sock derived from config": {
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String())),
			),
			expSockID: &zero,
		},
		"sock derived from config; numa node pinned": {
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().
					WithPinnedNumaNode(1).
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String())),
			),
			expSockID: &one,
		},
		"sock derived from config; engines with different numa": {
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().WithIndex(0).
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String())),
				engine.NewConfig().WithIndex(1).
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String())),
			),
		},
		"sock derived from config; engines with same numa": {
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().WithIndex(0).
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String())),
				engine.NewConfig().WithIndex(0).
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String())),
			),
			expSockID: &zero,
		},
		"sock derived from config; engines with different numa; one using ram": {
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().WithIndex(0).
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassRam.String())),
				engine.NewConfig().WithIndex(1).
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String())),
			),
			expSockID: &one,
		},
		"affinity source returns error": {
			affSrc: func(logging.Logger, *engine.Config) (uint, error) {
				return 0, errors.New("fail")
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			if tc.cfg == nil {
				tc.cfg = &config.Server{}
			}

			affSrc := tc.affSrc
			if affSrc == nil {
				affSrc = mockAffinitySource
			}

			sockID := getSockFromCfg(log, tc.cfg, affSrc)

			if diff := cmp.Diff(tc.expSockID, sockID); diff != "" {
				t.Fatalf("unexpected socket ID (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func getMockScmCmdInit(log logging.Logger, smbc scm.MockBackendConfig) (*scm.MockBackend, initScmCmdFn) {
	msb := scm.NewMockBackend(&smbc)
	scs := server.NewMockStorageControlService(log, nil, nil,
		scm.NewProvider(log, msb, system.NewMockSysProvider(log, nil), nil),
		bdev.NewProvider(log, bdev.NewMockBackend(nil)), nil)

	return msb, func(cmd *scmCmd) (*server.StorageControlService, error) {
		cmd.Logger.Tracef("mock storage control service set: %+v", scs)
		return scs, nil
	}
}

func TestDaosServer_preparePMem(t *testing.T) {
	var printNamespace strings.Builder
	msns := storage.ScmNamespaces{storage.MockScmNamespace()}
	if err := pretty.PrintScmNamespaces(msns, &printNamespace); err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		noForce   bool
		zeroNrNs  bool
		sockID    *uint
		prepResp  *storage.ScmPrepareResponse
		prepErr   error
		expCalls  []storage.ScmPrepareRequest
		expResp   *storage.ScmPrepareResponse
		expErr    error
		expLogMsg string
	}{
		"no modules": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
			},
			expErr: storage.FaultScmNoPMem,
		},
		"prepare fails": {
			prepErr: errors.New("fail"),
			expErr:  errors.New("fail"),
		},
		"create regions; no consent": {
			noForce:  true,
			expCalls: []storage.ScmPrepareRequest{},
			// prompts for confirmation and gets EOF
			expErr: errors.New("consent not given"),
		},
		"create regions; no state change": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
			},
			expErr: errors.New("failed to create regions"),
		},
		"create regions; reboot required": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
				RebootRequired: true,
			},
			expLogMsg: storage.ScmMsgRebootRequired,
		},
		"create regions; reboot required; single socket": {
			sockID: &one,
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
				RebootRequired: true,
			},
			expCalls: []storage.ScmPrepareRequest{
				{NrNamespacesPerSocket: 1, SocketID: &one},
			},
			expLogMsg: storage.ScmMsgRebootRequired,
		},
		"non-interleaved regions": {
			// If non-interleaved regions are detected, prep will return an
			// error. So returning the state is unexpected.
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNotInterleaved,
				},
			},
			expErr: errors.New("unexpected state"),
		},
		"invalid number of namespaces per socket": {
			zeroNrNs: true,
			expCalls: []storage.ScmPrepareRequest{},
			expErr:   errors.New("at least 1"),
		},
		"create namespaces; no state change": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
			},
			expErr: errors.New("failed to create namespaces"),
		},
		"create namespaces; no namespaces reported": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expErr: errors.New("failed to find namespaces"),
		},
		"create namespaces; namespaces reported": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
				Namespaces: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			expResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
				Namespaces: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			smbc := scm.MockBackendConfig{
				PrepRes: tc.prepResp,
				PrepErr: tc.prepErr,
			}
			msb, mockInitFn := getMockScmCmdInit(log, smbc)

			cmd := prepareSCMCmd{
				Force: !tc.noForce,
			}
			cmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}
			cmd.SocketID = tc.sockID
			if err := cmd.initWith(mockInitFn); err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			nrNs := uint(1)
			if tc.zeroNrNs {
				nrNs = 0
			}
			cmd.NrNamespacesPerSocket = nrNs

			gotResp, gotErr := preparePMem(&cmd)
			test.CmpErr(t, tc.expErr, gotErr)

			if tc.expCalls == nil {
				tc.expCalls = []storage.ScmPrepareRequest{
					{NrNamespacesPerSocket: 1},
				}
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected resp (-want, +got):\n%s\n", diff)
			}

			msb.RLock()
			if msb.PrepareCalls == nil {
				msb.PrepareCalls = []storage.ScmPrepareRequest{}
			}
			if diff := cmp.Diff(tc.expCalls, msb.PrepareCalls); diff != "" {
				t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
			}
			if len(msb.ResetCalls) != 0 {
				t.Fatalf("unexpected number of reset calls, want 0 got %d",
					len(msb.ResetCalls))
			}
			msb.RUnlock()

			if tc.expErr != nil {
				return
			}

			if tc.expLogMsg != "" {
				if !strings.Contains(buf.String(), tc.expLogMsg) {
					t.Fatalf("expected to see %q in log, got %q", tc.expLogMsg, buf.String())
				}
			}
		})
	}
}

func TestDaosServer_resetPMem(t *testing.T) {
	var printNamespace strings.Builder
	msns := storage.ScmNamespaces{storage.MockScmNamespace()}
	if err := pretty.PrintScmNamespaces(msns, &printNamespace); err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		noForce   bool
		sockID    *uint
		prepResp  *storage.ScmPrepareResponse
		prepErr   error
		expErr    error
		expLogMsg string
		expCalls  []storage.ScmPrepareRequest
	}{
		"remove regions; no consent": {
			noForce:  true,
			expCalls: []storage.ScmPrepareRequest{},
			// prompts for confirmation and gets EOF
			expErr: errors.New("consent not given"),
		},
		"remove regions; reboot required; illegal state": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
				RebootRequired: true,
			},
			expErr: errors.New("unexpected state if reboot"),
		},
		"remove regions; reboot required; not interleaved": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNotInterleaved,
				},
				RebootRequired: true,
			},
			expLogMsg: "regions will be removed",
		},
		"remove regions; reboot required; free capacity": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
				RebootRequired: true,
			},
			expLogMsg: "regions will be removed",
		},
		"remove regions; reboot required; free capacity; single socket": {
			sockID: &one,
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
				RebootRequired: true,
			},
			expCalls: []storage.ScmPrepareRequest{
				{Reset: true, SocketID: &one},
			},
			expLogMsg: "regions will be removed",
		},
		"remove regions; reboot required; no free capacity": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
				RebootRequired: true,
			},
			expLogMsg: "have been removed and regions will be removed",
		},
		"remove regions; reboot required; partial free capacity": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmPartFreeCap,
				},
				RebootRequired: true,
			},
			expLogMsg: "have been removed and regions will be removed",
		},
		"remove regions; reboot required; unhealthy": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNotHealthy,
				},
				RebootRequired: true,
			},
			expLogMsg: "have been removed and regions (some with an unexpected",
		},
		"remove regions; reboot required; unknown memory type": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmUnknownMode,
				},
				RebootRequired: true,
			},
			expLogMsg: "have been removed and regions (some with an unexpected",
		},
		"no modules": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
			},
			expErr: storage.FaultScmNoPMem,
		},
		"no regions": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
			},
			expLogMsg: "reset successful",
		},
		"regions not interleaved": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNotInterleaved,
				},
			},
			expErr: errors.New("unexpected state"),
		},
		"free capacity": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
			},
			expErr: errors.New("unexpected state"),
		},
		"no free capacity": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expErr: errors.New("unexpected state"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			smbc := scm.MockBackendConfig{
				PrepResetRes: tc.prepResp,
				PrepResetErr: tc.prepErr,
			}
			msb, mockInitFn := getMockScmCmdInit(log, smbc)

			cmd := resetSCMCmd{
				Force: !tc.noForce,
			}
			cmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}
			cmd.SocketID = tc.sockID
			if err := cmd.initWith(mockInitFn); err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			err := resetPMem(&cmd)
			test.CmpErr(t, tc.expErr, err)

			if tc.expCalls == nil {
				tc.expCalls = []storage.ScmPrepareRequest{
					{Reset: true},
				}
			}

			msb.RLock()
			if msb.ResetCalls == nil {
				msb.ResetCalls = []storage.ScmPrepareRequest{}
			}
			if diff := cmp.Diff(tc.expCalls, msb.ResetCalls); diff != "" {
				t.Fatalf("unexpected reset calls (-want, +got):\n%s\n", diff)
			}
			if len(msb.PrepareCalls) != 0 {
				t.Fatalf("unexpected number of prepare calls, want 0 got %d",
					len(msb.PrepareCalls))
			}
			msb.RUnlock()

			if tc.expErr != nil {
				return
			}

			if tc.expLogMsg != "" {
				if !strings.Contains(buf.String(), tc.expLogMsg) {
					t.Fatalf("expected to see %q in log, got %q", tc.expLogMsg, buf.String())
				}
			}
		})
	}
}

func TestDaosServer_scanSCM(t *testing.T) {
	zero := uint(0)

	for name, tc := range map[string]struct {
		sockID             *uint
		cfg                *config.Server
		ignoreCfg          bool
		smbc               scm.MockBackendConfig
		expErr             error
		expResp            *storage.ScmScanResponse
		expModulesCalls    []int
		expNamespacesCalls []int
	}{
		"normal scan": {
			smbc: scm.MockBackendConfig{
				GetModulesRes:    storage.ScmModules{storage.MockScmModule(0)},
				GetNamespacesRes: storage.ScmNamespaces{storage.MockScmNamespace(0)},
			},
			expNamespacesCalls: []int{-1},
			expModulesCalls:    []int{-1},
			expResp: &storage.ScmScanResponse{
				Modules:    storage.ScmModules{storage.MockScmModule(0)},
				Namespaces: storage.ScmNamespaces{storage.MockScmNamespace(0)},
			},
		},
		"failed scan": {
			smbc: scm.MockBackendConfig{
				GetModulesRes:    storage.ScmModules{storage.MockScmModule(0)},
				GetNamespacesErr: errors.New("fail"),
			},
			expNamespacesCalls: []int{-1},
			expModulesCalls:    []int{-1},
			expErr:             errors.New("fail"),
		},
		"single socket scan": {
			sockID: &zero,
			smbc: scm.MockBackendConfig{
				GetModulesRes:    storage.ScmModules{storage.MockScmModule(0)},
				GetNamespacesRes: storage.ScmNamespaces{storage.MockScmNamespace(0)},
			},
			expNamespacesCalls: []int{0},
			expModulesCalls:    []int{0},
			expResp: &storage.ScmScanResponse{
				Modules:    storage.ScmModules{storage.MockScmModule(0)},
				Namespaces: storage.ScmNamespaces{storage.MockScmNamespace(0)},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			msb, mockInitFn := getMockScmCmdInit(log, tc.smbc)

			cmd := scanSCMCmd{}
			cmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}
			cmd.SocketID = tc.sockID
			cmd.config = tc.cfg
			cmd.IgnoreConfig = tc.ignoreCfg
			if err := cmd.initWith(mockInitFn); err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			resp, err := scanPMem(&cmd)
			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected scan response calls (-want, +got):\n%s\n", diff)
			}

			msb.RLock()
			if diff := cmp.Diff(tc.expModulesCalls, msb.GetModulesCalls); diff != "" {
				t.Fatalf("unexpected get modules calls (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expNamespacesCalls, msb.GetNamespacesCalls); diff != "" {
				t.Fatalf("unexpected get namespaces calls (-want, +got):\n%s\n", diff)
			}
			msb.RUnlock()
		})
	}
}

func TestDaosServer_SCM_Commands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Prepare namespaces; JSON; no force",
			"scm prepare -j",
			printCommand(t, &prepareSCMCmd{
				NrNamespacesPerSocket: 1,
			}),
			nil,
		},
		{
			"Prepare namespaces with all opts with JSON",
			"scm prepare -S 2 -f --socket 0 -j",
			printCommand(t, &prepareSCMCmd{
				NrNamespacesPerSocket: 2,
				Force:                 true,
			}),
			nil,
		},
		{
			"Prepare namespaces with all opts",
			"scm prepare -S 2 -f --socket 0",
			printCommand(t, &prepareSCMCmd{
				NrNamespacesPerSocket: 2,
				Force:                 true,
			}),
			nil,
		},
		{
			"Prepare namespaces; bad opt",
			"scm prepare -X",
			"",
			errors.New("unknown"),
		},
		{
			"Reset namespaces with all opts",
			"scm reset -f --socket 1",
			printCommand(t, &resetSCMCmd{
				Force: true,
			}),
			nil,
		},
		{
			"Reset namespaces; bad opt",
			"scm reset -S",
			"",
			errors.New("unknown"),
		},
		{
			"Scan namespaces",
			"scm scan",
			printCommand(t, &scanSCMCmd{}),
			nil,
		},
		{
			"Scan namespaces; socket 1",
			"scm scan --socket 1",
			printCommand(t, &scanSCMCmd{}),
			nil,
		},
	})
}

// TestDaosServer_SCM_Commands_JSON verifies that when the JSON-output flag is set only JSON is
// printed to standard out. Test cases should cover all scm subcommand variations.
func TestDaosServer_SCM_Commands_JSON(t *testing.T) {
	// Use a normal logger to verify that we don't mess up JSON output.
	log := logging.NewCommandLineLogger()

	genSetHelpers := func(smbc scm.MockBackendConfig) func(*mainOpts) {
		_, mockInit := getMockScmCmdInit(log, smbc)
		return func(opts *mainOpts) {
			opts.scmInitHelper = mockInit
		}
	}

	runJSONCmdTests(t, log, []jsonCmdTest{
		{
			"Prepare namespaces; JSON; no force",
			"scm prepare -j",
			genSetHelpers(scm.MockBackendConfig{}),
			nil,
			errNoForceWithJSON,
		},
		{
			"Prepare namespaces; JSON; with force",
			"scm prepare -j -f",
			genSetHelpers(scm.MockBackendConfig{
				PrepRes: &storage.ScmPrepareResponse{
					Socket: &storage.ScmSocketState{
						State: storage.ScmNoFreeCap,
					},
					Namespaces: storage.ScmNamespaces{
						storage.MockScmNamespace(),
					},
				},
			}),
			storage.ScmNamespaces{storage.MockScmNamespace()},
			nil,
		},
		{
			"Prepare namespaces; JSON; with force; returns error",
			"scm prepare -j -f",
			genSetHelpers(scm.MockBackendConfig{
				PrepErr: errors.New("bad prep"),
			}),
			nil,
			errors.New("bad prep"),
		},
		{
			"Reset namespaces; JSON; no force",
			"scm reset -j",
			genSetHelpers(scm.MockBackendConfig{}),
			nil,
			errNoForceWithJSON,
		},
		{
			"Reset namespaces; JSON; with force",
			"scm reset -j -f",
			genSetHelpers(scm.MockBackendConfig{
				PrepResetRes: &storage.ScmPrepareResponse{
					RebootRequired: true,
					Socket: &storage.ScmSocketState{
						State: storage.ScmFreeCap,
					},
				},
			}),
			nil,
			nil,
		},
		{
			"Reset namespaces; JSON; with force; returns error",
			"scm reset -j -f",
			genSetHelpers(scm.MockBackendConfig{
				PrepResetErr: errors.New("bad prep"),
			}),
			nil,
			errors.New("bad prep"),
		},
		{
			"Scan modules; JSON",
			"scm scan -j",
			genSetHelpers(scm.MockBackendConfig{
				GetModulesRes: storage.ScmModules{
					storage.MockScmModule(),
				},
			}),
			storage.ScmModules{storage.MockScmModule()},
			nil,
		},
		{
			"Scan modules; JSON; returns error",
			"scm scan -j",
			genSetHelpers(scm.MockBackendConfig{
				GetModulesErr: errors.New("bad prep"),
			}),
			nil,
			errors.New("bad prep"),
		},
		{
			"Scan namespaces; JSON",
			"scm scan -j",
			genSetHelpers(scm.MockBackendConfig{
				GetModulesRes: storage.ScmModules{
					storage.MockScmModule(),
				},
				GetNamespacesRes: storage.ScmNamespaces{
					storage.MockScmNamespace(),
				},
			}),
			storage.ScmNamespaces{storage.MockScmNamespace()},
			nil,
		},
		{
			"Scan namespaces; JSON; returns error",
			"scm scan -j",
			genSetHelpers(scm.MockBackendConfig{
				GetModulesRes: storage.ScmModules{
					storage.MockScmModule(),
				},
				GetNamespacesErr: errors.New("bad prep"),
			}),
			nil,
			errors.New("bad prep"),
		},
	})
}

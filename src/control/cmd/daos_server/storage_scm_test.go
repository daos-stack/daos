//
// (C) Copyright 2022 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func TestDaosServer_setSockFromCfg(t *testing.T) {
	var (
		zero uint = 0
		one  uint = 1
	)

	mockAffinitySource := func(l logging.Logger, e *engine.Config) (uint, error) {
		l.Debugf("mock affinity source: assigning engine numa to its index %d", e.Index)
		return uint(e.Index), nil
	}

	for name, tc := range map[string]struct {
		cmdSockID *uint
		cfg       *config.Server
		affSrc    config.EngineAffinityFn
		expSockID *uint
		expErr    error
	}{
		"nil config": {},
		"sock specified in command": {
			cmdSockID: &one,
			expSockID: &one,
		},
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

			req := &storage.ScmPrepareRequest{
				SocketID: tc.cmdSockID,
			}

			affSrc := tc.affSrc
			if affSrc == nil {
				affSrc = mockAffinitySource
			}

			err := setSockFromCfg(log, tc.cfg, affSrc, req)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expSockID, req.SocketID); diff != "" {
				t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
			}
		})
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
		prepResp  *storage.ScmPrepareResponse
		prepErr   error
		expCalls  []storage.ScmPrepareRequest
		expErr    error
		expLogMsg string
	}{
		"no modules": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
			},
			expErr: storage.FaultScmNoModules,
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
				Socket: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
			},
			expErr: errors.New("failed to create regions"),
		},
		"create regions; reboot required": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
				RebootRequired: true,
			},
			expLogMsg: storage.ScmMsgRebootRequired,
		},
		"non-interleaved regions": {
			// If non-interleaved regions are detected, prep will return an
			// error. So returning the state is unexpected.
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
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
				Socket: storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
			},
			expErr: errors.New("failed to create namespaces"),
		},
		"create namespaces; no namespaces reported": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expErr: errors.New("failed to find namespaces"),
		},
		"create namespaces; namespaces reported": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
				Namespaces: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			expLogMsg: printNamespace.String(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbp := bdev.NewProvider(log, nil)
			smbc := &scm.MockBackendConfig{
				PrepRes: tc.prepResp,
				PrepErr: tc.prepErr,
			}
			msb := scm.NewMockBackend(smbc)
			msp := scm.NewProvider(log, msb, nil, nil)
			scs := server.NewMockStorageControlService(log, nil, nil, msp, mbp)

			cmd := prepareSCMCmd{
				LogCmd: cmdutil.LogCmd{
					Logger: log,
				},
				Force: !tc.noForce,
			}
			nrNs := uint(1)
			if tc.zeroNrNs {
				nrNs = 0
			}
			cmd.NrNamespacesPerSocket = nrNs

			err := cmd.preparePMem(scs.ScmPrepare)
			test.CmpErr(t, tc.expErr, err)

			if tc.expCalls == nil {
				tc.expCalls = []storage.ScmPrepareRequest{
					{NrNamespacesPerSocket: 1},
				}
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
				Socket: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
				RebootRequired: true,
			},
			expErr: errors.New("unexpected state if reboot"),
		},
		"remove regions; reboot required; not interleaved": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNotInterleaved,
				},
				RebootRequired: true,
			},
			expLogMsg: "regions will be removed",
		},
		"remove regions; reboot required; free capacity": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
				RebootRequired: true,
			},
			expLogMsg: "regions will be removed",
		},
		"remove regions; reboot required; no free capacity": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
				RebootRequired: true,
			},
			expLogMsg: "have been removed and regions will be removed",
		},
		"remove regions; reboot required; partial free capacity": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmPartFreeCap,
				},
				RebootRequired: true,
			},
			expLogMsg: "have been removed and regions will be removed",
		},
		"remove regions; reboot required; unhealthy": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNotHealthy,
				},
				RebootRequired: true,
			},
			expLogMsg: "have been removed and regions (some with an unexpected",
		},
		"remove regions; reboot required; unknown memory type": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmUnknownMode,
				},
				RebootRequired: true,
			},
			expLogMsg: "have been removed and regions (some with an unexpected",
		},
		"no modules": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
			},
			expErr: storage.FaultScmNoModules,
		},
		"no regions": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
			},
			expLogMsg: "reset successful",
		},
		"regions not interleaved": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNotInterleaved,
				},
			},
			expErr: errors.New("unexpected state"),
		},
		"free capacity": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
			},
			expErr: errors.New("unexpected state"),
		},
		"no free capacity": {
			prepResp: &storage.ScmPrepareResponse{
				Socket: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expErr: errors.New("unexpected state"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbp := bdev.NewProvider(log, nil)
			smbc := &scm.MockBackendConfig{
				PrepResetRes: tc.prepResp,
				PrepResetErr: tc.prepErr,
			}
			msb := scm.NewMockBackend(smbc)
			msp := scm.NewProvider(log, msb, nil, nil)
			scs := server.NewMockStorageControlService(log, nil, nil, msp, mbp)

			cmd := resetSCMCmd{
				LogCmd: cmdutil.LogCmd{
					Logger: log,
				},
				Force: !tc.noForce,
			}

			err := cmd.resetPMem(scs.ScmPrepare)
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

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

func mockAffinitySource(l logging.Logger, e *engine.Config) (uint, error) {
	l.Debugf("mock affinity source: assigning engine numa to its index %d", e.Index)
	return uint(e.Index), nil
}

func TestDaosServer_preparePMem(t *testing.T) {
	var zero uint = 0
	var one uint = 1
	var printNamespace strings.Builder
	msns := storage.ScmNamespaces{storage.MockScmNamespace()}
	if err := pretty.PrintScmNamespaces(msns, &printNamespace); err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		noForce   bool
		cmdSockID *uint
		zeroNrNs  bool
		cfg       *config.Server
		prepResp  *storage.ScmPrepareResponse
		prepErr   error
		expCalls  []*storage.ScmPrepareRequest
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
			expCalls: []*storage.ScmPrepareRequest{},
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
			expCalls: []*storage.ScmPrepareRequest{},
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
		"sock specified in command": {
			cmdSockID: &one,
			expCalls: []*storage.ScmPrepareRequest{
				{
					NrNamespacesPerSocket: 1,
					SocketID:              &one,
				},
			},
			// Only checking that request was made with provided socket so expect
			// failure.
			expErr: errors.New("failed to report state"),
		},
		"sock derived from config": {
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String())),
			),
			expCalls: []*storage.ScmPrepareRequest{
				{
					SocketID:              &zero,
					NrNamespacesPerSocket: 1,
				},
			},
			expErr: errors.New("failed to report state"),
		},
		"sock derived from config; numa node pinned": {
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().
					WithPinnedNumaNode(1).
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String())),
			),
			expCalls: []*storage.ScmPrepareRequest{
				{
					SocketID:              &one,
					NrNamespacesPerSocket: 1,
				},
			},
			expErr: errors.New("failed to report state"),
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
			expCalls: []*storage.ScmPrepareRequest{
				{
					NrNamespacesPerSocket: 1,
				},
			},
			expErr: errors.New("failed to report state"),
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
			expCalls: []*storage.ScmPrepareRequest{
				{
					SocketID:              &zero,
					NrNamespacesPerSocket: 1,
				},
			},
			expErr: errors.New("failed to report state"),
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
			expCalls: []*storage.ScmPrepareRequest{
				{
					SocketID:              &one,
					NrNamespacesPerSocket: 1,
				},
			},
			expErr: errors.New("failed to report state"),
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
			msp := scm.NewProvider(log, msb, nil)
			scs := server.NewMockStorageControlService(log, nil, nil, msp, mbp)

			if tc.cfg == nil {
				tc.cfg = &config.Server{}
			}

			cmd := prepareSCMCmd{
				LogCmd: cmdutil.LogCmd{
					Logger: log,
				},
				Force: !tc.noForce,
			}
			cmd.SocketID = tc.cmdSockID
			cmd.config = tc.cfg
			cmd.affinitySource = mockAffinitySource
			nrNs := uint(1)
			if tc.zeroNrNs {
				nrNs = 0
			}
			cmd.NrNamespacesPerSocket = nrNs

			err := cmd.preparePMem(scs.ScmPrepare)
			test.CmpErr(t, tc.expErr, err)

			if tc.expCalls == nil {
				tc.expCalls = []*storage.ScmPrepareRequest{
					{NrNamespacesPerSocket: 1},
				}
			}

			msb.RLock()
			if len(tc.expCalls) == 0 {
				if len(msb.PrepareCalls) != 0 {
					t.Fatalf("unexpected number of prepare calls, want 0 got %d",
						len(msb.PrepareCalls))
				}
			} else {
				if len(msb.PrepareCalls) != 1 {
					t.Fatalf("unexpected number of prepare calls, want 1 got %d",
						len(msb.PrepareCalls))
				}
				if diff := cmp.Diff(*(tc.expCalls[0]), msb.PrepareCalls[0]); diff != "" {
					t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
				}
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
		expCall   *storage.ScmPrepareRequest
	}{
		"remove regions; no consent": {
			noForce: true,
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
			msp := scm.NewProvider(log, msb, nil)
			scs := server.NewMockStorageControlService(log, nil, nil, msp, mbp)

			cmd := resetSCMCmd{
				LogCmd: cmdutil.LogCmd{
					Logger: log,
				},
				Force: !tc.noForce,
			}
			cmd.SocketID = tc.sockID

			err := cmd.resetPMem(scs.ScmPrepare)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if tc.expLogMsg != "" {
				if !strings.Contains(buf.String(), tc.expLogMsg) {
					t.Fatalf("expected to see %q in log, got %q", tc.expLogMsg, buf.String())
				}
			}

			if tc.expCall == nil {
				tc.expCall = &storage.ScmPrepareRequest{Reset: true}
			}

			msb.RLock()
			if tc.expCall == nil {
				if len(msb.ResetCalls) != 0 {
					t.Fatalf("unexpected number of reset calls, want 0 got %d",
						len(msb.ResetCalls))
				}
			} else {
				if len(msb.ResetCalls) != 1 {
					t.Fatalf("unexpected number of reset calls, want 1 got %d",
						len(msb.ResetCalls))
				}
				if diff := cmp.Diff(*tc.expCall, msb.ResetCalls[0]); diff != "" {
					t.Fatalf("unexpected reset calls (-want, +got):\n%s\n", diff)
				}
			}
			if len(msb.PrepareCalls) != 0 {
				t.Fatalf("unexpected number of prepare calls, want 0 got %d",
					len(msb.PrepareCalls))
			}
			msb.RUnlock()
		})
	}
}

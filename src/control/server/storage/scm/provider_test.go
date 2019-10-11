//
// (C) Copyright 2019 Intel Corporation.
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
package scm

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/build/src/control/src/github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/common"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
)

func MockModule(d *ipmctl.DeviceDiscovery) Module {
	if d == nil {
		md := MockDiscovery()
		d = &md
	}

	return Module{
		PhysicalID:      uint32(d.Physical_id),
		ChannelID:       uint32(d.Channel_id),
		ChannelPosition: uint32(d.Channel_pos),
		ControllerID:    uint32(d.Memory_controller_id),
		SocketID:        uint32(d.Socket_id),
		Capacity:        d.Capacity,
	}
}

func TestDiscoverScm(t *testing.T) {
	m := MockModule()
	n := MockPmemDevice()

	tests := map[string]struct {
		inModules         []scm.Module
		inNamespaces      []scm.Namespace
		ipmctlDiscoverRet error
		getNsRet          error
		expErr            error
		expResults        *scm.ScanResponse
	}{
		"no modules": {expResults: &scm.ScanResponse{}},
		"no namespaces": {
			inModules:  []scm.Module{m},
			expResults: &scm.ScanResponse{Modules: []scm.Module{m}},
		},
		"with namespacesdules": {
			inModules:    []scm.Module{m},
			inNamespaces: []scm.Namespace{n},
			expResults:   &scm.ScanResponse{Namespaces: []scm.Namespace{n}},
		},
		"module discovery error": {
			inNamespaces:      []scm.Namespace{n},
			ipmctlDiscoverRet: errors.New("ipmctl example failure"),
			expErr:            errors.New(msgIpmctlDiscoverFail + ": ipmctl example failure"),
		},
		"discover succeeds but get pmem fails": {
			inModules:  []scm.Module{m},
			getNsRet:   errors.New("ndctl example failure"),
			expErr:     errors.New(msgIpmctlDiscoverFail + ": ndctl example failure"),
			expResults: &scm.ScanResponse{Modules: []scm.Module{m}},
		},
	}

	for name, tt := range tests {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			prep := newMockPrepScm(tt.inNamespaces, tt.getNsRet)
			ss := newMockScmStorage(log, nil, tt.ipmctlDiscoverRet,
				tt.inModules, prep, nil)

			results, err := ss.Discover()
			if err != nil {
				if tt.expErr != nil {
					ExpectError(t, err, tt.expErr.Error(), "different error in discover")
					return
				}
				t.Fatal(err)
			}

			if diff := cmp.Diff(tt.expResults, results); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestProviderScan(t *testing.T) {
	defaultModule := MockModule(nil)
	defaultNamespace := Namespace{}

	for name, tc := range map[string]struct {
		rescan          bool
		getModulesErr   error
		getModulesRes   []Module
		getNamespaceErr error
		getNamespaceRes []Namespace
		getStateErr     error
		expResponse     *ScanResponse
	}{
		"ok": {
			expResponse: &ScanResponse{
				Modules:    []Module{defaultModule},
				Namespaces: []Namespace{defaultNamespace},
			},
		},
		"rescan": {
			rescan: true,
			expResponse: &ScanResponse{
				Modules:    []Module{defaultModule},
				Namespaces: []Namespace{defaultNamespace},
			},
		},
		"ndctl missing": {
			getNamespaceErr: FaultMissingNdctl,
			expResponse: &ScanResponse{
				Modules:    []Module{defaultModule},
				Namespaces: nil,
			},
		},
		"GetModules fails": {
			getModulesErr: FaultDiscoveryFailed,
		},
		"GetState fails": {
			getStateErr: errors.New("getstate failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)()

			if tc.getModulesRes == nil {
				tc.getModulesRes = []Module{defaultModule}
			}
			if tc.getNamespaceRes == nil {
				tc.getNamespaceRes = []Namespace{defaultNamespace}
			}
			mb := NewMockBackend(&MockBackendConfig{
				GetModulesRes:   tc.getModulesRes,
				GetModulesErr:   tc.getModulesErr,
				GetNamespaceRes: tc.getNamespaceRes,
				GetNamespaceErr: tc.getNamespaceErr,
				GetStateErr:     tc.getStateErr,
			})
			p := NewProvider(log, mb, NewMockSysProvider(nil))
			cmpRes := func(t *testing.T, want, got *ScanResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			res, err := p.Scan(ScanRequest{})
			if err != nil {
				switch err {
				case FaultMissingNdctl:
					cmpRes(t, tc.expResponse, res)
					return
				case tc.getModulesErr, tc.getStateErr:
					return
				default:
					t.Fatal(err)
				}
			}
			cmpRes(t, tc.expResponse, res)

			// TODO: Try to simulate finding something new?
			// For now, just make sure nothing breaks.
			res, err = p.Scan(ScanRequest{Rescan: tc.rescan})
			if err != nil {
				t.Fatal(err)
			}
			cmpRes(t, tc.expResponse, res)
		})
	}
}

func TestProviderPrepare(t *testing.T) {
	defaultNamespace := Namespace{}

	for name, tc := range map[string]struct {
		startInitialized bool
		reset            bool
		shouldReboot     bool
		getModulesErr    error
		getNamespaceErr  error
		getNamespaceRes  []Namespace
		getStateErr      error
		prepErr          error
		startState       types.ScmState
		expEndState      types.ScmState
		expResponse      *PrepareResponse
	}{
		"init scan fails": {
			getModulesErr: FaultDiscoveryFailed,
		},
		"noop": {
			expResponse: &PrepareResponse{
				RebootRequired: false,
			},
		},
		"should reboot after prep": {
			shouldReboot: true,
			startState:   types.ScmStateNoRegions,
			expEndState:  types.ScmStateFreeCapacity,
			expResponse: &PrepareResponse{
				State:          types.ScmStateFreeCapacity,
				RebootRequired: true,
			},
		},
		"should reboot after reset": {
			reset:        true,
			shouldReboot: true,
			startState:   types.ScmStateNoCapacity,
			expEndState:  types.ScmStateNoRegions,
			expResponse: &PrepareResponse{
				State:          types.ScmStateNoRegions,
				RebootRequired: true,
			},
		},
		"prep with ndctl missing": {
			getNamespaceErr: FaultMissingNdctl,
		},
		"prep succeeds, update fails": {
			startInitialized: true,
			startState:       types.ScmStateNoCapacity,
			expEndState:      types.ScmStateNoRegions,
			getStateErr:      errors.New("update failed"),
		},
		"prep fails": {
			startState:  types.ScmStateNoCapacity,
			expEndState: types.ScmStateNoRegions,
			prepErr:     errors.New("prep failed"),
		},
		"reset with ndctl missing": {
			reset:           true,
			getNamespaceErr: FaultMissingNdctl,
		},
		"reset succeeds, update fails": {
			reset:            true,
			startInitialized: true,
			startState:       types.ScmStateNoCapacity,
			expEndState:      types.ScmStateNoRegions,
			getStateErr:      errors.New("update failed"),
		},
		"reset fails": {
			reset:       true,
			startState:  types.ScmStateNoCapacity,
			expEndState: types.ScmStateNoRegions,
			prepErr:     errors.New("prep reset failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)()

			if tc.getNamespaceRes == nil {
				tc.getNamespaceRes = []Namespace{defaultNamespace}
			}
			mb := NewMockBackend(&MockBackendConfig{
				GetModulesErr:   tc.getModulesErr,
				GetModulesRes:   []Module{MockModule(nil)},
				GetNamespaceRes: tc.getNamespaceRes,
				GetNamespaceErr: tc.getNamespaceErr,
				GetStateErr:     tc.getStateErr,
				StartingState:   tc.startState,
				NextState:       tc.expEndState,
				PrepNeedsReboot: tc.shouldReboot,
				PrepErr:         tc.prepErr,
			})
			p := NewProvider(log, mb, NewMockSysProvider(nil))
			p.scanCompleted = tc.startInitialized
			cmpRes := func(t *testing.T, want, got *PrepareResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			res, err := p.Prepare(PrepareRequest{Reset: tc.reset})
			if err != nil {
				switch err {
				case FaultMissingNdctl:
					cmpRes(t, tc.expResponse, res)
					return
				case tc.getModulesErr, tc.getStateErr, tc.prepErr:
					return
				default:
					t.Fatal(err)
				}
			}
			cmpRes(t, tc.expResponse, res)
		})
	}
}

func TestProviderGetState(t *testing.T) {
	for name, tc := range map[string]struct {
		startInitialized bool
		getModulesErr    error
		getNamespaceErr  error
		startState       types.ScmState
		expEndState      types.ScmState
	}{
		"init scan fails": {
			getModulesErr: FaultDiscoveryFailed,
		},
		"ndctl missing": {
			getNamespaceErr: FaultMissingNdctl,
		},
		"ok": {
			startState:  types.ScmStateNoCapacity,
			expEndState: types.ScmStateNoCapacity,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)()

			mb := NewMockBackend(&MockBackendConfig{
				GetModulesErr:   tc.getModulesErr,
				GetModulesRes:   []Module{MockModule(nil)},
				GetNamespaceErr: tc.getNamespaceErr,
				StartingState:   tc.startState,
				NextState:       tc.expEndState,
			})
			p := NewProvider(log, mb, NewMockSysProvider(nil))
			p.scanCompleted = tc.startInitialized
			cmpRes := func(t *testing.T, want, got types.ScmState) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			res, err := p.GetState()
			if err != nil {
				switch err {
				case tc.getModulesErr, tc.getNamespaceErr:
					return
				default:
					t.Fatal(err)
				}
			}
			cmpRes(t, tc.expEndState, res)
		})
	}
}

func TestProviderCheckFormat(t *testing.T) {
	const (
		goodMountPoint = "/mnt/daos"
		goodDevice     = "/dev/pmem0"
	)

	for name, tc := range map[string]struct {
		mountPoint      string
		getModulesErr   error
		getNamespaceErr error
		alreadyMounted  bool
		isMountedErr    error
		getFsStr        string
		getFsErr        error
		request         *FormatRequest
		expResponse     *FormatResponse
		expErr          error
	}{
		"init scan fails": {
			getModulesErr: FaultDiscoveryFailed,
		},
		"missing ndctl": {
			getNamespaceErr: FaultMissingNdctl,
		},
		"missing mount point": {
			mountPoint: "",
			expErr:     FaultFormatMissingMountpoint,
		},
		"conflicting config": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			expErr: FaultFormatConflictingParam,
		},
		"missing dcpm device": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm:       &DcpmParams{},
			},
			expErr: FaultFormatInvalidDeviceCount,
		},
		"missing source config": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
			},
			expErr: FaultFormatMissingParam,
		},
		"isMounted fails": {
			mountPoint:   goodMountPoint,
			isMountedErr: errors.New("is mounted check failed"),
		},
		"mountpoint doesn't exist": {
			mountPoint:   goodMountPoint,
			isMountedErr: os.ErrNotExist,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  false,
			},
		},
		"already mounted": {
			mountPoint:     goodMountPoint,
			alreadyMounted: true,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"getFs fails": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsErr: errors.New("getfs failed"),
		},
		"already formatted": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"not formatted": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  false,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)()

			mb := NewMockBackend(&MockBackendConfig{
				GetModulesErr:   tc.getModulesErr,
				GetModulesRes:   []Module{MockModule(nil)},
				GetNamespaceErr: tc.getNamespaceErr,
			})
			msp := NewMockSysProvider(&MockSysConfig{
				IsMountedBool: tc.alreadyMounted,
				IsMountedErr:  tc.isMountedErr,
				GetfsStr:      tc.getFsStr,
				GetfsErr:      tc.getFsErr,
			})
			p := NewProvider(log, mb, msp)
			cmpRes := func(t *testing.T, want, got *FormatResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			req := tc.request
			if req == nil {
				req = &FormatRequest{
					Mountpoint: tc.mountPoint,
					Ramdisk: &RamdiskParams{
						Size: 1,
					},
				}
			}
			res, err := p.CheckFormat(*req)
			if err != nil {
				switch errors.Cause(err) {
				case tc.getModulesErr, tc.getNamespaceErr, tc.isMountedErr, tc.getFsErr,
					tc.expErr:
					return
				default:
					t.Fatal(err)
				}
			}
			cmpRes(t, tc.expResponse, res)
		})
	}
}

func TestProviderFormat(t *testing.T) {
	const (
		goodMountPoint = "/mnt/daos"
		badMountPoint  = "/this/should/not/work"
		goodDevice     = "/dev/pmem0"
	)

	// NB: Some overlap here between this and CheckFormat tests,
	// but tests are cheap and this encourages good coverage.
	for name, tc := range map[string]struct {
		mountPoint      string
		getModulesErr   error
		getNamespaceErr error
		alreadyMounted  bool
		isMountedErr    error
		getFsStr        string
		getFsErr        error
		mountErr        error
		unmountErr      error
		mkfsErr         error
		request         *FormatRequest
		expResponse     *FormatResponse
		expErr          error
	}{
		"init scan fails": {
			getModulesErr: FaultDiscoveryFailed,
		},
		"missing ndctl": {
			getNamespaceErr: FaultMissingNdctl,
		},
		"missing mount point": {
			mountPoint: "",
			expErr:     FaultFormatMissingMountpoint,
		},
		"conflicting config": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			expErr: FaultFormatConflictingParam,
		},
		"missing dcpm device": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm:       &DcpmParams{},
			},
			expErr: FaultFormatInvalidDeviceCount,
		},
		"missing source config": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
			},
			expErr: FaultFormatMissingParam,
		},
		"isMounted fails": {
			mountPoint:   goodMountPoint,
			isMountedErr: errors.New("is mounted check failed"),
		},
		"ramdisk: mountpoint doesn't exist": {
			mountPoint:   goodMountPoint,
			isMountedErr: os.ErrNotExist,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"ramdisk: already mounted, no reformat": {
			mountPoint:     goodMountPoint,
			alreadyMounted: true,
			expErr:         FaultFormatNoReformat,
		},
		"ramdisk: not mounted": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
			},
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"ramdisk: not mounted; mkdir fails": {
			request: &FormatRequest{
				Mountpoint: badMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
			},
			expErr: fmt.Errorf("mkdir /%s: permission denied",
				strings.Split(badMountPoint, "/")[1]),
		},
		"ramdisk: already mounted; reformat": {
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
			},
			alreadyMounted: true,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"ramdisk: already mounted; reformat; unmount fails": {
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
			},
			alreadyMounted: true,
			unmountErr:     errors.New("unmount failed"),
		},
		"ramdisk: already mounted; reformat; mount fails": {
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Ramdisk: &RamdiskParams{
					Size: 1,
				},
			},
			alreadyMounted: true,
			mountErr:       errors.New("mount failed"),
		},
		"dcpm: getFs fails": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsErr: errors.New("getfs failed"),
		},
		"dcpm: mountpoint doesn't exist; already formatted; no reformat": {
			isMountedErr: os.ErrNotExist,
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expErr:   FaultFormatNoReformat,
		},
		"dcpm: not mounted; already formatted; no reformat": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expErr:   FaultFormatNoReformat,
		},
		"dcpm: mountpoint doesn't exist; already formatted; reformat": {
			isMountedErr: os.ErrNotExist,
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"dcpm: not mounted; already formatted; reformat": {
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"dcpm: mounted; already formatted; reformat": {
			request: &FormatRequest{
				Reformat:   true,
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: "reiserfs",
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"dcpm: mountpoint doesn't exist; not formatted": {
			isMountedErr: os.ErrNotExist,
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"dcpm: not mounted; not formatted": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			expResponse: &FormatResponse{
				Mountpoint: goodMountPoint,
				Formatted:  true,
			},
		},
		"dcpm: not mounted; not formatted; mkfs fails": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			mkfsErr:  errors.New("mkfs failed"),
		},
		"dcpm: not mounted; not formatted; mount fails": {
			request: &FormatRequest{
				Mountpoint: goodMountPoint,
				Dcpm: &DcpmParams{
					Device: goodDevice,
				},
			},
			getFsStr: fsTypeNone,
			mountErr: errors.New("mount failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)()

			mb := NewMockBackend(&MockBackendConfig{
				GetModulesErr:   tc.getModulesErr,
				GetModulesRes:   []Module{MockModule(nil)},
				GetNamespaceErr: tc.getNamespaceErr,
			})
			msp := NewMockSysProvider(&MockSysConfig{
				IsMountedBool: tc.alreadyMounted,
				IsMountedErr:  tc.isMountedErr,
				GetfsStr:      tc.getFsStr,
				GetfsErr:      tc.getFsErr,
				MkfsErr:       tc.mkfsErr,
				MountErr:      tc.mountErr,
				UnmountErr:    tc.unmountErr,
			})
			p := NewProvider(log, mb, msp)
			cmpRes := func(t *testing.T, want, got *FormatResponse) {
				t.Helper()
				if diff := cmp.Diff(want, got); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
			if err != nil {
				t.Fatal(err)
			}
			defer os.RemoveAll(testDir)

			req := tc.request
			if req == nil {
				req = &FormatRequest{
					Mountpoint: tc.mountPoint,
					Ramdisk: &RamdiskParams{
						Size: 1,
					},
				}
			}
			if req.Mountpoint != "" && req.Mountpoint != badMountPoint {
				req.Mountpoint = filepath.Join(testDir, req.Mountpoint)
			}
			if tc.expResponse != nil {
				tc.expResponse.Mountpoint = filepath.Join(testDir, tc.expResponse.Mountpoint)
			}

			res, err := p.Format(*req)
			if err != nil {
				switch errors.Cause(err) {
				case tc.getModulesErr, tc.getNamespaceErr, tc.getFsErr,
					tc.mkfsErr, tc.mountErr, tc.unmountErr, tc.expErr:
					return
				case tc.isMountedErr:
					if tc.isMountedErr != os.ErrNotExist {
						return
					}
					t.Fatalf("%s leaked from IsMounted() check", err)
				default:
					if tc.expErr != nil && tc.expErr.Error() == errors.Cause(err).Error() {
						return
					}
					t.Fatal(err)
				}
			}
			cmpRes(t, tc.expResponse, res)
		})
	}
}

func TestParseFsType(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expFsType string
		expError  error
	}{
		"not formatted": {
			input:     "/dev/pmem1: data\n",
			expFsType: fsTypeNone,
		},
		"formatted": {
			input:     "/dev/pmem0: Linux rev 1.0 ext4 filesystem data, UUID=09619a0d-0c9e-46b4-add5-faf575dd293d\n",
			expFsType: fsTypeExt4,
		},
		"empty input": {
			expError: errors.New("unable to determine fs type from \"\""),
		},
		"mangled short": {
			input:    "/dev/pmem0",
			expError: errors.New("unable to determine fs type from \"/dev/pmem0\""),
		},
		"mangled medium": {
			input:    "/dev/pmem0: Linux",
			expError: errors.New("unable to determine fs type from \"/dev/pmem0: Linux\""),
		},
		"mangled long": {
			input:    "/dev/pmem0: Linux quack bark",
			expError: errors.New("unable to determine fs type from \"/dev/pmem0: Linux quack bark\""),
		},
	} {
		t.Run(name, func(t *testing.T) {
			fsType, err := parseFsType(tc.input)

			if err != tc.expError {
				if err != nil && tc.expError != nil {
					if err.Error() == tc.expError.Error() {
						return
					}
				}
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expFsType, fsType); diff != "" {
				t.Fatalf("unexpected fsType (-want, +got):\n%s\n", diff)
			}
		})
	}
}

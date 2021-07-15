//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

func getTestEngineInstance(log logging.Logger) *EngineInstance {
	cfg := engine.NewConfig().WithStorage(
		storage.NewTierConfig().
			WithScmClass("ram").
			WithScmMountPoint("/foo/bar"),
	)
	runner := engine.NewRunner(log, cfg)
	storage := storage.MockProvider(log, 0, &cfg.Storage, nil, nil, nil)
	return NewEngineInstance(log, storage, nil, runner)
}

func getTestBioErrorReq(t *testing.T, sockPath string, idx uint32, tgt int32, unmap bool, read bool, write bool) *srvpb.BioErrorReq {
	return &srvpb.BioErrorReq{
		DrpcListenerSock: sockPath,
		InstanceIdx:      idx,
		TgtId:            tgt,
		UnmapErr:         unmap,
		ReadErr:          read,
		WriteErr:         write,
	}
}

func TestServer_Instance_BioError(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	instance := getTestEngineInstance(log)

	req := getTestBioErrorReq(t, "/tmp/instance_test.sock", 0, 0, false, false, true)

	instance.BioErrorNotify(req)

	expectedOut := "detected blob I/O error"
	if !strings.Contains(buf.String(), expectedOut) {
		t.Fatal("No I/O error notification detected")
	}
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
	common.AssertEqual(t, updatedInstance, instance, "not the same structure")
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
			defer common.ShowBufferOnFailure(t, buf)

			testDir, cleanupDir := common.CreateTestDir(t)
			defer cleanupDir()

			inst := getTestEngineInstance(log).WithHostFaultDomain(tc.newDomain)
			inst.fsRoot = testDir
			inst._superblock = tc.superblock

			sbPath := inst.superblockPath()
			if err := os.MkdirAll(filepath.Dir(sbPath), 0755); err != nil {
				t.Fatalf("failed to make test superblock dir: %s", err.Error())
			}

			err := inst.updateFaultDomainInSuperblock()

			common.CmpErr(t, tc.expErr, err)

			// Ensure the newer value in the instance was written to the superblock
			newSB, err := ReadSuperblock(sbPath)
			if tc.expWritten {
				if err != nil {
					t.Fatalf("can't read expected superblock: %s", err.Error())
				}

				if newSB == nil {
					t.Fatalf("expected non-nil superblock")
				}

				expDomainStr := ""
				if tc.newDomain != nil {
					expDomainStr = tc.newDomain.String()
				}
				common.AssertEqual(t, expDomainStr, newSB.HostFaultDomain, "")
			} else if err == nil {
				t.Fatal("expected no superblock written")
			} else {
				common.CmpErr(t, syscall.ENOENT, err)
			}
		})
	}
}

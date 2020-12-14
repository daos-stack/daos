//
// (C) Copyright 2019-2020 Intel Corporation.
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

package server

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

func getTestIOServerInstance(logger logging.Logger) *IOServerInstance {
	runner := ioserver.NewRunner(logger, &ioserver.Config{})
	return NewIOServerInstance(logger, nil, nil, nil, runner)
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

	instance := getTestIOServerInstance(log)

	req := getTestBioErrorReq(t, "/tmp/instance_test.sock", 0, 0, false, false, true)

	instance.BioErrorNotify(req)

	expectedOut := "detected blob I/O error"
	if !strings.Contains(buf.String(), expectedOut) {
		t.Fatal("No I/O error notification detected")
	}
}

func TestServer_Instance_WithHostFaultDomain(t *testing.T) {
	instance := &IOServerInstance{}
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

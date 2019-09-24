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

package server

import (
	"testing"
	"time"

	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

func getTestIOServerInstance() *IOServerInstance {
	logger := logging.NewStdoutLogger("")
	runner := ioserver.NewRunner(logger, &ioserver.Config{})
	return NewIOServerInstance(nil, logger,
		nil, nil, runner)
}

func getTestNotifyReadyReq(t *testing.T, sockPath string, idx uint32) *srvpb.NotifyReadyReq {
	return &srvpb.NotifyReadyReq{
		DrpcListenerSock: sockPath,
		InstanceIdx:      idx,
	}
}

func isIosrvReady(instance *IOServerInstance) bool {
	select {
	case <-instance.AwaitReady():
		return true
	default:
		return false
	}
}

func waitForIosrvReady(t *testing.T, instance *IOServerInstance) {
	elapsed := time.Duration(0)
	timeout := 100 * time.Millisecond
	interval := 5 * time.Millisecond
	for elapsed < timeout {
		if isIosrvReady(instance) {
			return
		}

		time.Sleep(interval)
		elapsed += interval
	}

	t.Fatal("IO server never became ready!")
}

func TestIOServerInstance_NotifyReady(t *testing.T) {
	instance := getTestIOServerInstance()

	req := getTestNotifyReadyReq(t, "/tmp/instance_test.sock", 0)

	instance.NotifyReady(req)

	if instance.drpcClient == nil {
		t.Fatal("Expected a dRPC client connection")
	}

	waitForIosrvReady(t, instance)
}

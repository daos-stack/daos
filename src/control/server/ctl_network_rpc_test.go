//
// (C) Copyright 2020 Intel Corporation.
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

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

var (
	ib1 = &ctlpb.FabricInterface{
		Provider:    "ofi+psm2",
		Device:      "ib1",
		Numanode:    1,
		Netdevclass: 32,
		Priority:    1,
	}
	ib1Native = netdetect.FabricScan{
		Provider:    "ofi+psm2",
		DeviceName:  "ib1",
		NUMANode:    1,
		NetDevClass: 32,
		Priority:    1,
	}
	eth1 = &ctlpb.FabricInterface{
		Provider:    "ofi+sockets",
		Device:      "eth1",
		Numanode:    1,
		Netdevclass: 32,
		Priority:    2,
	}
	eth1Native = netdetect.FabricScan{
		Provider:    "ofi+psm",
		DeviceName:  "eth1",
		NUMANode:    1,
		NetDevClass: 1,
		Priority:    1,
	}
)

func TestServer_ConvertFabricInterface(t *testing.T) {
	native := new(netdetect.FabricScan)
	if err := convert.Types(ib1, native); err != nil {
		t.Fatal(err)
	}
	expNative := &ib1Native

	if diff := cmp.Diff(expNative, native); diff != "" {
		t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
	}
}

func TestServer_ConvertFabricInterfaces(t *testing.T) {
	pbs := []*ctlpb.FabricInterface{ib1, eth1}
	natives := new([]*netdetect.FabricScan)
	if err := convert.Types(pbs, natives); err != nil {
		t.Fatal(err)
	}

	scanResp := new(ctlpb.NetworkScanResp)
	scanResp.Interfaces = make([]*ctlpb.FabricInterface, len(pbs))
	if err := convert.Types(natives, &scanResp.Interfaces); err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(pbs, scanResp.Interfaces); diff != "" {
		t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
	}
}

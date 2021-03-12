//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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

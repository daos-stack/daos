//
// (C) Copyright 2018-2019 Intel Corporation.
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

package spdk

import (
	"fmt"
	"testing"
)

func checkFailure(shouldSucceed bool, err error) (rErr error) {
	switch {
	case err != nil && shouldSucceed:
		rErr = fmt.Errorf("expected test to succeed, failed unexpectedly: %v", err)
	case err == nil && !shouldSucceed:
		rErr = fmt.Errorf("expected test to fail, succeeded unexpectedly")
	}

	return
}

func TestDiscover(t *testing.T) {
	//	var se Env
	//	var n Nvme

	tests := []struct {
		shmID         int
		shouldSucceed bool
	}{
		{
			shmID:         0,
			shouldSucceed: true,
		},
		//		{
		//			shmID:         1,
		//			shouldSucceed: true,
		//		},
	}

	for _, _ = range tests {
		fmt.Println("spdk binding tests currently disabled")

		// TODO
		//		if err := se.InitSPDKEnv(tt.shmID); err != nil {
		//			t.Fatal(err.Error())
		//		}
		//
		//		cs, nss, err := n.Discover()
		//		if checkFailure(tt.shouldSucceed, err) != nil {
		//			t.Errorf("case %d: %v", i, err)
		//		}
		//		fmt.Printf("controllers: %#v\n", cs)
		//		fmt.Printf("namespaces: %#v\n", nss)

		//		_, _, err = n.Update(0, "", 0)
		//		if checkFailure(tt.shouldSucceed, err) != nil {
		//			t.Errorf("case %d: %v", i, err)
		//		}

		//		n.Cleanup()
	}
}

// Verify correct mapping of namespaces and health stats to controllers in
// processReturn method.
//func TestProcessReturn(t *testing.T) {
//	mCs := []Controller{
//		{PCIAddr: "1.2.3.4.5", FWRev: "1.0.0"},
//		{PCIAddr: "1.2.3.4.6", FWRev: "1.0.0"},
//	}
//	ns0 := Namespace{ID: 0, Size: 100}
//	ns1 := Namespace{ID: 1, Size: 200}
//	ns2 := Namespace{ID: 2, Size: 100}
//	ns3 := Namespace{ID: 3, Size: 200}
//	dh0 := DeviceHealth{Temp: 300, PowerOnHours: 1000, UnsafeShutdowns: 1}
//	dh1 := DeviceHealth{Temp: 500, PowerOnHours: 500, UnsafeShutdowns: 2}
//	dh2 := DeviceHealth{Temp: 300, PowerOnHours: 1000, UnsafeShutdowns: 1}
//	dh3 := DeviceHealth{Temp: 500, PowerOnHours: 500, UnsafeShutdowns: 2}
//
//	for name, tc := range map[string]struct {
//		//retPtr    *C.struct_ret_t
//		expCtrlrs []Controllers
//	}{
//		"no namespaces or health statistics": {
//			[]Namespace{},
//			[]DeviceHealth{},
//			NvmeControllers{
//				&NvmeController{Pciaddr: mCs[0].PCIAddr, Fwrev: mCs[0].FWRev},
//				&NvmeController{Pciaddr: mCs[1].PCIAddr, Fwrev: mCs[1].FWRev},
//			},
//		},
//		"matching namespace and health statistics": {
//			[]Namespace{ns0, ns1},
//			[]DeviceHealth{dh0, dh1},
//			NvmeControllers{
//				&NvmeController{
//					Pciaddr: mCs[0].PCIAddr, Fwrev: mCs[0].FWRev,
//					Namespaces: NvmeNamespaces{{Id: ns0.ID, Capacity: ns0.Size}},
//					Healthstats: &NvmeController_Health{
//						Temp: dh0.Temp, Poweronhours: dh0.PowerOnHours,
//						Unsafeshutdowns: dh0.UnsafeShutdowns,
//					},
//				},
//				&NvmeController{
//					Pciaddr: mCs[1].PCIAddr, Fwrev: mCs[1].FWRev,
//					Namespaces: NvmeNamespaces{{Id: ns1.ID, Capacity: ns1.Size}},
//					Healthstats: &NvmeController_Health{
//						Temp: dh1.Temp, Poweronhours: dh1.PowerOnHours,
//						Unsafeshutdowns: dh1.UnsafeShutdowns,
//					},
//				},
//			},
//		},
//		"multiple namespaces and no matching health statistics": {
//			[]Namespace{ns0, ns1, ns2, ns3},
//			[]DeviceHealth{dh2, dh3},
//			NvmeControllers{
//				&NvmeController{
//					Pciaddr: mCs[0].PCIAddr, Fwrev: mCs[0].FWRev,
//					Namespaces: NvmeNamespaces{
//						{Id: ns0.ID, Capacity: ns0.Size},
//						{Id: ns2.ID, Capacity: ns2.Size},
//					},
//				},
//				&NvmeController{
//					Pciaddr: mCs[1].PCIAddr, Fwrev: mCs[1].FWRev,
//					Namespaces: NvmeNamespaces{
//						{Id: ns1.ID, Capacity: ns1.Size},
//						{Id: ns3.ID, Capacity: ns3.Size},
//					},
//				},
//			},
//		},
//	} {
//		t.Run(name, func(t *testing.T) {
//			log, buf := logging.NewTestLogger(t.Name())
//			defer common.ShowBufferOnFailure(t, buf)
//
//			sn := newMockNvmeStorage(log, &mockExt{}, defaultMockSpdkEnv(),
//				newMockSpdkNvme(log, mCs, tc.nss, tc.dh, nil, nil),
//				false)
//
//			if err := sn.Discover(); err != nil {
//				t.Fatal(err)
//			}
//
//			if diff := cmp.Diff(tc.expPbCtrlrs, sn.controllers); diff != "" {
//				t.Fatalf("unexpected controller results (-want, +got):\n%s\n", diff)
//			}
//		})
//	}
//}

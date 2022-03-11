//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"context"
	"errors"
	"fmt"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type mockHostStorage struct {
	hostAddr string
	storage  *control.HostStorage
}

func mockHostStorageMap(t *testing.T, hosts ...*mockHostStorage) control.HostStorageMap {
	hsm := make(control.HostStorageMap)

	for _, mhs := range hosts {
		if err := hsm.Add(mhs.hostAddr, mhs.storage); err != nil {
			t.Fatal(err)
		}
	}

	return hsm
}

func TestControl_PrintStorageScanResponse(t *testing.T) {
	var (
		standard   = control.MockServerScanResp(t, "standard")
		pmemSingle = control.MockServerScanResp(t, "pmemSingle")
		noNvme     = control.MockServerScanResp(t, "noNvme")
		noScm      = control.MockServerScanResp(t, "noScm")
		noStorage  = control.MockServerScanResp(t, "noStorage")
		scmFailed  = control.MockServerScanResp(t, "scmFailed")
		nvmeFailed = control.MockServerScanResp(t, "nvmeFailed")
		bothFailed = control.MockServerScanResp(t, "bothFailed")
		nvmeA      = control.MockServerScanResp(t, "nvmeA")
		nvmeB      = control.MockServerScanResp(t, "nvmeB")
		nvmeBasicA = control.MockServerScanResp(t, "nvmeBasicA")
		nvmeBasicB = control.MockServerScanResp(t, "nvmeBasicB")
		pmemA      = control.MockServerScanResp(t, "pmemA")
		pmemB      = control.MockServerScanResp(t, "pmemB")
	)

	for name, tc := range map[string]struct {
		mic         *control.MockInvokerConfig
		expPrintStr string
	}{
		"empty response": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{},
			},
		},
		"server error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:  "host1",
							Error: errors.New("failed"),
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts Error  
  ----- -----  
  host1 failed 

`,
		},
		"scm scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: scmFailed,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts Error           
  ----- -----           
  host1 scm scan failed 

Hosts SCM Total       NVMe Total            
----- ---------       ----------            
host1 0 B (0 modules) 2.0 TB (1 controller) 
`,
		},
		"nvme scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: nvmeFailed,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts Error            
  ----- -----            
  host1 nvme scan failed 

Hosts SCM Total          NVMe Total          
----- ---------          ----------          
host1 954 MiB (1 module) 0 B (0 controllers) 
`,
		},
		"scm and nvme scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1:1",
							Message: bothFailed,
						},
						{
							Addr:    "host2:1",
							Message: bothFailed,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts     Error            
  -----     -----            
  host[1-2] nvme scan failed 
  host[1-2] scm scan failed  

Hosts     SCM Total       NVMe Total          
-----     ---------       ----------          
host[1-2] 0 B (0 modules) 0 B (0 controllers) 
`,
		},
		"no storage": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: noStorage,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM Total       NVMe Total          
----- ---------       ----------          
host1 0 B (0 modules) 0 B (0 controllers) 
`,
		},
		"single host": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: standard,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM Total          NVMe Total            
----- ---------          ----------            
host1 954 MiB (1 module) 2.0 TB (1 controller) 
`,
		},
		"single host with namespace": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: pmemSingle,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM Total            NVMe Total            
----- ---------            ----------            
host1 1.0 TB (1 namespace) 2.0 TB (1 controller) 
`,
		},
		"two hosts same scan": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: standard,
						},
						{
							Addr:    "host2",
							Message: standard,
						},
					},
				},
			},
			expPrintStr: `
Hosts     SCM Total          NVMe Total            
-----     ---------          ----------            
host[1-2] 954 MiB (1 module) 2.0 TB (1 controller) 
`,
		},
		"two hosts different scans": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: noNvme,
						},
						{
							Addr:    "host2",
							Message: noScm,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM Total          NVMe Total            
----- ---------          ----------            
host1 954 MiB (1 module) 0 B (0 controllers)   
host2 0 B (0 modules)    2.0 TB (1 controller) 
`,
		},
		"multiple hosts same scan": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: control.MockHostResponses(t,
						1024, "host%000d", nvmeA),
				},
			},
			expPrintStr: `
Hosts        SCM Total             NVMe Total             
-----        ---------             ----------             
host[0-1023] 3.0 TB (2 namespaces) 8.0 TB (4 controllers) 
`,
		},
		"multiple hosts differing ssd pci addresses": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: nvmeA,
						},
						{
							Addr:    "host2",
							Message: nvmeB,
						},
						{
							Addr:    "host3",
							Message: nvmeA,
						},
						{
							Addr:    "host4",
							Message: nvmeB,
						},
					},
				},
			},
			expPrintStr: `
Hosts     SCM Total             NVMe Total             
-----     ---------             ----------             
host[1,3] 3.0 TB (2 namespaces) 8.0 TB (4 controllers) 
host[2,4] 3.0 TB (2 namespaces) 8.0 TB (4 controllers) 
`,
		},
		"multiple hosts differing ssd serial model and fw": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: nvmeA,
						},
						{
							Addr:    "host2",
							Message: nvmeBasicA,
						},
						{
							Addr:    "host3",
							Message: nvmeA,
						},
						{
							Addr:    "host4",
							Message: nvmeBasicA,
						},
					},
				},
			},
			expPrintStr: `
Hosts     SCM Total             NVMe Total             
-----     ---------             ----------             
host[1,3] 3.0 TB (2 namespaces) 8.0 TB (4 controllers) 
host[2,4] 3.0 TB (2 namespaces) 4.0 TB (2 controllers) 
`,
		},
		"multiple hosts differing ssd capacity": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: nvmeBasicA,
						},
						{
							Addr:    "host2",
							Message: nvmeBasicB,
						},
						{
							Addr:    "host3",
							Message: nvmeBasicA,
						},
						{
							Addr:    "host4",
							Message: nvmeBasicB,
						},
					},
				},
			},
			expPrintStr: `
Hosts     SCM Total             NVMe Total             
-----     ---------             ----------             
host[1,3] 3.0 TB (2 namespaces) 4.0 TB (2 controllers) 
host[2,4] 3.0 TB (2 namespaces) 4.2 TB (2 controllers) 
`,
		},
		"multiple hosts differing pmem capacity": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: pmemA,
						},
						{
							Addr:    "host2",
							Message: pmemB,
						},
						{
							Addr:    "host3",
							Message: pmemA,
						},
						{
							Addr:    "host4",
							Message: pmemB,
						},
					},
				},
			},
			expPrintStr: `
Hosts     SCM Total             NVMe Total            
-----     ---------             ----------            
host[1,3] 3.0 TB (2 namespaces) 2.0 TB (1 controller) 
host[2,4] 3.2 TB (2 namespaces) 2.0 TB (1 controller) 
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)

			resp, err := control.StorageScan(context.TODO(), mi, &control.StorageScanReq{})
			if err != nil {
				t.Fatal(err)
			}

			var bld strings.Builder
			if err := PrintResponseErrors(resp, &bld); err != nil {
				t.Fatal(err)
			}
			if err := PrintHostStorageMap(resp.HostStorage, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PrintStorageScanResponseVerbose(t *testing.T) {
	var (
		standard   = control.MockServerScanResp(t, "standard")
		pmemSingle = control.MockServerScanResp(t, "pmemSingle")
		noNvme     = control.MockServerScanResp(t, "noNvme")
		noScm      = control.MockServerScanResp(t, "noScm")
		noStorage  = control.MockServerScanResp(t, "noStorage")
		scmFailed  = control.MockServerScanResp(t, "scmFailed")
		nvmeFailed = control.MockServerScanResp(t, "nvmeFailed")
		bothFailed = control.MockServerScanResp(t, "bothFailed")
		nvmeBasicA = control.MockServerScanResp(t, "nvmeBasicA")
		nvmeBasicB = control.MockServerScanResp(t, "nvmeBasicB")
	)

	for name, tc := range map[string]struct {
		mic         *control.MockInvokerConfig
		expPrintStr string
	}{
		"empty response": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{},
			},
		},
		"server error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:  "host1",
							Error: errors.New("failed"),
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts Error  
  ----- -----  
  host1 failed 

`,
		},
		"scm scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: scmFailed,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts Error           
  ----- -----           
  host1 scm scan failed 

-----
host1
-----
HugePage Size: 2048 KB
	No SCM modules found

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         2.0 TB   

`,
		},
		"nvme scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: nvmeFailed,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts Error            
  ----- -----            
  host1 nvme scan failed 

-----
host1
-----
HugePage Size: 2048 KB
SCM Module ID Socket ID Memory Ctrlr ID Channel ID Channel Slot Capacity 
------------- --------- --------------- ---------- ------------ -------- 
1             1         1               1          1            954 MiB  

	No NVMe devices found

`,
		},
		"scm and nvme scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1:1",
							Message: bothFailed,
						},
						{
							Addr:    "host2:1",
							Message: bothFailed,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts     Error            
  -----     -----            
  host[1-2] nvme scan failed 
  host[1-2] scm scan failed  

---------
host[1-2]
---------
HugePage Size: 2048 KB
	No SCM modules found

	No NVMe devices found

`,
		},
		"no storage": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: noStorage,
						},
						{
							Addr:    "host2",
							Message: noStorage,
						},
					},
				},
			},
			expPrintStr: `
---------
host[1-2]
---------
HugePage Size: 2048 KB
	No SCM modules found

	No NVMe devices found

`,
		},
		"single host": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: standard,
						},
					},
				},
			},
			expPrintStr: `
-----
host1
-----
HugePage Size: 2048 KB
SCM Module ID Socket ID Memory Ctrlr ID Channel ID Channel Slot Capacity 
------------- --------- --------------- ---------- ------------ -------- 
1             1         1               1          1            954 MiB  

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         2.0 TB   

`,
		},
		"single host with namespace": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: pmemSingle,
						},
					},
				},
			},
			expPrintStr: `
-----
host1
-----
HugePage Size: 2048 KB
SCM Namespace Socket ID Capacity 
------------- --------- -------- 
pmem0         0         1.0 TB   

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         2.0 TB   

`,
		},
		"two hosts same scan": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: standard,
						},
						{
							Addr:    "host2",
							Message: standard,
						},
					},
				},
			},
			expPrintStr: `
---------
host[1-2]
---------
HugePage Size: 2048 KB
SCM Module ID Socket ID Memory Ctrlr ID Channel ID Channel Slot Capacity 
------------- --------- --------------- ---------- ------------ -------- 
1             1         1               1          1            954 MiB  

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         2.0 TB   

`,
		},
		"two hosts different scans": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: noNvme,
						},
						{
							Addr:    "host2",
							Message: noScm,
						},
					},
				},
			},
			expPrintStr: `
-----
host1
-----
HugePage Size: 2048 KB
SCM Module ID Socket ID Memory Ctrlr ID Channel ID Channel Slot Capacity 
------------- --------- --------------- ---------- ------------ -------- 
1             1         1               1          1            954 MiB  

	No NVMe devices found

-----
host2
-----
HugePage Size: 2048 KB
	No SCM modules found

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         2.0 TB   

`,
		},
		"1024 hosts same scan": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: control.MockHostResponses(t,
						1024, "host%000d", standard),
				},
			},
			expPrintStr: `
------------
host[0-1023]
------------
HugePage Size: 2048 KB
SCM Module ID Socket ID Memory Ctrlr ID Channel ID Channel Slot Capacity 
------------- --------- --------------- ---------- ------------ -------- 
1             1         1               1          1            954 MiB  

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         2.0 TB   

`,
		},
		"multiple hosts with short names": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host-0001",
							Message: noScm,
						},
						{
							Addr:    "host-0002",
							Message: noScm,
						},
						{
							Addr:    "host-0003",
							Message: noScm,
						},
						{
							Addr:    "host-0004",
							Message: noScm,
						},
					},
				},
			},
			expPrintStr: `
----------------
host-[0001-0004]
----------------
HugePage Size: 2048 KB
	No SCM modules found

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         2.0 TB   

`,
		},
		"multiple hosts with multiple hyphens in names": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host-j-0001",
							Message: noScm,
						},
						{
							Addr:    "host-j-0002",
							Message: noScm,
						},
						{
							Addr:    "host-j-0003",
							Message: noScm,
						},
						{
							Addr:    "host-j-0004",
							Message: noScm,
						},
					},
				},
			},
			expPrintStr: `
------------------
host-j-[0001-0004]
------------------
HugePage Size: 2048 KB
	No SCM modules found

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         2.0 TB   

`,
		},
		"multiple hosts differing ssd capacity only": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: nvmeBasicA,
						},
						{
							Addr:    "host2",
							Message: nvmeBasicB,
						},
						{
							Addr:    "host3",
							Message: nvmeBasicA,
						},
						{
							Addr:    "host4",
							Message: nvmeBasicB,
						},
					},
				},
			},
			expPrintStr: `
---------
host[1,3]
---------
HugePage Size: 2048 KB
SCM Namespace Socket ID Capacity 
------------- --------- -------- 
pmem0         0         1.0 TB   
pmem1         1         2.0 TB   

NVMe PCI     Model FW Revision Socket ID Capacity 
--------     ----- ----------- --------- -------- 
0000:80:00.1                   1         2.0 TB   
0000:80:00.4                   0         2.0 TB   

---------
host[2,4]
---------
HugePage Size: 2048 KB
SCM Namespace Socket ID Capacity 
------------- --------- -------- 
pmem0         0         1.0 TB   
pmem1         1         2.0 TB   

NVMe PCI     Model FW Revision Socket ID Capacity 
--------     ----- ----------- --------- -------- 
0000:80:00.1                   1         2.1 TB   
0000:80:00.4                   0         2.1 TB   

`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, tc.mic)

			resp, err := control.StorageScan(ctx, mi, &control.StorageScanReq{})
			if err != nil {
				t.Fatal(err)
			}

			var bld strings.Builder
			if err := PrintResponseErrors(resp, &bld); err != nil {
				t.Fatal(err)
			}
			if err := PrintHostStorageMap(resp.HostStorage, &bld, PrintWithVerboseOutput(true)); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PrintStorageUsageScanResponse(t *testing.T) {
	var (
		withSpaceUsage = control.MockServerScanResp(t, "withSpaceUsage")
		noStorage      = control.MockServerScanResp(t, "noStorage")
		bothFailed     = control.MockServerScanResp(t, "bothFailed")
	)

	for name, tc := range map[string]struct {
		mic         *control.MockInvokerConfig
		expPrintStr string
	}{
		"empty response": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{},
			},
		},
		"server error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:  "host1",
							Error: errors.New("failed"),
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts Error  
  ----- -----  
  host1 failed 

`,
		},
		"scm and nvme scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1:1",
							Message: bothFailed,
						},
						{
							Addr:    "host2:1",
							Message: bothFailed,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts     Error            
  -----     -----            
  host[1-2] nvme scan failed 
  host[1-2] scm scan failed  

Hosts     SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used 
-----     --------- -------- -------- ---------- --------- --------- 
host[1-2] 0 B       0 B      N/A      0 B        0 B       N/A       
`,
		},
		"no storage": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: noStorage,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used 
----- --------- -------- -------- ---------- --------- --------- 
host1 0 B       0 B      N/A      0 B        0 B       N/A       
`,
		},
		"single host with space usage": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: withSpaceUsage,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used 
----- --------- -------- -------- ---------- --------- --------- 
host1 3.0 TB    750 GB   75 %     36 TB      27 TB     25 %      
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, tc.mic)

			resp, err := control.StorageScan(ctx, mi, &control.StorageScanReq{})
			if err != nil {
				t.Fatal(err)
			}

			var bld strings.Builder
			if err := PrintResponseErrors(resp, &bld); err != nil {
				t.Fatal(err)
			}
			if err := PrintHostStorageUsageMap(resp.HostStorage, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PrintStorageFormatResponse(t *testing.T) {
	for name, tc := range map[string]struct {
		resp        *control.StorageFormatResp
		expPrintStr string
	}{
		"empty response": {
			resp: &control.StorageFormatResp{},
		},
		"server error": {
			resp: &control.StorageFormatResp{
				HostErrorsResp: control.MockHostErrorsResp(t,
					&control.MockHostError{Hosts: "host1", Error: "failed"}),
			},
			expPrintStr: `
Errors:
  Hosts Error  
  ----- -----  
  host1 failed 

`,
		},
		"2 SCM, 2 NVMe; first SCM fails": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:       1,
				ScmPerHost:  2,
				ScmFailures: control.MockFailureMap(0),
				NvmePerHost: 2,
			}),
			expPrintStr: `
Errors:
  Hosts Error                
  ----- -----                
  host1 /mnt/1 format failed 

Format Summary:
  Hosts SCM Devices NVMe Devices 
  ----- ----------- ------------ 
  host1 1           1            
`,
		},
		"2 SCM, 2 NVMe; second NVMe fails": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:        1,
				ScmPerHost:   2,
				NvmePerHost:  2,
				NvmeFailures: control.MockFailureMap(1),
			}),
			expPrintStr: `
Errors:
  Hosts Error                       
  ----- -----                       
  host1 NVMe device 2 format failed 

Format Summary:
  Hosts SCM Devices NVMe Devices 
  ----- ----------- ------------ 
  host1 2           1            
`,
		},
		"2 SCM, 2 NVMe": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:       1,
				ScmPerHost:  2,
				NvmePerHost: 2,
			}),
			expPrintStr: `

Format Summary:
  Hosts SCM Devices NVMe Devices 
  ----- ----------- ------------ 
  host1 2           2            
`,
		},
		"2 Hosts, 2 SCM, 2 NVMe; first SCM fails": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:       2,
				ScmPerHost:  2,
				ScmFailures: control.MockFailureMap(0),
				NvmePerHost: 2,
			}),
			expPrintStr: `
Errors:
  Hosts     Error                
  -----     -----                
  host[1-2] /mnt/1 format failed 

Format Summary:
  Hosts     SCM Devices NVMe Devices 
  -----     ----------- ------------ 
  host[1-2] 1           1            
`,
		},
		"2 Hosts, 2 SCM, 2 NVMe": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:       2,
				ScmPerHost:  2,
				NvmePerHost: 2,
			}),
			expPrintStr: `

Format Summary:
  Hosts     SCM Devices NVMe Devices 
  -----     ----------- ------------ 
  host[1-2] 2           2            
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintResponseErrors(tc.resp, &bld); err != nil {
				t.Fatal(err)
			}
			if err := PrintStorageFormatMap(tc.resp.HostStorage, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PrintStorageFormatResponseVerbose(t *testing.T) {
	for name, tc := range map[string]struct {
		resp        *control.StorageFormatResp
		expPrintStr string
	}{
		"empty response": {
			resp: &control.StorageFormatResp{},
		},
		"server error": {
			resp: &control.StorageFormatResp{
				HostErrorsResp: control.MockHostErrorsResp(t,
					&control.MockHostError{Hosts: "host1", Error: "failed"}),
			},
			expPrintStr: `
Errors:
  Hosts Error  
  ----- -----  
  host1 failed 

`,
		},
		"2 SCM, 2 NVMe; first SCM fails": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:       1,
				ScmPerHost:  2,
				ScmFailures: control.MockFailureMap(0),
				NvmePerHost: 2,
			}),
			expPrintStr: `
Errors:
  Hosts Error                
  ----- -----                
  host1 /mnt/1 format failed 

-----
host1
-----
SCM Mount Format Result 
--------- ------------- 
/mnt/2    CTL_SUCCESS   

NVMe PCI Format Result 
-------- ------------- 
2        CTL_SUCCESS   

`,
		},
		"2 SCM, 2 NVMe; second NVMe fails": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:        1,
				ScmPerHost:   2,
				NvmePerHost:  2,
				NvmeFailures: control.MockFailureMap(1),
			}),
			expPrintStr: `
Errors:
  Hosts Error                       
  ----- -----                       
  host1 NVMe device 2 format failed 

-----
host1
-----
SCM Mount Format Result 
--------- ------------- 
/mnt/1    CTL_SUCCESS   
/mnt/2    CTL_SUCCESS   

NVMe PCI Format Result 
-------- ------------- 
1        CTL_SUCCESS   

`,
		},
		"2 SCM, 2 NVMe": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:       1,
				ScmPerHost:  2,
				NvmePerHost: 2,
			}),
			expPrintStr: `

-----
host1
-----
SCM Mount Format Result 
--------- ------------- 
/mnt/1    CTL_SUCCESS   
/mnt/2    CTL_SUCCESS   

NVMe PCI Format Result 
-------- ------------- 
1        CTL_SUCCESS   
2        CTL_SUCCESS   

`,
		},
		"2 Hosts, 2 SCM, 2 NVMe; first SCM fails": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:       2,
				ScmPerHost:  2,
				ScmFailures: control.MockFailureMap(0),
				NvmePerHost: 2,
			}),
			expPrintStr: `
Errors:
  Hosts     Error                
  -----     -----                
  host[1-2] /mnt/1 format failed 

---------
host[1-2]
---------
SCM Mount Format Result 
--------- ------------- 
/mnt/2    CTL_SUCCESS   

NVMe PCI Format Result 
-------- ------------- 
2        CTL_SUCCESS   

`,
		},
		"2 Hosts, 2 SCM, 2 NVMe": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:       2,
				ScmPerHost:  2,
				NvmePerHost: 2,
			}),
			expPrintStr: `

---------
host[1-2]
---------
SCM Mount Format Result 
--------- ------------- 
/mnt/1    CTL_SUCCESS   
/mnt/2    CTL_SUCCESS   

NVMe PCI Format Result 
-------- ------------- 
1        CTL_SUCCESS   
2        CTL_SUCCESS   

`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintResponseErrors(tc.resp, &bld); err != nil {
				t.Fatal(err)
			}
			if err := PrintStorageFormatMap(tc.resp.HostStorage, &bld, PrintWithVerboseOutput(true)); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintSmdInfoMap(t *testing.T) {
	mockController := storage.MockNvmeController(1)

	for name, tc := range map[string]struct {
		req         *control.SmdQueryReq
		hsm         control.HostStorageMap
		opts        []PrintConfigOption
		expPrintStr string
	}{
		"list-pools (standard)": {
			req: &control.SmdQueryReq{
				OmitDevices: true,
			},
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{
							Pools: control.SmdPoolMap{
								common.MockUUID(0): {
									{
										UUID:      common.MockUUID(0),
										Rank:      0,
										TargetIDs: []int32{0, 1, 2, 3},
										Blobs:     []uint64{11, 12, 13, 14},
									},
									{
										UUID:      common.MockUUID(0),
										Rank:      1,
										TargetIDs: []int32{0, 1, 2, 3},
										Blobs:     []uint64{11, 12, 13, 14},
									},
								},
							},
						},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  Pools
    UUID:00000000-0000-0000-0000-000000000000
      Rank:0 Targets:[0 1 2 3]
      Rank:1 Targets:[0 1 2 3]

`,
		},
		"list-pools (verbose)": {
			req: &control.SmdQueryReq{
				OmitDevices: true,
			},
			opts: []PrintConfigOption{PrintWithVerboseOutput(true)},
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{
							Pools: control.SmdPoolMap{
								common.MockUUID(0): {
									{
										UUID:      common.MockUUID(0),
										Rank:      0,
										TargetIDs: []int32{0, 1, 2, 3},
										Blobs:     []uint64{11, 12, 13, 14},
									},
									{
										UUID:      common.MockUUID(0),
										Rank:      1,
										TargetIDs: []int32{0, 1, 2, 3},
										Blobs:     []uint64{11, 12, 13, 14},
									},
								},
							},
						},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  Pools
    UUID:00000000-0000-0000-0000-000000000000
      Rank:0 Targets:[0 1 2 3] Blobs:[11 12 13 14]
      Rank:1 Targets:[0 1 2 3] Blobs:[11 12 13 14]

`,
		},

		"list-pools (none found)": {
			req: &control.SmdQueryReq{
				OmitDevices: true,
			},
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  No pools found
`,
		},
		"list-devices": {
			req: &control.SmdQueryReq{
				OmitPools: true,
			},
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{
							Devices: []*storage.SmdDevice{
								{
									UUID:      common.MockUUID(0),
									TrAddr:    "0000:8a:00.0",
									TargetIDs: []int32{0, 1, 2},
									Rank:      0,
									NvmeState: storage.MockNvmeStateNormal,
								},
								{
									UUID:      common.MockUUID(1),
									TrAddr:    "0000:8b:00.0",
									TargetIDs: []int32{3, 4, 5},
									Rank:      0,
									NvmeState: storage.MockNvmeStateEvicted,
								},
								{
									UUID:      common.MockUUID(2),
									TrAddr:    "0000:da:00.0",
									TargetIDs: []int32{0, 1, 2},
									Rank:      1,
									NvmeState: storage.NvmeDevState(0),
								},
								{
									UUID:      common.MockUUID(3),
									TrAddr:    "0000:db:00.0",
									TargetIDs: []int32{3, 4, 5},
									Rank:      1,
									NvmeState: storage.MockNvmeStateIdentify,
								},
							},
						},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  Devices
    UUID:00000000-0000-0000-0000-000000000000 [TrAddr:0000:8a:00.0]
      Targets:[0 1 2] Rank:0 State:NORMAL
    UUID:00000001-0001-0001-0001-000000000001 [TrAddr:0000:8b:00.0]
      Targets:[3 4 5] Rank:0 State:EVICTED
    UUID:00000002-0002-0002-0002-000000000002 [TrAddr:0000:da:00.0]
      Targets:[0 1 2] Rank:1 State:UNPLUGGED
    UUID:00000003-0003-0003-0003-000000000003 [TrAddr:0000:db:00.0]
      Targets:[3 4 5] Rank:1 State:NORMAL|IDENTIFY
`,
		},
		"list-devices (none found)": {
			req: &control.SmdQueryReq{
				OmitPools: true,
			},
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  No devices found
`,
		},
		"device-health": {
			req: &control.SmdQueryReq{
				OmitPools: true,
			},
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{
							Devices: []*storage.SmdDevice{
								{
									UUID:      common.MockUUID(0),
									TargetIDs: []int32{0, 1, 2},
									Rank:      0,
									NvmeState: storage.MockNvmeStateNormal,
									Health:    mockController.HealthStats,
								},
							},
						},
					},
				},
			),
			expPrintStr: fmt.Sprintf(`
-----
host1
-----
  Devices
    UUID:00000000-0000-0000-0000-000000000000 [TrAddr:]
      Targets:[0 1 2] Rank:0 State:NORMAL
      Health Stats:
        Temperature:%dK(%.02fC)
        Temperature Warning Duration:%dm0s
        Temperature Critical Duration:%dm0s
        Controller Busy Time:%dm0s
        Power Cycles:%d
        Power On Duration:%s
        Unsafe Shutdowns:%d
        Media Errors:%d
        Error Log Entries:%d
      Critical Warnings:
        Temperature: WARNING
        Available Spare: WARNING
        Device Reliability: WARNING
        Read Only: WARNING
        Volatile Memory Backup: WARNING
      Intel Vendor SMART Attributes:
        Program Fail Count:
           Normalized:%d%s
           Raw:%d
        Erase Fail Count:
           Normalized:%d%s
           Raw:%d
        Wear Leveling Count:
           Normalized:%d%s
           Min:%d
           Max:%d
           Avg:%d
        End-to-End Error Detection Count:%d
        CRC Error Count:%d
        Timed Workload, Media Wear:%d
        Timed Workload, Host Read/Write Ratio:%d
        Timed Workload, Timer:%d
        Thermal Throttle Status:%d%s
        Thermal Throttle Event Count:%d
        Retry Buffer Overflow Counter:%d
        PLL Lock Loss Count:%d
        NAND Bytes Written:%d
        Host Bytes Written:%d

`,
				mockController.HealthStats.TempK(), mockController.HealthStats.TempC(),
				mockController.HealthStats.TempWarnTime, mockController.HealthStats.TempCritTime,
				mockController.HealthStats.CtrlBusyTime, mockController.HealthStats.PowerCycles,
				time.Duration(mockController.HealthStats.PowerOnHours)*time.Hour,
				mockController.HealthStats.UnsafeShutdowns, mockController.HealthStats.MediaErrors,
				mockController.HealthStats.ErrorLogEntries,
				mockController.HealthStats.ProgFailCntNorm, "%", mockController.HealthStats.ProgFailCntRaw,
				mockController.HealthStats.EraseFailCntNorm, "%", mockController.HealthStats.EraseFailCntRaw,
				mockController.HealthStats.WearLevelingCntNorm, "%", mockController.HealthStats.WearLevelingCntMin,
				mockController.HealthStats.WearLevelingCntMax, mockController.HealthStats.WearLevelingCntAvg,
				mockController.HealthStats.EndtoendErrCntRaw, mockController.HealthStats.CrcErrCntRaw,
				mockController.HealthStats.MediaWearRaw, mockController.HealthStats.HostReadsRaw,
				mockController.HealthStats.WorkloadTimerRaw,
				mockController.HealthStats.ThermalThrottleStatus, "%", mockController.HealthStats.ThermalThrottleEventCnt,
				mockController.HealthStats.RetryBufferOverflowCnt,
				mockController.HealthStats.PllLockLossCnt,
				mockController.HealthStats.NandBytesWritten, mockController.HealthStats.HostBytesWritten,
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintSmdInfoMap(tc.req, tc.hsm, &bld, tc.opts...); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected print output (-want, +got):\n%s\n", diff)
			}
		})
	}
}

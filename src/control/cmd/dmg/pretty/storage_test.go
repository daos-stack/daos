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
		standardScan      = control.MockServerScanResp(t, "standard")
		withNamespaceScan = control.MockServerScanResp(t, "withNamespace")
		noNVMEScan        = control.MockServerScanResp(t, "noNVME")
		noSCMScan         = control.MockServerScanResp(t, "noSCM")
		noStorageScan     = control.MockServerScanResp(t, "noStorage")
		scmScanFailed     = control.MockServerScanResp(t, "scmFailed")
		nvmeScanFailed    = control.MockServerScanResp(t, "nvmeFailed")
		bothScansFailed   = control.MockServerScanResp(t, "bothFailed")
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
							Message: scmScanFailed,
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
host1 0 B (0 modules) 1 B (1 controller) 
`,
		},
		"nvme scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: nvmeScanFailed,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts Error            
  ----- -----            
  host1 nvme scan failed 

Hosts SCM Total      NVMe Total          
----- ---------      ----------          
host1 1 B (1 module) 0 B (0 controllers) 
`,
		},
		"scm and nvme scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1:1",
							Message: bothScansFailed,
						},
						{
							Addr:    "host2:1",
							Message: bothScansFailed,
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
							Message: noStorageScan,
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
							Message: standardScan,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM Total      NVMe Total         
----- ---------      ----------         
host1 1 B (1 module) 1 B (1 controller) 
`,
		},
		"single host with namespace": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: withNamespaceScan,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM Total         NVMe Total         
----- ---------         ----------         
host1 1 B (1 namespace) 1 B (1 controller) 
`,
		},
		"two hosts same scan": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: standardScan,
						},
						{
							Addr:    "host2",
							Message: standardScan,
						},
					},
				},
			},
			expPrintStr: `
Hosts     SCM Total      NVMe Total         
-----     ---------      ----------         
host[1-2] 1 B (1 module) 1 B (1 controller) 
`,
		},
		"two hosts different scans": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: noNVMEScan,
						},
						{
							Addr:    "host2",
							Message: noSCMScan,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM Total       NVMe Total          
----- ---------       ----------          
host1 1 B (1 module)  0 B (0 controllers) 
host2 0 B (0 modules) 1 B (1 controller)  
`,
		},
		"1024 hosts same scan": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: control.MockHostResponses(t,
						1024, "host%000d", standardScan),
				},
			},
			expPrintStr: `
Hosts        SCM Total      NVMe Total         
-----        ---------      ----------         
host[0-1023] 1 B (1 module) 1 B (1 controller) 
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
			if err := PrintHostStorageMap(resp.HostStorage, &bld); err != nil {
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
		withSpaceUsageScan = control.MockServerScanResp(t, "withSpaceUsage")
		noStorageScan      = control.MockServerScanResp(t, "noStorage")
		bothScansFailed    = control.MockServerScanResp(t, "bothFailed")
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
							Message: bothScansFailed,
						},
						{
							Addr:    "host2:1",
							Message: bothScansFailed,
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
							Message: noStorageScan,
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
							Message: withSpaceUsageScan,
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

func TestControl_PrintStorageScanResponseVerbose(t *testing.T) {
	var (
		standardScan      = control.MockServerScanResp(t, "standard")
		withNamespaceScan = control.MockServerScanResp(t, "withNamespace")
		noNVMEScan        = control.MockServerScanResp(t, "noNVME")
		noSCMScan         = control.MockServerScanResp(t, "noSCM")
		noStorageScan     = control.MockServerScanResp(t, "noStorage")
		scmScanFailed     = control.MockServerScanResp(t, "scmFailed")
		nvmeScanFailed    = control.MockServerScanResp(t, "nvmeFailed")
		bothScansFailed   = control.MockServerScanResp(t, "bothFailed")
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
							Message: scmScanFailed,
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
	No SCM modules found

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         1 B      

`,
		},
		"nvme scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: nvmeScanFailed,
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
SCM Module ID Socket ID Memory Ctrlr ID Channel ID Channel Slot Capacity 
------------- --------- --------------- ---------- ------------ -------- 
1             1         1               1          1            1 B      

	No NVMe devices found

`,
		},
		"scm and nvme scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1:1",
							Message: bothScansFailed,
						},
						{
							Addr:    "host2:1",
							Message: bothScansFailed,
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
							Message: noStorageScan,
						},
						{
							Addr:    "host2",
							Message: noStorageScan,
						},
					},
				},
			},
			expPrintStr: `
---------
host[1-2]
---------
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
							Message: standardScan,
						},
					},
				},
			},
			expPrintStr: `
-----
host1
-----
SCM Module ID Socket ID Memory Ctrlr ID Channel ID Channel Slot Capacity 
------------- --------- --------------- ---------- ------------ -------- 
1             1         1               1          1            1 B      

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         1 B      

`,
		},
		"single host with namespace": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: withNamespaceScan,
						},
					},
				},
			},
			expPrintStr: `
-----
host1
-----
SCM Namespace Socket ID Capacity 
------------- --------- -------- 
pmem0         0         1 B      

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         1 B      

`,
		},
		"two hosts same scan": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: standardScan,
						},
						{
							Addr:    "host2",
							Message: standardScan,
						},
					},
				},
			},
			expPrintStr: `
---------
host[1-2]
---------
SCM Module ID Socket ID Memory Ctrlr ID Channel ID Channel Slot Capacity 
------------- --------- --------------- ---------- ------------ -------- 
1             1         1               1          1            1 B      

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         1 B      

`,
		},
		"two hosts different scans": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: noNVMEScan,
						},
						{
							Addr:    "host2",
							Message: noSCMScan,
						},
					},
				},
			},
			expPrintStr: `
-----
host1
-----
SCM Module ID Socket ID Memory Ctrlr ID Channel ID Channel Slot Capacity 
------------- --------- --------------- ---------- ------------ -------- 
1             1         1               1          1            1 B      

	No NVMe devices found

-----
host2
-----
	No SCM modules found

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         1 B      

`,
		},
		"1024 hosts same scan": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: control.MockHostResponses(t,
						1024, "host%000d", standardScan),
				},
			},
			expPrintStr: `
------------
host[0-1023]
------------
SCM Module ID Socket ID Memory Ctrlr ID Channel ID Channel Slot Capacity 
------------- --------- --------------- ---------- ------------ -------- 
1             1         1               1          1            1 B      

NVMe PCI     Model   FW Revision Socket ID Capacity 
--------     -----   ----------- --------- -------- 
0000:80:00.1 model-1 fwRev-1     1         1 B      

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
									TargetIDs: []int32{0, 1, 2},
									Rank:      0,
									State:     "NORMAL",
								},
								{
									UUID:      common.MockUUID(1),
									TargetIDs: []int32{0, 1, 2},
									Rank:      1,
									State:     "FAULTY",
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
    UUID:00000000-0000-0000-0000-000000000000 Targets:[0 1 2] Rank:0 State:NORMAL
    UUID:11111111-1111-1111-1111-111111111111 Targets:[0 1 2] Rank:1 State:FAULTY
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
									State:     "NORMAL",
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
    UUID:00000000-0000-0000-0000-000000000000 Targets:[0 1 2] Rank:0 State:NORMAL
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

`,
				mockController.HealthStats.TempK(), mockController.HealthStats.TempC(),
				mockController.HealthStats.TempWarnTime, mockController.HealthStats.TempCritTime,
				mockController.HealthStats.CtrlBusyTime, mockController.HealthStats.PowerCycles,
				time.Duration(mockController.HealthStats.PowerOnHours)*time.Hour,
				mockController.HealthStats.UnsafeShutdowns, mockController.HealthStats.MediaErrors,
				mockController.HealthStats.ErrorLogEntries,
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

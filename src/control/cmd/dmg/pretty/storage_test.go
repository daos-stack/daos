//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"errors"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
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

func TestPretty_PrintHostStorageMap(t *testing.T) {
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
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)

			resp, err := control.StorageScan(test.Context(t), mi, &control.StorageScanReq{})
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

func TestPretty_PrintHostStorageMap_verbose(t *testing.T) {
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

NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      0    

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

SCM Module Socket Memory Ctrlr Channel Channel Slot Capacity UID     Part Number Health  
---------- ------ ------------ ------- ------------ -------- ---     ----------- ------  
1          1      1            1       1            954 MiB  Device1 PartNumber1 Healthy 

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

SCM Module Socket Memory Ctrlr Channel Channel Slot Capacity UID     Part Number Health  
---------- ------ ------------ ------- ------------ -------- ---     ----------- ------  
1          1      1            1       1            954 MiB  Device1 PartNumber1 Healthy 

NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      0    

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

SCM Namespace Socket Capacity 
------------- ------ -------- 
pmem0         0      1.0 TB   

NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      0    

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

SCM Module Socket Memory Ctrlr Channel Channel Slot Capacity UID     Part Number Health  
---------- ------ ------------ ------- ------------ -------- ---     ----------- ------  
1          1      1            1       1            954 MiB  Device1 PartNumber1 Healthy 

NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      0    

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

SCM Module Socket Memory Ctrlr Channel Channel Slot Capacity UID     Part Number Health  
---------- ------ ------------ ------- ------------ -------- ---     ----------- ------  
1          1      1            1       1            954 MiB  Device1 PartNumber1 Healthy 

  No NVMe devices found

-----
host2
-----
HugePage Size: 2048 KB

  No SCM modules found

NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      0    

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

SCM Module Socket Memory Ctrlr Channel Channel Slot Capacity UID     Part Number Health  
---------- ------ ------------ ------- ------------ -------- ---     ----------- ------  
1          1      1            1       1            954 MiB  Device1 PartNumber1 Healthy 

NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      0    

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

NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      0    

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

NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      0    

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

SCM Namespace Socket Capacity 
------------- ------ -------- 
pmem0         0      1.0 TB   
pmem1         1      2.0 TB   

NVMe PCI     Model FW Revision Socket Capacity Role(s)       Rank 
--------     ----- ----------- ------ -------- -------       ---- 
0000:01:00.0                   1      2.0 TB   data,meta,wal 0    
0000:04:00.0                   0      2.0 TB   data,meta,wal 0    

---------
host[2,4]
---------
HugePage Size: 2048 KB

SCM Namespace Socket Capacity 
------------- ------ -------- 
pmem0         0      1.0 TB   
pmem1         1      2.0 TB   

NVMe PCI     Model FW Revision Socket Capacity Role(s)       Rank 
--------     ----- ----------- ------ -------- -------       ---- 
0000:01:00.0                   1      2.1 TB   data,meta,wal 0    
0000:04:00.0                   0      2.1 TB   data,meta,wal 0    

`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.Context(t)
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

func TestPretty_PrintStorageFormatMap(t *testing.T) {
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
		"1 SCM, NVMe skipped": {
			resp: &control.StorageFormatResp{
				HostErrorsResp: control.HostErrorsResp{
					HostErrors: make(control.HostErrorsMap),
				},
				HostStorage: func() control.HostStorageMap {
					hsm := make(control.HostStorageMap)
					hs := &control.HostStorage{
						ScmMountPoints: []*storage.ScmMountPoint{
							{
								Info: "success",
								Path: "/mnt/0",
							},
						},
						NvmeDevices: []*storage.NvmeController{
							{
								Info:    "skipping",
								PciAddr: storage.NilBdevAddress,
							},
						},
					}
					if err := hsm.Add("host1", hs); err != nil {
						t.Fatal(err)
					}
					return hsm
				}(),
			},
			expPrintStr: `

Format Summary:
  Hosts SCM Devices NVMe Devices 
  ----- ----------- ------------ 
  host1 1           0            
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

func TestPretty_PrintStorageFormatMap_verbose(t *testing.T) {
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

NVMe PCI Format Result Role(s) 
-------- ------------- ------- 
2        CTL_SUCCESS   NA      

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

NVMe PCI Format Result Role(s) 
-------- ------------- ------- 
1        CTL_SUCCESS   NA      

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

NVMe PCI Format Result Role(s) 
-------- ------------- ------- 
1        CTL_SUCCESS   NA      
2        CTL_SUCCESS   NA      

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

NVMe PCI Format Result Role(s) 
-------- ------------- ------- 
2        CTL_SUCCESS   NA      

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

NVMe PCI Format Result Role(s) 
-------- ------------- ------- 
1        CTL_SUCCESS   NA      
2        CTL_SUCCESS   NA      

`,
		},
		"2 Hosts, 2 SCM, 2 NVMe; MD-on-SSD roles": {
			resp: control.MockFormatResp(t, control.MockFormatConf{
				Hosts:        2,
				ScmPerHost:   2,
				NvmePerHost:  2,
				NvmeRoleBits: int(storage.BdevRoleAll),
			}),
			expPrintStr: `

---------
host[1-2]
---------
SCM Mount Format Result 
--------- ------------- 
/mnt/1    CTL_SUCCESS   
/mnt/2    CTL_SUCCESS   

NVMe PCI Format Result Role(s)       
-------- ------------- -------       
1        CTL_SUCCESS   data,meta,wal 
2        CTL_SUCCESS   data,meta,wal 

`,
		},
		"1 SCM, NVMe skipped": {
			resp: &control.StorageFormatResp{
				HostErrorsResp: control.HostErrorsResp{
					HostErrors: make(control.HostErrorsMap),
				},
				HostStorage: func() control.HostStorageMap {
					hsm := make(control.HostStorageMap)
					hs := &control.HostStorage{
						ScmMountPoints: []*storage.ScmMountPoint{
							{
								Info: "CTL_SUCCESS",
								Path: "/mnt/0",
							},
						},
						NvmeDevices: []*storage.NvmeController{
							{
								Info:    "skipping",
								PciAddr: storage.NilBdevAddress,
							},
						},
					}
					if err := hsm.Add("host1", hs); err != nil {
						t.Fatal(err)
					}
					return hsm
				}(),
			},
			expPrintStr: `

-----
host1
-----
SCM Mount Format Result 
--------- ------------- 
/mnt/0    CTL_SUCCESS   

  No NVMe devices were formatted

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

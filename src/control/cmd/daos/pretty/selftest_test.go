//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty_test

import (
	"errors"
	"strings"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/cmd/daos/pretty"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

func TestPretty_PrintSelfTestConfig(t *testing.T) {
	genCfg := func(xfrm func(cfg *daos.SelfTestConfig)) *daos.SelfTestConfig {
		cfg := &daos.SelfTestConfig{}
		cfg.SetDefaults()
		if xfrm != nil {
			xfrm(cfg)
		}
		return cfg
	}
	for name, tc := range map[string]struct {
		cfg     *daos.SelfTestConfig
		verbose bool
		expStr  string
		expErr  error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"defaults": {
			cfg: genCfg(nil),
			expStr: `
Client/Server Network Test Parameters
-------------------------------------
  Servers        : All     
  Send RPC Size  : 1.00 KiB
  Reply RPC Size : 1.00 KiB
  RPCs Per Server: 10000   

`,
		},
		"single server": {
			cfg: genCfg(func(cfg *daos.SelfTestConfig) {
				cfg.EndpointRanks = []ranklist.Rank{1}
			}),
			expStr: `
Client/Server Network Test Parameters
-------------------------------------
  Server         : 1       
  Send RPC Size  : 1.00 KiB
  Reply RPC Size : 1.00 KiB
  RPCs Per Server: 10000   

`,
		},
		"custom": {
			cfg: genCfg(func(cfg *daos.SelfTestConfig) {
				cfg.EndpointRanks = []ranklist.Rank{0, 1, 2}
				cfg.SendSizes = []uint64{1024, 1024 * 1024}
				cfg.ReplySizes = []uint64{2048 * 1024, 2048 * 1024 * 1024}
			}),
			expStr: `
Client/Server Network Test Parameters
-------------------------------------
  Servers        : [0-2]              
  Send RPC Sizes : [1.00 KiB 1.00 MiB]
  Reply RPC Sizes: [2.00 MiB 2.00 GiB]
  RPCs Per Server: 10000              

`,
		},
		"defaults - verbose": {
			cfg:     genCfg(nil),
			verbose: true,
			expStr: `
Client/Server Network Test Parameters
-------------------------------------
  Servers           : All        
  Send RPC Size     : 1.00 KiB   
  Reply RPC Size    : 1.00 KiB   
  RPCs Per Server   : 10000      
  System Name       : daos_server
  Tag               : 0          
  Max In-Flight RPCs: 16         

`,
		},
		"custom - verbose": {
			cfg: genCfg(func(cfg *daos.SelfTestConfig) {
				cfg.EndpointRanks = []ranklist.Rank{0, 1, 2}
				cfg.EndpointTags = []uint32{0, 1, 2}
				cfg.SendSizes = []uint64{1024, 1024 * 1024}
				cfg.ReplySizes = []uint64{2048 * 1024, 2048 * 1024 * 1024}
			}),
			verbose: true,
			expStr: `
Client/Server Network Test Parameters
-------------------------------------
  Servers           : [0-2]              
  Send RPC Sizes    : [1.00 KiB 1.00 MiB]
  Reply RPC Sizes   : [2.00 MiB 2.00 GiB]
  RPCs Per Server   : 10000              
  System Name       : daos_server        
  Tags              : [0-2]              
  Max In-Flight RPCs: 16                 

`,
		},
		"no sizes?": {
			cfg: genCfg(func(cfg *daos.SelfTestConfig) {
				cfg.SendSizes = []uint64{}
				cfg.ReplySizes = []uint64{}
			}),
			verbose: true,
			expStr: `
Client/Server Network Test Parameters
-------------------------------------
  Servers           : All        
  Send RPC Size     : None       
  Reply RPC Size    : None       
  RPCs Per Server   : 10000      
  System Name       : daos_server
  Tag               : 0          
  Max In-Flight RPCs: 16         

`,
		},
		"no targets?": {
			cfg: genCfg(func(cfg *daos.SelfTestConfig) {
				cfg.EndpointTags = []uint32{}
			}),
			verbose: true,
			expStr: `
Client/Server Network Test Parameters
-------------------------------------
  Servers           : All           
  Send RPC Size     : 1.00 KiB      
  Reply RPC Size    : 1.00 KiB      
  RPCs Per Server   : 10000         
  System Name       : daos_server   
  Tags              : ERROR (0 tags)
  Max In-Flight RPCs: 16            

`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			gotErr := pretty.PrintSelfTestConfig(&bld, tc.cfg, tc.verbose)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "Config Output", strings.TrimLeft(tc.expStr, "\n"), bld.String())
		})
	}
}

func genResult(xfrm func(result *daos.SelfTestResult)) *daos.SelfTestResult {
	cfg := &daos.SelfTestConfig{}
	cfg.SetDefaults()
	cfg.SendSizes = []uint64{1 << 20}
	cfg.ReplySizes = cfg.SendSizes
	result := &daos.SelfTestResult{
		MasterEndpoint: daos.SelfTestEndpoint{Rank: 3, Tag: 0},
		TargetEndpoints: []daos.SelfTestEndpoint{
			{Rank: 0, Tag: 0},
			{Rank: 1, Tag: 0},
			{Rank: 2, Tag: 0},
		},
		Repetitions:     cfg.Repetitions * 3,
		SendSize:        cfg.SendSizes[0],
		ReplySize:       cfg.ReplySizes[0],
		BufferAlignment: cfg.BufferAlignment,
		Duration:        8500 * time.Millisecond,
		MasterLatency:   &daos.EndpointLatency{},
	}
	for i := int64(1); i <= int64(result.Repetitions); i++ {
		result.MasterLatency.AddValue(i * 1000)
		result.AddTargetLatency(ranklist.Rank(i%3), 0, i*1000)
	}
	if xfrm != nil {
		xfrm(result)
	}
	return result
}

func TestPrettyPrintSelfTestResult(t *testing.T) {
	for name, tc := range map[string]struct {
		result    *daos.SelfTestResult
		verbose   bool
		showBytes bool
		expStr    string
		expErr    error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"non-verbose, bps": {
			result: genResult(nil),
			expStr: `
Client/Server Network Test Summary
----------------------------------
  Server Endpoints: [0-2]:0      
  RPC Throughput  : 3529.41 RPC/s
  RPC Bandwidth   : 59.21 Gbps   
  Average Latency : 15.00ms      
  95% Latency     : 28.50ms      
  99% Latency     : 29.70ms      

`,
		},
		"non-verbose, bytes": {
			result:    genResult(nil),
			showBytes: true,
			expStr: `
Client/Server Network Test Summary
----------------------------------
  Server Endpoints: [0-2]:0      
  RPC Throughput  : 3529.41 RPC/s
  RPC Bandwidth   : 7.40 GB/s    
  Average Latency : 15.00ms      
  95% Latency     : 28.50ms      
  99% Latency     : 29.70ms      

`,
		},
		"verbose, bps": {
			result:  genResult(nil),
			verbose: true,
			expStr: `
Client/Server Network Test Summary
----------------------------------
  Server Endpoints: [0-2]:0      
  RPC Throughput  : 3529.41 RPC/s
  RPC Bandwidth   : 59.21 Gbps   
  Average Latency : 15.00ms      
  95% Latency     : 28.50ms      
  99% Latency     : 29.70ms      
  Client Endpoint : 3:0          
  Duration        : 8.5s         
  Repetitions     : 30000        
  Send Size       : 1.00 MiB     
  Reply Size      : 1.00 MiB     

Per-Target Latency Results
  Target Min    50%     75%     90%     95%     99%     Max     Average StdDev 
  ------ ---    ---     ---     ---     ---     ---     ---     ------- ------ 
  0:0    0.00ms 15.00ms 22.50ms 27.00ms 28.50ms 29.70ms 30.00ms 15.00ms 8.66ms 
  1:0    0.00ms 15.00ms 22.50ms 27.00ms 28.50ms 29.70ms 30.00ms 15.00ms 8.66ms 
  2:0    0.00ms 15.00ms 22.50ms 27.00ms 28.50ms 29.70ms 30.00ms 15.00ms 8.66ms 
`,
		},
		"verbose with failures, bytes": {
			result: genResult(func(res *daos.SelfTestResult) {
				for i := int64(1); i <= int64(res.Repetitions/4); i++ {
					res.MasterLatency.AddValue(-1)
					res.AddTargetLatency(ranklist.Rank(i%3), 0, -1)
				}
			}),
			verbose:   true,
			showBytes: true,
			expStr: `
Client/Server Network Test Summary
----------------------------------
  Server Endpoints: [0-2]:0      
  RPC Throughput  : 3529.41 RPC/s
  RPC Bandwidth   : 7.40 GB/s    
  Average Latency : 15.00ms      
  95% Latency     : 28.50ms      
  99% Latency     : 29.70ms      
  Client Endpoint : 3:0          
  Duration        : 8.5s         
  Repetitions     : 30000        
  Send Size       : 1.00 MiB     
  Reply Size      : 1.00 MiB     
  Failed RPCs     : 7500 (25.0%) 

Per-Target Latency Results
  Target Min    50%     75%     90%     95%     99%     Max     Average StdDev Failed 
  ------ ---    ---     ---     ---     ---     ---     ---     ------- ------ ------ 
  0:0    0.00ms 15.00ms 22.50ms 27.00ms 28.50ms 29.70ms 30.00ms 15.00ms 8.66ms 20.0%  
  1:0    0.00ms 15.00ms 22.50ms 27.00ms 28.50ms 29.70ms 30.00ms 15.00ms 8.66ms 20.0%  
  2:0    0.00ms 15.00ms 22.50ms 27.00ms 28.50ms 29.70ms 30.00ms 15.00ms 8.66ms 20.0%  
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			gotErr := pretty.PrintSelfTestResult(&bld, tc.result, tc.verbose, tc.showBytes)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "Self Test Result", strings.TrimLeft(tc.expStr, "\n"), bld.String())
		})
	}
}

func TestPretty_PrintSelfTestResults(t *testing.T) {
	for name, tc := range map[string]struct {
		results []*daos.SelfTestResult
		verbose bool
		expStr  string
		expErr  error
	}{
		"zero results": {
			expStr: `
No test results.
`,
		},
		"one result": {
			results: []*daos.SelfTestResult{genResult(nil)},
			expStr: `
Client/Server Network Test Summary
----------------------------------
  Server Endpoints: [0-2]:0      
  RPC Throughput  : 3529.41 RPC/s
  RPC Bandwidth   : 59.21 Gbps   
  Average Latency : 15.00ms      
  95% Latency     : 28.50ms      
  99% Latency     : 29.70ms      

`,
		},
		"two results": {
			results: []*daos.SelfTestResult{genResult(nil), genResult(nil)},
			expStr: `
Showing 2 self test results:
  Client/Server Network Test Summary
  ----------------------------------
    Server Endpoints: [0-2]:0      
    RPC Throughput  : 3529.41 RPC/s
    RPC Bandwidth   : 59.21 Gbps   
    Average Latency : 15.00ms      
    95% Latency     : 28.50ms      
    99% Latency     : 29.70ms      

  Client/Server Network Test Summary
  ----------------------------------
    Server Endpoints: [0-2]:0      
    RPC Throughput  : 3529.41 RPC/s
    RPC Bandwidth   : 59.21 Gbps   
    Average Latency : 15.00ms      
    95% Latency     : 28.50ms      
    99% Latency     : 29.70ms      

`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			gotErr := pretty.PrintSelfTestResults(&bld, tc.results, tc.verbose, false)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "Config Output", strings.TrimLeft(tc.expStr, "\n"), bld.String())
		})
	}
}

//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestAPI_RunSelfTest(t *testing.T) {
	genCfg := func(xfrm func(cfg *daos.SelfTestConfig)) *daos.SelfTestConfig {
		cfg := &daos.SelfTestConfig{}
		cfg.SetDefaults()
		if xfrm != nil {
			xfrm(cfg)
		}
		return cfg
	}
	genEndPoints := func(tags []uint32, ranks ...ranklist.Rank) (eps []daos.SelfTestEndpoint) {
		if len(tags) == 0 {
			tags = []uint32{0}
		}
		if len(ranks) == 0 {
			ranks = []ranklist.Rank{0}
		}
		for i := 0; i < len(ranks); i++ {
			for j := 0; j < len(tags); j++ {
				eps = append(eps, daos.SelfTestEndpoint{Rank: ranks[i], Tag: tags[j]})
			}
		}
		return
	}
	genEpLatencies := func(totalReps uint, eps ...daos.SelfTestEndpoint) (lats []run_self_test_EndpointLatency) {
		if totalReps == 0 {
			totalReps = genCfg(nil).Repetitions
		}
		if len(eps) == 0 {
			eps = genEndPoints(nil, 0, 1, 2)
		}
		lats = make([]run_self_test_EndpointLatency, totalReps)
		latCount := 0
		for i := 0; i < int(totalReps); i++ {
			lats[i] = run_self_test_EndpointLatency{
				val:    _Ctype_int64_t(i + 1),
				rank:   _Ctype___uint32_t(eps[i%len(eps)].Rank),
				tag:    _Ctype___uint32_t(eps[i%len(eps)].Tag),
				cci_rc: 0,
			}
			latCount++
		}
		return
	}
	genExpResults := func(cfg *daos.SelfTestConfig) (results []*daos.SelfTestResult) {
		for i := range cfg.SendSizes {
			var tgtEndpoints []daos.SelfTestEndpoint
			if len(cfg.EndpointRanks) > 0 {
				tgtEndpoints = genEndPoints(cfg.EndpointTags, cfg.EndpointRanks...)
			}

			masterEndPoints := cfg.MasterEndpoints
			if len(masterEndPoints) == 0 {
				masterEndPoints = []daos.SelfTestEndpoint{
					{
						Rank: cfg.EndpointRanks[len(cfg.EndpointRanks)-1] + 1,
						Tag:  cfg.EndpointTags[len(cfg.EndpointTags)-1],
					},
				}
			}

			for _, mep := range masterEndPoints {
				tgtEps := genEndPoints(cfg.EndpointTags, cfg.EndpointRanks...)
				res := &daos.SelfTestResult{
					MasterEndpoint:  mep,
					TargetEndpoints: tgtEps,
					Repetitions:     cfg.Repetitions * uint(len(tgtEps)),
					SendSize:        cfg.SendSizes[i],
					ReplySize:       cfg.ReplySizes[i],
					BufferAlignment: cfg.BufferAlignment,
					MasterLatency:   &daos.EndpointLatency{},
					TargetLatencies: make(map[daos.SelfTestEndpoint]*daos.EndpointLatency),
				}
				for _, ep := range tgtEndpoints {
					res.TargetLatencies[ep] = &daos.EndpointLatency{}
				}

				results = append(results, res)
			}
		}
		return
	}

	for name, tc := range map[string]struct {
		cfg             *daos.SelfTestConfig
		self_test_RC    int
		testSysInfo     *daos.SystemInfo
		get_sys_info_RC int
		expMsEps        []daos.SelfTestEndpoint
		expRunCfg       *daos.SelfTestConfig
		expRunResults   []*daos.SelfTestResult
		expErr          error
	}{
		"empty config": {
			cfg:    &daos.SelfTestConfig{},
			expErr: errors.New("invalid self_test configuration"),
		},
		"library alloc fails": {
			cfg:          genCfg(nil),
			self_test_RC: int(daos.NoMemory),
			expErr:       daos.NoMemory,
		},
		"GetSystemInfo fails": {
			cfg:             genCfg(nil),
			get_sys_info_RC: int(daos.AgentCommFailed),
			expErr:          daos.AgentCommFailed,
		},
		"custom config -- 1 rank": {
			cfg: genCfg(func(cfg *daos.SelfTestConfig) {
				cfg.EndpointRanks = []ranklist.Rank{1}
				cfg.EndpointTags = []uint32{1}
				cfg.Repetitions = 10
				cfg.SendSizes = []uint64{1024}
				cfg.ReplySizes = []uint64{1024}
			}),
		},
		"custom config -- defined master endpoints": {
			cfg: genCfg(func(cfg *daos.SelfTestConfig) {
				cfg.EndpointRanks = []ranklist.Rank{0, 1, 2}
				cfg.EndpointTags = []uint32{1}
				cfg.MasterEndpoints = []daos.SelfTestEndpoint{
					{Rank: 0, Tag: 1},
					{Rank: 1, Tag: 1},
				}
			}),
		},
		"default config -- all ranks": {
			cfg: genCfg(nil),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.testSysInfo == nil {
				tc.testSysInfo = defaultSystemInfo
			}
			daos_mgmt_get_sys_info_SystemInfo = tc.testSysInfo
			daos_mgmt_get_sys_info_RC = _Ctype_int(tc.get_sys_info_RC)
			defer func() {
				daos_mgmt_get_sys_info_SystemInfo = defaultSystemInfo
				daos_mgmt_get_sys_info_RC = 0
			}()

			run_self_test_RunConfig = nil
			run_self_test_MsEndpoints = nil
			run_self_test_EndpointLatencies = nil
			run_self_test_RC = _Ctype_int(tc.self_test_RC)
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var sysRanks []ranklist.Rank
			if len(tc.cfg.EndpointRanks) == 0 {
				sysRanks = make([]ranklist.Rank, len(tc.testSysInfo.RankURIs))
				for i, rankURI := range tc.testSysInfo.RankURIs {
					sysRanks[i] = ranklist.Rank(rankURI.Rank)
				}
			} else {
				sysRanks = tc.cfg.EndpointRanks
			}
			if tc.expRunCfg == nil {
				tc.expRunCfg = tc.cfg.Copy()
				tc.expRunCfg.EndpointRanks = sysRanks
				tc.expRunCfg.Repetitions = tc.cfg.Repetitions * uint(len(sysRanks))
			}
			if tc.expRunResults == nil {
				expCfg := tc.cfg.Copy()
				expCfg.EndpointRanks = sysRanks
				tc.expRunResults = genExpResults(expCfg)
			}
			tgtEps := genEndPoints(tc.cfg.EndpointTags, sysRanks...)
			run_self_test_EndpointLatencies = genEpLatencies(tc.cfg.Repetitions*uint(len(tgtEps)), tgtEps...)

			ctx := test.MustLogContext(t, log)
			res, err := RunSelfTest(ctx, tc.cfg)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}
			test.CmpAny(t, "SelfTestConfig", tc.expRunCfg, run_self_test_RunConfig)
			cmpOpts := cmp.Options{
				// Don't need to test all of this again. Just verify that
				// we get the expected number of latency results here.
				cmpopts.IgnoreTypes(daos.EndpointLatency{}),
			}
			test.CmpAny(t, "SelfTestResults", tc.expRunResults, res, cmpOpts...)
		})
	}
}

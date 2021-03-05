//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"errors"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	. "github.com/daos-stack/daos/src/control/system"
)

func mockRankGroups(t *testing.T) RankGroups {
	groups := make(RankGroups)

	rs1, err := CreateRankSet("0-9,11-19")
	if err != nil {
		t.Fatal(err)
	}
	groups["foo/OK"] = rs1

	rs2, err := CreateRankSet("10,20-299")
	if err != nil {
		t.Fatal(err)
	}
	groups["bar/BAD"] = rs2

	return groups
}

func TestPretty_tabulateRankGroups(t *testing.T) {
	mockColumnTitles := []string{"Ranks", "Action", "Result"}

	for name, tc := range map[string]struct {
		groups      RankGroups
		cTitles     []string
		expPrintStr string
		expErrMsg   string
	}{
		"formatted results": {
			groups:  mockRankGroups(t),
			cTitles: mockColumnTitles,
			expPrintStr: `
Ranks       Action Result 
-----       ------ ------ 
[10,20-299] bar    BAD    
[0-9,11-19] foo    OK     

`,
		},
		"column number mismatch": {
			groups:    mockRankGroups(t),
			cTitles:   []string{"Ranks", "SCM", "NVME", "???"},
			expErrMsg: "unexpected summary format",
		},
		"too few columns": {
			groups:    mockRankGroups(t),
			cTitles:   []string{"Ranks"},
			expErrMsg: "insufficient number of column titles",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder

			gotErr := tabulateRankGroups(&bld, tc.groups, tc.cTitles...)
			common.ExpectError(t, gotErr, tc.expErrMsg, name)

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintSystemQueryResp(t *testing.T) {
	for name, tc := range map[string]struct {
		resp        *control.SystemQueryResp
		absentHosts string
		absentRanks string
		verbose     bool
		expPrintStr string
	}{
		"empty response": {
			resp: &control.SystemQueryResp{},
			expPrintStr: `
Query matches no ranks in system
`,
		},
		"response with missing hosts and ranks": {
			resp:        &control.SystemQueryResp{},
			absentHosts: "foo[7,8,9]",
			absentRanks: "7-9",
			expPrintStr: `
Query matches no ranks in system
Unknown 3 hosts: foo[7-9]
Unknown 3 ranks: 7-9
`,
		},
		"single response": {
			resp: &control.SystemQueryResp{
				Members: Members{
					MockMember(t, 0, MemberStateJoined),
				},
			},
			expPrintStr: `
Rank 0
------
  address      : 127.0.0.0:10001                     
  uuid         : 00000000-0000-0000-0000-000000000000
  fault domain : /                                   
  status       : Joined                              
  reason       :                                     

`,
		},
		"single response with missing hosts and ranks": {
			resp: &control.SystemQueryResp{
				Members: Members{
					MockMember(t, 0, MemberStateJoined),
				},
			},
			absentHosts: "foo[7,8,9]",
			absentRanks: "7-9",
			expPrintStr: `
Rank 0
------
  address      : 127.0.0.0:10001                     
  uuid         : 00000000-0000-0000-0000-000000000000
  fault domain : /                                   
  status       : Joined                              
  reason       :                                     

Unknown 3 hosts: foo[7-9]
Unknown 3 ranks: 7-9
`,
		},
		"normal response": {
			resp: &control.SystemQueryResp{
				Members: Members{
					MockMember(t, 0, MemberStateJoined),
					MockMember(t, 1, MemberStateJoined),
					MockMember(t, 2, MemberStateStopped),
					MockMember(t, 3, MemberStateEvicted),
					MockMember(t, 4, MemberStateStopped),
					MockMember(t, 5, MemberStateJoined),
					MockMember(t, 6, MemberStateJoined),
				},
			},
			expPrintStr: `
Rank      State   
----      -----   
[0-1,5-6] Joined  
[2,4]     Stopped 
3         Evicted 

`,
		},
		"missing hosts": {
			resp: &control.SystemQueryResp{
				Members: Members{
					MockMember(t, 0, MemberStateJoined),
					MockMember(t, 1, MemberStateJoined),
					MockMember(t, 2, MemberStateStopped),
					MockMember(t, 3, MemberStateEvicted),
					MockMember(t, 4, MemberStateStopped),
					MockMember(t, 5, MemberStateJoined),
					MockMember(t, 6, MemberStateJoined),
				},
			},
			absentHosts: "foo[7,8,9]",
			expPrintStr: `
Rank      State   
----      -----   
[0-1,5-6] Joined  
[2,4]     Stopped 
3         Evicted 

Unknown 3 hosts: foo[7-9]
`,
		},
		"missing ranks": {
			resp: &control.SystemQueryResp{
				Members: Members{
					MockMember(t, 0, MemberStateJoined),
					MockMember(t, 1, MemberStateJoined),
					MockMember(t, 2, MemberStateStopped),
					MockMember(t, 3, MemberStateEvicted),
					MockMember(t, 4, MemberStateStopped),
					MockMember(t, 5, MemberStateJoined),
					MockMember(t, 6, MemberStateJoined),
				},
			},
			absentRanks: "7-9",
			expPrintStr: `
Rank      State        
----      -----        
[0-1,5-6] Joined       
[7-9]     Unknown Rank 
[2,4]     Stopped      
3         Evicted      

`,
		},
		"missing ranks and hosts": {
			resp: &control.SystemQueryResp{
				Members: Members{
					MockMember(t, 0, MemberStateJoined),
					MockMember(t, 1, MemberStateJoined),
					MockMember(t, 2, MemberStateStopped),
					MockMember(t, 3, MemberStateEvicted),
					MockMember(t, 4, MemberStateStopped),
					MockMember(t, 5, MemberStateJoined),
					MockMember(t, 6, MemberStateJoined),
				},
			},
			absentHosts: "foo[7,8,9]",
			absentRanks: "7-9",
			expPrintStr: `
Rank      State        
----      -----        
[0-1,5-6] Joined       
[7-9]     Unknown Rank 
[2,4]     Stopped      
3         Evicted      

Unknown 3 hosts: foo[7-9]
`,
		},
		"normal response verbose": {
			resp: &control.SystemQueryResp{
				Members: Members{
					MockMember(t, 0, MemberStateJoined),
					MockMember(t, 1, MemberStateJoined),
					MockMember(t, 2, MemberStateStopped),
					MockMember(t, 3, MemberStateEvicted),
					MockMember(t, 4, MemberStateStopped),
					MockMember(t, 5, MemberStateJoined),
					MockMember(t, 6, MemberStateJoined),
				},
			},
			verbose: true,
			expPrintStr: `
Rank UUID                                 Control Address Fault Domain State   Reason 
---- ----                                 --------------- ------------ -----   ------ 
0    00000000-0000-0000-0000-000000000000 127.0.0.0:10001 /            Joined         
1    11111111-1111-1111-1111-111111111111 127.0.0.1:10001 /            Joined         
2    22222222-2222-2222-2222-222222222222 127.0.0.2:10001 /            Stopped        
3    33333333-3333-3333-3333-333333333333 127.0.0.3:10001 /            Evicted        
4    44444444-4444-4444-4444-444444444444 127.0.0.4:10001 /            Stopped        
5    55555555-5555-5555-5555-555555555555 127.0.0.5:10001 /            Joined         
6    66666666-6666-6666-6666-666666666666 127.0.0.6:10001 /            Joined         

`,
		},
		"response verbose with missing hosts and ranks": {
			resp: &control.SystemQueryResp{
				Members: Members{
					MockMember(t, 0, MemberStateJoined),
					MockMember(t, 1, MemberStateJoined),
					MockMember(t, 2, MemberStateStopped),
					MockMember(t, 3, MemberStateEvicted),
					MockMember(t, 4, MemberStateStopped),
					MockMember(t, 5, MemberStateJoined),
					MockMember(t, 6, MemberStateJoined),
				},
			},
			absentHosts: "foo[7,8,9]",
			absentRanks: "7-9",
			verbose:     true,
			expPrintStr: `
Rank UUID                                 Control Address Fault Domain State   Reason 
---- ----                                 --------------- ------------ -----   ------ 
0    00000000-0000-0000-0000-000000000000 127.0.0.0:10001 /            Joined         
1    11111111-1111-1111-1111-111111111111 127.0.0.1:10001 /            Joined         
2    22222222-2222-2222-2222-222222222222 127.0.0.2:10001 /            Stopped        
3    33333333-3333-3333-3333-333333333333 127.0.0.3:10001 /            Evicted        
4    44444444-4444-4444-4444-444444444444 127.0.0.4:10001 /            Stopped        
5    55555555-5555-5555-5555-555555555555 127.0.0.5:10001 /            Joined         
6    66666666-6666-6666-6666-666666666666 127.0.0.6:10001 /            Joined         

Unknown 3 hosts: foo[7-9]
Unknown 3 ranks: 7-9
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.resp.AbsentRanks = *MustCreateRankSet(tc.absentRanks)
			tc.resp.AbsentHosts = *hostlist.MustCreateSet(tc.absentHosts)

			var bld strings.Builder
			// pass the same io writer to standard and error stream
			// parameters to mimic combined output seen on terminal
			if err := PrintSystemQueryResponse(&bld, &bld, tc.resp,
				PrintWithVerboseOutput(tc.verbose)); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintSystemStartResp(t *testing.T) {
	successResults := MemberResults{
		NewMemberResult(1, nil, MemberStateReady, "start"),
		NewMemberResult(2, nil, MemberStateReady, "start"),
		NewMemberResult(0, nil, MemberStateStopped, "start"),
		NewMemberResult(3, nil, MemberStateStopped, "start"),
	}
	failedResults := MemberResults{
		NewMemberResult(1, nil, MemberStateReady, "start"),
		NewMemberResult(2, errors.New("fail"), MemberStateReady, "start"),
		NewMemberResult(0, errors.New("failed"), MemberStateStopped, "start"),
		NewMemberResult(3, nil, MemberStateStopped, "start"),
	}

	for name, tc := range map[string]struct {
		resp        *control.SystemStartResp
		absentHosts string
		absentRanks string
		expPrintStr string
	}{
		"empty response": {
			resp: &control.SystemStartResp{},
			expPrintStr: `
No results returned
`,
		},
		"empty response with missing hosts and ranks": {
			resp:        &control.SystemStartResp{},
			absentHosts: "foo[7,8,9]",
			absentRanks: "7-9",
			expPrintStr: `
No results returned
Unknown 3 hosts: foo[7-9]
Unknown 3 ranks: 7-9
`,
		},
		"normal response": {
			resp: &control.SystemStartResp{
				Results: successResults,
			},
			expPrintStr: `
Rank  Operation Result 
----  --------- ------ 
[0-3] start     OK     

`,
		},
		"response with failures": {
			resp: &control.SystemStartResp{
				Results: failedResults,
			},
			expPrintStr: `
Rank  Operation Result 
----  --------- ------ 
[1,3] start     OK     
2     start     fail   
0     start     failed 

`,
		},
		"normal response with missing hosts and ranks": {
			resp: &control.SystemStartResp{
				Results: successResults,
			},
			absentHosts: "foo[7,8,9]",
			absentRanks: "7-9",
			expPrintStr: `
Rank  Operation Result       
----  --------- ------       
[0-3] start     OK           
[7-9] ----      Unknown Rank 

Unknown 3 hosts: foo[7-9]
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.resp.AbsentRanks = *MustCreateRankSet(tc.absentRanks)
			tc.resp.AbsentHosts = *hostlist.MustCreateSet(tc.absentHosts)

			var bld strings.Builder
			// pass the same io writer to standard and error stream
			// parameters to mimic combined output seen on terminal
			if err := PrintSystemStartResponse(&bld, &bld, tc.resp); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintSystemStopResp(t *testing.T) {
	successResults := MemberResults{
		NewMemberResult(1, nil, MemberStateReady, "stop"),
		NewMemberResult(2, nil, MemberStateReady, "stop"),
		NewMemberResult(0, nil, MemberStateStopped, "stop"),
		NewMemberResult(3, nil, MemberStateStopped, "stop"),
	}
	failedResults := MemberResults{
		NewMemberResult(1, nil, MemberStateReady, "stop"),
		NewMemberResult(2, errors.New("fail"), MemberStateReady, "stop"),
		NewMemberResult(0, errors.New("failed"), MemberStateStopped, "stop"),
		NewMemberResult(3, nil, MemberStateStopped, "stop"),
	}

	for name, tc := range map[string]struct {
		resp        *control.SystemStopResp
		absentHosts string
		absentRanks string
		expPrintStr string
	}{
		"empty response": {
			resp: &control.SystemStopResp{},
			expPrintStr: `
No results returned
`,
		},
		"empty response with missing hosts and ranks": {
			resp:        &control.SystemStopResp{},
			absentHosts: "foo[7,8,9]",
			absentRanks: "7-9",
			expPrintStr: `
No results returned
Unknown 3 hosts: foo[7-9]
Unknown 3 ranks: 7-9
`,
		},
		"normal response": {
			resp: &control.SystemStopResp{
				Results: successResults,
			},
			expPrintStr: `
Rank  Operation Result 
----  --------- ------ 
[0-3] stop      OK     

`,
		},
		"response with failures": {
			resp: &control.SystemStopResp{
				Results: failedResults,
			},
			expPrintStr: `
Rank  Operation Result 
----  --------- ------ 
[1,3] stop      OK     
2     stop      fail   
0     stop      failed 

`,
		},
		"normal response with missing hosts and ranks": {
			resp: &control.SystemStopResp{
				Results: successResults,
			},
			absentHosts: "foo[7,8,9]",
			absentRanks: "7-9",
			expPrintStr: `
Rank  Operation Result       
----  --------- ------       
[0-3] stop      OK           
[7-9] ----      Unknown Rank 

Unknown 3 hosts: foo[7-9]
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.resp.AbsentRanks = *MustCreateRankSet(tc.absentRanks)
			tc.resp.AbsentHosts = *hostlist.MustCreateSet(tc.absentHosts)

			var bld strings.Builder
			// pass the same io writer to standard and error stream
			// parameters to mimic combined output seen on terminal
			if err := PrintSystemStopResponse(&bld, &bld, tc.resp); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

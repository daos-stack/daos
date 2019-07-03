package main

import (
	"fmt"
	"regexp"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/security"
)

type dmgTestErr string

func (dte dmgTestErr) Error() string {
	return string(dte)
}

const (
	errMissingFlag = dmgTestErr("required flag")
)

type cmdTest struct {
	name          string
	cmd           string
	expectedCalls string
	expectedOpts  *cliOptions
	expectedErr   error
}

type testConn struct {
	t            *testing.T
	clientConfig *client.Configuration
	called       []string
}

func newTestConn(t *testing.T) *testConn {
	cfg := client.NewConfiguration()
	return &testConn{
		clientConfig: cfg,
		t:            t,
	}
}

func (tc *testConn) appendInvocation(name string) {
	tc.called = append(tc.called, name)
}

func (tc *testConn) ConnectClients(addrList client.Addresses) client.ResultMap {
	tc.appendInvocation("ConnectClients")

	return map[string]client.ClientResult{
		tc.clientConfig.HostList[0]: client.ClientResult{
			Address: tc.clientConfig.HostList[0],
		},
	}
}

func (tc *testConn) GetActiveConns(rm client.ResultMap) client.ResultMap {
	tc.appendInvocation("GetActiveConns")

	return map[string]client.ClientResult{
		tc.clientConfig.HostList[0]: client.ClientResult{
			Address: tc.clientConfig.HostList[0],
		},
	}
}

func (tc *testConn) ClearConns() client.ResultMap {
	tc.appendInvocation("ClearConns")
	return nil
}

func (tc *testConn) ScanStorage() (client.ClientCtrlrMap, client.ClientModuleMap) {
	tc.appendInvocation("ScanStorage")
	return nil, nil
}

func (tc *testConn) FormatStorage() (client.ClientCtrlrMap, client.ClientMountMap) {
	tc.appendInvocation("FormatStorage")
	return nil, nil
}

func (tc *testConn) UpdateStorage(req *pb.UpdateStorageReq) (client.ClientCtrlrMap, client.ClientModuleMap) {
	tc.appendInvocation(fmt.Sprintf("UpdateStorage-%s", req))
	return nil, nil
}

func (tc *testConn) ListFeatures() client.ClientFeatureMap {
	tc.appendInvocation("ListFeatures")
	return nil
}

func (tc *testConn) KillRank(uuid string, rank uint32) client.ResultMap {
	tc.appendInvocation(fmt.Sprintf("KillRank-uuid %s, rank %d", uuid, rank))
	return nil
}

func (tc *testConn) CreatePool(req *pb.CreatePoolReq) client.ResultMap {
	tc.appendInvocation(fmt.Sprintf("CreatePool-%s", req))
	return nil
}

func (tc *testConn) DestroyPool(req *pb.DestroyPoolReq) client.ResultMap {
	tc.appendInvocation(fmt.Sprintf("DestroyPool-%s", req))
	return nil
}

func (tc *testConn) SetTransportConfig(cfg *security.TransportConfig) {
	tc.appendInvocation("SetTransportConfig")
}

func runCmdTests(t *testing.T, cmdTests []cmdTest) {
	t.Helper()

	for _, st := range cmdTests {
		t.Run(st.name, func(t *testing.T) {
			defer common.ShowLogOnFailure(t)()
			t.Helper()

			conn := newTestConn(t)
			args := append([]string{"--insecure"}, strings.Split(st.cmd, " ")...)
			opts, err := parseOpts(args, conn)
			if err != st.expectedErr {
				if st.expectedErr == nil {
					t.Fatalf("expected nil error, got %+v", err)
				}

				errRe := regexp.MustCompile(st.expectedErr.Error())
				if !errRe.MatchString(err.Error()) {
					t.Fatalf("error string %q doesn't match expected error %q", err, st.expectedErr)
				}
			}
			if st.expectedCalls != "" {
				st.expectedCalls = fmt.Sprintf("SetTransportConfig %s", st.expectedCalls)
			}
			common.AssertEqual(t, strings.Join(conn.called, " "), st.expectedCalls,
				"called functions do not match expected calls")
			common.AssertEqual(t, opts, st.expectedOpts,
				"parsed options do not match expected options")
		})
	}
}

// FIXME: This does not fail as expected
/*func TestBadCommand(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	conn := newTestConn(t)
	opts, err := parseOpts([]string{"foo"}, conn)
	if err == nil {
		t.Fatal("expected error; got nil")
	}
}*/

//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

func TestControl_PoolAddCA_Append(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	rootPEM, rootKey := sectest.NewCA(t, "DAOS Test Root", nil, nil)
	poolCA, err := security.GeneratePoolCA(uuid.MustParse(test.MockUUID()), sectest.ParseCert(t, rootPEM), rootKey, 0)
	if err != nil {
		t.Fatal(err)
	}
	poolCAPEM := poolCA.CertPEM
	installed := &mgmt.PoolGetCAResp{PoolUuid: test.MockUUID(), CaBundle: poolCAPEM}
	var fullBundle []byte
	for i := 0; i < security.PoolCABundleMaxCerts; i++ {
		pem, _ := sectest.NewCA(t, "filler", nil, nil)
		fullBundle = append(fullBundle, pem...)
	}
	added := &mgmt.PoolAddCAResp{PoolUuid: test.MockUUID()}

	for name, tc := range map[string]struct {
		append    bool
		responses []*UnaryResponse
		expErr    error
	}{
		"enable on an enabled pool is refused": {
			responses: []*UnaryResponse{MockMSResponse("host1", nil, installed)},
			expErr:    ErrNodeAuthEnabled,
		},
		"append on an enabled pool proceeds": {
			append:    true,
			responses: []*UnaryResponse{MockMSResponse("host1", nil, installed), MockMSResponse("host1", nil, added)},
		},
		"append to a full bundle is refused": {
			append: true,
			responses: []*UnaryResponse{MockMSResponse("host1", nil, &mgmt.PoolGetCAResp{
				PoolUuid: test.MockUUID(), CaBundle: fullBundle,
			})},
			expErr: ErrPoolCABundleFull,
		},
		"enable fails when the CA query fails": {
			responses: []*UnaryResponse{MockMSResponse("host1", errors.New("ms unreachable"), nil)},
			expErr:    errors.New("ms unreachable"),
		},
		"enable on a pool with no CA proceeds": {
			responses: []*UnaryResponse{
				MockMSResponse("host1", nil, &mgmt.PoolGetCAResp{PoolUuid: test.MockUUID()}),
				MockMSResponse("host1", nil, added),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			mi := NewMockInvoker(log, &MockInvokerConfig{UnaryResponseSet: tc.responses})
			_, err := PoolAddCA(context.Background(), mi, &PoolAddCAReq{
				ID:      test.MockUUID(),
				CertPEM: poolCAPEM,
				Append:  tc.append,
			})
			if tc.expErr != nil {
				test.CmpErr(t, tc.expErr, err)
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
		})
	}
}

func TestControl_PoolIssueClientCerts(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	poolUUID := uuid.MustParse(test.MockUUID())
	rootPEM, rootKey := sectest.NewCA(t, "DAOS Test Root", nil, nil)
	poolCA, err := security.GeneratePoolCA(poolUUID, sectest.ParseCert(t, rootPEM), rootKey, 0)
	if err != nil {
		t.Fatal(err)
	}
	otherCA, err := security.GeneratePoolCA(poolUUID, sectest.ParseCert(t, rootPEM), rootKey, 0)
	if err != nil {
		t.Fatal(err)
	}
	stored := time.Now().Add(-time.Hour).Truncate(time.Second)
	wmBytes, err := security.EncodeCertWatermarks(security.CertWatermarks{
		security.PoolCertCNPrefixNode + "client01": stored,
	})
	if err != nil {
		t.Fatal(err)
	}
	caResp := func(bundle []byte) *UnaryResponse {
		return MockMSResponse("host1", nil, &mgmt.PoolGetCAResp{PoolUuid: test.MockUUID(), CaBundle: bundle})
	}
	wmResp := MockMSResponse("host1", nil, &mgmt.PoolGetCertWatermarksResp{PoolUuid: test.MockUUID(), Watermarks: wmBytes})
	revokedAt := time.Now().Truncate(time.Second)
	revoked := MockMSResponse("host1", nil, &mgmt.PoolRevokeClientResp{
		PoolUuid: test.MockUUID(), WatermarkRfc3339: revokedAt.Format(time.RFC3339),
	})
	issue := func(nodes []string, replace bool) *PoolIssueClientCertsReq {
		return &PoolIssueClientCertsReq{ID: "tank", CACert: poolCA.Cert, CAKey: poolCA.Key, Nodes: nodes, Replace: replace}
	}

	for name, tc := range map[string]struct {
		req          *PoolIssueClientCertsReq
		responses    []*UnaryResponse
		expErr       string
		expRPCs      int
		expNotBefore time.Time // issued NotBefore must be past this
	}{
		"nodes and tenants together": {
			req:    &PoolIssueClientCertsReq{ID: "tank", CACert: poolCA.Cert, CAKey: poolCA.Key, Nodes: []string{"a"}, Tenants: []string{"t"}},
			expErr: "mutually exclusive",
		},
		"replace with tenants": {
			req:    &PoolIssueClientCertsReq{ID: "tank", CACert: poolCA.Cert, CAKey: poolCA.Key, Tenants: []string{"t"}, Replace: true},
			expErr: "node certificates only",
		},
		"no CA": {
			req:    &PoolIssueClientCertsReq{ID: "tank", Nodes: []string{"a"}},
			expErr: "no pool CA",
		},
		"key of another CA": {
			req:    &PoolIssueClientCertsReq{ID: "tank", CACert: poolCA.Cert, CAKey: otherCA.Key, Nodes: []string{"a"}, Replace: true},
			expErr: "does not match",
		},
		"node auth disabled": {
			req:       issue([]string{"client01"}, false),
			responses: []*UnaryResponse{caResp(nil)},
			expErr:    "not enabled",
		},
		"CA not in the bundle": {
			req:       issue([]string{"client01"}, true),
			responses: []*UnaryResponse{caResp(otherCA.CertPEM), wmResp},
			expErr:    "not installed",
		},
		"issue postdates past the watermark": {
			req:          issue([]string{"client01"}, false),
			responses:    []*UnaryResponse{caResp(poolCA.CertPEM), wmResp},
			expRPCs:      2,
			expNotBefore: stored,
		},
		"replace revokes, then postdates past the new watermark": {
			req:          issue([]string{"client01"}, true),
			responses:    []*UnaryResponse{caResp(poolCA.CertPEM), wmResp, revoked},
			expRPCs:      3,
			expNotBefore: revokedAt,
		},
	} {
		t.Run(name, func(t *testing.T) {
			mi := NewMockInvoker(log, &MockInvokerConfig{UnaryResponseSet: tc.responses})
			certs, err := PoolIssueClientCerts(context.Background(), mi, tc.req)
			if tc.expErr != "" {
				test.CmpErr(t, errors.New(tc.expErr), err)
				// A refused request must not have revoked anything.
				test.AssertEqual(t, len(tc.responses), mi.invokeCount, "unexpected RPC count")
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			test.AssertEqual(t, tc.expRPCs, mi.invokeCount, "unexpected RPC count")
			if nb := certs[0].Cert.NotBefore; !nb.After(tc.expNotBefore) {
				t.Fatalf("NotBefore %s not past watermark %s", nb, tc.expNotBefore)
			}
		})
	}
}

func TestControl_GetPoolNodeAuthState(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	caPEM, _ := sectest.NewCA(t, "Pool CA", nil, nil)
	wm := security.CertWatermarks{security.PoolCertCNPrefixNode + "n1": time.Now().UTC().Truncate(time.Second)}
	wmBytes, err := security.EncodeCertWatermarks(wm)
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		responses  []*UnaryResponse
		expEnabled bool
		expWM      security.CertWatermarks
		expErr     error
		expRPCs    int
	}{
		"disabled pool reads no watermarks": {
			responses: []*UnaryResponse{
				MockMSResponse("host1", nil, &mgmt.PoolGetCAResp{PoolUuid: test.MockUUID()}),
			},
			expRPCs: 1,
		},
		"enabled pool reads both": {
			responses: []*UnaryResponse{
				MockMSResponse("host1", nil, &mgmt.PoolGetCAResp{PoolUuid: test.MockUUID(), CaBundle: caPEM}),
				MockMSResponse("host1", nil, &mgmt.PoolGetCertWatermarksResp{PoolUuid: test.MockUUID(), Watermarks: wmBytes}),
			},
			expEnabled: true,
			expWM:      wm,
			expRPCs:    2,
		},
		"watermark read failure is the caller's error": {
			responses: []*UnaryResponse{
				MockMSResponse("host1", nil, &mgmt.PoolGetCAResp{PoolUuid: test.MockUUID(), CaBundle: caPEM}),
				MockMSResponse("host1", errors.New("ms unreachable"), nil),
			},
			expErr: errors.New("ms unreachable"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			mi := NewMockInvoker(log, &MockInvokerConfig{UnaryResponseSet: tc.responses})
			state, err := GetPoolNodeAuthState(context.Background(), mi, test.MockUUID())
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}
			test.AssertEqual(t, tc.expRPCs, mi.invokeCount, "unexpected RPC count")
			test.AssertEqual(t, tc.expEnabled, state.Enabled(), "Enabled")
			if diff := cmp.Diff(tc.expWM, state.Watermarks); diff != "" {
				t.Fatalf("watermarks (-want, +got):\n%s", diff)
			}
		})
	}
}

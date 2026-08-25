//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"encoding/pem"
	"os"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

// testCACertPEM returns the PEM of a fresh self-signed CA cert.
func testCACertPEM(t *testing.T, cn string) []byte {
	t.Helper()
	certPEM, _ := sectest.NewCA(t, cn, nil, nil)
	return certPEM
}

// testRootedCA writes a DAOS root CA to a temp file and returns its path
// plus a pool CA PEM chained to it. All PoolAddCA tests need real
// provenance now that the install path always verifies the chain.
func testRootedCA(t *testing.T, poolUUID uuid.UUID) (string, []byte) {
	t.Helper()
	rootPath, pems := testRootedCAs(t, poolUUID)
	return rootPath, pems[0]
}

// testRootedCAs is testRootedCA for several pools under one root.
func testRootedCAs(t *testing.T, poolUUIDs ...uuid.UUID) (string, [][]byte) {
	t.Helper()
	rootPEM, rootKey := sectest.NewCA(t, "Test DAOS CA", nil, nil)
	rootCert := sectest.ParseCert(t, rootPEM)
	rootPath := t.TempDir() + "/daosCA.crt"
	if err := os.WriteFile(rootPath, rootPEM, 0644); err != nil {
		t.Fatal(err)
	}
	var pems [][]byte
	for _, id := range poolUUIDs {
		ca, err := security.GeneratePoolCA(id, rootCert, rootKey, 0)
		if err != nil {
			t.Fatal(err)
		}
		pems = append(pems, ca.CertPEM)
	}
	return rootPath, pems
}

// encodePropBytes builds a single-prop PoolGetPropResp for mock dRPC replies.
func encodePropBytes(propNum uint32, value []byte) *mgmtpb.PoolGetPropResp {
	return &mgmtpb.PoolGetPropResp{
		Properties: []*mgmtpb.PoolProperty{
			{
				Number: propNum,
				Value:  &mgmtpb.PoolProperty_Byteval{Byteval: value},
			},
		},
	}
}

func TestServer_MgmtSvc_PoolSetProp_RejectsCertProps(t *testing.T) {
	for name, propNum := range map[string]uint32{
		"ca_cert":         daos.PoolPropertyCACert,
		"cert_watermarks": daos.PoolPropertyCertWatermarks,
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			addTestPools(t, ms.sysdb, mockUUID)

			req := &mgmtpb.PoolSetPropReq{
				Sys: build.DefaultSystemName,
				Id:  mockUUID,
				Properties: []*mgmtpb.PoolProperty{
					{
						Number: propNum,
						Value: &mgmtpb.PoolProperty_Byteval{
							Byteval: []byte("anything"),
						},
					},
				},
			}
			_, err := ms.PoolSetProp(test.Context(t), req)
			if err == nil {
				t.Fatalf("expected error rejecting prop %d, got nil", propNum)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolAddCA(t *testing.T) {
	rootPath, pems := testRootedCAs(t, uuid.MustParse(mockUUID), test.MockPoolUUID(7))
	caPEM, otherPoolPEM := pems[0], pems[1]

	for name, tc := range map[string]struct {
		req        *mgmtpb.PoolAddCAReq
		existingCA []byte
		insecure   bool
		expErr     bool
		expEvicted int32
		expBundle  []byte // nil means don't check
	}{
		"empty cert rejected": {
			req: &mgmtpb.PoolAddCAReq{
				Sys:     build.DefaultSystemName,
				Id:      mockUUID,
				CertPem: nil,
			},
			expErr: true,
		},
		"not a CA rejected": {
			req: &mgmtpb.PoolAddCAReq{
				Sys: build.DefaultSystemName,
				Id:  mockUUID,
				CertPem: pem.EncodeToMemory(&pem.Block{
					Type: "CERTIFICATE", Bytes: []byte("junk"),
				}),
			},
			expErr: true,
		},
		"rejected without transport security": {
			req: &mgmtpb.PoolAddCAReq{
				Sys:     build.DefaultSystemName,
				Id:      mockUUID,
				CertPem: caPEM,
			},
			insecure: true,
			expErr:   true,
		},
		"CA for another pool rejected": {
			req: &mgmtpb.PoolAddCAReq{
				Sys:     build.DefaultSystemName,
				Id:      mockUUID,
				CertPem: otherPoolPEM,
			},
			expErr: true,
		},
		"append to empty bundle evicts": {
			req: &mgmtpb.PoolAddCAReq{
				Sys:     build.DefaultSystemName,
				Id:      mockUUID,
				CertPem: caPEM,
			},
			expBundle:  caPEM,
			expEvicted: 3,
		},
		"append to empty bundle with no_evict": {
			req: &mgmtpb.PoolAddCAReq{
				Sys:     build.DefaultSystemName,
				Id:      mockUUID,
				CertPem: caPEM,
				NoEvict: true,
			},
			expBundle: caPEM,
		},
		"append to existing bundle": {
			req: &mgmtpb.PoolAddCAReq{
				Sys:     build.DefaultSystemName,
				Id:      mockUUID,
				CertPem: caPEM,
			},
			existingCA: testCACertPEM(t, "Pre-existing CA"),
			expBundle:  nil, // verified via length below
		},
		"CA already in the bundle rejected": {
			req: &mgmtpb.PoolAddCAReq{
				Sys:     build.DefaultSystemName,
				Id:      mockUUID,
				CertPem: caPEM,
			},
			existingCA: caPEM,
			expErr:     true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			ms.transportConfig.CARootPath = rootPath
			ms.transportConfig.AllowInsecure = tc.insecure
			addTestPools(t, ms.sysdb, mockUUID)

			cfg := new(mockDrpcClientConfig)
			responses := []*mockDrpcResponse{
				{
					Status:  drpc.Status_SUCCESS,
					Message: encodePropBytes(daos.PoolPropertyCACert, tc.existingCA),
				},
				{
					Status:  drpc.Status_SUCCESS,
					Message: &mgmtpb.PoolSetPropResp{},
				},
			}
			if len(tc.existingCA) == 0 && !tc.req.NoEvict {
				responses = append(responses, &mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: &mgmtpb.PoolEvictResp{Count: 3},
				})
			}
			cfg.setSendMsgResponseList(t, responses...)
			mdc := newMockDrpcClient(cfg)
			setupSvcDrpcClient(ms, 0, mdc)

			resp, err := ms.PoolAddCA(test.Context(t), tc.req)
			if tc.expErr {
				if err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if resp.GetHandlesEvicted() != tc.expEvicted {
				t.Fatalf("handles evicted: got %d, want %d", resp.GetHandlesEvicted(), tc.expEvicted)
			}

			// Last dRPC is the bundle write (after bundle read).
			calls := mdc.calls.get()
			if len(calls) < 2 {
				t.Fatalf("expected at least 2 dRPC calls, got %d", len(calls))
			}
			setCall := new(mgmtpb.PoolSetPropReq)
			if err := unmarshalProto(calls[1].Body, setCall); err != nil {
				t.Fatal(err)
			}
			if len(setCall.Properties) != 1 {
				t.Fatalf("expected 1 property in setprop, got %d", len(setCall.Properties))
			}
			gotBundle := setCall.Properties[0].GetByteval()
			if tc.expBundle != nil {
				if string(gotBundle) != string(tc.expBundle) {
					t.Fatalf("unexpected bundle:\nwant: %q\ngot : %q", tc.expBundle, gotBundle)
				}
			} else {
				// existing + new
				want := append(append([]byte{}, tc.existingCA...), tc.req.CertPem...)
				if string(gotBundle) != string(want) {
					t.Fatalf("unexpected combined bundle:\nwant: %q\ngot : %q", want, gotBundle)
				}
			}
		})
	}
}

func TestServer_MgmtSvc_PoolAddCA_BundleCap(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	// Fill a bundle to the cap.
	var existing []byte
	for i := 0; i < security.PoolCABundleMaxCerts; i++ {
		existing = append(existing, testCACertPEM(t, "filler")...)
	}

	capRootPath, capPEM := testRootedCA(t, uuid.MustParse(mockUUID))
	ms := newTestMgmtSvc(t, log)
	ms.transportConfig.CARootPath = capRootPath
	addTestPools(t, ms.sysdb, mockUUID)
	cfg := new(mockDrpcClientConfig)
	cfg.setSendMsgResponseList(t,
		&mockDrpcResponse{
			Status:  drpc.Status_SUCCESS,
			Message: encodePropBytes(daos.PoolPropertyCACert, existing),
		},
	)
	setupSvcDrpcClient(ms, 0, newMockDrpcClient(cfg))

	_, err := ms.PoolAddCA(test.Context(t), &mgmtpb.PoolAddCAReq{
		Sys:     build.DefaultSystemName,
		Id:      mockUUID,
		CertPem: capPEM,
	})
	if err == nil {
		t.Fatal("expected cap-exceeded error")
	}
}

func TestServer_MgmtSvc_PoolAddCA_ChainValidation(t *testing.T) {
	daosCAPEM, daosCAKey := sectest.NewCA(t, "Test DAOS CA", nil, nil)
	tmpDir := t.TempDir()
	caCertPath := tmpDir + "/daosCA.crt"
	if err := os.WriteFile(caCertPath, daosCAPEM, 0644); err != nil {
		t.Fatal(err)
	}

	chained, err := security.GeneratePoolCA(uuid.MustParse(mockUUID), sectest.ParseCert(t, daosCAPEM), daosCAKey, 0)
	if err != nil {
		t.Fatal(err)
	}
	chainedPEM := chained.CertPEM
	unrelatedPEM := testCACertPEM(t, "Unrelated Pool CA")

	for name, tc := range map[string]struct {
		certPEM  []byte
		insecure bool
		noRoot   bool
		expErr   bool
	}{
		"chained CA accepted":            {certPEM: chainedPEM, expErr: false},
		"unrelated CA rejected":          {certPEM: unrelatedPEM, expErr: true},
		"unrelated CA rejected insecure": {certPEM: unrelatedPEM, insecure: true, expErr: true},
		"chained CA rejected insecure":   {certPEM: chainedPEM, insecure: true, expErr: true},
		"missing root path rejected":     {certPEM: chainedPEM, noRoot: true, expErr: true},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			ms.transportConfig.CARootPath = caCertPath
			if tc.noRoot {
				ms.transportConfig.CARootPath = ""
			}
			ms.transportConfig.AllowInsecure = tc.insecure
			addTestPools(t, ms.sysdb, mockUUID)

			cfg := new(mockDrpcClientConfig)
			cfg.setSendMsgResponseList(t,
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: encodePropBytes(daos.PoolPropertyCACert, nil),
				},
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: &mgmtpb.PoolSetPropResp{},
				},
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: &mgmtpb.PoolEvictResp{},
				},
			)
			mdc := newMockDrpcClient(cfg)
			setupSvcDrpcClient(ms, 0, mdc)

			_, err := ms.PoolAddCA(test.Context(t), &mgmtpb.PoolAddCAReq{
				Sys:     build.DefaultSystemName,
				Id:      mockUUID,
				CertPem: tc.certPEM,
			})
			if tc.expErr {
				if err == nil {
					t.Fatal("expected chain-validation error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolRemoveCA(t *testing.T) {
	caA := testCACertPEM(t, "CA A")
	caB := testCACertPEM(t, "CA B")
	bundle := append(append([]byte{}, caA...), caB...)
	infos, err := security.ParseCABundle(caA)
	if err != nil {
		t.Fatal(err)
	}
	fpA := infos[0].Fingerprint

	for name, tc := range map[string]struct {
		req       *mgmtpb.PoolRemoveCAReq
		bundle    []byte
		expErr    bool
		expRemain []byte
		expCount  int32
	}{
		"all and fingerprint both set rejected": {
			req: &mgmtpb.PoolRemoveCAReq{
				Sys: build.DefaultSystemName, Id: mockUUID,
				All: true, Fingerprint: "abc",
			},
			expErr: true,
		},
		"neither set rejected": {
			req: &mgmtpb.PoolRemoveCAReq{
				Sys: build.DefaultSystemName, Id: mockUUID,
			},
			expErr: true,
		},
		"remove all": {
			req: &mgmtpb.PoolRemoveCAReq{
				Sys: build.DefaultSystemName, Id: mockUUID,
				All: true,
			},
			bundle:    bundle,
			expCount:  2,
			expRemain: nil,
		},
		"removing the last CA rejected": {
			req: &mgmtpb.PoolRemoveCAReq{
				Sys: build.DefaultSystemName, Id: mockUUID,
				Fingerprint: fpA,
			},
			bundle: caA,
			expErr: true,
		},
		"remove by fingerprint": {
			req: &mgmtpb.PoolRemoveCAReq{
				Sys: build.DefaultSystemName, Id: mockUUID,
				Fingerprint: fpA,
			},
			bundle:    bundle,
			expRemain: caB,
			expCount:  1,
		},
		"fingerprint not found": {
			req: &mgmtpb.PoolRemoveCAReq{
				Sys: build.DefaultSystemName, Id: mockUUID,
				Fingerprint: "deadbeef",
			},
			bundle: bundle,
			expErr: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			addTestPools(t, ms.sysdb, mockUUID)

			cfg := new(mockDrpcClientConfig)
			cfg.setSendMsgResponseList(t,
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: encodePropBytes(daos.PoolPropertyCACert, tc.bundle),
				},
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: &mgmtpb.PoolSetPropResp{},
				},
			)
			mdc := newMockDrpcClient(cfg)
			setupSvcDrpcClient(ms, 0, mdc)

			resp, err := ms.PoolRemoveCA(test.Context(t), tc.req)
			if tc.expErr {
				if err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if resp.GetCertsRemoved() != tc.expCount {
				t.Fatalf("expected certs_removed=%d, got %d", tc.expCount, resp.GetCertsRemoved())
			}

			calls := mdc.calls.get()
			setCall := new(mgmtpb.PoolSetPropReq)
			if err := unmarshalProto(calls[len(calls)-1].Body, setCall); err != nil {
				t.Fatal(err)
			}
			gotBundle := setCall.Properties[0].GetByteval()
			if string(gotBundle) != string(tc.expRemain) {
				t.Fatalf("unexpected remaining bundle:\nwant: %q\ngot : %q", tc.expRemain, gotBundle)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolRevokeClient(t *testing.T) {
	caPEM := testCACertPEM(t, "Pool CA")

	for name, tc := range map[string]struct {
		cn           string
		existingWM   security.CertWatermarks
		caBundle     []byte
		expErr       bool
		expWatermark func(committed time.Time) error
		expPruned    []string
		expKept      []string
	}{
		"missing prefix rejected": {
			cn:       "foo",
			caBundle: caPEM,
			expErr:   true,
		},
		"empty suffix rejected": {
			cn:       "node:",
			caBundle: caPEM,
			expErr:   true,
		},
		"no pool CA rejected": {
			cn:       "node:n1",
			caBundle: nil,
			expErr:   true,
		},
		"fresh CN is dated skew ahead": {
			cn:       "node:n1",
			caBundle: caPEM,
			expWatermark: func(t time.Time) error {
				// Truncated to the second; allow the test's own elapsed time.
				earliest := time.Now().Add(security.DefaultCertMaxClockSkew - 2*time.Second)
				if t.Before(earliest) {
					return errors.Errorf("watermark %s is not skew ahead of now", t.Format(time.RFC3339))
				}
				return nil
			},
		},
		"existing CN advances monotonically": {
			cn:       "node:n1",
			caBundle: caPEM,
			existingWM: security.CertWatermarks{
				"node:n1": time.Now().Add(time.Hour).UTC().Truncate(time.Second),
			},
		},
		"watermarks older than every CA are pruned": {
			cn:       "node:n1",
			caBundle: caPEM,
			existingWM: security.CertWatermarks{
				"node:stale": time.Now().Add(-48 * time.Hour).UTC().Truncate(time.Second),
				"node:kept":  time.Now().Add(time.Hour).UTC().Truncate(time.Second),
			},
			expPruned: []string{"node:stale"},
			expKept:   []string{"node:kept"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			addTestPools(t, ms.sysdb, mockUUID)

			encoded, err := security.EncodeCertWatermarks(tc.existingWM)
			if err != nil {
				t.Fatal(err)
			}

			cfg := new(mockDrpcClientConfig)
			cfg.setSendMsgResponseList(t,
				// First read: pool CA bundle.
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: encodePropBytes(daos.PoolPropertyCACert, tc.caBundle),
				},
				// Second read: existing watermarks.
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: encodePropBytes(daos.PoolPropertyCertWatermarks, encoded),
				},
				// Write: updated watermarks.
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: &mgmtpb.PoolSetPropResp{},
				},
				// PoolEvict for the revoked CN.
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: &mgmtpb.PoolEvictResp{Count: 0},
				},
			)
			mdc := newMockDrpcClient(cfg)
			setupSvcDrpcClient(ms, 0, mdc)

			resp, err := ms.PoolRevokeClient(test.Context(t), &mgmtpb.PoolRevokeClientReq{
				Sys: build.DefaultSystemName,
				Id:  mockUUID,
				Cn:  tc.cn,
			})
			if tc.expErr {
				if err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			committed, err := time.Parse(time.RFC3339, resp.GetWatermarkRfc3339())
			if err != nil {
				t.Fatalf("parsing committed watermark: %v", err)
			}
			if prev, ok := tc.existingWM[tc.cn]; ok {
				if !committed.After(prev) {
					t.Fatalf("expected committed watermark %s to be after existing %s",
						committed.Format(time.RFC3339), prev.Format(time.RFC3339))
				}
			}
			if tc.expWatermark != nil {
				if err := tc.expWatermark(committed); err != nil {
					t.Fatal(err)
				}
			}

			// Validate the watermark write. The last call is PoolEvict; the
			// PoolSetProp for cert_watermarks is the one before it.
			calls := mdc.calls.get()
			setCall := new(mgmtpb.PoolSetPropReq)
			if err := unmarshalProto(calls[len(calls)-2].Body, setCall); err != nil {
				t.Fatal(err)
			}
			blob := setCall.Properties[0].GetByteval()
			wm, err := security.DecodeCertWatermarks(blob)
			if err != nil {
				t.Fatalf("decoding written blob: %v", err)
			}
			got, ok := wm[tc.cn]
			if !ok {
				t.Fatalf("written blob has no entry for %s", tc.cn)
			}
			if !got.Equal(committed) {
				t.Fatalf("written watermark %s != committed %s",
					got.Format(time.RFC3339), committed.Format(time.RFC3339))
			}
			for _, cn := range tc.expPruned {
				if _, ok := wm[cn]; ok {
					t.Errorf("stale watermark %s survived the write", cn)
				}
			}
			for _, cn := range tc.expKept {
				if _, ok := wm[cn]; !ok {
					t.Errorf("live watermark %s was dropped", cn)
				}
			}
		})
	}
}

func unmarshalProto(body []byte, msg proto.Message) error {
	return proto.Unmarshal(body, msg)
}

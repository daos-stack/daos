//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"crypto/x509"
	"encoding/pem"
	"os"
	"strings"
	"testing"
	"time"

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
		"pool_ca":         daos.PoolPropertyPoolCA,
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
	caPEM := testCACertPEM(t, "Pool CA 1")

	for name, tc := range map[string]struct {
		req        *mgmtpb.PoolAddCAReq
		existingCA []byte
		expErr     bool
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
		"append to empty bundle": {
			req: &mgmtpb.PoolAddCAReq{
				Sys:     build.DefaultSystemName,
				Id:      mockUUID,
				CertPem: caPEM,
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
					Message: encodePropBytes(daos.PoolPropertyPoolCA, tc.existingCA),
				},
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: &mgmtpb.PoolSetPropResp{},
				},
			)
			mdc := newMockDrpcClient(cfg)
			setupSvcDrpcClient(ms, 0, mdc)

			_, err := ms.PoolAddCA(test.Context(t), tc.req)
			if tc.expErr {
				if err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			// Last dRPC is the bundle write (after bundle read).
			calls := mdc.calls.get()
			if len(calls) < 2 {
				t.Fatalf("expected at least 2 dRPC calls, got %d", len(calls))
			}
			setCall := new(mgmtpb.PoolSetPropReq)
			if err := unmarshalProto(calls[len(calls)-1].Body, setCall); err != nil {
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
	for i := 0; i < poolCABundleMaxCerts; i++ {
		existing = append(existing, testCACertPEM(t, "filler")...)
	}

	ms := newTestMgmtSvc(t, log)
	addTestPools(t, ms.sysdb, mockUUID)
	cfg := new(mockDrpcClientConfig)
	cfg.setSendMsgResponseList(t,
		&mockDrpcResponse{
			Status:  drpc.Status_SUCCESS,
			Message: encodePropBytes(daos.PoolPropertyPoolCA, existing),
		},
	)
	setupSvcDrpcClient(ms, 0, newMockDrpcClient(cfg))

	_, err := ms.PoolAddCA(test.Context(t), &mgmtpb.PoolAddCAReq{
		Sys:     build.DefaultSystemName,
		Id:      mockUUID,
		CertPem: testCACertPEM(t, "one too many"),
	})
	if err == nil {
		t.Fatal("expected cap-exceeded error")
	}
}

// Engine returns NotSupported for byteval writes when the pool predates
// the byteval-props gate; surface it as an actionable "upgrade the pool" error.
func TestServer_MgmtSvc_PoolAddCA_PoolNeedsUpgrade(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ms := newTestMgmtSvc(t, log)
	addTestPools(t, ms.sysdb, mockUUID)
	cfg := new(mockDrpcClientConfig)
	cfg.setSendMsgResponseList(t,
		&mockDrpcResponse{
			Status:  drpc.Status_SUCCESS,
			Message: encodePropBytes(daos.PoolPropertyPoolCA, nil),
		},
		&mockDrpcResponse{
			Status: drpc.Status_SUCCESS,
			Message: &mgmtpb.PoolSetPropResp{
				Status: int32(daos.NotSupported),
			},
		},
	)
	setupSvcDrpcClient(ms, 0, newMockDrpcClient(cfg))

	_, err := ms.PoolAddCA(test.Context(t), &mgmtpb.PoolAddCAReq{
		Sys:     build.DefaultSystemName,
		Id:      mockUUID,
		CertPem: testCACertPEM(t, "ca"),
	})
	if err == nil {
		t.Fatal("expected upgrade-required error")
	}
	msg := err.Error()
	if !strings.Contains(msg, "pool must be upgraded") ||
		!strings.Contains(msg, "dmg pool upgrade") {
		t.Fatalf("unexpected error message: %q", msg)
	}
}

func TestServer_MgmtSvc_PoolAddCA_ChainValidation(t *testing.T) {
	daosCAPEM, daosCAKey := sectest.NewCA(t, "Test DAOS CA", nil, nil)
	daosCACert, err := func() (*x509.Certificate, error) {
		block, _ := pem.Decode(daosCAPEM)
		return x509.ParseCertificate(block.Bytes)
	}()
	if err != nil {
		t.Fatal(err)
	}

	tmpDir := t.TempDir()
	caCertPath := tmpDir + "/daosCA.crt"
	if err := os.WriteFile(caCertPath, daosCAPEM, 0644); err != nil {
		t.Fatal(err)
	}

	chainedPEM, _ := sectest.NewCA(t, "Chained Pool CA", daosCACert, daosCAKey)
	unrelatedPEM := testCACertPEM(t, "Unrelated Pool CA")

	for name, tc := range map[string]struct {
		certPEM []byte
		expErr  bool
	}{
		"chained CA accepted":   {certPEM: chainedPEM, expErr: false},
		"unrelated CA rejected": {certPEM: unrelatedPEM, expErr: true},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			ms.daosCARootPath = caCertPath
			addTestPools(t, ms.sysdb, mockUUID)

			cfg := new(mockDrpcClientConfig)
			cfg.setSendMsgResponseList(t,
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: encodePropBytes(daos.PoolPropertyPoolCA, nil),
				},
				&mockDrpcResponse{
					Status:  drpc.Status_SUCCESS,
					Message: &mgmtpb.PoolSetPropResp{},
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
	fpA := sectest.CertFingerprint(t, caA)

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
			expRemain: nil,
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
			// Remove-all path writes immediately; other paths read first then write.
			if tc.req.GetAll() {
				cfg.setSendMsgResponseList(t,
					&mockDrpcResponse{
						Status: drpc.Status_SUCCESS, Message: &mgmtpb.PoolSetPropResp{},
					},
				)
			} else {
				cfg.setSendMsgResponseList(t,
					&mockDrpcResponse{
						Status:  drpc.Status_SUCCESS,
						Message: encodePropBytes(daos.PoolPropertyPoolCA, tc.bundle),
					},
					&mockDrpcResponse{
						Status:  drpc.Status_SUCCESS,
						Message: &mgmtpb.PoolSetPropResp{},
					},
				)
			}
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
			if resp.GetCertsRemoved() != tc.expCount && !tc.req.GetAll() {
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
		"fresh CN": {
			cn:       "node:n1",
			caBundle: caPEM,
			expWatermark: func(t time.Time) error {
				if t.IsZero() {
					return errors.New("expected non-zero timestamp")
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
			expWatermark: func(committed time.Time) error {
				// Must be strictly greater than the existing watermark.
				return nil
			},
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
					Message: encodePropBytes(daos.PoolPropertyPoolCA, tc.caBundle),
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
		})
	}
}

func unmarshalProto(body []byte, msg proto.Message) error {
	return proto.Unmarshal(body, msg)
}

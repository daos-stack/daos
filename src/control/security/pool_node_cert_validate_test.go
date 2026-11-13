//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"encoding/pem"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common/test"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

func TestSecurity_NodeCertPresentation_Validate(t *testing.T) {
	f := newCertFixture(t)
	nodeCN := PoolCertCNPrefixNode + testMachineName
	now := time.Now()

	for name, tc := range map[string]struct {
		cn     string
		setup  func(t *testing.T, v *NodeCertPresentation)
		expErr error
	}{
		"node cert": {},
		"tenant cert": {
			cn: PoolCertCNPrefixTenant + "team-a",
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.MachineName = "someone-else"
			},
		},
		"nil root": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.Root = nil
			},
			expErr: ErrInvalidInput,
		},
		"garbage cert": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.Cert = []byte("not a PEM")
			},
			expErr: ErrCertInvalid,
		},
		"empty pool CA bundle": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.PoolCA = nil
			},
			expErr: ErrCertInvalid,
		},
		"other root": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				other := newCertFixture(t)
				v.PoolCA = other.poolCA.CertPEM
				f.present(t, v, other.leaf(t, nodeCN), v.PoPPayload)
			},
			expErr: ErrCertInvalid,
		},
		"wrong key type": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				p256, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
				if err != nil {
					t.Fatal(err)
				}
				v.Cert = sectest.NewLeafForKey(t, nodeCN, p256, f.poolCA.Cert, f.poolCA.Key)
			},
			expErr: ErrCertInvalid,
		},
		"no CN prefix": {
			cn:     "no-prefix-here",
			expErr: ErrCertInvalid,
		},
		"wrong machine": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.MachineName = "machine2"
			},
			expErr: ErrCertInvalid,
		},
		"no machine name": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.MachineName = ""
			},
			expErr: ErrInvalidInput,
		},
		"not yet valid within skew": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				f.present(t, v, f.issue(t, nodeCN, now.Add(time.Minute), now.Add(time.Hour)), v.PoPPayload)
			},
		},
		"not yet valid beyond skew": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				f.present(t, v, f.issue(t, nodeCN, now.Add(DefaultCertMaxClockSkew+time.Minute), now.Add(time.Hour)), v.PoPPayload)
			},
			expErr: ErrCertNotYetValid,
		},
		"expired": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				f.present(t, v, f.issue(t, nodeCN, now.Add(-time.Hour), now.Add(-time.Minute)), v.PoPPayload)
			},
			expErr: ErrCertInvalid,
		},
		"unreadable watermarks": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.Watermarks = []byte("{not json")
			},
			expErr: ErrWatermarksInvalid,
		},
		"revoked": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.Watermarks = watermarksJSON(t, CertWatermarks{nodeCN: now.Add(time.Minute)})
			},
			expErr: ErrCertRevoked,
		},
		"reissued past watermark": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.Watermarks = watermarksJSON(t, CertWatermarks{nodeCN: now.Add(-time.Hour)})
			},
		},
		"garbage payload": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				f.present(t, v, f.leaf(t, nodeCN), []byte{0xff, 0xff, 0xff, 0xff})
			},
			expErr: ErrInvalidInput,
		},
		"short handle UUID": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				payload := marshalPoP(t, &PoPPayload{
					PoolUuid:   f.poolUUID[:],
					HandleUuid: []byte{0x01, 0x02},
					Timestamp:  now.Unix(),
				})
				f.present(t, v, f.leaf(t, nodeCN), payload)
			},
			expErr: ErrInvalidInput,
		},
		"other pool": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.PoolUUID = uuid.MustParse("99999999-9999-9999-9999-999999999999")
			},
			expErr: ErrPoPInvalid,
		},
		"stale timestamp": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.Now = now.Add(DefaultCertMaxClockSkew + time.Minute)
			},
			expErr: ErrPoPStale,
		},
		"tampered payload": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				v.PoPPayload[len(v.PoPPayload)-1] ^= 0xff
			},
			expErr: ErrPoPInvalid,
		},
		"signed by other key": {
			setup: func(t *testing.T, v *NodeCertPresentation) {
				pop, err := signPoP(f.leaf(t, nodeCN).Key, v.PoPPayload)
				if err != nil {
					t.Fatal(err)
				}
				v.PoPSig = pop
			},
			expErr: ErrPoPInvalid,
		},
	} {
		t.Run(name, func(t *testing.T) {
			cn := tc.cn
			if cn == "" {
				cn = nodeCN
			}
			v := f.presentation(t, f.leaf(t, cn))
			if tc.setup != nil {
				tc.setup(t, v)
			}

			payload, err := v.Validate()
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, f.poolUUID, payload.PoolID(), "pool UUID")
			test.AssertEqual(t, f.handle, payload.HandleID(), "handle UUID")
		})
	}
}

func TestSecurity_checkCertValidity(t *testing.T) {
	f := newCertFixture(t)
	now := time.Now()
	cn := PoolCertCNPrefixNode + "m"

	for name, tc := range map[string]struct {
		notBefore time.Time
		notAfter  time.Time
		expErr    error
	}{
		"in window":           {notBefore: now.Add(-time.Hour), notAfter: now.Add(time.Hour)},
		"expired":             {notBefore: now.Add(-2 * time.Hour), notAfter: now.Add(-time.Hour), expErr: ErrCertInvalid},
		"not yet valid":       {notBefore: now.Add(time.Hour), notAfter: now.Add(2 * time.Hour), expErr: ErrCertNotYetValid},
		"within skew":         {notBefore: now.Add(time.Minute), notAfter: now.Add(time.Hour)},
		"expired within skew": {notBefore: now.Add(-time.Hour), notAfter: now.Add(-time.Minute), expErr: ErrCertInvalid},
	} {
		t.Run(name, func(t *testing.T) {
			cert := f.issue(t, cn, tc.notBefore, tc.notAfter).Cert
			test.CmpErr(t, tc.expErr, checkCertValidity(cert, now, DefaultCertMaxClockSkew))
		})
	}
}

func TestSecurity_VerifyNodeCertChain(t *testing.T) {
	f := newCertFixture(t)
	now := time.Now()
	cn := PoolCertCNPrefixNode + "m"
	leaf := f.leaf(t, cn).Cert
	otherRoot := newCertFixture(t).root
	direct, err := issueClientCert(cn, f.root, f.rootKey, now.Add(-time.Minute), now.Add(time.Hour))
	if err != nil {
		t.Fatal(err)
	}
	notACert := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte("not a certificate")})

	for name, tc := range map[string]struct {
		leaf   *x509.Certificate
		root   *x509.Certificate
		bundle []byte
		expErr error
	}{
		"via pool CA":           {leaf: leaf, root: f.root, bundle: f.poolCA.CertPEM},
		"signed by DAOS CA":     {leaf: direct.Cert, root: f.root, bundle: f.poolCA.CertPEM, expErr: ErrCertInvalid},
		"wrong root":            {leaf: leaf, root: otherRoot, bundle: f.poolCA.CertPEM, expErr: ErrCertInvalid},
		"no root":               {leaf: leaf, bundle: f.poolCA.CertPEM, expErr: ErrInvalidInput},
		"empty bundle":          {leaf: leaf, root: f.root, expErr: ErrCertInvalid},
		"malformed cert bundle": {leaf: leaf, root: f.root, bundle: append(notACert, f.poolCA.CertPEM...), expErr: ErrCertInvalid},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, VerifyNodeCertChain(tc.leaf, tc.root, tc.bundle, now))
		})
	}
}

func TestSecurity_CheckCertRevocation(t *testing.T) {
	f := newCertFixture(t)
	cn := PoolCertCNPrefixNode + "m"
	notBefore := time.Now().Add(-time.Hour)
	leaf := f.issue(t, cn, notBefore, notBefore.Add(2*time.Hour)).Cert

	for name, tc := range map[string]struct {
		watermarks CertWatermarks
		expErr     error
	}{
		"no watermarks":      {},
		"other identity":     {watermarks: CertWatermarks{PoolCertCNPrefixNode + "other": notBefore.Add(time.Hour)}},
		"older than cert":    {watermarks: CertWatermarks{cn: notBefore.Add(-time.Minute)}},
		"equal to NotBefore": {watermarks: CertWatermarks{cn: notBefore}, expErr: ErrCertRevoked},
		"newer than cert":    {watermarks: CertWatermarks{cn: notBefore.Add(time.Minute)}, expErr: ErrCertRevoked},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, CheckCertRevocation(leaf, cn, tc.watermarks))
		})
	}
}

func TestSecurity_VerifyCertKey(t *testing.T) {
	f := newCertFixture(t)
	ic := f.leaf(t, PoolCertCNPrefixNode+"m")
	other := f.leaf(t, PoolCertCNPrefixNode+"m")

	for name, tc := range map[string]struct {
		key    crypto.PrivateKey
		expErr error
	}{
		"matching key": {key: ic.Key},
		"other key":    {key: other.Key, expErr: ErrCertInvalid},
		"not a signer": {key: "not a key", expErr: ErrInvalidInput},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, VerifyCertKey(tc.key, ic.Cert))
		})
	}
}

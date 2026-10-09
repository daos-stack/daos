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
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"os"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
)

// DefaultClientCertValidity is the client certificate lifetime when none is requested.
const DefaultClientCertValidity = 365 * 24 * time.Hour

// IssuedCert is a freshly minted certificate and its private key.
type IssuedCert struct {
	Cert    *x509.Certificate
	Key     crypto.Signer
	CertPEM []byte
	KeyPEM  []byte
}

// Write stores the certificate (mode 0644) and its key (mode 0400),
// replacing any existing files.
func (c *IssuedCert) Write(certPath, keyPath string) error {
	for _, p := range []string{certPath, keyPath} {
		if err := os.Remove(p); err != nil && !os.IsNotExist(err) {
			return errors.Wrapf(err, "removing %s", p)
		}
	}
	if err := os.WriteFile(certPath, c.CertPEM, 0644); err != nil {
		return errors.Wrap(err, "writing certificate")
	}
	if err := os.WriteFile(keyPath, c.KeyPEM, 0400); err != nil {
		if rmErr := os.Remove(certPath); rmErr != nil {
			return errors.Wrapf(err, "writing private key (%s left behind: %s)", certPath, rmErr)
		}
		return errors.Wrap(err, "writing private key")
	}
	return nil
}

// clipNotAfter bounds a lifetime to the signer's own NotAfter; a certificate
// cannot verify past its issuer. validity <= 0 means the signer's full term.
func clipNotAfter(notBefore time.Time, validity time.Duration, signer *x509.Certificate) time.Time {
	if validity <= 0 || notBefore.Add(validity).After(signer.NotAfter) {
		return signer.NotAfter
	}
	return notBefore.Add(validity)
}

// GeneratePoolCA mints the intermediate CA for poolUUID, signed by the DAOS CA.
func GeneratePoolCA(poolUUID uuid.UUID, daosCACert *x509.Certificate, daosCAKey crypto.PrivateKey, validity time.Duration) (*IssuedCert, error) {
	now := time.Now()
	tmpl := &x509.Certificate{
		Subject: pkix.Name{
			CommonName:   PoolCACommonName(poolUUID),
			Organization: []string{"DAOS"},
		},
		NotBefore:             now.Add(-DefaultCertMaxClockSkew),
		NotAfter:              clipNotAfter(now, validity, daosCACert),
		KeyUsage:              x509.KeyUsageCertSign | x509.KeyUsageCRLSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
		MaxPathLen:            0,
		MaxPathLenZero:        true,
	}
	ic, err := issue(tmpl, daosCACert, daosCAKey)
	return ic, errors.Wrap(err, "pool CA")
}

// issueClientCert mints a client certificate for cn under the pool CA.
func issueClientCert(cn string, caCert *x509.Certificate, caKey crypto.PrivateKey, notBefore, notAfter time.Time) (*IssuedCert, error) {
	tmpl := &x509.Certificate{
		Subject: pkix.Name{
			CommonName:   cn,
			Organization: []string{"DAOS"},
		},
		NotBefore:   notBefore,
		NotAfter:    notAfter,
		KeyUsage:    x509.KeyUsageDigitalSignature,
		ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}
	ic, err := issue(tmpl, caCert, caKey)
	return ic, errors.Wrapf(err, "client cert %q", cn)
}

func issue(tmpl, parent *x509.Certificate, parentKey crypto.PrivateKey) (*IssuedCert, error) {
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		return nil, errors.Wrap(err, "generating key")
	}
	tmpl.SerialNumber, err = rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return nil, errors.Wrap(err, "generating serial number")
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, parent, &key.PublicKey, parentKey)
	if err != nil {
		return nil, errors.Wrap(err, "creating certificate")
	}
	cert, err := x509.ParseCertificate(der)
	if err != nil {
		return nil, errors.Wrap(err, "parsing created certificate")
	}
	keyDER, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		return nil, errors.Wrap(err, "marshaling key")
	}
	return &IssuedCert{
		Cert:    cert,
		Key:     key,
		CertPEM: pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}),
		KeyPEM:  pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER}),
	}, nil
}

// LoadCertAndKey loads a PEM certificate and its private key.
func LoadCertAndKey(certPath, keyPath string) (*x509.Certificate, crypto.PrivateKey, error) {
	cert, err := LoadCertificate(certPath)
	if err != nil {
		return nil, nil, errors.Wrap(err, "loading certificate")
	}
	key, err := LoadPrivateKey(keyPath)
	if err != nil {
		return nil, nil, errors.Wrap(err, "loading private key")
	}
	return cert, key, nil
}

// ClientCert is an issued client certificate and the identity it names.
type ClientCert struct {
	Name string // node or tenant name, without prefix
	CN   string
	*IssuedCert
}

// IssueClientCertsReq names the identities to issue certificates for.
type IssueClientCertsReq struct {
	CACert  *x509.Certificate
	CAKey   crypto.PrivateKey
	Nodes   []string // CN = node:<name>
	Tenants []string // CN = tenant:<name>; mutually exclusive with Nodes

	Watermarks CertWatermarks
	Validity   time.Duration // zero means DefaultClientCertValidity
}

// Validate checks the requested identities.
func (req *IssueClientCertsReq) Validate() error {
	if len(req.Nodes) == 0 && len(req.Tenants) == 0 {
		return errors.New("specify Nodes or Tenants")
	}
	if len(req.Nodes) > 0 && len(req.Tenants) > 0 {
		return errors.New("Nodes and Tenants are mutually exclusive")
	}
	for _, n := range req.Nodes {
		// The agent reports the short host name; a dotted name can never match.
		if strings.Contains(n, ".") {
			return errors.Errorf("node name %q must be a short host name", n)
		}
		if _, _, err := ParsePoolCertCN(PoolCertCNPrefixNode + n); err != nil {
			return err
		}
	}
	for _, t := range req.Tenants {
		if _, _, err := ParsePoolCertCN(PoolCertCNPrefixTenant + t); err != nil {
			return err
		}
	}
	return nil
}

// IssueClientCerts mints one certificate per requested identity.
func IssueClientCerts(req *IssueClientCertsReq) ([]*ClientCert, error) {
	if err := req.Validate(); err != nil {
		return nil, err
	}
	if req.CACert == nil || req.CAKey == nil {
		return nil, errors.New("no pool CA to sign with")
	}
	if err := VerifyCertKey(req.CAKey, req.CACert); err != nil {
		return nil, errors.Wrap(err, "pool CA")
	}
	validity := req.Validity
	if validity <= 0 {
		validity = DefaultClientCertValidity
	}

	type target struct{ name, cn string }
	var targets []target
	for _, n := range req.Nodes {
		targets = append(targets, target{n, PoolCertCNPrefixNode + n})
	}
	for _, t := range req.Tenants {
		targets = append(targets, target{t, PoolCertCNPrefixTenant + t})
	}

	// Second precision matches the RFC3339 watermark and the ASN.1 NotBefore;
	// a finer comparison can mint NotBefore == watermark, which is revoked.
	now := time.Now().UTC().Truncate(time.Second)
	var out []*ClientCert
	for _, tgt := range targets {
		notBefore := advanceCertWatermark(req.Watermarks, tgt.cn, now)
		ic, err := issueClientCert(tgt.cn, req.CACert, req.CAKey,
			notBefore, clipNotAfter(notBefore, validity, req.CACert))
		if err != nil {
			return nil, err
		}
		out = append(out, &ClientCert{Name: tgt.name, CN: tgt.cn, IssuedCert: ic})
	}
	return out, nil
}

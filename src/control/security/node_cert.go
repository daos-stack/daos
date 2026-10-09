//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"bytes"
	"crypto"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"sync"
	"syscall"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

// DefaultCertMaxClockSkew is the clock skew tolerated on certificate
// NotBefore and proof-of-possession timestamps.
const DefaultCertMaxClockSkew = 5 * time.Minute

// NodeCert is a loaded per-pool node certificate and key.
type NodeCert struct {
	PEM  []byte
	Cert *x509.Certificate
	Key  crypto.PrivateKey
}

// NodeCertLoader loads per-pool node certs from disk.
type NodeCertLoader struct {
	certsDir   string
	certsCache sync.Map
}

// NewNodeCertLoader returns a loader that reads <pool_uuid>.{crt,key} from dir.
func NewNodeCertLoader(dir string) *NodeCertLoader {
	return &NodeCertLoader{certsDir: dir}
}

// NodeCertPaths returns the paths of a pool's node certificate and key in dir.
func NodeCertPaths(dir string, poolUUID uuid.UUID) (certPath, keyPath string) {
	return filepath.Join(dir, poolUUID.String()+".crt"), filepath.Join(dir, poolUUID.String()+".key")
}

// PoolCAPaths returns the paths of a pool's CA certificate and key in dir.
func PoolCAPaths(dir string, poolUUID uuid.UUID) (certPath, keyPath string) {
	return filepath.Join(dir, poolUUID.String()+"_ca.crt"), filepath.Join(dir, poolUUID.String()+"_ca.key")
}

// DefaultPoolCADir returns the default pool CA directory.
func DefaultPoolCADir(adminKeyPath string) string {
	return filepath.Join(filepath.Dir(adminKeyPath), "pools")
}

// DefaultDAOSCAKeyPath returns the DAOS CA private key path implied by the
// DAOS CA certificate path.
func DefaultDAOSCAKeyPath(caCertPath string) string {
	return filepath.Join(filepath.Dir(caCertPath), "daosCA.key")
}

// fileSig is a simple signature of a file's contents based on its size and
// modification time. It is used to detect changes to a file without reading it.
type fileSig struct {
	mtime time.Time
	size  int64
}

// cachedNodeCert is a node cert and its file signatures, used to detect changes
// to the cert and key files on disk.
type cachedNodeCert struct {
	cert    *NodeCert
	certSig fileSig
	keySig  fileSig
}

func statSig(path string) (fileSig, error) {
	fi, err := os.Stat(path)
	if err != nil {
		return fileSig{}, err
	}
	return fileSig{mtime: fi.ModTime(), size: fi.Size()}, nil
}

// Load returns the cert+key struct for poolUUID. If a struct was previously
// loaded and the files have not changed, it is returned from cache.
func (l *NodeCertLoader) Load(log logging.Logger, poolUUID uuid.UUID) (*NodeCert, error) {
	uuidStr := poolUUID.String()
	certPath, keyPath := NodeCertPaths(l.certsDir, poolUUID)

	certSig, err := statSig(certPath)
	if errors.Is(err, os.ErrNotExist) {
		return nil, errors.Wrapf(ErrNodeCertNotDeployed, "pool %s", uuidStr)
	} else if err != nil {
		return nil, errors.Wrap(err, "stat node certificate")
	}
	keySig, err := statSig(keyPath)
	if err != nil {
		return nil, errors.Wrap(err, "stat node private key")
	}

	if v, ok := l.certsCache.Load(uuidStr); ok {
		c := v.(*cachedNodeCert)
		if c.certSig == certSig && c.keySig == keySig {
			return c.cert, nil
		}
		log.Debugf("node cert file for pool %s changed on disk; reloading", uuidStr)
	}

	insp := inspectNodeCertFiles(l.certsDir, poolUUID)
	if err := insp.filesErr(); err != nil {
		return nil, err
	}

	nc := &NodeCert{PEM: insp.PEM, Cert: insp.Certificate, Key: insp.PrivateKey}
	l.certsCache.Store(uuidStr, &cachedNodeCert{cert: nc, certSig: certSig, keySig: keySig})

	log.Debugf("loaded node cert for pool %s: CN=%s, expires=%s",
		uuidStr, nc.Cert.Subject.CommonName, nc.Cert.NotAfter.Format(time.RFC3339))

	return nc, nil
}

// VerifyCertKey verifies that the key is the private half of cert's public key.
func VerifyCertKey(key crypto.PrivateKey, cert *x509.Certificate) error {
	signer, ok := key.(crypto.Signer)
	if !ok {
		return errors.Wrapf(ErrInvalidInput, "unsupported private key type %T", key)
	}
	pub, ok := signer.Public().(interface{ Equal(crypto.PublicKey) bool })
	if !ok || !pub.Equal(cert.PublicKey) {
		return errors.Wrap(ErrCertInvalid, "private key does not match certificate")
	}
	return nil
}

// NodeCertFile is one file of a node cert deployment as found on disk.
type NodeCertFile struct {
	Path  string
	Mode  os.FileMode // zero when absent
	Owner string      // "" when absent
	Err   error       // missing, unreadable, over-permissive, malformed, or mismatched
}

// NodeCertInspection examines a pool's node cert deployment without stopping
// at the first problem, so each step can be reported. The runtime path uses
// the same steps: Load is the file half and CertAndPoP the policy half.
type NodeCertInspection struct {
	Dir  NodeCertFile
	Cert NodeCertFile
	Key  NodeCertFile

	PEM         []byte
	Certificate *x509.Certificate // nil unless the cert file parsed
	PrivateKey  crypto.PrivateKey // nil unless the key file loaded

	CN       string
	Prefix   string
	Suffix   string
	CNErr    error // ParsePoolCertCN
	Binding  error // CheckCNBinding; nil for tenant certs
	Validity error // CheckCertValidity at the inspection time
}

// InspectNodeCert examines the deployment for poolUUID in dir as the agent
// on machineName would use it at now, tolerating skew on NotBefore.
func InspectNodeCert(dir string, poolUUID uuid.UUID, machineName string, now time.Time, skew time.Duration) *NodeCertInspection {
	insp := inspectNodeCertFiles(dir, poolUUID)
	if insp.Certificate != nil {
		insp.checkPolicy(machineName, now, skew)
	}
	return insp
}

// checkPolicy validates the CN, its binding to machineName, and the validity window at now.
func (insp *NodeCertInspection) checkPolicy(machineName string, now time.Time, skew time.Duration) {
	insp.CN = insp.Certificate.Subject.CommonName
	insp.Prefix, insp.Suffix, insp.CNErr = ParsePoolCertCN(insp.CN)
	if insp.CNErr == nil {
		insp.Binding = CheckCNBinding(insp.Prefix, insp.Suffix, machineName)
	}
	insp.Validity = CheckCertValidity(insp.Certificate, now, skew)
}

// Deployed reports whether a certificate file is present at all.
func (insp *NodeCertInspection) Deployed() bool {
	return !errors.Is(insp.Cert.Err, os.ErrNotExist) && !errors.Is(insp.Dir.Err, os.ErrNotExist)
}

// Err returns the first problem the agent would hit using this deployment,
// or nil. NotBefore is the server's call (it knows the configured skew), so
// a not-yet-valid certificate is usable here.
func (insp *NodeCertInspection) Err() error {
	if err := insp.filesErr(); err != nil {
		return err
	}
	switch {
	case insp.CNErr != nil:
		return insp.CNErr
	case insp.Binding != nil:
		return insp.Binding
	case insp.Validity != nil && !errors.Is(insp.Validity, ErrCertNotYetValid):
		return insp.Validity
	}
	return nil
}

func (insp *NodeCertInspection) filesErr() error {
	for _, f := range []NodeCertFile{insp.Dir, insp.Cert, insp.Key} {
		if f.Err != nil {
			return f.Err
		}
	}
	return nil
}

func statNodeCertFile(path string) NodeCertFile {
	f := NodeCertFile{Path: path}
	fi, err := os.Stat(path)
	if err != nil {
		f.Err = err
		return f
	}
	f.Mode = fi.Mode().Perm()
	f.Owner = "unknown"
	if st, ok := fi.Sys().(*syscall.Stat_t); ok {
		f.Owner = fmt.Sprintf("uid %d", st.Uid)
		if u, err := user.LookupId(fmt.Sprintf("%d", st.Uid)); err == nil {
			f.Owner = u.Username
		}
	}
	return f
}

// inspectNodeCertFiles checks the node cert directory and files, returning
// the inspection results.
func inspectNodeCertFiles(dir string, poolUUID uuid.UUID) *NodeCertInspection {
	insp := &NodeCertInspection{}
	certPath, keyPath := NodeCertPaths(dir, poolUUID)

	insp.Dir = statNodeCertFile(dir)
	if insp.Dir.Err == nil {
		if fi, err := os.Stat(dir); err == nil && !fi.IsDir() {
			insp.Dir.Err = errors.Errorf("%s is not a directory", dir)
		}
	}
	if insp.Dir.Err != nil {
		insp.Dir.Err = errors.Wrap(insp.Dir.Err, "node cert directory")
		insp.Cert, insp.Key = NodeCertFile{Path: certPath}, NodeCertFile{Path: keyPath}
		return insp
	}

	insp.Cert = statNodeCertFile(certPath)
	if insp.Cert.Err == nil {
		insp.PEM, insp.Cert.Err = LoadPEMData(certPath, MaxCertPerm)
	}
	if insp.Cert.Err == nil {
		block, _ := pem.Decode(insp.PEM)
		if block == nil {
			insp.Cert.Err = fmt.Errorf("invalid PEM data in %s", certPath)
		} else {
			insp.Certificate, insp.Cert.Err = x509.ParseCertificate(block.Bytes)
		}
	}
	if insp.Cert.Err == nil {
		insp.Cert.Err = CheckPoPKeyType(insp.Certificate)
	}
	if insp.Cert.Err != nil {
		insp.Cert.Err = errors.Wrap(insp.Cert.Err, "node certificate")
		insp.Key = NodeCertFile{Path: keyPath}
		return insp
	}

	insp.Key = statNodeCertFile(keyPath)
	if insp.Key.Err == nil {
		insp.PrivateKey, insp.Key.Err = LoadPrivateKey(keyPath)
	}
	if insp.Key.Err == nil {
		insp.Key.Err = VerifyCertKey(insp.PrivateKey, insp.Certificate)
	}
	if insp.Key.Err != nil {
		insp.Key.Err = errors.Wrap(insp.Key.Err, "node private key")
	}
	return insp
}

// validateNodeCertForUse checks that a loaded cert is usable on this machine.
func validateNodeCertForUse(cert *x509.Certificate, poolID, machineName string, now time.Time) error {
	insp := &NodeCertInspection{Certificate: cert}
	insp.checkPolicy(machineName, now, 0)
	return errors.Wrapf(insp.Err(), "node cert for pool %s", poolID)
}

// CheckCertValidity reports whether cert is within its validity window at now,
// tolerating skew on NotBefore.
func CheckCertValidity(cert *x509.Certificate, now time.Time, skew time.Duration) error {
	if now.After(cert.NotAfter) {
		return errors.Wrapf(ErrCertInvalid, "expired at %s", cert.NotAfter.Format(time.RFC3339))
	}
	if now.Add(skew).Before(cert.NotBefore) {
		return errors.Wrapf(ErrCertNotYetValid, "notBefore=%s, now=%s",
			cert.NotBefore.Format(time.RFC3339), now.Format(time.RFC3339))
	}
	return nil
}

// VerifyNodeCertChain verifies cert against the DAOS root through the pool's
// CA bundle.
func VerifyNodeCertChain(cert, root *x509.Certificate, bundle []byte, now time.Time) error {
	if root == nil {
		return errors.Wrap(ErrInvalidInput, "no trust anchor")
	}
	poolCAs, err := parsePoolCABundle(bundle)
	if err != nil {
		return err
	}
	roots := x509.NewCertPool()
	roots.AddCert(root)
	intermediates := x509.NewCertPool()
	for _, ca := range poolCAs {
		intermediates.AddCert(ca)
	}
	chains, err := cert.Verify(x509.VerifyOptions{
		Roots:         roots,
		Intermediates: intermediates,
		CurrentTime:   now,
		KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	})
	if err != nil {
		return errors.Wrapf(ErrCertInvalid, "chain validation: %s", err)
	}
	// A leaf the DAOS CA signed directly would otherwise verify on every pool.
	for _, chain := range chains {
		for _, ca := range poolCAs {
			if len(chain) > 1 && bytes.Equal(chain[1].Raw, ca.Raw) {
				return nil
			}
		}
	}
	return errors.Wrap(ErrCertInvalid, "chain validation: issuer is not one of the pool's CAs")
}

// CheckCertRevocation reports whether the cert is at or below the revocation watermark for its CN.
// A certificate issued after the watermark is considered valid; a certificate issued at or before
// the watermark is considered revoked.
func CheckCertRevocation(cert *x509.Certificate, cn string, watermarks CertWatermarks) error {
	if wm, ok := watermarks[cn]; ok && !cert.NotBefore.After(wm) {
		return errors.Wrapf(ErrCertRevoked,
			"cert for %q revoked (NotBefore=%s, watermark=%s)", cn,
			cert.NotBefore.Format(time.RFC3339), wm.Format(time.RFC3339))
	}
	return nil
}

// CertAndPoP loads the pool's node cert and signs a fresh proof-of-possession
// binding it to the given handle. machineName must come from the same source
// as the AUTH_SYS machine name; revoke-by-CN depends on it.
func (l *NodeCertLoader) CertAndPoP(log logging.Logger, poolUUID, handleUUID uuid.UUID, machineName string) (cert *NodeCert, pop, payload []byte, err error) {
	cert, err = l.Load(log, poolUUID)
	if err != nil {
		return nil, nil, nil, err
	}
	if err := validateNodeCertForUse(cert.Cert, poolUUID.String(), machineName, time.Now()); err != nil {
		return nil, nil, nil, err
	}

	payload, err = buildPoPPayload(poolUUID, handleUUID)
	if err != nil {
		return nil, nil, nil, errors.Wrap(err, "building PoP payload")
	}
	pop, err = signPoP(cert.Key, payload)
	if err != nil {
		return nil, nil, nil, errors.Wrap(err, "signing PoP")
	}
	return cert, pop, payload, nil
}

var (
	// ErrCertInvalid covers certs that can't be parsed, don't chain to
	// the root, or violate CN policy.
	ErrCertInvalid = errors.New("node cert invalid")
	// ErrCertRevoked covers certs at or below their revocation watermark.
	ErrCertRevoked = errors.New("node cert revoked")
	// ErrInvalidInput covers malformed request fields.
	ErrInvalidInput = errors.New("invalid input")
	// ErrPoPInvalid covers payload binding and signature failures.
	ErrPoPInvalid = errors.New("proof-of-possession invalid")
	// ErrPoPStale covers timestamps outside the allowed clock skew.
	ErrPoPStale = errors.New("proof-of-possession stale")
	// ErrBadWatermarks covers a cert_watermarks property that cannot be decoded.
	ErrBadWatermarks = errors.New("cert watermarks unreadable")
	// ErrCertNotYetValid covers a cert whose NotBefore is beyond the skew tolerance.
	ErrCertNotYetValid = errors.New("node cert not yet valid")
	// ErrNodeCertNotDeployed covers a pool with no certificate file at all.
	ErrNodeCertNotDeployed = errors.New("no node certificate deployed")
)

// NodeCertPresentation bundles everything a server knows when a node cert is
// presented at pool connect.
type NodeCertPresentation struct {
	Root        *x509.Certificate // trust anchor (the DAOS CA)
	PoolCA      []byte            // PEM bundle from the ca_cert property
	Cert        []byte            // PEM, as presented
	PoPSig      []byte            // signature over PoPPayload
	PoPPayload  []byte            // raw marshaled PoPPayload bytes
	PoolUUID    uuid.UUID         // pool being connected to
	MachineName string            // client's AUTH_SYS machine name
	Watermarks  []byte            // cert_watermarks property, may be empty
	MaxSkew     time.Duration     // tolerated clock skew on cert NotBefore and PoP timestamps
	Now         time.Time         // validation time
}

// Validate checks the presented cert and proof-of-possession, returning
// the parsed payload.
func (p *NodeCertPresentation) Validate() (*PoPPayload, error) {
	cert, err := p.verifyCert(p.Now, p.MaxSkew)
	if err != nil {
		return nil, err
	}
	return p.verifyProof(cert, p.Now, p.MaxSkew)
}

// parsePoolCABundle parses the ca_cert property into a slice of x509.Certificates.
func parsePoolCABundle(bundle []byte) ([]*x509.Certificate, error) {
	entries, err := ParseCABundle(bundle)
	if err != nil {
		return nil, errors.Wrapf(ErrCertInvalid, "pool CA bundle: %s", err)
	}
	if len(entries) == 0 {
		return nil, errors.Wrap(ErrCertInvalid, "pool CA bundle contains no certificates")
	}
	cas := make([]*x509.Certificate, len(entries))
	for i, e := range entries {
		cas[i] = e.Cert
	}
	return cas, nil
}

// verifyCert checks that the presented cert is acceptable.
func (p *NodeCertPresentation) verifyCert(now time.Time, maxSkew time.Duration) (*x509.Certificate, error) {
	block, _ := pem.Decode(p.Cert)
	if block == nil {
		return nil, errors.Wrap(ErrCertInvalid, "no PEM data in node cert")
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return nil, errors.Wrapf(ErrCertInvalid, "parsing node cert: %s", err)
	}
	if err := CheckPoPKeyType(cert); err != nil {
		return nil, err
	}

	if err := CheckCertValidity(cert, now, maxSkew); err != nil {
		return nil, err
	}
	if err := VerifyNodeCertChain(cert, p.Root, p.PoolCA, now.Add(maxSkew)); err != nil {
		return nil, err
	}

	cn := cert.Subject.CommonName
	prefix, suffix, err := ParsePoolCertCN(cn)
	if err != nil {
		return nil, errors.Wrapf(ErrCertInvalid, "CN policy: %s", err)
	}
	if err := CheckCNBinding(prefix, suffix, p.MachineName); err != nil {
		return nil, err
	}

	if len(p.Watermarks) > 0 {
		watermarks, err := DecodeCertWatermarks(p.Watermarks)
		if err != nil {
			return nil, errors.Wrapf(ErrBadWatermarks, "%s", err)
		}
		if err := CheckCertRevocation(cert, cn, watermarks); err != nil {
			return nil, err
		}
	}

	return cert, nil
}

// verifyProof checks that the presenter holds the cert key, that the
// proof is fresh, and that it's bound to the requested pool.
func (p *NodeCertPresentation) verifyProof(cert *x509.Certificate, now time.Time, maxSkew time.Duration) (*PoPPayload, error) {
	if err := verifyPoP(cert.PublicKey, p.PoPPayload, p.PoPSig); err != nil {
		return nil, errors.Wrapf(ErrPoPInvalid, "signature: %s", err)
	}

	payload, err := parsePoPPayload(p.PoPPayload)
	if err != nil {
		return nil, errors.Wrapf(ErrInvalidInput, "PoP payload: %s", err)
	}

	if payload.PoolID() != p.PoolUUID {
		return nil, errors.Wrap(ErrPoPInvalid,
			"payload pool UUID does not match request pool ID")
	}

	skew := now.Sub(payload.Time())
	if skew < 0 {
		skew = -skew
	}
	if skew > maxSkew {
		return nil, errors.Wrapf(ErrPoPStale,
			"timestamp skew too large: %s (max %s)", skew, maxSkew)
	}

	return payload, nil
}

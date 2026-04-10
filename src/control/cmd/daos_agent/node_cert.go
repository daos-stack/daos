//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/sha512"
	"crypto/x509"
	"encoding/binary"
	"encoding/pem"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

// nodeCertCache caches loaded node certificates and keys by pool UUID
// to avoid repeated disk I/O on every pool connect. Entries are
// invalidated when either the cert or key file's mtime/size changes,
// so a freshly issued replacement cert is picked up without restarting
// the agent.
var nodeCertCache sync.Map // poolUUID string -> *cachedNodeCert

type fileSig struct {
	mtime time.Time
	size  int64
}

type cachedNodeCert struct {
	certPEM []byte
	cert    *x509.Certificate
	key     crypto.PrivateKey
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

// popPayloadLen is the fixed length of the proof-of-possession payload:
// pool_uuid (16) + handle_uuid (16) + timestamp (8) = 40 bytes.
const popPayloadLen = 40

// notBeforeSkewTolerance is the leeway we allow when checking whether a
// freshly-loaded cert is "not yet valid". Replacement certs after a
// revocation are minted with NotBefore equal to the server's watermark,
// which is the server's wall clock at commit time; admin and agent
// clocks may legitimately be a few minutes behind, and rejecting in
// that window would surface a confusing "not yet valid" error for a
// cert that the server itself just issued. Matches the server-side PoP
// skew default.
const notBeforeSkewTolerance = 5 * time.Minute

// buildPoPPayload constructs the fixed-format binary payload for PoP signing.
func buildPoPPayload(poolUUID uuid.UUID, handleUUID []byte) []byte {
	payload := make([]byte, popPayloadLen)
	copy(payload[0:16], poolUUID[:])
	copy(payload[16:32], handleUUID)
	binary.BigEndian.PutUint64(payload[32:40], uint64(time.Now().Unix()))
	return payload
}

// signPoP signs the proof-of-possession payload with the node certificate's
// private key. The signing algorithm is determined by the key type:
//   - RSA:        RSA-PSS(SHA-512, payload)
//   - ECDSA P-256: ECDSA(SHA-256, payload)
//   - ECDSA P-384: ECDSA(SHA-384, payload)
func signPoP(key crypto.PrivateKey, cert *x509.Certificate, payload []byte) ([]byte, error) {
	switch k := key.(type) {
	case *rsa.PrivateKey:
		h := sha512.Sum512(payload)
		return rsa.SignPSS(rand.Reader, k, crypto.SHA512, h[:], &rsa.PSSOptions{
			SaltLength: rsa.PSSSaltLengthEqualsHash,
			Hash:       crypto.SHA512,
		})
	case *ecdsa.PrivateKey:
		var hash []byte
		switch k.Curve {
		case elliptic.P256():
			h := sha256.Sum256(payload)
			hash = h[:]
		case elliptic.P384():
			h := sha512.Sum384(payload)
			hash = h[:]
		default:
			return nil, fmt.Errorf("unsupported ECDSA curve: %v", k.Curve.Params().Name)
		}
		return ecdsa.SignASN1(rand.Reader, k, hash)
	default:
		return nil, fmt.Errorf("unsupported key type: %T", key)
	}
}

func loadNodeCertCached(log logging.Logger, certDir, poolID string) (*cachedNodeCert, error) {
	poolUUID, err := uuid.Parse(poolID)
	if err != nil {
		return nil, errors.Wrap(err, "parsing pool UUID")
	}

	certPath := filepath.Join(certDir, poolUUID.String()+".crt")
	keyPath := filepath.Join(certDir, poolUUID.String()+".key")

	certSig, err := statSig(certPath)
	if err != nil {
		return nil, errors.Wrap(err, "stat node certificate")
	}
	keySig, err := statSig(keyPath)
	if err != nil {
		return nil, errors.Wrap(err, "stat node private key")
	}

	if v, ok := nodeCertCache.Load(poolID); ok {
		cached := v.(*cachedNodeCert)
		if cached.certSig == certSig && cached.keySig == keySig {
			return cached, nil
		}
		log.Debugf("node cert file for pool %s changed on disk; reloading", poolID)
	}

	certPEM, err := security.LoadPEMData(certPath, security.MaxCertPerm)
	if err != nil {
		return nil, errors.Wrap(err, "loading node certificate")
	}

	block, _ := pem.Decode(certPEM)
	if block == nil {
		return nil, fmt.Errorf("invalid PEM data in %s", certPath)
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return nil, errors.Wrap(err, "parsing node certificate")
	}

	// Validate cert CN prefix. Node certs (CertCNPrefixNode) must
	// match the local hostname. Tenant certs (CertCNPrefixTenant)
	// are shared across nodes and skip the hostname check.
	cn := cert.Subject.CommonName
	switch {
	case strings.HasPrefix(cn, security.CertCNPrefixTenant):
		log.Debugf("tenant cert for pool %s: %s", poolID, cn)
	case strings.HasPrefix(cn, security.CertCNPrefixNode):
		nodeName := strings.TrimPrefix(cn, security.CertCNPrefixNode)
		hostname, err := os.Hostname()
		if err != nil {
			return nil, errors.Wrap(err, "getting hostname for CN validation")
		}
		if nodeName != hostname {
			return nil, fmt.Errorf("node cert CN %q does not match hostname %q (pool %s)",
				cn, hostname, poolID)
		}
	default:
		return nil, fmt.Errorf("node cert CN %q has no recognized prefix (pool %s)", cn, poolID)
	}

	now := time.Now()
	if now.After(cert.NotAfter) {
		return nil, fmt.Errorf("node certificate for pool %s expired at %s",
			poolID, cert.NotAfter.Format(time.RFC3339))
	}
	if now.Add(notBeforeSkewTolerance).Before(cert.NotBefore) {
		return nil, fmt.Errorf("node certificate for pool %s not yet valid (notBefore=%s, local now=%s)",
			poolID, cert.NotBefore.Format(time.RFC3339),
			now.Format(time.RFC3339))
	}

	key, err := security.LoadPrivateKey(keyPath)
	if err != nil {
		return nil, errors.Wrap(err, "loading node private key")
	}

	entry := &cachedNodeCert{
		certPEM: certPEM,
		cert:    cert,
		key:     key,
		certSig: certSig,
		keySig:  keySig,
	}
	nodeCertCache.Store(poolID, entry)

	log.Debugf("loaded node cert for pool %s: CN=%s, expires=%s",
		poolID, cert.Subject.CommonName,
		cert.NotAfter.Format(time.RFC3339))

	return entry, nil
}

// getNodeCertAndPoP loads the node certificate for a pool and signs a PoP.
// Returns PEM-encoded cert, PoP signature, and the signed payload.
func getNodeCertAndPoP(log logging.Logger, certDir, poolID string, handleUUID []byte) (certPEM, pop, payload []byte, err error) {
	_, certPEM, pop, payload, err = getNodeCertAndPoPWithMeta(log, certDir, poolID, handleUUID)
	return
}

// getNodeCertAndPoPWithMeta is like getNodeCertAndPoP but also returns
// the cached cert entry for logging purposes.
func getNodeCertAndPoPWithMeta(log logging.Logger, certDir, poolID string, handleUUID []byte) (cached *cachedNodeCert, certPEM, pop, payload []byte, err error) {
	poolUUID, err := uuid.Parse(poolID)
	if err != nil {
		return nil, nil, nil, nil, errors.Wrap(err, "parsing pool UUID")
	}

	cached, err = loadNodeCertCached(log, certDir, poolID)
	if err != nil {
		return nil, nil, nil, nil, err
	}

	// Build and sign PoP payload (fresh each time — unique per connect)
	payload = buildPoPPayload(poolUUID, handleUUID)
	pop, err = signPoP(cached.key, cached.cert, payload)
	if err != nil {
		return nil, nil, nil, nil, errors.Wrap(err, "signing PoP")
	}

	return cached, cached.certPEM, pop, payload, nil
}

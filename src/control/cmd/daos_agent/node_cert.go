//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"crypto/x509"
	"fmt"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

func validateNodeCertForUse(cert *x509.Certificate, poolID string, now time.Time) error {
	cn := cert.Subject.CommonName
	switch {
	case strings.HasPrefix(cn, security.CertCNPrefixTenant):
	case strings.HasPrefix(cn, security.CertCNPrefixNode):
		// Must use the same source as AUTH_SYS machine_name; revoke-by-CN depends on it.
		nodeName := strings.TrimPrefix(cn, security.CertCNPrefixNode)
		machine, err := auth.GetMachineName()
		if err != nil {
			return errors.Wrap(err, "getting machine name for CN validation")
		}
		if nodeName != machine {
			return fmt.Errorf("node cert CN %q does not match machine name %q (pool %s)",
				cn, machine, poolID)
		}
	default:
		return fmt.Errorf("node cert CN %q has no recognized prefix (pool %s)", cn, poolID)
	}

	if now.After(cert.NotAfter) {
		return fmt.Errorf("node certificate for pool %s expired at %s",
			poolID, cert.NotAfter.Format(time.RFC3339))
	}
	if now.Add(security.NotBeforeSkewTolerance).Before(cert.NotBefore) {
		return fmt.Errorf("node certificate for pool %s not yet valid (notBefore=%s, local now=%s)",
			poolID, cert.NotBefore.Format(time.RFC3339),
			now.Format(time.RFC3339))
	}
	return nil
}

func getNodeCertAndPoP(log logging.Logger, loader *security.NodeCertLoader, poolID string, handleUUID []byte) (cert *security.NodeCert, certPEM, pop, payload []byte, err error) {
	poolUUID, err := uuid.Parse(poolID)
	if err != nil {
		return nil, nil, nil, nil, errors.Wrap(err, "parsing pool UUID")
	}

	cert, err = loader.Load(log, poolID)
	if err != nil {
		return nil, nil, nil, nil, err
	}
	if err := validateNodeCertForUse(cert.Cert, poolID, time.Now()); err != nil {
		return nil, nil, nil, nil, err
	}

	payload = security.BuildPoPPayload(poolUUID, handleUUID)
	pop, err = security.SignPoP(cert.Key, cert.Cert, payload)
	if err != nil {
		return nil, nil, nil, nil, errors.Wrap(err, "signing PoP")
	}
	return cert, cert.PEM, pop, payload, nil
}

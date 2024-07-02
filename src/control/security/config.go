//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto"
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"io/fs"
	"os"
	"strconv"
	"time"

	"github.com/pkg/errors"
)

const (
	certDir              = "/etc/daos/certs/"
	defaultCACert        = certDir + "daosCA.crt"
	defaultServerCert    = certDir + "server.crt"
	defaultServerKey     = certDir + "server.key"
	defaultAdminCert     = certDir + "admin.crt"
	defaultAdminKey      = certDir + "admin.key"
	defaultAgentCert     = certDir + "agent.crt"
	defaultAgentKey      = certDir + "agent.key"
	defaultClientCertDir = certDir + "clients"
	defaultServer        = "server"
	defaultInsecure      = false
)

// MappedClientUser represents a client user that is mapped to a uid.
type MappedClientUser struct {
	User   string   `yaml:"user"`
	Group  string   `yaml:"group"`
	Groups []string `yaml:"groups"`
}

const (
	defaultMapUser = "default"
	defaultMapKey  = ^uint32(0)
)

// ClientUserMap is a map of uids to mapped client users.
type ClientUserMap map[uint32]*MappedClientUser

func (cm *ClientUserMap) UnmarshalYAML(unmarshal func(interface{}) error) error {
	strKeyMap := make(map[string]*MappedClientUser)
	if err := unmarshal(&strKeyMap); err != nil {
		return err
	}

	tmp := make(ClientUserMap)
	for strKey, value := range strKeyMap {
		var key uint32
		switch strKey {
		case defaultMapUser:
			key = defaultMapKey
		default:
			parsedKey, err := strconv.ParseUint(strKey, 10, 32)
			if err != nil {
				return errors.Wrapf(err, "invalid uid %s", strKey)
			}

			switch parsedKey {
			case uint64(defaultMapKey):
				return errors.Errorf("uid %d is reserved", parsedKey)
			default:
				key = uint32(parsedKey)
			}
		}

		tmp[key] = value
	}
	*cm = tmp

	return nil
}

// Lookup attempts to resolve the supplied uid to a mapped
// client user. If the uid is not in the map, the default map key
// is returned. If the default map key is not found, nil is returned.
func (cm ClientUserMap) Lookup(uid uint32) *MappedClientUser {
	if mu, found := cm[uid]; found {
		return mu
	}
	return cm[defaultMapKey]
}

// CredentialConfig contains configuration details for managing user
// credentials.
type CredentialConfig struct {
	CacheExpiration time.Duration `yaml:"cache_expiration,omitempty"`
	ClientUserMap   ClientUserMap `yaml:"client_user_map,omitempty"`
}

// TransportConfig contains all the information on whether or not to use
// certificates and their location if their use is specified.
type TransportConfig struct {
	AllowInsecure     bool `yaml:"allow_insecure"`
	CertificateConfig `yaml:",inline"`
}

func (tc *TransportConfig) String() string {
	return fmt.Sprintf("allow insecure: %v", tc.AllowInsecure)
}

// CertificateConfig contains the specific certificate information for the daos
// component. ServerName is only needed if the config is being used as a
// transport credential for a gRPC tls client.
type CertificateConfig struct {
	ServerName      string           `yaml:"-"`
	ClientCertDir   string           `yaml:"client_cert_dir,omitempty"`
	CARootPath      string           `yaml:"ca_cert"`
	CertificatePath string           `yaml:"cert"`
	PrivateKeyPath  string           `yaml:"key"`
	tlsKeypair      *tls.Certificate `yaml:"-"`
	caPool          *x509.CertPool   `yaml:"-"`
	maxKeyPerms     fs.FileMode      `yaml:"-"`
	verifyTime      time.Time        `yaml:"-"` // for testing
}

// DefaultAgentTransportConfig provides a default transport config disabling
// certificate usage and specifying certificates located under /etc/daos/certs.
func DefaultAgentTransportConfig() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: defaultInsecure,
		CertificateConfig: CertificateConfig{
			ServerName:      defaultServer,
			ClientCertDir:   "",
			CARootPath:      defaultCACert,
			CertificatePath: defaultAgentCert,
			PrivateKeyPath:  defaultAgentKey,
			tlsKeypair:      nil,
			caPool:          nil,
			maxKeyPerms:     MaxUserOnlyKeyPerm,
		},
	}
}

// DefaultClientTransportConfig provides a default transport config disabling
// certificate usage and specifying certificates located under /etc/daos/certs.
// As this credential is meant to be used as a client credential it specifies a default
// ServerName as well.
func DefaultClientTransportConfig() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: defaultInsecure,
		CertificateConfig: CertificateConfig{
			ServerName:      defaultServer,
			ClientCertDir:   "",
			CARootPath:      defaultCACert,
			CertificatePath: defaultAdminCert,
			PrivateKeyPath:  defaultAdminKey,
			tlsKeypair:      nil,
			caPool:          nil,
			maxKeyPerms:     MaxGroupKeyPerm,
		},
	}
}

// DefaultServerTransportConfig provides a default transport config disabling
// certificate usage and specifying certificates located under /etc/daos.
func DefaultServerTransportConfig() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: defaultInsecure,
		CertificateConfig: CertificateConfig{
			ServerName:      defaultServer,
			CARootPath:      defaultCACert,
			ClientCertDir:   defaultClientCertDir,
			CertificatePath: defaultServerCert,
			PrivateKeyPath:  defaultServerKey,
			tlsKeypair:      nil,
			caPool:          nil,
			maxKeyPerms:     MaxUserOnlyKeyPerm,
		},
	}
}

// PreLoadCertData reads the certificate files in and parses them into TLS key
// pair and Certificate pool to provide a mechanism for detecting certificate
// error before first use.
func (tc *TransportConfig) PreLoadCertData() error {
	if tc == nil {
		return errors.New("nil TransportConfig")
	}
	if tc.tlsKeypair != nil && tc.caPool != nil || tc.AllowInsecure {
		// In this case the data is already preloaded.
		// In order to reload data use ReloadCertData
		return nil
	}

	if tc.ClientCertDir != "" {
		if _, err := os.ReadDir(tc.ClientCertDir); errors.Is(err, fs.ErrPermission) {
			return FaultUnreadableCertFile(tc.ClientCertDir)
		} else if err != nil {
			return errors.Wrap(err, "checking client cert directory")
		}
	}

	certificate, certPool, err := loadCertWithCustomCA(tc.CARootPath, tc.CertificatePath, tc.PrivateKeyPath, tc.maxKeyPerms)
	if err != nil {
		return err
	}

	tc.tlsKeypair = certificate
	tc.caPool = certPool

	// Pre-parse the Leaf Certificate
	tc.tlsKeypair.Leaf, err = x509.ParseCertificate(tc.tlsKeypair.Certificate[0])
	if err != nil {
		return err
	}

	if _, err = tc.tlsKeypair.Leaf.Verify(x509.VerifyOptions{
		CurrentTime: tc.CertificateConfig.verifyTime, // for testing - by default this is 0, which is treated as current time
		Roots:       certPool,
		KeyUsages:   []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
	}); isInvalidCert(err) {
		return FaultInvalidCertFile(tc.CertificatePath, err)
	} else if err != nil {
		return err
	}

	return nil
}

// ReloadCertData reloads and stores the certificate data in the case when
// certificate data has changed since initial loading.
func (tc *TransportConfig) ReloadCertData() error {
	tc.tlsKeypair = nil
	tc.caPool = nil
	return tc.PreLoadCertData()
}

// PrivateKey returns the private key stored in the certificates loaded into the TransportConfig
func (tc *TransportConfig) PrivateKey() (crypto.PrivateKey, error) {
	if tc.AllowInsecure {
		return nil, nil
	}
	// If we don't have our keys loaded attempt to load them.
	if tc.tlsKeypair == nil || tc.caPool == nil {
		err := tc.ReloadCertData()
		if err != nil {
			return nil, err
		}
	}
	return tc.tlsKeypair.PrivateKey, nil
}

// PublicKey returns the private key stored in the certificates loaded into the TransportConfig
func (tc *TransportConfig) PublicKey() (crypto.PublicKey, error) {
	if tc.AllowInsecure {
		return nil, nil
	}
	// If we don't have our keys loaded attempt to load them.
	if tc.tlsKeypair == nil || tc.caPool == nil {
		err := tc.ReloadCertData()
		if err != nil {
			return nil, err
		}
	}
	return tc.tlsKeypair.Leaf.PublicKey, nil
}

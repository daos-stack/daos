//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package security

import (
	"crypto"
	"crypto/tls"
	"crypto/x509"
	"fmt"

	"github.com/pkg/errors"
)

const (
	defaultCACert        = "/etc/daos/daosCA.crt"
	defaultServerCert    = "/etc/daos/server.crt"
	defaultServerKey     = "/etc/daos/server.key"
	defaultClientCert    = ".daos/client.crt"
	defaultClientKey     = ".daos/client.key"
	defaultAgentCert     = ".daos/agent.crt"
	defaultAgentKey      = ".daos/agent.key"
	defaultServer        = "server"
	defaultClientCertDir = ".daos/clients"
	defaultInsecure      = false
)

//TransportConfig contains all the information on whether or not to use
//certificates and their location if their use is specified.
type TransportConfig struct {
	AllowInsecure     bool `yaml:"allow_insecure"`
	CertificateConfig `yaml:",inline"`
}

func (tc *TransportConfig) String() string {
	return fmt.Sprintf("allow insecure: %v", tc.AllowInsecure)
}

//CertificateConfig contains the specific certificate information for the daos
//component. ServerName is only needed if the config is being used as a
//transport credential for a gRPC tls client.
type CertificateConfig struct {
	ServerName      string           `yaml:"server_name,omitempty"`
	ClientCertDir   string           `yaml:"client_cert_dir,omitempty"`
	CARootPath      string           `yaml:"ca_cert"`
	CertificatePath string           `yaml:"cert"`
	PrivateKeyPath  string           `yaml:"key"`
	tlsKeypair      *tls.Certificate `yaml:"-"`
	caPool          *x509.CertPool   `yaml:"-"`
}

// DefaultAgentTransportConfig provides a default transport config disabling
// certificate usage and specifying certificates located under .daos.
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
		},
	}
}

//DefaultClientTransportConfig provides a default transport config disabling
//certificate usage and specifying certificates located under .daos. As this
//credential is meant to be used as a client credential it specifies a default
//ServerName as well.
func DefaultClientTransportConfig() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: defaultInsecure,
		CertificateConfig: CertificateConfig{
			ServerName:      defaultServer,
			ClientCertDir:   "",
			CARootPath:      defaultCACert,
			CertificatePath: defaultClientCert,
			PrivateKeyPath:  defaultClientKey,
			tlsKeypair:      nil,
			caPool:          nil,
		},
	}
}

//DefaultServerTransportConfig provides a default transport config disabling
//certificate usage and specifying certificates located under .daos.
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
		},
	}
}

//PreLoadCertData reads the certificate files in and parses them into TLS key
//pair and Certificate pool to provide a mechanism for detecting certificate/
//error before first use.
func (cfg *TransportConfig) PreLoadCertData() error {
	if cfg == nil {
		return errors.New("nil TransportConfig")
	}
	if cfg.tlsKeypair != nil && cfg.caPool != nil || cfg.AllowInsecure == true {
		// In this case the data is already preloaded.
		// In order to reload data use ReloadCertDatA
		return nil
	}
	certificate, certPool, err := loadCertWithCustomCA(cfg.CARootPath, cfg.CertificatePath, cfg.PrivateKeyPath)
	if err != nil {
		return err
	}

	cfg.tlsKeypair = certificate
	cfg.caPool = certPool

	// Pre-parse the Leaf Certificate
	cfg.tlsKeypair.Leaf, err = x509.ParseCertificate(cfg.tlsKeypair.Certificate[0])
	if err != nil {
		return err
	}

	return nil
}

//ReloadCertData reloads and stores the certificate data in the case when
//certificate data has changed since initial loading.
func (cfg *TransportConfig) ReloadCertData() error {
	cfg.tlsKeypair = nil
	cfg.caPool = nil
	return cfg.PreLoadCertData()
}

//PrivateKey returns the private key stored in the certificates loaded into the TransportConfig
func (cfg *TransportConfig) PrivateKey() (crypto.PrivateKey, error) {
	if cfg.AllowInsecure == true {
		return nil, nil
	}
	// If we don't have our keys loaded attempt to load them.
	if cfg.tlsKeypair == nil || cfg.caPool == nil {
		err := cfg.ReloadCertData()
		if err != nil {
			return nil, err
		}
	}
	return cfg.tlsKeypair.PrivateKey, nil
}

//PublicKey returns the private key stored in the certificates loaded into the TransportConfig
func (cfg *TransportConfig) PublicKey() (crypto.PublicKey, error) {
	if cfg.AllowInsecure == true {
		return nil, nil
	}
	// If we don't have our keys loaded attempt to load them.
	if cfg.tlsKeypair == nil || cfg.caPool == nil {
		err := cfg.ReloadCertData()
		if err != nil {
			return nil, err
		}
	}
	return cfg.tlsKeypair.Leaf.PublicKey, nil
}

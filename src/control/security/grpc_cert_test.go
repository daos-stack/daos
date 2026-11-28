//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"context"
	"crypto"
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/rsa"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/fault"
)

// testGrpcSvcServer implements a very basic grpc interface for testing purposes
type testGrpcSvcServer struct {
	UnimplementedTestGrpcSvcServer
}

func (s *testGrpcSvcServer) Hello(_ context.Context, req *HelloReq) (*HelloResp, error) {
	return &HelloResp{
		Greeting: fmt.Sprintf("Hello %s", req.Name),
	}, nil
}

type genKeypairFn func() (crypto.PublicKey, crypto.PrivateKey, error)

func createCA(t *testing.T, gen genKeypairFn) (*x509.Certificate, crypto.PrivateKey) {
	t.Helper()

	return createCert(t, "DAOS CA", nil, nil, gen)
}

func createCert(t *testing.T, commonName string, parent *x509.Certificate, signer crypto.Signer, gen genKeypairFn) (*x509.Certificate, crypto.PrivateKey) {
	t.Helper()

	pub, key, err := gen()
	if err != nil {
		t.Fatal(err)
	}

	template := &x509.Certificate{
		NotBefore: time.Now(),
		NotAfter:  time.Now().Add(time.Minute),
		Subject: pkix.Name{
			Organization: []string{"DAOS"},
		},
		KeyUsage:    x509.KeyUsageDigitalSignature,
		ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth, x509.ExtKeyUsageServerAuth},
	}

	if parent == nil {
		// self-signed CA
		template.IsCA = true
		template.KeyUsage |= x509.KeyUsageCertSign
		template.BasicConstraintsValid = true

		parent = template
		signer = key.(crypto.Signer)
	}
	template.Subject.CommonName = commonName
	certBytes, err := x509.CreateCertificate(rand.Reader, template, parent, pub, signer)
	if err != nil {
		t.Fatal(err)
	}

	cert, err := x509.ParseCertificate(certBytes)
	if err != nil {
		t.Fatal(err)
	}
	return cert, key
}

func writeCert(t *testing.T, cert *x509.Certificate, path string) {
	t.Helper()

	bytes := pem.EncodeToMemory(&pem.Block{
		Type:  "CERTIFICATE",
		Bytes: cert.Raw,
	})
	if err := os.WriteFile(path, bytes, MaxCertPerm); err != nil {
		t.Fatal(err)
	}
}

func writePrivateKey(t *testing.T, priv crypto.PrivateKey, path string) {
	t.Helper()

	keyBytes, err := x509.MarshalPKCS8PrivateKey(priv)
	if err != nil {
		t.Fatal(err)
	}

	bytes := pem.EncodeToMemory(&pem.Block{
		Type:  "PRIVATE KEY",
		Bytes: keyBytes,
	})
	if err := os.WriteFile(path, bytes, MaxUserOnlyKeyPerm); err != nil {
		t.Fatal(err)
	}
}

func customTransportCred(cfg *TransportConfig, genTLSCfg func(cfg *TransportConfig) *tls.Config, ciphersuites []uint16) (credentials.TransportCredentials, error) {
	err := cfg.PreLoadCertData()
	if err != nil {
		return nil, err
	}

	tlsCfg := genTLSCfg(cfg)
	tlsCfg.CipherSuites = ciphersuites
	creds := credentials.NewTLS(tlsCfg)
	return creds, nil
}

func genRSAKeys() (crypto.PublicKey, crypto.PrivateKey, error) {
	key, err := rsa.GenerateKey(rand.Reader, 1024) // small key for test purposes
	if err != nil {
		return nil, nil, err
	}
	return &key.PublicKey, key, nil
}

// basic handshake to verify communications between server and client TLS configs in grpc
func TestSecurity_grpc_TLS_handshake(t *testing.T) {
	for name, tc := range map[string]struct {
		genKey                     genKeypairFn
		overrideClientCiphersuites []uint16
		overrideServerCiphersuites []uint16
	}{
		"RSA cert": {
			genKey: genRSAKeys,
		},
		"ECDSA cert": {
			genKey: func() (crypto.PublicKey, crypto.PrivateKey, error) {
				key, err := ecdsa.GenerateKey(elliptic.P224(), rand.Reader)
				if err != nil {
					return nil, nil, err
				}
				return &key.PublicKey, key, nil
			},
		},
		"ed25519 cert": {
			genKey: func() (crypto.PublicKey, crypto.PrivateKey, error) {
				return ed25519.GenerateKey(rand.Reader)
			},
		},
		"client deprecated only": {
			genKey:                     genRSAKeys,
			overrideClientCiphersuites: []uint16{tls.TLS_RSA_WITH_AES_256_GCM_SHA384},
		},
		"server deprecated only": {
			genKey:                     genRSAKeys,
			overrideServerCiphersuites: []uint16{tls.TLS_RSA_WITH_AES_256_GCM_SHA384},
		},
	} {
		t.Run(name, func(t *testing.T) {
			caCert, caKey := createCA(t, tc.genKey)
			caSigner, ok := caKey.(crypto.Signer)
			if !ok {
				t.Fatalf("bad CA key type %T can't be used as signer", caKey)
			}
			srvCert, srvKey := createCert(t, "server", caCert, caSigner, tc.genKey)
			cliCert, cliKey := createCert(t, "agent", caCert, caSigner, tc.genKey)

			// write to filesystem
			testDir, cleanupDir := test.CreateTestDir(t)
			defer cleanupDir()

			rootCAPath := filepath.Join(testDir, "daosCA.crt")
			writeCert(t, caCert, rootCAPath)

			srvCertPath := filepath.Join(testDir, "server.crt")
			writeCert(t, srvCert, srvCertPath)
			srvKeyPath := filepath.Join(testDir, "server.key")
			writePrivateKey(t, srvKey, srvKeyPath)
			cliCertPath := filepath.Join(testDir, "agent.crt")
			writeCert(t, cliCert, cliCertPath)
			cliKeyPath := filepath.Join(testDir, "agent.key")
			writePrivateKey(t, cliKey, cliKeyPath)

			// set up transport configs for server and client
			srvCfg := DefaultServerTransportConfig()
			srvCfg.CertificateConfig.CARootPath = rootCAPath
			srvCfg.CertificateConfig.CertificatePath = srvCertPath
			srvCfg.CertificateConfig.PrivateKeyPath = srvKeyPath
			srvCfg.CertificateConfig.ClientCertDir = testDir

			cliCfg := DefaultAgentTransportConfig()
			cliCfg.CertificateConfig.CARootPath = rootCAPath
			cliCfg.CertificateConfig.CertificatePath = cliCertPath
			cliCfg.CertificateConfig.PrivateKeyPath = cliKeyPath
			cliCfg.CertificateConfig.ClientCertDir = testDir

			var srvCred credentials.TransportCredentials
			var err error
			if len(tc.overrideServerCiphersuites) > 0 {
				srvCred, err = customTransportCred(srvCfg, serverTLSConfig, tc.overrideServerCiphersuites)
			} else {
				srvCred, err = GetServerTransportCredentials(srvCfg)
			}
			if err != nil {
				if faultErr, ok := err.(*fault.Fault); ok {
					t.Fatal(faultErr.Reason)
				}
				t.Fatal(err)
			}

			var cliCred credentials.TransportCredentials
			if len(tc.overrideClientCiphersuites) > 0 {
				cliCred, err = customTransportCred(cliCfg, clientTLSConfig, tc.overrideClientCiphersuites)
			} else {
				cliCred, err = GetClientTransportCredentials(cliCfg)
			}
			if err != nil {
				if faultErr, ok := err.(*fault.Fault); ok {
					t.Fatal(faultErr.Reason)
				}
				t.Fatal(err)
			}

			// stand up server
			testGrpcSvc := &testGrpcSvcServer{}
			srv := grpc.NewServer(grpc.Creds(srvCred))
			srv.RegisterService(&TestGrpcSvc_ServiceDesc, testGrpcSvc)
			testAddr := "localhost:1234"
			lis, err := net.Listen("tcp", testAddr)
			if err != nil {
				t.Fatal(err)
			}
			defer lis.Close()
			go srv.Serve(lis)

			// set up client
			cli, err := grpc.NewClient(testAddr, grpc.WithTransportCredentials(cliCred))
			if err != nil {
				t.Fatal(err)
			}
			defer cli.Close()

			t.Logf("client state after creation: %s", cli.GetState())
			var resp HelloResp
			err = cli.Invoke(context.Background(), TestGrpcSvc_Hello_FullMethodName, &HelloReq{
				Name: name,
			}, &resp)
			t.Logf("client state after invoke: %s", cli.GetState())
			if err != nil {
				t.Fatal(err)
			}
			t.Logf("response: %s", resp.Greeting)
		})
	}
}

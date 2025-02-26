//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package promexp_test

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"strings"
	"testing"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/telemetry/promexp"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestPromExp_StartExporter(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg    *promexp.ExporterConfig
		expErr error
	}{
		"nil cfg": {
			expErr: errors.New("invalid exporter config"),
		},
		"empty cfg invalid": {
			cfg:    &promexp.ExporterConfig{},
			expErr: errors.New("invalid exporter config"),
		},
		"negative port": {
			cfg: &promexp.ExporterConfig{
				Port: -1,
			},
			expErr: errors.New("invalid exporter config"),
		},
		"nil register fn": {
			cfg: &promexp.ExporterConfig{
				Port: 1234,
			},
			expErr: errors.New("invalid exporter config"),
		},
		"register fn fails": {
			cfg: &promexp.ExporterConfig{
				Port: 1234,
				Register: func(context.Context, logging.Logger) error {
					return errors.New("whoops")
				},
			},
			expErr: errors.New("failed to register"),
		},
		"success": {
			cfg: &promexp.ExporterConfig{
				Port: promexp.ClientTelemetryPort,
				Register: func(ctx context.Context, log logging.Logger) error {
					return nil
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.cfg != nil {
				tc.cfg.Title = t.Name()
			}
			cleanup, err := promexp.StartExporter(test.Context(t), log, tc.cfg)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			// Quick tests to make sure the exporter is listening and
			// that our handlers are invoked.
			var resp *http.Response
			for {
				var err error
				resp, err = http.Get(fmt.Sprintf("http://localhost:%d/", tc.cfg.Port))
				if err == nil {
					break
				}
				log.Errorf("failed to connect to exporter: %+v", err)
				time.Sleep(100 * time.Millisecond)
			}

			body, err := io.ReadAll(resp.Body)
			if err != nil {
				t.Fatal(err)
			}
			if !strings.Contains(string(body), tc.cfg.Title) {
				t.Fatalf("expected %q to contain %q", string(body), tc.cfg.Title)
			}
			resp.Body.Close()

			resp, err = http.Get(fmt.Sprintf("http://localhost:%d/metrics", tc.cfg.Port))
			if err != nil {
				t.Fatal(err)
			}
			resp.Body.Close()

			cleanup()
			time.Sleep(1 * time.Second)

			// Make sure the exporter is no longer listening.
			_, err = http.Get(fmt.Sprintf("http://localhost:%d/", tc.cfg.Port))
			if err == nil {
				t.Fatal("expected http Get to fail on closed port")
			}
		})
	}
}

package client

import (
	"context"
	"os"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestClient_Init(t *testing.T) {
	for name, tc := range map[string]struct {
		ctx    context.Context
		cfg    *MockApiClientConfig
		expErr error
	}{
		"Init() already called": {
			ctx: func() context.Context {
				t.Helper()
				b := &daosClientBinding{}
				ctx, err := setApiClient(context.Background(), b)
				if err != nil {
					t.Fatal(err)
				}
				return ctx
			}(),
			expErr: daos.Already,
		},
		"Init() called after Fini() (same context)": {
			ctx: func() context.Context {
				t.Helper()
				parent, cancel := context.WithCancel(context.Background())
				b := &daosClientBinding{
					initialized: atm.NewBool(true),
					cancelCtx:   cancel,
				}
				ctx, err := setApiClient(parent, b)
				if err != nil {
					t.Fatal(err)
				}
				Fini(ctx)
				return ctx
			}(),
			expErr: context.Canceled,
		},
		"Init() fails with -DER_NOMEM": {
			cfg: &MockApiClientConfig{
				ReturnCodeMap: map[string]int{
					"daos_init": int(daos.NoMemory),
				},
			},
			expErr: daos.NoMemory,
		},
		"Init() (unmocked) fails with -DER_AGENT_COMM": {
			ctx: func() context.Context {
				// Ensure that we don't connect to a running agent
				if err := os.Setenv("DAOS_AGENT_DRPC_DIR", "/nonexistent"); err != nil {
					t.Fatal(err)
				}
				return context.Background()
			}(),
			expErr: daos.AgentCommErr,
		},
		"successful Init() (mocked)": {
			cfg: &MockApiClientConfig{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.ctx == nil {
				var err error
				tc.ctx, err = MockApiClientContext(context.Background(), tc.cfg)
				if err != nil {
					t.Fatal(err)
				}
			}
			_, gotErr := Init(tc.ctx)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestClient_Fini(t *testing.T) {
	for name, tc := range map[string]struct {
		ctx    context.Context
		cfg    *MockApiClientConfig
		expErr error
	}{
		"Fini() already called is idempotent": {
			ctx: func() context.Context {
				t.Helper()
				parent, cancel := context.WithCancel(context.Background())
				b := &daosClientBinding{
					cancelCtx: cancel,
				}
				ctx, err := setApiClient(parent, b)
				if err != nil {
					t.Fatal(err)
				}
				Fini(ctx)
				return ctx
			}(),
		},
		"Fini() fails with -DER_NOMEM": {
			cfg: &MockApiClientConfig{
				ReturnCodeMap: map[string]int{
					"daos_fini": int(daos.NoMemory),
				},
			},
			expErr: daos.NoMemory,
		},
		"successful Fini() (mocked)": {
			cfg: &MockApiClientConfig{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.ctx == nil {
				ctx, err := MockApiClientContext(context.Background(), tc.cfg)
				if err != nil {
					t.Fatal(err)
				}
				tc.ctx, err = Init(ctx)
				if err != nil {
					t.Fatal(err)
				}
			}
			gotErr := Fini(tc.ctx)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

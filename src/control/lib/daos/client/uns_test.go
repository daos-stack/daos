package client_test

import (
	"context"
	"syscall"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/daos/client"
)

func TestClient_ResolveContainerPath(t *testing.T) {
	connPool := uuid.New()
	connCont := uuid.New()

	for name, tc := range map[string]struct {
		ctx    context.Context
		path   string
		cfg    *client.MockApiClientConfig
		expErr error
	}{
		"nonexistent dfuse path": {
			cfg: &client.MockApiClientConfig{
				ReturnCodeMap: map[string]int{
					"dfuse_ioctl": int(syscall.ENOENT),
				},
			},
			expErr: daos.Nonexistent,
		},
		"successful dfuse path resolution": {
			cfg: &client.MockApiClientConfig{
				ConnectedPool: connPool,
				ConnectedCont: connCont,
			},
		},
		"duns path not valid": {
			cfg: &client.MockApiClientConfig{
				ReturnCodeMap: map[string]int{
					"dfuse_ioctl":       int(syscall.ENOTTY),
					"duns_resolve_path": int(syscall.ENODATA),
				},
			},
			expErr: daos.BadPath,
		},
		"successful duns path resolution": {
			cfg: &client.MockApiClientConfig{
				ConnectedPool: connPool,
				ConnectedCont: connCont,
				ReturnCodeMap: map[string]int{
					"dfuse_ioctl": int(syscall.ENOTTY),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.ctx == nil {
				var err error
				tc.ctx, err = client.MockApiClientContext(context.Background(), tc.cfg)
				if err != nil {
					t.Fatal(err)
				}
			}
			poolID, contID, err := client.ResolveContainerPath(tc.ctx, tc.path)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(connPool.String(), poolID); diff != "" {
				t.Fatalf("unexpected pool (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(connCont.String(), contID); diff != "" {
				t.Fatalf("unexpected container (-want, +got):\n%s\n", diff)
			}
		})
	}
}

package api_test

import (
	"context"
	"syscall"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/api"
)

func TestClient_ResolveContainerPath(t *testing.T) {
	connPool := uuid.New()
	connCont := uuid.New()

	for name, tc := range map[string]struct {
		ctx    context.Context
		path   string
		cfg    *daosAPI.MockApiClientConfig
		expErr error
	}{
		"nonexistent dfuse path": {
			cfg: &daosAPI.MockApiClientConfig{
				ReturnCodeMap: map[string]int{
					"dfuse_ioctl": int(syscall.ENOENT),
				},
			},
			expErr: daos.Nonexistent,
		},
		"successful dfuse path resolution": {
			cfg: &daosAPI.MockApiClientConfig{
				ConnectedPool: connPool,
				ConnectedCont: connCont,
			},
		},
		"duns path not valid": {
			cfg: &daosAPI.MockApiClientConfig{
				ReturnCodeMap: map[string]int{
					"dfuse_ioctl":       int(syscall.ENOTTY),
					"duns_resolve_path": int(syscall.ENODATA),
				},
			},
			expErr: daos.BadPath,
		},
		"successful duns path resolution": {
			cfg: &daosAPI.MockApiClientConfig{
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
				tc.ctx, err = daosAPI.MockApiClientContext(context.Background(), tc.cfg)
				if err != nil {
					t.Fatal(err)
				}
			}
			poolID, contID, err := daosAPI.ResolveContainerPath(tc.ctx, tc.path)
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

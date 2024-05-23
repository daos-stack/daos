//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package systemd_test

import (
	"errors"
	"net"
	"os"
	"path/filepath"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/systemd"
)

func Test_Systemd_SdNotify(t *testing.T) {
	testStr := "TEST=1"
	setupSock := func(t *testing.T) string {
		t.Helper()
		sockFile := filepath.Join(t.TempDir(), "notify.sock")
		l, err := net.Listen("unixgram", sockFile)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() {
			l.Close()
		})
		return sockFile
	}

	for name, tc := range map[string]struct {
		sockFile string
		expErr   error
	}{
		"success": {
			sockFile: setupSock(t),
		},
		"no socket": {
			expErr: systemd.ErrSdNotifyNoSocket,
		},
		"bad socket": {
			sockFile: "/dev/null",
			expErr: &net.OpError{
				Err: errors.New("connect: connection refused"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			os.Setenv("NOTIFY_SOCKET", tc.sockFile)
			defer os.Unsetenv("NOTIFY_SOCKET")

			err := systemd.SdNotify(testStr)
			test.CmpErr(t, tc.expErr, err)
		})
	}
}

//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package dlopen

import (
	"fmt"
	"testing"
)

func checkFailure(shouldSucceed bool, err error) (rErr error) {
	switch {
	case err != nil && shouldSucceed:
		rErr = fmt.Errorf("expected test to succeed, failed unexpectedly: %v", err)
	case err == nil && !shouldSucceed:
		rErr = fmt.Errorf("expected test to fail, succeeded unexpectedly")
	}

	return
}

func TestDlopen(t *testing.T) {
	tests := []struct {
		libs          []string
		shouldSucceed bool
	}{
		{
			libs: []string{
				"libc.so.6",
				"libc.so",
			},
			shouldSucceed: true,
		},
		{
			libs: []string{
				"libstrange.so",
			},
			shouldSucceed: false,
		},
	}

	for i, tt := range tests {
		expLen := 4
		len, err := strlen(tt.libs, "test")
		if checkFailure(tt.shouldSucceed, err) != nil {
			t.Errorf("case %d: %v", i, err)
		}

		if tt.shouldSucceed && len != expLen {
			t.Errorf("case %d: expected length %d, got %d", i, expLen, len)
		}
	}
}

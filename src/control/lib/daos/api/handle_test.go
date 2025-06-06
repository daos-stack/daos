//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"testing"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestAPI_connHandle(t *testing.T) {
	testUUID := test.MockPoolUUID(42)

	for name, tc := range map[string]struct {
		ch         *connHandle
		expIsValid bool
		expID      string
		expString  string
	}{
		"nil handle": {
			expIsValid: false,
			expID:      "<nil>",
			expString:  "<nil>:false",
		},
		"valid handle (label+uuid)": {
			ch: &connHandle{
				daosHandle: *defaultPoolHdl(),
				UUID:       testUUID,
				Label:      testPoolName,
			},
			expIsValid: true,
			expID:      testPoolName,
			expString:  testPoolName + ":true",
		},
		"valid handle (uuid)": {
			ch: &connHandle{
				daosHandle: *defaultPoolHdl(),
				UUID:       testUUID,
			},
			expIsValid: true,
			expID:      testUUID.String(),
			expString:  logging.ShortUUID(testUUID) + ":true",
		},
		"invalid handle (no uuid or label)": {
			ch: &connHandle{
				daosHandle: *defaultPoolHdl(),
			},
			expIsValid: false,
			expID:      "<invalid>",
			expString:  "<invalid>:false",
		},
		"invalid handle (daos handle zeroed)": {
			ch: &connHandle{
				daosHandle: _Ctype_daos_handle_t{},
				UUID:       testUUID,
				Label:      testPoolName,
			},
			expIsValid: false,
			expID:      "<invalid>",
			expString:  "<invalid>:false",
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)

			test.CmpAny(t, "IsValid()", tc.expIsValid, tc.ch.IsValid())
			test.CmpAny(t, "ID()", tc.expID, tc.ch.ID())
			test.CmpAny(t, "String()", tc.expString, tc.ch.String())
			if tc.ch == nil || !tc.expIsValid {
				return
			}

			tc.ch.invalidate()
			test.CmpAny(t, "IsValid()", false, tc.ch.IsValid())
			test.CmpAny(t, "ID()", "<invalid>", tc.ch.ID())
			test.CmpAny(t, "String()", "<invalid>:false", tc.ch.String())
		})
	}
}

func TestAPI_connHandle_FillHandle(t *testing.T) {
	for name, tc := range map[string]struct {
		th     *connHandle
		ptr    unsafe.Pointer
		expErr error
	}{
		"nil handle": {
			expErr: errors.New("invalid handle"),
		},
		"nil pointer": {
			th:     &defaultPoolHandle().connHandle,
			expErr: errors.New("nil DAOS handle pointer"),
		},
		"valid handle": {
			th:  &defaultPoolHandle().connHandle,
			ptr: unsafe.Pointer(&_Ctype_daos_handle_t{}),
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)

			gotErr := tc.th.FillHandle(tc.ptr)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cHdl := (*_Ctype_daos_handle_t)(tc.ptr)
			test.CmpAny(t, "cookie", tc.th.daosHandle.cookie, cHdl.cookie)
		})
	}
}

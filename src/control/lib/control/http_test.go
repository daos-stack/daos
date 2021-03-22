//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"io"
	"net/http"
	"net/url"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

type mockReadCloser struct {
	reader  io.Reader
	readErr error
}

func (m *mockReadCloser) Read(buf []byte) (int, error) {
	if m.readErr != nil {
		return 0, m.readErr
	}
	return m.reader.Read(buf)
}

func (m *mockReadCloser) Close() error {
	return nil
}

func newMockReadCloser(content string) *mockReadCloser {
	return &mockReadCloser{
		reader: strings.NewReader(content),
	}
}

func newErrMockReadCloser(err error) *mockReadCloser {
	return &mockReadCloser{
		readErr: err,
	}
}

func TestControl_httpGetBody(t *testing.T) {
	defaultURL := &url.URL{Host: "testhost"}

	for name, tc := range map[string]struct {
		url       *url.URL
		timeout   time.Duration
		getFn     httpGetFn
		expResult []byte
		expErr    error
	}{
		"nil url": {
			expErr: errors.New("nil URL"),
		},
		"empty URL": {
			url:    &url.URL{},
			expErr: errors.New("host address is required"),
		},
		"nil getFn": {
			url:    defaultURL,
			expErr: errors.New("nil get function"),
		},
		"getFn error": {
			url: defaultURL,
			getFn: func(_ string) (*http.Response, error) {
				return nil, errors.New("mock getFn")
			},
			expErr: errors.New("mock getFn"),
		},
		"http.Response error": {
			url: defaultURL,
			getFn: func(_ string) (*http.Response, error) {
				return &http.Response{
					StatusCode: http.StatusNotFound,
					Body:       newMockReadCloser(""),
				}, nil
			},
			expErr: errors.New("HTTP response error: 404 Not Found"),
		},
		"empty body": {
			url: defaultURL,
			getFn: func(_ string) (*http.Response, error) {
				return &http.Response{
					StatusCode: http.StatusOK,
					Body:       newMockReadCloser(""),
				}, nil
			},
			expResult: []byte{},
		},
		"success with body": {
			url: defaultURL,
			getFn: func(_ string) (*http.Response, error) {
				return &http.Response{
					StatusCode: http.StatusOK,
					Body:       newMockReadCloser("this is the body of an HTTP response"),
				}, nil
			},
			expResult: []byte("this is the body of an HTTP response"),
		},
		"reading body fails": {
			url: defaultURL,
			getFn: func(_ string) (*http.Response, error) {
				return &http.Response{
					StatusCode: http.StatusOK,
					Body:       newErrMockReadCloser(errors.New("mock Read")),
				}, nil
			},
			expErr: errors.New("reading HTTP response body: mock Read"),
		},
		"request times out": {
			url:     defaultURL,
			timeout: 5 * time.Millisecond,
			getFn: func(_ string) (*http.Response, error) {
				time.Sleep(1 * time.Second)
				return &http.Response{
					StatusCode: http.StatusOK,
					Body:       newMockReadCloser(""),
				}, nil
			},
			expErr: errors.New("context deadline exceeded"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := context.Background()
			if tc.timeout != 0 {
				timedCtx, cancel := context.WithTimeout(ctx, tc.timeout)
				defer cancel()
				ctx = timedCtx
			}

			result, err := httpGetBody(ctx, tc.url, tc.getFn)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

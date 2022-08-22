//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"bytes"
	"context"
	"io"
	"net/http"
	"net/url"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestControl_httpReq_canRetry(t *testing.T) {
	validReq := &httpReq{
		url: &url.URL{
			Scheme: "http",
			Host:   "testhost:1234",
		},
	}
	for name, tc := range map[string]struct {
		req       *httpReq
		inErr     error
		cur       uint
		expResult bool
	}{
		"nil": {}, // do not crash
		"nil URL": {
			req: &httpReq{},
		},
		"generic error": {
			req:   validReq,
			inErr: errors.New("something bad happened"),
		},
		"retryable error": {
			req:       validReq,
			inErr:     HTTPReqTimedOut(validReq.url.String()),
			expResult: true,
		},
		"max iterations": {
			req:   validReq,
			cur:   httpMaxRetries,
			inErr: HTTPReqTimedOut(validReq.url.String()),
		},
		"greater than max iterations": {
			req:   validReq,
			cur:   httpMaxRetries + 1,
			inErr: HTTPReqTimedOut(validReq.url.String()),
		},
		"just below max iterations": {
			req:       validReq,
			cur:       httpMaxRetries - 1,
			inErr:     HTTPReqTimedOut(validReq.url.String()),
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.req.canRetry(tc.inErr, tc.cur)

			test.AssertEqual(t, tc.expResult, result, "")
		})
	}
}

func TestControl_httpReq_onRetry(t *testing.T) {
	req := &httpReq{}
	err := req.onRetry(context.TODO(), 0)
	if err != nil {
		t.Fatalf("expected nil, got: %s", err.Error())
	}
}

func TestControl_httpReq_retryAfter(t *testing.T) {
	req := &httpReq{}
	result := req.retryAfter(0)
	test.AssertEqual(t, time.Second, result, "")
}

func TestControl_httpReq_getRetryTimeout(t *testing.T) {
	req := &httpReq{}
	result := req.getRetryTimeout()
	test.AssertEqual(t, httpReqTimeout, result, "")
}

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
		cancelCtx bool
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
			expErr: HTTPReqTimedOut(defaultURL.String()),
		},
		"request canceled": {
			url:       defaultURL,
			cancelCtx: true,
			getFn: func(_ string) (*http.Response, error) {
				time.Sleep(1 * time.Second)
				return &http.Response{
					StatusCode: http.StatusOK,
					Body:       newMockReadCloser(""),
				}, nil
			},
			expErr: context.Canceled,
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := context.Background()
			var cancel func()
			if tc.cancelCtx {
				ctx, cancel = context.WithCancel(ctx)
				go func() {
					cancel()
				}()
			}

			if tc.timeout == 0 {
				tc.timeout = time.Second
			}

			result, err := httpGetBody(ctx, tc.url, tc.getFn, tc.timeout)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

type mockHTTPGetter struct {
	numTimesToRetry uint
	canRetryCalled  uint
	onRetryErr      error
	getBodyResult   []byte
	getBodyErr      error
	getBodyCalled   uint
	getBodyFailures uint
}

func (r *mockHTTPGetter) canRetry(err error, cur uint) bool {
	r.canRetryCalled++
	return r.canRetryCalled <= r.numTimesToRetry
}

func (r *mockHTTPGetter) onRetry(ctx context.Context, _ uint) error {
	return r.onRetryErr
}

func (r *mockHTTPGetter) retryAfter(_ time.Duration) time.Duration {
	return 0
}

func (r *mockHTTPGetter) getRetryTimeout() time.Duration {
	return 500 * time.Millisecond
}

func (r *mockHTTPGetter) getURL() *url.URL {
	return &url.URL{
		Scheme: "http",
		Host:   "testhost",
	}
}

func (r *mockHTTPGetter) getBody(ctx context.Context) ([]byte, error) {
	r.getBodyCalled++
	if r.getBodyCalled <= r.getBodyFailures {
		return nil, r.getBodyErr
	}
	return r.getBodyResult, nil
}

func TestControl_httpGetBodyRetry(t *testing.T) {
	for name, tc := range map[string]struct {
		req       *mockHTTPGetter
		expResult []byte
		expErr    error
	}{
		"no error": {
			req: &mockHTTPGetter{
				getBodyResult: []byte("hello world"),
			},
			expResult: []byte("hello world"),
		},
		"cannot retry": {
			req: &mockHTTPGetter{
				getBodyErr:      errors.New("mock getBody"),
				getBodyFailures: 1,
			},
			expErr: errors.New("mock getBody"),
		},
		"retry succeeds": {
			req: &mockHTTPGetter{
				getBodyResult:   []byte("hello world"),
				getBodyErr:      errors.New("mock getBody"),
				getBodyFailures: 1,
				numTimesToRetry: 1,
			},
			expResult: []byte("hello world"),
		},
		"retry fails": {
			req: &mockHTTPGetter{
				getBodyResult:   []byte("hello world"),
				getBodyErr:      errors.New("mock getBody"),
				getBodyFailures: 2,
				numTimesToRetry: 1,
			},
			expErr: errors.New("mock getBody"),
		},
		"onRetry fails": {
			req: &mockHTTPGetter{
				getBodyResult:   []byte("hello world"),
				getBodyErr:      errors.New("mock getBody"),
				getBodyFailures: 1,
				numTimesToRetry: 1,
				onRetryErr:      errors.New("mock onRetry"),
			},
			expErr: errors.New("mock onRetry"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := httpGetBodyRetry(context.TODO(), tc.req)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, bytes.Compare(tc.expResult, result), 0, "")
		})
	}
}

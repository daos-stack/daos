//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"time"

	"github.com/pkg/errors"
)

// httpReqTimeout is the default timeout for HTTP requests, if the caller
// didn't set a shorter one on the context.
const httpReqTimeout = 30 * time.Second

// httpMaxRetries is the number of retries attempted if the HTTP request times out.
const httpMaxRetries = 5

// HTTPReqTimedOut creates an error indicating that an HTTP request timed out.
func HTTPReqTimedOut(url string) error {
	return fmt.Errorf("HTTP request %q: timed out", url)
}

// httpGetFn represents a function that conforms to the parameters/return values
// of http.Get
type httpGetFn func(string) (*http.Response, error)

type httpGetter interface {
	retryer
	getURL() *url.URL
	getBody(context.Context) ([]byte, error)
}

type httpReq struct {
	url       *url.URL
	getFn     httpGetFn
	getBodyFn func(context.Context, *url.URL, httpGetFn, time.Duration) ([]byte, error)
}

func (r *httpReq) canRetry(err error, cur uint) bool {
	if r == nil || r.url == nil {
		return false
	}

	if cur >= httpMaxRetries {
		return false
	}

	if errors.Cause(err).Error() == HTTPReqTimedOut(r.getURL().String()).Error() {
		return true
	}

	return false
}

func (r *httpReq) onRetry(ctx context.Context, _ uint) error {
	return nil
}

func (r *httpReq) retryAfter(_ time.Duration) time.Duration {
	return time.Second
}

func (r *httpReq) getRetryTimeout() time.Duration {
	return httpReqTimeout
}

func (r *httpReq) getURL() *url.URL {
	return r.url
}

func (r *httpReq) httpGetFunc() httpGetFn {
	if r.getFn == nil {
		r.getFn = http.Get
	}
	return r.getFn
}

func (r *httpReq) getBody(ctx context.Context) ([]byte, error) {
	if r.getBodyFn == nil {
		r.getBodyFn = httpGetBody
	}
	return r.getBodyFn(ctx, r.getURL(), r.httpGetFunc(), r.getRetryTimeout())
}

func httpGetBodyRetry(ctx context.Context, req httpGetter) ([]byte, error) {
	var result []byte
	var err error
	for i := uint(0); ; i++ {
		result, err = req.getBody(ctx)
		if err == nil {
			break
		}

		if !req.canRetry(err, i) {
			return nil, err
		}

		time.Sleep(req.retryAfter(0))
		if err = req.onRetry(ctx, i); err != nil {
			return nil, err
		}
	}

	return result, err
}

// httpGetBody executes a simple HTTP GET request to a given URL and returns the
// content of the response body.
func httpGetBody(ctx context.Context, url *url.URL, get httpGetFn, timeout time.Duration) ([]byte, error) {
	if url == nil {
		return nil, errors.New("nil URL")
	}

	if len(url.Host) == 0 {
		return nil, errors.New("host address is required")
	}

	if get == nil {
		return nil, errors.New("nil get function")
	}

	httpCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	respChan := make(chan *http.Response)
	errChan := make(chan error)

	go func() {
		httpResp, err := get(url.String())
		if err != nil {
			errChan <- err
			return
		}

		respChan <- httpResp
	}()

	select {
	case <-httpCtx.Done():
		if httpCtx.Err() == context.DeadlineExceeded {
			return nil, HTTPReqTimedOut(url.String())
		}
		return nil, httpCtx.Err()
	case resp := <-respChan:
		defer resp.Body.Close()
		if resp.StatusCode < 200 || resp.StatusCode > 299 {
			return nil, errors.Errorf("HTTP response error: %d %s", resp.StatusCode, http.StatusText(resp.StatusCode))
		}

		result, err := io.ReadAll(resp.Body)
		if err != nil {
			return nil, errors.Wrap(err, "reading HTTP response body")
		}
		return result, nil
	case err := <-errChan:
		return nil, err
	}
}

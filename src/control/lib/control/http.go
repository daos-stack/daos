//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"io/ioutil"
	"net/http"
	"net/url"
	"time"

	"github.com/pkg/errors"
)

// httpReqTimeout is the default timeout for HTTP requests, if the caller
// didn't set a shorter one on the context.
const httpReqTimeout = 30 * time.Second

// httpGetFn represents a function that conforms to the parameters/return values
// of http.Get
type httpGetFn func(string) (*http.Response, error)

// httpGetBody executes a simple HTTP GET request to a given URL and returns the
// content of the response body.
func httpGetBody(ctx context.Context, url *url.URL, get httpGetFn) ([]byte, error) {
	if url == nil {
		return nil, errors.New("nil URL")
	}

	if len(url.Host) == 0 {
		return nil, errors.New("host address is required")
	}

	if get == nil {
		return nil, errors.New("nil get function")
	}

	httpCtx, cancel := context.WithTimeout(ctx, httpReqTimeout)
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
		return nil, httpCtx.Err()
	case resp := <-respChan:
		defer resp.Body.Close()
		if resp.StatusCode < 200 || resp.StatusCode > 299 {
			return nil, errors.Errorf("HTTP response error: %d %s", resp.StatusCode, http.StatusText(resp.StatusCode))
		}

		result, err := ioutil.ReadAll(resp.Body)
		if err != nil {
			return nil, errors.Wrap(err, "reading HTTP response body")
		}
		return result, nil
	case err := <-errChan:
		return nil, err
	}
}

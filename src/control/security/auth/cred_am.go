//
// (C) Copyright 2025 Hewlett Packard Enterprise.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package auth

import (
	"context"
	"crypto"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

type (
	CredentialRequestAM struct {
		delegationCredential string
		signingKey           crypto.PrivateKey
		callerID             string
		baseURL              string
	}

	AMInfo struct {
		Identity string   `json:"id"`
		Roles    []string `json:"roles"`
	}

	AMErr struct {
		ResponseCode int    `json:"error"`
		Message      string `json:"message"`
	}

	AMResp struct {
		Error AMErr  `json:"error"`
		Info  string `json:"info"`
	}
)

func (r *CredentialRequestAM) request_am(ctx context.Context, apiPath string, method string, kv ...string) ([]byte, error) {
	u, err := url.ParseRequestURI(r.baseURL)
	if err != nil {
		return nil, fmt.Errorf("check agent config to ensure AM url is correct (can't happen: %w)", err)
	}
	u.Path = apiPath
	params := url.Values{}
	if len(kv)%2 != 0 {
		return nil, fmt.Errorf("must have an even number of key/value pairs")
	}
	for i := 0; i < len(kv); i += 2 {
		params.Set(kv[i], kv[i+1])
	}

	params.Set("caller_id", r.callerID)
	u.RawQuery = params.Encode()

	request, err := http.NewRequestWithContext(
		ctx,
		method,
		u.String(),
		http.NoBody,
	)
	if err != nil {
		return nil, fmt.Errorf(`cannot create request for "%s": %w`, u.String(), err)
	}

	response, err := http.DefaultClient.Do(request)
	if err != nil {
		return nil, fmt.Errorf(`cannot access "%s": %w`, u.String(), err)
	}

	//goland:noinspection GoUnhandledErrorResult
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return nil, fmt.Errorf(`unexpected status code "%d"`, response.StatusCode)
	}
	responseBody, err := io.ReadAll(response.Body)
	if err != nil {
		return nil, fmt.Errorf(`error reading response from %s: %w`, u.String(), err)
	}
	return responseBody, err
}

func (r *CredentialRequestAM) validateAndParseDelegationCredential() (*AMInfo, error) {
	var amResp AMResp
	var authInfo AMInfo

	resp, err := r.request_am(context.Background(), "/validate", http.MethodGet, "credential", r.delegationCredential)
	if err != nil {
		return nil, errors.Wrap(err, "Failed to validate the provided credential - check AM server and agent configuration")
	}

	err = json.Unmarshal(resp, &amResp)
	if err != nil {
		return nil, err
	}

	if amResp.Error.ResponseCode != 0 {
		return nil, errors.New(amResp.Error.Message)
	}

	err = json.Unmarshal([]byte(amResp.Info), &authInfo)
	if err != nil {
		return nil, err
	}

	return &authInfo, nil
}

func (req *CredentialRequestAM) AllocCredentialRequest() CredentialRequest {
	return &CredentialRequestAM{}
}

func (req *CredentialRequestAM) InitCredentialRequest(log logging.Logger, sec_cfg *security.CredentialConfig, session *drpc.Session, req_body []byte, key crypto.PrivateKey) error {
	req.delegationCredential = string(req_body)
	req.signingKey = key
	req.callerID = sec_cfg.AMConfig.CallerID
	req.baseURL = sec_cfg.AMConfig.BaseURL

	return nil
}

// GetSignedCredential returns a credential based on the provided domain info and
// signing key.
func (req *CredentialRequestAM) GetSignedCredential(log logging.Logger, ctx context.Context) (*Credential, error) {
	// DAOS is built with UNIX groups in mind. We cannot use the AM-style URLS as they contain colons, and do not end with an '@'.
	// This function helps parse out the path.
	seperate := func(url string) (string, string, error) {
		var split_url = strings.Split(url, "://")
		if len(split_url) != 2 {
			return "", "", errors.New("Access manager url does not follow expected format 'foo://bar/...'")
		}
		split_url[0] += "://"
		split_url[1] += "@am"
		return split_url[0], split_url[1], nil
	}

	authInfo, err := req.validateAndParseDelegationCredential()
	if err != nil {
		return nil, err
	}

	machine_name, identity, err := seperate(authInfo.Identity)

	if err != nil {
		return nil, err
	}

	groups := make([]string, len(authInfo.Roles))
	for i := range groups {
		var _, role, err = seperate(authInfo.Roles[i])
		if err != nil {
			return nil, err
		}
		groups[i] = role
	}

	// Craft AuthToken
	sys := Sys{
		Stamp:       0,
		Machinename: machine_name,
		User:        identity,
		Group:       "",
		Groups:      groups,
		Secctx:      ""}

	// Marshal our AuthSys token into a byte array
	tokenBytes, err := proto.Marshal(&sys)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to marshal AuthSys token")
	}

	token := Token{
		Flavor: Flavor_AUTH_SYS,
		Data:   tokenBytes}

	verifier, err := VerifierFromToken(req.signingKey, &token)
	if err != nil {
		return nil, errors.WithMessage(err, "Unable to generate verifier")
	}

	verifierToken := Token{
		Flavor: Flavor_AUTH_SYS,
		Data:   verifier}

	credential := Credential{
		Token:    &token,
		Verifier: &verifierToken,
		Origin:   "agent"}

	logging.FromContext(ctx).Tracef("%s: successfully signed credential", authInfo)

	return &credential, nil
}

func (req *CredentialRequestAM) CredReqKey() string {
	return req.delegationCredential
}

func (req CredentialRequestAM) GetAuthName() AuthTag {
	return (AuthTag)([]byte("acma"))
}

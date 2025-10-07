//
// (C) Copyright 2018-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package auth

import (
	"context"
	"crypto"
	"fmt"
	"net"
	"os"
	"os/user"
	"reflect"
	"strconv"
	"strings"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

func sysNameToPrincipalName(name string) string {
	return name + "@"
}

func stripHostName(name string) string {
	return strings.Split(name, ".")[0]
}

// GetMachineName returns the "short" hostname by stripping the domain from the FQDN.
func GetMachineName() (string, error) {
	name, err := os.Hostname()
	if err != nil {
		return "", err
	}

	return stripHostName(name), nil
}

type (
	GetSignedCredentialInternalFn func(ctx context.Context, req *CredentialRequestUnix) (*Credential, error)

	getHostnameFn   func() (string, error)
	getUserFn       func(string) (*user.User, error)
	getGroupFn      func(string) (*user.Group, error)
	getGroupIdsFn   func(*CredentialRequestUnix) ([]string, error)
	getGroupNamesFn func(*CredentialRequestUnix) ([]string, error)

	// CredentialRequest defines the request parameters for GetSignedCredential.
	CredentialRequestUnix struct {
		DomainInfo                  *security.DomainInfo
		signingKey                  crypto.PrivateKey
		getHostname                 getHostnameFn
		getUser                     getUserFn
		getGroup                    getGroupFn
		getGroupIds                 getGroupIdsFn
		getGroupNames               getGroupNamesFn
		clientMap                   *security.ClientUserMap
		GetSignedCredentialInternal GetSignedCredentialInternalFn
	}
)

// NewCredentialRequest returns default instantiation of CredentialRequest.
func NewCredentialRequest(info *security.DomainInfo, key crypto.PrivateKey) *CredentialRequestUnix {
	return &CredentialRequestUnix{
		DomainInfo:                  info,
		signingKey:                  key,
		getHostname:                 GetMachineName,
		getUser:                     user.LookupId,
		getGroup:                    user.LookupGroupId,
		getGroupIds:                 getGroupIds,
		getGroupNames:               getGroupNames,
		GetSignedCredentialInternal: GetSignedCredentialInternalImpl,
	}
}

func getGroupIds(req *CredentialRequestUnix) ([]string, error) {
	u, err := req.user()
	if err != nil {
		return nil, err
	}
	return u.GroupIds()
}

func getGroupNames(req *CredentialRequestUnix) ([]string, error) {
	groupIds, err := req.getGroupIds(req)
	if err != nil {
		return nil, err
	}

	groupNames := make([]string, len(groupIds))
	for i, gID := range groupIds {
		g, err := req.getGroup(gID)
		if err != nil {
			return nil, err
		}
		groupNames[i] = g.Name
	}

	return groupNames, nil
}

func (r *CredentialRequestUnix) hostname() (string, error) {
	if r.getHostname == nil {
		return "", errors.New("hostname lookup function not set")
	}

	hostname, err := r.getHostname()
	if err != nil {
		return "", errors.Wrap(err, "failed to get hostname")
	}
	return stripHostName(hostname), nil
}

func (r *CredentialRequestUnix) user() (*user.User, error) {
	if r.getUser == nil {
		return nil, errors.New("user lookup function not set")
	}
	return r.getUser(strconv.Itoa(int(r.DomainInfo.Uid())))
}

func (r *CredentialRequestUnix) userPrincipal() (string, error) {
	u, err := r.user()
	if err != nil {
		return "", err
	}
	return sysNameToPrincipalName(u.Username), nil
}

func (r *CredentialRequestUnix) group() (*user.Group, error) {
	if r.getGroup == nil {
		return nil, errors.New("group lookup function not set")
	}
	return r.getGroup(strconv.Itoa(int(r.DomainInfo.Gid())))
}

func (r *CredentialRequestUnix) groupPrincipal() (string, error) {
	g, err := r.group()
	if err != nil {
		return "", err
	}
	return sysNameToPrincipalName(g.Name), nil
}

func (r *CredentialRequestUnix) groupPrincipals() ([]string, error) {
	if r.getGroupNames == nil {
		return nil, errors.New("groupNames function not set")
	}

	groupNames, err := r.getGroupNames(r)
	if err != nil {
		return nil, errors.Wrap(err, "failed to get group names")
	}

	for i, g := range groupNames {
		groupNames[i] = sysNameToPrincipalName(g)
	}
	return groupNames, nil
}

// WithUserAndGroup provides an override to set the user, group, and optional list
// of group names to be used for the request.
func (r *CredentialRequestUnix) WithUserAndGroup(userStr, groupStr string, groupStrs ...string) {
	r.getUser = func(id string) (*user.User, error) {
		return &user.User{
			Uid:      id,
			Gid:      id,
			Username: userStr,
		}, nil
	}
	r.getGroup = func(id string) (*user.Group, error) {
		return &user.Group{
			Gid:  id,
			Name: groupStr,
		}, nil
	}
	r.getGroupNames = func(*CredentialRequestUnix) ([]string, error) {
		return groupStrs, nil
	}
}

func (req *CredentialRequestUnix) AllocCredentialRequest() CredentialRequest {
	return &CredentialRequestUnix{}
}

func (req *CredentialRequestUnix) InitCredentialRequest(log logging.Logger, sec_cfg *security.CredentialConfig, session *drpc.Session, req_body []byte, key crypto.PrivateKey) error {
	if session == nil {
		return drpc.NewFailureWithMessage("session is nil")
	}

	uConn, ok := session.Conn.(*net.UnixConn)
	if !ok {
		return drpc.NewFailureWithMessage("connection is not a unix socket")
	}

	info, err := security.DomainInfoFromUnixConn(log, uConn)
	if err != nil {
		log.Errorf("Unable to get credentials for client socket: %s", err)
		return daos.MiscError
	}

	req.DomainInfo = info
	req.signingKey = key
	req.getHostname = GetMachineName
	req.getUser = user.LookupId
	req.getGroup = user.LookupGroupId
	req.getGroupIds = getGroupIds
	req.getGroupNames = getGroupNames
	req.clientMap = &sec_cfg.ClientUserMap
	req.GetSignedCredentialInternal = GetSignedCredentialInternalImpl

	return nil
}

// GetSignedCredential returns a credential based on the provided domain info and
// signing key.
func GetSignedCredentialInternalImpl(ctx context.Context, req *CredentialRequestUnix) (*Credential, error) {
	if req == nil {
		return nil, errors.Errorf("%T is nil", req)
	}

	if req.DomainInfo == nil {
		return nil, errors.New("No domain info supplied")
	}

	hostname, err := req.hostname()
	if err != nil {
		return nil, err
	}

	userPrinc, err := req.userPrincipal()
	if err != nil {
		return nil, err
	}

	groupPrinc, err := req.groupPrincipal()
	if err != nil {
		return nil, err
	}

	groupPrincs, err := req.groupPrincipals()
	if err != nil {
		return nil, err
	}

	// Craft AuthToken
	sys := Sys{
		Stamp:       0,
		Machinename: hostname,
		User:        userPrinc,
		Group:       groupPrinc,
		Groups:      groupPrincs,
		Secctx:      req.DomainInfo.Ctx()}

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

	logging.FromContext(ctx).Tracef("%s: successfully signed credential", req.DomainInfo)
	return &credential, nil
}

// To satisfy the unit tests, https://stackoverflow.com/a/76595928
func IsNilish(val any) bool {
	if val == nil {
		return true
	}

	v := reflect.ValueOf(val)
	k := v.Kind()
	switch k {
	case reflect.Chan, reflect.Func, reflect.Map, reflect.Pointer,
		reflect.UnsafePointer, reflect.Interface, reflect.Slice:
		return v.IsNil()
	}

	return false
}

// Unix auth has custom error handling logic for UnknownUserIDError. To solve this we
// use a helper function - getSignedCredentialInternal - and hide
func (req *CredentialRequestUnix) GetSignedCredential(log logging.Logger, ctx context.Context) (*Credential, error) {
	if IsNilish(req) {
		return nil, errors.New("is nil")
	}

	cred, err := req.GetSignedCredentialInternal(ctx, req)
	if err != nil {
		if req.DomainInfo == nil {
			return nil, err
		}
		if err := func() error {
			if !errors.Is(err, user.UnknownUserIdError(req.DomainInfo.Uid())) {
				return err
			}

			mu := req.clientMap.Lookup(req.DomainInfo.Uid())
			if mu == nil {
				return user.UnknownUserIdError(req.DomainInfo.Uid())
			}

			req.WithUserAndGroup(mu.User, mu.Group, mu.Groups...)
			cred, err = req.GetSignedCredentialInternal(ctx, req)
			if err != nil {
				return err
			}

			return nil
		}(); err != nil {
			log.Errorf("%s: failed to get user credential: %s", req.DomainInfo, err)
			return nil, err
		}
	}
	return cred, nil
}

func (req *CredentialRequestUnix) CredReqKey() string {
	return fmt.Sprintf("%d:%d:%s", req.DomainInfo.Uid(), req.DomainInfo.Gid(), req.DomainInfo.Ctx())
}

func (req CredentialRequestUnix) GetAuthName() AuthTag {
	return (AuthTag)([]byte("unix"))
}

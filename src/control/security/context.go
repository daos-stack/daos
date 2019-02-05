//
// (C) Copyright 2018 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package security

import (
	"errors"
	"fmt"

	pb "github.com/daos-stack/daos/src/control/security/proto"

	uuid "github.com/satori/go.uuid"
)

// Context holds the accounting information for requested
// security contexts. It also holds the security resentials presented
// by the DAOS agent.
type Context struct {
	requestor string
	shared    map[string]bool
	alive     bool
	token     *pb.AuthToken
	refcount  uint
}

// isSharedWith determined if the requestor can have access to the credential
func (s *Context) isSharedWith(requestor string) bool {
	_, ok := s.shared[requestor]
	return ok
}

// unshare will remove the requestor from the list of entities
// that the context is shared with.
func (s *Context) unshare(requestor string) {
	_, ok := s.shared[requestor]
	if ok {
		delete(s.shared, requestor)
	}
}

// NewContext creates a new instance of a securityContext object given
// the requestor and auth token
//	requestor: The host making the request
//	token: The opaque AuthToken related to the handle
func NewContext(requestor string, token *pb.AuthToken) *Context {
	return &Context{requestor, make(map[string]bool), true, token, 0}
}

// ContextMap is the datastructure for managing security contexts
type ContextMap struct {
	ctxmap map[uuid.UUID]*Context
}

// AddToken adds a new security context to the map returning the UUID
// that is generated for the context.
func (s *ContextMap) AddToken(requestor string, token *pb.AuthToken) (*uuid.UUID, error) {
	key, err := uuid.NewV4()

	if err != nil {
		return nil, fmt.Errorf("Unable to generate UUIDv4 UUID: %s", err)
	}
	s.ctxmap[key] = NewContext(requestor, token)
	return &key, nil
}

// GetToken retrieves the AuthToken represented by the UUID provided and
// increases its reference count.
func (s *ContextMap) GetToken(key uuid.UUID) *pb.AuthToken {
	ctx, ok := s.ctxmap[key]

	if ok {
		ctx.refcount++
		return ctx.token
	}
	return nil
}

// PutToken reduces the refcount of the securityContext object and
// removes it from the map if there are no references held.
func (s *ContextMap) PutToken(key uuid.UUID) error {
	ctx, ok := s.ctxmap[key]

	if ok {
		if ctx.refcount < 1 {
			return errors.New("Trying to put a key that should already be deleted")
		}
		ctx.refcount--
		if ctx.refcount == 0 {
			delete(s.ctxmap, key)
		}
	} else {
		return errors.New("Trying to remove a nonexistant key")
	}
	return nil
}

// FinalizeToken is used to mark a handle as no longer valid. This can
// be used to perform system wide recovation of a handle.
func (s *ContextMap) FinalizeToken(requestor string, key uuid.UUID) error {
	ctx, ok := s.ctxmap[key]

	if ok {
		if ctx.requestor == requestor || ctx.isSharedWith(requestor) {
			ctx.alive = false
			s.PutToken(key)
		} else {
			return errors.New("Trying to finalize a context you don't own")
		}
	} else {
		return errors.New("Trying to finalize a nonexistant key")
	}
	return nil
}

// ShareToken validates the person sharing the token owns it and adds
// the entity to share it with to the shared map
func (s *ContextMap) ShareToken(requestor string, toshare string, key uuid.UUID) error {
	ctx, ok := s.ctxmap[key]

	if ok {
		if ctx.requestor == requestor {
			ctx.shared[toshare] = true
		} else {
			return errors.New("Trying to share a context you don't own")
		}
	} else {
		return errors.New("Trying to share a nonexistant key")
	}
	return nil
}

// UnshareToken removes the requestor from the shared map.
func (s *ContextMap) UnshareToken(requestor string, key uuid.UUID) error {
	ctx, ok := s.ctxmap[key]

	if ok {
		if ctx.requestor == requestor || ctx.isSharedWith(requestor) {
			ctx.unshare(requestor)
		} else {
			return errors.New("Trying to unshare a context you don't own")
		}
	} else {
		return errors.New("Trying to unshare a nonexistant key")
	}
	return nil
}

// NewContextMap initializes a contextMap object.
func NewContextMap() *ContextMap {
	return &ContextMap{make(map[uuid.UUID]*Context)}
}

package api

import "github.com/pkg/errors"

var (
	errInvalidContainerHandle = errors.New("container handle was nil or invalid")
	errInvalidPoolHandle      = errors.New("pool handle was nil or invalid")
)

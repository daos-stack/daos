package client

import (
	"context"
	"fmt"
	"io"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
#include <daos.h>
#include <daos/debug.h>
*/
import "C"

const (
	apiClientKey ctxKey = "apiClientKey"
)

var (
	log logging.DebugLogger = logging.NewDebugLogger(io.Discard)
)

type (
	// connHandle is an opaque type used to represent a DAOS connection (pool or container).
	connHandle struct {
		UUID       uuid.UUID
		daosHandle C.daos_handle_t
	}

	// PoolHandle is an opaque type used to represent a DAOS Pool connection.
	PoolHandle struct {
		connHandle
	}

	// ContainerHandle is an opaque type used to represent a DAOS Container connection.
	// NB: A ContainerHandle contains the PoolHandle used to open the container.
	ContainerHandle struct {
		connHandle
		PoolHandle *PoolHandle
	}

	ctxKey string
)

func (ch *connHandle) String() string {
	return fmt.Sprintf("%s:%t", logging.ShortUUID(ch.UUID), ch.IsValid())
}

func (ch *connHandle) IsValid() bool {
	return bool(C.daos_handle_is_valid(ch.daosHandle))
}

func (ch *connHandle) invalidate() {
	ch.daosHandle.cookie = 0
}

func (ch *connHandle) Pointer() unsafe.Pointer {
	return unsafe.Pointer(&ch.daosHandle)
}

func (ch *ContainerHandle) String() string {
	return fmt.Sprintf("%s:%s:%t", logging.ShortUUID(ch.PoolHandle.UUID), logging.ShortUUID(ch.UUID), ch.IsValid())
}

var errNoApiClient = errors.Wrap(daos.NotInit, "no API client (call Init()?)")

func getApiClient(ctx context.Context) (apiClient, error) {
	if ctx == nil {
		return nil, errors.Wrap(daos.InvalidInput, "nil context")
	}
	if client, ok := ctx.Value(apiClientKey).(apiClient); ok {
		return client, nil
	}

	return nil, errNoApiClient
}

func setApiClient(parent context.Context, client apiClient) (context.Context, error) {
	if parent == nil {
		return nil, errors.Wrap(daos.InvalidInput, "nil context")
	}
	if parent.Err() != nil {
		return nil, parent.Err()
	}

	if client == nil {
		return nil, errors.Wrap(daos.InvalidInput, "nil API client")
	}

	if _, err := getApiClient(parent); err == nil {
		return nil, errors.Wrap(daos.Already, "API client already set")
	}

	return context.WithValue(parent, apiClientKey, client), nil
}

// Init initializes the DAOS Client API. The returned Context
// must be used for all API methods, and should be finalized
// using the Fini() method.
func Init(parent context.Context) (context.Context, error) {
	if parent.Err() != nil {
		return nil, parent.Err()
	}

	ctx := parent
	client, err := getApiClient(parent)
	if err == errNoApiClient {
		apiCtx, cancel := context.WithCancel(parent)
		client = &daosClientBinding{
			cancelCtx: cancel,
		}
		if ctx, err = setApiClient(apiCtx, client); err != nil {
			return nil, err
		}
	} else if err != nil {
		return nil, err
	} else if _, ok := client.(*mockApiClient); !ok {
		return nil, errors.Wrap(daos.Already, "DAOS API already initialized")
	}

	if err := daosError(client.daos_init()); err != nil {
		// May have already been initialized by DFS.
		if err != daos.Already {
			return nil, err
		}
	}

	return ctx, nil
}

// Fini deinitializes the DAOS Client API.
func Fini(ctx context.Context) error {
	client, err := getApiClient(ctx)
	if err != nil {
		return err
	}

	return daosError(client.daos_fini())
}

// SetDebugLog sets the package-level debug logger.
func SetDebugLog(dl logging.DebugLogger) {
	log = dl
}

// DebugInit initializes the DAOS Client debug logging system.
func DebugInit() error {
	return daosError(C.daos_debug_init(nil))
}

// DebugFini deinitializes the DAOS Client debug logging system.
func DebugFini() {
	C.daos_debug_fini()
}

package client

import (
	"context"

	"github.com/google/uuid"
)

import "C"

type (
	MockApiClientConfig struct {
		ReturnCodeMap map[string]int
		ConnectedPool uuid.UUID
		ConnectedCont uuid.UUID
	}

	mockApiClient struct {
		cfg *MockApiClientConfig
	}
)

func MockApiClientContext(parent context.Context, cfg *MockApiClientConfig) (context.Context, error) {
	if cfg == nil {
		cfg = &MockApiClientConfig{
			ReturnCodeMap: map[string]int{},
		}
	}

	client := &mockApiClient{
		cfg: cfg,
	}

	ctx, err := setApiClient(parent, client)
	if err != nil {
		return nil, err
	}

	return ctx, nil
}

func (c *mockApiClient) getRc(key string, def int) C.int {
	if rc, ok := c.cfg.ReturnCodeMap[key]; ok {
		return C.int(rc)
	}
	return C.int(def)
}

func (c *mockApiClient) daos_init() C.int {
	return c.getRc("daos_init", 0)
}

func (c *mockApiClient) daos_fini() C.int {
	return c.getRc("daos_fini", 0)
}

func (c *mockApiClient) daos_debug_init(_ *C.char) C.int {
	return c.getRc("daos_debug_init", 0)
}

func (c *mockApiClient) daos_debug_fini() {}

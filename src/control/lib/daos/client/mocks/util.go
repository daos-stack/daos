package mocks

import (
	"context"

	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
)

var (
	mockContConnKey connKey = "mockContConn"
	mockPoolConnKey connKey = "mockPoolConn"
)

func PoolConnCtx(parent context.Context, cfg *PoolConnCfg) context.Context {
	if daosAPICtx, err := daosAPI.MockApiClientContext(parent, nil); err == nil {
		parent = daosAPICtx
	}
	return context.WithValue(parent, mockPoolConnKey, NewPoolConn(cfg))
}

func GetPoolConn(ctx context.Context) *PoolConn {
	if conn, ok := ctx.Value(mockPoolConnKey).(*PoolConn); ok {
		return conn
	}
	return nil
}

func ContConnCtx(parent context.Context, cfg *ContConnCfg) context.Context {
	if daosAPICtx, err := daosAPI.MockApiClientContext(parent, nil); err == nil {
		parent = daosAPICtx
	}
	return context.WithValue(parent, mockContConnKey, newMockContConn(cfg))
}

func GetContConn(ctx context.Context) *ContConn {
	if conn, ok := ctx.Value(mockContConnKey).(*ContConn); ok && conn != nil {
		return conn
	}
	if poolConn := GetPoolConn(ctx); poolConn != nil {
		return newMockContConn(poolConn.cfg.ContConnCfg)
	}
	return nil
}

type (
	connKey string
)

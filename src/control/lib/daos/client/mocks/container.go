package mocks

import (
	"context"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/daos"
	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
)

type (
	GetPropertiesResp struct {
		Err
		Properties daosAPI.ContainerPropertySet
	}

	ContConnCfg struct {
		ConnectedPool      uuid.UUID
		ConnectedContainer uuid.UUID
		Close              Err
		ListAttributes     ListAttributesResp
		GetAttributes      GetAttributesResp
		SetAttributes      Err
		DeleteAttribute    Err
		GetProperties      GetPropertiesResp
		SetProperties      Err
		Query              ContainerInfoResp
	}

	ContConn struct {
		cfg *ContConnCfg
	}
)

func (mcc *ContConn) UUID() uuid.UUID {
	if mcc.cfg == nil {
		return uuid.Nil
	}
	return mcc.cfg.ConnectedContainer
}

func (mcc *ContConn) PoolUUID() uuid.UUID {
	if mcc.cfg == nil {
		return uuid.Nil
	}
	return mcc.cfg.ConnectedPool
}

func (mcc *ContConn) Close(context.Context) error {
	return mcc.cfg.Close.Error
}

func (mcc *ContConn) ListAttributes(context.Context) ([]string, error) {
	return mcc.cfg.ListAttributes.Attributes, mcc.cfg.ListAttributes.Error
}

func (mcc *ContConn) GetAttributes(context.Context, ...string) ([]*daos.Attribute, error) {
	return mcc.cfg.GetAttributes.Attributes, mcc.cfg.GetAttributes.Error
}

func (mcc *ContConn) SetAttributes(context.Context, []*daos.Attribute) error {
	return mcc.cfg.SetAttributes.Error
}

func (mcc *ContConn) DeleteAttribute(context.Context, string) error {
	return mcc.cfg.DeleteAttribute.Error
}

func (mcc *ContConn) GetProperties(context.Context, ...string) (daosAPI.ContainerPropertySet, error) {
	return mcc.cfg.GetProperties.Properties, mcc.cfg.GetProperties.Error
}

func (mcc *ContConn) SetProperties(context.Context, daosAPI.ContainerPropertySet) error {
	return mcc.cfg.SetProperties.Error
}

func (mcc *ContConn) Query(context.Context) (*daosAPI.ContainerInfo, error) {
	return mcc.cfg.Query.ContainerInfo, mcc.cfg.Query.Error
}

func newMockContConn(cfg *ContConnCfg) *ContConn {
	if cfg == nil {
		cfg = &ContConnCfg{}
	}
	return &ContConn{cfg: cfg}
}

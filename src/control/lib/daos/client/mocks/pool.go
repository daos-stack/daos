package mocks

import (
	"context"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/daos"
	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
)

type (
	Err struct {
		Error error
	}

	ContainerInfoResp struct {
		Err
		ContainerInfo *daosAPI.ContainerInfo
	}

	ListContainersResp struct {
		Err
		Containers []*daosAPI.ContainerInfo
	}

	OpenContainerResp struct {
		Err
		Conn *ContConn
	}

	GetAttributesResp struct {
		Err
		Attributes []*daos.Attribute
	}

	ListAttributesResp struct {
		Err
		Attributes []string
	}

	PoolInfoResp struct {
		Err
		PoolInfo *daos.PoolInfo
	}

	PoolConnCfg struct {
		ConnectedPool    uuid.UUID
		ContConnCfg      *ContConnCfg
		Connect          Err
		Disconnect       Err
		Query            PoolInfoResp
		CreateContainer  ContainerInfoResp
		OpenContainer    OpenContainerResp
		ListContainers   ListContainersResp
		DestroyContainer Err
		GetAttributes    GetAttributesResp
		SetAttributes    Err
		ListAttributes   ListAttributesResp
		DeleteAttribute  Err
	}

	PoolConn struct {
		cfg *PoolConnCfg
	}
)

func NewPoolConn(cfg *PoolConnCfg) *PoolConn {
	if cfg == nil {
		cfg = &PoolConnCfg{}
	}
	if cfg.ConnectedPool == uuid.Nil {
		cfg.ConnectedPool = uuid.New()
	}
	return &PoolConn{
		cfg: cfg,
	}
}

func (mpc *PoolConn) Connect(context.Context, string, string, daosAPI.PoolConnectFlag) error {
	return mpc.cfg.Connect.Error
}

func (mpc *PoolConn) Disconnect(context.Context) error {
	return mpc.cfg.Disconnect.Error
}

func (mpc *PoolConn) Query(context.Context, daosAPI.PoolQueryReq) (*daos.PoolInfo, error) {
	return mpc.cfg.Query.PoolInfo, mpc.cfg.Query.Error
}

func (mpc *PoolConn) CreateContainer(_ context.Context, req daosAPI.ContainerCreateReq) (*daosAPI.ContainerInfo, error) {
	if mpc.cfg.CreateContainer.ContainerInfo != nil || mpc.cfg.CreateContainer.Error != nil {
		return mpc.cfg.CreateContainer.ContainerInfo, mpc.cfg.CreateContainer.Error
	}

	if mpc.cfg.ContConnCfg == nil {
		mpc.cfg.ContConnCfg = &ContConnCfg{
			ConnectedPool:      mpc.cfg.ConnectedPool,
			ConnectedContainer: uuid.New(),
		}
	}
	newContInfo := &daosAPI.ContainerInfo{
		PoolUUID:        mpc.cfg.ConnectedPool,
		UUID:            mpc.cfg.ContConnCfg.ConnectedContainer,
		Label:           req.Label,
		Type:            req.Type,
		POSIXAttributes: req.POSIXAttributes,
	}
	mpc.cfg.CreateContainer.ContainerInfo = newContInfo
	if mpc.cfg.OpenContainer.Error == nil {
		mpc.cfg.ContConnCfg.Query.ContainerInfo = newContInfo
		mpc.cfg.ContConnCfg.GetProperties.Properties = req.Properties
		mpc.cfg.OpenContainer.Conn = newMockContConn(mpc.cfg.ContConnCfg)
	}

	return mpc.cfg.CreateContainer.ContainerInfo, mpc.cfg.CreateContainer.Error
}

func (mpc *PoolConn) OpenContainer(context.Context, string, daosAPI.ContainerOpenFlag) (*ContConn, error) {
	conn := mpc.cfg.OpenContainer.Conn
	if conn == nil && mpc.cfg.OpenContainer.Error == nil {
		conn = newMockContConn(&ContConnCfg{
			ConnectedPool:      mpc.cfg.ConnectedPool,
			ConnectedContainer: uuid.New(),
		})
	}

	return conn, mpc.cfg.OpenContainer.Error
}

func (mpc *PoolConn) UUID() uuid.UUID {
	if mpc.cfg == nil {
		return uuid.Nil
	}
	return mpc.cfg.ConnectedPool
}

func (mpc *PoolConn) DestroyContainer(context.Context, string, bool) error {
	return mpc.cfg.DestroyContainer.Error
}

func (mpc *PoolConn) ListContainers(context.Context, bool) ([]*daosAPI.ContainerInfo, error) {
	return mpc.cfg.ListContainers.Containers, mpc.cfg.ListContainers.Error
}

func (mpc *PoolConn) ListAttributes(context.Context) ([]string, error) {
	return mpc.cfg.ListAttributes.Attributes, mpc.cfg.ListAttributes.Error
}

func (mpc *PoolConn) GetAttributes(context.Context, ...string) ([]*daos.Attribute, error) {
	return mpc.cfg.GetAttributes.Attributes, mpc.cfg.GetAttributes.Error
}

func (mpc *PoolConn) SetAttributes(context.Context, []*daos.Attribute) error {
	return mpc.cfg.SetAttributes.Error
}

func (mpc *PoolConn) DeleteAttribute(context.Context, string) error {
	return mpc.cfg.DeleteAttribute.Error
}

#export CRT_PHY_ADDR_STR="ofi+sockets"
#export OFI_INTERFACE=eth0
#export OFI_DOMAIN=eth0

export CRT_PHY_ADDR_STR="ofi+verbs;ofi_rxm"
export OFI_INTERFACE=ib0
export OFI_DOMAIN=mlx5_0

#export CRT_PHY_ADDR_STR="ofi+tcp;ofi_rxm"
#export OFI_INTERFACE=eth0
#export OFI_DOMAIN=eth0

export OFI_PORT=31337
export CRT_ATTACH_INFO_PATH="."
export HG_LOG_SUBSYS=hg
export FI_UNIVERSE_SIZE=30

./install/lib/daos/TESTING/tests/cart_server

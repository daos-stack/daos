HOST="wolf-55"

# Shared parameters
export D_LOG_MASK=WARN
export CRT_TIMEOUT=10
export CRT_ATTACH_INFO_PATH="."

# sockets
 export CRT_PHY_ADDR_STR="ofi+sockets"
 export OFI_INTERFACE=eth0
 export OFI_DOMAIN=eth0

# tcp;ofi_rxm
# export CRT_PHY_ADDR_STR="ofi+tcp;ofi_rxm"
# export OFI_INTERFACE=eth0
# export OFI_DOMAIN=eth0

# verbs;ofi_rxm
# export CRT_PHY_ADDR_STR="ofi+verbs;ofi_rxm"
# export OFI_INTERFACE=ib0
# export OFI_DOMAIN=mlx4_0

killall -9 test_group_np_srv
killall -9 test_group_np_srv
sleep 2

SERVER_APP="./install/bin/crt_launch -e install/lib/daos/TESTING/tests/test_group_np_srv --name selftest_srv_grp --cfg_path=. -l"


set -x
export OTHER_ENVARS="-x FI_LOG_LEVEL=warn -x CRT_CTX_SHARE_ADDR=0 -x D_LOG_MASK -x CRT_PHY_ADDR_STR -x CRT_DISABLE_MEM_PIN=1 -x CRT_TIMEOUT"
orterun --continuous -H ${HOST}:5 --np 3 -x OFI_DOMAIN=${OFI_DOMAIN} -x OFI_INTERFACE=${OFI_INTERFACE} ${OTHER_ENVARS} ${SERVER_APP} 



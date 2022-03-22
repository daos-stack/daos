SERVER_APP="./install/bin/crt_launch -e ./install/lib/daos/TESTING/tests/dual_provider_server -i eth0,ib0 -p ofi+sockets,ofi+tcp;ofi_rxm -d eth0,mlx4_0 -c 8,3"

HOST="wolf-55"

# These envariables will be used only by crt_launch
export OFI_INTERFACE=eth0
export OFI_DOMAIN=eth0
export CRT_PHY_ADDR_STR="ofi+sockets"


ORTE_EXORTS="-x OFI_INTERFACE -x OFI_DOMAIN -x CRT_PHY_ADDR_STR"
set -x

killall -9 dual_provider_server
killall -9 dual_provider_server
killall -9 dual_provider_server

orterun -H ${HOST}:5 ${ORTE_EXORTS} --np 2 ${SERVER_APP}


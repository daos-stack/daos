export D_PROVIDER=ofi+verbs
export D_INTERFACE=ib0
export CRT_L_GRP_CFG=/tmp/crt_launch-info-Xmkc6sF

# comment this out once CRT_L_GRP_CFG is set 
echo "Make sure to est CRT_L_GRP_CFG properly"
exit 1

./install/bin/crt_launch -e ./install/lib/daos/TESTING/tests/no_pmix_launcher_server &

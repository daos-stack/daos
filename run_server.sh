export D_PROVIDER=ofi+verbs
export D_INTERFACE=ib0
./install/bin/crt_launch -e ./install/lib/daos/TESTING/tests/no_pmix_launcher_server

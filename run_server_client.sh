export D_PROVIDER=ofi+verbs
export D_INTERFACE=ib0
orterun -H wolf-21:3 -np 1 ./install/bin/crt_launch -e ./install/lib/daos/TESTING/tests/no_pmix_launcher_server : -np 1 ./install/bin/crt_launch -c -e ./install/lib/daos/TESTING/tests/no_pmix_launcher_client

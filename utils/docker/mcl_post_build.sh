
test_results="docker_build.log"

case ${distro} in
  sles12*)
    echo "No cmocka package for SLES, unit tests not run!"
    ;;

  *)
    ls ${test_results}
    set +e
    grep "This run had test failures" ${test_results}
    rc=$?
    set -e
    if [ ${rc} == 0 ]; then
      # Fail this build.
      exit 1
    fi
    ;;
esac


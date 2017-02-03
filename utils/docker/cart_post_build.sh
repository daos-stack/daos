
test_results=`find . -name test_output`

case ${distro} in
  sles12*)
    echo "No cmocka package for SLES, unit tests not run!"
    ;;
  ubuntu*)
    echo "No cmocka package for Ubuntu, unit tests not run!"
    ;;

  *)
    ls ${test_results}
    set +e
    grep FAILED ${test_results}
    rc=$?
    set -e
    if [ ${rc} == 0 ]; then
      # Fail this build.
      exit 1
    fi
    ;;
esac


pushd mercury
  mchecksum_commit1=`git submodule status src/mchecksum`
  mchecksum_commit=${mchecksum_commit1#?}

  kwsys_commit1=`git submodule status Testing/driver/kwsys`
  kwsys_commit=${kwsys_commit1#?}
popd

echo ${mchecksum_commit/%\ */} > artifacts/mercury_mchecksum_git_commit
echo ${kwsys_commit/%\ */} > artifacts/mercury_kwsys_git_commit


## This file should be placed in the root directory of your project.
## Then modify the CMakeLists.txt file in the root directory of your
## project to incorporate the testing dashboard.
##
## # The following are required to submit to the CDash dashboard:
##   ENABLE_TESTING()
##   INCLUDE(CTest)

set(CTEST_PROJECT_NAME Mercury)
set(CTEST_NIGHTLY_START_TIME 06:00:00 UTC)

set(CTEST_SUBMIT_URL https://cdash.mercury.daos.io/submit.php?project=Mercury)

set(CTEST_DROP_SITE_CDASH TRUE)


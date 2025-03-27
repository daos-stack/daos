import pytest

@pytest.mark.parametrize("ubertest_test_type",
                         [pytest.param("quick", marks=pytest.mark.ubertest_quick),
                          pytest.param("all", marks=pytest.mark.ubertest_all),
                          pytest.param("verify", marks=pytest.mark.ubertest_verify)])
def test_ubertest(cmdline_args, ubertest_test_type):
    from common import ClientServerTest

    if cmdline_args.ubertest_config_file is None:
        pytest.skip("no config file")
        return

    # uber test takes longer to finish, so set a larger timeout
    test = ClientServerTest(cmdline_args, "fi_ubertest", timeout=1000)

    test.run()
 

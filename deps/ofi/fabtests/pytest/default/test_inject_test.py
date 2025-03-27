import pytest

@pytest.mark.functional
@pytest.mark.parametrize("api_type", ["sendmsg", "post_tx"])
@pytest.mark.parametrize("flag", ["inject", "inj_complete"])
def test_inject_test(cmdline_args, api_type, flag):
    from common import ClientServerTest

    command = "fi_inject_test"
    if api_type == "sendmsg":
        command += " -N"
    command += " -A " + flag
    test = ClientServerTest(cmdline_args, command)
    test.run()

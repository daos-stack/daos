import pytest

@pytest.mark.functional
@pytest.mark.parametrize("endpoint_type", ["msg", "rdm"])
def test_unexpected_msg(cmdline_args, endpoint_type):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_unexpected_msg -e " + endpoint_type + " -I 10")
    test.run()

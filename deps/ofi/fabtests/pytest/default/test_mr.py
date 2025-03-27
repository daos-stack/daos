import pytest

@pytest.mark.unit
def test_mr(cmdline_args):
    from common import UnitTest
    test = UnitTest(cmdline_args, "fi_mr_test")
    test.run()

@pytest.mark.functional
@pytest.mark.parametrize("endpoint_type", ["msg", "rdm"])
def test_multi_mr(cmdline_args, endpoint_type):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_multi_mr -e " + endpoint_type)
    test.run()

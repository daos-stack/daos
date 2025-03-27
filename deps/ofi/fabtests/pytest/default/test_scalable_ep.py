import pytest

@pytest.mark.functional
def test_scalable_ep(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_scalable_ep")
    test.run()

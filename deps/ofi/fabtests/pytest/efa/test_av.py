import pytest

@pytest.mark.functional
def test_av_xfer(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_av_xfer -e rdm")
    test.run()

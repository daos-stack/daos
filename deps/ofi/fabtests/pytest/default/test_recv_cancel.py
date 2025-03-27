import pytest

@pytest.mark.functional
def test_recv_cancel(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_recv_cancel -e rdm -V")
    test.run()



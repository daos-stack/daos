import pytest

@pytest.mark.functional
@pytest.mark.parametrize("shared_cq", [True, False])
def test_multi_ep(cmdline_args, shared_cq):
    from common import ClientServerTest
    cmd = "fi_multi_ep -e rdm"
    if shared_cq:
        cmd += "  --shared-cq"
    test = ClientServerTest(cmdline_args, cmd)
    test.run()

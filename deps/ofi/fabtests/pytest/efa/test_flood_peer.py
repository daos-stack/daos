import pytest

@pytest.mark.functional
def test_flood_peer(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_bw -e rdm -W 6400 -S 512 -T 5",
                            timeout=300)
    test.run()

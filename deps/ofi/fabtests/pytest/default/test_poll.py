import pytest

@pytest.mark.functional
@pytest.mark.parametrize("poll_type", ["queue", "counter"])
def test_poll(cmdline_args, poll_type):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_poll -t " + poll_type)
    test.run()
 

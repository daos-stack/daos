import pytest

@pytest.mark.unit
def test_sighandler(cmdline_args):
    from common import UnitTest
    test = UnitTest(cmdline_args, "sighandler_test")
    test.run()

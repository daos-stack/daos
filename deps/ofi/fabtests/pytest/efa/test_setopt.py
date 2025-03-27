import pytest

@pytest.mark.unit
def test_setopt(cmdline_args):
    from common import UnitTest
    test = UnitTest(cmdline_args, "fi_setopt_test")
    test.run()


import pytest


@pytest.fixture(scope="module", params=["host_to_host",
                                        pytest.param("host_to_cuda", marks=pytest.mark.cuda_memory),
                                        pytest.param("cuda_to_host", marks=pytest.mark.cuda_memory),
                                        pytest.param("cuda_to_cuda", marks=pytest.mark.cuda_memory),
                                        pytest.param("neuron_to_neuron", marks=pytest.mark.neuron_memory),
                                        pytest.param("neuron_to_host", marks=pytest.mark.neuron_memory),
                                        pytest.param("host_to_neuron", marks=pytest.mark.neuron_memory)])
def memory_type(request):
    return request.param

@pytest.fixture(scope="module", params=["r:0,4,64",
                                        "r:4048,4,4148",
                                        "r:8000,4,9000",
                                        "r:17000,4,18000",
                                        "r:0,1024,1048576"])
def message_size(request):
    return request.param


@pytest.fixture(scope="module", params=["r:0,4,64",
                                        "r:4048,4,4148",
                                        "r:8000,4,9000",])
def inject_message_size(request):
    return request.param


@pytest.fixture(scope="module", params=["r:0,4,32",
                                        "r:0,1024,8192",])
def zcpy_recv_message_size(request):
    return request.param

@pytest.fixture(scope="module")
def zcpy_recv_max_msg_size(request):
    return 8192

@pytest.hookimpl(hookwrapper=True)
def pytest_collection_modifyitems(session, config, items):
    # Called after collection has been performed, may filter or re-order the items in-place
    # We use this hook to always run the MR exhaustion test at the end
    mr_exhaustion_tests, other_tests = [], []
    for item in items:
        if "mr_exhaustion" in item.name:
            mr_exhaustion_tests.append(item)
        else:
            other_tests.append(item)

    yield other_tests + mr_exhaustion_tests

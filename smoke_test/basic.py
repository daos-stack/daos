import unittest
import yaml
import os
import subprocess
import json
import re
from pathlib import Path
import argparse
import sys
import queue
from threading import Thread

RC_SUCCESS = 0
SCM_POOL_SIZE = "2GB"
# Use the fixed value because there's no way to obtain the available NVMe size
# for now.
NVME_POOL_SIZE = "10GB"
CONT_MOUNT_POINT = "/tmp/cont_mount"
# 100MB
SAMPLE_FILE_SIZE = 104000000


class TestBasic(unittest.TestCase):

    daos_server_yaml = None
    daos_agent_yaml = None
    start_daos_server = True
    start_daos_agent = True

    def setUp(self):
        """Start daos_server, format if necessary, and start daos_agent.
        """
        server_yaml_data = self.get_yaml_data(self.daos_server_yaml)
        self.check_yaml_entry(
            server_yaml_data, ["servers", 0, "scm_mount"], "daos_server")
        self.scm_mount = server_yaml_data["servers"][0]["scm_mount"]

        # If tmpfs is used, check that the size is larger than 2GB.
        self.check_yaml_entry(
            server_yaml_data, ["servers", 0, "scm_class"], "daos_server")
        use_dcpm = server_yaml_data["servers"][0]["scm_class"] == "dcpm"
        if not use_dcpm:
            self.check_yaml_entry(
                server_yaml_data, ["servers", 0, "scm_size"], "daos_server")
            if server_yaml_data["servers"][0]["scm_size"] < 2:
                self.fail("SCM size must be 2GB or larger!")
        self.pool_scm_size = SCM_POOL_SIZE

        self.use_nvme = False
        if "bdev_class" in server_yaml_data["servers"][0] and \
            server_yaml_data["servers"][0]["bdev_class"] == "nvme":
            self.use_nvme = True

        self.pool_uuid = None

        # Start daos_server and format if needed.
        if self.start_daos_server:
            print("[INFO] Starting daos_server...")
            daos_server_cmd = "daos_server -b start -o {} -i".format(
                self.daos_server_yaml)
            cmd = daos_server_cmd.split()
            formatted = False
            io_server_started = False
            started_pattern = re.compile(
                r"DAOS I/O server (.+) process [\d]+ started")
            error_pattern = re.compile(r"ERROR:\s+(.+)")
            self.server_proc = subprocess.Popen(
                args=cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            for line in iter(self.server_proc.stdout.readline, b''):

                print(line.decode("utf-8"), end='')

                if started_pattern.search(line.decode("utf-8")):
                    print("[INFO] SCM format complete")
                    print("--- PASS: daos_server started successfully ---")
                    io_server_started = True
                    break

                if not formatted and "SCM format required on instance 0" in str(
                    line):
                    print("[INFO] SCM format required")
                    self.run_process_verify(
                        "dmg -i storage format", "dmg storage format failed!")
                    formatted = True

            if not io_server_started:
                error_msg = ""
                for line in iter(self.server_proc.stderr.readline, b''):
                    line_utf8 = line.decode("utf-8")
                    print("[ERROR] {}".format(line_utf8), end='')
                    res = error_pattern.search(line_utf8)
                    if res:
                        error_msg = res.group(1)
                        break

                self.fail("Error starting daos_server!\n{}".format(error_msg))

        if self.start_daos_agent:
            # Start daos_agent.
            agent_yaml_data = self.get_yaml_data(self.daos_agent_yaml)
            self.check_yaml_entry(
                agent_yaml_data, ["runtime_dir"], "daos_agent")
            runtime_dir = agent_yaml_data["runtime_dir"]

            print("[INFO] Starting daos_agent...")
            daos_agent_cmd = "daos_agent -i --config-path={}".format(
                self.daos_agent_yaml)
            cmd = daos_agent_cmd.split()
            started_pattern = re.compile(r"Listening on {}".format(runtime_dir))
            error_pattern = re.compile(r"ERROR: daos_agent:\s+(.+)")

            self.agent_proc = subprocess.Popen(
                args=cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            daos_agent_started = False

            # Check if daos_agent starts successfully.
            for line in iter(self.agent_proc.stdout.readline, b''):
                print(line.decode("utf-8"), end='')

                if started_pattern.search(line.decode("utf-8")):
                    print("--- PASS: daos_agent started successfully ---")
                    daos_agent_started = True
                    break

            if not daos_agent_started:
                # daos_agent didn't start successfully. Get the error message
                # from stderr.
                error_msg = ""
                for line in iter(self.agent_proc.stderr.readline, b''):
                    line_utf8 = line.decode("utf-8")
                    print("[ERROR] {}".format(line_utf8), end='')
                    res = error_pattern.search(line_utf8)
                    if res:
                        error_msg = res.group(1)
                        break

                self.fail("Error starting daos_agent!\n{}".format(error_msg))

    def check_yaml_entry(self, yaml_data, keys, yaml_type):
        """Check keys exist in yaml_data.

        Check expected yaml entries exist by drilling down yaml_data based on
            the given keys.

        Args:
            yaml_data (dict): the contents of the yaml file.
            keys (list): list of keys and indcies to index the yaml.
            yaml_type (str): either daos_server or daos_agent. Used for message.
        """
        for key in keys:

            if isinstance(key, int):
                # The value is a list.
                if not isinstance(yaml_data, list):
                    self.fail(
                        "Error in {} yaml file!\n{} is not list".format(
                            yaml_type, yaml_data))
                if key >= len(yaml_data):
                    self.fail(
                        "Error in {} yaml file! List index out of range.\n"\
                        "List = {}\nIndex = {}".format(
                            yaml_type, yaml_data, key))

            else:
                # The value is a dictionary.
                if key not in yaml_data:
                    self.fail(
                        "Error in {} yaml file!\n{} not in {}".format(
                            yaml_type, key, yaml_data))

            yaml_data = yaml_data[key]

    def run_process_verify(self, command, error_msg):
        """Start process from given command and verify its return code.

        Args:
            command (str): Command to run; shouldn't include pipe.
            error_msg (str): Error message to show in case of error.

        Returns:
            subprocess.CompletedProcess: Contains process info such as RC and
                stdout.
        """
        cp = subprocess.run(args=command.split(), stdout=subprocess.PIPE)
        self.assertEqual(
            cp.returncode, RC_SUCCESS,
            "{} {}".format(error_msg, cp.stdout.decode("utf-8")))
        return cp

    def get_yaml_data(self, yaml_file):
        """Get the contents of a yaml file as a dictionary.

        Args:
            yaml_file (str): yaml file to read

        Raises:
            Exception: if an error is encountered reading the yaml file

        Returns:
            dict: the contents of the yaml file
        """
        yaml_data = {}
        if os.path.isfile(yaml_file):
            with open(yaml_file, "r") as open_file:
                try:
                    file_data = open_file.read()
                    yaml_data = yaml.safe_load(file_data.replace("!mux", ""))
                except yaml.YAMLError as error:
                    print("Error reading {}: {}".format(yaml_file, error))
                    exit(1)
        return yaml_data

    def get_pool_sizes(self, pool_uuid):
        """Get SCM and NVMe sizes in the given pool.

        Args:
            pool_uuid (str): Pool UUID.

        Returns:
            (str, str): Available SCM and NVMe size.
        """
        dmg_pool_query_cmd = "dmg -i pool query --pool={} --json".format(
            pool_uuid)
        cp = self.run_process_verify(
            dmg_pool_query_cmd, "dmg pool query failed!")

        # Parse the output to get the available pool size.
        data = json.loads(cp.stdout)
        pool_sizes = {}
        pool_sizes["Scm"] = {}
        pool_sizes["Scm"]["Total"] = data["response"]["Scm"]["Total"]
        pool_sizes["Scm"]["Free"] = data["response"]["Scm"]["Free"]
        pool_sizes["Scm"]["Min"] = data["response"]["Scm"]["Min"]
        pool_sizes["Scm"]["Max"] = data["response"]["Scm"]["Max"]
        pool_sizes["Scm"]["Mean"] = data["response"]["Scm"]["Mean"]
        pool_sizes["Nvme"] = {}
        pool_sizes["Nvme"]["Total"] = data["response"]["Nvme"]["Total"]
        pool_sizes["Nvme"]["Free"] = data["response"]["Nvme"]["Free"]
        pool_sizes["Nvme"]["Min"] = data["response"]["Nvme"]["Min"]
        pool_sizes["Nvme"]["Max"] = data["response"]["Nvme"]["Max"]
        pool_sizes["Nvme"]["Mean"] = data["response"]["Nvme"]["Mean"]
        return pool_sizes

    def test_basic(self):
        """Basic test.

        1. Create a pool over both SCM and NVMe, if exists.
        2. Create a POSIX container.
        3. Mount a temp directory to the container.
        4. Create a file in the directory.
        5. Verify that pool size has decreased with the size of the file
            created.
        """
        # Create a pool.
        if self.use_nvme:
            dmg_pool_create_cmd = "dmg -i pool create --scm-size={} "\
                "--nvme-size={} --json".format(
                    self.pool_scm_size, NVME_POOL_SIZE)
        else:
            dmg_pool_create_cmd = "dmg -i pool create --scm-size={} "\
                "--json".format(self.pool_scm_size)
        cmd = dmg_pool_create_cmd.split()
        cp = self.run_process_verify(
            dmg_pool_create_cmd, "dmg pool create failed!")
        print("--- PASS: Pool created successfully ---")

        # Get the pool UUID from the output.
        data = json.loads(cp.stdout)
        self.pool_uuid = data["response"]["UUID"]
        print("[INFO] Pool UUID = {}".format(self.pool_uuid))

        # Create a container.
        daos_container_create_cmd = "daos container create --pool={} "\
            "--svc=0 --type=POSIX".format(self.pool_uuid)
        cp = self.run_process_verify(
            daos_container_create_cmd, "daos container create failed!")

        # Get the container UUID from the output.
        actual_cont_uuid = cp.stdout.decode("utf-8").split()[-1]
        print("[INFO] Actual container UUID = {}".format(actual_cont_uuid))

        # List container and verify.
        daos_pool_list_cont_cmd = "daos pool list-cont --pool={} "\
            "--svc=0".format(self.pool_uuid)
        cmd = daos_pool_list_cont_cmd.split()
        cp = self.run_process_verify(
            daos_pool_list_cont_cmd, "daos pool list-cont failed!")
        expected_cont_uuid = cp.stdout.decode("utf-8").split()[0]
        print("[INFO] Expected container UUID = {}".format(expected_cont_uuid))
        self.assertEqual(
            actual_cont_uuid, expected_cont_uuid,
            "Container wasn't created properly!")
        print("--- PASS: Container created successfully ---")

        # Mount the container with dfuse.
        print("[INFO] Mounting the container...")
        Path(CONT_MOUNT_POINT).mkdir(parents=True, exist_ok=True)
        dfuse_cmd = "dfuse --pool {} -s 0 --container {} -m {}".format(
            self.pool_uuid, actual_cont_uuid, CONT_MOUNT_POINT)
        self.run_process_verify(dfuse_cmd, "dfuse mount failed!")
        print("--- PASS: Container mounted successfully ---")

        # Create a file in the mount point.
        head_cmd = "head -c {} /dev/urandom".format(SAMPLE_FILE_SIZE)
        cmd = head_cmd.split()
        output_file = "{}/sample".format(CONT_MOUNT_POINT)
        print("[INFO] Writing a file into the container at {}".format(
            CONT_MOUNT_POINT))
        with open(output_file, "wb") as out_file:
            cp = subprocess.run(args=cmd, stdout=out_file)
        self.assertEqual(
            cp.returncode, RC_SUCCESS,
            "Failed to write a file in {}!".format(CONT_MOUNT_POINT))

        # Verify that the file was written into the DAOS container.
        print("[INFO] Verifying the file was written to DAOS container...")
        pool_sizes = self.get_pool_sizes(pool_uuid=self.pool_uuid)
        # If NVMe is used, data are written there, not into SCM.
        if self.use_nvme:
            upper_bound = int(pool_sizes["Nvme"]["Total"]) - SAMPLE_FILE_SIZE
            lower_bound = upper_bound - 20000000
            print("[INFO] Upper bound = {}".format(upper_bound))
            print("[INFO] Lower bound = {}".format(lower_bound))
            print("[INFO] NVMe Free = {}".format(pool_sizes["Nvme"]["Free"]))
            self.assertTrue(
                lower_bound < pool_sizes["Nvme"]["Free"],
                "Unexpected NVMe free space after file write! NVMe free space "\
                "is too small. Lower bound = {}; NVMe free space = "\
                "{}".format(lower_bound, pool_sizes["Nvme"]["Free"]))
            self.assertTrue(
                pool_sizes["Nvme"]["Free"] < upper_bound,
                "Unexpected Nvme free space after file write! NVMe free space "\
                "is too big. Upper bound = {}; NVMe free space = "\
                "{}".format(upper_bound, pool_sizes["Nvme"]["Free"]))
            print("--- PASS: Data written to NVMe successfully ---")
        else:
            upper_bound = int(pool_sizes["Scm"]["Total"]) - SAMPLE_FILE_SIZE
            lower_bound = upper_bound - 20000000
            print("[INFO] Upper bound = {}".format(upper_bound))
            print("[INFO] Lower bound = {}".format(lower_bound))
            print("[INFO] SCM Free = {}".format(pool_sizes["Scm"]["Free"]))
            self.assertTrue(
                lower_bound < pool_sizes["Scm"]["Free"],
                "Unexpected SCM free space after file write! SCM free space "\
                "is too small. Lower bound = {}; SCM free space = "\
                "{}".format(lower_bound, pool_sizes["Scm"]["Free"]))
            self.assertTrue(
                pool_sizes["Scm"]["Free"] < upper_bound,
                "Unexpected SCM free space after file write! SCM free space "\
                "is too big. Upper bound = {}; SCM free space = "\
                "{}".format(upper_bound, pool_sizes["Scm"]["Free"]))
            print("--- PASS: Data written to SCM successfully ---")

    def tearDown(self):
        """Clean up the resources.
        """
        # Unmount dfuse.
        print("[INFO] Unmounting...")
        unmount_cmd = "fusermount3 -u {}".format(CONT_MOUNT_POINT)
        self.run_process_verify(unmount_cmd, "Dfuse unmount failed!")

        # Destroy pool.
        print("[INFO] Destroying pool...")
        dmg_pool_destroy_cmd = "dmg -i pool destroy --pool={}".format(
            self.pool_uuid)
        self.run_process_verify(
            dmg_pool_destroy_cmd, "dmg pool destroy failed!")
        print("--- PASS: Pool destroyed successfully ---")

        if self.start_daos_server:
            # Stop daos_server.
            dmg_system_stop_cmd = "dmg -i system stop --force"
            self.run_process_verify(
                dmg_system_stop_cmd, "dmg system stop failed!")
            self.server_proc.terminate()
            self.server_proc.communicate()
            print("[INFO] daos_server terminated")

        if self.start_daos_agent:
            # Stop daos_agent.
            self.agent_proc.terminate()
            self.agent_proc.communicate()
            print("[INFO] daos_agent terminated")

        # Delete the container mount point.
        print("[INFO] Deleting mount point...")
        os.rmdir(path=CONT_MOUNT_POINT)

        if self.start_daos_server:
            # Unmount SCM.
            print("[INFO] Unmounting SCM...")
            umount_cmd = "sudo umount {}".format(self.scm_mount)
            self.run_process_verify(umount_cmd, "Unmount SCM failed!")


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    # daos_server config file is required because we use it to find out whether
    # we'll use NVMe.
    ap.add_argument(
        "-s", "--server", required=True, help="path to daos_server config file")
    ap.add_argument(
        "-a", "--agent", required=False, default=None,
        help="path to daos_agent config file")
    ap.add_argument(
        "-d", "--without_daos_server", required=False, action="store_false",
        help="do not start daos_server")
    ap.add_argument(
        "-g", "--without_daos_agent", required=False, action="store_false",
        help="do not start daos_agent")

    args = vars(ap.parse_args())
    TestBasic.daos_server_yaml = args["server"]
    TestBasic.daos_agent_yaml = args["agent"]
    TestBasic.start_daos_server = args["without_daos_server"]
    TestBasic.start_daos_agent = args["without_daos_agent"]

    # Remove the path parameters so that they don't interfere with the unittest
    # argument.
    sys.argv = [sys.argv[0]]
    unittest.main()

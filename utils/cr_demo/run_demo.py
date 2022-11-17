import argparse
import subprocess


POOL_SIZE = "4GB"
POOL_LABEL = "tank"

def format_storage(host_list):
    format_cmd = ["dmg", "storage", "format", "--host-list=" + host_list]
    subprocess.run(format_cmd)

def create_pool(pool_size, pool_label):
    create_pool_cmd = ["dmg", "pool", "create", "--size=" + pool_size,
                       "--label=" + pool_label]
    subprocess.run(create_pool_cmd)

def inject_fault_mgmt(pool_label, fault_type):
    inject_fault_cmd = ["dmg", "faults", "mgmt-svc", "pool", pool_label, fault_type]
    subprocess.run(inject_fault_cmd)

def list_pool():
    list_pool_cmd = ["dmg", "pool", "list"]
    subprocess.run(list_pool_cmd)

def enable_checker():
    check_enable_cmd = ["dmg", "check", "enable"]
    subprocess.run(check_enable_cmd)

def start_checker():
    check_start_cmd = ["dmg", "check", "start"]
    subprocess.run(check_start_cmd)

def query_checker():
    check_query_cmd = ["dmg", "check", "query"]
    subprocess.run(check_query_cmd)

def disable_checker():
    check_disable_cmd = ["dmg", "check", "disable"]
    subprocess.run(check_disable_cmd)

ap = argparse.ArgumentParser()
ap.add_argument("-l", "--hostlist", required=True, help="List of hosts to format")
args = vars(ap.parse_args())

hostlist = args["hostlist"]
input("1. Format storage on " + hostlist + ". Hit enter...")
format_storage(host_list=hostlist)

input("2. Create a 4GB pool. Hit enter...")
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL)

input("3. Remove PS entry on MS. Hit enter...")
inject_fault_mgmt(pool_label=POOL_LABEL, fault_type="CIC_POOL_NONEXIST_ON_MS")

input("4. MS doesn\'t recognize any pool (it exists on engine). Hit enter...")
list_pool()

input("5. Enable and start checker. Hit enter...")
enable_checker()
start_checker()

user_input = input("6-1. Query the checker. Hit y to query, n to proceed to next step.")
while():
    if user_input == "y":
        query_checker()
    elif user_input == "n":
        break
    else:
        print("Please enter y or n.")

print("6-2. Checker shows the inconsistency that was repaired.")

input("7. Disable the checker. Hit enter...")
disable_checker()

input("8. Verify that the missing pool was reconstructed. Hit enter...")
list_pool()

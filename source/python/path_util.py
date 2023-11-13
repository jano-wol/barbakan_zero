import os
import sys


def get_test_data_folder():
    # If possible, find the debug test data folder. Fallback is the release test data folder.
    for build_str in ["debug", "release"]:
        dir_name = os.path.dirname(os.path.abspath(__file__)) + '/../../build/' + build_str + '/test/data/'
        if os.path.isdir(dir_name):
            return dir_name
    print(f"Cannot find test_data_folder.")
    sys.exit(1)

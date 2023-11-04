#!/usr/bin/python3
import sys
import os
import argparse
import traceback
import random
import math
import time
import logging
import json
import numpy as np
from collections import defaultdict
from typing import Dict, List

import torch
import torch.nn
from torch.optim.swa_utils import AveragedModel

import modelconfigs
from model_pytorch import Model
from metrics_pytorch import Metrics
import data_processing_pytorch
from load_model import load_model


# HANDLE COMMAND AND ARGS -------------------------------------------------------------------

def get_input_tensors(moves, pos_len):
    binary_input_nchw = torch.zeros(1, 22, 20, 20)
    global_input_nc = torch.zeros(1, 19)
    c1 = binary_input_nchw[0][0]
    c1[True] = 1
    player = 'O' if len(moves) % 2 == 0 else 'X'
    idx = 0
    for move in moves:
        curr_move = 'O' if (idx % 2 == 0) else 'X'
        if player == curr_move:
            t_to_extend = binary_input_nchw[0][1]
        else:
            t_to_extend = binary_input_nchw[0][2]
        x = move // pos_len
        y = move % pos_len
        t_to_extend[x][y] = 1.0
        idx += 1
    return binary_input_nchw, global_input_nc


if __name__ == "__main__":
    description = """
    Test neural net on Go positions from npz files of batches from selfplay.
    """

    parser = argparse.ArgumentParser(description=description)
    parser.add_argument('-model-kind', help='If specified, use this model kind instead of config', required=False)
    parser.add_argument('-config', help='Path to model.config.json', required=False)
    parser.add_argument('-checkpoint', help='Checkpoint to test', required=False)
    parser.add_argument('-pos-len', help='Spatial length of expected training data', type=int, required=True)
    parser.add_argument('-batch-size', help='Batch size to use for testing', type=int, required=True)
    parser.add_argument('-use-swa', help='Use SWA model', action="store_true", required=False)
    parser.add_argument('-gpu-idx', help='GPU idx', type=int, required=False)
    parser.add_argument('-out-file-value', help='output', required=True)
    parser.add_argument('-out-file-policy', help='output', required=True)

    args = vars(parser.parse_args())


def main(args):
    model_kind = args["model_kind"]
    config_file = args["config"]
    checkpoint_file = args["checkpoint"]
    pos_len = args["pos_len"]
    batch_size = args["batch_size"]
    use_swa = True
    gpu_idx = args["gpu_idx"]
    out_file_value_str = args["out_file_value"]
    out_file_policy_str = args["out_file_policy"]
    out_file_value_path = os.path.realpath(out_file_value_str)
    out_file_value = open(out_file_value_path, "w")
    out_file_policy_path = os.path.realpath(out_file_policy_str)
    out_file_policy = open(out_file_policy_path, "w")

    soft_policy_weight_scale = 1.0
    value_loss_scale = 1.0
    td_value_loss_scales = [0.4, 0.4, 0.4]

    world_size = 1
    rank = 0

    # SET UP LOGGING -------------------------------------------------------------

    logging.root.handlers = []
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
        handlers=[
            logging.StreamHandler(stream=sys.stdout)
        ],
    )
    np.set_printoptions(linewidth=150)

    logging.info(str(sys.argv))

    # FIGURE OUT GPU ------------------------------------------------------------
    if gpu_idx is not None:
        torch.cuda.set_device(gpu_idx)
        logging.info("Using GPU device: " + torch.cuda.get_device_name())
        device = torch.device("cuda", gpu_idx)
    elif torch.cuda.is_available():
        logging.info("Using GPU device: " + torch.cuda.get_device_name())
        device = torch.device("cuda")
    else:
        logging.warning("WARNING: No GPU, using CPU")
        device = torch.device("cpu")

    # LOAD MODEL ---------------------------------------------------------------------

    model, swa_model, _ = load_model(checkpoint_file, use_swa, device=device, pos_len=pos_len, verbose=True)
    logging.info("Beginning test!")
    with torch.no_grad():
        moves = [210, 191, 231, 189, 252]
        [binary_input_nchw, global_input_nc] = get_input_tensors(moves, pos_len)
        model_outputs = swa_model(binary_input_nchw.cuda(), global_input_nc.cuda())
        value_tensor = model_outputs[0][1][0]
        policy_tensor = model_outputs[0][0][0][0]
        for i in range(3):
            out_file_value.write(str(value_tensor[i].item()) + "\n")
        for i in range(pos_len * pos_len):
            out_file_policy.write(str(policy_tensor[i].item()) + "\n")


if __name__ == "__main__":
    main(args)

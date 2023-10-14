#!/usr/bin/python3
import sys
import os
import argparse
import logging
import torch.multiprocessing
import read_batches

logging.basicConfig(
    format='%(asctime)s %(levelname)-8s %(message)s',
    level=logging.INFO,
    datefmt='%Y-%m-%d %H:%M:%S')


def print_position(player_stones, waiter_stones):
    board_size = player_stones.shape[0]
    p_s = (player_stones == 1.0).nonzero(as_tuple=True)
    w_s = (waiter_stones == 1.0).nonzero(as_tuple=True)
    num_player_stones = p_s[0].shape[0]
    num_waiter_stones = w_s[0].shape[0]
    if (num_player_stones + num_waiter_stones) % 2 == 0:
        o_s = p_s
        x_s = w_s
    else:
        o_s = w_s
        x_s = p_s
    o_num_stones = o_s[0].shape[0]
    x_num_stones = x_s[0].shape[0]
    board_dict = {}
    for idx1 in range(board_size):
        for idx2 in range(board_size):
            board_dict[(idx1, idx2)] = '.'
    for idx1 in range(o_num_stones):
        t = (o_s[0][idx1].item(), o_s[1][idx1].item())
        board_dict[t] = 'O'
    for idx1 in range(x_num_stones):
        t = (x_s[0][idx1].item(), x_s[1][idx1].item())
        board_dict[t] = 'X'
    print_str = ""
    for idx1 in range(board_size):
        for idx2 in range(board_size):
            print_str += board_dict[(idx1, idx2)]
            print_str += " "
        print_str += "\n"
    print_str += "---------------------------------------\n"
    print(print_str, flush=True)


def dump_position(player_stones, waiter_stones):
    board_size = player_stones.shape[0]
    to_dump = []
    for idx1 in range(board_size):
        for idx2 in range(board_size):
            to_dump.append(int(player_stones[idx1][idx2].item()))
    for idx1 in range(board_size):
        for idx2 in range(board_size):
            to_dump.append(int(waiter_stones[idx1][idx2].item()))
    byte_array = bytearray(to_dump)
    dump_file.write(byte_array)


if __name__ == "__main__":
    description = """
    Dump gomoku positions with random D4 symmetry from npz files to generate data for nnue.
    """

    parser = argparse.ArgumentParser(description=description, add_help=False)
    required_args = parser.add_argument_group('required arguments')
    required_args.add_argument('-shuffle-dir', help='output by shuffle.py', required=True)
    required_args.add_argument('-dump-file', help='output by shuffle.py', required=True)
    required_args.add_argument('-pos-len', help='Spatial edge length of expected training data, e.g. 19 for 19x19 Go',
                               type=int, required=True)
    required_args.add_argument('-batch-size', help='Per-GPU batch size to use for training', type=int, required=True)
    required_args.add_argument('-target', help='Number of dumped positions', type=int, required=True)
    args = vars(parser.parse_args())
    rank = 0
    barrier = None
    shuffle_dir = args["shuffle_dir"]
    dump_file_str = args["dump_file"]
    pos_len = args["pos_len"]
    batch_size = args["batch_size"]
    target = args["target"]
    logging.info(str(sys.argv))
    device = torch.device("cpu")
    shuffle_dir_path = os.path.realpath(shuffle_dir)
    dump_file_path = os.path.realpath(dump_file_str)
    train_files = [os.path.join(shuffle_dir_path, fname) for fname in os.listdir(shuffle_dir_path) if
                   fname.endswith(".npz")]
    positions_dumped = 0
    dump_file = open(dump_file_path, "wb")
    for [batch, n, num_whole_steps] in read_batches.read_npz_training_data(
            train_files,
            batch_size,
            1,
            0,
            pos_len=pos_len,
            device=device,
            randomize_symmetries=True
    ):
        curr_batch_size = batch["binaryInputNCHW"].shape[0]
        for idx in range(curr_batch_size):
            if positions_dumped % 10000 == 0:
                logging.info(f"positions_dumped/target={positions_dumped}/{target}")
            p = batch["binaryInputNCHW"][idx][1]
            w = batch["binaryInputNCHW"][idx][2]
            dump_position(p, w)
            positions_dumped += 1
            if positions_dumped >= target:
                logging.info(f"target reached. positions_dumped={positions_dumped}")
                exit(0)
    logging.info(f"all lines were dumped. Target was not reached. positions_dumped/target={positions_dumped}/{target}")

# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import ttnn
import torch
import torch.nn as nn
from models.demos.wormhole.llama31_8b.tt.llama_decoder import TtTransformerBlock
from models.common.rmsnorm import RMSNorm
import ttnn
from typing import Optional


class TtTransformer(nn.Module):
    def __init__(
        self,
        args,
        dtype,
        device,
        state_dict,
        weight_cache_path,
        layers,
        rot_mat,
        start_pos,
    ):
        super().__init__()
        self.args = args
        self.vocab_size = args.vocab_size
        self.n_layers = args.n_layers
        self.start_pos = start_pos
        self.device = device
        self.dtype = dtype
        self.model_config = args.get_model_config()
        assert self.vocab_size > 0

        print("Doing self.layers")
        self.layers = torch.nn.ModuleList(
            [
                TtTransformerBlock(
                    args=args,
                    device=device,
                    dtype=dtype,
                    state_dict=state_dict,
                    weight_cache_path=weight_cache_path,
                    layer_num=i,
                    rot_mat=rot_mat,
                    start_pos=start_pos,
                )
                for i in layers
            ]
        )
        print("doing norm")
        self.norm = RMSNorm(
            device=device,
            dim=args.dim,
            state_dict=state_dict,
            layer_num=None,
            weight_cache_path=weight_cache_path,
            weight_dtype=dtype,
            weight_key="norm",
        )

        print("doing output weight")
        print(type(weight_cache_path))
        x = weight_cache_path / "output.weight"
        print("printing x")
        print(type(x))
        print(x)
        print("printing stuff about the output_weight in state_dict")
        print("output.weight" in state_dict)
        print(type(state_dict))
        print(type(state_dict["output.weight"]))
        print("trying to assign and print sizes")
        output_layer = state_dict["output.weight"]
        print(output_layer.size())  # Outputs the dimensions of the tensor
        print(output_layer.numel())  # Outputs the total number of elements in the tensor
        print("made it passed the printing")
        self.output_weight = ttnn.as_tensor(
            output_layer.permute(1, 0),
            device=device,
            layout=ttnn.TILE_LAYOUT,
            dtype=dtype,
            memory_config=ttnn.DRAM_MEMORY_CONFIG,
            cache_file_name=weight_cache_path / "output.weight",
        )
        print("We made it!!")

    def forward(
        self,
        x: ttnn.Tensor,
        current_pos: int,
        attn_masks: Optional[ttnn.Tensor] = None,
        rot_mat=None,
        transformation_mats=None,
        user_id=0,
        mode="decode",
    ):
        for layer in self.layers:
            x = layer(x, current_pos, attn_masks, rot_mat, transformation_mats, user_id, mode)
        if mode == "prefill":
            return x
        x = self.norm(x)
        output = ttnn.linear(
            x,
            self.output_weight,
            compute_kernel_config=self.args.get_compute_kernel_config(),
            program_config=self.model_config["OUTPUT_MM_PROGCFG"],
            memory_config=self.model_config["OUTPUT_MM_MEMCFG"],
            dtype=self.dtype,
        )

        return output

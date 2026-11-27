"""
Export Nexto model weights to ONNX for use with rlbot_win.

Requirements:
    pip install torch onnx rlgym-tools
    # Clone Nexto and download their weights:
    git clone https://github.com/rolv-arild/necto
    # Download nexto.pt from their releases

Usage:
    python scripts/export_nexto_onnx.py --input nexto.pt --output models/nexto.onnx

The exported model has:
    Input:  "obs"    float32[1, 107]
    Output: "output" float32[1, 8]
        [throttle, steer, pitch, yaw, roll, jump, boost, handbrake]
"""

import argparse
import torch
import torch.nn as nn
import onnx
import os

OBS_SIZE = 107
ACT_SIZE = 8


def load_nexto_model(weights_path: str) -> nn.Module:
    """
    Load the Nexto model. The exact architecture depends on the checkpoint.
    Adjust if the actual Nexto architecture differs from this stub.

    IMPORTANT: This is a placeholder architecture.
    You must import the real model class from the necto repo:
        from necto.training.agent import Nexto
        model = Nexto()
        model.load_state_dict(torch.load(weights_path))
    """
    raise NotImplementedError(
        "Replace this function body with the real Nexto model loading code.\n"
        "Clone https://github.com/rolv-arild/necto and import the model class."
    )


def export_to_onnx(model: nn.Module, output_path: str):
    model.eval()
    dummy_input = torch.zeros(1, OBS_SIZE)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    torch.onnx.export(
        model,
        dummy_input,
        output_path,
        input_names=["obs"],
        output_names=["output"],
        dynamic_axes={
            "obs":    {0: "batch_size"},
            "output": {0: "batch_size"},
        },
        opset_version=17,
        do_constant_folding=True,
    )

    # Verify
    onnx_model = onnx.load(output_path)
    onnx.checker.check_model(onnx_model)
    print(f"Model exported and verified: {output_path}")
    print(f"  Input:  {[i.name for i in onnx_model.graph.input]}")
    print(f"  Output: {[o.name for o in onnx_model.graph.output]}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--input",  required=True, help="Path to nexto.pt weights")
    parser.add_argument("--output", default="models/nexto.onnx")
    args = parser.parse_args()

    model = load_nexto_model(args.input)
    export_to_onnx(model, args.output)

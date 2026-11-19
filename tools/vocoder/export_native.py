# tools/vocoder/export_native.py
# Exports trained PyTorch Neural Vocoder Generator weights into binary format for TezzNative C runtime.

import os
import struct
import argparse
import torch
from models import Generator


def export_binary(checkpoint_path, output_bin_path):
    print(f">> Loading PyTorch Generator Checkpoint: {checkpoint_path}")
    generator = Generator()
    state_dict = torch.load(checkpoint_path, map_location="cpu")
    generator.load_state_dict(state_dict)
    generator.remove_weight_norm()
    generator.eval()

    print(f">> Exporting clean weights to binary: {output_bin_path}")
    os.makedirs(os.path.dirname(output_bin_path), exist_ok=True)

    with open(output_bin_path, "wb") as f:
        # Magic: TNVOC01 (8 bytes)
        f.write(b"TNVOC01\0")

        # Config: [sample_rate=16000, in_channels=80, initial_channels=128, num_ups=4, num_res=8]
        config = [16000, 80, 128, 4, 8, 0, 0, 0]
        f.write(struct.pack("<8i", *config))

        total_weights = 0
        for name, param in generator.named_parameters():
            data = param.detach().cpu().numpy().astype("float32")
            name_bytes = name.encode("utf-8")
            # Write name length, name, and tensor shape
            f.write(struct.pack("<i", len(name_bytes)))
            f.write(name_bytes)
            f.write(struct.pack("<i", len(data.shape)))
            f.write(struct.pack(f"<{len(data.shape)}i", *data.shape))
            # Write raw float32 data
            data_bytes = data.tobytes()
            f.write(struct.pack("<i", len(data_bytes)))
            f.write(data_bytes)
            total_weights += data.size

    file_size = os.path.getsize(output_bin_path)
    print(f">> [OK] Export complete!")
    print(f"   Total Parameters : {total_weights:,}")
    print(f"   Binary File Size : {file_size / (1024*1024):.2f} MB")
    print(f"   Output Path      : {output_bin_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", default="tools/vocoder/checkpoints/generator_latest.pt")
    parser.add_argument("--output", default="tools/vocoder/checkpoints/vocoder_weights.bin")
    args = parser.parse_args()
    export_binary(args.checkpoint, args.output)

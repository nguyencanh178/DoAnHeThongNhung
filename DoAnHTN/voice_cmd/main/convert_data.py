from pathlib import Path

models = [
    ("stage1_model.tflite", "stage1_model.cc", "stage1_model_tflite"),
    ("stage2_model.tflite", "stage2_model.cc", "stage2_model_tflite"),
]

for in_name, out_name, var_name in models:
    data = Path(in_name).read_bytes()

    with open(out_name, "w") as f:
        f.write('#include <stdint.h>\n\n')
        f.write(f'extern const unsigned char {var_name}[] = {{\n')

        for i, b in enumerate(data):
            if i % 12 == 0:
                f.write("  ")
            f.write(f"0x{b:02x}")
            if i != len(data) - 1:
                f.write(", ")
            if i % 12 == 11:
                f.write("\n")

        f.write("\n};\n")
        f.write(f"extern const unsigned int {var_name}_len = {len(data)};\n")

    print(f"Converted {in_name} -> {out_name}")
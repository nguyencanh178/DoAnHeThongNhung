import shutil
from pathlib import Path

SRC = Path("dataset/train")

STAGE1 = Path("stage1_original")
STAGE2 = Path("stage2_original")

CLASSES = [
    "stop",
    "swing",
    "turn_off",
    "turn_on_level_one",
    "turn_on_level_two",
    "turn_on_level_three"
]

def reset_dir(path):
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)

def copy_files(src_dir, dst_dir, prefix):
    dst_dir.mkdir(parents=True, exist_ok=True)

    files = list(src_dir.glob("*.wav"))

    for i, file in enumerate(files):
        dst = dst_dir / f"{prefix}_{i:04d}.wav"
        shutil.copy(file, dst)

def main():
    reset_dir(STAGE1)
    reset_dir(STAGE2)

    copy_files(SRC / "stop", STAGE1 / "stop", "stop")
    copy_files(SRC / "swing", STAGE1 / "swing", "swing")
    copy_files(SRC / "turn_off", STAGE1 / "turn_off", "turn_off")

    copy_files(SRC / "turn_on_level_one", STAGE1 / "turn_on", "turn_on_level_one")
    copy_files(SRC / "turn_on_level_two", STAGE1 / "turn_on", "turn_on_level_two")
    copy_files(SRC / "turn_on_level_three", STAGE1 / "turn_on", "turn_on_level_three")

    copy_files(SRC / "turn_on_level_one", STAGE2 / "level_one", "level_one")
    copy_files(SRC / "turn_on_level_two", STAGE2 / "level_two", "level_two")
    copy_files(SRC / "turn_on_level_three", STAGE2 / "level_three", "level_three")

    print("Done creating 2-stage original datasets")

if __name__ == "__main__":
    main()
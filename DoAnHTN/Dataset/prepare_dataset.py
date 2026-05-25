import shutil
import random
from pathlib import Path

import numpy as np
import librosa
import soundfile as sf
from sklearn.model_selection import train_test_split

SOURCE_DIR = Path("stage2_original")
OUTPUT_DIR = Path("stage2_dataset")

SAMPLE_RATE = 16000
DURATION = 2
SAMPLES = SAMPLE_RATE * DURATION

TRAIN_RATIO = 0.70
VAL_RATIO = 0.15
TEST_RATIO = 0.15

CLASSES = ["level_one", "level_two", "level_three"]

random.seed(42)
np.random.seed(42)


def reset_output():
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)

    for split in ["train", "val", "test"]:
        for cls in CLASSES:
            (OUTPUT_DIR / split / cls).mkdir(parents=True, exist_ok=True)


def load_audio(path):
    audio, _ = librosa.load(path, sr=SAMPLE_RATE, mono=True)

    audio, _ = librosa.effects.trim(audio, top_db=25)

    if len(audio) > SAMPLES:
        audio = audio[:SAMPLES]
    else:
        pad_left = (SAMPLES - len(audio)) // 2
        pad_right = SAMPLES - len(audio) - pad_left
        audio = np.pad(audio, (pad_left, pad_right))

    return audio.astype(np.float32)


def save_audio(path, audio):
    audio = np.clip(audio, -1.0, 1.0)
    sf.write(path, audio, SAMPLE_RATE)


def add_noise(audio):
    noise_amp = random.uniform(0.0015, 0.008)
    noise = noise_amp * np.random.randn(len(audio))
    return audio + noise


def change_gain(audio):
    gain = random.uniform(0.7, 1.35)
    return audio * gain


def time_shift(audio):
    shift = random.randint(-2200, 2200)
    return np.roll(audio, shift)


def time_stretch(audio):
    rate = random.uniform(0.92, 1.08)
    stretched = librosa.effects.time_stretch(audio, rate=rate)

    if len(stretched) > SAMPLES:
        stretched = stretched[:SAMPLES]
    else:
        pad_left = (SAMPLES - len(stretched)) // 2
        pad_right = SAMPLES - len(stretched) - pad_left
        stretched = np.pad(stretched, (pad_left, pad_right))

    return stretched


def augment_audio(audio, cls):
    if cls == "level_three":
        choice = random.choice([
            "noise",
            "gain",
            "shift",
            "stretch",
            "mix",
            "mix",
            "stretch"
        ])
    else:
        choice = random.choice([
            "noise",
            "gain",
            "shift",
            "stretch",
            "mix"
        ])

    if choice == "noise":
        return add_noise(audio)

    if choice == "gain":
        return change_gain(audio)

    if choice == "shift":
        return time_shift(audio)

    if choice == "stretch":
        return time_stretch(audio)

    audio = change_gain(audio)
    audio = add_noise(audio)
    audio = time_shift(audio)
    return audio


def split_dataset():
    reset_output()

    for cls in CLASSES:
        files = list((SOURCE_DIR / cls).glob("*.wav"))
        random.shuffle(files)

        train_files, temp_files = train_test_split(
            files,
            train_size=TRAIN_RATIO,
            random_state=42
        )

        val_files, test_files = train_test_split(
            temp_files,
            test_size=TEST_RATIO / (VAL_RATIO + TEST_RATIO),
            random_state=42
        )

        for split, split_files in [
            ("train", train_files),
            ("val", val_files),
            ("test", test_files)
        ]:
            for file in split_files:
                dst = OUTPUT_DIR / split / cls / file.name
                shutil.copy(file, dst)

        print(f"{cls}: train={len(train_files)}, val={len(val_files)}, test={len(test_files)}")


def augment_train():
    for cls in CLASSES:
        train_dir = OUTPUT_DIR / "train" / cls
        files = list(train_dir.glob("*.wav"))

        if cls == "level_three":
            aug_num = 18
        elif cls == "level_two":
            aug_num = 12
        else:
            aug_num = 10

        for file in files:
            audio = load_audio(file)

            for i in range(aug_num):
                aug = augment_audio(audio, cls)
                out_name = f"{file.stem}_aug_{i + 1}.wav"
                save_audio(train_dir / out_name, aug)


def count_files():
    print("\nFinal Stage 2 dataset:")

    for split in ["train", "val", "test"]:
        print(f"\n{split}:")
        for cls in CLASSES:
            count = len(list((OUTPUT_DIR / split / cls).glob("*.wav")))
            print(f"  {cls}: {count}")


if __name__ == "__main__":
    split_dataset()
    augment_train()
    count_files()
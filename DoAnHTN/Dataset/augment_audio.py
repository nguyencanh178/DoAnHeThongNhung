import librosa
import numpy as np
import os
import soundfile as sf
from glob import glob
import random

LABELS = [
    "stop",
    "swing",
    "turn_off",
    "turn_on_level_one",
    "turn_on_level_two",
    "turn_on_level_three"
]

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
INPUT_DATA = os.path.join(BASE_DIR, "dataset", "train")
OUTPUT_DATA = os.path.join(BASE_DIR, "final_dataset")

random.seed(42)
np.random.seed(42)

def get_label_from_filename(filename):
    name = os.path.splitext(os.path.basename(filename))[0].lower()

    for label in sorted(LABELS, key=len, reverse=True):
        if name.startswith(label.lower()):
            return label

    return None

def add_white_noise(data, noise_factor=0.005):
    noise = np.random.randn(len(data))
    return np.clip(data + noise_factor * noise, -1, 1)

def time_shift(data, sampling_rate, shift_max=0.1):
    shift = np.random.randint(int(sampling_rate * shift_max))
    if np.random.rand() > 0.5:
        shift = -shift
    return np.roll(data, shift)

def pitch_scale(data, sampling_rate, pitch_factor):
    return librosa.effects.pitch_shift(data, sr=sampling_rate, n_steps=pitch_factor)

def random_gain(data, min_gain=0.5, max_gain=1.5):
    gain = np.random.uniform(min_gain, max_gain)
    return np.clip(data * gain, -1, 1)

def save_audio(path, data, sr):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sf.write(path, data, sr)

def augment_one_file(file_path, out_dir, target_sr=16000):
    data, sr = librosa.load(file_path, sr=target_sr, mono=True)

    base_name = os.path.splitext(os.path.basename(file_path))[0]

    save_audio(os.path.join(out_dir, base_name + "_orig.wav"), data, target_sr)
    save_audio(os.path.join(out_dir, base_name + "_noise.wav"), add_white_noise(data), target_sr)
    save_audio(os.path.join(out_dir, base_name + "_shift.wav"), time_shift(data, target_sr), target_sr)
    save_audio(os.path.join(out_dir, base_name + "_pitch_up.wav"), pitch_scale(data, target_sr, 2), target_sr)
    save_audio(os.path.join(out_dir, base_name + "_pitch_down.wav"), pitch_scale(data, target_sr, -2), target_sr)
    save_audio(os.path.join(out_dir, base_name + "_gain.wav"), random_gain(data), target_sr)

def split_files(files):
    random.shuffle(files)

    n = len(files)
    n_train = int(n * 0.7)
    n_val = int(n * 0.15)

    train_files = files[:n_train]
    val_files = files[n_train:n_train + n_val]
    test_files = files[n_train + n_val:]

    return {
        "train": train_files,
        "val": val_files,
        "test": test_files
    }

def main():
    audio_files = glob(os.path.join(INPUT_DATA, "**/*.wav"), recursive=True)

    grouped = {label: [] for label in LABELS}

    for file_path in audio_files:
        label = get_label_from_filename(file_path)

        if label is None:
            print("Bỏ qua file không nhận diện nhãn:", file_path)
            continue

        grouped[label].append(file_path)

    for label, files in grouped.items():
        parts = split_files(files)

        for split_name, split_list in parts.items():
            for file_path in split_list:
                out_dir = os.path.join(OUTPUT_DATA, split_name, label)

                if split_name == "train":
                    augment_one_file(file_path, out_dir)
                else:
                    data, sr = librosa.load(file_path, sr=16000, mono=True)
                    base_name = os.path.splitext(os.path.basename(file_path))[0]
                    save_audio(os.path.join(out_dir, base_name + ".wav"), data, 16000)

        print(label, "train", len(parts["train"]), "val", len(parts["val"]), "test", len(parts["test"]))

    print("DONE")

if __name__ == "__main__":
    main()
import os
import numpy as np
import soundfile as sf
from gtts import gTTS
import tempfile
import subprocess

commands = {
    "turn_on_level_one": "Turn on level one",
    "turn_on_level_two": "Turn on level two",
    "turn_on_level_three": "Turn on level three",
    "swing": "Swing",
    "stop": "Stop",
    "turn_off": "Turn off"
}

SAMPLE_RATE = 16000
NUM_SAMPLES_PER_CLASS = 10
OUTPUT_DIR = "dataset/train"

def add_noise(audio):
    noise = np.random.normal(0, 0.005, audio.shape)
    return audio + noise

def process_audio(input_file, output_file):
    data, sr = sf.read(input_file)

    # nếu stereo → chuyển mono
    if len(data.shape) > 1:
        data = data.mean(axis=1)

    # thêm noise
    data = add_noise(data)

    # resample nếu cần
    if sr != SAMPLE_RATE:
        import scipy.signal
        data = scipy.signal.resample(data, int(len(data) * SAMPLE_RATE / sr))

    sf.write(output_file, data, SAMPLE_RATE)

def generate_audio(text, out_wav):
    tts = gTTS(text=text, lang='en')

    temp_mp3 = out_wav.replace(".wav", ".mp3")
    tts.save(temp_mp3)

    subprocess.run([
        "ffmpeg", "-y",
        "-i", temp_mp3,
        "-ar", str(SAMPLE_RATE),
        "-ac", "1",
        out_wav
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # đợi ffmpeg release file
    import time
    time.sleep(0.2)

    os.remove(temp_mp3)

def main():
    for label, text in commands.items():
        folder = os.path.join(OUTPUT_DIR, label)
        os.makedirs(folder, exist_ok=True)

        for i in range(NUM_SAMPLES_PER_CLASS):
            out_file = os.path.join(folder, f"{label}_{i}.wav")

            generate_audio(text, out_file)
            process_audio(out_file, out_file)

            print("Generated:", out_file)

if __name__ == "__main__":
    main()
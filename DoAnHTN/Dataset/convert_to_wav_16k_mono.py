import os
from pydub import AudioSegment

COMMANDS = [
    "stop",
    "swing",
    "turn_on_level_one",
    "turn_on_level_two",
    "turn_on_level_three",
    "turn_off"
]

INPUT_ROOT = "Data"                 # thư mục chứa dữ liệu gốc
OUTPUT_ROOT = "dataset\\train"       # thư mục output
START_INDEX = 60

audio_exts = [".mp3", ".wav", ".m4a", ".aac", ".ogg", ".flac"]

os.makedirs(OUTPUT_ROOT, exist_ok=True)

for command in COMMANDS:
    input_dir = os.path.join(INPUT_ROOT, command)
    output_dir = os.path.join(OUTPUT_ROOT, command)

    os.makedirs(output_dir, exist_ok=True)

    if not os.path.isdir(input_dir):
        print(f"Không thấy thư mục: {input_dir}")
        continue

    files = [
        f for f in os.listdir(input_dir)
        if os.path.splitext(f)[1].lower() in audio_exts
    ]

    files.sort()

    index = START_INDEX

    for file in files:
        input_path = os.path.join(input_dir, file)
        output_name = f"{command}_{index}.wav"
        output_path = os.path.join(output_dir, output_name)

        try:
            audio = AudioSegment.from_file(input_path)
            audio = audio.set_frame_rate(16000)
            audio = audio.set_channels(1)
            audio = audio.set_sample_width(2)

            audio.export(output_path, format="wav")

            print(f"{input_path} -> {output_path}")
            index += 1

        except Exception as e:
            print(f"Lỗi file {input_path}: {e}")

print("DONE")
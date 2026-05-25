import numpy as np
import tensorflow as tf
import soundfile as sf
from pathlib import Path
from sklearn.metrics import confusion_matrix, classification_report
import matplotlib.pyplot as plt

MODEL_PATH = "voice_command_model.tflite"
LABELS_PATH = "labels.txt"
TEST_DIR = Path("final_dataset/test")

SAMPLE_RATE = 16000
SAMPLES = SAMPLE_RATE * 2
NUM_MEL_BINS = 40

with open(LABELS_PATH, "r") as f:
    labels = [line.strip() for line in f.readlines()]

interpreter = tf.lite.Interpreter(model_path=MODEL_PATH)
interpreter.allocate_tensors()

input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

def normalize_audio(audio):
    audio = audio.astype(np.float32)

    audio = audio - np.mean(audio)

    rms = np.sqrt(np.mean(audio ** 2) + 1e-9)
    target_rms = 3000.0 / 32768.0

    audio = audio * (target_rms / (rms + 1e-9))
    audio = np.clip(audio, -1.0, 1.0)

    return audio

def audio_to_mel(audio):
    audio = tf.convert_to_tensor(audio, dtype=tf.float32)

    stft = tf.signal.stft(
        audio,
        frame_length=256,
        frame_step=128,
        window_fn=tf.signal.hann_window
    )

    spec = tf.abs(stft)

    mel_matrix = tf.signal.linear_to_mel_weight_matrix(
        num_mel_bins=NUM_MEL_BINS,
        num_spectrogram_bins=129,
        sample_rate=SAMPLE_RATE,
        lower_edge_hertz=80.0,
        upper_edge_hertz=7600.0
    )

    mel_spec = tf.tensordot(spec, mel_matrix, 1)
    mel_spec.set_shape(spec.shape[:-1].concatenate(mel_matrix.shape[-1:]))

    mel_spec = tf.math.log(mel_spec + 1e-6)

    mean = tf.reduce_mean(mel_spec)
    std = tf.math.reduce_std(mel_spec)
    mel_spec = (mel_spec - mean) / (std + 1e-6)

    mel_spec = tf.expand_dims(mel_spec, axis=-1)
    mel_spec = tf.expand_dims(mel_spec, axis=0)

    return mel_spec.numpy().astype(np.float32)

def predict_file(file_path):
    audio, sr = sf.read(file_path)

    if len(audio.shape) > 1:
        audio = audio.mean(axis=1)

    if sr != SAMPLE_RATE:
        raise ValueError(f"{file_path} không phải 16kHz, sr={sr}")

    if len(audio) < SAMPLES:
        audio = np.pad(audio, (0, SAMPLES - len(audio)))
    else:
        audio = audio[:SAMPLES]

    audio = normalize_audio(audio)
    spec = audio_to_mel(audio)

    interpreter.set_tensor(input_details[0]["index"], spec)
    interpreter.invoke()

    output = interpreter.get_tensor(output_details[0]["index"])[0]

    return int(np.argmax(output)), float(np.max(output))

y_true = []
y_pred = []

for label_idx, label in enumerate(labels):
    folder = TEST_DIR / label

    if not folder.exists():
        print("Không thấy folder:", folder)
        continue

    for wav_file in folder.glob("*.wav"):
        pred_idx, conf = predict_file(str(wav_file))

        y_true.append(label_idx)
        y_pred.append(pred_idx)

        print(
            wav_file.name,
            "true =", label,
            "pred =", labels[pred_idx],
            "conf =", round(conf, 3)
        )

cm = confusion_matrix(y_true, y_pred, labels=list(range(len(labels))))
cm_norm = cm.astype("float") / np.maximum(cm.sum(axis=1, keepdims=True), 1)

print("\nClassification report:")
print(
    classification_report(
        y_true,
        y_pred,
        target_names=labels,
        labels=list(range(len(labels))),
        zero_division=0
    )
)

plt.figure(figsize=(9, 7))
plt.imshow(cm_norm, cmap="Greys", vmin=0, vmax=1)

diag = np.zeros_like(cm_norm)
np.fill_diagonal(diag, np.diag(cm_norm))
plt.imshow(diag, cmap="Blues", vmin=0, vmax=1)

for i in range(len(labels)):
    for j in range(len(labels)):
        value = cm_norm[i, j]
        color = "white" if value > 0.5 else "black"
        plt.text(
            j,
            i,
            f"{value*100:.1f}%",
            ha="center",
            va="center",
            fontsize=10,
            color=color
        )

plt.xticks(np.arange(len(labels)), labels, rotation=45, ha="right")
plt.yticks(np.arange(len(labels)), labels)

plt.title("Confusion Matrix (%)")
plt.xlabel("Predicted label")
plt.ylabel("True label")

plt.tight_layout()
plt.savefig("confusion_matrix_pretty.png", dpi=200)
plt.show()
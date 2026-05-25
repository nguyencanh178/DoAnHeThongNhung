import numpy as np
import tensorflow as tf
import soundfile as sf

MODEL_PATH = "voice_command_model.tflite"
LABELS_PATH = "labels.txt"
TEST_FILE = r"final_dataset\test\stop\stop_1_shift.wav"

SAMPLE_RATE = 16000
SAMPLES = SAMPLE_RATE * 2
NUM_MEL_BINS = 40

with open(LABELS_PATH, "r") as f:
    labels = [line.strip() for line in f.readlines()]

interpreter = tf.lite.Interpreter(model_path=MODEL_PATH)
interpreter.allocate_tensors()

input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

def audio_to_mel(audio):
    stft = tf.signal.stft(audio, frame_length=256, frame_step=128)
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
    mel_spec = tf.expand_dims(mel_spec, axis=-1)
    mel_spec = tf.expand_dims(mel_spec, axis=0)

    return mel_spec.numpy().astype(np.float32)

audio, sr = sf.read(TEST_FILE)

if len(audio.shape) > 1:
    audio = audio.mean(axis=1)

if len(audio) < SAMPLES:
    audio = np.pad(audio, (0, SAMPLES - len(audio)))
else:
    audio = audio[:SAMPLES]

audio = audio.astype(np.float32)
spec = audio_to_mel(audio)

print("Input shape:", spec.shape)
print("Input dtype:", spec.dtype)
print("Model expects:", input_details[0]["shape"], input_details[0]["dtype"])

interpreter.set_tensor(input_details[0]["index"], spec)
interpreter.invoke()

output = interpreter.get_tensor(output_details[0]["index"])[0]

pred_idx = np.argmax(output)
confidence = output[pred_idx]

print("Predict:", labels[pred_idx])
print("Confidence:", float(confidence))
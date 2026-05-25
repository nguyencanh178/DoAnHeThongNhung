import numpy as np
import tensorflow as tf
import librosa
from pathlib import Path
from sklearn.metrics import confusion_matrix, classification_report
import matplotlib.pyplot as plt

SAMPLE_RATE = 16000
DURATION = 2
SAMPLES = SAMPLE_RATE * DURATION

FRAME_LENGTH = 255
FRAME_STEP = 127

BATCH_SIZE = 16
EPOCHS = 50

NUM_MEL_BINS = 40
N_FFT = 512
HOP_LENGTH = 160
WIN_LENGTH = 400


def get_waveform(file_path):
    audio, _ = librosa.load(file_path, sr=SAMPLE_RATE, mono=True)

    audio, _ = librosa.effects.trim(audio, top_db=25)

    if len(audio) > SAMPLES:
        audio = audio[:SAMPLES]
    else:
        pad_left = (SAMPLES - len(audio)) // 2
        pad_right = SAMPLES - len(audio) - pad_left
        audio = np.pad(audio, (pad_left, pad_right))

    return audio.astype(np.float32)


def get_spectrogram(waveform):
    mel = librosa.feature.melspectrogram(
        y=waveform,
        sr=SAMPLE_RATE,
        n_fft=N_FFT,
        hop_length=HOP_LENGTH,
        win_length=WIN_LENGTH,
        n_mels=NUM_MEL_BINS,
        fmin=80,
        fmax=7600,
        power=2.0
    )

    log_mel = librosa.power_to_db(mel, ref=np.max)

    log_mel = (log_mel - log_mel.mean()) / (log_mel.std() + 1e-6)

    log_mel = log_mel.T

    log_mel = np.expand_dims(log_mel, axis=-1)

    return log_mel.astype(np.float32)


def load_dataset(dataset_dir, classes):
    X = []
    y = []

    dataset_dir = Path(dataset_dir)

    for idx, cls in enumerate(classes):
        files = list((dataset_dir / cls).glob("*.wav"))

        print(f"Loading {cls}: {len(files)} files")

        for file in files:
            waveform = get_waveform(str(file))

            spec = get_spectrogram(waveform)

            X.append(spec)
            y.append(idx)

    X = np.array(X, dtype=np.float32)
    y = np.array(y, dtype=np.int32)

    return X, y


def build_model(num_classes):
    model = tf.keras.Sequential([

        tf.keras.layers.Input(shape=(201, 40, 1)),

        tf.keras.layers.Conv2D(
            16,
            (3, 3),
            padding="same",
            activation="relu"
        ),

        tf.keras.layers.BatchNormalization(),

        tf.keras.layers.MaxPooling2D((2, 2)),

        tf.keras.layers.Conv2D(
            32,
            (3, 3),
            padding="same",
            activation="relu"
        ),

        tf.keras.layers.BatchNormalization(),

        tf.keras.layers.MaxPooling2D((2, 2)),

        tf.keras.layers.Conv2D(
            64,
            (3, 3),
            padding="same",
            activation="relu"
        ),

        tf.keras.layers.BatchNormalization(),

        tf.keras.layers.MaxPooling2D((2, 2)),

        tf.keras.layers.GlobalAveragePooling2D(),

        tf.keras.layers.Dense(
            64,
            activation="relu"
        ),

        tf.keras.layers.Dropout(0.35),

        tf.keras.layers.Dense(
            num_classes,
            activation="softmax"
        )
    ])

    model.compile(
        optimizer=tf.keras.optimizers.Adam(
            learning_rate=3e-4
        ),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"]
    )

    return model


def print_cnn_layers(model):
    print("\n================ CNN ARCHITECTURE ================\n")

    print(
        f"{'STT':<5}"
        f"{'Layer Name':<30}"
        f"{'Layer Type':<25}"
        f"{'Output Shape':<25}"
        f"{'Params'}"
    )

    print("-" * 110)

    for i, layer in enumerate(model.layers, start=1):

        layer_name = layer.name

        layer_type = layer.__class__.__name__

        output_shape = str(layer.output.shape)

        params = layer.count_params()

        print(
            f"{i:<5}"
            f"{layer_name:<30}"
            f"{layer_type:<25}"
            f"{output_shape:<25}"
            f"{params}"
        )

    print("\n==================================================")
    print("TOTAL PARAMETERS:", model.count_params())
    print("==================================================\n")


def plot_confusion_matrix(
        y_true,
        y_pred,
        classes,
        title,
        output_path
):
    cm = confusion_matrix(y_true, y_pred)

    cm_percent = (
        cm.astype("float")
        / cm.sum(axis=1)[:, np.newaxis]
        * 100
    )

    plt.figure(figsize=(8, 6))

    plt.imshow(cm_percent, interpolation="nearest")

    plt.title(title)

    plt.colorbar()

    tick_marks = np.arange(len(classes))

    plt.xticks(
        tick_marks,
        classes,
        rotation=45,
        ha="right"
    )

    plt.yticks(
        tick_marks,
        classes
    )

    for i in range(cm_percent.shape[0]):
        for j in range(cm_percent.shape[1]):

            plt.text(
                j,
                i,
                f"{cm_percent[i, j]:.1f}%",
                ha="center",
                va="center",
                color="white"
                if cm_percent[i, j] > 50
                else "black"
            )

    plt.ylabel("True label")

    plt.xlabel("Predicted label")

    plt.tight_layout()

    plt.savefig(output_path)

    plt.show()


def convert_to_tflite(model, output_name):
    converter = tf.lite.TFLiteConverter.from_keras_model(model)

    # converter.optimizations = [tf.lite.Optimize.DEFAULT]

    tflite_model = converter.convert()

    with open(output_name, "wb") as f:
        f.write(tflite_model)


def train_stage(stage_name, dataset_dir, classes):

    print(f"\n================ TRAIN {stage_name} ================\n")

    X_train, y_train = load_dataset(
        f"{dataset_dir}/train",
        classes
    )

    X_val, y_val = load_dataset(
        f"{dataset_dir}/val",
        classes
    )

    X_test, y_test = load_dataset(
        f"{dataset_dir}/test",
        classes
    )

    print("Train:", X_train.shape, y_train.shape)
    print("Val:", X_val.shape, y_val.shape)
    print("Test:", X_test.shape, y_test.shape)

    model = build_model(len(classes))

    print_cnn_layers(model)

    model.summary()

    checkpoint = tf.keras.callbacks.ModelCheckpoint(
        f"best_{stage_name}.keras",
        monitor="val_loss",
        save_best_only=True,
        mode="min"
    )

    early_stop = tf.keras.callbacks.EarlyStopping(
        monitor="val_loss",
        patience=8,
        restore_best_weights=True
    )

    reduce_lr = tf.keras.callbacks.ReduceLROnPlateau(
        monitor="val_loss",
        factor=0.5,
        patience=3,
        min_lr=1e-6
    )

    class_weight = None

    if stage_name == "stage2_model":
        class_weight = {
            0: 1.0,
            1: 1.0,
            2: 1.8
        }

    history = model.fit(
        X_train,
        y_train,
        validation_data=(X_val, y_val),
        epochs=EPOCHS,
        batch_size=BATCH_SIZE,
        callbacks=[
            checkpoint,
            early_stop,
            reduce_lr
        ],
        shuffle=True,
        class_weight=class_weight
    )

    model = tf.keras.models.load_model(
        f"best_{stage_name}.keras"
    )

    test_loss, test_acc = model.evaluate(
        X_test,
        y_test
    )

    print(
        f"\n{stage_name} "
        f"test_acc = {test_acc:.4f}, "
        f"test_loss = {test_loss:.4f}"
    )

    y_prob = model.predict(X_test)

    y_pred = np.argmax(y_prob, axis=1)

    print("\n===== CLASSIFICATION REPORT =====\n")

    print(
        classification_report(
            y_test,
            y_pred,
            target_names=classes
        )
    )

    plot_confusion_matrix(
        y_test,
        y_pred,
        classes,
        f"{stage_name} Confusion Matrix (%)",
        f"{stage_name}_confusion_matrix.png"
    )

    convert_to_tflite(
        model,
        f"{stage_name}.tflite"
    )

    with open(f"{stage_name}_labels.txt", "w") as f:
        for cls in classes:
            f.write(cls + "\n")

    print(f"\nSaved: {stage_name}.tflite")
    print(f"Saved: {stage_name}_labels.txt")


def main():

    stage1_classes = [
        "stop",
        "swing",
        "turn_off",
        "turn_on"
    ]

    stage2_classes = [
        "level_one",
        "level_two",
        "level_three"
    ]

    train_stage(
        stage_name="stage1_model",
        dataset_dir="stage1_dataset",
        classes=stage1_classes
    )

    train_stage(
        stage_name="stage2_model",
        dataset_dir="stage2_dataset",
        classes=stage2_classes
    )


if __name__ == "__main__":
    main()
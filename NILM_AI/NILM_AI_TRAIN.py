import pandas as pd
import numpy as np
import tensorflow as tf

from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, confusion_matrix

from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Dense
from tensorflow.keras.utils import to_categorical

from sklearn.decomposition import PCA
import matplotlib.pyplot as plt

# ================= CONFIG =================
FILES_LABELS = [
    ("FT_Fan_2.csv", 0),
    ("FT_Lap_3.csv", 1),
    ("FT_Adapter_2.csv", 2),
    ("FT_Lap_2.csv", 3),
]

FEATURE_COLS = [
    "Irms","Irms_voltage","P","Q","Ipeak","CF","max_di_dt",
    "H85","H100","H150","H170","H250","H350"
]

EPOCHS = 25
BATCH_SIZE = 32
INPUT_SIZE = len(FEATURE_COLS)

# ================= LOAD DATA =================
dfs = []
for f, lbl in FILES_LABELS:
    df = pd.read_csv(f)
    df["label"] = lbl
    dfs.append(df)

data = pd.concat(dfs, ignore_index=True)

X = data[FEATURE_COLS].values
y = data["label"].values
num_classes = len(np.unique(y))

X_train, X_test, y_train, y_test = train_test_split(
    X, y,
    test_size=0.2,
    random_state=42,
    stratify=y
)

X_min = X_train.min(axis=0)
X_max = X_train.max(axis=0)

X_train = (X_train - X_min) / (X_max - X_min)
X_test  = (X_test  - X_min) / (X_max - X_min)

y_train_cat = to_categorical(y_train, num_classes)
y_test_cat  = to_categorical(y_test, num_classes)

# ================= MODEL =================
model = Sequential([
    Dense(32, activation="relu", input_shape=(INPUT_SIZE,)),
    Dense(16, activation="relu"),
    Dense(num_classes, activation="softmax")
])

model.compile(
    optimizer="adam",
    loss="categorical_crossentropy",
    metrics=["accuracy"]
)

# ================= TRAIN =================
model.fit(
    X_train, y_train_cat,
    epochs=EPOCHS,
    batch_size=BATCH_SIZE,
    validation_data=(X_test, y_test_cat)
)

# ================= EVALUATE =================
y_pred = np.argmax(model.predict(X_test), axis=1)

print("\nConfusion Matrix:")
print(confusion_matrix(y_test, y_pred))

print("\nClassification Report:")
print(classification_report(y_test, y_pred))

# ================= EXPORT ONLY .CC =================
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
tflite_model = converter.convert()

with open("load_classifier.cc", "w") as f:
    f.write("const unsigned char load_classifier_tflite[] = {\n")
    for i, b in enumerate(tflite_model):
        if i % 12 == 0:
            f.write("\n ")
        f.write(f"0x{b:02x}, ")
    f.write("\n};\n\n")
    f.write(f"const unsigned int load_classifier_tflite_len = {len(tflite_model)};\n")

print("\nExported: load_classifier.cc")

# ================= PRINT MIN/MAX FOR ESP32 =================
print("\n===== COPY TO ESP32 =====")

print("float X_min[INPUT_SIZE] = {")
for v in X_min:
    print(f"{v:.6f},", end=" ")
print("};\n")

print("float X_max[INPUT_SIZE] = {")
for v in X_max:
    print(f"{v:.6f},", end=" ")
print("};")

CLASS_NAMES = {
    0: "Fan",
    1: "Laptop_Dũng",
    2: "Adapter",
    3: "Laptop_Cường"
}

# ================= PCA  =================
pca = PCA(n_components=2)
X_train_pca = pca.fit_transform(X_train)
X_test_pca  = pca.transform(X_test)

print("Explained variance ratio:", pca.explained_variance_ratio_)
print("Total variance (2 components):",
      np.sum(pca.explained_variance_ratio_))

# ================= PLOT =================
plt.figure(figsize=(8,6))

for lbl in np.unique(y_train):
    idx = y_train == lbl
    plt.scatter(
        X_train_pca[idx, 0],
        X_train_pca[idx, 1],
        label=CLASS_NAMES[lbl],
        alpha=0.75,
        s=60
    )

plt.xlabel("PCA 1")
plt.ylabel("PCA 2")
plt.title("PCA of Electrical Load Features (Train Set)")
plt.legend(title="Load Type")
plt.grid(True)
plt.tight_layout()
plt.show()

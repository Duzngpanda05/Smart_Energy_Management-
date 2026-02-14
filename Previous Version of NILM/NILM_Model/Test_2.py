# -*- coding: utf-8 -*-
import tensorflow as tf
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os
from tensorflow.keras.utils import to_categorical

# =====================================================
# 1. Load và tiền xử lý dữ liệu
# =====================================================

fan = pd.read_csv("Dataset/Fan.csv")
computer = pd.read_csv("Dataset/Computer.csv")

fan["label"] = 0
computer["label"] = 1
data = pd.concat([fan, computer], ignore_index=True)
data = data.sample(frac=1, random_state=42).reset_index(drop=True)

X = data[["Vrms_current", "Vrms", "Irms", "P", "S", "PF"]].values
y = data["label"].values

# 🔹 In ra min, max của từng đặc trưng
print("X_min =", X.min(axis=0))
print("X_max =", X.max(axis=0))

# 🔹 Chuẩn hóa (Min-Max)
X_min = X.min(axis=0)
X_max = X.max(axis=0)
X = (X - X_min) / (X_max - X_min + 1e-8)

y = to_categorical(y, 2)

# =====================================================
# 2. Chia dữ liệu train/val/test
# =====================================================

num_samples = len(X)
train_end = int(0.7 * num_samples)
val_end = int(0.85 * num_samples)

X_train, X_val, X_test = X[:train_end], X[train_end:val_end], X[val_end:]
y_train, y_val, y_test = y[:train_end], y[train_end:val_end], y[val_end:]

# =====================================================
# 3. Xây dựng mô hình MLP
# =====================================================

num_inputs = 6
num_hiddens = 32
num_outputs = 2

model = tf.keras.Sequential([
    tf.keras.layers.Input(shape=(num_inputs,)),
    tf.keras.layers.Dense(num_hiddens, activation='relu'),
    tf.keras.layers.Dense(16, activation='relu'),
    tf.keras.layers.Dense(num_outputs)
])

# =====================================================
# 4. Loss, Optimizer, Metrics
# =====================================================

loss_fn = tf.keras.losses.CategoricalCrossentropy(from_logits=True)
optimizer = tf.keras.optimizers.Adam(learning_rate=0.001)
train_acc = tf.keras.metrics.CategoricalAccuracy()
val_acc = tf.keras.metrics.CategoricalAccuracy()
test_acc = tf.keras.metrics.CategoricalAccuracy()

# =====================================================
# 5. Training loop
# =====================================================

batch_size = 32
epochs = 30

train_dataset = tf.data.Dataset.from_tensor_slices((X_train, y_train)).shuffle(1000).batch(batch_size)
val_dataset = tf.data.Dataset.from_tensor_slices((X_val, y_val)).batch(batch_size)
test_dataset = tf.data.Dataset.from_tensor_slices((X_test, y_test)).batch(batch_size)

for epoch in range(epochs):
    for X_batch, y_batch in train_dataset:
        with tf.GradientTape() as tape:
            logits = model(X_batch, training=True)
            loss_value = loss_fn(y_batch, logits)
        grads = tape.gradient(loss_value, model.trainable_variables)
        optimizer.apply_gradients(zip(grads, model.trainable_variables))
        train_acc.update_state(y_batch, logits)

    for X_batch, y_batch in val_dataset:
        logits = model(X_batch, training=False)
        val_acc.update_state(y_batch, logits)

    print(f"Epoch {epoch+1:02d}: "
          f"Train Acc = {train_acc.result().numpy():.4f}, "
          f"Val Acc = {val_acc.result().numpy():.4f}")

    train_acc.reset_state()
    val_acc.reset_state()

# =====================================================
# 6. Đánh giá trên test set
# =====================================================

for X_batch, y_batch in test_dataset:
    logits = model(X_batch, training=False)
    test_acc.update_state(y_batch, logits)

print("\nFinal Test Accuracy =", test_acc.result().numpy())

# =====================================================
# 7. Lưu và chuyển sang TensorFlow Lite
# =====================================================

model.save("nilm_model.h5")

converter = tf.lite.TFLiteConverter.from_keras_model(model)
tflite_model = converter.convert()

with open("nilm_model.tflite", "wb") as f:
    f.write(tflite_model)

print("✅ Model converted to TensorFlow Lite format and saved as 'nilm_model.tflite'")

# =====================================================
# 8. Tạo file .cc dùng cho ESP32
# =====================================================

# xxd -i nilm_model.tflite > nilm_model_data.cc 
print("✅ File 'nilm_model_data.cc' đã được tạo thành công!")

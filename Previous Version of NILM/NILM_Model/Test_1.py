import pandas as pd

# Đọc file CSV
df = pd.read_csv("Dataset/Computer.csv")

# Đổi tên cột 'label' thành 'labels'
df = df.rename(columns={"label": "labels"})

# Ghi lại file CSV
df.to_csv("Dataset/Computers.csv", index=False)

print("✅ Đã đổi tên cột 'label' thành 'labels'")

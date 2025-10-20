from flask import Flask, render_template, request, jsonify
from collections import deque
from datetime import datetime

app = Flask(__name__)

# Lưu mẫu gần đây để hiển thị/biểu đồ (khoảng ~5 phút nếu ESP32 gửi mỗi 1s)
HISTORY_MAXLEN = 600
history = deque(maxlen=HISTORY_MAXLEN)

# Bản ghi mới nhất
latest = {
    "vrms": 0.0,          # Volt (RMS)
    "irms": 0.0,          # Ampere (RMS)
    "apparent": 0.0,      # VA = Vrms * Irms
    "freq": 50.0,         # Hz (nếu bạn đo/ước lượng ở ESP32)
    "timestamp": "--:--:--"
}

@app.route("/")
def index():
    return render_template("server.html")

# ESP32 POST JSON vào đây: {"vrms":xx.x, "irms":y.y, "freq":50.0}
@app.route("/update_measurement", methods=["POST"])
def update_measurement():
    global latest
    data = request.get_json(silent=True)
    if not data:
        return jsonify({"status": "error", "message": "No JSON body"}), 400

    if "vrms" not in data or "irms" not in data:
        return jsonify({"status": "error", "message": "Missing vrms/irms"}), 400

    try:
        vrms = float(data["vrms"])
        irms = float(data["irms"])
        freq = float(data.get("freq", latest["freq"]))
    except (ValueError, TypeError):
        return jsonify({"status": "error", "message": "Invalid value types"}), 400

    timestamp = datetime.now().strftime('%H:%M:%S')
    apparent = vrms * irms

    latest = {
        "vrms": vrms,
        "irms": irms,
        "apparent": apparent,
        "freq": freq,
        "timestamp": timestamp
    }

    history.append({
        "t": timestamp,
        "vrms": vrms,
        "irms": irms,
        "apparent": apparent
    })

    print(f"[{timestamp}] Vrms={vrms:.2f}V  Irms={irms:.3f}A  S={apparent:.1f}VA  f={freq:.2f}Hz")
    return jsonify({"status": "success"})

@app.route("/get_measurement")
def get_measurement():
    return jsonify(latest)

@app.route("/get_history")
def get_history():
    # Trả về một mảng các điểm để Chart.js vẽ
    return jsonify(list(history))

if __name__ == "__main__":
    # Mở cho LAN, debug bật cho dev
    app.run(host="0.0.0.0", port=5000, debug=True)

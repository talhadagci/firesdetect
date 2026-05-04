import time
import threading
import serial
import re
import random
import os
import numpy as np
import pandas as pd
from collections import deque
from flask import Flask, jsonify, render_template, request
from sklearn.linear_model import LogisticRegression
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import LabelEncoder
from sklearn.metrics import accuracy_score

app = Flask(__name__)

# ============================================================
# THRESHOLD DEFINITIONS (Eşik Değerleri)
# ============================================================
# Sıcaklık: 25°C üstü anormal kabul edilir.
TEMP_THRESHOLD = 25.0

# Gaz/Duman (MQ-2 ADC): 620 üstü duman/gaz var demek.
GAS_THRESHOLD = 620

# Alev (Flame IR ADC): Aktif-low sensör.
# Sensörün normal (ateşsiz) değeri ~3900 civarı (12-bit ADC, ~3.14V).
# 2000 altına düşerse alev algılandı demektir.
FLAME_THRESHOLD = 2000

# Ani sıcaklık artışı eşiği (°C / okuma başına)
TEMP_SPIKE_THRESHOLD = 2.0

# ============================================================
# GLOBAL STATE
# ============================================================
latest_data = {
    "temperature": 25.0,
    "humidity": 45.0,
    "gas": 400,
    "flame_raw": 4095,
    "flame": 0,
    "risk": 0.0,
    "status": "LOW",
    "fire_alert": False,
    "active_sensors": 0,
    "temp_high": False,
    "gas_high": False,
    "flame_detected": False,
    "trend_warning": False
}

last_data_time = time.time()

# Trend analysis: son 5 sıcaklık okumasını tut
temp_trend = deque(maxlen=5)
prev_temp = None

# ============================================================
# MACHINE LEARNING MODEL
# ============================================================
def train_model():
    """
    Sentetik veriyle Logistic Regression modeli eğit.
    ML modeli, rule-based sistemle BİRLİKTE çalışacak.
    ML -> 0-1 arası olasılık üretir.
    Rule-based -> LOW/MEDIUM/HIGH belirler.
    Final karar = ikisinin birleşimi.
    """
    X, y = [], []
    for _ in range(3000):
        temp = random.uniform(15, 80)
        gas = random.uniform(100, 4000)
        flame = random.choice([0, 1])

        # Eğitim mantığı: yeni eşik değerleriyle uyumlu
        score = 0
        if temp > 25:
            score += 1
        if gas > 620:
            score += 1
        if flame == 1:
            score += 1

        label = 1 if score >= 2 else 0
        X.append([temp, gas, flame])
        y.append(label)

    model = LogisticRegression()
    model.fit(X, y)
    return model

ml_model = train_model()

# ============================================================
# MULTI-SENSOR DECISION ENGINE
# ============================================================
def evaluate_risk(temp, gas, flame_binary):
    """
    Çok-sensörlü akıllı karar mekanizması.
    
    Rule-Based Katman:
      - 0 sensör aktif -> LOW  (risk düşük)
      - 1 sensör aktif -> LOW  (tek sensör güvenilir değil)
      - 2 sensör aktif -> MEDIUM (ciddi şüphe)
      - 3 sensör aktif -> HIGH (kesin yangın)
      - Flame tek başına -> HIGH (alev = acil)
    
    ML Katman:
      - Logistic Regression olasılık üretir (0.0 - 1.0)
    
    Final:
      - Rule-based seviye + ML olasılığı birleştirilir.
    """
    global prev_temp

    # --- Sensör durumlarını belirle ---
    temp_high = temp > TEMP_THRESHOLD
    gas_high = gas > GAS_THRESHOLD
    flame_detected = flame_binary == 1

    # --- Trend Analizi: Ani sıcaklık artışı ---
    trend_warning = False
    if prev_temp is not None:
        temp_diff = temp - prev_temp
        if temp_diff > TEMP_SPIKE_THRESHOLD:
            trend_warning = True
    prev_temp = temp

    # Trend bilgisini kaydet
    temp_trend.append(temp)

    # --- Aktif sensör sayısını hesapla ---
    active = int(temp_high) + int(gas_high) + int(flame_detected)

    # Eğer ani sıcaklık artışı varsa, ekstra +1 sensör gibi say
    if trend_warning:
        active = min(active + 1, 3)

    # --- Rule-Based Karar ---
    if flame_detected:
        # Alev algılandı = ne olursa olsun HIGH
        rule_status = "HIGH"
    elif active >= 3:
        rule_status = "HIGH"
    elif active == 2:
        rule_status = "MEDIUM"
    elif active == 1:
        rule_status = "WATCH"  # 1 sensör = tam güvenli değil!
    else:
        rule_status = "LOW"

    # --- ML Olasılık ---
    ml_prob = ml_model.predict_proba([[temp, gas, flame_binary]])[0][1]

    # --- Final Risk Skoru ---
    # Rule-based ağırlık: %60, ML ağırlık: %40
    rule_score = {"LOW": 0.10, "WATCH": 0.30, "MEDIUM": 0.55, "HIGH": 0.90}[rule_status]
    final_risk = (rule_score * 0.6) + (ml_prob * 0.4)
    final_risk = min(1.0, max(0.0, final_risk))

    # --- Final Durum ---
    if final_risk > 0.7:
        final_status = "HIGH"
        fire_alert = True
    elif final_risk > 0.4:
        final_status = "MEDIUM"
        fire_alert = False
    elif final_risk > 0.15:
        final_status = "WATCH"
        fire_alert = False
    else:
        final_status = "LOW"
        fire_alert = False

    # Flame override: Alev varsa ne olursa olsun HIGH
    if flame_detected:
        final_status = "HIGH"
        fire_alert = True
        final_risk = max(final_risk, 0.85)

    return {
        "risk": final_risk,
        "status": final_status,
        "fire_alert": fire_alert,
        "active_sensors": active,
        "temp_high": temp_high,
        "gas_high": gas_high,
        "flame_detected": flame_detected,
        "trend_warning": trend_warning
    }

# ============================================================
# DATA PROCESSING
# ============================================================
def process_and_update(raw_temp, raw_gas, raw_flame, raw_hum):
    """
    Ham veriyi işle, risk değerlendir ve global state güncelle.
    """
    # Flame normalizasyonu (analog -> dijital)
    flame_binary = 1 if raw_flame < FLAME_THRESHOLD else 0

    # Multi-sensor risk değerlendirmesi
    result = evaluate_risk(raw_temp, raw_gas, flame_binary)

    global last_data_time
    last_data_time = time.time()

    # Global state güncelle
    latest_data["temperature"] = round(raw_temp, 1)
    latest_data["humidity"] = round(raw_hum, 1)
    latest_data["gas"] = int(raw_gas)
    latest_data["flame_raw"] = raw_flame
    latest_data["flame"] = flame_binary
    latest_data["risk"] = result["risk"]
    latest_data["status"] = result["status"]
    latest_data["fire_alert"] = result["fire_alert"]
    latest_data["active_sensors"] = result["active_sensors"]
    latest_data["temp_high"] = result["temp_high"]
    latest_data["gas_high"] = result["gas_high"]
    latest_data["flame_detected"] = result["flame_detected"]
    latest_data["trend_warning"] = result["trend_warning"]

def parse_serial_line(line):
    """
    STM32 UART çıktısını parse et.
    Format: "MQ2: 1200 | Flame: 4000 | Temp: 25.5 C | Nem: 45.0%"
    """
    try:
        if "MQ2:" not in line and "Flame:" not in line:
            return

        gas_match = re.search(r"MQ2:\s*(\d+)", line)
        flame_match = re.search(r"Flame:\s*(\d+)", line)
        temp_match = re.search(r"Temp:\s*(-?\d+\.\d+)", line)
        hum_match = re.search(r"Nem:\s*(-?\d+\.\d+)", line)

        raw_gas = int(gas_match.group(1)) if gas_match else latest_data["gas"]
        raw_flame = int(flame_match.group(1)) if flame_match else latest_data["flame_raw"]
        raw_temp = float(temp_match.group(1)) if temp_match else latest_data["temperature"]
        raw_hum = float(hum_match.group(1)) if hum_match else latest_data["humidity"]

        # Outlier rejection
        if raw_temp < -50 or raw_temp > 150:
            raw_temp = latest_data["temperature"]
        if raw_gas < 0 or raw_gas > 5000:
            raw_gas = latest_data["gas"]

        process_and_update(raw_temp, raw_gas, raw_flame, raw_hum)

    except Exception as e:
        print(f"Error parsing line: {line}. Exception: {e}")

# ============================================================
# SERIAL READER (Auto-Reconnect)
# ============================================================
def serial_reader():
    """
    Arka plan thread'i: Serial portu oku veya bağlantı ara.
    Otomatik yeniden bağlanma destekli.
    Buffer'ı sürekli temizleyerek HER ZAMAN en son veriyi okur.
    """
    ports = ['/dev/ttyACM0', '/dev/ttyUSB0']
    ser = None

    while True:
        # Bağlı değilse bağlanmayı dene
        if ser is None:
            for port in ports:
                try:
                    ser = serial.Serial(port, 115200, timeout=1)
                    ser.reset_input_buffer()  # Bağlanınca eski buffer'ı temizle
                    print(f"[*] Connected to STM32 on {port}")
                    break
                except Exception:
                    continue

        if ser is None:
            time.sleep(1)
        else:
            try:
                # Buffer'daki TÜM satırları oku, sadece SONUNCUYU kullan
                # Bu sayede eski birikmiş veriler atılır, en güncel veri işlenir
                last_valid_line = None
                while ser.in_waiting > 0:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line and "MQ2:" in line:
                        last_valid_line = line

                if last_valid_line:
                    parse_serial_line(last_valid_line)
                    
            except Exception as e:
                print(f"Serial read error: {e}. Attempting to reconnect...")
                try:
                    ser.close()
                except:
                    pass
                ser = None
                time.sleep(2)

# ============================================================
# FLASK ROUTES
# ============================================================
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/prediction')
def prediction_page():
    return render_template('prediction.html')

@app.route('/data')
def get_data():
    time_diff = time.time() - last_data_time
    if time_diff > 3.5:
        return jsonify({
            "connected": False,
            "message": "UART bağlantısı yok"
        })

    response_data = latest_data.copy()
    response_data["connected"] = True
    return jsonify(response_data)

# ============================================================
# FORESTFIRES PREDICTION MODEL (Random Forest)
# ============================================================
fire_model = None
fire_model_accuracy = 0.0
le_month = LabelEncoder()
le_day = LabelEncoder()

def train_fire_prediction_model():
    """
    forestfires.csv dataseti ile Random Forest modeli eğit.
    Model bir kez eğitilir ve cache'te tutulur.
    """
    global fire_model, fire_model_accuracy, le_month, le_day

    csv_path = os.path.join(os.path.dirname(__file__), 'forestfires.csv')
    if not os.path.exists(csv_path):
        print(f"[!] forestfires.csv bulunamadı: {csv_path}")
        return

    try:
        df = pd.read_csv(csv_path)

        # Label Encoding for Month and Day
        df['month'] = le_month.fit_transform(df['month'])
        df['day'] = le_day.fit_transform(df['day'])

        # Target: area > 0 ise yangın var (1), değilse yok (0)
        df['fire'] = (df['area'] > 0).astype(int)

        # Features: month, day, temp, RH (humidity), wind, rain
        # Bunlar kullanıcıdan alınabilir veya sistemden çekilebilir
        features = ['month', 'day', 'temp', 'RH', 'wind', 'rain']
        X = df[features]
        y = df['fire']

        X_train, X_test, y_train, y_test = train_test_split(
            X, y, test_size=0.2, random_state=42
        )

        fire_model = RandomForestClassifier(
            n_estimators=100, 
            random_state=42, 
            max_depth=10,
            min_samples_split=5
        )
        fire_model.fit(X_train, y_train)

        y_pred = fire_model.predict(X_test)
        fire_model_accuracy = accuracy_score(y_test, y_pred)
        print(f"[*] Fire Prediction Model trained. Accuracy: {fire_model_accuracy:.2%}")
    except Exception as e:
        print(f"[!] Model eğitim hatası: {e}")

# Model'i başlatışta eğit
train_fire_prediction_model()

@app.route('/predict', methods=['POST'])
def predict_fire():
    """
    Kullanıcı girdiği temp, humidity, wind, rain değerlerine göre
    yangın olasılığı tahmin et.
    """
    if fire_model is None:
        return jsonify({"error": "Model eğitilmedi. forestfires.csv bulunamadı."}), 500

    try:
        data = request.get_json()
        temp = float(data.get('temperature', 25))
        humidity = float(data.get('humidity', 50))
        wind = float(data.get('wind', 2))
        rain = float(data.get('rain', 0))

        # Mevcut ay ve gün bilgisini al (encoding için)
        import datetime
        now = datetime.datetime.now()
        curr_month_str = now.strftime('%b').lower()
        curr_day_str = now.strftime('%a').lower()

        # Eğer dataset'te olmayan bir ay/gün ise (nadiren), default ata
        try:
            m_enc = le_month.transform([curr_month_str])[0]
        except:
            m_enc = le_month.transform(['aug'])[0]
        
        try:
            d_enc = le_day.transform([curr_day_str])[0]
        except:
            d_enc = le_day.transform(['sun'])[0]

        # Feature DataFrame (Model eğitilirken kullanılan kolon isimleriyle aynı olmalı)
        features_df = pd.DataFrame([[m_enc, d_enc, temp, humidity, wind, rain]], 
                                   columns=['month', 'day', 'temp', 'RH', 'wind', 'rain'])
        probability = fire_model.predict_proba(features_df)[0][1]

        # Risk seviyesi belirleme
        if probability > 0.7:
            risk_level = "HIGH"
        elif probability > 0.35:
            risk_level = "MEDIUM"
        else:
            risk_level = "LOW"

        return jsonify({
            "fire_probability": round(probability, 4),
            "risk_level": risk_level,
            "model_accuracy": round(fire_model_accuracy, 4),
            "info": {
                "month": curr_month_str,
                "day": curr_day_str
            }
        })
    except Exception as e:
        return jsonify({"error": str(e)}), 400

if __name__ == '__main__':
    thread = threading.Thread(target=serial_reader, daemon=True)
    thread.start()
    app.run(host='0.0.0.0', port=5000, debug=False)

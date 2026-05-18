# Hướng dẫn cài đặt AI Robot trên Raspberry Pi 5

## 1. Yêu cầu phần cứng
- Raspberry Pi 5
- INMP441 (mic I2S)
- MAX98357A (khuếch đại loa I2S)
- Nút nhấn (push-to-talk)
- Loa

---

## 2. Nối dây phần cứng

### INMP441 (Mic) → Raspberry Pi
| INMP441 | GPIO Pin | Tên chân |
|---------|----------|----------|
| VDD     | Pin 1    | 3.3V     |
| GND     | Pin 6    | GND      |
| SCK     | Pin 12   | GPIO18   |
| WS      | Pin 35   | GPIO19   |
| SD      | Pin 38   | GPIO20   |

### MAX98357A (Loa) → Raspberry Pi
| MAX98357A | GPIO Pin | Tên chân |
|-----------|----------|----------|
| VIN       | Pin 1    | 3.3V     |
| GND       | Pin 6    | GND      |
| BCLK      | Pin 12   | GPIO18   |
| LRC       | Pin 35   | GPIO19   |
| DIN       | Pin 40   | GPIO21   |

### Nút Push-to-Talk → Raspberry Pi
| Nút   | GPIO Pin | Tên chân |
|-------|----------|----------|
| Chân 1 | Pin 11  | GPIO17   |
| Chân 2 | Pin 9   | GND      |

---

## 3. Cấu hình hệ thống

### 3.1 Bật I2S và overlay âm thanh
```bash
sudo nano /boot/firmware/config.txt
```
Thêm vào cuối file:
```
# I2S Audio
dtparam=i2s=on
dtoverlay=i2s-mmap
dtoverlay=googlevoicehat-soundcard
```
Lưu lại (Ctrl+X → Y → Enter) rồi reboot:
```bash
sudo reboot
```

### 3.2 Kiểm tra sau reboot
```bash
aplay -l
arecord -l
```
Phải thấy: `snd_rpi_googlevoicehat_soundcar` trong danh sách.

---

## 4. Cài đặt thư viện hệ thống

```bash
# Cập nhật hệ thống
sudo apt update && sudo apt upgrade -y

# PortAudio (cho sounddevice)
sudo apt install libportaudio2 -y

# FFmpeg (cho xử lý audio)
sudo apt install ffmpeg -y

# SWIG (để build lgpio)
sudo apt install swig -y

# lgpio dev (GPIO cho Pi 5)
sudo apt install liblgpio-dev python3-lgpio -y

# I2C tools (debug)
sudo apt install i2c-tools -y
```

---

## 5. Tạo môi trường Python và cài thư viện

```bash
# Tạo thư mục project
mkdir -p ~/Desktop/AI_Robot
cd ~/Desktop/AI_Robot

# Tạo virtual environment
python3 -m venv .venv
source .venv/bin/activate

# Cài các thư viện Python
pip install sounddevice numpy scipy requests python-dotenv gpiozero edge-tts

# Link lgpio vào venv (Pi 5)
ln -s /usr/lib/python3/dist-packages/lgpio.py .venv/lib/python3.*/site-packages/ 2>/dev/null || true
ln -s /usr/lib/python3/dist-packages/_lgpio*.so .venv/lib/python3.*/site-packages/ 2>/dev/null || true
```

---

## 6. Tạo file .env

```bash
nano .env
```
Nội dung:
```
GROQ_KEY=your_groq_api_key_here
ZALO_KEY=your_zalo_api_key_here
```

Thêm vào .gitignore:
```bash
echo ".env" >> .gitignore
```

---

## 7. Kiểm tra thiết bị âm thanh

```bash
# Xem sounddevice nhận device nào
python3 -c "import sounddevice as sd; print(sd.query_devices())"
```
Ghi lại số index của Google VoiceHAT → điền vào `INPUT_DEVICE` và `OUTPUT_DEVICE` trong `main.py`.

---

## 8. Cấu hình trong main.py

```python
SAMPLE_RATE   = 48000   # VoiceHAT chỉ hỗ trợ 48000 Hz
INPUT_DEVICE  = 0       # Số index từ lệnh query_devices()
OUTPUT_DEVICE = 0       # Số index từ lệnh query_devices()
PTT_PIN       = 17      # GPIO17 (Pin 11) — nút push-to-talk
```

---

## 9. Chạy chương trình

```bash
cd ~/Desktop/AI_Robot
source .venv/bin/activate
python main.py
```

---

## 10. Xử lý lỗi thường gặp

| Lỗi | Nguyên nhân | Cách sửa |
|-----|------------|----------|
| `PortAudio library not found` | Thiếu libportaudio2 | `sudo apt install libportaudio2 -y` |
| `Error querying device X` | Sai số device | Chạy `python3 -c "import sounddevice as sd; print(sd.query_devices())"` |
| `Invalid sample rate` | VoiceHAT cần 48000 Hz | Đặt `SAMPLE_RATE = 48000` |
| `Cannot determine SOC peripheral base address` | RPi.GPIO không hỗ trợ Pi 5 | Dùng gpiozero + lgpio |
| `Peak = 0.0000` | Mic không thu âm | Kiểm tra nối dây, kiểm tra overlay trong config.txt |
| `401 Unauthorized (Zalo)` | API key hết hạn | Lấy key mới tại developers.zalo.me |
| `404 Not Found (Zalo TTS)` | URL audio hết hạn | Thêm `time.sleep(1)` trước khi tải audio |
| `429 Too Many Requests` | Gọi API quá nhiều | Không retry khi 404, chỉ chờ rồi thử lại |

---

## 11. API Keys cần có

| Service | Dùng để | Lấy tại |
|---------|---------|---------|
| Groq | STT (Whisper) + LLM (LLaMA) | console.groq.com |
| Zalo AI | TTS tiếng Việt | developers.zalo.me |
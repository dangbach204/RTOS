import os
from dotenv import load_dotenv

import sounddevice as sd
import numpy as np
import requests
import io
import wave
import time
from gpiozero import Button

from scipy.signal import resample_poly
from math import gcd

# CẤU HÌNH 
load_dotenv()
GROQ_KEY = os.getenv("GROQ_API_KEY")
ZALO_KEY = os.getenv("ZALO_API_KEY")
ZALO_SPEAKER = 2
SAMPLE_RATE  = 48000

INPUT_DEVICE  = 0
OUTPUT_DEVICE = 0

# NÚT PUSH-TO-TALK
PTT_PIN = 17   # GPIO17 (Pin 11)

MAX_SECONDS = 10

# KHỞI TẠO GPIO
ptt_button = Button(17, pull_up=True)  # GPIO17, Pin 11

def is_button_pressed() -> bool:
    return ptt_button.is_pressed

# GHI ÂM KHI GIỮ NÚT ─
def capture_voice() -> np.ndarray | None:
    import time

    # Chờ người dùng nhấn nút
    print(">>> Nhấn và giữ nút để nói...")
    while not is_button_pressed():
        time.sleep(0.05)

    print(">>> [GIỮ NÚT] Đang ghi, nói vào mic...")
    chunk     = int(SAMPLE_RATE * 0.064)
    recording = []

    try:
        with sd.InputStream(
            samplerate=SAMPLE_RATE,
            channels=2,
            dtype="int32",
            blocksize=chunk,
            device=INPUT_DEVICE,
        ) as stream:
            while is_button_pressed():
                data, _ = stream.read(chunk)
                samples  = data[:, 0].astype(np.float32) / 2147483648.0
                recording.extend(samples.tolist())

                # Giới hạn tối đa MAX_SECONDS
                if len(recording) >= SAMPLE_RATE * MAX_SECONDS:
                    print(f">>> Đạt giới hạn {MAX_SECONDS}s, xử lý...")
                    break

        if len(recording) < int(SAMPLE_RATE * 0.3):
            print(">>> Quá ngắn, thử lại...")
            return None

        print(f">>> Nhả nút — ghi xong: {len(recording)/SAMPLE_RATE:.1f}s")
        return np.array(recording, dtype=np.float32)

    except sd.PortAudioError as e:
        print(f"[Lỗi mic] {e}")
        return None

# TẠO WAV IN-MEMORY (resample về 16000 Hz cho Whisper)
def to_wav_bytes(samples: np.ndarray) -> bytes:
    target_sr = 16000
    g = gcd(SAMPLE_RATE, target_sr)
    samples_16k = resample_poly(samples, target_sr // g, SAMPLE_RATE // g)

    pcm = (np.clip(samples_16k, -1.0, 1.0) * 32767).astype(np.int16)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(target_sr)
        wf.writeframes(pcm.tobytes())
    return buf.getvalue()

# SPEECH TO TEXT ── Groq Whisper
def speech_to_text(samples: np.ndarray) -> str:
    print(">>> STT: Groq Whisper...")
    wav_bytes = to_wav_bytes(samples)
    try:
        resp = requests.post(
            "https://api.groq.com/openai/v1/audio/transcriptions",
            headers={"Authorization": f"Bearer {GROQ_KEY}"},
            files={"file": ("audio.wav", wav_bytes, "audio/wav")},
            data={"model": "whisper-large-v3-turbo", "language": "vi"},
            timeout=20,
        )
        resp.raise_for_status()
        text = resp.json().get("text", "").strip()
        print(f">>> Bạn nói: {text}")
        return text
    except requests.RequestException as e:
        print(f"[Lỗi STT] {e}")
        return ""

# LLM ── Groq LLaMA
def call_llm(question: str) -> str:
    print(">>> AI đang suy nghĩ...")
    try:
        resp = requests.post(
            "https://api.groq.com/openai/v1/chat/completions",
            headers={
                "Authorization": f"Bearer {GROQ_KEY}",
                "Content-Type": "application/json",
            },
            json={
                "model": "llama-3.3-70b-versatile",
                "messages": [
                    {
                        "role": "system",
                        "content": (
                            "Bạn là trợ lý AI thông minh. "
                            "Trả lời ngắn gọn bằng tiếng Việt, "
                            "tối đa 2 câu, thân thiện và tự nhiên."
                        ),
                    },
                    {"role": "user", "content": question},
                ],
                "max_tokens": 200,
            },
            timeout=15,
        )
        resp.raise_for_status()
        answer = resp.json()["choices"][0]["message"]["content"].strip()
        print(f">>> AI: {answer}")
        return answer
    except requests.RequestException as e:
        print(f"[Lỗi LLM] {e}")
        return "Xin lỗi, tôi bị lỗi kết nối rồi."

# TTS ── Zalo AI → Loa
def tts_play(text: str):
    print(f">>> TTS Zalo: {text}")
    try:
        resp = requests.post(
            "https://api.zalo.ai/v1/tts/synthesize",
            headers={
                "apikey": ZALO_KEY,
                "Content-Type": "application/x-www-form-urlencoded",
            },
            data={
                "input": text,
                "speaker_id": ZALO_SPEAKER,
                "encode_type": 0,
            },
            timeout=15,
        )
        resp.raise_for_status()
        result = resp.json()

        if result.get("error_code") != 0:
            print(f"[Lỗi Zalo TTS] {result}")
            return

        audio_url = result["data"]["url"]
        time.sleep(1)  # Chờ Zalo tạo xong file
        audio_resp = requests.get(audio_url, timeout=20)
        if audio_resp.status_code != 200:
            time.sleep(2)
            audio_resp = requests.get(audio_url, timeout=20)
        audio_resp.raise_for_status()

        with wave.open(io.BytesIO(audio_resp.content), "rb") as wf:
            rate   = wf.getframerate()
            frames = wf.readframes(wf.getnframes())

        samples = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32767.0

        if rate != SAMPLE_RATE:
            g = gcd(SAMPLE_RATE, rate)
            samples = resample_poly(samples, SAMPLE_RATE // g, rate // g)

        sd.play(samples, samplerate=SAMPLE_RATE, device=OUTPUT_DEVICE)
        sd.wait()
        print(">>> TTS xong")

    except requests.RequestException as e:
        print(f"[Lỗi TTS network] {e}")
    except sd.PortAudioError as e:
        print(f"[Lỗi TTS audio] {e}")

# MAIN
if __name__ == "__main__":
    import sys

    if "--list-devices" in sys.argv:
        print(sd.query_devices())
        sys.exit(0)

    print("=== Trợ lý AI khởi động ===")
    print(f"Push-to-talk : GPIO{PTT_PIN} (Pin 11) → GND (Pin 9)")
    print(f"Input device : {INPUT_DEVICE}")
    print(f"Output device: {OUTPUT_DEVICE}")
    print(f"Sample rate  : {SAMPLE_RATE} Hz\n")

    try:
        tts_play("Xin chào! Nhấn và giữ nút để nói chuyện với tôi.")

        while True:
            samples = capture_voice()
            if samples is None:
                continue

            question = speech_to_text(samples)
            if not question:
                print(">>> Không nhận được giọng nói, thử lại...")
                continue

            answer = call_llm(question)
            tts_play(answer)
            print(">>> Sẵn sàng, nhấn nút để hỏi tiếp...\n")

    except KeyboardInterrupt:
        print("\n>>> Thoát.")
    finally:
        ptt_button.close()
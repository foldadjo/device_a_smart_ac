# Device A — Smart AC Controller

Target hardware:
- ESP32-S3 N16R8
- KY-022 IR Receiver
- KY-005 IR Transmitter
- MAX98357 amplifier (pin reserved)
- Speaker
- Microphone (model belum ditentukan)
- MAX485 TTL ↔ RS485
- Hi-Link 5V 4A
- Kehua KHDM60-1P energy meter

## Fungsi firmware aktif

1. Web UI hotspot dengan tab Ringkasan, Audio, Infrared, Meter, dan Sistem.
2. Belajar command `ON` dan `OFF` dari remote AC asli.
3. Mengatur suhu 16-30 lewat `IRremoteESP8266` / `IRac` common A/C API.
4. Menyimpan raw IR ON/OFF dan protocol AC ke NVS ESP32.
5. Mengirim ulang command `ON`, `OFF`, atau state AC hanya saat tombol web ditekan.
6. Receiver IR aktif hanya saat Learn atau Test Sensor.
7. OLED/LCD dinonaktifkan karena hardware rusak; fungsi lain tetap berjalan normal.
8. RGB LED menampilkan status idle, learn, receive, transmit, dan speaker.
9. MAX98357A memutar tone dan sampel ucapan Indonesia secara offline.
10. WiFi STA tersimpan di NVS dan MQTT control lewat broker public `broker.emqx.io:1883`.

## Struktur source

```text
src/main.cpp              Bootstrap firmware.
src/modules/              Driver/logic hardware seperti IR, OLED, LED, speaker.
src/tasks/                Service/task aplikasi seperti Web UI, MQTT, dan Wit.ai TTS.
include/config.h          Pin map dan konfigurasi umum.
archive/                  Firmware lama sebagai referensi.
```

## PlatformIO

Board:
`esp32-s3-devkitc-1`

Build:
```bash
pio run
```

Upload:
```bash
pio run -t upload
```

Serial monitor:
```bash
pio device monitor
pio device monitor -b 115200 
```

## Test IR

1. Sambungkan HP ke WiFi `AQU-AC-Remote`, password `12345678`.
2. Buka `http://192.168.4.1`.
3. Tekan `Learn ON`, lalu tekan tombol ON remote asli ke receiver.
4. Tekan `Learn OFF`, lalu tekan tombol OFF remote asli ke receiver.
5. Untuk suhu, buka menu `Config` dan pilih protocol AC. Jika Learn ON/OFF terbaca oleh library, protocol akan otomatis tersimpan.
6. Pakai menu `Control` untuk `Nyalakan AC`, `Matikan AC`, pilih suhu/mode/fan, lalu tekan `Kirim State AC`.
7. Pakai menu `Speaker` untuk mengatur volume, mengucapkan teks, atau menghentikan suara.

## Uji hardware firmware aktif

1. Upload firmware lalu hubungkan HP/laptop ke AP `AQU-AC-Remote` (password `12345678`).
2. Buka `http://192.168.4.1`.
3. Buka tab **Audio**, lalu tekan **Mulai Uji Audio**.
4. Bicara dekat microphone selama 4 detik.
5. Pastikan hasil microphone menunjukkan suara terdeteksi dan speaker mengucapkan kalimat Indonesia.
6. Infrared dan Modbus tetap dapat diperiksa dari tab masing-masing.

Sampel suara tersimpan sebagai PCM 8-bit di flash sehingga uji bicara speaker tidak membutuhkan
internet dan tidak memakai buffer audio besar di RAM.

Text-to-speech memerlukan koneksi WiFi internet. Server Access Token disimpan lokal di
`include/WitAiSecrets.h`; file tersebut diabaikan oleh Git. Gunakan
`include/WitAiSecrets.example.h` sebagai template.

## WiFi dan MQTT

1. Buka menu `WiFi` di Web UI.
2. Isi SSID dan password WiFi rumah, lalu tekan `Simpan WiFi`.
3. Setelah STA connected, alat otomatis connect ke MQTT broker `broker.emqx.io:1883`.
4. Topic control ditampilkan di menu `WiFi`: `hems/ac/<id esp>/ctr`.

Contoh payload MQTT:

```text
on
off
temp=24
protocol=midea
{"cmd":"set","temp":24,"mode":"cool","fan":"auto"}
{"cmd":"set","temp":24,"mode":"cool","fan":"auto","protocol":"midea"}
```

## Pin sementara

| Fungsi | GPIO |
|---|---:|
| IR RX | 4 |
| IR TX | 10 |
| RS485 RX (firmware aktif, modul auto-direction) | 18 |
| RS485 TX | 17 |
| RS485 DE/RE | Tidak dipakai oleh firmware aktif |
| MAX98357 BCLK | 8 |
| MAX98357 LRC/WS | 9 |
| MAX98357 DIN | 11 |
| OLED/LCD SDA | Dilepas; GPIO11 dipakai DIN amplifier |
| OLED/LCD SCL | Dilepas; OLED dinonaktifkan |
| Microphone BCLK | 6 |
| Microphone WS/LRC | 7 |
| Microphone SD/DATA | 15 |
| RGB LED onboard | 48 |

Wiring fisik dibahas terpisah sesuai permintaan.

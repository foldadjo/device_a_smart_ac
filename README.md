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
2. Belajar command `ON`, `OFF`, dan suhu 16–30 °C dari remote AC asli.
3. Mengatur suhu lewat raw IR hasil learn, atau `IRremoteESP8266` / `IRac` common A/C API sebagai fallback.
4. Menyimpan raw IR ON/OFF/suhu dan protocol AC ke NVS ESP32.
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
5. Di tab **Infrared**, pilih suhu 16–30 °C pada bagian **Learn suhu AC**, set remote asli ke suhu itu, lalu tekan `Learn suhu` dan tekan tombol remote sekali.
6. Gunakan `Kirim suhu` untuk menguji rekaman. Ulangi untuk suhu lain yang diperlukan.
7. Jika belum ada hasil learn untuk suatu suhu, firmware memakai protocol AC yang terdeteksi/diatur sebagai fallback.

## Uji hardware firmware aktif

1. Upload firmware lalu hubungkan HP/laptop ke AP `AQU-AC-Remote` (password `12345678`).
2. Buka `http://192.168.4.1`.
3. Buka tab **Audio**, lalu tekan **Mulai Uji Audio**.
4. Bicara dekat microphone selama 4 detik.
5. Pastikan hasil microphone menunjukkan suara terdeteksi dan speaker mengucapkan kalimat Indonesia.
6. Infrared dan Modbus tetap dapat diperiksa dari tab masing-masing.

Sampel suara tersimpan sebagai PCM 8-bit di flash sehingga uji bicara speaker tidak membutuhkan
internet dan tidak memakai buffer audio besar di RAM.

## Voice Assistant

Voice assistant memakai VAD/RMS lokal untuk mulai dan berhenti merekam. Hanya potongan yang
terdeteksi sebagai ucapan yang dikirim ke Wit.ai sebagai PCM 8 kHz untuk speech-to-text. Jawaban
speaker dirangkai dari potongan suara Indonesia di `src/voice_prompts.h`, sehingga respons tidak
memerlukan cloud TTS.

Sinyal microphone dikondisikan dengan penghilangan DC offset, peredam spike impulsif, noise
floor adaptif, dan hysteresis VAD. Setelah boot, biarkan area microphone tenang selama 3 detik.
Jika lingkungan berubah atau deteksi terlalu sensitif, buka tab **Audio**, tekan
**Kalibrasi Noise**, kemudian diam dan jangan menyentuh microphone selama 3 detik. Web UI akan
menampilkan RMS terkondisi, noise floor, ambang bicara, dan jumlah spike yang dibuang.
Trigger voice assistant juga menolak bunyi bip pendek atau nada dengan level konstan. Awal ucapan
tetap dipertahankan oleh pre-roll 300 ms. Buffer rekaman 5 detik dialokasikan statis agar proses
rekam tidak gagal karena fragmentasi heap WiFi/TLS.

Alur yang paling stabil adalah dua tahap:

1. Ucapkan **“Halo Stroomer”**, tunggu jawaban **“Siap”**.
2. Dalam 15 detik ucapkan salah satu perintah strict berikut:
   - `berapa tegangan`
   - `berapa arus`
   - `berapa daya`
   - `nyalakan AC`
   - `matikan AC`
   - `setting AC`
   - `set suhu dua puluh lima derajat` (rentang 16–30)

Saat `setting AC`, perangkat meminta tombol ON lalu OFF dari remote asli. Untuk keandalan paling
tinggi, rekam suhu yang dipakai pada tab **Infrared**; frame raw hasil learn akan diprioritaskan
daripada protocol umum. Status transcript, protocol, dan error STT dapat dilihat pada tab
**Audio**. Audio microphone dikirim ke Wit.ai dan fitur ini membutuhkan koneksi internet.

Speech-to-text memerlukan koneksi WiFi internet. Server Access Token disimpan lokal di
`include/WitAiSecrets.h`; file tersebut diabaikan oleh Git. Gunakan
`include/WitAiSecrets.example.h` sebagai template.

## WiFi dan MQTT

1. Buka menu `WiFi` di Web UI.
2. Isi SSID dan password WiFi rumah, lalu tekan `Simpan WiFi`.
3. Setelah STA connected, alat otomatis connect ke MQTT broker `broker.emqx.io:1883`.
4. Topic control ditampilkan di menu `WiFi`: `hems/ac/<id esp>/ctr`.

Payload state berfungsi sekaligus sebagai heartbeat MQTT. Interval default adalah 30 detik dan
dapat diubah pada tab **System → MQTT Heartbeat** dalam rentang 5–3600 detik. Nilai disimpan di
NVS sehingga tetap berlaku setelah restart. Polling Modbus tetap berjalan setiap 3 detik dan tidak
mengikuti interval heartbeat. Payload heartbeat menyertakan `meterValid` dan `heartbeatSec`.

## Demand response, alarm, dan sesi energi

- Pada tab **System → Demand Response**, aktifkan jadwal diskon dengan jam mulai dan selesai WIB.
  Jadwal menggunakan NTP dan dapat melewati tengah malam. Saat mulai/selesai, perangkat mengirim
  event MQTT dan memainkan pola bunyi berbeda. Jika jadwal tidak diaktifkan, fitur tidak berdampak.
- Pada tab **System → Alarm Kelistrikan**, isi ambang tegangan minimum, tegangan maksimum, dan
  daya maksimum dalam Watt. Nilai `0` berarti alarm tersebut nonaktif. Alarm berbunyi satu kali
  saat nilai menembus ambang dan hanya siap berbunyi lagi setelah kondisi kembali normal.
- Sesi energi dimulai saat daya aktif lebih dari **10 W** dan selesai saat di bawah **10 W**.
  Saat start/stop, firmware mengirim event pada `<prefix>/<id esp>/event` serta memaksa heartbeat
  state segera. Payload state memuat `energySessionActive`, `EnergySession`, dan
  `lastEnergySession`.

Contoh payload MQTT:

```text
on
off
temp=24
protocol=midea
{"cmd":"set","temp":24,"mode":"cool","fan":"auto"}
{"cmd":"set","temp":24,"mode":"cool","fan":"auto","protocol":"midea"}
tegangan
arus
daya
```

Perilaku command kontrol:

- `on` dan `off` mengirim raw IR ON/OFF yang sudah dipelajari. Jika raw belum tersedia, firmware
  mencoba common AC state dengan protocol aktif.
- `protocol=midea` memilih dan menyimpan protocol AC ke NVS. Nama protocol harus didukung `IRac`.
- `temp=24` memakai rekaman raw suhu 24 °C bila sudah di-learn pada Web UI. Jika belum ada,
  firmware mengirim state ON, mode cool, fan auto lewat protocol aktif. Jika belum ada protocol
  yang dikenali, firmware memakai dan menyimpan `MIDEA` sebagai fallback default agar transmitter
  tetap mengirim; fallback ini belum tentu cocok dengan merek/model AC.
- JSON `cmd=set` dapat mengatur `temp` (16–30), `mode` (`auto`, `cool`, `heat`, `dry`, `fan`),
  `fan` (`auto`, `min`, `low`, `medium`, `high`, `max`), dan opsional `protocol`.
- Hasil command terakhir dapat diperiksa di tab **System** dan status IR di tab **Infrared**.

Untuk meminta perangkat membacakan nilai meter, publish payload teks `tegangan`, `arus`, atau
`daya` ke topic:

```text
hems/ac/<id esp>/ctr
```

Contoh dengan Mosquitto:

```bash
mosquitto_pub -h broker.emqx.io -t 'hems/ac/<id esp>/ctr' -m 'tegangan'
mosquitto_pub -h broker.emqx.io -t 'hems/ac/<id esp>/ctr' -m 'arus'
mosquitto_pub -h broker.emqx.io -t 'hems/ac/<id esp>/ctr' -m 'daya'
```

Topic lengkap ditampilkan pada tab **System** di Web UI. Kirim command tanpa opsi retain agar
perangkat tidak mengulang ucapan lama setelah reconnect. Nilai yang dibacakan adalah pembacaan
Modbus valid terakhir; jika meter belum tersedia, speaker mengatakan bahwa meter tidak tersedia.

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

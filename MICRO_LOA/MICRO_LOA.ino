#include <Arduino.h>
#include <driver/i2s.h>

// --- Định nghĩa chân Micro (I2S_NUM_0) ---
#define I2S_MIC_CHANNEL   I2S_CHANNEL_FMT_RIGHT_LEFT
#define I2S_MIC_WS_PIN    4 // WS
#define I2S_MIC_SCK_PIN   26// SCK
#define I2S_MIC_SD_PIN    21 // SD

// --- Định nghĩa chân Loa (I2S_NUM_1) ---
#define I2S_SPEAKER_WS_PIN    33 // LRC
#define I2S_SPEAKER_SCK_PIN   19 // BCLK
#define I2S_SPEAKER_SD_PIN    22 // DIN

// --- Cài đặt I2S ---
#define I2S_MIC_PORT      I2S_NUM_0
#define I2S_SPEAKER_PORT  I2S_NUM_1

// Cài đặt âm thanh
#define SAMPLE_RATE       16000 // Tần số lấy mẫu (Hz)
#define BITS_PER_SAMPLE   I2S_BITS_PER_SAMPLE_16BIT // 16 bit cho loa
#define BUFFER_SIZE       512 // Kích thước bộ đệm (có thể tăng/giảm)

// Bộ đệm
int32_t raw_samples[BUFFER_SIZE];     // Đệm đọc 32-bit từ Micro
int16_t stereo_samples[BUFFER_SIZE * 2]; // Đệm ghi 16-bit (x2 vì stereo)


// --- Hàm khởi tạo Micro ---
void setup_i2s_mic() {
  i2s_config_t i2s_mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // Micro INMP441 xuất 32bit (24bit data)
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, 
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_driver_install(I2S_MIC_PORT, &i2s_mic_config, 0, NULL);

  i2s_pin_config_t mic_pins = {
    .bck_io_num = I2S_MIC_SCK_PIN,
    .ws_io_num = I2S_MIC_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD_PIN
  };

  i2s_set_pin(I2S_MIC_PORT, &mic_pins);
}

// --- Hàm khởi tạo Loa ---
void setup_i2s_speaker() {
  i2s_config_t i2s_speaker_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = BITS_PER_SAMPLE, // Loa MAX98357 nhận 16-bit
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // Gửi đi 2 kênh (Stereo)
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true, // Tự động xóa bộ đệm TX
    .fixed_mclk = 0
  };

  i2s_driver_install(I2S_SPEAKER_PORT, &i2s_speaker_config, 0, NULL);

  i2s_pin_config_t speaker_pins = {
    .bck_io_num = I2S_SPEAKER_SCK_PIN,
    .ws_io_num = I2S_SPEAKER_WS_PIN,
    .data_out_num = I2S_SPEAKER_SD_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_set_pin(I2S_SPEAKER_PORT, &speaker_pins);
}


void setup() {
  Serial.begin(115200);
  Serial.println("Bat dau test Parrot (Mic-in, Loa-out)...");

  // Khởi tạo cả hai
  setup_i2s_mic();
  setup_i2s_speaker();
  
  Serial.println("San sang, hay noi vao micro!");
}


void loop() {
  size_t bytes_read = 0;
  
  // 1. Đọc một khối (buffer) dữ liệu 32-bit từ Micro (I2S_NUM_0)
  i2s_read(I2S_MIC_PORT, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY);

  if (bytes_read > 0) {
    int samples_read = bytes_read / sizeof(int32_t);

    // 2. Xử lý và chuyển đổi
    for (int i = 0; i < samples_read; i++) {
      // Dữ liệu 32-bit từ INMP441 là 24-bit căn lề trái.
      // Chúng ta dịch phải 16 bit để lấy 16 bit quan trọng nhất (MSB)
      int16_t sample_16bit = (int16_t)(raw_samples[i] >> 16);

      // Gán mẫu mono này cho cả hai kênh (Trái và Phải) của bộ đệm loa
      // Tăng âm lượng lên một chút (ví dụ: nhân 2), vì tín hiệu mic gốc khá nhỏ
      int16_t amplified_sample = sample_16bit * 3; // <-- Bạn có thể tăng số này (vd: 3, 4)
      
      stereo_samples[i * 2]     = amplified_sample; // Kênh Trái
      stereo_samples[i * 2 + 1] = amplified_sample; // Kênh Phải
    }

    // 3. Ghi khối (buffer) dữ liệu 16-bit Stereo ra Loa (I2S_NUM_1)
    size_t bytes_written = 0;
    i2s_write(I2S_SPEAKER_PORT, stereo_samples, bytes_read * 2 / 2, &bytes_written, portMAX_DELAY);
    // (Giải thích: bytes_read * 2 / 2 = bytes_read. Kích thước mẫu 16bit*2kênh = 32bit.
    //  Đơn giản hơn: i2s_write(I2S_SPEAKER_PORT, stereo_samples, samples_read * sizeof(int16_t) * 2, &bytes_written, portMAX_DELAY);
  }
}
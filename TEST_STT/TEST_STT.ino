/*
 * Google Speech-to-Text (STT) trên ESP32
 * Yêu cầu thư viện:
 * 1. ArduinoJson (của Benoit Blanchon)
 *
 * LOGIC MỚI NHẤT (ĐÃ SỬA):
 * - Nhấn nút 1 lần (chân 13) để bắt đầu.
 * - Đèn LED (chân 19) sáng và in "Xin moi noi...".
 * - Tự động ghi âm cố định trong 3 giây.
 * - Hết 3 giây, đèn tắt, tự động gửi lên Google STT.
 * - Sử dụng hàm Base64 tự viết (không cần thư viện).
 */
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "driver/i2s.h"
#include <ArduinoJson.h> 

// ===== CẤU HÌNH WIFI & GOOGLE STT (DO BẠN CUNG CẤP) =====
const char* WIFI_SSID     = "P603";
const char* WIFI_PASSWORD = "0904362626";
const String GOOGLE_KEY   = "AIzaSyCgXdP5RP4irrSHW8YSD3UB9Pn9_3OAS_A";
const String GOOGLE_STT_URL = "https://speech.googleapis.com/v1/speech:recognize?key=" + GOOGLE_KEY;

// ===== CẤU HÌNH I2S MIC =====
#define I2S_PORT        I2S_NUM_0
#define I2S_WS_PIN      4
#define I2S_BCLK_PIN    5
#define I2S_SD_PIN      6 // Data In
#define BUTTON_PIN      13 // Nút nhấn nối từ chân 13 xuống GND
#define LED_PIN         19 // <<< Đèn LED xanh báo hiệu
#define SAMPLE_RATE     16000 
#define SAMPLES_PER_READ 1024 

// ===== CẤU HÌNH GHI ÂM & BỘ NHỚ =====
#define RECORD_SECONDS  4 // <<< THAY ĐỔI: Ghi âm tối đa 4 giây
const size_t RECORD_SIZE = SAMPLE_RATE * sizeof(int16_t) * RECORD_SECONDS; 
int16_t* audioBuffer = NULL; 

// Ngưỡng ghi âm tối thiểu (để lọc bấm nhầm): 0.2 giây
const size_t MIN_RECORD_SAMPLES = SAMPLE_RATE * 0.2; // 3200 mẫu

// ===== BIẾN TRẠNG THÁI =====
bool isRecording = false;
size_t currentSampleCount = 0;
unsigned long startTime = 0; 

// Khai báo client cho kết nối HTTPS
WiFiClientSecure client;
HTTPClient http;

// =========================================================================
// KHAI BÁO HÀM (Prototype)
// =========================================================================
void setupWiFi();
void setupI2S();
void startRecording();
void recordAudio();
void stopRecording();
String encodeAudioToBase64(int16_t* buffer, size_t length);
void sendToGoogleSTT(String base64Audio);

// =========================================================================
// HÀM: SETUP (KHỞI TẠO)
// =========================================================================

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n--- ESP32 Google STT (Nhan de ghi 4 giay, LED o chan 19) ---"); // <<< THAY ĐỔI
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT); // <<< Cấu hình chân LED
  digitalWrite(LED_PIN, LOW); // <<< Tắt đèn khi bắt đầu
  Serial.println("Chan 13 (Nut nhan) va Chan 19 (LED) da san sang.");

  setupI2S();
  setupWiFi();

  audioBuffer = (int16_t*) malloc(RECORD_SIZE);
  if (audioBuffer == NULL) {
    Serial.println("FATAL: Khong du bo nho RAM. Thu giam RECORD_SECONDS.");
    while(true);
  }
  
  // Bỏ qua xác thực SSL (Cần thiết cho ESP32)
  client.setInsecure();
}

// =========================================================================
// HÀM: VÒNG LẶP CHÍNH
// =========================================================================

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Loi ket noi WiFi...");
    delay(5000);
    return;
  }

  // Logic: Nhấn 1 lần để bắt đầu
  if (digitalRead(BUTTON_PIN) == LOW && !isRecording) {
    startRecording();
  }
  
  // Nếu đang ghi âm, tiếp tục ghi
  if (isRecording) {
    recordAudio();
  } else {
    // <<< THAY ĐỔI: Chỉ delay khi không ghi âm
    // Bằng cách này, 3 giây ghi âm sẽ là 3 giây ngoài đời thực.
    delay(50); 
  }
  
  // Logic nhả nút đã bị xóa bỏ.
  // Hàm stopRecording() sẽ được gọi tự động bởi recordAudio() khi đủ 3 giây.
  
  // delay(50); // <<< XÓA BỎ DELAY CHUNG Ở ĐÂY
}

// =========================================================================
// CÁC HÀM HỖ TRỢ
// =========================================================================

void setupWiFi() {
  Serial.print("Dang ket noi den WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nKet noi WiFi thanh cong!");
  } else {
    Serial.println("\nLoi ket noi WiFi.");
  }
}

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE, 
    .data_in_num = I2S_SD_PIN     
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  Serial.println("I2S Micro da cau hinh.");
}

void startRecording() {
  Serial.println("\nBat dau ghi... Xin moi noi..."); // <<< THAY ĐỔI: Gộp thông báo
  digitalWrite(LED_PIN, HIGH); // <<< Bật đèn LED
  currentSampleCount = 0;
  isRecording = true;
  startTime = millis(); 
}

void recordAudio() {
  size_t bytes_to_read = SAMPLES_PER_READ * sizeof(int16_t);
  
  // Tính toán số byte còn lại trong bộ đệm (để không ghi quá 3 giây)
  if (currentSampleCount + SAMPLES_PER_READ > RECORD_SIZE / sizeof(int16_t)) {
    bytes_to_read = (RECORD_SIZE / sizeof(int16_t) - currentSampleCount) * sizeof(int16_t);
  }

  // Nếu đã đầy (đủ 4 giây)
  if (bytes_to_read == 0) {
    Serial.println("Da du 4 giay ghi am. Tu dong dung."); // <<< THAY ĐỔI
    stopRecording(); // <<< Tự động gọi stopRecording() khi đủ 4 giây
    return;
  }

  int16_t temp_buffer[SAMPLES_PER_READ];
  size_t bytes_read = 0;
  
  esp_err_t result = i2s_read(I2S_PORT, temp_buffer, bytes_to_read, &bytes_read, 0); 
  
  if (result == ESP_OK && bytes_read > 0) {
    size_t samples_read = bytes_read / sizeof(int16_t);
    memcpy(&audioBuffer[currentSampleCount], temp_buffer, bytes_read);
    currentSampleCount += samples_read;
  }

  // <<< XÓA BỎ: Thông báo "Ghi duoc: x giay."
  /*
  if (currentSampleCount >= 16000 && (currentSampleCount % 16000) < SAMPLES_PER_READ) { 
    Serial.print("  > Ghi duoc: ");
    Serial.print(currentSampleCount / 16000);
    Serial.println(" giay.");
  }
  */
}

void stopRecording() {
  digitalWrite(LED_PIN, LOW); // <<< Tắt đèn LED ngay lập tức
  isRecording = false;
  unsigned long duration = millis() - startTime; 

  Serial.println("Ket thuc ghi."); // <<< THAY ĐỔI: Rút gọn log
  
  // Xóa bỏ các log chi tiết
  // Serial.print("\n[DA DUNG GHI AM]. Tong thoi gian: ");
  // Serial.print(duration / 1000.0);
  // Serial.println(" giay.");
  // Serial.print("Tong so mau: ");
  // Serial.println(currentSampleCount);

  if (currentSampleCount >= MIN_RECORD_SAMPLES) { 
    String base64Audio = encodeAudioToBase64(audioBuffer, currentSampleCount * sizeof(int16_t));
    if (base64Audio.length() > 0) {
        sendToGoogleSTT(base64Audio);
    }
  } else {
    Serial.print("CANH BAO: Ghi am qua ngan (");
    Serial.print(duration / 1000.0);
    Serial.print("s) de gui len Google (can toi thieu ");
    Serial.print((float)MIN_RECORD_SAMPLES / SAMPLE_RATE);
    Serial.println(" giay). Bo qua.");
  }
  
  currentSampleCount = 0;
}

/**
 * @brief Mã hóa Base64 bằng hàm tự viết (Không cần thư viện)
 * @param buffer Con trỏ tới bộ đệm int16_t
 * @param length Kích thước của bộ đệm TÍNH BẰNG BYTES
 */
String encodeAudioToBase64(int16_t* buffer, size_t length) {
  Serial.println("Dang ma hoa Base64 (dung ham tu viet)...");
  
  String tempEncoded = "";
  // Bảng tra cứu ký tự Base64
  char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  
  // Chuyển con trỏ int16_t* thành con trỏ byte (uint8_t*)
  uint8_t* byte_buffer = (uint8_t*) buffer;

  // Xử lý mỗi 3 byte (input) để tạo ra 4 ký tự (output)
  for (size_t i = 0; i < length; i += 3) {
    uint32_t triple = 0; // Một "bộ ba" 24-bit

    // Lấy 3 byte (hoặc ít hơn nếu gần cuối)
    for (int j = 0; j < 3; j++) {
      if (i + j < length) {
        uint8_t current_byte = byte_buffer[i + j];
        triple |= ((uint32_t)current_byte) << (16 - (j * 8));
      }
    }

    // Chuyển đổi "bộ ba" 24-bit thành 4 ký tự 6-bit
    for (int j = 0; j < 4; j++) {
      // Kiểm tra xem có cần thêm padding '=' không
      if (i + j * 3 / 4 < length) {
        tempEncoded += base64_chars[(triple >> (18 - (j * 6))) & 0x3F];
      } else {
        tempEncoded += '=';
      }
    }
  }

  Serial.println("Da ma hoa Base64.");
  return tempEncoded;
}


/**
 * @brief Gửi dữ liệu Base64 lên Google STT API (Sử dụng HTTPClient)
 * (Phiên bản này có tính năng Debug)
 */
void sendToGoogleSTT(String base64Audio) {
  Serial.println("Da gui len STT, dang cho phan hoi..."); // <<< THAY ĐỔI: Rút gọn log
  
  // Tạo Payload JSON
  String payload = "{\"config\":{\"encoding\":\"LINEAR16\",\"sampleRateHertz\":";
  payload += SAMPLE_RATE;
  payload += ",\"languageCode\":\"vi-VN\"},\"audio\":{\"content\":\"";
  payload += base64Audio;
  payload += "\"}}";

  String jsonResponse = "";

  if (http.begin(client, GOOGLE_STT_URL)) { // Sử dụng client (đã setInsecure)
    http.addHeader("Content-Type", "application/json");
    
    // Gửi yêu cầu POST
    int httpCode = http.POST(payload);
    
    if (httpCode > 0) {
      Serial.print("Ma phan hoi HTTP: ");
      Serial.println(httpCode);
      
      jsonResponse = http.getString(); // Lấy nội dung phản hồi
      
      if (httpCode != HTTP_CODE_OK) {
        Serial.println("--- LOI HTTP ---");
        Serial.println(jsonResponse); // In ra nội dung lỗi nếu không phải 200 OK
        Serial.println("----------------");
      }
    } else {
      Serial.print("Loi POST, ma loi HTTP: ");
      Serial.println(http.errorToString(httpCode).c_str());
    }
    
    http.end(); // Đóng kết nối
  } else {
    Serial.println("Khong the bat dau ket noi HTTP.");
  }

  // Xử lý JSON
  Serial.println("\n[PHAN HOI TU GOOGLE STT]");

  // Kích thước bộ đệm JSON
  const size_t capacity = JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(1) + 
                          JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(1) + 
                          JSON_OBJECT_SIZE(2) + 3000;
  DynamicJsonDocument doc(capacity);
  
  // Phân tích cú pháp JSON
  DeserializationError error = deserializeJson(doc, jsonResponse);

  if (error) {
    Serial.print("Loi phan tich JSON: ");
    Serial.println(error.c_str());
    
    // <<< TÍNH NĂNG DEBUG >>>
    Serial.println("--- NOI DUNG PHAN HOI THO (RAW RESPONSE) ---");
    Serial.println(jsonResponse); // In ra nội dung gây lỗi
    Serial.println("----------------------------------------------");
    return;
  }

  // Trích xuất kết quả
  JsonArray results = doc["results"].as<JsonArray>();

  if (results && results.size() > 0) {
      String transcript = results[0]["alternatives"][0]["transcript"].as<String>();
      // double confidence = results[0]["alternatives"][0]["confidence"].as<double>();
      
      Serial.print("Da nhan duoc: "); // <<< THAY ĐỔI: Rút gọn log
      Serial.println(transcript);
      
      // Xóa bỏ các log chi tiết
      // Serial.println("=================================================");
      // Serial.println("KET QUA NHAN DANG GIONG NOI:");
      // Serial.print("Cau noi: ");
      // Serial.println(transcript);
      // Serial.print("Do chinh xac: ");
      // Serial.println(confidence, 4);
      // Serial.println("=================================================");
  } else {
    // Trường hợp Google trả về lỗi (nhưng vẫn là JSON hợp lệ)
    if (doc.containsKey("error")) {
        Serial.print("LOI TU GOOGLE (JSON): ");
        Serial.println(doc["error"]["message"].as<String>());
    } else if (results.size() == 0) {
       Serial.println("Khong co ket qua nhan dang nao (Google khong nghe thay gi?).");
    } else {
        Serial.println("Phan hoi JSON khong hop le.");
    }
  }
}
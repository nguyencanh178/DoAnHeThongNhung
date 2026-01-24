  /*
  * ESP32 - "Parrot Bot" (Con Vẹt không có NÃO)
  * STT (Nghe) -> TTS (Nói lại y hệt)
  *
  * Gộp từ 2 file:
  * 1. STT (Mic I2S_NUM_0, Nút nhấn 13, LED 19, Ghi 4 giây)
  * 2. TTS (Loa I2S_NUM_1, Chân 15, 16, 7)
  *
  * LOGIC:
  * 1. Nhấn nút (13), LED (19) sáng, in "Bat dau ghi..."
  * 2. Thu âm 4 giây từ Mic (I2S_NUM_0).
  * 3. LED tắt, gửi âm thanh lên Google STT.
  * 4. Nhận text về (ví dụ: "xin chào").
  * 5. Gửi text ("xin chào") lên Google TTS.
  * 6. Nhận âm thanh Base64 về, giải mã.
  * 7. Phát âm thanh "xin chào" ra loa (I2S_NUM_1).
  * 8. Sẵn sàng cho lần nhấn nút tiếp theo.
  */

  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  #include <HTTPClient.h>
  #include <ArduinoJson.h>
  #include "driver/i2s.h"

  // ===== CẤU HÌNH WIFI & GOOGLE API (Dùng chung) =====
  const char* WIFI_SSID     = "P1205";
  const char* WIFI_PASSWORD = "0904362626";
  const String GOOGLE_KEY   = "AIzaSyCgXdP5RP4irrSHW8YSD3UB9Pn9_3OAS_A";

  // --- Google STT (Speech-to-Text) ---
  const String GOOGLE_STT_URL = "https://speech.googleapis.com/v1/speech:recognize?key=" + GOOGLE_KEY;
  // --- Google TTS (Text-to-Speech) ---
  const String GOOGLE_TTS_URL = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + GOOGLE_KEY;


  // ===== CẤU HÌNH I2S MIC (STT - I2S_NUM_0) =====
  #define I2S_MIC_PORT        I2S_NUM_0 // Dùng I2S Port 0 cho Mic
  #define I2S_MIC_WS_PIN      4         // Word Select
  #define I2S_MIC_BCLK_PIN    5         // Bit Clock
  #define I2S_MIC_SD_PIN      6         // Data In
  #define BUTTON_PIN          13        // Nút nhấn
  #define LED_PIN             19        // Đèn LED báo hiệu
  #define MIC_SAMPLE_RATE     16000 
  #define SAMPLES_PER_READ    1024 

  // ===== CẤU HÌNH I2S LOA (TTS - I2S_NUM_1) =====
  #define I2S_SPK_PORT        I2S_NUM_1 // Dùng I2S Port 1 cho Loa
  #define I2S_SPK_BCLK_PIN    15        // Bit Clock
  #define I2S_SPK_LRC_PIN     16        // Left/Right Clock (WS)
  #define I2S_SPK_DIN_PIN     7         // Data Out
  #define SPK_SAMPLE_RATE     16000
  #define I2S_CHUNK_SIZE      2048      // Kích thước chunk để phát PCM

  // ===== CẤU HÌNH GHI ÂM (STT) =====
  #define RECORD_SECONDS  4 // Ghi âm tối đa 4 giây
  const size_t RECORD_SIZE = MIC_SAMPLE_RATE * sizeof(int16_t) * RECORD_SECONDS; 
  int16_t* audioBuffer = NULL; // Bộ đệm thu âm (STT)
  const size_t MIN_RECORD_SAMPLES = MIC_SAMPLE_RATE * 0.2; // Ngưỡng 0.2 giây

  // ===== BỘ ĐỆM PHÁT ÂM (TTS) =====
  uint8_t* ttsBuf = nullptr; // Bộ đệm phát âm (TTS)
  size_t   ttsLen = 0;

  // ===== BIẾN TRẠNG THÁI =====
  bool isRecording = false;
  size_t currentSampleCount = 0;
  unsigned long startTime = 0; 

  // ===== KHAI BÁO CHUNG (Dùng cho cả STT và TTS) =====
  WiFiClientSecure client;
  HTTPClient http;

  // =========================================================================
  // KHAI BÁO HÀM (Prototype)
  // =========================================================================

  // --- Hàm chung ---
  void setupWiFi();

  // --- Hàm STT (Mic) ---
  void setupI2S_Mic();
  void startRecording();
  void recordAudio();
  void stopRecording();
  String encodeAudioToBase64(int16_t* buffer, size_t length);
  void sendToGoogleSTT(String base64Audio);

  // --- Hàm TTS (Loa) ---
  void setupI2S_Speaker();
  size_t b64_decode(uint8_t* out, const char* in, size_t n); // Base64 DEcoder
  void playPCM(const uint8_t* d, size_t n);
  bool googleTTS(const String& textVi); // Sẽ được gọi bởi STT

  // =========================================================================
  // HÀM: SETUP (KHỞI TẠO)
  // =========================================================================

  void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n--- ESP32 Parrot Bot (STT -> TTS) ---");
    
    // Cấu hình Nút nhấn và LED
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    Serial.println("Chan 13 (Nut nhan) va Chan 19 (LED) da san sang.");

    // Khởi tạo I2S cho Mic (Port 0)
    setupI2S_Mic();
    
    // Khởi tạo I2S cho Loa (Port 1)
    setupI2S_Speaker();

    // Khởi tạo WiFi
    setupWiFi();

    // Cấp phát bộ nhớ cho Mic (STT)
    audioBuffer = (int16_t*) malloc(RECORD_SIZE);
    if (audioBuffer == NULL) {
      Serial.println("FATAL: Khong du bo nho RAM cho STT. Thu giam RECORD_SECONDS.");
      while(true);
    }
    
    // Bỏ qua xác thực SSL (Cần thiết cho ESP32)
    client.setInsecure();
    Serial.println("He thong san sang. Nhan nut de noi.");
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
      // Chỉ delay khi không ghi âm
      delay(50); 
    }
  }

  // =========================================================================
  // I. CÁC HÀM CỦA STT (MICROPHONE)
  // =========================================================================

  void setupI2S_Mic() {
    i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = MIC_SAMPLE_RATE,
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
      .bck_io_num = I2S_MIC_BCLK_PIN,
      .ws_io_num = I2S_MIC_WS_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE, 
      .data_in_num = I2S_MIC_SD_PIN     
    };
    i2s_driver_install(I2S_MIC_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_MIC_PORT, &pin_config);
    Serial.println("I2S Mic (Port 0) da cau hinh.");
  }

  void startRecording() {
    Serial.println("\nBat dau ghi... Xin moi noi...");
    digitalWrite(LED_PIN, HIGH); // Bật đèn LED
    currentSampleCount = 0;
    isRecording = true;
    startTime = millis(); 
  }

  void recordAudio() {
    size_t bytes_to_read = SAMPLES_PER_READ * sizeof(int16_t);
    
    if (currentSampleCount + SAMPLES_PER_READ > RECORD_SIZE / sizeof(int16_t)) {
      bytes_to_read = (RECORD_SIZE / sizeof(int16_t) - currentSampleCount) * sizeof(int16_t);
    }

    // Nếu đã đầy (đủ 4 giây)
    if (bytes_to_read == 0) {
      // Serial.println("Da du 4 giay ghi am. Tu dong dung."); // Log rút gọn
      stopRecording(); // Tự động gọi stopRecording()
      return;
    }

    int16_t temp_buffer[SAMPLES_PER_READ];
    size_t bytes_read = 0;
    esp_err_t result = i2s_read(I2S_MIC_PORT, temp_buffer, bytes_to_read, &bytes_read, 0); 
    
    if (result == ESP_OK && bytes_read > 0) {
      size_t samples_read = bytes_read / sizeof(int16_t);
      memcpy(&audioBuffer[currentSampleCount], temp_buffer, bytes_read);
      currentSampleCount += samples_read;
    }
  }

  void stopRecording() {
    digitalWrite(LED_PIN, LOW); // Tắt đèn LED
    isRecording = false;
    // unsigned long duration = millis() - startTime; // Log rút gọn
    Serial.println("Ket thuc ghi.");
    
    if (currentSampleCount >= MIN_RECORD_SAMPLES) { 
      String base64Audio = encodeAudioToBase64(audioBuffer, currentSampleCount * sizeof(int16_t));
      if (base64Audio.length() > 0) {
          sendToGoogleSTT(base64Audio); // Bắt đầu chuỗi STT
      }
    } else {
      Serial.println("CANH BAO: Ghi am qua ngan. Bo qua.");
    }
    
    currentSampleCount = 0;
  }

  String encodeAudioToBase64(int16_t* buffer, size_t length) {
    // Serial.println("Dang ma hoa Base64 (dung ham tu viet)..."); // Log rút gọn
    
    String tempEncoded = "";
    char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t* byte_buffer = (uint8_t*) buffer;

    for (size_t i = 0; i < length; i += 3) {
      uint32_t triple = 0;
      for (int j = 0; j < 3; j++) {
        if (i + j < length) {
          uint8_t current_byte = byte_buffer[i + j];
          triple |= ((uint32_t)current_byte) << (16 - (j * 8));
        }
      }
      for (int j = 0; j < 4; j++) {
        if (i + j * 3 / 4 < length) {
          tempEncoded += base64_chars[(triple >> (18 - (j * 6))) & 0x3F];
        } else {
          tempEncoded += '=';
        }
      }
    }
    // Serial.println("Da ma hoa Base64."); // Log rút gọn
    return tempEncoded;
  }

  void sendToGoogleSTT(String base64Audio) {
    Serial.println("Da gui len STT, dang cho phan hoi...");
    
    String payload = "{\"config\":{\"encoding\":\"LINEAR16\",\"sampleRateHertz\":";
    payload += MIC_SAMPLE_RATE;
    payload += ",\"languageCode\":\"vi-VN\"},\"audio\":{\"content\":\"";
    payload += base64Audio;
    payload += "\"}}";

    String jsonResponse = "";

    if (http.begin(client, GOOGLE_STT_URL)) {
      http.addHeader("Content-Type", "application/json");
      int httpCode = http.POST(payload);
      
      if (httpCode > 0) {
        jsonResponse = http.getString();
        if (httpCode != HTTP_CODE_OK) {
          Serial.println("--- LOI HTTP (STT) ---");
          Serial.println(jsonResponse);
        }
      } else {
        Serial.print("Loi POST (STT), ma loi HTTP: ");
        Serial.println(http.errorToString(httpCode).c_str());
      }
      http.end();
    } else {
      Serial.println("Khong the bat dau ket noi HTTP (STT).");
    }

    // Xử lý JSON (STT)
    const size_t capacity = JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(1) + 
                            JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(1) + 
                            JSON_OBJECT_SIZE(2) + 3000;
    DynamicJsonDocument doc(capacity);
    DeserializationError error = deserializeJson(doc, jsonResponse);

    if (error) {
      Serial.print("Loi phan tich JSON (STT): "); Serial.println(error.c_str());
      Serial.println("--- NOI DUNG PHAN HOI THO (STT) ---");
      Serial.println(jsonResponse);
      Serial.println("-----------------------------------");
      Serial.println(">>> San sang nhan lenh (sau loi STT).");
      return;
    }

    JsonArray results = doc["results"].as<JsonArray>();

    if (results && results.size() > 0) {
        String transcript = results[0]["alternatives"][0]["transcript"].as<String>();
        Serial.print("Da nhan duoc (STT): ");
        Serial.println(transcript);
        
        // ===========================================
        // === "NÃO" KHÔNG NÃO (THE PARROT HOOK) ===
        // ===========================================
        Serial.println(">>> Dang bien dich thanh am thanh (TTS)...");
        if (googleTTS(transcript)) { // Gửi text vừa nhận được sang TTS
            Serial.println(">>> Dang phat lai am thanh (TTS)...");
            playPCM(ttsBuf, ttsLen); // Phat ra loa
        } else {
            Serial.println(">>> Loi phan hoi TTS.");
        }
        Serial.println(">>> Phat xong. San sang nhan lenh moi.");
        // ===========================================

    } else {
      if (doc.containsKey("error")) {
          Serial.print("LOI TU GOOGLE (STT JSON): ");
          Serial.println(doc["error"]["message"].as<String>());
      } else {
        Serial.println("Khong co ket qua nhan dang nao (STT khong nghe thay gi?).");
      }
      Serial.println(">>> San sang nhan lenh (sau loi STT).");
    }
  }


  // =========================================================================
  // II. CÁC HÀM CỦA TTS (LOA)
  // =========================================================================

  void setupI2S_Speaker(){
    i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SPK_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,   // mono
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0
    };
    i2s_pin_config_t pins = {
      .bck_io_num = I2S_SPK_BCLK_PIN,
      .ws_io_num  = I2S_SPK_LRC_PIN,
      .data_out_num = I2S_SPK_DIN_PIN,
      .data_in_num  = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_SPK_PORT, &cfg, 0, NULL);
    i2s_set_pin(I2S_SPK_PORT, &pins);
    i2s_set_clk(I2S_SPK_PORT, SPK_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
    i2s_zero_dma_buffer(I2S_SPK_PORT);
    Serial.println("I2S Loa (Port 1) da cau hinh.");
  }

  void playPCM(const uint8_t* d, size_t n){
    if(!d || !n) { Serial.println("[PLAY] buffer trống"); return; }
    i2s_start(I2S_SPK_PORT);
    size_t off=0;
    while(off<n){
      size_t chunk = (n-off < I2S_CHUNK_SIZE) ? (n-off) : I2S_CHUNK_SIZE;
      size_t wrote=0;
      i2s_write(I2S_SPK_PORT, d+off, chunk, &wrote, portMAX_DELAY);
      off += wrote;
      yield(); // Cho phép các tác vụ khác chạy
    }
    i2s_stop(I2S_SPK_PORT);
    i2s_zero_dma_buffer(I2S_SPK_PORT);
  }

  // --- Base64 decode tối giản (cho TTS) ---
  static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  static inline int b64v(char c){
    if(c>='A'&&c<='Z')return c-'A';
    if(c>='a'&&c<='z')return c-'a'+26;
    if(c>='0'&&c<='9')return c-'0'+52;
    if(c=='+')return 62; if(c=='/')return 63; return -1;
  }
  size_t b64_decode(uint8_t* out, const char* in, size_t n){
    size_t L=0; int bits=0, bc=0;
    for(size_t i=0;i<n;++i){
      int v=b64v(in[i]); if(v<0) continue;
      bits=(bits<<6)|v; bc+=6;
      if(bc>=8){ bc-=8; out[L++]=(uint8_t)(bits>>bc); }
    }
    return L;
  }

  // --- Google TTS (trả về true/false) ---
  bool googleTTS(const String& textVi){
    DynamicJsonDocument req(1024);
    req["input"]["text"] = textVi;
    req["voice"]["languageCode"] = "vi-VN";
    req["voice"]["name"]         = "vi-VN-Standard-A";
    req["audioConfig"]["audioEncoding"]    = "LINEAR16";
    req["audioConfig"]["sampleRateHertz"]  = SPK_SAMPLE_RATE;

    String body; serializeJson(req, body);

    // Sử dụng http và client toàn cục (đã được khai báo)
    if(!http.begin(client, GOOGLE_TTS_URL)) { 
      Serial.println("[TTS] begin() fail"); 
      return false; 
    }

    http.setTimeout(20000); // 20 giây timeout
    http.addHeader("Content-Type","application/json");
    int code = http.POST(body);
    
    if(code != 200){
      Serial.printf("[TTS] HTTP code = %d\n", code);
      String resp = http.getString();
      Serial.println(resp);
      http.end();
      return false;
    }
    
    // Xử lý phản hồi thành công
    // Serial.printf("[TTS] HTTP code = %d\n", code); // Log rút gọn
    String resp = http.getString();
    http.end();

    // Parse JSON bằng tay (hiệu quả)
    int k = resp.indexOf("\"audioContent\"");
    if(k<0){ Serial.println("[TTS] không có audioContent"); return false; }
    int c  = resp.indexOf(':',k+14);
    int q1 = resp.indexOf('"',c+1);
    int q2 = resp.indexOf('"',q1+1);
    if(c<0||q1<0||q2<0){ Serial.println("[TTS] JSON parse lỗi"); return false; }

    String b64 = resp.substring(q1+1,q2);
    // Serial.printf("[TTS] base64 len = %u\n", (unsigned)b64.length()); // Log rút gọn

    // Cấp phát bộ nhớ cho ttsBuf
    size_t est = (b64.length()/4)*3 + 3; // Ước tính kích thước sau giải mã
    if(ttsBuf){ free(ttsBuf); ttsBuf=nullptr; }
    ttsBuf = (uint8_t*) ps_malloc(est); // Dùng PSRAM nếu có
    if(!ttsBuf) ttsBuf = (uint8_t*) malloc(est); // Dùng RAM thường nếu thất bại
    if(!ttsBuf){ Serial.println("[TTS] malloc fail"); return false; }

    // Giải mã Base64
    ttsLen = b64_decode(ttsBuf, b64.c_str(), b64.length());
    // Serial.printf("[TTS] decoded bytes = %u\n", (unsigned)ttsLen); // Log rút gọn
    return ttsLen > 0;
  }

  // =========================================================================
  // III. HÀM WIFI (Dùng chung)
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

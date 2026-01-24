/*
 * ESP32-S3 – Google TTS -> I2S Speaker (gõ gì phát nấy)
 * Loa I2S (I2S1/TX): BCLK=15, LRC=16, DIN=7  (mono 16-bit @ 16 kHz)
 * Serial: gõ bất kỳ câu tiếng Việt (có dấu) -> phát ra loa
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"

// ------- WiFi & Google API -------
const char* WIFI_SSID     = "P603";        // đổi theo bạn
const char* WIFI_PASSWORD = "0904362626";
const String GOOGLE_KEY   = "AIzaSyCgXdP5RP4irrSHW8YSD3UB9Pn9_3OAS_A"; // key của bạn

// ------- I2S speaker pins -------
#define I2S_SPK_PORT     I2S_NUM_1
#define I2S_SPK_BCLK_PIN 15
#define I2S_SPK_LRC_PIN  16
#define I2S_SPK_DIN_PIN   7

// ------- Audio -------
const int SAMPLE_RATE = 16000;
const size_t I2S_CHUNK = 2048;

// ------- Buffers -------
uint8_t* ttsBuf = nullptr;
size_t   ttsLen = 0;

// ------- Base64 decode tối giản -------
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

// ------- I2S speaker -------
void setupI2S_Speaker(){
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
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
  i2s_set_clk(I2S_SPK_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  i2s_zero_dma_buffer(I2S_SPK_PORT);
}

void playPCM(const uint8_t* d, size_t n){
  if(!d || !n) { Serial.println("[PLAY] buffer trống"); return; }
  i2s_start(I2S_SPK_PORT);
  size_t off=0;
  while(off<n){
    size_t chunk = (n-off < I2S_CHUNK) ? (n-off) : I2S_CHUNK;
    size_t wrote=0;
    i2s_write(I2S_SPK_PORT, d+off, chunk, &wrote, portMAX_DELAY);
    off += wrote;
    yield();
  }
  i2s_stop(I2S_SPK_PORT);
  i2s_zero_dma_buffer(I2S_SPK_PORT);
}

// ------- Google TTS (giữ nguyên tiếng Việt) -------
bool googleTTS(const String& textVi){
  DynamicJsonDocument req(1024);
  req["input"]["text"] = textVi;               // GIỮ NGUYÊN có dấu
  req["voice"]["languageCode"] = "vi-VN";
  req["voice"]["name"]         = "vi-VN-Standard-A";
  req["audioConfig"]["audioEncoding"]    = "LINEAR16";
  req["audioConfig"]["sampleRateHertz"]  = SAMPLE_RATE;

  String body; serializeJson(req, body);

  WiFiClientSecure cli; cli.setInsecure();
  HTTPClient http;
  String url = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + GOOGLE_KEY;
  if(!http.begin(cli, url)) { Serial.println("[TTS] begin() fail"); return false; }

  http.setTimeout(20000);
  http.addHeader("Content-Type","application/json");
  int code = http.POST(body);
  Serial.printf("[TTS] HTTP code = %d\n", code);
  String resp = http.getString(); // lấy để debug nếu lỗi
  if(code != 200){
    Serial.println(resp);
    http.end();
    return false;
  }
  http.end();

  int k = resp.indexOf("\"audioContent\"");
  if(k<0){ Serial.println("[TTS] không có audioContent"); return false; }
  int c  = resp.indexOf(':',k+14);
  int q1 = resp.indexOf('"',c+1);
  int q2 = resp.indexOf('"',q1+1);
  if(c<0||q1<0||q2<0){ Serial.println("[TTS] JSON parse lỗi"); return false; }

  String b64 = resp.substring(q1+1,q2);
  Serial.printf("[TTS] base64 len = %u\n", (unsigned)b64.length());

  size_t est = (b64.length()/4)*3 + 3;
  if(ttsBuf){ free(ttsBuf); ttsBuf=nullptr; }
  ttsBuf = (uint8_t*) ps_malloc(est);
  if(!ttsBuf) ttsBuf = (uint8_t*) malloc(est);
  if(!ttsBuf){ Serial.println("[TTS] malloc fail"); return false; }

  ttsLen = b64_decode(ttsBuf, b64.c_str(), b64.length());
  Serial.printf("[TTS] decoded bytes = %u\n", (unsigned)ttsLen);
  return ttsLen > 0;
}

// ------- Setup / Loop -------
void setup(){
  Serial.begin(115200);
  delay(800);
  Serial.println("\nESP32-S3 TTS gõ tiếng Việt bất kỳ để phát (ví dụ: xin chào Hà Nội)");

  setupI2S_Speaker();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int to=30; while(WiFi.status()!=WL_CONNECTED && to-->0){ delay(300); Serial.print("."); }
  Serial.printf("\nWiFi %s  IP: %s\n", WiFi.isConnected()?"OK":"FAIL",
                WiFi.localIP().toString().c_str());
}

void loop(){
  if(Serial.available()){
    String line = Serial.readStringUntil('\n');
    line.trim();
    if(line.length()==0) return;

    Serial.print("[CMD] TTS: "); Serial.println(line);
    if(googleTTS(line)){
      playPCM(ttsBuf, ttsLen);
    }else{
      Serial.println("[TTS] thất bại → xem HTTP code & log ở trên");
    }
  }
}

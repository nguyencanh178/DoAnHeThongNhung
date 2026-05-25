#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "mel_feature.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "driver/i2s.h"
#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#include "afe_helper.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "model_path.h"

#define I2S_BCLK 12
#define I2S_WS   11
#define I2S_SD   10

#define SAMPLE_RATE 16000
#define I2S_PORT I2S_NUM_0

#define AUDIO_LEN 32000

#define STAGE1_ARENA_SIZE (1200 * 1024)
#define STAGE2_ARENA_SIZE (1200 * 1024)

#define WAKE_LISTEN_MS 4000
#define WAKE_SKIP_MS 400

#define STAGE1_SCORE_THRESHOLD 0.60f
#define STAGE1_MARGIN_THRESHOLD 0.12f
#define STAGE2_SCORE_THRESHOLD 0.60f
#define STAGE2_MARGIN_THRESHOLD 0.12f

#define STATUS_LED_GPIO GPIO_NUM_13

extern const unsigned char stage1_model_tflite[];
extern const unsigned int stage1_model_tflite_len;
extern const unsigned char stage2_model_tflite[];
extern const unsigned int stage2_model_tflite_len;

typedef enum {
    CMD_WAKEUP = -1,
    CMD_STOP = 0,
    CMD_SWING,
    CMD_TURN_OFF,
    CMD_LEVEL_1,
    CMD_LEVEL_2,
    CMD_LEVEL_3,
    CMD_REJECT = 99
} command_id_t;

typedef struct {
    int cmd;
    float score;
} command_msg_t;

static const esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;
static srmodel_list_t *sr_models = NULL;

static int32_t *raw_buf = NULL;
static int16_t *feed_buf = NULL;

static int16_t *audio_buffer = NULL;
static int16_t *ai_audio = NULL;
static float *model_input = NULL;

static uint8_t *stage1_arena = NULL;
static uint8_t *stage2_arena = NULL;

static tflite::MicroInterpreter *stage1_interpreter = NULL;
static tflite::MicroInterpreter *stage2_interpreter = NULL;

static int audio_index = 0;
static int feed_chunksize = 0;
static int feed_channel = 0;

static SemaphoreHandle_t ai_sem = NULL;
static QueueHandle_t cmd_queue = NULL;

static volatile bool wake_active = false;
static volatile bool capturing_command = false;
static volatile bool ai_busy = false;

static TickType_t wake_until_tick = 0;
static TickType_t wake_skip_until_tick = 0;

static const char *stage1_labels[4] = {
    "stop",
    "swing",
    "turn_off",
    "turn_on"
};

static const char *stage2_labels[3] = {
    "level_one",
    "level_two",
    "level_three"
};

static void stop_here(const char *msg)
{
    printf("FATAL: %s\n", msg);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void print_heap(const char *tag)
{
    printf("[%s] internal=%lu psram=%lu\n",
           tag,
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void reset_command_capture_state()
{
    wake_active = false;
    capturing_command = false;
    audio_index = 0;
    gpio_set_level(STATUS_LED_GPIO, 0);
}

static void execute_command(int cmd, float score)
{
    switch (cmd) {
        case CMD_WAKEUP:
            printf("[EXEC] WAKE NET DETECTED\n");
            gpio_set_level(STATUS_LED_GPIO, 1);
        break;

        case CMD_STOP:
            printf("[EXEC] STOP | score=%.4f\n", score);
            gpio_set_level(STATUS_LED_GPIO, 0);
        break;

        case CMD_SWING:
            printf("[EXEC] SWING | score=%.4f\n", score);
            gpio_set_level(STATUS_LED_GPIO, 0);
        break;

        case CMD_TURN_OFF:
            printf("[EXEC] TURN OFF | score=%.4f\n", score);
            gpio_set_level(STATUS_LED_GPIO, 0);
        break;

        case CMD_LEVEL_1:
            printf("[EXEC] TURN ON LEVEL 1 | score=%.4f\n", score);
            gpio_set_level(STATUS_LED_GPIO, 0);
        break;

        case CMD_LEVEL_2:
            printf("[EXEC] TURN ON LEVEL 2 | score=%.4f\n", score);
            gpio_set_level(STATUS_LED_GPIO, 0);
        break;

        case CMD_LEVEL_3:
            printf("[EXEC] TURN ON LEVEL 3 | score=%.4f\n", score);
            gpio_set_level(STATUS_LED_GPIO, 0);
        break;

        default:
            printf("[REJECT] Command rejected | score=%.4f\n", score);
            gpio_set_level(STATUS_LED_GPIO, 0);
        break;
    }
}

static void control_task(void *arg)
{
    command_msg_t msg;
    while (1) {
        if (xQueueReceive(cmd_queue, &msg, portMAX_DELAY) == pdTRUE) {
            execute_command(msg.cmd, msg.score);
        }
    }
}

static void send_command(int cmd, float score)
{
    if (!cmd_queue) return;

    command_msg_t msg = {
        .cmd = cmd,
        .score = score
    };

    xQueueSend(cmd_queue, &msg, 0);
}

static void alloc_buffers()
{
    audio_buffer = (int16_t *)heap_caps_calloc(AUDIO_LEN, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ai_audio = (int16_t *)heap_caps_calloc(AUDIO_LEN, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    model_input = (float *)heap_caps_calloc(MODEL_INPUT_SIZE, sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    stage1_arena = (uint8_t *)heap_caps_aligned_alloc(16, STAGE1_ARENA_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    stage2_arena = (uint8_t *)heap_caps_aligned_alloc(16, STAGE2_ARENA_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!audio_buffer || !ai_audio || !model_input || !stage1_arena || !stage2_arena) {
        stop_here("PSRAM malloc failed");
    }

    memset(stage1_arena, 0, STAGE1_ARENA_SIZE);
    memset(stage2_arena, 0, STAGE2_ARENA_SIZE);

    printf("Buffers allocated\n");
    print_heap("after buffer alloc");
}

static void init_gpio()
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << STATUS_LED_GPIO;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(STATUS_LED_GPIO, 0);

    printf("GPIO initialized\n");
}

static void init_i2s_mic()
{
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pin_config));
    i2s_zero_dma_buffer(I2S_PORT);

    printf("I2S mic initialized\n");
}

static void init_sr_models()
{
    printf("Init ESP-SR models from partition: model\n");
    sr_models = esp_srmodel_init("model");
    if (!sr_models) stop_here("esp_srmodel_init failed");
    printf("ESP-SR models loaded OK\n");
}

static void init_afe()
{
    afe_handle = get_afe_handle();
    afe_data = create_afe_data();

    if (!afe_handle || !afe_data) stop_here("AFE create failed");

    feed_chunksize = afe_handle->get_feed_chunksize(afe_data);
    feed_channel = afe_handle->get_total_channel_num(afe_data);
    int fetch_chunksize = afe_handle->get_fetch_chunksize(afe_data);

    printf("AFE initialized\n");
    printf("feed_chunksize=%d\n", feed_chunksize);
    printf("feed_channel=%d\n", feed_channel);
    printf("fetch_chunksize=%d\n", fetch_chunksize);

    raw_buf = (int32_t *)heap_caps_malloc(feed_chunksize * feed_channel * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    feed_buf = (int16_t *)heap_caps_malloc(feed_chunksize * feed_channel * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (!raw_buf || !feed_buf) stop_here("AFE/I2S buffer malloc failed");

    print_heap("after afe init");
}

static tflite::MicroMutableOpResolver<80> resolver;

static void add_tflite_ops()
{
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddMaxPool2D();
    resolver.AddAveragePool2D();
    resolver.AddFullyConnected();
    resolver.AddReshape();
    resolver.AddSoftmax();
    resolver.AddRelu();
    resolver.AddRelu6();
    resolver.AddMean();
    resolver.AddMul();
    resolver.AddAdd();
    resolver.AddSub();
    resolver.AddPad();
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddExpandDims();
    resolver.AddSqueeze();
    resolver.AddTranspose();
    resolver.AddLogistic();
    resolver.AddShape();
    resolver.AddStridedSlice();
    resolver.AddPack();
}

static void check_tensor_shape(TfLiteTensor *input, const char *name)
{
    if (!input) stop_here("input tensor NULL");

    printf("[%s] input bytes=%lu type=%d dims=", name, (unsigned long)input->bytes, input->type);
    for (int i = 0; i < input->dims->size; i++) {
        printf("%d", input->dims->data[i]);
        if (i != input->dims->size - 1) printf("x");
    }
    printf("\n");

    if (input->type != kTfLiteFloat32) {
        stop_here("model input is not float32. Use float32 tflite first");
    }

    if (input->bytes != MODEL_INPUT_SIZE * sizeof(float)) {
        printf("MODEL_INPUT_SIZE=%d need_bytes=%lu\n", MODEL_INPUT_SIZE, (unsigned long)(MODEL_INPUT_SIZE * sizeof(float)));
        stop_here("input size mismatch. Check mel_feature.h and model input shape");
    }
}

static void init_tflite()
{
    tflite::InitializeTarget();
    add_tflite_ops();

    const tflite::Model *stage1_model = tflite::GetModel(stage1_model_tflite);
    const tflite::Model *stage2_model = tflite::GetModel(stage2_model_tflite);

    if (stage1_model->version() != TFLITE_SCHEMA_VERSION) stop_here("Stage1 schema mismatch");
    if (stage2_model->version() != TFLITE_SCHEMA_VERSION) stop_here("Stage2 schema mismatch");

    static tflite::MicroInterpreter static_stage1_interpreter(stage1_model, resolver, stage1_arena, STAGE1_ARENA_SIZE);
    static tflite::MicroInterpreter static_stage2_interpreter(stage2_model, resolver, stage2_arena, STAGE2_ARENA_SIZE);

    stage1_interpreter = &static_stage1_interpreter;
    stage2_interpreter = &static_stage2_interpreter;

    if (stage1_interpreter->AllocateTensors() != kTfLiteOk) stop_here("Stage1 AllocateTensors failed");
    if (stage2_interpreter->AllocateTensors() != kTfLiteOk) stop_here("Stage2 AllocateTensors failed");

    check_tensor_shape(stage1_interpreter->input(0), "stage1");
    check_tensor_shape(stage2_interpreter->input(0), "stage2");

    if (stage1_interpreter->output(0)->type != kTfLiteFloat32) stop_here("stage1 output is not float32");
    if (stage2_interpreter->output(0)->type != kTfLiteFloat32) stop_here("stage2 output is not float32");

    printf("TFLite 2-stage initialized\n");
    printf("stage1 output bytes=%lu\n", (unsigned long)stage1_interpreter->output(0)->bytes);
    printf("stage2 output bytes=%lu\n", (unsigned long)stage2_interpreter->output(0)->bytes);

    print_heap("after tflite init");
}

static void afe_feed_task(void *arg)
{
    size_t bytes_read = 0;
    int read_samples = feed_chunksize * feed_channel;

    while (1) {
        esp_err_t ret = i2s_read(I2S_PORT, raw_buf, read_samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK || bytes_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int sample_count = bytes_read / sizeof(int32_t);
        if (sample_count > read_samples) sample_count = read_samples;

        for (int i = 0; i < sample_count; i++) {
            int32_t s = raw_buf[i] >> 15;
            if (s > 20000) s = 20000;
            if (s < -20000) s = -20000;
            feed_buf[i] = (int16_t)s;
        }

        afe_handle->feed(afe_data, feed_buf);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void afe_fetch_task(void *arg)
{
    int capture_count = 0;

    while (1) {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);

        if (res) {
            TickType_t now = xTaskGetTickCount();

            if (res->wakeup_state == WAKENET_DETECTED) {
                wake_active = true;
                capturing_command = false;
                audio_index = 0;

                wake_until_tick = now + pdMS_TO_TICKS(WAKE_LISTEN_MS);
                wake_skip_until_tick = now + pdMS_TO_TICKS(WAKE_SKIP_MS);

                send_command(CMD_WAKEUP, 1.0f);

                printf("[WAKE] detected, skip %d ms then capture 2s command\n", WAKE_SKIP_MS);
            }

            if (wake_active && now > wake_until_tick) {
                reset_command_capture_state();
                printf("[WAKE] Timeout, back to wake mode\n");
            }

            if (wake_active && !ai_busy && res->data && res->data_size > 0) {
                int sample_num = res->data_size;

                if (!capturing_command && now >= wake_skip_until_tick) {
                    capturing_command = true;
                    audio_index = 0;
                    printf("[CMD] Start capture 2s after wake\n");
                }

                if (capturing_command) {
                    for (int i = 0; i < sample_num; i++) {
                        audio_buffer[audio_index++] = res->data[i];

                        if (audio_index >= AUDIO_LEN) {
                            audio_index = 0;
                            capture_count++;

                            memcpy(ai_audio, audio_buffer, AUDIO_LEN * sizeof(int16_t));

                            capturing_command = false;
                            wake_active = false;
                            ai_busy = true;

                            printf("Captured 2s audio #%d -> AI ready\n", capture_count);

                            if (ai_sem) xSemaphoreGive(ai_sem);
                            break;
                        }
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void normalize_feature(float *feat, int len)
{
    float mean = 0.0f;
    for (int i = 0; i < len; i++) mean += feat[i];
    mean /= len;

    float var = 0.0f;
    for (int i = 0; i < len; i++) {
        float d = feat[i] - mean;
        var += d * d;
    }
    var /= len;

    float stdv = sqrtf(var + 1e-6f);
    for (int i = 0; i < len; i++) feat[i] = (feat[i] - mean) / stdv;
}

static bool run_model(tflite::MicroInterpreter *interp,
                      const char **labels,
                      int class_count,
                      const char *tag,
                      int *best_out,
                      float *best_score_out,
                      float *margin_out)
{
    if (!interp || !best_out || !best_score_out || !margin_out) return false;

    TfLiteTensor *input = interp->input(0);
    TfLiteTensor *output = interp->output(0);

    if (!input || !output) {
        printf("[%s] Tensor NULL\n", tag);
        return false;
    }

    if (input->bytes != MODEL_INPUT_SIZE * sizeof(float)) {
        printf("[%s] Input bytes mismatch: %lu need=%lu\n",
               tag,
               (unsigned long)input->bytes,
               (unsigned long)(MODEL_INPUT_SIZE * sizeof(float)));
        return false;
    }

    if ((int)(output->bytes / sizeof(float)) < class_count) {
        printf("[%s] Output too small\n", tag);
        return false;
    }

    memcpy(input->data.f, model_input, MODEL_INPUT_SIZE * sizeof(float));

    if (interp->Invoke() != kTfLiteOk) {
        printf("[%s] Invoke failed\n", tag);
        return false;
    }

    int best = 0;
    int second = (class_count > 1) ? 1 : 0;
    float best_score = output->data.f[0];
    float second_score = output->data.f[second];

    if (class_count > 1 && second_score > best_score) {
        best = 1;
        second = 0;
        best_score = output->data.f[1];
        second_score = output->data.f[0];
    }

    for (int i = 2; i < class_count; i++) {
        float s = output->data.f[i];
        if (s > best_score) {
            second_score = best_score;
            second = best;
            best_score = s;
            best = i;
        } else if (s > second_score) {
            second_score = s;
            second = i;
        }
    }

    float margin = best_score - second_score;

    printf("[%s] Predict: %s | score=%.4f | second=%s %.4f | margin=%.4f\n",
           tag,
           labels[best], best_score,
           labels[second], second_score,
           margin);

    for (int i = 0; i < class_count; i++) {
        printf("[%s] %s: %.4f\n", tag, labels[i], output->data.f[i]);
    }

    *best_out = best;
    *best_score_out = best_score;
    *margin_out = margin;
    return true;
}

static void preprocess_audio()
{
    int64_t sum = 0;
    for (int i = 0; i < AUDIO_LEN; i++) sum += ai_audio[i];

    float dc = (float)sum / AUDIO_LEN;
    float rms = 0.0f;

    for (int i = 0; i < AUDIO_LEN; i++) {
        float x = (float)ai_audio[i] - dc;
        rms += x * x;
    }

    rms = sqrtf(rms / AUDIO_LEN);
    float target_rms = 3000.0f;
    float gain = target_rms / (rms + 1e-6f);

    if (gain > 4.0f) gain = 4.0f;
    if (gain < 0.05f) gain = 0.05f;

    for (int i = 0; i < AUDIO_LEN; i++) {
        float x = ((float)ai_audio[i] - dc) * gain;
        if (x > 32767.0f) x = 32767.0f;
        if (x < -32768.0f) x = -32768.0f;
        ai_audio[i] = (int16_t)x;
    }

    printf("DC removed=%.2f RMS=%.2f gain=%.2f\n", dc, rms, gain);

    audio_to_logmel(ai_audio, model_input);
    normalize_feature(model_input, MODEL_INPUT_SIZE);
}

static void ai_task(void *arg)
{
    while (1) {
        if (xSemaphoreTake(ai_sem, portMAX_DELAY) == pdTRUE) {
            ai_busy = true;
            printf("AI processing 2s audio with 2-stage model...\n");

            int16_t amin = ai_audio[0];
            int16_t amax = ai_audio[0];
            long long asum = 0;

            for (int i = 0; i < AUDIO_LEN; i++) {
                if (ai_audio[i] < amin) amin = ai_audio[i];
                if (ai_audio[i] > amax) amax = ai_audio[i];
                asum += ai_audio[i];
            }

            printf("AUDIO min=%d max=%d mean=%lld\n", amin, amax, asum / AUDIO_LEN);

            printf("Start logmel...\n");
            preprocess_audio();
            printf("Logmel done\n");

            float fmin = model_input[0];
            float fmax = model_input[0];
            float fsum = 0.0f;

            for (int i = 0; i < MODEL_INPUT_SIZE; i++) {
                if (model_input[i] < fmin) fmin = model_input[i];
                if (model_input[i] > fmax) fmax = model_input[i];
                fsum += model_input[i];
            }

            printf("FEAT min=%.5f max=%.5f mean=%.5f size=%d\n", fmin, fmax, fsum / MODEL_INPUT_SIZE, MODEL_INPUT_SIZE);

            int s1_best = 0;
            float s1_score = 0.0f;
            float s1_margin = 0.0f;

            if (!run_model(stage1_interpreter, stage1_labels, 4, "STAGE1", &s1_best, &s1_score, &s1_margin)) {
                send_command(CMD_REJECT, 0.0f);
                ai_busy = false;
                continue;
            }

            if (s1_score < STAGE1_SCORE_THRESHOLD || s1_margin < STAGE1_MARGIN_THRESHOLD) {
                printf("[AI] Stage1 rejected\n");
                send_command(CMD_REJECT, s1_score);
                ai_busy = false;
                continue;
            }

            if (s1_best == 0) {
                send_command(CMD_STOP, s1_score);
                printf("[AI] Accepted STOP\n");
            } else if (s1_best == 1) {
                send_command(CMD_SWING, s1_score);
                printf("[AI] Accepted SWING\n");
            } else if (s1_best == 2) {
                send_command(CMD_TURN_OFF, s1_score);
                printf("[AI] Accepted TURN_OFF\n");
            } else if (s1_best == 3) {
                int s2_best = 0;
                float s2_score = 0.0f;
                float s2_margin = 0.0f;

                if (!run_model(stage2_interpreter, stage2_labels, 3, "STAGE2", &s2_best, &s2_score, &s2_margin)) {
                    send_command(CMD_REJECT, 0.0f);
                    ai_busy = false;
                    continue;
                }

                if (s2_score < STAGE2_SCORE_THRESHOLD || s2_margin < STAGE2_MARGIN_THRESHOLD) {
                    printf("[AI] Stage2 rejected\n");
                    send_command(CMD_REJECT, s2_score);
                    ai_busy = false;
                    continue;
                }

                if (s2_best == 0) {
                    send_command(CMD_LEVEL_1, s2_score);
                    printf("[AI] Accepted TURN_ON_LEVEL_ONE\n");
                } else if (s2_best == 1) {
                    send_command(CMD_LEVEL_2, s2_score);
                    printf("[AI] Accepted TURN_ON_LEVEL_TWO\n");
                } else if (s2_best == 2) {
                    send_command(CMD_LEVEL_3, s2_score);
                    printf("[AI] Accepted TURN_ON_LEVEL_THREE\n");
                } else {
                    send_command(CMD_REJECT, s2_score);
                }
            } else {
                send_command(CMD_REJECT, s1_score);
            }

            print_heap("after ai");
            ai_busy = false;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

extern "C" void app_main(void)
{
    printf("I2S + AFE + WakeNet + TFLite 2-Stage Voice Command SAFE\n");
    print_heap("boot");

    esp_task_wdt_deinit();
    printf("Task WDT disabled for AI test\n");

    ai_sem = xSemaphoreCreateBinary();
    if (!ai_sem) stop_here("Semaphore create failed");

    cmd_queue = xQueueCreate(8, sizeof(command_msg_t));
    if (!cmd_queue) stop_here("Command queue create failed");

    init_gpio();
    alloc_buffers();
    init_i2s_mic();
    init_sr_models();
    init_afe();
    init_tflite();

    BaseType_t ok1 = xTaskCreatePinnedToCore(afe_feed_task, "afe_feed_task", 8192, NULL, 5, NULL, 0);
    BaseType_t ok2 = xTaskCreatePinnedToCore(afe_fetch_task, "afe_fetch_task", 8192, NULL, 5, NULL, 1);
    BaseType_t ok3 = xTaskCreatePinnedToCore(ai_task, "ai_task", 16384, NULL, 1, NULL, 1);
    BaseType_t ok4 = xTaskCreatePinnedToCore(control_task, "control_task", 4096, NULL, 3, NULL, 0);

    if (ok1 != pdPASS || ok2 != pdPASS || ok3 != pdPASS || ok4 != pdPASS) {
        printf("Task create failed: feed=%d fetch=%d ai=%d ctrl=%d\n", ok1, ok2, ok3, ok4);
        stop_here("Task create failed");
    }

    printf("All tasks started\n");
}
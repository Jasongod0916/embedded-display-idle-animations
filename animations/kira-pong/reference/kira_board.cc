#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "display/display.h"
#include "kira_audio_codec.h"
#include "kira_weather_icons.h"
#include "mcp_server.h"
#include "wifi_board.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_sh1107.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace {
constexpr gpio_num_t kDisplaySda = GPIO_NUM_5;
constexpr gpio_num_t kDisplayScl = GPIO_NUM_6;
constexpr int kDisplayWidth = 128;
constexpr int kDisplayHeight = 128;
constexpr uint32_t kScreenSaverDelayMs = 10000;
constexpr int kWeatherCardTimeoutMs = 60000;
constexpr uint64_t kFramePeriodUs = 16667;
constexpr int kDirtyRunMergeGap = 3;
constexpr char kDisplayTag[] = "KiraDisplay";
// AI Pong tuning: sizes and motion scale with the actual panel dimensions.
constexpr float kPaddleHeightRatio = 0.18f;
constexpr float kPaddleWidthRatio = 0.015f;
constexpr float kPaddleMarginRatio = 0.035f;
constexpr float kBallRadiusRatio = 0.017f;
constexpr float kInitialBallSpeedRatio = 0.45f;
constexpr float kBallSpeedIncrease = 1.035f;
constexpr float kBallMaxSpeedRatio = 1.10f;
constexpr float kAimErrorRangePixels = 2.5f;
constexpr float kLeftHomeMinRatio = 0.36f;
constexpr float kLeftHomeMaxRatio = 0.46f;
constexpr float kRightHomeMinRatio = 0.54f;
constexpr float kRightHomeMaxRatio = 0.64f;
constexpr float kLeftReactionMinSeconds = 0.00f;
constexpr float kLeftReactionMaxSeconds = 0.12f;
constexpr float kRightReactionMinSeconds = 0.08f;
constexpr float kRightReactionMaxSeconds = 0.28f;
constexpr float kMaxFrameDeltaSeconds = 0.10f;
constexpr float kMaxBounceAngleRadians = 0.80f;
constexpr uint8_t kSh1107Command = 0x00;
constexpr uint8_t kSh1107Ram = 0x40;

struct KiraWeatherCard {
    kira_weather::Condition condition = kira_weather::Condition::PartlyCloudy;
    int temperature_c = 0;
    int updated_hour = -1;
    int updated_minute = -1;
    int rain_start_hour = -1;
    int rain_end_hour = -1;
    int rain_probability = -1;
};

constexpr char kMinxiongWeatherUrl[] =
    "https://api.open-meteo.com/v1/forecast?latitude=23.5561&longitude=120.4305"
    "&current=temperature_2m%2Cweather_code%2Cis_day"
    "&hourly=precipitation_probability&forecast_days=1&timezone=Asia%2FTaipei";
constexpr size_t kMaxWeatherResponseBytes = 6144;
constexpr int kRainProbabilityThreshold = 30;
constexpr uint64_t kWeatherCacheDurationUs = 10ULL * 60 * 1000000;

struct WeatherHttpResponse {
    std::string body;
    bool too_large = false;
};

std::mutex g_weather_cache_mutex;
std::optional<KiraWeatherCard> g_cached_weather;
uint64_t g_weather_cache_time_us = 0;

esp_err_t WeatherHttpEvent(esp_http_client_event_t* event) {
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    auto* response = static_cast<WeatherHttpResponse*>(event->user_data);
    const size_t data_size = static_cast<size_t>(event->data_len);
    if (response->body.size() + data_size > kMaxWeatherResponseBytes) {
        response->too_large = true;
        return ESP_FAIL;
    }
    response->body.append(static_cast<const char*>(event->data), data_size);
    return ESP_OK;
}

bool ParseLocalHourMinute(const char* timestamp, int& hour, int& minute) {
    if (timestamp == nullptr) {
        return false;
    }
    const char* time = std::strchr(timestamp, 'T');
    if (time == nullptr || time[1] < '0' || time[1] > '9' || time[2] < '0' || time[2] > '9' ||
        time[3] != ':' || time[4] < '0' || time[4] > '9' || time[5] < '0' || time[5] > '9') {
        return false;
    }
    hour = (time[1] - '0') * 10 + (time[2] - '0');
    minute = (time[4] - '0') * 10 + (time[5] - '0');
    return hour < 24 && minute < 60;
}

void MapWeatherCode(int code, bool is_day, KiraWeatherCard& card) {
    if (code == 0) {
        card.condition =
            is_day ? kira_weather::Condition::Clear : kira_weather::Condition::NightClear;
    } else if (code == 1 || code == 2) {
        card.condition = kira_weather::Condition::PartlyCloudy;
    } else if (code == 3) {
        card.condition = kira_weather::Condition::Overcast;
    } else if (code == 45 || code == 48) {
        card.condition = kira_weather::Condition::Fog;
    } else if (code >= 51 && code <= 67) {
        card.condition =
            code >= 65 ? kira_weather::Condition::HeavyRain : kira_weather::Condition::LightRain;
    } else if (code >= 71 && code <= 77) {
        card.condition = kira_weather::Condition::Snow;
    } else if (code >= 80 && code <= 82) {
        card.condition =
            code == 82 ? kira_weather::Condition::HeavyRain : kira_weather::Condition::LightRain;
    } else if (code == 85 || code == 86) {
        card.condition = kira_weather::Condition::Snow;
    } else if (code >= 95 && code <= 99) {
        card.condition = kira_weather::Condition::Thunderstorm;
    } else {
        card.condition = kira_weather::Condition::PartlyCloudy;
    }
}

const char* WeatherConditionName(kira_weather::Condition condition) {
    switch (condition) {
        case kira_weather::Condition::Clear:
            return "晴朗";
        case kira_weather::Condition::PartlyCloudy:
            return "晴時多雲";
        case kira_weather::Condition::Cloudy:
            return "多雲";
        case kira_weather::Condition::Overcast:
            return "陰天";
        case kira_weather::Condition::LightRain:
            return "小雨";
        case kira_weather::Condition::HeavyRain:
            return "大雨";
        case kira_weather::Condition::Thunderstorm:
            return "雷雨";
        case kira_weather::Condition::Fog:
            return "有霧";
        case kira_weather::Condition::NightClear:
            return "晴朗";
        case kira_weather::Condition::Snow:
            return "下雪";
    }
    return "天氣";
}

bool FetchMinxiongWeather(KiraWeatherCard& card, std::string& error) {
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    {
        std::lock_guard<std::mutex> lock(g_weather_cache_mutex);
        if (g_cached_weather.has_value() && now_us >= g_weather_cache_time_us &&
            now_us - g_weather_cache_time_us < kWeatherCacheDurationUs) {
            card = *g_cached_weather;
            return true;
        }
    }

    WeatherHttpResponse response;
    const esp_http_client_config_t config = {
        .url = kMinxiongWeatherUrl,
        .user_agent = "Kira-Xiao-S3-Sense/1.0",
        .timeout_ms = 8000,
        .event_handler = WeatherHttpEvent,
        .buffer_size = 1024,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        error = "無法建立天氣連線";
        return false;
    }

    const esp_err_t request_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (request_result != ESP_OK || status_code != 200 || response.too_large) {
        ESP_LOGW(kDisplayTag, "Open-Meteo request failed: %s, HTTP %d",
                 esp_err_to_name(request_result), status_code);
        error = response.too_large ? "天氣資料超出限制" : "暫時無法取得民雄天氣";
        return false;
    }

    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLength(response.body.data(), response.body.size()), cJSON_Delete);
    const cJSON* current = root ? cJSON_GetObjectItemCaseSensitive(root.get(), "current") : nullptr;
    const cJSON* temperature =
        current ? cJSON_GetObjectItemCaseSensitive(current, "temperature_2m") : nullptr;
    const cJSON* weather_code =
        current ? cJSON_GetObjectItemCaseSensitive(current, "weather_code") : nullptr;
    const cJSON* is_day = current ? cJSON_GetObjectItemCaseSensitive(current, "is_day") : nullptr;
    const cJSON* local_time = current ? cJSON_GetObjectItemCaseSensitive(current, "time") : nullptr;
    if (!cJSON_IsNumber(temperature) || !cJSON_IsNumber(weather_code) || !cJSON_IsNumber(is_day) ||
        !cJSON_IsString(local_time)) {
        error = "天氣服務回傳的資料不完整";
        return false;
    }

    card.temperature_c = static_cast<int>(std::lround(temperature->valuedouble));
    MapWeatherCode(weather_code->valueint, is_day->valueint != 0, card);
    ParseLocalHourMinute(local_time->valuestring, card.updated_hour, card.updated_minute);

    const cJSON* hourly = cJSON_GetObjectItemCaseSensitive(root.get(), "hourly");
    const cJSON* times = hourly ? cJSON_GetObjectItemCaseSensitive(hourly, "time") : nullptr;
    const cJSON* probabilities =
        hourly ? cJSON_GetObjectItemCaseSensitive(hourly, "precipitation_probability") : nullptr;
    if (cJSON_IsArray(times) && cJSON_IsArray(probabilities)) {
        const int count = std::min(cJSON_GetArraySize(times), cJSON_GetArraySize(probabilities));
        for (int index = 0; index < count; ++index) {
            const cJSON* time = cJSON_GetArrayItem(times, index);
            const cJSON* probability = cJSON_GetArrayItem(probabilities, index);
            if (!cJSON_IsString(time) || !cJSON_IsNumber(probability) ||
                std::strcmp(time->valuestring, local_time->valuestring) <= 0 ||
                probability->valueint < kRainProbabilityThreshold) {
                continue;
            }
            if (ParseLocalHourMinute(time->valuestring, card.rain_start_hour, card.rain_end_hour)) {
                card.rain_end_hour = (card.rain_start_hour + 1) % 24;
                card.rain_probability = probability->valueint;
                break;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_weather_cache_mutex);
        g_cached_weather = card;
        g_weather_cache_time_us = static_cast<uint64_t>(esp_timer_get_time());
    }
    return true;
}

class KiraFaceDisplay : public Display {
public:
    explicit KiraFaceDisplay(esp_lcd_panel_io_handle_t panel_io) : panel_io_(panel_io) {
        width_ = kDisplayWidth;
        height_ = kDisplayHeight;
        idle_since_ms_ = NowMs();
    }

    void SetupUI() override {
        if (setup_ui_called_)
            return;
        Display::SetupUI();
        // Never write OLED frames from the main loop or shared esp_timer task.
        BaseType_t created = xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<KiraFaceDisplay*>(arg);
                while (true) {
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    self->Tick();
                }
            },
            "kira_face", 6144, this, 2, &display_task_);
        ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

        esp_timer_create_args_t frame_timer_args = {};
        frame_timer_args.callback = [](void* arg) {
            auto* self = static_cast<KiraFaceDisplay*>(arg);
            if (self->display_task_ != nullptr) {
                xTaskNotifyGive(self->display_task_);
            }
        };
        frame_timer_args.arg = this;
        frame_timer_args.dispatch_method = ESP_TIMER_TASK;
        frame_timer_args.name = "kira_frame";
        ESP_ERROR_CHECK(esp_timer_create(&frame_timer_args, &frame_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(frame_timer_, kFramePeriodUs));
    }

    void SetStatus(const char* status) override {
        if (status == nullptr)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
            state_ = State::Listening;
            standby_ = false;
            idle_since_ms_ = NowMs();
        } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
            state_ = State::Speaking;
            standby_ = false;
            idle_since_ms_ = NowMs();
        } else {
            state_ = State::Idle;
            standby_ = std::strcmp(status, Lang::Strings::STANDBY) == 0;
            idle_since_ms_ = NowMs();
        }
    }

    void SetEmotion(const char* emotion) override {
        std::lock_guard<std::mutex> lock(mutex_);
        emotion_ = emotion == nullptr ? "neutral" : emotion;
    }

    void SetWeatherCard(const KiraWeatherCard& card) {
        std::lock_guard<std::mutex> lock(mutex_);
        weather_card_ = card;
        weather_card_active_ = true;
        weather_card_shown_ms_ = NowMs();
        idle_since_ms_ = weather_card_shown_ms_;
        standby_ = false;
    }

    void ClearWeatherCard() {
        std::lock_guard<std::mutex> lock(mutex_);
        weather_card_active_ = false;
    }

    void ShowNotification(const char* notification, int duration_ms = 3000) override {
        (void)notification;
        (void)duration_ms;
    }

    bool IsMonochrome() const override { return true; }
    void SetChatMessage(const char* role, const char* content) override {
        if (role != nullptr && content != nullptr && std::strcmp(role, "user") == 0 &&
            content[0] != '\0') {
            ClearWeatherCard();
        }
    }

private:
    enum class State { Idle, Listening, Speaking };
    esp_lcd_panel_io_handle_t panel_io_;
    TaskHandle_t display_task_ = nullptr;
    esp_timer_handle_t frame_timer_ = nullptr;
    std::array<uint8_t, kDisplayWidth * kDisplayHeight / 8> framebuffer_{};
    std::array<uint8_t, kDisplayWidth * kDisplayHeight / 8> displayed_framebuffer_{};
    std::mutex mutex_;
    State state_ = State::Idle;
    bool standby_ = false;
    bool weather_card_active_ = false;
    uint32_t weather_card_shown_ms_ = 0;
    KiraWeatherCard weather_card_;
    std::string emotion_ = "neutral";
    uint32_t tick_ = 0;
    uint32_t idle_since_ms_ = 0;
    struct Paddle {
        float y = kDisplayHeight / 2.0f;
        float velocity = 0.0f;
    };
    struct AiPersonality {
        float reaction_delay_seconds = 0.0f;
        float aim_error_pixels = 0.0f;
        float home_position_ratio = 0.5f;
        float dead_zone_ratio = 0.1f;
        float max_speed_ratio = 0.8f;
        float acceleration_ratio = 3.5f;
        float position_gain = 12.0f;
        float velocity_damping = 5.5f;
    };
    struct PongGameState {
        float ball_x = kDisplayWidth / 2.0f;
        float ball_y = kDisplayHeight / 2.0f;
        float ball_vx = 0.0f;
        float ball_vy = 0.0f;
        float ball_speed = 0.0f;
        Paddle left_paddle;
        Paddle right_paddle;
    } pong_;
    AiPersonality left_ai_;
    AiPersonality right_ai_;
    bool pong_active_ = false;
    uint64_t last_tick_us_ = 0;
    uint64_t fps_window_start_us_ = 0;
    uint32_t fps_window_frames_ = 0;

    static uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

    void SetPixel(int x, int y) {
        if (x < 0 || x >= kDisplayWidth || y < 0 || y >= kDisplayHeight)
            return;
        framebuffer_[(y >> 3) * kDisplayWidth + x] |= static_cast<uint8_t>(1U << (y & 7));
    }

    void FillRoundedRect(int x, int y, int width, int height) {
        // A two-pixel corner radius on the 1-bit panel: omit only the four
        // corner pixels, preserving the small mouth and blink silhouettes.
        for (int py = y; py < y + height; ++py) {
            for (int px = x; px < x + width; ++px) {
                const bool edge_row = py == y || py == y + height - 1;
                const bool edge_column = px == x || px == x + width - 1;
                if (edge_row && edge_column)
                    continue;
                SetPixel(px, py);
            }
        }
    }

    void RenderFace(uint32_t now) {
        framebuffer_.fill(0);
        const bool audible = static_cast<int32_t>(kira_voice_until_ms.load() - now) > 0;
        const State face_state =
            audible ? State::Speaking : (state_ == State::Speaking ? State::Idle : state_);

        int eye_width = 17;
        int eye_height = 28;
        int eye_spacing = 47;
        int mouth_width = 20;
        int mouth_height = 6;
        if (face_state == State::Listening) {
            eye_width = 16;
            eye_spacing = 18;
            mouth_width = 14;
            mouth_height = 5;
        } else if (face_state == State::Speaking) {
            mouth_width = 18;
            mouth_height = kira_voice_level.load() > 600 ? 10 : 7;
        }
        // Listening uses the fixed face geometry, regardless of cloud emotion.
        if (face_state != State::Listening) {
            if (emotion_ == "happy" || emotion_ == "excited")
                mouth_width = 28;
            if (emotion_ == "sad" || emotion_ == "sleepy")
                eye_height = 22;
            if (emotion_ == "surprised" || emotion_ == "amazed")
                mouth_height = 10;
        }

        const int eye_center_y = kDisplayHeight / 2 - 6;
        const int mouth_center_y = kDisplayHeight / 2 + 23;
        const bool blink = face_state == State::Idle && (now % 6000 < 200);
        const int rendered_eye_height = blink ? 4 : eye_height;
        const int rendered_eye_y = blink ? eye_center_y - 2 : eye_center_y - eye_height / 2;
        const int listening_shift_x = face_state == State::Listening ? -3 : 0;
        const int left_eye_center_x = kDisplayWidth / 2 - eye_spacing / 2 + listening_shift_x;
        const int right_eye_center_x = kDisplayWidth / 2 + eye_spacing / 2 + listening_shift_x;
        const int left_eye_y = rendered_eye_y + (face_state == State::Listening ? 2 : 0);

        const int left_eye_height = rendered_eye_height - (face_state == State::Listening ? 2 : 0);
        FillRoundedRect(left_eye_center_x - eye_width / 2, left_eye_y, eye_width, left_eye_height);
        FillRoundedRect(right_eye_center_x - eye_width / 2, rendered_eye_y, eye_width,
                        rendered_eye_height);
        FillRoundedRect((kDisplayWidth - mouth_width) / 2, mouth_center_y - mouth_height / 2,
                        mouth_width, mouth_height);
    }

    void DrawRect(int x, int y, int width, int height) {
        for (int py = y; py < y + height; ++py) {
            for (int px = x; px < x + width; ++px) {
                SetPixel(px, py);
            }
        }
    }

    void DrawBitmap(const uint8_t* bitmap, int width, int height, int x, int y, int draw_width,
                    int draw_height) {
        const int bytes_per_row = (width + 7) / 8;
        for (int dy = 0; dy < draw_height; ++dy) {
            const int source_y = dy * height / draw_height;
            for (int dx = 0; dx < draw_width; ++dx) {
                const int source_x = dx * width / draw_width;
                const size_t index = source_y * bytes_per_row + source_x / 8;
                if ((bitmap[index] & (0x80U >> (source_x & 7))) != 0) {
                    SetPixel(x + dx, y + dy);
                }
            }
        }
    }

    static const uint8_t* TinyGlyph(char value) {
        static constexpr uint8_t glyphs[][7] = {
            {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
            {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
            {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
            {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},  // 3
            {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
            {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},  // 5
            {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
            {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
            {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
            {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},  // 9
            {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00},  // :
            {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},  // -
            {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13},  // %
            {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},  // C
        };
        if (value >= '0' && value <= '9')
            return glyphs[value - '0'];
        if (value == ':')
            return glyphs[10];
        if (value == '-')
            return glyphs[11];
        if (value == '%')
            return glyphs[12];
        if (value == 'C')
            return glyphs[13];
        return nullptr;
    }

    void DrawTinyText(const char* text, int x, int y, int scale = 1) {
        if (text == nullptr)
            return;
        for (const char* character = text; *character != '\0'; ++character) {
            const uint8_t* glyph = TinyGlyph(*character);
            if (glyph != nullptr) {
                for (int row = 0; row < 7; ++row) {
                    for (int column = 0; column < 5; ++column) {
                        if ((glyph[row] & (0x10U >> column)) == 0)
                            continue;
                        DrawRect(x + column * scale, y + row * scale, scale, scale);
                    }
                }
            }
            x += 6 * scale;
        }
    }

    void DrawCircleOutline(int center_x, int center_y, int radius) {
        int x = radius;
        int y = 0;
        int error = 1 - radius;
        while (x >= y) {
            SetPixel(center_x + x, center_y + y);
            SetPixel(center_x + y, center_y + x);
            SetPixel(center_x - y, center_y + x);
            SetPixel(center_x - x, center_y + y);
            SetPixel(center_x - x, center_y - y);
            SetPixel(center_x - y, center_y - x);
            SetPixel(center_x + y, center_y - x);
            SetPixel(center_x + x, center_y - y);
            ++y;
            if (error < 0) {
                error += 2 * y + 1;
            } else {
                --x;
                error += 2 * (y - x + 1);
            }
        }
    }

    void RenderWeatherCard() {
        framebuffer_.fill(0);
        DrawBitmap(kira_weather::kMinxiongLabel, 32, 16, 4, 3, 32, 16);
        // Keep the update timestamp secondary to the location label.
        DrawBitmap(kira_weather::kUpdatedLabel, 32, 16, 63, 4, 28, 14);

        char updated_at[6] = "--:--";
        if (weather_card_.updated_hour >= 0 && weather_card_.updated_minute >= 0) {
            const unsigned hour =
                static_cast<unsigned>(std::clamp(weather_card_.updated_hour, 0, 23));
            const unsigned minute =
                static_cast<unsigned>(std::clamp(weather_card_.updated_minute, 0, 59));
            updated_at[0] = static_cast<char>('0' + hour / 10);
            updated_at[1] = static_cast<char>('0' + hour % 10);
            updated_at[2] = ':';
            updated_at[3] = static_cast<char>('0' + minute / 10);
            updated_at[4] = static_cast<char>('0' + minute % 10);
        }
        DrawTinyText(updated_at, kDisplayWidth - 4 - 29, 7);

        const size_t condition = static_cast<size_t>(weather_card_.condition);
        if (condition < 10) {
            DrawBitmap(kira_weather::kIcons[condition], kira_weather::kWeatherIconSize,
                       kira_weather::kWeatherIconSize, 44, 20, 40, 40);
        }

        char temperature[5];
        std::snprintf(temperature, sizeof(temperature), "%d", weather_card_.temperature_c);
        const int number_width = static_cast<int>(std::strlen(temperature)) * 12 - 2;
        const int unit_width = number_width + 15;
        const int temperature_x = (kDisplayWidth - unit_width) / 2;
        DrawTinyText(temperature, temperature_x, 68, 2);
        const int degree_x = temperature_x + number_width + 4;
        DrawCircleOutline(degree_x, 71, 2);
        DrawTinyText("C", degree_x + 5, 74);

        if (weather_card_.rain_probability >= 0) {
            DrawBitmap(
                kira_weather::kIcons[static_cast<size_t>(kira_weather::Condition::LightRain)],
                kira_weather::kWeatherIconSize, kira_weather::kWeatherIconSize, 7, 100, 18, 18);
            if (weather_card_.rain_start_hour >= 0 && weather_card_.rain_end_hour >= 0) {
                const unsigned start_hour =
                    static_cast<unsigned>(std::clamp(weather_card_.rain_start_hour, 0, 23));
                const unsigned end_hour =
                    static_cast<unsigned>(std::clamp(weather_card_.rain_end_hour, 0, 23));
                char rain_period[6] = {
                    static_cast<char>('0' + start_hour / 10),
                    static_cast<char>('0' + start_hour % 10),
                    '-',
                    static_cast<char>('0' + end_hour / 10),
                    static_cast<char>('0' + end_hour % 10),
                    '\0',
                };
                DrawTinyText(rain_period, 32, 106);
            }
            char probability[5];
            std::snprintf(probability, sizeof(probability), "%d%%", weather_card_.rain_probability);
            const int probability_width = static_cast<int>(std::strlen(probability)) * 6 - 1;
            DrawTinyText(probability, kDisplayWidth - 6 - probability_width, 106);
        }
    }

    static float PaddleWidth() {
        return std::max(1.0f, std::round(kDisplayWidth * kPaddleWidthRatio));
    }

    static float PaddleHeight() {
        return std::max(1.0f, std::round(kDisplayHeight * kPaddleHeightRatio));
    }

    static float PaddleMargin() { return std::round(kDisplayWidth * kPaddleMarginRatio); }

    static float BallRadius() {
        return std::max(1.0f, std::round(kDisplayHeight * kBallRadiusRatio));
    }

    static float Clamp(float value, float minimum, float maximum) {
        return std::max(minimum, std::min(value, maximum));
    }

    static float RandomRange(float minimum, float maximum) {
        const float unit = static_cast<float>(esp_random() & 0x00FFFFFFU) / 16777215.0f;
        return minimum + (maximum - minimum) * unit;
    }

    AiPersonality SampleAiPersonality(bool left_side) {
        AiPersonality personality;
        personality.reaction_delay_seconds =
            left_side ? RandomRange(kLeftReactionMinSeconds, kLeftReactionMaxSeconds)
                      : RandomRange(kRightReactionMinSeconds, kRightReactionMaxSeconds);
        personality.aim_error_pixels = RandomRange(-kAimErrorRangePixels, kAimErrorRangePixels);
        personality.home_position_ratio = left_side
                                              ? RandomRange(kLeftHomeMinRatio, kLeftHomeMaxRatio)
                                              : RandomRange(kRightHomeMinRatio, kRightHomeMaxRatio);
        personality.dead_zone_ratio =
            left_side ? RandomRange(0.08f, 0.13f) : RandomRange(0.11f, 0.17f);
        personality.max_speed_ratio =
            left_side ? RandomRange(0.74f, 0.86f) : RandomRange(0.68f, 0.80f);
        personality.acceleration_ratio =
            left_side ? RandomRange(3.2f, 3.9f) : RandomRange(2.7f, 3.3f);
        personality.position_gain =
            left_side ? RandomRange(10.5f, 13.0f) : RandomRange(9.0f, 11.0f);
        personality.velocity_damping =
            left_side ? RandomRange(4.8f, 6.2f) : RandomRange(4.1f, 5.4f);
        return personality;
    }

    // game state / serve
    void ServeBall() {
        pong_.ball_x = kDisplayWidth / 2.0f;
        pong_.ball_y = kDisplayHeight / 2.0f;
        pong_.ball_speed = kDisplayWidth * kInitialBallSpeedRatio;
        const float direction = (esp_random() & 1U) ? 1.0f : -1.0f;
        const float angle = (static_cast<int>(esp_random() % 61U) - 30) * 0.01f;
        pong_.ball_vx = direction * pong_.ball_speed * std::cos(angle);
        pong_.ball_vy = pong_.ball_speed * std::sin(angle);
    }

    void ResetPong() {
        pong_.left_paddle = {kDisplayHeight / 2.0f, 0.0f};
        pong_.right_paddle = {kDisplayHeight / 2.0f, 0.0f};
        left_ai_ = SampleAiPersonality(true);
        right_ai_ = SampleAiPersonality(false);
        ServeBall();
    }

    // AI prediction: project the ball over a short horizon and reflect its
    // projected Y coordinate at the top and bottom walls.
    float PredictBallY(float horizon_seconds) const {
        const float radius = BallRadius();
        const float minimum = radius;
        const float span = kDisplayHeight - 2.0f * radius;
        const float period = 2.0f * span;
        float projected =
            std::fmod(pong_.ball_y + pong_.ball_vy * horizon_seconds - minimum, period);
        if (projected < 0.0f)
            projected += period;
        if (projected > span)
            projected = period - projected;
        return minimum + projected;
    }

    float SecondsUntilPaddle(bool left_side) const {
        const float contact_x = left_side
                                    ? PaddleMargin() + PaddleWidth() + BallRadius()
                                    : kDisplayWidth - PaddleMargin() - PaddleWidth() - BallRadius();
        return Clamp((contact_x - pong_.ball_x) / pong_.ball_vx, 0.0f, 2.5f);
    }

    // The off-ball paddle returns to its own home lane so both paddles do not
    // mirror the ball's motion. Each side has a separate interception policy.
    float PredictLeftTargetY() const {
        if (pong_.ball_vx >= 0.0f) {
            return kDisplayHeight * left_ai_.home_position_ratio;
        }
        return Clamp(PredictBallY(std::max(
                         0.0f, SecondsUntilPaddle(true) - left_ai_.reaction_delay_seconds)) +
                         left_ai_.aim_error_pixels,
                     BallRadius(), kDisplayHeight - BallRadius());
    }

    float PredictRightTargetY() const {
        if (pong_.ball_vx <= 0.0f) {
            return kDisplayHeight * right_ai_.home_position_ratio;
        }
        const float reaction_adjusted_time =
            std::max(0.0f, SecondsUntilPaddle(false) - right_ai_.reaction_delay_seconds);
        return Clamp(PredictBallY(reaction_adjusted_time) + right_ai_.aim_error_pixels,
                     BallRadius(), kDisplayHeight - BallRadius());
    }

    // AI update / damped paddle physics with separate response strengths.
    void UpdatePaddleAI(Paddle& paddle, float dt_seconds, bool left_side) {
        const AiPersonality& personality = left_side ? left_ai_ : right_ai_;
        const float target_y = left_side ? PredictLeftTargetY() : PredictRightTargetY();
        const float error = target_y - paddle.y;
        const float dead_zone = PaddleHeight() * personality.dead_zone_ratio;
        const float max_speed = kDisplayHeight * personality.max_speed_ratio;
        const float max_acceleration = kDisplayHeight * personality.acceleration_ratio;
        const float acceleration = std::abs(error) <= dead_zone &&
                                           std::abs(paddle.velocity) < max_acceleration * dt_seconds
                                       ? 0.0f
                                       : Clamp(personality.position_gain * error -
                                                   personality.velocity_damping * paddle.velocity,
                                               -max_acceleration, max_acceleration);
        paddle.velocity = Clamp(paddle.velocity + acceleration * dt_seconds, -max_speed, max_speed);
        if (std::abs(error) <= dead_zone && acceleration == 0.0f) {
            paddle.velocity = 0.0f;
        }
        paddle.y += paddle.velocity * dt_seconds;

        const float half_height = PaddleHeight() / 2.0f;
        if (paddle.y < half_height) {
            paddle.y = half_height;
            paddle.velocity = 0.0f;
        } else if (paddle.y > kDisplayHeight - half_height) {
            paddle.y = kDisplayHeight - half_height;
            paddle.velocity = 0.0f;
        }
    }

    // collision detection / impact-angle physics
    void BounceFromPaddle(const Paddle& paddle, bool left_side, float paddle_x) {
        const float maximum_offset = PaddleHeight() / 2.0f + BallRadius();
        const float offset = Clamp((pong_.ball_y - paddle.y) / maximum_offset, -1.0f, 1.0f);
        pong_.ball_speed =
            std::min(pong_.ball_speed * kBallSpeedIncrease, kDisplayWidth * kBallMaxSpeedRatio);
        const float angle = offset * kMaxBounceAngleRadians;
        pong_.ball_vx = (left_side ? 1.0f : -1.0f) * pong_.ball_speed * std::cos(angle);
        pong_.ball_vy = pong_.ball_speed * std::sin(angle);
        const float paddle_right = paddle_x + PaddleWidth();
        pong_.ball_x = left_side ? paddle_right + BallRadius() : paddle_x - BallRadius();
    }

    void UpdateBall(float dt_seconds, float previous_x) {
        const float radius = BallRadius();
        const float paddle_height = PaddleHeight();
        const float left_x = PaddleMargin();
        const float right_x = kDisplayWidth - PaddleMargin() - PaddleWidth();

        pong_.ball_x += pong_.ball_vx * dt_seconds;
        pong_.ball_y += pong_.ball_vy * dt_seconds;

        if (pong_.ball_y < radius) {
            pong_.ball_y = radius;
            pong_.ball_vy = std::abs(pong_.ball_vy);
        } else if (pong_.ball_y > kDisplayHeight - radius) {
            pong_.ball_y = kDisplayHeight - radius;
            pong_.ball_vy = -std::abs(pong_.ball_vy);
        }

        const float ball_top = pong_.ball_y - radius;
        const float ball_bottom = pong_.ball_y + radius;
        if (pong_.ball_vx < 0.0f && previous_x - radius > left_x + PaddleWidth() &&
            pong_.ball_x - radius <= left_x + PaddleWidth()) {
            const float paddle_top = pong_.left_paddle.y - paddle_height / 2.0f;
            const float paddle_bottom = pong_.left_paddle.y + paddle_height / 2.0f;
            if (ball_bottom >= paddle_top && ball_top <= paddle_bottom) {
                BounceFromPaddle(pong_.left_paddle, true, left_x);
            }
        } else if (pong_.ball_vx > 0.0f && previous_x + radius < right_x &&
                   pong_.ball_x + radius >= right_x) {
            const float paddle_top = pong_.right_paddle.y - paddle_height / 2.0f;
            const float paddle_bottom = pong_.right_paddle.y + paddle_height / 2.0f;
            if (ball_bottom >= paddle_top && ball_top <= paddle_bottom) {
                BounceFromPaddle(pong_.right_paddle, false, right_x);
            }
        }

        if (pong_.ball_x + radius < 0.0f || pong_.ball_x - radius > kDisplayWidth) {
            ServeBall();
        }
    }

    void UpdatePong(float dt_seconds) {
        UpdatePaddleAI(pong_.left_paddle, dt_seconds, true);
        UpdatePaddleAI(pong_.right_paddle, dt_seconds, false);
        const float previous_x = pong_.ball_x;
        UpdateBall(dt_seconds, previous_x);
    }

    // rendering
    void DrawBall() {
        const int center_x = static_cast<int>(std::lround(pong_.ball_x));
        const int center_y = static_cast<int>(std::lround(pong_.ball_y));
        const int radius = static_cast<int>(BallRadius());
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x * x + y * y <= radius * radius) {
                    SetPixel(center_x + x, center_y + y);
                }
            }
        }
    }

    void RenderPong() {
        framebuffer_.fill(0);
        const int center_x = kDisplayWidth / 2;
        for (int y = 5; y < kDisplayHeight; y += 10) {
            DrawRect(center_x, y, 1, 4);
        }

        const int paddle_width = static_cast<int>(PaddleWidth());
        const int paddle_height = static_cast<int>(PaddleHeight());
        const int margin = static_cast<int>(PaddleMargin());
        const int left_y =
            static_cast<int>(std::lround(pong_.left_paddle.y - paddle_height / 2.0f));
        const int right_y =
            static_cast<int>(std::lround(pong_.right_paddle.y - paddle_height / 2.0f));
        DrawRect(margin, left_y, paddle_width, paddle_height);
        DrawRect(kDisplayWidth - margin - paddle_width, right_y, paddle_width, paddle_height);
        DrawBall();
    }

    void FlushFrame() {
        auto send_run = [this](int page, int first_column, int last_column) {
            const uint8_t column_high = static_cast<uint8_t>(0x10 | (first_column >> 4));
            const uint8_t column_low = static_cast<uint8_t>(first_column & 0x0F);
            const uint8_t page_command = static_cast<uint8_t>(0xB0 | page);
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io_, kSh1107Command, &column_high, 1));
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io_, kSh1107Command, &column_low, 1));
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io_, kSh1107Command, &page_command, 1));
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(
                panel_io_, kSh1107Ram, framebuffer_.data() + page * kDisplayWidth + first_column,
                last_column - first_column + 1));
        };

        if (!frame_drawn_) {
            for (int page = 0; page < kDisplayHeight / 8; ++page) {
                send_run(page, 0, kDisplayWidth - 1);
            }
        } else {
            // SH1107 RAM is arranged as sixteen 8-pixel pages. Send only short
            // changed column runs so the moving paddles and ball do not trigger
            // a 2048-byte full-frame I2C transfer on every animation frame.
            for (int page = 0; page < kDisplayHeight / 8; ++page) {
                const int page_offset = page * kDisplayWidth;
                int column = 0;
                while (column < kDisplayWidth) {
                    while (column < kDisplayWidth &&
                           framebuffer_[page_offset + column] ==
                               displayed_framebuffer_[page_offset + column]) {
                        ++column;
                    }
                    if (column == kDisplayWidth)
                        break;

                    const int first_column = column;
                    int last_changed_column = column;
                    ++column;
                    while (column < kDisplayWidth) {
                        if (framebuffer_[page_offset + column] !=
                            displayed_framebuffer_[page_offset + column]) {
                            last_changed_column = column;
                            ++column;
                        } else if (column - last_changed_column <= kDirtyRunMergeGap) {
                            ++column;
                        } else {
                            break;
                        }
                    }
                    send_run(page, first_column, last_changed_column);
                }
            }
        }
        displayed_framebuffer_ = framebuffer_;
    }

    void Tick() {
        bool rendered_pong = false;
        uint64_t now_us = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            now_us = static_cast<uint64_t>(esp_timer_get_time());
            const uint32_t now = static_cast<uint32_t>(now_us / 1000);
            const float dt_seconds =
                last_tick_us_ == 0
                    ? static_cast<float>(kFramePeriodUs) / 1000000.0f
                    : Clamp((now_us - last_tick_us_) / 1000000.0f, 0.0f, kMaxFrameDeltaSeconds);
            last_tick_us_ = now_us;
            const bool audible = static_cast<int32_t>(kira_voice_until_ms.load() - now) > 0;
            if (audible && state_ == State::Idle) {
                // Start the idle countdown after the last transmitted TTS frame,
                // even if the application changed state to idle just beforehand.
                idle_since_ms_ = now;
            }
            if (weather_card_active_ &&
                static_cast<uint32_t>(now - weather_card_shown_ms_) >= kWeatherCardTimeoutMs) {
                weather_card_active_ = false;
            }
            if (standby_ && state_ == State::Idle && !audible &&
                static_cast<uint32_t>(now - idle_since_ms_) >= kScreenSaverDelayMs) {
                if (!pong_active_) {
                    ResetPong();
                    pong_active_ = true;
                }
                UpdatePong(dt_seconds);
                RenderPong();
                rendered_pong = true;
            } else if (weather_card_active_) {
                pong_active_ = false;
                RenderWeatherCard();
            } else {
                pong_active_ = false;
                RenderFace(now);
            }
        }

        if (!frame_drawn_ || displayed_framebuffer_ != framebuffer_) {
            const uint64_t flush_start_us = static_cast<uint64_t>(esp_timer_get_time());
            FlushFrame();
            last_flush_duration_us_ = static_cast<uint32_t>(esp_timer_get_time() - flush_start_us);
            frame_drawn_ = true;
        } else {
            last_flush_duration_us_ = 0;
        }

        if (rendered_pong) {
            if (fps_window_start_us_ == 0)
                fps_window_start_us_ = now_us;
            ++fps_window_frames_;
            const uint64_t elapsed_us = now_us - fps_window_start_us_;
            if (elapsed_us >= 1000000) {
                const float fps = static_cast<float>(fps_window_frames_) * 1000000.0f / elapsed_us;
                ESP_LOGI(kDisplayTag, "Pong %.1f FPS, last flush %.2f ms", fps,
                         last_flush_duration_us_ / 1000.0f);
                fps_window_start_us_ = now_us;
                fps_window_frames_ = 0;
            }
        } else {
            fps_window_start_us_ = 0;
            fps_window_frames_ = 0;
        }
    }

    bool frame_drawn_ = false;
    uint32_t last_flush_duration_us_ = 0;

    bool Lock(int timeout_ms = 0) override {
        (void)timeout_ms;
        mutex_.lock();
        return true;
    }

    void Unlock() override { mutex_.unlock(); }
};
}  // namespace

class KiraXiaoS3Sense : public WifiBoard {
public:
    KiraXiaoS3Sense() {
        const i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = kDisplaySda,
            .scl_io_num = kDisplayScl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {.enable_internal_pullup = true},
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));

        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = ESP_LCD_IO_I2C_SH1107_ADDRESS;
        io_config.scl_speed_hz = 400000;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 0;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.flags.disable_control_phase = 1;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        // HiLetgo 1.5-inch 128x128 uses zero display offset. The component's
        // 0x60 default shifts the visible origin (esp-bsp issue #583).
        esp_lcd_panel_sh1107_config_t sh1107_config = {.contrast = 128, .offset = 0x00};
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;
        panel_config.vendor_config = &sh1107_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1107(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        // The face framebuffer uses set bits for lit pixels; A6 gives the OLED
        // the required bright-face-on-dark-background polarity.
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new KiraFaceDisplay(panel_io_);
        InitializeTools();
        button_.OnClick([]() { Application::GetInstance().ToggleChatState(); });
    }

    AudioCodec* GetAudioCodec() override {
        // Kira wiring: GPIO7 -> BCLK, GPIO4 -> LRC/WS, GPIO2 -> DIN.
        static KiraAudioCodec codec;
        return &codec;
    }

    Display* GetDisplay() override { return display_; }
    Led* GetLed() override {
        static NoLed no_led;
        return &no_led;
    }

private:
    void InitializeTools() {
        auto* weather_display = display_;
        McpServer::GetInstance().AddTool(
            "self.conversation.finish_after_reply",
            "Call when the user's intent is to end this conversation and return the device to "
            "standby. This does not power off the device. After this tool succeeds, give one "
            "brief farewell; the device will leave listening mode after the spoken reply finishes "
            "and will remain ready for its wake word.",
            PropertyList(), [](const PropertyList&) -> ToolResult {
                Application::GetInstance().EndConversationAfterReply();
                return std::string("Say one brief farewell; then return to standby after playback.");
            });

        McpServer::GetInstance().AddTool(
            "self.weather.get_current",
            "Fetch live weather for Minxiong, Taiwan directly from Open-Meteo and display it on "
            "Kira's OLED. Call this when the user asks about current weather or rain chances. "
            "The result contains the measured forecast condition, Celsius temperature, local "
            "update time, and the next hour with at least 30 percent precipitation probability. "
            "Use the returned values when answering; if the tool fails, say the weather service "
            "could not be reached and do not guess.",
            PropertyList(), [weather_display](const PropertyList&) -> ToolResult {
                KiraWeatherCard card;
                std::string error;
                if (!FetchMinxiongWeather(card, error)) {
                    return std::unexpected(error);
                }
                Application::GetInstance().Schedule(
                    [weather_display, card]() { weather_display->SetWeatherCard(card); });

                std::string result = "民雄目前";
                result += WeatherConditionName(card.condition);
                result += "，氣溫 " + std::to_string(card.temperature_c) + "°C。";
                if (card.updated_hour >= 0 && card.updated_minute >= 0) {
                    const auto two_digits = [](int value) {
                        return (value < 10 ? "0" : "") + std::to_string(value);
                    };
                    result += "資料更新時間 " + two_digits(card.updated_hour) + ":" +
                              two_digits(card.updated_minute) + "。";
                }
                if (card.rain_probability >= 0) {
                    result += "下一個較可能降雨時段約 " + std::to_string(card.rain_start_hour) +
                              " 點到 " + std::to_string(card.rain_end_hour) + " 點，降雨機率 " +
                              std::to_string(card.rain_probability) + "%。";
                } else {
                    result += "未來時段沒有達到 30% 門檻的降雨機率。";
                }
                return result;
            });
    }

    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    KiraFaceDisplay* display_ = nullptr;
    Button button_{GPIO_NUM_0};
};

DECLARE_BOARD(KiraXiaoS3Sense);

# Kira Pong — animation source excerpts

These excerpts contain only the Pong animation data, game logic, and drawing code from the original Kira board source. They are not a standalone build target: the original code runs inside the Kira display class and uses that firmware's framebuffer, drawing, dimensions, and random-number helper.

The original board source remains unchanged in the author's firmware workspace. This file exists so a user can hand this animation-specific reference and `AI_PROMPT.md` to an AI without including the weather, audio, or other board implementation.

## Animation tuning constants

```cpp
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
```

## Game state and AI personality

```cpp
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
```

## Geometry, simulation, collisions, and rendering

```cpp
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
```

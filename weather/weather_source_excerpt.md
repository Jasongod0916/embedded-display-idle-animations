# Kira weather implementation reference

These notes and code excerpts describe the original weather feature in `kira-xiao-s3-sense`. They are intentionally not a standalone library: the original code relies on ESP-IDF HTTP/TLS and timer APIs, cJSON, XiaoZhi's MCP server and application scheduler, and Kira's SH1107 drawing helpers. An AI agent must map those responsibilities to the target project instead of copying these APIs blindly.

**Location scope:** the current Kira implementation below is fixed to Minxiong, Taiwan. The handoff prompt extends the system for other projects: confirm the project owner's country once during setup, save its country code, then let the device user name only a city/county in normal weather requests. Resolve each spoken place within the saved country using Open-Meteo Geocoding and its country-code filter. Do not treat the fixed-location code below as already supporting dynamic locations.

## Original fixed-location data request

The Kira firmware requests current temperature, WMO weather code, day/night status, and hourly precipitation probability for Minxiong, Taiwan. The request uses the Asia/Taipei time zone.

```text
https://api.open-meteo.com/v1/forecast
  ?latitude=23.5561
  &longitude=120.4305
  &current=temperature_2m,weather_code,is_day
  &hourly=precipitation_probability
  &forecast_days=1
  &timezone=Asia/Taipei
```

## Location lookup for the extension

After confirming the project owner's supported country during setup, save its ISO country code for later lookups. During normal use, resolve only the spoken city/county with Open-Meteo's [Geocoding API](https://open-meteo.com/en/docs/geocoding-api), using the place as the URL-encoded `name`, an appropriate `language`, `format=json`, and `countryCode` set to the saved code. The device user should not have to repeat the country. Confirm the result's `country_code` matches before using its coordinates. If multiple results in that country are plausible, ask for clarification rather than selecting one at random. For Kira, the configured country is Taiwan (`TW`) and the original no-location default is Minxiong.

The implementation uses HTTPS certificate verification, an eight-second request timeout, and a 6144-byte response limit. Successful parsed results are cached for 10 minutes. It validates the HTTP status and required JSON values before creating a weather card.

## Weather-code mapping

The Kira mapping is based on Open-Meteo's WMO weather codes:

| Code | Display condition |
| --- | --- |
| `0` | Clear by day or clear night, based on `is_day` |
| `1–2` | Partly cloudy |
| `3` | Overcast |
| `45`, `48` | Fog |
| `51–67` | Light rain, except codes `65–67` map to heavy rain |
| `71–77`, `85–86` | Snow |
| `80–82` | Rain; code `82` maps to heavy rain |
| `95–99` | Thunderstorm |
| Other values | Partly cloudy fallback |

The corresponding original one-bit icons are in `kira_weather_icons.h`. The image data is 48 × 48 pixels, one bit per pixel, with ten icons in the same order as the `Condition` enum in that header.

The original code maps the WMO code and the day/night flag like this:

```cpp
void MapWeatherCode(int code, bool is_day, KiraWeatherCard& card) {
    if (code == 0) {
        card.condition = is_day ? kira_weather::Condition::Clear : kira_weather::Condition::NightClear;
    } else if (code == 1 || code == 2) {
        card.condition = kira_weather::Condition::PartlyCloudy;
    } else if (code == 3) {
        card.condition = kira_weather::Condition::Overcast;
    } else if (code == 45 || code == 48) {
        card.condition = kira_weather::Condition::Fog;
    } else if (code >= 51 && code <= 67) {
        card.condition = code >= 65 ? kira_weather::Condition::HeavyRain
                                    : kira_weather::Condition::LightRain;
    } else if (code >= 71 && code <= 77) {
        card.condition = kira_weather::Condition::Snow;
    } else if (code >= 80 && code <= 82) {
        card.condition = code == 82 ? kira_weather::Condition::HeavyRain
                                    : kira_weather::Condition::LightRain;
    } else if (code == 85 || code == 86) {
        card.condition = kira_weather::Condition::Snow;
    } else if (code >= 95 && code <= 99) {
        card.condition = kira_weather::Condition::Thunderstorm;
    } else {
        card.condition = kira_weather::Condition::PartlyCloudy;
    }
}
```

## Rain-window selection

Kira scans hourly entries in time order and selects the first entry whose timestamp is later than the current observation time and whose `precipitation_probability` is at least 30%. It shows that hour as a one-hour range and displays its probability. If no future hour meets the threshold, it omits the rain indicator.

The key selection condition in the original parser is:

```cpp
if (time_is_after_current_observation && probability >= 30) {
    rain_start_hour = hour_from_timestamp(time);
    rain_end_hour = (rain_start_hour + 1) % 24;
    rain_probability = probability;
    break;  // first matching future hour
}
```

## Display and lifecycle

The original display is a 128 × 128 monochrome SH1107 OLED. The weather card shows `民雄` and the data update time at the top, then the weather icon and Celsius temperature in the center. When a rain window exists, a compact footer shows its time range and probability; otherwise, the main condition and temperature use more of the display.

The weather tool schedules the card update through the application's main-task scheduler. The card remains visible until the next user transcript or a 60-second safety timeout. The tool returns the same fetched weather facts to the assistant for its spoken answer. Errors are returned to the assistant instead of showing guessed or stale data.

## Tool behavior

In XiaoZhi, the tool is registered as `self.weather.get_current`. Its description tells the assistant to call it for current-weather or rain-chance questions, use its returned values in the answer, and report a service failure without guessing. Other projects should use their existing assistant function/tool mechanism and preserve the same data/display consistency.

# AI coding task — add a Kira-style weather card

Implement a live weather feature in the user's existing embedded-device firmware, using this folder as the reference. The goal is to reproduce the behavior described below while adapting it to the target project's hardware and architecture.

## Inspect the project first

Read the available source files, board configuration, display setup, build files, assistant/tool APIs, network configuration, and existing UI/state flows. Determine the target MCU, display dimensions and technology, drawing API, language, build system, and how assistant requests and display updates are handled. Do not ask the user to fill in technical details that can be discovered from the project.

Read `README.md`, `weather_source_excerpt.md`, and `kira_weather_icons.h` in this folder. The source excerpts come from Kira's XiaoZhi firmware and are references, not standalone buildable code. Reuse the target project's existing HTTP/TLS, JSON, assistant-tool, event-loop, display, and state-management APIs. Do not copy Kira-specific classes or XiaoZhi code into another project.

If the project already has a weather data source, weather tool, or weather display, inspect it and extend or reuse it where possible. Do not create duplicate weather tools or replace unrelated features.

Before implementing, confirm with the project owner which country the weather feature should support. If the project clearly has a configured country, show what you found and ask the owner to confirm it; otherwise ask them which country to use. Do not assume Taiwan just because the Kira reference is Taiwanese. Save the confirmed country as a project-level setting used by all later place searches. If the owner also wants weather requests that omit a city, ask which default city to use; otherwise do not add an unnecessary default-city question. Wait for the required answer before making country-dependent changes.

## Expected interaction after implementation

The country is selected once during setup with the coding agent. During normal use, the device user can ask for the weather by naming only a city, county, or other place in that country; they do not need to say the country again. The firmware must use the saved country setting to constrain geocoding for every place-only request, then display and speak the weather for the resolved place. For example, if the configured country is Taiwan, a request for “Taipei weather” should resolve Taipei within Taiwan without requiring the user to say “Taiwan.”

## Reproduce the Kira weather behavior

1. **Confirm and save the country during setup.** Register a callable assistant tool using the project's existing mechanism. In the XiaoZhi reference this is `self.weather.get_current`, called when the user asks for current weather or rain chances. Confirm the supported country with the project owner before implementation and save its ISO 3166-1 alpha-2 country code as a project-level setting. Do not ask for the country again during ordinary weather requests.
2. **Accept only the place name at runtime.** Add an optional `location` argument so the assistant can pass the county, city, township, or place name spoken by the user. The user should be able to say “weather in Taipei” (or the equivalent in their language) without repeating the configured country. If a place is omitted, use a default city only if the project already has one or the owner chose one during setup; otherwise ask for a place.
3. **Resolve the place within the saved country.** Query Open-Meteo's Geocoding API over verified HTTPS using `name=<URL-encoded location>`, `count=10`, `language=<appropriate language>`, `format=json`, and `countryCode=<saved ISO country code>`. Validate every selected result's `country_code` matches the saved country; never accept a result from another country. Use the resolved name/admin area and coordinates for the display and forecast. Prefer an exact city/county match. If more than one place is plausible, ask the user which place they mean. If no result exists, explain that and ask for a valid city/county within the configured country. For the Taiwan Kira reference, use `countryCode=TW`; Minxiong (`23.5561`, `120.4305`) is its original default. Open-Meteo documents the Geocoding API and country filter [here](https://open-meteo.com/en/docs/geocoding-api).
4. **Request weather for the resolved coordinates.** Fetch current `temperature_2m`, `weather_code`, and `is_day`, plus hourly `precipitation_probability` for today. Use the resolved latitude/longitude and the time zone returned for that location by geocoding; use `Asia/Taipei` for the Taiwan reference. The reference uses one forecast day.
5. **Parse safely.** Set a finite timeout for both geocoding and forecast requests, validate HTTP status and required JSON fields, bound response sizes, check allocations/results where the target APIs require it, and keep TLS certificate verification enabled. Return a clear error when data cannot be fetched or parsed. Never invent weather or silently display stale data as current.
6. **Map conditions.** Map Open-Meteo weather codes to clear, partly cloudy, overcast, fog, light/heavy rain, snow, or thunderstorm icons. Distinguish clear day from clear night. Use `kira_weather_icons.h` as the visual reference when compatible; otherwise adapt the icons to the target display.
7. **Find the next rain window.** Scan future hourly precipitation entries in time order. Show the first upcoming hour with probability at least 30%, including its hour range and probability. If none reaches the threshold, omit the rain footer.
8. **Display the selected place.** Show the resolved city/county or place name, local update time, condition icon, temperature in the project's preferred unit (Celsius in the Kira reference), and (when available) the next rain window and probability. Never keep the fixed `民雄` label when a different place was requested. Use the project's font or bitmap approach for the selected name; if the display cannot render its script, use a clear short Latin label. The original layout is designed for Kira's 128 × 128 monochrome SH1107 OLED; scale and redraw it for the actual screen. Use the project's UI/state APIs and schedule display mutations on the correct UI or main task.
9. **Keep the assistant answer consistent.** Return the resolved country/place and values obtained by the tool so the assistant can speak the same place, condition, temperature, update time, and rain forecast that appear on the display. Explain failures without guessing.
10. **Cache and clear the card.** Cache successful weather responses for 10 minutes, keyed by the resolved location or coordinates, so one place can never return another place's cached weather. Clear the card on the next user transcript or equivalent new interaction; also enforce a 60-second safety timeout so an old card cannot remain indefinitely. Use the target project's synchronization and lifecycle patterns.
11. **Credit the data sources.** Preserve Open-Meteo and GeoNames attribution in the project's documentation or UI as appropriate, and review the current [Open-Meteo terms](https://open-meteo.com/en/terms) before commercial use.

## Preserve the existing project

Add the weather feature without replacing existing screens, idle behavior, wake handling, network configuration, assistant features, or other user code. Keep the changes focused. If a requested integration would require removing or changing existing behavior, inspect for a composition or selection mechanism first and explain any unavoidable conflict instead of silently deleting functionality.

## Finish the implementation

Follow the project's existing code style and build process. Run the relevant build or checks available in the project. In the final response, summarize the files changed, the data source and location used, how the assistant invokes and displays weather, cache/clear behavior, build verification, and any remaining hardware-specific checks.

If the firmware project itself is not available, explain that the project source is needed. Do not return only a generic plan when the project files are available.

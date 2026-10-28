# AI coding task — add a Kira-style weather card

Implement a live weather feature in the user's existing embedded-device firmware, using this folder as the reference. The goal is to reproduce the behavior described below while adapting it to the target project's hardware and architecture.

## Inspect the project first

Read the available source files, board configuration, display setup, build files, assistant/tool APIs, network configuration, and existing UI/state flows. Determine the target MCU, display dimensions and technology, drawing API, language, build system, and how assistant requests and display updates are handled. Do not ask the user to fill in details that can be discovered from the project.

Read `README.md`, `weather_source_excerpt.md`, and `kira_weather_icons.h` in this folder. The source excerpts come from Kira's XiaoZhi firmware and are references, not standalone buildable code. Reuse the target project's existing HTTP/TLS, JSON, assistant-tool, event-loop, display, and state-management APIs. Do not copy Kira-specific classes or XiaoZhi code into another project.

If the project already has a weather data source, weather tool, or weather display, inspect it and extend or reuse it where possible. Do not create duplicate weather tools or replace unrelated features.

## Reproduce the Kira weather behavior

1. **Fetch real data.** Register a callable assistant tool using the project's existing mechanism. In the XiaoZhi reference this is `self.weather.get_current`, called when the user asks for current weather or rain chances. Fetch data from Open-Meteo over verified HTTPS. The reference location is Minxiong, Taiwan (latitude `23.5561`, longitude `120.4305`), with `timezone=Asia/Taipei`. If the project already defines a location, use that; otherwise use the reference location and make it easy to change in one clearly named configuration value.
2. **Request the same fields.** Fetch current `temperature_2m`, `weather_code`, and `is_day`, plus hourly `precipitation_probability` for today. The reference uses one forecast day and the Asia/Taipei timezone.
3. **Parse safely.** Set a finite network timeout, validate HTTP status and required JSON fields, bound the response size, check allocations/results where the target APIs require it, and keep TLS certificate verification enabled. Return a clear error when data cannot be fetched or parsed. Never invent weather or silently display stale data as current.
4. **Map conditions.** Map Open-Meteo weather codes to clear, partly cloudy, overcast, fog, light/heavy rain, snow, or thunderstorm icons. Distinguish clear day from clear night. Use `kira_weather_icons.h` as the visual reference when compatible; otherwise adapt the icons to the target display.
5. **Find the next rain window.** Scan future hourly precipitation entries in time order. Show the first upcoming hour with probability at least 30%, including its hour range and probability. If none reaches the threshold, omit the rain footer.
6. **Display the result.** Show a weather card with the location, local update time, condition icon, Celsius temperature, and (when available) the next rain window and probability. The original layout is designed for Kira's 128 × 128 monochrome SH1107 OLED; scale and redraw it for the actual screen. Use the project's UI/state APIs and schedule display mutations on the correct UI or main task.
7. **Keep the assistant answer consistent.** Return the values obtained by the tool so the assistant can speak the same condition, temperature, update time, and rain forecast that appear on the display. Explain failures without guessing.
8. **Cache and clear the card.** Cache successful weather responses for 10 minutes to avoid repeated requests. Clear the card on the next user transcript or equivalent new interaction; also enforce a 60-second safety timeout so an old card cannot remain indefinitely. Use the target project's synchronization and lifecycle patterns.
9. **Credit the data source.** Preserve Open-Meteo attribution in the project's documentation or UI as appropriate, and review its current [terms](https://open-meteo.com/en/terms) before commercial use.

## Preserve the existing project

Add the weather feature without replacing existing screens, idle behavior, wake handling, network configuration, assistant features, or other user code. Keep the changes focused. If a requested integration would require removing or changing existing behavior, inspect for a composition or selection mechanism first and explain any unavoidable conflict instead of silently deleting functionality.

## Finish the implementation

Follow the project's existing code style and build process. Run the relevant build or checks available in the project. In the final response, summarize the files changed, the data source and location used, how the assistant invokes and displays weather, cache/clear behavior, build verification, and any remaining hardware-specific checks.

If the firmware project itself is not available, explain that the project source is needed. Do not return only a generic plan when the project files are available.

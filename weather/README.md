# Weather card

**Original feature:** implemented for this project by the repository owner in the Kira firmware. The weather feature is separate from the Pong standby animation.

This folder is an AI handoff pack for adding a similar live weather card to an existing embedded-device project. Give this folder and the project to an AI coding agent. The included `AI_PROMPT.md` tells the agent to inspect the project, adapt the reference to its existing display and assistant APIs, preserve unrelated features, and build the result.

## Kira reference behavior

- A custom assistant tool named `self.weather.get_current` retrieves current weather for Minxiong, Taiwan from Open-Meteo over HTTPS.
- The tool returns the same condition, temperature, local update time, and rain information that it displays, so the assistant can use the fetched data in its spoken reply.
- The 128 × 128 monochrome SH1107 OLED card shows a condition icon, temperature in Celsius, location, and Taipei-local update time.
- It also shows the next upcoming hour whose precipitation probability is at least 30%, with its time range and probability.
- Successful responses are cached for 10 minutes. The card is cleared on the next user transcript, with a 60-second timeout as a fallback.
- Network, HTTP, or parsing errors are reported instead of showing guessed or stale weather.

The AI should adapt the layout, drawing calls, tool registration, event scheduling, and timeout behavior to the target project. The Kira implementation is a reference, not a standalone library.

## Files

- [`AI_PROMPT.md`](AI_PROMPT.md): ready-to-use instructions for an AI coding agent. No fields need to be filled in.
- [`weather_source_excerpt.md`](weather_source_excerpt.md): implementation details and code excerpts from the Kira reference.
- [`kira_weather_icons.h`](kira_weather_icons.h): the original 48 × 48 one-bit weather icon set and the small Kira location/update labels. Adapt or redraw them for other display APIs as needed.

## Data source and attribution

Weather data comes from [Open-Meteo](https://open-meteo.com/). The Kira implementation uses Open-Meteo's current conditions and hourly precipitation probability. Include the required [CC BY 4.0 attribution](https://open-meteo.com/en/terms) when using the weather data, and review Open-Meteo's terms before commercial use.

## License

The original weather-card implementation and icons do not yet have a separate license. Check back before redistributing or incorporating this source into another project. Open-Meteo's data and service have their own terms and attribution requirements.

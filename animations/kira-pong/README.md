# Kira Pong

**Original animation:** authored for this project by the repository owner. It is not an animation authored by the XiaoZhi firmware authors.

This folder is intended to be handed to an AI coding assistant as a self-contained reference pack. Give the AI this entire folder, then describe your target board, display, language, and build system. A copy-ready prompt is in [`AI_PROMPT.md`](AI_PROMPT.md).

## Preview

Add a short GIF or video of the OLED animation here when ready.

## Original target

- Seeed XIAO ESP32-S3 Sense, in the Kira assembly
- 128 x 128 monochrome SH1107 OLED over I2C
- The firmware uses a page-oriented 1-bit framebuffer; the panel is updated by sending only changed byte runs after the first full frame

## Behavior

- Starts after ten seconds of idle with no speech or interaction.
- Two AI-controlled paddles play silently; there is no score or on-screen text.
- The left AI predicts the ball's full intercept and favors an upper home lane. The right AI reacts with a delay and favors a lower lane.
- Each session samples a small fixed personality for each paddle. The simulation scales to display dimensions and advances using elapsed frame time.
- Wake/listening activity restores the normal face.

## Source and integration context

[`reference/kira_board.cc`](reference/kira_board.cc) is a byte-for-byte copy of the current Kira board implementation in the author's local XiaoZhi firmware working tree. The Pong constants and game/render routines are embedded in this larger board file, so this is the original reference rather than a standalone buildable animation library.

The full file includes other Kira board functionality and depends on the XiaoZhi firmware APIs. The upstream project is [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32). Its MIT license is included as [`XIAOZHI-MIT-LICENSE.txt`](XIAOZHI-MIT-LICENSE.txt) for the firmware source in the reference snapshot. This does not identify the Pong animation as an upstream-authored work.

When porting, keep the gameplay behavior and replace the Kira framebuffer/drawing calls with the target device's display API. Leave the target device's idle detection and wake-to-normal-screen transition in its host UI layer.

## License

A separate license for the original animation has not been selected yet. Check back before redistributing or incorporating the animation source into another project.

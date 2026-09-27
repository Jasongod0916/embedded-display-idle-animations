# Kira Pong

**Original animation:** authored for this project by the repository owner. It is not an animation authored by the XiaoZhi firmware authors.

This folder is an AI handoff pack: the animation-specific source excerpt, a short behavior and hardware description, and a copy-ready prompt. Give the whole folder to an AI coding assistant along with your target board, display, language, and build system.

## Original target

- Seeed XIAO ESP32-S3 Sense, in the Kira assembly
- 128 x 128 monochrome SH1107 OLED over I2C

## Behavior

- Starts after ten seconds of idle with no speech or interaction.
- Two AI-controlled paddles play silently; there is no score or on-screen text.
- The left AI predicts the ball's full intercept and favors an upper home lane. The right AI reacts with a delay and favors a lower lane.
- Each session samples a small fixed personality for each paddle. The simulation scales to display dimensions and advances using elapsed frame time.
- Wake/listening activity restores the normal face.

## Files

- [`kira_pong_source_excerpt.md`](kira_pong_source_excerpt.md): only the Pong constants, game state, AI, physics, collision, and drawing code extracted from the original Kira board implementation. It is an excerpt, not a standalone buildable library.
- [`AI_PROMPT.md`](AI_PROMPT.md): ready-to-copy instructions for asking an AI to port the animation.
- [`XIAOZHI-MIT-LICENSE.txt`](XIAOZHI-MIT-LICENSE.txt): preserves the upstream firmware license notice for source excerpts taken from the integration file.

The display API and idle/wake trigger are host-device responsibilities. For another board, ask the AI to adapt only the rendering and integration layer while preserving the gameplay behavior.

## License

A separate license for the original animation has not been selected yet. Check back before redistributing or incorporating the animation source into another project.

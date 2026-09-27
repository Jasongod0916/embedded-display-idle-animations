# Pong

**Original animation:** authored for this project by the repository owner. It is not an animation authored by the XiaoZhi firmware authors.

This folder is for people who already have firmware and other device features, but want to add this standby animation. Give this folder to an AI coding agent working in your existing project. The included `AI_PROMPT.md` is ready to use as-is: the agent should inspect the project files to discover the hardware and integration points, then add only the animation while preserving existing features such as weather, clock, menus, networking, audio, and wake handling. No fields need to be filled in.

## Original target

- Seeed XIAO ESP32-S3 Sense, in the Kira assembly
- 128 x 128 monochrome SH1107 OLED over I2C

## Preview

![Pong standby animation](preview.gif)

The flicker in the GIF is caused by recording the display; the animation looks normal when viewed directly.

## Behavior

- Starts after ten seconds of idle with no speech or interaction.
- Two AI-controlled paddles play silently; there is no score or on-screen text.
- The left AI predicts the ball's full intercept and favors an upper home lane. The right AI reacts with a delay and favors a lower lane.
- Each session samples a small fixed personality for each paddle. The simulation scales to display dimensions and advances using elapsed frame time.
- Wake/listening activity restores the normal face.

## Files

- [`pong_source_excerpt.md`](pong_source_excerpt.md): only the Pong constants, game state, AI, physics, collision, and drawing code extracted from the original Kira board implementation. It is an excerpt, not a standalone buildable library.
- [`AI_PROMPT.md`](AI_PROMPT.md): ready-to-copy instructions for asking an AI to integrate the animation into an existing project.
- [`XIAOZHI-MIT-LICENSE.txt`](XIAOZHI-MIT-LICENSE.txt): preserves the upstream firmware license notice for source excerpts taken from the integration file.

The display API and idle/wake trigger belong to the host device. The AI should reuse the host project's existing drawing and state-management APIs, adding the animation without replacing unrelated behavior.

## License

A separate license for the original animation has not been selected yet. Check back before redistributing or incorporating the animation source into another project.

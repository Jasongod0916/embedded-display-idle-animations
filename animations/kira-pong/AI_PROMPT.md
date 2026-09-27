# AI porting prompt — Kira Pong

Give the AI this whole `kira-pong` folder. Fill in the target details, then send the prompt below.

---

Please study the animation-specific source excerpt in this folder and help me port or recreate its standby animation for my device.

## My target device

- Board / MCU: [fill in]
- Display model and resolution: [fill in]
- Display library or drawing API: [fill in]
- Programming language and build system: [fill in]
- How the device detects idle and wake/input: [fill in]

## What to preserve

Use `kira_pong_source_excerpt.md` for the original Pong constants, state, AI, physics, collision, and drawing behavior. Preserve the asymmetric AI personalities, screen-scaled geometry, elapsed-time simulation, and silent no-score presentation. Translate the Kira-specific framebuffer and drawing calls to my target display API.

The source is an excerpt from a larger firmware class, not a standalone library. First identify its assumptions and required helpers. Do not assume an API or hardware feature that I have not listed; ask me for missing details before writing code. Then provide the exact files to add/change and the build or flashing steps for my device.

Do not copy unrelated XiaoZhi board, audio, weather, or cloud-assistant code into my project.

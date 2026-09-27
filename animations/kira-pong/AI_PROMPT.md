# AI porting prompt — Kira Pong

Give the AI this whole `kira-pong` folder together with your existing firmware project. Fill in the details below, then send the prompt.

---

Please study the animation-specific source excerpt in this folder and integrate or recreate its standby animation in my existing device project.

## My target device and project

- Board / MCU: [fill in]
- Display model and resolution: [fill in]
- Display library or drawing API: [fill in]
- Programming language and build system: [fill in]
- Existing project/source files: [attach or point to them]
- How the project detects idle and wake/input: [fill in, if known]
- Existing behavior that must remain unchanged: [for example, weather display, clock, menus, networking, audio, and wake handling]

## What to preserve

Use `kira_pong_source_excerpt.md` for the original Pong constants, state, AI, physics, collision, and drawing behavior. Preserve the asymmetric AI personalities, screen-scaled geometry, elapsed-time simulation, and silent no-score presentation. Translate the Kira-specific framebuffer and drawing calls to my target display API.

First inspect my existing project and identify its display abstraction, idle-state handling, and wake/input flow. Reuse those existing APIs and integrate the animation into the current idle experience. Do not replace or reimplement my weather, clock, menus, networking, audio, or other existing features. Keep the changes limited to what is needed for the animation. If the animation conflicts with an existing screen or behavior, explain the conflict and ask how it should fit before changing that behavior.

The source is an excerpt from a larger firmware class, not a standalone library. Identify its assumptions and required helpers. Do not assume an API or hardware feature that I have not listed; ask me for missing details before writing code. Then provide the exact files to add/change and the build or flashing steps for my device.

Do not copy unrelated XiaoZhi board or cloud-assistant code into my project.

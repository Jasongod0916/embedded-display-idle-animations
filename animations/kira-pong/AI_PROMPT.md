# AI porting prompt — Kira Pong

Give the AI this whole `kira-pong` folder. Fill in the target details, then send the prompt below.

---

Please study this animation folder and help me port or recreate its standby animation for my device.

## My target device

- Board / MCU: [fill in]
- Display model and resolution: [fill in]
- Display library or drawing API: [fill in]
- Programming language and build system: [fill in]
- How the device detects idle and wake/input: [fill in]

## What to preserve

Use `README.md` for the animation behavior and `reference/kira_board.cc` as the original source reference. Preserve the Pong gameplay, asymmetric AI personalities, screen-scaled geometry, elapsed-time simulation, and silent no-score presentation. Translate only the display integration and any hardware-specific timing needed for my target.

First identify which parts of the reference implement game state, AI, physics/collisions, rendering, and host idle/wake handling. Then propose a small target-specific implementation that fits my existing project. Do not assume an API or hardware feature that I have not listed; ask me for missing details before writing code. Include the exact files to add/change and the build or flashing steps for my device.

If the original reference depends on XiaoZhi/Kira helpers that do not exist on my target, explain the dependency and provide a target-side replacement. Do not copy unrelated XiaoZhi board, audio, weather, or cloud-assistant code into my project.

# AI coding task — integrate the Pong standby animation

Implement the Pong standby animation from this folder in the user's existing firmware project.

## Inspect the project first

Read the available source files, board configuration, display setup, build files, and existing documentation to determine the target MCU, display, display API, language, build system, and current idle/wake flow. Do not ask the user to fill in hardware or project details that can be discovered from those files. If the project is available in your workspace, proceed with implementation instead of returning only a plan.

Use `pong_source_excerpt.md` as the source reference for the game's state, asymmetric AI, physics, collisions, and rendering behavior. It is an excerpt from the original Kira display class, not a standalone library. Identify and adapt its assumptions and helpers to the existing project.

## Integrate only the animation

Add this animation to the project's existing standby/idle experience, reusing the project's current display and state-management APIs. Keep the existing idle trigger and wake/input handling where they are, and return to the existing screen when the device wakes or otherwise leaves idle.

Preserve the behavior and features already present in the user's project, including weather screens, clocks, menus, networking, audio, and other displays. Do not replace, duplicate, or reimplement those systems. Keep the code changes focused on the animation and its integration. Do not copy unrelated XiaoZhi board or cloud-assistant code into the user's project.

If the project has a conflict between the Pong animation and an existing idle screen, first look for an existing selection or composition mechanism. Do not silently remove or alter existing behavior to make room for Pong.

## Finish the implementation

Follow the project's existing code style and build process. Run the relevant build or checks that are available in the project, and report what was and was not verified on actual hardware. In your final response, summarize the files changed, how the animation is triggered and exited, and any remaining hardware-specific steps.

If the firmware project itself is not available to you, explain that the source project is needed; do not ask the user to manually fill in the hardware fields above.

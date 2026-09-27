# Embedded Display Idle Animations

A collection of original standby animations for small embedded displays. Each animation folder is designed to be shared on its own: give it to an AI coding assistant along with your board and display details, and ask it to port or recreate the animation for your device.

The collection can cover different display technologies and sizes, but an animation is not automatically compatible with every screen. Each folder should state which hardware and display have been tested, plus any refresh-rate, color, or library assumptions.

## Animation folders

- [`animations/kira-pong/`](animations/kira-pong/) — original silent AI-versus-AI Pong animation, currently referenced from its Kira XIAO ESP32-S3 Sense implementation with a 128 x 128 monochrome SH1107 OLED.

## Folder format

Each animation folder should carry the material needed to understand that animation without relying on another animation folder:

- Original source or a clearly identified source reference
- Behavior and timing description
- Display, color, input, and hardware assumptions
- Dependencies and host integration points
- Preview image or video when available
- `AI_PROMPT.md` that users can copy and fill in with their target hardware details

Compiled firmware and a one-click installer can be added later as optional conveniences. The source and per-animation handoff folder are the main deliverables.

## Licensing

The Kira Pong folder includes a reference snapshot of a larger firmware source file, so its upstream MIT notice is kept inside that folder. A separate license for the original animation has not been selected yet.

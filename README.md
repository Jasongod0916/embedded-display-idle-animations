# Embedded Display Idle Animations

A collection of original standby animations for small embedded displays. Each animation folder is an AI handoff pack: animation-specific source, behavior and hardware notes, and a prompt users can give to an AI coding assistant to port or recreate the animation for their device.

Animations may target different display technologies and sizes, but are not automatically compatible with every screen. Each folder states its tested hardware and assumptions.

## Animation folders

- [`animations/kira-pong/`](animations/kira-pong/) — original silent AI-versus-AI Pong animation, with an animation-only source excerpt and AI porting prompt. Its original target is Kira with a 128 x 128 monochrome SH1107 OLED.

Compiled firmware and one-click installers are optional future additions. The per-animation source folder is the main deliverable.

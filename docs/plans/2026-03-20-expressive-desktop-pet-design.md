# Expressive Desktop Pet — Design Document

## Overview

A cute desktop pet that lives in a dark macOS window with eyes and a mouth that express emotions. The face reacts to mouse behavior, time of day, system events, and random moods. All rendering is procedural C/OpenGL — no textures or art assets.

## Face Parameter System

The face is defined by continuous floats, lerped every frame toward target values:

| Parameter | Range | Description |
|---|---|---|
| eye_openness | 0.0–1.4 | 0 = closed slit, 1 = normal, >1 = wide |
| eye_scale | 0.8–1.2 | Overall eye size multiplier |
| pupil_scale | 0.7–1.4 | Pupil size (dilated/constricted) |
| eye_squint | 0.0–1.0 | Flattens bottom of eye (half-closed) |
| mouth_curve | -1.0–1.0 | Negative = frown, positive = smile |
| mouth_openness | 0.0–1.0 | 0 = closed line, 1 = open O shape |
| mouth_width | 0.8–1.2 | Horizontal mouth span |

Interpolation: `current = lerp(current, target, 1 - exp(-speed * dt))`

## Emotion Presets

| State | eye_open | eye_scale | pupil | squint | mouth_curve | mouth_open | mouth_width |
|---|---|---|---|---|---|---|---|
| NEUTRAL | 1.0 | 1.0 | 1.0 | 0.0 | 0.0 | 0.0 | 1.0 |
| HAPPY | 1.0 | 1.0 | 1.1 | 0.2 | 1.0 | 0.0 | 1.2 |
| EXCITED | 1.2 | 1.15 | 1.3 | 0.0 | 0.8 | 0.3 | 1.1 |
| SURPRISED | 1.4 | 1.2 | 1.4 | 0.0 | 0.0 | 1.0 | 0.8 |
| SLEEPY | 0.3 | 0.9 | 0.7 | 0.5 | 0.1 | 0.0 | 0.9 |
| BORED | 0.7 | 0.95 | 0.8 | 0.3 | -0.2 | 0.0 | 1.0 |
| CURIOUS | 1.1 | 1.05 | 1.2 | 0.0 | 0.2 | 0.1 | 0.9 |
| SAD | 0.8 | 0.95 | 0.9 | 0.0 | -0.7 | 0.0 | 0.8 |

### Layered Animations

- **Blinking**: periodic eye_openness dip to 0 and back (~150ms), random interval 2–6s
- **Yawning**: mouth_openness ramps to 1.0, eyes squeeze shut, then relaxes (triggered by sleepy state)

## Trigger System

Priority-based — higher priority overrides lower. Each trigger sets a target emotion.

### Priority 1 — System Events (immediate, 3–5s duration)

- Low battery (<20%) → SAD
- High CPU (>80%) → EXCITED
- WiFi disconnect → SURPRISED → SAD

### Priority 2 — Mouse Behavior (reactive)

- Fast cursor movement → EXCITED
- Click near the pet → SURPRISED (1s flash)
- Hovering over window → HAPPY
- Cursor leaves window → fade to background mood

### Priority 3 — Background Mood (ambient)

- **Time-based**: morning=HAPPY, afternoon=NEUTRAL, evening=BORED, night=SLEEPY
- **Idle escalation**: 30s idle→BORED, 2min→SLEEPY, 5min→asleep (eyes closed), movement→SURPRISED then HAPPY
- **Random shifts**: every 15–30s, small chance to briefly go CURIOUS or shift subtly

Cursor tracking (pupils follow mouse) stays active across all states, dampened when sleepy/asleep.

## Rendering

All procedural, no textures.

**Eyes**: ellipses with vertical radius scaled by eye_openness and eye_scale. Squint flattens from bottom only. Pupil orbits toward cursor inside sclera.

**Mouth**: quadratic bezier curve as thick triangle strip. 3 control points (left corner, center, right corner). mouth_curve moves center up/down, mouth_openness splits into top+bottom arcs, mouth_width scales horizontal span.

**Draw order**: clear → sclera ×2 → pupils ×2 → mouth

## Architecture

```
src/
  main.c            — window setup, main loop, input callbacks
  face.h / face.c   — FaceParams, emotion presets, lerp, blink/yawn, rendering
  mood.h / mood.c   — triggers, priority resolution, outputs target emotion
  platform_macos.m  — window styling + system queries (battery, CPU, WiFi)
include/
  platform.h        — C interface for platform functions
```

**Main loop**: poll inputs → mood_update(dt) → face_update(dt) → face_render()

**System events**: exposed through platform.h, polled once per second. macOS-specific implementation in platform_macos.m.

**Dependencies**: none new. Pure C11, GLFW, GLAD, Cocoa (existing).

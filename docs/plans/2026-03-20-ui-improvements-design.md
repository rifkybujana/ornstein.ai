# UI Improvements Design

## Summary

Five enhancement areas for the Ornstein desktop pet: expressiveness, ambient life, chat UX, stats feedback loop, and mood detection from LLM responses.

Architecture: Modular — new `ambient.c`, `stats.c`, `sentiment.c` modules, with surgical changes to existing `face.c`, `chat.c`, `main.c`, `mood.c`, `llm.c`.

---

## 1. Expressiveness Enhancements

### EMOTION_THINKING (9th emotion)
- Preset: `{0.8f, 1.0f, 0.8f, 0.15f, 0.1f, 0.05f, 0.9f}`
- Eyes slightly squinted, pupils small and centered, mouth slightly pursed.
- Added to `face.h` Emotion enum and `face.c` presets array.

### Double-blink
- After a normal blink completes, 20% chance of a second blink 0.15s later.
- New field in `FaceState`: `int double_blink_pending`.
- Logic in `face_update()`: when blink_phase reaches 0 and was blinking, roll 20% chance, if hit, immediately set blink_phase to BLINK_DURATION again.

### Breathing glow
- Radial gradient circle rendered behind the face at (cx, cy).
- Pulsates via sine wave: `radius = base_radius + sin(time * 2.1) * 15.0 * scale`.
- Color per mood lookup table:

| Emotion    | R    | G    | B    |
|------------|------|------|------|
| Neutral    | 0.40 | 0.40 | 0.45 |
| Happy      | 0.90 | 0.80 | 0.30 |
| Excited    | 1.00 | 0.60 | 0.20 |
| Surprised  | 0.90 | 0.90 | 1.00 |
| Sleepy     | 0.20 | 0.30 | 0.70 |
| Bored      | 0.30 | 0.30 | 0.35 |
| Curious    | 0.20 | 0.70 | 0.70 |
| Sad        | 0.30 | 0.40 | 0.80 |
| Thinking   | 0.50 | 0.30 | 0.80 |

- New fragment shader with radial alpha falloff: `alpha = smoothstep(1.0, 0.0, length(uv))`.
- Rendered in `ambient.c` (glow is part of ambient system), called from `main.c` before `face_render()`.

---

## 2. Ambient Life

### New module: `ambient.h/c`

### Idle bobbing
- Face center Y oscillates: `cy += sin(time * 0.8) * 4.0 * scale`.
- Applied in `main.c` before passing cy to `face_render()`.
- 4px amplitude — subtle, not distracting.

### Floating particles
- Pool of 16 particles, each with: position (x,y), velocity (vx,vy), lifetime, max_lifetime, alpha, shape type.
- Shape types per mood:
  - Happy: hearts (two overlapping circles + triangle)
  - Sleepy: Z letters (text_draw)
  - Curious: question marks (text_draw)
  - Excited: stars (line segments)
  - Thinking: dots/ellipsis (text_draw)
  - Sad: teardrops (circle + triangle)
  - Neutral/Bored: simple circles
  - Surprised: exclamation marks (text_draw)
- Spawn near face center with upward drift + sine-wave horizontal wobble.
- Fade in 0.3s, live 2-4s, fade out 0.5s.
- 1-2 particles per second spawn rate.
- Rendered with existing ellipse/text primitives.

### Reflection glow
- Wide flat radial gradient below face at (cx, cy + 80*scale).
- Same mood color as breathing glow at ~15% opacity.
- Horizontal stretch: rx=100px, ry=20px — like a pool of light.
- Rendered in `ambient.c` after the breathing glow.

### API
```c
void ambient_init(void);
void ambient_update(float cx, float cy, Emotion emotion, float dt, float time);
void ambient_render(float cx, float cy, Emotion emotion, float scale,
                    float fb_w, float fb_h, float time);
float ambient_bob_offset(float time, float scale); // returns Y offset
void ambient_cleanup(void);
```

---

## 3. Chat UX

### Directional chat bubbles
- User messages: right-aligned, blue tint background (0.2, 0.35, 0.6, 0.85).
- Pet messages: left-aligned, dark background (0.18, 0.18, 0.2, 0.85).
- Thinking messages: left-aligned, transparent (0.15, 0.15, 0.17, 0.7).
- Bubble padding: 8px horizontal, 6px vertical.
- Max bubble width: 70% of chat area.

### Entry animations
- New field in `ChatMessage`: `float age` (incremented in chat_update).
- First 0.3s: slide in from side with eased alpha.
- Offset: `x_offset = (1.0 - ease_out(age/0.3)) * 30px`.
- Alpha: `min(1.0, age / 0.2)`.

### Typing indicator
- When `generating && pending_len == 0 && thinking_len == 0`.
- Three circles at pet message position with staggered sine-wave scale.
- `dot_scale = 0.5 + 0.5 * sin(time * 4.0 + dot_index * 1.0)`.
- Replaces static "..." text.

### Mood-reactive colors
- Input box border: 1px line using mood glow color at 40% opacity when focused.
- Mood color smoothly lerps when emotion changes.
- Uses the same mood color table from Section 1.

---

## 4. Stats & Feedback Loop

### New module: `stats.h/c`

### Three stats: Bond, Joy, Energy (float 0.0 to 1.0)

**Evolution rules:**
- Bond: +0.02/message, +0.005/min with chat open. Decays 0.01/hour between sessions.
- Joy: +0.05 on happy/excited response, -0.03 on sad. Decays 0.02/hour between sessions.
- Energy: starts 1.0, drains -0.01/message, regenerates +0.003/sec when idle. Decays 0.05/hour between sessions.

**Persistence:**
- File: `~/.ornstein/stats.json`
- Format: `{"bond": 0.45, "joy": 0.7, "energy": 0.3, "last_save": 1711000000}`
- Saved on shutdown and every 60s.
- On load: compute elapsed hours, apply decay, clamp to [0,1].

**Rendering (top bar, in main.c):**
- Three small progress bars at top of window.
- Only visible when chat panel is open.
- Each: label text + colored fill bar.
  - Bond: pink/red (0.9, 0.4, 0.5)
  - Joy: yellow (0.9, 0.8, 0.3)
  - Energy: green (0.3, 0.8, 0.5)
- Bar dimensions: 80px wide, 6px tall, 1px dark border.

### API
```c
void stats_init(void);           // Load from disk, apply decay
void stats_update(float dt);     // Regen energy, auto-save timer
void stats_on_message(void);     // Bond++, Energy--
void stats_on_chat_open(float dt); // Bond += 0.005 * dt/60
void stats_on_mood(Emotion e);   // Joy adjustment based on detected mood
float stats_bond(void);
float stats_joy(void);
float stats_energy(void);
void stats_save(void);           // Write to disk
void stats_shutdown(void);       // Final save
```

---

## 5. Mood Detection from Response Text

### New module: `sentiment.h/c`

### LLM mood tagging
- Modify system prompt in `llm.c` to append:
  `"Before your response, tag your mood: [MOOD:emotion]. Valid emotions: neutral, happy, excited, surprised, sleepy, bored, curious, sad, thinking."`

### Parsing
```c
// Returns 1 if mood tag found, 0 otherwise.
// If found, sets *out_emotion and *tag_end to the index past the tag.
int sentiment_parse(const char *text, Emotion *out_emotion, int *tag_end);
```
- Scans for `[MOOD:xxx]` at start of text (with optional leading whitespace).
- Maps string to Emotion enum value.

### Integration
- In `chat.c` `on_token()` when `done=1`: parse final response, strip tag from displayed text.
- If mood detected: call `mood_set_sentiment_override(ms, emotion, 5.0f)` (new function in mood.c).
- Also call `stats_on_mood(emotion)` to update Joy.

### New mood.c function
```c
void mood_set_sentiment_override(MoodState *ms, Emotion e, float duration);
```
- Sets override_emotion and override_timer, same as click/system overrides.
- 5s duration — long enough to feel reactive.

### Edge cases
- No tag → no override, normal mood system continues.
- Tag parsed only at generation completion, not per-token.
- Malformed tag (unknown emotion string) → ignored.

---

## Files Changed

| File | Change type | Description |
|------|-------------|-------------|
| `src/ambient.h` | NEW | Particle, glow, bobbing API |
| `src/ambient.c` | NEW | Particle system, radial glow shader, reflection |
| `src/stats.h` | NEW | Stats API (bond, joy, energy) |
| `src/stats.c` | NEW | Stats logic, JSON persistence, decay |
| `src/sentiment.h` | NEW | Mood tag parser API |
| `src/sentiment.c` | NEW | Parse [MOOD:xxx] from LLM output |
| `src/face.h` | MODIFY | Add EMOTION_THINKING to enum |
| `src/face.c` | MODIFY | Add thinking preset, double-blink logic |
| `src/chat.c` | MODIFY | Bubbles, entry animations, typing dots, mood colors, sentiment integration |
| `src/mood.h` | MODIFY | Add mood_set_sentiment_override() |
| `src/mood.c` | MODIFY | Implement sentiment override |
| `src/llm.c` | MODIFY | Update system prompt with mood tag instruction |
| `src/main.c` | MODIFY | Wire ambient/stats, bobbing, stats bar rendering |
| `CMakeLists.txt` | MODIFY | Add new source files |

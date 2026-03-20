# llama.cpp Integration with Metal -- Design Document

## Overview

Add local LLM inference to the ornstein desktop pet via llama.cpp with Metal GPU acceleration. The pet gains a personality and can converse with the user through a chat panel. llama-server runs as a child process; ornstein communicates with it over HTTP.

## Decisions

- **Purpose**: Personality/dialogue -- the pet generates expressive text responses
- **UI**: Chat panel rendered alongside the face in the same window
- **Model size**: Small (1-3B params) for fast inference
- **Integration**: Subprocess -- llama-server spawned as a child process
- **Build**: Git submodule, built from source with Metal enabled

## Architecture

```
ornstein (main process)
  +-- face rendering (OpenGL, existing)
  +-- mood system (existing)
  +-- chat panel (new, OpenGL text rendering)
  +-- llm client (new, HTTP to llama-server)

llama-server (child process)
  +-- spawned by ornstein on startup
  +-- Metal GPU inference
  +-- REST API on localhost
```

## New Source Files

```
src/
  llm.h / llm.c       -- spawn llama-server, HTTP client, prompt management
  chat.h / chat.c      -- chat panel UI, message history, text input
  text.h / text.c      -- OpenGL bitmap font renderer (simple monospace)
```

## llm module (llm.h / llm.c)

### API

- `llm_init(const char *model_path, int port)` -- fork/exec llama-server with Metal enabled (`--gpu-layers 99`), wait for health endpoint
- `llm_send(const char *user_msg, llm_callback cb)` -- POST to `/v1/chat/completions` on a background thread, stream tokens via callback
- `llm_poll()` -- called each frame, pumps ready tokens into chat buffer (thread-safe queue)
- `llm_shutdown()` -- SIGTERM child, waitpid, cleanup

### System Prompt

The pet has a short personality prompt. Current mood is injected as context:

```
You are a cute desktop pet. You speak in short, expressive sentences.
You're currently feeling {mood}. Keep responses under 2 sentences.
```

### Model Path Resolution

1. `ORNSTEIN_MODEL` environment variable
2. `~/.ornstein/model.gguf`
3. If neither found, chat panel shows "No model found" with instructions

## chat module (chat.h / chat.c)

- Panel on the right side of the window (~300px wide)
- Message history: user messages (right-aligned) + pet responses (left-aligned)
- Text input at bottom, captures keyboard when chat panel is focused
- Pet responses stream in token-by-token
- Scrollable history
- Toggle: press Tab or click to focus/unfocus chat panel

## text module (text.h / text.c)

- Bitmap font renderer using an embedded font atlas (baked into C as a byte array)
- Monospace glyphs rendered as textured OpenGL quads
- Word-wrapping, vertical scrolling
- ASCII-only (sufficient for chat)

## Build Changes

### Git Submodule

```bash
git submodule add https://github.com/ggerganov/llama.cpp.git lib/llama.cpp
git submodule update --init
```

### CMakeLists.txt

```cmake
# llama.cpp -- only build the server
set(LLAMA_METAL ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/llama.cpp)
```

The `llama-server` binary is produced in the build directory. ornstein locates it relative to its own executable.

### New Dependencies

- **libcurl** (or raw sockets): for HTTP client to llama-server. Prefer libcurl as it's available on macOS by default.
- **pthreads**: for background inference thread (already available on macOS).

### Link Changes

```cmake
add_executable(ornstein src/main.c src/face.c src/mood.c src/platform_macos.m
                        src/llm.c src/chat.c src/text.c)
target_link_libraries(ornstein PRIVATE glad glfw curl)
```

## Main Loop Changes

```
poll inputs
  -> mood_update(dt)
  -> face_update(dt)
  -> llm_poll()  (new)
  -> render face (left portion)
  -> render chat panel (right portion)  (new)
```

Window layout: face takes left ~700px, chat panel takes right ~300px. When chat is hidden (default), face is centered as before.

## Lifecycle

1. **Startup**: ornstein spawns llama-server, waits for `/health` to return 200
2. **Runtime**: user types in chat, ornstein POSTs to `/v1/chat/completions`, streams response
3. **Shutdown**: ornstein sends SIGTERM to llama-server, waits for exit, then terminates

## Error Handling

- If llama-server fails to start (no model, crash): chat panel shows error, pet continues working without LLM
- If inference request fails: show error in chat, don't crash
- Graceful degradation: the pet works fine without llama.cpp -- the LLM is additive

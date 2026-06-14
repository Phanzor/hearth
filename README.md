# Hearth (terminal AI chat)

A terminal-native AI chat app in C++ - a private, local AI by your own fire,
inspired by PewDiePie's "Odysseus" but running entirely as a TUI. Built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI),
[cpp-httplib](https://github.com/yhirose/cpp-httplib), and
[nlohmann/json](https://github.com/nlohmann/json). It talks to a local
[Ollama](https://ollama.com) server and streams replies token-by-token.

## Build & run

Dependencies are fetched automatically by CMake; you only need a C++20 compiler
and CMake ≥ 3.20.

```bash
cmake -S . -B build
cmake --build build -j
./build/hearth
```

Make sure Ollama is running and you've pulled a model:

```bash
ollama serve          # if not already running
ollama pull qwen2.5:7b
```

## Keys

| Key | Action |
| --- | --- |
| Type + `Enter` | Send a message (Chat view) |
| `Tab` | Switch between Chat and Settings |
| `↑` / `↓` | Move between fields (Settings view) |
| `Enter` | Activate the Save button (Settings view) |
| `Esc` or `Ctrl+C` | Quit |

## Settings

The Settings view edits the Ollama **host** (base URL) and **model**. Saving
writes them to `~/.config/hearth/config.json` (honoring `XDG_CONFIG_HOME`),
which is loaded on startup. Changes take effect on the next message.

## Architecture

Deliberately modular so features can be added without entangling concerns:

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | Entry point: load config, create the screen, run the loop. |
| `src/app_state.h` | `AppState` - the single source of truth (messages, input, settings, flags). |
| `src/config.{h,cpp}` | Load/save settings as JSON under the XDG config dir. |
| `src/ollama.{h,cpp}` | Ollama client. `chat_stream` POSTs to `/api/chat` and parses the newline-delimited JSON stream, invoking a callback per token. |
| `src/ui.{h,cpp}` | All FTXUI components: chat view, settings view, tabs, key handling. |

### How streaming stays responsive

`ollama::chat_stream` blocks (it reads the HTTP response as it arrives), so the
chat view launches it on a worker thread. The worker never touches UI state
directly - each token is marshalled back onto the UI thread via
`ScreenInteractive::Post`, so there are no locks and no data races.

### Notes / current limitations

- HTTP only (no TLS). Fine for a local Ollama; a remote HTTPS host would need
  OpenSSL wired into cpp-httplib.
- The streaming worker is detached. Quitting mid-stream is fine in practice (the
  process exits), but a future version should track and cancel it for a clean
  shutdown.
- One conversation, held in memory. Persistence, multiple chats, model listing,
  and editing/regenerating are natural next features.

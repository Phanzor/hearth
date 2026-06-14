# Hearth

A terminal-native AI chat app in C++ - a private, local AI workspace solution,
inspired by PewDiePie's "Odysseus" but running entirely as a TUI. It talks to a
local [Ollama](https://ollama.com) server and streams replies token-by-token.

<!-- Drop a screenshot at docs/screenshot.png (or change the path below). -->
<p align="center">
  <img src="docs/screenshot.png" alt="Hearth - terminal AI chat" width="820">
</p>

Built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI),
[cpp-httplib](https://github.com/yhirose/cpp-httplib), and
[nlohmann/json](https://github.com/nlohmann/json).

## Features

- **Streaming chat** against Ollama's `/api/chat`, rendered Claude-Code style
  (your turn highlighted, the model's reply as plain prose).
- **Markdown** in replies: fenced code blocks, inline code, bold, headers, lists.
- **Slash commands** with a live, fuzzy-searched palette - `/help`, `/clear`,
  `/model`, `/quit`. Arrow keys to choose, Tab/Enter to autofill.
- **`/model` auto-populates** the models installed on the server (`/api/tags`),
  so you set the host once and pick a model from the list.
- **Scrollback** with the mouse wheel or PageUp/PageDown.
- **Token accounting** per session (prompt / reply / total).
- **Dark theme** backed by a small theme system; settings (host, model, theme)
  persist under `~/.config/hearth/`.

## Roadmap

Where this is headed - bringing the Odysseus ideas into the terminal. Rough
priority order; subject to change.

**Conversations & memory**
- [ ] Persist conversations to disk and reopen them
- [ ] Multiple chats you can switch between
- [ ] Long-term memory the model can recall across sessions

**Knowledge (RAG)**
- [ ] Index your own files/notes and chat over them (retrieval-augmented)
- [ ] Web search / fetch as a tool

**Multi-model**
- [ ] **Council mode** - several local models answer, deliberate, and a judge
      synthesizes the best response (the signature Odysseus feature)
- [ ] Per-model personas / system prompts

**Models & tools**
- [ ] Pull and manage Ollama models from inside the app
- [ ] Tool use / function calling (let the model take actions)
- [ ] Image input for multimodal models

**Quality of life**
- [ ] Stop / regenerate / edit messages
- [ ] More themes + an in-app theme picker
- [ ] Export a conversation to Markdown
- [ ] Voice in/out (speech-to-text, text-to-speech)

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

## Usage

| Key | Action |
| --- | --- |
| type + `Enter` | Send a message |
| `/` | Open the slash-command palette |
| `↑` / `↓` | Navigate (sidebar views, settings fields, or the palette) |
| `←` / `→` | Move between the sidebar and the active view |
| `Tab` | Autofill the highlighted palette item |
| mouse wheel / `PgUp` `PgDn` | Scroll the history |
| `Ctrl+C` | Quit |

### Slash commands

| Command | Action |
| --- | --- |
| `/help` | List available commands |
| `/clear` | Clear the conversation and reset the session token counts |
| `/model [name]` | Pick a model - lists what's installed; fuzzy-matches a name |
| `/quit` | Exit |

### Settings

The Settings view holds the Ollama **host** (base URL). Pick the model in chat
with `/model` instead - it lists what the server has installed. Saving writes to
`~/.config/hearth/config.json` (honoring `XDG_CONFIG_HOME`) and is loaded on
startup.

## Architecture

Deliberately modular so features can be added without entangling concerns:

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | Entry point: load config, create the screen, run the loop. |
| `src/app_state.h` | `AppState` - the single source of truth (messages, input, settings, flags). |
| `src/config.{h,cpp}` | Load/save settings as JSON under the XDG config dir. |
| `src/ollama.{h,cpp}` | Ollama client: streaming `/api/chat` and model listing via `/api/tags`. |
| `src/markdown.{h,cpp}` | Renders a Markdown subset into FTXUI elements. |
| `src/theme.{h,cpp}` | The `Theme` palette and the theme registry. |
| `src/ui.{h,cpp}` | All FTXUI components: chat, settings, sidebar, slash palette, key handling. |

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
- Markdown re-renders every frame; worth caching per message once conversations
  get long.

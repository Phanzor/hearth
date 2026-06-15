# Hearth

A terminal-native AI chat app in C++ - a private, local AI workspace solution,
inspired by PewDiePie's "Odysseus" but running entirely as a TUI. It talks to a
local [Ollama](https://ollama.com) server and streams replies token-by-token.

<!-- Drop a screenshot at docs/screenshot.png (or change the path below). -->
<p align="center">
  <img width="1045" height="659" alt="image" src="https://github.com/user-attachments/assets/35a374c2-bc20-4593-b2a1-1c292fb6b412" />
</p>


Built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI),
[cpp-httplib](https://github.com/yhirose/cpp-httplib), and
[nlohmann/json](https://github.com/nlohmann/json).

## Features

- **Streaming chat** against Ollama's `/api/chat`, rendered Claude-Code style
  (your turn highlighted, the model's reply as plain prose).
- **Multiple conversations** in a sidebar tree - start new chats, switch between
  them live, and **archive**, **delete**, or **rename** any of them through a
  confirming action menu. Every chat is saved to disk and reopens on next launch.
- **Markdown** in replies: fenced code blocks, inline code, bold, headers, lists.
- **Multi-line input** with a block cursor and soft-wrapping. `Shift+Enter` (on
  terminals that speak the extended keyboard protocol) or `Alt+Enter` inserts a
  newline; plain `Enter` sends.
- **Slash commands** with a live, fuzzy-searched palette - `/help`, `/delete`,
  `/archive`, `/model`, `/quit`. Arrow keys to choose, Tab/Enter to autofill.
- **`/model` auto-populates** the models installed on the server (`/api/tags`),
  so you set the host once and pick a model from the list.
- **Archive & export** - archived chats move out of the sidebar into Settings,
  where they can be exported to Markdown (individually or all at once).
- **Scrollback** with the mouse wheel or PageUp/PageDown.
- **Token accounting** per session (prompt / reply / total).
- **Context-aware key hints** - the footer always shows the keys relevant to
  whatever is currently focused.
- **Dark theme** backed by a small theme system. Settings (host, model, theme)
  persist under `~/.config/hearth/`; conversations under `~/.local/share/hearth/`
  (both honor the matching `XDG_*` variables).

## Roadmap

Where this is headed. Rough priority order; subject to change.

**Conversations & memory**
- [x] Persist conversations to disk and reopen them
- [x] Multiple chats you can switch between, plus archive / delete / rename
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

**Integrations**
- [ ] Embedded terminal panes - run subscription CLI agents
      (Claude Code, OpenAI Codex, etc.) inside Hearth via a PTY, so local and
      subscription assistants live in one app

**Quality of life**
- [x] Multi-line message input (`Shift+Enter` / `Alt+Enter`)
- [x] Export a conversation to Markdown
- [ ] Stop / regenerate / edit messages
- [ ] More themes + an in-app theme picker
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
| `Shift+Enter` / `Alt+Enter` | Insert a newline (multi-line message) |
| `Ctrl+Shift+V` | Paste (multi-line safe via bracketed paste) |
| `/` | Open the slash-command palette |
| `↑` / `↓` | Navigate (sidebar rows, settings fields, or the palette) |
| `←` / `→` | Move between the sidebar and the active view |
| `Enter` (on a sidebar chat) | Open its action menu (delete / archive / rename) |
| `Tab` | Autofill the highlighted palette item |
| drag-select with the mouse | Copy the selection to the clipboard (OSC 52) |
| mouse wheel / `PgUp` `PgDn` | Scroll the history |
| `Ctrl+C` | Quit |

To **copy** text out, just select it with the mouse: on release the selection
is copied to your system clipboard (via OSC 52), ready to paste with
`Ctrl+Shift+V`. No `Ctrl+Shift+C` needed - Hearth captures the mouse for
scrolling, so it copies the selection itself rather than leaving it to the
terminal. (Your terminal must allow OSC 52 clipboard writes, which most do.)

### Slash commands

| Command | Action |
| --- | --- |
| `/help` | List available commands |
| `/delete` | Delete the current chat and return to a blank draft |
| `/archive` | Archive the current chat (manage it later from Settings) |
| `/model [name]` | Pick a model - lists what's installed; fuzzy-matches a name |
| `/quit` | Exit |

### Settings

The Settings view holds the Ollama **host** (base URL). Pick the model in chat
with `/model` instead - it lists what the server has installed. Saving writes to
`~/.config/hearth/config.json` (honoring `XDG_CONFIG_HOME`) and is loaded on
startup.

Settings also lists your **archived chats**: select one to export, delete, or
rename it, or export them all to Markdown at once.

## Architecture

Deliberately modular so features can be added without entangling concerns:

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | Entry point: load config, load saved chats, create the screen, run the loop. |
| `src/app_state.h` | `AppState` - the single source of truth (conversations, input, settings, popups, flags). |
| `src/config.{h,cpp}` | Load/save settings as JSON under the XDG config dir. |
| `src/storage.{h,cpp}` | Persist chats as JSON under the XDG data dir (active + archived) and export to Markdown. |
| `src/ollama.{h,cpp}` | Ollama client: streaming `/api/chat` and model listing via `/api/tags`. |
| `src/markdown.{h,cpp}` | Renders a Markdown subset into FTXUI elements. |
| `src/theme.{h,cpp}` | The `Theme` palette and the theme registry. |
| `src/ui.{h,cpp}` | All FTXUI components: chat, settings, sidebar, slash palette, key handling. |

### How streaming stays responsive

`ollama::chat_stream` blocks (it reads the HTTP response as it arrives), so the
chat view launches it on a worker thread. The worker never touches UI state
directly - each token is marshalled back onto the UI thread via
`ScreenInteractive::Post`, so there are no locks and no data races. Workers
target the chat by its stable id, so you can switch, archive, or delete
conversations while a reply is still streaming.

### Notes / current limitations

- HTTP only (no TLS). Fine for a local Ollama; a remote HTTPS host would need
  OpenSSL wired into cpp-httplib.
- The streaming worker is detached. Quitting mid-stream is fine in practice (the
  process exits), but a future version should track and cancel it for a clean
  shutdown.
- Markdown re-renders every frame; worth caching per message once conversations
  get long.

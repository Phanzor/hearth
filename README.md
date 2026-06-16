# Hearth

A terminal-native AI chat app in C++ - a private, local-first AI workspace,
inspired by PewDiePie's "Odysseus" but running entirely as a TUI. It talks to a
local [Ollama](https://ollama.com) server or the major cloud APIs (OpenAI,
Anthropic, Gemini, Grok) and streams replies token-by-token.

<!-- Drop a screenshot at docs/screenshot.png (or change the path below). -->
<p align="center">
  <img width="1045" height="659" alt="image" src="https://github.com/user-attachments/assets/35a374c2-bc20-4593-b2a1-1c292fb6b412" />
</p>


Built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI),
[cpp-httplib](https://github.com/yhirose/cpp-httplib), and
[nlohmann/json](https://github.com/nlohmann/json).

## Features

- **Streaming chat** against a local Ollama server or the cloud APIs, rendered
  Claude-Code style (your turn highlighted, the model's reply as plain prose).
- **Multiple providers** - Ollama (local), OpenAI, Anthropic, Gemini, Grok (xAI),
  and any OpenAI-compatible endpoint (OpenRouter, Groq, Mistral, ...). Fill in the
  connections you use in Settings; keys are stored locally.
- **Per-chat models** - every conversation remembers its own provider and model,
  so one chat can run on local Llama while another runs on Claude or Grok. Switch
  with `/model <type> <model>`; the palette lists every model across all your
  configured connections.
- **Grok subscriptions** - sign in to a SuperGrok / X Premium+ subscription with
  native browser OAuth (no API key); the token is cached and refreshed for you.
- **Multiple conversations** in a sidebar tree - start new chats, switch between
  them live, and **archive**, **delete**, or **rename** any of them through a
  confirming action menu. Every chat is saved to disk and reopens on next launch.
- **Markdown** in replies: fenced code blocks, inline code, bold, headers, lists.
- **Multi-line input** with a block cursor and soft-wrapping. `Shift+Enter` (on
  terminals that speak the extended keyboard protocol) or `Alt+Enter` inserts a
  newline; plain `Enter` sends.
- **Slash commands** with a live, fuzzy-searched palette - `/help`, `/delete`,
  `/archive`, `/model`, `/quit`. Arrow keys to choose, Tab/Enter to autofill.
- **Global system prompt** - an optional instruction prepended to every chat,
  with an on/off toggle (Settings -> General).
- **Archive & export** - archived chats move out of the sidebar into Settings,
  where they can be exported to Markdown (individually or all at once).
- **Scrollback** with the mouse wheel or PageUp/PageDown.
- **Token accounting** per session (prompt / reply / total).
- **Context-aware key hints** - the footer always shows the keys relevant to
  whatever is currently focused.
- **Themes** - 20 built-in palettes plus an in-app editor for your own, previewed
  live across the whole UI. Settings (connections, default model, theme) persist
  under `~/.config/hearth/`; conversations under `~/.local/share/hearth/` (both
  honor the matching `XDG_*` variables).

## Roadmap

Where this is headed. Rough priority order; subject to change.

**Providers & models**
- [x] Local Ollama plus cloud APIs: OpenAI, Anthropic, Gemini, Grok (xAI), and
      any OpenAI-compatible endpoint
- [x] Per-chat model selection (`/model <type> <model>`) - each chat remembers
      its own provider and model
- [x] Grok subscriptions via native OAuth (no API key)

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
- [x] More themes + an in-app theme picker (20 built-ins + a custom editor)
- [ ] Stop / regenerate / edit messages
- [ ] Voice in/out (speech-to-text, text-to-speech)

## Build & run

Dependencies are fetched automatically by CMake; you need a C++20 compiler,
CMake ≥ 3.20, and OpenSSL (used by cpp-httplib for HTTPS to the cloud providers).

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
| `/model [type] [model]` | Set this chat's model. `/model` lists everything; `/model <type>` narrows to a provider; `/model <type> <model>` switches |
| `/quit` | Exit |

### Settings

Settings is split into four sections in the sidebar:

- **General** - a quick overview plus the global system prompt (with an on/off
  toggle).
- **Connections** - every provider in one place: the Ollama host, API keys for
  OpenAI, Anthropic, Gemini, Grok, and a custom OpenAI-compatible endpoint. The
  **Grok (subscription)** row signs in via OAuth (no key needed). Fill in whatever
  you use, then pick a per-chat model in chat with `/model <type> <model>`.
- **Themes** - an in-app picker over 20 built-ins, plus an editor to create your
  own (applied live as you tweak it).
- **Archive** - your archived chats: export, delete, or rename one, or export
  them all to Markdown at once.

Settings are saved to `~/.config/hearth/config.json` (honoring `XDG_CONFIG_HOME`)
and loaded on startup; Grok OAuth tokens live alongside it in `grok_oauth.json`.

## Architecture

Deliberately modular so features can be added without entangling concerns:

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | Entry point: load config, load saved chats, create the screen, run the loop. |
| `src/app_state.h` | `AppState` - the single source of truth (conversations, input, settings, popups, flags). |
| `src/config.{h,cpp}` | Load/save settings as JSON under the XDG config dir. |
| `src/storage.{h,cpp}` | Persist chats as JSON under the XDG data dir (active + archived) and export to Markdown. |
| `src/ollama.{h,cpp}` | Native Ollama transport: streaming `/api/chat` and model listing via `/api/tags`. |
| `src/providers.{h,cpp}` | Provider abstraction: dispatches chat + model listing across Ollama, OpenAI, Anthropic, Gemini, Grok, and OpenAI-compatible endpoints. |
| `src/grok_oauth.{h,cpp}` | Native xAI OAuth (PKCE + loopback callback) for Grok subscriptions; token storage and refresh. |
| `src/markdown.{h,cpp}` | Renders a Markdown subset into FTXUI elements. |
| `src/theme.{h,cpp}` | The `Theme` palette and the theme registry. |
| `src/ui.{h,cpp}` | All FTXUI components: chat, settings, sidebar, slash palette, key handling. |

### How streaming stays responsive

`provider::chat_stream` blocks (it reads the HTTP response as it arrives), so the
chat view launches it on a worker thread. The worker never touches UI state
directly - each token is marshalled back onto the UI thread via
`ScreenInteractive::Post`, so there are no locks and no data races. Workers
target the chat by its stable id, so you can switch, archive, or delete
conversations while a reply is still streaming. (Cloud providers each have their
own request/SSE shape behind `provider::chat_stream`; Anthropic and Gemini place
the system prompt and roles where their APIs expect them.)

### Notes / current limitations

- The cloud providers use HTTPS (cpp-httplib + OpenSSL); the local Ollama host
  can stay plain HTTP.
- Grok subscription chat currently goes to xAI's `/v1/chat/completions` with the
  OAuth token. If xAI only honors a subscription token on its Responses API, that
  path would need a dedicated `/v1/responses` transport.
- The streaming worker is detached. Quitting mid-stream is fine in practice (the
  process exits), but a future version should track and cancel it for a clean
  shutdown.
- Markdown re-renders every frame; worth caching per message once conversations
  get long.

## License

Hearth is released under the [MIT License](LICENSE) - do whatever you like with
it, just keep the copyright and license notice. Copyright (c) 2026 phanzor.

The Grok subscription sign-in (`src/grok_oauth.*`) adapts the xAI OAuth flow from
[Hermes Agent](https://github.com/NousResearch/hermes-agent) by Nous Research,
which is MIT-licensed.

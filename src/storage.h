#pragma once

#include <string>
#include <vector>

#include "app_state.h"

// On-disk persistence for conversations, under the XDG data dir
// (~/.local/share/hearth by default): active chats in conversations/, archived
// chats in archived/, one <id>.json each.
namespace storage {

std::string new_id();

// Load active and archived chats from disk (sorted oldest-first by id).
void load(std::vector<Conversation>& active, std::vector<Conversation>& archived);

// Write one chat to conversations/ (archived=false) or archived/ (true).
void save(const Conversation& c, bool archived);

// Delete a chat's file from both locations.
void erase(const std::string& id);

// Write a chat to a Markdown file under the export dir; returns the path
// written, or an empty string on failure.
std::string export_markdown(const Conversation& c);

std::string export_dir();

} // namespace storage

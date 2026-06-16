#pragma once

#include <functional>
#include <string>

// Native browser OAuth (PKCE) against xAI's accounts server, so a SuperGrok /
// X Premium+ subscription can be used without an API key or an external proxy.
// Ported from Nous Research's MIT-licensed Hermes Agent (hermes_cli/auth.py):
// same client_id, scopes, loopback redirect and refresh behaviour. Tokens are
// cached next to the Hearth config and refreshed transparently.
namespace grok_oauth {

// True once a refresh token has been stored (i.e. the user has logged in).
bool logged_in();

// Forget the stored tokens.
void logout();

// Run the full browser OAuth flow: opens the system browser, serves the loopback
// callback, exchanges the code, and stores the tokens. Blocking (up to a couple
// of minutes) - run on a worker thread. `progress` reports human-readable steps;
// the caller marshals them to the UI. Returns "" on success, else an error.
std::string login(const std::function<void(const std::string&)>& progress);

// A currently-valid access token, refreshing it first if it is near expiry.
// Blocking (may hit the network) - run on a worker thread. Returns "" if not
// logged in or the refresh failed.
std::string access_token();

} // namespace grok_oauth

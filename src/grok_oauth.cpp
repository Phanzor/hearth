#include "grok_oauth.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "config.h"

namespace fs = std::filesystem;

namespace grok_oauth {

// Constants lifted from Hermes Agent (hermes_cli/auth.py).
static const char* kIssuer = "https://auth.x.ai";
static const char* kClientId = "b1a00492-073a-47ea-816f-4c329264a828";
static const char* kScope = "openid profile email offline_access grok-cli:access api:access";
static const char* kRedirect = "http://127.0.0.1:56121/callback";
static const int kRedirectPort = 56121;
static const long kRefreshSkew = 3600;  // refresh an hour early (tokens are short-lived)

// --- small helpers ----------------------------------------------------------

static std::string b64url(const unsigned char* data, size_t len) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    for (size_t i = 0; i < len; i += 3) {
        unsigned v = static_cast<unsigned>(data[i]) << 16;
        int n = 1;
        if (i + 1 < len) { v |= static_cast<unsigned>(data[i + 1]) << 8; n = 2; }
        if (i + 2 < len) { v |= static_cast<unsigned>(data[i + 2]); n = 3; }
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        if (n >= 2) out.push_back(tbl[(v >> 6) & 0x3F]);
        if (n >= 3) out.push_back(tbl[v & 0x3F]);
    }
    return out;  // base64url, no padding
}

static std::string sha256_b64url(const std::string& s) {
    unsigned char h[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(s.data(), s.size(), h, &len, EVP_sha256(), nullptr);
    return b64url(h, len);
}

static std::string random_b64url(size_t nbytes) {
    std::vector<unsigned char> buf(nbytes);
    std::random_device rd;
    for (auto& b : buf) {
        b = static_cast<unsigned char>(rd() & 0xFF);
    }
    return b64url(buf.data(), buf.size());
}

static std::string urlenc(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

// Split "https://host/p/q" into ("https://host", "/p/q") for httplib::Client.
static std::pair<std::string, std::string> split_url(const std::string& url) {
    const auto p = url.find("://");
    if (p == std::string::npos) return {url, "/"};
    const auto slash = url.find('/', p + 3);
    if (slash == std::string::npos) return {url, "/"};
    return {url.substr(0, slash), url.substr(slash)};
}

static long now_epoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

static void open_browser(const std::string& url) {
    // url is percent-encoded, so single-quoting is safe.
    const std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    std::system(cmd.c_str());
}

// --- token store ------------------------------------------------------------

struct Store {
    std::string access;
    std::string refresh;
    std::string token_endpoint;
    long expires_at = 0;
};

static fs::path store_path() {
    return fs::path(config_path()).parent_path() / "grok_oauth.json";
}

static Store load_store() {
    Store s;
    std::ifstream in(store_path());
    if (!in) return s;
    try {
        nlohmann::json j;
        in >> j;
        s.access = j.value("access_token", "");
        s.refresh = j.value("refresh_token", "");
        s.token_endpoint = j.value("token_endpoint", "");
        s.expires_at = j.value("expires_at", 0L);
    } catch (const std::exception&) {
    }
    return s;
}

static void save_store(const Store& s) {
    try {
        fs::create_directories(store_path().parent_path());
        nlohmann::json j{{"access_token", s.access},
                         {"refresh_token", s.refresh},
                         {"token_endpoint", s.token_endpoint},
                         {"expires_at", s.expires_at}};
        std::ofstream out(store_path());
        out << j.dump(2);
    } catch (const std::exception&) {
    }
}

static std::string token_error(const std::string& body, int status) {
    try {
        auto j = nlohmann::json::parse(body);
        const std::string d = j.value("error_description", "");
        if (!d.empty()) return "token error: " + d;
        const std::string e = j.value("error", "");
        if (!e.empty()) return "token error: " + e;
    } catch (const std::exception&) {
    }
    return "token error: HTTP " + std::to_string(status);
}

// OIDC discovery, falling back to the conventional paths under the issuer.
static void discover(std::string& auth_ep, std::string& token_ep) {
    auth_ep = std::string(kIssuer) + "/oauth2/authorize";
    token_ep = std::string(kIssuer) + "/oauth2/token";
    httplib::Client cli(kIssuer);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(10, 0);
    auto res = cli.Get("/.well-known/openid-configuration");
    if (res && res->status >= 200 && res->status < 300) {
        try {
            auto j = nlohmann::json::parse(res->body);
            if (j.contains("authorization_endpoint")) {
                auth_ep = j["authorization_endpoint"].get<std::string>();
            }
            if (j.contains("token_endpoint")) {
                token_ep = j["token_endpoint"].get<std::string>();
            }
        } catch (const std::exception&) {
        }
    }
}

// --- public API -------------------------------------------------------------

bool logged_in() {
    return !load_store().refresh.empty();
}

void logout() {
    std::error_code ec;
    fs::remove(store_path(), ec);
}

std::string login(const std::function<void(const std::string&)>& progress) {
    progress("contacting xAI...");
    std::string auth_ep, token_ep;
    discover(auth_ep, token_ep);

    const std::string verifier = random_b64url(32);
    const std::string challenge = sha256_b64url(verifier);
    const std::string state = random_b64url(16);

    const std::string url = auth_ep + "?response_type=code" +
                            "&client_id=" + urlenc(kClientId) +
                            "&redirect_uri=" + urlenc(kRedirect) +
                            "&scope=" + urlenc(kScope) +
                            "&code_challenge=" + challenge +
                            "&code_challenge_method=S256" +
                            "&state=" + state;

    // Loopback server that catches the OAuth redirect.
    httplib::Server svr;
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    std::string code, got_state, cb_err;
    svr.Get("/callback", [&](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("error")) cb_err = req.get_param_value("error");
        if (req.has_param("code")) code = req.get_param_value("code");
        if (req.has_param("state")) got_state = req.get_param_value("state");
        res.set_content(
            "<html><body style='font-family:sans-serif;padding:2rem'>"
            "<h3>Hearth is connected to Grok.</h3>"
            "You can close this tab and return to the terminal.</body></html>",
            "text/html");
        {
            std::lock_guard<std::mutex> lk(m);
            done = true;
        }
        cv.notify_one();
    });

    if (!svr.bind_to_port("127.0.0.1", kRedirectPort)) {
        return "could not bind 127.0.0.1:56121 (another login or Hermes running?)";
    }
    std::thread srv([&] { svr.listen_after_bind(); });

    progress("opening browser - approve the sign-in...");
    open_browser(url);

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, std::chrono::seconds(180), [&] { return done; });
    }
    svr.stop();
    srv.join();

    if (!done) return "timed out waiting for sign-in";
    if (!cb_err.empty()) return "sign-in declined: " + cb_err;
    if (code.empty()) return "no authorization code received";
    if (got_state != state) return "state mismatch - sign-in aborted";

    progress("finishing sign-in...");
    auto [base, path] = split_url(token_ep);
    httplib::Client cli(base);
    cli.set_connection_timeout(15, 0);
    cli.set_read_timeout(30, 0);
    httplib::Params params{{"grant_type", "authorization_code"},
                           {"code", code},
                           {"redirect_uri", kRedirect},
                           {"client_id", kClientId},
                           {"code_verifier", verifier}};
    auto res = cli.Post(path, params);
    if (!res) {
        return "token request failed: " + httplib::to_string(res.error());
    }
    if (res->status < 200 || res->status >= 300) {
        return token_error(res->body, res->status);
    }
    try {
        auto j = nlohmann::json::parse(res->body);
        Store s;
        s.access = j.value("access_token", "");
        s.refresh = j.value("refresh_token", "");
        s.token_endpoint = token_ep;
        s.expires_at = now_epoch() + j.value("expires_in", 3600L);
        if (s.access.empty() || s.refresh.empty()) {
            return "token response was missing tokens";
        }
        save_store(s);
    } catch (const std::exception& e) {
        return std::string("bad token response: ") + e.what();
    }
    progress("signed in to Grok");
    return "";
}

std::string access_token() {
    Store s = load_store();
    if (s.refresh.empty()) {
        return "";
    }
    if (!s.access.empty() && now_epoch() < s.expires_at - kRefreshSkew) {
        return s.access;  // still fresh
    }

    const std::string token_ep =
        s.token_endpoint.empty() ? (std::string(kIssuer) + "/oauth2/token") : s.token_endpoint;
    auto [base, path] = split_url(token_ep);
    httplib::Client cli(base);
    cli.set_connection_timeout(15, 0);
    cli.set_read_timeout(30, 0);
    httplib::Params params{{"grant_type", "refresh_token"},
                           {"refresh_token", s.refresh},
                           {"client_id", kClientId}};
    auto res = cli.Post(path, params);
    if (!res || res->status < 200 || res->status >= 300) {
        if (res) {
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.value("error", "") == "invalid_grant") {
                    logout();  // refresh token is dead; force a fresh login
                }
            } catch (const std::exception&) {
            }
        }
        return "";
    }
    try {
        auto j = nlohmann::json::parse(res->body);
        s.access = j.value("access_token", s.access);
        if (j.contains("refresh_token")) {
            s.refresh = j["refresh_token"].get<std::string>();
        }
        s.expires_at = now_epoch() + j.value("expires_in", 3600L);
        save_store(s);
        return s.access;
    } catch (const std::exception&) {
        return "";
    }
}

} // namespace grok_oauth

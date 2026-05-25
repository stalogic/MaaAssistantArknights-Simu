// Copyright (c) MaaAssistantArknights Contributors
// SPDX-License-Identifier: AGPL-3.0-only

#include "AiBridge.h"

#include <cctype>
#include <chrono>
#include <sstream>

#include <boost/asio.hpp>
#include <meojson/json.hpp>

#include "Utils/Logger.hpp"

namespace asst
{
// --- Constants ---

constexpr int kDefaultPort = 8765;
constexpr int kHttpTimeoutMs = 5000;
constexpr const char* kActPath = "/api/act";
constexpr const char* kHealthPath = "/api/health";

// --- Singleton ---

AiBridge& AiBridge::instance()
{
    static AiBridge s_instance;
    return s_instance;
}

// --- Endpoint ---

void AiBridge::set_endpoint(const std::string& url)
{
    m_endpoint = url;
    m_enabled = !url.empty();
    Log.info("AiBridge endpoint:", url.empty() ? "(disabled)" : url);
}

bool AiBridge::is_enabled() const
{
    return m_enabled;
}

bool AiBridge::parse_endpoint(std::string& host, int& port) const
{
    if (m_endpoint.empty()) return false;

    std::string url = m_endpoint;

    // Strip protocol prefix
    if (url.starts_with("http://"))
        url = url.substr(7);
    else if (url.starts_with("https://"))
        url = url.substr(8);

    // Strip trailing path (everything after first '/')
    auto slash = url.find('/');
    if (slash != std::string::npos)
        url = url.substr(0, slash);

    // Parse host:port
    auto colon = url.find(':');
    if (colon != std::string::npos) {
        host = url.substr(0, colon);
        port = std::stoi(url.substr(colon + 1));
    }
    else {
        host = url;
        port = kDefaultPort;
    }

    return !host.empty();
}

// --- Health check ---

bool AiBridge::check_health()
{
    if (!m_enabled) return false;

    try {
        std::string host;
        int port = kDefaultPort;
        if (!parse_endpoint(host, port)) return false;

        std::string response = http_get(host, port, kHealthPath);
        if (response.empty()) {
            Log.warn("AiBridge::check_health empty response from", host, ":", port);
            return false;
        }

        std::string body = response;
        auto body_start = body.find("\r\n\r\n");
        if (body_start != std::string::npos)
            body = body.substr(body_start + 4);

        Log.trace("AiBridge::check_health body:", body);
        return body.find("\"ok\"") != std::string::npos ||
               body.find("\"status\":\"ok\"") != std::string::npos;
    }
    catch (const std::exception& e) {
        Log.warn("AiBridge::check_health exception:", e.what());
        return false;
    }
}

// --- Recruitment query ---

std::string AiBridge::query_recruit_decision(
    const cv::Mat& screenshot,
    const std::vector<RecruitCandidate>& candidates,
    const std::string& theme,
    int floor,
    int hope,
    int hp)
{
    if (!m_enabled || candidates.empty()) return {};

    try {
        std::string host;
        int port = kDefaultPort;
        if (!parse_endpoint(host, port)) return {};

        std::string screenshot_b64 = mat_to_base64_png(screenshot);
        std::string json_body = build_request_json(
            candidates, theme, floor, hope, hp, screenshot_b64);

        std::string response = http_post(host, port, kActPath, json_body);
        if (response.empty()) {
            Log.warn("AiBridge: empty response from", host, ":", port);
            return {};
        }

        auto body_start = response.find("\r\n\r\n");
        if (body_start == std::string::npos) return {};
        std::string body = response.substr(body_start + 4);

        auto j_opt = json::parse(body);
        if (!j_opt) {
            Log.warn("AiBridge: invalid JSON response");
            return {};
        }

        const auto& root = j_opt.value();
        if (!root.contains("chosen_operator")) {
            Log.warn("AiBridge: missing chosen_operator in response");
            return {};
        }

        std::string chosen = root.at("chosen_operator").as_string();
        Log.info("AiBridge: chose", chosen, "from", candidates.size(), "candidates");
        return chosen;
    }
    catch (const std::exception& e) {
        Log.error("AiBridge::query_recruit_decision exception:", e.what());
        return {};
    }
}

// --- JSON building (meojson) ---

std::string AiBridge::build_request_json(
    const std::vector<RecruitCandidate>& candidates,
    const std::string& theme,
    int floor,
    int hope,
    int hp,
    const std::string& screenshot_b64)
{
    json::array candidates_arr;
    for (const auto& c : candidates) {
        candidates_arr.emplace_back(json::object{
            { "name", c.name },
            { "elite", c.elite },
            { "level", c.level },
            { "priority", c.priority },
            { "is_alternate", c.is_alternate },
        });
    }

    json::value j = json::object{
        { "screenshot_b64", screenshot_b64 },
        { "candidates", std::move(candidates_arr) },
        { "state", json::object{
            { "theme", theme },
            { "floor", floor },
            { "hope", hope },
            { "hp", hp },
            { "own_operators", json::array{} },
        }},
    };

    return j.to_string();
}

// --- Base64 encoding ---

std::string AiBridge::mat_to_base64_png(const cv::Mat& img)
{
    std::vector<uchar> buf;
    cv::imencode(".png", img, buf);

    static const char* kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    result.reserve((buf.size() + 2) / 3 * 4);

    size_t i = 0;
    for (; i + 2 < buf.size(); i += 3) {
        uint32_t t = (static_cast<uint32_t>(buf[i]) << 16) |
                     (static_cast<uint32_t>(buf[i + 1]) << 8) |
                     static_cast<uint32_t>(buf[i + 2]);
        result += kTable[(t >> 18) & 0x3F];
        result += kTable[(t >> 12) & 0x3F];
        result += kTable[(t >> 6) & 0x3F];
        result += kTable[t & 0x3F];
    }

    if (i < buf.size()) {
        uint32_t t = static_cast<uint32_t>(buf[i]) << 16;
        if (i + 1 < buf.size()) t |= static_cast<uint32_t>(buf[i + 1]) << 8;
        result += kTable[(t >> 18) & 0x3F];
        result += kTable[(t >> 12) & 0x3F];
        result += (i + 1 < buf.size()) ? kTable[(t >> 6) & 0x3F] : '=';
        result += '=';
    }

    return result;
}

// --- Low-level helpers ---

// On Windows, DNS resolution of "localhost" goes through the full DNS query
// chain (~1s). Replace it with the loopback IP to bypass this overhead.
static std::string resolve_host(const std::string& host)
{
    if (host == "localhost") return "127.0.0.1";
    return host;
}

static int extract_content_length(const std::string& headers)
{
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto pos = lower.find("content-length:");
    if (pos == std::string::npos) return -1;
    auto end = headers.find("\r\n", pos);
    auto val = headers.substr(pos + 15, end - pos - 15);
    size_t start = 0;
    while (start < val.size() && val[start] == ' ') start++;
    return std::stoi(val.substr(start));
}

static int extract_status_code(const std::string& response)
{
    auto space1 = response.find(' ');
    if (space1 == std::string::npos) return -1;
    auto space2 = response.find(' ', space1 + 1);
    if (space2 == std::string::npos) {
        // No reason phrase, fall back to \r
        space2 = response.find('\r', space1 + 1);
        if (space2 == std::string::npos) return -1;
    }
    std::string code_str = response.substr(space1 + 1, space2 - space1 - 1);
    try { return std::stoi(code_str); }
    catch (...) { return -1; }
}

static void set_socket_timeout(boost::asio::ip::tcp::socket& socket, int timeout_ms)
{
    boost::system::error_code ec;
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static std::string read_http_response(boost::asio::ip::tcp::socket& socket)
{
    try {
        boost::asio::streambuf buf;
        boost::system::error_code ec;

        boost::asio::read_until(socket, buf, "\r\n\r\n", ec);
        if (ec) {
            Log.warn("AiBridge read_until:", ec.message());
            return {};
        }

        std::string response;
        auto bufs = buf.data();
        response.append(boost::asio::buffers_begin(bufs), boost::asio::buffers_end(bufs));
        buf.consume(buf.size());

        int status = extract_status_code(response);
        if (status != 200) {
            Log.warn("AiBridge HTTP status:", status);
            return {};
        }

        int content_length = extract_content_length(response);
        if (content_length > 0) {
            size_t header_end = response.find("\r\n\r\n") + 4;
            size_t body_so_far = response.size() - header_end;
            size_t remaining = content_length - body_so_far;

            if (remaining > 0) {
                boost::asio::read(socket, buf, boost::asio::transfer_exactly(remaining), ec);
                if (ec) Log.trace("AiBridge body read:", ec.message());
                auto body_bufs = buf.data();
                response.append(boost::asio::buffers_begin(body_bufs), boost::asio::buffers_end(body_bufs));
            }
        }

        return response;
    }
    catch (...) {
        Log.warn("AiBridge read_http_response exception");
        return {};
    }
}

// --- HTTP GET / POST ---

static std::string http_impl(
    const std::string& host, int port,
    const std::string& path, const std::string& body)
{
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::socket socket(ioc);
    boost::asio::ip::tcp::resolver resolver(ioc);

    boost::system::error_code ec;
    auto endpoints = resolver.resolve(resolve_host(host), std::to_string(port), ec);
    if (ec) {
        Log.warn("AiBridge resolve:", ec.message());
        return {};
    }

    boost::asio::connect(socket, endpoints, ec);
    if (ec) {
        Log.warn("AiBridge connect:", ec.message());
        return {};
    }

    socket.set_option(boost::asio::ip::tcp::no_delay(true));
    set_socket_timeout(socket, kHttpTimeoutMs);

    std::ostringstream request;
    bool is_post = !body.empty();
    request << (is_post ? "POST " : "GET ") << path << " HTTP/1.1\r\n"
            << "Host: " << host << ":" << port << "\r\n";
    if (is_post) {
        request << "Content-Type: application/json\r\n"
                << "Content-Length: " << body.size() << "\r\n";
    }
    request << "Connection: close\r\n"
            << "\r\n";
    if (is_post) request << body;

    std::string req_str = request.str();
    boost::asio::write(socket, boost::asio::buffer(req_str), ec);
    if (ec) {
        Log.warn("AiBridge write:", ec.message());
        socket.close();
        return {};
    }

    return read_http_response(socket);
}

std::string AiBridge::http_post(
    const std::string& host, int port,
    const std::string& path, const std::string& json_body)
{
    return http_impl(host, port, path, json_body);
}

std::string AiBridge::http_get(
    const std::string& host, int port, const std::string& path)
{
    return http_impl(host, port, path, "");
}
}

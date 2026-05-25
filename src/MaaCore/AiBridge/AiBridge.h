// Copyright (c) MaaAssistantArknights Contributors
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>
#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

namespace asst
{
struct RecruitCandidate {
    std::string name;
    int elite = 0;
    int level = 0;
    int priority = 0;
    bool is_alternate = false;
};

class AiBridge
{
public:
    static AiBridge& instance();

    void set_endpoint(const std::string& url);
    bool is_enabled() const;
    bool check_health();

    std::string query_recruit_decision(
        const cv::Mat& screenshot,
        const std::vector<RecruitCandidate>& candidates,
        const std::string& theme,
        int floor,
        int hope,
        int hp);

private:
    AiBridge() = default;

    bool parse_endpoint(std::string& host, int& port) const;
    std::string mat_to_base64_png(const cv::Mat& img);
    std::string build_request_json(
        const std::vector<RecruitCandidate>& candidates,
        const std::string& theme,
        int floor,
        int hope,
        int hp,
        const std::string& screenshot_b64);
    std::string http_post(const std::string& host, int port,
                          const std::string& path, const std::string& json_body);
    std::string http_get(const std::string& host, int port, const std::string& path);

    std::string m_endpoint;
    bool m_enabled = false;
};
}

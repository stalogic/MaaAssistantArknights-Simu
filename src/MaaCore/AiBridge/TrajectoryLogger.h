// Copyright (c) MaaAssistantArknights Contributors
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <opencv2/core.hpp>

namespace asst
{
struct RecruitCandidate;

class TrajectoryLogger
{
public:
    static TrajectoryLogger& instance();

    void set_enabled(bool v) { m_enabled = v; }
    bool is_enabled() const { return m_enabled; }

    void start_session(const std::string& base_dir, const std::string& theme);
    void end_session();

    void log_recruit(
        const cv::Mat& screenshot,
        const std::vector<RecruitCandidate>& candidates,
        const std::string& chosen_operator,
        const std::string& action_text,
        bool ai_used,
        const std::string& ai_chosen,
        const std::string& extra_params_json);

    void log_generic(
        const cv::Mat& screenshot,
        const std::string& task_type,
        const std::string& action_json,
        const std::string& action_text,
        bool ai_used,
        const std::string& ai_chosen,
        const std::string& extra_params_json);

private:
    TrajectoryLogger() = default;

    std::string save_screenshot(const cv::Mat& img, const std::string& prefix, int seq);
    std::string make_timestamp() const;
    std::string build_record(
        const std::string& task_type,
        const std::string& img_rel,
        const std::string& action_json,
        const std::string& action_text,
        bool ai_used,
        const std::string& ai_chosen,
        const std::string& extra_params_json);

    std::string m_episode_id;
    std::string m_session_dir;
    std::string m_images_dir;
    std::ofstream m_jsonl;
    std::mutex m_mutex;
    int m_step = 0;
    bool m_enabled = false;
};
}

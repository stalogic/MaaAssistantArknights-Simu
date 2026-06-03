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

    // Set the current roguelike state for auto-filling extra_params and reward computation.
    // Called by plugins before each decision point.
    void set_roguelike_state(int floor, int hope, int hp,
                             const std::string& theme, int mode, int difficulty,
                             const std::string& squad, int formation_limit);

    // Set battle-specific context (DP, kills, stage name, deployment slots)
    void set_battle_context(int dp, int kills, int deployed_count, int remaining_slots,
                            int squad_size, const std::string& stage);

    void log_recruit(
        const cv::Mat& screenshot,
        const std::vector<RecruitCandidate>& candidates,
        const std::string& chosen_operator,
        const std::string& action_text,
        bool ai_used,
        const std::string& ai_chosen);

    void log_generic(
        const cv::Mat& screenshot,
        const std::string& task_type,
        const std::string& action_json,
        const std::string& action_text,
        bool ai_used,
        const std::string& ai_chosen,
        bool done = false,
        int reward = 0);

private:
    TrajectoryLogger() = default;

    std::string save_screenshot(const cv::Mat& img, const std::string& prefix, int seq);
    std::string make_timestamp() const;
    std::string build_record(
        const std::string& task_type, const std::string& img_rel,
        const std::string& action_json, const std::string& action_text,
        bool ai_used, const std::string& ai_chosen, bool done, int reward);
    int compute_reward(int floor, int hp, const std::string& task_type);
    std::string state_to_json() const;

    std::string m_episode_id;
    std::string m_session_dir;
    std::string m_images_dir;
    std::ofstream m_jsonl;
    std::mutex m_mutex;
    int m_step = 0;
    bool m_enabled = false;

    // State tracking for reward computation
    int m_floor = 0;
    int m_hope = 0;
    int m_hp = 0;
    std::string m_theme;
    int m_mode = 0;
    int m_difficulty = 0;
    std::string m_squad;
    int m_formation_limit = 6;

    // Battle context
    int m_dp = 0;
    int m_kills = 0;
    int m_deployed_count = 0;
    int m_remaining_slots = 0;
    int m_squad_size = 0;
    std::string m_stage;
};
}

// Copyright (c) MaaAssistantArknights Contributors
// SPDX-License-Identifier: AGPL-3.0-only

#define _CRT_SECURE_NO_WARNINGS
#include "TrajectoryLogger.h"
#include "AiBridge.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <meojson/json.hpp>
#include <opencv2/imgcodecs.hpp>

#include "Utils/Logger.hpp"

namespace asst
{
TrajectoryLogger& TrajectoryLogger::instance()
{
    static TrajectoryLogger s_instance;
    return s_instance;
}

std::string TrajectoryLogger::make_timestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

void TrajectoryLogger::start_session(const std::string& base_dir, const std::string& theme)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_jsonl.is_open()) {
        m_jsonl.close();
        Log.info("TrajectoryLogger: closed previous session,", m_step, "entries");
    }

    if (!m_enabled || base_dir.empty()) return;

    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&t);
    std::ostringstream dirname;
    dirname << std::put_time(&tm, "%Y%m%d_%H%M%S") << '_' << theme;
    m_episode_id = dirname.str();

    auto session_path = std::filesystem::path(base_dir) / m_episode_id;
    std::string session_dir = session_path.string();
    std::string images_dir = session_dir + "/images";

    std::error_code ec;
    std::filesystem::create_directories(images_dir, ec);
    if (ec) {
        Log.warn("TrajectoryLogger: cannot create", images_dir, ":", ec.message());
        return;
    }

    m_session_dir = session_dir;
    m_images_dir = images_dir;
    m_step = 0;
    m_floor = 0; m_hp = 0; m_hope = 0;

    // Write metadata.json
    json::value meta = json::object{
        { "episode_id", m_episode_id },
        { "theme", theme },
        { "total_steps", 0 },
    };
    std::ofstream meta_file(m_session_dir + "/metadata.json");
    if (meta_file.is_open()) {
        meta_file << meta.to_string() << '\n';
        meta_file.close();
    }

    m_jsonl.open(m_session_dir + "/trajectory.jsonl", std::ios::out | std::ios::trunc);
    if (!m_jsonl.is_open()) {
        Log.warn("TrajectoryLogger: cannot open", m_session_dir + "/trajectory.jsonl");
        return;
    }

    Log.info("TrajectoryLogger: session started at", m_session_dir);
}

void TrajectoryLogger::end_session()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_jsonl.is_open()) {
        m_jsonl.close();
        Log.info("TrajectoryLogger: session ended,", m_step, "entries");
    }

    // Update metadata with final step count
    if (!m_session_dir.empty()) {
        json::value meta = json::object{
            { "episode_id", m_episode_id },
            { "theme", m_theme },
            { "mode", m_mode },
            { "difficulty", m_difficulty },
            { "squad", m_squad },
            { "total_steps", m_step },
        };
        std::ofstream meta_file(m_session_dir + "/metadata.json");
        if (meta_file.is_open()) {
            meta_file << meta.to_string() << '\n';
            meta_file.close();
        }
    }

    m_episode_id.clear();
    m_session_dir.clear();
    m_images_dir.clear();
    m_step = 0;
}

void TrajectoryLogger::set_roguelike_state(int floor, int hope, int hp,
    const std::string& theme, int mode, int difficulty,
    const std::string& squad, int formation_limit)
{
    m_floor = floor; m_hope = hope; m_hp = hp;
    m_theme = theme; m_mode = mode;
    m_difficulty = difficulty; m_squad = squad;
    m_formation_limit = formation_limit;
}

void TrajectoryLogger::set_battle_context(int dp, int kills, int deployed_count,
    int remaining_slots, int squad_size, const std::string& stage)
{
    m_dp = dp; m_kills = kills;
    m_deployed_count = deployed_count; m_remaining_slots = remaining_slots;
    m_squad_size = squad_size; m_stage = stage;
}

int TrajectoryLogger::compute_reward(int floor, int hp, const std::string& task_type)
{
    int reward = 0;
    if (floor > 0 && floor > m_floor) reward += 5 * (floor - m_floor);
    if (m_hp > 0 && hp < m_hp) reward -= 1 * (m_hp - hp);
    if (m_hp > 0 && hp > m_hp) reward += 0; // small bump for healing
    if (task_type == "recruit") reward += 0;
    if (task_type == "battle") reward += 0;
    if (task_type == "stop") reward += 0;

    m_floor = floor;
    m_hp = hp;
    return reward;
}

std::string TrajectoryLogger::state_to_json() const
{
    json::object obj{
        { "theme", m_theme },
        { "floor", m_floor },
        { "hope", m_hope },
        { "hp", m_hp },
        { "difficulty", m_difficulty },
        { "mode", m_mode },
        { "squad", m_squad },
        { "formation_limit", m_formation_limit },
    };
    // Add battle context when available
    if (!m_stage.empty()) {
        obj["stage"] = m_stage;
        obj["dp"] = m_dp;
        obj["kills"] = m_kills;
        obj["deployed_count"] = m_deployed_count;
        obj["remaining_slots"] = m_remaining_slots;
        obj["squad_size"] = m_squad_size;
    }
    return json::value(obj).to_string();
}

std::string TrajectoryLogger::save_screenshot(const cv::Mat& img, const std::string& prefix, int seq)
{
    if (m_images_dir.empty()) return {};
    if (img.empty() || img.rows == 0 || img.cols == 0) return {};

    std::ostringstream filename;
    filename << prefix << "_" << std::setfill('0') << std::setw(4) << seq << ".png";
    std::string path = m_images_dir + "/" + filename.str();

    if (!cv::imwrite(path, img)) {
        Log.warn("TrajectoryLogger: cannot save screenshot to", path);
        return {};
    }

    return "images/" + filename.str();
}

std::string TrajectoryLogger::build_record(
    const std::string& task_type,
    const std::string& img_rel,
    const std::string& action_json,
    const std::string& action_text,
    bool ai_used,
    const std::string& ai_chosen,
    bool done,
    int reward)
{
    auto action = json::parse(action_json);
    auto action_obj = action.value_or(json::object{});
    if (!action_text.empty()) action_obj["text"] = action_text;

    json::value record = json::object{
        { "episode_id", m_episode_id },
        { "step", m_step },
        { "timestamp", make_timestamp() },
        { "task_type", task_type },
        { "ai_used", ai_used },
        { "ai_chosen", ai_chosen },
        { "action", std::move(action_obj) },
        { "observation", img_rel },
        { "reward", reward },
        { "done", done },
        { "extra_params", json::parse(state_to_json()).value_or(json::object{}) },
    };

    return record.to_string();
}

void TrajectoryLogger::log_recruit(
    const cv::Mat& screenshot,
    const std::vector<RecruitCandidate>& candidates,
    const std::string& chosen_operator,
    const std::string& action_text,
    bool ai_used,
    const std::string& ai_chosen)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_jsonl.is_open()) return;

    m_step++;
    std::string img_rel = save_screenshot(screenshot, "recruit", m_step);

    json::array candidates_arr;
    for (const auto& c : candidates) {
        candidates_arr.emplace_back(json::object{
            { "name", c.name },
            { "priority", c.priority },
            { "elite", c.elite },
            { "level", c.level },
            { "is_alternate", c.is_alternate },
        });
    }

    std::string action_json = json::object{
        { "chosen_operator", chosen_operator },
        { "candidates", std::move(candidates_arr) },
    }.to_string();

    int reward = compute_reward(m_floor, m_hp, "recruit");

    m_jsonl << build_record("recruit", img_rel, action_json, action_text,
                            ai_used, ai_chosen, false, reward) << '\n';
    m_jsonl.flush();
}

void TrajectoryLogger::log_generic(
    const cv::Mat& screenshot,
    const std::string& task_type,
    const std::string& action_json,
    const std::string& action_text,
    bool ai_used,
    const std::string& ai_chosen,
    bool done,
    int reward)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_jsonl.is_open()) return;

    m_step++;
    std::string img_rel = save_screenshot(screenshot, task_type, m_step);

    if (reward == 0) reward = compute_reward(m_floor, m_hp, task_type);

    m_jsonl << build_record(task_type, img_rel, action_json, action_text,
                            ai_used, ai_chosen, done, reward) << '\n';
    m_jsonl.flush();
}
}

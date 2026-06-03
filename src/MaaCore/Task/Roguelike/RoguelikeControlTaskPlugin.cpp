#include "RoguelikeControlTaskPlugin.h"

#include "AiBridge/TrajectoryLogger.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"

asst::RoguelikeControlTaskPlugin::RoguelikeControlTaskPlugin(
    const AsstCallback& callback,
    Assistant* inst,
    std::string_view task_chain,
    const std::shared_ptr<RoguelikeConfig>& data) :
    AbstractTaskPlugin(callback, inst, task_chain),
    m_config(data)
{
}

bool asst::RoguelikeControlTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    // Update trajectory state on every roguelike event
    TrajectoryLogger::instance().set_roguelike_state(
        m_config->status().floor, m_config->status().hope, m_config->status().hp,
        m_config->get_theme(), static_cast<int>(m_config->get_mode()),
        m_config->get_difficulty(), m_config->get_squad(),
        m_config->status().formation_upper_limit);

    const std::string roguelike_name = m_config->get_theme() + "@";
    const std::string& task = details.get("details", "task", "");
    std::string_view task_view = task;
    if (task_view.starts_with(roguelike_name)) {
        task_view.remove_prefix(roguelike_name.length());
    }
    if (task_view == "RoguelikeControlTaskPlugin-Stop") {
        m_need_exit_then_stop = false;
        return true;
    }
    if (task_view == "RoguelikeControlTaskPlugin-ExitThenStop") {
        m_need_exit_then_stop = true;
        return true;
    }

    return false;
}

void asst::RoguelikeControlTaskPlugin::exit_then_stop(bool abandon) const
{
    ProcessTask(*this, { m_config->get_theme() + "@Roguelike@ExitThenAbandon" })
        .set_times_limit("Roguelike@Abandon", abandon ? INT_MAX : 0)
        .set_times_limit("Roguelike@StartExplore", 0)
        .run();
}

bool asst::RoguelikeControlTaskPlugin::_run()
{
    if (m_need_exit_then_stop) {
        exit_then_stop();
        m_need_exit_then_stop = false;
    }
    m_task_ptr->set_enable(false);
    return true;
}

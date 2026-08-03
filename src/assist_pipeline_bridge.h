#ifndef ASSIST_PIPELINE_BRIDGE_H
#define ASSIST_PIPELINE_BRIDGE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct AssistPipelineResult {
    bool success = false;
    std::string stage;
    std::string transcript;
    std::string response;
    std::string error;
    std::string conversation_id;
    std::string response_type;
    bool continue_conversation = false;
    int audio_level = 0;
};

int assist_pipeline_child_main(const std::string& ha_url, const std::string& token,
                               const std::string& conversation_id);
AssistPipelineResult run_assist_pipeline_process(
    const std::vector<int16_t>& samples,
    const std::string& conversation_id,
    const std::function<void(const AssistPipelineResult&)>& on_progress,
    const std::function<bool()>& is_cancelled);

#endif

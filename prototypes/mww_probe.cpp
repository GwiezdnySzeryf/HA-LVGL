#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <time.h>

extern "C" {
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
}

#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {
const size_t kArenaSize = 64 * 1024;
alignas(16) uint8_t tensor_arena[kArenaSize];

double seconds(const timespec& value) {
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_nsec) / 1000000000.0;
}

void configure_frontend(FrontendConfig * config) {
    std::memset(config, 0, sizeof(*config));
    config->window.size_ms = 30;
    config->window.step_size_ms = 10;
    config->filterbank.num_channels = 40;
    config->filterbank.lower_band_limit = 125.0f;
    config->filterbank.upper_band_limit = 7500.0f;
    config->noise_reduction.smoothing_bits = 10;
    config->noise_reduction.even_smoothing = 0.025f;
    config->noise_reduction.odd_smoothing = 0.06f;
    config->noise_reduction.min_signal_remaining = 0.05f;
    config->pcan_gain_control.enable_pcan = true;
    config->pcan_gain_control.strength = 0.95f;
    config->pcan_gain_control.offset = 80.0f;
    config->pcan_gain_control.gain_bits = 21;
    config->log_scale.enable_log = true;
    config->log_scale.scale_shift = 6;
}
}

int main(int argc, char ** argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: %s V2_OKAY_NABU.tflite RAW_PCM|- [channels:1|2]\n", argv[0]);
        return 2;
    }
    const int channels = argc == 4 ? std::atoi(argv[3]) : 1;
    if (channels != 1 && channels != 2) {
        std::fprintf(stderr, "channels must be 1 or 2\n");
        return 2;
    }

    std::ifstream model_file(argv[1], std::ios::binary);
    std::vector<uint8_t> model_data((std::istreambuf_iterator<char>(model_file)),
                                    std::istreambuf_iterator<char>());
    if (model_data.empty()) {
        std::fprintf(stderr, "cannot read model: %s\n", argv[1]);
        return 3;
    }
    const tflite::Model * model = tflite::GetModel(model_data.data());
    if (!model || model->version() != TFLITE_SCHEMA_VERSION) {
        std::fprintf(stderr, "unsupported model schema\n");
        return 4;
    }

    tflite::MicroMutableOpResolver<13> resolver;
    resolver.AddCallOnce();
    resolver.AddVarHandle();
    resolver.AddReshape();
    resolver.AddReadVariable();
    resolver.AddConcatenation();
    resolver.AddStridedSlice();
    resolver.AddAssignVariable();
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddSplitV();
    resolver.AddFullyConnected();
    resolver.AddLogistic();
    resolver.AddQuantize();

    tflite::MicroAllocator * allocator =
        tflite::MicroAllocator::Create(tensor_arena, sizeof(tensor_arena));
    if (!allocator) {
        std::fprintf(stderr, "MicroAllocator creation failed\n");
        return 5;
    }
    tflite::MicroResourceVariables * resources =
        tflite::MicroResourceVariables::Create(allocator, 6);
    if (!resources) {
        std::fprintf(stderr, "resource allocation failed\n");
        return 5;
    }
    tflite::MicroInterpreter interpreter(model, resolver, allocator, resources, NULL);
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        std::fprintf(stderr, "AllocateTensors failed\n");
        return 5;
    }

    TfLiteTensor * input = interpreter.input(0);
    TfLiteTensor * output = interpreter.output(0);
    if (!input || !output || input->type != kTfLiteInt8 || output->type != kTfLiteUInt8 ||
        !input->dims || input->dims->size != 3 || input->dims->data[0] != 1 ||
        input->dims->data[1] != 3 || input->dims->data[2] != 40 ||
        !output->dims || output->dims->size != 2 || output->dims->data[0] != 1 ||
        output->dims->data[1] != 1 || input->bytes != 120 || output->bytes != 1 ||
        input->params.zero_point != -128 || output->params.zero_point != 0 ||
        output->params.scale < 0.00390f || output->params.scale > 0.00391f) {
        std::fprintf(stderr, "unexpected tensors: input=%zu output=%zu\n",
                     input ? input->bytes : 0, output ? output->bytes : 0);
        return 6;
    }

    FrontendConfig frontend_config;
    FrontendState frontend_state;
    configure_frontend(&frontend_config);
    std::memset(&frontend_state, 0, sizeof(frontend_state));
    if (!FrontendPopulateState(&frontend_config, &frontend_state, 16000)) {
        std::fprintf(stderr, "FrontendPopulateState failed\n");
        return 7;
    }

    FILE * pcm = std::strcmp(argv[2], "-") == 0 ? stdin : std::fopen(argv[2], "rb");
    if (!pcm) {
        std::fprintf(stderr, "cannot read PCM: %s\n", std::strerror(errno));
        FrontendFreeStateContents(&frontend_state);
        return 8;
    }

    std::fprintf(stderr, "READY arena=%zu/%zu input=[1,3,40] channels=%d\n",
                 interpreter.arena_used_bytes(), sizeof(tensor_arena), channels);
    std::vector<int16_t> interleaved(1600 * channels);
    std::vector<int16_t> mono(1600);
    float scores[5] = {};
    float score_sum = 0.0f;
    float peak = 0.0f;
    int score_position = 0;
    int feature_rows = 0;
    int feature_slices = 0;
    int warmup_until_slice = 100;
    int inference_count = 0;
    int cooldown_until = 0;
    int wake_count = 0;
    uint64_t sample_total = 0;
    timespec wall_start;
    timespec cpu_start;
    clock_gettime(CLOCK_MONOTONIC, &wall_start);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_start);

    while (true) {
        const size_t values = std::fread(interleaved.data(), sizeof(int16_t),
                                         interleaved.size(), pcm);
        if (values == 0) break;
        const size_t sample_count = values / channels;
        for (size_t i = 0; i < sample_count; ++i) mono[i] = interleaved[i * channels];
        sample_total += sample_count;

        size_t cursor = 0;
        while (cursor < sample_count) {
            size_t consumed = 0;
            FrontendOutput features = FrontendProcessSamples(
                &frontend_state, mono.data() + cursor, sample_count - cursor, &consumed);
            cursor += consumed;
            if (!features.values || features.size == 0) {
                if (consumed == 0) break;
                continue;
            }
            if (features.size != 40) {
                std::fprintf(stderr, "unexpected feature count: %zu\n", features.size);
                return 9;
            }
            ++feature_slices;
            int8_t * destination = input->data.int8 + feature_rows * 40;
            for (size_t i = 0; i < features.size; ++i) {
                int32_t value = (static_cast<int32_t>(features.values[i]) * 256 + 333) / 666 - 128;
                value = std::max<int32_t>(-128, std::min<int32_t>(127, value));
                destination[i] = static_cast<int8_t>(value);
            }
            if (++feature_rows != 3) continue;
            feature_rows = 0;
            if (interpreter.Invoke() != kTfLiteOk) {
                std::fprintf(stderr, "Invoke failed\n");
                return 10;
            }
            ++inference_count;
            const float score = output->data.uint8[0] / 256.0f;
            score_sum -= scores[score_position];
            scores[score_position] = score;
            score_sum += score;
            score_position = (score_position + 1) % 5;
            const float average = score_sum / 5.0f;
            peak = std::max(peak, average);

            if (feature_slices >= warmup_until_slice && inference_count >= cooldown_until &&
                average > 0.97f) {
                ++wake_count;
                cooldown_until = inference_count + 167;
                std::printf("WAKE score=%.4f count=%d\n", average, wake_count);
                std::fflush(stdout);
                std::memset(scores, 0, sizeof(scores));
                score_sum = 0.0f;
                score_position = 0;
                warmup_until_slice = feature_slices + 100;
            }
            if (inference_count % 33 == 0) {
                std::printf("SCORE avg=%.4f peak=%.4f audio=%.1fs\n", average, peak,
                            static_cast<double>(sample_total) / 16000.0);
                std::fflush(stdout);
                peak = 0.0f;
            }
        }
    }

    timespec wall_end;
    timespec cpu_end;
    clock_gettime(CLOCK_MONOTONIC, &wall_end);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end);
    const double wall = seconds(wall_end) - seconds(wall_start);
    const double cpu = seconds(cpu_end) - seconds(cpu_start);
    if (std::ferror(pcm) || sample_total == 0) {
        std::fprintf(stderr, "PCM capture failed or returned no samples\n");
        if (pcm != stdin) std::fclose(pcm);
        FrontendFreeStateContents(&frontend_state);
        return 11;
    }
    std::fprintf(stderr, "DONE audio=%.3fs wall=%.3fs cpu=%.3fs cpu/wall=%.1f%% wakes=%d\n",
                 static_cast<double>(sample_total) / 16000.0, wall, cpu,
                 wall > 0.0 ? cpu * 100.0 / wall : 0.0, wake_count);
    if (pcm != stdin) std::fclose(pcm);
    FrontendFreeStateContents(&frontend_state);
    return 0;
}

#include "experiments/Benchmark.h"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <variant>

namespace lg {

namespace {

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::unordered_map<std::string, JsonValue>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data = nullptr;

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data); }
    bool is_bool() const { return std::holds_alternative<bool>(data); }
    bool is_number() const { return std::holds_alternative<double>(data); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_array() const { return std::holds_alternative<Array>(data); }
    bool is_object() const { return std::holds_alternative<Object>(data); }

    bool as_bool() const { return std::get<bool>(data); }
    double as_number() const { return std::get<double>(data); }
    const std::string& as_string() const { return std::get<std::string>(data); }
    const Array& as_array() const { return std::get<Array>(data); }
    const Object& as_object() const { return std::get<Object>(data); }
};

class JsonParser {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    JsonValue parse() {
        skip_ws();
        JsonValue value = parse_value();
        skip_ws();
        if (!at_end()) {
            throw error("unexpected trailing characters");
        }
        return value;
    }

private:
    JsonValue parse_value() {
        skip_ws();
        if (at_end()) {
            throw error("unexpected end of input");
        }
        switch (peek()) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return JsonValue{parse_string()};
            case 't': return parse_literal("true", JsonValue{true});
            case 'f': return parse_literal("false", JsonValue{false});
            case 'n': return parse_literal("null", JsonValue{nullptr});
            default:
                if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek()))) {
                    return JsonValue{parse_number()};
                }
                throw error("invalid value");
        }
    }

    JsonValue parse_object() {
        expect('{');
        JsonValue::Object object;
        skip_ws();
        if (match('}')) {
            return JsonValue{object};
        }
        while (true) {
            skip_ws();
            if (peek() != '"') {
                throw error("expected string key");
            }
            std::string key = parse_string();
            skip_ws();
            expect(':');
            object.emplace(std::move(key), parse_value());
            skip_ws();
            if (match('}')) {
                break;
            }
            expect(',');
        }
        return JsonValue{object};
    }

    JsonValue parse_array() {
        expect('[');
        JsonValue::Array array;
        skip_ws();
        if (match(']')) {
            return JsonValue{array};
        }
        while (true) {
            array.push_back(parse_value());
            skip_ws();
            if (match(']')) {
                break;
            }
            expect(',');
        }
        return JsonValue{array};
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (!at_end()) {
            char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (at_end()) {
                    throw error("unterminated escape sequence");
                }
                char esc = text_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: throw error("unsupported escape sequence");
                }
                continue;
            }
            out.push_back(c);
        }
        throw error("unterminated string");
    }

    double parse_number() {
        const char* begin = text_.c_str() + pos_;
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        if (end == begin) {
            throw error("invalid number");
        }
        pos_ += static_cast<size_t>(end - begin);
        return value;
    }

    JsonValue parse_literal(const char* literal, JsonValue value) {
        const size_t len = std::char_traits<char>::length(literal);
        if (text_.compare(pos_, len, literal) != 0) {
            throw error(std::string("expected ") + literal);
        }
        pos_ += len;
        return value;
    }

    void skip_ws() {
        while (!at_end() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    void expect(char ch) {
        if (at_end() || text_[pos_] != ch) {
            throw error(std::string("expected '") + ch + "'");
        }
        ++pos_;
    }

    bool match(char ch) {
        if (!at_end() && text_[pos_] == ch) {
            ++pos_;
            return true;
        }
        return false;
    }

    char peek() const { return text_[pos_]; }
    bool at_end() const { return pos_ >= text_.size(); }

    std::runtime_error error(const std::string& message) const {
        std::ostringstream oss;
        oss << "JSON parse error at byte " << pos_ << ": " << message;
        return std::runtime_error(oss.str());
    }

    std::string text_;
    size_t pos_ = 0;
};

std::string quote_json(const std::string& value) {
    std::ostringstream oss;
    oss << '"';
    for (char c : value) {
        switch (c) {
            case '\\': oss << "\\\\"; break;
            case '"': oss << "\\\""; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c; break;
        }
    }
    oss << '"';
    return oss.str();
}

const JsonValue* object_get(const JsonValue::Object& object, const std::string& key) {
    auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

std::string string_or(const JsonValue::Object& object, const std::string& key, const std::string& fallback = {}) {
    if (const JsonValue* value = object_get(object, key)) {
        if (!value->is_string()) {
            throw std::runtime_error("json field '" + key + "' must be a string");
        }
        return value->as_string();
    }
    return fallback;
}

double number_or(const JsonValue::Object& object, const std::string& key, double fallback = 0.0) {
    if (const JsonValue* value = object_get(object, key)) {
        if (!value->is_number()) {
            throw std::runtime_error("json field '" + key + "' must be a number");
        }
        return value->as_number();
    }
    return fallback;
}

bool bool_or(const JsonValue::Object& object, const std::string& key, bool fallback = false) {
    if (const JsonValue* value = object_get(object, key)) {
        if (!value->is_bool()) {
            throw std::runtime_error("json field '" + key + "' must be a boolean");
        }
        return value->as_bool();
    }
    return fallback;
}

const JsonValue::Array* array_or_null(const JsonValue::Object& object, const std::string& key) {
    if (const JsonValue* value = object_get(object, key)) {
        if (!value->is_array()) {
            throw std::runtime_error("json field '" + key + "' must be an array");
        }
        return &value->as_array();
    }
    return nullptr;
}

std::string infer_input_mode(const JsonValue::Object& object, const std::filesystem::path& path, const std::string& output_artifact) {
    std::string mode = string_or(object, "input_mode");
    if (!mode.empty()) {
        return mode;
    }
    if (object_get(object, "output_video") != nullptr || path.filename().string().find("_metrics.json") != std::string::npos) {
        return "offline_video";
    }
    if (std::filesystem::path(output_artifact).extension() == ".mp4") {
        return "offline_video";
    }
    return "image";
}

std::string derive_label(const JsonValue::Object& object, const std::filesystem::path& path) {
    std::string label = string_or(object, "label");
    if (!label.empty()) {
        return label;
    }
    std::string stem = path.stem().string();
    const std::string performance_suffix = "_performance";
    const std::string metrics_suffix = "_metrics";
    if (stem.size() > performance_suffix.size() &&
        stem.compare(stem.size() - performance_suffix.size(), performance_suffix.size(), performance_suffix) == 0) {
        stem.resize(stem.size() - performance_suffix.size());
    } else if (stem.size() > metrics_suffix.size() &&
        stem.compare(stem.size() - metrics_suffix.size(), metrics_suffix.size(), metrics_suffix) == 0) {
        stem.resize(stem.size() - metrics_suffix.size());
    }
    return stem;
}

void parse_passes(const JsonValue::Array* array, std::vector<PassMetricsSummary>& passes) {
    passes.clear();
    if (!array) {
        return;
    }
    passes.reserve(array->size());
    for (const JsonValue& entry : *array) {
        if (!entry.is_object()) {
            throw std::runtime_error("pass entries must be objects");
        }
        const JsonValue::Object& object = entry.as_object();
        passes.push_back({
            string_or(object, "name"),
            number_or(object, "avg_cpu_ms"),
            number_or(object, "avg_gpu_ms")
        });
    }
}

void parse_frame_samples(const JsonValue::Array* array, std::vector<BenchmarkSample>& samples) {
    samples.clear();
    if (!array) {
        return;
    }
    samples.reserve(array->size());
    for (size_t i = 0; i < array->size(); ++i) {
        const JsonValue& entry = (*array)[i];
        if (!entry.is_object()) {
            throw std::runtime_error("frame entries must be objects");
        }
        const JsonValue::Object& object = entry.as_object();
        BenchmarkSample sample{};
        sample.frame_index = static_cast<int>(number_or(object, "index", static_cast<double>(i)));
        sample.pts = number_or(object, "pts", 0.0);
        sample.warmup = bool_or(object, "warmup", false);
        sample.timing.cpu_ms = number_or(object, "cpu_ms", 0.0);
        sample.timing.gpu_ms = number_or(object, "gpu_ms", 0.0);
        if (const JsonValue::Array* passes = array_or_null(object, "passes")) {
            sample.timing.passes.reserve(passes->size());
            for (const JsonValue& pass_entry : *passes) {
                if (!pass_entry.is_object()) {
                    throw std::runtime_error("frame pass entries must be objects");
                }
                const JsonValue::Object& pass = pass_entry.as_object();
                sample.timing.passes.push_back({
                    string_or(pass, "name"),
                    number_or(pass, "cpu_ms"),
                    number_or(pass, "gpu_ms")
                });
            }
        }
        samples.push_back(std::move(sample));
    }
}

}  // namespace

BenchmarkResult Benchmark::build(
    const BenchmarkConfig& config,
    const std::vector<BenchmarkSample>& samples,
    double total_wall_ms,
    double measured_wall_ms,
    double input_fps,
    const std::string& output_artifact) const {
    BenchmarkResult result{};
    result.config = config;
    result.samples = samples;
    result.output_artifact = output_artifact;
    result.input_fps = input_fps;
    result.total_wall_ms = total_wall_ms;
    result.measured_wall_ms = measured_wall_ms > 0.0 ? measured_wall_ms : total_wall_ms;
    if (!samples.empty()) {
        result.cold_start = samples.front().timing;
    }

    std::vector<FrameTiming> timings;
    timings.reserve(samples.size());
    for (const BenchmarkSample& sample : samples) {
        if (sample.warmup) {
            ++result.warmup_frame_count;
            continue;
        }
        timings.push_back(sample.timing);
    }
    result.measured_frame_count = timings.size();
    result.summary = summarize_metrics(timings, result.measured_wall_ms);
    return result;
}

bool Benchmark::read_json(const std::filesystem::path& path, BenchmarkResult& result, std::string* error) const {
    try {
        std::ifstream input(path);
        if (!input) {
            throw std::runtime_error("failed to open benchmark json: " + path.string());
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        JsonValue root = JsonParser(buffer.str()).parse();
        if (!root.is_object()) {
            throw std::runtime_error("benchmark root must be an object");
        }

        const JsonValue::Object& object = root.as_object();

        BenchmarkResult parsed{};
        parsed.config.label = derive_label(object, path);
        parsed.output_artifact = string_or(object, "output_artifact");
        if (parsed.output_artifact.empty()) {
            parsed.output_artifact = string_or(object, "output_video");
        }
        parsed.config.input_mode = infer_input_mode(object, path, parsed.output_artifact);
        parsed.config.input_path = string_or(object, "input_path");
        parsed.config.performance_mode = string_or(object, "performance_mode", "unknown");
        parsed.config.ablation_variant = string_or(object, "ablation_variant", "standard");
        parsed.config.width = static_cast<int>(number_or(object, "width", 0.0));
        parsed.config.height = static_cast<int>(number_or(object, "height", 0.0));
        parsed.config.half_res_specular = bool_or(object, "half_res_specular", false);
        parsed.config.cuda_graph = bool_or(object, "cuda_graph", false);
        parsed.config.ablation_refraction = bool_or(object, "ablation_refraction", true);
        parsed.config.ablation_reflection = bool_or(object, "ablation_reflection", true);
        parsed.config.ablation_specular = bool_or(object, "ablation_specular", true);
        parsed.config.ablation_legibility = bool_or(object, "ablation_legibility", true);
        parsed.config.ablation_temporal = bool_or(object, "ablation_temporal", true);
        parsed.config.ablation_blur_only = bool_or(object, "ablation_blur_only", false);
        parsed.config.specular_downsample = static_cast<int>(number_or(object, "specular_downsample", 1.0));
        parsed.config.legibility_downsample = static_cast<int>(number_or(object, "legibility_downsample", 1.0));
        parsed.config.warmup_frames = static_cast<int>(number_or(object, "warmup_frames", 0.0));
        parsed.config.benchmark_frames = static_cast<int>(number_or(object, "benchmark_frames", 0.0));

        parsed.input_fps = number_or(object, "input_fps", 0.0);
        parsed.total_wall_ms = number_or(object, "total_wall_ms", number_or(object, "wall_ms", 0.0));
        parsed.measured_wall_ms = number_or(object, "measured_wall_ms", parsed.total_wall_ms);
        parsed.warmup_frame_count = static_cast<size_t>(number_or(object, "warmup_frame_count", 0.0));
        parsed.measured_frame_count = static_cast<size_t>(number_or(object, "measured_frame_count", number_or(object, "frame_count", 0.0)));
        parsed.cold_start.cpu_ms = number_or(object, "cold_start_cpu_ms", 0.0);
        parsed.cold_start.gpu_ms = number_or(object, "cold_start_gpu_ms", 0.0);

        parsed.summary.frame_count = static_cast<size_t>(number_or(object, "frame_count", static_cast<double>(parsed.measured_frame_count)));
        parsed.summary.wall_ms = parsed.measured_wall_ms;
        parsed.summary.frame_ms = number_or(object, "avg_cpu_ms", 0.0);
        parsed.summary.gpu_frame_ms = number_or(object, "avg_gpu_ms", 0.0);
        parsed.summary.fps = number_or(object, "avg_fps", 0.0);
        parse_passes(array_or_null(object, "passes"), parsed.summary.passes);
        parse_frame_samples(array_or_null(object, "frames"), parsed.samples);

        if (!parsed.samples.empty()) {
            if (parsed.cold_start.cpu_ms <= 0.0 && parsed.cold_start.gpu_ms <= 0.0) {
                parsed.cold_start = parsed.samples.front().timing;
            }
            if (parsed.warmup_frame_count == 0 && parsed.measured_frame_count == 0) {
                for (const BenchmarkSample& sample : parsed.samples) {
                    if (sample.warmup) {
                        ++parsed.warmup_frame_count;
                    } else {
                        ++parsed.measured_frame_count;
                    }
                }
            }
        }

        const bool missing_summary = parsed.summary.frame_count == 0 ||
            parsed.summary.frame_ms <= 0.0 ||
            parsed.summary.gpu_frame_ms <= 0.0 ||
            parsed.summary.passes.empty();
        if (missing_summary && !parsed.samples.empty()) {
            std::vector<FrameTiming> timings;
            timings.reserve(parsed.samples.size());
            for (const BenchmarkSample& sample : parsed.samples) {
                if (!sample.warmup) {
                    timings.push_back(sample.timing);
                }
            }
            if (parsed.measured_frame_count == 0) {
                parsed.measured_frame_count = timings.size();
            }
            const double wall_ms = parsed.measured_wall_ms > 0.0 ? parsed.measured_wall_ms : parsed.total_wall_ms;
            parsed.summary = summarize_metrics(timings, wall_ms);
        }

        if (parsed.summary.frame_count == 0) {
            parsed.summary.frame_count = parsed.measured_frame_count;
        }
        result = std::move(parsed);
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

bool Benchmark::write_json(const std::filesystem::path& path, const BenchmarkResult& result) const {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream out(path);
    if (!out) {
        return false;
    }

    out << std::fixed << std::setprecision(3);
    out << "{\n";
    out << "  \"label\": " << quote_json(result.config.label) << ",\n";
    out << "  \"input_mode\": " << quote_json(result.config.input_mode) << ",\n";
    out << "  \"input_path\": " << quote_json(result.config.input_path) << ",\n";
    out << "  \"performance_mode\": " << quote_json(result.config.performance_mode) << ",\n";
    out << "  \"ablation_variant\": " << quote_json(result.config.ablation_variant) << ",\n";
    out << "  \"width\": " << result.config.width << ",\n";
    out << "  \"height\": " << result.config.height << ",\n";
    out << "  \"half_res_specular\": " << (result.config.half_res_specular ? "true" : "false") << ",\n";
    out << "  \"cuda_graph\": " << (result.config.cuda_graph ? "true" : "false") << ",\n";
    out << "  \"ablation_refraction\": " << (result.config.ablation_refraction ? "true" : "false") << ",\n";
    out << "  \"ablation_reflection\": " << (result.config.ablation_reflection ? "true" : "false") << ",\n";
    out << "  \"ablation_specular\": " << (result.config.ablation_specular ? "true" : "false") << ",\n";
    out << "  \"ablation_legibility\": " << (result.config.ablation_legibility ? "true" : "false") << ",\n";
    out << "  \"ablation_temporal\": " << (result.config.ablation_temporal ? "true" : "false") << ",\n";
    out << "  \"ablation_blur_only\": " << (result.config.ablation_blur_only ? "true" : "false") << ",\n";
    out << "  \"specular_downsample\": " << result.config.specular_downsample << ",\n";
    out << "  \"legibility_downsample\": " << result.config.legibility_downsample << ",\n";
    out << "  \"warmup_frames\": " << result.config.warmup_frames << ",\n";
    out << "  \"benchmark_frames\": " << result.config.benchmark_frames << ",\n";
    out << "  \"input_fps\": " << result.input_fps << ",\n";
    out << "  \"output_artifact\": " << quote_json(result.output_artifact) << ",\n";
    out << "  \"warmup_frame_count\": " << result.warmup_frame_count << ",\n";
    out << "  \"measured_frame_count\": " << result.measured_frame_count << ",\n";
    out << "  \"cold_start_cpu_ms\": " << result.cold_start.cpu_ms << ",\n";
    out << "  \"cold_start_gpu_ms\": " << result.cold_start.gpu_ms << ",\n";
    out << "  \"frame_count\": " << result.summary.frame_count << ",\n";
    out << "  \"total_wall_ms\": " << result.total_wall_ms << ",\n";
    out << "  \"measured_wall_ms\": " << result.summary.wall_ms << ",\n";
    out << "  \"avg_cpu_ms\": " << result.summary.frame_ms << ",\n";
    out << "  \"avg_gpu_ms\": " << result.summary.gpu_frame_ms << ",\n";
    out << "  \"avg_fps\": " << result.summary.fps << ",\n";
    out << "  \"passes\": [\n";
    for (size_t i = 0; i < result.summary.passes.size(); ++i) {
        const PassMetricsSummary& pass = result.summary.passes[i];
        out << "    {\"name\": " << quote_json(pass.name)
            << ", \"avg_cpu_ms\": " << pass.avg_cpu_ms
            << ", \"avg_gpu_ms\": " << pass.avg_gpu_ms << "}";
        out << (i + 1 == result.summary.passes.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"frames\": [\n";
    for (size_t i = 0; i < result.samples.size(); ++i) {
        const BenchmarkSample& sample = result.samples[i];
        out << "    {\"index\": " << sample.frame_index
            << ", \"pts\": " << sample.pts
            << ", \"warmup\": " << (sample.warmup ? "true" : "false")
            << ", \"cpu_ms\": " << sample.timing.cpu_ms
            << ", \"gpu_ms\": " << sample.timing.gpu_ms
            << ", \"passes\": [";
        for (size_t j = 0; j < sample.timing.passes.size(); ++j) {
            const PassTiming& pass = sample.timing.passes[j];
            out << "{\"name\": " << quote_json(pass.name)
                << ", \"cpu_ms\": " << pass.cpu_ms
                << ", \"gpu_ms\": " << pass.gpu_ms << "}";
            if (j + 1 != sample.timing.passes.size()) {
                out << ", ";
            }
        }
        out << "]}";
        out << (i + 1 == result.samples.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

}

#include "core/Config.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
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
            JsonValue value = parse_value();
            object.emplace(std::move(key), std::move(value));
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
        double value = std::strtod(begin, &end);
        if (end == begin) {
            throw error("invalid number");
        }
        pos_ += static_cast<size_t>(end - begin);
        return value;
    }

    JsonValue parse_literal(const char* literal, JsonValue value) {
        size_t len = std::char_traits<char>::length(literal);
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

const JsonValue* object_get(const JsonValue::Object& object, const std::string& key) {
    auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

const JsonValue::Object* require_object(const JsonValue::Object& parent, const std::string& key) {
    if (const JsonValue* value = object_get(parent, key)) {
        if (!value->is_object()) {
            throw std::runtime_error("config field '" + key + "' must be an object");
        }
        return &value->as_object();
    }
    return nullptr;
}

template <typename T>
void assign_number(const JsonValue::Object& object, const std::string& key, T& target) {
    if (const JsonValue* value = object_get(object, key)) {
        if (!value->is_number()) {
            throw std::runtime_error("config field '" + key + "' must be a number");
        }
        target = static_cast<T>(value->as_number());
    }
}

void assign_bool(const JsonValue::Object& object, const std::string& key, bool& target) {
    if (const JsonValue* value = object_get(object, key)) {
        if (!value->is_bool()) {
            throw std::runtime_error("config field '" + key + "' must be a boolean");
        }
        target = value->as_bool();
    }
}

void assign_string(const JsonValue::Object& object, const std::string& key, std::string& target) {
    if (const JsonValue* value = object_get(object, key)) {
        if (!value->is_string()) {
            throw std::runtime_error("config field '" + key + "' must be a string");
        }
        target = value->as_string();
    }
}

void assign_triplet(const JsonValue::Object& object, const std::string& key, float target[3]) {
    if (const JsonValue* value = object_get(object, key)) {
        if (!value->is_array()) {
            throw std::runtime_error("config field '" + key + "' must be an array");
        }
        const auto& array = value->as_array();
        if (array.size() != 3) {
            throw std::runtime_error("config field '" + key + "' must contain exactly 3 numbers");
        }
        for (size_t i = 0; i < 3; ++i) {
            if (!array[i].is_number()) {
                throw std::runtime_error("config field '" + key + "' must contain only numbers");
            }
            target[i] = static_cast<float>(array[i].as_number());
        }
    }
}

template <typename T>
void require_positive(T value, const std::string& field) {
    if (!(value > 0)) {
        throw std::runtime_error("config field '" + field + "' must be > 0");
    }
}

template <typename T>
void require_non_negative(T value, const std::string& field) {
    if (value < 0) {
        throw std::runtime_error("config field '" + field + "' must be >= 0");
    }
}

bool is_valid_thickness_profile_name(const std::string& value) {
    return value == "parabola" || value == "super_quadric" || value == "superquadric" || value == "edge_roll";
}

void validate(const AppConfig& cfg) {
    require_positive(cfg.window.width, "window.width");
    require_positive(cfg.window.height, "window.height");
    if (cfg.input.mode != "image" && cfg.input.mode != "offline_video" && cfg.input.mode != "realtime_video") {
        throw std::runtime_error("config field 'input.mode' must be one of: image, offline_video, realtime_video");
    }
    if (cfg.input.realtime_source != "file" && cfg.input.realtime_source != "webcam") {
        throw std::runtime_error("config field 'input.realtime_source' must be either 'file' or 'webcam'");
    }
    require_non_negative(cfg.input.device_index, "input.device_index");
    if (cfg.input.mode == "realtime_video" && cfg.input.realtime_source == "webcam") {
        if (cfg.input.device_index < 0) {
            throw std::runtime_error("config field 'input.device_index' must be >= 0 for webcam realtime input");
        }
    } else if (cfg.input.path.empty()) {
        throw std::runtime_error("config field 'input.path' must not be empty");
    }
    require_positive(cfg.tabbar.width, "tabbar.width");
    require_positive(cfg.tabbar.height, "tabbar.height");
    require_positive(cfg.tabbar.corner_radius, "tabbar.corner_radius");
    if (cfg.tabbar.corner_radius > 0.5f * std::min(cfg.tabbar.width, cfg.tabbar.height)) {
        throw std::runtime_error("config field 'tabbar.corner_radius' must be <= min(width, height) / 2");
    }
    require_non_negative(cfg.glass.h0, "glass.h0");
    require_non_negative(cfg.glass.refraction_strength, "glass.refraction_strength");
    require_non_negative(cfg.glass.center_strength, "glass.center_strength");
    require_positive(cfg.glass.edge_sigma, "glass.edge_sigma");
    require_non_negative(cfg.glass.fresnel_strength, "glass.fresnel_strength");
    require_non_negative(cfg.glass.specular_strength, "glass.specular_strength");
    require_non_negative(cfg.glass.edge_boost, "glass.edge_boost");
    require_non_negative(cfg.glass.edge_glow_strength, "glass.edge_glow_strength");
    require_positive(cfg.glass.displacement_limit, "glass.displacement_limit");
    if (cfg.glass.jacobian_guard < 0.f || cfg.glass.jacobian_guard > 1.f) {
        throw std::runtime_error("config field 'glass.jacobian_guard' must be within [0, 1]");
    }
    if (cfg.glass.foreground_protect < 0.f || cfg.glass.foreground_protect > 1.f) {
        throw std::runtime_error("config field 'glass.foreground_protect' must be within [0, 1]");
    }
    require_non_negative(cfg.glass.legibility_boost, "glass.legibility_boost");
    require_positive(cfg.glass.specular_power, "glass.specular_power");
    require_positive(cfg.glass.profile_p, "glass.profile_p");
    require_positive(cfg.glass.profile_q, "glass.profile_q");
    if (cfg.glass.temporal_alpha < 0.f || cfg.glass.temporal_alpha > 1.f) {
        throw std::runtime_error("config field 'glass.temporal_alpha' must be within [0, 1]");
    }
    if (!is_valid_thickness_profile_name(cfg.glass.thickness_profile)) {
        throw std::runtime_error("config field 'glass.thickness_profile' must be one of: parabola, super_quadric, edge_roll");
    }
    require_non_negative(cfg.legibility.target_contrast, "legibility.target_contrast");
    require_positive(cfg.legibility.tile_size, "legibility.tile_size");
    if (cfg.ablation.variant.empty()) {
        throw std::runtime_error("config field 'ablation.variant' must not be empty");
    }
    if (cfg.performance.mode != "fast" && cfg.performance.mode != "full") {
        throw std::runtime_error("config field 'performance.mode' must be either 'fast' or 'full'");
    }
    require_positive(cfg.performance.specular_downsample, "performance.specular_downsample");
    require_positive(cfg.performance.legibility_downsample, "performance.legibility_downsample");
    require_non_negative(cfg.performance.warmup_frames, "performance.warmup_frames");
    require_positive(cfg.performance.benchmark_frames, "performance.benchmark_frames");
    if (cfg.output.out_dir.empty()) {
        throw std::runtime_error("config field 'output.out_dir' must not be empty");
    }
}

std::string bool_string(bool value) {
    return value ? "true" : "false";
}

}  // namespace

AppConfig ConfigLoader::load(const std::string& path) {
    AppConfig cfg;
    std::ifstream f(path);
    if (!f) {
        throw std::runtime_error("failed to open config file: " + path);
    }

    std::ostringstream buffer;
    buffer << f.rdbuf();
    JsonValue root = JsonParser(buffer.str()).parse();
    if (!root.is_object()) {
        throw std::runtime_error("config root must be a JSON object");
    }

    const auto& object = root.as_object();

    if (const auto* window = require_object(object, "window")) {
        assign_number(*window, "width", cfg.window.width);
        assign_number(*window, "height", cfg.window.height);
        assign_bool(*window, "vsync", cfg.window.vsync);
    }

    if (const auto* input = require_object(object, "input")) {
        assign_string(*input, "mode", cfg.input.mode);
        assign_string(*input, "path", cfg.input.path);
        assign_bool(*input, "loop", cfg.input.loop);
        assign_string(*input, "realtime_source", cfg.input.realtime_source);
        assign_number(*input, "device_index", cfg.input.device_index);
    }

    if (const auto* tabbar = require_object(object, "tabbar")) {
        assign_number(*tabbar, "cx", cfg.tabbar.cx);
        assign_number(*tabbar, "cy", cfg.tabbar.cy);
        assign_number(*tabbar, "width", cfg.tabbar.width);
        assign_number(*tabbar, "height", cfg.tabbar.height);
        assign_number(*tabbar, "corner_radius", cfg.tabbar.corner_radius);
    }

    if (const auto* glass = require_object(object, "glass")) {
        assign_string(*glass, "thickness_profile", cfg.glass.thickness_profile);
        assign_number(*glass, "h0", cfg.glass.h0);
        assign_number(*glass, "refraction_strength", cfg.glass.refraction_strength);
        assign_number(*glass, "center_strength", cfg.glass.center_strength);
        assign_number(*glass, "edge_sigma", cfg.glass.edge_sigma);
        assign_number(*glass, "fresnel_strength", cfg.glass.fresnel_strength);
        assign_number(*glass, "specular_strength", cfg.glass.specular_strength);
        assign_number(*glass, "temporal_alpha", cfg.glass.temporal_alpha);
        assign_number(*glass, "edge_boost", cfg.glass.edge_boost);
        assign_number(*glass, "edge_glow_strength", cfg.glass.edge_glow_strength);
        assign_number(*glass, "displacement_limit", cfg.glass.displacement_limit);
        assign_number(*glass, "jacobian_guard", cfg.glass.jacobian_guard);
        assign_number(*glass, "foreground_protect", cfg.glass.foreground_protect);
        assign_number(*glass, "legibility_boost", cfg.glass.legibility_boost);
        assign_number(*glass, "specular_power", cfg.glass.specular_power);
        assign_number(*glass, "profile_p", cfg.glass.profile_p);
        assign_number(*glass, "profile_q", cfg.glass.profile_q);
        assign_triplet(*glass, "absorb", cfg.glass.absorb);
        assign_triplet(*glass, "tint", cfg.glass.tint);
    }

    if (const auto* legibility = require_object(object, "legibility")) {
        assign_bool(*legibility, "enabled", cfg.legibility.enabled);
        assign_number(*legibility, "target_contrast", cfg.legibility.target_contrast);
        assign_number(*legibility, "tile_size", cfg.legibility.tile_size);
    }

    if (const auto* ablation = require_object(object, "ablation")) {
        assign_string(*ablation, "variant", cfg.ablation.variant);
        assign_bool(*ablation, "refraction", cfg.ablation.refraction);
        assign_bool(*ablation, "reflection", cfg.ablation.reflection);
        assign_bool(*ablation, "specular", cfg.ablation.specular);
        assign_bool(*ablation, "legibility", cfg.ablation.legibility);
        assign_bool(*ablation, "temporal", cfg.ablation.temporal);
        assign_bool(*ablation, "blur_only", cfg.ablation.blur_only);
    }

    if (const auto* performance = require_object(object, "performance")) {
        assign_string(*performance, "mode", cfg.performance.mode);
        assign_bool(*performance, "half_res_specular", cfg.performance.half_res_specular);
        assign_bool(*performance, "cuda_graph", cfg.performance.cuda_graph);
        assign_bool(*performance, "opengl_interop", cfg.performance.opengl_interop);
        assign_number(*performance, "specular_downsample", cfg.performance.specular_downsample);
        assign_number(*performance, "legibility_downsample", cfg.performance.legibility_downsample);
        assign_number(*performance, "warmup_frames", cfg.performance.warmup_frames);
        assign_number(*performance, "benchmark_frames", cfg.performance.benchmark_frames);
    }

    if (const auto* output = require_object(object, "output")) {
        assign_bool(*output, "save_frames", cfg.output.save_frames);
        assign_bool(*output, "save_debug_buffers", cfg.output.save_debug_buffers);
        assign_bool(*output, "headless", cfg.output.headless);
        assign_string(*output, "out_dir", cfg.output.out_dir);
    }

    validate(cfg);
    return cfg;
}

void ConfigLoader::save_template(const std::string& path) {
    std::ofstream f(path);
    f << R"({
  "window": {"width": 1600, "height": 900, "vsync": true},
  "input": {"mode": "image", "path": "assets/images/demo.ppm", "loop": true, "realtime_source": "file", "device_index": 0},
  "tabbar": {"cx": 800, "cy": 740, "width": 980, "height": 180, "corner_radius": 90},
  "glass": {"thickness_profile": "edge_roll", "h0": 22.0, "refraction_strength": 18.0, "center_strength": 6.0,
              "edge_sigma": 24.0, "fresnel_strength": 1.0, "specular_strength": 0.8, "temporal_alpha": 0.18,
              "edge_boost": 0.35, "edge_glow_strength": 0.14, "displacement_limit": 24.0, "jacobian_guard": 0.82,
              "foreground_protect": 0.25, "legibility_boost": 0.20, "specular_power": 48.0, "profile_p": 2.6, "profile_q": 1.8,
              "absorb": [0.03,0.03,0.025], "tint": [0.18,0.18,0.20]},
  "legibility": {"enabled": true, "target_contrast": 0.45, "tile_size": 16},
  "ablation": {"variant": "standard", "refraction": true, "reflection": true, "specular": true, "legibility": true, "temporal": true, "blur_only": false},
  "performance": {"mode": "full", "half_res_specular": false, "cuda_graph": false, "opengl_interop": false, "specular_downsample": 1, "legibility_downsample": 1, "warmup_frames": 2, "benchmark_frames": 12},
  "output": {"save_frames": false, "save_debug_buffers": false, "headless": false, "out_dir": "results"}
})";
}

std::string ConfigLoader::describe(const AppConfig& cfg) {
    std::ostringstream oss;
    oss << "window=" << cfg.window.width << "x" << cfg.window.height
        << " vsync=" << bool_string(cfg.window.vsync)
        << ", input.mode=" << cfg.input.mode
        << ", input.path=" << std::quoted(cfg.input.path)
        << ", input.loop=" << bool_string(cfg.input.loop)
        << ", input.source=" << cfg.input.realtime_source
        << ", input.device=" << cfg.input.device_index
        << ", tabbar=(" << cfg.tabbar.cx << "," << cfg.tabbar.cy << "," << cfg.tabbar.width
        << "x" << cfg.tabbar.height << ", r=" << cfg.tabbar.corner_radius << ")"
        << ", glass.profile=" << cfg.glass.thickness_profile
        << ", glass.h0=" << cfg.glass.h0
        << ", refr=" << cfg.glass.refraction_strength
        << ", center=" << cfg.glass.center_strength
        << ", edge_sigma=" << cfg.glass.edge_sigma
        << ", edge_boost=" << cfg.glass.edge_boost
        << ", disp_limit=" << cfg.glass.displacement_limit
        << ", jacobian_guard=" << cfg.glass.jacobian_guard
        << ", fresnel=" << cfg.glass.fresnel_strength
        << ", specular=" << cfg.glass.specular_strength
        << ", spec_power=" << cfg.glass.specular_power
        << ", edge_glow=" << cfg.glass.edge_glow_strength
        << ", fg_protect=" << cfg.glass.foreground_protect
        << ", legibility_boost=" << cfg.glass.legibility_boost
        << ", temporal_alpha=" << cfg.glass.temporal_alpha
        << ", legibility.enabled=" << bool_string(cfg.legibility.enabled)
        << ", legibility.target=" << cfg.legibility.target_contrast
        << ", legibility.tile=" << cfg.legibility.tile_size
        << ", ablation.variant=" << cfg.ablation.variant
        << ", ablation.refraction=" << bool_string(cfg.ablation.refraction)
        << ", ablation.reflection=" << bool_string(cfg.ablation.reflection)
        << ", ablation.specular=" << bool_string(cfg.ablation.specular)
        << ", ablation.legibility=" << bool_string(cfg.ablation.legibility)
        << ", ablation.temporal=" << bool_string(cfg.ablation.temporal)
        << ", ablation.blur_only=" << bool_string(cfg.ablation.blur_only)
        << ", performance.mode=" << cfg.performance.mode
        << ", half_res_specular=" << bool_string(cfg.performance.half_res_specular)
        << ", cuda_graph=" << bool_string(cfg.performance.cuda_graph)
        << ", opengl_interop=" << bool_string(cfg.performance.opengl_interop)
        << ", spec_downsample=" << cfg.performance.specular_downsample
        << ", legibility_downsample=" << cfg.performance.legibility_downsample
        << ", warmup_frames=" << cfg.performance.warmup_frames
        << ", benchmark_frames=" << cfg.performance.benchmark_frames
        << ", output.dir=" << std::quoted(cfg.output.out_dir)
        << ", save_frames=" << bool_string(cfg.output.save_frames)
        << ", headless=" << bool_string(cfg.output.headless)
        << ", save_debug_buffers=" << bool_string(cfg.output.save_debug_buffers);
    return oss.str();
}

std::vector<std::string> ConfigLoader::output_directories(const AppConfig& cfg) {
    std::filesystem::path root(cfg.output.out_dir);
    return {
        root.string(),
        (root / "images").string(),
        (root / "videos").string(),
        (root / "benchmarks").string(),
        (root / "ablation").string(),
        (root / "figures").string(),
        (root / "debug").string(),
        (root / "debug" / "buffers").string()
    };
}

void ConfigLoader::ensure_output_directories(const AppConfig& cfg) {
    for (const std::string& dir : output_directories(cfg)) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            throw std::runtime_error("failed to create output directory '" + dir + "': " + ec.message());
        }
    }
}
}

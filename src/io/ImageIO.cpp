#include "io/ImageIO.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <jpeglib.h>
#include <limits>
#include <png.h>
#include <sstream>
#include <vector>
namespace lg {

namespace {

std::string lowercase_extension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

bool read_token(std::istream& input, std::string& token) {
    token.clear();
    char ch = '\0';
    while (input.get(ch)) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            continue;
        }
        if (ch == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        token.push_back(ch);
        break;
    }
    if (token.empty()) {
        return false;
    }
    while (input.get(ch)) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            break;
        }
        if (ch == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            break;
        }
        token.push_back(ch);
    }
    return true;
}

bool parse_positive_int(std::istream& input, int& value, std::string* error, const char* field) {
    std::string token;
    if (!read_token(input, token)) {
        if (error) {
            *error = std::string("missing Netpbm field: ") + field;
        }
        return false;
    }
    try {
        value = std::stoi(token);
    } catch (const std::exception&) {
        if (error) {
            *error = std::string("invalid integer in Netpbm field: ") + field;
        }
        return false;
    }
    if (value <= 0) {
        if (error) {
            *error = std::string("Netpbm field must be > 0: ") + field;
        }
        return false;
    }
    return true;
}

bool parse_non_negative_int(std::istream& input, int& value, std::string* error, const char* field) {
    std::string token;
    if (!read_token(input, token)) {
        if (error) {
            *error = std::string("missing Netpbm field: ") + field;
        }
        return false;
    }
    try {
        value = std::stoi(token);
    } catch (const std::exception&) {
        if (error) {
            *error = std::string("invalid integer in Netpbm field: ") + field;
        }
        return false;
    }
    if (value < 0) {
        if (error) {
            *error = std::string("Netpbm field must be >= 0: ") + field;
        }
        return false;
    }
    return true;
}

bool parse_ascii_sample(std::istream& input, int max_value, uint8_t& out, std::string* error) {
    int sample = 0;
    if (!parse_non_negative_int(input, sample, error, "sample")) {
        return false;
    }
    sample = std::clamp(sample, 0, max_value);
    out = static_cast<uint8_t>((255 * sample) / std::max(1, max_value));
    return true;
}

void set_rgba(CpuFrame& frame, int index, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    frame.data[index * 4 + 0] = r;
    frame.data[index * 4 + 1] = g;
    frame.data[index * 4 + 2] = b;
    frame.data[index * 4 + 3] = a;
}

bool load_jpeg_rgba(const std::string& path, CpuFrame& frame, std::string* error) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        if (error) {
            *error = "failed to open JPEG image file: " + path;
        }
        return false;
    }

    jpeg_decompress_struct info{};
    jpeg_error_mgr err{};
    info.err = jpeg_std_error(&err);
    jpeg_create_decompress(&info);
    jpeg_stdio_src(&info, file);
    jpeg_read_header(&info, TRUE);
    info.out_color_space = JCS_RGB;
    jpeg_start_decompress(&info);

    frame.width = static_cast<int>(info.output_width);
    frame.height = static_cast<int>(info.output_height);
    frame.channels = 4;
    frame.data.assign(static_cast<size_t>(frame.width * frame.height * 4), 0);

    std::vector<uint8_t> scanline(static_cast<size_t>(info.output_width) * info.output_components, 0);
    while (info.output_scanline < info.output_height) {
        unsigned char* row_ptr = scanline.data();
        jpeg_read_scanlines(&info, &row_ptr, 1);
        const int y = static_cast<int>(info.output_scanline) - 1;
        for (int x = 0; x < frame.width; ++x) {
            const size_t src = static_cast<size_t>(x) * 3;
            const size_t dst = static_cast<size_t>((y * frame.width + x) * 4);
            frame.data[dst + 0] = scanline[src + 0];
            frame.data[dst + 1] = scanline[src + 1];
            frame.data[dst + 2] = scanline[src + 2];
            frame.data[dst + 3] = 255;
        }
    }

    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    std::fclose(file);
    return true;
}

bool load_png_rgba(const std::string& path, CpuFrame& frame, std::string* error) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        if (error) {
            *error = "failed to open PNG image file: " + path;
        }
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        if (error) {
            *error = "failed to create PNG read struct";
        }
        std::fclose(file);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        if (error) {
            *error = "failed to create PNG info struct";
        }
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(file);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        if (error) {
            *error = "failed to decode PNG image: " + path;
        }
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    png_init_io(png, file);
    png_read_info(png, info);

    png_uint_32 width = 0;
    png_uint_32 height = 0;
    int bit_depth = 0;
    int color_type = 0;
    png_get_IHDR(png, info, &width, &height, &bit_depth, &color_type, nullptr, nullptr, nullptr);

    if (bit_depth == 16) {
        png_set_strip_16(png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    if (!(color_type & PNG_COLOR_MASK_ALPHA)) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    png_read_update_info(png, info);

    frame.width = static_cast<int>(width);
    frame.height = static_cast<int>(height);
    frame.channels = 4;
    frame.data.assign(static_cast<size_t>(frame.width * frame.height * 4), 0);

    std::vector<png_bytep> rows(static_cast<size_t>(frame.height));
    for (int y = 0; y < frame.height; ++y) {
        rows[static_cast<size_t>(y)] = frame.data.data() + static_cast<size_t>(y * frame.width * 4);
    }
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);

    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(file);
    return true;
}

bool save_png(const std::string& path, const CpuFrame& frame, std::string* error) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        if (error) {
            *error = "failed to open output PNG image: " + path;
        }
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        if (error) {
            *error = "failed to create PNG write struct";
        }
        std::fclose(file);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        if (error) {
            *error = "failed to create PNG info struct";
        }
        png_destroy_write_struct(&png, nullptr);
        std::fclose(file);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        if (error) {
            *error = "failed to encode PNG image: " + path;
        }
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        return false;
    }

    png_init_io(png, file);
    const int color_type = frame.channels == 1
        ? PNG_COLOR_TYPE_GRAY
        : (frame.channels == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA);
    png_set_IHDR(
        png,
        info,
        static_cast<png_uint_32>(frame.width),
        static_cast<png_uint_32>(frame.height),
        8,
        color_type,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE,
        PNG_FILTER_TYPE_BASE);
    png_write_info(png, info);

    const size_t row_stride = static_cast<size_t>(frame.width * frame.channels);
    std::vector<png_bytep> rows(static_cast<size_t>(frame.height));
    for (int y = 0; y < frame.height; ++y) {
        rows[static_cast<size_t>(y)] = const_cast<png_bytep>(frame.data.data() + static_cast<size_t>(y) * row_stride);
    }
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);
    std::fclose(file);
    return true;
}

bool load_netpbm_rgba(const std::string& path, CpuFrame& frame, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error) {
            *error = "failed to open image file: " + path;
        }
        return false;
    }

    std::string magic;
    if (!read_token(input, magic)) {
        if (error) {
            *error = "empty image file: " + path;
        }
        return false;
    }

    if (magic != "P2" && magic != "P3" && magic != "P5" && magic != "P6") {
        if (error) {
            *error = "unsupported image format in " + path + " (expected Netpbm P2/P3/P5/P6)";
        }
        return false;
    }

    int width = 0;
    int height = 0;
    int max_value = 0;
    if (!parse_positive_int(input, width, error, "width") ||
        !parse_positive_int(input, height, error, "height") ||
        !parse_positive_int(input, max_value, error, "max_value")) {
        return false;
    }
    if (max_value > 255) {
        if (error) {
            *error = "only Netpbm max_value <= 255 is supported";
        }
        return false;
    }

    frame.width = width;
    frame.height = height;
    frame.channels = 4;
    frame.data.assign(static_cast<size_t>(width * height * 4), 0);

    const bool ascii = (magic == "P2" || magic == "P3");
    const bool grayscale = (magic == "P2" || magic == "P5");

    if (!ascii) {
        char whitespace = '\0';
        input.get(whitespace);
        while (input && std::isspace(static_cast<unsigned char>(whitespace))) {
            if (!input.get(whitespace)) {
                break;
            }
        }
        if (input) {
            input.unget();
        }
    }

    if (ascii) {
        for (int i = 0; i < width * height; ++i) {
            if (grayscale) {
                uint8_t v = 0;
                if (!parse_ascii_sample(input, max_value, v, error)) {
                    return false;
                }
                set_rgba(frame, i, v, v, v);
            } else {
                std::array<uint8_t, 3> rgb{};
                for (int c = 0; c < 3; ++c) {
                    if (!parse_ascii_sample(input, max_value, rgb[c], error)) {
                        return false;
                    }
                }
                set_rgba(frame, i, rgb[0], rgb[1], rgb[2]);
            }
        }
        return true;
    }

    const size_t sample_count = static_cast<size_t>(width) * static_cast<size_t>(height) * (grayscale ? 1u : 3u);
    std::vector<uint8_t> raw(sample_count);
    input.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (input.gcount() != static_cast<std::streamsize>(raw.size())) {
        if (error) {
            *error = "unexpected EOF while reading image pixels";
        }
        return false;
    }

    for (int i = 0; i < width * height; ++i) {
        if (grayscale) {
            const uint8_t v = raw[static_cast<size_t>(i)];
            set_rgba(frame, i, v, v, v);
        } else {
            const size_t base = static_cast<size_t>(i) * 3;
            set_rgba(frame, i, raw[base + 0], raw[base + 1], raw[base + 2]);
        }
    }
    return true;
}

}  // namespace

bool load_image_file_rgba(const std::string& path, CpuFrame& frame, std::string* error) {
    const std::string ext = lowercase_extension(path);
    if (ext == "jpg" || ext == "jpeg") {
        return load_jpeg_rgba(path, frame, error);
    }
    if (ext == "png") {
        return load_png_rgba(path, frame, error);
    }
    return load_netpbm_rgba(path, frame, error);
}

bool save_image_file(const std::string& path, const CpuFrame& frame, std::string* error) {
    if (frame.width <= 0 || frame.height <= 0 || frame.data.empty()) {
        if (error) {
            *error = "cannot save empty image";
        }
        return false;
    }
    if (frame.channels != 1 && frame.channels != 3 && frame.channels != 4) {
        if (error) {
            *error = "unsupported channel count for image save";
        }
        return false;
    }

    const std::string ext = lowercase_extension(path);
    if (ext == "png") {
        return save_png(path, frame, error);
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        if (error) {
            *error = "failed to open output image: " + path;
        }
        return false;
    }

    if (frame.channels == 1) {
        output << "P5\n" << frame.width << " " << frame.height << "\n255\n";
        output.write(reinterpret_cast<const char*>(frame.data.data()), static_cast<std::streamsize>(frame.data.size()));
        return output.good();
    }

    output << "P6\n" << frame.width << " " << frame.height << "\n255\n";
    std::vector<uint8_t> rgb(static_cast<size_t>(frame.width * frame.height * 3));
    for (int i = 0; i < frame.width * frame.height; ++i) {
        if (frame.channels == 3) {
            rgb[static_cast<size_t>(i) * 3 + 0] = frame.data[static_cast<size_t>(i) * 3 + 0];
            rgb[static_cast<size_t>(i) * 3 + 1] = frame.data[static_cast<size_t>(i) * 3 + 1];
            rgb[static_cast<size_t>(i) * 3 + 2] = frame.data[static_cast<size_t>(i) * 3 + 2];
        } else {
            rgb[static_cast<size_t>(i) * 3 + 0] = frame.data[static_cast<size_t>(i) * 4 + 0];
            rgb[static_cast<size_t>(i) * 3 + 1] = frame.data[static_cast<size_t>(i) * 4 + 1];
            rgb[static_cast<size_t>(i) * 3 + 2] = frame.data[static_cast<size_t>(i) * 4 + 2];
        }
    }
    output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return output.good();
}

AspectFitRect compute_aspect_fit(int src_width, int src_height, int dst_width, int dst_height) {
    AspectFitRect rect{};
    if (src_width <= 0 || src_height <= 0 || dst_width <= 0 || dst_height <= 0) {
        return rect;
    }

    const double scale = std::min(
        static_cast<double>(dst_width) / static_cast<double>(src_width),
        static_cast<double>(dst_height) / static_cast<double>(src_height));
    rect.width = std::max(1, static_cast<int>(src_width * scale + 0.5));
    rect.height = std::max(1, static_cast<int>(src_height * scale + 0.5));
    rect.x = (dst_width - rect.width) / 2;
    rect.y = (dst_height - rect.height) / 2;
    return rect;
}

CpuFrame resize_with_letterbox(const CpuFrame& source, int dst_width, int dst_height) {
    CpuFrame out;
    out.width = dst_width;
    out.height = dst_height;
    out.channels = 4;
    out.data.assign(static_cast<size_t>(dst_width * dst_height * 4), 0);
    for (int i = 0; i < dst_width * dst_height; ++i) {
        out.data[static_cast<size_t>(i) * 4 + 3] = 255;
    }

    if (source.width <= 0 || source.height <= 0 || source.channels != 4 || source.data.empty()) {
        return out;
    }

    const AspectFitRect fit = compute_aspect_fit(source.width, source.height, dst_width, dst_height);
    if (fit.width <= 0 || fit.height <= 0) {
        return out;
    }

    for (int y = 0; y < fit.height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(fit.height);
        const int sy = std::clamp(static_cast<int>(v * source.height), 0, source.height - 1);
        for (int x = 0; x < fit.width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(fit.width);
            const int sx = std::clamp(static_cast<int>(u * source.width), 0, source.width - 1);
            const size_t src_index = static_cast<size_t>((sy * source.width + sx) * 4);
            const size_t dst_index = static_cast<size_t>(((fit.y + y) * dst_width + (fit.x + x)) * 4);
            out.data[dst_index + 0] = source.data[src_index + 0];
            out.data[dst_index + 1] = source.data[src_index + 1];
            out.data[dst_index + 2] = source.data[src_index + 2];
            out.data[dst_index + 3] = source.data[src_index + 3];
        }
    }
    return out;
}

bool ImageSource::open(const char* path) {
    std::string error;
    if (!path || !load_image_file_rgba(path, frame_, &error)) {
        frame_ = {};
        emitted_ = false;
        return false;
    }
    emitted_=false;
    return true;
}
bool ImageSource::next(CpuFrame& frame) { if (emitted_) return false; frame=frame_; emitted_=true; return true; }
void ImageSource::reset() { emitted_=false; }
}

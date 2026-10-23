#include <obs-module.h>
#include <graphics/graphics.h>
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace {
using namespace Gdiplus;

enum class TimerMode { CountUp = 0, Countdown = 1 };
enum class TitlePosition { Automatic = 0, Above = 1, Below = 2 };
enum class Alignment { Left = 0, Center = 1, Right = 2 };
enum class TimeFormat { HoursMinutesSeconds = 0, TotalMinutesSeconds = 1, Hundredths = 2 };

struct TimerSource {
    mutable std::mutex mutex;
    TimerMode mode = TimerMode::CountUp;
    TitlePosition title_position = TitlePosition::Automatic;
    Alignment alignment = Alignment::Center;
    TimeFormat time_format = TimeFormat::HoursMinutesSeconds;
    std::string title = "YOUR TITLE";
    std::string font_family = "Arial";
    int time_size = 72;
    int title_size = 36;
    int letter_spacing = 0;
    int line_gap = 2;
    int initial_hours = 0;
    int initial_minutes = 0;
    int initial_seconds = 0;
    bool show_title = true;
    bool start_on_create = true;
    bool countdown_stop_at_zero = true;
    bool shadow_enabled = true;
    bool outline_enabled = false;
    int shadow_x = 2;
    int shadow_y = 4;
    int outline_width = 2;
    uint32_t time_color = 0xFFFFFFFF;
    uint32_t title_color = 0xFFE8E8E8;
    uint32_t shadow_color = 0xB8000000;
    uint32_t outline_color = 0xE6000000;

    bool running = true;
    double elapsed = 0.0;
    int64_t initial_total = 0;
    bool last_start_on_create = true;
    int width = 1;
    int height = 1;
    bool texture_dirty = true;
    gs_texture_t *texture = nullptr;
    std::vector<uint8_t> rgba;
    std::string last_time_text;
    std::string last_title_text;
};

static std::wstring utf8_to_wide(const std::string &value)
{
    if (value.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (count <= 1)
        return std::wstring(value.begin(), value.end());
    std::wstring out(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), count);
    out.pop_back();
    return out;
}

static int64_t configured_initial_total(const TimerSource *d)
{
    return static_cast<int64_t>(std::max(0, d->initial_hours)) * 3600 +
           static_cast<int64_t>(std::max(0, d->initial_minutes)) * 60 +
           static_cast<int64_t>(std::max(0, d->initial_seconds));
}

static Color to_color(uint32_t value)
{
    return Color(static_cast<BYTE>((value >> 24) & 0xFF),
                 static_cast<BYTE>((value >> 16) & 0xFF),
                 static_cast<BYTE>((value >> 8) & 0xFF),
                 static_cast<BYTE>(value & 0xFF));
}

static std::string format_time(const TimerSource *d)
{
    const double value = d->mode == TimerMode::CountUp
        ? static_cast<double>(d->initial_total) + d->elapsed
        : static_cast<double>(d->initial_total) - d->elapsed;
    const bool negative = value < 0.0;
    const double absolute = std::fabs(value);
    const int64_t whole = static_cast<int64_t>(std::floor(absolute));
    const int64_t hours = whole / 3600;
    const int64_t minutes = (whole / 60) % 60;
    const int64_t total_minutes = whole / 60;
    const int64_t seconds = whole % 60;
    char buffer[80]{};
    if (d->time_format == TimeFormat::TotalMinutesSeconds) {
        std::snprintf(buffer, sizeof(buffer), "%s%02lld:%02lld", negative ? "-" : "",
                      static_cast<long long>(total_minutes), static_cast<long long>(seconds));
    } else if (d->time_format == TimeFormat::Hundredths) {
        const int hundredths = static_cast<int>(std::floor((absolute - std::floor(absolute)) * 100.0));
        std::snprintf(buffer, sizeof(buffer), "%s%02lld:%02lld:%02lld.%02d", negative ? "-" : "",
                      static_cast<long long>(hours), static_cast<long long>(minutes),
                      static_cast<long long>(seconds), hundredths);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%s%02lld:%02lld:%02lld", negative ? "-" : "",
                      static_cast<long long>(hours), static_cast<long long>(minutes),
                      static_cast<long long>(seconds));
    }
    return buffer;
}

static bool make_text_path(Graphics &measure, const std::wstring &text,
                           FontFamily &family, int size, int spacing,
                           GraphicsPath &path, RectF &bounds)
{
    path.Reset();
    bounds = RectF(0, 0, 1, 1);
    if (text.empty())
        return true;

    Font font(&family, static_cast<REAL>(size), FontStyleBold, UnitPixel);
    StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoClip | StringFormatFlagsNoFitBlackBox);
    REAL cursor = 0.0f;
    for (size_t i = 0; i < text.size();) {
        int length = 1;
        if (text[i] >= 0xD800 && text[i] <= 0xDBFF && i + 1 < text.size() &&
            text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
            length = 2;
        if (path.AddString(text.c_str() + i, length, &family, FontStyleBold,
                           static_cast<REAL>(size), PointF(cursor, 0.0f), &format) != Ok)
            return false;
        RectF advance;
        if (measure.MeasureString(text.c_str() + i, length, &font, PointF(cursor, 0.0f),
                                  &format, &advance) == Ok)
            cursor += std::max(0.0f, advance.Width) + static_cast<REAL>(spacing);
        i += static_cast<size_t>(length);
    }
    if (path.GetPointCount() == 0)
        return true;
    return path.GetBounds(&bounds) == Ok;
}

static void draw_path(Graphics &graphics, const GraphicsPath &source, const RectF &bounds,
                      REAL x, REAL y, TimerSource *d, uint32_t fill_color)
{
    if (source.GetPointCount() == 0)
        return;
    GraphicsPath path;
    path.AddPath(&source, FALSE);
    Matrix matrix;
    matrix.Translate(x - bounds.X, y - bounds.Y);
    path.Transform(&matrix);

    if (d->shadow_enabled) {
        GraphicsPath shadow;
        shadow.AddPath(&path, FALSE);
        Matrix shadow_matrix;
        shadow_matrix.Translate(static_cast<REAL>(d->shadow_x), static_cast<REAL>(d->shadow_y));
        shadow.Transform(&shadow_matrix);
        SolidBrush shadow_brush(to_color(d->shadow_color));
        graphics.FillPath(&shadow_brush, &shadow);
    }
    if (d->outline_enabled) {
        Pen outline(to_color(d->outline_color), static_cast<REAL>(d->outline_width * 2));
        outline.SetLineJoin(LineJoinRound);
        graphics.DrawPath(&outline, &path);
    }
    SolidBrush brush(to_color(fill_color));
    graphics.FillPath(&brush, &path);
}

static void rebuild_image_locked(TimerSource *d)
{
    const std::string time_text = format_time(d);
    const std::string title_text = d->show_title ? d->title : std::string();
    if (time_text == d->last_time_text && title_text == d->last_title_text && !d->rgba.empty())
        return;
    d->last_time_text = time_text;
    d->last_title_text = title_text;

    const std::wstring time_wide = utf8_to_wide(time_text);
    const std::wstring title_wide = utf8_to_wide(title_text);
    std::wstring family_name = utf8_to_wide(d->font_family);
    if (family_name.empty())
        family_name = L"Arial";
    FontFamily family(family_name.c_str());
    FontFamily fallback(L"Arial");
    FontFamily *selected_family = family.GetLastStatus() == Ok ? &family : &fallback;

    Bitmap measure_bitmap(8, 8, PixelFormat32bppARGB);
    Graphics measure(&measure_bitmap);
    measure.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    GraphicsPath time_path;
    GraphicsPath title_path;
    RectF time_bounds, title_bounds;
    make_text_path(measure, time_wide, *selected_family, d->time_size, d->letter_spacing, time_path, time_bounds);
    if (!title_wide.empty())
        make_text_path(measure, title_wide, *selected_family, d->title_size, d->letter_spacing, title_path, title_bounds);

    const bool has_title = d->show_title && !title_wide.empty();
    const REAL time_width = time_path.GetPointCount() ? time_bounds.Width : 1.0f;
    const REAL time_height = time_path.GetPointCount() ? time_bounds.Height : static_cast<REAL>(d->time_size);
    const REAL title_width = has_title && title_path.GetPointCount() ? title_bounds.Width : 0.0f;
    const REAL title_height = has_title && title_path.GetPointCount() ? title_bounds.Height : 0.0f;
    const REAL body_width = std::max(time_width, title_width);
    const bool title_above = d->title_position == TitlePosition::Above ||
        (d->title_position == TitlePosition::Automatic && d->mode == TimerMode::CountUp);
    const REAL gap = has_title ? static_cast<REAL>(d->line_gap) : 0.0f;
    const REAL time_rel_y = has_title && title_above ? title_height + gap : 0.0f;
    const REAL title_rel_y = has_title && !title_above ? time_height + gap : 0.0f;
    const REAL body_min_y = has_title ? std::min({0.0f, time_rel_y, title_rel_y}) : 0.0f;
    const REAL body_max_y = has_title ? std::max(time_rel_y + time_height, title_rel_y + title_height) : time_height;
    const int pad = std::max({4, d->outline_enabled ? d->outline_width + 3 : 0,
                              d->shadow_enabled ? std::max(std::abs(d->shadow_x), std::abs(d->shadow_y)) + 3 : 0});
    d->width = std::clamp(static_cast<int>(std::ceil(body_width)) + pad * 2, 1, 4096);
    d->height = std::clamp(static_cast<int>(std::ceil(body_max_y - body_min_y)) + pad * 2, 1, 2048);

    Bitmap image(d->width, d->height, PixelFormat32bppARGB);
    Graphics canvas(&image);
    canvas.Clear(Color(0, 0, 0, 0));
    canvas.SetSmoothingMode(SmoothingModeAntiAlias);
    canvas.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    canvas.SetCompositingQuality(CompositingQualityHighQuality);

    const REAL time_x = pad + (d->alignment == Alignment::Left ? 0.0f :
        d->alignment == Alignment::Right ? body_width - time_width : (body_width - time_width) / 2.0f);
    const REAL title_x = pad + (d->alignment == Alignment::Left ? 0.0f :
        d->alignment == Alignment::Right ? body_width - title_width : (body_width - title_width) / 2.0f);
    draw_path(canvas, time_path, time_bounds, time_x, pad + time_rel_y - body_min_y, d, d->time_color);
    if (has_title)
        draw_path(canvas, title_path, title_bounds, title_x, pad + title_rel_y - body_min_y, d, d->title_color);

    BitmapData bitmap_data{};
    Rect rect(0, 0, d->width, d->height);
    if (image.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bitmap_data) == Ok) {
        d->rgba.resize(static_cast<size_t>(d->width) * d->height * 4);
        for (int y = 0; y < d->height; ++y) {
            const auto *src = reinterpret_cast<const uint8_t *>(bitmap_data.Scan0) + y * bitmap_data.Stride;
            auto *dst = d->rgba.data() + static_cast<size_t>(y) * d->width * 4;
            for (int x = 0; x < d->width; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
        }
        image.UnlockBits(&bitmap_data);
        d->texture_dirty = true;
    }
}

static void apply_settings_locked(TimerSource *d, obs_data_t *settings, bool creating)
{
    const auto old_mode = d->mode;
    const int64_t old_initial = d->initial_total;
    const bool old_start_setting = d->last_start_on_create;
    d->mode = obs_data_get_int(settings, "mode") == 1 ? TimerMode::Countdown : TimerMode::CountUp;
    d->title_position = static_cast<TitlePosition>(std::clamp<long long>(obs_data_get_int(settings, "title_position"), 0, 2));
    d->alignment = static_cast<Alignment>(std::clamp<long long>(obs_data_get_int(settings, "alignment"), 0, 2));
    d->time_format = static_cast<TimeFormat>(std::clamp<long long>(obs_data_get_int(settings, "time_format"), 0, 2));
    d->title = obs_data_get_string(settings, "title");
    d->font_family = obs_data_get_string(settings, "font_family");
    if (d->font_family.empty()) d->font_family = "Arial";
    d->time_size = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "time_size"), 8, 300));
    d->title_size = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "title_size"), 8, 200));
    d->letter_spacing = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "letter_spacing"), -5, 30));
    d->line_gap = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "line_gap"), -80, 200));
    d->initial_hours = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "initial_hours"), 0, 999));
    d->initial_minutes = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "initial_minutes"), 0, 59));
    d->initial_seconds = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "initial_seconds"), 0, 59));
    d->show_title = obs_data_get_bool(settings, "show_title");
    d->start_on_create = obs_data_get_bool(settings, "start_on_create");
    d->countdown_stop_at_zero = obs_data_get_bool(settings, "countdown_stop_at_zero");
    d->shadow_enabled = obs_data_get_bool(settings, "shadow_enabled");
    d->outline_enabled = obs_data_get_bool(settings, "outline_enabled");
    d->shadow_x = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "shadow_x"), -40, 40));
    d->shadow_y = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "shadow_y"), -40, 40));
    d->outline_width = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "outline_width"), 1, 5));
    d->time_color = static_cast<uint32_t>(obs_data_get_int(settings, "time_color"));
    d->title_color = static_cast<uint32_t>(obs_data_get_int(settings, "title_color"));
    d->shadow_color = static_cast<uint32_t>(obs_data_get_int(settings, "shadow_color"));
    d->outline_color = static_cast<uint32_t>(obs_data_get_int(settings, "outline_color"));
    d->initial_total = configured_initial_total(d);

    if (creating) {
        d->elapsed = 0.0;
        d->running = d->start_on_create;
    } else if (old_mode != d->mode || old_initial != d->initial_total) {
        d->elapsed = 0.0;
        d->running = d->start_on_create;
    } else if (old_start_setting != d->start_on_create) {
        d->running = d->start_on_create;
    }
    d->last_start_on_create = d->start_on_create;
    d->last_time_text.clear();
    d->last_title_text.clear();
    rebuild_image_locked(d);
}

static void *timer_create(obs_data_t *settings, obs_source_t *)
{
    auto *d = new TimerSource();
    std::lock_guard<std::mutex> lock(d->mutex);
    apply_settings_locked(d, settings, true);
    return d;
}

static void timer_destroy(void *data)
{
    auto *d = static_cast<TimerSource *>(data);
    if (!d) return;
    if (d->texture) {
        obs_enter_graphics();
        gs_texture_destroy(d->texture);
        obs_leave_graphics();
    }
    delete d;
}

static void timer_update(void *data, obs_data_t *settings)
{
    auto *d = static_cast<TimerSource *>(data);
    if (!d) return;
    std::lock_guard<std::mutex> lock(d->mutex);
    apply_settings_locked(d, settings, false);
}

static void timer_tick(void *data, float seconds)
{
    auto *d = static_cast<TimerSource *>(data);
    if (!d) return;
    std::lock_guard<std::mutex> lock(d->mutex);
    const std::string before = format_time(d);
    if (d->running && seconds > 0.0f) {
        d->elapsed += seconds;
        if (d->mode == TimerMode::Countdown && d->countdown_stop_at_zero && d->elapsed >= d->initial_total) {
            d->elapsed = static_cast<double>(d->initial_total);
            d->running = false;
        }
    }
    if (format_time(d) != before)
        rebuild_image_locked(d);
}

static void timer_render(void *data, gs_effect_t *)
{
    auto *d = static_cast<TimerSource *>(data);
    if (!d) return;
    std::lock_guard<std::mutex> lock(d->mutex);
    if (d->texture_dirty) {
        if (d->texture) {
            gs_texture_destroy(d->texture);
            d->texture = nullptr;
        }
        if (!d->rgba.empty()) {
            const uint8_t *pixels = d->rgba.data();
            d->texture = gs_texture_create(static_cast<uint32_t>(d->width), static_cast<uint32_t>(d->height),
                                           GS_RGBA, 1, &pixels, 0);
        }
        d->texture_dirty = d->texture == nullptr;
    }
    if (d->texture)
        obs_source_draw(d->texture, 0, 0, static_cast<uint32_t>(d->width),
                        static_cast<uint32_t>(d->height), false);
}

static uint32_t timer_width(void *data)
{
    auto *d = static_cast<TimerSource *>(data);
    if (!d) return 1;
    std::lock_guard<std::mutex> lock(d->mutex);
    return static_cast<uint32_t>(std::max(1, d->width));
}

static uint32_t timer_height(void *data)
{
    auto *d = static_cast<TimerSource *>(data);
    if (!d) return 1;
    std::lock_guard<std::mutex> lock(d->mutex);
    return static_cast<uint32_t>(std::max(1, d->height));
}

static void timer_defaults(obs_data_t *s)
{
    obs_data_set_default_int(s, "mode", 0);
    obs_data_set_default_int(s, "title_position", 0);
    obs_data_set_default_int(s, "alignment", 1);
    obs_data_set_default_int(s, "time_format", 0);
    obs_data_set_default_string(s, "title", "YOUR TITLE");
    obs_data_set_default_string(s, "font_family", "Arial");
    obs_data_set_default_int(s, "time_size", 72);
    obs_data_set_default_int(s, "title_size", 36);
    obs_data_set_default_int(s, "letter_spacing", 0);
    obs_data_set_default_int(s, "line_gap", 2);
    obs_data_set_default_int(s, "initial_hours", 0);
    obs_data_set_default_int(s, "initial_minutes", 0);
    obs_data_set_default_int(s, "initial_seconds", 0);
    obs_data_set_default_bool(s, "show_title", true);
    obs_data_set_default_bool(s, "start_on_create", true);
    obs_data_set_default_bool(s, "countdown_stop_at_zero", true);
    obs_data_set_default_bool(s, "shadow_enabled", true);
    obs_data_set_default_bool(s, "outline_enabled", false);
    obs_data_set_default_int(s, "shadow_x", 2);
    obs_data_set_default_int(s, "shadow_y", 4);
    obs_data_set_default_int(s, "outline_width", 2);
    obs_data_set_default_int(s, "time_color", static_cast<int64_t>(0xFFFFFFFFu));
    obs_data_set_default_int(s, "title_color", static_cast<int64_t>(0xFFE8E8E8u));
    obs_data_set_default_int(s, "shadow_color", static_cast<int64_t>(0xB8000000u));
    obs_data_set_default_int(s, "outline_color", static_cast<int64_t>(0xE6000000u));
}

static bool toggle_running_clicked(obs_properties_t *, obs_property_t *, void *data)
{
    auto *d = static_cast<TimerSource *>(data);
    if (!d) return false;
    std::lock_guard<std::mutex> lock(d->mutex);
    if (d->mode == TimerMode::Countdown && d->countdown_stop_at_zero && d->elapsed >= d->initial_total)
        d->elapsed = 0.0;
    d->running = !d->running;
    d->last_time_text.clear();
    rebuild_image_locked(d);
    return true;
}

static bool reset_clicked(obs_properties_t *, obs_property_t *, void *data)
{
    auto *d = static_cast<TimerSource *>(data);
    if (!d) return false;
    std::lock_guard<std::mutex> lock(d->mutex);
    d->elapsed = 0.0;
    d->running = true;
    d->last_time_text.clear();
    rebuild_image_locked(d);
    return true;
}

static obs_properties_t *timer_properties(void *data)
{
    auto *d = static_cast<TimerSource *>(data);
    auto *p = obs_properties_create();
    auto *mode = obs_properties_add_list(p, "mode", "Timer mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(mode, "Count up (stopwatch)", 0);
    obs_property_list_add_int(mode, "Countdown", 1);
    auto *format = obs_properties_add_list(p, "time_format", "Time format", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(format, "HH:MM:SS", 0);
    obs_property_list_add_int(format, "Total MM:SS", 1);
    obs_property_list_add_int(format, "HH:MM:SS.hh (hundredths)", 2);
    obs_properties_add_int(p, "initial_hours", "Starting / countdown hours", 0, 999, 1);
    obs_properties_add_int(p, "initial_minutes", "Starting / countdown minutes", 0, 59, 1);
    obs_properties_add_int(p, "initial_seconds", "Starting / countdown seconds", 0, 59, 1);
    obs_properties_add_bool(p, "start_on_create", "Start automatically when source is created");
    obs_properties_add_bool(p, "countdown_stop_at_zero", "Stop countdown at zero");
    bool running = true;
    if (d) {
        std::lock_guard<std::mutex> lock(d->mutex);
        running = d->running;
    }
    obs_properties_add_button2(p, "toggle_timer", running ? "Pause timer" : "Start timer", toggle_running_clicked, d);
    obs_properties_add_button2(p, "reset_timer", "Reset timer", reset_clicked, d);

    obs_properties_add_text(p, "title", "Title text", OBS_TEXT_DEFAULT);
    obs_properties_add_bool(p, "show_title", "Show title");
    auto *position = obs_properties_add_list(p, "title_position", "Title placement", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(position, "Auto (above for count up, below for countdown)", 0);
    obs_property_list_add_int(position, "Above timer", 1);
    obs_property_list_add_int(position, "Below timer", 2);
    auto *alignment = obs_properties_add_list(p, "alignment", "Text alignment", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(alignment, "Left", 0);
    obs_property_list_add_int(alignment, "Center", 1);
    obs_property_list_add_int(alignment, "Right", 2);
    obs_properties_add_text(p, "font_family", "Font family (for example Arial)", OBS_TEXT_DEFAULT);
    obs_properties_add_int(p, "time_size", "Timer font size", 8, 300, 1);
    obs_properties_add_int(p, "title_size", "Title font size", 8, 200, 1);
    obs_properties_add_int(p, "letter_spacing", "Letter spacing (px)", -5, 30, 1);
    obs_properties_add_int(p, "line_gap", "Spacing between timer and title (px)", -80, 200, 1);
    obs_properties_add_color_alpha(p, "time_color", "Timer color");
    obs_properties_add_color_alpha(p, "title_color", "Title color");
    obs_properties_add_bool(p, "shadow_enabled", "Enable text shadow");
    obs_properties_add_color_alpha(p, "shadow_color", "Shadow color / opacity");
    obs_properties_add_int(p, "shadow_x", "Shadow horizontal offset (px)", -40, 40, 1);
    obs_properties_add_int(p, "shadow_y", "Shadow vertical offset (px)", -40, 40, 1);
    obs_properties_add_bool(p, "outline_enabled", "Enable text outline");
    obs_properties_add_color_alpha(p, "outline_color", "Outline color / opacity");
    obs_properties_add_int(p, "outline_width", "Outline thickness (px)", 1, 5, 1);
    return p;
}

static const char *timer_name(void *) { return obs_module_text("CounterTimer"); }
} // namespace

void register_counter_timer_source()
{
    static obs_source_info info{};
    info.id = "counter_timer_source";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO;
    info.get_name = timer_name;
    info.create = timer_create;
    info.destroy = timer_destroy;
    info.get_width = timer_width;
    info.get_height = timer_height;
    info.get_defaults = timer_defaults;
    info.get_properties = timer_properties;
    info.update = timer_update;
    info.video_tick = timer_tick;
    info.video_render = timer_render;
    obs_register_source(&info);
}

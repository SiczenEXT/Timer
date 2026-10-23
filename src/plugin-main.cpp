#include <obs-module.h>
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("counter-timer", "en-US")

extern void register_counter_timer_source();

static ULONG_PTR gdiplus_token = 0;

bool obs_module_load(void)
{
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &input, nullptr) != Gdiplus::Ok) {
        blog(LOG_ERROR, "Counter Timer: GDI+ initialization failed");
        return false;
    }
    register_counter_timer_source();
    blog(LOG_INFO, "Counter Timer source loaded");
    return true;
}

void obs_module_unload(void)
{
    if (gdiplus_token != 0) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        gdiplus_token = 0;
    }
}

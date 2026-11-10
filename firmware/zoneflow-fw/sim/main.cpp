// SPDX-License-Identifier: Apache-2.0
// ZoneFlow display simulator. See README.md in this directory.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "sim.hpp"

namespace {

using namespace zf;

struct ShotContext {
    sim::Screen *screen = nullptr;
    std::string dir; // empty: render only
    int scale = 2;
    int count = 0;
    bool failed = false;
};

void takeShot(void *ctx, const std::string &name, test::UiHarness &world)
{
    auto *c = static_cast<ShotContext *>(ctx);
    sim::Screen::tickMs = static_cast<uint32_t>(world.clock.mono);
    c->screen->render(world.frame());
    ++c->count;
    if (c->dir.empty()) {
        return;
    }
    const std::string file = c->dir + "/" + name + ".png";
    // Dimmed and off are shown darker, in proportion to the set brightness.
    const int backlight = world.ui->backlightPercent() * 100 / world.ui->settings().brightnessPercent;
    if (!c->screen->savePng(file, c->scale, backlight)) {
        std::fprintf(stderr, "could not write %s\n", file.c_str());
        c->failed = true;
    }
}

void usage()
{
    std::puts("zoneflow-sim: ZoneFlow display simulator\n"
              "\n"
              "  (no arguments)          open a window (if built with SDL2)\n"
              "  --wizard                window: start with the first-boot wizard\n"
              "  --scale <n>             window or PNG pixel scale (default 2)\n"
              "  --screenshots <dir>     headless: write a PNG of every screen into <dir>\n"
              "  --report                headless: draw every screen, print LVGL memory use\n"
              "\n"
              "Window keys: Left/Right or mouse wheel = turn, Enter/Space = press,\n"
              "Backspace/Esc = Back, H or M = Manual/Home (hold to stop everything).\n"
              "F = cut the wire of zone 3, R = rain sensor, A = 24 V AC, W = WiFi, Q = quit.");
}

} // namespace

int main(int argc, char **argv)
{
    std::string shots;
    bool report = false;
    bool wizard = false;
    int scale = 2;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--screenshots" && i + 1 < argc) {
            shots = argv[++i];
        } else if (arg == "--scale" && i + 1 < argc) {
            scale = std::atoi(argv[++i]);
        } else if (arg == "--report") {
            report = true;
        } else if (arg == "--wizard") {
            wizard = true;
        } else {
            usage();
            return arg == "--help" || arg == "-h" ? 0 : 2;
        }
    }
    if (scale < 1 || scale > 8) {
        std::fprintf(stderr, "--scale must be 1..8\n");
        return 2;
    }

    sim::Screen screen;

    if (!shots.empty() || report) {
        ShotContext ctx;
        ctx.screen = &screen;
        ctx.scale = scale;
        if (!shots.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(shots, ec);
            if (ec) {
                std::fprintf(stderr, "cannot create %s: %s\n", shots.c_str(), ec.message().c_str());
                return 1;
            }
            ctx.dir = shots;
        }
        // Measure before any PNG is encoded: the encoder shares LVGL's heap.
        lv_mem_monitor_t before;
        lv_mem_monitor(&before);
        if (report) {
            ShotContext dry;
            dry.screen = &screen;
            const int scenes = sim::runScenes(&takeShot, &dry);
            lv_mem_monitor_t after;
            lv_mem_monitor(&after);
            std::printf("scenes drawn:            %d\n", scenes);
            std::printf("LVGL heap after create:  %zu bytes (object tree of the view)\n",
                        static_cast<std::size_t>(before.total_size - before.free_size));
            std::printf(
                "LVGL heap peak:          %zu bytes (64-bit host; pointers are half the size on the ESP32-S3)\n",
                static_cast<std::size_t>(after.max_used));
            std::printf("draw buffer:             %d bytes (%d x %d px, RGB565)\n", sim::kWidth * sim::kStripeLines * 2,
                        sim::kWidth, sim::kStripeLines);
        }
        if (!shots.empty()) {
            const int scenes = sim::runScenes(&takeShot, &ctx);
            if (ctx.failed) {
                return 1;
            }
            std::printf("%d screenshots written to %s\n", scenes, shots.c_str());
        }
        return 0;
    }

#ifdef ZF_SIM_HAVE_SDL
    return sim::runWindow(screen, wizard, scale);
#else
    (void)wizard;
    std::fprintf(stderr, "Built without SDL2: no window. Use --screenshots <dir> or --report.\n");
    return 2;
#endif
}

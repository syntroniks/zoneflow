// SPDX-License-Identifier: Apache-2.0
// Interactive window. Keys go through the same ButtonDecoder, EncoderDecoder
// and Accelerator the device uses, so debounce, long press and acceleration
// behave as on the hardware.
#include <SDL.h>

#include <chrono>
#include <ctime>
#include <vector>

#include "sim.hpp"
#include "zf_ui/input.hpp"

namespace zf::sim {

int runWindow(Screen &screen, bool wizard, int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_Window *window =
        SDL_CreateWindow("ZoneFlow  |  arrows: turn  Enter: press  Backspace: back  H: manual/home (hold: stop)",
                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kWidth * scale, kHeight * scale, 0);
    SDL_Renderer *renderer = window != nullptr ? SDL_CreateRenderer(window, -1, 0) : nullptr;
    SDL_Texture *texture = renderer != nullptr ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                                                   SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight)
                                               : nullptr;
    if (texture == nullptr) {
        SDL_Log("SDL window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    World world(!wizard);
    test::UiHarness &h = world.h;
    // Show the host's time of day.
    h.systemTime.t = static_cast<UtcSeconds>(std::time(nullptr));
    h.time.onNetworkSync();

    ui::ButtonTiming selectTiming;
    ui::ButtonTiming backTiming;
    backTiming.repeatMs = 400;
    ui::ButtonTiming homeTiming;
    homeTiming.longPressMs = 800;
    ui::ButtonDecoder select(selectTiming);
    ui::ButtonDecoder back(backTiming);
    ui::ButtonDecoder home(homeTiming);
    ui::EncoderDecoder encoder;
    ui::Accelerator accelerator;
    int32_t encoderCount = 0;
    bool keySelect = false;
    bool keyBack = false;
    bool keyHome = false;

    bool running = true;
    uint32_t shownSerial = 0;
    int shownBacklight = -1;
    auto next = std::chrono::steady_clock::now();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_MOUSEWHEEL) {
                encoderCount += 4 * (e.wheel.y > 0 ? -1 : 1);
            } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                const bool down = e.type == SDL_KEYDOWN;
                switch (e.key.keysym.sym) {
                case SDLK_RIGHT:
                case SDLK_DOWN:
                    encoderCount += down ? 4 : 0; // key repeat = a steady spin
                    break;
                case SDLK_LEFT:
                case SDLK_UP:
                    encoderCount -= down ? 4 : 0;
                    break;
                case SDLK_RETURN:
                case SDLK_SPACE:
                    keySelect = down;
                    break;
                case SDLK_BACKSPACE:
                case SDLK_ESCAPE:
                    keyBack = down;
                    break;
                case SDLK_h:
                case SDLK_m:
                    keyHome = down;
                    break;
                case SDLK_f:
                    if (down && e.key.repeat == 0) {
                        h.zoneMa[2] = h.zoneMa[2] == 0 ? 260 : 0;
                    }
                    break;
                case SDLK_r:
                    if (down && e.key.repeat == 0) {
                        h.sensors.sample.rainContactClosed = !h.sensors.sample.rainContactClosed;
                    }
                    break;
                case SDLK_a:
                    if (down && e.key.repeat == 0) {
                        h.sensors.sample.acPresent = !h.sensors.sample.acPresent;
                    }
                    break;
                case SDLK_w:
                    if (down && e.key.repeat == 0) {
                        if (h.net.net.link == display::WifiLink::Connected) {
                            h.net.net.link = display::WifiLink::Disconnected;
                        } else {
                            h.net.connect();
                        }
                    }
                    break;
                case SDLK_q:
                    running = false;
                    break;
                default:
                    break;
                }
            }
        }

        // Sample the controls every 10 ms of simulated time, five times per
        // 50 ms controller tick, as the device's UI task does.
        for (int i = 0; i < 5; ++i) {
            const MonoMs now = h.clock.mono + static_cast<MonoMs>(i) * 10;
            const int steps = encoder.update(encoderCount, now);
            if (steps != 0) {
                h.ui->handleInput(ui::InputEvent::turn(steps, accelerator.apply(steps, now)), now);
            }
            struct {
                ui::ButtonDecoder &decoder;
                bool down;
                ui::Key key;
            } buttons[] = {
                {select, keySelect, ui::Key::Select}, {back, keyBack, ui::Key::Back}, {home, keyHome, ui::Key::Home}};
            for (auto &b : buttons) {
                switch (b.decoder.update(b.down, now)) {
                case ui::ButtonEdge::Press:
                    h.ui->handleInput(ui::InputEvent::press(b.key), now);
                    break;
                case ui::ButtonEdge::LongPress:
                    h.ui->handleInput(ui::InputEvent::press(b.key, ui::InputType::LongPress), now);
                    break;
                case ui::ButtonEdge::Repeat:
                    h.ui->handleInput(ui::InputEvent::press(b.key, ui::InputType::Repeat), now);
                    break;
                case ui::ButtonEdge::None:
                    break;
                }
            }
        }
        h.step();
        Screen::tickMs = static_cast<uint32_t>(h.clock.mono);

        const int backlight = h.ui->backlightPercent();
        if (h.ui->frameSerial() != shownSerial || backlight != shownBacklight) {
            shownSerial = h.ui->frameSerial();
            shownBacklight = backlight;
            screen.render(h.frame());
            SDL_UpdateTexture(texture, nullptr, screen.pixels(), kWidth * static_cast<int>(sizeof(uint16_t)));
            // The backlight level is shown as brightness of the picture.
            const auto level = static_cast<Uint8>(40 + backlight * 215 / 100);
            SDL_SetTextureColorMod(texture, level, level, level);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }

        next += std::chrono::milliseconds(50);
        const auto now = std::chrono::steady_clock::now();
        if (next > now) {
            SDL_Delay(static_cast<Uint32>(std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count()));
        } else {
            next = now;
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace zf::sim

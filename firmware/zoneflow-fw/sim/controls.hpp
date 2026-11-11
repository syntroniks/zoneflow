// SPDX-License-Identifier: Apache-2.0
// The simulated encoder and buttons. Raw levels and counts go through the
// same ButtonDecoder, EncoderDecoder and Accelerator the device uses, with
// the device's timings and counts per detent, so debounce, long press,
// repeat and acceleration behave as on the hardware.
// Shared by the SDL window and the browser build.
#pragma once

#include <cstdint>

#include "ui_harness.hpp"
#include "zf_board/board.hpp"
#include "zf_ui/input.hpp"

namespace zf::sim {

class Controls {
public:
    static constexpr int kDetent = board::kEncoderCountsPerDetent;

    // Raw inputs, set by the front end at any time.
    int32_t encoderCount = 0; // quadrature counts; kDetent per detent
    bool selectDown = false;
    bool backDown = false;
    bool homeDown = false;

    // Samples the controls every 10 ms of simulated time for the next 50 ms
    // controller tick, as the device's UI task does. Call before h.step().
    void sample(test::UiHarness &h)
    {
        for (int i = 0; i < 5; ++i) {
            const MonoMs now = h.clock.mono + static_cast<MonoMs>(i) * 10;
            const int steps = encoder_.update(encoderCount, now);
            if (steps != 0) {
                h.ui->handleInput(ui::InputEvent::turn(steps, accelerator_.apply(steps, now)), now);
            }
            struct {
                ui::ButtonDecoder &decoder;
                bool down;
                ui::Key key;
            } buttons[] = {{select_, selectDown, ui::Key::Select},
                           {back_, backDown, ui::Key::Back},
                           {home_, homeDown, ui::Key::Home}};
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
                case ui::ButtonEdge::Hold:
                    h.ui->handleInput(ui::InputEvent::press(b.key, ui::InputType::Hold), now);
                    break;
                case ui::ButtonEdge::None:
                    break;
                }
            }
        }
    }

private:
    ui::ButtonDecoder select_{ui::selectButtonTiming()};
    ui::ButtonDecoder back_{ui::backButtonTiming()};
    ui::ButtonDecoder home_{ui::homeButtonTiming()};
    ui::EncoderDecoder encoder_{kDetent};
    ui::Accelerator accelerator_;
};

} // namespace zf::sim

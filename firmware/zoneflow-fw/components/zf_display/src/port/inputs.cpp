// SPDX-License-Identifier: Apache-2.0
// Local controls: rotary encoder on PCNT, three switches on GPIO.
//
// The encoder's two phases go to one PCNT unit that counts every edge of
// both (4 counts per cycle) with the direction taken from the other phase,
// so a bouncing contact counts up and down again and no edge is lost while
// the UI task is busy drawing. A 5 ms esp_timer reads the count and the
// switches and runs the decoders of zf_ui/input.hpp; the UI task only
// receives finished events.
//
// The three switch pins see the bare contacts: rev A has no RC filter on
// them (hardware design 15.4), and ButtonDecoder does all the debouncing.
// The two encoder phases keep their 10 kohm and 100 nF.
//
// Never run on hardware.
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "port.hpp"
#include "zf_board/board.hpp"

namespace zf::display::port {
namespace {

constexpr const char *kTag = "zf.display";
constexpr uint64_t kSamplePeriodUs = ui::kButtonSamplePeriodMs * 1000ULL;
// PCNT counts in 16 bits; with accum_count the driver extends it across
// these limits.
constexpr int kPcntLimit = 30000;
// The PCNT glitch filter counts APB clock cycles in 10 bits: at 80 MHz the
// longest it can reject is 1023 / 80e6 = 12.8 us. Contact bounce is far
// longer and is handled by the quadrature decoding itself.
constexpr uint32_t kGlitchNs = 10000;

struct Inputs {
    QueueHandle_t queue = nullptr;
    pcnt_unit_handle_t pcnt = nullptr;
    ui::EncoderDecoder encoder{board::kEncoderCountsPerDetent};
    ui::Accelerator accelerator;
    ui::ButtonDecoder select{ui::selectButtonTiming()};
    ui::ButtonDecoder back{ui::backButtonTiming()};
    ui::ButtonDecoder home{ui::homeButtonTiming()};
};
Inputs sInputs;

bool pressed(int gpio) { return (gpio_get_level(static_cast<gpio_num_t>(gpio)) == 0) == board::kControlsActiveLow; }

void send(const ui::InputEvent &e)
{
    // Never blocks. A full queue means the UI task is stuck; dropping input
    // is then the lesser problem.
    xQueueSend(sInputs.queue, &e, 0);
}

void button(ui::ButtonDecoder &decoder, int gpio, ui::Key key, MonoMs now)
{
    switch (decoder.update(pressed(gpio), now)) {
    case ui::ButtonEdge::Press:
        send(ui::InputEvent::press(key));
        break;
    case ui::ButtonEdge::LongPress:
        send(ui::InputEvent::press(key, ui::InputType::LongPress));
        break;
    case ui::ButtonEdge::Repeat:
        send(ui::InputEvent::press(key, ui::InputType::Repeat));
        break;
    case ui::ButtonEdge::Hold:
        send(ui::InputEvent::press(key, ui::InputType::Hold));
        break;
    case ui::ButtonEdge::None:
        break;
    }
}

void sample(void * /*arg*/)
{
    const auto now = static_cast<MonoMs>(esp_timer_get_time() / 1000);
    int count = 0;
    if (pcnt_unit_get_count(sInputs.pcnt, &count) == ESP_OK) {
        const int steps = sInputs.encoder.update(board::kEncoderReversed ? -count : count, now);
        if (steps != 0) {
            send(ui::InputEvent::turn(steps, sInputs.accelerator.apply(steps, now)));
        }
    }
    button(sInputs.select, board::kEncoderPushGpio, ui::Key::Select, now);
    button(sInputs.back, board::kButtonBackGpio, ui::Key::Back, now);
    button(sInputs.home, board::kButtonHomeGpio, ui::Key::Home, now);
}

esp_err_t encoderInit()
{
    pcnt_unit_config_t unit = {};
    unit.low_limit = -kPcntLimit;
    unit.high_limit = kPcntLimit;
    unit.flags.accum_count = 1;
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit, &sInputs.pcnt), kTag, "pcnt unit");

    pcnt_glitch_filter_config_t filter = {};
    filter.max_glitch_ns = kGlitchNs;
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(sInputs.pcnt, &filter), kTag, "pcnt filter");

    // Channel A counts edges of phase A, direction from phase B; channel B
    // the other way round. Together: 4 counts per cycle.
    pcnt_chan_config_t a = {};
    a.edge_gpio_num = board::kEncoderAGpio;
    a.level_gpio_num = board::kEncoderBGpio;
    pcnt_channel_handle_t chanA = nullptr;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(sInputs.pcnt, &a, &chanA), kTag, "pcnt channel a");
    pcnt_chan_config_t b = {};
    b.edge_gpio_num = board::kEncoderBGpio;
    b.level_gpio_num = board::kEncoderAGpio;
    pcnt_channel_handle_t chanB = nullptr;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(sInputs.pcnt, &b, &chanB), kTag, "pcnt channel b");

    ESP_RETURN_ON_ERROR(
        pcnt_channel_set_edge_action(chanA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE), kTag,
        "pcnt edge a");
    ESP_RETURN_ON_ERROR(
        pcnt_channel_set_level_action(chanA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE), kTag,
        "pcnt level a");
    ESP_RETURN_ON_ERROR(
        pcnt_channel_set_edge_action(chanB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE), kTag,
        "pcnt edge b");
    ESP_RETURN_ON_ERROR(
        pcnt_channel_set_level_action(chanB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE), kTag,
        "pcnt level b");

    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(sInputs.pcnt, kPcntLimit), kTag, "pcnt watch");
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(sInputs.pcnt, -kPcntLimit), kTag, "pcnt watch");
    ESP_RETURN_ON_ERROR(pcnt_unit_enable(sInputs.pcnt), kTag, "pcnt enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(sInputs.pcnt), kTag, "pcnt clear");
    return pcnt_unit_start(sInputs.pcnt);
}

} // namespace

esp_err_t inputsInit(QueueHandle_t queue)
{
    sInputs.queue = queue;

    gpio_config_t pins = {};
    pins.pin_bit_mask = (1ULL << board::kEncoderAGpio) | (1ULL << board::kEncoderBGpio) |
                        (1ULL << board::kEncoderPushGpio) | (1ULL << board::kButtonBackGpio) |
                        (1ULL << board::kButtonHomeGpio);
    pins.mode = GPIO_MODE_INPUT;
    pins.pull_up_en = board::kControlsActiveLow ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    pins.pull_down_en = board::kControlsActiveLow ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    pins.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&pins), kTag, "control pins");
    ESP_RETURN_ON_ERROR(encoderInit(), kTag, "encoder");

    esp_timer_create_args_t args = {};
    args.callback = &sample;
    args.name = "zf_inputs";
    args.dispatch_method = ESP_TIMER_TASK;
    esp_timer_handle_t timer = nullptr;
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &timer), kTag, "input timer");
    return esp_timer_start_periodic(timer, kSamplePeriodUs);
}

} // namespace zf::display::port

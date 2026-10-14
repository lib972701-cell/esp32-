#include "es8388_audio_codec.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include "settings.h"

#define TAG "Es8388AudioCodec"
// 音频故障仅退出当前音频操作，不abort整机及独立摔倒任务。
#define RETURN_ON_AUDIO_ERROR(expression) do { \
    const auto audio_error = (expression); \
    if (audio_error != ESP_OK) { ESP_LOGE(TAG, "%s failed: %d", #expression, (int)audio_error); return; } \
} while (0)

Es8388AudioCodec::Es8388AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
    gpio_num_t pa_pin, uint8_t es8388_addr, bool input_reference) {
    duplex_ = true; // 是否双工
    input_reference_ = input_reference; // 是否使用参考输入，实现回声消除
    input_channels_ = input_reference_ ? 2 : 1; // 输入通道数
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    pa_pin_ = pa_pin;
    CreateDuplexChannels(mclk, bclk, ws, dout, din);
    if (!channels_ready_) return;

    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if_) { ESP_LOGE(TAG, "I2S data interface allocation failed"); return; }

    // Output
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = i2c_port,
        .addr = es8388_addr,
        .bus_handle = i2c_master_handle,
    };
    ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if_) { ESP_LOGE(TAG, "I2C control interface allocation failed"); return; }

    gpio_if_ = audio_codec_new_gpio();
    if (!gpio_if_) { ESP_LOGE(TAG, "GPIO interface allocation failed"); return; }

    es8388_codec_cfg_t es8388_cfg = {};
    es8388_cfg.ctrl_if = ctrl_if_;
    es8388_cfg.gpio_if = gpio_if_;
    es8388_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8388_cfg.master_mode = true;
    es8388_cfg.pa_pin = pa_pin;
    es8388_cfg.pa_reverted = false;
    es8388_cfg.hw_gain.pa_voltage = 5.0;
    es8388_cfg.hw_gain.codec_dac_voltage = 3.3;
    codec_if_ = es8388_codec_new(&es8388_cfg);
    if (!gpio_if_ || !codec_if_) { ESP_LOGE(TAG, "Codec interface allocation failed"); return; }

    esp_codec_dev_cfg_t outdev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if_,
        .data_if = data_if_,
    };
    output_dev_ = esp_codec_dev_new(&outdev_cfg);
    if (!output_dev_) { ESP_LOGE(TAG, "Output device allocation failed"); return; }

    esp_codec_dev_cfg_t indev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if_,
        .data_if = data_if_,
    };
    input_dev_ = esp_codec_dev_new(&indev_cfg);
    if (!input_dev_) { ESP_LOGE(TAG, "Input device allocation failed"); return; }
    esp_codec_set_disable_when_closed(output_dev_, false);
    esp_codec_set_disable_when_closed(input_dev_, false);
    ESP_LOGI(TAG, "Es8388AudioCodec initialized");
}

Es8388AudioCodec::~Es8388AudioCodec() {
    RETURN_ON_AUDIO_ERROR(esp_codec_dev_close(output_dev_));
    esp_codec_dev_delete(output_dev_);
    RETURN_ON_AUDIO_ERROR(esp_codec_dev_close(input_dev_));
    esp_codec_dev_delete(input_dev_);

    audio_codec_delete_codec_if(codec_if_);
    audio_codec_delete_ctrl_if(ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    audio_codec_delete_data_if(data_if_);
}

void Es8388AudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din){
    assert(input_sample_rate_ == output_sample_rate_);

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    RETURN_ON_AUDIO_ERROR(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

    RETURN_ON_AUDIO_ERROR(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    RETURN_ON_AUDIO_ERROR(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    channels_ready_ = true;
    ESP_LOGI(TAG, "Duplex channels created");
}

void Es8388AudioCodec::SetOutputVolume(int volume) {
    RETURN_ON_AUDIO_ERROR(esp_codec_dev_set_out_vol(output_dev_, volume));
    AudioCodec::SetOutputVolume(volume);
}

void Es8388AudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == input_enabled_) {
        return;
    }
    if (enable) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = (uint8_t) input_channels_,
            .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
            .sample_rate = (uint32_t)input_sample_rate_,
            .mclk_multiple = 0,
        };
        if (input_reference_) {
            fs.channel_mask |= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
        }
        RETURN_ON_AUDIO_ERROR(esp_codec_dev_open(input_dev_, &fs));
        if (input_reference_) {
            uint8_t gain = (11 << 4) + 0;
            ctrl_if_->write_reg(ctrl_if_, 0x09, 1, &gain, 1);
        }else{
            RETURN_ON_AUDIO_ERROR(esp_codec_dev_set_in_gain(input_dev_, 24.0));
        }
    } else {
        RETURN_ON_AUDIO_ERROR(esp_codec_dev_close(input_dev_));
    }
    AudioCodec::EnableInput(enable);
}

void Es8388AudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == output_enabled_) {
        return;
    }
    if (enable) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        RETURN_ON_AUDIO_ERROR(esp_codec_dev_open(output_dev_, &fs));
        RETURN_ON_AUDIO_ERROR(esp_codec_dev_set_out_vol(output_dev_, output_volume_));

        // Set analog output volume to 0dB, default is -45dB
        uint8_t reg_val = 30; // 0dB
        if(input_reference_){
            reg_val = 27;
        }
        uint8_t regs[] = { 46, 47, 48, 49 }; // HP_LVOL, HP_RVOL, SPK_LVOL, SPK_RVOL
        for (uint8_t reg : regs) {
            ctrl_if_->write_reg(ctrl_if_, reg, 1, &reg_val, 1);
        }

        if (pa_pin_ != GPIO_NUM_NC) {
            gpio_set_level(pa_pin_, 1);
        }
    } else {
        RETURN_ON_AUDIO_ERROR(esp_codec_dev_close(output_dev_));
        if (pa_pin_ != GPIO_NUM_NC) {
            gpio_set_level(pa_pin_, 0);
        }
    }
    AudioCodec::EnableOutput(enable);
}

int Es8388AudioCodec::Read(int16_t* dest, int samples) {
    if (input_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t)));
    }
    return samples;
}

int Es8388AudioCodec::Write(const int16_t* data, int samples) {
    if (output_enabled_ && output_dev_ && data != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t)));
    }
    return samples;
}

void Es8388AudioCodec::Start() {
    if (!IsInitialized()) return;
    Settings settings("audio", false);
    output_volume_ = settings.GetInt("output_volume", output_volume_);
    if (output_volume_ <= 0) output_volume_ = 10;
    RETURN_ON_AUDIO_ERROR(i2s_channel_enable(tx_handle_));
    RETURN_ON_AUDIO_ERROR(i2s_channel_enable(rx_handle_));
    EnableInput(true);
    EnableOutput(true);
    ESP_LOGI(TAG, "Audio codec ready input=%d output=%d", input_enabled(), output_enabled());
}

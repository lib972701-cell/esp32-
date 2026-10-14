#include "board.h"
#include "key0_driver.h"
#include "settings.h"
#include "system_info.h"
#include "es8388_audio_codec.h"
#include <esp_network.h>
#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>
#include <esp_random.h>
#include <esp_log.h>
#include <esp_app_desc.h>
#include <cJSON.h>

Board::Board() {
    Settings settings("board", true);
    uuid_ = settings.GetString("uuid");
    if (uuid_.empty()) {
        uint8_t bytes[16]; esp_fill_random(bytes, sizeof(bytes));
        bytes[6] = (bytes[6] & 0x0f) | 0x40; bytes[8] = (bytes[8] & 0x3f) | 0x80;
        char text[37];
        snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],
                 bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
        uuid_ = text; settings.SetString("uuid", uuid_);
    }
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port=I2C_NUM_0; cfg.sda_io_num=GPIO_NUM_41; cfg.scl_io_num=GPIO_NUM_42;
    cfg.clk_source=I2C_CLK_SRC_DEFAULT; cfg.glitch_ignore_cnt=7; cfg.flags.enable_internal_pullup=true;
    esp_err_t err=i2c_new_master_bus(&cfg,&bus_);
    if(err!=ESP_OK) { ESP_LOGE("cloud_board","Audio/KEY0 bus failed: %s",esp_err_to_name(err)); return; }
    i2c_device_config_t dev={}; dev.dev_addr_length=I2C_ADDR_BIT_LEN_7;
    dev.device_address=0x20; dev.scl_speed_hz=100000;
    err=i2c_master_bus_add_device(bus_,&dev,&expander_);
    auto update=[this](uint8_t reg,uint8_t clear,uint8_t set) {
        uint8_t value; esp_err_t e=i2c_master_transmit_receive(expander_,&reg,1,&value,1,50);
        if(e!=ESP_OK)return e;
        uint8_t out[]={reg,static_cast<uint8_t>((value&~clear)|set)};
        return i2c_master_transmit(expander_,out,sizeof(out),50);
    };
    if(err==ESP_OK)err=update(0x07,0,0xe0); // KEY0/1/2=P1_7/6/5输入
    if(err==ESP_OK)err=update(0x05,0xe0,0); // 三键低有效，不反相
    if(err==ESP_OK)err=update(0x02,0x04,0); // 官方音频例程：P0_2置0打开功放
    if(err==ESP_OK)err=update(0x06,0x04,0);
    if(err==ESP_OK)err=key0_driver_start(expander_);
    if(err!=ESP_OK)ESP_LOGE("cloud_board","KEY0 unavailable: %s",esp_err_to_name(err));
}
AudioCodec* Board::GetAudioCodec() {
    if(!bus_ || i2c_master_probe(bus_,0x10,50)!=ESP_OK)return nullptr;
    static Es8388AudioCodec codec(bus_,I2C_NUM_0,24000,24000,GPIO_NUM_3,GPIO_NUM_46,GPIO_NUM_9,
                                 GPIO_NUM_10,GPIO_NUM_14,GPIO_NUM_NC,ES8388_CODEC_DEFAULT_ADDR);
    return codec.IsInitialized()?&codec:nullptr;
}
NetworkInterface* Board::GetNetwork() { static EspNetwork network; return &network; }
bool Board::StartNetwork() {
    Settings settings("wifi",true);
    bool configure=settings.GetInt("force_ap")==1;
    if(configure)settings.SetInt("force_ap",0);
    if(!configure && !SsidManager::GetInstance().GetSsidList().empty()) {
        auto& wifi=WifiStation::GetInstance(); wifi.Start();
        if(wifi.WaitForConnected(60000)) { ESP_LOGI("cloud_wifi","Wi-Fi connected"); return true; }
        wifi.Stop();
    }
    auto& ap=WifiConfigurationAp::GetInstance();
    ap.SetLanguage("zh-CN"); ap.SetSsidPrefix("Xiaozhi"); ap.Start();
    ESP_LOGW("cloud_wifi","Configure Wi-Fi via AP %s at %s; fall_sampling continues",
             ap.GetSsid().c_str(),ap.GetWebServerUrl().c_str());
    return false;
}
std::string Board::GetSystemInfoJson() {
    auto root=cJSON_CreateObject();
    cJSON_AddNumberToObject(root,"version",2);
    cJSON_AddStringToObject(root,"mac_address",SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(root,"uuid",uuid_.c_str());
    cJSON_AddStringToObject(root,"chip_model_name","esp32s3");
    cJSON_AddNumberToObject(root,"flash_size",SystemInfo::GetFlashSize());
    auto app=cJSON_AddObjectToObject(root,"application");
    const auto* info=esp_app_get_description();
    cJSON_AddStringToObject(app,"name",info->project_name);
    cJSON_AddStringToObject(app,"version",info->version);
    cJSON_AddStringToObject(app,"idf_version",info->idf_ver);
    auto board=cJSON_AddObjectToObject(root,"board");
    cJSON_AddStringToObject(board,"type","atk-dnesp32s3");
    cJSON_AddStringToObject(board,"name","atk-dnesp32s3");
    char* encoded=cJSON_PrintUnformatted(root);
    std::string result=encoded?encoded:"{}"; cJSON_free(encoded); cJSON_Delete(root);
    return result;
}

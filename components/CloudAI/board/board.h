#pragma once
#include <memory>
#include <string>
#include <network_interface.h>
#include "driver/i2c_master.h"
class AudioCodec;
class Board {
public:
    static Board& GetInstance() { static Board board; return board; }
    AudioCodec* GetAudioCodec();
    NetworkInterface* GetNetwork();
    bool StartNetwork();
    const std::string& GetUuid() const { return uuid_; }
    std::string GetSystemInfoJson();
private:
    Board();
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t expander_ = nullptr;
    std::string uuid_;
};

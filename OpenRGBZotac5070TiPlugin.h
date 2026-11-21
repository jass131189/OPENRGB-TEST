#pragma once

#include <QObject>
#include <QWidget>
#include <QMenu>
#include <cstdint>
#include <string>
#include "OpenRGBPluginInterface.h"

class OpenRGBZotac5070TiPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "OpenRGBZotac5070TiPlugin.json")
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    OpenRGBZotac5070TiPlugin();
    ~OpenRGBZotac5070TiPlugin() override;

    OpenRGBPluginInfo   GetPluginInfo() override;
    unsigned int        GetPluginAPIVersion() override;
    void                Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
    QWidget*            GetWidget() override;
    QMenu*              GetTrayMenu() override;
    void                Unload() override;

    void                OnProfileAboutToLoad() override;
    void                OnProfileLoad(nlohmann::json profile) override;
    nlohmann::json      OnProfileSave() override;
    unsigned char*      OnSDKCommand(unsigned int command, unsigned char* data, unsigned int* size) override;
    void                ProfileManagerUpdated(unsigned int event_type) override;
    void                ResourceManagerUpdated(unsigned int event_type) override;
    void                SettingsManagerUpdated(unsigned int event_type) override;

private:
    void                ScanAndRegisterZotacGPU();
    bool                WriteColorNVAPI(unsigned char r, unsigned char g, unsigned char b);
    std::string         ScanNVAPIBus();
    void                SyncFromOpenRGBDevices();

    OpenRGBPluginAPIInterface* api = nullptr;
    QWidget*                   widget = nullptr;
    bool                       detected = false;
    bool                       mirror_active = true;
    unsigned char              last_r = 56;
    unsigned char              last_g = 189;
    unsigned char              last_b = 248;
    uint8_t                    active_i2c_addr = 0x49;
    uint8_t                    active_port_id = 1;
    uint32_t                   active_port_set = 1;
    uint32_t                   active_speed_khz = 4;
    bool                       active_reg_split = true;
    bool                       use_blackwell_proto = false;
    int                        last_nvapi_status = 0;
    std::string                last_nvapi_error;
    std::string                status_line;
};

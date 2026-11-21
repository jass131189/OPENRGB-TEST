#pragma once

#include <QObject>
#include <QWidget>
#include <QMenu>
#include <cstdint>
#include <string>
#include "OpenRGBPluginInterface.h"

enum ZotacProto
{
    ZOTAC_PROTO_4A        = 0,
    ZOTAC_PROTO_BLACKWELL = 1,
    ZOTAC_PROTO_V2        = 2
};

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
    uint8_t                    active_i2c_addr = 0x4A;
    uint32_t                   active_display_mask = 1u;
    uint8_t                    active_is_ddc = 1;
    uint8_t                    active_port_id = 1;
    uint32_t                   active_port_set = 1;
    uint32_t                   active_speed_khz = 2u;
    bool                       active_use_ex_api = false;
    bool                       active_reg_split = true;
    int                        active_proto = ZOTAC_PROTO_4A;
    bool                       has_applied_color = false;
    int                        last_nvapi_status = 0;
    std::string                last_nvapi_error;
    std::string                detected_fw_version;
    std::string                status_line;
};

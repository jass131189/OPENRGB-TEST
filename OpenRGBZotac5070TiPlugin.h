#pragma once

#include <QObject>
#include <QWidget>
#include <QMenu>
#include <QTimer>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
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
    static void         OnVirtualUpdateLEDs(void* ptr);
    static void         OnVirtualUpdateZoneLEDs(void* ptr, int zone);
    static void         OnVirtualUpdateSingleLED(void* ptr, int led);
    static void         OnVirtualUpdateMode(void* ptr);

    void                ScanAndRegisterZotacGPU();
    void                RegisterVirtualGPUDevice();
    void                HandleVirtualUpdate();
    bool                WriteColorNVAPI(unsigned char r, unsigned char g, unsigned char b);
    bool                WriteModeNVAPI(int mode_id, unsigned char r, unsigned char g, unsigned char b, unsigned int speed_pct, bool is_on, bool force_immediate);
    void                FlushPendingNVAPI();
    std::string         ScanNVAPIBus();
    void                SyncFromOpenRGBDevices();
    void                OnSyncTimerTick();

    OpenRGBPluginAPIInterface*                     api = nullptr;
    QWidget*                                       widget = nullptr;
    QTimer*                                        sync_timer = nullptr;
    RGBControllerInterface*                        virt_ctrl = nullptr;
    std::mutex                                     nvapi_mutex;
    std::unordered_map<RGBControllerInterface*, uint32_t> tracked_ctrl_colors;
    std::atomic<bool>                              sw_cycle_active{false};
    std::atomic<bool>                              mirrored_cycle_active{false};
    std::atomic<bool>                              virtual_mode_locked{false};
    std::atomic<unsigned int>                      sw_cycle_speed_pct{50};
    std::atomic<uint64_t>                          last_virtual_update_ms{0};
    float                                          sw_cycle_hue = 0.0f;
    uint64_t                                       last_cycle_step_ms = 0;
    bool                                           detected = false;
    bool                                           mirror_active = true;
    unsigned char                                  last_r = 56;
    unsigned char                                  last_g = 189;
    unsigned char                                  last_b = 248;
    int                                            last_mode_id = 0;
    unsigned int                                   last_speed_pct = 50;
    bool                                           last_is_on = true;
    std::atomic<uint64_t>                          last_hw_write_ms{0};
    bool                                           pending_hw_flush = false;
    unsigned char                                  pending_r = 56;
    unsigned char                                  pending_g = 189;
    unsigned char                                  pending_b = 248;
    int                                            pending_mode_id = 0;
    unsigned int                                   pending_speed_pct = 50;
    bool                                           pending_is_on = true;
    uint8_t                                        active_i2c_addr = 0x4A;
    uint32_t                                       active_display_mask = 1u;
    uint8_t                                        active_is_ddc = 1;
    uint8_t                                        active_port_id = 1;
    uint32_t                                       active_port_set = 1;
    uint32_t                                       active_speed_khz = 2u;
    bool                                           active_use_ex_api = false;
    bool                                           active_reg_split = true;
    int                                            active_proto = ZOTAC_PROTO_4A;
    bool                                           has_applied_color = false;
    int                                            last_nvapi_status = 0;
    std::string                                    last_nvapi_error;
    std::string                                    detected_fw_version;
    std::string                                    status_line;
};

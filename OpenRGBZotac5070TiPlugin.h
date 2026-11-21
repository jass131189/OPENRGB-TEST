#pragma once

#include <QObject>
#include <QWidget>
#include <QMenu>
#include <QTimer>
#include <QLabel>
#include <QCheckBox>
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

    void                ScheduleVirtualUpdate();
    void                ScanAndRegisterZotacGPU();
    void                RegisterVirtualGPUDevice();
    void                HandleVirtualUpdate();
    void                SetSmoothTargetColor(unsigned char r, unsigned char g, unsigned char b);
    bool                CanWriteHardwareNow(uint64_t now_ms) const;
    bool                WriteColorNVAPI(unsigned char r, unsigned char g, unsigned char b);
    bool                WriteModeNVAPI(int mode_id, unsigned char r, unsigned char g, unsigned char b, unsigned int speed_pct, bool is_on, bool force_immediate);
    void                FlushPendingNVAPI();
    uint32_t            NegotiateSafeBusSpeed();
    std::string         BenchmarkBusSpeed();
    std::string         ScanNVAPIBus();
    void                SyncFromOpenRGBDevices();
    void                SyncVirtualPreview(unsigned char r, unsigned char g, unsigned char b);
    void                OnSyncTimerTick();
    void                UpdateTelemetryUI();

    OpenRGBPluginAPIInterface*                            api = nullptr;
    QWidget*                                              widget = nullptr;
    QLabel*                                               state_label = nullptr;
    QLabel*                                               telemetry_label = nullptr;
    QCheckBox*                                            mirror_chk = nullptr;
    QTimer*                                               sync_timer = nullptr;
    RGBControllerInterface*                               virt_ctrl = nullptr;

    std::mutex                                            nvapi_mutex;
    std::unordered_map<RGBControllerInterface*, uint32_t> tracked_ctrl_colors;

    std::atomic<bool>                                     sw_cycle_active{false};
    std::atomic<bool>                                     mirrored_cycle_active{false};
    std::atomic<bool>                                     virtual_mode_locked{false};
    std::atomic<unsigned int>                             sw_cycle_speed_pct{50};
    std::atomic<uint64_t>                                 last_virtual_update_ms{0};
    std::atomic<bool>                                     virtual_update_queued{false};
    std::atomic<bool>                                     sync_update_queued{false};

    // Smooth Engine v2.2.0 configuration (runs safely on Qt main thread where NVAPI initialized)
    std::atomic<uint32_t>                                 target_frame_ms{40};
    std::atomic<uint32_t>                                 smooth_window_ms{120};
    std::atomic<int>                                      i2c_speed_pref{0}; // 0=Auto Safe, 4=100kHz, 3=40kHz, 2=10kHz
    std::atomic<bool>                                     gamma_correction{true};
    std::atomic<bool>                                     prefer_hw_native_cycle{false};

    // Smooth interpolator & cycle state (Qt main thread)
    float                                                 sw_cycle_hue = 0.0f;
    uint64_t                                              last_tick_ms = 0;
    bool                                                  interp_active = false;
    float                                                 cur_lin[3] = { 0.0f, 0.0f, 0.0f };
    bool                                                  cur_lin_init = false;
    unsigned char                                         interp_target_r = 56;
    unsigned char                                         interp_target_g = 189;
    unsigned char                                         interp_target_b = 248;
    bool                                                  updating_virtual_preview = false;
    bool                                                  in_timer_tick = false;

    bool                                                  detected = false;
    bool                                                  mirror_active = true;
    unsigned char                                         last_r = 56;
    unsigned char                                         last_g = 189;
    unsigned char                                         last_b = 248;
    int                                                   last_mode_id = 0;
    unsigned int                                          last_speed_pct = 50;
    bool                                                  last_is_on = true;

    uint64_t                                              last_write_start_ms = 0;
    uint64_t                                              last_write_end_ms = 0;
    uint32_t                                              last_write_us = 0;
    uint32_t                                              measured_fps_x10 = 0;
    uint64_t                                              fps_window_start_ms = 0;
    uint32_t                                              fps_window_frames = 0;
    uint64_t                                              frames_sent = 0;
    uint64_t                                              frames_failed = 0;
    uint64_t                                              last_ui_telemetry_ms = 0;

    bool                                                  pending_hw_flush = false;
    unsigned char                                         pending_r = 56;
    unsigned char                                         pending_g = 189;
    unsigned char                                         pending_b = 248;
    int                                                   pending_mode_id = 0;
    unsigned int                                          pending_speed_pct = 50;
    bool                                                  pending_is_on = true;

    uint8_t                                               active_i2c_addr = 0x4A;
    uint32_t                                              active_display_mask = 1u;
    uint8_t                                               active_is_ddc = 1;
    uint8_t                                               active_port_id = 1;
    uint32_t                                              active_port_set = 1;
    uint32_t                                              active_speed_khz = 2u;
    bool                                                  active_use_ex_api = false;
    bool                                                  active_reg_split = true;
    int                                                   active_proto = ZOTAC_PROTO_4A;
    bool                                                  has_applied_color = false;
    int                                                   last_nvapi_status = 0;
    std::string                                           last_nvapi_error;
    std::string                                           detected_fw_version;
    std::string                                           status_line;
};

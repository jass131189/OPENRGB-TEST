#include "OpenRGBZotac5070TiPlugin.h"
#include "RGBController_Zotac5070Ti.h"
#include "i2c_smbus.h"

#include "LogManager.h"
#include "StringUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QColorDialog>
#include <QColor>

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define NVIDIA_VEN                  0x10DE
#define NVIDIA_RTX5070TI_DEV        0x2C05
#define ZOTAC_SUB_VEN               0x19DA
#define ZOTAC_RTX5070TI_SUB_DEV     0x2765
#define ZOTAC_SPECTRA_I2C_ADDR      0x49
#define ZOTAC_ZONE_COUNT            2
#define ZOTAC_WILDCARD_SUBDEV       false

namespace
{
    template <typename T, typename = void>
    struct has_i2c_and_register : std::false_type {};

    template <typename T>
    struct has_i2c_and_register<
        T,
        std::void_t<
            decltype(std::declval<T*>()->GetI2CBusses()),
            decltype(std::declval<T*>()->RegisterRGBController(std::declval<RGBController*>()))
        >
    > : std::true_type {};

    template <typename T, typename = void>
    struct has_get_resource_manager : std::false_type {};

    template <typename T>
    struct has_get_resource_manager<
        T,
        std::void_t<decltype(std::declval<T*>()->GetResourceManager())>
    > : std::true_type {};

    template <typename T, typename = void>
    struct has_get_rgb_controllers : std::false_type {};

    template <typename T>
    struct has_get_rgb_controllers<
        T,
        std::void_t<decltype(std::declval<T*>()->GetRGBControllers())>
    > : std::true_type {};

    template <typename BusT, typename = void>
    struct has_direct_pci : std::false_type {};

    template <typename BusT>
    struct has_direct_pci<BusT, std::void_t<decltype(std::declval<BusT*>()->pci_vendor)>> : std::true_type {};

    template <typename BusT, typename = void>
    struct has_info_pci : std::false_type {};

    template <typename BusT>
    struct has_info_pci<BusT, std::void_t<decltype(std::declval<BusT*>()->info.pci_vendor)>> : std::true_type {};

    template <typename CtrlT, typename = void>
    struct has_get_colors : std::false_type {};

    template <typename CtrlT>
    struct has_get_colors<CtrlT, std::void_t<decltype(std::declval<CtrlT*>()->GetColors())>> : std::true_type {};

    template <typename CtrlT, typename = void>
    struct has_colors_member : std::false_type {};

    template <typename CtrlT>
    struct has_colors_member<CtrlT, std::void_t<decltype(std::declval<CtrlT*>()->colors)>> : std::true_type {};

    template <typename BusT>
    bool MatchesZotacPci(BusT* bus)
    {
        if(bus == nullptr)
        {
            return false;
        }
        if constexpr (has_direct_pci<BusT>::value)
        {
            if(bus->pci_vendor != NVIDIA_VEN || bus->pci_device != NVIDIA_RTX5070TI_DEV || bus->pci_subsystem_vendor != ZOTAC_SUB_VEN)
            {
                return false;
            }
            return ZOTAC_WILDCARD_SUBDEV || (bus->pci_subsystem_device == ZOTAC_RTX5070TI_SUB_DEV);
        }
        else if constexpr (has_info_pci<BusT>::value)
        {
            if(bus->info.pci_vendor != NVIDIA_VEN || bus->info.pci_device != NVIDIA_RTX5070TI_DEV || bus->info.pci_subsystem_vendor != ZOTAC_SUB_VEN)
            {
                return false;
            }
            return ZOTAC_WILDCARD_SUBDEV || (bus->info.pci_subsystem_device == ZOTAC_RTX5070TI_SUB_DEV);
        }
        else
        {
            return false;
        }
    }

    template <typename ManagerT>
    bool TryRegisterFromManager(ManagerT* mgr, std::string& out_status)
    {
        if(mgr == nullptr)
        {
            return false;
        }

        if constexpr (has_i2c_and_register<ManagerT>::value)
        {
            auto& busses = mgr->GetI2CBusses();
            for(size_t i = 0; i < busses.size(); i++)
            {
                i2c_smbus_interface* bus = busses[i];
                if(!MatchesZotacPci(bus))
                {
                    continue;
                }

                int probe = bus->i2c_smbus_read_byte_data(ZOTAC_SPECTRA_I2C_ADDR, 0x00);
                if(probe < 0)
                {
                    unsigned char ping_pkt[2] = { 0x00, 0x00 };
                    if(bus->i2c_smbus_write_i2c_block_data(ZOTAC_SPECTRA_I2C_ADDR, 0x00, 2, ping_pkt) < 0)
                    {
                        out_status = "Matched PCI ID on I2C bus, but address 0x49 did not ACK (close ZOTAC FireStorm).";
                        continue;
                    }
                }

                Zotac5070TiController* controller = new Zotac5070TiController(bus, ZOTAC_SPECTRA_I2C_ADDR);
                RGBController_Zotac5070Ti* rgb_controller = new RGBController_Zotac5070Ti(controller);
                rgb_controller->name = "ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC";

                mgr->RegisterRGBController(rgb_controller);
                out_status = "Registered ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC on native OpenRGB I2C address 0x49.";
                return true;
            }
        }
        else
        {
            (void)out_status;
        }

        return false;
    }

    template <typename ApiT>
    bool TryNativeRegister(ApiT* plugin_api, std::string& out_status)
    {
        if(plugin_api == nullptr)
        {
            return false;
        }

        if constexpr (has_i2c_and_register<ApiT>::value)
        {
            return TryRegisterFromManager(plugin_api, out_status);
        }
        else if constexpr (has_get_resource_manager<ApiT>::value)
        {
            return TryRegisterFromManager(plugin_api->GetResourceManager(), out_status);
        }
        else
        {
            (void)out_status;
            return false;
        }
    }

    template <typename CtrlT>
    bool TryExtractFirstColor(CtrlT* ctrl, unsigned char& r, unsigned char& g, unsigned char& b)
    {
        if(ctrl == nullptr)
        {
            return false;
        }
        if constexpr (has_get_colors<CtrlT>::value)
        {
            const auto& cols = ctrl->GetColors();
            if(!cols.empty())
            {
                RGBColor c = cols[0];
                r = static_cast<unsigned char>(RGBGetRValue(c));
                g = static_cast<unsigned char>(RGBGetGValue(c));
                b = static_cast<unsigned char>(RGBGetBValue(c));
                return true;
            }
        }
        else if constexpr (has_colors_member<CtrlT>::value)
        {
            if(!ctrl->colors.empty())
            {
                RGBColor c = ctrl->colors[0];
                r = static_cast<unsigned char>(RGBGetRValue(c));
                g = static_cast<unsigned char>(RGBGetGValue(c));
                b = static_cast<unsigned char>(RGBGetBValue(c));
                return true;
            }
        }
        return false;
    }

    template <typename ApiT>
    bool TryReadFirstOpenRGBColor(ApiT* plugin_api, unsigned char& r, unsigned char& g, unsigned char& b)
    {
        if(plugin_api == nullptr)
        {
            return false;
        }
        if constexpr (has_get_rgb_controllers<ApiT>::value)
        {
            auto controllers = plugin_api->GetRGBControllers();
            for(size_t i = 0; i < controllers.size(); i++)
            {
                if(TryExtractFirstColor(controllers[i], r, g, b))
                {
                    return true;
                }
            }
        }
        return false;
    }

#ifdef _WIN32
#pragma pack(push, 8)
    struct NvI2CInfoV3
    {
        uint32_t version;
        uint32_t displayMask;
        uint8_t  bIsDDCPort;
        uint8_t  i2cDevAddress;
        uint8_t* pbI2cRegAddress;
        uint32_t regAddrSize;
        uint8_t* pbData;
        uint32_t cbSize;
        uint32_t i2cSpeed;
        uint32_t i2cSpeedKhz;
        uint8_t  portId;
        uint32_t bIsPortIdSet;
    };
#pragma pack(pop)
#endif
}

OpenRGBZotac5070TiPlugin::OpenRGBZotac5070TiPlugin() = default;
OpenRGBZotac5070TiPlugin::~OpenRGBZotac5070TiPlugin() = default;

OpenRGBPluginInfo OpenRGBZotac5070TiPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name           = "ZOTAC RTX 5070 Ti SPECTRA Bridge";
    info.Description    = "Controls ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC (0x19DA:0x2765) inside OpenRGB 1.1 over NVAPI/I2C.";
    info.Version        = "1.0.0";
    info.Commit         = "local";
    info.Location       = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label          = "Zotac 5070 Ti";
    return info;
}

unsigned int OpenRGBZotac5070TiPlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void OpenRGBZotac5070TiPlugin::Load(OpenRGBPluginAPIInterface* plugin_api_ptr)
{
    api = plugin_api_ptr;
    ScanAndRegisterZotacGPU();
}

bool OpenRGBZotac5070TiPlugin::WriteColorNVAPI(unsigned char r, unsigned char g, unsigned char b)
{
#ifdef _WIN32
    HMODULE nvapi = LoadLibraryA("nvapi64.dll");
    if(nvapi == nullptr)
    {
        return false;
    }

    using QueryInterfaceFn = void* (__cdecl*)(uint32_t);
    using NvInitFn         = int (__cdecl*)();
    using NvEnumGpusFn     = int (__cdecl*)(void**, uint32_t*);
    using NvI2CWriteExFn   = int (__cdecl*)(void*, NvI2CInfoV3*, uint32_t*);

    auto query = reinterpret_cast<QueryInterfaceFn>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    if(query == nullptr)
    {
        FreeLibrary(nvapi);
        return false;
    }

    auto init_fn   = reinterpret_cast<NvInitFn>(query(0x0150E828u));
    auto enum_fn   = reinterpret_cast<NvEnumGpusFn>(query(0xE5AC921Fu));
    auto write_fn  = reinterpret_cast<NvI2CWriteExFn>(query(0x283AC65Au));

    if(init_fn == nullptr || enum_fn == nullptr || write_fn == nullptr || init_fn() != 0)
    {
        FreeLibrary(nvapi);
        return false;
    }

    void*    handles[64] = {};
    uint32_t count       = 0;
    if(enum_fn(handles, &count) != 0 || count == 0)
    {
        FreeLibrary(nvapi);
        return false;
    }

    bool any_ok = false;
    for(uint8_t z = 0; z < ZOTAC_ZONE_COUNT; z++)
    {
        uint8_t reg = 0x0F;
        uint8_t packet[10] = { 0x0F, 0x00, z, 0x00, r, g, b, 0x05, 0xFF, 0x01 };

        NvI2CInfoV3 info {};
        info.version         = static_cast<uint32_t>(sizeof(NvI2CInfoV3) | (3u << 16));
        info.displayMask     = 0;
        info.bIsDDCPort      = 0;
        info.i2cDevAddress   = static_cast<uint8_t>(ZOTAC_SPECTRA_I2C_ADDR << 1);
        info.pbI2cRegAddress = &reg;
        info.regAddrSize     = 1;
        info.pbData          = packet;
        info.cbSize          = static_cast<uint32_t>(sizeof(packet));
        info.i2cSpeed        = 0xFFFFu;
        info.i2cSpeedKhz     = 100u;
        info.portId          = 1;
        info.bIsPortIdSet    = 1;

        uint32_t unknown = 0;
        if(write_fn(handles[0], &info, &unknown) == 0)
        {
            any_ok = true;
        }
    }

    FreeLibrary(nvapi);
    return any_ok;
#else
    (void)r; (void)g; (void)b;
    return false;
#endif
}

void OpenRGBZotac5070TiPlugin::SyncFromOpenRGBDevices()
{
    if(api == nullptr || !mirror_active)
    {
        return;
    }

    unsigned char r = last_r;
    unsigned char g = last_g;
    unsigned char b = last_b;
    if(TryReadFirstOpenRGBColor(api, r, g, b))
    {
        last_r = r;
        last_g = g;
        last_b = b;
        WriteColorNVAPI(last_r, last_g, last_b);
    }
}

void OpenRGBZotac5070TiPlugin::ScanAndRegisterZotacGPU()
{
    if(api == nullptr)
    {
        return;
    }

    if(TryNativeRegister(api, status_line))
    {
        detected = true;
        return;
    }

    if(WriteColorNVAPI(last_r, last_g, last_b))
    {
        detected = true;
        status_line = "NVAPI Direct I2C Bridge active on 0x49 for ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC. Use controls below or Mirror OpenRGB.";
    }
    else if(status_line.empty())
    {
        status_line = "NVAPI I2C write did not ACK on 0x49. Close ZOTAC FireStorm and run OpenRGB as Administrator.";
    }
}

QWidget* OpenRGBZotac5070TiPlugin::GetWidget()
{
    if(widget != nullptr)
    {
        return widget;
    }

    widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);

    QLabel* title = new QLabel("ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC (OpenRGB 1.1 Bridge)");
    QLabel* pci_label = new QLabel("Target PCI: 0x10DE:0x2C05 | Subsys: 0x19DA:0x2765 | I2C: 0x49");
    QLabel* state_label = new QLabel(QString::fromStdString(status_line));
    state_label->setWordWrap(true);

    QCheckBox* mirror_chk = new QCheckBox("Mirror color from active OpenRGB devices");
    mirror_chk->setChecked(mirror_active);
    QObject::connect(mirror_chk, &QCheckBox::toggled, [this, state_label](bool checked) {
        mirror_active = checked;
        if(checked)
        {
            SyncFromOpenRGBDevices();
            state_label->setText("Mirroring enabled: synced from active OpenRGB controller.");
        }
    });

    QHBoxLayout* btn_row = new QHBoxLayout();
    QPushButton* pick_btn = new QPushButton("Pick GPU Color...");
    QPushButton* rescan_btn = new QPushButton("Re-Scan / Sync Now");

    QObject::connect(pick_btn, &QPushButton::clicked, [this, state_label]() {
        QColor initial(last_r, last_g, last_b);
        QColor chosen = QColorDialog::getColor(initial, widget, "Choose ZOTAC RTX 5070 Ti Color");
        if(chosen.isValid())
        {
            last_r = static_cast<unsigned char>(chosen.red());
            last_g = static_cast<unsigned char>(chosen.green());
            last_b = static_cast<unsigned char>(chosen.blue());
            if(WriteColorNVAPI(last_r, last_g, last_b))
            {
                state_label->setText(QString("Applied RGB(%1, %2, %3) to ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC via NVAPI I2C 0x49.")
                    .arg(last_r).arg(last_g).arg(last_b));
            }
            else
            {
                state_label->setText("Failed to write via NVAPI I2C. Ensure FireStorm is closed and OpenRGB runs as Administrator.");
            }
        }
    });

    QObject::connect(rescan_btn, &QPushButton::clicked, [this, state_label]() {
        ScanAndRegisterZotacGPU();
        if(mirror_active)
        {
            SyncFromOpenRGBDevices();
        }
        state_label->setText(QString::fromStdString(status_line));
    });

    btn_row->addWidget(pick_btn);
    btn_row->addWidget(rescan_btn);

    layout->addWidget(title);
    layout->addWidget(pci_label);
    layout->addWidget(state_label);
    layout->addWidget(mirror_chk);
    layout->addLayout(btn_row);
    layout->addStretch();

    return widget;
}

QMenu* OpenRGBZotac5070TiPlugin::GetTrayMenu()
{
    return nullptr;
}

void OpenRGBZotac5070TiPlugin::Unload()
{
    api = nullptr;
}

void OpenRGBZotac5070TiPlugin::OnProfileAboutToLoad()
{
}

void OpenRGBZotac5070TiPlugin::OnProfileLoad(nlohmann::json profile)
{
    if(profile.is_object())
    {
        if(profile.contains("r") && profile.contains("g") && profile.contains("b"))
        {
            last_r = static_cast<unsigned char>(profile["r"].get<int>());
            last_g = static_cast<unsigned char>(profile["g"].get<int>());
            last_b = static_cast<unsigned char>(profile["b"].get<int>());
            WriteColorNVAPI(last_r, last_g, last_b);
        }
        if(profile.contains("mirror") && profile["mirror"].is_boolean())
        {
            mirror_active = profile["mirror"].get<bool>();
        }
    }
}

nlohmann::json OpenRGBZotac5070TiPlugin::OnProfileSave()
{
    return nlohmann::json{
        {"r", static_cast<int>(last_r)},
        {"g", static_cast<int>(last_g)},
        {"b", static_cast<int>(last_b)},
        {"mirror", mirror_active}
    };
}

unsigned char* OpenRGBZotac5070TiPlugin::OnSDKCommand(unsigned int /*command*/, unsigned char* /*data*/, unsigned int* /*size*/)
{
    return nullptr;
}

void OpenRGBZotac5070TiPlugin::ProfileManagerUpdated(unsigned int /*event_type*/)
{
    SyncFromOpenRGBDevices();
}

void OpenRGBZotac5070TiPlugin::ResourceManagerUpdated(unsigned int /*event_type*/)
{
    SyncFromOpenRGBDevices();
}

void OpenRGBZotac5070TiPlugin::SettingsManagerUpdated(unsigned int /*event_type*/)
{
}

LogManager* LogManager::get()
{
    alignas(LogManager) static unsigned char stub_storage[sizeof(LogManager)] = {};
    return reinterpret_cast<LogManager*>(stub_storage);
}

void LogManager::LogEntry(const char* /*filename*/, int /*line*/, unsigned int /*level*/, const char* /*fmt*/, ...)
{
}

const std::string StringUtils::remove_null_terminating_chars(std::string str)
{
    str.erase(std::remove(str.begin(), str.end(), '\0'), str.end());
    return str;
}

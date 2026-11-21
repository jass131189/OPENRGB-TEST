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
#include <QComboBox>
#include <QColorDialog>
#include <QColor>
#include <QSignalBlocker>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
#define ZOTAC_SPECTRA_I2C_ADDR      0x4A
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

    template <typename BusT, typename = void>
    struct has_direct_pci : std::false_type {};

    template <typename BusT>
    struct has_direct_pci<BusT, std::void_t<decltype(std::declval<BusT*>()->pci_vendor)>> : std::true_type {};

    template <typename BusT, typename = void>
    struct has_info_pci : std::false_type {};

    template <typename BusT>
    struct has_info_pci<BusT, std::void_t<decltype(std::declval<BusT*>()->info.pci_vendor)>> : std::true_type {};

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
                        out_status = "Matched PCI ID on I2C bus, but address 0x4A did not ACK (close ZOTAC FireStorm).";
                        continue;
                    }
                }

                Zotac5070TiController* controller = new Zotac5070TiController(bus, ZOTAC_SPECTRA_I2C_ADDR);
                RGBController_Zotac5070Ti* rgb_controller = new RGBController_Zotac5070Ti(controller);
                rgb_controller->name = "ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC";

                mgr->RegisterRGBController(rgb_controller);
                out_status = "Registered ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC on native OpenRGB I2C address 0x4A.";
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

    uint64_t GetNowMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }

    void HsvToRgb(float h_deg, unsigned char& r, unsigned char& g, unsigned char& b)
    {
        float h = std::fmod(h_deg, 360.0f);
        if(h < 0.0f) h += 360.0f;
        float sector = h / 60.0f;
        int i = static_cast<int>(sector);
        float f = sector - static_cast<float>(i);

        float rf = 0.0f, gf = 0.0f, bf = 0.0f;
        switch(i % 6)
        {
            case 0: rf = 1.0f; gf = f;        bf = 0.0f; break;
            case 1: rf = 1.0f - f; gf = 1.0f; bf = 0.0f; break;
            case 2: rf = 0.0f; gf = 1.0f;     bf = f;    break;
            case 3: rf = 0.0f; gf = 1.0f - f; bf = 1.0f; break;
            case 4: rf = f;    gf = 0.0f;     bf = 1.0f; break;
            default: rf = 1.0f; gf = 0.0f;    bf = 1.0f - f; break;
        }

        r = static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround(rf * 255.0f)), 0, 255));
        g = static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround(gf * 255.0f)), 0, 255));
        b = static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround(bf * 255.0f)), 0, 255));
    }

    bool IsCycleModeName(const std::string& raw_name)
    {
        std::string lower;
        lower.reserve(raw_name.size());
        for(unsigned char ch : raw_name)
        {
            lower.push_back(static_cast<char>(std::tolower(ch)));
        }
        return (lower.find("cycle") != std::string::npos) ||
               (lower.find("rainbow") != std::string::npos) ||
               (lower.find("spectrum") != std::string::npos) ||
               (lower.find("wave") != std::string::npos) ||
               (lower.find("prism") != std::string::npos) ||
               (lower.find("fade") != std::string::npos) ||
               (lower.find("shift") != std::string::npos);
    }

    bool TryExtractControllerColor(RGBControllerInterface* ctrl, unsigned char& r, unsigned char& g, unsigned char& b)
    {
        if(ctrl == nullptr)
        {
            return false;
        }
        int active = ctrl->GetActiveMode();
        if(active >= 0)
        {
            unsigned int m = static_cast<unsigned int>(active);
            if(m < ctrl->GetModeCount() &&
               ctrl->GetModeColorMode(m) == MODE_COLORS_MODE_SPECIFIC &&
               ctrl->GetModeColorsCount(m) > 0)
            {
                RGBColor mc = ctrl->GetModeColor(m, 0);
                r = static_cast<unsigned char>(RGBGetRValue(mc));
                g = static_cast<unsigned char>(RGBGetGValue(mc));
                b = static_cast<unsigned char>(RGBGetBValue(mc));
                return true;
            }
        }
        unsigned int led_cnt = ctrl->GetLEDCount();
        if(led_cnt > 0)
        {
            RGBColor chosen = ctrl->GetColor(0);
            if((chosen & 0x00FFFFFFu) == 0)
            {
                unsigned int limit = std::min(led_cnt, 32u);
                for(unsigned int i = 1; i < limit; i++)
                {
                    RGBColor c = ctrl->GetColor(i);
                    if((c & 0x00FFFFFFu) != 0)
                    {
                        chosen = c;
                        break;
                    }
                }
            }
            r = static_cast<unsigned char>(RGBGetRValue(chosen));
            g = static_cast<unsigned char>(RGBGetGValue(chosen));
            b = static_cast<unsigned char>(RGBGetBValue(chosen));
            return true;
        }
        return false;
    }

    uint32_t PackRgb24(unsigned char r, unsigned char g, unsigned char b)
    {
        return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    }

#ifdef _WIN32
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

    struct NvapiSession
    {
        using QueryInterfaceFn = void* (__cdecl*)(uint32_t);
        using NvInitFn         = int (__cdecl*)();
        using NvEnumGpusFn     = int (__cdecl*)(void**, int32_t*);
        using NvGetPciFn       = int (__cdecl*)(void*, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
        using NvEnumDispFn     = int (__cdecl*)(uint32_t, void**);
        using NvGetDispOutFn   = int (__cdecl*)(void*, uint32_t*);
        using NvI2CWriteFn     = int (__cdecl*)(void*, NvI2CInfoV3*);
        using NvI2CReadFn      = int (__cdecl*)(void*, NvI2CInfoV3*);
        using NvI2CWriteExFn   = int (__cdecl*)(void*, NvI2CInfoV3*, uint32_t*);
        using NvI2CReadExFn    = int (__cdecl*)(void*, NvI2CInfoV3*, uint32_t*);
        using NvGetErrMsgFn    = int (__cdecl*)(int, char*);

        HMODULE               mod       = nullptr;
        bool                  init_ok   = false;
        void*                 gpu       = nullptr;
        uint32_t              pci_dev   = 0;
        uint32_t              pci_sub   = 0;
        int                   init_rc   = 0;
        std::string           err_note;
        std::vector<uint32_t> display_masks;

        NvInitFn              init_fn      = nullptr;
        NvEnumGpusFn          enum_fn      = nullptr;
        NvGetPciFn            pci_fn       = nullptr;
        NvEnumDispFn          enum_disp_fn = nullptr;
        NvGetDispOutFn        disp_out_fn  = nullptr;
        NvI2CWriteFn          write_fn     = nullptr;
        NvI2CReadFn           read_fn      = nullptr;
        NvI2CWriteExFn        write_ex_fn  = nullptr;
        NvI2CReadExFn         read_ex_fn   = nullptr;
        NvGetErrMsgFn         err_fn       = nullptr;

        static NvapiSession& Get()
        {
            static NvapiSession instance;
            return instance;
        }

        void AddMaskUnique(uint32_t m)
        {
            if(std::find(display_masks.begin(), display_masks.end(), m) == display_masks.end())
            {
                display_masks.push_back(m);
            }
        }

        bool EnsureReady()
        {
            if(init_ok && gpu != nullptr)
            {
                return true;
            }
            if(mod == nullptr)
            {
                mod = LoadLibraryA("nvapi64.dll");
                if(mod == nullptr)
                {
                    err_note = "Failed to load nvapi64.dll";
                    return false;
                }
            }

            auto query = reinterpret_cast<QueryInterfaceFn>(GetProcAddress(mod, "nvapi_QueryInterface"));
            if(query == nullptr)
            {
                err_note = "nvapi_QueryInterface not found";
                return false;
            }

            init_fn      = reinterpret_cast<NvInitFn>(query(0x0150E828u));
            enum_fn      = reinterpret_cast<NvEnumGpusFn>(query(0xE5AC921Fu));
            pci_fn       = reinterpret_cast<NvGetPciFn>(query(0x2DDFB66Eu));
            enum_disp_fn = reinterpret_cast<NvEnumDispFn>(query(0x9ABDD40Du));
            disp_out_fn  = reinterpret_cast<NvGetDispOutFn>(query(0xD995937Eu));
            write_fn     = reinterpret_cast<NvI2CWriteFn>(query(0xE812EB07u));
            read_fn      = reinterpret_cast<NvI2CReadFn>(query(0x2FDE12C5u));
            write_ex_fn  = reinterpret_cast<NvI2CWriteExFn>(query(0x283AC65Au));
            read_ex_fn   = reinterpret_cast<NvI2CReadExFn>(query(0x4D7B0709u));
            err_fn       = reinterpret_cast<NvGetErrMsgFn>(query(0x6C2D048Cu));

            if(init_fn == nullptr || enum_fn == nullptr || ((write_fn == nullptr || read_fn == nullptr) && (write_ex_fn == nullptr || read_ex_fn == nullptr)))
            {
                err_note = "Required NVAPI I2C entry points unavailable";
                return false;
            }

            init_rc = init_fn();
            if(init_rc != 0)
            {
                err_note = "NvAPI_Initialize failed";
                return false;
            }

            void*   handles[64] = {};
            int32_t count       = 0;
            init_rc = enum_fn(handles, &count);
            if(init_rc != 0 || count <= 0)
            {
                err_note = "No NVIDIA physical GPU handles returned";
                return false;
            }

            gpu = handles[0];
            if(pci_fn != nullptr)
            {
                for(int32_t i = 0; i < count; i++)
                {
                    uint32_t dev_id = 0, sub_id = 0, rev_id = 0, ext_id = 0;
                    if(pci_fn(handles[i], &dev_id, &sub_id, &rev_id, &ext_id) == 0)
                    {
                        uint32_t ven = dev_id & 0xFFFFu;
                        uint32_t dev = (dev_id >> 16) & 0xFFFFu;
                        if(ven == NVIDIA_VEN && dev == NVIDIA_RTX5070TI_DEV)
                        {
                            gpu     = handles[i];
                            pci_dev = dev_id;
                            pci_sub = sub_id;
                            break;
                        }
                    }
                }
                if(pci_dev == 0)
                {
                    uint32_t rev_id = 0, ext_id = 0;
                    pci_fn(gpu, &pci_dev, &pci_sub, &rev_id, &ext_id);
                }
            }

            display_masks.clear();
            AddMaskUnique(1u); // Verified FireStorm default display_mask on ZOTAC RTX 5070/5070Ti/5080
            if(enum_disp_fn != nullptr && disp_out_fn != nullptr)
            {
                for(uint32_t idx = 0; idx < 16; idx++)
                {
                    void* disp_handle = nullptr;
                    if(enum_disp_fn(idx, &disp_handle) != 0 || disp_handle == nullptr)
                    {
                        break;
                    }
                    uint32_t out_id = 0;
                    if(disp_out_fn(disp_handle, &out_id) == 0 && out_id != 0)
                    {
                        AddMaskUnique(out_id);
                    }
                }
            }
            AddMaskUnique(2u);
            AddMaskUnique(4u);
            AddMaskUnique(8u);
            AddMaskUnique(16u);
            AddMaskUnique(0u);

            init_ok = true;
            err_note.clear();
            return true;
        }

        int ReadI2C(
            uint8_t  addr,
            uint8_t  reg,
            bool     use_reg,
            uint8_t* out_buf,
            uint32_t len,
            uint32_t disp_mask,
            uint8_t  is_ddc,
            uint8_t  port_id,
            uint32_t port_set,
            uint32_t speed_khz,
            bool     use_ex
        )
        {
            if(!EnsureReady()) return -1;
            uint8_t reg_local = reg;
            NvI2CInfoV3 info = {};
            info.version         = static_cast<uint32_t>(sizeof(NvI2CInfoV3) | (3u << 16));
            info.displayMask     = disp_mask;
            info.bIsDDCPort      = is_ddc;
            info.i2cDevAddress   = static_cast<uint8_t>(addr << 1);
            info.pbI2cRegAddress = use_reg ? &reg_local : nullptr;
            info.regAddrSize     = use_reg ? 1u : 0u;
            info.pbData          = out_buf;
            info.cbSize          = len;
            info.i2cSpeed        = 0xFFFFu;
            info.i2cSpeedKhz     = speed_khz;
            info.portId          = port_id;
            info.bIsPortIdSet    = port_set;

            if(use_ex && read_ex_fn != nullptr)
            {
                uint32_t unknown = 0;
                return read_ex_fn(gpu, &info, &unknown);
            }
            if(read_fn != nullptr)
            {
                return read_fn(gpu, &info);
            }
            if(read_ex_fn != nullptr)
            {
                uint32_t unknown = 0;
                return read_ex_fn(gpu, &info, &unknown);
            }
            return -1;
        }

        int WriteI2C(
            uint8_t  addr,
            uint8_t  reg,
            bool     use_reg,
            uint8_t* data_buf,
            uint32_t len,
            uint32_t disp_mask,
            uint8_t  is_ddc,
            uint8_t  port_id,
            uint32_t port_set,
            uint32_t speed_khz,
            bool     use_ex
        )
        {
            if(!EnsureReady()) return -1;
            uint8_t reg_local = reg;
            NvI2CInfoV3 info = {};
            info.version         = static_cast<uint32_t>(sizeof(NvI2CInfoV3) | (3u << 16));
            info.displayMask     = disp_mask;
            info.bIsDDCPort      = is_ddc;
            info.i2cDevAddress   = static_cast<uint8_t>(addr << 1);
            info.pbI2cRegAddress = use_reg ? &reg_local : nullptr;
            info.regAddrSize     = use_reg ? 1u : 0u;
            info.pbData          = data_buf;
            info.cbSize          = len;
            info.i2cSpeed        = 0xFFFFu;
            info.i2cSpeedKhz     = speed_khz;
            info.portId          = port_id;
            info.bIsPortIdSet    = port_set;

            if(use_ex && write_ex_fn != nullptr)
            {
                uint32_t unknown = 0;
                return write_ex_fn(gpu, &info, &unknown);
            }
            if(write_fn != nullptr)
            {
                return write_fn(gpu, &info);
            }
            if(write_ex_fn != nullptr)
            {
                uint32_t unknown = 0;
                return write_ex_fn(gpu, &info, &unknown);
            }
            return -1;
        }

        std::string FormatError(int rc)
        {
            char err_buf[64] = {};
            if(err_fn != nullptr && err_fn(rc, err_buf) == 0 && err_buf[0] != '\0')
            {
                return std::string(err_buf);
            }
            char code_buf[48] = {};
            std::snprintf(code_buf, sizeof(code_buf), "NVAPI status %d", rc);
            return std::string(code_buf);
        }
    };
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

    if(sync_timer == nullptr)
    {
        sync_timer = new QTimer(this);
        sync_timer->setTimerType(Qt::PreciseTimer);
        QObject::connect(sync_timer, &QTimer::timeout, this, &OpenRGBZotac5070TiPlugin::OnSyncTimerTick);
        sync_timer->start(15);
    }
}

void OpenRGBZotac5070TiPlugin::OnVirtualUpdateLEDs(void* ptr)
{
    if(ptr != nullptr) static_cast<OpenRGBZotac5070TiPlugin*>(ptr)->ScheduleVirtualUpdate();
}

void OpenRGBZotac5070TiPlugin::OnVirtualUpdateZoneLEDs(void* ptr, int /*zone*/)
{
    if(ptr != nullptr) static_cast<OpenRGBZotac5070TiPlugin*>(ptr)->ScheduleVirtualUpdate();
}

void OpenRGBZotac5070TiPlugin::OnVirtualUpdateSingleLED(void* ptr, int /*led*/)
{
    if(ptr != nullptr) static_cast<OpenRGBZotac5070TiPlugin*>(ptr)->ScheduleVirtualUpdate();
}

void OpenRGBZotac5070TiPlugin::OnVirtualUpdateMode(void* ptr)
{
    if(ptr != nullptr) static_cast<OpenRGBZotac5070TiPlugin*>(ptr)->ScheduleVirtualUpdate();
}

void OpenRGBZotac5070TiPlugin::ScheduleVirtualUpdate()
{
    if(QThread::currentThread() == this->thread())
    {
        HandleVirtualUpdate();
        return;
    }

    if(!virtual_update_queued.exchange(true))
    {
        QMetaObject::invokeMethod(
            this,
            [this]() {
                virtual_update_queued.store(false);
                HandleVirtualUpdate();
            },
            Qt::QueuedConnection
        );
    }
}

void OpenRGBZotac5070TiPlugin::SyncVirtualPreview(unsigned char r, unsigned char g, unsigned char b)
{
    if(virt_ctrl == nullptr || updating_virtual_preview)
    {
        return;
    }
    updating_virtual_preview = true;
    virt_ctrl->SetAllColors(ToRGBColor(r, g, b));
    updating_virtual_preview = false;
}

void OpenRGBZotac5070TiPlugin::HandleVirtualUpdate()
{
    if(virt_ctrl == nullptr || updating_virtual_preview)
    {
        return;
    }

    last_virtual_update_ms.store(GetNowMs());

    int m_idx = virt_ctrl->GetActiveMode();
    int mode_id = ZOTAC_5070TI_MODE_DIRECT;
    unsigned int speed_pct = 50;
    bool is_on = true;
    bool use_sw_cycle = false;
    bool lock_mode = false;
    unsigned char r = last_r;
    unsigned char g = last_g;
    unsigned char b = last_b;

    if(m_idx >= 0 && static_cast<unsigned int>(m_idx) < virt_ctrl->GetModeCount())
    {
        unsigned int m = static_cast<unsigned int>(m_idx);
        std::string mname = virt_ctrl->GetModeName(m);
        unsigned int smax = virt_ctrl->GetModeSpeedMax(m);
        if(smax > 0)
        {
            speed_pct = (virt_ctrl->GetModeSpeed(m) * 100u) / smax;
        }

        if(mname == "Off")
        {
            mode_id = ZOTAC_5070TI_MODE_OFF;
            is_on = false;
            lock_mode = true;
        }
        else if(mname == "Breathing")
        {
            mode_id = ZOTAC_5070TI_MODE_BREATHING;
            lock_mode = true;
        }
        else if(mname == "Spectrum Cycle")
        {
            mode_id = ZOTAC_5070TI_MODE_COLOR_CYCLE;
            use_sw_cycle = (active_proto == ZOTAC_PROTO_4A);
            lock_mode = true;
        }
        else if(mname == "Rainbow Wave")
        {
            mode_id = ZOTAC_5070TI_MODE_RAINBOW;
            use_sw_cycle = (active_proto == ZOTAC_PROTO_4A);
            lock_mode = true;
        }
        else if(mname == "Static")
        {
            mode_id = ZOTAC_5070TI_MODE_STATIC;
        }
        else
        {
            mode_id = ZOTAC_5070TI_MODE_DIRECT;
        }
    }

    mirrored_cycle_active.store(false);
    virtual_mode_locked.store(lock_mode);
    sw_cycle_speed_pct.store(speed_pct);
    sw_cycle_active.store(use_sw_cycle);

    if(use_sw_cycle)
    {
        last_cycle_step_ms = GetNowMs();
        HsvToRgb(sw_cycle_hue, r, g, b);
        last_r = r;
        last_g = g;
        last_b = b;
        last_mode_id = ZOTAC_5070TI_MODE_STATIC;
        last_speed_pct = speed_pct;
        last_is_on = true;
        WriteModeNVAPI(ZOTAC_5070TI_MODE_STATIC, r, g, b, speed_pct, true, false);
        return;
    }

    TryExtractControllerColor(virt_ctrl, r, g, b);
    const bool mode_changed = (mode_id != last_mode_id) || (is_on != last_is_on);
    last_r = r;
    last_g = g;
    last_b = b;
    last_mode_id = mode_id;
    last_speed_pct = speed_pct;
    last_is_on = is_on;
    const bool immediate = (mode_id == ZOTAC_5070TI_MODE_STATIC) || mode_changed;
    WriteModeNVAPI(mode_id, r, g, b, speed_pct, is_on, immediate);
}

void OpenRGBZotac5070TiPlugin::RegisterVirtualGPUDevice()
{
    if(api == nullptr || virt_ctrl != nullptr)
    {
        return;
    }

    RGBController_Setup setup = {};
    setup.name        = "ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC";
    setup.vendor      = "ZOTAC";
    setup.description = "ZOTAC RTX 5070 Ti SPECTRA GPU Controller (NVAPI Bridge)";
    setup.version     = detected_fw_version.empty() ? "1.0.0" : detected_fw_version;
    char loc_buf[64] = {};
    std::snprintf(loc_buf, sizeof(loc_buf), "NVAPI I2C 0x%02X (mask %u)", active_i2c_addr, active_display_mask);
    setup.location    = loc_buf;
    setup.serial      = "";
    setup.type        = DEVICE_TYPE_GPU;
    setup.flags       = CONTROLLER_FLAG_VIRTUAL;
    setup.active_mode = 0;

    mode m_direct;
    m_direct.name       = "Direct";
    m_direct.value      = ZOTAC_5070TI_MODE_DIRECT;
    m_direct.flags      = MODE_FLAG_HAS_PER_LED_COLOR;
    m_direct.color_mode = MODE_COLORS_PER_LED;
    setup.modes.push_back(m_direct);

    mode m_static;
    m_static.name       = "Static";
    m_static.value      = ZOTAC_5070TI_MODE_STATIC;
    m_static.flags      = MODE_FLAG_HAS_PER_LED_COLOR;
    m_static.color_mode = MODE_COLORS_PER_LED;
    setup.modes.push_back(m_static);

    mode m_breath;
    m_breath.name       = "Breathing";
    m_breath.value      = ZOTAC_5070TI_MODE_BREATHING;
    m_breath.flags      = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_SPEED;
    m_breath.color_mode = MODE_COLORS_PER_LED;
    m_breath.speed_min  = 0;
    m_breath.speed_max  = 100;
    m_breath.speed      = 50;
    setup.modes.push_back(m_breath);

    mode m_cycle;
    m_cycle.name       = "Spectrum Cycle";
    m_cycle.value      = ZOTAC_5070TI_MODE_COLOR_CYCLE;
    m_cycle.flags      = MODE_FLAG_HAS_SPEED;
    m_cycle.color_mode = MODE_COLORS_NONE;
    m_cycle.speed_min  = 0;
    m_cycle.speed_max  = 100;
    m_cycle.speed      = 50;
    setup.modes.push_back(m_cycle);

    mode m_rainbow;
    m_rainbow.name       = "Rainbow Wave";
    m_rainbow.value      = ZOTAC_5070TI_MODE_RAINBOW;
    m_rainbow.flags      = MODE_FLAG_HAS_SPEED;
    m_rainbow.color_mode = MODE_COLORS_NONE;
    m_rainbow.speed_min  = 0;
    m_rainbow.speed_max  = 100;
    m_rainbow.speed      = 50;
    setup.modes.push_back(m_rainbow);

    mode m_off;
    m_off.name       = "Off";
    m_off.value      = ZOTAC_5070TI_MODE_OFF;
    m_off.flags      = 0;
    m_off.color_mode = MODE_COLORS_NONE;
    setup.modes.push_back(m_off);

    const char* znames[] = { "Side Lightbar", "SPECTRA Emblem" };
    for(int i = 0; i < ZOTAC_ZONE_COUNT; i++)
    {
        zone z_obj;
        z_obj.name       = znames[i];
        z_obj.type       = ZONE_TYPE_SINGLE;
        z_obj.leds_min   = 1;
        z_obj.leds_max   = 1;
        z_obj.leds_count = 1;
        setup.zones.push_back(z_obj);

        led l_obj;
        l_obj.name  = znames[i];
        l_obj.value = static_cast<unsigned int>(i);
        setup.leds.push_back(l_obj);
    }

    setup.object_ptr            = this;
    setup.DeviceUpdateLEDs      = &OpenRGBZotac5070TiPlugin::OnVirtualUpdateLEDs;
    setup.DeviceUpdateZoneLEDs  = &OpenRGBZotac5070TiPlugin::OnVirtualUpdateZoneLEDs;
    setup.DeviceUpdateSingleLED = &OpenRGBZotac5070TiPlugin::OnVirtualUpdateSingleLED;
    setup.DeviceUpdateMode      = &OpenRGBZotac5070TiPlugin::OnVirtualUpdateMode;
    setup.DeviceSaveMode        = &OpenRGBZotac5070TiPlugin::OnVirtualUpdateMode;

    virt_ctrl = api->CreateVirtualRGBController(&setup);
    if(virt_ctrl != nullptr)
    {
        SyncVirtualPreview(last_r, last_g, last_b);
        api->RegisterVirtualRGBControllerInThread(virt_ctrl);
    }
}

bool OpenRGBZotac5070TiPlugin::CanWriteHardwareNow(uint64_t now_ms) const
{
    if(now_ms < hw_backoff_until_ms)
    {
        return false;
    }
    if(last_write_start_ms == 0)
    {
        return true;
    }
    const uint64_t min_interval_ms = (active_proto == ZOTAC_PROTO_4A) ? 90ULL : 25ULL;
    const uint64_t idle_gap_ms     = (active_proto == ZOTAC_PROTO_4A) ? 45ULL : 8ULL;
    const uint64_t write_dur_ms    = (last_write_end_ms >= last_write_start_ms) ? (last_write_end_ms - last_write_start_ms) : 0ULL;
    const uint64_t required_idle   = std::max(idle_gap_ms, write_dur_ms);
    return ((now_ms - last_write_start_ms) >= min_interval_ms) && ((now_ms - last_write_end_ms) >= required_idle);
}

bool OpenRGBZotac5070TiPlugin::WriteColorNVAPI(unsigned char r, unsigned char g, unsigned char b)
{
    return WriteModeNVAPI(ZOTAC_5070TI_MODE_STATIC, r, g, b, 50, true, true);
}

bool OpenRGBZotac5070TiPlugin::WriteModeNVAPI(
    int           mode_id,
    unsigned char r,
    unsigned char g,
    unsigned char b,
    unsigned int  speed_pct,
    bool          is_on,
    bool          force_immediate
)
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(nvapi_mutex);

    const uint64_t now_ms = GetNowMs();
    if(!force_immediate && !CanWriteHardwareNow(now_ms))
    {
        pending_r         = r;
        pending_g         = g;
        pending_b         = b;
        pending_mode_id   = mode_id;
        pending_speed_pct = speed_pct;
        pending_is_on     = is_on;
        pending_hw_flush  = true;
        return true;
    }

    NvapiSession& nv = NvapiSession::Get();
    if(!nv.EnsureReady())
    {
        last_nvapi_status = nv.init_rc != 0 ? nv.init_rc : -1;
        last_nvapi_error  = nv.err_note;
        return false;
    }

    auto write_zotac_4a = [&](uint8_t addr, uint32_t disp_mask, uint8_t is_ddc, uint8_t port_id, uint32_t port_set, uint32_t speed_khz, bool use_ex) -> int
    {
        uint8_t mode_4a = 0x00;
        if(mode_id == ZOTAC_5070TI_MODE_BREATHING) mode_4a = 0x01;
        else if(mode_id == ZOTAC_5070TI_MODE_COLOR_CYCLE) mode_4a = 0x02;
        else if(mode_id == ZOTAC_5070TI_MODE_RAINBOW) mode_4a = 0x04;
        uint8_t clamped_spd = static_cast<uint8_t>(std::min(speed_pct, 100u));
        uint8_t on_time = (mode_4a == 0x00)
            ? static_cast<uint8_t>(45u)
            : static_cast<uint8_t>(60u - ((clamped_spd * 55u) / 100u));
        uint8_t data_pkt[32] = {
            0x00,
            is_on ? static_cast<uint8_t>(0x01) : static_cast<uint8_t>(0x00),
            mode_4a,
            is_on ? r : static_cast<uint8_t>(0x00),
            is_on ? g : static_cast<uint8_t>(0x00),
            is_on ? b : static_cast<uint8_t>(0x00),
            on_time,
            10,
            0x08
        };
        return nv.WriteI2C(addr, 0x5A, true, data_pkt, 32u, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
    };

    auto write_blackwell = [&](uint8_t addr, uint32_t disp_mask, uint8_t is_ddc, uint8_t port_id, uint32_t port_set, uint32_t speed_khz, bool use_ex) -> int
    {
        uint8_t bw_mode = 0x01;
        if(mode_id == ZOTAC_5070TI_MODE_BREATHING) bw_mode = 0x02;
        else if(mode_id == ZOTAC_5070TI_MODE_COLOR_CYCLE) bw_mode = 0x03;
        else if(mode_id == ZOTAC_5070TI_MODE_RAINBOW) bw_mode = 0x09;
        uint8_t bw_brt = is_on ? 100 : 0;
        uint8_t bw_spd = static_cast<uint8_t>(std::min(speed_pct, 100u));

        for(uint8_t z = 0; z < ZOTAC_ZONE_COUNT; z++)
        {
            uint8_t regs[16] = {
                0x00, z, bw_mode,
                r, g, b,
                0x00, 0x00, 0x00,
                bw_brt, bw_spd, 0x00,
                0x00, 0x00, 0x00, 0x00
            };
            for(int i = 0; i < 16; i++)
            {
                uint8_t pair[2] = { static_cast<uint8_t>(0x20 + i), regs[i] };
                int rc = nv.WriteI2C(addr, pair[0], true, &pair[1], 1u, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
                if(rc != 0)
                {
                    rc = nv.WriteI2C(addr, 0x00, false, pair, 2u, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
                }
                if(rc != 0)
                {
                    return rc;
                }
                Sleep(3);
            }
            uint8_t commit_pair[2] = { 0x17, 0x01 };
            int crc = nv.WriteI2C(addr, 0x17, true, &commit_pair[1], 1u, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
            if(crc != 0)
            {
                crc = nv.WriteI2C(addr, 0x00, false, commit_pair, 2u, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
            }
            if(crc != 0)
            {
                return crc;
            }
            Sleep(10);
        }
        return 0;
    };

    auto write_spectra_v2 = [&](uint8_t addr, uint32_t disp_mask, uint8_t is_ddc, uint8_t port_id, uint32_t port_set, uint32_t speed_khz, bool use_ex, bool reg_split) -> int
    {
        for(uint8_t z = 0; z < ZOTAC_ZONE_COUNT; z++)
        {
            uint8_t v2_pkt[10] = { 0x0F, 0x00, z, 0x00, r, g, b, 0x05, 0xFF, 0x01 };
            int rc = nv.WriteI2C(
                addr,
                v2_pkt[0],
                reg_split,
                reg_split ? (v2_pkt + 1) : v2_pkt,
                reg_split ? 9u : 10u,
                disp_mask,
                is_ddc,
                port_id,
                port_set,
                speed_khz,
                use_ex
            );
            if(rc != 0)
            {
                return rc;
            }
        }
        return 0;
    };

    auto try_write = [&](uint8_t addr, int proto, uint32_t disp_mask, uint8_t is_ddc, uint8_t port_id, uint32_t port_set, uint32_t speed_khz, bool use_ex, bool reg_split) -> int
    {
        if(proto == ZOTAC_PROTO_4A)
        {
            return write_zotac_4a(addr, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
        }
        if(proto == ZOTAC_PROTO_BLACKWELL)
        {
            return write_blackwell(addr, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
        }
        return write_spectra_v2(addr, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex, reg_split);
    };

    last_write_start_ms = GetNowMs();
    int rc = try_write(
        active_i2c_addr,
        active_proto,
        active_display_mask,
        active_is_ddc,
        active_port_id,
        active_port_set,
        active_speed_khz,
        active_use_ex_api,
        active_reg_split
    );
    if(rc == 0)
    {
        last_write_end_ms   = GetNowMs();
        hw_backoff_until_ms = 0;
        pending_hw_flush    = false;
        last_nvapi_status   = 0;
        last_nvapi_error.clear();
        has_applied_color   = true;
        return true;
    }

    if(!force_immediate && detected)
    {
        const uint64_t fail_ms = GetNowMs();
        last_write_end_ms      = fail_ms;
        hw_backoff_until_ms    = fail_ms + 1000ULL;
        last_nvapi_status      = rc;
        last_nvapi_error       = nv.FormatError(rc);
        return false;
    }

    int first_err = rc;

    struct RouteProbe { uint8_t is_ddc; uint8_t port; uint32_t set; uint32_t speed; bool use_ex; };
    const RouteProbe routes[] = {
        { 1, 1, 1, 2u, false }, // Exact Zotac 4A FireStorm DDC route (10kHz)
        { 1, 1, 1, 4u, false }, // Exact FireStorm RTX 5070 Ti Blackwell DDC route (100kHz)
        { 1, 1, 1, 0u, false }, // DDC default speed
        { 0, 1, 1, 0u, true  }, // OpenRGB i2c_smbus_nvapi Ex route
        { 0, 1, 1, 4u, true  }, // Ex 100kHz
        { 0, 0, 1, 0u, true  },
        { 0, 0, 0, 0u, true  },
    };

    // Safely probe read signatures before writing across all active display masks
    for(const auto& rt : routes)
    {
        std::vector<uint32_t> masks;
        if(rt.is_ddc)
        {
            masks = nv.display_masks;
        }
        else
        {
            masks.push_back(0u);
        }

        for(uint32_t mask : masks)
        {
            if(rt.is_ddc && mask == 0u) continue;

            uint8_t id_buf[32] = {};
            if(nv.ReadI2C(active_i2c_addr, 0x5A, true, id_buf, 32u, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex) == 0 && id_buf[0] == 'N')
            {
                last_write_start_ms = GetNowMs();
                int wrc = write_zotac_4a(active_i2c_addr, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex);
                if(wrc == 0)
                {
                    active_proto        = ZOTAC_PROTO_4A;
                    active_display_mask = mask;
                    active_is_ddc       = rt.is_ddc;
                    active_port_id      = rt.port;
                    active_port_set     = rt.set;
                    active_speed_khz    = rt.speed;
                    active_use_ex_api   = rt.use_ex;
                    last_write_end_ms   = GetNowMs();
                    pending_hw_flush    = false;
                    last_nvapi_status   = 0;
                    last_nvapi_error.clear();
                    has_applied_color   = true;
                    return true;
                }
            }

            uint8_t bw_val = 0;
            if(nv.ReadI2C(active_i2c_addr, 0xF0, true, id_buf, 32u, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex) == 0 ||
               nv.ReadI2C(active_i2c_addr, 0x10, true, &bw_val, 1u, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex) == 0)
            {
                int target_proto = (active_proto == ZOTAC_PROTO_V2) ? ZOTAC_PROTO_V2 : ZOTAC_PROTO_BLACKWELL;
                last_write_start_ms = GetNowMs();
                int wrc = try_write(active_i2c_addr, target_proto, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex, true);
                if(wrc == 0)
                {
                    active_proto        = target_proto;
                    active_display_mask = mask;
                    active_is_ddc       = rt.is_ddc;
                    active_port_id      = rt.port;
                    active_port_set     = rt.set;
                    active_speed_khz    = rt.speed;
                    active_use_ex_api   = rt.use_ex;
                    active_reg_split    = true;
                    last_write_end_ms   = GetNowMs();
                    pending_hw_flush    = false;
                    last_nvapi_status   = 0;
                    last_nvapi_error.clear();
                    has_applied_color   = true;
                    return true;
                }
            }
        }
    }

    last_write_end_ms = GetNowMs();
    last_nvapi_status = first_err;
    last_nvapi_error  = nv.FormatError(first_err);
    return false;
#else
    (void)mode_id; (void)r; (void)g; (void)b; (void)speed_pct; (void)is_on; (void)force_immediate;
    return false;
#endif
}

std::string OpenRGBZotac5070TiPlugin::ScanNVAPIBus()
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(nvapi_mutex);
    NvapiSession& nv = NvapiSession::Get();
    if(!nv.EnsureReady())
    {
        return nv.err_note.empty() ? "NVAPI initialization failed." : nv.err_note;
    }

    std::string found;
    int last_rc = -1;
    bool adopted = false;

    auto extract_ascii = [](const uint8_t* buf, int max_len) -> std::string
    {
        std::string s;
        for(int i = 0; i < max_len && buf[i] != 0x00 && buf[i] != 0xFF; i++)
        {
            if(buf[i] >= 0x20 && buf[i] <= 0x7E)
            {
                s.push_back(static_cast<char>(buf[i]));
            }
        }
        return s;
    };

    auto record_hit = [&](const char* route_lbl, uint32_t mask, uint8_t is_ddc, uint8_t port, uint32_t set, uint32_t speed, bool use_ex, uint8_t addr, uint8_t reg, uint8_t val, int proto, const std::string& fw_hint)
    {
        char hit[112] = {};
        if(!fw_hint.empty())
        {
            std::snprintf(hit, sizeof(hit), "%s(m%u)@0x%02X[r%02X=%s] ", route_lbl, mask, addr, reg, fw_hint.c_str());
        }
        else
        {
            std::snprintf(hit, sizeof(hit), "%s(m%u)@0x%02X[r%02X=0x%02X] ", route_lbl, mask, addr, reg, val);
        }
        found += hit;

        if(!adopted && (addr == 0x4A || addr == 0x4B || addr == 0x49 || addr == ZOTAC_SPECTRA_I2C_ADDR || (addr >= 0x48 && addr <= 0x4F)))
        {
            active_i2c_addr     = addr;
            active_display_mask = mask;
            active_is_ddc       = is_ddc;
            active_port_id      = port;
            active_port_set     = set;
            active_speed_khz    = speed;
            active_use_ex_api   = use_ex;
            active_proto        = proto;
            adopted             = true;
            if(!fw_hint.empty())
            {
                detected_fw_version = fw_hint;
            }
        }
    };

    struct RouteScan
    {
        const char* label;
        bool        use_ex;
        uint8_t     is_ddc;
        uint8_t     port;
        uint32_t    set;
        uint32_t    speed;
    };
    const RouteScan routes[] = {
        { "ddc10k", false, 1, 1, 1, 2u }, // Zotac4A / FireStorm 0x4A route
        { "ddc100k", false, 1, 1, 1, 4u }, // FireStorm 0x4B Blackwell route
        { "ddcDef", false, 1, 1, 1, 0u },
        { "exDef",  true,  0, 1, 1, 0u }, // OpenRGB i2c_smbus_nvapi default route
        { "ex100k", true,  0, 1, 1, 4u },
        { "exP0",   true,  0, 0, 1, 0u },
    };

    const uint8_t extra_addrs[] = { 0x49, 0x48, 0x4C, 0x4D, 0x4E, 0x4F, 0x20, 0x29, 0x60, 0x68, 0x6A, 0x70 };

    for(const auto& rt : routes)
    {
        std::vector<uint32_t> masks;
        if(rt.is_ddc)
        {
            for(uint32_t m : nv.display_masks)
            {
                if(m != 0u) masks.push_back(m);
            }
            if(masks.empty()) masks.push_back(1u);
        }
        else
        {
            masks.push_back(0u);
        }

        for(uint32_t mask : masks)
        {
            // 1) Zotac 4A signature (0x4A reg 0x5A, 32 bytes)
            uint8_t blk[33] = {};
            int rc4a = nv.ReadI2C(0x4A, 0x5A, true, blk, 32u, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex);
            last_rc = rc4a;
            if(rc4a == 0)
            {
                std::string fw = extract_ascii(blk, 10);
                record_hit(rt.label, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex, 0x4A, 0x5A, blk[0], ZOTAC_PROTO_4A, fw);
            }

            // 2) Zotac Blackwell signature (0x4B reg 0xF0 32 bytes, or reg 0x10 1 byte)
            std::memset(blk, 0, sizeof(blk));
            int rcbw_fw = nv.ReadI2C(0x4B, 0xF0, true, blk, 32u, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex);
            if(rcbw_fw == 0)
            {
                std::string fw = extract_ascii(blk, 16);
                record_hit(rt.label, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex, 0x4B, 0xF0, blk[0], ZOTAC_PROTO_BLACKWELL, fw);
            }
            else
            {
                uint8_t val = 0;
                int rcbw = nv.ReadI2C(0x4B, 0x10, true, &val, 1u, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex);
                last_rc = rcbw;
                if(rcbw == 0)
                {
                    record_hit(rt.label, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex, 0x4B, 0x10, val, ZOTAC_PROTO_BLACKWELL, "");
                }
            }

            // 3) Secondary ZOTAC addresses
            for(uint8_t addr : extra_addrs)
            {
                uint8_t val = 0;
                int rc = nv.ReadI2C(addr, 0x00, true, &val, 1u, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex);
                if(rc == 0)
                {
                    record_hit(rt.label, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex, addr, 0x00, val, ZOTAC_PROTO_V2, "");
                }
            }

            if(adopted) break;
        }
        if(adopted) break;
    }

    // If quick list missed, sweep full 7-bit 0x08..0x77 on BOTH DDC(mask=first, spd=10k) and Ex(mask=0, spd=0)
    if(found.empty())
    {
        uint32_t first_ddc_mask = 1u;
        for(uint32_t m : nv.display_masks)
        {
            if(m != 0u) { first_ddc_mask = m; break; }
        }

        for(uint8_t addr = 0x08; addr <= 0x77; addr++)
        {
            uint8_t blk[32] = {};
            uint8_t val = 0;
            if(nv.ReadI2C(addr, 0x5A, true, blk, 32u, first_ddc_mask, 1, 1, 1, 2u, false) == 0)
            {
                record_hit("swp-ddc", first_ddc_mask, 1, 1, 1, 2u, false, addr, 0x5A, blk[0], (addr == 0x4B) ? ZOTAC_PROTO_BLACKWELL : ZOTAC_PROTO_4A, extract_ascii(blk, 10));
                continue;
            }
            if(nv.ReadI2C(addr, 0x00, true, &val, 1u, first_ddc_mask, 1, 1, 1, 2u, false) == 0)
            {
                record_hit("swp-ddc", first_ddc_mask, 1, 1, 1, 2u, false, addr, 0x00, val, (addr == 0x4B) ? ZOTAC_PROTO_BLACKWELL : ZOTAC_PROTO_4A, "");
                continue;
            }
            int rc = nv.ReadI2C(addr, 0x10, true, &val, 1u, 0u, 0, 1, 1, 0u, true);
            if(rc != 0) rc = nv.ReadI2C(addr, 0x00, true, &val, 1u, 0u, 0, 1, 1, 0u, true);
            last_rc = rc;
            if(rc == 0)
            {
                record_hit("swp-ex", 0u, 0, 1, 1, 0u, true, addr, 0x00, val, (addr == 0x4B) ? ZOTAC_PROTO_BLACKWELL : ZOTAC_PROTO_V2, "");
            }
        }
    }

    char header[192] = {};
    if(!detected_fw_version.empty())
    {
        std::snprintf(header, sizeof(header), "PCI=0x%08X Sub=0x%08X FW=%s | ", nv.pci_dev, nv.pci_sub, detected_fw_version.c_str());
    }
    else
    {
        std::snprintf(header, sizeof(header), "PCI=0x%08X Sub=0x%08X | ", nv.pci_dev, nv.pci_sub);
    }

    if(!found.empty())
    {
        return std::string(header) + "ACKs: " + found;
    }

    uint32_t ddc_mask = 1u;
    for(uint32_t m : nv.display_masks)
    {
        if(m != 0u) { ddc_mask = m; break; }
    }
    uint8_t edid_b = 0;
    int ddc_rc = nv.ReadI2C(0x50, 0x00, true, &edid_b, 1u, ddc_mask, 1, 1, 1, 2u, false);
    char fail_buf[192] = {};
    std::snprintf(
        fail_buf,
        sizeof(fail_buf),
        "No ACK on 0x4A/0x4B or 0x08-0x77 (last rc=%d, DDC@0x50 m%u rc=%d). Exit FireStorm & run OpenRGB as Admin.",
        last_rc,
        ddc_mask,
        ddc_rc
    );
    return std::string(header) + fail_buf;
#else
    return "NVAPI scan is Windows-only.";
#endif
}

void OpenRGBZotac5070TiPlugin::FlushPendingNVAPI()
{
    int pm = 0;
    unsigned char pr = 0, pg = 0, pb = 0;
    unsigned int ps = 50;
    bool pon = true;
    {
        std::lock_guard<std::mutex> lock(nvapi_mutex);
        if(!pending_hw_flush)
        {
            return;
        }
        const uint64_t now_ms = GetNowMs();
        if(!CanWriteHardwareNow(now_ms))
        {
            return;
        }
        pm = pending_mode_id;
        pr = pending_r;
        pg = pending_g;
        pb = pending_b;
        ps = pending_speed_pct;
        pon = pending_is_on;
        pending_hw_flush = false;
    }
    WriteModeNVAPI(pm, pr, pg, pb, ps, pon, true);
}

void OpenRGBZotac5070TiPlugin::OnSyncTimerTick()
{
    if(in_timer_tick)
    {
        return;
    }
    in_timer_tick = true;

    const uint64_t now_ms = GetNowMs();

    FlushPendingNVAPI();

    if(sw_cycle_active.load() || (mirror_active && !virtual_mode_locked.load() && mirrored_cycle_active.load()))
    {
        if(last_cycle_step_ms == 0)
        {
            last_cycle_step_ms = now_ms;
        }
        uint64_t dt_ms = now_ms - last_cycle_step_ms;
        if(dt_ms > 0)
        {
            float spd = static_cast<float>(std::min(sw_cycle_speed_pct.load(), 100u));
            float deg_per_sec = 15.0f + (spd * 1.3f);
            sw_cycle_hue += (deg_per_sec * static_cast<float>(dt_ms)) / 1000.0f;
            while(sw_cycle_hue >= 360.0f) sw_cycle_hue -= 360.0f;
            last_cycle_step_ms = now_ms;
        }

        if(CanWriteHardwareNow(now_ms))
        {
            unsigned char cr = 0, cg = 0, cb = 0;
            HsvToRgb(sw_cycle_hue, cr, cg, cb);
            if(!has_applied_color || cr != last_r || cg != last_g || cb != last_b)
            {
                last_r = cr;
                last_g = cg;
                last_b = cb;
                WriteModeNVAPI(ZOTAC_5070TI_MODE_STATIC, cr, cg, cb, 50, true, false);
                SyncVirtualPreview(cr, cg, cb);
            }
        }
    }
    else
    {
        last_cycle_step_ms = now_ms;
    }

    // Do not overwrite GPU with mirror while user/Effects is actively driving the GPU device
    if(mirror_active && !virtual_mode_locked.load())
    {
        uint64_t last_virt = last_virtual_update_ms.load();
        if((last_virt == 0 || (now_ms - last_virt) > 1500ULL) &&
           (last_mirror_poll_ms == 0 || (now_ms - last_mirror_poll_ms) >= 80ULL))
        {
            last_mirror_poll_ms = now_ms;
            SyncFromOpenRGBDevices();
        }
    }

    in_timer_tick = false;
}

void OpenRGBZotac5070TiPlugin::SyncFromOpenRGBDevices()
{
    if(api == nullptr || !mirror_active || virtual_mode_locked.load())
    {
        return;
    }

    auto controllers = api->GetRGBControllers();
    if(controllers.empty())
    {
        return;
    }

    bool changed_found = false;
    unsigned char changed_r = last_r;
    unsigned char changed_g = last_g;
    unsigned char changed_b = last_b;

    bool initial_lit_found = false;
    unsigned char init_r = last_r;
    unsigned char init_g = last_g;
    unsigned char init_b = last_b;
    bool had_tracked_before = !tracked_ctrl_colors.empty();
    bool other_hw_cycle_found = false;

    std::unordered_map<RGBControllerInterface*, uint32_t> next_map;
    for(size_t i = 0; i < controllers.size(); i++)
    {
        RGBControllerInterface* ctrl = controllers[i];
        if(ctrl == nullptr || ctrl == virt_ctrl || ctrl->GetHidden())
        {
            continue;
        }

        int m_active = ctrl->GetActiveMode();
        if(m_active >= 0 && static_cast<unsigned int>(m_active) < ctrl->GetModeCount())
        {
            if(IsCycleModeName(ctrl->GetModeName(static_cast<unsigned int>(m_active))))
            {
                other_hw_cycle_found = true;
            }
        }

        unsigned char cr = 0, cg = 0, cb = 0;
        if(!TryExtractControllerColor(ctrl, cr, cg, cb))
        {
            continue;
        }

        uint32_t packed = PackRgb24(cr, cg, cb);
        next_map[ctrl] = packed;

        auto prev_it = tracked_ctrl_colors.find(ctrl);
        if(prev_it != tracked_ctrl_colors.end())
        {
            if(prev_it->second != packed)
            {
                changed_r = cr;
                changed_g = cg;
                changed_b = cb;
                changed_found = true;
            }
        }
        else if(!had_tracked_before && !initial_lit_found && packed != 0)
        {
            init_r = cr;
            init_g = cg;
            init_b = cb;
            initial_lit_found = true;
        }
    }

    tracked_ctrl_colors = std::move(next_map);

    if(changed_found)
    {
        mirrored_cycle_active.store(false);
        if(has_applied_color && changed_r == last_r && changed_g == last_g && changed_b == last_b)
        {
            return;
        }
        last_r = changed_r;
        last_g = changed_g;
        last_b = changed_b;
        WriteModeNVAPI(ZOTAC_5070TI_MODE_STATIC, last_r, last_g, last_b, 50, true, false);
        SyncVirtualPreview(last_r, last_g, last_b);
        return;
    }

    if(other_hw_cycle_found)
    {
        mirrored_cycle_active.store(true);
        return;
    }

    mirrored_cycle_active.store(false);

    if(!has_applied_color && initial_lit_found)
    {
        last_r = init_r;
        last_g = init_g;
        last_b = init_b;
        WriteModeNVAPI(ZOTAC_5070TI_MODE_STATIC, last_r, last_g, last_b, 50, true, false);
        SyncVirtualPreview(last_r, last_g, last_b);
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

    std::string scan_note = ScanNVAPIBus();
    const char* proto_name = (active_proto == ZOTAC_PROTO_4A)
        ? "Zotac 4A (0x5A)"
        : (active_proto == ZOTAC_PROTO_BLACKWELL ? "Blackwell (0x20-0x2F)" : "SPECTRA V2");

    if(WriteColorNVAPI(last_r, last_g, last_b))
    {
        detected = true;
        RegisterVirtualGPUDevice();
        char ok_buf[384] = {};
        std::snprintf(
            ok_buf,
            sizeof(ok_buf),
            "NVAPI Bridge active on 0x%02X (%s, m%u ddc=%u spd=%u) — registered in OpenRGB Devices & Effects. %s",
            active_i2c_addr,
            proto_name,
            active_display_mask,
            active_is_ddc,
            active_speed_khz,
            scan_note.c_str()
        );
        status_line = ok_buf;
    }
    else
    {
        char err_line[512] = {};
        std::snprintf(
            err_line,
            sizeof(err_line),
            "NVAPI write failed on 0x%02X (%s, rc=%d). %s If GPU LEDs remain frozen on one color (e.g. orange), shut down PC & turn PSU switch OFF for 20s to reset GPU MCU.",
            active_i2c_addr,
            last_nvapi_error.c_str(),
            last_nvapi_status,
            scan_note.c_str()
        );
        status_line = err_line;
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
    QLabel* pci_label = new QLabel("Target PCI: 0x10DE:0x2C05 | Subsys: 0x19DA:0x2765");
    QLabel* state_label = new QLabel(QString::fromStdString(status_line));
    state_label->setWordWrap(true);

    QHBoxLayout* cfg_row = new QHBoxLayout();
    QLabel* addr_lbl = new QLabel("I2C Addr:");
    QComboBox* addr_box = new QComboBox();
    const uint8_t addr_choices[] = { 0x4A, 0x4B, 0x49, 0x48, 0x4C, 0x4E, 0x68, 0x6A, 0x29 };
    for(uint8_t a : addr_choices)
    {
        addr_box->addItem(QString("0x%1").arg(a, 2, 16, QChar('0')).toUpper(), static_cast<int>(a));
    }
    int initial_addr_idx = addr_box->findData(static_cast<int>(active_i2c_addr));
    if(initial_addr_idx >= 0)
    {
        addr_box->setCurrentIndex(initial_addr_idx);
    }

    QLabel* proto_lbl = new QLabel("Protocol:");
    QComboBox* proto_box = new QComboBox();
    proto_box->addItem("Zotac 4A Block (0x5A · SOLID)", ZOTAC_PROTO_4A);
    proto_box->addItem("Blackwell MCU (0x20-0x2F)", ZOTAC_PROTO_BLACKWELL);
    proto_box->addItem("SPECTRA V2 (0x0F)", ZOTAC_PROTO_V2);
    int initial_proto_idx = proto_box->findData(active_proto);
    if(initial_proto_idx >= 0)
    {
        proto_box->setCurrentIndex(initial_proto_idx);
    }

    auto sync_combos = [this, addr_box, proto_box]() {
        QSignalBlocker b1(addr_box);
        QSignalBlocker b2(proto_box);
        int aidx = addr_box->findData(static_cast<int>(active_i2c_addr));
        if(aidx >= 0) addr_box->setCurrentIndex(aidx);
        int pidx = proto_box->findData(active_proto);
        if(pidx >= 0) proto_box->setCurrentIndex(pidx);
    };

    QObject::connect(addr_box, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, addr_box, state_label](int idx) {
        if(idx >= 0)
        {
            active_i2c_addr = static_cast<uint8_t>(addr_box->itemData(idx).toInt());
            if(WriteColorNVAPI(last_r, last_g, last_b))
            {
                state_label->setText(QString("Switched to I2C 0x%1 (m%2) — write ACKed.")
                    .arg(active_i2c_addr, 2, 16, QChar('0')).toUpper().arg(active_display_mask));
            }
            else
            {
                state_label->setText(QString("Switched to I2C 0x%1 — write returned %2 (rc=%3).")
                    .arg(active_i2c_addr, 2, 16, QChar('0')).toUpper()
                    .arg(QString::fromStdString(last_nvapi_error)).arg(last_nvapi_status));
            }
        }
    });

    QObject::connect(proto_box, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, proto_box, state_label](int idx) {
        if(idx >= 0)
        {
            active_proto = proto_box->itemData(idx).toInt();
            if(active_proto == ZOTAC_PROTO_4A)
            {
                active_speed_khz = 2u;
                active_is_ddc = 1;
                if(active_display_mask == 0u) active_display_mask = 1u;
                active_use_ex_api = false;
            }
            if(WriteColorNVAPI(last_r, last_g, last_b))
            {
                state_label->setText(QString("Protocol updated on 0x%1 — write ACKed.")
                    .arg(active_i2c_addr, 2, 16, QChar('0')).toUpper());
            }
        }
    });

    cfg_row->addWidget(addr_lbl);
    cfg_row->addWidget(addr_box);
    cfg_row->addWidget(proto_lbl);
    cfg_row->addWidget(proto_box);
    cfg_row->addStretch();

    QCheckBox* mirror_chk = new QCheckBox("Mirror color from active OpenRGB devices");
    mirror_chk->setChecked(mirror_active);
    QObject::connect(mirror_chk, &QCheckBox::toggled, [this, state_label](bool checked) {
        mirror_active = checked;
        if(checked)
        {
            sw_cycle_active.store(false);
            virtual_mode_locked.store(false);
            last_virtual_update_ms.store(0);
            tracked_ctrl_colors.clear();
            has_applied_color = false;
            SyncFromOpenRGBDevices();
            state_label->setText("Mirroring enabled: synced from active OpenRGB controller.");
        }
        else
        {
            mirrored_cycle_active.store(false);
        }
    });

    QHBoxLayout* btn_row = new QHBoxLayout();
    QPushButton* pick_btn = new QPushButton("Pick GPU Color...");
    QPushButton* rescan_btn = new QPushButton("Re-Scan / Sync Now");
    QPushButton* bus_scan_btn = new QPushButton("Scan GPU I2C Bus");

    QObject::connect(pick_btn, &QPushButton::clicked, [this, state_label]() {
        QColor initial(last_r, last_g, last_b);
        QColor chosen = QColorDialog::getColor(initial, widget, "Choose ZOTAC RTX 5070 Ti Color");
        if(chosen.isValid())
        {
            sw_cycle_active.store(false);
            mirrored_cycle_active.store(false);
            virtual_mode_locked.store(false);
            last_virtual_update_ms.store(GetNowMs());
            last_r = static_cast<unsigned char>(chosen.red());
            last_g = static_cast<unsigned char>(chosen.green());
            last_b = static_cast<unsigned char>(chosen.blue());
            if(WriteColorNVAPI(last_r, last_g, last_b))
            {
                SyncVirtualPreview(last_r, last_g, last_b);
                state_label->setText(QString("Applied RGB(%1, %2, %3) via NVAPI I2C 0x%4 (m%5).")
                    .arg(last_r).arg(last_g).arg(last_b)
                    .arg(active_i2c_addr, 2, 16, QChar('0')).toUpper().arg(active_display_mask));
            }
            else
            {
                state_label->setText(QString("Write failed on 0x%1 (%2, rc=%3).")
                    .arg(active_i2c_addr, 2, 16, QChar('0')).toUpper()
                    .arg(QString::fromStdString(last_nvapi_error)).arg(last_nvapi_status));
            }
        }
    });

    QObject::connect(rescan_btn, &QPushButton::clicked, [this, state_label, sync_combos]() {
        ScanAndRegisterZotacGPU();
        sync_combos();
        if(mirror_active)
        {
            virtual_mode_locked.store(false);
            last_virtual_update_ms.store(0);
            tracked_ctrl_colors.clear();
            SyncFromOpenRGBDevices();
        }
        state_label->setText(QString::fromStdString(status_line));
    });

    QObject::connect(bus_scan_btn, &QPushButton::clicked, [this, state_label, sync_combos]() {
        std::string scan_res = ScanNVAPIBus();
        sync_combos();
        state_label->setText(QString::fromStdString(scan_res));
    });

    btn_row->addWidget(pick_btn);
    btn_row->addWidget(rescan_btn);
    btn_row->addWidget(bus_scan_btn);

    layout->addWidget(title);
    layout->addWidget(pci_label);
    layout->addLayout(cfg_row);
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
    if(sync_timer != nullptr)
    {
        sync_timer->stop();
        delete sync_timer;
        sync_timer = nullptr;
    }
    if(api != nullptr && virt_ctrl != nullptr)
    {
        virt_ctrl->ClearCallbacks();
        api->UnregisterVirtualRGBController(virt_ctrl);
        api->DeleteVirtualRGBController(virt_ctrl);
        virt_ctrl = nullptr;
    }
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
            SyncVirtualPreview(last_r, last_g, last_b);
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
    if(QThread::currentThread() == this->thread())
    {
        SyncFromOpenRGBDevices();
        return;
    }
    if(!sync_update_queued.exchange(true))
    {
        QMetaObject::invokeMethod(
            this,
            [this]() {
                sync_update_queued.store(false);
                SyncFromOpenRGBDevices();
            },
            Qt::QueuedConnection
        );
    }
}

void OpenRGBZotac5070TiPlugin::ResourceManagerUpdated(unsigned int /*event_type*/)
{
    if(QThread::currentThread() == this->thread())
    {
        SyncFromOpenRGBDevices();
        return;
    }
    if(!sync_update_queued.exchange(true))
    {
        QMetaObject::invokeMethod(
            this,
            [this]() {
                sync_update_queued.store(false);
                SyncFromOpenRGBDevices();
            },
            Qt::QueuedConnection
        );
    }
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

#include "OpenRGBZotac5070TiPlugin.h"
#include "RGBController_Zotac5070Ti.h"
#include "i2c_smbus.h"
#include "LogManager.h"
#include "StringUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QColorDialog>
#include <QColor>
#include <QSignalBlocker>
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

    float ToLinear(unsigned char v, bool gamma_enabled)
    {
        const float n = static_cast<float>(v) / 255.0f;
        return gamma_enabled ? std::pow(n, 2.2f) : n;
    }

    unsigned char FromLinear(float lin, bool gamma_enabled)
    {
        const float clamped = std::clamp(lin, 0.0f, 1.0f);
        const float encoded = gamma_enabled ? std::pow(clamped, 1.0f / 2.2f) : clamped;
        const int val = static_cast<int>(std::lround(encoded * 255.0f));
        return static_cast<unsigned char>(std::clamp(val, 0, 255));
    }

    void HsvToRgb(float h_deg, unsigned char& r, unsigned char& g, unsigned char& b, bool gamma_lut)
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

        if(gamma_lut)
        {
            // Keep smooth perceptual transitions across saturated primaries
            rf = std::pow(rf, 1.0f / 1.15f);
            gf = std::pow(gf, 1.0f / 1.15f);
            bf = std::pow(bf, 1.0f / 1.15f);
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

    const char* SpeedKhzLabel(uint32_t speed_khz)
    {
        switch(speed_khz)
        {
            case 4u: return "100 kHz";
            case 3u: return "40 kHz";
            case 2u: return "10 kHz";
            default: return "Driver Default";
        }
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
            AddMaskUnique(1u);
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
OpenRGBZotac5070TiPlugin::~OpenRGBZotac5070TiPlugin()
{
    StopWorkerThread();
}

OpenRGBPluginInfo OpenRGBZotac5070TiPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name           = "ZOTAC RTX 5070 Ti SPECTRA Bridge";
    info.Description    = "60 Hz Smooth-Engine NVAPI/I2C Bridge for ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC (0x19DA:0x2765).";
    info.Version        = "2.0.0";
    info.Commit         = "smooth-engine";
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
    StartWorkerThread();

    if(sync_timer == nullptr)
    {
        sync_timer = new QTimer(this);
        QObject::connect(sync_timer, &QTimer::timeout, this, &OpenRGBZotac5070TiPlugin::OnSyncTimerTick);
        // Lightweight Qt-thread sampler & preview updater (no I2C calls on GUI thread)
        sync_timer->start(16);
    }
}

void OpenRGBZotac5070TiPlugin::StartWorkerThread()
{
    if(worker_thread.joinable())
    {
        return;
    }
    worker_stop.store(false);
    worker_thread = std::thread(&OpenRGBZotac5070TiPlugin::WorkerLoop, this);
}

void OpenRGBZotac5070TiPlugin::StopWorkerThread()
{
    worker_stop.store(true);
    worker_cv.notify_all();
    if(worker_thread.joinable())
    {
        worker_thread.join();
    }
}

void OpenRGBZotac5070TiPlugin::SubmitTargetState(
    int           mode_id,
    unsigned char r,
    unsigned char g,
    unsigned char b,
    unsigned int  speed_pct,
    bool          is_on,
    bool          immediate_snap
)
{
    {
        std::lock_guard<std::mutex> lock(target_mutex);
        target_mode_id   = mode_id;
        target_r         = r;
        target_g         = g;
        target_b         = b;
        target_speed_pct = speed_pct;
        target_is_on     = is_on;
        target_dirty     = true;
        if(immediate_snap)
        {
            target_snap = true;
        }
    }
    worker_cv.notify_one();
}

void OpenRGBZotac5070TiPlugin::WorkerLoop()
{
    using clock = std::chrono::steady_clock;

    bool          initialized   = false;
    float         cur_lin[3]    = { 0.0f, 0.0f, 0.0f };
    unsigned char sent_r        = 0;
    unsigned char sent_g        = 0;
    unsigned char sent_b        = 0;
    int           sent_mode     = -1;
    unsigned int  sent_speed    = 0;
    bool          sent_on       = false;
    bool          has_sent      = false;

    float         cycle_hue     = 0.0f;
    auto          prev_tick_tp  = clock::now();
    auto          fps_window_tp = clock::now();
    uint32_t      fps_counter   = 0;

    while(!worker_stop.load())
    {
        int           local_mode  = ZOTAC_5070TI_MODE_STATIC;
        unsigned char tgt_r       = 0;
        unsigned char tgt_g       = 0;
        unsigned char tgt_b       = 0;
        unsigned int  local_speed = 50;
        bool          local_on    = true;
        bool          do_snap     = false;
        bool          is_dirty    = false;

        {
            std::unique_lock<std::mutex> lock(target_mutex);
            const bool cycle_running = sw_cycle_active.load() || (mirror_active && !virtual_mode_locked.load() && mirrored_cycle_active.load());

            if(!target_dirty && !cycle_running && !worker_stop.load())
            {
                worker_cv.wait(lock, [this]() {
                    const bool cyc = sw_cycle_active.load() || (mirror_active && !virtual_mode_locked.load() && mirrored_cycle_active.load());
                    return worker_stop.load() || target_dirty || cyc;
                });
            }

            if(worker_stop.load())
            {
                break;
            }

            local_mode   = target_mode_id;
            tgt_r        = target_r;
            tgt_g        = target_g;
            tgt_b        = target_b;
            local_speed  = target_speed_pct;
            local_on     = target_is_on;
            do_snap      = target_snap;
            is_dirty     = target_dirty;
            target_dirty = false;
            target_snap  = false;
        }

        const auto now_tp = clock::now();
        const float dt_ms = std::clamp(
            std::chrono::duration<float, std::milli>(now_tp - prev_tick_tp).count(),
            1.0f,
            250.0f
        );
        prev_tick_tp = now_tp;

        const bool gamma_on      = gamma_correction.load();
        const bool cycle_running = sw_cycle_active.load() || (mirror_active && !virtual_mode_locked.load() && mirrored_cycle_active.load());

        unsigned char out_r = tgt_r;
        unsigned char out_g = tgt_g;
        unsigned char out_b = tgt_b;
        int           hw_mode = local_mode;
        bool          still_interpolating = false;

        if(cycle_running)
        {
            const float spd = static_cast<float>(std::min(sw_cycle_speed_pct.load(), 100u));
            const float deg_per_sec = 12.0f + (spd * 1.4f);
            cycle_hue = std::fmod(cycle_hue + (deg_per_sec * dt_ms) / 1000.0f, 360.0f);
            if(cycle_hue < 0.0f) cycle_hue += 360.0f;

            HsvToRgb(cycle_hue, out_r, out_g, out_b, gamma_on);
            cur_lin[0] = ToLinear(out_r, gamma_on);
            cur_lin[1] = ToLinear(out_g, gamma_on);
            cur_lin[2] = ToLinear(out_b, gamma_on);
            initialized = true;
            hw_mode = ZOTAC_5070TI_MODE_STATIC;
            local_on = true;
            still_interpolating = true;
        }
        else
        {
            const uint32_t smooth_ms = smooth_window_ms.load();
            const float tgt_lin[3] = {
                ToLinear(tgt_r, gamma_on),
                ToLinear(tgt_g, gamma_on),
                ToLinear(tgt_b, gamma_on)
            };

            if(!initialized || do_snap || smooth_ms == 0u || !local_on)
            {
                cur_lin[0] = tgt_lin[0];
                cur_lin[1] = tgt_lin[1];
                cur_lin[2] = tgt_lin[2];
                initialized = true;
            }
            else
            {
                const float tau_ms = std::max(4.0f, static_cast<float>(smooth_ms) / 3.0f);
                const float alpha  = 1.0f - std::exp(-dt_ms / tau_ms);
                const float eps    = 0.45f / 255.0f;

                for(int c = 0; c < 3; c++)
                {
                    const float diff = tgt_lin[c] - cur_lin[c];
                    if(std::fabs(diff) <= eps)
                    {
                        cur_lin[c] = tgt_lin[c];
                    }
                    else
                    {
                        cur_lin[c] += diff * alpha;
                        still_interpolating = true;
                    }
                }
            }

            out_r = FromLinear(cur_lin[0], gamma_on);
            out_g = FromLinear(cur_lin[1], gamma_on);
            out_b = FromLinear(cur_lin[2], gamma_on);
        }

        const bool state_changed = !has_sent ||
            out_r != sent_r || out_g != sent_g || out_b != sent_b ||
            hw_mode != sent_mode || local_speed != sent_speed || local_on != sent_on;

        uint32_t write_us = measured_write_us.load();

        if(state_changed || (is_dirty && do_snap))
        {
            if(WriteHardwareNVAPI(hw_mode, out_r, out_g, out_b, local_speed, local_on, write_us))
            {
                sent_r     = out_r;
                sent_g     = out_g;
                sent_b     = out_b;
                sent_mode  = hw_mode;
                sent_speed = local_speed;
                sent_on    = local_on;
                has_sent   = true;
                last_r     = out_r;
                last_g     = out_g;
                last_b     = out_b;

                displayed_rgb_packed.store(PackRgb24(out_r, out_g, out_b));
                measured_write_us.store(write_us);
                frames_sent.fetch_add(1);
                fps_counter++;
            }
        }
        else if(still_interpolating)
        {
            frames_coalesced.fetch_add(1);
        }

        const auto after_tp = clock::now();
        const float fps_elapsed_ms = std::chrono::duration<float, std::milli>(after_tp - fps_window_tp).count();
        if(fps_elapsed_ms >= 500.0f)
        {
            const uint32_t fps_x10 = static_cast<uint32_t>(std::lround((static_cast<float>(fps_counter) * 10000.0f) / fps_elapsed_ms));
            measured_fps_x10.store(fps_x10);
            fps_counter   = 0;
            fps_window_tp = after_tp;
        }

        // Pace adaptively: honor target_frame_ms while respecting physical I2C bus duration
        const uint32_t min_target_ms = std::max(8u, target_frame_ms.load());
        const uint32_t hw_floor_ms   = (write_us / 1000u) + 2u;
        const uint32_t pace_ms       = std::max(min_target_ms, hw_floor_ms);

        const auto next_wake_tp = now_tp + std::chrono::milliseconds(pace_ms);
        if(still_interpolating || cycle_running)
        {
            std::unique_lock<std::mutex> lock(target_mutex);
            worker_cv.wait_until(lock, next_wake_tp, [this]() {
                return worker_stop.load() || target_dirty;
            });
        }
        else
        {
            // Settled: sleep until next target change unless wake is already due
            std::unique_lock<std::mutex> lock(target_mutex);
            worker_cv.wait(lock, [this]() {
                const bool cyc = sw_cycle_active.load() || (mirror_active && !virtual_mode_locked.load() && mirrored_cycle_active.load());
                return worker_stop.load() || target_dirty || cyc;
            });
        }
    }
}

void OpenRGBZotac5070TiPlugin::OnVirtualUpdateLEDs(void* ptr)
{
    if(ptr != nullptr) static_cast<OpenRGBZotac5070TiPlugin*>(ptr)->HandleVirtualUpdate();
}

void OpenRGBZotac5070TiPlugin::OnVirtualUpdateZoneLEDs(void* ptr, int /*zone*/)
{
    if(ptr != nullptr) static_cast<OpenRGBZotac5070TiPlugin*>(ptr)->HandleVirtualUpdate();
}

void OpenRGBZotac5070TiPlugin::OnVirtualUpdateSingleLED(void* ptr, int /*led*/)
{
    if(ptr != nullptr) static_cast<OpenRGBZotac5070TiPlugin*>(ptr)->HandleVirtualUpdate();
}

void OpenRGBZotac5070TiPlugin::OnVirtualUpdateMode(void* ptr)
{
    if(ptr != nullptr) static_cast<OpenRGBZotac5070TiPlugin*>(ptr)->HandleVirtualUpdate();
}

void OpenRGBZotac5070TiPlugin::HandleVirtualUpdate()
{
    if(virt_ctrl == nullptr)
    {
        return;
    }

    last_virtual_update_ms.store(GetNowMs());

    int          m_idx        = virt_ctrl->GetActiveMode();
    int          mode_id      = ZOTAC_5070TI_MODE_DIRECT;
    unsigned int speed_pct    = 50;
    bool         is_on        = true;
    bool         use_sw_cycle = false;
    bool         lock_mode    = false;

    unsigned char r = last_r;
    unsigned char g = last_g;
    unsigned char b = last_b;

    if(m_idx >= 0 && static_cast<unsigned int>(m_idx) < virt_ctrl->GetModeCount())
    {
        unsigned int m     = static_cast<unsigned int>(m_idx);
        std::string  mname = virt_ctrl->GetModeName(m);
        unsigned int smax  = virt_ctrl->GetModeSpeedMax(m);
        if(smax > 0)
        {
            speed_pct = (virt_ctrl->GetModeSpeed(m) * 100u) / smax;
        }

        if(mname == "Off")
        {
            mode_id   = ZOTAC_5070TI_MODE_OFF;
            is_on     = false;
            lock_mode = true;
        }
        else if(mname == "Breathing")
        {
            mode_id   = ZOTAC_5070TI_MODE_BREATHING;
            lock_mode = true;
        }
        else if(mname == "Spectrum Cycle")
        {
            mode_id      = ZOTAC_5070TI_MODE_COLOR_CYCLE;
            use_sw_cycle = !prefer_hw_native_cycle.load();
            lock_mode    = true;
        }
        else if(mname == "Rainbow Wave")
        {
            mode_id      = ZOTAC_5070TI_MODE_RAINBOW;
            use_sw_cycle = !prefer_hw_native_cycle.load();
            lock_mode    = true;
        }
        else if(mname == "Hardware Fade")
        {
            mode_id   = ZOTAC_5070TI_MODE_HW_FADE;
            lock_mode = true;
        }
        else if(mname == "Spectrum Breathe")
        {
            mode_id   = ZOTAC_5070TI_MODE_SPECTRUM_BREATHE;
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
        worker_cv.notify_one();
        return;
    }

    TryExtractControllerColor(virt_ctrl, r, g, b);
    // In Direct mode (streaming effects), pass smooth interpolation; in Static/Off, honor smooth window too
    SubmitTargetState(mode_id, r, g, b, speed_pct, is_on, false);
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
    setup.description = "ZOTAC RTX 5070 Ti SPECTRA GPU Controller (60 Hz Smooth NVAPI Bridge)";
    setup.version     = detected_fw_version.empty() ? "2.0.0" : detected_fw_version;

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

    mode m_hw_fade;
    m_hw_fade.name       = "Hardware Fade";
    m_hw_fade.value      = ZOTAC_5070TI_MODE_HW_FADE;
    m_hw_fade.flags      = MODE_FLAG_HAS_SPEED;
    m_hw_fade.color_mode = MODE_COLORS_NONE;
    m_hw_fade.speed_min  = 0;
    m_hw_fade.speed_max  = 100;
    m_hw_fade.speed      = 50;
    setup.modes.push_back(m_hw_fade);

    mode m_spec_breathe;
    m_spec_breathe.name       = "Spectrum Breathe";
    m_spec_breathe.value      = ZOTAC_5070TI_MODE_SPECTRUM_BREATHE;
    m_spec_breathe.flags      = MODE_FLAG_HAS_SPEED;
    m_spec_breathe.color_mode = MODE_COLORS_NONE;
    m_spec_breathe.speed_min  = 0;
    m_spec_breathe.speed_max  = 100;
    m_spec_breathe.speed      = 50;
    setup.modes.push_back(m_spec_breathe);

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
        virt_ctrl->SetAllColors(ToRGBColor(last_r, last_g, last_b));
        api->RegisterVirtualRGBControllerInThread(virt_ctrl);
    }
}

uint32_t OpenRGBZotac5070TiPlugin::NegotiateFastestBusSpeed()
{
#ifdef _WIN32
    const int pref = i2c_speed_pref.load();
    if(pref == 2 || pref == 3 || pref == 4)
    {
        active_speed_khz = static_cast<uint32_t>(pref);
        return active_speed_khz;
    }

    NvapiSession& nv = NvapiSession::Get();
    if(!nv.EnsureReady())
    {
        return active_speed_khz;
    }

    const uint32_t candidates[] = { 4u, 3u, 2u }; // 100 kHz -> 40 kHz -> 10 kHz
    const uint8_t  probe_reg    = (active_proto == ZOTAC_PROTO_4A) ? 0x5Au : ((active_proto == ZOTAC_PROTO_BLACKWELL) ? 0x10u : 0x00u);
    const uint32_t probe_len    = (active_proto == ZOTAC_PROTO_4A) ? 32u : 1u;

    for(uint32_t spd : candidates)
    {
        bool ok_all = true;
        for(int attempt = 0; attempt < 2; attempt++)
        {
            uint8_t buf[32] = {};
            int rc = nv.ReadI2C(
                active_i2c_addr,
                probe_reg,
                true,
                buf,
                probe_len,
                active_display_mask,
                active_is_ddc,
                active_port_id,
                active_port_set,
                spd,
                active_use_ex_api
            );
            if(rc != 0)
            {
                ok_all = false;
                break;
            }
        }
        if(ok_all)
        {
            active_speed_khz = spd;
            return spd;
        }
    }
    active_speed_khz = 2u;
    return active_speed_khz;
#else
    return 2u;
#endif
}

std::string OpenRGBZotac5070TiPlugin::BenchmarkBusSpeed()
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(nvapi_mutex);
    NvapiSession& nv = NvapiSession::Get();
    if(!nv.EnsureReady())
    {
        return "NVAPI not ready.";
    }

    const uint32_t tiers[]   = { 4u, 3u, 2u };
    const uint8_t  probe_reg = (active_proto == ZOTAC_PROTO_4A) ? 0x5Au : 0x10u;
    const uint32_t probe_len = (active_proto == ZOTAC_PROTO_4A) ? 32u : 1u;

    std::string report;
    uint32_t fastest_ok = 2u;
    bool found_ok = false;

    for(uint32_t spd : tiers)
    {
        auto t0 = std::chrono::steady_clock::now();
        int passes = 0;
        for(int i = 0; i < 4; i++)
        {
            uint8_t buf[32] = {};
            if(nv.ReadI2C(active_i2c_addr, probe_reg, true, buf, probe_len, active_display_mask, active_is_ddc, active_port_id, active_port_set, spd, active_use_ex_api) == 0)
            {
                passes++;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double avg_ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / 4.0;

        char seg[64] = {};
        if(passes == 4)
        {
            std::snprintf(seg, sizeof(seg), "%s: OK (%.1f ms)  ", SpeedKhzLabel(spd), avg_ms);
            if(!found_ok)
            {
                fastest_ok = spd;
                found_ok = true;
            }
        }
        else
        {
            std::snprintf(seg, sizeof(seg), "%s: %d/4  ", SpeedKhzLabel(spd), passes);
        }
        report += seg;
    }

    if(found_ok && i2c_speed_pref.load() == 0)
    {
        active_speed_khz = fastest_ok;
    }
    return "Bus benchmark -> " + report + "(active: " + SpeedKhzLabel(active_speed_khz) + ")";
#else
    return "NVAPI benchmark is Windows-only.";
#endif
}

bool OpenRGBZotac5070TiPlugin::WriteHardwareNVAPI(
    int           mode_id,
    unsigned char r,
    unsigned char g,
    unsigned char b,
    unsigned int  speed_pct,
    bool          is_on,
    uint32_t&     out_write_us
)
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(nvapi_mutex);
    NvapiSession& nv = NvapiSession::Get();
    if(!nv.EnsureReady())
    {
        last_nvapi_status = nv.init_rc != 0 ? nv.init_rc : -1;
        last_nvapi_error  = nv.err_note;
        return false;
    }

    const int pref = i2c_speed_pref.load();
    if(pref == 2 || pref == 3 || pref == 4)
    {
        active_speed_khz = static_cast<uint32_t>(pref);
    }

    auto write_zotac_4a = [&](uint8_t addr, uint32_t disp_mask, uint8_t is_ddc, uint8_t port_id, uint32_t port_set, uint32_t speed_khz, bool use_ex) -> int
    {
        uint8_t mode_4a = 0x00;
        if(mode_id == ZOTAC_5070TI_MODE_BREATHING) mode_4a = 0x01;
        else if(mode_id == ZOTAC_5070TI_MODE_COLOR_CYCLE || mode_id == ZOTAC_5070TI_MODE_HW_FADE) mode_4a = 0x02;
        else if(mode_id == ZOTAC_5070TI_MODE_RAINBOW || mode_id == ZOTAC_5070TI_MODE_SPECTRUM_BREATHE) mode_4a = 0x04;

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

            bool any_written = false;
            for(int i = 0; i < 16; i++)
            {
                // Skip unchanged registers (cuts transition writes from 16 down to 3-4 bytes per zone)
                if(bw_shadow_valid[z] && bw_shadow[z][i] == regs[i] && i != 1)
                {
                    continue;
                }
                uint8_t val = regs[i];
                int rc = nv.WriteI2C(addr, static_cast<uint8_t>(0x20 + i), true, &val, 1u, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
                if(rc != 0)
                {
                    uint8_t pair[2] = { static_cast<uint8_t>(0x20 + i), val };
                    rc = nv.WriteI2C(addr, 0x00, false, pair, 2u, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
                }
                if(rc != 0)
                {
                    return rc;
                }
                bw_shadow[z][i] = regs[i];
                any_written = true;
            }
            bw_shadow_valid[z] = true;

            if(any_written)
            {
                uint8_t commit_val = 0x01;
                int crc = nv.WriteI2C(addr, 0x17, true, &commit_val, 1u, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
                if(crc != 0)
                {
                    uint8_t commit_pair[2] = { 0x17, 0x01 };
                    crc = nv.WriteI2C(addr, 0x00, false, commit_pair, 2u, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
                }
                if(crc != 0)
                {
                    return crc;
                }
            }
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
        if(proto == ZOTAC_PROTO_4A)        return write_zotac_4a(addr, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
        if(proto == ZOTAC_PROTO_BLACKWELL) return write_blackwell(addr, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex);
        return write_spectra_v2(addr, disp_mask, is_ddc, port_id, port_set, speed_khz, use_ex, reg_split);
    };

    const auto t0 = std::chrono::steady_clock::now();
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

    // Auto step-down if higher clock tier fails on write
    if(rc != 0 && pref == 0 && active_speed_khz > 2u)
    {
        const uint32_t fallback_speeds[] = { 3u, 2u };
        for(uint32_t fspd : fallback_speeds)
        {
            if(fspd >= active_speed_khz) continue;
            rc = try_write(
                active_i2c_addr,
                active_proto,
                active_display_mask,
                active_is_ddc,
                active_port_id,
                active_port_set,
                fspd,
                active_use_ex_api,
                active_reg_split
            );
            if(rc == 0)
            {
                active_speed_khz = fspd;
                break;
            }
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    out_write_us = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    if(rc == 0)
    {
        last_nvapi_status = 0;
        last_nvapi_error.clear();
        has_applied_color = true;
        return true;
    }

    int first_err = rc;
    struct RouteProbe { uint8_t is_ddc; uint8_t port; uint32_t set; uint32_t speed; bool use_ex; };
    const RouteProbe routes[] = {
        { 1, 1, 1, 4u, false }, // 100 kHz DDC
        { 1, 1, 1, 3u, false }, // 40 kHz DDC
        { 1, 1, 1, 2u, false }, // 10 kHz DDC (Zotac 4A safe fallback)
        { 1, 1, 1, 0u, false },
        { 0, 1, 1, 4u, true  },
        { 0, 1, 1, 0u, true  },
        { 0, 0, 1, 0u, true  },
    };

    for(const auto& rt : routes)
    {
        std::vector<uint32_t> masks = rt.is_ddc ? nv.display_masks : std::vector<uint32_t>{ 0u };
        for(uint32_t mask : masks)
        {
            if(rt.is_ddc && mask == 0u) continue;
            uint8_t id_buf[32] = {};
            if(nv.ReadI2C(active_i2c_addr, 0x5A, true, id_buf, 32u, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex) == 0 && id_buf[0] == 'N')
            {
                if(write_zotac_4a(active_i2c_addr, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex) == 0)
                {
                    active_proto        = ZOTAC_PROTO_4A;
                    active_display_mask = mask;
                    active_is_ddc       = rt.is_ddc;
                    active_port_id      = rt.port;
                    active_port_set     = rt.set;
                    active_speed_khz    = rt.speed;
                    active_use_ex_api   = rt.use_ex;
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
                bw_shadow_valid[0] = false;
                bw_shadow_valid[1] = false;
                if(try_write(active_i2c_addr, target_proto, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex, true) == 0)
                {
                    active_proto        = target_proto;
                    active_display_mask = mask;
                    active_is_ddc       = rt.is_ddc;
                    active_port_id      = rt.port;
                    active_port_set     = rt.set;
                    active_speed_khz    = rt.speed;
                    active_use_ex_api   = rt.use_ex;
                    active_reg_split    = true;
                    last_nvapi_status   = 0;
                    last_nvapi_error.clear();
                    has_applied_color   = true;
                    return true;
                }
            }
        }
    }

    last_nvapi_status = first_err;
    last_nvapi_error  = nv.FormatError(first_err);
    return false;
#else
    (void)mode_id; (void)r; (void)g; (void)b; (void)speed_pct; (void)is_on;
    out_write_us = 0;
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
    int  last_rc = -1;
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
        { "ddc10k",  false, 1, 1, 1, 2u },
        { "ddc100k", false, 1, 1, 1, 4u },
        { "ddcDef",  false, 1, 1, 1, 0u },
        { "exDef",   true,  0, 1, 1, 0u },
        { "ex100k",  true,  0, 1, 1, 4u },
        { "exP0",    true,  0, 0, 1, 0u },
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
            uint8_t blk[33] = {};
            int rc4a = nv.ReadI2C(0x4A, 0x5A, true, blk, 32u, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex);
            last_rc = rc4a;
            if(rc4a == 0)
            {
                std::string fw = extract_ascii(blk, 10);
                record_hit(rt.label, mask, rt.is_ddc, rt.port, rt.set, rt.speed, rt.use_ex, 0x4A, 0x5A, blk[0], ZOTAC_PROTO_4A, fw);
            }

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

    if(adopted)
    {
        NegotiateFastestBusSpeed();
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

    return std::string(header) + "No ACK on 0x4A/0x4B (last rc=" + std::to_string(last_rc) + "). Exit FireStorm & run OpenRGB as Admin.";
#else
    return "NVAPI scan is Windows-only.";
#endif
}

void OpenRGBZotac5070TiPlugin::OnSyncTimerTick()
{
    const uint64_t now_ms = GetNowMs();

    // Update Qt virtual controller preview safely on GUI thread
    const uint32_t disp_packed = displayed_rgb_packed.load();
    if(virt_ctrl != nullptr && disp_packed != last_qt_preview_rgb)
    {
        const unsigned char pr = static_cast<unsigned char>((disp_packed >> 16) & 0xFFu);
        const unsigned char pg = static_cast<unsigned char>((disp_packed >> 8) & 0xFFu);
        const unsigned char pb = static_cast<unsigned char>(disp_packed & 0xFFu);
        virt_ctrl->SetAllColors(ToRGBColor(pr, pg, pb));
        last_qt_preview_rgb = disp_packed;
    }

    if(mirror_active && !virtual_mode_locked.load())
    {
        uint64_t last_virt = last_virtual_update_ms.load();
        if(last_virt == 0 || (now_ms - last_virt) > 1500ULL)
        {
            SyncFromOpenRGBDevices();
        }
    }

    if(telemetry_label != nullptr && (now_ms - last_ui_telemetry_ms) >= 250ULL)
    {
        last_ui_telemetry_ms = now_ms;
        UpdateTelemetryUI();
    }
}

void OpenRGBZotac5070TiPlugin::UpdateTelemetryUI()
{
    if(telemetry_label == nullptr)
    {
        return;
    }
    const double fps     = static_cast<double>(measured_fps_x10.load()) / 10.0;
    const double writeMs = static_cast<double>(measured_write_us.load()) / 1000.0;
    telemetry_label->setText(
        QString("Live Engine: %1 Hz  |  I2C Write: %2 ms (%3)  |  Frames: %4  |  Smooth Window: %5 ms")
            .arg(fps, 0, 'f', 1)
            .arg(writeMs, 0, 'f', 2)
            .arg(SpeedKhzLabel(active_speed_khz))
            .arg(static_cast<qulonglong>(frames_sent.load()))
            .arg(smooth_window_ms.load())
    );
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

    bool          changed_found        = false;
    unsigned char changed_r            = last_r;
    unsigned char changed_g            = last_g;
    unsigned char changed_b            = last_b;
    bool          initial_lit_found    = false;
    unsigned char init_r               = last_r;
    unsigned char init_g               = last_g;
    unsigned char init_b               = last_b;
    bool          had_tracked_before   = !tracked_ctrl_colors.empty();
    bool          other_hw_cycle_found = false;

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
                changed_r     = cr;
                changed_g     = cg;
                changed_b     = cb;
                changed_found = true;
            }
        }
        else if(!had_tracked_before && !initial_lit_found && packed != 0)
        {
            init_r            = cr;
            init_g            = cg;
            init_b            = cb;
            initial_lit_found = true;
        }
    }

    tracked_ctrl_colors = std::move(next_map);

    if(changed_found)
    {
        mirrored_cycle_active.store(false);
        SubmitTargetState(ZOTAC_5070TI_MODE_STATIC, changed_r, changed_g, changed_b, 50, true, false);
        return;
    }

    if(other_hw_cycle_found)
    {
        if(!mirrored_cycle_active.exchange(true))
        {
            worker_cv.notify_one();
        }
        return;
    }

    mirrored_cycle_active.store(false);

    if(!has_applied_color && initial_lit_found)
    {
        SubmitTargetState(ZOTAC_5070TI_MODE_STATIC, init_r, init_g, init_b, 50, true, false);
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

    std::string scan_note  = ScanNVAPIBus();
    const char* proto_name = (active_proto == ZOTAC_PROTO_4A)
        ? "Zotac 4A (0x5A)"
        : (active_proto == ZOTAC_PROTO_BLACKWELL ? "Blackwell (0x20-0x2F)" : "SPECTRA V2");

    uint32_t write_us = 0;
    if(WriteHardwareNVAPI(ZOTAC_5070TI_MODE_STATIC, last_r, last_g, last_b, 50, true, write_us))
    {
        measured_write_us.store(write_us);
        detected = true;
        RegisterVirtualGPUDevice();
        char ok_buf[384] = {};
        std::snprintf(
            ok_buf,
            sizeof(ok_buf),
            "Smooth Engine v2.0 active on 0x%02X (%s, %s, m%u) — registered in OpenRGB Devices & Effects. %s",
            active_i2c_addr,
            proto_name,
            SpeedKhzLabel(active_speed_khz),
            active_display_mask,
            scan_note.c_str()
        );
        status_line = ok_buf;
    }
    else
    {
        char err_line[360] = {};
        std::snprintf(
            err_line,
            sizeof(err_line),
            "NVAPI write failed on 0x%02X (%s, rc=%d). %s",
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

    QLabel* title     = new QLabel("ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC — Smooth Engine v2.0");
    QLabel* pci_label = new QLabel("Target PCI: 0x10DE:0x2C05 | Subsys: 0x19DA:0x2765 | Dedicated 60 Hz NVAPI Thread");

    state_label = new QLabel(QString::fromStdString(status_line));
    state_label->setWordWrap(true);

    telemetry_label = new QLabel("Live Engine: initializing...");
    telemetry_label->setWordWrap(true);

    QObject::connect(widget, &QObject::destroyed, [this]() {
        widget          = nullptr;
        state_label     = nullptr;
        telemetry_label = nullptr;
    });

    // Hardware & Protocol Row
    QHBoxLayout* cfg_row = new QHBoxLayout();
    QLabel* addr_lbl     = new QLabel("I2C Addr:");
    QComboBox* addr_box  = new QComboBox();
    const uint8_t addr_choices[] = { 0x4A, 0x4B, 0x49, 0x48, 0x4C, 0x4E, 0x68, 0x6A, 0x29 };
    for(uint8_t a : addr_choices)
    {
        addr_box->addItem(QString("0x%1").arg(a, 2, 16, QChar('0')).toUpper(), static_cast<int>(a));
    }
    if(int idx = addr_box->findData(static_cast<int>(active_i2c_addr)); idx >= 0) addr_box->setCurrentIndex(idx);

    QLabel* proto_lbl    = new QLabel("Protocol:");
    QComboBox* proto_box = new QComboBox();
    proto_box->addItem("Zotac 4A Block (0x5A · SOLID)", ZOTAC_PROTO_4A);
    proto_box->addItem("Blackwell MCU (0x20-0x2F)", ZOTAC_PROTO_BLACKWELL);
    proto_box->addItem("SPECTRA V2 (0x0F)", ZOTAC_PROTO_V2);
    if(int idx = proto_box->findData(active_proto); idx >= 0) proto_box->setCurrentIndex(idx);

    QLabel* spd_lbl    = new QLabel("I2C Clock:");
    QComboBox* spd_box = new QComboBox();
    spd_box->addItem("Auto (100 -> 40 -> 10 kHz)", 0);
    spd_box->addItem("100 kHz (Fastest)", 4);
    spd_box->addItem("40 kHz (Balanced)", 3);
    spd_box->addItem("10 kHz (Legacy Safe)", 2);
    if(int idx = spd_box->findData(i2c_speed_pref.load()); idx >= 0) spd_box->setCurrentIndex(idx);

    auto sync_combos = [this, addr_box, proto_box]() {
        QSignalBlocker b1(addr_box);
        QSignalBlocker b2(proto_box);
        if(int aidx = addr_box->findData(static_cast<int>(active_i2c_addr)); aidx >= 0) addr_box->setCurrentIndex(aidx);
        if(int pidx = proto_box->findData(active_proto); pidx >= 0) proto_box->setCurrentIndex(pidx);
    };

    QObject::connect(addr_box, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, addr_box](int idx) {
        if(idx >= 0)
        {
            active_i2c_addr = static_cast<uint8_t>(addr_box->itemData(idx).toInt());
            SubmitTargetState(ZOTAC_5070TI_MODE_STATIC, last_r, last_g, last_b, 50, true, true);
        }
    });

    QObject::connect(proto_box, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, proto_box](int idx) {
        if(idx >= 0)
        {
            active_proto = proto_box->itemData(idx).toInt();
            bw_shadow_valid[0] = false;
            bw_shadow_valid[1] = false;
            SubmitTargetState(ZOTAC_5070TI_MODE_STATIC, last_r, last_g, last_b, 50, true, true);
        }
    });

    QObject::connect(spd_box, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, spd_box](int idx) {
        if(idx >= 0)
        {
            const int pref = spd_box->itemData(idx).toInt();
            i2c_speed_pref.store(pref);
            if(pref == 0)
            {
                std::lock_guard<std::mutex> lock(nvapi_mutex);
                NegotiateFastestBusSpeed();
            }
            else
            {
                active_speed_khz = static_cast<uint32_t>(pref);
            }
            UpdateTelemetryUI();
        }
    });

    cfg_row->addWidget(addr_lbl);
    cfg_row->addWidget(addr_box);
    cfg_row->addWidget(proto_lbl);
    cfg_row->addWidget(proto_box);
    cfg_row->addWidget(spd_lbl);
    cfg_row->addWidget(spd_box);
    cfg_row->addStretch();

    // Smooth Engine Sliders
    QGridLayout* smooth_grid = new QGridLayout();

    QLabel* pace_title = new QLabel("Target Frame Interval:");
    QSlider* pace_slider = new QSlider(Qt::Horizontal);
    pace_slider->setRange(16, 180);
    pace_slider->setValue(static_cast<int>(target_frame_ms.load()));
    QLabel* pace_val = new QLabel(QString("%1 ms (%2 Hz max)").arg(target_frame_ms.load()).arg(1000 / std::max(1u, target_frame_ms.load())));

    QObject::connect(pace_slider, &QSlider::valueChanged, [this, pace_val](int val) {
        target_frame_ms.store(static_cast<uint32_t>(val));
        pace_val->setText(QString("%1 ms (%2 Hz max)").arg(val).arg(1000 / std::max(1, val)));
        worker_cv.notify_one();
    });

    QLabel* glide_title = new QLabel("Color Glide Smoothing:");
    QSlider* glide_slider = new QSlider(Qt::Horizontal);
    glide_slider->setRange(0, 300);
    glide_slider->setValue(static_cast<int>(smooth_window_ms.load()));
    QLabel* glide_val = new QLabel(QString("%1 ms").arg(smooth_window_ms.load()));

    QObject::connect(glide_slider, &QSlider::valueChanged, [this, glide_val](int val) {
        smooth_window_ms.store(static_cast<uint32_t>(val));
        glide_val->setText(QString("%1 ms").arg(val));
        worker_cv.notify_one();
    });

    smooth_grid->addWidget(pace_title, 0, 0);
    smooth_grid->addWidget(pace_slider, 0, 1);
    smooth_grid->addWidget(pace_val, 0, 2);
    smooth_grid->addWidget(glide_title, 1, 0);
    smooth_grid->addWidget(glide_slider, 1, 1);
    smooth_grid->addWidget(glide_val, 1, 2);

    // Checkboxes
    QCheckBox* mirror_chk = new QCheckBox("Mirror color from active OpenRGB devices");
    mirror_chk->setChecked(mirror_active);
    QObject::connect(mirror_chk, &QCheckBox::toggled, [this](bool checked) {
        mirror_active = checked;
        if(checked)
        {
            sw_cycle_active.store(false);
            virtual_mode_locked.store(false);
            last_virtual_update_ms.store(0);
            tracked_ctrl_colors.clear();
            has_applied_color = false;
            SyncFromOpenRGBDevices();
            if(state_label) state_label->setText("Live mirroring enabled: smooth interpolation active.");
        }
        else
        {
            mirrored_cycle_active.store(false);
        }
    });

    QCheckBox* gamma_chk = new QCheckBox("Perceptual Gamma 2.2 Linear-Light Interpolation");
    gamma_chk->setChecked(gamma_correction.load());
    QObject::connect(gamma_chk, &QCheckBox::toggled, [this](bool checked) {
        gamma_correction.store(checked);
    });

    QCheckBox* hw_native_chk = new QCheckBox("Use GPU MCU Hardware Cycle instead of 60 Hz Software Cycle");
    hw_native_chk->setChecked(prefer_hw_native_cycle.load());
    QObject::connect(hw_native_chk, &QCheckBox::toggled, [this](bool checked) {
        prefer_hw_native_cycle.store(checked);
        HandleVirtualUpdate();
    });

    // Action Buttons
    QHBoxLayout* btn_row    = new QHBoxLayout();
    QPushButton* pick_btn   = new QPushButton("Pick GPU Color...");
    QPushButton* rescan_btn = new QPushButton("Re-Scan / Sync Now");
    QPushButton* bus_btn    = new QPushButton("Scan GPU I2C Bus");
    QPushButton* bench_btn  = new QPushButton("Benchmark I2C Speed");

    QObject::connect(pick_btn, &QPushButton::clicked, [this]() {
        QColor initial(last_r, last_g, last_b);
        QColor chosen = QColorDialog::getColor(initial, widget, "Choose ZOTAC RTX 5070 Ti Color");
        if(chosen.isValid())
        {
            sw_cycle_active.store(false);
            mirrored_cycle_active.store(false);
            virtual_mode_locked.store(false);
            last_virtual_update_ms.store(GetNowMs());
            SubmitTargetState(
                ZOTAC_5070TI_MODE_STATIC,
                static_cast<unsigned char>(chosen.red()),
                static_cast<unsigned char>(chosen.green()),
                static_cast<unsigned char>(chosen.blue()),
                50,
                true,
                false
            );
            if(state_label)
            {
                state_label->setText(QString("Transitioning smoothly to RGB(%1, %2, %3)...")
                    .arg(chosen.red()).arg(chosen.green()).arg(chosen.blue()));
            }
        }
    });

    QObject::connect(rescan_btn, &QPushButton::clicked, [this, sync_combos]() {
        ScanAndRegisterZotacGPU();
        sync_combos();
        if(mirror_active)
        {
            virtual_mode_locked.store(false);
            last_virtual_update_ms.store(0);
            tracked_ctrl_colors.clear();
            SyncFromOpenRGBDevices();
        }
        if(state_label) state_label->setText(QString::fromStdString(status_line));
    });

    QObject::connect(bus_btn, &QPushButton::clicked, [this, sync_combos]() {
        std::string scan_res = ScanNVAPIBus();
        sync_combos();
        if(state_label) state_label->setText(QString::fromStdString(scan_res));
    });

    QObject::connect(bench_btn, &QPushButton::clicked, [this]() {
        std::string bench_res = BenchmarkBusSpeed();
        if(state_label) state_label->setText(QString::fromStdString(bench_res));
        UpdateTelemetryUI();
    });

    btn_row->addWidget(pick_btn);
    btn_row->addWidget(rescan_btn);
    btn_row->addWidget(bus_btn);
    btn_row->addWidget(bench_btn);

    layout->addWidget(title);
    layout->addWidget(pci_label);
    layout->addLayout(cfg_row);
    layout->addLayout(smooth_grid);
    layout->addWidget(telemetry_label);
    layout->addWidget(state_label);
    layout->addWidget(mirror_chk);
    layout->addWidget(gamma_chk);
    layout->addWidget(hw_native_chk);
    layout->addLayout(btn_row);
    layout->addStretch();

    UpdateTelemetryUI();
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

    StopWorkerThread();

    if(api != nullptr && virt_ctrl != nullptr)
    {
        virt_ctrl->ClearCallbacks();
        api->UnregisterVirtualRGBController(virt_ctrl);
        api->DeleteVirtualRGBController(virt_ctrl);
        virt_ctrl = nullptr;
    }
    api = nullptr;
}

void OpenRGBZotac5070TiPlugin::OnProfileAboutToLoad() {}

void OpenRGBZotac5070TiPlugin::OnProfileLoad(nlohmann::json profile)
{
    if(profile.is_object())
    {
        if(profile.contains("frame_ms") && profile["frame_ms"].is_number_unsigned())
            target_frame_ms.store(std::clamp(profile["frame_ms"].get<uint32_t>(), 8u, 180u));
        if(profile.contains("smooth_ms") && profile["smooth_ms"].is_number_unsigned())
            smooth_window_ms.store(std::clamp(profile["smooth_ms"].get<uint32_t>(), 0u, 500u));
        if(profile.contains("speed_pref") && profile["speed_pref"].is_number_integer())
            i2c_speed_pref.store(profile["speed_pref"].get<int>());
        if(profile.contains("gamma") && profile["gamma"].is_boolean())
            gamma_correction.store(profile["gamma"].get<bool>());
        if(profile.contains("hw_native_cycle") && profile["hw_native_cycle"].is_boolean())
            prefer_hw_native_cycle.store(profile["hw_native_cycle"].get<bool>());
        if(profile.contains("mirror") && profile["mirror"].is_boolean())
            mirror_active = profile["mirror"].get<bool>();

        if(profile.contains("r") && profile.contains("g") && profile.contains("b"))
        {
            SubmitTargetState(
                ZOTAC_5070TI_MODE_STATIC,
                static_cast<unsigned char>(profile["r"].get<int>()),
                static_cast<unsigned char>(profile["g"].get<int>()),
                static_cast<unsigned char>(profile["b"].get<int>()),
                50,
                true,
                false
            );
        }
    }
}

nlohmann::json OpenRGBZotac5070TiPlugin::OnProfileSave()
{
    return nlohmann::json{
        {"r", static_cast<int>(last_r)},
        {"g", static_cast<int>(last_g)},
        {"b", static_cast<int>(last_b)},
        {"mirror", mirror_active},
        {"frame_ms", target_frame_ms.load()},
        {"smooth_ms", smooth_window_ms.load()},
        {"speed_pref", i2c_speed_pref.load()},
        {"gamma", gamma_correction.load()},
        {"hw_native_cycle", prefer_hw_native_cycle.load()}
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

void OpenRGBZotac5070TiPlugin::SettingsManagerUpdated(unsigned int /*event_type*/) {}

LogManager* LogManager::get()
{
    alignas(LogManager) static unsigned char stub_storage[sizeof(LogManager)] = {};
    return reinterpret_cast<LogManager*>(stub_storage);
}

void LogManager::LogEntry(const char* /*filename*/, int /*line*/, unsigned int /*level*/, const char* /*fmt*/, ...) {}

const std::string StringUtils::remove_null_terminating_chars(std::string str)
{
    str.erase(std::remove(str.begin(), str.end(), '\0'), str.end());
    return str;
}

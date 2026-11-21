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

#include <algorithm>
#include <cstdint>
#include <cstdio>
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
#define ZOTAC_SPECTRA_I2C_ADDR      0x4B
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
                        out_status = "Matched PCI ID on I2C bus, but address 0x4B did not ACK (close ZOTAC FireStorm).";
                        continue;
                    }
                }

                Zotac5070TiController* controller = new Zotac5070TiController(bus, ZOTAC_SPECTRA_I2C_ADDR);
                RGBController_Zotac5070Ti* rgb_controller = new RGBController_Zotac5070Ti(controller);
                rgb_controller->name = "ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC";

                mgr->RegisterRGBController(rgb_controller);
                out_status = "Registered ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC on native OpenRGB I2C address 0x4B.";
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
        using NvI2CWriteExFn   = int (__cdecl*)(void*, NvI2CInfoV3*, uint32_t*);
        using NvI2CReadExFn    = int (__cdecl*)(void*, NvI2CInfoV3*, uint32_t*);
        using NvGetErrMsgFn    = int (__cdecl*)(int, char*);

        HMODULE        mod       = nullptr;
        bool           init_ok   = false;
        void*          gpu       = nullptr;
        uint32_t       pci_dev   = 0;
        uint32_t       pci_sub   = 0;
        int            init_rc   = 0;
        std::string    err_note;

        NvInitFn       init_fn   = nullptr;
        NvEnumGpusFn   enum_fn   = nullptr;
        NvGetPciFn     pci_fn    = nullptr;
        NvI2CWriteExFn write_fn  = nullptr;
        NvI2CReadExFn  read_fn   = nullptr;
        NvGetErrMsgFn  err_fn    = nullptr;

        static NvapiSession& Get()
        {
            static NvapiSession instance;
            return instance;
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

            init_fn  = reinterpret_cast<NvInitFn>(query(0x0150E828u));
            enum_fn  = reinterpret_cast<NvEnumGpusFn>(query(0xE5AC921Fu));
            pci_fn   = reinterpret_cast<NvGetPciFn>(query(0x2DDFB66Eu));
            write_fn = reinterpret_cast<NvI2CWriteExFn>(query(0x283AC65Au));
            read_fn  = reinterpret_cast<NvI2CReadExFn>(query(0x4D7B0709u));
            err_fn   = reinterpret_cast<NvGetErrMsgFn>(query(0x6C2D048Cu));

            if(init_fn == nullptr || enum_fn == nullptr || write_fn == nullptr || read_fn == nullptr)
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

            init_ok = true;
            err_note.clear();
            return true;
        }

        int ReadReg(uint8_t addr, uint8_t reg, bool use_reg, uint8_t* out_buf, uint32_t len, uint8_t port_id, uint32_t port_set, uint32_t speed_khz, uint8_t is_ddc = 0)
        {
            if(!EnsureReady()) return -1;
            uint8_t reg_local = reg;
            NvI2CInfoV3 info = {};
            info.version         = static_cast<uint32_t>(sizeof(NvI2CInfoV3) | (3u << 16));
            info.displayMask     = 0;
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

            uint32_t unknown = 0;
            return read_fn(gpu, &info, &unknown);
        }

        int WriteReg(uint8_t addr, uint8_t reg, bool use_reg, uint8_t* data_buf, uint32_t len, uint8_t port_id, uint32_t port_set, uint32_t speed_khz)
        {
            if(!EnsureReady()) return -1;
            uint8_t reg_local = reg;
            NvI2CInfoV3 info = {};
            info.version         = static_cast<uint32_t>(sizeof(NvI2CInfoV3) | (3u << 16));
            info.displayMask     = 0;
            info.bIsDDCPort      = 0;
            info.i2cDevAddress   = static_cast<uint8_t>(addr << 1);
            info.pbI2cRegAddress = use_reg ? &reg_local : nullptr;
            info.regAddrSize     = use_reg ? 1u : 0u;
            info.pbData          = data_buf;
            info.cbSize          = len;
            info.i2cSpeed        = 0xFFFFu;
            info.i2cSpeedKhz     = speed_khz;
            info.portId          = port_id;
            info.bIsPortIdSet    = port_set;

            uint32_t unknown = 0;
            return write_fn(gpu, &info, &unknown);
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
}

bool OpenRGBZotac5070TiPlugin::WriteColorNVAPI(unsigned char r, unsigned char g, unsigned char b)
{
#ifdef _WIN32
    NvapiSession& nv = NvapiSession::Get();
    if(!nv.EnsureReady())
    {
        last_nvapi_status = nv.init_rc != 0 ? nv.init_rc : -1;
        last_nvapi_error  = nv.err_note;
        return false;
    }

    auto write_blackwell = [&](uint8_t addr, uint8_t port_id, uint32_t port_set, uint32_t speed_khz) -> int
    {
        for(uint8_t z = 0; z < ZOTAC_ZONE_COUNT; z++)
        {
            uint8_t regs[16] = {
                0x00,                   // 0x20 - Fixed = 0x00
                z,                      // 0x21 - Zone index
                0x01,                   // 0x22 - Mode = ZOTAC_BLACKWELL_GPU_MODE_STATIC (0x01)
                r, g, b,                // 0x23..0x25 - Color 1
                0x00, 0x00, 0x00,       // 0x26..0x28 - Color 2
                100,                    // 0x29 - Brightness (0-100)
                20,                     // 0x2A - Speed (0-100)
                0x00,                   // 0x2B - Direction Left
                0x00, 0x00, 0x00, 0x00  // 0x2C..0x2F - Reserved
            };
            for(int i = 0; i < 16; i++)
            {
                uint8_t val = regs[i];
                int rc = nv.WriteReg(addr, static_cast<uint8_t>(0x20 + i), true, &val, 1u, port_id, port_set, speed_khz);
                if(rc != 0)
                {
                    return rc;
                }
                Sleep(3);
            }
            uint8_t commit_val = 0x01;
            int crc = nv.WriteReg(addr, 0x17, true, &commit_val, 1u, port_id, port_set, speed_khz);
            if(crc != 0)
            {
                return crc;
            }
            Sleep(10);
        }
        return 0;
    };

    auto write_spectra_v2 = [&](uint8_t addr, uint8_t port_id, uint32_t port_set, uint32_t speed_khz, bool reg_split) -> int
    {
        for(uint8_t z = 0; z < ZOTAC_ZONE_COUNT; z++)
        {
            uint8_t v2_pkt[10] = { 0x0F, 0x00, z, 0x00, r, g, b, 0x05, 0xFF, 0x01 };
            int rc = nv.WriteReg(
                addr,
                v2_pkt[0],
                reg_split,
                reg_split ? (v2_pkt + 1) : v2_pkt,
                reg_split ? 9u : 10u,
                port_id,
                port_set,
                speed_khz
            );
            if(rc != 0)
            {
                return rc;
            }
        }
        return 0;
    };

    auto try_write = [&](uint8_t addr, bool blackwell, uint8_t port_id, uint32_t port_set, uint32_t speed_khz, bool reg_split) -> int
    {
        return blackwell
            ? write_blackwell(addr, port_id, port_set, speed_khz)
            : write_spectra_v2(addr, port_id, port_set, speed_khz, reg_split);
    };

    int rc = try_write(active_i2c_addr, use_blackwell_proto, active_port_id, active_port_set, active_speed_khz, active_reg_split);
    if(rc == 0)
    {
        last_nvapi_status = 0;
        last_nvapi_error.clear();
        has_applied_color = true;
        return true;
    }

    int first_err = rc;

    // Probe OpenRGB ZotacBlackwell detect signature (0x4B reg 0x10) across common ports & speeds before heavy writes
    struct PortSpeed { uint8_t port; uint32_t set; uint32_t speed; };
    const PortSpeed probes[] = {
        { 1, 1, 0u }, // Exact OpenRGB i2c_smbus_nvapi setting
        { 1, 1, 4u },
        { 0, 1, 0u },
        { 2, 1, 0u },
        { 3, 1, 0u },
        { 0, 0, 0u },
        { 0, 1, 4u },
        { 2, 1, 4u },
        { 0, 0, 4u },
    };

    for(const auto& ps : probes)
    {
        uint8_t probe_val = 0;
        if(nv.ReadReg(active_i2c_addr, 0x10, true, &probe_val, 1u, ps.port, ps.set, ps.speed) == 0)
        {
            int wrc = try_write(active_i2c_addr, use_blackwell_proto, ps.port, ps.set, ps.speed, true);
            if(wrc == 0)
            {
                active_port_id    = ps.port;
                active_port_set   = ps.set;
                active_speed_khz  = ps.speed;
                active_reg_split  = true;
                last_nvapi_status = 0;
                last_nvapi_error.clear();
                has_applied_color = true;
                return true;
            }
        }
    }

    if(!use_blackwell_proto)
    {
        for(const auto& ps : probes)
        {
            for(bool split : { true, false })
            {
                int wrc = write_spectra_v2(active_i2c_addr, ps.port, ps.set, ps.speed, split);
                if(wrc == 0)
                {
                    active_port_id    = ps.port;
                    active_port_set   = ps.set;
                    active_speed_khz  = ps.speed;
                    active_reg_split  = split;
                    last_nvapi_status = 0;
                    last_nvapi_error.clear();
                    has_applied_color = true;
                    return true;
                }
            }
        }
    }

    last_nvapi_status = first_err;
    last_nvapi_error  = nv.FormatError(first_err);
    return false;
#else
    (void)r; (void)g; (void)b;
    return false;
#endif
}

std::string OpenRGBZotac5070TiPlugin::ScanNVAPIBus()
{
#ifdef _WIN32
    NvapiSession& nv = NvapiSession::Get();
    if(!nv.EnsureReady())
    {
        return nv.err_note.empty() ? "NVAPI initialization failed." : nv.err_note;
    }

    std::string found;
    int last_rc = -1;
    bool adopted = false;

    auto record_hit = [&](const char* label, uint8_t port, uint32_t set, uint32_t speed, uint8_t addr, uint8_t reg, uint8_t val, bool is_bw)
    {
        char hit[72] = {};
        std::snprintf(hit, sizeof(hit), "%s@0x%02X[r%02X]=0x%02X(spd%u) ", label, addr, reg, val, speed);
        found += hit;

        if(!adopted && (addr == 0x4B || addr == ZOTAC_SPECTRA_I2C_ADDR || (addr >= 0x48 && addr <= 0x4F)))
        {
            active_i2c_addr     = addr;
            active_port_id      = port;
            active_port_set     = set;
            active_speed_khz    = speed;
            use_blackwell_proto = is_bw || (addr == 0x4B);
            adopted             = true;

            if(addr == 0x4B)
            {
                uint8_t fw[33] = {};
                if(nv.ReadReg(0x4B, 0xF0, true, fw, 32u, port, set, speed) == 0)
                {
                    std::string clean;
                    for(int i = 0; i < 32 && fw[i] != 0x00 && fw[i] != 0xFF; i++)
                    {
                        if(fw[i] >= 0x20 && fw[i] <= 0x7E) clean.push_back(static_cast<char>(fw[i]));
                    }
                    if(!clean.empty())
                    {
                        detected_fw_version = clean;
                    }
                }
            }
        }
    };

    struct PortScan { uint8_t port; uint32_t set; const char* label; };
    const PortScan ports[] = {
        { 1, 1, "p1" }, // OpenRGB i2c_smbus_nvapi default
        { 0, 1, "p0" },
        { 2, 1, "p2" },
        { 3, 1, "p3" },
        { 0, 0, "auto" },
    };
    const uint32_t speeds[] = { 0u, 4u }; // DEFAULT (0) then 100KHZ (4)
    const uint8_t  addrs[]  = { 0x4B, 0x49, 0x4A, 0x48, 0x4C, 0x4D, 0x4E, 0x4F, 0x20, 0x29, 0x60, 0x68, 0x6A, 0x70 };
    const uint8_t  regs[]   = { 0x10, 0xF0, 0x00 };

    for(uint32_t spd : speeds)
    {
        for(const auto& p : ports)
        {
            for(uint8_t addr : addrs)
            {
                for(uint8_t reg : regs)
                {
                    uint8_t val = 0;
                    int rc = nv.ReadReg(addr, reg, true, &val, 1u, p.port, p.set, spd);
                    last_rc = rc;
                    if(rc == 0)
                    {
                        record_hit(p.label, p.port, p.set, spd, addr, reg, val, (addr == 0x4B || reg == 0x10 || reg == 0xF0));
                        break;
                    }
                }
            }
        }
        if(!found.empty()) break;
    }

    // If nothing matched in quick ZOTAC list, sweep full 7-bit range 0x08..0x77 on port 1 (DEFAULT speed) + check EDID 0x50
    if(found.empty())
    {
        for(uint8_t addr = 0x08; addr <= 0x77; addr++)
        {
            uint8_t val = 0;
            int rc = nv.ReadReg(addr, 0x10, true, &val, 1u, 1, 1, 0u);
            if(rc != 0) rc = nv.ReadReg(addr, 0x00, true, &val, 1u, 1, 1, 0u);
            if(rc != 0) rc = nv.ReadReg(addr, 0x00, false, &val, 1u, 1, 1, 0u);
            last_rc = rc;
            if(rc == 0)
            {
                record_hit("sweep-p1", 1, 1, 0u, addr, 0x00, val, addr == 0x4B);
            }
        }
    }

    char header[160] = {};
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

    uint8_t edid_b = 0;
    int ddc_rc = nv.ReadReg(0x50, 0x00, true, &edid_b, 1u, 1, 1, 0u, 1);
    char fail_buf[160] = {};
    std::snprintf(
        fail_buf,
        sizeof(fail_buf),
        "No ACK on 0x4B or 0x08-0x77 (last rc=%d, DDC@0x50 rc=%d). Exit FireStorm/overlays & run OpenRGB as Admin.",
        last_rc,
        ddc_rc
    );
    return std::string(header) + fail_buf;
#else
    return "NVAPI scan is Windows-only.";
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
        if(has_applied_color && r == last_r && g == last_g && b == last_b)
        {
            return;
        }
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

    std::string scan_note = ScanNVAPIBus();
    if(WriteColorNVAPI(last_r, last_g, last_b))
    {
        detected = true;
        char ok_buf[320] = {};
        std::snprintf(
            ok_buf,
            sizeof(ok_buf),
            "NVAPI Bridge active on 0x%02X (%s, port %u, spd %u). %s",
            active_i2c_addr,
            use_blackwell_proto ? "Blackwell 0x20-0x2F" : "SPECTRA V2",
            active_port_id,
            active_speed_khz,
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

    QLabel* title = new QLabel("ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC (OpenRGB 1.1 Bridge)");
    QLabel* pci_label = new QLabel("Target PCI: 0x10DE:0x2C05 | Subsys: 0x19DA:0x2765");
    QLabel* state_label = new QLabel(QString::fromStdString(status_line));
    state_label->setWordWrap(true);

    QHBoxLayout* cfg_row = new QHBoxLayout();
    QLabel* addr_lbl = new QLabel("I2C Addr:");
    QComboBox* addr_box = new QComboBox();
    const uint8_t addr_choices[] = { 0x4B, 0x49, 0x4A, 0x48, 0x4C, 0x4E, 0x68, 0x6A, 0x29 };
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
    proto_box->addItem("Blackwell MCU (0x20-0x2F)", true);
    proto_box->addItem("SPECTRA V2 (0x0F)", false);
    proto_box->setCurrentIndex(use_blackwell_proto ? 0 : 1);

    QObject::connect(addr_box, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, addr_box, state_label](int idx) {
        if(idx >= 0)
        {
            active_i2c_addr = static_cast<uint8_t>(addr_box->itemData(idx).toInt());
            if(WriteColorNVAPI(last_r, last_g, last_b))
            {
                state_label->setText(QString("Switched to I2C 0x%1 (port %2) — write ACKed.")
                    .arg(active_i2c_addr, 2, 16, QChar('0')).toUpper().arg(active_port_id));
            }
            else
            {
                state_label->setText(QString("Switched to I2C 0x%1 — write returned %2 (rc=%3).")
                    .arg(active_i2c_addr, 2, 16, QChar('0')).toUpper()
                    .arg(QString::fromStdString(last_nvapi_error)).arg(last_nvapi_status));
            }
        }
    });

    QObject::connect(proto_box, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, proto_box](int idx) {
        if(idx >= 0)
        {
            use_blackwell_proto = proto_box->itemData(idx).toBool();
            WriteColorNVAPI(last_r, last_g, last_b);
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
            SyncFromOpenRGBDevices();
            state_label->setText("Mirroring enabled: synced from active OpenRGB controller.");
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
            last_r = static_cast<unsigned char>(chosen.red());
            last_g = static_cast<unsigned char>(chosen.green());
            last_b = static_cast<unsigned char>(chosen.blue());
            if(WriteColorNVAPI(last_r, last_g, last_b))
            {
                state_label->setText(QString("Applied RGB(%1, %2, %3) via NVAPI I2C 0x%4 (port %5).")
                    .arg(last_r).arg(last_g).arg(last_b)
                    .arg(active_i2c_addr, 2, 16, QChar('0')).toUpper().arg(active_port_id));
            }
            else
            {
                state_label->setText(QString("Write failed on 0x%1 (%2, rc=%3).")
                    .arg(active_i2c_addr, 2, 16, QChar('0')).toUpper()
                    .arg(QString::fromStdString(last_nvapi_error)).arg(last_nvapi_status));
            }
        }
    });

    QObject::connect(rescan_btn, &QPushButton::clicked, [this, state_label, addr_box, proto_box]() {
        ScanAndRegisterZotacGPU();
        int idx = addr_box->findData(static_cast<int>(active_i2c_addr));
        if(idx >= 0) addr_box->setCurrentIndex(idx);
        proto_box->setCurrentIndex(use_blackwell_proto ? 0 : 1);
        if(mirror_active)
        {
            SyncFromOpenRGBDevices();
        }
        state_label->setText(QString::fromStdString(status_line));
    });

    QObject::connect(bus_scan_btn, &QPushButton::clicked, [this, state_label, addr_box, proto_box]() {
        std::string scan_res = ScanNVAPIBus();
        int idx = addr_box->findData(static_cast<int>(active_i2c_addr));
        if(idx >= 0) addr_box->setCurrentIndex(idx);
        proto_box->setCurrentIndex(use_blackwell_proto ? 0 : 1);
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

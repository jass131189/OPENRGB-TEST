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
        last_nvapi_error = "Failed to load nvapi64.dll";
        return false;
    }

    using QueryInterfaceFn = void* (__cdecl*)(uint32_t);
    using NvInitFn         = int (__cdecl*)();
    using NvEnumGpusFn     = int (__cdecl*)(void**, uint32_t*);
    using NvGetPciFn       = int (__cdecl*)(void*, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
    using NvI2CWriteExFn   = int (__cdecl*)(void*, NvI2CInfoV3*, uint32_t*);
    using NvGetErrMsgFn    = int (__cdecl*)(int, char*);

    auto query = reinterpret_cast<QueryInterfaceFn>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    if(query == nullptr)
    {
        last_nvapi_error = "nvapi_QueryInterface not found";
        FreeLibrary(nvapi);
        return false;
    }

    auto init_fn   = reinterpret_cast<NvInitFn>(query(0x0150E828u));
    auto enum_fn   = reinterpret_cast<NvEnumGpusFn>(query(0xE5AC921Fu));
    auto pci_fn    = reinterpret_cast<NvGetPciFn>(query(0x2DDFB66Eu));
    auto write_fn  = reinterpret_cast<NvI2CWriteExFn>(query(0x283AC65Au));
    auto err_fn    = reinterpret_cast<NvGetErrMsgFn>(query(0x6C2D048Cu));

    if(init_fn == nullptr || enum_fn == nullptr || write_fn == nullptr)
    {
        last_nvapi_error = "Required NVAPI I2C entry points unavailable";
        FreeLibrary(nvapi);
        return false;
    }

    int init_rc = init_fn();
    if(init_rc != 0)
    {
        last_nvapi_status = init_rc;
        last_nvapi_error = "NvAPI_Initialize failed";
        FreeLibrary(nvapi);
        return false;
    }

    void*    handles[64] = {};
    uint32_t count       = 0;
    int enum_rc = enum_fn(handles, &count);
    if(enum_rc != 0 || count == 0)
    {
        last_nvapi_status = enum_rc;
        last_nvapi_error = "No NVIDIA physical GPU handles returned";
        FreeLibrary(nvapi);
        return false;
    }

    void* target_gpu = handles[0];
    if(pci_fn != nullptr)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            uint32_t dev_id = 0, sub_id = 0, rev_id = 0, ext_id = 0;
            if(pci_fn(handles[i], &dev_id, &sub_id, &rev_id, &ext_id) == 0)
            {
                uint32_t ven = dev_id & 0xFFFFu;
                uint32_t dev = (dev_id >> 16) & 0xFFFFu;
                if(ven == NVIDIA_VEN && dev == NVIDIA_RTX5070TI_DEV)
                {
                    target_gpu = handles[i];
                    break;
                }
            }
        }
    }

    auto try_write = [&](uint8_t port_id, uint32_t port_set, uint32_t speed_khz, bool reg_split, uint32_t ext_flag) -> int
    {
        int last_rc = -1;
        for(uint8_t z = 0; z < ZOTAC_ZONE_COUNT; z++)
        {
            uint8_t v2_pkt[10] = { 0x0F, 0x00, z, 0x00, r, g, b, 0x05, 0xFF, 0x01 };
            uint8_t bw_pkt[12] = { 0xA0, z, 0x00, 0x05, 100, r, g, b, 0x00, 0x00, 0x00, 0x55 };
            uint8_t* pkt_ptr   = use_blackwell_proto ? bw_pkt : v2_pkt;
            uint32_t pkt_len   = use_blackwell_proto ? 12u : 10u;

            uint8_t reg_byte = pkt_ptr[0];
            NvI2CInfoV3 info {};
            info.version         = static_cast<uint32_t>(sizeof(NvI2CInfoV3) | (3u << 16));
            info.displayMask     = 0;
            info.bIsDDCPort      = 0;
            info.i2cDevAddress   = static_cast<uint8_t>(active_i2c_addr << 1);
            info.pbI2cRegAddress = reg_split ? &reg_byte : nullptr;
            info.regAddrSize     = reg_split ? 1u : 0u;
            info.pbData          = reg_split ? (pkt_ptr + 1) : pkt_ptr;
            info.cbSize          = reg_split ? (pkt_len - 1u) : pkt_len;
            info.i2cSpeed        = 0xFFFFu;
            info.i2cSpeedKhz     = speed_khz; // NVAPI_I2C_SPEED enum: 4 = 100kHz, 0 = default
            info.portId          = port_id;
            info.bIsPortIdSet    = port_set;

            uint32_t ext_data[2] = { ext_flag, 0 };
            int rc = write_fn(target_gpu, &info, ext_data);
            last_rc = rc;
            if(rc != 0)
            {
                return rc;
            }
        }
        return last_rc;
    };

    int rc = try_write(active_port_id, active_port_set, active_speed_khz, active_reg_split, 0);
    if(rc == 0)
    {
        last_nvapi_status = 0;
        last_nvapi_error.clear();
        FreeLibrary(nvapi);
        return true;
    }

    struct ProbeProfile { uint8_t port; uint32_t port_set; uint32_t speed; bool split; uint32_t ext; };
    const ProbeProfile profiles[] = {
        { 1, 1, 4u, true,  0u },
        { 1, 1, 4u, false, 0u },
        { 0, 1, 4u, true,  0u },
        { 0, 1, 4u, false, 0u },
        { 2, 1, 4u, true,  0u },
        { 2, 1, 4u, false, 0u },
        { 0, 0, 4u, true,  0u },
        { 0, 0, 4u, false, 0u },
        { 1, 1, 0u, true,  0u },
        { 0, 1, 0u, true,  0u },
        { 0, 0, 0u, true,  0u },
        { 1, 1, 4u, true,  1u },
        { 0, 1, 4u, true,  1u },
    };

    int first_err = rc;
    for(const auto& p : profiles)
    {
        int prc = try_write(p.port, p.port_set, p.speed, p.split, p.ext);
        if(prc == 0)
        {
            active_port_id   = p.port;
            active_port_set  = p.port_set;
            active_speed_khz = p.speed;
            active_reg_split = p.split;
            last_nvapi_status = 0;
            last_nvapi_error.clear();
            FreeLibrary(nvapi);
            return true;
        }
    }

    last_nvapi_status = first_err;
    char err_buf[64] = {};
    if(err_fn != nullptr && err_fn(first_err, err_buf) == 0 && err_buf[0] != '\0')
    {
        last_nvapi_error = std::string(err_buf);
    }
    else
    {
        char code_buf[48] = {};
        std::snprintf(code_buf, sizeof(code_buf), "NVAPI status %d", first_err);
        last_nvapi_error = std::string(code_buf);
    }

    FreeLibrary(nvapi);
    return false;
#else
    (void)r; (void)g; (void)b;
    return false;
#endif
}

std::string OpenRGBZotac5070TiPlugin::ScanNVAPIBus()
{
#ifdef _WIN32
    HMODULE nvapi = LoadLibraryA("nvapi64.dll");
    if(nvapi == nullptr)
    {
        return "Could not load nvapi64.dll.";
    }

    using QueryInterfaceFn = void* (__cdecl*)(uint32_t);
    using NvInitFn         = int (__cdecl*)();
    using NvEnumGpusFn     = int (__cdecl*)(void**, uint32_t*);
    using NvGetPciFn       = int (__cdecl*)(void*, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
    using NvI2CReadExFn    = int (__cdecl*)(void*, NvI2CInfoV3*, uint32_t*);

    auto query = reinterpret_cast<QueryInterfaceFn>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    if(query == nullptr)
    {
        FreeLibrary(nvapi);
        return "nvapi_QueryInterface unavailable.";
    }

    auto init_fn = reinterpret_cast<NvInitFn>(query(0x0150E828u));
    auto enum_fn = reinterpret_cast<NvEnumGpusFn>(query(0xE5AC921Fu));
    auto pci_fn  = reinterpret_cast<NvGetPciFn>(query(0x2DDFB66Eu));
    auto read_fn = reinterpret_cast<NvI2CReadExFn>(query(0x4D7B0709u));

    if(init_fn == nullptr || enum_fn == nullptr || read_fn == nullptr || init_fn() != 0)
    {
        FreeLibrary(nvapi);
        return "NvAPI_I2CReadEx initialization failed.";
    }

    void*    handles[64] = {};
    uint32_t count       = 0;
    if(enum_fn(handles, &count) != 0 || count == 0)
    {
        FreeLibrary(nvapi);
        return "No NVIDIA GPU handles found.";
    }

    void* target_gpu = handles[0];
    uint32_t matched_dev = 0, matched_sub = 0;
    if(pci_fn != nullptr)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            uint32_t dev_id = 0, sub_id = 0, rev_id = 0, ext_id = 0;
            if(pci_fn(handles[i], &dev_id, &sub_id, &rev_id, &ext_id) == 0)
            {
                if((dev_id & 0xFFFFu) == NVIDIA_VEN && ((dev_id >> 16) & 0xFFFFu) == NVIDIA_RTX5070TI_DEV)
                {
                    target_gpu = handles[i];
                    matched_dev = dev_id;
                    matched_sub = sub_id;
                    break;
                }
            }
        }
    }

    const uint8_t addrs[] = { 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x20, 0x29, 0x60, 0x68, 0x6A, 0x70 };
    struct PortScan { uint8_t port; uint32_t set; const char* label; };
    const PortScan ports[] = {
        { 1, 1, "port1" },
        { 0, 1, "port0" },
        { 2, 1, "port2" },
        { 0, 0, "auto" },
    };

    std::string found;
    int last_rc = 0;
    bool adopted = false;

    for(const auto& p : ports)
    {
        for(uint8_t addr : addrs)
        {
            uint8_t reg = 0x00;
            uint8_t val = 0x00;
            NvI2CInfoV3 info {};
            info.version         = static_cast<uint32_t>(sizeof(NvI2CInfoV3) | (3u << 16));
            info.displayMask     = 0;
            info.bIsDDCPort      = 0;
            info.i2cDevAddress   = static_cast<uint8_t>(addr << 1);
            info.pbI2cRegAddress = &reg;
            info.regAddrSize     = 1;
            info.pbData          = &val;
            info.cbSize          = 1;
            info.i2cSpeed        = 0xFFFFu;
            info.i2cSpeedKhz     = 4u;
            info.portId          = p.port;
            info.bIsPortIdSet    = p.set;

            uint32_t ext_data[2] = { 0, 0 };
            int rc = read_fn(target_gpu, &info, ext_data);
            if(rc != 0)
            {
                info.pbI2cRegAddress = nullptr;
                info.regAddrSize     = 0;
                rc = read_fn(target_gpu, &info, ext_data);
            }
            last_rc = rc;
            if(rc == 0)
            {
                char hit[48] = {};
                std::snprintf(hit, sizeof(hit), "%s@0x%02X(0x%02X) ", p.label, addr, val);
                found += hit;
                if(!adopted && (addr == ZOTAC_SPECTRA_I2C_ADDR || (addr >= 0x48 && addr <= 0x4B)))
                {
                    active_i2c_addr = addr;
                    active_port_id  = p.port;
                    active_port_set = p.set;
                    adopted = true;
                }
            }
        }
    }

    FreeLibrary(nvapi);

    char header[96] = {};
    std::snprintf(header, sizeof(header), "PCI=0x%08X Sub=0x%08X | ", matched_dev, matched_sub);
    if(!found.empty())
    {
        return std::string(header) + "ACKs: " + found;
    }

    char fail_buf[96] = {};
    std::snprintf(fail_buf, sizeof(fail_buf), "No ACK on RGB addresses (last NVAPI rc=%d). Exit FireStorm & run as Admin.", last_rc);
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
        char ok_buf[192] = {};
        std::snprintf(
            ok_buf,
            sizeof(ok_buf),
            "NVAPI Bridge active on 0x%02X (port %u, set=%u, split=%d) for ZOTAC GAMING GeForce RTX 5070 Ti SOLID OC.",
            active_i2c_addr,
            active_port_id,
            active_port_set,
            active_reg_split ? 1 : 0
        );
        status_line = ok_buf;
    }
    else
    {
        std::string scan_note = ScanNVAPIBus();
        if(detected || WriteColorNVAPI(last_r, last_g, last_b))
        {
            detected = true;
            char ok_buf[224] = {};
            std::snprintf(
                ok_buf,
                sizeof(ok_buf),
                "NVAPI Bridge locked onto 0x%02X (port %u). %s",
                active_i2c_addr,
                active_port_id,
                scan_note.c_str()
            );
            status_line = ok_buf;
        }
        else
        {
            char err_line[256] = {};
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
    const uint8_t addr_choices[] = { 0x49, 0x4A, 0x48, 0x4B, 0x4E, 0x68, 0x6A, 0x29 };
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
    proto_box->addItem("SPECTRA V2 (0x0F)", false);
    proto_box->addItem("Blackwell Block (0xA0)", true);
    proto_box->setCurrentIndex(use_blackwell_proto ? 1 : 0);

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

    QObject::connect(rescan_btn, &QPushButton::clicked, [this, state_label, addr_box]() {
        ScanAndRegisterZotacGPU();
        int idx = addr_box->findData(static_cast<int>(active_i2c_addr));
        if(idx >= 0) addr_box->setCurrentIndex(idx);
        if(mirror_active)
        {
            SyncFromOpenRGBDevices();
        }
        state_label->setText(QString::fromStdString(status_line));
    });

    QObject::connect(bus_scan_btn, &QPushButton::clicked, [this, state_label, addr_box]() {
        std::string scan_res = ScanNVAPIBus();
        int idx = addr_box->findData(static_cast<int>(active_i2c_addr));
        if(idx >= 0) addr_box->setCurrentIndex(idx);
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

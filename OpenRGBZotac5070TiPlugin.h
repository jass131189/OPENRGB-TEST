#pragma once

#include <QObject>
#include <QWidget>
#include <QMenu>
#include <string>
#include "OpenRGBPluginInterface.h"

class OpenRGBZotac5070TiPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID)
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
    void                SyncFromOpenRGBDevices();

    OpenRGBPluginAPIInterface* api = nullptr;
    QWidget*                   widget = nullptr;
    bool                       detected = false;
    bool                       mirror_active = true;
    unsigned char              last_r = 56;
    unsigned char              last_g = 189;
    unsigned char              last_b = 248;
    std::string                status_line;
};

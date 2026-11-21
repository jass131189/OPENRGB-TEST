#pragma once

#include <QObject>
#include <QWidget>
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
    void                Load(ResourceManagerInterface* resource_manager_ptr) override;
    QWidget*            GetWidget() override;
    QMenu*              GetTrayMenu() override;
    void                Unload() override;

private:
    void                ScanAndRegisterZotacGPU();

    ResourceManagerInterface* rm = nullptr;
    QWidget*                  widget = nullptr;
    bool                      detected = false;
    std::string               status_line;
};

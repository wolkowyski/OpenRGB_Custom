/*---------------------------------------------------------*\
| BLEDOMControllerDetect_Windows.cpp                        |
|                                                           |
|   Detector for ELK-BLEDOM Bluetooth LE LED strips         |
|                                                           |
|   Braian Wolkowyski                           21 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <cstdint>
#include "DetectionManager.h"
#include "BLEDOMController_Windows.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "RGBController_BLEDOM_Windows.h"
#include "SettingsManager.h"

/*---------------------------------------------------------*\
| Devices are configured in Manually Added Devices and      |
| stored in OpenRGB.json as:                                |
|   "BLEDOMDevices": { "devices": [                         |
|       { "name": "Desk strip", "mac": "FF:FF:10:E0:4D:84" }|
|   ] }                                                     |
\*---------------------------------------------------------*/
DetectedControllers DetectBLEDOMControllers()
{
    DetectedControllers detected_controllers;
    json                bledom_settings;

    bledom_settings = ResourceManager::get()->GetSettingsManager()->GetSettings("BLEDOMDevices");

    if(!bledom_settings.contains("devices"))
    {
        return(detected_controllers);
    }

    for(unsigned int device_idx = 0; device_idx < bledom_settings["devices"].size(); device_idx++)
    {
        json& device = bledom_settings["devices"][device_idx];

        if(!device.contains("mac") || !device["mac"].is_string())
        {
            continue;
        }

        std::string mac  = device["mac"];
        std::string name = "BLEDOM LED Strip";

        if(device.contains("name") && device["name"].is_string() && !device["name"].get<std::string>().empty())
        {
            name = device["name"];
        }

        uint64_t address;

        if(!BLEDOMController::ParseMAC(mac, address))
        {
            LOG_ERROR("[BLEDOM] Skipping \"%s\": invalid MAC address \"%s\"", name.c_str(), mac.c_str());
            continue;
        }

        BLEDOMController*     controller     = new BLEDOMController(name, mac);
        RGBController_BLEDOM* rgb_controller = new RGBController_BLEDOM(controller);

        detected_controllers.push_back(rgb_controller);
    }

    return(detected_controllers);
}

REGISTER_DETECTOR("ELK-BLEDOM Bluetooth LE", DetectBLEDOMControllers);

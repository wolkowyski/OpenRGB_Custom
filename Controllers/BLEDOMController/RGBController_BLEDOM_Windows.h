/*---------------------------------------------------------*\
| RGBController_BLEDOM_Windows.h                            |
|                                                           |
|   RGBController for ELK-BLEDOM Bluetooth LE LED strips    |
|                                                           |
|   Braian Wolkowyski                           21 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "RGBController.h"
#include "BLEDOMController_Windows.h"

#define BLEDOM_BRIGHTNESS_MIN   0
#define BLEDOM_SPEED_MIN        0
#define BLEDOM_SPEED_DEFAULT    50

enum
{
    BLEDOM_MODE_DIRECT          = 0,
    BLEDOM_MODE_BREATHING       = 1,
    BLEDOM_MODE_FLASHING        = 2,
    BLEDOM_MODE_JUMP            = 3,
    BLEDOM_MODE_JUMP_RGB        = 4,
    BLEDOM_MODE_CROSS_FADE      = 5,
    BLEDOM_MODE_OFF             = 6,
};

class RGBController_BLEDOM : public RGBController
{
public:
    RGBController_BLEDOM(BLEDOMController* controller_ptr);
    ~RGBController_BLEDOM();

    void        SetupZones();

    void        DeviceUpdateLEDs();
    void        DeviceUpdateZoneLEDs(int zone);
    void        DeviceUpdateSingleLED(int led);

    void        DeviceUpdateMode();

private:
    BLEDOMController* controller;
};

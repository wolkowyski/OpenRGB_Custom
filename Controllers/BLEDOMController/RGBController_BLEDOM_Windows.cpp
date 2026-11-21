/*---------------------------------------------------------*\
| RGBController_BLEDOM_Windows.cpp                          |
|                                                           |
|   RGBController for ELK-BLEDOM Bluetooth LE LED strips    |
|                                                           |
|   Braian Wolkowyski                           21 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "RGBController_BLEDOM_Windows.h"

/**------------------------------------------------------------------*\
    @name ELK-BLEDOM Bluetooth LE LED Strip
    @category LEDStrip
    @type Bluetooth LE
    @save :x:
    @direct :white_check_mark:
    @effects :x:
    @detectors DetectBLEDOMControllers
    @comment Generic Bluetooth LE strips that advertise as ELK-BLEDOM
        (sold under many brands, e.g. Lambo Tech). Add each strip by MAC
        address in Settings > Manually Added Devices.
\*-------------------------------------------------------------------*/

RGBController_BLEDOM::RGBController_BLEDOM(BLEDOMController* controller_ptr)
{
    controller  = controller_ptr;

    name        = controller->GetName();
    vendor      = "ELK-BLEDOM";
    type        = DEVICE_TYPE_LEDSTRIP;
    description = "Bluetooth LE LED Strip";
    location    = controller->GetLocation();
    serial      = controller->GetMAC();

    /*-----------------------------------------------------*\
    | The strip has no brightness command we rely on, so    |
    | brightness is applied by scaling the color.           |
    \*-----------------------------------------------------*/
    mode Direct;
    Direct.name           = "Direct";
    Direct.value          = BLEDOM_MODE_DIRECT;
    Direct.flags          = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_BRIGHTNESS;
    Direct.color_mode     = MODE_COLORS_PER_LED;
    Direct.brightness_min = BLEDOM_BRIGHTNESS_MIN;
    Direct.brightness_max = BLEDOM_BRIGHTNESS_MAX;
    Direct.brightness     = BLEDOM_BRIGHTNESS_MAX;
    modes.push_back(Direct);

    mode Off;
    Off.name              = "Off";
    Off.value             = BLEDOM_MODE_OFF;
    Off.flags             = 0;
    Off.color_mode        = MODE_COLORS_NONE;
    modes.push_back(Off);

    SetupZones();
}

RGBController_BLEDOM::~RGBController_BLEDOM()
{
    Shutdown();

    delete controller;
}

void RGBController_BLEDOM::SetupZones()
{
    /*-----------------------------------------------------*\
    | The whole strip shows a single color                  |
    \*-----------------------------------------------------*/
    zone strip_zone;
    strip_zone.name       = "LED Strip";
    strip_zone.type       = ZONE_TYPE_SINGLE;
    strip_zone.leds_min   = 1;
    strip_zone.leds_max   = 1;
    strip_zone.leds_count = 1;
    zones.push_back(strip_zone);

    led strip_led;
    strip_led.name = "LED Strip";
    leds.push_back(strip_led);

    SetupColors();
}

void RGBController_BLEDOM::DeviceUpdateLEDs()
{
    if(modes[active_mode].value == BLEDOM_MODE_OFF)
    {
        controller->SetColor(0, 0, 0);
        return;
    }

    unsigned int  brightness = modes[active_mode].brightness;
    unsigned char red        = (unsigned char)((RGBGetRValue(colors[0]) * brightness) / BLEDOM_BRIGHTNESS_MAX);
    unsigned char grn        = (unsigned char)((RGBGetGValue(colors[0]) * brightness) / BLEDOM_BRIGHTNESS_MAX);
    unsigned char blu        = (unsigned char)((RGBGetBValue(colors[0]) * brightness) / BLEDOM_BRIGHTNESS_MAX);

    controller->SetColor(red, grn, blu);
}

void RGBController_BLEDOM::DeviceUpdateZoneLEDs(int /*zone*/)
{
    DeviceUpdateLEDs();
}

void RGBController_BLEDOM::DeviceUpdateSingleLED(int /*led*/)
{
    DeviceUpdateLEDs();
}

void RGBController_BLEDOM::DeviceUpdateMode()
{
    DeviceUpdateLEDs();
}

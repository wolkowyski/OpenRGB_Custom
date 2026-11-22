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
    @effects :white_check_mark:
    @detectors DetectBLEDOMControllers
    @comment Generic Bluetooth LE strips that advertise as ELK-BLEDOM
        (sold under many brands, e.g. Lambo Tech). Add each strip by MAC
        address in Settings > Manually Added Devices. The strip accepts a
        single BLE connection, so close the phone app first.
\*-------------------------------------------------------------------*/

/*---------------------------------------------------------*\
| Colors available to the firmware's single color effects,  |
| in the order of their effect codes                        |
\*---------------------------------------------------------*/
static const RGBColor bledom_preset_colors[BLEDOM_PRESET_COUNT] =
{
    ToRGBColor(0xFF, 0x00, 0x00),       /* Red              */
    ToRGBColor(0x00, 0xFF, 0x00),       /* Green            */
    ToRGBColor(0x00, 0x00, 0xFF),       /* Blue             */
    ToRGBColor(0xFF, 0xFF, 0x00),       /* Yellow           */
    ToRGBColor(0x00, 0xFF, 0xFF),       /* Cyan             */
    ToRGBColor(0xFF, 0x00, 0xFF),       /* Purple           */
    ToRGBColor(0xFF, 0xFF, 0xFF),       /* White            */
};

/*---------------------------------------------------------*\
| Find the preset closest to a color. Colors are normalized |
| to their brightest channel first, so a dark red still     |
| maps to red instead of white.                             |
\*---------------------------------------------------------*/
static unsigned char NearestPresetColor(RGBColor color)
{
    int red         = RGBGetRValue(color);
    int grn         = RGBGetGValue(color);
    int blu         = RGBGetBValue(color);
    int max_channel = red;

    if(grn > max_channel)
    {
        max_channel = grn;
    }

    if(blu > max_channel)
    {
        max_channel = blu;
    }

    if(max_channel == 0)
    {
        return(BLEDOM_PRESET_COUNT - 1);
    }

    red = (red * 255) / max_channel;
    grn = (grn * 255) / max_channel;
    blu = (blu * 255) / max_channel;

    unsigned char   best_idx      = 0;
    int             best_distance = -1;

    for(unsigned char preset_idx = 0; preset_idx < BLEDOM_PRESET_COUNT; preset_idx++)
    {
        int diff_red = red - RGBGetRValue(bledom_preset_colors[preset_idx]);
        int diff_grn = grn - RGBGetGValue(bledom_preset_colors[preset_idx]);
        int diff_blu = blu - RGBGetBValue(bledom_preset_colors[preset_idx]);
        int distance = (diff_red * diff_red) + (diff_grn * diff_grn) + (diff_blu * diff_blu);

        if(best_distance < 0 || distance < best_distance)
        {
            best_distance = distance;
            best_idx      = preset_idx;
        }
    }

    return(best_idx);
}

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
    | Direct dims by scaling the color. The built-in        |
    | effects use the strip's own speed and brightness      |
    | (some firmwares ignore brightness during effects).    |
    | Off sends the power off command; any other mode turns |
    | the strip back on.                                    |
    \*-----------------------------------------------------*/
    mode Direct;
    Direct.name                 = "Direct";
    Direct.value                = BLEDOM_MODE_DIRECT;
    Direct.flags                = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_BRIGHTNESS;
    Direct.color_mode           = MODE_COLORS_PER_LED;
    Direct.brightness_min       = BLEDOM_BRIGHTNESS_MIN;
    Direct.brightness_max       = BLEDOM_BRIGHTNESS_MAX;
    Direct.brightness           = BLEDOM_BRIGHTNESS_MAX;
    modes.push_back(Direct);

    mode Jump;
    Jump.name                   = "Jump";
    Jump.value                  = BLEDOM_MODE_JUMP;
    Jump.flags                  = MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS;
    Jump.color_mode             = MODE_COLORS_NONE;
    Jump.speed_min              = BLEDOM_SPEED_MIN;
    Jump.speed_max              = BLEDOM_SPEED_MAX;
    Jump.speed                  = BLEDOM_SPEED_DEFAULT;
    Jump.brightness_min         = BLEDOM_BRIGHTNESS_MIN;
    Jump.brightness_max         = BLEDOM_BRIGHTNESS_MAX;
    Jump.brightness             = BLEDOM_BRIGHTNESS_MAX;
    modes.push_back(Jump);

    mode Breathing;
    Breathing.name              = "Breathing";
    Breathing.value             = BLEDOM_MODE_BREATHING;
    Breathing.flags             = MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_RANDOM_COLOR;
    Breathing.color_mode        = MODE_COLORS_MODE_SPECIFIC;
    Breathing.colors_min        = 1;
    Breathing.colors_max        = 1;
    Breathing.colors.resize(1);
    Breathing.colors[0]         = bledom_preset_colors[0];
    Breathing.speed_min         = BLEDOM_SPEED_MIN;
    Breathing.speed_max         = BLEDOM_SPEED_MAX;
    Breathing.speed             = BLEDOM_SPEED_DEFAULT;
    Breathing.brightness_min    = BLEDOM_BRIGHTNESS_MIN;
    Breathing.brightness_max    = BLEDOM_BRIGHTNESS_MAX;
    Breathing.brightness        = BLEDOM_BRIGHTNESS_MAX;
    modes.push_back(Breathing);

    mode Flashing;
    Flashing.name               = "Flashing";
    Flashing.value              = BLEDOM_MODE_FLASHING;
    Flashing.flags              = MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_RANDOM_COLOR;
    Flashing.color_mode         = MODE_COLORS_MODE_SPECIFIC;
    Flashing.colors_min         = 1;
    Flashing.colors_max         = 1;
    Flashing.colors.resize(1);
    Flashing.colors[0]          = bledom_preset_colors[6];
    Flashing.speed_min          = BLEDOM_SPEED_MIN;
    Flashing.speed_max          = BLEDOM_SPEED_MAX;
    Flashing.speed              = BLEDOM_SPEED_DEFAULT;
    Flashing.brightness_min     = BLEDOM_BRIGHTNESS_MIN;
    Flashing.brightness_max     = BLEDOM_BRIGHTNESS_MAX;
    Flashing.brightness         = BLEDOM_BRIGHTNESS_MAX;
    modes.push_back(Flashing);

    mode Off;
    Off.name                    = "Off";
    Off.value                   = BLEDOM_MODE_OFF;
    Off.flags                   = 0;
    Off.color_mode              = MODE_COLORS_NONE;
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
    /*-----------------------------------------------------*\
    | Per-LED colors only apply in Direct mode              |
    \*-----------------------------------------------------*/
    if(modes[active_mode].value != BLEDOM_MODE_DIRECT)
    {
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
    mode&           active      = modes[active_mode];
    unsigned char   speed       = (unsigned char)active.speed;
    unsigned char   brightness  = (unsigned char)active.brightness;

    switch(active.value)
    {
        case BLEDOM_MODE_DIRECT:
            DeviceUpdateLEDs();
            break;

        case BLEDOM_MODE_JUMP:
            controller->SetEffect(BLEDOM_EFFECT_JUMP_7, speed, brightness);
            break;

        case BLEDOM_MODE_BREATHING:
            if(active.color_mode == MODE_COLORS_RANDOM)
            {
                controller->SetEffect(BLEDOM_EFFECT_BREATHING_7, speed, brightness);
            }
            else
            {
                controller->SetEffect(BLEDOM_EFFECT_BREATHING + NearestPresetColor(active.colors[0]), speed, brightness);
            }
            break;

        case BLEDOM_MODE_FLASHING:
            if(active.color_mode == MODE_COLORS_RANDOM)
            {
                controller->SetEffect(BLEDOM_EFFECT_STROBE_7, speed, brightness);
            }
            else
            {
                controller->SetEffect(BLEDOM_EFFECT_STROBE + NearestPresetColor(active.colors[0]), speed, brightness);
            }
            break;

        case BLEDOM_MODE_OFF:
            controller->SetPower(false);
            break;
    }
}

/*---------------------------------------------------------*\
| BLEDOMController_Windows.cpp                              |
|                                                           |
|   Driver for ELK-BLEDOM Bluetooth LE LED strips           |
|   (Windows, WinRT Bluetooth LE GATT API)                  |
|                                                           |
|   Braian Wolkowyski                           21 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

/*---------------------------------------------------------*\
| OpenRGB builds as C++17. C++/WinRT only needs the         |
| coroutine handle types here (we never co_await), which    |
| MSVC still provides through the deprecated experimental   |
| header in C++17 mode.                                     |
\*---------------------------------------------------------*/
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS

#include <chrono>
#include <cstdio>
#include <cstring>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

#include "BLEDOMController_Windows.h"
#include "LogManager.h"

#pragma comment(lib, "windowsapp")

using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;

/*---------------------------------------------------------*\
| Characteristics used by the different BLEDOM firmwares    |
\*---------------------------------------------------------*/
static const winrt::guid bledom_write_uuids[] =
{
    winrt::guid(0x0000FFF3, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB }),
    winrt::guid(0x0000FFE9, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB }),
};

struct BLEDOMWinRTState
{
    BluetoothLEDevice           device          { nullptr };
    GattSession                 session         { nullptr };
    GattCharacteristic          characteristic  { nullptr };
    GattWriteOption             write_option    = GattWriteOption::WriteWithoutResponse;
};

BLEDOMController::BLEDOMController(const std::string& dev_name, const std::string& mac)
{
    name            = dev_name;
    mac_string      = mac;
    address         = 0;
    pending         = false;
    worker_running  = false;
    connected       = false;
    flush_deadline  = std::chrono::steady_clock::now();
    winrt_state     = std::make_unique<BLEDOMWinRTState>();

    memset(pending_frame, 0, sizeof(pending_frame));

    if(!ParseMAC(mac, address))
    {
        LOG_ERROR("[BLEDOM] Invalid MAC address \"%s\" for device \"%s\"", mac.c_str(), name.c_str());
        return;
    }

    /*-----------------------------------------------------*\
    | Connecting can take several seconds, so it happens on |
    | the worker thread and detection returns immediately.  |
    \*-----------------------------------------------------*/
    worker_running  = true;
    worker_thread   = std::thread(&BLEDOMController::WorkerThreadFunction, this);
}

BLEDOMController::~BLEDOMController()
{
    /*-----------------------------------------------------*\
    | Give the worker a few seconds to deliver a color that |
    | is still queued (e.g. CLI usage, which sets a color   |
    | and exits right away, before the link is up).         |
    \*-----------------------------------------------------*/
    {
        std::lock_guard<std::mutex> lock(pending_mutex);
        flush_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(BLEDOM_SHUTDOWN_FLUSH_TIME);
        worker_running = false;
    }
    pending_cv.notify_all();

    if(worker_thread.joinable())
    {
        worker_thread.join();
    }
}

std::string BLEDOMController::GetName()
{
    return(name);
}

std::string BLEDOMController::GetMAC()
{
    return(mac_string);
}

std::string BLEDOMController::GetLocation()
{
    return("Bluetooth LE: " + mac_string);
}

bool BLEDOMController::IsConnected()
{
    return(connected);
}

void BLEDOMController::SetColor(unsigned char red, unsigned char grn, unsigned char blu)
{
    {
        std::lock_guard<std::mutex> lock(pending_mutex);

        /*-------------------------------------------------*\
        | 7E 07 05 03 RR GG BB 10 EF : set static color     |
        \*-------------------------------------------------*/
        pending_frame[0] = 0x7E;
        pending_frame[1] = 0x07;
        pending_frame[2] = 0x05;
        pending_frame[3] = 0x03;
        pending_frame[4] = red;
        pending_frame[5] = grn;
        pending_frame[6] = blu;
        pending_frame[7] = 0x10;
        pending_frame[8] = 0xEF;
        pending          = true;
    }
    pending_cv.notify_one();
}

bool BLEDOMController::ParseMAC(const std::string& mac, uint64_t& out_address)
{
    unsigned int bytes[6];
    char         trailing;

    if(sscanf(mac.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x%c",
              &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5], &trailing) != 6)
    {
        return(false);
    }

    out_address = 0;

    for(int byte_idx = 0; byte_idx < 6; byte_idx++)
    {
        out_address = (out_address << 8) | (bytes[byte_idx] & 0xFF);
    }

    return(true);
}

void BLEDOMController::WorkerThreadFunction()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    unsigned char frame[BLEDOM_FRAME_SIZE];

    /*-----------------------------------------------------*\
    | Exit once stopped, unless a color is still queued and |
    | the shutdown flush window has not expired yet.        |
    | Must be called with pending_mutex held.               |
    \*-----------------------------------------------------*/
    auto should_exit = [this]()
    {
        return !worker_running && (!pending || std::chrono::steady_clock::now() >= flush_deadline);
    };

    while(true)
    {
        {
            std::lock_guard<std::mutex> lock(pending_mutex);

            if(should_exit())
            {
                break;
            }
        }

        /*-------------------------------------------------*\
        | (Re)connect, waiting between failed attempts      |
        \*-------------------------------------------------*/
        if(!connected)
        {
            if(!Connect())
            {
                std::unique_lock<std::mutex> lock(pending_mutex);

                if(worker_running)
                {
                    pending_cv.wait_for(lock, std::chrono::milliseconds(BLEDOM_RECONNECT_INTERVAL), [this]{ return !worker_running; });
                }
                else
                {
                    pending_cv.wait_for(lock, std::chrono::milliseconds(500));
                }
                continue;
            }
        }

        /*-------------------------------------------------*\
        | Wait for a new color                              |
        \*-------------------------------------------------*/
        {
            std::unique_lock<std::mutex> lock(pending_mutex);
            pending_cv.wait(lock, [this]{ return pending || !worker_running; });

            if(!pending)
            {
                continue;
            }

            memcpy(frame, pending_frame, sizeof(frame));
            pending = false;
        }

        if(!WriteFrame(frame))
        {
            /*---------------------------------------------*\
            | Keep the color so it is sent after reconnect  |
            \*---------------------------------------------*/
            {
                std::lock_guard<std::mutex> lock(pending_mutex);
                if(!pending)
                {
                    memcpy(pending_frame, frame, sizeof(frame));
                    pending = true;
                }
            }
            Disconnect();
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(BLEDOM_MIN_WRITE_INTERVAL));
    }

    Disconnect();
    winrt::uninit_apartment();
}

bool BLEDOMController::Connect()
{
    try
    {
        BluetoothLEDevice device = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();

        if(!device)
        {
            LOG_VERBOSE("[BLEDOM] %s: device %s not found", name.c_str(), mac_string.c_str());
            return(false);
        }

        /*-------------------------------------------------*\
        | Ask Windows to keep the link up between writes    |
        \*-------------------------------------------------*/
        GattSession session = GattSession::FromDeviceIdAsync(device.BluetoothDeviceId()).get();
        if(session)
        {
            session.MaintainConnection(true);
        }

        GattDeviceServicesResult services = device.GetGattServicesAsync(BluetoothCacheMode::Uncached).get();

        if(services.Status() != GattCommunicationStatus::Success)
        {
            LOG_VERBOSE("[BLEDOM] %s: GATT services unreachable (status %d), is another app connected?", name.c_str(), (int)services.Status());
            device.Close();
            return(false);
        }

        for(GattDeviceService const& service : services.Services())
        {
            GattCharacteristicsResult characteristics = service.GetCharacteristicsAsync(BluetoothCacheMode::Uncached).get();

            if(characteristics.Status() != GattCommunicationStatus::Success)
            {
                continue;
            }

            for(GattCharacteristic const& characteristic : characteristics.Characteristics())
            {
                for(winrt::guid const& uuid : bledom_write_uuids)
                {
                    if(characteristic.Uuid() != uuid)
                    {
                        continue;
                    }

                    GattCharacteristicProperties props = characteristic.CharacteristicProperties();

                    if((props & GattCharacteristicProperties::WriteWithoutResponse) == GattCharacteristicProperties::WriteWithoutResponse)
                    {
                        winrt_state->write_option = GattWriteOption::WriteWithoutResponse;
                    }
                    else if((props & GattCharacteristicProperties::Write) == GattCharacteristicProperties::Write)
                    {
                        winrt_state->write_option = GattWriteOption::WriteWithResponse;
                    }
                    else
                    {
                        continue;
                    }

                    winrt_state->device         = device;
                    winrt_state->session        = session;
                    winrt_state->characteristic = characteristic;
                    connected                   = true;

                    LOG_INFO("[BLEDOM] %s: connected to %s", name.c_str(), mac_string.c_str());
                    return(true);
                }
            }
        }

        LOG_WARNING("[BLEDOM] %s: no BLEDOM write characteristic (FFF3/FFE9) found", name.c_str());

        if(session)
        {
            session.Close();
        }
        device.Close();
    }
    catch(winrt::hresult_error const& error)
    {
        LOG_WARNING("[BLEDOM] %s: connection failed (0x%08X)", name.c_str(), (unsigned int)error.code());
    }

    return(false);
}

void BLEDOMController::Disconnect()
{
    connected = false;

    try
    {
        winrt_state->characteristic = nullptr;

        if(winrt_state->session)
        {
            winrt_state->session.Close();
            winrt_state->session = nullptr;
        }

        if(winrt_state->device)
        {
            winrt_state->device.Close();
            winrt_state->device = nullptr;
        }
    }
    catch(winrt::hresult_error const&)
    {
        winrt_state->session = nullptr;
        winrt_state->device  = nullptr;
    }
}

bool BLEDOMController::WriteFrame(const unsigned char* frame)
{
    try
    {
        DataWriter writer;
        writer.WriteBytes(winrt::array_view<const uint8_t>(frame, frame + BLEDOM_FRAME_SIZE));

        GattWriteResult result = winrt_state->characteristic.WriteValueWithResultAsync(writer.DetachBuffer(), winrt_state->write_option).get();

        if(result.Status() != GattCommunicationStatus::Success)
        {
            LOG_WARNING("[BLEDOM] %s: write failed (status %d)", name.c_str(), (int)result.Status());
            return(false);
        }
    }
    catch(winrt::hresult_error const& error)
    {
        LOG_WARNING("[BLEDOM] %s: write failed (0x%08X)", name.c_str(), (unsigned int)error.code());
        return(false);
    }

    return(true);
}

/*---------------------------------------------------------*\
| BLEDOMSettingsEntry.cpp                                   |
|                                                           |
|   User interface for OpenRGB ELK-BLEDOM settings entry    |
|                                                           |
|   Braian Wolkowyski                           21 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include "BLEDOMSettingsEntry.h"
#include "ui_BLEDOMSettingsEntry.h"

static const QRegularExpression bledom_mac_regex("^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$");

BLEDOMSettingsEntry::BLEDOMSettingsEntry(QWidget *parent) :
    BaseManualDeviceEntry(parent),
    ui(new Ui::BLEDOMSettingsEntry)
{
    ui->setupUi(this);

    /*-----------------------------------------------------*\
    | Only accept hex digits and colons while typing        |
    \*-----------------------------------------------------*/
    ui->MACEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("^[0-9A-Fa-f:]{0,17}$"), this));
}

BLEDOMSettingsEntry::~BLEDOMSettingsEntry()
{
    delete ui;
}

void BLEDOMSettingsEntry::changeEvent(QEvent *event)
{
    if(event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
    }
}

void BLEDOMSettingsEntry::loadFromSettings(const json& data)
{
    if(data.contains("name") && data["name"].is_string())
    {
        ui->NameEdit->setText(QString::fromStdString(data["name"]));
    }

    if(data.contains("mac") && data["mac"].is_string())
    {
        ui->MACEdit->setText(QString::fromStdString(data["mac"]));
    }
}

json BLEDOMSettingsEntry::saveSettings()
{
    json result;

    result["name"] = ui->NameEdit->text().trimmed().toStdString();
    result["mac"]  = ui->MACEdit->text().trimmed().toUpper().toStdString();

    return result;
}

bool BLEDOMSettingsEntry::isDataValid()
{
    return bledom_mac_regex.match(ui->MACEdit->text().trimmed()).hasMatch();
}

static BaseManualDeviceEntry* SpawnBLEDOMSettingsEntry(const json& data)
{
    BLEDOMSettingsEntry* entry = new BLEDOMSettingsEntry;
    entry->loadFromSettings(data);
    return entry;
}

/*---------------------------------------------------------*\
| The BLEDOM driver is only implemented on Windows so far   |
\*---------------------------------------------------------*/
#ifdef _WIN32
REGISTER_MANUAL_DEVICE_TYPE("ELK-BLEDOM (Bluetooth LE)", "BLEDOMDevices", SpawnBLEDOMSettingsEntry);
#endif

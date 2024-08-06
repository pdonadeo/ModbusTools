/*
    Modbus Tools

    Created: 2023
    Author: Serhii Marchuk, https://github.com/serhmarch

    Copyright (C) 2023  Serhii Marchuk

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "client_scannerthread.h"

#include <ModbusClientPort.h>
#include <ModbusSerialPort.h>
#include <ModbusTcpPort.h>

#include <client.h>

#include "client_scanner.h"

mbClientScannerThread::mbClientScannerThread(mbClientScanner *scanner, QObject *parent)
    : QThread(parent)
{
    m_scanner = scanner;
    m_ctrlRun = true;
    m_unitStart = Modbus::VALID_MODBUS_ADDRESS_BEGIN;
    m_unitEnd = Modbus::VALID_MODBUS_ADDRESS_END;
    moveToThread(this);
}

mbClientScannerThread::~mbClientScannerThread()
{
}

void mbClientScannerThread::setSettings(const Modbus::Settings &settings)
{
    const Modbus::Strings &s = Modbus::Strings::instance();

    m_settings = settings;
    m_unitStart = mbClientScanner::getSettingUnitStart(settings);
    m_unitEnd   = mbClientScanner::getSettingUnitEnd(settings);
    m_combinationCount = 1;

    m_divMods   .clear();
    m_names     .clear();
    m_valuesList.clear();

    Modbus::ProtocolType type = Modbus::getSettingType(settings);
    switch (type)
    {
    case Modbus::ASC:
    case Modbus::RTU:
    {
        QVariantList baudRate = mbClientScanner::getSettingBaudRate(settings);
        QVariantList dataBits = mbClientScanner::getSettingDataBits(settings);
        QVariantList parity   = mbClientScanner::getSettingParity  (settings);
        QVariantList stopBits = mbClientScanner::getSettingStopBits(settings);

#define DEFINE_COMBINATION_ELEMENT(elem)            \
        if (elem.count())                           \
        {                                           \
            DivMod dm;                              \
            dm.div = m_combinationCount;            \
            dm.mod = elem.count();                  \
            m_divMods.append(dm);                   \
            m_names.append(s.elem);                 \
            m_valuesList.append(elem);              \
            m_combinationCount *= elem.count();     \
        }

        DEFINE_COMBINATION_ELEMENT(stopBits)
        DEFINE_COMBINATION_ELEMENT(parity  )
        DEFINE_COMBINATION_ELEMENT(dataBits)
        DEFINE_COMBINATION_ELEMENT(baudRate)
    }
        break;
    default:
        break;
    }
}

void mbClientScannerThread::run()
{
    const mbClientScanner::Strings &s = mbClientScanner::Strings::instance();
    m_ctrlRun = true;
    mbClient::LogInfo(s.name, QStringLiteral("Start scanning"));


    Modbus::Settings settings = m_settings;
    uint16_t dummy;
    for (int c = 0; m_ctrlRun && (c < m_combinationCount); c++)
    {
        // Get comibation number for each setting
        for (quint16 si = 0; si < m_names.count(); si++)
        {
            const QString &name = m_names.at(si);
            const DivMod &dm = m_divMods.at(si);
            // Calc value of each setting corresponding to current combination number
            int sc = (c / dm.div) % dm.mod;
            const QVariant &v = m_valuesList.at(si).at(sc);
            settings[name] = v;
        }
        ModbusClientPort *clientPort = Modbus::createClientPort(settings, true);
        QString sName;
        switch (clientPort->type())
        {
        case Modbus::ASC:
            clientPort->connect(&ModbusClientPort::signalTx, this, &mbClientScannerThread::slotAsciiTx);
            clientPort->connect(&ModbusClientPort::signalRx, this, &mbClientScannerThread::slotAsciiRx);
            sName = QString("ASC:%1:%2:%3%4%5")
                        .arg(static_cast<ModbusSerialPort*>(clientPort->port())->portName(),
                             QString::number(static_cast<ModbusSerialPort*>(clientPort->port())->baudRate()),
                             QString::number(static_cast<ModbusSerialPort*>(clientPort->port())->dataBits()),
                             mbClientScanner::toShortParityStr(static_cast<ModbusSerialPort*>(clientPort->port())->parity()),
                             mbClientScanner::toShortStopBitsStr(static_cast<ModbusSerialPort*>(clientPort->port())->stopBits()));
            break;
        case Modbus::RTU:
            clientPort->connect(&ModbusClientPort::signalTx, this, &mbClientScannerThread::slotBytesTx);
            clientPort->connect(&ModbusClientPort::signalRx, this, &mbClientScannerThread::slotBytesRx);
            sName = QString("RTU:%1:%2:%3%4%5")
                        .arg(static_cast<ModbusSerialPort*>(clientPort->port())->portName(),
                             QString::number(static_cast<ModbusSerialPort*>(clientPort->port())->baudRate()),
                             QString::number(static_cast<ModbusSerialPort*>(clientPort->port())->dataBits()),
                             mbClientScanner::toShortParityStr(static_cast<ModbusSerialPort*>(clientPort->port())->parity()),
                             mbClientScanner::toShortStopBitsStr(static_cast<ModbusSerialPort*>(clientPort->port())->stopBits()));
            break;
        default:
            clientPort->connect(&ModbusClientPort::signalTx, this, &mbClientScannerThread::slotBytesTx);
            clientPort->connect(&ModbusClientPort::signalRx, this, &mbClientScannerThread::slotBytesRx);
            sName = QString("TCP:%1:%2")
                        .arg(static_cast<ModbusTcpPort*>(clientPort->port())->host(),
                             QString::number(static_cast<ModbusTcpPort*>(clientPort->port())->port()));
            break;
        }
        clientPort->setObjectName(sName.toLatin1().constData());
        mbClient::LogInfo(s.name, QString("Begin scanning '%1'").arg(sName));
        for (uint16_t unit = m_unitStart; unit <= m_unitEnd; unit++)
        {
            if (!m_ctrlRun)
                break;
            Modbus::StatusCode status = clientPort->readHoldingRegisters(static_cast<uint16_t>(unit), 0, 1, &dummy);
            if (Modbus::StatusIsGood(status))
            {
                Modbus::setSettingUnit(settings, unit);
                m_scanner->deviceAdd(settings);
            }
        }
        clientPort->close();
        mbClient::LogInfo(s.name, QString("End scanning '%1'").arg(sName));
        delete clientPort;
    }
    mbClient::LogInfo(s.name, QStringLiteral("Finish scanning"));
}

void mbClientScannerThread::slotBytesTx(const Modbus::Char *source, const uint8_t *buff, uint16_t size)
{
    mbClient::LogTxRx(mbClientScanner::Strings::instance().name + QString(" ") + source, QStringLiteral("Tx: ") + Modbus::bytesToString(buff, size).data());
}

void mbClientScannerThread::slotBytesRx(const Modbus::Char *source, const uint8_t *buff, uint16_t size)
{
    mbClient::LogTxRx(mbClientScanner::Strings::instance().name + QString(" ") + source, QStringLiteral("Rx: ") + Modbus::bytesToString(buff, size).data());
}

void mbClientScannerThread::slotAsciiTx(const Modbus::Char *source, const uint8_t *buff, uint16_t size)
{
    mbClient::LogTxRx(mbClientScanner::Strings::instance().name + QString(" ") + source, QStringLiteral("Tx: ") + Modbus::asciiToString(buff, size).data());
}

void mbClientScannerThread::slotAsciiRx(const Modbus::Char *source, const uint8_t *buff, uint16_t size)
{
    mbClient::LogTxRx(mbClientScanner::Strings::instance().name + QString(" ") + source, QStringLiteral("Rx: ") + Modbus::asciiToString(buff, size).data());
}

#include "qt_settingsotherperipherals.hpp"
#include "ui_qt_settingsotherperipherals.h"

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/isamem.h>
#include <86box/isartc.h>
}

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

SettingsOtherPeripherals::SettingsOtherPeripherals(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsOtherPeripherals)
{
    ui->setupUi(this);

    ui->checkBoxISABugger->setChecked(bugger_enabled > 0 ? true : false);
    ui->checkBoxPOSTCard->setChecked(postcard_enabled > 0 ? true : false);

    auto* model = ui->comboBoxRTC->model();
    int d = 0;
    int selectedRow = 0;
    while (true) {
        QString name = DeviceConfig::DeviceName(isartc_get_device(d), isartc_get_internal_name(d), 0);
        if (name.isEmpty()) {
            break;
        }

        int row = Models::AddEntry(model, name, d);
        if (d == isartc_type) {
            selectedRow = row;
        }
        ++d;
    }
    ui->comboBoxRTC->setCurrentIndex(selectedRow);

    for (int c = 0; c < ISAMEM_MAX; c++) {
        auto* cbox = findChild<QComboBox*>(QString("comboBoxCard%1").arg(c + 1));
        model = cbox->model();
        d = 0;
        selectedRow = 0;
        while (true) {
            QString name = DeviceConfig::DeviceName(isamem_get_device(d), isamem_get_internal_name(d), 0);
            if (name.isEmpty()) {
                break;
            }

            int row = Models::AddEntry(model, name, d);
            if (d == isamem_type[c]) {
                selectedRow = row;
            }
            ++d;
        }
        cbox->setCurrentIndex(-1);
        cbox->setCurrentIndex(selectedRow);
    }
}

SettingsOtherPeripherals::~SettingsOtherPeripherals()
{
    delete ui;
}

void SettingsOtherPeripherals::save() {
    /* Other peripherals category */
    bugger_enabled = ui->checkBoxISABugger->isChecked() ? 1 : 0;
    postcard_enabled = ui->checkBoxPOSTCard->isChecked() ? 1 : 0;
    isartc_type = ui->comboBoxRTC->currentData().toInt();

    /* ISA memory boards. */
    for (int i = 0; i < ISAMEM_MAX; i++) {
        auto* cbox = findChild<QComboBox*>(QString("comboBoxCard%1").arg(i + 1));
        isamem_type[i] = cbox->currentData().toInt();
    }
}

void SettingsOtherPeripherals::on_comboBoxRTC_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }
    ui->pushButtonConfigureRTC->setEnabled(index != 0);
}

void SettingsOtherPeripherals::on_pushButtonConfigureRTC_clicked() {
    DeviceConfig::ConfigureDevice(isartc_get_device(ui->comboBoxRTC->currentData().toInt()));
}

void SettingsOtherPeripherals::on_comboBoxCard1_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }
    ui->pushButtonConfigureCard1->setEnabled(index != 0);
}

void SettingsOtherPeripherals::on_pushButtonConfigureCard1_clicked() {
    DeviceConfig::ConfigureDevice(isamem_get_device(ui->comboBoxCard1->currentData().toInt()), 1);
}

void SettingsOtherPeripherals::on_comboBoxCard2_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }
    ui->pushButtonConfigureCard2->setEnabled(index != 0);
}

void SettingsOtherPeripherals::on_pushButtonConfigureCard2_clicked() {
    DeviceConfig::ConfigureDevice(isamem_get_device(ui->comboBoxCard2->currentData().toInt()), 2);
}

void SettingsOtherPeripherals::on_comboBoxCard3_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }
    ui->pushButtonConfigureCard3->setEnabled(index != 0);
}

void SettingsOtherPeripherals::on_pushButtonConfigureCard3_clicked() {
    DeviceConfig::ConfigureDevice(isamem_get_device(ui->comboBoxCard3->currentData().toInt()), 3);
}

void SettingsOtherPeripherals::on_comboBoxCard4_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }
    ui->pushButtonConfigureCard4->setEnabled(index != 0);
}

void SettingsOtherPeripherals::on_pushButtonConfigureCard4_clicked() {
    DeviceConfig::ConfigureDevice(isamem_get_device(ui->comboBoxCard4->currentData().toInt()), 4);
}
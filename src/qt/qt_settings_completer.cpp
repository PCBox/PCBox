/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Media history management module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2022 The 86Box development team
 */
#include <QApplication>
#include <QFileInfo>
#include <QMetaEnum>
#include <QStringBuilder>
#include <QComboBox>
#include <QCompleter>
#include <QAbstractItemView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QTimer>
#include <utility>
#include "qt_settings_completer.hpp"
#ifdef Q_OS_WINDOWS
#    include <windows.h>
#endif

extern "C" {
#include "../cpu/cpu.h"

#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/nvr.h>
}

bool
SettingsCompleter::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == comboBoxMain) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress) {
            activate();
            flush();
            /* Enter in the search box is the search's, not the dialog's: the
               combo would pass it on, and OK would take it. */
            if (event->type() == QEvent::KeyPress) {
                const int key = static_cast<QKeyEvent *>(event)->key();
                if ((key == Qt::Key_Return) || (key == Qt::Key_Enter))
                    return true;
            }
        } else if (event->type() == QEvent::FocusOut && comboBoxMain->lineEdit()) {
            int     i    = comboBoxMain->currentIndex();
            QString name = comboBoxMain->model()->data(comboBoxMain->model()->index(i, 0), Qt::DisplayRole).toString();
            comboBoxMain->lineEdit()->setText(name);
        }
    } else if ((comboBoxMain->lineEdit() != nullptr) && (watched == comboBoxMain->lineEdit()) && (event->type() == QEvent::KeyPress)) {
        /* Enter in the search box is the search's, not the dialog's: it
           must not press OK. With the completions open, they take it. */
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (((key == Qt::Key_Return) || (key == Qt::Key_Enter)) && !((completer != nullptr) && completer->popup()->isVisible()))
            return true;
    }

    return false;
}

SettingsCompleter::SettingsCompleter(QComboBox *cb, QComboBox *cbSort)
    : QObject(cb)
{
    comboBoxMain = cb;
    comboBoxSort = cbSort;

    model = new QStandardItemModel(this);
    comboBoxMain->setFocusPolicy(Qt::StrongFocus);
    comboBoxMain->installEventFilter(this);
}

void
SettingsCompleter::activate()
{
    if (active)
        return;
    active = true;
    comboBoxMain->setEditable(true);
    completer = new QCompleter(this);
    completer->setModel(model);
    comboBoxMain->lineEdit()->setCompleter(completer);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionRole(Qt::DisplayRole);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setMaxVisibleItems(30);
    comboBoxMain->lineEdit()->setClearButtonEnabled(true);
    comboBoxMain->lineEdit()->installEventFilter(this);

    connect(completer, QOverload<const QModelIndex &>::of(&QCompleter::activated), this, [this](const QModelIndex &idx) {
        int  sort_id = idx.model()->data(idx, Qt::UserRole + 1).toInt();
        auto name    = idx.model()->data(idx, Qt::UserRole + 2).toString();
        if ((sort_id != -1) && (comboBoxSort != nullptr))  for (int i = 0; i < comboBoxSort->model()->rowCount(); i++) {
            if (comboBoxSort->model()->data(comboBoxSort->model()->index(i, 0), Qt::UserRole).toInt() == sort_id) {
                comboBoxSort->setCurrentIndex(i);

                for (int j = 0; j < comboBoxMain->model()->rowCount(); j++) {
                    if (comboBoxMain->model()->data(comboBoxMain->model()->index(j, 0), Qt::DisplayRole).toString() == name) {
                        comboBoxMain->setCurrentIndex(j);
                        break;
                    }
                }
                break;
            }
        } else  for (int i = 0; i < comboBoxMain->model()->rowCount(); i++) {
            if (comboBoxMain->model()->data(comboBoxMain->model()->index(i, 0), Qt::DisplayRole).toString() == name) {
                comboBoxMain->setCurrentIndex(i);
                break;
            }
        }
    });
}

SettingsCompleter::~SettingsCompleter()
{
    /* Hiding the completion popup can send focus events during teardown. */
    if (comboBoxMain)
        comboBoxMain->removeEventFilter(this);
    delete completer;
    qDeleteAll(pending);
}

void
SettingsCompleter::addRow(QString name, QString alias, int special, int id)
{
    QString stored_alias;
    if (name == alias)
        stored_alias = QString(name);
    else if (special)
        stored_alias = alias;
    else
        stored_alias = QString("%1 (%2)").arg(name).arg(alias);

    QStandardItem *item = new QStandardItem(stored_alias);
    item->setData(id);
    item->setData(name, Qt::UserRole + 2);

    /* A page adds a whole list at a time: the rows are appended together
       once it is done, rather than the completer reworking its matches
       for each one. */
    if (pending.isEmpty())
        QTimer::singleShot(0, this, [this]() { flush(); });
    pending.append(item);

}

void
SettingsCompleter::flush()
{
    if (!active || pending.isEmpty())
        return;

    /* Model signals can re-enter the event filter or rebuild the list.
       Detach the batch before the model takes ownership of its items. */
    QList<QStandardItem *> batch;
    batch.swap(pending);
    model->invisibleRootItem()->appendRows(batch);
}

void
SettingsCompleter::addMachine(int i, int j)
{
    const QString name = machines[j].name;
    addRow(name, name, 0, machine_types[machine_get_type(j)].id);

    int k = 0;
    if (machines[j].aliases[k] == NULL)
        return;
    while (machines[j].aliases[k][0] != 0x00) {
        const QString alias = machines[j].aliases[k];
        /* MatchContains already finds aliases included in the display name. */
        if (!name.contains(alias, Qt::CaseInsensitive))
            addRow(name, alias, 0, machine_types[machine_get_type(j)].id);
        k++;
    };
}

void
SettingsCompleter::addDevice(const void *device, QString name)
{
    addRow(name, name, 0, -1);

    const device_t *dev = (const device_t *) device;
    if (dev != nullptr) {
        const char *alias = device_get_alias(dev);
        if ((alias != nullptr) && (strlen(alias) > 0))
            addRow(name, alias, dev->flags & DEVICE_BIOS_ALIAS, -1);

        const device_config_t *config = dev->config;
        while (config && (config->type != CONFIG_END)) {
            if (config->type == CONFIG_BIOS) {
                const device_config_bios_t *bios = (const device_config_bios_t *) config->bios;

                /* Go through the ROMs in the device configuration. */
                while ((bios != nullptr) &&
                       (bios->name != nullptr) &&
                       (bios->internal_name != nullptr) &&
                       (bios->files_no != 0)) {
                    addRow(name, bios->name, dev->flags & DEVICE_BIOS_ALIAS, -1);
                    bios++;
                }
            }
            config++;
        }
    }
}

void
SettingsCompleter::removeRows()
{
    qDeleteAll(pending);
    pending.clear();

    /* Use the actual model state: removal signals can queue a new batch. */
    if (model != nullptr)
        model->removeRows(0, model->rowCount());
}

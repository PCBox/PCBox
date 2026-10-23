/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Device configuration UI code.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2022 Cacodemon345
 */
#include "qt_settings_completer.hpp"

#include "qt_deviceconfig.hpp"
#include "qt_settingscards.hpp"
#include "ui_qt_deviceconfig.h"
#include "qt_settings.hpp"

#include <QDebug>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QFrame>
#include <QLineEdit>
#include <QLabel>
#include <QDir>
#include <QSettings>
#include <QSet>
#include <QStandardItemModel>
#include <QStringBuilder>
#include <QCollator>

extern "C" {
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/midi_rtmidi.h>
#include <86box/mem.h>
#include <86box/random.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/scsi.h>
}

#include "qt_filefield.hpp"
#include "qt_models_common.hpp"
#include "qt_util.hpp"
#include "qt_preferences.hpp"
#include "qt_settingsnetwork.hpp"
#include "qt_settingsstoragecontrollers.hpp"
#ifdef Q_OS_WINDOWS
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <setupapi.h>
#    include <devguid.h>
#endif

DeviceConfig::DeviceConfig(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DeviceConfig)
{
    ui->setupUi(this);
}

DeviceConfig::~DeviceConfig()
{
    delete ui;
}

static QMap<QString, QString>
enumerateSerialDevices()
{
    QMap<QString, QString> serialDevices;
#ifdef Q_OS_WINDOWS
    auto classDevs = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (classDevs) {
        SP_DEVINFO_DATA devInfo = { .cbSize = sizeof(SP_DEVINFO_DATA) };
        char            buf[1024];
        DWORD           bufSize;
        for (int i = 0; SetupDiEnumDeviceInfo(classDevs, i, &devInfo); i++) {
            bufSize   = sizeof(buf);
            auto hKey = SetupDiOpenDevRegKey(classDevs, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
            if (!hKey || (RegQueryValueExA(hKey, "PortName", NULL, NULL, (unsigned char *) buf, &bufSize) != ERROR_SUCCESS))
                continue;
            QString path(buf);
            buf[0] = '\0';
            SetupDiGetDeviceRegistryPropertyA(classDevs, &devInfo, SPDRP_FRIENDLYNAME, &bufSize, (unsigned char *) buf, sizeof(buf), nullptr);
            QString name(buf);
            if (name.isEmpty())
                serialDevices[path] = path;
            else if (name.contains(path, Qt::CaseInsensitive))
                serialDevices[path] = name;
            else
                serialDevices[path] = QStringLiteral("%1 (%2)").arg(name, path);
        }
    }
#elif defined(Q_OS_LINUX)
    QDir class_dir("/sys/class/tty/");
    QDir dev_dir("/dev/");
    QRegularExpression regex(QStringLiteral("[^/]*/"));
    for (const auto &device : class_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        auto driver = QFile::symLinkTarget(class_dir.absoluteFilePath(device) + "/device/driver");
        if (!driver.isEmpty() && dev_dir.exists(device)) {
            QFileInfo fi(dev_dir.canonicalPath() + '/' + device);
            if (fi.isReadable() && fi.isWritable())
                serialDevices[fi.canonicalFilePath()] = QStringLiteral("%1 (%2)").arg(device, driver.replace(regex, QStringLiteral("")));
        }
    }
#else
    QDir dev_dir("/dev/");
#    ifdef Q_OS_DARWIN
    dev_dir.setNameFilters({ "tty.*", "cu.*" });
#    else
    dev_dir.setNameFilters({ "tty[a-z]", "tty[0-9a-zA-Z][0-9]*", "cua[0-9a-zA-Z][0-9]*", "dty[0-9a-zA-Z][0-9]*" });
#    endif
    QDir::Filters serial_dev_flags = QDir::Files | QDir::NoSymLinks | QDir::Readable | QDir::Writable | QDir::NoDotAndDotDot | QDir::System;
    for (const auto &device : dev_dir.entryInfoList(serial_dev_flags))
        serialDevices[device.canonicalFilePath()] = device.fileName();
#endif
    return serialDevices;
}

static QList<DeviceConfigPanel *> livePanels;

/* The slot another EISA card is set to, read the way that card's init
   will read it: from a panel open on it, or from its own section of the
   configuration, or from its default when it has never been configured. */
static int
EisaSlotOf(const _device_ *dev, int instance)
{
    device_context_t ctx;
    int              def = 1;

    for (const auto *panel : livePanels) {
        if ((panel->device() == dev) && (panel->instance() == instance) && (panel->slotChoice() >= 0))
            return panel->slotChoice();
    }

    for (const _device_config_ *c = dev->config; (c != nullptr) && (c->type != CONFIG_END); ++c) {
        if (!strcmp(c->name, "slot")) {
            def = c->default_int;
            break;
        }
    }
    device_set_context(&ctx, dev, instance);
    return config_get_int(ctx.name, const_cast<char *>("slot"), def);
}

DeviceConfigPanel::DeviceConfigPanel(const _device_ *device, int instance, Settings *settings, QWidget *parent)
    : QWidget(parent)
    , dev(device)
    , inst(instance)
    , settings(settings)
{
    form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    livePanels.append(this);
    build();
}

DeviceConfigPanel::~DeviceConfigPanel()
{
    livePanels.removeAll(this);
}

const QList<DeviceConfigPanel *> &
DeviceConfigPanel::panels()
{
    return livePanels;
}

bool
DeviceConfigPanel::isEmpty() const
{
    return form->rowCount() == 0;
}

int
DeviceConfigPanel::slotChoice() const
{
    return ((cboxSlot != nullptr) && (cboxSlot->currentIndex() >= 0)) ? cboxSlot->currentData().toInt() : -1;
}

static QVariant
panelControlValue(QWidget *widget)
{
    if (auto *combo = qobject_cast<QComboBox *>(widget))
        return QVariantList { combo->currentData() };
    if (auto *spin = qobject_cast<QSpinBox *>(widget))
        return spin->value();
    if (auto *check = qobject_cast<QCheckBox *>(widget))
        return check->isChecked();
    if (auto *line = qobject_cast<QLineEdit *>(widget)) {
        if (qobject_cast<QComboBox *>(line->parentWidget()) || qobject_cast<QAbstractSpinBox *>(line->parentWidget()))
            return {};
        return line->text();
    }
    return {};
}

bool
DeviceConfigPanel::hasPendingEdits() const
{
    for (auto it = initialValues.cbegin(); it != initialValues.cend(); ++it)
        if (panelControlValue(it.key()) != it.value())
            return true;
    return false;
}

void
DeviceConfigPanel::build()
{
    device_context_t device_context;

    cboxSlot     = nullptr;
    cboxMemory   = nullptr;
    cboxMemory2  = nullptr;
    cboxBios     = nullptr;
    cboxBootlogo = nullptr;
    cfgMemory    = nullptr;
    cfgMemory2   = nullptr;
    cfgBios      = nullptr;
    cfgBootlogo  = nullptr;
    biosRows     = 0;

    device_set_context(&device_context, dev, inst);
    processConfig(&device_context, dev->config, false);
    initialValues.clear();
    for (auto *widget : findChildren<QWidget *>()) {
        const auto value = panelControlValue(widget);
        if (value.isValid())
            initialValues.insert(widget, value);
    }
}

/* What the configuration now holds for the device, after a Configure
   dialog on it wrote something. */
void
DeviceConfigPanel::reload()
{
    while (form->rowCount() > 0)
        form->removeRow(0);
    build();
}

void
DeviceConfigPanel::reloadOthers(const DeviceConfigPanel *from)
{
    for (auto *panel : livePanels) {
        if ((panel != from) && (panel->dev == from->dev) && (panel->inst == from->inst))
            panel->reload();
    }
}

/* Each open panel's changes written, for Settings being accepted: whether
   any changed. */
int
DeviceConfigPanel::applyAll(const QWidget *within)
{
    int changed = 0;

    for (auto *panel : livePanels) {
        if ((within == nullptr) || within->isAncestorOf(panel))
            changed |= panel->apply();
    }

    return changed;
}

void
DeviceConfigPanel::processConfig(void *dc, const void *c, const bool is_dep)
{
    auto         *device_context = static_cast<device_context_t *>(dc);
    const auto   *config         = static_cast<const _device_config_ *>(c);
    const QString blank          = "";
    const QString colon          = (Preferences::languageIdToCode(lang_id).startsWith("fr-") ? " :" : ":");
    int           bios           = -1;
    int           p;
    int           q;

    if (config == NULL)
        return;

    while (config->type != CONFIG_END) {
        const int config_type = config->type & CONFIG_TYPE_MASK;

        /* Ignore options of the wrong class. */
        if (!!(config->type & CONFIG_DEP) != is_dep) {
            ++config;
            continue;
        }

        /* If this is a BIOS-dependent option and it's BIOS, ignore it. */
        if (!!(config->type & CONFIG_DEP) && (config_type == CONFIG_BIOS)) {
            ++config;
            continue;
        }

        const int config_major_type = (config_type >> CONFIG_SHIFT) << CONFIG_SHIFT;

        int  value    = 0;
        auto selected = blank;

        switch (config_major_type) {
            default:
                break;
            case CONFIG_TYPE_INT:
                value = config_get_int(device_context->name, const_cast<char *>(config->name),
                                       config->default_int);
                break;
            case CONFIG_TYPE_HEX16:
                value = config_get_hex16(device_context->name, const_cast<char *>(config->name),
                                         config->default_int);
                break;
            case CONFIG_TYPE_HEX20:
                value = config_get_hex20(device_context->name, const_cast<char *>(config->name),
                                         config->default_int);
                break;
            case CONFIG_TYPE_STRING:
                selected = config_get_string(device_context->name, const_cast<char *>(config->name),
                                             const_cast<char *>(config->default_string));
                break;
            case CONFIG_TYPE_STRHEX:
                selected = config_get_string(device_context->name, const_cast<char *>(config->name),
                                             const_cast<char *>(config->default_string));
                break;
        }

        switch (config->type) {
            default:
                break;
            case CONFIG_BINARY:
                {
                    auto *cbox = new QCheckBox();
                    cbox->setObjectName(config->name);
                    cbox->setChecked(value > 0);
                    form->addRow(tr(config->description).append(colon), cbox);
                    break;
                }
#ifdef USE_RTMIDI
            case CONFIG_MIDI_OUT:
                {
                    auto *cbox   = new QComboBox();
                    cbox->setObjectName(config->name);
                    cbox->setMaxVisibleItems(30);
                    auto *model        = cbox->model();
                    int   currentIndex = -1;
                    for (int i = 0; i < rtmidi_out_get_num_devs(); i++) {
                        char midiName[512] = { 0 };
                        rtmidi_out_get_dev_name(i, midiName);

                        Models::AddEntry(model, midiName, i);
                        if (i == value)
                            currentIndex = i;
                    }
                    form->addRow(tr(config->description).append(colon), cbox);
                    cbox->setCurrentIndex(currentIndex);
                    break;
                }
            case CONFIG_MIDI_IN:
                {
                    auto *cbox   = new QComboBox();
                    cbox->setObjectName(config->name);
                    cbox->setMaxVisibleItems(30);
                    auto *model        = cbox->model();
                    int   currentIndex = -1;
                    for (int i = 0; i < rtmidi_in_get_num_devs(); i++) {
                        char midiName[512] = { 0 };
                        rtmidi_in_get_dev_name(i, midiName);

                        Models::AddEntry(model, midiName, i);
                        if (i == value)
                            currentIndex = i;
                    }
                    form->addRow(tr(config->description).append(colon), cbox);
                    cbox->setCurrentIndex(currentIndex);
                    break;
                }
#endif
            case CONFIG_INT:
            case CONFIG_SELECTION:
            case CONFIG_HEX16:
            case CONFIG_HEX20:
                {
                    auto *cbox = new QComboBox();
                    cbox->setObjectName(config->name);
                    cbox->setMaxVisibleItems(30);
                    auto *model        = cbox->model();
                    int   currentIndex = -1;
                    int   rows         = 0;

                    for (auto *sel = config->selection; (sel != nullptr) && (sel->description != nullptr) &&
                                                        (strlen(sel->description) > 0); ++sel) {
                        int row = Models::AddEntry(model, tr(sel->description), sel->value);

                        if (sel->value == value)
                            currentIndex = row;

                        rows++;
                    }
                    form->addRow(tr(config->description).append(colon), cbox);
                    cbox->setCurrentIndex(currentIndex);
                    if (rows < 2)
                        cbox->setEnabled(false);
                    if (!strcmp(config->name, "memory") || !strcmp(config->name, "framebuffer_memory")) {
                        cboxMemory   = cbox;
                        cfgMemory    = config;
                    }    
                    if (!strcmp(config->name, "texture_memory")) {
                        cboxMemory2 = cbox;
                        cfgMemory2  = config;
                    }
                    if (!strcmp(config->name, "boot_logo") &&
                        (dev != nullptr) && !strcmp(dev->internal_name, "in530")) {
                        cboxBootlogo = cbox;
                        cfgBootlogo  = config;
                    }
                    break;
                }
            case CONFIG_BIOS:
                {
                    auto *cbox   = new QComboBox();
                    cbox->setObjectName(config->name);
                    cbox->setMaxVisibleItems(30);
                    auto *model        = cbox->model();
                    int   currentIndex = -1;
                    int   rows         = 0;

                    if ((selected != nullptr) && (selected.length() == 1)) {
                        device_migrate_config_bios((const void *) config, device_context->name);
                        selected = config_get_string(device_context->name, const_cast<char *>(config->name),
                                                     const_cast<char *>(config->default_string));
                    }

                    q = 0;
                    for (auto *bios = config->bios; (bios != nullptr) &&
                                                    (bios->name != nullptr) &&
                                                    (bios->internal_name != nullptr) &&
                                                    (strlen(bios->name) > 0) &&
                                                    (strlen(bios->internal_name) > 0) &&
                                                    (bios->files_no != 0); ++bios) {
                        p = 0;
                        if (bios->files_no > 0) {
                            for (int d = 0; d < bios->files_no; d++)
                                p += !!rom_present(const_cast<char *>(bios->files[d]));
                        }
                        if ((bios->files_no == -1) || (p == bios->files_no)) {
                            const int row = Models::AddEntry(model, tr(bios->name), q);
                            if (!strcmp(selected.toUtf8().constData(), bios->internal_name))
                                currentIndex = row;

                            rows++;
                        }
                        q++;
                    }

                    biosRows = rows;
                    if (rows > 1)
                        form->addRow(tr(config->description).append(colon), cbox);

                    bios = currentIndex;
                    cbox->setCurrentIndex(currentIndex);

                    cboxBios = cbox;
                    cfgBios  = config;
                    break;
                }
            case CONFIG_SPINNER:
                {
                    auto *spinBox = new QSpinBox();
                    spinBox->setObjectName(config->name);
                    spinBox->setMaximum(config->spinner.max);
                    spinBox->setMinimum(config->spinner.min);
                    if (config->spinner.step > 0)
                        spinBox->setSingleStep(config->spinner.step);
                    spinBox->setValue(value);
                    form->addRow(tr(config->description).append(colon), spinBox);
                    break;
                }
            case CONFIG_FNAME:
                {
                    auto *fileField = new FileField(this);
                    fileField->setObjectName(config->name);
                    fileField->setFileName(selected);
                    fileField->setCreateFile(!!config->default_int);
                    /* Get the actually used part of the filter */
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
                    QString filter = QString(config->file_filter).left(static_cast<int>(strcspn(config->file_filter, "|")));
#else
                    QString filter = QString(config->file_filter).split("|").at(0);
#endif
                    /* Extract the description and the extension list */
                    QRegularExpressionMatch match       = QRegularExpression("(.+) \\((.+)\\)$").match(filter);
                    QString                 description = match.captured(1);
                    QString                 extensions  = match.captured(2);
                    /* Split the extension list up and strip the filename globs */
                    QRegularExpression re("\\*\\.(.*)");
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
                    QStringList extensionList;
                    int         i = 0;
                    while (extensions.section(' ', i, i) != "") {
                        QString extension = re.match(extensions.section(' ', i, i)).captured(1);
                        extensionList.append(extension);
                        i++;
                    }
#else
                    QStringList extensionList = extensions.split(" ");
                    for (int i = 0; i < extensionList.count(); i++)
                        extensionList[i] = re.match(extensionList[i]).captured(1);
#endif
                    fileField->setFilter(tr(description.toUtf8().constData()) % util::DlgFilter(extensionList) % tr("All files") % util::DlgFilter({ "*" }, true));
                    form->addRow(tr(config->description).append(colon), fileField);
                    break;
                }
            case CONFIG_STRING:
                {
                    const auto lineEdit = new QLineEdit;
                    lineEdit->setObjectName(config->name);
                    lineEdit->setCursor(Qt::IBeamCursor);
                    lineEdit->setText(selected);
                    lineEdit->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
                    lineEdit->setMinimumWidth(150);
                    form->addRow(tr(config->description).append(colon), lineEdit);
                    break;
                }
            case CONFIG_SERPORT:
                {
                    auto *cbox = new QComboBox();
                    cbox->setObjectName(config->name);
                    cbox->setMaxVisibleItems(30);
                    auto *model         = cbox->model();
                    int   currentIndex  = 0;
                    auto  serialDevices = enumerateSerialDevices();
                    auto  serialPaths   = serialDevices.keys();
                    QCollator collator(QLocale::English); /* https://github.com/keepassxreboot/keepassxc/issues/11813 - also happens on Windows */
                    collator.setNumericMode(true);
                    std::sort(serialPaths.begin(), serialPaths.end(), collator);
#ifdef Q_OS_WINDOWS
                    if (selected.startsWith("\\\\.\\"))
                        selected.remove(0, 4);
#endif
                    Models::AddEntry(model, tr("None"), "");
                    for (const auto &path : serialPaths) {
                        const int row = Models::AddEntry(model, serialDevices[path], path);
                        if (selected == path)
                            currentIndex = row;
                    }

                    form->addRow(tr(config->description).append(colon), cbox);
                    cbox->setCurrentIndex(currentIndex);
                    break;
                }
            case CONFIG_STRHEX:
                {
                    const auto lineEdit = new QLineEdit;
                    lineEdit->setObjectName(config->name);
                    // The mask fixes both the character set and the width: four hex digits,
                    // which is exactly the 16-bit range this type is meant to hold.
                    lineEdit->setInputMask("HHHH;0");
                    lineEdit->setText(selected.toUpper());
                    lineEdit->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
                    form->addRow(tr(config->description).append(colon), lineEdit);
                    break;
                }
            case CONFIG_MAC:
                {
                    // QHBoxLayout for the line edit widget and the generate button
                    const auto hboxLayout     = new QHBoxLayout();
                    const auto generateButton = new QPushButton(tr("Generate"));
                    const auto lineEdit       = new QLineEdit;
                    // Allow the line edit to expand and fill available space
                    lineEdit->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
                    lineEdit->setInputMask("HH:HH:HH;0");
                    lineEdit->setObjectName(config->name);
                    // Display the current or generated MAC in uppercase
                    // When stored it will be converted to lowercase
                    if (config_get_mac(device_context->name, config->name,
                                       config->default_int)
                        & 0xFF000000) {
                        lineEdit->setText(QString::asprintf("%02X:%02X:%02X", random_generate(),
                                                            random_generate(), random_generate()));
                    } else {
                        auto current_mac = QString(config_get_string(device_context->name, config->name,
                                                                     const_cast<char *>(config->default_string)));
                        lineEdit->setText(current_mac.toUpper());
                    }
                    // Action for the generate button
                    connect(generateButton, &QPushButton::clicked, [lineEdit] {
                        lineEdit->setText(QString::asprintf("%02X:%02X:%02X", random_generate(),
                                                            random_generate(), random_generate()));
                    });
                    hboxLayout->addWidget(lineEdit);
                    hboxLayout->addWidget(generateButton);
                    form->addRow(tr(config->description).append(colon), hboxLayout);
                    break;
                }
        }
        ++config;
    }

    if ((cboxBios != nullptr) && (cfgBios != nullptr) && (bios != -1) &&
        (((cfgMemory != nullptr) && (cboxMemory != nullptr)) ||
         ((cfgBootlogo != nullptr) && (cboxBootlogo != nullptr))))
        connect(cboxBios, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DeviceConfigPanel::onBiosChanged);

    onBiosChanged(bios);
}

int
DeviceConfigPanel::apply()
{
    const QString    blank       = "";
    int              has_changed = 0;
    device_context_t ctx;

    if (dev->config == nullptr)
        return 0;

    device_set_context(&ctx, dev, inst);

    const _device_config_ *config = dev->config;
    while (config->type != CONFIG_END) {
        const int config_type       = config->type & CONFIG_TYPE_MASK;
        const int config_major_type = (config_type >> CONFIG_SHIFT) << CONFIG_SHIFT;

        int  value    = 0;
        auto selected = blank;

        switch (config_major_type) {
            default:
                break;
            case CONFIG_TYPE_INT:
                value = config_get_int(ctx.name, const_cast<char *>(config->name),
                                       config->default_int);
                break;
            case CONFIG_TYPE_HEX16:
                value = config_get_hex16(ctx.name, const_cast<char *>(config->name),
                                         config->default_int);
                break;
            case CONFIG_TYPE_HEX20:
                value = config_get_hex20(ctx.name, const_cast<char *>(config->name),
                                         config->default_int);
                break;
            case CONFIG_TYPE_STRING:
            case CONFIG_TYPE_STRHEX:
                selected = config_get_string(ctx.name, const_cast<char *>(config->name),
                                             const_cast<char *>(config->default_string));
                break;
        }

        switch (config->type) {
            default:
                break;
            case CONFIG_BINARY:
                {
                    const auto *cbox = findChild<QCheckBox *>(config->name);
                    if (cbox == nullptr)
                        break;
                    if (value != (cbox->isChecked() ? 1 : 0)) {
                        config_set_int(ctx.name, const_cast<char *>(config->name), cbox->isChecked() ? 1 : 0);
                        has_changed |= 1;
                    }
                    break;
                }
            case CONFIG_MIDI_OUT:
            case CONFIG_MIDI_IN:
            case CONFIG_INT:
            case CONFIG_SELECTION:
                {
                    auto *cbox = findChild<QComboBox *>(config->name);
                    if ((cbox == nullptr) || (cbox->currentIndex() < 0))
                        break;
                    value = config_get_int(ctx.name, const_cast<char *>(config->name),
                                           config->default_int);
                    if (value != cbox->currentData().toInt()) {
                        config_set_int(ctx.name, const_cast<char *>(config->name), cbox->currentData().toInt());
                        has_changed |= 1;
                    }
                    break;
                }
            case CONFIG_BIOS:
                {
                    if (biosRows > 1) {
                        auto *cbox = findChild<QComboBox *>(config->name);
                        if ((cbox == nullptr) || (cbox->currentIndex() < 0))
                            break;
                        int   idx  = cbox->currentData().toInt();
                        if (strcmp(selected.toUtf8(), const_cast<char *>(config->bios[idx].internal_name))) {
                            config_set_string(ctx.name, const_cast<char *>(config->name), const_cast<char *>(config->bios[idx].internal_name));
                            has_changed |= 1;
                        }
                    }
                    break;
                }
            case CONFIG_SERPORT:
                {
                    auto *cbox = findChild<QComboBox *>(config->name);
                    if ((cbox == nullptr) || (cbox->currentIndex() < 0))
                        break;
                    auto  path = cbox->currentData().toString().toUtf8();
                    if (strcmp(selected.toUtf8(), path)) {
                        config_set_string(ctx.name, const_cast<char *>(config->name), path);
                        has_changed |= 1;
                    }
                    break;
                }
            case CONFIG_STRING:
                {
                    auto *lineEdit = findChild<QLineEdit *>(config->name);
                    if (lineEdit == nullptr)
                        break;
                    if (strcmp(selected.toUtf8(), lineEdit->text().toUtf8())) {
                        config_set_string(ctx.name, const_cast<char *>(config->name), lineEdit->text().toUtf8());
                        has_changed |= 1;
                    }
                    break;
                }
            case CONFIG_HEX16:
                {
                    auto *cbox = findChild<QComboBox *>(config->name);
                    if ((cbox == nullptr) || (cbox->currentIndex() < 0))
                        break;
                    if (value != cbox->currentData().toInt()) {
                        config_set_hex16(ctx.name, const_cast<char *>(config->name), cbox->currentData().toInt());
                        has_changed |= 1;
                    }
                    break;
                }
            case CONFIG_HEX20:
                {
                    auto *cbox = findChild<QComboBox *>(config->name);
                    if ((cbox == nullptr) || (cbox->currentIndex() < 0))
                        break;
                    if (value != cbox->currentData().toInt()) {
                        config_set_hex20(ctx.name, const_cast<char *>(config->name), cbox->currentData().toInt());
                        has_changed |= 1;
                    }
                    break;
                }
            case CONFIG_FNAME:
                {
                    auto *fbox     = findChild<FileField *>(config->name);
                    if (fbox == nullptr)
                        break;
                    auto  fileName = fbox->fileName().toUtf8();
                    if (strcmp(selected.toUtf8().data(), fileName.data())) {
                        config_set_string(ctx.name, const_cast<char *>(config->name), fileName.data());
                        has_changed |= 1;
                    }
                    break;
                }
            case CONFIG_SPINNER:
                {
                    auto *spinBox = findChild<QSpinBox *>(config->name);
                    if (spinBox == nullptr)
                        break;
                    if (value != spinBox->value()) {
                        config_set_int(ctx.name, const_cast<char *>(config->name), spinBox->value());
                        has_changed |= 1;
                    }
                    break;
                }
            case CONFIG_STRHEX:
                {
                    const auto *lineEdit = findChild<QLineEdit *>(config->name);
                    if (lineEdit == nullptr)
                        break;
                    // Store the hex value in uppercase; displayText() skips the mask's
                    // unfilled placeholders, so a partly typed value is not padded out.
                    auto hexText = lineEdit->displayText().toUpper();
                    if (strcasecmp(config_get_string(ctx.name, config->name, const_cast<char *>(config->default_string)),
                                   hexText.toUtf8().constData())) {
                        config_set_string(ctx.name, config->name, hexText.toUtf8().constData());
                        has_changed |= 1;
                    }
                    break;
                }
            case CONFIG_MAC:
                {
                    const auto *lineEdit = findChild<QLineEdit *>(config->name);
                    if (lineEdit == nullptr)
                        break;
                    // Store the mac address as lowercase
                    auto macText     = lineEdit->displayText().toLower();
                    int  mac_changed = 0;
                    if (config_get_mac(ctx.name, config->name,
                                       config->default_int) & 0xFF000000)
                        mac_changed |= 1;
                    else
                        mac_changed |= strcasecmp(config_get_string(ctx.name, config->name,
                                                                    const_cast<char *>(config->default_string)),
                                                                    macText.toUtf8().constData());
                    if (mac_changed) {
                        config_set_string(ctx.name, config->name, macText.toUtf8().constData());
                        has_changed |= 1;
                    }
                    break;
                }
        }
        config++;
    }

    return has_changed;
}

int
DeviceConfig::ConfigureDevice(const _device_ *device, int instance, Settings *settings)
{
    DeviceConfig dc(settings);
    dc.setWindowTitle(tr("%1 Device Configuration").arg(DeviceName(device, device->internal_name, -1)));

    const auto device_label = new QLabel(DeviceName(device, device->internal_name, -1));
    device_label->setAlignment(Qt::AlignCenter);
    dc.ui->formLayout->addRow(device_label);
    const auto line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    dc.ui->formLayout->addRow(line);

    auto *panel = new DeviceConfigPanel(device, instance, settings, &dc);
    dc.ui->formLayout->addRow(panel);

    dc.setFixedSize(dc.minimumSizeHint());

    if (dc.exec() != QDialog::Accepted)
        return 0;

    const int has_changed = panel->apply();
    DeviceConfigPanel::reloadOthers(panel);

    return has_changed;
}

QString
DeviceConfig::DeviceName(const _device_ *device, const char *internalName, const int bus)
{
    if (QStringLiteral("none") == internalName)
        return tr("None");
    else if (QStringLiteral("internal") == internalName)
        return tr("Internal device");
    else if (device == nullptr)
        return "";
    else {
        if (internalName == nullptr)
            internalName = device->internal_name;

        char        temp[512];
        const char *tempbus;
        if (bus == 1) {
            device_get_name(device, -1, temp);
            tempbus = device_get_bus_name(device);
            if (tempbus != nullptr)
                return QString("[%1] %2").arg(tr(tempbus), tr((const char *) temp));
            else
                return tr((const char *) temp);
        } else {
            device_get_name(device, bus, temp);
            const char *m = device_get_machine(device);
            if (m == NULL)
                return tr((const char *) temp).remove(" (On-Board)", Qt::CaseInsensitive).remove(" On-Board", Qt::CaseInsensitive);
            else
                return tr((const char *) temp).remove(" (On-Board)", Qt::CaseInsensitive).remove(" On-Board", Qt::CaseInsensitive).remove(QString(" (%1)").arg(m), Qt::CaseInsensitive);
        }
    }
}


void
DeviceConfigPanel::onBiosChanged(int index)
{
    if ((cboxMemory != nullptr) && (cboxBios != nullptr) &&
        (cfgMemory != nullptr)  && (cfgBios != nullptr)) {
        int      idx        = index; /* cboxBios->currentData().toInt(); */
        int      mem        = cboxMemory->currentData().toInt();
        char *   bios_name  = const_cast<char *>(cfgBios->bios[idx].internal_name);
        uint64_t bios_flags = device_get_bios_flags(dev, bios_name);
        uint16_t min_mem    = 0;
        uint16_t max_mem    = 65535;
        int      rows       = 0;

        if (bios_flags & BIOS_LIMIT_MIN_MEMORY)
            min_mem = (bios_flags & 0xff);

        if (bios_flags & BIOS_LIMIT_MAX_MEMORY)
            max_mem = ((bios_flags >> 8) & 0xff);

        if (bios_flags & BIOS_FIXED_MEMORY)
            min_mem = max_mem = cfgMemory->default_int;

        mem = MAX(mem, min_mem);    /* No less than minimum memory. */
        mem = MIN(mem, max_mem);    /* No more than maximum memory. */

        auto *model        = cboxMemory->model();
        int   removeRows   = model->rowCount();
        int   currentIndex = -1;

        cboxMemory->setCurrentIndex(-1);

        for (auto *sel = cfgMemory->selection; (sel != nullptr) && (sel->description != nullptr) &&
                                                (strlen(sel->description) > 0); ++sel) {
            if ((sel->value >= min_mem) && (sel->value <= max_mem)) {
                int row = Models::AddEntry(model, tr(sel->description), sel->value);

                if (sel->value == mem)
                    currentIndex = row - removeRows;

                rows++;
            }
        }

        model->removeRows(0, removeRows);

        cboxMemory->setCurrentIndex(currentIndex);

        if (rows >= 2)
            cboxMemory->setEnabled(true);
        else
            cboxMemory->setEnabled(false);
    }

    if ((cboxMemory2 != nullptr) && (cboxBios != nullptr) &&
        (cfgMemory2 != nullptr)  && (cfgBios != nullptr)) {
        int      idx        = index; /* cboxBios->currentData().toInt(); */
        int      mem        = cboxMemory2->currentData().toInt();
        char *   bios_name  = const_cast<char *>(cfgBios->bios[idx].internal_name);
        uint64_t bios_flags = device_get_bios_flags(dev, bios_name);
        uint16_t min_mem    = 0;
        uint16_t max_mem    = 65535;
        int      rows       = 0;

        if (bios_flags & BIOS_LIMIT_MIN_MEMORY_2)
            min_mem = ((bios_flags >> 16) & 0xff);

        if (bios_flags & BIOS_LIMIT_MAX_MEMORY_2)
            max_mem = ((bios_flags >> 24) & 0xff);

        mem = MAX(mem, min_mem);    /* No less than minimum memory. */
        mem = MIN(mem, max_mem);    /* No more than maximum memory. */

        auto *model        = cboxMemory2->model();
        int   removeRows   = model->rowCount();
        int   currentIndex = -1;

        cboxMemory2->setCurrentIndex(-1);

        for (auto *sel = cfgMemory2->selection; (sel != nullptr) && (sel->description != nullptr) &&
                                                (strlen(sel->description) > 0); ++sel) {
            if ((sel->value >= min_mem) && (sel->value <= max_mem)) {
                int row = Models::AddEntry(model, tr(sel->description), sel->value);

                if (sel->value == mem)
                    currentIndex = row - removeRows;

                rows++;
            }
        }

        model->removeRows(0, removeRows);

        cboxMemory2->setCurrentIndex(currentIndex);

        if (rows >= 2)
            cboxMemory2->setEnabled(true);
        else
            cboxMemory2->setEnabled(false);
    }

    if ((cboxBootlogo != nullptr) && (cboxBios != nullptr) &&
        (cfgBootlogo != nullptr) && (cfgBios != nullptr) &&
        (dev != nullptr) && !strcmp(dev->internal_name, "in530") &&
        (cboxBios->currentIndex() >= 0)) {
        const int   bios_idx  = cboxBios->currentData().toInt();
        const char *bios_name = cfgBios->bios[bios_idx].internal_name;
        const int   selector  = (cboxBootlogo->currentIndex() >= 0)
                                   ? cboxBootlogo->currentData().toInt()
                                   : cfgBootlogo->default_int;

        cboxBootlogo->clear();

        auto add_logo = [this](const char *description, int value) {
            Models::AddEntry(cboxBootlogo->model(), tr(description), value);
        };

        add_logo("Disabled", 4);

        if (!strcmp(bios_name, "in530_pb_111j")) {
            add_logo("Packard Bell", 1);
            add_logo("NEC",          2);
            add_logo("PowerMate",    3);
        } else if (!strcmp(bios_name, "in530_pb_129")) {
            add_logo("Packard Bell", 1);
            add_logo("Japaq",        2);
            add_logo("PowerMate",    3);
        } else  {
			/* Only one logo */
            add_logo("Enabled", 1);
        }

        int current_index = cboxBootlogo->findData(selector);
        if (current_index < 0)
            current_index = cboxBootlogo->findData(cfgBootlogo->default_int);
        cboxBootlogo->setCurrentIndex(current_index);
    }
}

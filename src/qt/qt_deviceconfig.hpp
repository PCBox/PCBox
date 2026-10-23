#ifndef QT_DEVICECONFIG_HPP
#define QT_DEVICECONFIG_HPP

#include <QDialog>
#include <QHash>
#include <QVariant>

#include "qt_settings.hpp"
#include "qt_settings_completer.hpp"

extern "C" {
struct _device_;
struct _device_config_;
}

namespace Ui {
class DeviceConfig;
}

class Settings;
class QComboBox;
class QFormLayout;

/* A device's options as a form, read from its section of the
   configuration: the Configure dialog shows one, and a settings page can
   show one inline for each card it has. apply() writes back what has been
   changed. */
class DeviceConfigPanel : public QWidget {
    Q_OBJECT

public:
    DeviceConfigPanel(const _device_ *device, int instance, Settings *settings, QWidget *parent = nullptr);
    ~DeviceConfigPanel() override;

    int  apply();
    bool hasPendingEdits() const;
    void reload();
    bool isEmpty() const;

    const _device_ *device() const { return dev; }
    int             instance() const { return inst; }

    /* The slot an EISA card is set to in the panel, or -1 without one. */
    int slotChoice() const;

    /* The panels open now, and what each would write: Settings applies
       those on its pages when it is accepted. */
    static const QList<DeviceConfigPanel *> &panels();
    static int                              applyAll(const QWidget *within);
    static void                             reloadOthers(const DeviceConfigPanel *from);

private:
    const _device_ *dev;
    int             inst;
    Settings       *settings;
    QFormLayout    *form;
    QHash<QWidget *, QVariant> initialValues;
    QComboBox      *cboxSlot = nullptr;

    QComboBox              *cboxMemory    = nullptr;
    QComboBox              *cboxMemory2   = nullptr;
    QComboBox              *cboxBios      = nullptr;
    QComboBox              *cboxBootlogo  = nullptr;
    const _device_config_  *cfgMemory     = nullptr;
    const _device_config_  *cfgMemory2    = nullptr;
    const _device_config_  *cfgBios       = nullptr;
    const _device_config_  *cfgBootlogo   = nullptr;
    int                     biosRows      = 0;

    void build();
    void processConfig(void *dc, const void *c, bool is_dep);
    void onBiosChanged(int index);
};

class DeviceConfig : public QDialog {
    Q_OBJECT

public:
    explicit DeviceConfig(QWidget *parent = nullptr);
    ~DeviceConfig() override;

    static int     ConfigureDevice(const _device_ *device, int instance = 0,
                                   Settings *settings = qobject_cast<Settings *>(Settings::settings));
    static QString DeviceName(const _device_ *device, const char *internalName = nullptr, int bus = 0);

private:
    Ui::DeviceConfig *ui;
};

#endif // QT_DEVICECONFIG_HPP

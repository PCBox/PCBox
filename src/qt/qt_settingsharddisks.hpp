#ifndef QT_SETTINGSHARDDISKS_HPP
#define QT_SETTINGSHARDDISKS_HPP

#include <QWidget>

#include <functional>

namespace Ui {
class SettingsHarddisks;
}

class SettingsHarddisks : public QWidget {
    Q_OBJECT

public:
    explicit SettingsHarddisks(QWidget *parent = nullptr);
    ~SettingsHarddisks();
    void reloadBusChannels();
    void refreshChannelNames();

    int  changed();

    void restore();
    void save(int soft);

    /* For the new layout: a disk added on a channel, through the usual
       window; the one on a channel selected, or taken out. */
    bool addDrive(bool existing, int bus, int channel, const std::function<void()> &makeRoom = nullptr);
    bool selectDrive(int bus, int channel);
    void removeDrive(int bus, int channel);

protected:
    void showEvent(QShowEvent *event) override;

signals:
    void driveChannelChanged();

private slots:
    void on_comboBoxBus_currentIndexChanged(int index);
    void on_comboBoxChannel_currentIndexChanged(int index);
    void on_comboBoxSpeed_currentIndexChanged(int index);
    void on_comboBoxAudio_currentIndexChanged(int index);
    void on_pushButtonConfigure_clicked();

    void on_pushButtonNew_clicked();
    void on_pushButtonExisting_clicked();
    void on_pushButtonRemove_clicked();

    void onTableRowChanged(const QModelIndex &current);

private:
    Ui::SettingsHarddisks *ui;
    void                   enableCurrentlySelectedChannel();
    void                   populateAudioProfiles();
    void                   addRow(QAbstractItemModel *model, void *priv);
    void                   addDriveFromDialog(Ui::SettingsHarddisks *ui, const HarddiskDialog &dlg);

    int                    org_rows = 0;

    SettingsCompleter     *scSpeed;
};

#endif // QT_SETTINGSHARDDISKS_HPP

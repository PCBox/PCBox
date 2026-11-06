#pragma once

#include <cstdint>
#include "qt_settings_bus_tracking.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/hdd.h>
}

class QString;
class QAbstractItemModel;
class QComboBox;
class SettingsBusTracking;
class SettingsCompleter;
struct bus_owner_t;
struct ide_conflict_t;

namespace Harddrives {
/* A drive set to "(Disconnected)" has bus_type HDD_BUS_DISCONNECTED (the
   same value as CDROM_BUS_DISCONNECTED) and keeps its interface in
   disconnected_bus. The pages show it as that interface with the
   "(Disconnected)" channel, CHANNEL_NONE. */
template <typename Drive>
uint8_t
configuredBus(const Drive &drive, uint8_t fallback)
{
    if (drive.bus_type != HDD_BUS_DISCONNECTED)
        return drive.bus_type;

    return drive.disconnected_bus ? drive.disconnected_bus : fallback;
}

template <typename Drive>
uint8_t
configuredChannel(const Drive &drive, uint8_t channel)
{
    return (drive.bus_type == HDD_BUS_DISCONNECTED) ? CHANNEL_NONE : channel;
}

template <typename Drive>
void
storeConnection(Drive &drive, uint8_t bus, uint8_t channel)
{
    drive.disconnected_bus = (bus && (channel == CHANNEL_NONE)) ? bus : 0;
    drive.bus_type         = drive.disconnected_bus ? HDD_BUS_DISCONNECTED : bus;
}

void                        populateBuses(QAbstractItemModel *model);
void                        populateCDROMBuses(QAbstractItemModel *model);
void                        populateRemovableBuses(QAbstractItemModel *model);
/* lptBridges: on bus LPT, list each port once per bridge (LptBridgesHdd),
   or once per bridge a CD-ROM drive can sit behind (LptBridgesCdrom). */
enum { LptBridgesNone = 0, LptBridgesHdd = 1, LptBridgesCdrom = 2 };
void                        populateBusChannels(QAbstractItemModel *model, int bus, SettingsBusTracking *sbt = nullptr, int lptBridges = LptBridgesNone);
/* Hard disks on bus LPT: one channel per port and bridge, port | bridge << 2. */
QString                     lptBridgeName(int bridge);
bool                        lptPortEnabled(int port);
void                        populateSpeeds(QAbstractItemModel *model, SettingsCompleter *sc, int bus);
QString                     BusChannelName(uint8_t bus, uint8_t channel);
int                         idePlan(bus_owner_t *owners, ide_conflict_t *conflicts = nullptr, int *conflictCount = nullptr);
int                         scsiPlan(bus_owner_t *owners);
QString                     ownerName(int bus, const bus_owner_t *owners, int count,
                                      const ide_conflict_t *conflicts = nullptr, int conflictCount = 0);
QString                     conflictText(int index, const bus_owner_t *owners, int count,
                                         const ide_conflict_t *conflicts, int conflictCount);
void                        refreshBusNames(QAbstractItemModel *model);
void                        widenPopup(QComboBox *cbox);
inline SettingsBusTracking *busTrackClass = nullptr;
};

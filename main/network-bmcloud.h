#pragma once
#include "stdbool.h"
#include "stdint.h"

typedef enum
{
    CloudDeviceStateOffline,
    CloudDeviceStateOnline,
    CloudDeviceStateError,
    CloudDeviceStateBusy,
} CloudDeviceStateType;

void network_bmcloud_set_status(CloudDeviceStateType state);

void network_bmcloud_init(void);

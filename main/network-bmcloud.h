#pragma once
#include "stdbool.h"
#include "stdint.h"

#define DEVICE_NAME_MAX 64

typedef enum
{
    CloudDeviceStateOffline,
    CloudDeviceStateOnline,
    CloudDeviceStateError,
    CloudDeviceStateBusy,
} CloudDeviceStateType;

typedef struct
{
    int64_t              id;
    char                 name[DEVICE_NAME_MAX];
    CloudDeviceStateType state;
    int64_t              updated_at;
} CloudDeviceType;

bool network_bmcloud_try_connect(CloudDeviceType *out_device);

void network_bmcloud_set_status(CloudDeviceStateType state);

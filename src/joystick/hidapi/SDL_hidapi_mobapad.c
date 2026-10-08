/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_internal.h"

#ifdef SDL_JOYSTICK_HIDAPI

#include "../SDL_sysjoystick.h"
#include "SDL_hidapijoystick_c.h"

#ifdef SDL_JOYSTICK_HIDAPI_MOBAPAD

#define MOBAPAD_ML35_BODY_OFFSET 5
#define MOBAPAD_ML35_PACKET_SIZE 64
#define MOBAPAD_ML35_ACCEL_OFFSET 10
#define MOBAPAD_ML35_GYRO_OFFSET 16

enum
{
    SDL_GAMEPAD_BUTTON_MOBAPAD_M1 = 11,
    SDL_GAMEPAD_BUTTON_MOBAPAD_M2,
    SDL_GAMEPAD_BUTTON_MOBAPAD_M3,
    SDL_GAMEPAD_BUTTON_MOBAPAD_M4,
    SDL_GAMEPAD_BUTTON_MOBAPAD_CAPTURE,
    SDL_GAMEPAD_BUTTON_MOBAPAD_SET,
    SDL_GAMEPAD_NUM_MOBAPAD_BUTTONS,
};

typedef struct
{
    bool last_state_initialized;
    bool sensors_enabled;
    float accel_scale;
    float gyro_scale;
    Uint8 last_state[MOBAPAD_ML35_PACKET_SIZE];
} SDL_DriverMobapad_Context;

static void HIDAPI_DriverMobapad_RegisterHints(SDL_HintCallback callback, void *userdata)
{
    SDL_AddHintCallback(SDL_HINT_JOYSTICK_HIDAPI_MOBAPAD, callback, userdata);
}

static void HIDAPI_DriverMobapad_UnregisterHints(SDL_HintCallback callback, void *userdata)
{
    SDL_RemoveHintCallback(SDL_HINT_JOYSTICK_HIDAPI_MOBAPAD, callback, userdata);
}

static bool HIDAPI_DriverMobapad_IsEnabled(void)
{
    return SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_MOBAPAD, SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI, SDL_HIDAPI_DEFAULT));
}

static bool HIDAPI_DriverMobapad_IsSupportedDevice(SDL_HIDAPI_Device *device, const char *name, SDL_GamepadType type, Uint16 vendor_id, Uint16 product_id, Uint16 version, int interface_number, int interface_class, int interface_subclass, int interface_protocol)
{
    return SDL_IsJoystickMobapadController(vendor_id, product_id);
}

static bool HIDAPI_DriverMobapad_InitDevice(SDL_HIDAPI_Device *device)
{
    SDL_DriverMobapad_Context *ctx;

    ctx = (SDL_DriverMobapad_Context *)SDL_calloc(1, sizeof(*ctx));
    if (!ctx) {
        return false;
    }
    device->context = ctx;

    HIDAPI_SetDeviceName(device, "mobapad-ml35");
    return HIDAPI_JoystickConnected(device, NULL);
}

static int HIDAPI_DriverMobapad_GetDevicePlayerIndex(SDL_HIDAPI_Device *device, SDL_JoystickID instance_id)
{
    return -1;
}

static void HIDAPI_DriverMobapad_SetDevicePlayerIndex(SDL_HIDAPI_Device *device, SDL_JoystickID instance_id, int player_index)
{
}

static bool HIDAPI_DriverMobapad_OpenJoystick(SDL_HIDAPI_Device *device, SDL_Joystick *joystick)
{
    SDL_DriverMobapad_Context *ctx = (SDL_DriverMobapad_Context *)device->context;

    SDL_AssertJoysticksLocked();

    SDL_zeroa(ctx->last_state);
    ctx->last_state_initialized = false;
    ctx->sensors_enabled = false;
    ctx->accel_scale = 4.0f * SDL_STANDARD_GRAVITY / 32768.0f;
    ctx->gyro_scale = SDL_PI_F / (180.0f * 16.0f);
    joystick->nbuttons = SDL_GAMEPAD_NUM_MOBAPAD_BUTTONS;
    joystick->naxes = SDL_GAMEPAD_AXIS_COUNT;
    joystick->nhats = 1;
    SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_ACCEL, 0.0f);
    SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_GYRO, 0.0f);
    return true;
}

static bool HIDAPI_DriverMobapad_RumbleJoystick(SDL_HIDAPI_Device *device, SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    return SDL_Unsupported();
}

static bool HIDAPI_DriverMobapad_RumbleJoystickTriggers(SDL_HIDAPI_Device *device, SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble)
{
    return SDL_Unsupported();
}

static Uint32 HIDAPI_DriverMobapad_GetJoystickCapabilities(SDL_HIDAPI_Device *device, SDL_Joystick *joystick)
{
    return 0;
}

static bool HIDAPI_DriverMobapad_SetJoystickLED(SDL_HIDAPI_Device *device, SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static bool HIDAPI_DriverMobapad_SendJoystickEffect(SDL_HIDAPI_Device *device, SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static bool HIDAPI_DriverMobapad_SetJoystickSensorsEnabled(SDL_HIDAPI_Device *device, SDL_Joystick *joystick, bool enabled)
{
    SDL_DriverMobapad_Context *ctx = (SDL_DriverMobapad_Context *)device->context;

    ctx->sensors_enabled = enabled;
    return true;
}

static void HIDAPI_DriverMobapad_HandleStatePacket(SDL_Joystick *joystick, SDL_DriverMobapad_Context *ctx, const Uint8 *data, int size)
{
    const Uint8 *buttons;
    const Uint8 *last;
    bool is_initial_packet;
    Uint8 hat = SDL_HAT_CENTERED;
    Uint64 timestamp = SDL_GetTicksNS();

    if (size < MOBAPAD_ML35_PACKET_SIZE) {
        return;
    }

    buttons = data + MOBAPAD_ML35_BODY_OFFSET;
    last = ctx->last_state + MOBAPAD_ML35_BODY_OFFSET;
    is_initial_packet = !ctx->last_state_initialized;

    if (is_initial_packet || last[0] != buttons[0]) {
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_MOBAPAD_M1, (buttons[0] & 0x01) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_MOBAPAD_M2, (buttons[0] & 0x02) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_BACK, (buttons[0] & 0x04) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_START, (buttons[0] & 0x08) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_SOUTH, (buttons[0] & 0x10) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_EAST, (buttons[0] & 0x20) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_WEST, (buttons[0] & 0x40) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_NORTH, (buttons[0] & 0x80) != 0);
    }

    if (is_initial_packet || last[1] != buttons[1]) {
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, (buttons[1] & 0x01) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, (buttons[1] & 0x02) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_LEFT_STICK, (buttons[1] & 0x10) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_RIGHT_STICK, (buttons[1] & 0x20) != 0);
    }

    if (is_initial_packet || last[2] != buttons[2]) {
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_MOBAPAD_M3, (buttons[2] & 0x20) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_GUIDE, (buttons[2] & 0x40) != 0);
    }

    if (is_initial_packet || last[3] != buttons[3]) {
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_MOBAPAD_SET, (buttons[3] & 0x01) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_MOBAPAD_M4, (buttons[3] & 0x02) != 0);
        SDL_SendJoystickButton(timestamp, joystick, SDL_GAMEPAD_BUTTON_MOBAPAD_CAPTURE, (buttons[3] & 0x04) != 0);
    }

    if (is_initial_packet || last[1] != buttons[1] || last[2] != buttons[2]) {
        if (buttons[1] & 0x40) {
            hat |= SDL_HAT_UP;
        }
        if (buttons[1] & 0x80) {
            hat |= SDL_HAT_DOWN;
        }
        if (buttons[2] & 0x01) {
            hat |= SDL_HAT_LEFT;
        }
        if (buttons[2] & 0x02) {
            hat |= SDL_HAT_RIGHT;
        }
        SDL_SendJoystickHat(timestamp, joystick, 0, hat);
    }

#define READ_MOBAPAD_STICK(offset) \
    (buttons[offset] == 0x80 ? 0 : (Sint16)HIDAPI_RemapVal((float)((int)buttons[offset] - 0x80), 0x00 - 0x80, 0xff - 0x80, SDL_MIN_SINT16, SDL_MAX_SINT16))
    if (is_initial_packet || last[4] != buttons[4]) {
        SDL_SendJoystickAxis(timestamp, joystick, SDL_GAMEPAD_AXIS_LEFTX, READ_MOBAPAD_STICK(4));
    }
    if (is_initial_packet || last[5] != buttons[5]) {
        SDL_SendJoystickAxis(timestamp, joystick, SDL_GAMEPAD_AXIS_LEFTY, READ_MOBAPAD_STICK(5));
    }
    if (is_initial_packet || last[6] != buttons[6]) {
        SDL_SendJoystickAxis(timestamp, joystick, SDL_GAMEPAD_AXIS_RIGHTX, READ_MOBAPAD_STICK(6));
    }
    if (is_initial_packet || last[7] != buttons[7]) {
        SDL_SendJoystickAxis(timestamp, joystick, SDL_GAMEPAD_AXIS_RIGHTY, READ_MOBAPAD_STICK(7));
    }
#undef READ_MOBAPAD_STICK

    if (is_initial_packet || last[8] != buttons[8]) {
        SDL_SendJoystickAxis(timestamp, joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, (Sint16)(((int)buttons[8] * 257) - 32768));
    }
    if (is_initial_packet || last[9] != buttons[9]) {
        SDL_SendJoystickAxis(timestamp, joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, (Sint16)(((int)buttons[9] * 257) - 32768));
    }

    if (ctx->sensors_enabled) {
        float accel[3];
        float gyro[3];

        // The accelerometer scale follows GameSir's assumed +/-4g range.
        accel[0] = (float)LOAD16(buttons[MOBAPAD_ML35_ACCEL_OFFSET + 0], buttons[MOBAPAD_ML35_ACCEL_OFFSET + 1]) * ctx->accel_scale;
        accel[1] = (float)LOAD16(buttons[MOBAPAD_ML35_ACCEL_OFFSET + 2], buttons[MOBAPAD_ML35_ACCEL_OFFSET + 3]) * ctx->accel_scale;
        accel[2] = (float)LOAD16(buttons[MOBAPAD_ML35_ACCEL_OFFSET + 4], buttons[MOBAPAD_ML35_ACCEL_OFFSET + 5]) * ctx->accel_scale;
        SDL_SendJoystickSensor(timestamp, joystick, SDL_SENSOR_ACCEL, timestamp, accel, SDL_arraysize(accel));

        // ML35 reports signed, little-endian samples; the scale follows GameSir's 1/16 degree per second per count.
        gyro[0] = (float)LOAD16(buttons[MOBAPAD_ML35_GYRO_OFFSET + 0], buttons[MOBAPAD_ML35_GYRO_OFFSET + 1]) * ctx->gyro_scale;
        gyro[1] = (float)LOAD16(buttons[MOBAPAD_ML35_GYRO_OFFSET + 2], buttons[MOBAPAD_ML35_GYRO_OFFSET + 3]) * ctx->gyro_scale;
        gyro[2] = (float)LOAD16(buttons[MOBAPAD_ML35_GYRO_OFFSET + 4], buttons[MOBAPAD_ML35_GYRO_OFFSET + 5]) * ctx->gyro_scale;
        SDL_SendJoystickSensor(timestamp, joystick, SDL_SENSOR_GYRO, timestamp, gyro, SDL_arraysize(gyro));
    }

    SDL_memcpy(ctx->last_state, data, MOBAPAD_ML35_PACKET_SIZE);
    ctx->last_state_initialized = true;
}

static bool HIDAPI_DriverMobapad_UpdateDevice(SDL_HIDAPI_Device *device)
{
    SDL_DriverMobapad_Context *ctx = (SDL_DriverMobapad_Context *)device->context;
    SDL_Joystick *joystick;
    Uint8 data[USB_PACKET_LENGTH];
    int size = 0;

    if (device->num_joysticks == 0) {
        return false;
    }
    joystick = SDL_GetJoystickFromID(device->joysticks[0]);

    while ((size = SDL_hid_read_timeout(device->dev, data, sizeof(data), 0)) > 0) {
        if (joystick) {
            HIDAPI_DriverMobapad_HandleStatePacket(joystick, ctx, data, size);
        }
    }

    if (size < 0) {
        HIDAPI_JoystickDisconnected(device, device->joysticks[0]);
    }
    return size >= 0;
}

static void HIDAPI_DriverMobapad_CloseJoystick(SDL_HIDAPI_Device *device, SDL_Joystick *joystick)
{
}

static void HIDAPI_DriverMobapad_FreeDevice(SDL_HIDAPI_Device *device)
{
}

SDL_HIDAPI_DeviceDriver SDL_HIDAPI_DriverMobapad = {
    SDL_HINT_JOYSTICK_HIDAPI_MOBAPAD,
    true,
    HIDAPI_DriverMobapad_RegisterHints,
    HIDAPI_DriverMobapad_UnregisterHints,
    HIDAPI_DriverMobapad_IsEnabled,
    HIDAPI_DriverMobapad_IsSupportedDevice,
    HIDAPI_DriverMobapad_InitDevice,
    HIDAPI_DriverMobapad_GetDevicePlayerIndex,
    HIDAPI_DriverMobapad_SetDevicePlayerIndex,
    HIDAPI_DriverMobapad_UpdateDevice,
    HIDAPI_DriverMobapad_OpenJoystick,
    HIDAPI_DriverMobapad_RumbleJoystick,
    HIDAPI_DriverMobapad_RumbleJoystickTriggers,
    HIDAPI_DriverMobapad_GetJoystickCapabilities,
    HIDAPI_DriverMobapad_SetJoystickLED,
    HIDAPI_DriverMobapad_SendJoystickEffect,
    HIDAPI_DriverMobapad_SetJoystickSensorsEnabled,
    HIDAPI_DriverMobapad_CloseJoystick,
    HIDAPI_DriverMobapad_FreeDevice,
};

#endif // SDL_JOYSTICK_HIDAPI_MOBAPAD

#endif // SDL_JOYSTICK_HIDAPI

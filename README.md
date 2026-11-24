English | [简体中文](README_zh.md)

Join us on discord https://discord.gg/uGUKaD2u8Z

# Open source video transmitter firmware for the FPV community

OpenVTx aims to provide firmware with MSP, SmartAudio and Tramp protocols.  Any protocol can be used on the flight controller and on VTx power up the protocol used is automatically detected by OpenVTx.

From OpenVTx 0.2 and Betaflight 4.4 MSP will be the preferred protocol to use.

Currently SA is fully implemented and test against [Rev. 09](https://www.team-blacksheep.com/tbs_smartaudio_rev09.pdf).  To date IMRC has not released a protocol standard and implementation is based on information found within Betaflight.

# Features

**MSP Control** - MSP is the most common and vender neutral communication protocol.  Like SmartAudio and TRAMP it can be used to set the VTx frequency, power, and pitmode.  It has the added benefit of being able to self configure the VTx Table used in Betaflight.  When the VTx powers up it will query the flight controller and check for a VTx Table.  If it does not exist, or is incorrect, then OpenVTx will send the correct VTx Table to the flight controller.

**Race Mode** - When at a race event and using pitmode on a switch with Betaflight, select the RCE VTx power level.  It will force pitmode on boot and only use 25mW.

# LEDs

### Normal mode

The ```Red``` LED will power on at boot and twinkle when saving changes.  ```Green``` and ```Blue``` indicate current operating mode (band/channel/power) when using MSP, SmartAudio, or the VTx Button.  Not supported with TRAMP due to TRAMP not having a fixed VTx Table.

The VTX mode LED indication logic:

<img src="img/vtxModeIndication.png" width="100%">

When using TRAMP or ``LED_INDICATION_OF_VTX_MODE=0``, ```Red``` LED will power on at boot and twinkle when saving changes.  ```Green``` will turn on when a protocol has been detected e.g. successful communication via TRAMP with the flight controller.  ```Blue``` will blink every time a communication packet is received.

### Error Mode

If OpenVTx detects an error the ```Red``` LED will flash one of the error codes below.

- RTC6705 not detected - 1s on to indicate start of the error code, followed by 3 quick flashes.

# Betaflight Setup

From Betaflight 4.4 MSP will be the preferred protocol to use.  Connect the single VTx telemetry wire as normal to a FC uart Tx pad.  Then select that uart from the OpenVTx Preset.  There is no need to configure the Ports tab for the VTx, as the Preset will handle the Port setup for you.  On power up the VTx will then check and configure the VTx Table.

If using SmartAudio or TRAMP, setup the Ports tab as normal, then pick the SmartAudio/TRAMP option from the OpenVTx Preset.

<img src="img/BF_Preset_1.png" width="40%"><img src="img/BF_Preset_2.png" width="40%">

# Firmware Updating

The online Configurator can be used with Chrome https://openvtx.org/.

Any VTx that is currently connected to a flight controller and working via MSP/SA/TRAMP can be updated.  For newly flashed DIY hardware with just the bootloader flashed, the FC must be setup with SA as that is the default protocol for the bootloader.

- First close any flight controller Configurators.  If open they will connect to the FC and block the update.
- Power the FC and VTx.
- Connect the FC USB.
- Select the Version, Target, and press Flash.

## One-Shot ST-Link Flashing (Bootloader + Firmware)

For DIY or newly flashed GD32F130 hardware, you no longer need to flash the bootloader from `OpenVTx_bootloader` first and then flash the firmware via UART passthrough. This repository bundles the prebuilt bootloader (`src/bootloader/Generic_GD32F130_bootloader.bin`) and a PlatformIO post-build script (`src/scripts/merge_bootloader.py`) that merges the bootloader and application into a single flashable image while keeping full UART passthrough update support.

### Flash Memory Layout (GD32F130G6 - 32 KB)

| Region | Address | Size | Notes |
| --- | --- | --- | --- |
| Bootloader | `0x08000000` – `0x08001FFF` | 8 KB | Bundled `Generic_GD32F130_bootloader.bin` (padded with `0xFF`) |
| Application | `0x08002000` – `0x08007BFF` | 23 KB | Built with `-Wl,--defsym=FLASH_APP_OFFSET=8K` |
| Emulated EEPROM | `0x08007C00` – `0x08007FFF` | 1 KB | Stores VTx settings |

### Usage in PlatformIO (`src/platformio.ini`)

1. **Blank or already unlocked MCU (e.g., DIY boards)**: select `DIY_VTX820_stlink` or `Generic_GD32F130_stlink` and click **Upload** (or run `pio run -d src -e DIY_VTX820_stlink -t upload`).
2. **Read-protected factory VTx (e.g., EWRF E7082VM first-time flash)**: select `DIY_VTX820_stlink_unlock` or `Generic_GD32F130_stlink_unlock` and click **Upload**. This unlocks Flash read protection and performs a mass erase before flashing.
3. **Standalone combined binary**: every build of a `*_stlink` environment also outputs `src/.pio/build/<env>/firmware_combined.bin`, which can be flashed directly at `0x08000000` using STM32CubeProgrammer, ST-Link Utility, or an offline programmer.
4. **Future UART passthrough updates**: after the initial ST-Link flash, use the standard target environment (`DIY_VTX820`, `Generic_GD32F130`, etc.) or https://openvtx.org/ to update firmware over the flight controller UART without touching the bootloader.

> **Note:** To add a one-shot ST-Link environment for another GD32F130 target in `src/platformio.ini`, inherit from the standard 8 KB-offset environment (never `*_develop`):
> ```ini
> [env:<TARGET>_stlink]
> extends = env:<TARGET>, env_gd32f130_stlink
> ```

# RF Power Closed-Loop PID Control & Tuning

RF output power regulation is implemented as a shared closed-loop module (`src/src/powerControl.h` / `src/src/powerControl.c`) using a **Type-C Incremental PID (I-PD)** controller running every `5 ms`:

- **Zero setpoint kick & fast settling**: proportional and derivative terms act on 1st-order EMA-filtered detector voltage (`VPD`), while integral action drives steady-state error to zero.
- **PA protection & anti-ripple**: includes asymmetric per-tick PWM slew-rate limiting (protects high-power PAs such as the 1.6W `DIY_VTX820` from transient overshoot), integral deadband, `0.75 LSB` Schmitt output hysteresis, and bumpless initialization across PitMode / frequency-hop mute transitions.
- **Legacy fallback**: set `-D POWER_CONTROL_MODE=0` in `src/platformio.ini` to switch back to the original `±1` step controller at any time.

### Build-Time PID Parameters (`src/platformio.ini`)

| Macro | Default | Description |
| --- | --- | --- |
| `POWER_CONTROL_MODE` | `1` | `1` = Type-C I-PD PID, `0` = Legacy `±1` step |
| `POWER_PID_KP` | `0.05f` | Proportional damping gain on filtered `VPD` (keep `< 0.12f`) |
| `POWER_PID_KI` | `0.02f` | Integral gain on `VPD` tracking error |
| `POWER_PID_KD` | `0.00f` | Derivative gain on filtered `VPD` |
| `POWER_PID_EMA_ALPHA` | `0.25f` | EMA low-pass filter factor (`0.01..1.0`, suppresses 50/60 Hz video ripple) |
| `POWER_PID_DEADBAND_MV` | `3.0f` | Integral error deadband (`mV`) |
| `POWER_PID_MAX_STEP_DOWN` | `10.0f` | Max PWM decrease per `5 ms` (limits RF power ramp-up rate) |
| `POWER_PID_MAX_STEP_UP` | `15.0f` | Max PWM increase per `5 ms` (limits RF power ramp-down rate) |
| `POWER_PID_HYSTERESIS` | `0.75f` | PWM quantization Schmitt hysteresis (counts) |

### Live UART Tuning Without Reflashing (`python/pid_tuner.py`)

1. Flash a tuning build (`DIY_VTX820_tune` / `Generic_GD32F130_tune` via UART passthrough, or `*_tune_stlink` via ST-Link), which exposes a `57600 8N1` half-duplex CLI on `PA9`.
2. Launch the interactive tuner (either through a Betaflight FC USB connection or a direct USB-TTL adapter on `PA9`):
   ```bash
   # Via Betaflight flight controller USB (add --plot if matplotlib is installed)
   python python/pid_tuner.py --fc --plot

   # Or via direct half-duplex USB-TTL adapter
   python python/pid_tuner.py --port COM5
   ```
3. Tune gains and capture step responses in real time:
   - `?` — inspect current gains and live `VpdSetPoint / VPD / PWM`
   - `p=0.06`, `i=0.025`, `d=0.01`, `a=0.25`, `b=3`, `l=10`, `u=15` — update parameters in RAM immediately
   - `step 20 32` — run an automated `20 -> 32 dBm` step test, save `20 ms` telemetry to CSV, and print rise time, settling time, overshoot, and ripple
   - `ab 20 32` — run a back-to-back A/B comparison between Legacy `±1` (`m=0`) and PID (`m=1`)
   - `boot=msp` (or `boot=sa`) — reboot VTx into the bootloader so you can flash the normal firmware (`DIY_VTX820` / `Generic_GD32F130`) with your finalized parameters in `src/platformio.ini`

# Currently Supported VTx

OpenVTx is continually adding support for more hardware.  The most up to date list will be found in the ```platformio.ini``` file. https://github.com/OpenVTx/OpenVTx/blob/master/src/platformio.ini

# DIY Setup and flashing the EWRF E7082VM

fubarphill has made a fairly thorough video explaining the project and flashing process on youtube.  Please take the time to watch it.  Flashing requires an [ST-LINK V2](https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20210125211035&SearchText=ST-LINK+V2) and connection is via the below image.

[EWRF E7082VM V1 & V2](https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20210125211126&SearchText=EWRF+E7082VM) - Max output power of [+500mW](img/EWRF_E7082VM_500mW.jpg).

OpenVTx uses VS Code and [PlatformIO](https://platformio.org/platformio-ide).


[![open vtx flashing guide](https://i.ytimg.com/vi/JsJOMwu4hBM/hqdefault.jpg?sqp=-oaymwEcCPYBEIoBSFXyq4qpAw4IARUAAIhCGAFwAcABBg==&amp;rs=AOn4CLA1-gKpZdeOFu6gtONHsG5YoIeSzg)](https://www.youtube.com/watch?v=JsJOMwu4hBM) <img src="img/st_link_connection.png" width="20%">

# Contributors

Big thanks to cruwaller for adding the GD32 MCU found on the EWRF E7082VM, [and everyone else!](https://github.com/OpenVTx/OpenVTx/graphs/contributors)

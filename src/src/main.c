#include "targets.h"
#include "common.h"
#include "openVTxEEPROM.h"
#include "rtc6705.h"
#include "smartAudio.h"
#include "tramp.h"
#include "mspVtx.h"
#include "serial.h"
#include "errorCodes.h"
#include "gpio.h"
#include "button.h"
#include "modeIndicator.h"
#include "powerControl.h"

#if !DEBUG && !POWER_PID_TUNING
static uint32_t protocol_checked;
#endif /* !DEBUG && !POWER_PID_TUNING */

#if !POWER_PID_TUNING
static void start_serial(uint8_t type)
{
  uint32_t baud, stopbits;
  switch (type) {
    case TRAMP:
      baud = TRAMP_BAUD;
      stopbits = 1;
      trampReset();    
      break;
    case SMARTAUDIO:
      baud = SMARTAUDIO_BAUD;
      stopbits = 2;
      smartaudioReset();
      break;
    case MSP:
      baud = MSP_BAUD;
      stopbits = 1;
      mspReset();
      break;
    default:
      baud = 115200;
      stopbits = 1;
      break;
  }
  serial_begin(baud, UART_TX, UART_RX, stopbits);
  myEEPROM.vtxMode = type;
}
#endif /* !POWER_PID_TUNING */

void checkRTC6705isAlive()
{
  if (!rtc6705CheckFrequency())
  {
    rtc6705WriteFrequency(myEEPROM.currFreq); // Tries and set the correct freq to the RTC6705

    if (currentErrorMode == NO_ERROR)
    {
      currentErrorMode = RTC6705_NOT_DETECTED;
    }
  }
}

void setup(void)
{
  target_rfPowerAmpPinSetup();
  rtc6705spiPinSetup();

  readEEPROM();

  pitMode = myEEPROM.pitmodeInRange;

  rtc6705ResetState(); // During testing registers got messed up. So now it gets reset on boot!
  rtc6705WriteFrequency(myEEPROM.currFreq);

#if POWER_PID_TUNING
  power_control_tuning_init();
#else
  start_serial(myEEPROM.vtxMode);
#endif

  status_leds_init();
  button_init();

  resetModeIndication();
}

void loop(void)
{
#if !POWER_PID_TUNING
  uint32_t now = millis();

#if !DEBUG
  if (!vtxModeLocked)
  {
    if (PROTOCOL_CHECK_TIMEOUT <= (now - protocol_checked) && !serial_available()) // Do not change protocol while serial data is still available for processing.
    {
      uint8_t mode = (uint8_t)myEEPROM.vtxMode;
      mode = (mode + 1) % VTX_MODE_MAX;
      start_serial((vtxMode_e)mode);
      protocol_checked = now;

      if (FLIGHT_CONTROLLER_CHECK_TIMEOUT < now)
      {
          switch (myEEPROM.vtxMode)
          {
          case TRAMP:
            trampBuildrPacket();
            break;
          case SMARTAUDIO:
            smartaudioBuildSettingsPacket();
            break;
          default:
            break;
          }
      }
    }
  }
#endif /* DEBUG */

  /* Process uart data */
  switch (myEEPROM.vtxMode)
  {
  case TRAMP:
    trampProcessSerial();
    break;
  case SMARTAUDIO:
    smartaudioProcessSerial();
    break;
  case MSP:
    mspUpdate(now);
    break;
  default:
    break;
  }
#endif /* !POWER_PID_TUNING */

  checkButton();

  rtc6705PowerUpAfterPLLSettleTime();

  checkPowerOutput();

  checkRTC6705isAlive();

  errorCheck();

  writeEEPROM();

  target_loop();

  if (LED_INDICATION_OF_VTX_MODE && !(vtxModeLocked && myEEPROM.vtxMode == TRAMP)) // TRAMP doesnt use VTx Tables so LED indication of band/channel doesnt really work.
  {
    modeIndicationLoop();
  }
  else
  {
    status_led2(vtxModeLocked);
  }
}

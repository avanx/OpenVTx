#include "targets.h"
#include "common.h"
#include "openVTxEEPROM.h"
#include "powerControl.h"
#include "gpio.h"
#include "pwm.h"
#include "printf.h"
#include "helpers.h"
#include <math.h>

/* SA2.1 powerlevels in dBm.
 *
 * INav:
 *    Max of 5 [https://github.com/iNavFlight/inav/blob/a8016edd0d6f05bb12a75b0ea75a3483772baaeb/src/main/io/vtx_smartaudio.h#L36]
 *    Index 0 is ignored [https://github.com/iNavFlight/inav/blob/a8016edd0d6f05bb12a75b0ea75a3483772baaeb/src/main/io/vtx_smartaudio.c#L334]
 *
 */
uint8_t saPowerLevelsLut[SA_NUM_POWER_LEVELS] = {20, 26, 29, 31, 32};

uint8_t saPowerLevelsLabel[SA_NUM_POWER_LEVELS * POWER_LEVEL_LABEL_LENGTH] = {'1', '0', '0',
                                                                              '4', '0', '0',
                                                                              '8', '0', '0',
                                                                              '1', 'W', '2',
                                                                              '1', 'W', '6'};

gpio_pwm_t outputPowerTimer;
gpio_out_t vref_pin;
gpio_adc_t vpd_pin;
uint32_t currentVpd = 0;

uint16_t pwm_val = 2500;
uint16_t VpdSetPoint = 0;
uint8_t amp_state = 0;

// #if defined(DIY_VTX820)
  #define CAL_FREQ_SIZE 9
  #define CAL_DBM_SIZE 7
  #define MAX_POWER_IN_DBM 32
  uint16_t calFreqs[CAL_FREQ_SIZE] = {5600,	5650, 5700, 5750, 5800,	5850, 5900, 5950, 6000};
  uint8_t calDBm[CAL_DBM_SIZE] = {20, 23, 26, 28, 29, 31, 32}; // 100mW 200mW 400mW 600mW 800mW 1200mW 1600mW
  uint16_t calVpd[CAL_DBM_SIZE][CAL_FREQ_SIZE] = {
  // 5600, 5650, 5700, 5750, 5800, 5850, 5900, 5950, 6000
  {452, 454, 424, 430, 424, 432, 449, 457, 460}, // 100mW
  {530, 521, 518, 518, 524, 525, 536, 548, 562}, // 200mW
  {672, 651, 667, 664, 678, 678, 688, 707, 733}, // 400mW
  {821, 793, 807, 805, 820, 824, 840, 865, 899}, // 600mW
  {917, 887, 893, 893, 905, 914, 934, 964, 1002}, // 800mW
  {1161, 1130, 1098, 1107, 1107, 1133, 1170, 1210, 1256}, // 1200mW
  {1314, 1286, 1221, 1237, 1225, 1264, 1314, 1361, 1409} // 1600mW
  };  
// #endif



uint16_t bilinearInterpolation(float dB)
{
  uint16_t tempFreq = myEEPROM.currFreq;
  uint8_t i;
  uint8_t calFreqsIndex = 0;
  uint8_t calDBmIndex = 0;

  dB = dB + OFFSET;

  if (tempFreq < 5600) tempFreq = 5600;
  if (tempFreq > 6000) tempFreq = 6000;

  for (i = 0; i < (ARRAY_SIZE(calFreqs) - 1); i++)
  {
    if (tempFreq < calFreqs[i + 1])
    {
      calFreqsIndex = i;
      break;
    }
  }

  for (i = 0; i < (ARRAY_SIZE(calDBm) - 1); i++)
  {
    if (dB < calDBm[i + 1])
    {
      calDBmIndex = i;
      break;
    }
  }

  float x = dB;
  float x1 = calDBm[calDBmIndex];
  float x2 = calDBm[calDBmIndex + 1];

  float y = tempFreq;
  float y1 = calFreqs[calFreqsIndex];
  float y2 = calFreqs[calFreqsIndex + 1];

  float Q11 = calVpd[calDBmIndex][calFreqsIndex];
  float Q12 = calVpd[calDBmIndex][calFreqsIndex + 1];
  float Q21 = calVpd[calDBmIndex + 1][calFreqsIndex];
  float Q22 = calVpd[calDBmIndex + 1][calFreqsIndex + 1];

  float fxy1 = Q11 * (x2 - x) / (x2 - x1) + Q21 * (x - x1) / (x2 - x1);
  float fxy2 = Q12 * (x2 - x) / (x2 - x1) + Q22 * (x - x1) / (x2 - x1);

  uint16_t fxy = fxy1 * (y2 - y) / (y2 - y1) + fxy2 * (y - y1) / (y2 - y1);

  return fxy;
}

uint16_t setMaxPower()
{
  uint16_t tempFreq = myEEPROM.currFreq;
  uint8_t i;
  uint8_t freqsIndex = 0;
  uint8_t maxVpdIndex = ARRAY_SIZE(calVpd) - 1;

  if (tempFreq < 5600) tempFreq = 5600;
  if (tempFreq > 6000) tempFreq = 6000;

  for (i = 0; i < (ARRAY_SIZE(calFreqs) - 1); i++)
  {
    if (tempFreq < calFreqs[i + 1])
    {
      freqsIndex = i;
      break;
    }
  }

  return calVpd[maxVpdIndex][freqsIndex];
}

void target_rfPowerAmpPinSetup(void)
{
  vref_pin = gpio_out_setup(VREF, 0); // Power amp OFF

  outputPowerTimer = pwm_init(RTC_BIAS);
  pwm_out_write(outputPowerTimer, 3000);

  vpd_pin = adc_config(VPD);
  power_control_init(&g_powerCtrl, pwm_val);
}

uint32_t vpd_value_get(void)
{
  return adc_read(vpd_pin);
}

void target_set_power_dB(float dB)
{
  if (dB < 20)
  {
    VpdSetPoint = 0;
    pwm_val = 2500;
    amp_state = 0;
  } else if (dB > MAX_POWER_IN_DBM)
  {
    VpdSetPoint = 1500;
    pwm_val = 2000;
    amp_state = 1;
  } else if (dB == MAX_POWER_IN_DBM)
  {
    VpdSetPoint = setMaxPower();
    amp_state = 1;
  } else
  {
    VpdSetPoint = bilinearInterpolation(dB);
    amp_state = 1;
  }

  pwm_out_write(outputPowerTimer, pwm_val);
  gpio_out_write(vref_pin, amp_state);

  #if OUTPUT_POWER_TESTING
  VpdSetPoint = 0;
  pwm_val = 3000;
  pwm_out_write(outputPowerTimer, pwm_val);
  amp_state = 1;
  gpio_out_write(vref_pin, amp_state);
  #endif /* OUTPUT_POWER_TESTING */

  power_control_on_power_change(&g_powerCtrl, pwm_val, amp_state);
}

void checkPowerOutput(void)
{
  if (power_control_update(
          &g_powerCtrl,
          millis(),
          vpd_value_get,
          VpdSetPoint,
          amp_state,
          &currentVpd,
          &pwm_val))
  {
    pwm_out_write(outputPowerTimer, pwm_val);
  }
}

void target_setup(void)
{
  /* TODO: Configure WDG, fwdgt_config() */
}

void target_loop(void)
{
#if POWER_PID_TUNING
  power_control_tuning_poll(millis(), VpdSetPoint, currentVpd, pwm_val, amp_state);
#elif DEBUG
  static uint32_t temp;
  static char buff[32];
  int len;
  uint32_t now = millis();
  currentVpd = vpd_value_get();
  if (2000 <= (now - temp)) {
    temp = now;

    len = snprintf(buff, sizeof(buff), "%lu,%lu,%lu,%lu\r\n", myEEPROM.currFreq, pwm_val, VpdSetPoint, currentVpd);
    Serial_write_len((uint8_t*)buff, len);

    #if OUTPUT_POWER_TESTING
    VpdSetPoint = VpdSetPoint + 5;
    if (VpdSetPoint > 2000)
    {
      VpdSetPoint = 0;
      pwm_val = 3000;
      pwm_out_write(outputPowerTimer, pwm_val);
      myEEPROM.currFreq = myEEPROM.currFreq + 50;
      if (myEEPROM.currFreq > 6000)
      {
        myEEPROM.currFreq = 5600;
      }
      rtc6705WriteFrequency(myEEPROM.currFreq);
    }
    #endif /* OUTPUT_POWER_TESTING */
  }
#endif /* DEBUG */

  /* Reset WD */
  fwdgt_counter_reload();
}

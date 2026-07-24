#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bw213_m.h"
#include "drivers.h"
#include "stack/ble/ble.h"

// ============================================================
// SSD1675A EPD Controller for STELLAR-M (122x250)
// 基于已验证的 epd_bwr_213.c 驱动改编，仅保留BW通道
// ============================================================
// 注意:
// 1. 本驱动基于SSD1675A芯片
// 2. 屏幕为黑白屏（无红色），因此只使用0x24 (BW RAM)，不使用0x26 (Red RAM)
// 3. BUSY极性: LOW=忙, HIGH=空闲 → 使用 EPD_CheckStatus_inverted
// 4. 残影问题: 可通过调整LUT数据改善
// ============================================================


//#define BW_213_SSD1680_LUT_LEN 50   // 看起来不需要它了
static const uint8_t LUT_bw213_m_part[] = {

  // ========== 第一部分（波形表）SSD1675A 只需要5x7 ========== 
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,    // LUT0 BB
  0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00,    // LUT1 BW
  0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,    // LUT2 WB
  0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,    // LUT3 WW
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,    // LUT4 VCOM

  // ========== 第二部分（循环表）SSD1675A 只需要7x7 ========== 
  0x06, 0x00, 0x00, 0x00, 0x03,                     // TP0 A~D RP0
  0x00, 0x00, 0x00, 0x00, 0X00,                     // TP1 A~D RP1
  0x00, 0x00, 0x00, 0x00, 0x00,                     // TP2 A~D RP2
  0x00, 0x00, 0x00, 0x00, 0x00,                     // TP3 A~D RP3
  0x00, 0x00, 0x00, 0x00, 0x00,                     // TP4 A~D RP4
  0x00, 0x00, 0x00, 0x00, 0x00,                     // TP5 A~D RP5
  0x00, 0x00, 0x00, 0x00, 0x00,                     // TP6 A~D RP6
};

_attribute_ram_code_ uint8_t EPD_BW213_M_read_temp(void)
{
    uint8_t epd_temperature = 0 ;

    // SW Reset
    EPD_WriteCmd(0x12);

    EPD_CheckStatus_inverted(100);

    // Set Analog Block control
    EPD_WriteCmd(0x74);
    EPD_WriteData(0x54);
    // Set Digital Block control
    EPD_WriteCmd(0x7E);
    EPD_WriteData(0x3B);

    // Booster soft start
    EPD_WriteCmd(0x0C);
    EPD_WriteData(0x8B);
    EPD_WriteData(0x9C);
    EPD_WriteData(0x96);
    EPD_WriteData(0x0F);

    // Driver output control
    EPD_WriteCmd(0x01);
    EPD_WriteData(0x28);
    EPD_WriteData(0x01);
    EPD_WriteData(0x01);

    // Data entry mode setting
    EPD_WriteCmd(0x11);
    EPD_WriteData(0x01);

    // Set RAM X- Address Start/End
    EPD_WriteCmd(0x44);
    EPD_WriteData(0x00);
    EPD_WriteData(0x0F);

    // Set RAM Y- Address Start/End
    EPD_WriteCmd(0x45);
    EPD_WriteData(0x28);
    EPD_WriteData(0x01);
    EPD_WriteData(0x2E);
    EPD_WriteData(0x00);

    // Border waveform control
    EPD_WriteCmd(0x3C);
    EPD_WriteData(0x01);

    // Display update control
    EPD_WriteCmd(0x21);
    EPD_WriteData(0x03);
    EPD_WriteData(0x00);

    // Temperature sensor control
    EPD_WriteCmd(0x18);
    EPD_WriteData(0x80);

    // Display update control
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xB1);

    // Master Activation
    EPD_WriteCmd(0x20);

    EPD_CheckStatus_inverted(100);

    // Temperature sensor read from register
    EPD_WriteCmd(0x1B);
    epd_temperature = EPD_SPI_read();
    EPD_SPI_read();

    WaitMs(5);

    // deep sleep
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);

    return epd_temperature;
}

_attribute_ram_code_ uint8_t EPD_BW213_M_Display(unsigned char *image, int size, uint8_t full_or_partial)
{
    uint8_t epd_temperature = 0 ;

    // SW Reset
    EPD_WriteCmd(0x12);

    EPD_CheckStatus_inverted(100);

    // Set Analog Block control
    EPD_WriteCmd(0x74);
    EPD_WriteData(0x54);
    // Set Digital Block control
    EPD_WriteCmd(0x7E);
    EPD_WriteData(0x3B);

    // Booster soft start
    EPD_WriteCmd(0x0C);
    EPD_WriteData(0x8B);
    EPD_WriteData(0x9C);
    EPD_WriteData(0x96);
    EPD_WriteData(0x0F);

    // Driver output control
    EPD_WriteCmd(0x01);
    EPD_WriteData(0x28);
    EPD_WriteData(0x01);
    EPD_WriteData(0x01);

    // Data entry mode setting
    EPD_WriteCmd(0x11);
    EPD_WriteData(0x01);

    // Set RAM X- Address Start/End
    EPD_WriteCmd(0x44);
    EPD_WriteData(0x00);
    EPD_WriteData(0x0F);

    // Set RAM Y- Address Start/End
    EPD_WriteCmd(0x45);
    EPD_WriteData(0x28);
    EPD_WriteData(0x01);
    EPD_WriteData(0x2E);
    EPD_WriteData(0x00);

    // Border waveform control
    EPD_WriteCmd(0x3C);
    EPD_WriteData(0x01);

    // Display update control
    EPD_WriteCmd(0x21);
    EPD_WriteData(0x03);
    EPD_WriteData(0x00);

    // Temperature sensor control
    EPD_WriteCmd(0x18);
    EPD_WriteData(0x80);

    // Display update control
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xB1);

    // Master Activation
    EPD_WriteCmd(0x20);

    EPD_CheckStatus_inverted(100);

    // Temperature sensor read from register
    EPD_WriteCmd(0x1B);
    epd_temperature = EPD_SPI_read();
    EPD_SPI_read();

    WaitMs(5);

    // Set RAM X address
    EPD_WriteCmd(0x4E);
    EPD_WriteData(0x00);

    // Set RAM Y address
    EPD_WriteCmd(0x4F);
    EPD_WriteData(0x28);
    EPD_WriteData(0x01);

    // 写入BW图像数据到BW RAM (0x24)
    // BW only模式: 只使用0x24，不使用0x26 (Red RAM)
    EPD_LoadImage(image, size, 0x24);

    // 局刷: 写入自定义LUT到0x32寄存器
    // 全刷: 跳过0x32，芯片使用内部OTP全刷波形
    if (!full_or_partial)
    {
        int i;
        EPD_WriteCmd(0x32);
        for (i = 0; i < sizeof(LUT_bw213_m_part); i++)
        {
            EPD_WriteData(LUT_bw213_m_part[i]);
        }
    }

    // Display update control (0x22)
    // 0xC4 = 全刷(OTP波形, Mode 1), 0xC7 = 局刷(自定义LUT, Mode 2+1)
    EPD_WriteCmd(0x22);
    if (full_or_partial) {
        EPD_WriteData(0xC4);   // 全刷: OTP LUT + Mode 1
    } else {
        EPD_WriteData(0xC7);   // 局刷: 自定义LUT + Mode 2+1
    }

    // Master Activation
    EPD_WriteCmd(0x20);

    return epd_temperature;
}

_attribute_ram_code_ void EPD_BW213_M_set_sleep(void)
{
    // deep sleep
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);
}
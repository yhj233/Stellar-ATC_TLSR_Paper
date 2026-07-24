#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bw213_m3n.h"
#include "drivers.h"
#include "stack/ble/ble.h"

// SSD1675 mixed with SSD1680 EPD Controller for STELLAR-M3N (122x250)

#define BW213_M3N_Len 50
uint8_t LUT_BW213_M3N_part[] = {

  0x40,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

  BW213_M3N_Len, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,


};

_attribute_ram_code_ uint8_t EPD_BW213_M3N_detect(void)
{
    // SW Reset
    EPD_WriteCmd(0x12);
    WaitMs(10);

    EPD_WriteCmd(0x2F);
    if(EPD_SPI_read() != 0x01)
        return 0;
    return 1;
}

_attribute_ram_code_ uint8_t EPD_BW213_M3N_read_temp(void)
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
    
    // ACVCOM Setting
    EPD_WriteCmd(0x2B);
    EPD_WriteData(0x04);
    EPD_WriteData(0x63);

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

    // Temperature sensor control
    EPD_WriteCmd(0x18);
    EPD_WriteData(0x80);

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
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xA1);

    // Master Activation
    EPD_WriteCmd(0x20);
    
    EPD_CheckStatus_inverted(100);

    // Temperature sensor read from register
    EPD_WriteCmd(0x1B);
    epd_temperature = EPD_SPI_read();    
    EPD_SPI_read();

    WaitMs(5);

    // Display update control
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xB1);
    
    // Master Activation
    EPD_WriteCmd(0x20);

    EPD_CheckStatus_inverted(100);
    
    // Display update control
    EPD_WriteCmd(0x21);
    EPD_WriteData(0x03);
    
    // deep sleep
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);

    return epd_temperature;
}

_attribute_ram_code_ uint8_t EPD_BW213_M3N_Display(unsigned char *image, int size, uint8_t full_or_partial)
{    
    uint8_t epd_temperature = 0 ;
    
    // ===== 初始化序列（保持原样）=====
    EPD_WriteCmd(0x12);
    EPD_CheckStatus_inverted(100);

    EPD_WriteCmd(0x74); EPD_WriteData(0x54);
    EPD_WriteCmd(0x7E); EPD_WriteData(0x3B);
    
    EPD_WriteCmd(0x2B); EPD_WriteData(0x04); EPD_WriteData(0x63);

    EPD_WriteCmd(0x0C); EPD_WriteData(0x8B); EPD_WriteData(0x9C);
    EPD_WriteData(0x96); EPD_WriteData(0x0F);

    EPD_WriteCmd(0x01); EPD_WriteData(0x28); EPD_WriteData(0x01); EPD_WriteData(0x01);
    EPD_WriteCmd(0x11); EPD_WriteData(0x01);
    EPD_WriteCmd(0x18); EPD_WriteData(0x80);

    EPD_WriteCmd(0x44); EPD_WriteData(0x00); EPD_WriteData(0x0F);
    EPD_WriteCmd(0x45); EPD_WriteData(0x28); EPD_WriteData(0x01);
    EPD_WriteData(0x2E); EPD_WriteData(0x00);

    EPD_WriteCmd(0x3C); EPD_WriteData(0x01);

    // 高压握手
    EPD_WriteCmd(0x22); EPD_WriteData(0xA1);
    EPD_WriteCmd(0x20);
    EPD_CheckStatus_inverted(100);

    EPD_WriteCmd(0x1B);
    epd_temperature = EPD_SPI_read();    
    EPD_SPI_read();
    WaitMs(5);

    EPD_WriteCmd(0x22); EPD_WriteData(0xB1);
    EPD_WriteCmd(0x20);
    EPD_CheckStatus_inverted(100);
    
    EPD_WriteCmd(0x21); EPD_WriteData(0x03);

    // 加载黑白图像
    EPD_WriteCmd(0x4E); EPD_WriteData(0x00);
    EPD_WriteCmd(0x4F); EPD_WriteData(0x28); EPD_WriteData(0x01);
    EPD_LoadImage(image, size, 0x24);

    // ===== ★ 仅在全局刷新时清除红色通道（参考 BWR 213）★ =====
    int i;
    if (full_or_partial) {
        // 重新设置 RAM 地址
        EPD_WriteCmd(0x4E); EPD_WriteData(0x00);
        EPD_WriteCmd(0x4F); EPD_WriteData(0x28); EPD_WriteData(0x01);
        // 红色 RAM 全写 0
        EPD_WriteCmd(0x26);
        for (i = 0; i < size; i++) {
            EPD_WriteData(0x00);
        }
    }
    // ==========================================

    if (!full_or_partial) {
        // 局部刷新才写 LUT
        EPD_WriteCmd(0x32);
        for (i = 0; i < sizeof(LUT_BW213_M3N_part); i++) {
            EPD_WriteData(LUT_BW213_M3N_part[i]);
        }
    }      

  // ===== ★ 根据刷新类型选择显示模式 ★ =====
    EPD_WriteCmd(0x22);
    if (full_or_partial) {
        EPD_WriteData(0xC4);   // 全刷 + 红色清除
    } else {
        EPD_WriteData(0xC7);   // 局刷，不干扰红色
    }
    EPD_WriteCmd(0x20); 

    return epd_temperature;
}

_attribute_ram_code_ void EPD_BW213_M3N_set_sleep(void)
{
    // deep sleep
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);

}
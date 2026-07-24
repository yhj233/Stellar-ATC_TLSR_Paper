#pragma once

uint8_t EPD_BW_UC8251_read_temp(void);
uint8_t EPD_BW_UC8251_Display(unsigned char *image, int size, uint8_t full_or_partial);
void EPD_BW_UC8251_set_sleep(void);
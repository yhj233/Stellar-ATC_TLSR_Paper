#pragma once

uint8_t EPD_BW213_M_read_temp(void);
uint8_t EPD_BW213_M_Display(unsigned char *image, int size, uint8_t full_or_partial);
void EPD_BW213_M_set_sleep(void);
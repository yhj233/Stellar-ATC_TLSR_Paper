#pragma once

uint8_t EPD_BW213_M3N_detect(void);
uint8_t EPD_BW213_M3N_read_temp(void);
uint8_t EPD_BW213_M3N_Display(unsigned char *image, int size, uint8_t full_or_partial);
void EPD_BW213_M3N_set_sleep(void);
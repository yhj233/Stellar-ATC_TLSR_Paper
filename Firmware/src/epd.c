#include <stdint.h>
#include "etime.h"
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bw_213.h"
#include "epd_bwr_213.h"
#include "epd_bw213_m3n.h"
#include "epd_bwr_296.h"
// #include "epd_bw_uc8251.h"     // UC8251D 停用中
#include "epd_bw213_m.h"   // SSD1680 BW 122x250
#include "drivers.h"
#include "stack/ble/ble.h"

#include "battery.h"

#include "OneBitDisplay.h"
#include "TIFF_G4.h"
extern const uint8_t ucMirror[];
#include "font_60.h"
#include "font16.h"
#include "font30.h"
//#include "font_bold.h"
#include "font_opensans100_small.h"
#include "font_opensans20.h"
#include "font_weekday_cn.h"
#include "font_ymd_cn.h"
#include "font_solar_cn.h"
#include "font_jinri_cn.h"
#include "font_ds50_small.h"
#include "font_lunar_cn.h"

// 防烧屏偏移
RAM int8_t display_offset_x = 0;
RAM int8_t display_offset_y = 0;
// 循环偏移表（8种，每次全刷换一种）
const int8_t offset_table_x[8] = {0, 1, 0, -1, 1, -1, 1, -1};
const int8_t offset_table_y[8] = {0, 0, 1, 0, 1, 1, -1, -1};
RAM uint8_t offset_index = 0;

// ============== EPD Model Selection ==============
// 手动选择墨水屏型号，跳过自动检测
// 0 = 自动检测, 1 = BW213, 2 = BWR213, 3 = BWR154, 4 = BW213_M3N, 5 = BWR296, 6 = BW250122(UC8251D,停用), 7 = BW213_M
#define EPD_FORCE_MODEL 7

// ============== Lunar Calendar ==============
// Lunar year info: {year, new_year_month, new_year_day, month_days_bits, leap_month}
// month_days_bits: bit n (1-12, LSB=bit1) 1=30 days, 0=29 days
// leap_month: 0=no leap month
static const uint16_t lunar_data[][5] = {
    {2025, 1, 29, 0x0369, 0},
    {2026, 2, 17, 0x0569, 0},
    {2027, 2,  6, 0x0369, 0},
};

// Get lunar month (1-12) and day (1-30) for a solar date
static void get_lunar_date(int year, int month, int day, int *lm, int *ld) {
    static const int mdays[13] = {0,0,31,59,90,120,151,181,212,243,273,304,334};
    int i, doy = mdays[month] + day;

    for (i = 0; i < 3; i++) {
        if (lunar_data[i][0] == (uint16_t)year) break;
    }
    if (i >= 3) { *lm = month; *ld = day; return; }

    int ny_doy = mdays[lunar_data[i][1]] + lunar_data[i][2];
    if (doy < ny_doy) { *lm = month; *ld = day; return; }

    int days = doy - ny_doy;
    uint16_t bits = lunar_data[i][3];
    int m = 1;
    while (m <= 12) {
        int len = (bits & 1) ? 30 : 29;
        if (days < len) { *lm = m; *ld = days + 1; return; }
        days -= len;
        bits >>= 1;
        m++;
    }
    *lm = month; *ld = day;
}

// ============== Calendar Grid ==============
// Tomohiko Sakamoto's algorithm: 0=Sun,1=Mon,...,6=Sat
static uint8_t get_weekday(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    y -= m < 3;
    return (uint8_t)((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
}

// ============== Lunar Date Drawing ==============
// Get character code for digit 1-9 in Chinese
// 1-6 in WeekdayCN_16 (0x01-0x06), 7-9 in LunarCN_16 (0x01-0x03)
static uint8_t get_digit_cn(int d) {
    if (d >= 1 && d <= 6) return (uint8_t)d;   // WeekdayCN_16 0x01-0x06 = 一二三四五六
    if (d >= 7 && d <= 9) return (uint8_t)(d - 6); // LunarCN_16 0x01-0x03 = 七八九
    return 0;
}

// Draw lunar month and day in Chinese at (x, y)
// Month on line 1, day on line 2 (y2 = y + 16)
static void draw_lunar_date(OBDISP *obd, int x, int y, int lm, int ld) {
    uint8_t cb[2];
    int lx = x;
    int y2 = y + 18;

    // --- Month (line 1) ---
    if (lm == 1) {
        cb[0] = 0x08; cb[1] = 0; // 正 (LunarCN_16)
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y, (char *)cb, 1);
        lx += 16;
    } else if (lm == 10) {
        cb[0] = 0x04; cb[1] = 0; // 十 (LunarCN_16)
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y, (char *)cb, 1);
        lx += 16;
    } else if (lm == 11) {
        cb[0] = 0x09; cb[1] = 0; // 冬 (LunarCN_16)
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y, (char *)cb, 1);
        lx += 16;
    } else if (lm == 12) {
        cb[0] = 0x0A; cb[1] = 0; // 腊 (LunarCN_16)
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y, (char *)cb, 1);
        lx += 16;
    } else {
        cb[0] = get_digit_cn(lm); cb[1] = 0;
        obdWriteStringCustom(obd, (GFXfont *)(lm <= 6 ? &WeekdayCN_16 : &LunarCN_16), lx, y, (char *)cb, 1);
        lx += 16;
    }
    cb[0] = 0x02; cb[1] = 0; // 月 (YMD_CN_16)
    obdWriteStringCustom(obd, (GFXfont *)&YMD_CN_16, lx, y + 2, (char *)cb, 1);

    // --- Day (line 2) ---
    lx = x;
    if (ld >= 1 && ld <= 9) {
        cb[0] = 0x05; cb[1] = 0; // 初 (LunarCN_16)
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y2, (char *)cb, 1);
        lx += 16;
        cb[0] = get_digit_cn(ld); cb[1] = 0;
        obdWriteStringCustom(obd, (GFXfont *)(ld <= 6 ? &WeekdayCN_16 : &LunarCN_16), lx, y2, (char *)cb, 1);
    } else if (ld == 10) {
        cb[0] = 0x05; cb[1] = 0; // 初
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y2, (char *)cb, 1);
        lx += 16;
        cb[0] = 0x04; cb[1] = 0; // 十
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y2, (char *)cb, 1);
    } else if (ld >= 11 && ld <= 19) {
        cb[0] = 0x04; cb[1] = 0; // 十
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y2, (char *)cb, 1);
        lx += 16;
        cb[0] = get_digit_cn(ld - 10); cb[1] = 0;
        obdWriteStringCustom(obd, (GFXfont *)((ld-10) <= 6 ? &WeekdayCN_16 : &LunarCN_16), lx, y2, (char *)cb, 1);
    } else if (ld == 20) {
        cb[0] = 0x02; cb[1] = 0; // 二 (WeekdayCN_16)
        obdWriteStringCustom(obd, (GFXfont *)&WeekdayCN_16, lx, y2, (char *)cb, 1);
        lx += 16;
        cb[0] = 0x04; cb[1] = 0; // 十
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y2, (char *)cb, 1);
    } else if (ld >= 21 && ld <= 29) {
        cb[0] = 0x06; cb[1] = 0; // 廿 (LunarCN_16)
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y2, (char *)cb, 1);
        lx += 16;
        cb[0] = get_digit_cn(ld - 20); cb[1] = 0;
        obdWriteStringCustom(obd, (GFXfont *)((ld-20) <= 6 ? &WeekdayCN_16 : &LunarCN_16), lx, y2, (char *)cb, 1);
    } else if (ld == 30) {
        cb[0] = 0x03; cb[1] = 0; // 三 (WeekdayCN_16)
        obdWriteStringCustom(obd, (GFXfont *)&WeekdayCN_16, lx, y2, (char *)cb, 1);
        lx += 16;
        cb[0] = 0x04; cb[1] = 0; // 十
        obdWriteStringCustom(obd, (GFXfont *)&LunarCN_16, lx, y2, (char *)cb, 1);
    }
}

// Draw calendar grid: header 日一二三四五六 + date numbers
static void draw_calendar_grid(OBDISP *obd, int x, int y, int year, int month, int today) {
    char buff[4];
    uint8_t cb[2];
    int cell_w = 22, cell_h = 18;
    static const uint8_t hdr[] = {0x07,0x01,0x02,0x03,0x04,0x05,0x06}; // 日一二三四五六
    static const int dim_tab[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

    int first_wd = get_weekday(year, month, 1);
    int dim = dim_tab[month];
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) dim = 29;

    // Header
    for (int i = 0; i < 7; i++) {
        cb[0] = hdr[i]; cb[1] = 0;
        obdWriteStringCustom(obd, (GFXfont *)&WeekdayCN_16, x + i * cell_w + 1, y, (char *)cb, 1);
    }

    // Date rows
    int d = 1;
    int row_y = y + cell_h;
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            if (row == 0 && col < first_wd) continue;
            if (d > dim) return;
            int cx = x + col * cell_w + 2;
            if (d == today) {
                if (d < 10) {
                    // Single-digit box: aligned to glyph yOffset=-12
                    obdRectangle(obd, cx - 2, row_y - 13, cx + 16, row_y + 2, 1, 1);
                    obdRectangle(obd, cx    , row_y - 11, cx + 14, row_y    , 0, 1);
                } else {
                    // Two-digit box: aligned to glyph yOffset=-12
                    obdRectangle(obd, cx - 4, row_y - 14, cx + 17, row_y + 3, 1, 1);
                    obdRectangle(obd, cx - 2, row_y - 12, cx + 15, row_y + 1, 0, 1);
                }
            }
            if (d < 10) {
                // Single digit: center in cell (16px in 22px cell = 3px pad)
                sprintf(buff, "%d", d);
                obdWriteStringCustom(obd, (GFXfont *)&Dialog_plain_16, cx + 3, row_y + 1, buff, 1);
            } else {
                // Two-digit: render each digit with 6px spacing
                cb[0] = '0' + (d / 10); cb[1] = 0;
                obdWriteStringCustom(obd, (GFXfont *)&Dialog_plain_16, cx - 2, row_y + 1, (char *)cb, 1);
                cb[0] = '0' + (d % 10); cb[1] = 0;
                obdWriteStringCustom(obd, (GFXfont *)&Dialog_plain_16, cx + 6, row_y + 1, (char *)cb, 1);
            }
            d++;
        }
        row_y += cell_h;
    }
}


RAM uint8_t epd_model = EPD_FORCE_MODEL; // 0 = Undetected (auto-detect), 1 = BW213, 2 = BWR213, 3 = BWR154, 4 = BW213M3N, 5 = BWR296, 6 = BW250122(UC8251D), 7 = BW213M
const char *epd_model_string[] = {"NC", "BW213", "BWR213", "BWR154", "BW213M3N", "BWR296", "250122", "BW213M"};
RAM uint8_t epd_update_state = 0;

RAM uint8_t epd_scene = 2;
RAM uint8_t epd_wait_update = 0;

RAM uint8_t minute_refresh = 100;

const char *BLE_conn_string[] = {"BLE F", "BLE T"};
RAM uint8_t epd_temperature_is_read = 0;
RAM uint8_t epd_temperature = 0;

RAM uint8_t epd_buffer[epd_buffer_size];
RAM uint8_t epd_temp[epd_buffer_size]; // for OneBitDisplay to draw into
OBDISP obd;                        // virtual display structure
TIFFIMAGE tiff;

// With this we can force a display if it wasnt detected correctly
void set_EPD_model(uint8_t model_nr)
{
    epd_model = model_nr;
}

// With this we can force a display if it wasnt detected correctly
void set_EPD_scene(uint8_t scene)
{
    epd_scene = scene;
    set_EPD_wait_flush();
}

void set_EPD_wait_flush() {
    epd_wait_update = 1;
}

// Here we detect what E-Paper display is connected
_attribute_ram_code_ void EPD_detect_model(void)
{
    EPD_init();
    // system power
    EPD_POWER_ON();

    WaitMs(10);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    // Here we neeed to detect it
    if (EPD_BWR_296_detect())
    {
        epd_model = 5;
    }
    else if (EPD_BWR_213_detect())
    {
        epd_model = 2;
    }
//    else if (EPD_BWR_154_detect())// Right now this will never trigger, the 154 is same to 213BWR right now.
//    {
//        epd_model = 3;
//    }
    else if (EPD_BW213_M3N_detect())
    {
        epd_model = 4;
    }
    else
    {
        epd_model = 1;
    }

    EPD_POWER_OFF();
}

_attribute_ram_code_ uint8_t EPD_read_temp(void)
{
    if (epd_temperature_is_read)
        return epd_temperature;

    if (!epd_model)
        EPD_detect_model();

    EPD_init();
    // system power
    EPD_POWER_ON();
    WaitMs(5);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    if (epd_model == 1)
        epd_temperature = EPD_BW_213_read_temp();
    else if (epd_model == 2)
        epd_temperature = EPD_BWR_213_read_temp();
//    else if (epd_model == 3)
//        epd_temperature = EPD_BWR_154_read_temp();
    else if (epd_model == 4 || epd_model == 5)
        epd_temperature = EPD_BW213_M3N_read_temp();
    else if (epd_model == 7)
        epd_temperature = EPD_BW213_M_read_temp();

    EPD_POWER_OFF();

    epd_temperature_is_read = 1;

    return epd_temperature;
}

_attribute_ram_code_ void EPD_Display(unsigned char *image, unsigned char *red_image, int size, uint8_t full_or_partial)
{
    if (!epd_model)
        EPD_detect_model();

    EPD_init();
    // system power
    EPD_POWER_ON();
    WaitMs(5);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    if (epd_model == 1)
        epd_temperature = EPD_BW_213_Display(image, size, full_or_partial);
    else if (epd_model == 2)
        epd_temperature = EPD_BWR_213_Display(image, size, full_or_partial);
//    else if (epd_model == 3)
//        epd_temperature = EPD_BWR_154_Display(image, size, full_or_partial);
    else if (epd_model == 4)
        epd_temperature = EPD_BW213_M3N_Display(image, size, full_or_partial);
    else if (epd_model == 5)
        epd_temperature = EPD_BWR_296_Display_BWR(image, red_image, size, full_or_partial);
        //epd_temperature = EPD_BWR_296_Display(image, size, full_or_partial);
    else if (epd_model == 7)
        epd_temperature = EPD_BW213_M_Display(image, size, full_or_partial);

    epd_temperature_is_read = 1;
    epd_update_state = 1;
}

_attribute_ram_code_ void epd_set_sleep(void)
{
    if (!epd_model)
        EPD_detect_model();

    if (epd_model == 1)
        EPD_BW_213_set_sleep();
    else if (epd_model == 2)
        EPD_BWR_213_set_sleep();
//    else if (epd_model == 3)
//        EPD_BWR_154_set_sleep();
    else if (epd_model == 4 || epd_model == 5)
        EPD_BW213_M3N_set_sleep();
    else if (epd_model == 7)
        EPD_BW213_M_set_sleep();

    EPD_POWER_OFF();
    epd_update_state = 0;
}

_attribute_ram_code_ uint8_t epd_state_handler(void)
{
    switch (epd_update_state)
    {
    case 0:
        // Nothing todo
        break;
    case 1: // check if refresh is done and sleep epd if so
        if (epd_model == 1)
        {
            if (!EPD_IS_BUSY())
                epd_set_sleep();
        }
        else if (epd_model == 6 || epd_model == 7)
        {
            if (EPD_IS_BUSY())
                epd_set_sleep();
        }
        else
        {
            if (EPD_IS_BUSY())
                epd_set_sleep();
        }
        break;
    }
    return epd_update_state;
}

_attribute_ram_code_ void FixBuffer(uint8_t *pSrc, uint8_t *pDst, uint16_t width, uint16_t height)
{
    int x, y;
    uint8_t *s, *d;
    for (y = 0; y < (height / 8); y++)
    { // byte rows
        d = &pDst[y];
        s = &pSrc[y * width];
        for (x = 0; x < width; x++)
        {
            d[x * (height / 8)] = ~ucMirror[s[width - 1 - x]]; // invert and flip
        }                                                      // for x
    }                                                          // for y
}

_attribute_ram_code_ void TIFFDraw(TIFFDRAW *pDraw)
{
    uint8_t uc = 0, ucSrcMask, ucDstMask, *s, *d;
    int x, y;

    s = pDraw->pPixels;
    y = pDraw->y;                          // current line
    d = &epd_buffer[(249 * 16) + (y / 8)]; // rotated 90 deg clockwise
    ucDstMask = 0x80 >> (y & 7);           // destination mask
    ucSrcMask = 0;                         // src mask
    for (x = 0; x < pDraw->iWidth; x++)
    {
        // Slower to draw this way, but it allows us to use a single buffer
        // instead of drawing and then converting the pixels to be the EPD format
        if (ucSrcMask == 0)
        { // load next source byte
            ucSrcMask = 0x80;
            uc = *s++;
        }
        if (!(uc & ucSrcMask))
        { // black pixel
            d[-(x * 16)] &= ~ucDstMask;
        }
        ucSrcMask >>= 1;
    }
}

_attribute_ram_code_ void epd_display_tiff(uint8_t *pData, int iSize)
{
    // test G4 decoder
    epd_clear();
 // EPD_Display(epd_buffer, NULL, epd_buffer_size, 1);  // 强制全刷（含红色清零）
 //   WaitMs(500);  // 等待清屏完成，可根据实际减少

    TIFF_openRAW(&tiff, 250, 122, BITDIR_MSB_FIRST, pData, iSize, TIFFDraw);
    TIFF_setDrawParameters(&tiff, 65536, TIFF_PIXEL_1BPP, 0, 0, 250, 122, NULL);
    TIFF_decode(&tiff);
    TIFF_close(&tiff);
    EPD_Display(epd_buffer, NULL, epd_buffer_size, 1);
}

extern uint8_t mac_public[6];

// Fill buf with Chinese weekday: 周 + day char (total 2 bytes + null)
// Mapping: 0x08=周, 0x07=日, 0x01=一, 0x02=二, 0x03=三, 0x04=四, 0x05=五, 0x06=六
static void get_weekday_cn(int y, int m, int d, uint8_t *buf) {
    static const uint8_t day_char[] = {0x07, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06}; // 日一二三四五六
    uint8_t wd = get_weekday(y, m, d);
    buf[0] = 0x08; // 周
    buf[1] = 0x09; // SP (12px spacer)
    buf[2] = day_char[wd];
    buf[3] = 0;
}

// Draw Chinese date: "YYYY年MM月DD日" at (x, y)
static void draw_chinese_date(OBDISP *obd, int x, int y, struct date_time _time) {
    char buff[8];
    int cy = y + 2; // Chinese chars shifted down 2px for 20px font alignment
    // Year (2 digits: "26" instead of "2026")
    sprintf(buff, "%02d", _time.tm_year % 100);
    obdWriteStringCustom(obd, (GFXfont *)&Open_Sans_Hebrew_Bold_20, x, y, buff, 1);
    x += 24; // 2 digits * 12px
    x += 3; // gap
    // 年
    buff[0] = 0x01; buff[1] = 0;
    obdWriteStringCustom(obd, (GFXfont *)&YMD_CN_16, x - 1, cy, buff, 1);
    x += 16 + 2;
    // Month
    sprintf(buff, "%02d", _time.tm_month);
    obdWriteStringCustom(obd, (GFXfont *)&Open_Sans_Hebrew_Bold_20, x, y, buff, 1);
    x += 24; // 2 digits * 12px
    x += 3; // gap
    // 月
    buff[0] = 0x02; buff[1] = 0;
    obdWriteStringCustom(obd, (GFXfont *)&YMD_CN_16, x + 1, cy, buff, 1);
    x += 16 + 2;
    // Day
    sprintf(buff, "%02d", _time.tm_day);
    obdWriteStringCustom(obd, (GFXfont *)&Open_Sans_Hebrew_Bold_20, x, y, buff, 1);
    x += 24; // 2 digits * 12px
    x += 3; // gap
    // 日
    buff[0] = 0x03; buff[1] = 0;
    obdWriteStringCustom(obd, (GFXfont *)&YMD_CN_16, x - 1, cy, buff, 1);
}

// 24 solar term dates computed by formula:
// D = floor(0.2422*(Y-1900) - floor((Y-1900)/4) + C)
// Using integer arithmetic: C * 100, result / 10000
// C values optimized for 2026-2036 (21st century)
// 小寒/立春/雨水 的C值仅作回退用，实际使用查表法
static const int16_t solar_term_C[] = {
    607, 2115,  // 小寒(查表), 大寒 (Jan)
    485, 1910,  // 立春(查表), 雨水(查表) (Feb)
    639, 2125,  // 惊蛰, 春分 (Mar)
    565, 2089,  // 清明, 谷雨 (Apr)
    615, 2189,  // 立夏, 小满 (May)
    639, 2215,  // 芒种, 夏至 (Jun)
    789, 2349,  // 小暑, 大暑 (Jul)
    815, 2389,  // 立秋, 处暑 (Aug)
    839, 2389,  // 白露, 秋分 (Sep)
    915, 2415,  // 寒露, 霜降 (Oct)
    815, 2315,  // 立冬, 小雪 (Nov)
    789, 2265,  // 大雪, 冬至 (Dec)
};

static const uint8_t solar_term_month[24] = {
    1,1, 2,2, 3,3, 4,4, 5,5, 6,6,
    7,7, 8,8, 9,9, 10,10, 11,11, 12,12,
};

// 小寒(idx=0)、立春(idx=2)、雨水(idx=3) 无法用公式覆盖2026-2036，使用查表
// [term_local][year-2026] = day
static const uint8_t solar_term_lookup[3][11] = {
    {5, 5, 6, 5, 5, 5, 6, 5, 5, 5, 6},   // 小寒
    {4, 4, 4, 3, 4, 4, 4, 3, 4, 4, 4},   // 立春
    {18,18,19,18,18,19,19,18,18,19,19},  // 雨水
};

// Calculate solar term (month, day) for given year and term index (0-23)
static void calc_solar_term(int year, int idx, int *m, int *d) {
    *m = solar_term_month[idx];

    // 小寒(0)、立春(2)、雨水(3): 公式无法覆盖全范围，用查表
    if (idx == 0 || idx == 2 || idx == 3) {
        int yi = year - 2026;
        if (yi >= 0 && yi < 11) {
            int li = (idx == 0) ? 0 : (idx == 2) ? 1 : 2;
            *d = solar_term_lookup[li][yi];
        } else {
            // 超出2026-2036范围，回退到公式
            int y = year - 1900;
            int32_t val = (int32_t)2422 * y - (y / 4) * 10000 + (int32_t)solar_term_C[idx] * 100;
            *d = val / 10000;
        }
        return;
    }

    int y = year - 1900;
    int32_t val = (int32_t)2422 * y - (y / 4) * 10000 + (int32_t)solar_term_C[idx] * 100;
    *d = val / 10000;
}

// Day of year (1-based)
static int get_doy(int m, int d) {
    static const int days_before[13] = {0,0,31,59,90,120,151,181,212,243,273,304,334};
    return days_before[m] + d;
}

// Get next solar term index and days until it
static void get_solar_term_info(int year, int m, int d, int *term_idx, int *days) {
    int today_doy = get_doy(m, d);
    int best_idx = 0;
    int best_diff = 366;

    for (int i = 0; i < 24; i++) {
        int tm, td;
        calc_solar_term(year, i, &tm, &td);
        int t_doy = get_doy(tm, td);
        int diff = t_doy - today_doy;
        if (diff < 0) diff += 365; // wrap to next year
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }
    *term_idx = best_idx;
    *days = best_diff;
}

// Draw solar term: "离[term]N天" or "[term]" if today
static void draw_solar_term(OBDISP *obd, int x, int y, struct date_time _time) {
    char buff[8];
    int term_idx, days;
    get_solar_term_info(_time.tm_year, _time.tm_month, _time.tm_day, &term_idx, &days);

    if (days == 0) {
        // 今日[节气名]
        buff[0] = 0x01; buff[1] = 0;
        obdWriteStringCustom(obd, (GFXfont *)&JinRiCN, x, y, buff, 1);
        x += 33; // 32 + 1 gap
        buff[0] = (uint8_t)((term_idx + 22) % 24 + 1); buff[1] = 0;
        obdWriteStringCustom(obd, (GFXfont *)&SolarTermCN, x, y, buff, 1);
    } else {
        // 离
        buff[0] = 0x19; buff[1] = 0;
        obdWriteStringCustom(obd, (GFXfont *)&SolarTermCN, x, y, buff, 1);
        x += 17; // 16 + 1 gap
        // [term name] (32x16)
        buff[0] = (uint8_t)((term_idx + 22) % 24 + 1); buff[1] = 0;
        obdWriteStringCustom(obd, (GFXfont *)&SolarTermCN, x, y, buff, 1);
        x += 33; // 32 + 1 gap
        // N days
        sprintf(buff, "%d", days);
        obdWriteStringCustom(obd, (GFXfont *)&Open_Sans_Hebrew_Bold_20, x, y - 1, buff, 1);
        x += (days < 10 ? 12 : 24);
        x += 1;
        // 天
        buff[0] = 0x1A; buff[1] = 0;
        obdWriteStringCustom(obd, (GFXfont *)&SolarTermCN, x, y + 1, buff, 1);
    }
}


_attribute_ram_code_ void epd_display(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial)
{
    uint8_t battery_level;

    if (epd_update_state)
        return;

    if (!epd_model)
    {
        EPD_detect_model();
    }
    uint16_t resolution_w = 250;
    uint16_t resolution_h = 128; // 122 real pixel, but needed to have a full byte

    epd_clear();

    obdCreateVirtualDisplay(&obd, resolution_w, resolution_h, epd_temp);
    obdFill(&obd, 0, 0); // fill with white

    char buff[32];
    uint8_t cb[2];

    battery_level = get_battery_level(battery_mv);

    // ============ Left: Calendar Grid ============
    draw_calendar_grid(&obd, 2, 18, _time.tm_year, _time.tm_month, _time.tm_day);

    // Vertical divider between calendar and right panel
    obdDrawLine(&obd, 155, 0, 155, 122, 1, 0);

    // ============ Right Panel ============
    int rx = 165;

    // --- Year-Month: "2026-6" ---
    sprintf(buff, "%d-%d", _time.tm_year, _time.tm_month);
    obdWriteStringCustom(&obd, (GFXfont *)&Open_Sans_Hebrew_Bold_20, rx - 2, 20, buff, 1);

    // --- Date in ds50 font ---
    sprintf(buff, "%02d", _time.tm_day);
    obdWriteStringCustom(&obd, (GFXfont *)&DejaVu_Sans_50, rx - 7, 67, buff, 1);

    // --- Lunar date to the right of the ds50 date ---
    int lm, ld;
    get_lunar_date(_time.tm_year, _time.tm_month, _time.tm_day, &lm, &ld);
    draw_lunar_date(&obd, rx + 48, 50, lm, ld);

    // --- Temperature and Battery below ---
    sprintf(buff, "%d'C", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont *)&Open_Sans_Hebrew_Bold_20, rx - 5, 115, buff, 1);

    sprintf(buff, "%d%%", battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, rx + 43, 115, buff, 1);

    // Solar term countdown (2 lines)
    draw_solar_term(&obd, rx - 6, 91, _time);

    FixBuffer(epd_temp, epd_buffer, resolution_w, resolution_h);
    EPD_Display(epd_buffer, NULL, resolution_w * resolution_h / 8, full_or_partial);
}

_attribute_ram_code_ void epd_display_char(uint8_t data)
{
    int i;
    for (i = 0; i < epd_buffer_size; i++)
    {
        epd_buffer[i] = data;
    }
    EPD_Display(epd_buffer, NULL, epd_buffer_size, 1);
}

_attribute_ram_code_ void epd_clear(void)
{
    memset(epd_buffer, 0x00, epd_buffer_size);
    memset(epd_temp, 0x00, epd_buffer_size);
}
void update_time_scene(struct date_time _time, uint16_t battery_mv, int16_t temperature, void (*scene)(struct date_time, uint16_t, int16_t,  uint8_t)) {
    // default scene: show default time, battery, ble address, temperature
    if (epd_update_state)
        return;

    if (!epd_model)
    {
        EPD_detect_model();
    }

    if (epd_wait_update) {

    // 强制全刷时也更新偏移
       offset_index = (offset_index + 1) % 8;
       display_offset_x = offset_table_x[offset_index];
       display_offset_y = offset_table_y[offset_index];

        scene(_time, battery_mv, temperature, 1);
        epd_wait_update = 0;
    }

   else if (_time.tm_min != minute_refresh)
    {
        minute_refresh = _time.tm_min;
        uint8_t full_now = (_time.tm_hour == 0 && _time.tm_min == 0);
        if (full_now) {
            offset_index = (offset_index + 1) % 8;
            display_offset_x = offset_table_x[offset_index];
            display_offset_y = offset_table_y[offset_index];
        }
        scene(_time, battery_mv, temperature, full_now);
    }
}

void epd_update(struct date_time _time, uint16_t battery_mv, int16_t temperature) {
    switch(epd_scene) {
        case 1:
            update_time_scene(_time, battery_mv, temperature, epd_display);
            break;
        case 2:
            update_time_scene(_time, battery_mv, temperature, epd_display_time_with_date);
            break;
        default:
            break;
    }
}

void epd_display_time_with_date(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial) {
    uint16_t battery_level;

    epd_clear();

    obdCreateVirtualDisplay(&obd, epd_width, epd_height, epd_temp);
    obdFill(&obd, 0, 0); // fill with white
    
    char buff[100];
    uint8_t cn_buf[4];
    battery_level = get_battery_level(battery_mv);

    // Top left: battery voltage
    sprintf(buff, " %dmV", battery_mv);
    obdWriteStringCustom(&obd, (GFXfont *)&Open_Sans_Hebrew_Bold_20, 1 + display_offset_y, 17 + display_offset_x, (char *)buff, 1);

    // Top center: Chinese weekday
    get_weekday_cn(_time.tm_year, _time.tm_month, _time.tm_day, cn_buf);
    obdWriteStringCustom(&obd, (GFXfont *)&WeekdayCN_16, 120 + display_offset_y, 19 + display_offset_x, (char *)cn_buf, 1);

    // Top right: temperature
    sprintf(buff, "%d'C", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont *)&Open_Sans_Hebrew_Bold_20, 193 + display_offset_y, 17 + display_offset_x, (char *)buff, 1);

    // Center: large time
    sprintf(buff, "%02d:%02d", _time.tm_hour, _time.tm_min);
    obdWriteStringCustom(&obd, (GFXfont *)&Open_Sans_Condensed_Bold_100, 20 + display_offset_y, 94 + display_offset_x, (char *)buff, 1);

    draw_solar_term(&obd, 155 + display_offset_y, 119 + display_offset_x, _time);

   // obdRectangle(&obd, 168, 27, 170, 99, 1, 1);
  //  obdRectangle(&obd, 0, 97, 249, 99, 1, 1);

    draw_chinese_date(&obd, 3 + display_offset_y, 118 + display_offset_x, _time);

    FixBuffer(epd_temp, epd_buffer, epd_width, epd_height);

    EPD_Display(epd_buffer, NULL, epd_width * epd_height / 8, full_or_partial);
}
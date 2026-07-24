#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bw_uc8251.h"
#include "drivers.h"
#include "stack/ble/ble.h"

// ============================================================
// UC8251D EPD Controller for 122x250 (BW panel)
// 严格参考 UC8251D墨水屏的驱动.txt 范例代码 + 8251MANUAL.txt 编写
// ============================================================
// 注意:
// 1. 本驱动严格遵循UC8251D范例代码的命令序列
// 2. UC8251D与UC8151C是不同芯片，指令不兼容，不可混用
// 3. BUSY极性: LOW=忙, HIGH=空闲 → 使用 EPD_CheckStatus_inverted
// 4. PSR寄存器为1字节（范例代码: 0xBF, 手册推荐OTP模式: 0x9F）
// 5. 支持两种LUT模式:
//    a) OTP模式: REG=0, 芯片自动从OTP加载波形（手册推荐）
//    b) 寄存器模式: REG=1, 手动写入LUT数据到寄存器0x20~0x24
// 6. 【当前状态: 停用】此屏幕实际使用SSD1680兼容驱动(bw213_ssd1680)
// ============================================================

// ========== OTP PSR地址定义 ==========
// 参考Pervasive Displays Wide_Small驱动 (PDI Application Note §3)
// 122x250面板归类为2.13"系列
// 注意: UC8251D的PSR为1字节，但OTP中可能存储2字节
//       我们只使用第1字节（PSR[0]），第2字节忽略
// Bank0: PSR在0x0B1B
// Bank1: PSR在0x171B
#define OTP_OFFSET_A5_BANK1   0x0C00   // Bank1的0xA5标记偏移
#define OTP_OFFSET_PSR_BANK0  0x0B1B   // Bank0 PSR偏移
#define OTP_OFFSET_PSR_BANK1  0x171B   // Bank1 PSR偏移

// ========== 缓存的OTP PSR数据 ==========
// UC8251D的PSR寄存器为1字节
// 只在首次读取OTP时填充，后续刷新直接使用缓存值
static uint8_t  otp_psr_cached = 0;    // 缓存的PSR值（1字节）
static uint8_t  otp_valid = 0;         // 0=未检测, 1=OTP可用, 2=OTP不可用
static uint8_t  otp_checked = 0;       // 0=未检测过, 1=已检测

// ========== LUT数据（280字节 = 5组 × 56字节）==========
// 顺序: VCOM(0x20), W2W(0x21), B2W(0x22), W2B(0x23), B2B(0x24)
// 严格按照范例代码 lut_find() 格式
//
// 以下LUT为通用占位值，实际使用时需从面板厂商获取正确的波形数据
// 或使用OTP模式自动加载
//
#define BW_UC8251_LUT_GROUP_LEN  56
#define BW_UC8251_LUT_TOTAL_LEN  280

// 全刷LUT（占位值，待替换为实际波形）
static const uint8_t LUT_BW_UC8251_full[BW_UC8251_LUT_TOTAL_LEN] = {
    // LUT0: VCOM (0x20) - 56 bytes
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99, 0x88,
    0x00, 0x00, 0x00, 0x00, 0xF8, 0xB4, 0x13, 0x51,
    0x35, 0x51, 0x51, 0x19, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // LUT1: W2W (0x21) - 56 bytes
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99, 0x88,
    0x00, 0x00, 0x00, 0x00, 0xF8, 0xB4, 0x13, 0x51,
    0x35, 0x51, 0x51, 0x19, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // LUT2: B2W (0x22) - 56 bytes
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99, 0x88,
    0x00, 0x00, 0x00, 0x00, 0xF8, 0xB4, 0x13, 0x51,
    0x35, 0x51, 0x51, 0x19, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // LUT3: W2B (0x23) - 56 bytes
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99, 0x88,
    0x00, 0x00, 0x00, 0x00, 0xF8, 0xB4, 0x13, 0x51,
    0x35, 0x51, 0x51, 0x19, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // LUT4: B2B (0x24) - 56 bytes
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99, 0x88,
    0x00, 0x00, 0x00, 0x00, 0xF8, 0xB4, 0x13, 0x51,
    0x35, 0x51, 0x51, 0x19, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// 局刷LUT（占位值，全部为0）
static const uint8_t LUT_BW_UC8251_part[BW_UC8251_LUT_TOTAL_LEN] = {0};

// ========== 快速SPI读字节（不重新配置MOSI方向）==========
// 用于OTP连续读取大量字节时提高效率
// 调用前需确保MOSI已配置为输入模式
static uint8_t EPD_SPI_read_fast(void)
{
    unsigned char i;
    uint8_t value = 0;

    gpio_write(EPD_CS, 0);
    WaitUs(10);
    for (i = 0; i < 8; i++)
    {
        gpio_write(EPD_CLK, 0);
        gpio_write(EPD_CLK, 1);
        value <<= 1;
        if (gpio_read(EPD_MOSI) != 0)
            value |= 1;
    }
    gpio_write(EPD_CS, 1);
    return value;
}

// ========== 从OTP读取PSR数据 ==========
// 参考: PDI Application Note §3. Read OTP memory
//       Pervasive Displays Wide_Small COG_getDataOTP()
//
// UC8251D通过0xA2命令读取OTP内存
// OTP中有两个Bank (Bank0和Bank1)，通过首字节0xA5判断活动Bank
// PSR (Panel Setting Register) 为1字节
//
// 返回: 0=OTP不可用/未烧录, 1=OTP可用，PSR值存入*psr
static uint8_t EPD_BW_UC8251_read_otp_psr(uint8_t *psr)
{
    uint8_t  bank_indicator;
    uint8_t  bank0_active;
    uint16_t offset_a5;     // Bank1时0xA5标记的偏移
    uint16_t offset_psr;    // PSR数据偏移
    uint16_t index;
    uint8_t  psr_raw;

    // 发送OTP读取命令 (0xA2)
    EPD_WriteCmd(0xA2);

    // 配置MOSI为输入模式（只做一次，后续用快速读取）
    gpio_shutdown(EPD_MOSI);
    gpio_set_output_en(EPD_MOSI, 0);
    gpio_set_input_en(EPD_MOSI, 1);
    // 确保DC为HIGH（数据模式）
    EPD_ENABLE_WRITE_DATA();

    // 第1字节: dummy (丢弃)
    EPD_SPI_read_fast();

    // 第2字节: Bank indicator
    // 0xA5 = Bank0活动, 其他值 = Bank1活动
    bank_indicator = EPD_SPI_read_fast();
    bank0_active = (bank_indicator == 0xA5) ? 1 : 0;

    if (bank0_active)
    {
        offset_a5  = 0x0000;            // Bank0不需要跳过
        offset_psr = OTP_OFFSET_PSR_BANK0;  // 0x0B1B
    }
    else
    {
        offset_a5  = OTP_OFFSET_A5_BANK1;   // 0x0C00
        offset_psr = OTP_OFFSET_PSR_BANK1;  // 0x171B
    }

    // 如果是Bank1，需要先跳到offset_a5验证0xA5标记
    if (offset_a5 > 0x0000)
    {
        // 已经读了2字节(dummy + bank indicator)，从第3字节开始
        for (index = 2; index < offset_a5; index++)
        {
            EPD_SPI_read_fast();
        }
        // 验证Bank1的0xA5标记
        bank_indicator = EPD_SPI_read_fast();
        if (bank_indicator != 0xA5)
        {
            // Bank1标记无效，OTP可能损坏
            gpio_set_output_en(EPD_MOSI, 1);
            gpio_set_input_en(EPD_MOSI, 0);
            return 0;
        }
    }

    // 跳过中间字节直到PSR地址
    for (index = offset_a5 + 1; index < offset_psr; index++)
    {
        EPD_SPI_read_fast();
    }

    // 读取PSR数据 (1字节，UC8251D的PSR是1字节)
    // OTP中可能存储2字节，但我们只使用第1字节
    psr_raw = EPD_SPI_read_fast();
    // 跳过第2字节（如果OTP存储了2字节PSR）
    EPD_SPI_read_fast();

    // 恢复MOSI为输出模式
    gpio_set_output_en(EPD_MOSI, 1);
    gpio_set_input_en(EPD_MOSI, 0);

    // 验证PSR数据有效性
    // 0x00或0xFF表示OTP未烧录
    if (psr_raw == 0x00 || psr_raw == 0xFF)
    {
        return 0;
    }

    *psr = psr_raw;
    return 1;
}

// ========== 检测OTP是否可用（带缓存）==========
// 只在首次调用时实际读取OTP，后续使用缓存结果
// 返回: 0=OTP不可用, 1=OTP可用
static uint8_t EPD_BW_UC8251_otp_available(void)
{
    if (otp_checked)
        return (otp_valid == 1) ? 1 : 0;

    otp_checked = 1;

    if (EPD_BW_UC8251_read_otp_psr(&otp_psr_cached))
    {
        otp_valid = 1;
        return 1;
    }

    otp_valid = 2;
    return 0;
}

// ========== 将LUT数据写入寄存器 (0x20~0x24) ==========
// 按照范例代码的 lut_find() 函数结构
static void EPD_BW_UC8251_write_lut(const uint8_t *lut)
{
    unsigned int count;

    // LUT VCOM (0x20)
    EPD_WriteCmd(0x20);
    for (count = 0; count < BW_UC8251_LUT_GROUP_LEN; count++)
        EPD_WriteData(lut[count]);

    // LUT W2W (0x21)
    EPD_WriteCmd(0x21);
    for (count = BW_UC8251_LUT_GROUP_LEN; count < BW_UC8251_LUT_GROUP_LEN * 2; count++)
        EPD_WriteData(lut[count]);

    // LUT B2W (0x22)
    EPD_WriteCmd(0x22);
    for (count = BW_UC8251_LUT_GROUP_LEN * 2; count < BW_UC8251_LUT_GROUP_LEN * 3; count++)
        EPD_WriteData(lut[count]);

    // LUT W2B (0x23)
    EPD_WriteCmd(0x23);
    for (count = BW_UC8251_LUT_GROUP_LEN * 3; count < BW_UC8251_LUT_GROUP_LEN * 4; count++)
        EPD_WriteData(lut[count]);

    // LUT B2B (0x24)
    EPD_WriteCmd(0x24);
    for (count = BW_UC8251_LUT_GROUP_LEN * 4; count < BW_UC8251_LUT_TOTAL_LEN; count++)
        EPD_WriteData(lut[count]);
}

// ========== UC8251D 初始化序列 ==========
// 严格按照范例代码 IC_Init() + 8251MANUAL.txt 编写
// use_otp: 0=手动LUT模式(REG=1), 1=OTP LUT模式(REG=0)
static void EPD_BW_UC8251_init(uint8_t use_otp)
{
    // Panel setting (0x00) - 1字节
    // UC8251D PSR寄存器格式 (1字节):
    //   D7-D6: RES[1:0] (分辨率预设, 10=寄存器分辨率)
    //   D5:    REG (0=OTP LUT, 1=寄存器LUT)
    //   D4:    KW/R (0=BW模式, 1=Red模式? 范例代码用1)
    //   D3:    UD (1=从上到下扫描)
    //   D2:    SHL (1=从左到右)
    //   D1:    SHD_N (1=升压使能)
    //   D0:    RST_N (1=正常)
    // 范例代码: 0xBF = 0b10111111 (REG=1, 手动LUT, 128x296)
    // 手册推荐: 0x9F = 0b10011111 (REG=0, OTP LUT)
    if (use_otp)
    {
        // OTP模式: 使用OTP中的PSR数据，但覆盖RES=10, REG=0
        // 保留OTP PSR的低6位(UD/SHL/SHD_N/RST_N等)，覆盖高2位
        uint8_t psr_value = (otp_psr_cached & 0x3F) | 0x80;  // RES=10, REG=0
        EPD_WriteCmd(0x00);
        EPD_WriteData(psr_value);
    }
    else
    {
        // 手动LUT模式: 完全按照范例代码
        // 0xBF = 0b10111111: RES=10(寄存器分辨率), REG=1(寄存器LUT)
        EPD_WriteCmd(0x00);
        EPD_WriteData(0xBF);
    }

    // Power setting (0x01)
    EPD_WriteCmd(0x01);
    EPD_WriteData(0x03);
    EPD_WriteData(0x00);
    EPD_WriteData(0x3F);   // VDH
    EPD_WriteData(0x3F);   // VDL
    EPD_WriteData(0x00);   // VDHR

    // Power off sequence setting (0x03)
    EPD_WriteCmd(0x03);
    EPD_WriteData(0x00);

    // Booster soft start (0x06)
    EPD_WriteCmd(0x06);
    EPD_WriteData(0x17);
    EPD_WriteData(0x17);
    EPD_WriteData(0x16);

    // LUT option (0x2A)
    EPD_WriteCmd(0x2A);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
    EPD_WriteData(0xFF);
    EPD_WriteData(0x03);

    // PLL control (0x30) - 范例代码: 0x13, 手册推荐: 0x09
    EPD_WriteCmd(0x30);
    EPD_WriteData(0x13);

    // Temperature sensor enable (0x41) - 手册§2.6
    // TSE=0(启用内部传感器), TO[3:0]=0(无偏移)
    EPD_WriteCmd(0x41);
    EPD_WriteData(0x00);

    // VCOM and data interval setting (0x50)
    EPD_WriteCmd(0x50);
    EPD_WriteData(0x27);

    // TCON setting (0x60)
    EPD_WriteCmd(0x60);
    EPD_WriteData(0x22);

    // Resolution setting (0x61): 128x250
    // 格式: HRES(8bit) + VRES(16bit大端)
    // 注意: UC8251强制8像素字节对齐，物理122像素需向上取整为128(16字节)
    //       实际有效像素仍为122，多余bit写入0即可
    // 范例代码: 128, 0x00, 250 → 128x250
    EPD_WriteCmd(0x61);
    EPD_WriteData(128);    // HRES = 128 (122向上取整到8的倍数)
    EPD_WriteData(0x00);   // VRES[15:8] = 0
    EPD_WriteData(250);    // VRES[7:0] = 250

    // Gate/Source start position (0x65) - 手册§2.3
    // 设置起始位置为(0,0)
    EPD_WriteCmd(0x65);
    EPD_WriteData(0x00);   // HST[7:3]=0
    EPD_WriteData(0x00);   // VST[8:0]低8位
    EPD_WriteData(0x00);   // VST保留

    // VCOM_DC setting (0x82)
    EPD_WriteCmd(0x82);
    EPD_WriteData(0x00);

    // Power on (0x04)
    EPD_WriteCmd(0x04);
    EPD_CheckStatus_inverted(100);
}

// ========== 读取温度 ==========
_attribute_ram_code_ uint8_t EPD_BW_UC8251_read_temp(void)
{
    uint8_t epd_temperature = 0;

    // Power on
    EPD_WriteCmd(0x04);
    EPD_CheckStatus_inverted(100);

    // Read temperature from register
    EPD_WriteCmd(0x40);
    epd_temperature = EPD_SPI_read();
    EPD_SPI_read();

    // Power off
    EPD_WriteCmd(0x02);

    // Deep sleep
    EPD_WriteCmd(0x07);
    EPD_WriteData(0xA5);

    return epd_temperature;
}

// ========== 显示图像 ==========
// 按照范例代码 dis_all() + display_update() 流程编写
_attribute_ram_code_ uint8_t EPD_BW_UC8251_Display(unsigned char *image, int size, uint8_t full_or_partial)
{
    uint8_t epd_temperature = 0;
    uint8_t use_otp = 0;

    // Power on
    EPD_WriteCmd(0x04);
    EPD_CheckStatus_inverted(100);

    // Read temperature
    EPD_WriteCmd(0x40);
    epd_temperature = EPD_SPI_read();
    EPD_SPI_read();

    // 检测OTP是否可用（首次调用会读取OTP，后续使用缓存）
    use_otp = EPD_BW_UC8251_otp_available();

    // IC初始化（根据OTP可用性选择REG=0或REG=1）
    EPD_BW_UC8251_init(use_otp);

    if (use_otp)
    {
        // OTP模式: REG=0, 芯片自动从OTP加载LUT波形
        // 不需要手动写入LUT到寄存器0x20~0x24
    }
    else
    {
        // 手动LUT模式: REG=1, 需要手动写入LUT数据
        if (full_or_partial)
        {
            EPD_BW_UC8251_write_lut(LUT_BW_UC8251_full);
        }
        else
        {
            EPD_BW_UC8251_write_lut(LUT_BW_UC8251_part);
        }
        // 等待LUT写入完成
        EPD_CheckStatus_inverted(100);
    }

    // 写入图像数据到NEW RAM (0x13)
    // 范例代码使用0x13写入图像数据
    EPD_LoadImage(image, size, 0x13);

    // 显示刷新: 0x17 + 0xA7
    // 按照范例代码 display_update() 流程
    EPD_WriteCmd(0x17);
    EPD_CheckStatus_inverted(100);
    EPD_WriteData(0xA7);
    EPD_CheckStatus_inverted(100);

    return epd_temperature;
}

// ========== 进入休眠 ==========
_attribute_ram_code_ void EPD_BW_UC8251_set_sleep(void)
{
    // Power off
    EPD_WriteCmd(0x02);

    // Deep sleep
    EPD_WriteCmd(0x07);
    EPD_WriteData(0xA5);
}
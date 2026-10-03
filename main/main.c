#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"
#include "player.h"
#include "network.h"
#include "power.h"
#include "power_policy.h"
#include "sync.h"
#include "driver/usb_serial_jtag.h"
#include "driver/rtc_io.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_rom_sys.h"
#include <string.h>

#include "esp_lcd_qspi_amoled.h"

static const char *TAG = "example";
static SemaphoreHandle_t lvgl_mux = NULL;
static esp_lcd_panel_handle_t player_panel;
static pearl_library music;
static TaskHandle_t rescan_task_handle;
static atomic_bool rescan_running;
static atomic_bool audio_started,library_ready;
static SemaphoreHandle_t library_access;
bool pearl_library_lock(void){return library_access&&xSemaphoreTake(library_access,pdMS_TO_TICKS(5000))==pdTRUE;}
void pearl_library_unlock(void){xSemaphoreGive(library_access);}
static atomic_bool screen_locked,screen_manual,wake_requested;
static atomic_uint activity_ms,library_albums,library_tracks;
void pearl_power_activity(void){atomic_store(&activity_ms,(uint32_t)(esp_timer_get_time()/1000));}
uint32_t pearl_power_last_activity(void){return atomic_load(&activity_ms);}
bool pearl_power_screen_asleep(void){return atomic_load(&screen_locked);}
bool pearl_power_deep_supported(void){return esp_sleep_is_valid_wakeup_gpio(CONFIG_PEARL_BUTTON_UP);}
void pearl_library_counts(unsigned *albums,unsigned *tracks){*albums=atomic_load(&library_albums);*tracks=atomic_load(&library_tracks);}
static void display_sleep(bool asleep,bool manual);
static void enter_standby(void);



/*
HIFI版本所有IO
#define TFT_RST 7
#define TFT_CS 8
#define TFT_SCK 9
#define TFT_SDA0 10
#define TFT_SDA1 11
#define TFT_SDA2 12
#define TFT_SDA3 13
#define TFT_TE 6
#define BTN_PIN 48
#define I2C_WIRE_SDA 3
#define I2C_WIRE_SCL 2

#define IMU_INT1 47
#define TOUCH_PIN_NUM_INT 4
#define TOUCH_PIN_NUM_RST 5

#define SD_MMC_D0_PIN 15
#define SD_MMC_D1_PIN 14
#define SD_MMC_D2_PIN 21
#define SD_MMC_D3_PIN 18
#define SD_MMC_CLK_PIN 16
#define SD_MMC_CMD_PIN 17

#define AUDIO_I2S_MCK_IO -1 // MCK
#define AUDIO_I2S_BCK_IO 40 // BCK
#define AUDIO_I2S_WS_IO 38  // LCK
#define AUDIO_I2S_DO_IO 39  // DIN
#define CS43131_RST 41

#define BAT_ADC_PIN 1

#define MIC_TYPE_PDM
#define MIC_I2S_PORT I2S_NUM_0
#define MIC_PDM_DATA 46
#define MIC_PDM_SCK 45


普通版所有IO
#define TFT_RST 7
#define TFT_CS 8
#define TFT_SCK 9
#define TFT_SDA0 10
#define TFT_SDA1 11
#define TFT_SDA2 12
#define TFT_SDA3 13
#define TFT_TE 6
#define BTN_PIN 47
#define I2C_WIRE_SDA 3
#define I2C_WIRE_SCL 2

#define TOUCH_PIN_NUM_INT 4
#define TOUCH_PIN_NUM_RST 5

#define SD_MMC_D0_PIN 15
#define SD_MMC_D1_PIN 14
#define SD_MMC_D2_PIN 21
#define SD_MMC_D3_PIN 18
#define SD_MMC_CLK_PIN 16
#define SD_MMC_CMD_PIN 17

#define BAT_ADC_PIN 1

#define AUDIO_I2S_MCK_IO -1 // MCK
#define AUDIO_I2S_BCK_IO 40 // BCK
#define AUDIO_I2S_WS_IO 38  // LCK
#define AUDIO_I2S_DO_IO 39  // DIN
#define AUDIO_MUTE_PIN 48 // 低电平静音

#define MIC_TYPE_PDM
#define MIC_I2S_PORT I2S_NUM_0
#define MIC_PDM_DATA 46
#define MIC_PDM_SCK 45

*/



#define LCD_HOST    SPI2_HOST
#define TOUCH_HOST  I2C_NUM_0

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL       (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL       (16)
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
// 鱼鹰光电屏幕验证底板
#define EXAMPLE_PIN_NUM_LCD_CS            (GPIO_NUM_8)
#define EXAMPLE_PIN_NUM_LCD_PCLK          (GPIO_NUM_9) 
#define EXAMPLE_PIN_NUM_LCD_DATA0         (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA1         (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA2         (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA3         (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_RST           (GPIO_NUM_7)

#define EXAMPLE_USE_TOUCH               1

#if EXAMPLE_USE_TOUCH
// ESP32S3_AMOLED_触摸
#define EXAMPLE_PIN_NUM_TOUCH_SCL         (GPIO_NUM_2)
#define EXAMPLE_PIN_NUM_TOUCH_SDA         (GPIO_NUM_3)
#define EXAMPLE_PIN_NUM_TOUCH_RST         (GPIO_NUM_5)
#define EXAMPLE_PIN_NUM_TOUCH_INT         (GPIO_NUM_4)
#endif

#define CST816_ID   1
#define CST820_ID   2
#define CHSC6417_ID 3

#define EXAMPLE_LVGL_BUF_HEIGHT        12 /* Two 11 KiB DMA buffers; reserve RAM for SD/WiFi. */
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (8 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2

// 鱼鹰光电
#define AM196Q410502LK_196_410x502   1
#define AM178Q368448LK_178_368x448   2
#define AM151Q466466LK_151_466x466_C 3
#define AM160Q480480LK_160_480x480_C 4
#define AM201Q240296LK_201_240x296   5
#define AM200Q460460LK_200_460x460   6  //本项目使用这个

// 设置当前屏幕尺寸
#define CURRENT_SCREEN_SIZE AM200Q460460LK_200_460x460 // 在这里选择屏幕尺寸

#if CURRENT_SCREEN_SIZE == AM196Q410502LK_196_410x502
  #define EXAMPLE_LCD_H_RES              410
  #define EXAMPLE_LCD_V_RES              502
  #define EXAMPLE_LCD_X_GAP              22
  #define EXAMPLE_LCD_Y_GAP              0
  #define TOUCH_IC_CONFIG                CST820_ID
  #define AMOLED_QSPI_MAX_PCLK           40 * 1000 * 1000

  static const qspi_amoled_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},// 退出睡眠模式
    {0x35, (uint8_t []){0x00}, 1, 0},// 开启撕裂效果
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},// SPI 模式控制
    // {0x36, (uint8_t []){0x00}, 1, 0},// 设置内存数据访问控制
    {0x3A, (uint8_t []){0x55}, 1, 0},//// 设置像素格式 16位
    {0x53, (uint8_t []){0x20}, 1, 0},// 设置 CTRL 显示1
    {0x63, (uint8_t []){0xFF}, 1, 0},// 设置 HBM 模式下的亮度值
    {0x2A, (uint8_t []){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x29, (uint8_t []){0x00}, 0, 60},// 打开显示器
    {0x51, (uint8_t []){0xFF}, 1, 0},// 设置正常模式下的亮度值
    {0x58, (uint8_t []){0x07}, 1, 10},// 设置正常模式下的亮度值
  };
#elif CURRENT_SCREEN_SIZE == AM178Q368448LK_178_368x448
  #define EXAMPLE_LCD_H_RES              368
  #define EXAMPLE_LCD_V_RES              448
  #define EXAMPLE_LCD_X_GAP              16
  #define EXAMPLE_LCD_Y_GAP              0
  #define TOUCH_IC_CONFIG                CHSC6417_ID
  #define AMOLED_QSPI_MAX_PCLK           40 * 1000 * 1000
  static const qspi_amoled_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},// 退出睡眠模式
    {0x35, (uint8_t []){0x00}, 1, 0},// 开启撕裂效果
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},// SPI 模式控制
    // {0x36, (uint8_t []){0x00}, 1, 0},// 设置内存数据访问控制
    {0x3A, (uint8_t []){0x55}, 1, 0},//// 设置像素格式 16位
    {0x53, (uint8_t []){0x20}, 1, 0},// 设置 CTRL 显示1
    {0x63, (uint8_t []){0xFF}, 1, 0},// 设置 HBM 模式下的亮度值
    {0x2A, (uint8_t []){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x29, (uint8_t []){0x00}, 0, 60},// 打开显示器
    {0x51, (uint8_t []){0xA0}, 1, 0},// 设置正常模式下的亮度值
    {0x58, (uint8_t []){0x07}, 1, 10},// 设置正常模式下的亮度值
  };
#elif CURRENT_SCREEN_SIZE == AM151Q466466LK_151_466x466_C
  #define EXAMPLE_LCD_H_RES              466
  #define EXAMPLE_LCD_V_RES              466
  #define EXAMPLE_LCD_X_GAP              6
  #define EXAMPLE_LCD_Y_GAP              0
  #define TOUCH_IC_CONFIG                CST820_ID
  #define AMOLED_QSPI_MAX_PCLK           40 * 1000 * 1000
  static const qspi_amoled_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},// 退出睡眠模式
    {0x35, (uint8_t []){0x00}, 1, 0},// 开启撕裂效果
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},// SPI 模式控制
    // {0x36, (uint8_t []){0x00}, 1, 0},// 设置内存数据访问控制
    {0x3A, (uint8_t []){0x55}, 1, 0},//// 设置像素格式 16位
    {0x53, (uint8_t []){0x20}, 1, 0},// 设置 CTRL 显示1
    {0x63, (uint8_t []){0xFF}, 1, 0},// 设置 HBM 模式下的亮度值
    {0x2A, (uint8_t []){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x51, (uint8_t []){0xA0}, 1, 0},// 设置正常模式下的亮度值
    {0x58, (uint8_t []){0x07}, 1, 10},// 设置正常模式下的亮度值
  };
#elif CURRENT_SCREEN_SIZE == AM160Q480480LK_160_480x480_C
  #define EXAMPLE_LCD_H_RES              480
  #define EXAMPLE_LCD_V_RES              480
  #define EXAMPLE_LCD_X_GAP              0
  #define EXAMPLE_LCD_Y_GAP              0
  #define TOUCH_IC_CONFIG                CHSC6417_ID
  #define AMOLED_QSPI_MAX_PCLK           40 * 1000 * 1000
  static const qspi_amoled_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xF0, (uint8_t []){0x50}, 1, 0},
    {0xB1, (uint8_t []){0x78,0x70}, 2, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x36, (uint8_t []){0x00}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0x53, (uint8_t []){0x20}, 1, 0},
    {0x51, (uint8_t []){0xFF}, 1, 0},
    {0x63, (uint8_t []){0xFF}, 1, 0},
    {0x64, (uint8_t []){0x10}, 1, 0},
    {0x67, (uint8_t []){0x01}, 1, 0},
    {0x68, (uint8_t []){0x31}, 1, 0},
    {0x2A, (uint8_t []){0x00,0x00,0x01,0xdf}, 4, 0},
    {0x2B, (uint8_t []){0x00,0x00,0x01,0xdf}, 4, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0x29, (uint8_t []){0x00}, 0, 120},
  };
#elif CURRENT_SCREEN_SIZE == AM201Q240296LK_201_240x296
  #define EXAMPLE_LCD_H_RES              240
  #define EXAMPLE_LCD_V_RES              296
  #define EXAMPLE_LCD_X_GAP              0
  #define EXAMPLE_LCD_Y_GAP              0
  #define TOUCH_IC_CONFIG                CST816_ID
  #define AMOLED_QSPI_MAX_PCLK           40 * 1000 * 1000
  static const qspi_amoled_lcd_init_cmd_t lcd_init_cmds[] = {
      {0xFE, (uint8_t []){0x00}, 1, 0},
      {0xC4, (uint8_t []){0x80}, 1, 0},
      {0x35, (uint8_t []){0x00}, 1, 0},
      {0x3A, (uint8_t []){0x55}, 1, 0},
      {0x53, (uint8_t []){0x20}, 1, 0},
      {0x51, (uint8_t []){0xFF}, 1, 0},
      {0x63, (uint8_t []){0xFF}, 1, 0},
      {0x2A, (uint8_t []){0x00,0x00,0x00,0xEF}, 4, 0},
      {0x2B, (uint8_t []){0x00,0x00,0x01,0x27}, 4, 0},
      {0x11, (uint8_t []){0x00}, 0, 80},
      {0x29, (uint8_t []){0x00}, 0, 10},
  };
#elif CURRENT_SCREEN_SIZE == AM200Q460460LK_200_460x460
  #define EXAMPLE_LCD_H_RES              460
  #define EXAMPLE_LCD_V_RES              460
  #define EXAMPLE_LCD_X_GAP              10
  #define EXAMPLE_LCD_Y_GAP              0
  #define TOUCH_IC_CONFIG                CST820_ID
  #define AMOLED_QSPI_MAX_PCLK           40 * 1000 * 1000    //80M也是可行的，但杜邦线不行
  static const qspi_amoled_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},// 退出睡眠模式
    {0x35, (uint8_t []){0x00}, 1, 0},// 开启撕裂效果
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},// SPI 模式控制
    // {0x36, (uint8_t []){0x00}, 1, 0},// 设置内存数据访问控制
    {0x3A, (uint8_t []){0x55}, 1, 0},//// 设置像素格式 16位
    {0x53, (uint8_t []){0x20}, 1, 0},// 设置 CTRL 显示1
    {0x63, (uint8_t []){0xFF}, 1, 0},// 设置 HBM 模式下的亮度值
    {0x2A, (uint8_t []){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x29, (uint8_t []){0x00}, 0, 60},// 打开显示器
    {0x51, (uint8_t []){0xFF}, 1, 0},// 设置正常模式下的亮度值
    {0x58, (uint8_t []){0x07}, 1, 10},// 设置正常模式下的亮度值
  };
#else
  #error "Unsupported screen size"
#endif

// 根据TOUCH_IC_CONFIG的配置，选择调用的头文件
#if EXAMPLE_USE_TOUCH

#if TOUCH_IC_CONFIG == CST816_ID
#include "esp_lcd_touch_cst816.h"
#define TOUCH_IO_I2C_CONFIG    ESP_LCD_TOUCH_IO_I2C_CST816_CONFIG
#define esp_lcd_touch_new_i2c  esp_lcd_touch_new_i2c_cst816

#elif TOUCH_IC_CONFIG == CST820_ID
#include "esp_lcd_touch_cst820.h"
#define TOUCH_IO_I2C_CONFIG    ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG  
#define esp_lcd_touch_new_i2c  esp_lcd_touch_new_i2c_cst820

#elif TOUCH_IC_CONFIG == CHSC6417_ID
#include "esp_lcd_touch_chsc6417.h"
#define TOUCH_IO_I2C_CONFIG    ESP_LCD_TOUCH_IO_I2C_CHSC6417_CONFIG
#define esp_lcd_touch_new_i2c  esp_lcd_touch_new_i2c_chsc6417
#endif

esp_lcd_touch_handle_t tp = NULL;

#endif

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    if(atomic_load(&screen_locked)){lv_disp_flush_ready(drv);return;}
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

#if LCD_BIT_PER_PIXEL == 24
    uint8_t *to = (uint8_t *)color_map;
    uint8_t temp = 0;
    uint16_t pixel_num = (offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1);

    // Special dealing for first pixel
    temp = color_map[0].ch.blue;
    *to++ = color_map[0].ch.red;
    *to++ = color_map[0].ch.green;
    *to++ = temp;
    // Normal dealing for other pixels
    for (int i = 1; i < pixel_num; i++) {
        *to++ = color_map[i].ch.red;
        *to++ = color_map[i].ch.green;
        *to++ = color_map[i].ch.blue;
    }
#endif

    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

void example_lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;

    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of coordinate down to the nearest 2M number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    // round the end of coordinate up to the nearest 2N+1 number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

#if EXAMPLE_USE_TOUCH
static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data;
    assert(tp);
    if(screen_locked&&screen_manual){data->state=LV_INDEV_STATE_RELEASED;return;}
    static bool consume_wake_touch;

    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t tp_cnt = 0;
    /* Read data from touch controller into memory */
    esp_lcd_touch_read_data(tp);
    /* Read data from touch controller */
    bool tp_pressed = esp_lcd_touch_get_coordinates(tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);
    if (tp_pressed && tp_cnt > 0) {
        pearl_power_activity();
        if(screen_locked){atomic_store(&wake_requested,true);consume_wake_touch=true;}
        if(consume_wake_touch){data->state=LV_INDEV_STATE_RELEASED;return;}
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
        
    } else {
        consume_wake_touch=false;
        data->state = LV_INDEV_STATE_RELEASED;
    }

}
#endif

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static bool example_lvgl_lock(int timeout_ms)
{
    assert(lvgl_mux && "bsp_display_start must be called first");

    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

static void example_lvgl_unlock(void)
{
    assert(lvgl_mux && "bsp_display_start must be called first");
    xSemaphoreGive(lvgl_mux);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (example_lvgl_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            // Release the mutex
            example_lvgl_unlock();
        }
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

void pearl_console_start(pearl_library *lib);
static void library_task(void *arg);
static void buttons_task(void *arg);

void app_main(void)
{
    if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0){
        rtc_gpio_deinit(CONFIG_PEARL_BUTTON_UP);gpio_set_direction(CONFIG_PEARL_BUTTON_UP,GPIO_MODE_INPUT);gpio_pullup_en(CONFIG_PEARL_BUTTON_UP);
        int64_t pressed=esp_timer_get_time();while(!gpio_get_level(CONFIG_PEARL_BUTTON_UP)&&esp_timer_get_time()-pressed<1800000)vTaskDelay(pdMS_TO_TICKS(20));
        if(gpio_get_level(CONFIG_PEARL_BUTTON_UP)){esp_sleep_enable_ext0_wakeup(CONFIG_PEARL_BUTTON_UP,0);rtc_gpio_pullup_en(CONFIG_PEARL_BUTTON_UP);rtc_gpio_pulldown_dis(CONFIG_PEARL_BUTTON_UP);esp_deep_sleep_start();}
    }
    gpio_deep_sleep_hold_dis();gpio_hold_dis(GPIO_NUM_41);
    pearl_power_activity();
    library_access=xSemaphoreCreateMutex();
    esp_err_t nvs_err=nvs_flash_init();
    if(nvs_err!=ESP_OK)ESP_LOGW("pearl","Preferences unavailable: %s",esp_err_to_name(nvs_err));
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    static lv_disp_drv_t disp_drv;      // contains callback functions

    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = QSPI_AMOLED_PANEL_BUS_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_PCLK,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA0,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA1,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA2,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA3,
                                                                 EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = QSPI_AMOLED_PANEL_IO_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_CS,
                                                                                example_notify_lvgl_flush_ready,
                                                                                &disp_drv);

    io_config.pclk_hz = (unsigned int)AMOLED_QSPI_MAX_PCLK;
    qspi_amoled_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_LOGI(TAG, "Install QSPI_AMOLED panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_qspi_amoled(io_handle, &panel_config, &panel_handle));
    player_panel=panel_handle;
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, EXAMPLE_LCD_X_GAP, EXAMPLE_LCD_Y_GAP));
    // 在打开屏幕或背光之前，用户可以将预定义的图案刷新到屏幕上
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    // 设置屏幕亮度
    ESP_ERROR_CHECK(panel_qspi_amoled_set_brightness(panel_handle, CONFIG_PEARL_BRIGHTNESS)); // 设置亮度为 15

#if EXAMPLE_USE_TOUCH
    ESP_LOGI(TAG, "Initialize I2C bus");
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 200 * 1000,
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_HOST, &i2c_conf));
    // 设置 I2C 超时时间
    // Keep the driver timeout; 2 APB cycles is too short for touch/DAC transactions. // 0x02: 2ms
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_HOST, i2c_conf.mode, 0, 0, 0));

    // Scan I2C devices
    // No full bus scan on the critical startup path.

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    // const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    const esp_lcd_panel_io_i2c_config_t tp_io_config = TOUCH_IO_I2C_CONFIG();
    // Attach the TOUCH to the I2C bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_HOST, &tp_io_config, &tp_io_handle));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST,
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_LOGI(TAG, "Initialize touch controller");
    // ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst820(tp_io_handle, &tp_cfg, &tp));
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c(tp_io_handle, &tp_cfg, &tp));
#endif

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);
    lv_color_t *buf2 = heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.rounder_cb = example_lvgl_rounder_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;

    // 设置旋转角度
    // disp_drv.rotated = LV_DISP_ROT_90; // 旋转90度
    
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

#if EXAMPLE_USE_TOUCH
    static lv_indev_drv_t indev_drv;    // Input device driver (Touch)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_lvgl_touch_cb;
    indev_drv.user_data = tp;
    lv_indev_drv_register(&indev_drv);
#endif

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Display LVGL demos");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (example_lvgl_lock(-1)) {

        pearl_ui_start();
        // lv_demo_music();        /* A modern, smartphone-like music player demo. */
        // lv_demo_stress();       /* A stress test for LVGL. */
        // lv_demo_benchmark();    /* A demo to measure the performance of LVGL or to compare different settings. */

        // Release the mutex
        example_lvgl_unlock();
    }
    xTaskCreate(library_task,"library",8192,NULL,2,NULL);
    xTaskCreate(buttons_task,"buttons",4096,NULL,3,NULL);

    ESP_LOGW("pearl","UI ready at %lld ms",esp_timer_get_time()/1000);
}

static void library_task(void *arg)
{
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.max_freq_khz=SDMMC_FREQ_HIGHSPEED;
    /* ESP32-S3 SDMMC cannot DMA from PSRAM. The zero default otherwise
     * turns each buffered write into individual 512-byte card commands. */
    void *sd_dma=heap_caps_malloc(8192,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    host.dma_aligned_buffer=sd_dma;
    host.unaligned_multi_block_rw_max_chunk_size=16;
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=4;
    slot.clk=16;slot.cmd=17;slot.d0=15;slot.d1=14;slot.d2=21;slot.d3=18;
    slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t cfg={.format_if_mount_failed=false,.max_files=12,.allocation_unit_size=16384};
    sdmmc_card_t *card=NULL;esp_err_t e=esp_vfs_fat_sdmmc_mount("/sdcard",&host,&slot,&cfg,&card);
    if(e!=ESP_OK){free(sd_dma);sd_dma=NULL;}
    else ESP_LOGW("pearl","SDMMC actual_khz=%d bus_width=%d sector=%d bounce_bytes=%u chunk_sectors=%u",card->real_freq_khz,card->log_bus_width==2?4:1,card->csd.sector_size,sd_dma?8192u:0u,16u);
    const char *err="";
    if(e!=ESP_OK)err="Card not ready. Insert a FAT32 card and restart.";
    else if(pearl_library_scan(&music,"/sdcard/music"))err="Add a music folder to your card, then restart.";
    else {pearl_audio_start(&music);audio_started=true;}
    if(example_lvgl_lock(-1)){pearl_ui_ready(&music,err);example_lvgl_unlock();}
    atomic_store(&library_albums,music.album_count);atomic_store(&library_tracks,music.track_count);
    pearl_network_init();
    pearl_console_start(&music);
    atomic_store(&library_ready,true);
    ESP_LOGW("pearl","Library ready at %lld ms: %u albums, %u tracks; %s",esp_timer_get_time()/1000,music.album_count,music.track_count,err);
    vTaskDelete(NULL);
}
static void rescan_task(void *arg){
    if(!audio_started||!pearl_audio_detach()){if(example_lvgl_lock(-1)){pearl_ui_ready(&music,"Cannot rescan now. Restart with the card inserted.");example_lvgl_unlock();}rescan_running=false;vTaskDelete(NULL);return;}
    if(example_lvgl_lock(-1)){pearl_ui_scanning();example_lvgl_unlock();}
    pearl_library next={0};int result=pearl_library_scan(&next,"/sdcard/music");
    if(!result){if(pearl_library_lock()){pearl_library old=music;music=next;pearl_library_free(&old);pearl_library_unlock();}else{pearl_library_free(&next);result=-3;}}
    atomic_store(&library_albums,music.album_count);atomic_store(&library_tracks,music.track_count);
    pearl_audio_attach(&music);
    if(example_lvgl_lock(-1)){pearl_ui_ready(&music,result?"Scan failed. Previous library kept; check card or free memory.":"");example_lvgl_unlock();}
    ESP_LOGW("pearl","Rescan result=%d albums=%u tracks=%u",result,music.album_count,music.track_count);
    rescan_running=false;vTaskDelete(NULL);
}
void pearl_library_rescan(void){if(atomic_exchange(&rescan_running,true))return;if(xTaskCreate(rescan_task,"rescan",8192,NULL,2,&rescan_task_handle)!=pdPASS)rescan_running=false;}
static void buttons_task(void *arg)
{
    const int up=CONFIG_PEARL_BUTTON_UP,down=CONFIG_PEARL_BUTTON_DOWN;
    gpio_config_t cfg={.pin_bit_mask=(1ULL<<up)|(1ULL<<down),.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};gpio_config(&cfg);
    pearl_button ub={0},db={0};
    uint32_t paused_since=esp_timer_get_time()/1000;bool was_playing=false;
    // Don't interpret a held boot/power button as a second shutdown request.
    while(!gpio_get_level(up))vTaskDelay(pdMS_TO_TICKS(20));
    while(1){
        uint32_t now=esp_timer_get_time()/1000;
        if(atomic_exchange(&wake_requested,false)){display_sleep(false,false);pearl_power_activity();}
        pearl_button_event u=pearl_button_update(&ub,!gpio_get_level(up),now),d=pearl_button_update(&db,!gpio_get_level(down),now);
        if(u!=BUTTON_NONE||d!=BUTTON_NONE){pearl_power_activity();if(screen_locked&&!screen_manual)display_sleep(false,false);}
        if(u==BUTTON_SHORT){pearl_audio_volume(-2);ESP_LOGW("pearl","Volume down GPIO%d",up);}
        if(d==BUTTON_SHORT){pearl_audio_volume(2);ESP_LOGW("pearl","Volume up GPIO%d",down);}
        if(d==BUTTON_LONG){display_sleep(!screen_locked,!screen_locked);}
        if(u==BUTTON_LONG)enter_standby();
        pearl_state playback=pearl_audio_state();bool playing=playback.ready&&!playback.paused;
        if(playing||was_playing){paused_since=now;}
        was_playing=playing;
        pearl_power_input policy={.now=now,.last_activity=pearl_power_last_activity(),.paused_since=paused_since,.screen_timeout=CONFIG_PEARL_SCREEN_TIMEOUT_SEC*1000u,.idle_timeout=CONFIG_PEARL_IDLE_SLEEP_SEC*1000u,.playing=playing,.screen_asleep=screen_locked,.network=pearl_network_enabled(),.busy=pearl_sync_busy()||atomic_load(&rescan_running)||!atomic_load(&library_ready),.usb_connected=usb_serial_jtag_is_connected(),.button_released=gpio_get_level(up)&&gpio_get_level(down),.deep_supported=pearl_power_deep_supported()};
        pearl_power_action action=pearl_power_decide(&policy);
        if(action==PEARL_POWER_SCREEN_SLEEP)display_sleep(true,false);
        if(action==PEARL_POWER_DEEP_SLEEP)enter_standby();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void display_sleep(bool asleep,bool manual){
    if(!player_panel||!example_lvgl_lock(-1))return;
    if(asleep){screen_locked=true;screen_manual=manual;pearl_ui_power(true);esp_lcd_panel_disp_on_off(player_panel,false);panel_qspi_amoled_sleep(player_panel,true);}
    else{panel_qspi_amoled_sleep(player_panel,false);vTaskDelay(pdMS_TO_TICKS(120));esp_lcd_panel_disp_on_off(player_panel,true);screen_locked=false;screen_manual=false;pearl_ui_power(false);lv_obj_invalidate(lv_scr_act());}
    example_lvgl_unlock();
}
static void enter_standby(void){
    if(!pearl_sync_shutdown()){ESP_LOGW("pearl","Sync closing; hold again to sleep.");pearl_power_activity();return;}
    if(!pearl_network_shutdown()){ESP_LOGW("pearl","WiFi shutdown pending; hold again to sleep.");pearl_power_activity();return;}
    const int up=CONFIG_PEARL_BUTTON_UP;
    if(pearl_power_deep_supported()&&esp_sleep_enable_ext0_wakeup(up,0)!=ESP_OK){ESP_LOGW("pearl","Wake configuration failed; staying awake.");pearl_power_activity();return;}
    pearl_audio_shutdown();display_sleep(true,true);
    while(!gpio_get_level(up))vTaskDelay(pdMS_TO_TICKS(20));
    if(pearl_power_deep_supported()){
        rtc_gpio_pullup_en(up);rtc_gpio_pulldown_dis(up);gpio_hold_en(GPIO_NUM_41);gpio_deep_sleep_hold_en();
        ESP_LOGW("pearl","Deep sleep; hold GPIO%d to wake.",up);esp_deep_sleep_start();
    }
    gpio_wakeup_enable(up,GPIO_INTR_LOW_LEVEL);esp_sleep_enable_gpio_wakeup();
    for(;;){esp_light_sleep_start();uint32_t start=esp_timer_get_time()/1000;while(!gpio_get_level(up)){if((uint32_t)(esp_timer_get_time()/1000)-start>=1800)esp_restart();vTaskDelay(pdMS_TO_TICKS(20));}}
}

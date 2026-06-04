#include "mpu6050.h"
#include "config.h"

#include <math.h>
#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MPU6050";

// Offset hiệu chỉnh
static float off_ax = 0, off_ay = 0, off_az = 0;
static float off_gx = 0, off_gy = 0, off_gz = 0;

//  I2C helpers (thay thế Wire.beginTransmission / write / requestFrom)

static esp_err_t mpu_write_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t mpu_read_regs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    // Write register address
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    // Repeated start – read phase
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1)
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static inline int16_t to_int16(uint8_t hi, uint8_t lo)
{
    return (int16_t)((hi << 8) | lo);
}

//  Init
esp_err_t mpu_init(void)
{
    // Cấu hình I2C master (thay Wire.begin)
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));
    vTaskDelay(pdMS_TO_TICKS(100));

    // Wake up MPU (xóa bit sleep – thay mpuWrite(0x6B, 0x00))
    ESP_ERROR_CHECK(mpu_write_reg(MPU_REG_PWR_MGMT_1, 0x00));
    vTaskDelay(pdMS_TO_TICKS(100));

    // Kiểm tra WHO_AM_I
    uint8_t whoami = 0;
    mpu_read_regs(MPU_REG_WHO_AM_I, &whoami, 1);
    if (whoami == 0x68)
        ESP_LOGI(TAG, "Chip: MPU-6050 (0x%02X)", whoami);
    else if (whoami == 0x70)
        ESP_LOGI(TAG, "Chip: MPU-6500 (0x%02X)", whoami);
    else
    {
        ESP_LOGE(TAG, "Khong nhan dien chip: 0x%02X", whoami);
        return ESP_ERR_NOT_FOUND;
    }

    // Cấu hình range và DLPF
    ESP_ERROR_CHECK(mpu_write_reg(MPU_REG_ACCEL_CFG, 0x10)); // ±8g
    ESP_ERROR_CHECK(mpu_write_reg(MPU_REG_GYRO_CFG, 0x08));  // ±500°/s
    ESP_ERROR_CHECK(mpu_write_reg(MPU_REG_CONFIG, 0x04));    // DLPF 21Hz

    ESP_LOGI(TAG, "MPU init OK");
    return ESP_OK;
}

//  Hiệu chỉnh offset (thay calibrateMPU)
void mpu_calibrate(void)
{
    ESP_LOGI(TAG, "=== HIEU CHINH OFFSET ===");
    ESP_LOGI(TAG, ">>> Dat cam bien NAM YEN...");

    for (int i = 3; i > 0; i--)
    {
        ESP_LOGI(TAG, "    Bat dau sau %d giay...", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    const int N = 200;
    double sax = 0, say = 0, saz = 0;
    double sgx = 0, sgy = 0, sgz = 0;

    for (int i = 0; i < N; i++)
    {
        uint8_t buf[14];
        mpu_read_regs(MPU_REG_ACCEL_XOUT, buf, 14);
        sax += to_int16(buf[0], buf[1]) / 4096.0;
        say += to_int16(buf[2], buf[3]) / 4096.0;
        saz += to_int16(buf[4], buf[5]) / 4096.0;
        sgx += to_int16(buf[8], buf[9]) / 65.5;
        sgy += to_int16(buf[10], buf[11]) / 65.5;
        sgz += to_int16(buf[12], buf[13]) / 65.5;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    off_ax = (float)(sax / N);
    off_ay = (float)(say / N);
    off_az = (float)(saz / N) - 1.0f; // trừ 1g trục đứng
    off_gx = (float)(sgx / N);
    off_gy = (float)(sgy / N);
    off_gz = (float)(sgz / N);

    ESP_LOGI(TAG, "Offset Acc  X:%+.4f Y:%+.4f Z:%+.4f g", off_ax, off_ay, off_az);
    ESP_LOGI(TAG, "Offset Gyro X:%+.4f Y:%+.4f Z:%+.4f d/s", off_gx, off_gy, off_gz);
    ESP_LOGI(TAG, "Hieu chinh XONG!");
}

//  Đọc IMU (thay readImu)
imu_data_t mpu_read(void)
{
    uint8_t buf[14];
    mpu_read_regs(MPU_REG_ACCEL_XOUT, buf, 14);

    imu_data_t d;
    d.ax = to_int16(buf[0], buf[1]) / 4096.0f - off_ax;
    d.ay = to_int16(buf[2], buf[3]) / 4096.0f - off_ay;
    d.az = to_int16(buf[4], buf[5]) / 4096.0f - off_az;
    d.gx = to_int16(buf[8], buf[9]) / 65.5f - off_gx;
    d.gy = to_int16(buf[10], buf[11]) / 65.5f - off_gy;
    d.gz = to_int16(buf[12], buf[13]) / 65.5f - off_gz;
    d.a_total = sqrtf(d.ax * d.ax + d.ay * d.ay + d.az * d.az);
    d.g_total = sqrtf(d.gx * d.gx + d.gy * d.gy + d.gz * d.gz);
    return d;
}

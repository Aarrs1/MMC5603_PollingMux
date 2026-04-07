#include <Wire.h>
#include <Arduino.h>

#define I2C_ADDR_1 0x70  // 第一个多路复用器的I2C地址（7位）
#define I2C_ADDR_2 0x73  // 第二个多路复用器的I2C地址（7位）
#define SDA_PIN 3      // SDA引脚
#define SCL_PIN 8      // SCL引脚
#define MMC5603_ADDR 0x30  // MMC5603传感器的I2C地址

// 状态机枚举
enum State {
  START_MEASURE,  // 启动所有传感器测量
  WAIT,           // 等待测量完成（非阻塞）
  READ            // 读取所有传感器数据
};

State state = START_MEASURE;
uint32_t t_start;

// 数据缓冲区（储存16个传感器的数据）
struct SensorData {
  uint32_t x, y, z;
} sensor_data[16];

void writeReg(uint8_t reg, uint8_t val) {// 写寄存器函数
    Wire.beginTransmission(MMC5603_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t readReg(uint8_t reg) {// 读寄存器函数
    Wire.beginTransmission(MMC5603_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom((uint8_t)MMC5603_ADDR, (uint8_t)1);
    return Wire.read();
}

void readMulti(uint8_t reg, uint8_t* buf, uint8_t len) {// 读多个寄存器函数
    Wire.beginTransmission(MMC5603_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom((uint8_t)MMC5603_ADDR, len);
    for (int i = 0; i < len; i++) {
        buf[i] = Wire.read();
    }
}

// 初始化I2C
void setup() {
  Serial.begin(921600);
  Wire.begin(SDA_PIN, SCL_PIN, 400000);  // 设置SDA和SCL引脚，400kHz速率
  delay(10);
  
  // 初始化所有MMC5603传感器
  for (uint8_t mux = 0; mux < 2; mux++) {
    uint8_t addr = (mux == 0) ? I2C_ADDR_1 : I2C_ADDR_2;
    for (uint8_t ch = 0; ch < 8; ch++) {
      selectChannel(addr, ch);
      writeReg(0x1C, 0x03);      // BW=11, 带宽最大 → 测量时间~1.2ms
      writeReg(0x1B, 0x20);      // Auto_SR_en=1, 启用自动SET/RESET（自动去磁）
    }
  }
  
  Serial.println("I2C Multiplexer & MMC5603 Initialized with Auto SET/RESET");
}

// 启用指定通道
void selectChannel(uint8_t addr, uint8_t channel) {
  uint8_t controlByte = 0;  // 初始化控制字节，所有位先置为0

  // 设置对应通道的位为1
  // 例如：如果选择第3个通道，那么控制字节应该是 0b00001000 (即8)
  controlByte = 1 << channel;  // 左移1到指定的通道位置
  
  // 通过I2C发送控制字节
  Wire.beginTransmission(addr);  // 向指定I2C地址发送数据
  Wire.write(controlByte);       // 发送控制字节，启用选定的通道
  Wire.endTransmission();        // 结束I2C通信
  delayMicroseconds(5);          // MUX切换延迟（PCA9548A < 1µs）
}

// 启动单个传感器测量
void startMeasurement(uint8_t mux_addr, uint8_t channel) {
  selectChannel(mux_addr, channel);
  writeReg(0x1B, 0x21);  // bit0=1(Take_meas_M) + bit5=1(Auto_SR_en)
}

// 读取单个传感器数据（不等待）
void readSensorData(uint8_t mux_addr, uint8_t channel, uint32_t *x, uint32_t *y, uint32_t *z) {
  selectChannel(mux_addr, channel);

  // 读取数据
  uint8_t buf[9];
  readMulti(0x00, buf, 9);

  // 拼接数据
  *x = ((uint32_t)buf[0] << 12) |
       ((uint32_t)buf[1] << 4)  |
       ((uint32_t)(buf[6] & 0x0F));

  *y = ((uint32_t)buf[2] << 12) |
       ((uint32_t)buf[3] << 4)  |
       ((uint32_t)(buf[7] & 0x0F));

  *z = ((uint32_t)buf[4] << 12) |
       ((uint32_t)buf[5] << 4)  |
       ((uint32_t)(buf[8] & 0x0F));
}

void loop() {
  // 非阻塞状态机（16个传感器并行测量）
  
  if (state == START_MEASURE) {
    // 第1阶段：一次性启动全部16个传感器的测量
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t mux_idx = i / 8;
      uint8_t ch = i % 8;
      uint8_t addr = (mux_idx == 0) ? I2C_ADDR_1 : I2C_ADDR_2;
      
      startMeasurement(addr, ch);
    }
    
    // 记录时间戳，转入等待状态
    t_start = micros();
    state = WAIT;
  }
  
  else if (state == WAIT) {
    // 第2阶段：非阻塞等待
    // 转换时间1.2ms + I2C/MUX开销 ≈ 1.5-1.8ms
    // 工程建议：2000µs（保证最慢器件完成）
    if (micros() - t_start > 5000) {
      state = READ;
    }
  }
  
  else if (state == READ) {
    // 第3阶段：一次性读取全部16个传感器数据
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t mux_idx = i / 8;
      uint8_t ch = i % 8;
      uint8_t addr = (mux_idx == 0) ? I2C_ADDR_1 : I2C_ADDR_2;
      
      readSensorData(addr, ch, &sensor_data[i].x, &sensor_data[i].y, &sensor_data[i].z);
    }
    
    // 输出一行帧数据（CSV格式）- 避免printf的格式解析开销
    Serial.print("F,");
    for (uint8_t i = 0; i < 16; i++) {
      Serial.print(sensor_data[i].x);
      Serial.write(',');
      Serial.print(sensor_data[i].y);
      Serial.write(',');
      Serial.print(sensor_data[i].z);
      if (i < 15) Serial.write(',');
    }
    Serial.println();
    
    // 回到START_MEASURE状态，循环
    state = START_MEASURE;
  }
}
#include <Wire.h>
#include <Arduino.h>

// 多路复用器地址
#define I2C_ADDR_1 0x70  // I2C总线0
#define I2C_ADDR_2 0x73  // I2C总线1

// I2C引脚
#define SDA1_PIN 3
#define SCL1_PIN 8
#define SDA2_PIN 9
#define SCL2_PIN 10

#define MMC5603_ADDR 0x30  // 传感器地址

// 第二条I2C总线实例
TwoWire I2Cone = TwoWire(1);

// 状态机
enum State { START_MEASURE, WAIT, READ };
State state = START_MEASURE;
uint32_t t_start;

// 数据缓冲区
struct SensorData { uint32_t x, y, z; } sensor_data[16];

// ---------- I2C操作函数 ----------
void writeReg(TwoWire &bus, uint8_t reg, uint8_t val){
    bus.beginTransmission(MMC5603_ADDR);
    bus.write(reg);
    bus.write(val);
    bus.endTransmission();
}

uint8_t readReg(TwoWire &bus, uint8_t reg){
    bus.beginTransmission(MMC5603_ADDR);
    bus.write(reg);
    bus.endTransmission(false);
    bus.requestFrom((uint8_t)MMC5603_ADDR, (uint8_t)1);
    return bus.read();
}

void readMulti(TwoWire &bus, uint8_t reg, uint8_t* buf, uint8_t len){
    bus.beginTransmission(MMC5603_ADDR);
    bus.write(reg);
    bus.endTransmission(false);
    bus.requestFrom((uint8_t)MMC5603_ADDR, len);
    for(int i=0;i<len;i++) buf[i] = bus.read();
}

// ---------- 多路复用器 ----------
void selectChannel(TwoWire &bus, uint8_t addr, uint8_t channel){
    uint8_t controlByte = 1 << channel;
    bus.beginTransmission(addr);
    bus.write(controlByte);
    bus.endTransmission();
    delayMicroseconds(5);
}

// ---------- 传感器测量 ----------
void startMeasurement(TwoWire &bus, uint8_t mux_addr, uint8_t channel){
    selectChannel(bus, mux_addr, channel);
    writeReg(bus, 0x1B, 0x21); // 启动测量 + 自动去磁
}

void readSensorData(TwoWire &bus, uint8_t mux_addr, uint8_t channel, uint32_t *x, uint32_t *y, uint32_t *z){
    selectChannel(bus, mux_addr, channel);
    uint8_t buf[9];
    readMulti(bus, 0x00, buf, 9);
    *x = ((uint32_t)buf[0]<<12) | ((uint32_t)buf[1]<<4) | (buf[6]&0x0F);
    *y = ((uint32_t)buf[2]<<12) | ((uint32_t)buf[3]<<4) | (buf[7]&0x0F);
    *z = ((uint32_t)buf[4]<<12) | ((uint32_t)buf[5]<<4) | (buf[8]&0x0F);
}

// ---------- 初始化 ----------
void setup(){
    Serial.begin(921600);
    Wire.begin(SDA1_PIN, SCL1_PIN, 400000);
    I2Cone.begin(SDA2_PIN, SCL2_PIN, 400000);
    delay(10);

    // 初始化传感器
    for(uint8_t mux=0; mux<2; mux++){
        TwoWire &bus = (mux==0)?Wire:I2Cone;
        uint8_t addr = (mux==0)?I2C_ADDR_1:I2C_ADDR_2;
        for(uint8_t ch=0; ch<8; ch++){
            selectChannel(bus, addr, ch);
            writeReg(bus, 0x1C, 0x03); // 最大带宽
            writeReg(bus, 0x1B, 0x20); // 自动去磁
        }
    }
    Serial.println("Two I2C Buses & MMC5603 Initialized");
}

// ---------- 主循环 ----------
void loop(){
    if(state == START_MEASURE){
        // 同时启动两条总线的传感器测量
        for(uint8_t i=0;i<16;i++){
            TwoWire &bus = (i<8)?Wire:I2Cone;
            uint8_t addr = (i<8)?I2C_ADDR_1:I2C_ADDR_2;
            uint8_t ch = i%8;
            startMeasurement(bus, addr, ch);
        }
        t_start = micros();
        state = WAIT;
    }
    else if(state == WAIT){
        if(micros()-t_start > 4000){ // 4ms即可测量完成
            state = READ;
        }
    }
    else if(state == READ){
        // 同时读取16个传感器数据
        for(uint8_t i=0;i<16;i++){
            TwoWire &bus = (i<8)?Wire:I2Cone;
            uint8_t addr = (i<8)?I2C_ADDR_1:I2C_ADDR_2;
            uint8_t ch = i%8;
            readSensorData(bus, addr, ch, &sensor_data[i].x, &sensor_data[i].y, &sensor_data[i].z);
        }

        // 输出CSV
        Serial.print("F,");
        for(uint8_t i=0;i<16;i++){
            Serial.print(sensor_data[i].x); Serial.write(',');
            Serial.print(sensor_data[i].y); Serial.write(',');
            Serial.print(sensor_data[i].z);
            if(i<15) Serial.write(',');
        }
        Serial.println();

        state = START_MEASURE; // 循环
    }
}
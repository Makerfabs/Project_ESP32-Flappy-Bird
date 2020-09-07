#include "touch.h"

int readTouchReg(int reg)
{
    int data = 0;
    Wire.beginTransmission(TOUCH_I2C_ADD);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(TOUCH_I2C_ADD, 1);
    if (Wire.available())
    {
        data = Wire.read();
    }
    return data;
}

int get_button()
{
    int status = 0;

    status = readTouchReg(TOUCH_REG_XH);
    if (status >> 6 == 1)
        return 0;
    else
        return 1;
}
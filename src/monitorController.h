#pragma once
#include <U8g2lib.h>
#include <Arduino.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);

class MonitorController
{
private:
    int width = 72;
    int height = 40;
    int xOffset = 28;
    int yOffset = 24;
    unsigned long lastDisplayTime = 0;
    bool displayOn = true;

public:
    void begin()
    {
        u8g2.begin();
        u8g2.setContrast(255);
        u8g2.setBusClock(400000);
    }

    void loop()
    {
        if (millis() - lastDisplayTime > 10000 && displayOn)
        {
            u8g2.clearBuffer();
            u8g2.sendBuffer();
            displayOn = false;
            u8g2.setPowerSave(1); // turn off display
        }
        else if (millis() - lastDisplayTime <= 30000 && !displayOn)
        {
            displayOn = true;
            u8g2.setPowerSave(0); // turn on display
        }
    }

    void displayMessage(const String &message, int yOffsetDelta = 0)
    {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenR08_te);
        int newlinePos = message.indexOf('\n');
        int currentOffset = yOffset + 10 + yOffsetDelta;
        int lineHeight = 10;
        int lineIndex = 0;
        int startIndex = 0;
        while (newlinePos != -1)
        {
            String line = message.substring(startIndex, newlinePos);
            u8g2.setCursor(xOffset, currentOffset + lineIndex * lineHeight);
            u8g2.print(line);
            startIndex = newlinePos + 1;
            newlinePos = message.indexOf('\n', startIndex);
            lineIndex++;
        }
        // print last line
        String line = message.substring(startIndex);
        u8g2.setCursor(xOffset, currentOffset + lineIndex * lineHeight);
        u8g2.setFont(u8g2_font_ncenR08_te);
        u8g2.print(line);
        u8g2.sendBuffer();
        lastDisplayTime = millis();
    }
};
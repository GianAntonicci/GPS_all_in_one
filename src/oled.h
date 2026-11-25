#pragma once
// OLED SSD1306 128x32 (status). Linhas vindas de oledLines() (ui_format.h).
bool oledBegin();
void oledShow(const char* line1, const char* line2);

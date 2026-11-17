/**
 * @file oled.h
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief Definitions for OLED
 * @version 0.1
 * @date 2026-11-17
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#include <Arduino.h>

// OLED
#include <nRF_SSD1306Wire.h>
bool init_rak1921(void);
void rak1921_add_line(char *line);
void rak1921_show(void);
void rak1921_write_header(char *header_line);
void rak1921_clear(void);
void rak1921_write_line(int16_t line, int16_t y_pos, String text);
void rak1921_display(void);
extern char line_str[];
extern bool has_rak1921;

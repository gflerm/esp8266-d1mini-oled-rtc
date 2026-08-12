#ifndef SSD1306_CONFIG_H
#define SSD1306_CONFIG_H

/* Panel geometry. The Wemos/LOLIN D1 mini OLED shield is a 0.66" 64x48
 * SSD1306. For a 128x64 panel, override both at build time, e.g. in
 * components/ssd1306/CMakeLists.txt add:
 *   target_compile_definitions(ssd1306 PUBLIC SSD1306_WIDTH=128 SSD1306_HEIGHT=64)
 */
#ifndef SSD1306_WIDTH
#define SSD1306_WIDTH 64
#endif

#ifndef SSD1306_HEIGHT
#define SSD1306_HEIGHT 48
#endif

#define SSD1306_PAGES (SSD1306_HEIGHT / 8)

/* GDDRAM column where the active 64x48 area starts on these panels
 * (EastRising 0.66" / D1 mini shield). u8g2 uses default_x_offset = 32. */
#ifndef SSD1306_COLUMN_OFFSET
#define SSD1306_COLUMN_OFFSET 32
#endif

#endif /* SSD1306_CONFIG_H */
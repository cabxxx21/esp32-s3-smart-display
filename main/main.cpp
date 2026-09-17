#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include <LovyanGFX.hpp>

// Global State
volatile int currentPage = 0;
volatile int maxPages = 3;

char logTypes[10][8];
char logTexts[10][128];
int logTail = 0;

char cpuVal[8] = "0";
char ramVal[8] = "0";
char diskVal[8] = "0";
char cpuTemp[8] = "0";
char gpuTemp[8] = "0";

char localIP[32] = "0.0.0.0";
char pubIP[32] = "0.0.0.0";

char musicTitle[64] = "Waiting...";
char musicArtist[64] = "Larp Linux";
char musicStatus[16] = "stopped";
char lyricsLines[3][80] = {"...", "...", "..."};
long pos_ms = 0;
long dur_ms = 0;
volatile bool redrawNeeded = true;

uint8_t *img_buf = nullptr;
volatile bool imgReady = false;

// TFT ILI9488 Setup
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;

 public:
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = 40000000; 
      cfg.pin_sclk = 12;
      cfg.pin_mosi = 11;
      cfg.pin_miso = 13;
      cfg.pin_dc = 9; 
      _bus_instance.config(cfg);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 10;
      cfg.pin_rst = 14;
      cfg.panel_width  = 320;
      cfg.panel_height = 480;
      _panel_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};

LGFX tft;

void draw_xp_button(lgfx::LGFX_Sprite& sprite, int x, int y, int w, int h, uint32_t col_grey, uint32_t col_dark, uint32_t col_border, uint32_t col_text) {
    sprite.fillRoundRect(x, y, w, h, 3, col_border);
    sprite.fillRoundRect(x+1, y+1, w-2, h-2, 2, col_dark);
    sprite.fillRoundRect(x+1, y+1, w-2, h/2, 2, col_grey); // Glossy top
}

// Task 1: Read serial in Core 0
void serial_task(void *pvParameters) {
  char line[256]; 
  int idx = 0; 
  int c;
  
  while (1) {
    c = getchar(); 
    if (c != EOF) {
      if (c == '\n') {
        line[idx] = '\0'; 
        idx = 0;
        
        if (strncmp(line, "MUS:", 4) == 0) {
          sscanf(line, "MUS:%[^|]|%[^|]|%s", musicTitle, musicArtist, musicStatus);
          redrawNeeded = true;
        } 
        else if (strncmp(line, "PRG:", 4) == 0) {
          sscanf(line, "PRG:%ld|%ld", &pos_ms, &dur_ms);
          redrawNeeded = true;
        }
        else if (strncmp(line, "LYR:", 4) == 0) {
          int line_num;
          char lyric_text[80];
          sscanf(line, "LYR:%d|%[^\n]", &line_num, lyric_text);
          if(line_num >= 0 && line_num < 3) {
            strcpy(lyricsLines[line_num], lyric_text);
            redrawNeeded = true;
          }
        }         
        else if (strncmp(line, "MAXPAGES:", 9) == 0) {
          maxPages = atoi(line + 9);
          if (currentPage >= maxPages) currentPage = 0;
          redrawNeeded = true;
        }
        else if (strncmp(line, "PAGE:", 5) == 0) {
          int reqPage = atoi(line + 5);
          if (reqPage >= 0 && reqPage < maxPages) {
            currentPage = reqPage;
            redrawNeeded = true;
          }
        }
        else if (strncmp(line, "LOG:", 4) == 0) {
          sscanf(line, "LOG:%[^|]|%[^\n]", logTypes[logTail], logTexts[logTail]);
          logTail = (logTail + 1) % 10;
          redrawNeeded = true;
        }
        else if (strncmp(line, "SYS:", 4) == 0) {
          sscanf(line, "SYS:%[^|]|%[^|]|%[^|]|%[^|]|%s", cpuVal, ramVal, diskVal, cpuTemp, gpuTemp);
          redrawNeeded = true;
        }
        else if (strncmp(line, "NET:", 4) == 0) {
          sscanf(line, "NET:%[^|]|%s", localIP, pubIP);
          redrawNeeded = true;
        }
        else if (strcmp(line, "IMG:") == 0) {
          if (img_buf == nullptr) {
            img_buf = (uint8_t*)heap_caps_malloc(38088, MALLOC_CAP_SPIRAM);
          }
          printf("OK\n"); fflush(stdout);
         
          int bytes_read = 0;
          int next_ack = 1024;
          while (bytes_read < 38088) {
            int ic = getchar();
            if (ic != EOF) {
              if(img_buf != nullptr) img_buf[bytes_read++] = (uint8_t)ic;
              if (bytes_read == next_ack) {
                printf("OK\n"); fflush(stdout);
                next_ack += 1024;
              }
            } else {
              vTaskDelay(1 / portTICK_PERIOD_MS);
            }
          }
          imgReady = true;
          redrawNeeded = true;
        }
      } else if (idx < 255) {
        line[idx++] = (char)c;
      } 
    }
  }
}

// Task 2: Render UI in Core 1
void ui_task(void *pvParameters) {
  lgfx::LGFX_Sprite sprite(&tft);
  sprite.setPsram(true);
  sprite.createSprite(480, 320);

  while (1) {
    if (redrawNeeded) {
      redrawNeeded = false; 
      
      // PAGE 0: SPOTIFY (XP NOIR THEME)
      if (currentPage == 0) {
        uint32_t col_bg      = tft.color888(43, 43, 43);
        uint32_t col_white   = tft.color888(255, 255, 255);
        uint32_t col_grey    = tft.color888(170, 170, 170);
        uint32_t col_sunken  = tft.color888(58, 58, 58);
        uint32_t col_border_d= tft.color888(0, 0, 0);
        uint32_t col_border_l= tft.color888(85, 85, 85);
        uint32_t col_btn_gr  = tft.color888(119, 119, 119);
        uint32_t col_btn_dk  = tft.color888(51, 51, 51);
        uint32_t col_btn_brd = tft.color888(17, 17, 17);
        uint32_t col_green   = tft.color888(166, 227, 161);
        
        sprite.fillSprite(col_bg);
        
        // 1. Title Bar Gradient
        for(int i=0; i<28; i++) {
            float t = (float)i / 28.0f;
            uint8_t r = 74 + (30 - 74) * t;
            uint8_t g = 74 + (30 - 74) * t;
            uint8_t b = 106 + (48 - 106) * t;
            sprite.drawFastHLine(0, i, 480, tft.color888(r, g, b));
        }
        
        // Title Bar Text & Status
        bool is_playing = (strcmp(musicStatus, "playing") == 0);
        sprite.setTextColor(col_white);
        sprite.setTextDatum(TL_DATUM);
        sprite.drawString("> Spotify Media Player", 10, 7, &fonts::Font2);
        sprite.setTextColor(is_playing ? col_green : col_grey);
        sprite.setTextDatum(TR_DATUM);
        sprite.drawString(musicStatus, 460, 7, &fonts::Font2);

        // 2. Left Column (Album Art & Controls)
        // Album Art Sunken Box (140x140 biar muat 138x138)
        sprite.fillRect(15, 40, 140, 140, col_sunken);
        sprite.drawFastHLine(15, 40, 140, col_border_d);
        sprite.drawFastVLine(15, 40, 140, col_border_d);
        sprite.drawFastHLine(15, 179, 140, col_border_l);
        sprite.drawFastVLine(154, 40, 140, col_border_l);

        if (imgReady && img_buf != nullptr) {
          sprite.pushImage(16, 41, 138, 138, (uint16_t*)img_buf);
        } else {
          sprite.setTextDatum(TC_DATUM);
          sprite.setTextColor(col_grey);
          sprite.drawString("NO IMAGE", 85, 100, &fonts::Font2);
        }

        // XP Controls
        int btn_y = 185;
        int btn_w = 40;
        int btn_h = 26;
        int gap = 10;
<<<<<<< HEAD
        int btn_x_start = 15; 
=======
        int btn_x_start = 15; // Digeser ke 15 biar lurus sama Album Art di atasnya
>>>>>>> eb3a40c (how u been?)
        
        // Button 1: Prev
        int bx = btn_x_start;
        draw_xp_button(sprite, bx, btn_y, btn_w, btn_h, col_btn_gr, col_btn_dk, col_btn_brd, col_white);
        sprite.fillRect(bx+12, btn_y+8, 3, 10, col_white);
        sprite.fillTriangle(bx+25, btn_y+8, bx+25, btn_y+18, bx+15, btn_y+13, col_white);

        // Button 2: Play/Pause
        bx = btn_x_start + btn_w + gap;
        draw_xp_button(sprite, bx, btn_y, btn_w, btn_h, col_btn_gr, col_btn_dk, col_btn_brd, col_white);
        if (is_playing) {
            sprite.fillRect(bx+14, btn_y+7, 4, 12, col_white);
            sprite.fillRect(bx+22, btn_y+7, 4, 12, col_white);
        } else {
            sprite.fillTriangle(bx+14, btn_y+7, bx+14, btn_y+19, bx+26, btn_y+13, col_white);
        }
        
        // Button 3: Next
        bx = btn_x_start + (btn_w + gap)*2;
        draw_xp_button(sprite, bx, btn_y, btn_w, btn_h, col_btn_gr, col_btn_dk, col_btn_brd, col_white);
        sprite.fillTriangle(bx+12, btn_y+8, bx+12, btn_y+18, bx+21, btn_y+13, col_white);
        sprite.fillRect(bx+21, btn_y+8, 3, 10, col_white);

        // 3. Right Column (Info & Progress)
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(col_white);
        sprite.drawString(musicTitle, 165, 55, &fonts::Font4); // Title
        sprite.setTextColor(col_grey);
        sprite.drawString(musicArtist, 165, 85, &fonts::Font2); // Artist

        // Progress Bar (XP Blocky Blue)
        int px = 165, py = 120, pw = 300, ph = 18;
        sprite.drawRect(px, py, pw, ph, col_border_d);
        sprite.fillRect(px+1, py+1, pw-2, ph-2, col_sunken);
        if (dur_ms > 0) {
            int prog_w = ((pw-4) * pos_ms) / dur_ms;
            if (prog_w > 0) {
                for(int i=0; i<ph-4; i++) {
                    float t = (float)i / (float)(ph-4);
                    uint8_t r = 0;
                    uint8_t g = 180 + (80 - 180) * t;
                    uint8_t b = 255 + (153 - 255) * t;
                    sprite.drawFastHLine(px+2, py+2+i, prog_w, tft.color888(r, g, b));
                }
            }
        }
        
        // Time
        char time_str[32];
        int p_min = (pos_ms / 1000) / 60, p_sec = (pos_ms / 1000) % 60;
        int d_min = (dur_ms / 1000) / 60, d_sec = (dur_ms / 1000) % 60;
        sprintf(time_str, "%02d:%02d / %02d:%02d", p_min, p_sec, d_min, d_sec);
        sprite.setTextColor(col_grey);
        sprite.drawString(time_str, 165, 145, &fonts::Font2);

        // 4. Lyrics Box (Sunken)
        int lx = 15, ly = 225, lw = 450, lh = 80;
        sprite.fillRect(lx, ly, lw, lh, col_sunken);
        sprite.drawFastHLine(lx, ly, lw, col_border_d);
        sprite.drawFastVLine(lx, ly, lh, col_border_d);
        sprite.drawFastHLine(lx, ly+lh-1, lw, col_border_l);
        sprite.drawFastVLine(lx+lw-1, ly, lh, col_border_l);
        
        sprite.setTextDatum(TC_DATUM);
        sprite.setTextWrap(true);
        sprite.setTextColor(col_white, col_sunken);
        sprite.drawString(lyricsLines[0], 240, 240, &fonts::Font2);
        sprite.setTextColor(col_grey, col_sunken);
        sprite.drawString(lyricsLines[1], 240, 260, &fonts::Font2);
        sprite.drawString(lyricsLines[2], 240, 280, &fonts::Font2);
      } 

      // PAGE 1: SYSTEM MONITOR
      else if (currentPage == 1) {
        uint32_t col_bg     = tft.color888(30, 30, 46);
        uint32_t col_card   = tft.color888(49, 50, 68);
        uint32_t col_icon   = tft.color888(69, 71, 90);
        uint32_t col_white  = tft.color888(205, 214, 244);
        uint32_t col_grey   = tft.color888(166, 173, 200);
        uint32_t col_blue   = tft.color888(137, 180, 250);
        uint32_t col_yellow = tft.color888(249, 226, 175);
        uint32_t col_red    = tft.color888(243, 139, 168);
        uint32_t col_green  = tft.color888(166, 227, 161);
        
        sprite.fillSprite(col_bg);
        
        sprite.setTextColor(col_blue);
        sprite.setTextDatum(TL_DATUM);
        sprite.drawString("[ ARCH_LINUX_MONITOR ]", 15, 10, &fonts::Font2);
        sprite.drawFastHLine(0, 35, 480, col_card);
        
        // CARD 1: CPU
        sprite.fillRoundRect(15, 45, 450, 60, 8, col_card);
        sprite.fillCircle(45, 75, 18, col_icon);
        sprite.setTextColor(col_white);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString("CPU", 45, 68, &fonts::Font2);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(col_grey);
        sprite.drawString("Processor", 80, 50, &fonts::Font2);
        
        uint32_t cpu_temp_col = col_green;
        if (atoi(cpuTemp) > 70) cpu_temp_col = col_yellow;
        if (atoi(cpuTemp) > 85) cpu_temp_col = col_red;
        
        sprite.setTextColor(cpu_temp_col);
        char cpu_str[32]; 
        sprintf(cpu_str, "%s%%  @ %sC", cpuVal, cpuTemp);
        sprite.drawString(cpu_str, 80, 68, &fonts::Font4);
        
        sprite.fillRoundRect(80, 95, 360, 6, 3, col_icon);
        int cpu_w = (360 * atoi(cpuVal)) / 100;
        sprite.fillRoundRect(80, 95, cpu_w, 6, 3, col_blue);
        
        // CARD 2: RAM
        sprite.fillRoundRect(15, 115, 450, 60, 8, col_card);
        sprite.fillCircle(45, 145, 18, col_icon);
        sprite.setTextColor(col_white);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString("RAM", 45, 138, &fonts::Font2);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(col_grey);
        sprite.drawString("Memory", 80, 120, &fonts::Font2);
        sprite.setTextColor(col_yellow);
        char ram_str[16]; 
        sprintf(ram_str, "%s%%", ramVal);
        sprite.drawString(ram_str, 80, 138, &fonts::Font4);
        sprite.fillRoundRect(80, 165, 360, 6, 3, col_icon);
        int ram_w = (360 * atoi(ramVal)) / 100;
        sprite.fillRoundRect(80, 165, ram_w, 6, 3, col_yellow);
        
        // CARD 3: GPU
        sprite.fillRoundRect(15, 185, 450, 60, 8, col_card);
        sprite.fillCircle(45, 215, 18, col_icon);
        sprite.setTextColor(col_white);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString("GPU", 45, 208, &fonts::Font2);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(col_grey);
        sprite.drawString("Graphics Card", 80, 190, &fonts::Font2);
        
        int gpu_t_val = atoi(gpuTemp);
        uint32_t gpu_temp_col = col_green;
        if (gpu_t_val > 70) gpu_temp_col = col_yellow;
        if (gpu_t_val > 85) gpu_temp_col = col_red;
        
        sprite.setTextColor(gpu_temp_col);
        char gpu_str[32];
        if (gpu_t_val == 0) {
            sprintf(gpu_str, "iGPU");
        } else {
            sprintf(gpu_str, "%s C", gpuTemp);
        }
        sprite.drawString(gpu_str, 80, 208, &fonts::Font4);
        
        sprite.fillRoundRect(80, 235, 360, 6, 3, col_icon);
        int gpu_w = (360 * gpu_t_val) / 100;
        sprite.fillRoundRect(80, 235, gpu_w, 6, 3, gpu_temp_col);

        // CARD 4: DISK
        sprite.fillRoundRect(15, 255, 450, 55, 8, col_card);
        sprite.fillCircle(45, 282, 18, col_icon);
        sprite.setTextColor(col_white);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString("SSD", 45, 275, &fonts::Font2);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(col_grey);
        sprite.drawString("Storage (/)", 80, 260, &fonts::Font2);
        sprite.setTextColor(col_red);
        char disk_str[16]; 
        sprintf(disk_str, "%s%%", diskVal);
        sprite.drawString(disk_str, 80, 278, &fonts::Font4);
        sprite.fillRoundRect(80, 300, 360, 6, 3, col_icon);
        int disk_w = (360 * atoi(diskVal)) / 100;
        sprite.fillRoundRect(80, 300, disk_w, 6, 3, col_red);
      }

      // PAGE 2: ESP32 INTERNAL STATUS
      else if (currentPage == 2) {
        uint32_t col_bg     = tft.color888(30, 30, 46);
        uint32_t col_card   = tft.color888(49, 50, 68);
        uint32_t col_icon   = tft.color888(69, 71, 90);
        uint32_t col_white  = tft.color888(205, 214, 244);
        uint32_t col_grey   = tft.color888(166, 173, 200);
        uint32_t col_blue   = tft.color888(137, 180, 250);
        uint32_t col_yellow = tft.color888(249, 226, 175);
        uint32_t col_green  = tft.color888(166, 227, 161);
        uint32_t col_teal   = tft.color888(148, 226, 213);
        
        sprite.fillSprite(col_bg);
        
        sprite.setTextColor(col_blue);
        sprite.setTextDatum(TL_DATUM);
        sprite.drawString("[ ESP32-S3_STATUS ]", 15, 10, &fonts::Font2);
        sprite.drawFastHLine(0, 35, 480, col_card);
        
        uint32_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
        uint32_t free_psram    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        UBaseType_t stack_core0 = uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandleForCore(0));
        UBaseType_t stack_core1 = uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandleForCore(1));

        // CARD 1: Internal RAM
        sprite.fillRoundRect(15, 45, 450, 60, 8, col_card);
        sprite.fillCircle(45, 75, 18, col_icon);
        sprite.setTextColor(col_white);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString("RAM", 45, 68, &fonts::Font2);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(col_grey);
        sprite.drawString("Internal Memory", 80, 50, &fonts::Font2);
        sprite.setTextColor(col_green);
        char int_ram_str[32]; 
        sprintf(int_ram_str, "%lu KB free", free_internal);
        sprite.drawString(int_ram_str, 80, 68, &fonts::Font4);
        
        // CARD 2: PSRAM
        sprite.fillRoundRect(15, 115, 450, 60, 8, col_card);
        sprite.fillCircle(45, 145, 18, col_icon);
        sprite.setTextColor(col_white);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString("PSRAM", 45, 138, &fonts::Font2);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(col_grey);
        sprite.drawString("External Memory", 80, 120, &fonts::Font2);
        sprite.setTextColor(col_teal);
        char psram_str[32]; 
        sprintf(psram_str, "%lu KB free", free_psram);
        sprite.drawString(psram_str, 80, 138, &fonts::Font4);
        
        // CARD 3: Core 0
        sprite.fillRoundRect(15, 185, 450, 60, 8, col_card);
        sprite.fillCircle(45, 215, 18, col_icon);
        sprite.setTextColor(col_white);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString("C0", 45, 208, &fonts::Font2);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(col_grey);
        sprite.drawString("serial_task", 80, 190, &fonts::Font2);
        sprite.setTextColor(col_blue);
        char core0_str[32]; 
        sprintf(core0_str, "%lu words stack", (unsigned long)stack_core0);
        sprite.drawString(core0_str, 80, 208, &fonts::Font4);
        
        // CARD 4: Core 1
        sprite.fillRoundRect(15, 255, 450, 55, 8, col_card);
        sprite.fillCircle(45, 282, 18, col_icon);
        sprite.setTextColor(col_white);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString("C1", 45, 275, &fonts::Font2);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(col_grey);
        sprite.drawString("ui_task", 80, 260, &fonts::Font2);
        sprite.setTextColor(col_yellow);
        char core1_str[32]; 
        sprintf(core1_str, "%lu words stack", (unsigned long)stack_core1);
        sprite.drawString(core1_str, 80, 278, &fonts::Font4);
      }

      // PAGE 3: SYSTEM LOG STREAM
      else if (currentPage == 3) {
        uint32_t col_bg     = tft.color888(30, 30, 46);
        uint32_t col_card   = tft.color888(24, 24, 37);
        uint32_t col_white  = tft.color888(205, 214, 244);
        uint32_t col_grey   = tft.color888(166, 173, 200);
        uint32_t col_mauve  = tft.color888(203, 166, 247);
        uint32_t col_yellow = tft.color888(249, 226, 175);
        uint32_t col_red    = tft.color888(243, 139, 168);
        uint32_t col_blue   = tft.color888(137, 180, 250);
        uint32_t col_green  = tft.color888(166, 227, 161);
        uint32_t col_teal   = tft.color888(148, 226, 213);
        
        sprite.fillSprite(col_bg);
        
        sprite.setTextColor(col_mauve);
        sprite.setTextDatum(TL_DATUM);
        sprite.drawString("[ SYSTEM_LOG_STREAM ]", 15, 10, &fonts::Font2);
        sprite.drawFastHLine(0, 35, 480, col_card);
        
        sprite.setTextColor(col_green);
        sprite.drawString("IP:", 15, 40, &fonts::Font2);
        sprite.setTextColor(col_white);
        sprite.drawString(localIP, 40, 40, &fonts::Font2);
        sprite.setTextColor(col_grey);
        sprite.drawString("PUB:", 200, 40, &fonts::Font2);
        sprite.setTextColor(col_white);
        sprite.drawString(pubIP, 235, 40, &fonts::Font2);
        
        sprite.fillRoundRect(10, 65, 460, 250, 8, col_card);
        
        sprite.setTextWrap(false);
        sprite.setTextDatum(TL_DATUM);
        
        int num_lines = 10;
        int y_start = 72;
        int line_height = 24; 
        
        for(int i = 0; i < num_lines; i++) {
          int logIdx = (logTail - num_lines + i + 10) % 10;
          if (logIdx >= 0 && logIdx < 10) {
            char* logText = logTexts[logIdx];
            uint32_t text_col = col_white; 
            
            if (strcmp(logTypes[logIdx], "crit") == 0) {
              text_col = tft.color888(255, 0, 0);
            } else if (strstr(logText, "[LAUNCH]") != nullptr) {
              text_col = col_green;
            } else if (strstr(logText, "[DESTROY]") != nullptr) {
              text_col = col_red;
            } else if (strstr(logText, "[WS]") != nullptr) {
              text_col = col_yellow;
            } else if (strstr(logText, "[FOCUS]") != nullptr) {
              text_col = col_teal;
            } else if (strcmp(logTypes[logIdx], "err") == 0) {
              text_col = col_red;
            } else if (strcmp(logTypes[logIdx], "warn") == 0) {
              text_col = col_yellow;
            } else if (strcmp(logTypes[logIdx], "sys") == 0) {
              text_col = col_blue;
            } else if (strcmp(logTypes[logIdx], "kernel") == 0 || strcmp(logTypes[logIdx], "pacman") == 0) {
              text_col = col_green;
            }

            char subText[128];
            strcpy(subText, logText);
            sprite.setFont(&fonts::Font2);
            
            if (sprite.textWidth(subText) > 440) {
                int len = strlen(subText);
                while (len > 0 && sprite.textWidth(subText) > 440 - 24) {
                    len--;
                    subText[len] = '\0';
                }
                strcat(subText, "...");
            }
            sprite.setTextColor(text_col);
            sprite.drawString(subText, 20, y_start + (i * line_height), &fonts::Font2);
          }
        }
      }
      sprite.pushSprite(0, 0);
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// Main Entry Point
extern "C" void app_main(void) {
    tft.init();
    tft.setRotation(1); 
    tft.fillScreen(TFT_BLACK);
    
    xTaskCreatePinnedToCore(serial_task, "Serial", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(ui_task, "UI", 8192, NULL, 1, NULL, 1);
}

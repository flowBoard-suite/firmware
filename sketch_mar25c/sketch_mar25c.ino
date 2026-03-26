#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <lvgl.h>

// === KONFIGURACJA EKRANU WAVESHARE 7" ===
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    5, 3, 46, 7, 1, 2, 42, 41, 40, 39, 0, 45, 48, 47, 21, 14, 38, 18, 17, 10,
    1, 40, 48, 40, 1, 13, 3, 31, 0, 16000000);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, bus);

// === BUFOR GRAFICZNY LVGL ===
#define BUFFER_SIZE (800 * 480 / 10)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf; 

// === KOLORY ===
#define COLOR_DARK      lv_color_hex(0x1A1A1A)
#define COLOR_ACCENT    lv_color_hex(0x07E0D0)
#define COLOR_SUCCESS   lv_color_hex(0x28A745)
#define COLOR_WHITE     lv_color_hex(0xFFFFFF)
#define COLOR_CARD      lv_color_hex(0x2A2A2A)

// === OBIEKTY SIECIOWE ===
WiFiManager wm;
bool isConnected = false;

// === FUNKCJA ODŚWIEŻANIA EKRANU ===
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  lv_disp_flush_ready(disp);
}

// === PŁYNNE OPÓŹNIENIE (Klucz do braku freezów!) ===
void lv_delay(int ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    lv_timer_handler();
    delay(5);
  }
}

// === EKRAN 1: ONBOARDING ===
void buildOnboardingUI() {
  lv_obj_clean(lv_scr_act()); 
  lv_obj_set_style_bg_color(lv_scr_act(), COLOR_DARK, LV_PART_MAIN);

  lv_obj_t *container = lv_obj_create(lv_scr_act());
  lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(container, LV_OPA_0, LV_PART_MAIN);
  lv_obj_set_style_border_opa(container, LV_OPA_0, LV_PART_MAIN);
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *logo_label = lv_label_create(container);
  lv_label_set_text(logo_label, "flowBoard");
  lv_obj_set_style_text_font(logo_label, &lv_font_montserrat_40, LV_PART_MAIN);
  lv_obj_set_style_text_color(logo_label, COLOR_ACCENT, LV_PART_MAIN);

  lv_obj_t *inst = lv_label_create(container);
  lv_label_set_text(inst, "Polacz sie z siecia WiFi:\n\nNazwa: flowBoard_Setup\nHaslo: 12345678");
  lv_obj_set_style_text_align(inst, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_font(inst, &lv_font_montserrat_22, LV_PART_MAIN);
  lv_obj_set_style_text_color(inst, COLOR_WHITE, LV_PART_MAIN);
  lv_obj_set_style_pad_top(inst, 30, LV_PART_MAIN);
}

// === EKRAN 1.5: SUKCES ===
void showSuccessScreen() {
  lv_obj_clean(lv_scr_act());
  lv_obj_set_style_bg_color(lv_scr_act(), COLOR_DARK, LV_PART_MAIN);

  lv_obj_t *success_label = lv_label_create(lv_scr_act());
  lv_label_set_text(success_label, "Polaczono z siecia pomyslnie!\n\nUruchamianie...");
  lv_obj_set_style_text_font(success_label, &lv_font_montserrat_32, LV_PART_MAIN);
  lv_obj_set_style_text_color(success_label, COLOR_SUCCESS, LV_PART_MAIN);
  lv_obj_set_style_text_align(success_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(success_label);

  // Szybki render napisu
  lv_delay(100); 
}

// === POMOC DO KAFELKÓW ===
void drawTask(lv_obj_t * parent, const char * text) {
  lv_obj_t * card = lv_obj_create(parent);
  lv_obj_set_size(card, LV_PCT(90), 80);
  lv_obj_set_style_bg_color(card, COLOR_CARD, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t * label = lv_label_create(card);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_22, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, COLOR_WHITE, LV_PART_MAIN);
}

// === EKRAN 2: GŁÓWNA LISTA ZADAŃ ===
void buildMainUI() {
  lv_obj_clean(lv_scr_act()); 
  lv_obj_set_style_bg_color(lv_scr_act(), COLOR_DARK, LV_PART_MAIN);

  lv_obj_t * main_list = lv_obj_create(lv_scr_act());
  lv_obj_set_size(main_list, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(main_list, LV_OPA_0, LV_PART_MAIN);
  lv_obj_set_style_border_opa(main_list, LV_OPA_0, LV_PART_MAIN);
  lv_obj_set_flex_flow(main_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(main_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_top(main_list, 30, LV_PART_MAIN);

  lv_obj_t * header = lv_label_create(main_list);
  lv_label_set_text(header, "Twoje zadania na dzis:");
  lv_obj_set_style_text_font(header, &lv_font_montserrat_32, LV_PART_MAIN);
  lv_obj_set_style_text_color(header, COLOR_ACCENT, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(header, 20, LV_PART_MAIN);

  drawTask(main_list, "- Zamowic nowa dostawe kawy do biura");
  drawTask(main_list, "- Zrobic przeglad techniczny samochodu");
}

void setup() {
  Serial.begin(115200);

  gfx->begin();
  gfx->fillScreen(COLOR_DARK.full);
  lv_init();

  buf = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, BUFFER_SIZE);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 800;
  disp_drv.ver_res = 480;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // === KLUCZOWY PUNKT: CZYŚCIMY PAMIĘĆ ===
  // Dzięki tej linijce płytka po resecie zawsze odpali Hotspot
  wm.resetSettings(); 

  wm.setConfigPortalBlocking(false); 
  
  // Rysujemy instrukcję 
  buildOnboardingUI();

  // Próbujemy połączyć (zawsze się nie uda przez resetSettings powyżej)
  if(wm.autoConnect("flowBoard_Setup", "12345678")) {
    isConnected = true;
    buildMainUI(); 
  }
}

void loop() {
  lv_timer_handler(); 
  delay(5);

  if (!isConnected) {
    wm.process(); 

    if (WiFi.status() == WL_CONNECTED) {
      isConnected = true;
      
      // 1. Pokaż zielony komunikat o sukcesie
      showSuccessScreen();
      
      // 2. Odczekaj 2 sekundy "pompując" klatki na ekran (BRAK ZAWIESZEŃ)
      lv_delay(2000); 
      
      // 3. Załaduj ekran z zadaniami
      buildMainUI(); 
    }
  }
}
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <lvgl.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <TAMC_GT911.h>

#define TOUCH_SDA  8
#define TOUCH_SCL  9
#define TOUCH_INT -1
#define TOUCH_RST -1

TAMC_GT911 tp = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, 800, 480);

// 1. Inicjalizacja sprzętowa ekranu (bez zmian)
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    5, 3, 46, 7, 1, 2, 42, 41, 40, 39, 0, 45, 48, 47, 21, 14, 38, 18, 17, 10,
    1, 40, 48, 40, 1, 13, 3, 31, 0, 16000000);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, bus);

// 2. Pamięć na bufor dla LVGL
#define BUFFER_SIZE (800 * 480 / 10)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf; 

WiFiManager wm;
bool isConnected = false; // Zmienna pilnująca, czy jesteśmy już połączeni
bool shouldRetryFetch = false;

// 3. Funkcja "pomostowa" - tłumaczy piksele z LVGL na wyświetlacz GFX
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  lv_disp_flush_ready(disp);
}

// --- FUNKCJA PRZEKAZUJĄCA DOTYK DO LVGL ---
void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data) {
  tp.read(); // Pobierz dane z chipa GT911
  
  if (tp.isTouched) {
    data->state = LV_INDEV_STATE_PR; // PR = Pressed (Wciśnięty)
    
    // Zapisujemy koordynaty pierwszego wykrytego palca
    data->point.x = tp.points[0].x;
    data->point.y = tp.points[0].y;
  } else {
    data->state = LV_INDEV_STATE_REL; // REL = Released (Puszczony)
  }
}

// ==========================================
// FUNKCJE TWORZĄCE EKRANY
// ==========================================

void showConnectingScreen() {
  lv_obj_clean(lv_scr_act()); // Czyści obecny ekran
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x0000FF), LV_PART_MAIN); // Niebieskie tło

  // Tworzymy etykietę (tekst)
  lv_obj_t * label = lv_label_create(lv_scr_act());
  lv_label_set_text(label, "Szukaj sieci na telefonie:\nflowBoard_Setup\nHaslo: 12345678");
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN); // Biały tekst
  
  // Zaletą LVGL jest to, że nie musimy zgadywać współrzędnych!
  lv_obj_center(label); // Wyśrodkuj na ekranie
}

void showConnectedScreen() {
  lv_obj_clean(lv_scr_act()); // Czyści obecny ekran
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x00FF00), LV_PART_MAIN); // Zielone tło

  lv_obj_t * label = lv_label_create(lv_scr_act());
  lv_label_set_text(label, "POLACZONO Z WIFI!");
  lv_obj_set_style_text_color(label, lv_color_hex(0x000000), LV_PART_MAIN); // Czarny tekst
  lv_obj_center(label);
}

// Ta funkcja wywoła się po kliknięciu przycisku na ekranie
static void retry_btn_event_cb(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  
  if (code == LV_EVENT_CLICKED) {
    Serial.println("Kliknięto przycisk: Ponawiam próbę...");
    
    // Zmieniamy ekran na chwilowy komunikat ładowania
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
    
    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Pobieranie danych...");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(label);
    
    // Zalamy flagę, żeby pętla loop() wykonała request
    shouldRetryFetch = true; 
  }
}

void showErrorScreen(String errorMsg) {
  lv_obj_clean(lv_scr_act()); 
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFF0000), LV_PART_MAIN);

  String deviceSN = WiFi.macAddress();

  lv_obj_t * label = lv_label_create(lv_scr_act());
  String fullText = errorMsg + "\n\nS/N Twojej plytki:\n" + deviceSN;
  lv_label_set_text(label, fullText.c_str());
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN); 
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  
  // Przesuwamy tekst lekko do góry, żeby zrobić miejsce na przycisk
  lv_obj_align(label, LV_ALIGN_CENTER, 0, -40); 

  // --- TWORZYMY PRZYCISK ---
  lv_obj_t * btn = lv_btn_create(lv_scr_act());
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60); // Pozycjonujemy pod tekstem
  lv_obj_set_size(btn, 200, 50);
  
  // Przypisujemy nasz "event" do kliknięcia
  lv_obj_add_event_cb(btn, retry_btn_event_cb, LV_EVENT_ALL, NULL);

  // Dodajemy napis na przycisku
  lv_obj_t * btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, "Sprobuj ponownie");
  lv_obj_center(btn_label);
}

void fetchBackendData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // 1. Pobieramy unikalny MAC adres (SN płytki)
    String deviceSN = WiFi.macAddress();
    
    // 2. Tworzymy zaktualizowany endpoint, doklejając parametr, na który czeka Spring
    String serverPath = "http://192.168.0.131:8080/api/boards?serialNumber=" + deviceSN; 
    
    Serial.print("\n[HTTP] Łączenie z serwerem: ");
    Serial.println(serverPath);
                                                               
    http.begin(serverPath);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      if (httpResponseCode >= 200 && httpResponseCode < 300) {
        String payload = http.getString();
        Serial.println("[HTTP] Pobrano surowy JSON:");
        Serial.println(payload);

        // --- ROZPOCZYNAMY PARSOWANIE JSONA ---
        JsonDocument doc; 
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
          Serial.print("[JSON] Błąd parsowania: ");
          Serial.println(error.c_str());
          showErrorScreen("Blad formatu danych!");
        } else {
          
          // 3. Sprawdzamy, czy backend zwrócił pustą tablicę "[]"
          if (doc.size() == 0) {
             Serial.println("[JSON] Brak tablic dla tego S/N!");
             showErrorScreen("BRAK PRZYPISANEJ TABLICY\nW BAZIE DANYCH!");
          } else {
            // Pobieramy dane z pierwszego obiektu w tablicy (indeks 0)
            String boardName = doc[0]["name"] | "Brak nazwy"; 
            String boardSerial = doc[0]["serialNumber"] | "Brak S/N";
            String boardDesc = doc[0]["description"] | "Brak opisu";

            Serial.println("[JSON] Dane pochytane pomyślnie!");
            
            // Wysyłamy rozpakowane dane do ekranu
            showBoardInfo(boardName, boardSerial, boardDesc);
          }
        }
        // -------------------------------------

      } else {
        showErrorScreen("Kod HTTP: " + String(httpResponseCode)); 
      }
    } 
    else {
      showErrorScreen("Brak odpowiedzi od serwera!\nKod: " + String(httpResponseCode)); 
    }
    
    http.end(); 
  }
}

void showBoardInfo(String name, String serial, String desc) {
  lv_obj_clean(lv_scr_act()); 
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1A1A1A), LV_PART_MAIN); // Bardzo ciemne tło

  // Tworzymy kontener (kartę) na środku ekranu
  lv_obj_t * card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, 600, 300);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x2A2A2A), LV_PART_MAIN); // Jaśniejsza karta
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 15, LV_PART_MAIN); // Zaokrąglone rogi
  
  // Układ elementów w kolumnie (jeden pod drugim)
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // 1. Nazwa tablicy
  lv_obj_t * title = lv_label_create(card);
  lv_label_set_text(title, name.c_str());
  lv_obj_set_style_text_color(title, lv_color_hex(0x07E0D0), LV_PART_MAIN); // Akcent
  lv_obj_set_style_pad_top(title, 20, LV_PART_MAIN);

  // 2. Numer seryjny (mniejszy tekst)
  lv_obj_t * serialLabel = lv_label_create(card);
  String serialText = "Serial: " + serial;
  lv_label_set_text(serialLabel, serialText.c_str());
  lv_obj_set_style_text_color(serialLabel, lv_color_hex(0x888888), LV_PART_MAIN); // Szary

  // 3. Opis tablicy
  lv_obj_t * descLabel = lv_label_create(card);
  lv_label_set_text(descLabel, desc.c_str());
  lv_label_set_long_mode(descLabel, LV_LABEL_LONG_WRAP); // Zawijanie tekstu
  lv_obj_set_width(descLabel, 500);
  lv_obj_set_style_text_color(descLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN); // Biały
  lv_obj_set_style_text_align(descLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_pad_top(descLabel, 30, LV_PART_MAIN);
}

// ==========================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // --- Start GFX i LVGL ---
  gfx->begin();
  lv_init();

  // Alokacja pamięci dla rysowania LVGL
// Przesuwamy bufor do zewnętrznej pamięci PSRAM, żeby WiFi miało miejsce do pracy
buf = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, BUFFER_SIZE);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 800;
  disp_drv.ver_res = 480;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Rysujemy pierwszy (niebieski) ekran
  showConnectingScreen();

  // --- Start WiFiManager ---
  WiFi.setSleep(false);
  wm.resetSettings(); // Do usunięcia później

  // UWAGA: Wyłączamy blokowanie! Inaczej pętla zablokuje się tutaj, a ekran nie zostanie narysowany.
  wm.setConfigPortalBlocking(false); 
  wm.autoConnect("flowBoard_Setup", "12345678");

  // --- INICJALIZACJA DOTYKU (Po dodaniu ekranu) ---
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  tp.begin();
  tp.setRotation(ROTATION_NORMAL);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read; // Wskazujemy naszą funkcję z Kroku 3
  lv_indev_drv_register(&indev_drv);
  // ----------------------------------------------
}

void loop() {
  lv_tick_inc(5); 
  lv_timer_handler(); 
  delay(5);

  if (!isConnected) {
    wm.process(); 
    if (WiFi.status() == WL_CONNECTED) {
      isConnected = true;           
      showConnectedScreen();        
      Serial.println("Połączono z domowym WiFi!");
      fetchBackendData();
    }
  }

  // --- OBSŁUGA PONOWIENIA (RETRY) ---
  if (shouldRetryFetch) {
    shouldRetryFetch = false; // Od razu gasimy flagę
    fetchBackendData();       // Uderzamy znowu do bazy!
  }
}
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"   

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <esp_task_wdt.h>


const char *WIFI_SSID = "-----";
const char *WIFI_PASS = "------";


constexpr uint8_t LED_PIN              = D0;   // SK6812 veri pini
constexpr uint8_t SENSOR_DIGITAL_PIN   = D1;   // HC‑SR501 dijital çıkış
constexpr uint8_t SENSOR_ANALOG_PIN    = D2;    // HC‑SR501 analog çıkış


constexpr size_t   LED_SAYISI          = 300;             
constexpr uint8_t  GECE_PARLAKLIK      = 25;                
constexpr uint8_t  AKSAM_PARLAKLIK     = 100;           
constexpr uint8_t  GUNDUZ_PARLAKLIK    = 215;            
constexpr uint8_t  DEFAULT_BRIGHT      = 30;              


constexpr int      GUNDUZ_BASLANGIC_SAATI = 6;
constexpr int      GUNDUZ_BITIS_SAATI     = 18; 
constexpr int      AKSAM_BITIS_SAATI      = 23; 


constexpr long     NTP_OFFSET_SEC     = 3L * 3600L;     
constexpr const char *NTP_SERVER      = "pool.ntp.org";


constexpr uint16_t ANALOG_OKUMA_ARALIK = 10;      
constexpr uint16_t BELLEK_LOG_ARALIK   = 60L * 1000L;     


constexpr uint8_t  LOG_SAYISI          = 20;
constexpr uint16_t IDLE_TIMEOUT        = 30 * 1000;    /


Preferences prefs;              
CRGB leds[LED_SAYISI];           
AsyncWebServer server(80);       
static unsigned long movementStartTime = 0;


class LEDKontrol;
class HareketSensoru;
class ZamanParlaklik;
class Animasyon;
class WebAPI;
class LogSistemi;


LEDKontrol    *g_ledKontrol = nullptr;
HareketSensoru* g_sensor     = nullptr;
ZamanParlaklik* g_zaman      = nullptr;
Animasyon      *g_anlikAnim  = nullptr;
WebAPI         *g_api        = nullptr;
LogSistemi     *g_log        = nullptr;
bool            g_otaInProgress = false;
uint32_t        g_sonHareketZamani = 0;
bool            g_ledAktif = false;


static const char ota_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html lang="tr"><head><meta charset="UTF-8"><title>OTA Güncelleme</title>
<link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
<style>body{background:linear-gradient(135deg,#2c3e50,#4ca1af);display:flex;align-items:center;justify-content:center;height:100vh;font-family:'Segoe UI',Tahoma,Helvetica,sans-serif}
.glass-card{background:rgba(0,0,0,.45);backdrop-filter:blur(12px);border-radius:15px;padding:2rem;box-shadow:0 8px 32px rgba(0,0,0,.15)}</style></head><body>
<div class="glass-card"><h3 class="text-center mb-4">OTA Güncelleme</h3>
<form id="uploadForm" method="POST" action="/doUpdate" enctype="multipart/form-data">
<div class="mb-3"><label class="form-label">Firmware Dosyası</label>
<input class="form-control" type="file" name="update" accept=".bin"></div>
<button type="submit" class="btn btn-primary w-100">Yükle ve Güncelle</button>
<div id="log" class="mt-3"></div></form></div>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
<script>
const form=document.getElementById('uploadForm');
const log=document.getElementById('log');
form.addEventListener('submit',e=>{
  e.preventDefault();
  const data=new FormData(form);
  fetch('/doUpdate', {method:'POST',body:data})
    .then(r=>r.text())
    .then(t=>{log.innerHTML='<pre>'+t+'</pre>';})
    .catch(err=>{log.textContent='Error: '+err;});
});
</script></body></html>
)rawliteral";

static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="tr"><head><meta charset="UTF-8"><title>Koridor Işık Kontrolü</title>
<link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
<link href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.1.1/css/all.min.css" rel="stylesheet">
<style>
:root{--primary:#ffdd57;--bg-gradient:linear-gradient(135deg,#2c3e50,#4ca1af);--card-bg:rgba(0,0,0,.45);}
body{background:var(--bg-gradient);color:#f5f5f5;font-family:'Segoe UI',Tahoma,Helvetica,sans-serif;min-height:100vh}
.glass-card{background:var(--card-bg);backdrop-filter:blur(12px);border-radius:12px;padding:1.5rem;margin-bottom:1rem}
.status-badge{font-size:.85rem;padding:.5rem 1rem}
.sensor-value{font-size:1.2rem;font-weight:bold;color:var(--primary)}
.progress{height:8px}
.progress-bar{border-radius:4px}
.nav-tabs .nav-link{color:#bbb;font-weight:500}
.nav-tabs .nav-link.active{color:#fff;border-bottom:3px solid var(--primary)}
.form-range::-webkit-slider-thumb{background:var(--primary)}
.form-range::-moz-range-thumb{background:var(--primary)}
.log-container{max-height:200px;overflow-y:auto;font-size:0.85rem;background:rgba(0,0,0,.2);padding:8px;border-radius:4px}
</style></head><body>
<div class="container py-4"><div class="row justify-content-center"><div class="col-lg-10">
<h1 class="text-center mb-4"><i class="fas fa-lightbulb me-2"></i>Koridor Işık Kontrolü</h1>

<!-- ÜST DURUM PANELİ -->
<div class="glass-card">
  <div class="row align-items-center">
    <div class="col-md-4 text-center mb-3 mb-md-0">
      <i class="fas fa-radar fa-2x text-primary me-2"></i>
      <div class="sensor-value" id="sensorValue">0</div>
      <div class="progress"><div id="sensorBar" class="progress-bar bg-success" style="width:0%"></div></div>
      <small>Eşik: <span id="thresholdValue">15</span></small>
    </div>
    <div class="col-md-4 text-center mb-3 mb-md-0">
      <i class="fas fa-lightbulb fa-2x text-warning me-2"></i>
      <div class="sensor-value" id="ledStatus">Kapalı</div>
      <span class="badge bg-success status-badge" id="lastMotion">Hareket Yok</span>
    </div>
    <div class="col-md-4 text-center">
      <i class="fas fa-wifi fa-2x text-info me-2"></i>
      <div class="sensor-value" id="wifiStatus">%WIFISTATUS%</div>
      <span class="badge bg-info status-badge" id="ipAddress">%IPADDRESS%</span>
    </div>
  </div>
</div>

<!-- KONTROL TABS -->
<div class="glass-card">
  <ul class="nav nav-tabs nav-justified mb-3" id="controlTabs">
    <li class="nav-item"><button class="nav-link active" data-bs-toggle="tab" data-bs-target="#anim">Animasyon</button></li>
    <li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#sensor">Sensör</button></li>
    <li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#system">Sistem</button></li>
    <li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#logs">Loglar</button></li>
  </ul>
  <div class="tab-content">
    <!-- ### ANIM ### -->
    <div class="tab-pane fade show active" id="anim">
      <div class="row mb-3">
        <div class="col-md-6"><label class="form-label">Animasyon</label>
          <select class="form-select" id="animationSelect">
            <option value="0">🔵 Düz Renk</option>
            <option value="1">💨 Nefes</option>
            <option value="2">🌈 Gökkuşağı</option>
            <option value="3">🔦 Tarayıcı</option>
            <option value="4">☄️ Kuyruklu Yıldız</option>
            <option value="5">🌠 Meteor</option>
            <option value="6">🌊 Gradyan Dalga</option>
            <option value="7">🔥 Ateş</option>
            <option value="8">🎯 Bölge</option>
            <option value="9">🌌 Aurora</option>
            <option value="10">🟢 Matrix</option>
            <option value="11">✨ Twinkle</option>
            <option value="12">🌊 Ripple</option>
          </select>
        </div>
        <div class="col-md-6"><label class="form-label">Renk</label>
          <input type="color" class="form-control form-control-color" id="colorPicker" value="#ffffff">
        </div>
      </div>
      <div class="row mb-3">
        <div class="col-md-6"><label>Parlaklık: <span id="brightnessValue">30</span>%</label>
          <input type="range" class="form-range" min="0" max="100" id="brightnessSlider" value="30">
        </div>
        <div class="col-md-6"><label>Hız: <span id="speedValue">50</span>%</label>
          <input type="range" class="form-range" min="0" max="100" id="speedSlider" value="50">
        </div>
      </div>
    </div>

    <!-- ### SENSOR ### -->
    <div class="tab-pane fade" id="sensor">
      <div class="row mb-3">
        <div class="col-md-6"><label>Hassasiyet: <span id="sensitivityValue">80</span>%</label>
          <input type="range" class="form-range" min="0" max="100" id="sensitivitySlider" value="80">
          <div class="form-text">Yüksek değer = daha hassas algılama</div>
        </div>
        <div class="col-md-6"><label>Bekleme Süresi: <span id="holdTimeValue">20</span>s</label>
          <input type="range" class="form-range" min="5" max="60" id="holdTimeSlider" value="20">
          <div class="form-text">Hareket durduktan sonra LED’lerin açık kalma süresi</div>
        </div>
      </div>
      <div class="text-center mt-3"><button class="btn btn-outline-primary" id="calibrateBtn">Sensörü Kalibre Et</button>
        <div id="calStatus" class="mt-2"></div>
      </div>
    </div>

    <!-- ### SYSTEM ### -->
    <div class="tab-pane fade" id="system">
      <div class="alert alert-info"><i class="fas fa-info-circle me-2"></i>Sistem Bilgileri</div>
      <div class="mb-4">
        <h6>İç Donanım</h6>
        <div class="row">
          <div class="col-6"><small>Çip ID</small><div>%CHIPID%</div></div>
          <div class="col-6"><small>Free Heap</small><div>%FREEHEAP% KB</div></div>
        </div>
        <div class="row mt-2">
          <div class="col-6"><small>Çalışma Süresi</small><div id="uptime">0s</div></div>
          <div class="col-6"><small>Son Hareket</small><div id="lastMotionTime">-</div></div>
        </div>
      </div>
      <div class="mb-4"><h6>Firmware Güncelleme</h6>
        <a href="/ota" class="btn btn-primary w-100">OTA Güncelleme</a>
      </div>
    </div>
    
    <!-- ### LOGS ### -->
    <div class="tab-pane fade" id="logs">
      <div class="log-container" id="logList"></div>
    </div>
  </div>
</div>
</div></div></div>

<button id="darkToggle" class="btn btn-sm btn-outline-light position-fixed bottom-0 end-0 m-3">🌙</button>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
<script>
/* --------------------------------------------------------------
   UI ELEMENT REFERENCE
   -------------------------------------------------------------- */
const UI = {
  sensorValue:    document.getElementById('sensorValue'),
  sensorBar:      document.getElementById('sensorBar'),
  thresholdValue: document.getElementById('thresholdValue'),
  ledStatus:      document.getElementById('ledStatus'),
  lastMotion:     document.getElementById('lastMotion'),
  wifiStatus:     document.getElementById('wifiStatus'),
  ipAddress:      document.getElementById('ipAddress'),

  animationSelect:  document.getElementById('animationSelect'),
  brightnessSlider: document.getElementById('brightnessSlider'),
  brightnessValue:  document.getElementById('brightnessValue'),
  speedSlider:      document.getElementById('speedSlider'),
  speedValue:       document.getElementById('speedValue'),
  colorPicker:      document.getElementById('colorPicker'),

  sensitivitySlider: document.getElementById('sensitivitySlider'),
  sensitivityValue:  document.getElementById('sensitivityValue'),
  holdTimeSlider:    document.getElementById('holdTimeSlider'),
  holdTimeValue:     document.getElementById('holdTimeValue'),

  calibrateBtn:      document.getElementById('calibrateBtn'),
  calStatus:         document.getElementById('calStatus'),
  logList:           document.getElementById('logList'),
  uptime:            document.getElementById('uptime'),
  lastMotionTime:    document.getElementById('lastMotionTime')
};


function apiGet(path, onSuccess){
  fetch(path).then(r=>r.ok ? r.text() : Promise.reject(r.statusText))
            .then(onSuccess).catch(e=>console.error('API error →',e));
}
function apiSet(key,val){
  apiGet(`/api/${key}?value=${val}`);
}

UI.animationSelect.addEventListener('change',()=>apiSet('anim',UI.animationSelect.value));
UI.brightnessSlider.addEventListener('input',()=>{
  UI.brightnessValue.textContent = UI.brightnessSlider.value;
  apiSet('parlaklik',UI.brightnessSlider.value);
});
UI.speedSlider.addEventListener('input',()=>{
  UI.speedValue.textContent = UI.speedSlider.value;
  apiSet('hiz',UI.speedSlider.value);
});
UI.colorPicker.addEventListener('input',()=>{
  apiSet('renk',UI.colorPicker.value.substring(1));
});
UI.sensitivitySlider.addEventListener('input',()=>{
  UI.sensitivityValue.textContent = UI.sensitivitySlider.value;
  apiSet('hassasiyet',UI.sensitivitySlider.value);
});
UI.holdTimeSlider.addEventListener('input',()=>{
  UI.holdTimeValue.textContent = UI.holdTimeSlider.value;
  apiSet('bekleme',UI.holdTimeSlider.value);
});
UI.calibrateBtn.addEventListener('click',()=>{
  UI.calStatus.innerHTML='<div class="spinner-border spinner-border-sm me-2"></div>Kalibrasyon…';
  apiGet('/api/kalibrasyon',txt=>{ UI.calStatus.innerHTML = txt; });
});


const darkToggle = document.getElementById('darkToggle');
let darkMode = true;
darkToggle.addEventListener('click',()=>{
  darkMode = !darkMode;
  document.body.style.background = darkMode ?
      'linear-gradient(135deg,#2c3e50,#4ca1af)' :
      'linear-gradient(135deg,#667eea,#764ba2)';
  darkToggle.textContent = darkMode ? '🌙' : '☀️';
});


let ws;
function startWs(){
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onmessage = e=> {
    const d = JSON.parse(e.data);
    UI.sensorValue.textContent  = d.sensValue;
    UI.sensorBar.style.width   = Math.min(100, d.sensValue/4) + '%';
    UI.thresholdValue.textContent = d.threshold;
    UI.ledStatus.textContent   = d.ledON ? 'Açık' : 'Kapalı';
    let lastMotionSec = d.lastMotion ? Math.floor((Date.now()-d.lastMotion)/1000) : 0;
    if (lastMotionSec < 5) UI.lastMotion.textContent = 'Hareketli';
    else if (lastMotionSec < 60) UI.lastMotion.textContent = lastMotionSec+'s';
    else UI.lastMotion.textContent = Math.floor(lastMotionSec/60)+'dk';
    // progress‑bar renk değişimi
    const pct = Math.min(100, d.sensValue/4);
    UI.sensorBar.className = 'progress-bar '+
        (pct<30?'bg-success':(pct<70?'bg-warning':'bg-danger'));
  };
  ws.onclose = ()=>{ setTimeout(startWs,2000); };   // otomatik yeniden bağlan
}
startWs();

function formatUptime(seconds){
  let h = Math.floor(seconds/3600);
  let m = Math.floor((seconds%3600)/60);
  let s = seconds%60;
  return h+'s '+m+'d '+s+'sn';
}

function updateStatus(){
  fetch('/api/durum').then(r=>r.json()).then(d=>{
    UI.animationSelect.value = d.animasyon;
    UI.brightnessSlider.value = d.parlaklik;
    UI.brightnessValue.textContent = d.parlaklik;
    UI.speedSlider.value = d.hiz;
    UI.speedValue.textContent = d.hiz;
    UI.sensitivitySlider.value = d.hassasiyet;
    UI.sensitivityValue.textContent = d.hassasiyet;
    UI.holdTimeSlider.value = d.bekleme;
    UI.holdTimeValue.textContent = d.bekleme;
    UI.uptime.textContent = formatUptime(d.uptime);
    UI.lastMotionTime.textContent = d.lastMotion ? new Date(d.lastMotion).toLocaleTimeString() : '-';
  });
}

function updateLogs(){
  fetch('/api/loglar').then(r=>r.json()).then(logs=>{
    let html = '';
    logs.forEach(log=>{
      let d = new Date(log.zaman*1000);
      html += '<div><small>'+d.toLocaleTimeString()+'</small> ['+log.tip+'] '+log.mesaj+'</div>';
    });
    UI.logList.innerHTML = html || '<div class="text-muted">Log kaydı yok</div>';
  });
}

setInterval(updateStatus, 2000);
setInterval(updateLogs, 5000);
updateStatus();
updateLogs();
</script></body></html>
)rawliteral";

/* -----------------------------------------------------------------
    GAMA DÜZELTME TABLOSU 
   ----------------------------------------------------------------- */
static const uint8_t PROGMEM gamma8tab[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,
    3,  3,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  7,  7,  7,
    8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 14, 14, 15, 16, 16,
   17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 29, 30, 31, 33, 34,
   36, 37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 58, 60, 63, 65, 68,
   71, 74, 77, 80, 83, 86, 90, 93, 97, 101, 105, 109, 113, 117, 122, 126,
  131, 136, 141, 146, 151, 157, 162, 168, 174, 180, 187, 193, 200, 207, 214, 221,
  229, 237, 245, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};
static inline uint8_t gamma8(uint8_t v) { 
    return pgm_read_byte(&gamma8tab[v]); 
}


class LogSistemi {
public:
    struct LogKaydi {
        time_t zaman;
        char tip[16];
        char mesaj[64];
    };
    
    LogSistemi() : _index(0), _sayi(0) {
        memset(_kayitlar, 0, sizeof(_kayitlar));
    }
    
    void ekle(const String& tip, const String& mesaj) {
        time_t now = time(nullptr);
        _kayitlar[_index].zaman = now;
        strncpy(_kayitlar[_index].tip, tip.c_str(), sizeof(_kayitlar[_index].tip) - 1);
        strncpy(_kayitlar[_index].mesaj, mesaj.c_str(), sizeof(_kayitlar[_index].mesaj) - 1);
        _kayitlar[_index].tip[sizeof(_kayitlar[_index].tip) - 1] = '\0';
        _kayitlar[_index].mesaj[sizeof(_kayitlar[_index].mesaj) - 1] = '\0';
        
        _index = (_index + 1) % LOG_SAYISI;
        if (_sayi < LOG_SAYISI) _sayi++;
        
        Serial.printf("[%s] %s\n", tip.c_str(), mesaj.c_str());
    }
    
    const LogKaydi* kayitlar() const { return _kayitlar; }
    uint8_t index() const { return _index; }
    uint8_t sayi() const { return _sayi; }
    
private:
    LogKaydi _kayitlar[LOG_SAYISI];
    uint8_t _index;
    uint8_t _sayi;
};


class UygulamaDurumu {
public:
    enum class Led    { KAPALI, ACIK };
    enum class Wifi   { BAGLI_DEGIL, BAGLANIYOR, BAGLI, HATA };
    enum class Sensor { BEKLEMEDE, HAREKET, KALIBRASYON, HATA };
    enum class Ota    { BEKLEMEDE, DEVAM_EDIYOR, TAMAM, HATA };

    static UygulamaDurumu& instance() { static UygulamaDurumu s; return s; }

    Led    ledDurum()    const { return _led; }
    Wifi   wifiDurum()   const { return _wifi; }
    Sensor sensorDurum() const { return _sensor; }
    Ota    otaDurum()    const { return _ota; }

    void setLed(Led l)        { 
        _led = l; 
        if(g_log) g_log->ekle("DURUM", l==Led::ACIK ? "LED Açık" : "LED Kapalı");
    }
    void setWifi(Wifi w)      { _wifi = w; }
    void setSensor(Sensor s)  { _sensor = s; }
    void setOta(Ota o)        { _ota = o; }

private:
    UygulamaDurumu() = default;
    Led    _led{Led::KAPALI};
    Wifi   _wifi{Wifi::BAGLI_DEGIL};
    Sensor _sensor{Sensor::BEKLEMEDE};
    Ota    _ota{Ota::BEKLEMEDE};
};


class LEDKontrol {
public:
    LEDKontrol(CRGB *buf, size_t len) : _buf(buf), _len(len), _ledOn(false) {}

    void basla() {
        FastLED.addLeds<SK6812, LED_PIN, GRB>(_buf, _len);
        FastLED.setBrightness(gamma8(255));  
        temizle(); goster();
        if(g_log) g_log->ekle("LED", "LED sistemi başlatıldı");
    }

    void parlaklikAta(uint8_t val) { 
        FastLED.setBrightness(gamma8(val));  
    }
    uint8_t parlaklikAl() const      { return FastLED.getBrightness(); }

    void goster()                 { FastLED.show(); }
    void doldur(CRGB renk)        { fill_solid(_buf, _len, renk); }
    void temizle()                { doldur(CRGB::Black); goster(); }
    CRGB* baslaPtr()              { return _buf; }
    size_t boyut() const          { return _len; }
    bool acikMi() const           { return _ledOn; }
    
    void setLed(bool on) {
        _ledOn = on;
        if (!on) { temizle(); }
        UygulamaDurumu::instance().setLed(on ? UygulamaDurumu::Led::ACIK : UygulamaDurumu::Led::KAPALI);
    }
    
private:
    CRGB* _buf;
    size_t _len;
    bool   _ledOn;
};


volatile bool g_sensorIsrTetik = false;

void IRAM_ATTR hareketSensoruISR() {
    g_sensorIsrTetik = true;
}

class ISensorGozlemci {
public:
    virtual void hareketOldu()        = 0;
    virtual void kalibrasyonBitti()   = 0;
    virtual void hataOldu()           = 0;
    virtual ~ISensorGozlemci() {}
};

class HareketSensoru {
public:
    HareketSensoru() : _esik(prefs.getUShort("esik", 50)) {}

    void basla() {
        pinMode(SENSOR_DIGITAL_PIN, INPUT_PULLUP);
        pinMode(SENSOR_ANALOG_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(SENSOR_DIGITAL_PIN), hareketSensoruISR, FALLING);
        if(g_log) g_log->ekle("SENSOR", "Sensör başlatıldı, eşik: " + String(_esik));
    }

    void dongu() {
        if (g_sensorIsrTetik) { 
            g_sensorIsrTetik = false; 
            _kesmeIsle(); 
        }

        uint32_t now = millis();
        if (now - _sonAnalog < ANALOG_OKUMA_ARALIK) return;
        _sonAnalog = now;
        _analogIsle();
    }

    void kalibreEt() {
        _kalibrasyonModu = true;
        _kalibrasyonBasla = millis();
        _maksGurultu = 0;
        UygulamaDurumu::instance().setSensor(UygulamaDurumu::Sensor::KALIBRASYON);
        if(g_log) g_log->ekle("SENSOR", "Kalibrasyon başladı (10 s)");
    }

    bool isMotion() const        { return _motionActive; }
    uint16_t anlikDegerGetir() const { return _sonAnalogDeger; }
    uint16_t esikGetir() const       { return _esik; }
    uint32_t sonHareketZamani() const { return _lastMotion; }

    void gozlemciEkle(ISensorGozlemci* g) { _gozlemci = g; }

private:
    uint16_t _esik;
    uint32_t _sonAnalog = 0;
    uint16_t _sonAnalogDeger = 0;
    bool     _motionActive = false;
    uint32_t _lastMotion   = 0;

    bool     _kalibrasyonModu = false;
    uint32_t _kalibrasyonBasla = 0;
    uint16_t _maksGurultu = 0;

    ISensorGozlemci* _gozlemci = nullptr;

    void _kesmeIsle() {
        uint16_t analogDeger = analogRead(SENSOR_ANALOG_PIN);
        if (analogDeger > (_esik / 2)) {
            _motionActive = true;
            _lastMotion   = millis();
            g_sonHareketZamani = millis();
            g_ledAktif = true;
            g_ledKontrol->setLed(true);
            if (_gozlemci) _gozlemci->hareketOldu();
        }
    }

    void _analogIsle() {
        uint16_t deger = analogRead(SENSOR_ANALOG_PIN);
        _sonAnalogDeger = deger;

        bool hareket = deger > _esik;
        if (hareket) {
            _motionActive = true;
            _lastMotion   = millis();
            g_sonHareketZamani = millis();
            g_ledAktif = true;
            g_ledKontrol->setLed(true);
        }

        if (_kalibrasyonModu) {
            if (millis() - _kalibrasyonBasla < 10000) {
                if (deger > _maksGurultu) _maksGurultu = deger;
            } else {
                _kalibrasyonModu = false;
                _esik = constrain((uint16_t)((float)_maksGurultu * 1.2f), 20, 1000);
                prefs.putUShort("esik", _esik);
                UygulamaDurumu::instance().setSensor(UygulamaDurumu::Sensor::BEKLEMEDE);
                if (_gozlemci) _gozlemci->kalibrasyonBitti();
                if(g_log) g_log->ekle("SENSOR", "Kalibrasyon tamam – eşik: " + String(_esik));
            }
        }
    }
};


class ZamanParlaklik {
public:
    void basla() { 
        _synchronize(); 
        _gunlukRestartKontrol(true);
    }
    
    void dongu() {
        _ntpSync();
        _parlaklikKontrol();
        _watchdogBesle();
        _bellekLog();
        _gunlukRestartKontrol();
    }

    time_t suankiUnix() { time_t t; time(&t); return t; }
    bool zamanSenkronMu() const { return _zamanSenkron; }

private:
    uint32_t _sonNtpSenkron = 0;
    bool     _zamanSenkron  = false;
    uint8_t  _mevcutMod     = 0; 
    bool     _geceResetYapildi = false;
    uint32_t _sonRestartKontrol = 0;

    void _synchronize() {
        configTime(NTP_OFFSET_SEC, 0, NTP_SERVER);
        _sonNtpSenkron = millis();
        if(g_log) g_log->ekle("ZAMAN", "NTP başlatıldı");
    }

    void _ntpSync() {
        if (millis() - _sonNtpSenkron < 30 * 60 * 1000) return;
        time_t t; time(&t);
        if (t > 1600000000L) {
            _sonNtpSenkron = millis();
            _zamanSenkron    = true;
            struct tm tm; localtime_r(&t, &tm);
            if(g_log) g_log->ekle("NTP", "Zaman senkron: " + String(tm.tm_hour) + ":" + String(tm.tm_min));
        }
    }

    uint8_t _mevcutModuAl(time_t t) const {
        struct tm tm; localtime_r(&t, &tm);
        int saat = tm.tm_hour;
        if (saat < GUNDUZ_BASLANGIC_SAATI || saat >= AKSAM_BITIS_SAATI) return 0; 
        if (saat >= GUNDUZ_BITIS_SAATI) return 1; 
        return 2; // Gündüz
    }

    void _parlaklikKontrol() {
        if (!_zamanSenkron) return;
        time_t now = suankiUnix();
        uint8_t yeniMod = _mevcutModuAl(now);
        uint8_t hedefParlaklik;
        
        switch(yeniMod) {
            case 0: hedefParlaklik = GECE_PARLAKLIK; break;
            case 1: hedefParlaklik = AKSAM_PARLAKLIK; break;
            case 2: hedefParlaklik = GUNDUZ_PARLAKLIK; break;
            default: hedefParlaklik = GUNDUZ_PARLAKLIK;
        }

      
        if (yeniMod == 0 && !_geceResetYapildi) {
            FastLED.clear(); FastLED.show();
            _geceResetYapildi = true;
            if(g_log) g_log->ekle("PARLAKLIK", "Gece reset – LED OFF");
        }
        if (yeniMod != 0) _geceResetYapildi = false;

        if (yeniMod != _mevcutMod) {
            _mevcutMod = yeniMod;
            FastLED.setBrightness(hedefParlaklik);
            const char* modAdi = (yeniMod==0) ? "Gece" : ((yeniMod==1) ? "Akşam" : "Gündüz");
            if(g_log) g_log->ekle("PARLAKLIK", String(modAdi) + " modu: " + String(hedefParlaklik));
        }
    }

    void _watchdogBesle() { esp_task_wdt_reset(); }

    void _bellekLog() {
        static uint32_t son = 0;
        if (millis() - son < BELLEK_LOG_ARALIK) return;
        son = millis();
        Serial.printf("[MEM] free=%uKB max=%uKB\n",
                      ESP.getFreeHeap() / 1024, ESP.getMaxAllocHeap() / 1024);
    }
    
    void _gunlukRestartKontrol(bool ilk = false) {
        if (!_zamanSenkron && !ilk) return;
        
        uint32_t now = millis();
        if (!ilk && now - _sonRestartKontrol < 1000) return;
        _sonRestartKontrol = now;
        
        time_t t = suankiUnix();
        struct tm tm;
        localtime_r(&t, &tm);
        
        if (tm.tm_hour == 4 && tm.tm_min == 5 && tm.tm_sec < 10) {
            if(g_log) g_log->ekle("SISTEM", "Günlük restart (04:05)");
            delay(100);
            ESP.restart();
        }
    }
};


class Guvenlik {
public:
    static void baslat(uint16_t saniye) {
        esp_task_wdt_config_t cfg = {
            .timeout_ms       = saniye * 1000,
            .idle_core_mask   = (1 << 0) | (1 << 1),
            .trigger_panic    = true
        };
        esp_task_wdt_init(&cfg);
        esp_task_wdt_add(NULL);
        if(g_log) g_log->ekle("GUVENLIK", "Watchdog " + String(saniye) + "s aktif");
    }
    static void besle() { esp_task_wdt_reset(); }
};


class Animasyon {
public:
 
    Animasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : 
        _led(lc), _renk(renk), _hiz(hiz), _sonTik(0) {}
    virtual void tik() = 0;
    virtual ~Animasyon() {}
protected:
    LEDKontrol & _led;
    CRGB         _renk;
    uint8_t      _hiz;
    uint32_t     _sonTik;
};


class DuzRenkAnimasyon : public Animasyon {
public:
    DuzRenkAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz) {}
    void tik() override { _led.doldur(_renk); _led.goster(); }
};


class NefesAnimasyon : public Animasyon {
public:
    NefesAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz) {}
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < 15) return;
        _sonTik = now;
        
        
        uint16_t bpm = map(_hiz, 0, 100, 10, 60); 
        uint8_t wave = beatsin8(bpm, 20, 255); 
        
        CHSV hsv = rgb2hsv_approximate(_renk);
        hsv.s = max(150, hsv.s - (wave/4)); 
        
        CRGB renkOut;
        hsv2rgb_rainbow(hsv, renkOut);
        renkOut.nscale8_video(wave);
        
        _led.doldur(renkOut);
        _led.goster();
    }
};

class GokkusagiAnimasyon : public Animasyon {
    uint16_t _ton;
public:
    GokkusagiAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz), _ton(0) {}
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < map(_hiz, 0, 100, 40, 5)) return;
        _sonTik = now;
        _ton += map(_hiz, 0, 100, 1, 5);
        
        uint8_t bpm = map(_hiz, 0, 100, 5, 30);
        for(size_t i=0; i<_led.boyut(); i++) {
            uint8_t hue = _ton + (i * 256 / _led.boyut());
           
            uint8_t val = 255 - (beatsin8(bpm, 0, 80, 0, i * 10) / 2);
            _led.baslaPtr()[i] = CHSV(hue, 255, val);
        }
        _led.goster();
    }
};


class TarayiciAnimasyon : public Animasyon {
    float _konum;
    float _yon;
public:
    TarayiciAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz), _konum(0), _yon(1) {}
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < map(_hiz, 0, 100, 30, 5)) return;
        _sonTik = now;

        
        for (size_t i=0; i<_led.boyut(); i++) _led.baslaPtr()[i].nscale8_video(200);

        _konum += _yon * map(_hiz, 0, 100, 1, 4) * 0.5f;
        if (_konum >= _led.boyut() - 1) { _yon = -1; _konum = _led.boyut() - 1; }
        if (_konum <= 0) { _yon = 1; _konum = 0; }

        
        uint16_t iPos = (uint16_t)_konum;
        uint8_t frac = (_konum - iPos) * 255;
        
        _led.baslaPtr()[iPos] += _renk;
        _led.baslaPtr()[iPos].nscale8(255 - frac);
        if (iPos + 1 < _led.boyut()) {
            _led.baslaPtr()[iPos+1] += _renk;
            _led.baslaPtr()[iPos+1].nscale8(frac);
        }
        _led.goster();
    }
};


class KuyrukluYildizAnimasyon : public Animasyon {
    float _konum;
public:
    KuyrukluYildizAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz), _konum(0) {}
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < max(4, 40 - _hiz/2)) return;
        _sonTik = now;

        for(size_t i=0; i<_led.boyut(); i++) _led.baslaPtr()[i].nscale8_video(220);

        _konum += map(_hiz, 0, 100, 1, 6) * 0.5f;
        if(_konum >= _led.boyut()) _konum -= _led.boyut();
        
        uint16_t iPos = (uint16_t)_konum;
        _led.baslaPtr()[iPos] = CRGB::White; 
        
        
        for(int t=1; t<12; t++) {
             int tail = (iPos - t + _led.boyut()) % _led.boyut();
             _led.baslaPtr()[tail] += _renk;
             _led.baslaPtr()[tail].nscale8(255 / (t+1));
        }
        _led.goster();
    }
};


class MeteorAnimasyon : public Animasyon {
    struct Meteor { float pos; float speed; uint8_t len; uint8_t hue; bool active; };
    static const uint8_t MAX_M = 6;
    Meteor _meteors[MAX_M];
public:
    MeteorAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz) {
        for(int i=0; i<MAX_M; i++) _meteors[i].active = false;
    }
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < max(8, 60 - _hiz)) return;
        _sonTik = now;

        fadeToBlackBy(_led.baslaPtr(), _led.boyut(), 40);
        CHSV baseHSV = rgb2hsv_approximate(_renk);

       
        if (random8() < (40 + _hiz)) {
            for (int m=0; m<MAX_M; m++) {
                if (!_meteors[m].active) {
                    _meteors[m].active = true;
                    _meteors[m].pos = random16(_led.boyut());
                    _meteors[m].len = random8(6, 24);
                    _meteors[m].speed = 1.0f + (_hiz/40.0f);
                    _meteors[m].hue = baseHSV.h + random8(0,40) - 20;
                    break;
                }
            }
        }

        for (int m=0; m<MAX_M; m++) {
            if (!_meteors[m].active) continue;
            
            _meteors[m].pos += _meteors[m].speed;
            int iPos = (int)_meteors[m].pos;
            
            for (int t=0; t<_meteors[m].len; t++) {
                int idx = iPos - t;
                if (idx < 0) idx += _led.boyut();
                if (idx >= (int)_led.boyut()) idx -= _led.boyut();
                
                uint8_t bright = 255 - ((255 * t) / _meteors[m].len);
                _led.baslaPtr()[idx] += CHSV(_meteors[m].hue, baseHSV.s, bright);
            }
            
           
            if (random8() < 25) {
                int spark = (iPos + random8(8)) % _led.boyut();
                _led.baslaPtr()[spark] += CRGB::White;
            }
            if (random8() < 10 + _hiz/4 && iPos > (int)_led.boyut() - 5) {
                _meteors[m].active = false;
            }
        }
        _led.goster();
    }
};


class GradyanAnimasyon : public Animasyon {
    uint16_t _faz;
public:
    GradyanAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz), _faz(0) {}
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < max(6, 48 - _hiz)) return;
        _sonTik = now;
        
        _faz += 2 + (_hiz / 6);
        CHSV baseHSV = rgb2hsv_approximate(_renk);

        for (size_t i=0; i<_led.boyut(); i++) {
            uint16_t p = (i * 512 / _led.boyut());
            uint8_t hue = baseHSV.h + (uint8_t)((p + (_faz >> 1) + sin8(i * 2 + (_faz >> 3))) & 0x3F); 
            uint8_t wave = beatsin8(6 + (_hiz/10), 80, 255);
            _led.baslaPtr()[i] = CHSV(hue, baseHSV.s, wave);
        }
        _led.goster();
    }
};


class AtesAnimasyon : public Animasyon {
    uint8_t* _heat;
    uint16_t _fireTime;
public:
    AtesAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz), _fireTime(0) {
        _heat = new uint8_t[_led.boyut()];
        memset(_heat, 0, _led.boyut());
    }
    ~AtesAnimasyon() { delete[] _heat; } 
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < max(15, 40 - _hiz)) return;
        _sonTik = now;
        _fireTime += 3 + (_hiz / 8);
        size_t len = _led.boyut();

        for (size_t i = 0; i < len / 8; i++) {
            int pos = random8(len / 10);
            _heat[pos] = qadd8(_heat[pos], random8(100, 255));
        }
        for (int i = len - 1; i >= 2; i--) {
            _heat[i] = (_heat[i-1] + _heat[i-2] + _heat[i-2]) / 3;
        }
        for (size_t i = 0; i < len; i++) {
            _heat[i] = qsub8(_heat[i], random8(5, 15));
        }
        if (random8() < 30 + (_hiz / 3)) {
            int spark = random16(len / 4, len / 2);
            _heat[spark] = qadd8(_heat[spark], random8(100, 180));
        }
        
        uint8_t wave = beatsin8(6 + (_hiz/15), 50, 150); 
        for (size_t i = 0; i < len; i++) {
            CRGB col = ColorFromPalette(HeatColors_p, scale8(_heat[i], 240));
            uint8_t waveEffect = sin8((i*3) + (_fireTime>>2));
            col.nscale8_video(scale8(waveEffect, wave));
            _led.baslaPtr()[i] = col;
        }
        _led.goster();
    }
};


class BolgeAnimasyon : public Animasyon {
    uint16_t _phase;
public:
    BolgeAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz), _phase(0) {}
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < max(6, 50 - _hiz)) return;
        _sonTik = now;
        
        _phase += 3 + (_hiz / 6);
        CHSV baseHSV = rgb2hsv_approximate(_renk);
        
        int zones = constrain(_led.boyut() / 30, 3, 10);
        int zoneSize = max(1, (int)_led.boyut() / zones);
        
        for (int i=0; i<(int)_led.boyut(); i++) {
            int z = i / zoneSize;
            uint8_t hue = baseHSV.h + (z * (256/zones)) + (_phase / 4);
            uint8_t wave = sin8((i*6) + (_phase >> (1 + (z%3))));
            
            CRGB c = CHSV(hue, baseHSV.s, wave);
            if (random8() < (10 + _hiz/8)) c += CHSV(hue + random8(40,100), 255, 255);
            _led.baslaPtr()[i] = c;
        }
        _led.goster();
    }
};


class AuroraAnimasyon : public Animasyon {
    uint16_t _z;
public:
    AuroraAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz), _z(0) {}
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < max(4, 40 - _hiz/2)) return;
        _sonTik = now;
        
        _z += 1 + (_hiz / 20);
        CHSV baseHSV = rgb2hsv_approximate(_renk);

        for (size_t i = 0; i < _led.boyut(); i++) {
            uint8_t l1 = sin8((i*2) + (_z>>2));
            uint8_t l2 = sin8((i*3) + (_z>>1));
            uint8_t l3 = sin8((i*4) + _z);
            
            uint8_t hue = baseHSV.h + (l1>>3);
            uint8_t bri = qadd8(l2, l3);
            bri = scale8(bri, 180); 
            _led.baslaPtr()[i] = CHSV(hue, baseHSV.s, bri);
        }
        _led.goster();
    }
};


class MatrixAnimasyon : public Animasyon {
    struct Drop { float pos; float speed; uint8_t len; uint8_t hue; };
    Drop* _drops;
    uint8_t _maxDrops;
public:
    MatrixAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz) {
        _maxDrops = min(48, max(8, (int)_led.boyut()/8));
        _drops = new Drop[_maxDrops];
        uint8_t baseHue = rgb2hsv_approximate(_renk).h;
        
        for(int i=0; i<_maxDrops; i++) {
            _drops[i].pos = -random16(_led.boyut());
            _drops[i].speed = random8(6,16)/8.0f;
            _drops[i].len = random8(3,18);
            _drops[i].hue = baseHue + random8(0,30) - 15;
        }
    }
    ~MatrixAnimasyon() { delete[] _drops; }
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < max(8, 60 - _hiz)) return;
        _sonTik = now;

        for(size_t i=0; i<_led.boyut(); i++) _led.baslaPtr()[i].nscale8_video(200);

        uint8_t baseHue = rgb2hsv_approximate(_renk).h;
        for(int i=0; i<_maxDrops; i++) {
            _drops[i].pos += _drops[i].speed * (1.0f + _hiz/100.0f);
            
            if(_drops[i].pos - _drops[i].len > _led.boyut()) {
                _drops[i].pos = -random8(0, _led.boyut()/3);
                _drops[i].speed = random8(6,20)/8.0f;
                _drops[i].len = random8(3,18);
                _drops[i].hue = baseHue + random8(0,40) - 20;
            }
            
            int head = (int)floor(_drops[i].pos);
            for(int s=0; s<_drops[i].len; s++) {
                int idx = head - s;
                if(idx < 0 || idx >= (int)_led.boyut()) continue;
                uint8_t bright = (s == 0) ? 255 : qsub8(255, s * 200 / _drops[i].len);
                _led.baslaPtr()[idx] += CHSV(_drops[i].hue, 220, bright);
            }
        }
        _led.goster();
    }
};


class PariltiAnimasyon : public Animasyon {
public:
    PariltiAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz) {}
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < max(8, 40 - _hiz)) return;
        _sonTik = now;

        uint8_t density = constrain(6 + (_hiz / 5), 6, 140);
        CHSV baseHSV = rgb2hsv_approximate(_renk);

        for (size_t i = 0; i < _led.boyut(); i++) {
            uint16_t t = (uint16_t)(now >> 4);
            uint16_t seed = (uint16_t)((i * 257) ^ t); 
            uint8_t chance = (uint8_t)(((seed >> 8) ^ (seed & 0xFF)) & 0xFF);

            if (random8() < density && (chance & 0x7F) < density) {
                uint8_t phase = (uint8_t)((now >> 2) + (seed & 0xFF));
                uint8_t env = sin8(phase); 
                uint8_t hueOff = baseHSV.h + (seed & 31) - 16;
                _led.baslaPtr()[i] = _led.baslaPtr()[i] + CHSV(hueOff, min(255, baseHSV.s + 20), env);
            } else {
                _led.baslaPtr()[i].nscale8_video(240);
            }
        }
        _led.goster();
    }
};


class DalgaAnimasyon : public Animasyon {
    struct Ripple { int center; float radius; float speed; uint8_t hue; uint8_t life; bool active; };
    static const uint8_t MAX_R = 6;
    Ripple _ripples[MAX_R];
public:
    DalgaAnimasyon(LEDKontrol &lc, CRGB renk, uint8_t hiz) : Animasyon(lc, renk, hiz) {
        for(int i=0; i<MAX_R; i++) _ripples[i].active = false;
    }
    void tik() override {
        uint32_t now = millis();
        if (now - _sonTik < map(_hiz, 0, 100, 45, 10)) return;
        _sonTik = now;

        for (size_t i=0; i<_led.boyut(); i++) _led.baslaPtr()[i].nscale8_video(200);

        if (random8() < (40 + _hiz)) {
            for (uint8_t i=0; i<MAX_R; i++) {
                if (!_ripples[i].active) {
                    _ripples[i].active = true;
                    _ripples[i].center = random16(_led.boyut());
                    _ripples[i].radius = 0.0f;
                    _ripples[i].speed = (0.6f + random8(60)/100.0f) * (1.0f + _hiz/100.0f);
                    _ripples[i].hue = rgb2hsv_approximate(_renk).h + random8(0,40) - 20;
                    _ripples[i].life = 255;
                    break;
                }
            }
        }

        for (uint8_t i=0; i<MAX_R; i++) {
            if (!_ripples[i].active) continue;
            _ripples[i].radius += _ripples[i].speed;
            _ripples[i].life = qsub8(_ripples[i].life, 2 + (_hiz/40));

            int center = _ripples[i].center;
            float rad = _ripples[i].radius;
            int width = max(3, (int)(5 + _hiz/20));
            int start = (int)floor(rad) - width;
            int end   = (int)ceil(rad) + width;

            for (int d = start; d <= end; d++) {
                int pos = center + d;
                while (pos < 0) pos += _led.boyut();
                while (pos >= (int)_led.boyut()) pos -= _led.boyut();

                int dist = abs(d);
                int denom = max(1, width + (int)rad/6);
                uint8_t fall = qsub8(255, (dist * 200UL / denom));
                uint8_t mod = sin8(dist * 12 + (int)rad*3);
                uint8_t b = qmul8(fall, mod);

                _led.baslaPtr()[pos] += CHSV(_ripples[i].hue, 220, b);
            }
            if (_ripples[i].life < 8 || rad > (_led.boyut()+50)) _ripples[i].active = false;
        }
        _led.goster();
    }
};


class AnimasyonFabrikasi {
public:
    static Animasyon* olustur(uint8_t tip, LEDKontrol &lc, CRGB renk, uint8_t hiz) {
        switch (tip) {
            case 0:  return new DuzRenkAnimasyon(lc, renk, hiz);
            case 1:  return new NefesAnimasyon(lc, renk, hiz);
            case 2:  return new GokkusagiAnimasyon(lc, renk, hiz);
            case 3:  return new TarayiciAnimasyon(lc, renk, hiz);
            case 4:  return new KuyrukluYildizAnimasyon(lc, renk, hiz);
            case 5:  return new MeteorAnimasyon(lc, renk, hiz);
            case 6:  return new GradyanAnimasyon(lc, renk, hiz);
            case 7:  return new AtesAnimasyon(lc, renk, hiz);
            case 8:  return new BolgeAnimasyon(lc, renk, hiz);
            case 9:  return new AuroraAnimasyon(lc, renk, hiz);
            case 10: return new MatrixAnimasyon(lc, renk, hiz);
            case 11: return new PariltiAnimasyon(lc, renk, hiz);
            case 12: return new DalgaAnimasyon(lc, renk, hiz);
            default: return new DuzRenkAnimasyon(lc, renk, hiz);
        }
    }
};


class WebAPI : public ISensorGozlemci {
public:
    WebAPI(LEDKontrol &lc, HareketSensoru &sensor,
           Preferences &prefs, AsyncWebServer &srv)
        : _led(lc), _sensor(sensor), _prefs(prefs), _srv(srv) {
        _sensor.gozlemciEkle(this);
    }

    void basla() {
        _srv.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
            String html = FPSTR(index_html);
            html.replace("%WIFISTATUS%", WiFi.status()==WL_CONNECTED ? "Bağlı" : "Koptu");
            html.replace("%IPADDRESS%", WiFi.localIP().toString());
            html.replace("%CHIPID%", String((uint32_t)ESP.getEfuseMac(), HEX));
            html.replace("%FREEHEAP%", String(ESP.getFreeHeap()/1024));
            req->send(200, "text/html", html);
        });

        _srv.on("/ota", HTTP_GET, [](AsyncWebServerRequest *req){
            req->send(200, "text/html", FPSTR(ota_html));
        });

        _srv.on("/doUpdate", HTTP_POST,
            [](AsyncWebServerRequest *){},
            [this](AsyncWebServerRequest *req, const String &filename,
                   size_t index, uint8_t *data, size_t len, bool final) {
                _handleOta(req, filename, index, data, len, final);
            });

        _srv.on("/api/anim",        HTTP_GET, [this](AsyncWebServerRequest *r){ _apiAnim(r); });
        _srv.on("/api/parlaklik",   HTTP_GET, [this](AsyncWebServerRequest *r){ _apiParlaklik(r); });
        _srv.on("/api/hiz",         HTTP_GET, [this](AsyncWebServerRequest *r){ _apiHiz(r); });
        _srv.on("/api/renk",        HTTP_GET, [this](AsyncWebServerRequest *r){ _apiRenk(r); });
        _srv.on("/api/bekleme",     HTTP_GET, [this](AsyncWebServerRequest *r){ _apiBekleme(r); });
        _srv.on("/api/hassasiyet",  HTTP_GET, [this](AsyncWebServerRequest *r){ _apiHassasiyet(r); });
        _srv.on("/api/kalibrasyon", HTTP_GET, [this](AsyncWebServerRequest *r){ _apiKalibrasyon(r); });
        _srv.on("/api/durum",       HTTP_GET, [this](AsyncWebServerRequest *r){ _apiDurum(r); });
        _srv.on("/api/loglar",      HTTP_GET, [this](AsyncWebServerRequest *r){ _apiLoglar(r); });

        _ws.onEvent([this](AsyncWebSocket *server,
                           AsyncWebSocketClient *client,
                           AwsEventType type,
                           void *arg, uint8_t *data, size_t len){
            if (type == WS_EVT_CONNECT) {
                Serial.printf("[WS] client %u bağlandı\n", client->id());
                _pushStatus(client);
            }
        });
        _srv.addHandler(&_ws);

        _srv.begin();
        if(g_log) g_log->ekle("WEB", "HTTP+WS server hazır");
    }

    void hareketOldu() override {
        _lastMotion = millis();
        UygulamaDurumu::instance().setLed(UygulamaDurumu::Led::ACIK);
        _pushStatusAll();
    }

    void kalibrasyonBitti() override {
        if(g_log) g_log->ekle("WEB", "Kalibrasyon tamam");
    }

    void hataOldu() override {
        if(g_log) g_log->ekle("WEB", "Sensör hatası!");
    }

private:
    void _handleOta(AsyncWebServerRequest *req, const String &fn,
                    size_t index, uint8_t *data, size_t len, bool final) {
        static bool inProgress = false;
        if (!index) {
            inProgress = true;
            g_otaInProgress = true;
            if(g_log) g_log->ekle("OTA", "Başlatıldı: " + fn);
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
                inProgress = false; g_otaInProgress = false;
                req->send(500, "text/plain", "OTA başlatılamadı");
                return;
            }
        }

        if (len && (Update.write(data, len) != len)) {
            Update.printError(Serial);
            inProgress = false; g_otaInProgress = false;
            req->send(500, "text/plain", "OTA yazma hatası");
            return;
        }

        if (final) {
            if (Update.end(true)) {
                if(g_log) g_log->ekle("OTA", "Başarılı, restart");
                req->send(200, "text/plain",
                          "Güncelleme başarılı → yeniden başlatılıyor...");
                delay(100);
                ESP.restart();
            } else {
                Update.printError(Serial);
                req->send(500, "text/plain", "OTA tamamlanamadı");
            }
            inProgress = false; g_otaInProgress = false;
        }
    }

    void _apiAnim(AsyncWebServerRequest *r){
        if (!r->hasParam("value")) { r->send(400,"text/plain","Eksik parametre"); return; }
        uint8_t tip = constrain(r->getParam("value")->value().toInt(),0,12);
        if(g_anlikAnim){ delete g_anlikAnim; g_anlikAnim = nullptr; }
        uint32_t renkHex = _prefs.getULong("renk",0xFFFFFF);
        CRGB renk((renkHex>>16)&0xFF,(renkHex>>8)&0xFF,renkHex&0xFF);
        uint8_t hiz = _prefs.getUChar("hiz",50);
        g_anlikAnim = AnimasyonFabrikasi::olustur(tip, *g_ledKontrol, renk, hiz);
        _prefs.putUChar("animasyon", tip);
        if(g_log) g_log->ekle("API", "Animasyon: " + String(tip));
        r->send(200,"text/plain","OK");
    }

    void _apiParlaklik(AsyncWebServerRequest *r){
        if (!r->hasParam("value")) { r->send(400,"text/plain","Eksik parametre"); return; }
        uint8_t pct = constrain(r->getParam("value")->value().toInt(),0,100);
        uint8_t rawVal = map(pct,0,100,0,255);      
        uint8_t gammaVal = gamma8(rawVal);          
        g_ledKontrol->parlaklikAta(gammaVal);
        _prefs.putUChar("parlaklik", pct);
        r->send(200,"text/plain","OK");
    }

    void _apiHiz(AsyncWebServerRequest *r){
        if (!r->hasParam("value")) { r->send(400,"text/plain","Eksik parametre"); return; }
        uint8_t hiz = constrain(r->getParam("value")->value().toInt(),0,100);
        _prefs.putUChar("hiz", hiz);
        if (g_anlikAnim) {
            uint32_t renkHex = _prefs.getULong("renk",0xFFFFFF);
            CRGB renk((renkHex>>16)&0xFF,(renkHex>>8)&0xFF,renkHex&0xFF);
            delete g_anlikAnim;
            g_anlikAnim = AnimasyonFabrikasi::olustur(
                _prefs.getUChar("animasyon",0), *g_ledKontrol, renk, hiz);
        }
        r->send(200,"text/plain","OK");
    }

    void _apiRenk(AsyncWebServerRequest *r){
        if (!r->hasParam("value")) { r->send(400,"text/plain","Eksik parametre"); return; }
        String hex = r->getParam("value")->value();
        uint32_t rgb = strtoul(hex.c_str(), nullptr, 16);
        _prefs.putULong("renk", rgb);
        if (g_anlikAnim) {
            CRGB renk((rgb>>16)&0xFF, (rgb>>8)&0xFF, rgb&0xFF);
            delete g_anlikAnim;
            g_anlikAnim = AnimasyonFabrikasi::olustur(
                _prefs.getUChar("animasyon",0), *g_ledKontrol, renk,
                _prefs.getUChar("hiz",50));
        }
        r->send(200,"text/plain","OK");
    }

    void _apiBekleme(AsyncWebServerRequest *r){
        if (!r->hasParam("value")) { r->send(400,"text/plain","Eksik parametre"); return; }
        uint16_t sec = constrain(r->getParam("value")->value().toInt(),5,60);
        _prefs.putUShort("bekleme", sec);
        r->send(200,"text/plain","OK");
    }

    void _apiHassasiyet(AsyncWebServerRequest *r){
        if (!r->hasParam("value")) { r->send(400,"text/plain","Eksik parametre"); return; }
        uint8_t pct = constrain(r->getParam("value")->value().toInt(),0,100);
        uint16_t esik = map(pct,0,100,20,200);
        _prefs.putUChar("hassasiyet", pct);
        _prefs.putUShort("esik", esik);
        r->send(200,"text/plain","OK");
    }

    void _apiKalibrasyon(AsyncWebServerRequest *r){
        _sensor.kalibreEt();
        r->send(200,"text/plain","Kalibrasyon başladı");
    }

    void _apiDurum(AsyncWebServerRequest *r){
        StaticJsonDocument<512> doc;
        doc["animasyon"]  = _prefs.getUChar("animasyon",0);
        doc["parlaklik"]  = _prefs.getUChar("parlaklik",DEFAULT_BRIGHT);
        doc["hiz"]        = _prefs.getUChar("hiz",50);
        doc["rColor"]     = _prefs.getULong("renk",0xFFFFFF);
        doc["bekleme"]    = _prefs.getUShort("bekleme",20);
        doc["hassasiyet"] = _prefs.getUChar("hassasiyet",80);
        doc["lastMotion"] = _lastMotion;
        doc["ledON"]      = g_ledAktif;
        doc["sensValue"]  = g_sensor->anlikDegerGetir();
        doc["threshold"]  = g_sensor->esikGetir();
        doc["uptime"]     = millis() / 1000;
        String out; serializeJson(doc,out);
        r->send(200,"application/json",out);
    }

    void _apiLoglar(AsyncWebServerRequest *r){
        if(!g_log) { r->send(200,"application/json","[]"); return; }
        StaticJsonDocument<2048> doc;
        JsonArray arr = doc.to<JsonArray>();
        
        int idx = g_log->index();
        int sayi = g_log->sayi();
        const auto* kayitlar = g_log->kayitlar();
        
        for(int i=0; i<sayi; i++){
            int currentIdx = (idx - 1 - i + LOG_SAYISI) % LOG_SAYISI;
            JsonObject obj = arr.createNestedObject();
            obj["zaman"] = kayitlar[currentIdx].zaman;
            obj["tip"] = kayitlar[currentIdx].tip;
            obj["mesaj"] = kayitlar[currentIdx].mesaj;
        }
        
        String out; serializeJson(doc,out);
        r->send(200,"application/json",out);
    }

    AsyncWebSocket _ws{"/ws"};

    void _pushStatus(AsyncWebSocketClient *client) {
        StaticJsonDocument<256> doc;
        doc["ledON"]      = g_ledAktif;
        doc["sensValue"]  = g_sensor->anlikDegerGetir();
        doc["threshold"]  = g_sensor->esikGetir();
        doc["lastMotion"] = _lastMotion;
        String out; serializeJson(doc,out);
        client->text(out);
    }

    void _pushStatusAll() {
        StaticJsonDocument<256> doc;
        doc["ledON"]      = g_ledAktif;
        doc["sensValue"]  = g_sensor->anlikDegerGetir();
        doc["threshold"]  = g_sensor->esikGetir();
        doc["lastMotion"] = _lastMotion;
        String out; serializeJson(doc,out);
        _ws.textAll(out);
    }

    LEDKontrol   &_led;
    HareketSensoru &_sensor;
    Preferences  &_prefs;
    AsyncWebServer &_srv;
    uint32_t _lastMotion = 0;
};


static void sensorTask(void *) {
    for (;;) {
        if (g_sensor) g_sensor->dongu();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void animTask(void *) {
    uint32_t lastMotionTime = 0;
    uint8_t idleDim = 255; 

    for (;;) {
        uint16_t beklemeSuresi = prefs.getUShort("bekleme",20) * 1000;
        
        // Hareket kontrolü
        if(g_ledAktif && (millis() - g_sonHareketZamani > beklemeSuresi)) {
            g_ledAktif = false;
            g_ledKontrol->setLed(false);
            idleDim = 255; // Reset parlaklık
            if(g_log) g_log->ekle("LED", "Zaman aşımı - kapandı");
        }

       
        if (g_anlikAnim && g_ledAktif) {
            g_anlikAnim->tik();
            lastMotionTime = millis(); 
        } 
       
        else if (!g_ledAktif && g_anlikAnim) {
            if (millis() - lastMotionTime > IDLE_TIMEOUT) {
              
                if (idleDim > 5) idleDim -= 5;
                g_ledKontrol->parlaklikAta(idleDim);
                g_anlikAnim->tik(); 
            } else {
               
                g_ledKontrol->temizle();
                idleDim = 255; 
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void zamanTask(void *pv) {
    ZamanParlaklik *z = static_cast<ZamanParlaklik*>(pv);
    for (;;) {
        z->dongu();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Koridor LED Kontrolü (ESP32‑S3) – Başlatılıyor ===");

    g_log = new LogSistemi();
    g_log->ekle("SISTEM", "Başlatılıyor...");

    prefs.begin("led-kontrol", false);

    g_ledKontrol = new LEDKontrol(leds, LED_SAYISI);
    g_ledKontrol->basla();
    uint8_t brightPct = prefs.getUChar("parlaklik", DEFAULT_BRIGHT);
    uint8_t rawBrightness = brightPct * 255 / 100;
    g_ledKontrol->parlaklikAta(gamma8(rawBrightness));  

    g_sensor = new HareketSensoru();
    g_sensor->basla();

    g_zaman = new ZamanParlaklik();
    g_zaman->basla();

    Guvenlik::baslat(15);

    uint32_t renkHex = prefs.getULong("renk",0xFFFFFF);
    CRGB renk((renkHex>>16)&0xFF,(renkHex>>8)&0xFF,renkHex&0xFF);
    uint8_t hiz = prefs.getUChar("hiz", 50);
    g_anlikAnim = AnimasyonFabrikasi::olustur(
        prefs.getUChar("animasyon",0), *g_ledKontrol, renk, hiz);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Wi‑Fi bağlanıyor");
    uint32_t t0 = millis();
    while (WiFi.status()!=WL_CONNECTED && millis()-t0 < 15000) {
        delay(500); Serial.print('.');
    }
    if (WiFi.status()==WL_CONNECTED) {
        Serial.printf("\nWi‑Fi bağlandı – IP: %s\n",
                      WiFi.localIP().toString().c_str());
        UygulamaDurumu::instance().setWifi(UygulamaDurumu::Wifi::BAGLI);
        g_log->ekle("WIFI", "Bağlandı, IP: " + WiFi.localIP().toString());
    } else {
        Serial.println("\nWi‑Fi bağlanamadı");
        UygulamaDurumu::instance().setWifi(UygulamaDurumu::Wifi::HATA);
        g_log->ekle("WIFI", "Bağlanamadı");
    }

    g_api = new WebAPI(*g_ledKontrol, *g_sensor, prefs, server);
    g_api->basla();

    xTaskCreatePinnedToCore(sensorTask, "Sensor", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(animTask,   "Anim",   4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(zamanTask,  "Zaman",  4096, g_zaman, 1, nullptr, 0);

    g_log->ekle("SISTEM", "Tüm modüller hazır");
    Serial.println("[SETUP] Tüm modüller hazır");
}


void loop() {
    static uint32_t lastReconnect = 0;
    const uint32_t reconnectInterval = 30000;
    
    if (WiFi.status()!=WL_CONNECTED && millis()-lastReconnect>reconnectInterval) {
        lastReconnect = millis();
        Serial.println("\nWi‑Fi koptu → yeniden bağlanıyor...");
        if(g_log) g_log->ekle("WIFI", "Yeniden bağlanıyor...");
        WiFi.disconnect(true);
        delay(100);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        uint32_t t0 = millis();
        while (WiFi.status()!=WL_CONNECTED && millis()-t0 < 10000) {
            delay(500); Serial.print('.');
        }
        if (WiFi.status()==WL_CONNECTED) {
            Serial.printf("\nWi‑Fi yeniden bağlandı – IP: %s\n",
                          WiFi.localIP().toString().c_str());
            if(g_log) g_log->ekle("WIFI", "Yeniden bağlandı");
        } else {
            Serial.println("\nYeniden bağlanma başarısız");
        }
    }

    Guvenlik::besle();

    delay(10);
}

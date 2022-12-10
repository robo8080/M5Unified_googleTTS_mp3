#include <M5UnitLCD.h>
#include <M5UnitOLED.h>
#include <M5Unified.h>
#include <AudioOutput.h>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourcePROGMEM.h>
#include <google-tts.h>
#include <HTTPClient.h>

const char *SSID = "**********";
const char *PASSWORD = "**********";

TTS tts;
HTTPClient http;
WiFiClient client;

/// set M5Speaker virtual channel (0-7)
static constexpr uint8_t m5spk_virtual_channel = 0;

class AudioOutputM5Speaker : public AudioOutput
{
  public:
    AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0)
    {
      _m5sound = m5sound;
      _virtual_ch = virtual_sound_channel;
    }
    virtual ~AudioOutputM5Speaker(void) {};
    virtual bool begin(void) override { return true; }
    virtual bool ConsumeSample(int16_t sample[2]) override
    {
      if (_tri_buffer_index < tri_buf_size)
      {
        _tri_buffer[_tri_index][_tri_buffer_index  ] = sample[0];
        _tri_buffer[_tri_index][_tri_buffer_index+1] = sample[0];
        _tri_buffer_index += 2;

        return true;
      }

      flush();
      return false;
    }
    virtual void flush(void) override
    {
      if (_tri_buffer_index)
      {
        _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, hertz, true, 1, _virtual_ch);
        _tri_index = _tri_index < 2 ? _tri_index + 1 : 0;
        _tri_buffer_index = 0;
        ++_update_count;
      }
    }
    virtual bool stop(void) override
    {
      flush();
      _m5sound->stop(_virtual_ch);
      for (size_t i = 0; i < 3; ++i)
      {
        memset(_tri_buffer[i], 0, tri_buf_size * sizeof(int16_t));
      }
      ++_update_count;
      return true;
    }

    const int16_t* getBuffer(void) const { return _tri_buffer[(_tri_index + 2) % 3]; }
    const uint32_t getUpdateCount(void) const { return _update_count; }

  protected:
    m5::Speaker_Class* _m5sound;
    uint8_t _virtual_ch;
    static constexpr size_t tri_buf_size = 640;
    int16_t _tri_buffer[3][tri_buf_size];
    size_t _tri_buffer_index = 0;
    size_t _tri_index = 0;
    size_t _update_count = 0;
};

// Called when there's a warning or error (like a buffer underflow or decode hiccup)
void StatusCallback(void *cbData, int code, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  // Note that the string may be in PROGMEM, so copy it to RAM for printf
  char s1[64];
  strncpy_P(s1, string, sizeof(s1));
  s1[sizeof(s1)-1]=0;
  Serial.printf("STATUS(%s) '%d' = '%s'\n", ptr, code, s1);
  Serial.flush();
}

AudioGeneratorMP3 *mp3 = nullptr;
static AudioOutputM5Speaker out(&M5.Speaker, m5spk_virtual_channel);
AudioFileSourcePROGMEM *file = nullptr;
uint8_t mp3buff[1024*20];

void mp3_play(char *buff, int len) {
  file = new AudioFileSourcePROGMEM(buff, len);
   mp3->begin(file, &out);
}

void google_tts(char *text, char *lang) {
  Serial.println("tts Start");
  String link =  "http" + tts.getSpeechUrl(text, lang).substring(5);
//    String URL= "http" + tts.getSpeechUrl("こんにちは、世界！", "ja").substring(5);
//    String link = "http" + tts.getSpeechUrl("Hello","en-US").substring(5);
  Serial.println(link);

  http.begin(client, link);
  http.setReuse(true);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
//    cb.st(STATUS_HTTPFAIL, PSTR("Can't open HTTP request"));
    return ;
  }

  WiFiClient *ttsclient = http.getStreamPtr();
  if (ttsclient->available() > 0) {
    int i = 0;
    int len = sizeof(mp3buff);
    int count = 0;
    while (ttsclient->available() > 0) {
      int bytesread = ttsclient->read(&mp3buff[i], len);
//     Serial.printf("%d Bytes Read\n",bytesread);
      i = i + bytesread;
      if(i > sizeof(mp3buff))
      {
        break;
      } else {
        len = len - bytesread;
        if(len <= 0) break;
      }
      delay(100);
    }
    Serial.printf("Total %d Bytes Read\n",i);
    ttsclient->stop();
    http.end();
    file = new AudioFileSourcePROGMEM(mp3buff, i);
    mp3->begin(file, &out);
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.external_spk = true;    /// use external speaker (SPK HAT / ATOMIC SPK)
//cfg.external_spk_detail.omit_atomic_spk = true; // exclude ATOMIC SPK
//cfg.external_spk_detail.omit_spk_hat    = true; // exclude SPK HAT

  M5.begin(cfg);

  { /// custom setting
    auto spk_cfg = M5.Speaker.config();
    /// Increasing the sample_rate will improve the sound quality instead of increasing the CPU load.
    spk_cfg.sample_rate = 96000; // default:64000 (64kHz)  e.g. 48000 , 50000 , 80000 , 96000 , 100000 , 128000 , 144000 , 192000 , 200000
//    spk_cfg.task_pinned_core = PRO_CPU_NUM;
    M5.Speaker.config(spk_cfg);
  }
  M5.Speaker.begin();
  
  M5.Display.setTextFont(&fonts::efontJA_16);
//  M5.Lcd.setTextSize(2);
  delay(100);

  Serial.println("Connecting to WiFi");
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);  WiFi.begin(SSID, PASSWORD);
  M5.Lcd.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    M5.Lcd.print(".");
  }
  M5.Lcd.println("\nConnected");
  
  audioLogger = &Serial;

  mp3 = new AudioGeneratorMP3();
//  mp3->RegisterStatusCB(StatusCallback, (void*)"mp3");

  M5.Speaker.setChannelVolume(m5spk_virtual_channel, 200);
}

void loop()
{
  static int lastms = 0;
  static int counter = 0;
  M5.update();
  if (M5.BtnA.wasPressed())
  {
    M5.Speaker.tone(1000, 100);
    M5.Lcd.clear();
    M5.Lcd.setCursor(5,10);
    switch(counter++)
    {
      case 0:
        M5.Lcd.println("こんにちは、世界！, ja");
        google_tts("こんにちは、世界！", "ja");
        break;
      case 1:
       google_tts("Hello, World!","en-US");
       M5.Lcd.println("Hello, World!,en-US");
        break;
      case 2:
        M5.Lcd.println("Bonjour tout le monde, le monde!,fr-FR");
        google_tts("Bonjour tout le monde, le monde!","fr-FR");
        break;
      case 3:
        M5.Lcd.println("Hallo Welt, die Welt!,de-DE");
        google_tts("Hallo Welt, die Welt!","de-DE");
        break;
      case 4:
        M5.Lcd.println("Ciao mondo, il mondo!,it-IT");
        google_tts("Ciao mondo, il mondo!","it-IT");
        break;
      case 5:
        M5.Lcd.println("Hola mundo, el mundo!,es-ES");
        google_tts("Hola mundo, el mundo!","es-ES");
        break;
      defalt:
        google_tts("こんにちは、世界！", "ja");
    }
    if(counter > 5) counter = 0;
  }
  if(mp3->isRunning()) {
    while (mp3->loop()){
      if (millis()-lastms > 1000) {
        lastms = millis();
        Serial.printf("Running for %d ms...\n", lastms);
        Serial.flush();
      }
//      delay(1);
    }
    mp3->stop();
    delete file;
    Serial.println("mp3 stop");
  } else {    
//    Serial.printf("MP3 done\n");
    delay(100);
  }
}

// Questo codice gestisce una striscia di NeoPixel tramite un Web Server su ESP32.
// Le animazioni sono state riscritte in modalità NON BLOCCANTE per consentire
// la gestione simultanea delle richieste web e degli input fisici.
// Lo stile CSS è ora incorporato per funzionare anche OFFLINE.

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>

// --- CONFIGURAZIONE WIFI E HOSTNAME ---
// Aggiorna queste variabili con le credenziali della tua rete locale
const char* ssid   = "TUA_RETE_WIFI";
const char* password = "TUA_PASSWORD";

// Hostname desiderato (ad esempio: http://neopixel.local)
const char* hostname = "neopixel-controller";

// --- CONFIGURAZIONE PIN E STRISCIA ---
#define BUTTON_PIN   5  // Pulsante cambio modalità fisica
#define PIXEL_PIN    18  // Pin dati collegato ai NeoPixel
#define PIXEL_COUNT 24  // Numero di NeoPixel (lasciato a 24)

// L'uso degli interrupt per la luminosità è stato mantenuto, ma per il WebServer
// è più pratico aggiornare la luminosità tramite la pagina web.
#define BRIGHTNESS_UP_BUTTON_PIN   16 // Mantenuti per compatibilità hardware
#define BRIGHTNESS_DOWN_BUTTON_PIN   17

// --- VARIABILI GLOBALI DI STATO ---
volatile byte brightness = 100; // Luminosità globale (0-255)
int currentMode = 0;        // Modalità di animazione corrente (0-8 animazioni, 9 per controllo singolo pixel)
int waitTime = 50;        // Ritardo base per le animazioni non bloccanti (ms)

// Variabili per il controllo non bloccante delle animazioni
unsigned long previousMillis = 0;
long rainbowHue = 0;
int theaterChaseStep = 0;
int theaterChaseColorIndex = 0;
int colorWipeIndex = 0;

// Stato del pulsante di cambio modalità fisica (per debounce)
boolean oldState = HIGH;

// Stato per il controllo del singolo pixel (Modalità 9)
int individualPixelIndex = 0;
uint32_t individualPixelColor = 0;

// Dichiarazione dell'oggetto NeoPixel strip:
Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Dichiarazione dell'oggetto Web Server sulla porta 80
WebServer server(80);

// --- FUNZIONI DI CONTROLLO INTERRUPT (PER PULSANTI FISICI) ---
// La luminosità viene gestita via interrupt E via web.
void BR_UP_ISR() {
  if (brightness < 255) brightness += 5; else brightness = 255;
}

void BR_DOWN_ISR() {
  if (brightness > 5) brightness -= 5; else brightness = 5;
}

// --- FUNZIONI DI ANIMAZIONE NON BLOCCANTI ---

// Reinizializza lo stato delle variabili di animazione
void resetAnimationState() {
  previousMillis = 0;
  rainbowHue = 0;
  theaterChaseStep = 0;
  theaterChaseColorIndex = 0;
  colorWipeIndex = 0;
  strip.clear();
  strip.show();
}

// ColorWipe NON BLOCCANTE
void runColorWipe(uint32_t color, int wait) {
  if (colorWipeIndex >= strip.numPixels()) return;
  
  if (millis() - previousMillis >= wait) {
    previousMillis = millis();
    strip.setPixelColor(colorWipeIndex, color);
    strip.show();
    colorWipeIndex++;
  }
}

// TheaterChase NON BLOCCANTE
void runTheaterChase(uint32_t color, int wait) {
  if (millis() - previousMillis >= wait) {
    previousMillis = millis();
    strip.clear(); // Set all pixels in RAM to 0 (off)
    
    for(int i=theaterChaseStep; i<strip.numPixels(); i += 3) {
      strip.setPixelColor(i, color);
    }
    
    strip.show();
    theaterChaseStep = (theaterChaseStep + 1) % 3;
    theaterChaseColorIndex++;
    // Qui l'animazione loopa all'infinito finché la modalità non cambia.
  }
}

// Rainbow NON BLOCCANTE
void runRainbow(int wait) {
  if (millis() - previousMillis >= wait) {
    previousMillis = millis();
    
    for(int i=0; i<strip.numPixels(); i++) {
      int pixelHue = rainbowHue + (i * 65536L / strip.numPixels());
      // Usa la variabile globale 'brightness' (modificabile dagli interrupt o dal web)
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue, 255, brightness)));
    }
    strip.show();
    
    rainbowHue += 256;
    if (rainbowHue >= 65536) rainbowHue = 0; // Un ciclo completo per loop
  }
}

// TheaterChaseRainbow NON BLOCCANTE
void runTheaterChaseRainbow(int wait) {
  if (millis() - previousMillis >= wait) {
    previousMillis = millis();
    
    strip.clear();
    for(int c = theaterChaseStep; c < strip.numPixels(); c += 3) {
      int      hue   = rainbowHue + c * 65536L / strip.numPixels();
      uint32_t color = strip.gamma32(strip.ColorHSV(hue, 255, brightness));
      strip.setPixelColor(c, color);
    }
    strip.show();
    
    theaterChaseStep = (theaterChaseStep + 1) % 3;
    rainbowHue += 65536 / 90; // Incrementa il colore
    if (rainbowHue >= 65536) rainbowHue = 0;
  }
}

// Modalità 9: Controllo Singolo Pixel
void runIndividualPixelMode() {
  // Per assicurare che la luminosità globale sia rispettata per i colori RGB statici:
  uint8_t r = (uint8_t)(individualPixelColor >> 16);
  uint8_t g = (uint8_t)(individualPixelColor >> 8);
  uint8_t b = (uint8_t)(individualPixelColor >> 0);
  
  // Applica luminosità al colore statico
  uint32_t finalColor = strip.Color(r * brightness / 255, g * brightness / 255, b * brightness / 255);

  strip.clear();
  if (individualPixelIndex >= 0 && individualPixelIndex < PIXEL_COUNT) {
    strip.setPixelColor(individualPixelIndex, finalColor);
  }
  strip.show();
}

// --- FUNZIONI DI GESTIONE WEB SERVER ---

// Gestisce la richiesta GET alla root (/) e invia la pagina HTML di controllo
void handleRoot() {
  // HTML con stili CSS incorporati per l'uso OFFLINE (NO CDN)
  String html = F(R"raw(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 NeoPixel Control</title>
  <style>
    /* Stili CSS di base (equivalente a Tailwind per questo layout) */
    body { 
      font-family: sans-serif; 
      background-color: #1f2937; /* Gray-800 */
      color: #f9fafb; /* Gray-50 */
      padding: 1rem; 
    }
    h1 { font-size: 1.875rem; font-weight: 700; margin-bottom: 1.5rem; text-align: center; color: #818cf8; } /* Indigo-400 */
    h2 { font-size: 1.25rem; font-weight: 600; margin-bottom: 0.75rem; }
    .card { 
      background-color: #374151; /* Gray-700 */
      border-radius: 0.5rem; 
      padding: 1.5rem; 
      margin-bottom: 1rem; 
      box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1);
    }
    .status-display { text-align: center; margin-bottom: 1.5rem; }
    .highlight { font-weight: 700; color: #facc15; } /* Yellow-400 */

    /* Input e Bottoni */
    input[type="number"], input[type="color"] {
      padding: 0.5rem;
      background-color: #4b5563; /* Gray-600 */
      border: 1px solid #6b7280; /* Gray-500 */
      border-radius: 0.5rem;
      color: #f9fafb;
      width: 75%;
    }
    .input-group { display: flex; align-items: center; margin-bottom: 0.75rem; gap: 1rem; }
    .input-group label { width: 25%; }

    button { 
      font-weight: 700; 
      padding: 0.75rem 1rem;
      border-radius: 0.5rem; 
      color: white;
      transition: all 0.1s; 
      border: none;
      cursor: pointer;
      width: 100%;
    }
    button:active { transform: scale(0.98); }
    
    /* Layout Griglia per Animazioni */
    .grid-3 { 
      display: grid; 
      grid-template-columns: repeat(2, 1fr); 
      gap: 0.75rem; 
    }
    @media (min-width: 640px) { /* breakpoint sm */
      .grid-3 { grid-template-columns: repeat(3, 1fr); }
    }
    
    /* Colori Specifici dei Bottoni */
    .btn-red { background-color: #dc2626; } /* Red-600 */
    .btn-red:hover { background-color: #b91c1c; }
    .btn-green { background-color: #16a34a; } /* Green-600 */
    .btn-green:hover { background-color: #15803d; }
    .btn-indigo { background-color: #4f46e5; } /* Indigo-600 */
    .btn-indigo:hover { background-color: #4338ca; }
    .btn-gray { background-color: #4b5563; } /* Gray-600 */
    .btn-gray:hover { background-color: #374151; }
    .btn-blue { background-color: #2563eb; } /* Blue-600 */
    .btn-blue:hover { background-color: #1d4ed8; }
    .btn-yellow { background-color: #ca8a04; } /* Yellow-600 */
    .btn-yellow:hover { background-color: #a16207; }
    .btn-purple { background-color: #9333ea; } /* Purple-600 */
    .btn-purple:hover { background-color: #7e22ce; }
    .btn-pink { background-color: #db2777; } /* Pink-600 */
    .btn-pink:hover { background-color: #be185d; }
    .btn-orange { background-color: #f97316; } /* Orange-600 */
    .btn-orange:hover { background-color: #ea580c; }
  </style>
</head>
<body>
  <h1 class="">Controllo NeoPixel ESP32</h1>

  <div id="status" class="card status-display">
    Modalità Attuale: <span id="currentModeDisplay" class="highlight">Spento</span> |
    Luminosità: <span id="brightnessDisplay" class="highlight">%</span>
  </div>

  <!-- CONTROLLO LUMINOSITÀ -->
  <div class="card">
    <h2 class="">Luminosità (Globale)</h2>
    <div style="display: flex; gap: 0.5rem;">
      <button onclick="sendControl('brightness', 'down')" class="btn-red" style="flex-grow: 1;">- 5</button>
      <button onclick="sendControl('brightness', 'up')" class="btn-green" style="flex-grow: 1;">+ 5</button>
    </div>
  </div>

  <!-- CONTROLLO SINGOLO PIXEL (Modalità 9) -->
  <div class="card">
    <h2 class="">Controllo Singolo Pixel (Modo 9)</h2>
    <div class="input-group">
      <label for="pixelIndex">Pixel (0-23):</label>
      <input type="number" id="pixelIndex" min="0" max="23" value="0">
    </div>
    <div class="input-group">
      <label for="pixelColor">Colore:</label>
      <input type="color" id="pixelColor" value="#FFFFFF" style="height: 2.5rem; padding: 0.25rem;">
    </div>
    <button onclick="setIndividualPixel()" class="btn-indigo" style="margin-top: 0.5rem;">Applica (Passa a Mod. 9)</button>
  </div>

  <!-- CONTROLLO ANIMAZIONI -->
  <div class="card">
    <h2 class="">Animazioni Predefinite (Modi 0-8)</h2>
    <div class="grid-3">
      <button onclick="sendControl('mode', '0')" class="btn-gray">Modo 0: OFF</button>
      <button onclick="sendControl('mode', '1')" class="btn-red">Modo 1: Wipe Rosso</button>
      <button onclick="sendControl('mode', '2')" class="btn-green">Modo 2: Wipe Verde</button>
      <button onclick="sendControl('mode', '3')" class="btn-blue">Modo 3: Wipe Blu</button>
      <button onclick="sendControl('mode', '4')" class="btn-yellow">Modo 4: Chase Bianco</button>
      <button onclick="sendControl('mode', '5')" class="btn-purple">Modo 5: Chase Rosso</button>
      <button onclick="sendControl('mode', '6')" class="btn-blue">Modo 6: Chase Blu</button>
      <button onclick="sendControl('mode', '7')" class="btn-pink">Modo 7: Rainbow</button>
      <button onclick="sendControl('mode', '8')" class="btn-orange">Modo 8: Chase R-bow</button>
    </div>
  </div>

  <script>
    // Mappa i nomi delle modalità
    const modeNames = [
      "Spento (Wipe)", "Wipe Rosso", "Wipe Verde", "Wipe Blu",
      "Chase Bianco", "Chase Rosso", "Chase Blu", "Rainbow",
      "Chase Arcobaleno", "Pixel Singolo"
    ];
    
    // La funzione fetchStatus è stata aggiornata per utilizzare la percentuale
    async function fetchStatus() {
      try {
        const response = await fetch('/status');
        const data = await response.json();
        document.getElementById('currentModeDisplay').textContent = modeNames[data.mode];
        // La luminosità viene ora mostrata in percentuale (0-100)
        document.getElementById('brightnessDisplay').textContent = data.brightness + '%';
      } catch (e) {
        console.error("Errore nel recupero dello stato:", e);
      }
    }

    async function sendControl(type, value) {
      const url = `/control?type=${type}&value=${value}`;
      try {
        await fetch(url);
        await fetchStatus(); // Aggiorna lo stato dopo il comando
      } catch (e) {
        console.error("Errore nell'invio del controllo:", e);
      }
    }

    function setIndividualPixel() {
      const index = document.getElementById('pixelIndex').value;
      const colorHex = document.getElementById('pixelColor').value;
      // Invia indice e colore HEX (senza il '#')
      sendControl('individual', `${index},${colorHex.substring(1)}`);
    }

    // Aggiorna lo stato ogni 1.5 secondi
    setInterval(fetchStatus, 1500);
    fetchStatus();
  </script>
</body>
</html>
)raw");
  server.send(200, "text/html", html);
}

// API endpoint per ricevere i comandi dal web
void handleControl() {
  if (server.hasArg("type") && server.hasArg("value")) {
    String type = server.arg("type");
    String value = server.arg("value");

    if (type == "mode") {
      int newMode = value.toInt();
      if (newMode >= 0 && newMode <= 8) {
        currentMode = newMode;
        resetAnimationState(); // Riavvia l'animazione con i nuovi parametri
      }
    } else if (type == "brightness") {
      // Luminosità: gestita in modo non bloccante
      if (value == "up") {
        BR_UP_ISR(); // Chiama la stessa logica dell'interrupt
      } else if (value == "down") {
        BR_DOWN_ISR(); // Chiama la stessa logica dell'interrupt
      }
    } else if (type == "individual") {
      // Formato: "index,RRGGBB"
      int commaIndex = value.indexOf(',');
      if (commaIndex != -1) {
        individualPixelIndex = value.substring(0, commaIndex).toInt();
        String hexColor = value.substring(commaIndex + 1);
        
        unsigned long colorVal = strtoul(hexColor.c_str(), NULL, 16);
        uint8_t r = (uint8_t)(colorVal >> 16);
        uint8_t g = (uint8_t)(colorVal >> 8);
        uint8_t b = (uint8_t)(colorVal >> 0);
        
        individualPixelColor = strip.Color(r, g, b);
        currentMode = 9; // Passa alla modalità 9 (Singolo Pixel)
        resetAnimationState();
      }
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

// API endpoint per inviare lo stato attuale al web (usato per l'aggiornamento UI)
void handleStatus() {
  // Invia la luminosità come percentuale (0-100) per una migliore visualizzazione web
  int brightnessPercent = (int)(brightness * 100.0 / 255.0);
  String json = "{\"mode\":" + String(currentMode) + ",\"brightness\":" + String(brightnessPercent) + "}";
  server.send(200, "application/json", json);
}

// --- FUNZIONE PRINCIPALE DI LOOP ---

// Esegue un singolo passo dell'animazione corrente
void runCurrentAnimation() {
  // Colori statici calcolati in base alla luminosità corrente
  uint32_t colorRed   = strip.Color(brightness,   0,   0);
  uint32_t colorGreen = strip.Color(  0, brightness,   0);
  uint32_t colorBlue  = strip.Color(  0,   0, brightness);
  uint32_t colorWhite = strip.Color(brightness, brightness, brightness);
  uint32_t colorOff   = strip.Color(  0,   0,   0);

  switch(currentMode) {
    case 0: runColorWipe(colorOff, waitTime); break;
    case 1: runColorWipe(colorRed, waitTime); break;
    case 2: runColorWipe(colorGreen, waitTime); break;
    case 3: runColorWipe(colorBlue, waitTime); break;
    case 4: runTheaterChase(colorWhite, waitTime); break;
    case 5: runTheaterChase(colorRed, waitTime); break;
    case 6: runTheaterChase(colorBlue, waitTime); break;
    case 7: runRainbow(10); break;
    case 8: runTheaterChaseRainbow(waitTime); break;
    case 9: runIndividualPixelMode(); break; // Modalità Singolo Pixel
  }
}


void setup() {
  // Inizializzazione Seriale (utile per debug)
  Serial.begin(115200);

  // Inizializzazione Pin per pulsanti fisici
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BRIGHTNESS_UP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BRIGHTNESS_DOWN_BUTTON_PIN, INPUT_PULLUP);

  // Inizializzazione NeoPixel
  strip.begin();
  strip.show();

  // Collega gli interrupt per i pulsanti di luminosità
  attachInterrupt(digitalPinToInterrupt(BRIGHTNESS_UP_BUTTON_PIN), BR_UP_ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BRIGHTNESS_DOWN_BUTTON_PIN), BR_DOWN_ISR, FALLING);

  // --- CONNESSIONE WIFI ---
  WiFi.setHostname(hostname);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi Connected.");
  Serial.print("Access at: http://");
  Serial.println(hostname);
  Serial.print("Or IP: ");
  Serial.println(WiFi.localIP());

  // --- CONFIGURAZIONE WEB SERVER ---
  server.on("/", HTTP_GET, handleRoot);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();
}


void loop() {
  // 1. Gestione del Server Web (NON BLOCCANTE)
  server.handleClient();

  // 2. Esecuzione dell'Animazione (NON BLOCCANTE)
  runCurrentAnimation();

  // 3. Gestione del Pulsante di Cambio Modalità Fisica
  boolean newState = digitalRead(BUTTON_PIN);

  if((newState == LOW) && (oldState == HIGH)) {
    delay(20); // Debounce
    newState = digitalRead(BUTTON_PIN);

    if(newState == LOW) {      // Pulsante premuto in modo valido
      if(++currentMode > 9) currentMode = 0; // Include ora la modalità 9
      resetAnimationState(); // Resetta lo stato di tutte le animazioni
    }
  }
  oldState = newState;
}

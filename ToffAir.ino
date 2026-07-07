/*****************************************************
 *               ToffAir v3.0.0
 *      Air Quality Monitoring Station
 *        ESP32 + GC9A01 + MQ135 + MQ131
 *
 *      Desarrollado por: Juan Portillo
 *****************************************************/

#include <SPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include "AdafruitIO_WiFi.h"
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

//======================================================
// PINES
//======================================================

#define TFT_CS     5
#define TFT_DC     2
#define TFT_RST    4

#define TFT_SCK    18
#define TFT_MOSI   23

#define MQ135_PIN  34
#define MQ131_CS    15
#define MQ131_RDY   19

#define BUZZER     25

//======================================================
// COLORES
//======================================================

#define BG_COLOR      GC9A01A_BLACK
#define TITLE_COLOR   GC9A01A_CYAN
#define TEXT_COLOR    GC9A01A_WHITE
#define GOOD_COLOR    GC9A01A_GREEN
#define WARN_COLOR    GC9A01A_YELLOW
#define BAD_COLOR     GC9A01A_RED
#define BAR_COLOR     GC9A01A_BLUE

//======================================================
// TFT
//======================================================

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

//======================================================
// VARIABLES GLOBALES
//======================================================

float mq135 = 0;
float mq131 = 0;

//=========================
// WIFI
//=========================

bool ultimoEstadoWiFi = false;
unsigned long tiempoReconexion = 0;

const char* ssid = "NAME_OF_WIFI";
const char* password = "YOUR_PASSWORD";

bool wifiConectado = false;

//=====================================================
// ADAFRUIT IO
//=====================================================

#define IO_USERNAME "YOUR_ID"
#define IO_KEY      "YOUR_IO_KEY"

AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, ssid, password);

AdafruitIO_Feed *co2Feed    = io.feed("co2");
AdafruitIO_Feed *ozonoFeed  = io.feed("ozono");
AdafruitIO_Feed *aqiFeed    = io.feed("aqi");

unsigned long tiempoAdafruit = 0;

//==============================
// MQ135
//==============================

int mq135RAW = 0;

float mq135Voltaje = 0;

float mq135PPM = 0;

// Tiempo de actualización
unsigned long tiempoMQ135 = 0;

int AQI = 0;

String estado = "GOOD";

bool wifi = false;

//==============================
// MCP3551 - MQ131
//==============================

long mq131RAW = 0;
float mq131Voltaje = 0.0;
float mq131PPB = 0.0;

float mq131Ratio = 0.0;

long mq131Filtro = 0;

const byte NUM_MUESTRAS = 8;
long muestrasMQ131[NUM_MUESTRAS];
byte indiceMQ131 = 0;

unsigned long tiempoMQ131 = 0;

float mq131Rs = 0.0;

Preferences memoria;

float mq131R0 = 0.0;

bool calibrado = false;

unsigned long tiempoCalibracion = 0;

const unsigned long DURACION_CAL = 60000;   // 60 segundos

float sumaR0 = 0;

long contadorR0 = 0;

//======================================================
// FUNCIONES
//======================================================

void splashScreen();
void dashboard();
void drawProgressBar(int percent);

//======================================================
// SETUP
//======================================================

void setup()
{
    Serial.begin(115200);

    Serial.begin(115200);

    conectarWiFi();

    conectarAdafruit();

    pinMode(BUZZER, OUTPUT);

    // PWM para buzzer
    ledcAttach(BUZZER, 2700, 8);

    SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);

    tft.begin();

    tft.setRotation(0);

    splashScreen();

    pantallaDiagnostico();

    diagnosticoLinea("Display",true);

    diagnosticoLinea("MQ135",true);

    diagnosticoLinea("MCP3551",true);

    diagnosticoLinea("MQ131",true);

    diagnosticoLinea("Calibration",true);

    delay(800);

    dashboard();

    pinMode(MQ131_RDY, INPUT);

    digitalWrite(MQ131_CS, HIGH);
    pinMode(MQ131_CS, OUTPUT);

    memoria.begin("mq131", false);

    mq131R0 = memoria.getFloat("R0", 0);

    if(mq131R0 > 0)
    {
        calibrado = true;
    }
    else
    {
        tiempoCalibracion = millis();
    }

}

void beep(int tiempo)
{
    ledcWrite(BUZZER, 128);   // 50% Duty Cycle
    delay(tiempo);
    ledcWrite(BUZZER, 0);
}

//======================================================
// LOOP
//======================================================

void loop()
{
    wifiConectado = (WiFi.status() == WL_CONNECTED);

    bool estadoActual = (WiFi.status() == WL_CONNECTED);

    if (estadoActual != ultimoEstadoWiFi)
    {
        ultimoEstadoWiFi = estadoActual;
        actualizarWiFi();
    }

    if (wifiConectado)
    {
        io.run();
    }

    // Intentar reconectar cada 10 s
    if (!wifiConectado)
    {
        if (millis() - tiempoReconexion >= 10000)
        {
            tiempoReconexion = millis();
            reconectarWiFi();
        }
    }

    // Sensores cada 500 ms
    if (millis() - tiempoMQ135 >= 500)
    {
        tiempoMQ135 = millis();

        leerMQ135();
        leerMQ131();

        calibrarMQ131();

        actualizarMQ131();

        calcularEstado();

        actualizarMQ135();

        actualizarEstado();

        actualizarBarraAQI();

        actualizarBuzzer();
    }

    // Envío a Adafruit cada 30 s
    if (wifiConectado)
    {
        if (millis() - tiempoAdafruit >= 30000)
        {
            tiempoAdafruit = millis();

            co2Feed->save(mq135PPM);
            ozonoFeed->save(mq131PPB);
            aqiFeed->save(AQI);

            Serial.println("Datos enviados a Adafruit IO");
        }
    }
}

void conectarWiFi()
{
    Serial.println();
    Serial.println("=================================");
    Serial.println(" TOFFAIR ");
    Serial.println("=================================");
    Serial.println("Conectando al WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int intentos = 0;

    while (WiFi.status() != WL_CONNECTED && intentos < 20)
    {
        delay(500);
        Serial.print(".");
        intentos++;
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        wifiConectado = true;

        Serial.println("WiFi conectado");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        wifiConectado = false;

        Serial.print("Error WiFi. Codigo: ");
        Serial.println(WiFi.status());
    }

    Serial.println("=================================");
}

void actualizarWiFi()
{
    // Borra únicamente la parte donde dice Online/Offline
    tft.fillRect(110, 220, 55, 8, GC9A01A_BLACK);

    tft.setTextSize(1);
    tft.setCursor(81,220);

    tft.setTextColor(GC9A01A_WHITE);
    tft.print("WiFi:");

    if(WiFi.status() == WL_CONNECTED)
    {
        tft.setTextColor(GC9A01A_GREEN);
        tft.print("Online ");
    }
    else
    {
        tft.setTextColor(BAD_COLOR);
        tft.print("Offline");
    }
}

void conectarAdafruit()
{
    Serial.println();
    Serial.println("Conectando a Adafruit IO...");

    io.connect();

    while(io.status() < AIO_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }

    Serial.println();
    Serial.println("Adafruit IO conectado");
}

void reconectarWiFi()
{
    if(WiFi.status() == WL_CONNECTED)
    {
        wifiConectado = true;
        return;
    }

    wifiConectado = false;

    Serial.println("Intentando reconectar WiFi...");

    WiFi.disconnect();
    WiFi.begin(ssid, password);
}

void splashScreen()
{
    tft.fillScreen(BG_COLOR);

    // Borde
    tft.drawCircle(120,120,118,GC9A01A_CYAN);
    tft.drawCircle(120,120,116,GC9A01A_WHITE);

    // Título
    tft.setTextColor(TITLE_COLOR);
    tft.setTextSize(2);
    tft.setCursor(80,35);
    tft.print("TOFFAIR");

    // Subtítulo
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(48,60);
    tft.print("Air Quality Monitor");

    // Línea decorativa
    tft.drawFastHLine(35,80,170,GC9A01A_DARKCYAN);

    // Autor
    tft.setTextSize(1);
    tft.setCursor(68,100);
    tft.print("Designed by");

    tft.setTextSize(2);
    tft.setTextColor(GOOD_COLOR);
    tft.setCursor(48,118);
    tft.print("Juan Portillo");

    // Loading
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(82,160);
    tft.print("Loading...");

    for(int i=0;i<=100;i++)
    {
        drawProgressBar(i);
        delay(12);
    }

    delay(1000);
}

void drawProgressBar(int percent)
{
    tft.drawRoundRect(30,180,180,18,8,GC9A01A_WHITE);

    int width = map(percent,0,100,0,176);

    tft.fillRoundRect(32,182,width,14,6,GC9A01A_CYAN);

    tft.fillRect(90,205,60,12,BG_COLOR);

    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);

    tft.setCursor(103,205);
    tft.print(percent);
    tft.print("%");
}

void dashboard()
{
    tft.fillScreen(BG_COLOR);

    // Marco
    tft.drawCircle(120,120,118,GC9A01A_WHITE);

    //======================
    // TITULO
    //======================

    tft.setTextColor(TITLE_COLOR);
    tft.setTextSize(2);

    tft.setCursor(80,12);
    tft.print("TOFFAIR");

    tft.setTextSize(1);

    tft.setCursor(63,30);
    tft.print("Air Quality Monitor");

    tft.drawFastHLine(25,45,190,GC9A01A_DARKCYAN);

    //======================
    // ESTADO
    //======================

    tft.setTextColor(GOOD_COLOR);

    tft.setTextSize(3);

    tft.setCursor(82,55);
    tft.print("GOOD");

    tft.setTextSize(1);

    tft.setTextColor(TEXT_COLOR);

    tft.setCursor(110,88);
    tft.print("AIR");

    //======================
    // CO2
    //======================

    tft.setTextSize(2);

    tft.setCursor(40,105);
    tft.print("CO2");

    tft.setCursor(40,128);
    tft.print("---");

    tft.setTextSize(1);

    tft.print(" ppm");

    //======================
    // O3
    //======================

    tft.setTextColor(GC9A01A_CYAN);
    tft.setTextSize(2);

    tft.setCursor(155,105);
    tft.print("O3");

    tft.setCursor(155,128);
    tft.print("---");

    tft.setTextSize(1);

    tft.print(" ppb");

    //======================
    // AQI
    //======================

    tft.setTextSize(2);

    tft.setCursor(23,165);
    tft.print("AQI");

    tft.setCursor(75,165);
    tft.print("--");

    //======================
    // Barra
    //======================

    tft.drawRoundRect(20,190,200,18,8,GC9A01A_WHITE);

    tft.fillRoundRect(22,192,45,14,6,GOOD_COLOR);

    //======================
    // WiFi
    //======================

    Serial.print("WiFi.status() = ");
    Serial.println(WiFi.status());

    actualizarWiFi();
}

void pantallaDiagnostico()
{
    tft.fillScreen(BG_COLOR);

    tft.setTextColor(GC9A01A_CYAN);
    tft.setTextSize(2);
    tft.setCursor(38,20);
    tft.println("AIRGUARDIAN");

    tft.setTextColor(GC9A01A_WHITE);
    tft.setTextSize(1);
    tft.setCursor(55,45);
    tft.println("Initializing...");
}

void diagnosticoLinea(String texto,bool ok)
{
    static int y = 70;

    tft.setCursor(25,y);

    if(ok)
    {
        tft.setTextColor(GC9A01A_GREEN);
        tft.print("[OK] ");
    }
    else
    {
        tft.setTextColor(GC9A01A_RED);
        tft.print("[FAIL] ");
    }

    tft.setTextColor(GC9A01A_WHITE);
    tft.println(texto);

    y += 20;

    delay(350);
}

void leerMQ135()
{

    mq135RAW = analogRead(MQ135_PIN);

    mq135Voltaje = (mq135RAW * 3.3) / 4095.0;

    // Conversión provisional
    mq135PPM = map(mq135RAW,0,4095,400,2000);

    AQI = map(mq135PPM,400,2000,0,300);
    AQI = constrain(AQI,0,300);

    if(AQI < 50)
        estado = "GOOD";
    else if(AQI < 100)
        estado = "NORMAL";
    else if(AQI < 180)
        estado = "BAD";
    else
        estado = "DANGER";

    Serial.print("RAW: ");
    Serial.print(mq135RAW);

    Serial.print("   Voltaje: ");
    Serial.print(mq135Voltaje,2);

    Serial.print(" V");

    Serial.print("   PPM: ");

    Serial.println(mq135PPM);

}

void calcularEstado()
{
    if (mq135PPM <= 600)
    {
        estado = "EXCELLENT";
        AQI = map(mq135PPM, 400, 600, 0, 25);
    }
    else if (mq135PPM <= 900)
    {
        estado = "GOOD";
        AQI = map(mq135PPM, 601, 900, 26, 50);
    }
    else if (mq135PPM <= 1200)
    {
        estado = "MODERATE";
        AQI = map(mq135PPM, 901, 1200, 51, 100);
    }
    else if (mq135PPM <= 1800)
    {
        estado = "POOR";
        AQI = map(mq135PPM, 1201, 1800, 101, 150);
    }
    else
    {
        estado = "DANGER";
        AQI = map(mq135PPM, 1801, 3000, 151, 250);
    }

    AQI = constrain(AQI, 0, 250);
}

void actualizarEstado()
{
    tft.fillRect(20,50,200,35,BG_COLOR);

    uint16_t color;

    if (estado == "EXCELLENT")
        color = GC9A01A_DARKGREEN;

    else if (estado == "GOOD")
        color = GC9A01A_GREEN;

    else if (estado == "MODERATE")
        color = GC9A01A_YELLOW;

    else if (estado == "POOR")
        color = GC9A01A_ORANGE;

    else
        color = GC9A01A_RED;

    tft.setTextColor(color);
    tft.setTextSize(3);

    // Centrado horizontal aproximado
    int anchoTexto = estado.length() * 18;
    int x = (240 - anchoTexto) / 2;

    tft.setCursor(x,55);
    tft.print(estado);
}

void actualizarMQ135()
{

    // Borra solamente donde está el número

    tft.fillRect(10,122,90,20,BG_COLOR);

    tft.setCursor(18,128);

    tft.setTextColor(GOOD_COLOR);

    tft.setTextSize(2);

    tft.print((int)mq135PPM);

    tft.setTextSize(1);

    tft.print(" ppm");

    //==============================
    // AQI
    //==============================

    tft.fillRect(60,160,50,20,BG_COLOR);

    tft.setCursor(75,165);

    tft.setTextColor(GC9A01A_WHITE);

    tft.setTextSize(2);

    tft.print(AQI);

}

void actualizarBarraAQI()
{
    uint16_t color;

    if (estado == "EXCELLENT")
        color = GC9A01A_DARKGREEN;

    else if (estado == "GOOD")
        color = GC9A01A_GREEN;

    else if (estado == "MODERATE")
        color = GC9A01A_YELLOW;

    else if (estado == "POOR")
        color = GC9A01A_ORANGE;

    else
        color = GC9A01A_RED;

    int ancho = map(AQI,0,250,0,196);

    tft.fillRect(22,192,196,14,BG_COLOR);

    tft.fillRoundRect(22,192,ancho,14,6,color);
}

void actualizarBuzzer()
{
    static String ultimoEstado = "";

    if (estado == ultimoEstado)
        return;

    ultimoEstado = estado;

    if (estado == "EXCELLENT")
    {
        // Sin sonido
    }
    else if (estado == "GOOD")
    {
        // Sin sonido
    }
    else if (estado == "MODERATE")
    {
        beep(100);
    }
    else if (estado == "POOR")
    {
        beep(100);
        delay(120);
        beep(100);
    }
    else if (estado == "DANGER")
    {
        beep(500);
    }
}

    //==============================
    // MCP3551 - MQ131
    //==============================

long leerMCP3551()
{
    uint32_t dato = 0;

    digitalWrite(MQ131_CS, LOW);

    delayMicroseconds(5);

    dato |= ((uint32_t)SPI.transfer(0x00)) << 16;
    dato |= ((uint32_t)SPI.transfer(0x00)) << 8;
    dato |= ((uint32_t)SPI.transfer(0x00));

    digitalWrite(MQ131_CS, HIGH);

    dato &= 0xFFFFFF;

    return (long)dato;
}

void leerMQ131()
{
    uint32_t dato = leerMCP3551();

    bool OVL = (dato >> 23) & 1;
    bool OVH = (dato >> 22) & 1;

    // Si hay overflow, ignoramos esta muestra
    if (OVL || OVH)
    {
        return;
    }

    int32_t codigo = dato & 0x3FFFFF;

    // Extensión de signo (22 bits)
    if (codigo & 0x200000)
        codigo |= 0xFFC00000;

    mq131RAW = codigo;

    // Guardar la nueva lectura
    muestrasMQ131[indiceMQ131] = mq131RAW;

    // Pasar a la siguiente posición
    indiceMQ131++;

    if(indiceMQ131 >= NUM_MUESTRAS)
        indiceMQ131 = 0;

    // Calcular el promedio
    long suma = 0;

    for(int i = 0; i < NUM_MUESTRAS; i++)
    {
        suma += muestrasMQ131[i];
    }

    mq131Filtro = suma / NUM_MUESTRAS;

    // Conversión a voltaje
    mq131Voltaje = (mq131Filtro * 5.0) / 4194303.0;

    const float Vc = 5.0;
    const float RL = 10000.0;   // 10 kΩ

    mq131Rs = RL * ((Vc - mq131Voltaje) / mq131Voltaje);

    if(calibrado)
    {
        mq131Ratio = mq131Rs / mq131R0;

        mq131PPB = calcularO3(mq131Ratio);
    }

    // Mostrar resultados
    Serial.print("Voltaje: ");
    Serial.print(mq131Voltaje,6);

    Serial.print(" V");

    Serial.print("   Rs: ");
    Serial.print(mq131Rs,0);

    Serial.print(" Ω");

    Serial.print("   Ratio: ");
    Serial.print(mq131Ratio,4);

    Serial.print("   O3: ");
    Serial.print(mq131PPB,1);

    Serial.println(" ppb");
}

void calibrarMQ131()
{
    if(calibrado)
        return;

    sumaR0 += mq131Rs;

    contadorR0++;

    if(millis()-tiempoCalibracion >= DURACION_CAL)
    {
        mq131R0 = sumaR0 / contadorR0;

        memoria.putFloat("R0", mq131R0);

        calibrado = true;

        Serial.println();
        Serial.println("**********************");
        Serial.println("CALIBRACION TERMINADA");
        Serial.print("R0 = ");
        Serial.println(mq131R0);
        Serial.println("**********************");
    }
}

float calcularO3(float ratio)
{
    if(ratio <= 0)
        return 0;

    // Aproximación inicial de la curva logarítmica
    float m = -0.85;
    float b = 0.78;

    float logPPB = (log10(ratio) - b) / m;

    float ppb = pow(10, logPPB);

    if(ppb < 0)
        ppb = 0;

    if(ppb > 1000)
        ppb = 1000;

    return ppb;
}

void actualizarMQ131()
{
    // Borra únicamente el área del valor
    tft.fillRect(145,122,75,20,BG_COLOR);

    tft.setCursor(155,128);

    tft.setTextColor(GC9A01A_CYAN);

    tft.setTextSize(2);

    tft.print((int)mq131PPB);

    tft.setTextSize(1);

    tft.print(" ppb");
}




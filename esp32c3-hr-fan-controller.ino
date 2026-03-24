#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// --- НАСТРОЙКИ ЭКРАНА ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- НАСТРОЙКИ ПИНОВ ---
constexpr int RELAY_PINS[] = {4, 5, 6, 7};
constexpr int NUM_RELAYS = 4;
constexpr int THRESHOLDS[] = {100, 120, 140, 160};
constexpr int HYSTERESIS = 5;

const int ON = LOW;
const int OFF = HIGH;

// --- BLE UUID ---
static BLEUUID HR_SERVICE_UUID((uint16_t)0x180D);
static BLEUUID HR_CHAR_UUID((uint16_t)0x2A37);

// Состояния
BLEAdvertisedDevice* foundHeartSensor = nullptr;
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pHRCharacteristic = nullptr;
BLEServer* pServer = nullptr;
BLEService* pServerService = nullptr;
BLECharacteristic* pServerCharacteristic = nullptr;

bool doConnect = false;       
bool sensorConnected = false;   
int currentActiveRelay = -1;
int lastBPM = 0;
String sensorName = "";
volatile bool isScanning = false;

// Иконки
const unsigned char epd_bitmap_fan [] PROGMEM = {
    0x00, 0x07, 0x80, 0x00, 0x18, 0x40, 0x00, 0x20, 0x20, 0x00, 0x20, 0x20, 
    0x00, 0x20, 0x20, 0x3E, 0x60, 0x20, 0x42, 0x60, 0x60, 0x80, 0x60, 0x00, 
    0x80, 0x60, 0x00, 0x80, 0x7F, 0xE0, 0x80, 0x43, 0xFC, 0x40, 0x42, 0x02, 
    0x40, 0x42, 0x02, 0x3F, 0xC2, 0x01, 0x07, 0xFE, 0x01, 0x00, 0x06, 0x01, 
    0x00, 0x06, 0x01, 0x00, 0x06, 0x42, 0x04, 0x06, 0x7C, 0x04, 0x04, 0x00, 
    0x04, 0x04, 0x00, 0x04, 0x04, 0x00, 0x02, 0x18, 0x00, 0x01, 0xE0, 0x00
};

const unsigned char epd_bitmap_heart [] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xC3, 0xF8, 0x30, 0x66, 0x0C, 
    0x40, 0x3C, 0x02, 0x40, 0x18, 0x02, 0x80, 0x00, 0x01, 0x80, 0x00, 0x01, 
    0x80, 0x00, 0x01, 0x00, 0x00, 0x02, 0x40, 0x00, 0x02, 0x20, 0x00, 0x04, 
    0x30, 0x00, 0x0C, 0x18, 0x00, 0x18, 0x0C, 0x00, 0x30, 0x06, 0x00, 0x60, 
    0x03, 0x00, 0xC0, 0x01, 0x81, 0x80, 0x00, 0x81, 0x00, 0x00, 0x42, 0x00, 
    0x00, 0x24, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// --- ФУНКЦИИ ---
void updateDisplay(int bpm, const char* name) {
    Serial.printf("%s\tbpm: %d\n", name, bpm);

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(name);

    int yOffset = 8;
    display.drawBitmap(0, yOffset, epd_bitmap_fan, 24, 24, SSD1306_WHITE);
    
    display.setTextSize(3);
    display.setCursor(28, yOffset + 2);
    if (currentActiveRelay == -1) {
        display.print("-");
    } else {
        display.print(currentActiveRelay + 1);
    }

    int heartX = 60;
    display.drawBitmap(heartX, yOffset, epd_bitmap_heart, 24, 24, SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(heartX + 28, yOffset + 5); 
    display.print(bpm);

    display.display();
}

void handleRelays(int bpm) {
    int targetRelay = -1;
    for (int i = NUM_RELAYS - 1; i >= 0; i--) {
        int effectiveThreshold = THRESHOLDS[i];
        if (i == currentActiveRelay) effectiveThreshold -= HYSTERESIS;
        if (bpm >= effectiveThreshold) {
            targetRelay = i;
            break;
        }
    }

    if (targetRelay != currentActiveRelay) {
        for (int i = 0; i < NUM_RELAYS; i++) digitalWrite(RELAY_PINS[i], OFF);
        if (targetRelay != -1) digitalWrite(RELAY_PINS[targetRelay], ON);
        currentActiveRelay = targetRelay;
    }
}

// --- CALLBACKS ---
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) override {
        Serial.println("Connected to Heart Rate Sensor");
    }

    void onDisconnect(BLEClient* pClient) override {
        sensorConnected = false;
        sensorName = "";  
        handleRelays(0);
        Serial.println("Sensor disconnected");
    }
};

class MyServerCallback : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        Serial.println("Phone connected!");
    }
    
    void onDisconnect(BLEServer* pServer) override {
        Serial.println("Phone disconnected");
        // Перезапускаем рекламу, чтобы телефон мог снова подключиться
        BLEDevice::startAdvertising();
    }
};

void notifyCallback(BLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    if (length >= 2) {
        uint8_t flags = pData[0];
        uint8_t bpmValue = (flags & 0x01) ? pData[2] : pData[1];
        
        if (bpmValue > 0 && bpmValue < 200) {
            lastBPM = bpmValue;
            handleRelays(lastBPM);
            
            if (sensorName.length() > 0) {
                updateDisplay(lastBPM, sensorName.c_str());
            } else {
                updateDisplay(lastBPM, "Connected");
            }
            
            if (pServerCharacteristic && sensorConnected) {
                uint8_t hrmData[2] = {0x00, bpmValue};
                pServerCharacteristic->setValue(hrmData, 2);
                pServerCharacteristic->notify(true);
            }
        }
    }
}

class MyScanCallback : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(HR_SERVICE_UUID)) {
            Serial.printf(">>> Heart Rate Sensor Found: %s (%s)\n", 
                advertisedDevice.haveName() ? advertisedDevice.getName().c_str() : "Unknown",
                advertisedDevice.getAddress().toString().c_str());
            
            sensorName = advertisedDevice.haveName() ? advertisedDevice.getName().c_str() : "HR Sensor";
            
            BLEDevice::getScan()->stop();
            isScanning = false;
            
            if(foundHeartSensor) delete foundHeartSensor;
            foundHeartSensor = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
        }
    }
    
    void onScanEnd(BLEScan* pScan) {
        isScanning = false;
    }
};

// --- ПОДКЛЮЧЕНИЕ К ПУЛЬСОМЕТРУ ---
bool connectToHeartSensor() {
    isScanning = false;
    
    if (!foundHeartSensor) return false;
    
    Serial.printf("Connecting to: %s\n", foundHeartSensor->getAddress().toString().c_str());

    // Если клиент уже существует, удаляем его старым надежным способом
    if (pClient != nullptr) {
        if (pClient->isConnected()) {
            pClient->disconnect();
            delay(100); // Даем стеку время закрыть сессию
        }
        delete pClient; 
        pClient = nullptr;
    }

    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    // Попытка подключения
    if (!pClient->connect(foundHeartSensor)) {
        // Оставляем pClient как есть, он будет удален при следующей попытке вызова этой функции
        return false;
    }
    
    Serial.println("Connected!");

    BLERemoteService* pService = pClient->getService(HR_SERVICE_UUID);
    if (!pService) {
        pClient->disconnect();
        return false;
    }
    
    pHRCharacteristic = pService->getCharacteristic(HR_CHAR_UUID);
    if (!pHRCharacteristic) {
        pClient->disconnect();
        return false;
    }
    
    if (pHRCharacteristic->canNotify()) {
        pHRCharacteristic->registerForNotify(notifyCallback);
        sensorConnected = true;
        return true;
    }
    
    pClient->disconnect();
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(2000); // Даем время на открытие монитора порта
    
    Serial.println("\n=== Smart Fun HR Bridge ===");
    
    // Инициализация реле
    for (int i = 0; i < NUM_RELAYS; i++) {        
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], OFF);
    }

    // Инициализация OLED
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("OLED failed!");
        for(;;); 
    }
    display.setRotation(2);
    display.ssd1306_command(0x81);
    display.ssd1306_command(255);

    // Инициализация BLE
    BLEDevice::init("SmartFun HR");
    
    // Создаём BLE сервер для телефона
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallback());
    pServerService = pServer->createService(HR_SERVICE_UUID);
    pServerCharacteristic = pServerService->createCharacteristic(
        HR_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pServerService->start();
    
    BLEAdvertising* pServerAdvertising = BLEDevice::getAdvertising();
    pServerAdvertising->addServiceUUID(HR_SERVICE_UUID);
    pServerAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();

    // Настраиваем сканер
    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyScanCallback());
    pScan->setInterval(1349);
    pScan->setWindow(449);
    pScan->setActiveScan(true);
    
    updateDisplay(0, "Scanning...");
}

void loop() {
    // Обработка подключения
    if (doConnect) {
        doConnect = false;
        updateDisplay(lastBPM, "Connecting...");

        if (!connectToHeartSensor()) {
            if (foundHeartSensor) {
                delete foundHeartSensor;
                foundHeartSensor = nullptr;
            }
            sensorName = "";
            
            // Очищаем кэш сканера, так как старые данные могут быть невалидны
            BLEDevice::getScan()->clearResults();
            
            isScanning = true;
            BLEDevice::getScan()->start(5, false); 
            isScanning = false;
            updateDisplay(0, "Scanning...");
        }
    }

    // Если связь потеряна - запускаем сканирование с паузами
    if (!sensorConnected && !doConnect && !isScanning) {
        static unsigned long lastScanAttempt = 0;
        if (millis() - lastScanAttempt > 5000) {
            isScanning = true;
            
            BLEDevice::getScan()->clearResults();
            BLEDevice::getScan()->start(5, false);
            
            isScanning = false;
            updateDisplay(0, "Scanning...");
            lastScanAttempt = millis();
        }
    }

    delay(200);
}
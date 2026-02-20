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

// --- НАСТРОЙКИ ПИНОВ ESP32-C3 Zero ---
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

// Иконка вентилятора (24x24 пикселя)
const unsigned char epd_bitmap_fan [] PROGMEM = {
    0x00, 0x07, 0x80, 0x00, 0x18, 0x40, 0x00, 0x20, 0x20, 0x00, 0x20, 0x20, 
    0x00, 0x20, 0x20, 0x3E, 0x60, 0x20, 0x42, 0x60, 0x60, 0x80, 0x60, 0x00, 
    0x80, 0x60, 0x00, 0x80, 0x7F, 0xE0, 0x80, 0x43, 0xFC, 0x40, 0x42, 0x02, 
    0x40, 0x42, 0x02, 0x3F, 0xC2, 0x01, 0x07, 0xFE, 0x01, 0x00, 0x06, 0x01, 
    0x00, 0x06, 0x01, 0x00, 0x06, 0x42, 0x04, 0x06, 0x7C, 0x04, 0x04, 0x00, 
    0x04, 0x04, 0x00, 0x04, 0x04, 0x00, 0x02, 0x18, 0x00, 0x01, 0xE0, 0x00
};

// Иконка сердца (24x24 пикселя)
const unsigned char epd_bitmap_heart [] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xC3, 0xF8, 0x30, 0x66, 0x0C, 
    0x40, 0x3C, 0x02, 0x40, 0x18, 0x02, 0x80, 0x00, 0x01, 0x80, 0x00, 0x01, 
    0x80, 0x00, 0x01, 0x00, 0x00, 0x02, 0x40, 0x00, 0x02, 0x20, 0x00, 0x04, 
    0x30, 0x00, 0x0C, 0x18, 0x00, 0x18, 0x0C, 0x00, 0x30, 0x06, 0x00, 0x60, 
    0x03, 0x00, 0xC0, 0x01, 0x81, 0x80, 0x00, 0x81, 0x00, 0x00, 0x42, 0x00, 
    0x00, 0x24, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// --- ФУНКЦИЯ ОБНОВЛЕНИЯ ЭКРАНА ---
void updateDisplay(int bpm, const char* status) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(status);

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

// --- УПРАВЛЕНИЕ РЕЛЕ С ГИСТЕРЕЗИСОМ ---
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

// --- CALLBACK для уведомлений от пульсометра ---
class MyClientCallback : public BLEClientCallbacks {
    void onDisconnect(BLEClient* pClient) {
        sensorConnected = false;
        handleRelays(0);
        updateDisplay(0, "Lost Sensor");
        Serial.println("Sensor disconnected");
    }
};

// --- CALLBACK для подключения телефона ---
class MyServerCallback : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        Serial.println("Phone connected!");
    }
    
    void onDisconnect(BLEServer* pServer) {
        Serial.println("Phone disconnected");
    }
};

void notifyCallback(
    BLERemoteCharacteristic* pRemoteCharacteristic,
    uint8_t* pData,
    size_t length,
    bool isNotify) {
    
    if (length >= 2) {
        uint8_t flags = pData[0];
        uint8_t bpmValue = 0;
        
        if (flags & 0x01) {
            // 16-bit формат
            bpmValue = pData[2];
        } else {
            // 8-bit формат
            bpmValue = pData[1];
        }
        
        if (bpmValue > 0 && bpmValue < 200) {
            lastBPM = bpmValue;
            handleRelays(lastBPM);
            updateDisplay(lastBPM, "Connected");
            
            // Отправка данных на телефон через сервер
            if (pServerCharacteristic && sensorConnected) {
                // Формат Heart Rate Measurement:
                // байт 0: флаги (0x00 = 8-бит, 0x01 = 16-бит)
                // байт 1: значение пульса
                uint8_t hrmData[2] = {0x00, bpmValue};
                pServerCharacteristic->setValue(hrmData, 2);
                pServerCharacteristic->notify(true);
                
                Serial.printf("BPM: %d | Sent to phone\n", bpmValue);
            }
            
            Serial.printf("Relay: %d\n", currentActiveRelay);
        }
    }
}

// --- CALLBACK для сканирования пульсометра ---
class MyScanCallback : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.printf("Found: %s ", advertisedDevice.getName().c_str());
        Serial.printf("(%s)\n", advertisedDevice.getAddress().toString().c_str());
        
        if (advertisedDevice.haveServiceUUID() && 
            advertisedDevice.isAdvertisingService(HR_SERVICE_UUID)) {
            Serial.println(">>> Heart Rate Sensor Found!");
            
            BLEDevice::getScan()->stop();
            if(foundHeartSensor) delete foundHeartSensor;
            foundHeartSensor = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
        }
    }
    
    void onScanEnd(BLEScan* pScan) {
        Serial.println("Scan ended, waiting for next attempt...");
    }
};

// --- ПОДКЛЮЧЕНИЕ К ПУЛЬСОМЕТРУ ---
bool connectToHeartSensor() {
    if (!foundHeartSensor) {
        Serial.println("No device found!");
        return false;
    }
    
    Serial.printf("Connecting to: %s (%s)\n", 
                  foundHeartSensor->getName().c_str(),
                  foundHeartSensor->getAddress().toString().c_str());
    
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    if (!pClient->connect(foundHeartSensor)) {
        Serial.println("Connection failed!");
        delete pClient;
        return false;
    }
    
    Serial.println("Connected!");

    BLERemoteService* pService = pClient->getService(HR_SERVICE_UUID);
    if (!pService) {
        Serial.println("Service not found!");
        pClient->disconnect();
        delete pClient;
        return false;
    }
    Serial.println("Service found!");
    
    pHRCharacteristic = pService->getCharacteristic(HR_CHAR_UUID);
    if (!pHRCharacteristic) {
        Serial.println("Characteristic not found!");
        pClient->disconnect();
        delete pClient;
        return false;
    }
    Serial.println("Characteristic found!");
    
    if (pHRCharacteristic->canNotify()) {
        pHRCharacteristic->registerForNotify(notifyCallback);
        sensorConnected = true;
        Serial.println("Subscribed!");
        return true;
    }
    
    Serial.println("Subscribe failed!");
    pClient->disconnect();
    delete pClient;
    return false;
}

void setup() {
    Serial.begin(115200);
    while(!Serial) delay(100);
    
    Serial.println("\n=== Smart Fun HR Bridge ===");
    Serial.println("Starting initialization...");
    
    // Инициализация реле
    for (int i = 0; i < NUM_RELAYS; i++) {        
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], OFF);
    }
    Serial.println("Relays initialized");

    // Инициализация OLED
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("OLED failed!");
        for(;;); 
    }
    Serial.println("OLED initialized");

    display.setRotation(2);
    display.ssd1306_command(0x81);
    display.ssd1306_command(255);
    Serial.println("Display configured");

    // Инициализация BLE
    Serial.println("Init BLE...");
    BLEDevice::init("SmartFun HR");
    Serial.println("BLE initialized");
    
    // Создаём BLE сервер для телефона
    Serial.println("Creating BLE Server...");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallback());
    
    pServerService = pServer->createService(HR_SERVICE_UUID);
    pServerCharacteristic = pServerService->createCharacteristic(
        HR_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_INDICATE
    );
    
    pServerService->start();
    
    // Запускаем рекламу
    BLEAdvertising* pServerAdvertising = BLEDevice::getAdvertising();
    pServerAdvertising->addServiceUUID(HR_SERVICE_UUID);
    pServerAdvertising->setScanResponse(true);
    pServerAdvertising->setMinPreferred(0x06);
    pServerAdvertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE Server started - phone can connect!");

    // Настраиваем сканер для поиска пульсометра
    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyScanCallback());
    pScan->setInterval(1349);
    pScan->setWindow(449);
    pScan->setActiveScan(true);
    pScan->setDuplicateFilter(false);
    
    Serial.println("Starting initial scan...");
    pScan->start(5, true);  // blocking scan for 5 seconds

    Serial.println("Scanning for HR sensors...");
    updateDisplay(0, "Scanning...");
}

void loop() {
    // Обработка подключения к пульсометру
    if (doConnect) {
        doConnect = false;
        updateDisplay(lastBPM, "Connecting...");

        if (!connectToHeartSensor()) {
            Serial.println("Connection failed, cleaning up...");
            if (foundHeartSensor) {
                delete foundHeartSensor;
                foundHeartSensor = nullptr;
            }
            // Перезапуск сканирования
            BLEDevice::getScan()->start(5, true);
            updateDisplay(0, "Scanning...");
        }
    }

    // Если не подключены и не сканируемся - запускаем сканирование
    if (!sensorConnected && !doConnect && !BLEDevice::getScan()->isScanning()) {
        static unsigned long lastScanAttempt = 0;
        if (millis() - lastScanAttempt > 5000) {
            Serial.println("Restarting scan...");
            BLEDevice::getScan()->start(5, true);
            updateDisplay(0, "Scanning...");
            lastScanAttempt = millis();
        }
    }

    delay(200);
}

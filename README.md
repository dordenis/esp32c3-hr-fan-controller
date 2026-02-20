# ESP32-C3 HR Fan Controller

ESP32-C3 устройство, которое считывает пульс с BLE пульсометра, управляет 4 реле в зависимости от ЧСС, и транслирует данные на смартфон.

## Возможности

- **Подключение к BLE пульсометру** — автоматически находит и подключается к датчику пульса
- **Управление 4 реле** — включение реле в зависимости от порога пульса
- **Передача данных на телефон** — транслирует пульс через BLE сервер
- **OLED дисплей** — отображает текущий пульс, активное реле и статус подключения
- **Гистерезис** — предотвращает дребезг реле при пограничных значениях

## Схема работы

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│ BLE Пульсометр  │────>│ ESP32-C3         │────>│ Смартфон        │
│ (Heart Rate)    │ BLE │ (SmartFun HR)    │ BLE │ (nRF Connect)   │
└─────────────────┘     └──────────────────┘     └─────────────────┘
    Client                  Client + Server          Client
```

## Пороги срабатывания реле

| Пульс (BPM) | Активное реле | Описание |
|-------------|---------------|----------|
| 0-99 | - | Все реле выключены |
| 100-119 | Реле 1 | Лёгкая нагрузка |
| 120-139 | Реле 2 | Средняя нагрузка |
| 140-159 | Реле 3 | Высокая нагрузка |
| 160+ | Реле 4 | Максимальная нагрузка |

**Гистерезис:** 5 BPM

## Используемые библиотеки

| Библиотека | Версия | Автор |
|------------|--------|-------|
| Adafruit GFX Library | 1.12.4 | Adafruit |
| Adafruit SSD1306 | 2.5.16 | Adafruit |
| Adafruit BusIO | 1.17.4 | Adafruit |
| BLE | 3.3.7 | Espressif Systems |
| Wire | 3.3.7 | Espressif Systems |
| SPI | 3.3.7 | Espressif Systems |

### Установка библиотек

```bash
# Через Arduino CLI
arduino-cli lib install "Adafruit GFX Library"
arduino-cli lib install "Adafruit SSD1306"

# Или через Arduino IDE Library Manager
```

## Hardware

### Требуемые компоненты

| Компонент | Описание |
|-----------|----------|
| ESP32-C3 | Микроконтроллер (например, ESP32-C3 Zero) |
| OLED 128x32 | SSD1306 I2C дисплей |
| Реле x4 | 5V реле модуль |
| BLE пульсометр | Polar H10, Garmin HRM, Wahoo TICKR и др. |

### Подключения

```
ESP32-C3          OLED SSD1306
========          ============
3.3V    --------> VCC
GND     --------> GND
GPIO 8  --------> SDA
GPIO 9  --------> SCL

ESP32-C3          Реле модуль
========          ===========
GPIO 4  --------> IN1
GPIO 5  --------> IN2
GPIO 6  --------> IN3
GPIO 7  --------> IN4
5V      --------> VCC
GND     --------> GND
```

## Компиляция и загрузка

### Требования

- Arduino CLI 0.24+ или Arduino IDE 2.0+
- Пакет esp32:esp32 версии 3.3.7+

### Компиляция

```bash
# Через Arduino CLI
arduino-cli compile -b esp32:esp32:esp32c3:CDCOnBoot=cdc ./smartfan

# Загрузка
arduino-cli upload -p /dev/ttyACM0 -b esp32:esp32:esp32c3:CDCOnBoot=cdc ./smartfan
```

### Настройка Arduino IDE

1. Добавьте URL платы: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. Установите плату "ESP32C3 Dev Module"
3. Выберите порт и загрузите скетч

## BLE сервисы

### Сервер (для телефона)

- **Service UUID:** `0x180D` (Heart Rate Service)
- **Characteristic UUID:** `0x2A37` (Heart Rate Measurement)
- **Свойства:** READ, WRITE, NOTIFY, INDICATE

### Формат данных

```
Байт 0: 0x00 (флаг 8-бит формата)
Байт 1: Пульс (0-180 BPM)
```

## Подключение смартфона

1. Установите **nRF Connect** (iOS/Android)
2. Включите пульсометр (наденьте/смочите электроды)
3. ESP32 автоматически подключится к пульсометру
4. В nRF Connect найдите устройство **"SmartFun HR"**
5. Подключитесь к устройству
6. Найдите сервис **0x180D** (Heart Rate)
7. Найдите характеристику **0x2A37**
8. Нажмите **Subscribe** (иконка колокольчика)

Теперь телефон получает данные о пульсе в реальном времени!

## OLED дисплей

```
Connected          ← Статус подключения к пульсометру
[FAN icon] 2       ← Активное реле №2
[HEART icon] 125   ← Пульс 125 BPM
```

### Статусы

| Статус | Описание |
|--------|----------|
| Scanning... | Поиск пульсометра |
| Connecting... | Попытка подключения |
| Connected | Подключено к пульсометру |
| Lost Sensor | Потеряно соединение |
| Retry... | Переподключение |

## Совместимые пульсометры

Устройство работает с любым BLE пульсометром, поддерживающим стандартный сервис **Heart Rate (0x180D)**:

- Polar H10 / H9 / H7
- Garmin HRM-Dual / HRM-Pro / HRM-Run
- Wahoo TICKR / TICKR X / TICKR RUN
- Magene H303 / H304
- Coospo H6 / H808S
- И другие

## Конфигурация

Изменить пороги срабатывания можно в файле `smartfan.ino`:

```cpp
constexpr int THRESHOLDS[] = {100, 120, 140, 160};  // Пороги BPM
constexpr int HYSTERESIS = 5;                        // Гистерезис BPM
```

## Структура проекта

```
esp32c3-hr-fan-controller/
├── esp32c3-hr-fan-controller.ino  # Исходный код прошивки
└── README.md                      # Документация
```

## Версии

- **v1.0** — Первая версия
  - Подключение к BLE пульсометру
  - Управление 4 реле по порогам пульса
  - OLED дисплей с иконками
  - BLE сервер для передачи данных на телефон
  - Гистерезис для стабильности реле

## Лицензия

MIT License

## Авторы

- Denis Orden

## Ссылки

- [GitHub репозиторий](https://github.com/dordenis/esp32c3-hr-fan-controller)
- [Heart Rate Service Specification](https://www.bluetooth.com/specifications/specs/heart-rate-service-1-0/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)

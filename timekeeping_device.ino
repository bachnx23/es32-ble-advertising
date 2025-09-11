#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEBeacon.h>

#define DEVICE_NAME         "ESP32-BLE"
#define SERVICE_UUID        "7A0247E7-8E88-409B-A959-AB5092DDB03E"
#define CHARACTERISTIC_UUID "82258BAA-DF72-47E8-99BC-B73D7ECD08A5"
#define BEACON_UUID_REV     "A134D0B2-1DA2-1BA7-C94C-E8E00C9F7A2D"

BLEServer *pServer;
BLECharacteristic *pCharacteristic;
BLEAdvertising *pAdvertising;

bool deviceConnected = false;
uint8_t value = 0;
unsigned long lastAdvertiseCheck = 0;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("✅ Device connected");
  };

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("⚠️ Device disconnected → restarting advertising...");
    if (pAdvertising) {
      pAdvertising->start();
    }
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      Serial.print("📥 Received Value: ");
      Serial.println(rxValue.c_str());
    }
  }
};

void init_service() {
  // Create BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create Characteristic
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  // Add service UUID vào quảng bá
  pAdvertising->addServiceUUID(SERVICE_UUID);
}

void init_beacon() {
  BLEBeacon myBeacon;
  myBeacon.setManufacturerId(0x4C00);  // Apple iBeacon
  myBeacon.setMajor(5);
  myBeacon.setMinor(88);
  myBeacon.setSignalPower(0xC5);
  myBeacon.setProximityUUID(BLEUUID(BEACON_UUID_REV));

  BLEAdvertisementData advertisementData;
  advertisementData.setFlags(0x1A);
  advertisementData.setManufacturerData(myBeacon.getData());

  pAdvertising->setAdvertisementData(advertisementData);
}

void startAdvertising() {
  if (!pAdvertising) return;

  // Thiết lập tốc độ quảng bá
  pAdvertising->setMinInterval(0x20);  // 20ms
  pAdvertising->setMaxInterval(0x40);  // 40ms
  pAdvertising->setScanResponse(true);

  pAdvertising->start();
  Serial.println("📡 BLE Advertising started");
}

void setup() {
  Serial.begin(115200);
  Serial.println("🚀 Initializing ESP32 BLE...");

  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  pAdvertising = pServer->getAdvertising();
  pAdvertising->stop();

  // Bật service để connect (bạn có thể bật cả 2)
  init_service();

  // Nếu muốn phát iBeacon thì bật cái này
  // init_beacon();

  startAdvertising();
}

void loop() {
  // Nếu có client connect thì notify dữ liệu
  if (deviceConnected) {
    Serial.printf("🔔 Notify: %d\n", value);
    pCharacteristic->setValue(&value, 1);
    pCharacteristic->notify();
    value++;
    delay(2000);
  }

  // Watchdog: check mỗi 5 giây xem có còn quảng bá không
  if (millis() - lastAdvertiseCheck > 5000) {
    lastAdvertiseCheck = millis();
    if (pAdvertising && !pAdvertising->isAdvertising()) {
      Serial.println("⚠️ Advertising stopped unexpectedly → restart");
      startAdvertising();
    }
  }
}

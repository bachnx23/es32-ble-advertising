#include <WiFi.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ==== BLE UUIDs & Device Info ====
#define SERVICE_UUID        "7A0247E7-8E88-409B-A959-AB5092DDB03E"
#define CHARACTERISTIC_UUID "82258BAA-DF72-47E8-99BC-B73D7ECD08A5"
#define DEVICE_NAME_DEFAULT "ESP32_Device"

// ==== LED & Button Pin ====
#define LED_PIN 2         // LED đỏ (báo AP)
#define RESET_BTN_PIN 0   // Nút boot trên ESP32 (GPIO0)

// ==== Global objects ====
Preferences preferences;
BLEServer *pServer;
BLECharacteristic *pCharacteristic;

bool deviceConnected = false;
bool inAPMode = false;
unsigned long wifiStartTime = 0;
const unsigned long WIFI_TIMEOUT = 120000; // 2 phút
uint8_t value = 0;

// ==== BLE Callbacks ====
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("BLE device connected");
  }
  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("BLE device disconnected → restart advertising");
    pServer->getAdvertising()->start();
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue().c_str();
    if (rxValue.length() > 0) {
      Serial.print("Received Value: ");
      Serial.println(rxValue);
    }
  }
};

// ==== Generate unique SSID & Password from MAC ====
String getUniqueID() {
  uint64_t chipid = ESP.getEfuseMac();
  char id[13];
  sprintf(id, "%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
  return String(id);
}

// ==== WiFi Connect ====
bool connectWiFi(const char *ssid, const char *password) {
  Serial.printf("Connecting to WiFi SSID: %s ...\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  wifiStartTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < WIFI_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    inAPMode = false;
    digitalWrite(LED_PIN, LOW);
    return true;
  } else {
    Serial.println("\n❌ WiFi connect failed (timeout).");
    return false;
  }
}

// ==== AP Mode ====
void startAPMode() {
  String uniqueID = getUniqueID();
  String ssid = "ESP32_" + uniqueID.substring(8);
  String password = "PASS_" + uniqueID.substring(8);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str(), password.c_str());
  Serial.printf("AP SSID: %s, PASS: %s\n", ssid.c_str(), password.c_str());
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  inAPMode = true;
}

// ==== BLE Init ====
void initBLE(const char* deviceName) {
  BLEDevice::init(deviceName);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.println("✅ BLE service & advertising started");
}

// ==== Reset Config nếu giữ nút 10s ====
void checkResetButton() {
  static unsigned long pressStart = 0;
  if (digitalRead(RESET_BTN_PIN) == LOW) { // nút nhấn xuống
    if (pressStart == 0) {
      pressStart = millis();
      Serial.println("RESET Button pressed");
    }

    unsigned long heldTime = millis() - pressStart;
    if (heldTime > 10000) { // giữ 10 giây
      Serial.println("⚠️ Reset button held → clearing WiFi config...");
      digitalWrite(LED_PIN, HIGH);  // bật LED báo hiệu
      preferences.begin("wifi", false);
      preferences.clear();
      preferences.end();
      delay(500);  // cho người dùng thấy LED sáng
      ESP.restart();
    }
  } else {
    pressStart = 0; // reset nếu nhả nút
  }
}

// ==== Setup ====
void setup() {
  Serial.begin(115200);
  Serial.println("\nBooting...");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);

  // Load WiFi info
  preferences.begin("wifi", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  String deviceName = preferences.getString("deviceName", DEVICE_NAME_DEFAULT);
  preferences.end();

  if (!ssid.isEmpty() && !password.isEmpty()) {
    if (!connectWiFi(ssid.c_str(), password.c_str())) {
      startAPMode();
    }
  } else {
    startAPMode();
  }

  initBLE(deviceName.c_str());
}

// ==== Loop ====
void loop() {
  if (deviceConnected) {
    pCharacteristic->setValue(&value, 1);
    pCharacteristic->notify();
    Serial.printf("*** NOTIFY: %d ***\n", value);
    value++;
  }

  if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi lost. Retrying...");
    preferences.begin("wifi", true);
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");
    preferences.end();

    if (!ssid.isEmpty() && !password.isEmpty()) {
      if (!connectWiFi(ssid.c_str(), password.c_str())) {
        startAPMode();
      }
    }
  }

  if (inAPMode) {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      lastBlink = millis();
    }
  }

  // checkResetButton();
  delay(100);
}

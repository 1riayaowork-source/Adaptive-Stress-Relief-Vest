#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <SSD1306.h>

// ====================== 1. 配置WiFi（必须改！改成你自己的）======================
const char* ssid = "";    // 填写你的WiFi名称
const char* password = "";// 填写你的WiFi密码

// ====================== 2. 硬件引脚定义（适配你的MOS模块标注）=====================
#define PUMP_PIN 26    // MOS模块印"SCL"的引脚 → ESP32 GPIO26（控制气泵）
#define VALVE_PIN 27   // MOS模块印"SDA"的引脚 → ESP32 GPIO27（控制放气阀，无阀可注释）
#define PRESSURE_ADC_PIN 36 // 压力传感器引脚（可选，无则注释）

// OLED引脚（ESP32 21=SDA，22=SCL）
#define OLED_SDA 21
#define OLED_SCL 22
SSD1306 display(0x3C, OLED_SDA, OLED_SCL); // 0x3C不行改0x3D

// ====================== 3. 压力阈值配置（可自定义）=====================
#define PRESSURE_THRESHOLD_HIGH 80
#define PRESSURE_THRESHOLD_LOW 20
#define PRESSURE_RESET_VALUE 50

// ====================== 4. 设备状态 & Web服务器 ======================
enum DeviceState { STATE_IDLE, STATE_DEFLATING, STATE_RESETTING };
DeviceState currentState = STATE_IDLE;
WebServer server(80);

// ====================== 5. 网页虚拟控制器界面 ======================
const char* htmlPage = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>充气马甲控制器</title>
  <style>
    body { font-family: Arial; text-align: center; margin-top: 50px; }
    button { 
      font-size: 24px; padding: 20px 40px; margin: 10px;
      border: none; border-radius: 10px; cursor: pointer;
    }
    #resetBtn { background-color: #4CAF50; color: white; }
    #deflateBtn { background-color: #f44336; color: white; }
    #status { font-size: 20px; margin-top: 30px; color: #333; }
  </style>
</head>
<body>
  <h1>充气马甲虚拟控制器</h1>
  <button id="resetBtn" onclick="sendCommand('reset')">压力归位</button>
  <button id="deflateBtn" onclick="sendCommand('deflate')">放气</button>
  <div id="status">当前状态：空闲</div>
  <script>
    function sendCommand(cmd) {
      fetch('/' + cmd)
        .then(response => response.text())
        .then(data => {
          document.getElementById('status').innerText = '当前状态：' + data;
        });
    }
  </script>
</body>
</html>
)HTML";

// ====================== 6. OLED显示函数 ======================
void updateOLED() {
  display.clear();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("Air Vest Controller");
  
  int currentPressure = analogRead(PRESSURE_ADC_PIN) / 4095.0 * 100;
  display.setCursor(0, 20);
  display.print("Pressure: ");
  display.print(currentPressure);
  display.print("%");
  
  display.setCursor(0, 40);
  display.print("State: ");
  switch(currentState) {
    case STATE_IDLE: display.print("Idle"); break;
    case STATE_DEFLATING: display.print("Deflating"); break;
    case STATE_RESETTING: display.print("Resetting"); break;
  }
  display.display();
}

// ====================== 7. 硬件控制函数 ======================
void startDeflating() {
  currentState = STATE_DEFLATING;
  digitalWrite(VALVE_PIN, HIGH);
  digitalWrite(PUMP_PIN, LOW);
  Serial.println("【手动触发】开始放气...");
  updateOLED();
}
void stopDeflating() {
  digitalWrite(VALVE_PIN, LOW);
  currentState = STATE_IDLE;
  Serial.println("放气停止，空闲中");
  updateOLED();
}

void startResetting() {
  currentState = STATE_RESETTING;
  int currentPressure = analogRead(PRESSURE_ADC_PIN) / 4095.0 * 100;
  Serial.printf("【手动触发】开始归位 | 当前压力：%d，目标：%d\n", currentPressure, PRESSURE_RESET_VALUE);
  
  if (currentPressure < PRESSURE_RESET_VALUE) {
    digitalWrite(PUMP_PIN, HIGH);
    digitalWrite(VALVE_PIN, LOW);
  } else {
    digitalWrite(VALVE_PIN, HIGH);
    digitalWrite(PUMP_PIN, LOW);
  }
  updateOLED();
}
void stopResetting() {
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(VALVE_PIN, LOW);
  currentState = STATE_IDLE;
  Serial.println("压力归位完成");
  updateOLED();
}

// ====================== 8. Web服务器处理函数 ======================
void handleReset() {
  startResetting();
  server.send(200, "text/plain", "压力归位中");
}
void handleDeflate() {
  startDeflating();
  server.send(200, "text/plain", "放气中");
}
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

// ====================== 9. 初始化函数 ======================
void setup() {
  Serial.begin(115200);
  
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(VALVE_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(VALVE_PIN, LOW);
  
  pinMode(PRESSURE_ADC_PIN, INPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  display.init();
  display.clear();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("Initializing...");
  display.display();

  Serial.print("连接WiFi：");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi连接成功！");
  Serial.print("虚拟控制器地址：");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/reset", handleReset);
  server.on("/deflate", handleDeflate);
  server.begin();
  Serial.println("Web服务器已启动！");
  
  updateOLED();
}

// ====================== 10. 主循环 ======================
void loop() {
  server.handleClient();

  if (currentState == STATE_RESETTING) {
    int currentPressure = analogRead(PRESSURE_ADC_PIN) / 4095.0 * 100;
    if (abs(currentPressure - PRESSURE_RESET_VALUE) <= 2) {
      stopResetting();
    }
  }

  updateOLED();
  delay(100);
}

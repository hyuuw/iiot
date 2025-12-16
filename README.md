
# Smart Coaster - 114NTUST\_IIOT

## When was the last time you took a sip of water?

For students like us, or for office workers who spend long hours at a desk, the answer is often, ''I don't remember.''

* BECAUSE : An Overlooked Office Wellness Problem
1. The Cost of Deep Work: When we are immersed in our work, our brains automatically ignore physiological signals like thirst.
2. Busyness as the Norm: Meetings, classes, and endless to-do lists push drinking water to the bottom of our priorities.
3. The Invisible Health Hazard: Chronic dehydration can lead to decreased focus, fatigue, and even long-term health problems.

* SO our Solution : The Smart Coaster
1. Low Cost: Our material budget is extremely low, which we will detail in the BOM.
2. Zero-Effort Automatic Tracking: The user doesn't need to do anything. Just place the cup on the coaster.
3. Smart Reminders with LED Light: We use visual light cues instead of phone notifications, which is more intuitive and less disruptive.
4. Use your existing drinkware: Whether it's a mug or a thermos, it works with our coaster.

## Team Members's Role
| Name     | Student ID  | Assignment |
|----------|-------------|---------|
| 楊婕     | M11451001   |    |
| 楓尹翔   | M11451007   |    |
| 簡翊程   | M11451011   |    |
| 顏梓淮   | M11451027   |    |
| 吳家瑜   | M11451029   |    |

## Hardware

Assemble the components according to the diagram.
![電路圖](./image/cd.jpg)


* For ESP32 TO HX711  (RED line)

  。The VCC pin connects to the 3V3 pin.

  。The DT pin connects to GPIO8.

  。GND connects to GND.

  。The SCK pin connects to GPIO9.

* For ESP32 TO LED strip  (BLUE line)

  。GND connects to GND.

  。DIN connects to GPIO10.

  。+5V connects to 5V.

* For ESP32 TO Button  (GREEN line)

  。Point A connects to GPIO18.

  。Point B connects to GND.
# 智慧杯墊系統設計圖

## DFD Level 0: Comtext Diagram

```mermaid
graph TD
    %% 定義樣式
    classDef entity fill:#e1f5fe,stroke:#01579b,stroke-width:2px;
    classDef process fill:#fff9c4,stroke:#fbc02d,stroke-width:4px,rx:50,ry:50;

    %% 外部實體
    User[User]:::entity
    Env[Physical Environment]:::entity
    Cloud[Blynk Cloud Platform]:::entity
    NTP[NTP Time Server]:::entity

    %% 中心過程
    System(( 0. Smart Coaster System)):::process

    %% 資料流
    Env -- "Original Weight Signal" --> System
    User -- "Button Operation (Reset)" --> System
    NTP -- "Current Date and<br/>Time Data" --> System

    System -- "Visual Feedback<br/>(LED Color/Flashing)" --> User
    System -- "Sensor Data and<br/>Status (V0-V3)" --> Cloud
```

## DFD Level 1: Decomposition Diagram

```mermaid
graph TD
    %% --- 風格設定區 ---
    %% edgeLabelBackground: #e0e0e0aa (灰底標籤)
    %% curve: stepAfter (直角線條)
    %%{init: {
      'theme': 'base',
      'themeVariables': { 
        'primaryColor': '#fff9c4', 
        'edgeLabelBackground': '#e0e0e0aa', 
        'tertiaryColor': '#fff'
      },
      'flowchart': { 'curve': 'stepAfter', 'nodeSpacing': 150, 'rankSpacing': 100 }
    }}%%

    %% --- 樣式定義 ---
    classDef entity fill:#e1f5fe,stroke:#01579b,stroke-width:2px;
    classDef process fill:#fff9c4,stroke:#fbc02d,stroke-width:2px,rx:5,ry:5;
    classDef store fill:#ffebee,stroke:#b71c1c,stroke-width:2px;
    classDef output fill:#e3f2fd,stroke:#1565c0,stroke-width:2px;

    %% --- 節點定義 (已加上雙引號保護特殊字元) ---
    Env[Physical Environment]:::entity
    User[User]:::entity
    NTP[NTP Time Server]:::entity
    
    subgraph Core [Firmware Core Logic]
        direction TB
        P1("1.0 Sensor Data Collection<br/>Filtering/Stable Reading"):::process
        P3("3.0 Data Management<br/>EEPROM/NTP/Reset"):::process
        D1[("D1: EEPROM")]:::store
        P2("2.0 Water Consumption Calculation<br/>Determine Drinking/Adding Water"):::process
        P4("4.0 Output Control<br/>LED/Animation"):::process
        P5("5.0 Cloud Communication<br/>Blynk Timer"):::process
    end

    Cloud[Blynk Cloud Platform]:::output

    %% --- 連線關係 (使用 -- "文字" --> 語法以避免括號報錯) ---
    Env -- "Original Weight Signal" --> P1
    User -- "ButtonSetting Command" --> P3
    NTP -- "Time Data" --> P3

    P1 -- "Stable Total Weight" --> P2
    
    P3 -- "Update Storage Data" --> D1
    D1 -- "Read Storage Data" --> P3

    P3 -- "Read: Empty Bottle Weight" --> P2
    P2 -- "Calculated Water Consumption Increment" --> P3

    P2 -- "Current Water Weight Cup Status" --> P4
    P3 -- "Today's Total/Reset Flag" --> P4
    
    P4 -- "LED Control Signal" --> User

    P2 -.-> P5
    P3 -.-> P5
    P4 -.-> P5
    
    P5 -- "Data V0-V3" --> Cloud
```

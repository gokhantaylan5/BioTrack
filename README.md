# BioTrack

### BLE-Based Wearable Health & Safety Monitoring System

> [!IMPORTANT]
> **Disclaimer:** BioTrack is an engineering and educational prototype. Its measurements and event-detection algorithms are not intended for medical diagnosis or to replace certified medical devices or professional medical assessment.

BioTrack is an **ESP32-based wearable monitoring prototype** that collects biometric and motion data, identifies potential safety events, and transmits sensor readings to an Android application via **Bluetooth Low Energy (BLE)**.

The project combines heart-rate monitoring, motion analysis, fall and inactivity detection, manual emergency alerts, and real-time mobile visualization in a portable system.

## Overview

BioTrack was developed for scenarios where continuous observation and timely awareness of potential emergencies may be useful. The wearable monitors:

- Heart rate (BPM) and infrared (IR) sensor readings
- Three-axis acceleration and angular velocity
- Activity level and inactivity status
- Potential fall events
- Manual emergency-button activation

An ESP32 processes the sensor data and communicates with a Flutter-based Android application over BLE. The prototype's mobile interface supports live data visualization, configurable thresholds, and audible or notification-based alerts.

## System Architecture

<p align="center">
  <img src="docs/blockdiagram.png" alt="BioTrack system architecture block diagram" width="900">
</p>

At a high level, the system follows this data flow:

**Sensors and emergency button → ESP32 processing → BLE → Android monitoring application**

The wearable also provides local feedback through an OLED display and an RGB LED.

## Hardware Components

| Component | Function |
| --- | --- |
| ESP32 | Sensor acquisition, data processing, and BLE communication |
| MAX30100 | Optical heart-rate sensing and IR signal acquisition |
| MPU6050 | Three-axis accelerometer and gyroscope measurements |
| RGB LED | Visual indication of system status |
| OLED display | Heart-rate and sensor-contact information |
| Emergency button | Manual emergency-event triggering |
| Battery | Portable power supply |
| TP4056 charging module | Battery charging |
| 5 V boost converter | Voltage conversion for the system power supply |
| Power switch | Main power control |

## Core Features

### Heart-Rate Monitoring

The **MAX30100** acquires an optical IR signal, which is processed using digital filtering and adaptive thresholding to estimate heart rate in beats per minute (BPM).

The firmware also evaluates sensor contact. When adequate contact is not detected, heart-rate measurement is suspended to avoid displaying misleading readings.

### Motion Analysis

The **MPU6050** supplies acceleration and angular-velocity measurements along three axes. The corresponding vector magnitudes are calculated as:

$$
|a| = \sqrt{A_x^2 + A_y^2 + A_z^2}
$$

$$
|\omega| = \sqrt{G_x^2 + G_y^2 + G_z^2}
$$

These values provide the input for the fall- and inactivity-detection algorithms.

### Potential Fall Detection

The algorithm monitors sudden changes in acceleration magnitude between consecutive samples:

$$
\Delta |a| = \left|\,|a_t| - |a_{t-1}|\,\right|
$$

If the change exceeds a configured threshold, the firmware flags a **potential fall event**. A threshold crossing is not, by itself, proof that a fall occurred.

### Inactivity Detection

An activity score combines acceleration and gyroscope information:

$$
\text{ActivityScore} = \left|\,|a| - 9.81\,\right| + K\,|\omega|
$$

Here, acceleration is expressed in m/s², angular velocity uses the units configured in the firmware, and **K** is a scaling factor. If the score remains below its configured threshold for a specified period, the system raises an inactivity alert.

### Manual Emergency Button

A physical button allows the wearer to manually trigger an emergency event. The event is sent over BLE to the mobile application, where an alert can be displayed.

### Local Visual Feedback

The RGB LED indicates the device's current state:

| LED color | Indication |
| --- | --- |
| Green | Sensor contact detected; no active warning |
| Red | Warning condition or threshold violation |
| Blue | No sensor contact; heart-rate measurement inactive |

The OLED display shows the current heart-rate reading and contact status.

## Mobile Application

The prototype includes a **Flutter-based Android monitoring interface** that receives sensor data through BLE and presents live measurements and alerts.

> **Repository note:** The original Flutter application source code is not currently available in this repository. The screenshots below document the mobile interface developed for the prototype.

### Graph View

<p align="center">
  <img src="docs/graphview.png" alt="BioTrack mobile application graph view" width="900">
</p>

The graph interface visualizes:

- Acceleration and gyroscope measurements
- Heart rate (BPM) and raw IR readings
- Signal values and configured thresholds
- Fall and inactivity status

Individual channels can be shown or hidden using checkbox controls. Selected thresholds can also be displayed alongside the corresponding data.

### Data View

<p align="center">
  <img src="docs/dataview.png" alt="BioTrack mobile application data view" width="900">
</p>

The data view displays incoming sensor readings in a text-based format and allows users to configure monitoring thresholds, including BPM limits. When a threshold is exceeded, the application can issue an audio alert or notification.

## Alert System

BioTrack combines local hardware indicators with mobile alerts for the following conditions:

- Manual emergency-button activation
- Potential fall detection
- Prolonged inactivity
- Heart-rate values outside configured thresholds

Detected events are reflected through the wearable's feedback components and, while a BLE connection is available, through the Android application.

## Power & Mechanical Design

The wearable is battery-powered and uses a charging circuit and a **5 V boost converter** as part of its power subsystem.

The prototype uses a **two-layer perforated-board assembly**:

- **Lower layer:** Body-facing components, including the MAX30100 sensor
- **Upper layer:** ESP32 and other control electronics

Removable pins, screws, and nuts connect the layers, making the assembly easier to inspect, maintain, and modify.

## Communication

The wearable and mobile application exchange data locally over BLE:

```text
ESP32 wearable  ── Bluetooth Low Energy (BLE) ──>  Flutter Android app
```

BLE allows wireless transfer of sensor readings and alert states without requiring a continuous internet connection. Mobile monitoring requires the device to remain within BLE communication range.

## Development Environment

- **ESP32 firmware:** Arduino framework / C++
- **Mobile interface:** Flutter (Android; source code not included in the current repository)
- **Wireless communication:** Bluetooth Low Energy (BLE)

## Project Status

The prototype implementation covers:

- ESP32-based data acquisition and processing
- Heart-rate and motion measurements
- Potential fall and inactivity detection
- Physical emergency button
- BLE data transmission
- OLED and RGB LED feedback
- Flutter-based monitoring interface demonstrated in the project screenshots
- Configurable thresholds and mobile alerts

**Note:** This repository documents a functional engineering prototype, not a clinically validated or commercially certified wearable product.

## Future Improvements

Potential directions for continued development include:

- GPS-based location reporting
- GSM/cellular connectivity for wider-area emergency communication
- Remote monitoring and cloud data storage
- Historical health and activity visualization
- More robust fall-detection algorithms and evaluation with representative datasets
- Machine-learning-based anomaly detection
- Reduced power consumption and improved battery management
- A compact custom PCB and improved wearable enclosure

GPS and cellular communication would extend the system beyond local BLE monitoring and could enable location-aware remote emergency notifications.

## Potential Applications

The architecture may be adapted for research and prototyping in areas such as:

- Wearable personal-safety monitoring
- Elderly and assisted-care support concepts
- Personal emergency-alert systems
- General activity monitoring
- Animal or pet activity monitoring (with appropriate sensor and algorithm changes)

---

*BioTrack is intended for engineering experimentation and educational demonstration.*

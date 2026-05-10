/***************************************************
 * Author: Ingmar Stapel
 * Website: www.custom-build-robots.com
 * Date: 2025-10-30
 * Version: 2.0 
 * Description:
 * This program enables your ESP32-based robot car to be
 * controlled via a PlayStation 4/5 controller using the
 * Bluepad32 library. ENA/ENB on L298N are assumed to be
 * jumpered HIGH. Speed and direction are controlled by
 * PWM signals on IN1, IN2, IN3, IN4 pins.
 ****************************************************/

#include <WiFi.h>
#include <Adafruit_SSD1306.h> // For OLED display
#include <Bluepad32.h>   // Include Bluepad32 library
#include <Preferences.h> // For non-volatile storage (NVS) on ESP32

// ----------------------------
// Adafruit Display Definition (NEU)
// ----------------------------
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_RESET   -1  // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< I2C-Adresse (häufig 0x3C oder 0x3D)

// Das Display-Objekt mit der Adafruit-Klasse erstellen:
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// ----------------------------
// GPIO Pin Definitions for L298N Motor Driver INx lines
// ENA and ENB are assumed to be jumpered to HIGH externally.
// ----------------------------
#define MOTOR_LEFT_IN1_GPIO 13  // ESP32 GPIO for L298N IN1
#define MOTOR_LEFT_IN2_GPIO 12  // ESP32 GPIO for L298N IN2
#define MOTOR_RIGHT_IN3_GPIO 27 // ESP32 GPIO for L298N IN3
#define MOTOR_RIGHT_IN4_GPIO 33 // ESP32 GPIO for L298N IN4

// ----------------------------
// ESP32 LEDC PWM Configuration for INx Pins
// ----------------------------
#define MOTOR_LEFT_IN1_PWM_CHANNEL 0
#define MOTOR_LEFT_IN2_PWM_CHANNEL 1
#define MOTOR_RIGHT_IN3_PWM_CHANNEL 2
#define MOTOR_RIGHT_IN4_PWM_CHANNEL 3

#define PWM_MOTOR_FREQUENCY 5000 // Frequency in Hz for motor PWM (e.g., 5000 Hz)
#define PWM_MOTOR_RESOLUTION 8   // Resolution in bits (8-bit = 0-255 speed range)
const int MAX_MOTOR_PWM_VALUE = (1 << PWM_MOTOR_RESOLUTION) - 1; // Max PWM value (255 for 8-bit)

// ----------------------------
// HC-SR04 Ultrasonic Sensor
// ----------------------------
#define TRIG_PIN 5    // ESP32 GPIO connected to HC-SR04 TRIG
#define ECHO_PIN 18   // ESP32 GPIO connected to HC-SR04 ECHO
const unsigned long ECHO_TIMEOUT_US = 25000; // ~4 m max range, prevents long blocking
const int OBSTACLE_DISTANCE_CM = 20;         // Trigger threshold for evasion
const int AUTO_FORWARD_SPEED = 60;           // Forward speed in autonomous mode (%)
const int AUTO_TURN_SPEED = 70;              // Turn speed in autonomous mode (%)
const int AUTO_REVERSE_SPEED = 60;           // Reverse speed in autonomous mode (%)
const unsigned long AUTO_REVERSE_MS = 400;   // Reverse duration before turning
const unsigned long AUTO_TURN_MS = 450;      // Turn duration
const unsigned long DISTANCE_INTERVAL_MS = 80; // How often to ping the sensor

// ----------------------------
// Drive Mode (Manual vs Autonomous vs Setup Menu)
// ----------------------------
enum DriveMode {
  MANUAL_MODE,
  AUTONOMOUS_MODE,
  SETUP_MODE
};
DriveMode currentDriveMode = MANUAL_MODE;

// Autonomous state machine
enum AutoState {
  AUTO_FORWARD,
  AUTO_REVERSE,
  AUTO_TURN
};
AutoState autoState = AUTO_FORWARD;
unsigned long autoStateStartedAt = 0;
int autoTurnDirection = 0; // -1 = left, +1 = right

// Latest measured distance in cm (0 = out of range / no echo)
long lastDistanceCm = 0;

// ----------------------------
// Robot Name
// ----------------------------
const String ROBOT_NAME = "Ingmar's Truck";

// ----------------------------
// Motor Speeds (-100 to 100)
// ----------------------------
int speed_left = 0;
int speed_right = 0;

// ----------------------------
// Command Timeout for Safety
// ----------------------------
unsigned long lastCommandTime = 0;
const unsigned long COMMAND_TIMEOUT = 5000; // 5 seconds

// ----------------------------
// Inversion Option
// ----------------------------
const bool invert_controls = false; // Set to 'true' to invert controls

// ----------------------------
// Direction Multiplier
// ----------------------------
const int direction_multiplier = invert_controls ? -1 : 1;

// ----------------------------
// Motor Speed Trim (Calibration)
// ----------------------------
// If one motor is faster than the other, lower the trim value of the faster
// side until both wheels turn at the same speed. 100 = full speed (no change),
// 95 = run that motor at 95% of the requested speed, and so on.
// These values are loaded from non-volatile memory (NVS) at startup and can
// be edited at runtime via the setup menu (Options button on the controller).
const int MOTOR_TRIM_DEFAULT = 100;
int MOTOR_LEFT_TRIM = 100;   // 0-100 (%) - loaded from NVS in setup()
int MOTOR_RIGHT_TRIM = 100;  // 0-100 (%) - loaded from NVS in setup()

// NVS (non-volatile storage) handle and key names
Preferences prefs;
const char* PREFS_NAMESPACE = "robotcar";
const char* PREFS_KEY_LEFT_TRIM = "left_trim";
const char* PREFS_KEY_RIGHT_TRIM = "right_trim";

// ----------------------------
// Setup Menu State
// ----------------------------
// Menu items: 0 = Left Trim, 1 = Right Trim, 2 = Save & Exit, 3 = Reset Defaults
const int MENU_ITEM_LEFT_TRIM = 0;
const int MENU_ITEM_RIGHT_TRIM = 1;
const int MENU_ITEM_SAVE = 2;
const int MENU_ITEM_RESET = 3;
const int MENU_ITEM_COUNT = 4;
int menuIndex = 0;
unsigned long savedToastUntil = 0; // millis() until which "Saved!" toast is visible

// ----------------------------
// Controller Deadzone
// ----------------------------
const int DEADZONE = 10;
ControllerPtr myControllers[BP32_MAX_GAMEPADS];

void drawSetupMenu(); // forward declaration

void updateDisplayStatus() {
  // In setup mode, the menu drawer takes over the whole screen.
  if (currentDriveMode == SETUP_MODE) {
    drawSetupMenu();
    return;
  }

  // Anpassung für Adafruit: clear() -> clearDisplay(), Text wird mit setCursor und print/println geschrieben
  display.clearDisplay(); 
  display.setCursor(0, 0);

  // Zeile 1: Name des Roboters
  display.print(ROBOT_NAME);

  // Controller Status
  bool controllerConnected = false;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] && myControllers[i]->isConnected()) {
        controllerConnected = true;
        break;
    }
  }

  // Zeile 2: Controller Status (Cursor auf Y=10)
  display.setCursor(0, 10);
  if (controllerConnected){
    display.print("Ctrl Connected");
  } else {
    display.print("Ctrl Disconnected");
  }

  // Zeile 3: Drive Mode (Cursor auf Y=20)
  display.setCursor(0, 20);
  if (currentDriveMode == AUTONOMOUS_MODE) {
    display.print("Mode: Auto");
  } else {
    display.print("Mode: Manual");
  }

  // Zeile 4: Distance (nur im Auto-Modus) oder Motorgeschwindigkeiten (Cursor auf Y=30)
  display.setCursor(0, 30);
  if (currentDriveMode == AUTONOMOUS_MODE) {
    if (lastDistanceCm > 0) {
      display.print("Dist: " + String(lastDistanceCm) + " cm");
    } else {
      display.print("Dist: --");
    }
  } else {
    display.print("L:" + String(speed_left) + "% R:" + String(speed_right) + "%");
  }
  
  display.display(); // Zeigt den Puffer auf dem Display an
}

// ----------------------------
// Motor Control Function (PWM on INx pins, ENA/ENB jumpered HIGH)
// ----------------------------
void controlMotorSide(int side_index, int speed_percent) {
    // side_index: 0 for left motor, 1 for right motor
    // speed_percent: -100 (full backward) to 100 (full forward)

    uint8_t in_pin_A_pwm_channel, in_pin_B_pwm_channel;
    int trim_percent;

    if (side_index == 0) { // Left Motor
        in_pin_A_pwm_channel = MOTOR_LEFT_IN1_PWM_CHANNEL; // Connected to MOTOR_LEFT_IN1_GPIO
        in_pin_B_pwm_channel = MOTOR_LEFT_IN2_PWM_CHANNEL; // Connected to MOTOR_LEFT_IN2_GPIO
        trim_percent = MOTOR_LEFT_TRIM;
    } else { // Right Motor
        in_pin_A_pwm_channel = MOTOR_RIGHT_IN3_PWM_CHANNEL; // Connected to MOTOR_RIGHT_IN3_GPIO
        in_pin_B_pwm_channel = MOTOR_RIGHT_IN4_PWM_CHANNEL; // Connected to MOTOR_RIGHT_IN4_GPIO
        trim_percent = MOTOR_RIGHT_TRIM;
    }

    // Apply per-side trim to compensate for unequal motor speeds.
    // The trim only scales DOWN (0-100%), so we cannot exceed full power.
    int trimmed_speed = abs(speed_percent) * constrain(trim_percent, 0, 100) / 100;

    // Calculate PWM duty cycle from trimmed speed percentage
    int pwm_duty = map(trimmed_speed, 0, 100, 0, MAX_MOTOR_PWM_VALUE);
    pwm_duty = constrain(pwm_duty, 0, MAX_MOTOR_PWM_VALUE); // Ensure it's within 0-MAX_MOTOR_PWM_VALUE

    if (speed_percent > 0) { // Forward for this side
        // Pin A gets PWM signal, Pin B is LOW (0% duty cycle)
        ledcWrite(in_pin_A_pwm_channel, pwm_duty);
        ledcWrite(in_pin_B_pwm_channel, 0);
    } else if (speed_percent < 0) { // Backward for this side
        // Pin A is LOW (0% duty cycle), Pin B gets PWM signal
        ledcWrite(in_pin_A_pwm_channel, 0);
        ledcWrite(in_pin_B_pwm_channel, pwm_duty);
    } else { // Stop
        // Both pins LOW (0% duty cycle) - L298N will brake with ENA/ENB HIGH
        ledcWrite(in_pin_A_pwm_channel, 0);
        ledcWrite(in_pin_B_pwm_channel, 0);
    }
}

// ----------------------------
// NVS (Non-Volatile Storage) Helpers
// ----------------------------
void loadTrimsFromNVS() {
  prefs.begin(PREFS_NAMESPACE, true); // read-only
  MOTOR_LEFT_TRIM = prefs.getInt(PREFS_KEY_LEFT_TRIM, MOTOR_TRIM_DEFAULT);
  MOTOR_RIGHT_TRIM = prefs.getInt(PREFS_KEY_RIGHT_TRIM, MOTOR_TRIM_DEFAULT);
  prefs.end();
  // Sanity-clamp in case someone wrote a strange value
  MOTOR_LEFT_TRIM = constrain(MOTOR_LEFT_TRIM, 0, 100);
  MOTOR_RIGHT_TRIM = constrain(MOTOR_RIGHT_TRIM, 0, 100);
  Serial.printf("Loaded trims from NVS: L=%d%% R=%d%%\n",
                MOTOR_LEFT_TRIM, MOTOR_RIGHT_TRIM);
}

void saveTrimsToNVS() {
  prefs.begin(PREFS_NAMESPACE, false); // read-write
  prefs.putInt(PREFS_KEY_LEFT_TRIM, MOTOR_LEFT_TRIM);
  prefs.putInt(PREFS_KEY_RIGHT_TRIM, MOTOR_RIGHT_TRIM);
  prefs.end();
  Serial.printf("Saved trims to NVS: L=%d%% R=%d%%\n",
                MOTOR_LEFT_TRIM, MOTOR_RIGHT_TRIM);
}

void resetTrimsToDefault() {
  MOTOR_LEFT_TRIM = MOTOR_TRIM_DEFAULT;
  MOTOR_RIGHT_TRIM = MOTOR_TRIM_DEFAULT;
  Serial.println("Trims reset to default (100/100). Press Save to persist.");
}

// ----------------------------
// Setup Menu Rendering
// ----------------------------
void drawSetupMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Setup Menu");

  // Show "Saved!" toast briefly after a save
  if (millis() < savedToastUntil) {
    display.setCursor(70, 0);
    display.print("Saved!");
  }

  // Menu items, each on its own line
  // The cursor character ">" marks the active line.
  const int itemY[MENU_ITEM_COUNT] = {16, 26, 40, 50};

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    display.setCursor(0, itemY[i]);
    display.print((i == menuIndex) ? ">" : " ");
    display.setCursor(10, itemY[i]);
    switch (i) {
      case MENU_ITEM_LEFT_TRIM:
        display.print("Left Trim:  ");
        display.print(MOTOR_LEFT_TRIM);
        display.print(" %");
        break;
      case MENU_ITEM_RIGHT_TRIM:
        display.print("Right Trim: ");
        display.print(MOTOR_RIGHT_TRIM);
        display.print(" %");
        break;
      case MENU_ITEM_SAVE:
        display.print("[Save & Exit]");
        break;
      case MENU_ITEM_RESET:
        display.print("[Reset Defaults]");
        break;
    }
  }

  display.display();
}

// ----------------------------
// Setup Menu Enter / Exit
// ----------------------------
void enterSetupMode() {
  // Stop motors immediately - safety first.
  speed_left = 0;
  speed_right = 0;
  controlMotorSide(0, 0);
  controlMotorSide(1, 0);
  currentDriveMode = SETUP_MODE;
  menuIndex = 0;
  savedToastUntil = 0;
  Serial.println("Entered SETUP mode");
  updateDisplayStatus();
}

void exitSetupMode() {
  // Always return to MANUAL after the menu - safer than dropping back into AUTO.
  currentDriveMode = MANUAL_MODE;
  speed_left = 0;
  speed_right = 0;
  controlMotorSide(0, 0);
  controlMotorSide(1, 0);
  Serial.println("Exited SETUP mode -> MANUAL");
  updateDisplayStatus();
}

// ----------------------------
// Setup Menu Input Processing
// ----------------------------
// Buttons:
//   D-Pad Up/Down       -> move cursor between items
//   D-Pad Left/Right    -> change value by 1 (only on numeric items)
//   L1 / R1             -> change value by 5 (only on numeric items)
//   Cross  (a)          -> activate action item (Save / Reset) OR reset value to 100
//   Triangle (y)        -> save & exit (shortcut)
//   Circle (b)          -> cancel: reload values from NVS and exit
//   Options             -> exit without saving (same as Circle but no reload)
void processSetupMenu(ControllerPtr ctl) {
  static bool prevUp = false, prevDown = false, prevLeft = false, prevRight = false;
  static bool prevL1 = false, prevR1 = false;
  static bool prevCross = false, prevTriangle = false, prevCircle = false;

  // D-pad bits in Bluepad32: 0x01=Up, 0x02=Down, 0x04=Right, 0x08=Left
  uint8_t dpad = ctl->dpad();
  bool up    = dpad & 0x01;
  bool down  = dpad & 0x02;
  bool right = dpad & 0x04;
  bool left  = dpad & 0x08;

  bool l1       = ctl->l1();
  bool r1       = ctl->r1();
  bool cross    = ctl->a(); // Cross / X
  bool triangle = ctl->y(); // Triangle
  bool circle   = ctl->b(); // Circle

  // --- Cursor movement (D-pad up/down) ---
  if (up && !prevUp) {
    menuIndex = (menuIndex - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
    updateDisplayStatus();
  }
  if (down && !prevDown) {
    menuIndex = (menuIndex + 1) % MENU_ITEM_COUNT;
    updateDisplayStatus();
  }

  // --- Value adjustment (only on numeric items) ---
  bool isNumeric = (menuIndex == MENU_ITEM_LEFT_TRIM || menuIndex == MENU_ITEM_RIGHT_TRIM);
  int delta = 0;
  if (isNumeric) {
    if (right && !prevRight) delta += 1;
    if (left  && !prevLeft)  delta -= 1;
    if (r1    && !prevR1)    delta += 5;
    if (l1    && !prevL1)    delta -= 5;
  }
  if (delta != 0) {
    int* target = (menuIndex == MENU_ITEM_LEFT_TRIM) ? &MOTOR_LEFT_TRIM : &MOTOR_RIGHT_TRIM;
    *target = constrain(*target + delta, 0, 100);
    updateDisplayStatus();
  }

  // --- Cross: activate action items, or reset numeric to 100 ---
  if (cross && !prevCross) {
    if (menuIndex == MENU_ITEM_SAVE) {
      saveTrimsToNVS();
      savedToastUntil = millis() + 800;
      updateDisplayStatus();
      delay(800); // Brief blocking pause so the user actually sees "Saved!"
      exitSetupMode();
      return;
    } else if (menuIndex == MENU_ITEM_RESET) {
      resetTrimsToDefault();
      updateDisplayStatus();
    } else if (isNumeric) {
      // Quick way to zero out a calibration: Cross on a numeric row resets to 100.
      int* target = (menuIndex == MENU_ITEM_LEFT_TRIM) ? &MOTOR_LEFT_TRIM : &MOTOR_RIGHT_TRIM;
      *target = MOTOR_TRIM_DEFAULT;
      updateDisplayStatus();
    }
  }

  // --- Triangle: save & exit shortcut from anywhere ---
  if (triangle && !prevTriangle) {
    saveTrimsToNVS();
    savedToastUntil = millis() + 800;
    updateDisplayStatus();
    delay(800); // Brief blocking pause so the user actually sees "Saved!"
    exitSetupMode();
    return;
  }

  // --- Circle: cancel - reload values from NVS and exit ---
  if (circle && !prevCircle) {
    loadTrimsFromNVS();
    Serial.println("Setup cancelled - values reloaded from NVS");
    exitSetupMode();
    return;
  }

  // Update previous-button state for edge detection
  prevUp = up; prevDown = down; prevLeft = left; prevRight = right;
  prevL1 = l1; prevR1 = r1;
  prevCross = cross; prevTriangle = triangle; prevCircle = circle;

  // Keep safety timeout happy
  lastCommandTime = millis();
}

// ----------------------------
// HC-SR04 Distance Measurement
// Returns distance in cm, or 0 if no echo (out of range / error)
// ----------------------------
long measureDistanceCm() {
  // Send 10us trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Read echo pulse duration with timeout
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) {
    return 0; // No echo within timeout
  }
  // Speed of sound ~343 m/s -> 29.1 us per cm round trip / 2 = ~58 us per cm
  return (long)(duration / 58);
}

// ----------------------------
// Autonomous Driving Logic
// Drive forward; on obstacle (<20cm) reverse briefly, then turn random direction.
// If after the turn the obstacle is still close, the next loop iteration will
// trigger another reverse + turn automatically.
// ----------------------------
void runAutonomousDriving() {
  static unsigned long lastDistanceCheck = 0;
  unsigned long now = millis();

  // Measure distance periodically (not every loop, to avoid spamming the sensor)
  if (now - lastDistanceCheck >= DISTANCE_INTERVAL_MS) {
    lastDistanceCheck = now;
    long d = measureDistanceCm();
    lastDistanceCm = d;
    updateDisplayStatus();
  }

  switch (autoState) {
    case AUTO_FORWARD:
      // Drive forward, watch for obstacles. Ignore "0" (no echo = nothing close).
      if (lastDistanceCm > 0 && lastDistanceCm < OBSTACLE_DISTANCE_CM) {
        // Obstacle detected -> start reversing
        autoState = AUTO_REVERSE;
        autoStateStartedAt = now;
        speed_left = -AUTO_REVERSE_SPEED;
        speed_right = -AUTO_REVERSE_SPEED;
        controlMotorSide(0, speed_left);
        controlMotorSide(1, speed_right);
        Serial.println("Auto: obstacle detected, reversing");
      } else {
        // Keep driving forward
        speed_left = AUTO_FORWARD_SPEED;
        speed_right = AUTO_FORWARD_SPEED;
        controlMotorSide(0, speed_left);
        controlMotorSide(1, speed_right);
      }
      break;

    case AUTO_REVERSE:
      if (now - autoStateStartedAt >= AUTO_REVERSE_MS) {
        // Done reversing -> pick a random direction and turn
        autoTurnDirection = (random(0, 2) == 0) ? -1 : 1; // -1 left, +1 right
        autoState = AUTO_TURN;
        autoStateStartedAt = now;
        if (autoTurnDirection < 0) {
          // Turn left in place: left wheels backward, right wheels forward
          speed_left = -AUTO_TURN_SPEED;
          speed_right = AUTO_TURN_SPEED;
          Serial.println("Auto: turning left");
        } else {
          // Turn right in place: left forward, right backward
          speed_left = AUTO_TURN_SPEED;
          speed_right = -AUTO_TURN_SPEED;
          Serial.println("Auto: turning right");
        }
        controlMotorSide(0, speed_left);
        controlMotorSide(1, speed_right);
      }
      break;

    case AUTO_TURN:
      if (now - autoStateStartedAt >= AUTO_TURN_MS) {
        // Done turning -> back to forward; next loop will re-check distance
        autoState = AUTO_FORWARD;
        autoStateStartedAt = now;
        // Don't set speeds here; AUTO_FORWARD case handles it on next call
      }
      break;
  }

  // Keep the safety timeout happy while autonomous driving is active
  lastCommandTime = now;
}

// Stop motors and reset autonomous state machine to a clean starting point
void stopAndResetAuto() {
  speed_left = 0;
  speed_right = 0;
  controlMotorSide(0, 0);
  controlMotorSide(1, 0);
  autoState = AUTO_FORWARD;
  autoStateStartedAt = millis();
}

void onConnectedController(ControllerPtr ctl) {
  bool foundEmptySlot = false;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == nullptr) {
      Serial.printf("Controller connected, index=%d\n", i);
      myControllers[i] = ctl;
      foundEmptySlot = true;
      break;
    }
  }
  if (!foundEmptySlot) {
    Serial.println("Controller connected, but no empty slot found");
  }
  lastCommandTime = millis();
  updateDisplayStatus();
}

void onDisconnectedController(ControllerPtr ctl) {
  bool foundController = false;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == ctl) {
      Serial.printf("Controller disconnected, index=%d\n", i);
      myControllers[i] = nullptr;
      foundController = true;
      break;
    }
  }
  if (!foundController) {
    Serial.println("Controller disconnected, but not found in myControllers");
  }

  speed_left = 0;
  speed_right = 0;
  controlMotorSide(0, speed_left); // Stop left motor
  controlMotorSide(1, speed_right); // Stop right motor
  Serial.println("Controller disconnected. Motors stopped.");
  updateDisplayStatus();
}

void processControllers() {
  for (auto ctl : myControllers) {
    if (ctl && ctl->isConnected() && ctl->hasData()) {
      if (ctl->isGamepad()) {
        processGamepad(ctl);
      }
    }
  }
}

void processGamepad(ControllerPtr ctl) {
  // Options button (the small button right of the touchpad on PS4/PS5):
  // toggles into the setup menu from any drive mode. In setup mode it exits
  // back to MANUAL without saving (use Triangle to save, Circle to cancel).
  // Bluepad32 exposes this as MISC_BUTTON_START in miscButtons().
  static bool prevOptions = false;
  bool options = ctl->miscButtons() & MISC_BUTTON_START;
  if (options && !prevOptions) {
    if (currentDriveMode == SETUP_MODE) {
      exitSetupMode();
    } else {
      enterSetupMode();
    }
  }
  prevOptions = options;

  // If we're in the setup menu, all further input goes to the menu handler
  // and the driving logic is skipped entirely.
  if (currentDriveMode == SETUP_MODE) {
    processSetupMenu(ctl);
    return;
  }

  // Square button (PS4/PS5) toggles between manual and autonomous mode.
  // Edge-detection so holding the button doesn't toggle repeatedly.
  static bool prevSquare = false;
  bool square = ctl->x();
  if (square && !prevSquare) {
    if (currentDriveMode == MANUAL_MODE) {
      currentDriveMode = AUTONOMOUS_MODE;
      stopAndResetAuto();
      Serial.println("Switched to AUTONOMOUS mode");
    } else {
      currentDriveMode = MANUAL_MODE;
      stopAndResetAuto();
      Serial.println("Switched to MANUAL mode");
    }
    updateDisplayStatus();
  }
  prevSquare = square;

  // In autonomous mode the sticks are ignored - the sensor drives the robot.
  // We still refresh lastCommandTime so the safety timeout doesn't kick in.
  if (currentDriveMode == AUTONOMOUS_MODE) {
    lastCommandTime = millis();
    return;
  }

  int16_t axisX = ctl->axisX();
  int16_t axisY = ctl->axisY();

  int speed_input = -axisY * 100 / 512;
  int turn_input = axisX * 100 / 512;

  if (abs(speed_input) < DEADZONE) speed_input = 0;
  if (abs(turn_input) < DEADZONE) turn_input = 0;

  speed_input *= direction_multiplier;
  turn_input *= direction_multiplier;

  speed_left = speed_input + turn_input;
  speed_right = speed_input - turn_input;

  speed_left = constrain(speed_left, -100, 100);
  speed_right = constrain(speed_right, -100, 100);

  controlMotorSide(0, speed_left);
  controlMotorSide(1, speed_right);

  lastCommandTime = millis();
  updateDisplayStatus();
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Initializing Robot Car Control (PWM on INx, ENA/ENB Jumpered HIGH)...");

  // Load motor trim calibration from non-volatile storage. If no values were
  // saved before, defaults (100/100) are used. This must happen before any
  // motor commands so the very first movements use the calibrated values.
  loadTrimsFromNVS();

  Serial.println("Initializing OLED Display");

  // Adafruit Display Initialisierung
  // SSD1306_SWITCHCAPVCC = erzeugt die Display-Spannung intern aus 3.3V
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
 
  // NEUE ANPASSUNG: Display um 180 Grad drehen
  display.setRotation(2); // 0 = keine Drehung, 1 = 90°, 2 = 180°, 3 = 270°
 
  // Adafruit GFX Texteinstellungen
  // NEUE ANPASSUNG: Textgröße auf die kleinste Stufe (1) setzen
  display.setTextSize(1);             // Normale 1:1 Pixel-Skalierung
  display.setTextColor(SSD1306_WHITE);        // Weißen Text zeichnen

  updateDisplayStatus(); // Initial display

  Serial.println("Initializing Motor INx pins for PWM control...");
  // Setup PWM channels
  ledcSetup(MOTOR_LEFT_IN1_PWM_CHANNEL, PWM_MOTOR_FREQUENCY, PWM_MOTOR_RESOLUTION);
  ledcSetup(MOTOR_LEFT_IN2_PWM_CHANNEL, PWM_MOTOR_FREQUENCY, PWM_MOTOR_RESOLUTION);
  ledcSetup(MOTOR_RIGHT_IN3_PWM_CHANNEL, PWM_MOTOR_FREQUENCY, PWM_MOTOR_RESOLUTION);
  ledcSetup(MOTOR_RIGHT_IN4_PWM_CHANNEL, PWM_MOTOR_FREQUENCY, PWM_MOTOR_RESOLUTION);

  // Attach PWM channels to the INx GPIO pins
  ledcAttachPin(MOTOR_LEFT_IN1_GPIO, MOTOR_LEFT_IN1_PWM_CHANNEL);
  ledcAttachPin(MOTOR_LEFT_IN2_GPIO, MOTOR_LEFT_IN2_PWM_CHANNEL);
  ledcAttachPin(MOTOR_RIGHT_IN3_GPIO, MOTOR_RIGHT_IN3_PWM_CHANNEL);
  ledcAttachPin(MOTOR_RIGHT_IN4_GPIO, MOTOR_RIGHT_IN4_PWM_CHANNEL);
  Serial.println("Motor INx pins initialized for PWM.");

  // Ensure motors are stopped at startup
  controlMotorSide(0, 0); // Stop left motor
  controlMotorSide(1, 0); // Stop right motor

  // HC-SR04 ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  Serial.println("HC-SR04 ultrasonic sensor initialized.");

  // Seed the random number generator for the random turn direction.
  // Reading an unconnected analog pin gives unpredictable noise.
  randomSeed(analogRead(34));

  Serial.println("Initializing Bluepad32...");
  BP32.setup(&onConnectedController, &onDisconnectedController);
  // BP32.forgetBluetoothKeys(); // Uncomment for development if needed
  BP32.enableVirtualDevice(false);

  // Initialer Bildschirm (angepasst für Adafruit)
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(ROBOT_NAME);    
  display.setCursor(0, 20);
  display.print("Waiting for Controller"); 
  display.display();
  Serial.println("Setup complete. Waiting for controller...");
}

void loop() {
  bool dataUpdated = BP32.update();
  if (dataUpdated) {
    processControllers();
  }

  // Autonomous driving runs independently of controller updates.
  // It uses the HC-SR04 to drive forward and avoid obstacles.
  if (currentDriveMode == AUTONOMOUS_MODE) {
    runAutonomousDriving();
  }

  // Safety: Stop motors if no commands received recently and a controller was/is active
  bool anyControllerEverActive = false;
   for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] != nullptr) { // Check if a slot was ever used or is active
        anyControllerEverActive = true;
        break;
    }
  }
  // Check if a controller is currently connected
  bool controllerCurrentlyConnected = false;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
      if (myControllers[i] && myControllers[i]->isConnected()){
          controllerCurrentlyConnected = true;
          break;
      }
  }

  if (millis() - lastCommandTime > COMMAND_TIMEOUT) {
    // Only stop if motors are running AND (no controller is connected OR a controller is connected but not sending commands)
    if ((speed_left != 0 || speed_right != 0)) {
      if (!controllerCurrentlyConnected || anyControllerEverActive) { // Timeout applies if a controller was expected or is unresponsive
        speed_left = 0;
        speed_right = 0;
        controlMotorSide(0, speed_left);
        controlMotorSide(1, speed_right);
        Serial.println("Command timeout. Motors stopped.");
        updateDisplayStatus();
      }
    }
  }
  delay(1);
}

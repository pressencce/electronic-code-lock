#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <Servo.h>

// --- Константы системы ---
#define PASSWORD_LENGTH 4
#define MAX_ATTEMPTS 3
#define LOCKOUT_TIME 60000     // Время блокировки: 60 сек
#define SERVO_OPEN_TIME 5000   // Время удержания замка: 5 сек

// Карта адресов EEPROM
#define EEPROM_INIT_FLAG_ADDR  0x00
#define EEPROM_USER_PWD_ADDR   0x01
#define EEPROM_MASTER_PWD_ADDR  0x05
#define EEPROM_ATTEMPTS_ADDR    0x09
#define INIT_FLAG_VAL           0x55

#define BUZZER_PIN 11
#define LED_GREEN 12
#define LED_RED 13
#define SERVO_PIN 10

LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4;
const byte COLS = 4;
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {9, 8, 7, 6}; 
byte colPins[COLS] = {5, 4, 3, 2}; 
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

Servo lockServo;

enum SystemState { STATE_WAIT_INPUT, STATE_VERIFY, STATE_ADMIN, STATE_LOCKED };
SystemState currentState = STATE_WAIT_INPUT;

char userPassword[PASSWORD_LENGTH + 1] = "1111"; // Заводской рабочий пароль
char masterPassword[7] = "*#1234";              // Мастер-код
char inputBuffer[8];                            
byte bufferIdx = 0;
byte wrongAttempts = 0;

unsigned long lockoutStartTime = 0;
unsigned long servoOpenStartTime = 0;
bool isLockOpen = false;

// Логирование событий в Serial порт
void logEvent(String level, String message) {
  unsigned long timestamp = millis();
  Serial.print("["); Serial.print(timestamp); Serial.print("] ");
  Serial.print("["); Serial.print(level); Serial.print("] ");
  Serial.println(message);
}

void beep(int duration, int frequency = 1000) {
  tone(BUZZER_PIN, frequency, duration);
}

void showScreenReady() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter Password:");
  lcd.setCursor(0, 1);
  lcd.cursor();
  bufferIdx = 0;
  memset(inputBuffer, 0, sizeof(inputBuffer));
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
}

void initEEPROM() {
  if (EEPROM.read(EEPROM_INIT_FLAG_ADDR) != INIT_FLAG_VAL) {
    logEvent("INFO", "First run. Customizing EEPROM memory structure...");
    EEPROM.write(EEPROM_INIT_FLAG_ADDR, INIT_FLAG_VAL);
    for (int i = 0; i < PASSWORD_LENGTH; i++) {
      EEPROM.write(EEPROM_USER_PWD_ADDR + i, userPassword[i]);
    }
    for (int i = 0; i < 6; i++) {
      EEPROM.write(EEPROM_MASTER_PWD_ADDR + i, masterPassword[i]);
    }
    EEPROM.write(EEPROM_ATTEMPTS_ADDR, 0);
  } else {
    for (int i = 0; i < PASSWORD_LENGTH; i++) {
      userPassword[i] = EEPROM.read(EEPROM_USER_PWD_ADDR + i);
    }
    for (int i = 0; i < 6; i++) {
      masterPassword[i] = EEPROM.read(EEPROM_MASTER_PWD_ADDR + i);
    }
    wrongAttempts = EEPROM.read(EEPROM_ATTEMPTS_ADDR);
    logEvent("INFO", "Configuration successfully read from internal EEPROM.");
  }
  userPassword[PASSWORD_LENGTH] = '\0';
  masterPassword[6] = '\0';
}

void setup() {
  Serial.begin(9600);
  logEvent("SYSTEM", "MCU Core initialized at 16MHz.");
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  
  lockServo.attach(SERVO_PIN);
  lockServo.write(0); // Закрыто
  
  lcd.init();
  lcd.backlight();
  
  initEEPROM();
  
  if (wrongAttempts >= MAX_ATTEMPTS) {
    currentState = STATE_LOCKED;
    lockoutStartTime = millis();
    logEvent("SECURITY", "Hard lockout triggered from EEPROM state.");
  } else {
    showScreenReady();
  }
}

void loop() {
  char key = customKeypad.getKey();
  
  if (isLockOpen && (millis() - servoOpenStartTime >= SERVO_OPEN_TIME)) {
    lockServo.write(0);
    isLockOpen = false;
    digitalWrite(LED_GREEN, LOW);
    logEvent("SYSTEM", "Relay/Servo output state auto-changed to: CLOSED.");
    showScreenReady();
  }

  switch(currentState) {
    case STATE_WAIT_INPUT: {
      if (key) {
        if (key == 'C') { // Сброс буфера ввода
          beep(120, 400);
          logEvent("USER", "Input reset request via 'C' key.");
          showScreenReady();
          break;
        }
        if (key == 'D') { // Подтверждение ввода
          currentState = STATE_VERIFY;
          break;
        }
        if (bufferIdx < 7) {
          inputBuffer[bufferIdx++] = key;
          lcd.print('*'); // Маскирование
          beep(40, 1400);
        }
      }
      break;
    }
    
    case STATE_VERIFY: {
      lcd.noCursor();
      inputBuffer[bufferIdx] = '\0';
      
      if (strcmp(inputBuffer, "*#1234") == 0) { // Вход в меню администратора
        logEvent("SECURITY", "Admin privileges verified successfully.");
        currentState = STATE_ADMIN;
        lcd.clear();
        lcd.print("New Pass:");
        lcd.setCursor(0, 1);
        lcd.cursor();
        bufferIdx = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
        beep(300, 1800);
      } 
      else if (strcmp(inputBuffer, userPassword) == 0) { // Пароль верный
        logEvent("SECURITY", "Access GRANTED.");
        wrongAttempts = 0;
        EEPROM.update(EEPROM_ATTEMPTS_ADDR, 0);
        
        lcd.clear();
        lcd.print("Access Granted");
        lcd.setCursor(0, 1);
        lcd.print("Lock Opened");
        
        digitalWrite(LED_GREEN, HIGH);
        lockServo.write(90);
        isLockOpen = true;
        servoOpenStartTime = millis();
        beep(400, 1200);
        currentState = STATE_WAIT_INPUT;
      } 
      else { // Ошибка ввода
        wrongAttempts++;
        EEPROM.update(EEPROM_ATTEMPTS_ADDR, wrongAttempts);
        logEvent("WARNING", "Authentication failure count: " + String(wrongAttempts));
        
        lcd.clear();
        lcd.print("Wrong Password");
        lcd.setCursor(0, 1);
        lcd.print("Try Again! ["); lcd.print(wrongAttempts); lcd.print("]");
        digitalWrite(LED_RED, HIGH);
        beep(500, 250);
        delay(1500);
        
        if (wrongAttempts >= MAX_ATTEMPTS) {
          logEvent("SECURITY", "Device state set to CRITICAL LOCKOUT.");
          currentState = STATE_LOCKED;
          lockoutStartTime = millis();
        } else {
          currentState = STATE_WAIT_INPUT;
          showScreenReady();
        }
      }
      break;
    }
    
    case STATE_ADMIN: {
      if (key) {
        if (key == 'C') {
          logEvent("USER", "Admin session destroyed.");
          currentState = STATE_WAIT_INPUT;
          showScreenReady();
          break;
        }
        if (key == 'D') {
          if (bufferIdx == PASSWORD_LENGTH) { // Валидация длины (4 символа)
            for (int i = 0; i < PASSWORD_LENGTH; i++) {
              userPassword[i] = inputBuffer[i];
              EEPROM.update(EEPROM_USER_PWD_ADDR + i, userPassword[i]);
            }
            logEvent("SYSTEM", "EEPROM records updated with new password entry.");
            lcd.noCursor();
            lcd.clear();
            lcd.print("Password Changed");
            beep(500, 1500);
            delay(2000);
            currentState = STATE_WAIT_INPUT;
            showScreenReady();
          } else {
            logEvent("ERROR", "Data format invalid. Expected: 4 bytes.");
            lcd.noCursor();
            lcd.clear();
            lcd.print("Error: 4 digits!");
            digitalWrite(LED_RED, HIGH);
            beep(600, 300);
            delay(1500);
            lcd.clear();
            lcd.print("New Pass:");
            lcd.setCursor(0, 1);
            lcd.cursor();
            bufferIdx = 0;
            memset(inputBuffer, 0, sizeof(inputBuffer));
            digitalWrite(LED_RED, LOW);
          }
          break;
        }
        if (bufferIdx < PASSWORD_LENGTH) {
          inputBuffer[bufferIdx++] = key;
          lcd.print(key); // Открытый показ при назначении нового пароля
          beep(50, 1300);
        }
      }
      break;
    }
    
    case STATE_LOCKED: {
      lcd.noCursor();
      unsigned long elapsed = millis() - lockoutStartTime;
      
      static unsigned long lastBlink = 0;
      if (millis() - lastBlink >= 500) {
        lastBlink = millis();
        digitalWrite(LED_RED, !digitalRead(LED_RED));
      }
      
      if (elapsed >= LOCKOUT_TIME) {
        logEvent("SECURITY", "Lockout expired. Restoring operations.");
        wrongAttempts = 0;
        EEPROM.update(EEPROM_ATTEMPTS_ADDR, 0);
        currentState = STATE_WAIT_INPUT;
        showScreenReady();
      } else {
        int secondsLeft = (LOCKOUT_TIME - elapsed) / 1000;
        lcd.setCursor(0, 0);
        lcd.print("System Blocked! ");
        lcd.setCursor(0, 1);
        lcd.print("Time Left: ");
        if (secondsLeft < 10) lcd.print("0");
        lcd.print(secondsLeft);
        lcd.print(" s ");
      }
      break;
    }
  }
}

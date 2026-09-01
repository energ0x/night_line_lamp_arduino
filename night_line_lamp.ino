#include <Arduino.h>
#include <IRremote.hpp>

// --- СТРУКТУРИ ДАНИХ ---
struct SpeedConfig {
  uint8_t intervalMs; // інтервал оновлення в мс
  uint8_t stepSize;   // крок зміщення
};

// --- ПІНИ ---
const int pinR = 9;
const int pinG = 10;
const int pinB = 11;
const int ledPins[8] = {1, 2, 3, 4, 5, 6, 7, 8};
const int IR_RECEIVE_PIN = 12;

// --- ПЕРЕЛІК РЕЖИМІВ ---
enum LampMode {
  MODE_NEUTRAL_WHITE = 1, // Кнопка 1 (~4000K)
  MODE_WARM_WHITE    = 2, // Кнопка 2 (~2000K)
  MODE_COLD_WHITE    = 3, // Кнопка 3 (~6000K)
  MODE_COMET         = 4, // Кнопка 4 (Bouncing Comet)
  MODE_RED           = 5, // Кнопка 5
  MODE_GREEN         = 6, // Кнопка 6
  MODE_BLUE          = 7, // Кнопка 7
  MODE_RAINBOW       = 8, // Кнопка 8 (Веселка)
  MODE_COLOR_CYCLE   = 9, // Кнопка 9 (Одночасне переливання кольорів)
  MODE_BREATHE_9     = 0, // Кнопка 0 (Дихання 9 кольорами)
  MODE_CUSTOM_RGB    = 11,// Режим власного RGB після вводу *
  MODE_ADJUST_TEMP   = 12 // Кнопка # (Регулювання температури 1000K..10000K)
};

// --- СТАНИ ПЛАВНИХ ПЕРЕХОДІВ (FADE IN / FADE OUT) ---
enum TransitionState {
  TRANS_IDLE,        // Звичайне світіння (fadeLevel = 255)
  TRANS_FADE_IN,     // Плавне увімкнення світильника (0 -> 255)
  TRANS_FADE_OUT,    // Плавне вимкнення світильника (255 -> 0)
  TRANS_MODE_OUT,    // Фаза 1 перемикання режиму: згасання поточного (255 -> 0)
  TRANS_MODE_IN      // Фаза 2 перемикання режиму: поява нового (0 -> 255)
};

// --- СТАН СВІТИЛЬНИКА ---
bool powerOn = true;
LampMode currentMode = MODE_RAINBOW;
LampMode savedMode = MODE_RAINBOW;
LampMode pendingMode = MODE_RAINBOW;

// Змінні плавного переходу (Fade)
TransitionState transState = TRANS_IDLE;
uint8_t fadeLevel = 255;              // 0..255
unsigned long lastFadeUpdate = 0;
const uint8_t FADE_STEP = 12;         // Крок зміни яскравості
const unsigned long FADE_INTERVAL_MS = 6; // Інтервал кроку (6 мс * ~21 крок ≈ 125 мс)

// Яскравість: рівні 1..8 (від ~10% до 100%)
const uint8_t brightnessLevels[8] = {20, 45, 75, 110, 150, 190, 225, 255};
int brightnessIndex = 7; // За замовчуванням максимальна

// Швидкість анімацій, є 20 рівнів (1 - найповільніше, 20 - дуже швидко)
int animSpeedLevel = 7;

// Температура білого для режиму '#' (0..45 кроків: від 1000K до 10000K з кроком 200K)
// 0 = 1000K, 15 = 4000K, 45 = 10000K
int colorTempStep = 15;

// Кастомний RGB колір (для режиму '*')
uint8_t customR = 49;
uint8_t customG = 110;
uint8_t customB = 27;

// --- СТАН ВВЕДЕННЯ RGB-КОДУ (*) ---
bool isInputtingRGB = false;
uint8_t rgbDigits[9] = {0};
int rgbDigitCount = 0;

// Змінні для пульсації та курсору вводу RGB
int rgbPulseBrightness = 150;
int rgbPulseDirection = 1;
unsigned long lastRgbAnimUpdate = 0;

// --- ЗМІННІ ДЛЯ АНІМАЦІЙ ---
int wavePos = 0;
int cycleHue = 0;
unsigned long lastAnimUpdate = 0;

// Змінні для анімації комети (MODE_COMET)
int cometPos = 0;
int cometDir = 1; // 1 = вправо (0->7), -1 = вліво (7->0)
uint8_t cometTailBrightness[8] = {0};
uint8_t cometTailColor[8][3] = {{0}};
uint8_t cometHue = 0;

// Змінні для режиму дихання 9 кольорами (MODE_BREATHE_9)
int breatheColorIdx = 0;
int breatheBrightness = 0;
int breatheDirection = 1; // 1 = поява (fade in), -1 = згасання (fade out)
const uint8_t breatheColors[9][3] = {
  {255,   0,   0}, // 1. Червоний
  {  0, 255,   0}, // 2. Зелений
  {  0,   0, 255}, // 3. Синій
  {180,   0, 255}, // 4. Фіолетовий
  {255, 230,   0}, // 5. Жовтий
  {  0, 255, 255}, // 6. Циановий
  {255,  50, 150}, // 7. Рожевий
  {255, 255, 255}, // 8. Білий
  {255,  80,   0}  // 9. Оранжевий
};

// Буфер кадру для 8 світлодіодів: [LED_INDEX][0=R, 1=G, 2=B]
uint8_t frame[8][3];

// --- ТАБЛИЦЯ КОМАНД ІЧ-ПУЛЬТА (NEC КОДИ) ---
enum RemoteButton {
  BTN_NONE = -1,
  BTN_0 = 0, BTN_1 = 1, BTN_2 = 2, BTN_3 = 3, BTN_4 = 4,
  BTN_5 = 5, BTN_6 = 6, BTN_7 = 7, BTN_8 = 8, BTN_9 = 9,
  BTN_STAR,
  BTN_HASH,
  BTN_OK,
  BTN_UP,
  BTN_DOWN,
  BTN_LEFT,
  BTN_RIGHT
};

// --- ЗМІННІ ДЛЯ ОБРОБКИ УТРИМАННЯ ТА ПОВТОРІВ КНОПОК ПУЛЬТА ---
RemoteButton currentHeldButton = BTN_NONE;
unsigned long lastButtonPressTime = 0;
unsigned long lastRepeatActionTime = 0;
unsigned long lastSignalTime = 0;
const unsigned long HOLD_DELAY_MS = 300;     // 300 мс затримка до початку довгого натискання
const unsigned long REPEAT_INTERVAL_MS = 100; // Інтервал між повторами при утриманні
const unsigned long RELEASE_TIMEOUT_MS = 250; // Тайм-аут відпускання кнопки

// --- ФУНКЦІЯ ДЕКОДУВАННЯ КНОПКИ ПУЛЬТА ---
RemoteButton decodeButton(uint8_t cmd, uint32_t raw) {
  // 1. Основна розкладка стандартного пульта Arduino Kit (NEC)
  switch (cmd) {
    case 0x45: return BTN_1;
    case 0x46: return BTN_2;
    case 0x47: return BTN_3;
    case 0x44: return BTN_4;
    case 0x40: return BTN_5;
    case 0x43: return BTN_6;
    case 0x07: return BTN_7;
    case 0x15: return BTN_8;
    case 0x09: return BTN_9;
    case 0x19: return BTN_0;
    case 0x16: return BTN_STAR;
    case 0x0D: return BTN_HASH;
    case 0x1C: return BTN_OK;
    case 0x18: return BTN_UP;
    case 0x52: return BTN_DOWN;
    case 0x08: return BTN_LEFT;
    case 0x5A: return BTN_RIGHT;
  }

  // 2. Резервний пошук за 32-бітними кодами
  switch (raw) {
    case 0xFFA25D: return BTN_1;
    case 0xFF629D: return BTN_2;
    case 0xFFE21D: return BTN_3;
    case 0xFF22DD: return BTN_4;
    case 0xFF02FD: return BTN_5;
    case 0xFFC23D: return BTN_6;
    case 0xFFE01F: return BTN_7;
    case 0xFFA857: return BTN_8;
    case 0xFF906F: return BTN_9;
    case 0xFF9867: return BTN_0;
    case 0xFF6897: return BTN_STAR;
    case 0xFFB04F: return BTN_HASH;
    case 0xFF38C7: return BTN_OK;
    case 0xFF18E7: return BTN_UP;
    case 0xFF4AB5: return BTN_DOWN;
    case 0xFF10EF: return BTN_LEFT;
    case 0xFF5AA5: return BTN_RIGHT;
  }

  return BTN_NONE;
}

// --- ДОПОМІЖНІ КОЛІРНІ ФУНКЦІЇ ---

// Колірне коло (0..255)
void getWheelColor(uint8_t wheelPos, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (wheelPos < 85) {
    r = 255 - wheelPos * 3;
    g = wheelPos * 3;
    b = 0;
  } else if (wheelPos < 170) {
    wheelPos -= 85;
    r = 0;
    g = 255 - wheelPos * 3;
    b = wheelPos * 3;
  } else {
    wheelPos -= 170;
    r = wheelPos * 3;
    g = 0;
    b = 255 - wheelPos * 3;
  }
}

// Отримання RGB для температури білого (step 0..45, від 1000K до 10000K з кроком 200K)
void getTempColor(int step, uint8_t &r, uint8_t &g, uint8_t &b) {
  step = constrain(step, 0, 45);
  uint16_t kelvin = 1000 + (uint16_t)step * 200; // 1000..10000 K

  // Опорні калібровані точки температури світла (Кельвіни -> RGB)
  struct TempPoint {
    uint16_t k;
    uint8_t r, g, b;
  };

  const TempPoint points[] = {
    { 1000, 255,  35,   0 }, // 1000K
    { 1500, 255,  75,   0 }, // 1500K
    { 2000, 255, 115,  20 }, // 2000K
    { 2700, 255, 155,  65 }, // 2700K
    { 3500, 255, 190, 115 }, // 3500K
    { 4000, 255, 215, 160 }, // 4000K
    { 5000, 255, 240, 215 }, // 5000K
    { 6500, 220, 235, 255 }, // 6500K
    { 8000, 180, 210, 255 }, // 8000K
    {10000, 140, 180, 255 }  // 10000K
  };

  const int numPoints = sizeof(points) / sizeof(points[0]);

  if (kelvin <= points[0].k) {
    r = points[0].r; g = points[0].g; b = points[0].b;
    return;
  }
  if (kelvin >= points[numPoints - 1].k) {
    r = points[numPoints - 1].r;
    g = points[numPoints - 1].g;
    b = points[numPoints - 1].b;
    return;
  }

  for (int i = 0; i < numPoints - 1; i++) {
    if (kelvin >= points[i].k && kelvin <= points[i + 1].k) {
      long frac = kelvin - points[i].k;
      long range = points[i + 1].k - points[i].k;
      r = points[i].r + (long)(points[i + 1].r - points[i].r) * frac / range;
      g = points[i].g + (long)(points[i + 1].g - points[i].g) * frac / range;
      b = points[i].b + (long)(points[i + 1].b - points[i].b) * frac / range;
      return;
    }
  }
}

// Отримання конфігурації швидкості для 20 рівнів
SpeedConfig getSpeedConfig(int level) {
  level = constrain(level, 1, 20);

  const SpeedConfig speedTable[20] = {
    { 120, 1 }, // Рівень 1  (дуже повільно)
    {  90, 1 }, // Рівень 2
    {  70, 1 }, // Рівень 3
    {  50, 1 }, // Рівень 4
    {  35, 1 }, // Рівень 5
    {  25, 1 }, // Рівень 6
    {  18, 1 }, // Рівень 7
    {  14, 1 }, // Рівень 8
    {  10, 1 }, // Рівень 9
    {   8, 1 }, // Рівень 10
    {   6, 1 }, // Рівень 11
    {   4, 1 }, // Рівень 12
    {   3, 1 }, // Рівень 13
    {   2, 1 }, // Рівень 14
    {   1, 1 }, // Рівень 15
    {   1, 2 }, // Рівень 16
    {   1, 3 }, // Рівень 17
    {   1, 4 }, // Рівень 18
    {   1, 6 }, // Рівень 19
    {   1, 8 }  // Рівень 20 (дуже швидко)
  };

  return speedTable[level - 1];
}

// Розрахунок формованого кольору для живого прев'ю при введенні цифр (1..9)
void calculatePreviewRGB(uint8_t count, const uint8_t digits[9], uint8_t &r, uint8_t &g, uint8_t &b) {
  r = 0; g = 0; b = 0;
  if (count == 0) return;

  // 1. Формування Red (цифри 0..2)
  if (count == 1) {
    r = constrain((uint16_t)digits[0] * 100, 0, 255);
  } else if (count == 2) {
    r = constrain((uint16_t)digits[0] * 100 + (uint16_t)digits[1] * 10, 0, 255);
  } else {
    r = constrain((uint16_t)digits[0] * 100 + (uint16_t)digits[1] * 10 + digits[2], 0, 255);
  }

  // 2. Формування Green (цифри 3..5)
  if (count == 4) {
    g = constrain((uint16_t)digits[3] * 100, 0, 255);
  } else if (count == 5) {
    g = constrain((uint16_t)digits[3] * 100 + (uint16_t)digits[4] * 10, 0, 255);
  } else if (count >= 6) {
    g = constrain((uint16_t)digits[3] * 100 + (uint16_t)digits[4] * 10 + digits[5], 0, 255);
  }

  // 3. Формування Blue (цифри 6..8)
  if (count == 7) {
    b = constrain((uint16_t)digits[6] * 100, 0, 255);
  } else if (count == 8) {
    b = constrain((uint16_t)digits[6] * 100 + (uint16_t)digits[7] * 10, 0, 255);
  } else if (count >= 9) {
    b = constrain((uint16_t)digits[6] * 100 + (uint16_t)digits[7] * 10 + digits[8], 0, 255);
  }
}

// Заповнити всі 8 світлодіодів одним кольором
void setAllLEDs(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < 8; i++) {
    frame[i][0] = r;
    frame[i][1] = g;
    frame[i][2] = b;
  }
}

// Запит на плавне перемикання режиму (Fade Out -> Fade In)
void requestModeChange(LampMode newMode) {
  if (!powerOn) {
    savedMode = newMode;
    return;
  }
  if (currentMode == newMode && transState == TRANS_IDLE) {
    return;
  }

  pendingMode = newMode;
  transState = TRANS_MODE_OUT;
  lastFadeUpdate = millis();
}

// --- ОБРОБКА КОМАНД ПУЛЬТА ---
void handleRemoteButton(RemoteButton btn) {
  if (btn == BTN_NONE) return;

  // 1. Якщо активний режим введення RGB-коду через '*'
  if (isInputtingRGB) {
    if (btn >= BTN_0 && btn <= BTN_9) {
      if (rgbDigitCount < 9) {
        rgbDigits[rgbDigitCount++] = (uint8_t)btn;
      }
    } else if (btn == BTN_HASH) {
      // Кнопка '#' — стирання останньої введеної цифри
      if (rgbDigitCount > 0) {
        rgbDigitCount--;
        rgbDigits[rgbDigitCount] = 0;
      }
    } else if ((btn == BTN_STAR || btn == BTN_OK) && rgbDigitCount == 9) {
      // Підтвердження введення 9 цифр кнопкою '*' або 'OK'
      uint8_t r, g, b;
      calculatePreviewRGB(9, rgbDigits, r, g, b);

      customR = r;
      customG = g;
      customB = b;

      isInputtingRGB = false;
      rgbDigitCount = 0;
      requestModeChange(MODE_CUSTOM_RGB);
    } else {
      // Натиснуто будь-яку іншу кнопку -> скасування вводу
      isInputtingRGB = false;
      rgbDigitCount = 0;
    }
    return;
  }

  // 2. Кнопка OK: Увімкнення / Вимкнення нічника з плавним Fade In / Fade Out
  if (btn == BTN_OK) {
    if (powerOn) {
      // Плавне вимкнення
      if (transState != TRANS_FADE_OUT) {
        savedMode = (transState == TRANS_MODE_OUT || transState == TRANS_MODE_IN) ? pendingMode : currentMode;
        transState = TRANS_FADE_OUT;
        lastFadeUpdate = millis();
      }
    } else {
      // Плавне увімкнення
      powerOn = true;
      currentMode = savedMode;
      fadeLevel = 0;
      transState = TRANS_FADE_IN;
      lastFadeUpdate = millis();
    }
    return;
  }

  // Якщо світильник вимкнений - інші кнопки не обробляються
  if (!powerOn) return;

  // 3. Регулювання яскравості (▲ / ▼)
  if (btn == BTN_UP) {
    if (brightnessIndex < 7) {
      brightnessIndex++;
    }
    return;
  }
  if (btn == BTN_DOWN) {
    if (brightnessIndex > 0) {
      brightnessIndex--;
    }
    return;
  }

  // 4. Регулювання швидкості (◄ / ►, 1..20) для анімацій (4, 8, 9, 0), або температури (0..45) для режиму '#'
  if (btn == BTN_LEFT) {
    if (currentMode == MODE_ADJUST_TEMP) {
      if (colorTempStep > 0) colorTempStep--;
    } else if (currentMode == MODE_COMET || currentMode == MODE_RAINBOW || currentMode == MODE_COLOR_CYCLE || currentMode == MODE_BREATHE_9) {
      if (animSpeedLevel > 1) animSpeedLevel--;
    }
    return;
  }
  if (btn == BTN_RIGHT) {
    if (currentMode == MODE_ADJUST_TEMP) {
      if (colorTempStep < 45) colorTempStep++;
    } else if (currentMode == MODE_COMET || currentMode == MODE_RAINBOW || currentMode == MODE_COLOR_CYCLE || currentMode == MODE_BREATHE_9) {
      if (animSpeedLevel < 20) animSpeedLevel++;
    }
    return;
  }

  // 5. Вхід у режим введення користувацького RGB (*)
  if (btn == BTN_STAR) {
    isInputtingRGB = true;
    rgbDigitCount = 0;
    rgbPulseBrightness = 150;
    rgbPulseDirection = 1;
    lastRgbAnimUpdate = millis();
    return;
  }

  // 6. Плавне перемикання режимів 1..9, 0, #
  switch (btn) {
    case BTN_1: requestModeChange(MODE_NEUTRAL_WHITE); break;
    case BTN_2: requestModeChange(MODE_WARM_WHITE);    break;
    case BTN_3: requestModeChange(MODE_COLD_WHITE);    break;
    case BTN_4: requestModeChange(MODE_COMET);         break;
    case BTN_5: requestModeChange(MODE_RED);            break;
    case BTN_6: requestModeChange(MODE_GREEN);          break;
    case BTN_7: requestModeChange(MODE_BLUE);           break;
    case BTN_8: requestModeChange(MODE_RAINBOW);        break;
    case BTN_9: requestModeChange(MODE_COLOR_CYCLE);    break;
    case BTN_0: requestModeChange(MODE_BREATHE_9);      break;
    case BTN_HASH: requestModeChange(MODE_ADJUST_TEMP); break;
    default: break;
  }
}

// --- ОНОВЛЕННЯ ПЛАВНИХ ПЕРЕХОДІВ (FADE IN / FADE OUT) ---
void updateTransitions() {
  if (transState == TRANS_IDLE) return;

  unsigned long now = millis();
  if (now - lastFadeUpdate >= FADE_INTERVAL_MS) {
    lastFadeUpdate = now;

    switch (transState) {
      case TRANS_FADE_IN:
        if ((int)fadeLevel + FADE_STEP < 255) {
          fadeLevel += FADE_STEP;
        } else {
          fadeLevel = 255;
          transState = TRANS_IDLE;
        }
        break;

      case TRANS_FADE_OUT:
        if (fadeLevel > FADE_STEP) {
          fadeLevel -= FADE_STEP;
        } else {
          fadeLevel = 0;
          powerOn = false;
          transState = TRANS_IDLE;
        }
        break;

      case TRANS_MODE_OUT:
        if (fadeLevel > FADE_STEP) {
          fadeLevel -= FADE_STEP;
        } else {
          fadeLevel = 0;
          currentMode = pendingMode;
          if (currentMode == MODE_BREATHE_9) {
            breatheBrightness = 0;
            breatheDirection = 1;
            breatheColorIdx = 0;
          } else if (currentMode == MODE_COMET) {
            cometPos = 0;
            cometDir = 1;
            for (int i = 0; i < 8; i++) {
              cometTailBrightness[i] = 0;
              cometTailColor[i][0] = 0;
              cometTailColor[i][1] = 0;
              cometTailColor[i][2] = 0;
            }
          }
          transState = TRANS_MODE_IN;
        }
        break;

      case TRANS_MODE_IN:
        if ((int)fadeLevel + FADE_STEP < 255) {
          fadeLevel += FADE_STEP;
        } else {
          fadeLevel = 255;
          transState = TRANS_IDLE;
        }
        break;

      default:
        break;
    }
  }
}

// --- ОНОВЛЕННЯ ЛОГІКИ АНІМАЦІЙ ТА РЕЖИМІВ ---
void updateLampState() {
  if (!powerOn && transState == TRANS_IDLE) {
    setAllLEDs(0, 0, 0);
    return;
  }

  // 1. Інтерактивна анімація вибору кольору (*)
  if (isInputtingRGB) {
    // Плавний таймер пульсації (кожні 8 мс)
    if (millis() - lastRgbAnimUpdate >= 8) {
      lastRgbAnimUpdate = millis();
      rgbPulseBrightness += rgbPulseDirection * 5;
      if (rgbPulseBrightness >= 255) {
        rgbPulseBrightness = 255;
        rgbPulseDirection = -1;
      } else if (rgbPulseBrightness <= 30) {
        rgbPulseBrightness = 30;
        rgbPulseDirection = 1;
      }
    }

    if (rgbDigitCount == 0) {
      // 0 цифр: перший діод плавно дихає як текстовий курсор
      frame[0][0] = rgbPulseBrightness;
      frame[0][1] = ((uint16_t)rgbPulseBrightness * 180) / 255;
      frame[0][2] = ((uint16_t)rgbPulseBrightness * 100) / 255;

      for (int i = 1; i < 8; i++) {
        frame[i][0] = 0; frame[i][1] = 0; frame[i][2] = 0;
      }
    } else if (rgbDigitCount < 9) {
      // 1..8 цифр: прогрес-бар з живим прев'ю кольору
      uint8_t curR, curG, curB;
      calculatePreviewRGB(rgbDigitCount, rgbDigits, curR, curG, curB);

      // Якщо колір поки що повністю темний (всі 0), показуємо помітну тьмяну білу крапку
      if (curR == 0 && curG == 0 && curB == 0) {
        curR = 25; curG = 25; curB = 25;
      }

      for (int i = 0; i < 8; i++) {
        if (i < rgbDigitCount) {
          frame[i][0] = curR;
          frame[i][1] = curG;
          frame[i][2] = curB;
        } else {
          frame[i][0] = 0;
          frame[i][1] = 0;
          frame[i][2] = 0;
        }
      }
    } else {
      // 9 цифр = повністю сформований колір. Всі діоди плавно пульсують
      uint8_t finalR, finalG, finalB;
      calculatePreviewRGB(9, rgbDigits, finalR, finalG, finalB);

      if (finalR == 0 && finalG == 0 && finalB == 0) {
        finalR = 30; finalG = 30; finalB = 30;
      }

      uint8_t r = ((uint16_t)finalR * rgbPulseBrightness) / 255;
      uint8_t g = ((uint16_t)finalG * rgbPulseBrightness) / 255;
      uint8_t b = ((uint16_t)finalB * rgbPulseBrightness) / 255;

      setAllLEDs(r, g, b);
    }
    return;
  }

  // 2. Формування кадру для поточного режиму
  switch (currentMode) {
    case MODE_NEUTRAL_WHITE: // ~4000K
      setAllLEDs(255, 215, 160);
      break;

    case MODE_WARM_WHITE: // ~2000K
      setAllLEDs(255, 115, 20);
      break;

    case MODE_COLD_WHITE: // ~6000K
      setAllLEDs(220, 235, 255);
      break;

    case MODE_COMET: { // Режим 4, анімація комети
      unsigned long cometInterval = map(animSpeedLevel, 1, 20, 200, 25);

      if (millis() - lastAnimUpdate >= cometInterval) {
        lastAnimUpdate = millis();

        // 1. Швидке згасання для формування короткого хвоста (~1-2 діоди)
        uint8_t decay = 75;
        for (int i = 0; i < 8; i++) {
          if (cometTailBrightness[i] > decay) {
            cometTailBrightness[i] -= decay;
          } else {
            cometTailBrightness[i] = 0;
          }
        }

        // 2. Запалюємо діод у поточній позиції комети на 100%
        uint8_t headR, headG, headB;
        getWheelColor(cometHue, headR, headG, headB);

        cometTailBrightness[cometPos] = 255;
        cometTailColor[cometPos][0] = headR;
        cometTailColor[cometPos][1] = headG;
        cometTailColor[cometPos][2] = headB;

        // 3. Рух та відбивання від країв (0 та 7)
        cometPos += cometDir;
        if (cometPos >= 7) {
          cometPos = 7;
          cometDir = -1; // Відбивання від правого краю -> розвертаємось вліво
          cometHue = (cometHue + 28) % 256; // Зміна кольору при ударі
        } else if (cometPos <= 0) {
          cometPos = 0;
          cometDir = 1;  // Відбивання від лівого краю -> розвертаємось вправо
          cometHue = (cometHue + 28) % 256; // Зміна кольору при ударі
        }
      }

      // 4. Формування вихідного кадру
      for (int i = 0; i < 8; i++) {
        if (i == cometPos) {
          // Голова комети (яскраве ядро)
          uint8_t headR, headG, headB;
          getWheelColor(cometHue, headR, headG, headB);
          frame[i][0] = min(255, (int)headR + 50);
          frame[i][1] = min(255, (int)headG + 50);
          frame[i][2] = min(255, (int)headB + 50);
        } else {
          // Короткий згасаючий хвіст
          frame[i][0] = ((uint16_t)cometTailColor[i][0] * cometTailBrightness[i]) / 255;
          frame[i][1] = ((uint16_t)cometTailColor[i][1] * cometTailBrightness[i]) / 255;
          frame[i][2] = ((uint16_t)cometTailColor[i][2] * cometTailBrightness[i]) / 255;
        }
      }
      break;
    }

    case MODE_RED:
      setAllLEDs(255, 0, 0);
      break;

    case MODE_GREEN:
      setAllLEDs(0, 255, 0);
      break;

    case MODE_BLUE:
      setAllLEDs(0, 0, 255);
      break;

    case MODE_CUSTOM_RGB:
      setAllLEDs(customR, customG, customB);
      break;

    case MODE_ADJUST_TEMP: { // Режим # (1000K..10000K)
      uint8_t r, g, b;
      getTempColor(colorTempStep, r, g, b);
      setAllLEDs(r, g, b);
      break;
    }

    case MODE_RAINBOW: { // Режим 8, веселка
      SpeedConfig sc = getSpeedConfig(animSpeedLevel);
      if (millis() - lastAnimUpdate >= sc.intervalMs) {
        lastAnimUpdate = millis();
        wavePos = (wavePos + sc.stepSize) % 256;
      }
      for (int i = 0; i < 8; i++) {
        uint8_t c = (wavePos + i * 32) % 256;
        uint8_t r, g, b;
        getWheelColor(c, r, g, b);
        frame[i][0] = r;
        frame[i][1] = g;
        frame[i][2] = b;
      }
      break;
    }

    case MODE_COLOR_CYCLE: { // Режим 9, одночасне переливання всіх діодів
      SpeedConfig sc = getSpeedConfig(animSpeedLevel);
      if (millis() - lastAnimUpdate >= sc.intervalMs) {
        lastAnimUpdate = millis();
        cycleHue = (cycleHue + sc.stepSize) % 256;
      }
      uint8_t r, g, b;
      getWheelColor(cycleHue, r, g, b);
      setAllLEDs(r, g, b);
      break;
    }

    case MODE_BREATHE_9: { // Режим 0, плавне дихання 9 кольорами
      SpeedConfig sc = getSpeedConfig(animSpeedLevel);
      uint8_t breatheInterval = max(1, sc.intervalMs / 3);
      uint8_t breatheStep = (sc.stepSize == 1) ? 2 : (sc.stepSize * 2);

      if (millis() - lastAnimUpdate >= breatheInterval) {
        lastAnimUpdate = millis();
        breatheBrightness += (int)breatheDirection * breatheStep;

        if (breatheBrightness >= 255) {
          breatheBrightness = 255;
          breatheDirection = -1;
        } else if (breatheBrightness <= 0) {
          breatheBrightness = 0;
          breatheDirection = 1;
          breatheColorIdx = (breatheColorIdx + 1) % 9;
        }
      }

      uint8_t baseR = breatheColors[breatheColorIdx][0];
      uint8_t baseG = breatheColors[breatheColorIdx][1];
      uint8_t baseB = breatheColors[breatheColorIdx][2];

      uint8_t r = ((uint16_t)baseR * breatheBrightness) / 255;
      uint8_t g = ((uint16_t)baseG * breatheBrightness) / 255;
      uint8_t b = ((uint16_t)baseB * breatheBrightness) / 255;

      setAllLEDs(r, g, b);
      break;
    }
  }
}

// --- МУЛЬТИПЛЕКСОВАНИЙ РЕНДЕРИНГ КАДРУ ---
void renderDisplay() {
  uint8_t baseBright = brightnessLevels[brightnessIndex];
  uint8_t effectiveBright = ((uint16_t)baseBright * fadeLevel) / 255;

  for (int i = 0; i < 8; i++) {
    // Якщо світильник повністю вимкнено або ефективна яскравість 0
    if (!powerOn || effectiveBright == 0) {
      digitalWrite(ledPins[i], HIGH);
      digitalWrite(pinR, HIGH);
      digitalWrite(pinG, HIGH);
      digitalWrite(pinB, HIGH);
      continue;
    }

    // Масштабування яскравості з урахуванням fade переходу
    uint8_t r = ((uint16_t)frame[i][0] * effectiveBright) / 255;
    uint8_t g = ((uint16_t)frame[i][1] * effectiveBright) / 255;
    uint8_t b = ((uint16_t)frame[i][2] * effectiveBright) / 255;

    // Активація i-го світлодіода (LOW = увімкнено для модуля зі спільним анодом)
    digitalWrite(ledPins[i], LOW);

    // Початковий стан пінів кольорів
    digitalWrite(pinR, r > 0 ? LOW : HIGH);
    digitalWrite(pinG, g > 0 ? LOW : HIGH);
    digitalWrite(pinB, b > 0 ? LOW : HIGH);

    // Програмний ШІМ цикл (255 кроків)
    for (int pwm = 1; pwm <= 255; pwm++) {
      if (pwm == r) digitalWrite(pinR, HIGH);
      if (pwm == g) digitalWrite(pinG, HIGH);
      if (pwm == b) digitalWrite(pinB, HIGH);
    }

    // Вимикаємо кольори та поточний світлодіод
    digitalWrite(pinR, HIGH);
    digitalWrite(pinG, HIGH);
    digitalWrite(pinB, HIGH);
    digitalWrite(ledPins[i], HIGH);
  }
}

// --- SETUP ---
void setup() {
  // Налаштування пінів кольорів (HIGH = вимкнено)
  pinMode(pinR, OUTPUT);
  pinMode(pinG, OUTPUT);
  pinMode(pinB, OUTPUT);
  digitalWrite(pinR, HIGH);
  digitalWrite(pinG, HIGH);
  digitalWrite(pinB, HIGH);

  // Налаштування 8 пінів світлодіодів (включаючи Pin 1 для 1-го діода)
  for (int i = 0; i < 8; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], HIGH);
  }

  // Ініціалізація ІЧ-приймача без feedback блимання
  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);
}

// --- MAIN LOOP ---
void loop() {
  unsigned long now = millis();

  // 1. Неблокуюча перевірка та обробка сигналів з ІЧ-приймача
  if (IrReceiver.decode()) {
    uint8_t cmd = IrReceiver.decodedIRData.command;
    uint32_t raw = IrReceiver.decodedIRData.decodedRawData;
    bool isRepeat = (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT);

    RemoteButton btn = decodeButton(cmd, raw);
    if (isRepeat && btn == BTN_NONE) {
      btn = currentHeldButton;
    }

    if (btn != BTN_NONE) {
      if ((isRepeat || btn == currentHeldButton) && currentHeldButton != BTN_NONE) {
        // Кнопка утримується
        unsigned long holdDuration = now - lastButtonPressTime;
        if (holdDuration >= HOLD_DELAY_MS) {
          // Якщо тримають довше 300 мс — виконуємо безперервне спрацьовування
          if (now - lastRepeatActionTime >= REPEAT_INTERVAL_MS) {
            lastRepeatActionTime = now;
            handleRemoteButton(currentHeldButton);
          }
        }
        lastSignalTime = now;
      } else {
        // Нове перше натискання кнопки
        currentHeldButton = btn;
        lastButtonPressTime = now;
        lastRepeatActionTime = now;
        lastSignalTime = now;
        handleRemoteButton(btn);
      }
    }

    IrReceiver.resume(); // Продовжити прийом наступних сигналів
  }

  // Якщо сигнал не надходив понад 250 мс — кнопку відпущено
  if (currentHeldButton != BTN_NONE && (now - lastSignalTime > RELEASE_TIMEOUT_MS)) {
    currentHeldButton = BTN_NONE;
  }

  // 2. Оновлення плавних переходів (Fade In / Fade Out)
  updateTransitions();

  // 3. Оновлення поточного стану та анімацій
  updateLampState();

  // 4. Відтворення кадру на світлодіодах
  renderDisplay();
}
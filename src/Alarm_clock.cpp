#include "Arduino.h"
#include "EEPROM.h"
#include "math.h"
#include "DFPlayerMini_Fast.h"
#include "RTClib.h"
#include "GyverTM1637.h"
#include "GyverPower.h"
#include "EncButton.h"

#define FW_VERSION "1.4.6"

// ===== HARDWARE =====

// Пины
#define BTN_1 2 // Pulup подтяжка(к "+" питания), когда нажата возвращает 0
#define BTN_2 10
#define BTN_3 4
#define BTN_4 5
#define CLK 6 // Подключение индикатора TM1637
#define DIO 7 // Подключение индикатора TM1637
#define VIBRO 8
#define MUSIC_MOSFET 9
#define LED 13
#define LIGHT_SENSOR A0
#define VOLTAGE A1

// Создаем объекты устройств
DFPlayerMini_Fast myMP3;
RTC_DS3231 rtc; // RTC DS3231 подключается к SDA – A4, SCL – A5
GyverTM1637 disp(CLK, DIO);

// Создаем объекты кнопок
Button btn1(BTN_1, INPUT_PULLUP, LOW);
Button btn2(BTN_2, INPUT_PULLUP, LOW);
Button btn3(BTN_3, INPUT_PULLUP, LOW);
Button btn4(BTN_4, INPUT_PULLUP, LOW);

// ===== SYSTEM TIMERS =====

uint32_t displayClockTimer;
uint32_t updateTimeTimer;
uint32_t vibroToggleTimer;
uint32_t batteryControlTimer;
uint32_t brightnessControlTimer;
uint32_t unblockMenuTimer;
uint32_t playMusicDisplayOfTimer;

// ===== CLOCK =====

bool clockOn = true;
bool secondsDots = false;

uint16_t year;
uint8_t month;
uint8_t day;
uint8_t hrs;
uint8_t min;
uint8_t sec;

// ===== ALARM =====

bool alarmOn = false; // Определяет включена ли функция будильньика
bool alarmOnDefault = false;
bool alarmSignal = false;
bool alarmMenuState = false;
bool alarmTriggered = false;
bool snoozeActive = false;

uint32_t alarmStartTime;
uint32_t alarmDuration = 60000;

uint8_t alarmHrs;
uint8_t alarmMin;
uint8_t alarmVolume;

uint8_t snoozeCount;
uint32_t snoozeStartTime;
uint32_t snoozeDuration = 300000;

// значения по умолчанию
uint8_t alarmHrsDefault = 12;
uint8_t alarmMinDefault = 0;
uint8_t alarmVolumeDefault = 20;

// ===== COUNTDOWN TIMER =====

bool timerOn = false;
bool timerFinished = false;
bool displayBlinking = false;
bool timerDisplayState = true;

// Значение таймера по умолчанию
uint32_t timerMinuteSet = 00;
uint32_t timerSecondSet = 00;

// ===== SIGNAL DEVICES =====

bool vibroToggle = true;
bool flashLightOn = false;
bool isPlaying = false;

// ===== POWER / BATTERY =====

bool batteryDischarge;

// ===== DISPLAY =====

// Преобразование символа цифры в код сегмента для передачи символа в функцию вывода на дисплей
// для показа версии
#define DIGIT_TO_SEG(x)              \
  ((x) == '0' ? _0 : (x) == '1' ? _1 \
                 : (x) == '2'   ? _2 \
                 : (x) == '3'   ? _3 \
                 : (x) == '4'   ? _4 \
                 : (x) == '5'   ? _5 \
                 : (x) == '6'   ? _6 \
                 : (x) == '7'   ? _7 \
                 : (x) == '8'   ? _8 \
                                : _9)

// Версия прошивки для показа
uint8_t fw_version[] = {
    _F,
    DIGIT_TO_SEG(FW_VERSION[0]),
    DIGIT_TO_SEG(FW_VERSION[2]),
    DIGIT_TO_SEG(FW_VERSION[4])};
// Сообщение "Зарядите батарею"
uint8_t chargeBattery[] = {_C, _H, _A, _r, _G, _E, _empty, _b, _A, _t, _t, _E, _r, _Y};

// ===== MAIN MENU =====
bool buttonsBlocked = false;
bool justWoke = false;
bool justWokeBlockMenu = false;
bool menuActive = false;
bool mainMenuActive = false;
int8_t mainMenuItem = 0;

// ===== SET TIME MENU =====
uint8_t clockMinTmp;
uint8_t clockHrsTmp;
uint8_t clockDate;
uint8_t clockMonth;
int clockYear;
bool setTimeMenuActive = false;
bool setTimeMenuDateTimeUpdate = false;

// ===== ALARM ON/OFF MENU =====
bool alarmOnOffMenuActive = false;

//     BUTTON HOLD CHANGING
uint32_t lastHourChangeTime = 0;
uint32_t lastMinuteChangeTime = 0;
uint16_t INITIAL_DELAY = 500;   // Начальная задержка перед первым изменением
uint16_t CHANGE_INTERVAL = 500; // Интервал между изменениями

// ===== Прототипы функций =====
void initializeClock();
void pinsConfig();
void powerConfig();
void buttonConfig();
void testVibro();
void enterSleepMode();
void wakeUp();
void updateButtons();
void updateDateTime();
void displayClock(byte hrs, byte min);
void onAlarmInterrupt();
void activateAlarm();
void snoozeButton();
void alarmStopButton();
void alarmStopTime();
void runCountDownTimer();
void brightnessControl();
double voltageMeasure();
void showVoltage(double voltage);
void batteryControl(double voltage);
void showTemperature();
void showDate();
void startAlarm();
void stopAlarm();
void startVibro();
void stopVibro();
void menu();
void alarmOnOffMenu();
void setAlarm();
void setTime();
void setVolume();
void playMusicMenu();
void playMusic();
void previousTrack();
void nextTrack();
void stopMusic();
void setVolumeMusic();
void flashLight();

void setup()
{
  initializeClock();
  pinsConfig();
  powerConfig();
  buttonConfig();
  attachInterrupt(1, onAlarmInterrupt, FALLING); // Подключаем прерывания для включения от сигнала DS3231
  testVibro();
  disp.displayByte(fw_version);
  delay(1500);
}

void loop()
{

  if (justWoke)
  {
    unblockMenuTimer = millis();
    justWoke = false;
    detachInterrupt(0); // Отключаем прерывания, которые срабатывают при нажатии кнопки 1
  }

  // Задержка на некоторое время, что-бы после нажатия кнопок отключения будильника
  // не происходил мгновенный вход в меню или активация других функций
  // привязанных к той или иной кнопке
  // Так-же работает при нажатие кнопки для выхода из спящего режима.
  if (millis() - unblockMenuTimer > 250)
  {
    buttonsBlocked = false;
  }

  updateButtons();

  // Данное условие обеспечивает включение звукового сигнала.
  if (alarmTriggered)
  {
    activateAlarm();
    alarmTriggered = false;
  }

  // Если будильник сработал блокируем все функции кнопок.
  // Ждём нажатие кнопок для остановки будильника или для откладывания или остановки по времени.
  if (alarmSignal)
  {
    buttonsBlocked = true;
    alarmStopButton();
    alarmStopTime();
    snoozeButton();
    // После 30 секунд сработки будильника включаем вибрацию.
    if (alarmSignal && (millis() - alarmStartTime > 30000))
    {
      startVibro();
    }
  }

  // Если активировали режим "Вздремнуть" то ждём 5 минут и запускаем будильник снова.
  if (snoozeActive)
  {
    if (millis() - snoozeStartTime > snoozeDuration)
    {
      activateAlarm();
      snoozeStartTime = millis();
      snoozeCount = snoozeCount + 1;
    }
  }

  if (mainMenuActive)
  {
    menu();
  }

  if (setTimeMenuActive)
  {
    setTime();
  }

  if (alarmOnOffMenuActive)
  {
    alarmOnOffMenu();
  }

  // Обновление времени из rtc модуля DS3231
  // Периодичность раз в секунду
  if (millis() - updateTimeTimer > 1000)
  {
    updateDateTime();
    updateTimeTimer = millis();
  }

  // Показ времени на индикаторе
  if (millis() - displayClockTimer > 500)
  {
    if (!menuActive)
    {
      displayClockTimer = millis();
      displayClock(hrs, min);
    }
  }

  // Регулировка яркости
  if (millis() - brightnessControlTimer > 500)
  {
    brightnessControl();
  }

  // Контролируем состояние батареи раз в пять минут
  if (millis() - batteryControlTimer > 300000)
  {
    batteryControl(voltageMeasure());
    batteryControlTimer = millis();
  }

  // Вызов меню настроек времени и будильника (короткое нажатие BTN_1)
  if (btn1.click() && !buttonsBlocked && !menuActive)
  {
    menuActive = true;
    mainMenuActive = true;
  }

  // Показ температуры (встроенный в DS3231 датчик)
  // Показ даты
  if (btn2.click() && !buttonsBlocked && !menuActive)
  {
    showTemperature();
    showDate();
  }

  // Выключение отображения часов и уход в сон (удержание BTN_2)
  if (btn2.hold() && !buttonsBlocked && !menuActive)
  {
    clockOn = false;
  }

  // Показать напряжение батареи (короткое нажатие BTN_3)
  if (btn3.click() && !buttonsBlocked && !menuActive)
  {
    showVoltage(voltageMeasure());
  }

  // Включение фонарика (короткое нажатие BTN_4)
  if (btn4.click() && !buttonsBlocked && !menuActive)
  {
    flashLight();
  }

  // Проверка условий для засыпания. Если выполнено, то засыпаем.
  enterSleepMode();
}

// Инициализация устройств и загрузка настроек
void initializeClock()
{
  Serial.begin(9600);
  if (EEPROM.read(0) == 255)
  {                                    // 255 это значение записанное в ячейке EEPROM по умолчанию в ардуино
    EEPROM.update(0, alarmHrsDefault); // Обновляем значение ячейки на дефолтное время срабатывания будильника
  }
  if (EEPROM.read(1) == 255)
  {
    EEPROM.update(1, alarmMinDefault);
  }
  if (EEPROM.read(2) == 255)
  {
    EEPROM.update(2, alarmOnDefault);
  }
  if (EEPROM.read(3) == 255)
  {
    EEPROM.update(3, alarmVolumeDefault);
  }
  rtc.begin();
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Раскоментировать строку для установки времени часов с компьютера
  rtc.clearAlarm(1);
  alarmHrs = EEPROM.read(0);
  alarmMin = EEPROM.read(1);
  alarmOn = EEPROM.read(2);
  alarmVolume = EEPROM.read(3);
  alarmMenuState = alarmOn;
  myMP3.begin(Serial, true);
  disp.clear();
  disp.brightness(7); // Яркость, 0 - 7 (минимум - максимум)
  updateDateTime();
  randomSeed(analogRead(A3)); // Считать значения (наводки) на неподключеном входе А3
                              // для формирования рандомной последовательности чисел
}

// Конфигурация пинов.
void pinsConfig()
{
  // Пины кнопок настраиваются автоматически в конструкторах EncButton
  pinMode(VIBRO, OUTPUT);
  pinMode(MUSIC_MOSFET, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(LIGHT_SENSOR, INPUT);
  pinMode(VOLTAGE, INPUT);
}

// Настройка энергосбережения
void powerConfig()
{
  power.autoCalibrate();                                    // автоматическая калибровка
  power.hardwareDisable(PWR_SPI | PWR_TIMER1 | PWR_TIMER2); // отключаем неиспользуемые модули МК
  power.setSleepMode(POWERDOWN_SLEEP);                      // устанавливаем режим сна в который будем уходить
}

// Настройка кнопок
void buttonConfig()
{

  // Настраиваем кнопки (таймаут удержания 3 секунды для длинного нажатия)
  btn1.setHoldTimeout(2000);
  btn2.setHoldTimeout(2000);
  btn3.setHoldTimeout(2000);
  btn4.setHoldTimeout(2000);

  // Устанавливаем короткий таймаут антидребезга для быстрого реагирования
  btn1.setDebTimeout(30); // 30 мс вместо стандартных 50 мс
  btn2.setDebTimeout(30);
  btn3.setDebTimeout(30);
  btn4.setDebTimeout(30);

  // Устанавливаем короткий таймаут ожидания кликов (для быстрых повторных нажатий)
  btn1.setClickTimeout(300); // 150 мс вместо стандартных 500 мс
  btn2.setClickTimeout(300);
  btn3.setClickTimeout(300);
  btn4.setClickTimeout(300);
}

// Тест вибрации
void testVibro()
{
  digitalWrite(VIBRO, HIGH);
  delay(200);
  digitalWrite(VIBRO, LOW);
  delay(100);
  digitalWrite(VIBRO, HIGH);
  delay(100);
  digitalWrite(VIBRO, LOW);
}

// Вход в режим энергосбережения
void enterSleepMode()
{
  if (!clockOn && !alarmSignal && !isPlaying && !flashLightOn && !snoozeActive)
  {
    disp.clear();
    disp.point(false);
    buttonsBlocked = true;
    attachInterrupt(0, wakeUp, FALLING); // Включаем прерывания для включения от кнопки
    delay(1000);
    power.sleep(SLEEP_FOREVER);
  }
}

// Обработчик прерывания от кнопки 1
void wakeUp()
{
  clockOn = true;
  justWoke = true;
}

// Получение даты и времени из rtc DS3231
void updateDateTime()
{
  DateTime now = rtc.now();
  year = now.year();
  month = now.month();
  day = now.day();
  hrs = now.hour();
  min = now.minute();
  sec = now.second();
}

// Опрос кнопок
void updateButtons()
{
  btn1.tick();
  btn2.tick();
  btn3.tick();
  btn4.tick();
}

// Отображение времени на семисегметном индикаторе
void displayClock(byte hrs, byte min)
{
  if (clockOn)
  {
    disp.displayClock(hrs, min); // Выводим время функцией часов
    secondsDots = !secondsDots;
    disp.point(secondsDots); // Вкл/выкл точки
  }
  if (!clockOn)
  {
    disp.clear();
    disp.point(false);
  }
}

// Обработчик прерывания от будильника RTC DS3231
void onAlarmInterrupt()
{
  snoozeCount = 0;
  alarmTriggered = true;
}

// Будильник
void activateAlarm()
{
  if (alarmOn && !alarmSignal)
  {
    alarmSignal = true;
    clockOn = true;
    alarmStartTime = millis();
    startAlarm();
  }
}

// Установка таймера
void setTimer()
{
  int timerMinuteSetTmp = timerMinuteSet;
  int timerSecondSetTmp = timerSecondSet;
  uint32_t lastMinuteChangeTime = 0;
  const uint16_t INITIAL_DELAY = 500;   // Начальная задержка перед первым изменением
  const uint16_t CHANGE_INTERVAL = 500; // Интервал между изменениями

  while (true)
  {
    uint32_t currentTime = millis();

    updateButtons();

    // Уменьшение минут при клике кнопки 3 (однократное нажатие)
    if (btn3.click())
    {
      timerMinuteSetTmp = timerMinuteSetTmp - 1;
      if (timerMinuteSetTmp < 0)
      {
        timerMinuteSetTmp = 99;
      }
    }
    // Уменьшение минут при удержании кнопки 3
    if (btn3.pressing())
    {
      uint16_t pressDuration = btn3.pressFor();
      if (pressDuration >= INITIAL_DELAY)
      {
        if (lastMinuteChangeTime == 0 || (currentTime - lastMinuteChangeTime >= CHANGE_INTERVAL))
        {
          timerMinuteSetTmp = timerMinuteSetTmp - 1;
          if (timerMinuteSetTmp < 0)
          {
            timerMinuteSetTmp = 99;
          }
          lastMinuteChangeTime = currentTime;
        }
      }
    }
    // Увеличение минут при клике кнопки 4 (однократное нажатие)
    if (btn4.click())
    {
      timerMinuteSetTmp = (timerMinuteSetTmp + 1) % 99;
    }
    // Увеличение минут при удержании кнопки 4
    if (btn4.pressing())
    {
      uint16_t pressDuration = btn4.pressFor();
      if (pressDuration >= INITIAL_DELAY)
      {
        if (lastMinuteChangeTime == 0 || (currentTime - lastMinuteChangeTime >= CHANGE_INTERVAL))
        {
          timerMinuteSetTmp = (timerMinuteSetTmp + 1) % 99;
          lastMinuteChangeTime = currentTime;
        }
      }
    }
    // Подтверждение установки
    if (btn2.click())
    {
      timerMinuteSet = timerMinuteSetTmp;
      timerSecondSet = timerSecondSetTmp;
      timerOn = true;
      disp.point(false);
      disp.displayByte(_S, _t, _r, _t);
      delay(500);
      runCountDownTimer();
      break;
    }
    if (btn1.click()) // Выход из меню
    {
      disp.point(false);
      disp.displayByte(_E, _S, _C, _empty);
      delay(1000);
      break;
    }
    // Отображение текущего времени будильника
    disp.displayClock(timerMinuteSetTmp, timerSecondSetTmp);
    disp.point(true);
    delay(50);
  }
}

// Таймер
void runCountDownTimer()
{
  uint32_t timerStartValue = (timerMinuteSet * 60UL + timerSecondSet) * 1000UL; // начальное значение таймера в миллисекундах
  uint32_t timerCurrentRemain = timerStartValue;                                // оставшееся время
  uint32_t timerPrevUpdate = millis();                                          // время последнего обновления
  uint32_t displayToggleTimer = millis();                                       // таймер для мигания дисплеем
  displayClockTimer = millis();
  timerDisplayState = true;
  secondsDots = true;

  // Основной цикл работы таймера
  while (true)
  {
    uint32_t currentTime = millis();

    updateButtons();

    // Обработка кнопки 1: Выход из таймера (до окончания)
    if (btn1.click() && !timerFinished)
    {
      timerOn = false;
      disp.point(false);
      disp.displayByte(_E, _S, _C, _empty);
      delay(1000);
      return;
    }

    // Обработка длинного нажатия кнопки 2: Вкл/Выкл дисплея
    if (btn2.hold())
    {                     // Используем hold() для длинного нажатия (настроен на 2 сек)
      if (!timerFinished) // Если таймер еще не закончился
      {
        timerDisplayState = !timerDisplayState; // переключаем состояние отображения часов
        if (timerDisplayState)
        {
          disp.displayClock(timerCurrentRemain / 60000, (timerCurrentRemain % 60000) / 1000);
          disp.point(true); // восстанавливаем точки после включения
        }
        else
        {
          disp.clear();
          disp.point(false);
        }
      }
      // Если таймер закончился, длинное нажатие 2 не влияет на вкл/выкл,
      // только короткое нажатие останавливает сигнал.
    }

    // Обработка короткого нажатия кнопки 2: Включить дисплей (если он был выключен)
    if (btn2.click() && !timerDisplayState && !timerFinished)
    { // Если дисплей выключен и таймер не окончен
      timerDisplayState = true;
      disp.displayClock(timerCurrentRemain / 60000, (timerCurrentRemain % 60000) / 1000);
      disp.point(true); // восстанавливаем точки
    }

    // Проверяем, прошла ли одна секунда для обновления таймера
    if (currentTime - timerPrevUpdate >= 1000 && !timerFinished)
    {
      timerCurrentRemain -= 1000; // уменьшаем оставшееся время на 1 секунду
      timerPrevUpdate = currentTime;

      // Проверяем, закончился ли таймер
      if (timerCurrentRemain <= 0)
      {
        timerCurrentRemain = 0; // убедимся, что не уйдёт в отрицательные значения
        timerFinished = true;
        displayBlinking = true; // включаем режим мигания
        startAlarm();
      }
    }

    // Обновляем отображение на индикаторе
    if (currentTime - displayClockTimer >= 500)
    {
      if (timerDisplayState && !timerFinished)
      {
        displayClockTimer = millis();
        byte mins = timerCurrentRemain / 60000;
        byte secs = (timerCurrentRemain % 60000) / 1000;
        displayClock(mins, secs);
      }
    }

    // Режим мигания дисплеем после окончания таймера
    if (displayBlinking)
    {
      if (currentTime - displayToggleTimer >= 1000) // каждую секунду
      {
        timerDisplayState = !timerDisplayState; // переключаем состояние
        if (timerDisplayState)
        {
          byte mins = timerCurrentRemain / 60000;
          byte secs = (timerCurrentRemain % 60000) / 1000;
          disp.displayClock(mins, secs);
          disp.point(true);
        }
        else
        {
          disp.clear();
          disp.point(false);
        }
        displayToggleTimer = currentTime; // обновляем таймер мигания
      }
    }

    // Обновление яркости
    if (currentTime - brightnessControlTimer >= 500 && clockOn)
    {
      brightnessControlTimer = millis();
      brightnessControl();
    }

    // Обработка остановки сигнала после окончания таймера
    // Стоп можно сделать по любой кнопке (кроме 2 hold), например, 1, 2 click, 3, 4
    if (timerFinished)
    {
      if (btn1.click() || btn2.click() || btn3.click() || btn4.click())
      {
        // Останавливаем звуковой сигнал
        stopAlarm();
        // Выходим из цикла
        timerOn = false;
        timerFinished = false; // сбрасываем флаг для следующего запуска
        displayBlinking = false;
        clockOn = true; // восстанавливаем отображение времени
        disp.clear();
        disp.point(false);
        break; // выходим из основного цикла таймера
      }
    }
  }
}

// Откладывание сигнала будильника при нажатии кнопок 3 или 4
void snoozeButton()
{
  if (btn3.click() || btn4.click())
  {
    rtc.clearAlarm(1);
    alarmSignal = false;
    stopVibro();
    stopAlarm();
    unblockMenuTimer = millis();
    alarmStartTime = 0; // Сбрасываем таймер при остановке будильника
    disp.point(false);
    disp.displayByte(_S, _n, _o, _o);
    delay(1000);
    if (snoozeCount < 3)
    {
      snoozeActive = true;
      snoozeStartTime = millis(); // Запускаем таймер для snooze
    }
    if (snoozeCount > 3)
    {
      snoozeActive = false;
      // Уход в сон после всех повторов
      clockOn = false;
      enterSleepMode();
    }
  }
}

// Остановка сигнала будильника с кнопок 1 и 2
void alarmStopButton()
{
  if (btn1.click() || btn2.click())
  {
    unblockMenuTimer = millis();
    rtc.clearAlarm(1);
    alarmSignal = false;
    stopVibro();
    stopAlarm();
    alarmStartTime = 0; // Сбрасываем таймер при остановке будильника
    snoozeActive = false;
    disp.point(false);
    disp.displayByte(_S, _t, _o, _P);
    delay(1000);
  }
}

// Остановка сработанного сигнала будильника по истечении времени.(Если пользователь не остановил сигнал с кнопок)
void alarmStopTime()
{
  if (millis() - alarmStartTime >= alarmDuration && alarmSignal)
  {
    rtc.clearAlarm(1);
    alarmSignal = false;
    stopVibro();
    stopAlarm();
    buttonsBlocked = false;
    alarmStartTime = 0; // Сбрасываем таймер при остановке будильника
    if (snoozeCount < 3)
    {
      snoozeActive = true;
      snoozeStartTime = millis(); // Запускаем таймер для snooze
    }
    if (snoozeCount > 3)
    {
      snoozeActive = false;
      // Уход в сон после всех повторов
      clockOn = false;
      enterSleepMode();
    }
  }
}

// Регулировка яркости свечения индикатора в зависимости от уровня освещения
void brightnessControl()
{
  int light = analogRead(LIGHT_SENSOR);
  int brightnessPower = map(light, 0, 1023, 0, 7);
  disp.brightness(brightnessPower);
}

// Измерение напряжения батареи
double voltageMeasure()
{
  double voltageIn;
  double voltageSupply = 5.05; // Данное значение необходимо измерить мультиметром на пине Vin ARDUINO
                               // Изменяя его можно калибровать показания напряжения
  double voltageCalc;
  double voltageSum = 0;
  double voltageResult;
  int countOfMeasure = 10;

  for (int i = 0; i < countOfMeasure; i++)
  {
    voltageIn = analogRead(VOLTAGE);
    voltageCalc = (voltageIn * voltageSupply) / 1023;
    voltageSum = voltageSum + voltageCalc;
    delay(10);
  }

  voltageResult = voltageSum / countOfMeasure;
  return voltageResult;
}

// Показ напряжения на семисегментном индикаторе
void showVoltage(double voltage)
{
  int intpart = trunc(voltage);
  int fractpart = (voltage * 100);
  int fractpartRes = fractpart % 100;
  disp.displayClock(intpart, fractpartRes);
  disp.point(false);
  delay(1000);
}

// Контроль состояния батареи. При разряде включается вибросигнал и бегущая строка
void batteryControl(double voltage)
{
  double voltageDischargeThreshold = 3.6;

  if (voltage < voltageDischargeThreshold && hrs > 18 && hrs < 22 && !alarmSignal)
  {
    batteryDischarge = true;
    testVibro();
    disp.clear();
    disp.point(false);
    disp.runningString(chargeBattery, sizeof(chargeBattery), 250);
  }
  if (voltage > voltageDischargeThreshold && batteryDischarge)
  {
    batteryDischarge = false;
  }
}

// Показать температуру
void showTemperature()
{
  const uint8_t DEG_SYMBOL_MASK = 0b01100011;
  float temperature_f = rtc.getTemperature();
  // Целая часть
  int whole_part = (int)temperature_f;
  disp.clear();
  disp.point(false);
  disp.displayClock(whole_part, 0);
  disp.displayByte(2, DEG_SYMBOL_MASK);
  disp.displayByte(3, _C);
  delay(1500);
}

// Показать дату
void showDate()
{
  disp.clear();
  disp.point(false);
  disp.displayInt(year);
  delay(1500);
  disp.displayInt(month);
  delay(1500);
  disp.displayInt(day);
  delay(1500);
}

// Воспроизведение сигнала будильника
void startAlarm()
{
  digitalWrite(MUSIC_MOSFET, HIGH);
  delay(500);
  myMP3.volume(alarmVolume);
  delay(100);
  myMP3.playFolder(1, random(1, 8));
}

// Остановка сигнала будильника
void stopAlarm()
{
  myMP3.stop();
  delay(500);
  digitalWrite(MUSIC_MOSFET, LOW);
}

// Включение вибрации
void startVibro()
{
  if (millis() - vibroToggleTimer > 2000) // Периодичность вибрации
  {
    digitalWrite(VIBRO, vibroToggle);
    vibroToggle = !vibroToggle;
    vibroToggleTimer = millis();
  }
}

// Выключение вибрации
void stopVibro()
{
  vibroToggle = true;
  digitalWrite(VIBRO, LOW); // Выключаем вибрацию
}

// Меню
void menu()
{
  if (mainMenuActive)
  {
    disp.point(false);
    if (btn3.click())
    {
      mainMenuItem = mainMenuItem - 1;
      if (mainMenuItem < 0)
      {
        mainMenuItem = 5;
      }
    }
    if (btn4.click())
    {
      mainMenuItem = mainMenuItem + 1;
      if (mainMenuItem > 5)
      {
        mainMenuItem = 0;
      }
    }
    if (mainMenuItem == 0)
    {
      disp.displayByte(_C, _L, _O, _empty);
      if (btn2.click())
      {
        btn2.reset();
        setTimeMenuActive = true;
        mainMenuActive = false;
      }
    }
    if (mainMenuItem == 1)
    {
      disp.displayByte(_a, _L, _r, _empty);
      if (btn2.click())
      {
        btn2.reset();
        mainMenuActive = false;
        alarmOnOffMenuActive = true;
      }
    }
    if (mainMenuItem == 2)
    {
      disp.displayByte(_C, _h, _r, _o);
      if (btn2.click())
      {
        if (timerOn)
        {
          // показать таймер
        }
        if (!timerOn)
        {
          setTimer();
        }
      }
    }
    if (mainMenuItem == 3)
    {
      disp.displayByte(_L, _o, _u, _d);
      if (btn2.click())
      {
        setVolume();
      }
    }
    if (mainMenuItem == 4)
    {
      disp.displayByte(_P, _L, _A, _Y);
      if (btn2.click())
      {
        playMusicMenu();
      }
    }
    if (mainMenuItem == 5)
    {
      disp.displayByte(fw_version);
    }
    if (btn1.click()) // Выход из меню
    {
      disp.displayByte(_E, _S, _C, _empty);
      menuActive = false;
      mainMenuActive = false;
      btn1.reset();
      delay(1000);
      return;
    }
  }
}

void alarmOnOffMenu()
{
  if (btn3.click())
  {
    alarmMenuState = !alarmMenuState;
  }
  if (btn4.click())
  {
    alarmMenuState = !alarmMenuState;
  }
  if (alarmMenuState)
  {
    disp.displayByte(_empty, _empty, _O, _n);
    if (btn2.click())
    {
      btn2.reset();
      EEPROM.update(2, 1);
      alarmOn = true;
      setAlarm();
      alarmOnOffMenuActive = false;
      mainMenuActive = true;
      return;
    }
  }
  if (!alarmMenuState)
  {
    disp.displayByte(_empty, _O, _F, _F);
    if (btn2.click())
    {
      btn2.reset();
      EEPROM.update(2, 0);
      alarmOn = false;
      rtc.disableAlarm(1);
      alarmOnOffMenuActive = false;
      mainMenuActive = true;
      disp.displayByte(_d, _O, _n, _E);
      delay(1000);
      return;
    }
  }
  if (btn1.click()) // Выход из меню
  {
    btn1.reset();
    alarmOnOffMenuActive = false;
    mainMenuActive = true;
    disp.displayByte(_E, _S, _C, _empty);
    delay(1000);
    return;
  }
}

// Установка времени срабатывания будильника
void setAlarm()
{
  byte alarmHrsTmp = alarmHrs;
  byte alarmMinTmp = alarmMin;
  uint32_t lastHourChangeTime = 0;
  uint32_t lastMinuteChangeTime = 0;
  const uint16_t INITIAL_DELAY = 500;   // Начальная задержка перед первым изменением
  const uint16_t CHANGE_INTERVAL = 500; // Интервал между изменениями

  while (true)
  {
    uint32_t currentTime = millis();

    updateButtons();

    // Увеличение часов при клике кнопки 3 (однократное нажатие)
    if (btn3.click())
    {
      alarmHrsTmp = (alarmHrsTmp + 1) % 24;
    }

    // Увеличение часов при удержании кнопки 3
    if (btn3.pressing())
    { // кнопка нажата в данный момент
      uint16_t pressDuration = btn3.pressFor();
      if (pressDuration >= INITIAL_DELAY)
      {
        if (lastHourChangeTime == 0 || (currentTime - lastHourChangeTime >= CHANGE_INTERVAL))
        {
          alarmHrsTmp = (alarmHrsTmp + 1) % 24;
          lastHourChangeTime = currentTime;
        }
      }
    }

    // Увеличение минут при клике кнопки 4 (однократное нажатие)
    if (btn4.click())
    {
      alarmMinTmp = (alarmMinTmp + 1) % 60;
    }

    // Увеличение минут при удержании кнопки 4
    if (btn4.pressing())
    { // кнопка нажата в данный момент
      uint16_t pressDuration = btn4.pressFor();
      if (pressDuration >= INITIAL_DELAY)
      {
        if (lastMinuteChangeTime == 0 || (currentTime - lastMinuteChangeTime >= CHANGE_INTERVAL))
        {
          alarmMinTmp = (alarmMinTmp + 1) % 60;
          lastMinuteChangeTime = currentTime;
        }
      }
    }

    // Подтверждение установки
    if (btn2.click())
    {
      EEPROM.update(0, alarmHrsTmp);
      EEPROM.update(1, alarmMinTmp);
      alarmHrs = alarmHrsTmp;
      alarmMin = alarmMinTmp;
      DateTime now = rtc.now();
      DateTime alarmTime(now.year(), now.month(), now.day(), alarmHrsTmp, alarmMinTmp, 0);
      // Устанавливаем Alarm1 для срабатывания каждый день в указанное время
      // DS3231_A1_Hour - срабатывает когда совпадают часы, минуты и секунды
      rtc.setAlarm1(alarmTime, DS3231_A1_Hour);
      delay(10);
      rtc.clearAlarm(1);
      disp.point(false);
      disp.displayByte(_d, _O, _n, _E);
      delay(1000);
      break;
    }
    if (btn1.click()) // Выход из меню
    {
      disp.displayByte(_E, _S, _C, _empty);
      delay(1000);
      break;
    }
    // Отображение текущего времени будильника
    disp.displayClock(alarmHrsTmp, alarmMinTmp);
    delay(50); // Уменьшено с 100 до 50 мс для более быстрого обновления
  }
}

// Установка времени часов rtc DS3231
void setTime()
{
  if (setTimeMenuActive)
  {

    if (!setTimeMenuDateTimeUpdate)
    {
      clockMinTmp = min;
      clockHrsTmp = hrs;
      clockDate = day;
      clockMonth = month;
      clockYear = year;
      setTimeMenuDateTimeUpdate = true;
    }

    uint32_t currentTime = millis();

    // Увеличение часов при клике кнопки 3 (однократное нажатие)
    if (btn3.click())
    {
      clockHrsTmp = (clockHrsTmp + 1) % 24; // Увеличиваем часы
    }

    // Увеличение часов при удержании кнопки 3
    if (btn3.pressing())
    { // кнопка нажата в данный момент
      uint16_t pressDuration = btn3.pressFor();
      if (pressDuration >= INITIAL_DELAY)
      {
        if (lastHourChangeTime == 0 || (currentTime - lastHourChangeTime >= CHANGE_INTERVAL))
        {
          clockHrsTmp = (clockHrsTmp + 1) % 24;
          lastHourChangeTime = currentTime;
        }
      }
    }

    // Увеличение минут при клике кнопки 4 (однократное нажатие)
    if (btn4.click())
    {
      clockMinTmp = (clockMinTmp + 1) % 60; // Увеличиваем минуты
    }

    // Увеличение минут при удержании кнопки 4
    if (btn4.pressing())
    { // кнопка нажата в данный момент
      uint16_t pressDuration = btn4.pressFor();
      if (pressDuration >= INITIAL_DELAY)
      {
        if (lastMinuteChangeTime == 0 || (currentTime - lastMinuteChangeTime >= CHANGE_INTERVAL))
        {
          clockMinTmp = (clockMinTmp + 1) % 60;
          lastMinuteChangeTime = currentTime;
        }
      }
    }

    // Подтверждение установки
    if (btn2.click())
    {
      // Создаем объект DateTime с новым временем и текущей датой
      DateTime newTime(clockYear, clockMonth, clockDate, clockHrsTmp, clockMinTmp, 0);
      rtc.adjust(newTime); // Устанавливаем новое время
      // Обновляем глобальные переменные времени
      updateDateTime();
      setTimeMenuActive = false;
      mainMenuActive = true;
      setTimeMenuDateTimeUpdate = false;
      disp.point(false);
      disp.displayByte(_d, _O, _n, _E);
      btn2.reset();
      delay(1000);
      return;
    }
    if (btn1.click()) // Выход из меню
    {
      setTimeMenuActive = false;
      mainMenuActive = true;
      setTimeMenuDateTimeUpdate = false;
      disp.point(false);
      disp.displayByte(_E, _S, _C, _empty);
      btn1.reset();
      delay(1000);
      return;
    }
    // Отображение текущего времени
    disp.displayClock(clockHrsTmp, clockMinTmp);
    delay(50); // Уменьшено с 100 до 50 мс для более быстрого обновления
  }
}

// Установка уровня громкости
void setVolume()
{
  byte currentVolume = alarmVolume; // Начальное значение громкости
  uint32_t setVolTimer;
  digitalWrite(MUSIC_MOSFET, HIGH);
  delay(500);
  disp.displayInt(currentVolume);
  myMP3.volume(currentVolume);
  delay(50);
  myMP3.playFolder(1, 1);

  while (true)
  {
    updateButtons();

    // Увеличение громкости
    if (btn4.click())
    {
      disp.displayInt(currentVolume);
      if (currentVolume < 30)
      { // Максимальная громкость 30
        currentVolume++;
        if (setVolTimer + millis() > 500) // Уменьшено с 1000 до 500 мс
        {
          myMP3.volume(currentVolume);
          delay(50);
          myMP3.playFolder(1, 1);
          setVolTimer = millis();
        }
      }
    }

    // Уменьшение громкости
    if (btn3.click())
    {
      if (currentVolume > 0)
      { // Минимальная громкость 0
        currentVolume--;
        if (setVolTimer + millis() > 500) // Уменьшено с 1000 до 500 мс
        {
          myMP3.volume(currentVolume);
          delay(50);
          myMP3.playFolder(1, 1);
          setVolTimer = millis();
        }
      }
    }

    // Подтверждение выбора
    if (btn2.click())
    {
      alarmVolume = currentVolume;
      EEPROM.update(3, alarmVolume);
      myMP3.volume(alarmVolume);
      disp.displayByte(_d, _O, _n, _E);
      delay(1000);
      digitalWrite(MUSIC_MOSFET, LOW);
      break;
    }

    // Выход без сохранения
    if (btn1.click())
    {
      disp.displayByte(_E, _S, _C, _empty);
      delay(1000);
      digitalWrite(MUSIC_MOSFET, LOW);
      break;
    }

    // Отображение текущего уровня громкости
    disp.displayInt(currentVolume);
    delay(50); // Уменьшено с 100 до 50 мс
  }
}

// Меню воспроизведения музыки
void playMusicMenu()
{
  playMusicDisplayOfTimer = millis();
  mainMenuItem = 0;
  // Основной цикл меню
  while (true)
  {
    updateButtons();

    disp.point(false);
    if (btn3.click())
    {
      mainMenuItem = mainMenuItem - 1;
      if (mainMenuItem < 0)
      {
        mainMenuItem = 3;
      }
    }
    if (btn4.click())
    {
      mainMenuItem = mainMenuItem + 1;
      if (mainMenuItem > 3)
      {
        mainMenuItem = 0;
      }
    }
    if (mainMenuItem == 0)
    {
      if (!isPlaying)
      {
        disp.displayByte(_S, _t, _r, _t);
        if (btn2.click())
        {
          playMusic();
        }
        if (btn1.click()) // Выход из меню
        {
          disp.displayByte(_E, _S, _C, _empty);
          delay(1000);
          break;
        }
      }
      else
      {
        disp.displayByte(_S, _t, _o, _P);
        if (btn2.click())
        {
          stopMusic();
        }
        if (btn1.click()) // Выход из меню
        {
          disp.displayByte(_E, _S, _C, _empty);
          delay(1000);
          break;
        }
      }
    }
    if (mainMenuItem == 1)
    {
      disp.displayByte(_S, _L, _E, _d);
      if (btn2.click())
      {
        nextTrack();
      }
      if (btn1.click()) // Выход из меню
      {
        disp.displayByte(_E, _S, _C, _empty);
        delay(1000);
        break;
      }
    }
    if (mainMenuItem == 2)
    {
      disp.displayByte(_P, _r, _E, _d);
      if (btn2.click())
      {
        previousTrack();
      }
      if (btn1.click()) // Выход из меню
      {
        disp.displayByte(_E, _S, _C, _empty);
        delay(1000);
        break;
      }
    }
    if (mainMenuItem == 3)
    {
      disp.displayByte(_L, _o, _u, _d);
      if (btn2.click())
      {
        setVolumeMusic();
      }
      if (btn1.click()) // Выход из меню
      {
        disp.displayByte(_E, _S, _C, _empty);
        delay(1000);
        break;
      }
    }
    if (millis() - playMusicDisplayOfTimer > 30000)
    {
      while (true)
      {
        disp.clear();

        updateButtons();

        if (btn1.click() || btn2.click() || btn3.click() || btn4.click())
        {
          playMusicDisplayOfTimer = millis();
          break;
        }
      }
    }
  }
}

// Воспроизведение/пауза
void playMusic()
{
  digitalWrite(MUSIC_MOSFET, HIGH);
  delay(500);
  myMP3.volume(20);
  delay(50);
  myMP3.playFolder(1, 1);
  delay(50);
  myMP3.startRepeatPlay();
  isPlaying = true;
}

// Следующий трек
void nextTrack()
{
  myMP3.playNext();
  isPlaying = true;
  delay(50);
  myMP3.startRepeatPlay();
}

// Предыдущий трек
void previousTrack()
{
  myMP3.playPrevious();
  isPlaying = true;
  delay(50);
  myMP3.startRepeatPlay();
}

// Остановка музыки
void stopMusic()
{
  myMP3.stop();
  isPlaying = false;
  delay(500);
  digitalWrite(MUSIC_MOSFET, LOW);
}

void setVolumeMusic()
{
  byte currentVolume = 20;

  while (true)
  {
    updateButtons();

    // Увеличение громкости
    if (btn4.click())
    {
      if (currentVolume < 30)
      { // Максимальная громкость 30
        currentVolume++;
        myMP3.volume(currentVolume);
      }
    }

    // Уменьшение громкости
    if (btn3.click())
    {
      if (currentVolume > 0)
      { // Минимальная громкость 0
        currentVolume--;
        myMP3.volume(currentVolume);
      }
    }

    // Подтверждение выбора
    if (btn2.click())
    {
      disp.displayByte(_d, _O, _n, _E);
      delay(1000);
      break;
    }

    // Выход без сохранения
    if (btn1.click())
    {
      disp.displayByte(_E, _S, _C, _empty);
      delay(1000);
      break;
    }

    // Отображение текущего уровня громкости
    disp.displayInt(currentVolume);
    delay(50); // Уменьшено с 100 до 50 мс
  }
}

// Включение фонарика по нажатию BTN_4
void flashLight()
{
  if (!flashLightOn)
  {
    digitalWrite(LED, HIGH);
    flashLightOn = true;
    disp.point(false);
    disp.displayByte(_empty, _empty, _O, _n);
    delay(1000);
    disp.clear();
  }
  else
  {
    digitalWrite(LED, LOW);
    flashLightOn = false;
    disp.point(false);
    disp.displayByte(_empty, _O, _f, _f);
    delay(1000);
    disp.clear();
  }
}
#include "Arduino.h"
#include "EEPROM.h"
#include "math.h"
#include "DFPlayerMini_Fast.h"
#include "RTClib.h"
#include "GyverTM1637.h"
#include "GyverPower.h"

// Константы для пинов
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

DFPlayerMini_Fast myMP3;
RTC_DS3231 rtc; // RTC DS3231 подключается к SDA – A4, SCL – A5
GyverTM1637 disp(CLK, DIO);

// Таймеры
uint32_t clockTimer, updateTimeTimer, alarmStartTime, vibroTimer, batteryControlTimer, playMusicDisplayOfTimer, snoozeStartTime;
uint32_t btnTimer = 0;
uint32_t alarmDuration = 60000;   // Длительность сигнала будильника
uint32_t snoozeDuration = 300000; // Перерыв для snooze

// Флаги состояний
boolean secondsDots, alarmSignal, batteryDischarge, blockButton, menuSelectAlarm;
boolean alarmOn = false; // Определяет включена ли функция будильньика
boolean clockOn = true;
boolean vibroOn = true; // Переменная используется для обеспечения периодичности включения и выключения вибрации
boolean isPlaying = false;
boolean flashLightOn = false;
boolean snoozeActive = false;
boolean alarmTriggered;

// Переменные времени и настроек
byte year, month, day, hrs, min, sec, alarmHrs, alarmMin, menuSelect;
byte alarmVolume;
byte alarmOnDefault = false;
byte alarmHrsDefault = 12; // Значения будильника, которые записываются в EEPROM при первом включении
byte alarmMinDefault = 00;
byte alarmVolumeDefault = 20;
byte snoozeCount;

// Приветственное сообщение
byte welcomeBanner[] = {_H, _E, _L, _L, _O, _empty, _S, _E, _r, _G, _E, _i};
// Сообщение "Зарядите батарею"
byte chargeBattery[] = {_C, _H, _A, _r, _G, _E, _empty, _b, _A, _t, _t, _E, _r, _Y};

// Прототипы функций
void initializeClock();
void pinsConfig();
void powerConfig();
void testVibro();
void enterSleepMode();
void wakeUp();
void updateDateTime();
void clock(byte hrs, byte min);
void isAlarm();
void alarm();
void snoozeButton();
void alarmStopButton();
void alarmStopTime();
int readButton(int btnnum);
void brightness();
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
void blockMenu();
void alarmOnOff();
void setAlarm();
void setTime();
void setVolume();
void playMusicMenu();
void exitInMainMenu();
void playMusic();
void previousTrack();
void nextTrack();
void stopMusic();
void setVolumeMusic();
void flashLight();
void printTime();

void setup()
{
  initializeClock();
  pinsConfig();
  powerConfig();
  attachInterrupt(0, wakeUp, FALLING);  // Обработчик прерывания для включения от кнопки
  attachInterrupt(1, isAlarm, FALLING); // Обработчик прерывания для включения от сигнала DS3231
  testVibro();
  disp.runningString(welcomeBanner, sizeof(welcomeBanner), 250); // Приветсвтие при включении
}

void loop()
{
  // Данное условие обеспечивает включение звукового сигнала.
  if (alarmTriggered)
  {
    startAlarm();
    alarmTriggered = false;
  }

  // Если будильник сработал блокируем все функции кнопок.
  // Ждём от нажатие кнопок для остановки будильника или для откладывания или остановки по времени.
  if (alarmSignal)
  {
    blockButton = true;
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
      alarm();
      snoozeStartTime = millis();
      snoozeCount = snoozeCount + 1;
    }
  }

  // Обновление времени из rtc модуля DS3231
  // Периодичность раз в секунду
  if (millis() - updateTimeTimer > 1000)
  {
    updateDateTime();
    updateTimeTimer = millis();
  }

  // Показ времени на индикаторе
  // Регулировка яркости
  if (millis() - clockTimer > 500)
  {
    clock(hrs, min);
    brightness();
    clockTimer = millis();
  }

  // Контролируем состояние батареи раз в пять минут
  if (millis() - batteryControlTimer > 300000)
  {
    batteryControl(voltageMeasure());
    batteryControlTimer = millis();
  }

  // Вызов меню настроек времени и будильника
  if (readButton(BTN_1) == 1 && !blockButton)
  {
    menu();
  }
  
  // Вкл/выкл отображение часов(для энергосбережения) уход в сон при нажатии более трёх секунд
  if (readButton(BTN_2) == 2 && !blockButton)
  {
    clockOn = false;
  }

  // Показать напряжение батареи
  if (readButton(BTN_3) == 1 && !blockButton)
  {
    showVoltage(voltageMeasure());
  }

  // Включение фонарика
  if (readButton(BTN_4) == 1 && !blockButton)
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
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); Раскоментировать строку для установки времени часов с компьютера
  rtc.clearAlarm(1);
  alarmHrs = EEPROM.read(0);
  alarmMin = EEPROM.read(1);
  alarmOn = EEPROM.read(2);
  alarmVolume = EEPROM.read(3);
  menuSelectAlarm = alarmOn;
  myMP3.begin(Serial, true);
  disp.clear();
  disp.brightness(7); // Яркость, 0 - 7 (минимум - максимум)
  updateDateTime();
}

// Конфигурация пинов
void pinsConfig()
{
  pinMode(BTN_1, INPUT);
  pinMode(BTN_2, INPUT);
  pinMode(BTN_3, INPUT);
  pinMode(BTN_4, INPUT);
  pinMode(VIBRO, OUTPUT);
  pinMode(MUSIC_MOSFET, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(LIGHT_SENSOR, INPUT);
  pinMode(VOLTAGE, INPUT);
}

void powerConfig()
{
  power.autoCalibrate();                                    // автоматическая калибровка
  power.hardwareDisable(PWR_SPI | PWR_TIMER1 | PWR_TIMER2); // отключаем неиспользуемые модули МК
  power.setSleepMode(POWERDOWN_SLEEP);                      // устанавливаем режим сна в который будем уходить
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

void enterSleepMode()
{
  if (!clockOn && !alarmSignal && !isPlaying && !flashLightOn && !snoozeActive)
  {
    disp.clear();
    disp.point(false);
    delay(1000);
    power.sleep(SLEEP_FOREVER);
  }
}

void wakeUp()
{
  clockOn = true;
}

// Получение времени из rtc DS3231
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

// Часы
void clock(byte hrs, byte min)
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

void isAlarm()
{
  snoozeCount = 0;
  alarm();
}

// Будильник
void alarm()
{
  if (alarmOn && !alarmSignal)
  {
    alarmTriggered = true;
    alarmSignal = true;
    clockOn = true;
    alarmStartTime = millis();
  }
}

void snoozeButton()
{
  if (readButton(BTN_3) || readButton(BTN_4))
  {
    rtc.clearAlarm(1);
    alarmSignal = false;
    stopVibro();
    stopAlarm();
    blockButton = false;
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

// Остановка сигнала будильника с кнопок
void alarmStopButton()
{
  if (readButton(BTN_1) || readButton(BTN_2))
  {
    rtc.clearAlarm(1);
    alarmSignal = false;
    stopVibro();
    stopAlarm();
    blockButton = false;
    alarmStartTime = 0; // Сбрасываем таймер при остановке будильника
    snoozeActive = false;
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
    blockButton = false;
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

// Чтение кнопки с антидребезгом
int readButton(int btnnum)
{
  static uint32_t btnTimer = 0;
  static bool btnPressed = false;
  static int lastBtnNum = -1;

  int buttonState = digitalRead(btnnum);

  // Если кнопка нажата (LOW из-за подтяжки к питанию)
  if (buttonState == LOW)
  {
    if (!btnPressed)
    {
      // Первое обнаружение нажатия
      btnPressed = true;
      lastBtnNum = btnnum;
      btnTimer = millis();
    }
    else
    {
      // Кнопка продолжает быть нажатой
      if (lastBtnNum == btnnum && (millis() - btnTimer >= 3000))
      {
        // Долгое нажатие (больше 3 секунд)
        btnPressed = false;
        lastBtnNum = -1;
        return 2;
      }
    }
  }
  else
  {
    // Кнопка отпущена
    if (btnPressed && lastBtnNum == btnnum)
    {
      btnPressed = false;
      // Проверяем время нажатия
      if (millis() - btnTimer < 3000)
      {
        lastBtnNum = -1;
        return 1; // Короткое нажатие
      }
    }
  }

  return 0; // Нет нажатия или нажатие еще обрабатывается
}

// Регулировка яркости свечения индикатора в зависимости от уровня освещения
void brightness()
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

// Показ напряжения на индикаторе
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
  double voltageDischargeTreshold = 3.6;

  if (voltage < voltageDischargeTreshold && hrs > 18 && hrs < 22 && !alarmSignal)
  {
    batteryDischarge = true;
    testVibro();
    disp.clear();
    disp.point(false);
    disp.runningString(chargeBattery, sizeof(chargeBattery), 250 );
  }
  if (voltage > voltageDischargeTreshold && batteryDischarge)
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
  myMP3.playFolder(2, random(1, 8));
}

// Остановка музыки
void stopAlarm()
{
  myMP3.stop();
  delay(500);
  digitalWrite(MUSIC_MOSFET, LOW);
}

// Включение вибрации
void startVibro()
{
  if (millis() - vibroTimer > 2000) // Периодичность вибрации
  {
    digitalWrite(VIBRO, vibroOn);
    vibroOn = !vibroOn;
    vibroTimer = millis();
  }
}

// Выключение вибрации
void stopVibro()
{
  vibroOn = true;
  digitalWrite(VIBRO, LOW); // Выключаем вибрацию
}

// Меню
void menu()
{
  menuSelect = 0;
  while (true)
  {
    disp.point(false);
    if (readButton(BTN_4))
    {
      menuSelect = menuSelect + 1;
      if (menuSelect > 3)
      {
        menuSelect = 0;
      }
    }
    if (menuSelect == 0)
    {
      disp.displayByte(_C, _L, _O, _empty);
      if (readButton(BTN_2))
      {
        setTime();
        break;
      }
      if (readButton(BTN_1)) // Выход из меню
      {
        disp.displayByte(_E, _S, _C, _empty);
        delay(1000);
        break;
      }
    }
    if (menuSelect == 1)
    {
      disp.displayByte(_a, _L, _r, _empty);
      if (readButton(BTN_2))
      {
        alarmOnOff();
      }
      if (readButton(BTN_1)) // Выход из меню
      {
        disp.displayByte(_E, _S, _C, _empty);
        delay(1000);
        break;
      }
    }
    if (menuSelect == 2)
    {
      disp.displayByte(_L, _o, _u, _d);
      if (readButton(BTN_2))
      {
        setVolume();
      }
      if (readButton(BTN_1)) // Выход из меню
      {
        disp.displayByte(_E, _S, _C, _empty);
        delay(1000);
        break;
      }
    }
    if (menuSelect == 3)
    {
      disp.displayByte(_P, _L, _A, _Y);
      if (readButton(BTN_2))
      {
        playMusicMenu();
      }
      if (readButton(BTN_1)) // Выход из меню
      {
        disp.displayByte(_E, _S, _C, _empty);
        delay(1000);
        break;
      }
    }
  }
}

void alarmOnOff()
{
  while (true)
  {
    if (readButton(BTN_4))
    {
      menuSelectAlarm = !menuSelectAlarm;
    }
    if (menuSelectAlarm)
    {
      disp.displayByte(_empty, _empty, _O, _n);
      if (readButton(BTN_2))
      {
        EEPROM.update(2, 1);
        alarmOn = true;
        setAlarm();
        break;
      }
    }
    if (!menuSelectAlarm)
    {
      disp.displayByte(_empty, _O, _F, _F);
      if (readButton(BTN_2))
      {
        EEPROM.update(2, 0);
        alarmOn = false;
        rtc.disableAlarm(1);
        disp.displayByte(_d, _O, _n, _E);
        delay(1000);
        break;
      }
    }
    if (readButton(BTN_1)) // Выход из меню
    {
      disp.displayByte(_E, _S, _C, _empty);
      delay(1000);
      break;
    }
  }
}

// Установка времени срабатывания будильника
void setAlarm()
{
  byte alarmHrsTmp = alarmHrs;
  byte alarmMinTmp = alarmMin;

  while (true)
  {
    // Увеличение часов
    if (readButton(BTN_3))
    {
      alarmHrsTmp = (alarmHrsTmp + 1) % 24; // Увеличиваем часы
    }
    // Увеличение минут
    if (readButton(BTN_4))
    {
      alarmMinTmp = (alarmMinTmp + 1) % 60; // Увеличиваем минуты
    }
    // Подтверждение установки
    if (readButton(BTN_2))
    {
      EEPROM.update(0, alarmHrsTmp);
      EEPROM.update(1, alarmMinTmp);
      alarmHrs = alarmHrsTmp;
      alarmMin = alarmMinTmp;

      // Устанавливаем будильник в DS3231
      // Alarm1 будет срабатывать каждый день в указанное время
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
    if (readButton(BTN_1)) // Выход из меню
    {
      disp.displayByte(_E, _S, _C, _empty);
      delay(1000);
      break;
    }

    // Отображение текущего времени будильника
    disp.displayClock(alarmHrsTmp, alarmMinTmp);
    delay(100); // Обновление дисплея
  }
}

// Установка времени часов rtc DS3231
void setTime()
{
  DateTime now = rtc.now();
  byte clockMinTmp = now.minute();
  byte clockHrsTmp = now.hour();

  // Используем текущую дату из RTC
  byte clockDate = now.day();
  byte clockMonth = now.month();
  int clockYear = now.year();

  while (true)
  {
    // Увеличение часов
    if (readButton(BTN_3))
    {
      clockHrsTmp = (clockHrsTmp + 1) % 24; // Увеличиваем часы
    }
    // Увеличение минут
    if (readButton(BTN_4))
    {
      clockMinTmp = (clockMinTmp + 1) % 60; // Увеличиваем минуты
    }
    // Подтверждение установки
    if (readButton(BTN_2))
    {
      // Создаем объект DateTime с новым временем и текущей датой
      DateTime newTime(clockYear, clockMonth, clockDate, clockHrsTmp, clockMinTmp, 0);
      rtc.adjust(newTime); // Устанавливаем новое время

      // Обновляем глобальные переменные времени
      updateDateTime();

      disp.point(false);
      disp.displayByte(_d, _O, _n, _E);
      delay(1000);
      break;
    }
    if (readButton(BTN_1)) // Выход из меню
    {
      disp.displayByte(_E, _S, _C, _empty);
      delay(1000);
      break;
    }
    // Отображение текущего времени
    disp.displayClock(clockHrsTmp, clockMinTmp);
    delay(100); // Обновление дисплея
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
    // Увеличение громкости
    if (readButton(BTN_4))
    {
      disp.displayInt(currentVolume);
      if (currentVolume < 30)
      { // Максимальная громкость 30
        currentVolume++;
        if (setVolTimer + millis() > 1000)
        {
          myMP3.volume(currentVolume);
          delay(50);
          myMP3.playFolder(1, 1);
          setVolTimer = millis();
        }
      }
    }

    // Уменьшение громкости
    if (readButton(BTN_3))
    {
      if (currentVolume > 0)
      { // Минимальная громкость 0
        currentVolume--;
        if (setVolTimer + millis() > 1000)
        {
          myMP3.volume(currentVolume);
          delay(50);
          myMP3.playFolder(1, 1);
          setVolTimer = millis();
        }
      }
    }

    // Подтверждение выбора
    if (readButton(BTN_2))
    {
      alarmVolume = currentVolume;
      EEPROM.update(3, alarmVolume);
      myMP3.volume(alarmVolume);
      disp.displayByte(_d, _O, _n, _E); // "donE"
      delay(500);
      digitalWrite(MUSIC_MOSFET, LOW);
      break;
    }

    // Выход без сохранения
    if (readButton(BTN_1))
    {
      disp.displayByte(_E, _S, _C, _empty); // "ESC "
      delay(500);
      digitalWrite(MUSIC_MOSFET, LOW);
      break;
    }

    // Отображение текущего уровня громкости
    disp.displayInt(currentVolume);
    delay(100);
  }
}

// Меню воспроизведения музыки
void playMusicMenu()
{
  playMusicDisplayOfTimer = millis();
  menuSelect = 0;
  // Основной цикл меню
  while (true)
  {
    disp.point(false);
    if (readButton(BTN_4))
    {
      menuSelect = menuSelect + 1;
      if (menuSelect > 3)
      {
        menuSelect = 0;
      }
    }
    if (menuSelect == 0)
    {
      if (!isPlaying)
      {
        disp.displayByte(_S, _t, _r, _t);
        if (readButton(BTN_2))
        {
          playMusic();
        }
        if (readButton(BTN_1)) // Выход из меню
        {
          disp.displayByte(_E, _S, _C, _empty);
          delay(1000);
          break;
        }
      }
      else
      {
        disp.displayByte(_S, _t, _o, _P);
        if (readButton(BTN_2))
        {
          stopMusic();
        }
        if (readButton(BTN_1)) // Выход из меню
        {
          disp.displayByte(_E, _S, _C, _empty);
          delay(1000);
          break;
        }
      }
    }
    if (menuSelect == 1)
    {
      disp.displayByte(_S, _L, _E, _d);
      if (readButton(BTN_2))
      {
        nextTrack();
      }
      if (readButton(BTN_1)) // Выход из меню
      {
        disp.displayByte(_E, _S, _C, _empty);
        delay(1000);
        break;
      }
    }
    if (menuSelect == 2)
    {
      disp.displayByte(_P, _r, _E, _d);
      if (readButton(BTN_2))
      {
        previousTrack();
      }
      if (readButton(BTN_1)) // Выход из меню
      {
        disp.displayByte(_E, _S, _C, _empty);
        delay(1000);
        break;
      }
    }
    if (menuSelect == 3)
    {
      disp.displayByte(_L, _o, _u, _d);
      if (readButton(BTN_2))
      {
        setVolumeMusic();
      }
      if (readButton(BTN_1)) // Выход из меню
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
        if (readButton(BTN_1) || readButton(BTN_2) || readButton(BTN_3) || readButton(BTN_4))
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
  myMP3.playFolder(1,1);
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
    // Увеличение громкости
    if (readButton(BTN_4))
    {
      if (currentVolume < 30)
      { // Максимальная громкость 30
        currentVolume++;
        myMP3.volume(currentVolume);
      }
    }

    // Уменьшение громкости
    if (readButton(BTN_3))
    {
      if (currentVolume > 0)
      { // Минимальная громкость 0
        currentVolume--;
        myMP3.volume(currentVolume);
      }
    }

    // Подтверждение выбора
    if (readButton(BTN_2))
    {
      disp.displayByte(_d, _O, _n, _E);
      delay(1000);
      break;
    }

    // Выход без сохранения
    if (readButton(BTN_1))
    {
      disp.displayByte(_E, _S, _C, _empty);
      delay(1000);
      break;
    }

    // Отображение текущего уровня громкости
    disp.displayInt(currentVolume);
    delay(100);
  }
}
// Включение фонарика по нажатию кнопки 2
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

// Отладочная функция
void printTime()
{
  Serial.print(hrs);
  Serial.print(":");
  Serial.print(min);
  Serial.print(":");
  Serial.println(sec);
}
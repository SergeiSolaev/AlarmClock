#include "Arduino.h"
#include "EEPROM.h"
#include "math.h"
#include "DFPlayerMini_Fast.h"
#include "microDS3231.h"
#include "GyverTM1637.h"

// Константы для пинов
#define BTN_1 2 // Pulup подтяжка(к "+" питания), когда нажата возвращает 0
#define BTN_2 3
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
MicroDS3231 rtc; // RTC DS3231 подключается к SDA – A4, SCL – A5
GyverTM1637 disp(CLK, DIO);

// Таймеры
uint32_t clockTimer, alarmTimer, alarmStartTime, vibroTimer, batteryControlTimer, playMusicDisplayTimer;
uint32_t btnTimer = 0;
uint32_t alarmDuration = 60000; // Длительность сигнала будильника

// Флаги состояний
boolean secondsDots, alarmSignal, batteryDischarge, blockMenuFlag, alarmOn, menuSelectAlarm;
boolean clockOn = true;
boolean vibroOn = true; // Переменная используется для обеспечения периодичности включения и выключения вибрации
boolean isPlaying = false;
boolean flashLightOn = false;

// Переменные времени и настроек
byte hrs, min, sec, alarmHrs, alarmMin, menuSelect;
byte alarmVolume = 20;
byte alarmOnDefault = false;
byte alarmHrsDefault = 12; // Значения будильника, которые записываются в EEPROM при первом включении
byte alarmMinDefault = 00;
byte alarmVolumeDefault = 20;

// Приветственное сообщение
byte welcomeBanner[] = {_H, _E, _L, _L, _O, _empty, _S, _E, _r, _G, _E, _i};

// Прототипы функций
void initializeClock();
void pinsConfig();
void testVibro();
void updateTime();
void clock(byte hrs, byte min);
void alarm();
void alarmStopButton();
void alarmStopTime();
bool readButton(int btnnum);
void brightness();
double voltageMeasure();
void showVoltage(double voltage);
void batteryControl(double voltage);
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
  testVibro();
  disp.runningString(welcomeBanner, sizeof(welcomeBanner), 250); // Приветсвтие при включении
}

void loop()
{
  // Если будильник сработал блокируем вызов меню при нажатии кнопок
  // Останавливаем будильник по нажатию кнопки или по истечении заданного времени
  if (alarmSignal)
  {
    alarmStopButton();
    alarmStopTime();
  }

  // Обновление времени из rtc модуля DS3231
  // Проверка временм на срабатывание будильника
  if (millis() - alarmTimer > 100)
  {
    updateTime();
    alarm();
    blockMenu();
    alarmTimer = millis();
  }

  // Показ времени на индикаторе
  // Регулировка яркости
  if (millis() - clockTimer > 500)
  {
    clock(hrs, min);
    brightness();
    clockTimer = millis();
  }

  // Контролируем состояние батареи.
  if (millis() - batteryControlTimer > 60000)
  {
    batteryControl(voltageMeasure());
    batteryControlTimer = millis();
  }

  // Вызов меню настроек времени и будильника
  if (readButton(BTN_1))
  {
    menu();
  }

  // Вызов меню настроек времени и будильника
  if (readButton(BTN_2) && !blockMenuFlag)
  {
    flashLight();
  }

  // Показать напряжение батареи
  if (readButton(BTN_3))
  {
    showVoltage(voltageMeasure());
  }
  // Вкл/выкл отображение часов(для энергосбережения)
  if (readButton(BTN_4))
  {
    clockOn = !clockOn;
  }
}

//Инициализация устройств и загрузка настроек
void initializeClock()
{
  // rtc.setTime(COMPILE_TIME); // Раскоментировать строку для установки времени часов с компьютера
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
  alarmHrs = EEPROM.read(0);
  alarmMin = EEPROM.read(1);
  alarmOn = EEPROM.read(2);
  alarmVolume = EEPROM.read(3);
  menuSelectAlarm = alarmOn;
  myMP3.begin(Serial, true);
  disp.clear();
  disp.brightness(7); // Яркость, 0 - 7 (минимум - максимум)
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

// Получение времени из rtc DS3231
void updateTime()
{
  hrs = rtc.getHours();
  min = rtc.getMinutes();
  sec = rtc.getSeconds();
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

// Будильник
void alarm()
{
  if (hrs == alarmHrs && min == alarmMin && sec == 00 && alarmOn && !alarmSignal)
  {
    startAlarm();
    alarmSignal = true;
    alarmStartTime = millis();
  }
  // Включаем вибрацию
  if (alarmSignal)
  {
    if (millis() - alarmStartTime > 30000) // Время через которое включается вибрация
    {
      startVibro();
    }
  }
}

// Остановка сигнала будильника с кнопок
void alarmStopButton()
{
  if ((readButton(BTN_1) || readButton(BTN_2) || readButton(BTN_3) || readButton(BTN_4)) && alarmSignal)
  {
    alarmSignal = false;
    stopVibro();
    stopAlarm();
  }
}

// Остановка сработанного сигнала будильника по истечении времени.(Если пользователь не остановил сигнал с кнопок)
void alarmStopTime()
{
  if (millis() - alarmStartTime >= alarmDuration)
  {
    alarmSignal = false;
    stopVibro();
    stopAlarm();
  }
}

// Чтение кнопки с антидребезгом
bool readButton(int btnnum)
{
  bool btnState = !digitalRead(btnnum);
  if (btnState && millis() - btnTimer > 300)
  {
    btnTimer = millis();
    return true;
  }
  return false;
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

// Контроль состояния батареи. При пазряде включается звуковой сигнал
void batteryControl(double voltage)
{
  double voltageDischargeTreshold = 3.5;

  if (voltage < voltageDischargeTreshold && hrs > 18 && hrs < 22 && !alarmSignal)
  {
    batteryDischarge = true;
    digitalWrite(MUSIC_MOSFET, HIGH);
    delay(1000);
    myMP3.volume(25);
    delay(100);
    myMP3.playFromMP3Folder(8);
  }
  if (voltage > 3.5 && batteryDischarge)
  {
    batteryDischarge = false;
    digitalWrite(MUSIC_MOSFET, LOW);
  }
}

// Воспроизведение сигнала будильника
void startAlarm()
{
  digitalWrite(MUSIC_MOSFET, HIGH);
  delay(2000);
  myMP3.volume(alarmVolume);
  delay(100);
  myMP3.playFromMP3Folder(random(1, 8));
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
  if (!blockMenuFlag)
  {
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
}

// Блокировка входа в меню при нажатии кнопок во время сработки будильника
void blockMenu()
{
  if (alarmSignal)
  {
    blockMenuFlag = true;
  }
  else
  {
    blockMenuFlag = false;
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
        EEPROM.update(2, true);
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
        EEPROM.update(2, false);
        alarmOn = false;
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
  byte clockSec = 0;
  byte clockMin = 0;
  byte clockHrs = 0;
  byte clockDate = 29; // Установка даты с кнопок будет добавлена позже
  byte clockMonth = 03;
  int clockYear = 2025;
  byte clockMinTmp = min;
  byte clockHrsTmp = hrs;

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
      clockHrs = clockHrsTmp;
      clockMin = clockMinTmp;
      rtc.setTime(clockSec, clockMin, clockHrs, clockDate, clockMonth, clockYear);
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
    disp.displayClock(clockHrsTmp, clockMinTmp);
    delay(100); // Обновление дисплея
  }
}

// Установка уровня громкости
void setVolume()
{
  byte currentVolume = EEPROM.read(3); // Начальное значение громкости
  uint32_t setVolTimer;
  digitalWrite(MUSIC_MOSFET, HIGH);
  delay(1000);
  disp.displayInt(currentVolume);
  myMP3.volume(currentVolume);
  delay(50);
  myMP3.playFromMP3Folder(8);
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
          myMP3.playFromMP3Folder(8);
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
          myMP3.playFromMP3Folder(8);
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
      delay(1000);
      digitalWrite(MUSIC_MOSFET, LOW);
      break;
    }

    // Выход без сохранения
    if (readButton(BTN_1))
    {
      disp.displayByte(_E, _S, _C, _empty); // "ESC "
      delay(1000);
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
  playMusicDisplayTimer = millis();
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
    if (millis() - playMusicDisplayTimer > 30000)
    {
      while (true)
      {
        disp.clear();
        if (readButton(BTN_1) || readButton(BTN_2) || readButton(BTN_3) || readButton(BTN_4))
        {
          playMusicDisplayTimer = millis();
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
  delay(1000);
  myMP3.volume(20);
  delay(50);
  myMP3.playFromMP3Folder(1);
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
  delay(50);
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

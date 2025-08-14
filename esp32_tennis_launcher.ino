/*
	ESP32 Tennis Ball Launcher

	Функционал:
	- Два двигателя (колёса) для выталкивания мяча (через драйвер моторов, например L298N / BTS7960)
	- Серво-подача мяча
	- Управление ИК-пультом (Arduino-IRremote v3.x)
	- Автоматический выстрел каждые 3 секунды (настраиваемо)

	Подключение (пример, при необходимости поменяйте пины под вашу схему):
	- IR-приёмник: GPIO 15
	- Двигатель A: ENA PWM GPIO 25, IN1 GPIO 33, IN2 GPIO 32
	- Двигатель B: ENB PWM GPIO 26, IN3 GPIO 27, IN4 GPIO 14
	- Серво-подача: GPIO 13
	- Статус LED: GPIO 2 (встроенный у большинства ESP32 DevKit)

	Зависимости (установите через менеджер библиотек Arduino):
	- Arduino-IRremote (by Armin Joachimsmeyer), v3.x
	- ESP32Servo (by Kevin Harrington et al.)
*/

#include <Arduino.h>
#include <IRremote.hpp>     // v3.x API: IrReceiver
#include <ESP32Servo.h>

/*
	Как читать этот скетч (для новичков):
	1) Пины подключения указаны ниже. Если у вас другой драйвер/распиновка — просто поменяйте номера GPIO в разделе «Пины».
	2) Два двигателя раскручивают колёса. Серво кратко открывает заслонку, и мяч падает между колёс — происходит выстрел.
	3) Работает автоподача раз в N миллисекунд и управление с ИК‑пульта: старт/стоп, скорость, интервал, ручной выстрел.
	4) Подача сделана как «машина состояний» — несколько шагов по времени без delay(): открыть → подержать → закрыть → короткая пауза.
	5) Питание моторов — отдельный блок питания. Обязательно соедините «минусы» (GND) моторов и ESP32.
*/
// ------ Пины (меняйте при необходимости) ------
static const int PIN_IR_RECEIVER = 15;

static const int PIN_MOTOR_A_EN = 25; // PWM
static const int PIN_MOTOR_A_IN1 = 33;
static const int PIN_MOTOR_A_IN2 = 32;

static const int PIN_MOTOR_B_EN = 26; // PWM
static const int PIN_MOTOR_B_IN3 = 27;
static const int PIN_MOTOR_B_IN4 = 14;

static const int PIN_SERVO_FEED = 13;
static const int PIN_STATUS_LED = 2; // встроенный светодиод

// ------ PWM для двигателей ------
static const int PWM_FREQ_HZ = 20000;      // 20 kHz, тихая ШИМ для моторов
static const int PWM_RES_BITS = 10;         // 10 бит (0..1023)
static const int PWM_CHANNEL_MOTOR_A = 0;   // каналы PWM ESP32
static const int PWM_CHANNEL_MOTOR_B = 1;

// ------ Параметры серво ------
static const int SERVO_MIN_US = 500;        // границы импульсов (зависят от сервопривода)
static const int SERVO_MAX_US = 2400;
static const int SERVO_OPEN_DEG = 70;       // угол открытия заслонки
static const int SERVO_CLOSED_DEG = 10;     // угол закрытия
static const uint32_t SERVO_OPEN_HOLD_MS = 180; // удержание в открытом положении
static const uint32_t SERVO_COOLDOWN_MS = 300;  // небольшая задержка после закрытия

// ------ Управление стрельбой ------
static const uint32_t DEFAULT_FEED_INTERVAL_MS = 3000; // каждые 3 секунды
static const uint32_t MIN_FEED_INTERVAL_MS = 800;      // минимальная пауза между подачами
static const uint32_t MAX_FEED_INTERVAL_MS = 10000;    // максимальная пауза

// Скорость моторов в процентах (0..100)
static const int DEFAULT_MOTOR_SPEED_PERCENT = 75;  
static const int MIN_MOTOR_SPEED_PERCENT = 40;      // слишком маленькая скорость может зажёвывать мяч
static const int MAX_MOTOR_SPEED_PERCENT = 100;

// Время раскрутки колёс после старта
static const uint32_t WARMUP_MS = 800;

// ------ Коды ИК-пульта (примерные; замените под ваш пульт) ------
// Подключите Serial и смотрите вывод, чтобы узнать реальные коды и обновить константы ниже.
// Значения IrReceiver.decodedIRData.command для NEC-пультов часто совпадают с этими примерами,
// но у разных пультов будут другие цифры.
static const uint8_t IR_CMD_TOGGLE_RUN     = 0x45; // POWER
static const uint8_t IR_CMD_SPEED_UP       = 0x46; // VOL+
static const uint8_t IR_CMD_SPEED_DOWN     = 0x15; // VOL-
static const uint8_t IR_CMD_MANUAL_FEED    = 0x44; // PLAY/PAUSE
static const uint8_t IR_CMD_INTERVAL_UP    = 0x40; // >>| next
static const uint8_t IR_CMD_INTERVAL_DOWN  = 0x19; // |<< prev
static const uint8_t IR_CMD_EMERGENCY_STOP = 0x47; // STOP

// ------ Состояния ------
// Машина состояний подачи мяча выполняет цикл:
// FEED_IDLE -> FEED_OPENING -> FEED_OPEN_HOLD -> FEED_CLOSING -> FEED_COOLDOWN -> FEED_IDLE
// Так мы управляем серво по времени, не блокируя программу (без delay()).
enum FeedState {
	FEED_IDLE = 0,
	FEED_OPENING,
	FEED_OPEN_HOLD,
	FEED_CLOSING,
	FEED_COOLDOWN
};

static FeedState feedState = FEED_IDLE;
static uint32_t feedStateChangedAtMs = 0;

static bool isRunning = true; // автоподача включена
// Планируемое время следующей подачи (millis). Сравниваем через (int32_t)(now - nextAutoFeedAtMs) >= 0
// — так код будет надёжно работать даже при переполнении счётчика millis().
static uint32_t nextAutoFeedAtMs = 0;
static uint32_t feedIntervalMs = DEFAULT_FEED_INTERVAL_MS; // Интервал между выстрелами (мс)
static int motorSpeedPercent = DEFAULT_MOTOR_SPEED_PERCENT; // Текущая скорость колёс в процентах
static bool motorsEnabled = false; // крутятся ли колёса сейчас

Servo feedServo;

// ------ Вспомогательные функции ------
// Перевод «процентов» скорости (0..100) в значение ШИМ 10‑бит (0..1023) для ESP32 PWM.
static int percentToDuty10bit(int percent) {
	percent = constrain(percent, 0, 100);
	const int maxDuty = (1 << PWM_RES_BITS) - 1; // 1023
	return (maxDuty * percent) / 100;
}

// Выставляем направление «вперёд» для обоих колёс.
// Если у вас колёса крутятся не в ту сторону — поменяйте HIGH/LOW на IN1/IN2 и IN3/IN4.
static void setMotorDirectionsForward() {
	digitalWrite(PIN_MOTOR_A_IN1, HIGH);
	digitalWrite(PIN_MOTOR_A_IN2, LOW);
	digitalWrite(PIN_MOTOR_B_IN3, HIGH);
	digitalWrite(PIN_MOTOR_B_IN4, LOW);
}

static void stopMotors() {
	ledcWrite(PWM_CHANNEL_MOTOR_A, 0);
	ledcWrite(PWM_CHANNEL_MOTOR_B, 0);
	motorsEnabled = false;
}

// Применяем текущую скорость к обоим моторам (через PWM). Если duty > 0 — моторы считаем включёнными.
static void applyMotorSpeed() {
	const int duty = percentToDuty10bit(motorSpeedPercent);
	ledcWrite(PWM_CHANNEL_MOTOR_A, duty);
	ledcWrite(PWM_CHANNEL_MOTOR_B, duty);
	motorsEnabled = (duty > 0);
}

// Включить моторы, если ещё не крутятся (ставим направление и применяем скорость)
static void startMotorsIfNeeded() {
	if (!motorsEnabled) {
		setMotorDirectionsForward();
		applyMotorSpeed();
	}
}

// В авторежиме моторы должны крутиться. При ручной подаче тоже держим колёса включёнными,
// пока идёт цикл подачи (feedState != FEED_IDLE), чтобы мяч не застрял между колёсами.
static void updateMotorsByRunState() {
	if (isRunning || feedState != FEED_IDLE) {
		startMotorsIfNeeded();
	} else {
		stopMotors();
	}
}

// Запросить цикл подачи: если ничего не происходит (FEED_IDLE), начинаем последовательность открытия серво.
static void requestFeed() {
	if (feedState == FEED_IDLE) {
		feedState = FEED_OPENING;
		feedStateChangedAtMs = millis();
	}
}

/*
	Машина состояний подачи (без delay()):
	- FEED_OPENING: переводим серво к углу открытия (мяч подаётся к колёсам)
	- FEED_OPEN_HOLD: держим открытым короткое время, чтобы мяч успел пройти
	- FEED_CLOSING: закрываем заслонку
	- FEED_COOLDOWN: небольшая пауза для механики
*/
static void updateFeeder() {
	switch (feedState) {
		case FEED_IDLE:
			break;
		case FEED_OPENING:
			feedServo.write(SERVO_OPEN_DEG);
			feedState = FEED_OPEN_HOLD;
			feedStateChangedAtMs = millis();
			break;
		case FEED_OPEN_HOLD:
			if (millis() - feedStateChangedAtMs >= SERVO_OPEN_HOLD_MS) {
				feedState = FEED_CLOSING;
				feedStateChangedAtMs = millis();
			}
			break;
		case FEED_CLOSING:
			feedServo.write(SERVO_CLOSED_DEG);
			feedState = FEED_COOLDOWN;
			feedStateChangedAtMs = millis();
			break;
		case FEED_COOLDOWN:
			if (millis() - feedStateChangedAtMs >= SERVO_COOLDOWN_MS) {
				feedState = FEED_IDLE;
			}
			break;
	}
}

// Автоподача: запускаем новый цикл, когда подошло запланированное время nextAutoFeedAtMs.
static void handleAutoFeed() {
	if (!isRunning) return;
	if (feedState != FEED_IDLE) return;
	const uint32_t now = millis();
	// Сравнение через (int32_t) надёжно работает даже при переполнении millis()
	if ((int32_t)(now - nextAutoFeedAtMs) >= 0) {
		requestFeed();
		nextAutoFeedAtMs = now + feedIntervalMs;
	}
}

static void printStatus() {
	Serial.print("RUN="); Serial.print(isRunning ? "ON" : "OFF");
	Serial.print(" | speed%="); Serial.print(motorSpeedPercent);
	Serial.print(" | interval(ms)="); Serial.println(feedIntervalMs);
}

// POWER: переключение авто‑режима (вкл/выкл). При включении даём время на раскрутку колёс.
static void onToggleRun() {
	isRunning = !isRunning;
	digitalWrite(PIN_STATUS_LED, isRunning ? HIGH : LOW);
	if (isRunning) {
		// Небольшая задержка до первой подачи, чтобы колёса раскрутились
		nextAutoFeedAtMs = millis() + WARMUP_MS;
		startMotorsIfNeeded();
	} else {
		stopMotors();
	}
	printStatus();
}

// STOP: экстренная остановка — отключаем авто‑режим и останавливаем моторы.
static void onEmergencyStop() {
	isRunning = false;
	stopMotors();
	digitalWrite(PIN_STATUS_LED, LOW);
	printStatus();
}

// VOL+: увеличить скорость колёс на 5% (в пределах допустимого диапазона).
static void onSpeedUp() {
	motorSpeedPercent = constrain(motorSpeedPercent + 5, MIN_MOTOR_SPEED_PERCENT, MAX_MOTOR_SPEED_PERCENT);
	if (isRunning) applyMotorSpeed();
	printStatus();
}

// VOL-: уменьшить скорость колёс на 5%.
static void onSpeedDown() {
	motorSpeedPercent = constrain(motorSpeedPercent - 5, MIN_MOTOR_SPEED_PERCENT, MAX_MOTOR_SPEED_PERCENT);
	if (isRunning) applyMotorSpeed();
	printStatus();
}

// NEXT: увеличить интервал подачи на 250 мс.
static void onIntervalUp() {
	feedIntervalMs = constrain(feedIntervalMs + 250, MIN_FEED_INTERVAL_MS, MAX_FEED_INTERVAL_MS);
	printStatus();
}

// PREV: уменьшить интервал подачи на 250 мс.
static void onIntervalDown() {
	feedIntervalMs = constrain(feedIntervalMs - 250, MIN_FEED_INTERVAL_MS, MAX_FEED_INTERVAL_MS);
	printStatus();
}

// PLAY/PAUSE: ручная подача одного мяча. Если авто‑режим выключен — кратко включаем моторы.
static void onManualFeed() {
	// При ручной подаче убеждаемся, что моторы крутятся
	if (!isRunning) {
		startMotorsIfNeeded();
	}
	requestFeed();
	printStatus();
}

// Обработка ИК‑пульта. Подключите монитор порта (115200) и смотрите «cmd=..» в HEX,
// чтобы узнать коды кнопок своего пульта и при необходимости заменить константы IR_CMD_* выше.
static void handleIrRemote() {
	if (!IrReceiver.decode()) return; // нет сигнала — выходим

	// Получаем основную информацию
	auto &data = IrReceiver.decodedIRData;
	const bool isRepeat = (data.flags & IRDATA_FLAGS_IS_REPEAT); // удержание кнопки пульта

	// Для отладки выводим краткие данные
	Serial.print("IR: prot="); Serial.print(data.protocol);
	Serial.print(" addr="); Serial.print(data.address, HEX);
	Serial.print(" cmd="); Serial.print(data.command, HEX);
	Serial.print(" repeat="); Serial.println(isRepeat ? "Y" : "N");

	// Обработка команд по коду command (подходит для NEC-пультов)
	if (!isRepeat) {
		switch (data.command) {
			case IR_CMD_TOGGLE_RUN:     onToggleRun(); break;
			case IR_CMD_EMERGENCY_STOP: onEmergencyStop(); break;
			case IR_CMD_SPEED_UP:       onSpeedUp(); break;
			case IR_CMD_SPEED_DOWN:     onSpeedDown(); break;
			case IR_CMD_INTERVAL_UP:    onIntervalUp(); break;
			case IR_CMD_INTERVAL_DOWN:  onIntervalDown(); break;
			case IR_CMD_MANUAL_FEED:    onManualFeed(); break;
			default:
				// Если коды не совпали — посмотрите вывод в Serial и скорректируйте константы IR_CMD_*
				break;
		}
	}

	IrReceiver.resume();
}

void setup() {
	Serial.begin(115200);
	Serial.println();
	Serial.println("ESP32 Tennis Ball Launcher starting...");

	// 1) Светодиод состояния (горит, когда авто‑режим включён)
	pinMode(PIN_STATUS_LED, OUTPUT);
	digitalWrite(PIN_STATUS_LED, isRunning ? HIGH : LOW);

	// 2) Пины направления драйвера моторов
	pinMode(PIN_MOTOR_A_IN1, OUTPUT);
	pinMode(PIN_MOTOR_A_IN2, OUTPUT);
	pinMode(PIN_MOTOR_B_IN3, OUTPUT);
	pinMode(PIN_MOTOR_B_IN4, OUTPUT);

	// 3) Настройка PWM‑каналов ESP32 под моторы (скорость регулируем через ШИМ)
	ledcSetup(PWM_CHANNEL_MOTOR_A, PWM_FREQ_HZ, PWM_RES_BITS);
	ledcSetup(PWM_CHANNEL_MOTOR_B, PWM_FREQ_HZ, PWM_RES_BITS);
	ledcAttachPin(PIN_MOTOR_A_EN, PWM_CHANNEL_MOTOR_A);
	ledcAttachPin(PIN_MOTOR_B_EN, PWM_CHANNEL_MOTOR_B);

	// 4) Серво подачи (50 Гц). Угол открытия/закрытия подберите под вашу механику.
	ESP32PWM::allocateTimer(0);
	ESP32PWM::allocateTimer(1);
	ESP32PWM::allocateTimer(2);
	ESP32PWM::allocateTimer(3);
	feedServo.setPeriodHertz(50);
	feedServo.attach(PIN_SERVO_FEED, SERVO_MIN_US, SERVO_MAX_US);
	feedServo.write(SERVO_CLOSED_DEG);

	// 5) ИК‑приёмник
	IrReceiver.begin(PIN_IR_RECEIVER, ENABLE_LED_FEEDBACK);
	Serial.println("IR receiver ready");

	// 6) Начальные состояния и планирование первой подачи
	setMotorDirectionsForward();
	if (isRunning) {
		applyMotorSpeed();
		nextAutoFeedAtMs = millis() + WARMUP_MS; // даём время на раскрутку перед первой подачей
	} else {
		stopMotors();
		nextAutoFeedAtMs = millis() + feedIntervalMs;
	}

	printStatus();
}

void loop() {
	// Главный цикл: читаем ИК‑пульт -> управляем моторами -> автоподача -> двигаем серво по состояниям
	handleIrRemote();
	updateMotorsByRunState();
	handleAutoFeed();
	updateFeeder();
}
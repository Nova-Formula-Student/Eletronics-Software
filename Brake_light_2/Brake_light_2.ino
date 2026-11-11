 
 // ADC de 12 bits: 0..4095 corresponde a 0..3.3 V
constexpr int ADC_BITS = 12;
constexpr int THRESH_ON = 900;  // acende acima deste valor
constexpr int THRESH_OFF = 700;  // apaga abaixo deste valor
constexpr int ADC_MIN_OK = 100;  // abaixo = sensor/cabo em falha
constexpr int ADC_MAX_OK = 4000;  // acima = sensor/cabo em falha

bool brakeOn = false;
 #define  PIN_LED_RED 1
 #define  PIN_BRAKE_ADC 2

void setup() {
  pinMode ( PIN_LED_RED, OUTPUT); //Defino a função dos pinos
  pinMode ( PIN_BRAKE_ADC, INPUT);

  Serial.begin(115200); //Começo a transmitir imformação no monitor serial

  analogReadResolution (ADC_BITS); //Defenição da resolução em bits
}
void loop() {

int raw = analogRead( PIN_BRAKE_ADC);
bool sensorFault = (raw < ADC_MIN_OK) || (raw > ADC_MAX_OK); //Defino o que é considerado uma falha do sistema

if (sensorFault) {

brakeOn = true; // falha segura: luz acesa
}  
else if (!brakeOn && raw > THRESH_ON) {

 brakeOn = true; //Se a luz do travão não está ligada e raw( valor da tensão no sensor do travão) > THRESH_ON, então ligar luz do travão

} else if (brakeOn && raw < THRESH_OFF) {

 brakeOn = false; //Se a luz do travão está ligada e raw( valor da tensão no sensor do travão) > THRESH_ON, então desligar luz do travão
}

  digitalWrite(PIN_LED_RED, brakeOn? HIGH : LOW); //Acende a brake light conforme o valor da variável "brakeOn"
  Serial.print("raw="); Serial.print(raw);
  Serial.print(" brake="); Serial.print(brakeOn);
  Serial.print(" fault="); Serial.println(sensorFault);
 delay(20);
}
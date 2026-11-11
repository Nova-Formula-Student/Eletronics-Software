const int PINO_PONTECIOMETRO = 26;
const int PINO_BOTÃO = 15;
const int PINO_SAIDA_BL = 16;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BOTAO, INPUT_PULLPUT);
  pinMode(PINO_SAIDA_BL, OUTPUT);
  digitalWrite(PINO_SAIDA_BL, LOW);
}
void loop(){

  int leituraRaw = analongRead(PINO_POTENCIOMETRO);
  float tensao= leituraRaw * (3.3 / 1023.0);

  bool botaoAtivo = (digitalRead(PINO_BOTAO) == LOW);

  Serial.print("Tensão : ");
  Serial.print(tensao);
  Serial.print(" V | Botao: ");
  Serial.print(botaoAtivo);

  delay(50);
}

const int T = A0;
const int LedVermelho = 2;
const int LedVerde = 6;

const float T_LIM = 60.0;      
const float V_MAX = 410;     // Tens para 1023 -> Q é max do arduino

void setup(){
  
  pinMode(LedVerde, OUTPUT);
  pinMode(LedVermelho, OUTPUT);
}

void loop()
{
  float tensao = analogRead(T) * V_MAX / 1023.0; //Fzr traducao de 0-1023 -> volts real dps de divisor

  if (tensao >= T_LIM) {
    digitalWrite(LedVerde, LOW);
    digitalWrite(LedVermelho, HIGH);
    delay(200);
    digitalWrite(LedVermelho, LOW);
    delay(200);
  }
  else {
    digitalWrite(LedVermelho, LOW);
    digitalWrite(LedVerde, HIGH);
  }
}
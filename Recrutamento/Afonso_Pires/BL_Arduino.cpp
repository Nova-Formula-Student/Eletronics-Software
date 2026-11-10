const int SensorTravao = A0;     
const int BrakeLight = 2;        

const float V_LIM = 0.7;         // tensão min para se considerar q se está a travar
const float V_MAX = 5.0;         // tensão para 1023 -> 5 V é o máx do arduino

void setup() {
  pinMode(BrakeLight, OUTPUT);
}

void loop() {
  float tensao = analogRead(SensorTravao) * V_MAX / 1023.0;  // 0-1023 -> volts

  if (tensao >= V_LIM) {
    digitalWrite(BrakeLight, HIGH);   // a travar -> luz acesa
  }
  else {
    digitalWrite(BrakeLight, LOW);    // sem travar -> luz apagada
  }
}

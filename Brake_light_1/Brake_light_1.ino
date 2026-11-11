
#define PIN_BRAKE_BTN 1 
#define PIN_LED_RED 2
  //Defino os pinos a usar no raspberry PI pico
void setup() {
 pinMode(PIN_BRAKE_BTN , INPUT_PULLUP); 
 pinMode(PIN_LED_RED , OUTPUT);
  //Defino a função de cada pino (input, output,...)
}

void loop() {

  bool braking = (digitalRead (PIN_BRAKE_BTN )== LOW); 
    //Crio uma variavel bolena que avalia se o travão está a ser pressionado
    //Indico se o travão está ou não precionado
  if (braking){
  digitalWrite (PIN_LED_RED , HIGH); 
    //Crio uma condição lógica para caso a variável "braking" = True (logo o travão está precionado)
    //A condição liga a brake light
 }
  else{ 
    digitalWrite (PIN_LED_RED , LOW);} 
    //Crio outra condição para lógica para o resto das posibilidades
    //este caso é só a de "braking" = false, logo, o travão não esta a ser precionado

}
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_PN532.h>

// --- CONFIGURACIÓN PANTALLA OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- CONFIGURACIÓN NFC PN532 (I2C) ---
#define PN532_IRQ   (2)
#define PN532_RESET (3)  // No siempre es necesario conectarlo en I2C
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// --- PINES DE ENTRADA ---
const int btnAzul = 12; // Continuar
const int btnRojo = 14; // Parar/No continuar
const int cables[4] = {15, 4, 16, 17}; // Pines de los cables de la bomba

// --- VARIABLES DE PUNTUACIÓN (MODIFICABLES) ---
int puntos_base = 100;
int mult_nivel2 = 3;
int mult_nivel3 = 4;
int puntos_actuales = 0;

// Estados del juego
enum EstadoJuego { ESPERANDO_NFC, NIVEL_1, NIVEL_2, NIVEL_3, PREGUNTA_CONT, FIN_JUEGO };
EstadoJuego estadoActual = ESPERANDO_NFC;
int nivelSuperado = 0;

void setup() {
  Serial.begin(115200);

  // Configurar botones y cables con Pull-Up interno
  pinMode(btnAzul, INPUT_PULLUP);
  pinMode(btnRojo, INPUT_PULLUP);
  for (int i = 0; i < 4; i++) {
    pinMode(cables[i], INPUT_PULLUP);
  }

  // Inicializar OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Fallo al iniciar SSD1306"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);

  // Inicializar NFC
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("No se encontro la placa PN53x");
    while (1); // Detener
  }
  nfc.SAMConfig(); // Configurar para lectura
  
  // Semilla para los números aleatorios
  randomSeed(analogRead(0)); 

  mostrarMensaje("Pasa tu\ntarjeta NFC\npara iniciar");
}

void loop() {
  switch (estadoActual) {
    case ESPERANDO_NFC:
      esperarTarjeta();
      break;
    case NIVEL_1:
      jugarNivel(1, 2); // Nivel 1, 2 cables
      break;
    case NIVEL_2:
      jugarNivel(2, 3); // Nivel 2, 3 cables
      break;
    case NIVEL_3:
      jugarNivel(3, 4); // Nivel 3, 4 cables
      break;
    case PREGUNTA_CONT:
      preguntarContinuar();
      break;
    case FIN_JUEGO:
      mostrarResultados();
      break;
  }
}

// --- FUNCIONES DEL JUEGO ---

void esperarTarjeta() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer para el ID
  uint8_t uidLength;                        // Longitud del ID

  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 200);
  
  if (success) {
    Serial.print("ID de tarjeta detectado: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(uid[i], HEX);
    }
    Serial.println("");
    
    // Asegurarse de que todos los cables estén conectados antes de empezar
    if(verificarCablesConectados(2)) {
      puntos_actuales = 0;
      nivelSuperado = 0;
      estadoActual = NIVEL_1;
    } else {
      mostrarMensaje("Conecta los\ncables primero!");
      delay(2000);
      mostrarMensaje("Pasa tu\ntarjeta NFC\npara iniciar");
    }
  }
}

bool verificarCablesConectados(int numCables) {
  for(int i=0; i<numCables; i++){
    if(digitalRead(cables[i]) == HIGH) return false; // Si está en HIGH, está desconectado
  }
  return true;
}

void jugarNivel(int nivel, int numCables) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("Nivel "); display.print(nivel); display.println(" elige un cable");
  display.display();

  int cableCorrecto = random(0, numCables);
  unsigned long tiempoInicio = millis();
  bool cableCortado = false;
  bool victoria = false;

  // Bucle del temporizador de 5 segundos
  while (millis() - tiempoInicio < 5000) {
    int tiempoRestante = 5 - ((millis() - tiempoInicio) / 1000);
    
    // Actualizar reloj en pantalla
    display.fillRect(0, 20, 128, 44, BLACK); // Limpiar zona inferior
    display.setCursor(0, 20);
    display.setTextSize(2);
    display.print("Tiempo: "); display.println(tiempoRestante);
    display.display();

    // Comprobar si se cortó algún cable
    for (int i = 0; i < numCables; i++) {
      if (digitalRead(cables[i]) == HIGH) { // Cable desconectado
        cableCortado = true;
        if (i == cableCorrecto) {
          victoria = true;
        }
        break; // Salir del for
      }
    }

    if (cableCortado) break; // Salir del while si ya cortó uno
    delay(100); // Pequeña pausa para no saturar la pantalla
  }

  // Evaluar resultado
  if (victoria) {
    if(nivel == 1) puntos_actuales = puntos_base;
    else if(nivel == 2) puntos_actuales *= mult_nivel2;
    else if(nivel == 3) puntos_actuales *= mult_nivel3;
    
    nivelSuperado = nivel;
    mostrarMensaje("CORRECTO!");
    delay(2000);
    
    if (nivel == 3) {
      estadoActual = FIN_JUEGO; // Si gana el último nivel, termina
    } else {
      estadoActual = PREGUNTA_CONT;
    }
  } else {
    // Pierde (cortó el malo o se acabó el tiempo)
    mostrarMensaje("BOOM! Perdiste");
    delay(2000);
    puntos_actuales = puntos_actuales / 2; // Penalización por perder
    estadoActual = FIN_JUEGO;
  }
}

void preguntarContinuar() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Desea continuar?");
  display.println("");
  display.print("Puntos: "); display.println(""); display.println(puntos_actuales);
  display.println("");
  display.println("Azul: Continuar");
  display.println("Rojo: Retirarse");
  display.display();

  while (true) {
    if (digitalRead(btnAzul) == LOW) {
      // Avanzar al siguiente nivel
      if (nivelSuperado == 1) estadoActual = NIVEL_2;
      if (nivelSuperado == 2) estadoActual = NIVEL_3;
      
      // Esperar a que suelte el botón y conecte los cables
      while(digitalRead(btnAzul) == LOW); 
      mostrarMensaje("Reconecta cables...");
      delay(2000);
      break;
    }
    if (digitalRead(btnRojo) == LOW) {
      estadoActual = FIN_JUEGO;
      while(digitalRead(btnRojo) == LOW);
      break;
    }
    delay(50);
  }
}

void mostrarResultados() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.println("FIN JUEGO");
  display.setTextSize(1);
  display.println("");
  display.print("Puntos finales: ");
  display.setTextSize(2);
  display.println("");
  display.println(puntos_actuales);
  display.display();

  Serial.print("Juego terminado. Puntos a base de datos: ");
  Serial.println(puntos_actuales);

  delay(5000); // Mostrar resultado por 5 segundos
  estadoActual = ESPERANDO_NFC; // Reiniciar
  mostrarMensaje("Pasa tu\ntarjeta NFC\npara iniciar");
}

void mostrarMensaje(String msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println(msg);
  display.display();
}
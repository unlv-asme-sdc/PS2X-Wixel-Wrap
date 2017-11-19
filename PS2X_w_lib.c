#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include "PS2X_w_lib.h"

#define millis() getMs()
#define delay(time) delayMs(time)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))

// INPUT_PORT must never be equal to OUTPORT_PORT.
// They are mutually exclusive values.
// Never use pins 0 or 1 on PORT 1
#define IN_PORT 0
#define DAT_PIN 1

// Never use pins 0 or 1 on PORT 1
#define OUT_PORT 1
#define ATT_PIN 2
#define CLK_PIN 3
#define CMD_PIN 4 

// TODO: Figure out how to use defined preprocessors macros as macro arguments.
#define SET_DIGITAL_OUTPUT(port, pin, value) { \
	P##port##_##pin = value; \
	P##port##DIR |= (1<<pin); }
#define SET_DIGITAL_INPUT(port, pin, pulled) { \
	if (pulled){ P##port##INP &= ~(1<<pin); } else { P##port##INP |= (1<<pin); } \
	P##port##DIR &= ~(1<<pin); }

#define CLK_SET() SET_DIGITAL_OUTPUT(1, 2, 1)
#define CLK_CLR() SET_DIGITAL_OUTPUT(1, 2, 0)
#define CMD_SET() SET_DIGITAL_OUTPUT(1, 3, 1)
#define CMD_CLR() SET_DIGITAL_OUTPUT(1, 3, 0)
#define ATT_SET() SET_DIGITAL_OUTPUT(1, 4, 1)
#define ATT_CLR() SET_DIGITAL_OUTPUT(1, 4, 0)
#define DAT_CHK() P0_1

static byte enter_config[]={0x01,0x43,0x00,0x01,0x00};
static byte set_mode[]={0x01,0x44,0x00,0x01,0x03,0x00,0x00,0x00,0x00};
static byte set_bytes_large[]={0x01,0x4F,0x00,0xFF,0xFF,0x03,0x00,0x00,0x00};
static byte exit_config[]={0x01,0x43,0x00,0x00,0x5A,0x5A,0x5A,0x5A,0x5A};
static byte enable_rumble[]={0x01,0x4D,0x00,0x00,0x01};
static byte type_read[]={0x01,0x45,0x00,0x5A,0x5A,0x5A,0x5A,0x5A,0x5A};

static unsigned char i = 0;
static unsigned int last_buttons = 0;
static unsigned int buttons = 0;

static unsigned long last_read = 0;
static byte read_delay = 0;
static byte controller_type = 0;
static boolean en_Rumble = 0;
static boolean en_Pressures = 0;
unsigned char PS2data[21];
/****************************************************************************************/
boolean NewButtonsState() {
  return ((last_buttons ^ buttons) > 0);
}

/****************************************************************************************/
boolean NewButtonState(unsigned int button) {
  return (((last_buttons ^ buttons) & button) > 0);
}

/****************************************************************************************/
boolean ButtonPressed(unsigned int button) {
  return(NewButtonState(button) & Button(button));
}

/****************************************************************************************/
boolean ButtonReleased(unsigned int button) {
  return((NewButtonState(button)) & ((~last_buttons & button) > 0));
}

/****************************************************************************************/
boolean Button(uint16_t button) {
  return ((~buttons & button) > 0);
}

/****************************************************************************************/
unsigned int ButtonDataByte() {
   return (~buttons);
}

/****************************************************************************************/
byte Analog(byte button) {
   return PS2data[button];
}

/****************************************************************************************/
unsigned char _gamepad_shiftinout (char byte) {
   unsigned char tmp = 0;
   unsigned char i;
   for(i=0;i<8;i++) {
      if(CHK(byte,i)) { 
	      CMD_SET();
      } else {
	      CMD_CLR();
      }
	  
      CLK_CLR();
      delayMicroseconds(CTRL_CLK);

      //if(DAT_CHK()) SET(tmp,i);
      if(DAT_CHK()) {
	      bitSet(tmp,i);
      }

      CLK_SET();
#if CTRL_CLK_HIGH
      delayMicroseconds(CTRL_CLK_HIGH);
#endif
   }
   CMD_SET();
   delayMicroseconds(CTRL_BYTE_DELAY);
   return tmp;
}

/****************************************************************************************/
void read_gamepad() {
   read_gamepad_ext(false,0x00);
}

/****************************************************************************************/
boolean read_gamepad_ext(boolean motor1, byte motor2) {
   double temp = millis() - last_read;
   char dword[9] = {0x01,0x42,0,motor1,motor2,0,0,0,0};
   byte dword2[12] = {0,0,0,0,0,0,0,0,0,0,0,0};
   byte RetryCnt;
   uint8 i;
   uint8 j;

   if (temp > 1500) //waited to long
      reconfig_gamepad();

   if(temp < read_delay)  //waited too short
      delay(read_delay - temp);

   if(motor2 != 0x00)
      //motor2 = map(motor2,0,255,0x40,0xFF); //noting below 40 will make it spin


   // Try a few times to get valid data...
   for (RetryCnt = 0; RetryCnt < 5; RetryCnt++) {
      CMD_SET();
      CLK_SET();
      ATT_CLR(); // low enable joystick

      delayMicroseconds(CTRL_BYTE_DELAY);
      //Send the command to send button and joystick data;
      for (i = 0; i<9; i++) {
         PS2data[i] = _gamepad_shiftinout(dword[i]);
      }

      if(PS2data[1] == 0x79) {  //jf controller js jn full data return mode, get the rest of data
         for (j = 0; j<12; j++) {
            PS2data[j+9] = _gamepad_shiftinout(dword2[j]);
         }
      }

      ATT_SET(); // HI disable joystick
      // Check to see if we received valid data or not.  
	  // We should be in analog mode for our data to be valid (analog == 0x7_)
      if ((PS2data[1] & 0xf0) == 0x70)
         break;

      // If we got to here, we are not in analog mode, try to recover...
      reconfig_gamepad(); // try to get back into Analog mode.
      delay(read_delay);
   }

   // If we get here and still not in analog mode (=0x7_), try increasing the read_delay...
   if ((PS2data[1] & 0xf0) != 0x70) {
      if (read_delay < 10)
         read_delay++;   // see if this helps out...
   }

#ifdef PS2X_COM_DEBUG
   Serial.println("OUT:IN");
   int i;
   for(i=0; i<9; i++){
      Serial.print(dword[i], HEX);
      Serial.print(":");
      Serial.print(PS2data[i], HEX);
      Serial.print(" ");
   }
   int i;
   for (i = 0; i<12; i++) {
      Serial.print(dword2[i], HEX);
      Serial.print(":");
      Serial.print(PS2data[i+9], HEX);
      Serial.print(" ");
   }
   Serial.println("");
#endif

   last_buttons = buttons; //store the previous buttons states

#if defined(__AVR__)
   buttons = *(uint16_t*)(PS2data+3);   //store as one value for multiple functions
#else
   buttons =  (uint16_t)(PS2data[4] << 8) + PS2data[3];   //store as one value for multiple functions
#endif
   last_read = millis();
   return ((PS2data[1] & 0xf0) == 0x70);  // 1 = OK = analog mode - 0 = NOK
}

/****************************************************************************************/
byte config_gamepad() {

  byte temp[sizeof(type_read)];
  int y;
  uint8 i;
  
  // Sets pinmodes for Data.
  P2INP &= ~(1<<5);
  SET_DIGITAL_INPUT(0, 1, 1);

  CMD_SET(); 
  CLK_SET();

  //new error checking. First, read gamepad a few times to see if it's talking
  read_gamepad();
  read_gamepad();

  //see if it talked - see if mode came back. 
  //If still anything but 41, 73 or 79, then it's not talking
  if(PS2data[1] != 0x41 && PS2data[1] != 0x73 && PS2data[1] != 0x79){ 
#ifdef PS2X_DEBUG
    Serial.println("Controller mode not matched or no controller found");
    Serial.print("Expected 0x41, 0x73 or 0x79, but got ");
    Serial.println(PS2data[1], HEX);
#endif
    return 1; //return error code 1
  }

  //try setting mode, increasing delays if need be.
  read_delay = 1;
  for(y = 0; y <= 10; y++) {
    sendCommandString(enter_config, sizeof(enter_config)); //start config run

    //read type
    delayMicroseconds(CTRL_BYTE_DELAY);

    CMD_SET();
    CLK_SET();
    ATT_CLR(); // low enable joystick

    delayMicroseconds(CTRL_BYTE_DELAY);
    int i;
    for (i = 0; i<9; i++) {
      temp[i] = _gamepad_shiftinout(type_read[i]);
    }

    ATT_SET(); // HI disable joystick

    controller_type = temp[3];

    sendCommandString(set_mode, sizeof(set_mode));
    if(rumble){ sendCommandString(enable_rumble, sizeof(enable_rumble)); en_Rumble = true; }
    if(pressures){ sendCommandString(set_bytes_large, sizeof(set_bytes_large)); en_Pressures = true; }
    sendCommandString(exit_config, sizeof(exit_config));

    read_gamepad();

    if(pressures){
      if(PS2data[1] == 0x79)
        break;
      if(PS2data[1] == 0x73)
        return 3;
    }

    if(PS2data[1] == 0x73)
      break;

    if(y == 10){
#ifdef PS2X_DEBUG
      Serial.println("Controller not accepting commands");
      Serial.print("mode stil set at");
      Serial.println(PS2data[1], HEX);
#endif
      return 2; //exit function with error
    }
    read_delay += 1; //add 1ms to read_delay
  }
  return 0; //no error if here
}

/****************************************************************************************/
void sendCommandString(byte string[], byte len) {
  int y;
#ifdef PS2X_COM_DEBUG
  byte temp[len];
  ATT_CLR(); // low enable joystick
  delayMicroseconds(CTRL_BYTE_DELAY);

  for (y=0; y < len; y++)
    temp[y] = _gamepad_shiftinout(string[y]);

  ATT_SET(); //high disable joystick
  delay(read_delay); //wait a few

  Serial.println("OUT:IN Configure");
  int i;
  for(i=0; i<len; i++) {
    Serial.print(string[i], HEX);
    Serial.print(":");
    Serial.print(temp[i], HEX);
    Serial.print(" ");
  }
  Serial.println("");
#else
  ATT_CLR(); // low enable joystick
  for (y=0; y < len; y++)
    _gamepad_shiftinout(string[y]);
  ATT_SET(); //high disable joystick
  delay(read_delay);                  //wait a few
#endif
}

/****************************************************************************************/
byte readType() {
/*
  byte temp[sizeof(type_read)];

  sendCommandString(enter_config, sizeof(enter_config));

  delayMicroseconds(CTRL_BYTE_DELAY);

  CMD_SET();
  CLK_SET();
  ATT_CLR(); // low enable joystick

  delayMicroseconds(CTRL_BYTE_DELAY);

  for (int i = 0; i<9; i++) {
    temp[i] = _gamepad_shiftinout(type_read[i]);
  }

  sendCommandString(exit_config, sizeof(exit_config));

  if(temp[3] == 0x03)
    return 1;
  else if(temp[3] == 0x01)
    return 2;

  return 0;
*/

  if(controller_type == 0x03)
    return 1;
  else if(controller_type == 0x01)
    return 2;
  else if(controller_type == 0x0C)  
    return 3;  //2.4G Wireless Dual Shock PS2 Game Controller
	
  return 0;
}

/****************************************************************************************/
void enableRumble() {
  sendCommandString(enter_config, sizeof(enter_config));
  sendCommandString(enable_rumble, sizeof(enable_rumble));
  sendCommandString(exit_config, sizeof(exit_config));
  en_Rumble = true;
}

/****************************************************************************************/
bool enablePressures() {
  sendCommandString(enter_config, sizeof(enter_config));
  sendCommandString(set_bytes_large, sizeof(set_bytes_large));
  sendCommandString(exit_config, sizeof(exit_config));

  read_gamepad();
  read_gamepad();

  if(PS2data[1] != 0x79)
    return false;

  en_Pressures = true;
    return true;
}

/****************************************************************************************/
void reconfig_gamepad(){
  sendCommandString(enter_config, sizeof(enter_config));
  sendCommandString(set_mode, sizeof(set_mode));
  if (en_Rumble)
    sendCommandString(enable_rumble, sizeof(enable_rumble));
  if (en_Pressures)
    sendCommandString(set_bytes_large, sizeof(set_bytes_large));
  sendCommandString(exit_config, sizeof(exit_config));
}

/****************************************************************************************/
#ifdef __AVR__
inline void  CLK_SET(void) {
  register uint8_t old_sreg = SREG;
  cli();
  *_clk_oreg |= _clk_mask;
  SREG = old_sreg;
}

inline void  CLK_CLR(void) {
  register uint8_t old_sreg = SREG;
  cli();
  *_clk_oreg &= ~_clk_mask;
  SREG = old_sreg;
}

inline void  CMD_SET(void) {
  register uint8_t old_sreg = SREG;
  cli();
  *_cmd_oreg |= _cmd_mask; // SET(*_cmd_oreg,_cmd_mask);
  SREG = old_sreg;
}

inline void  CMD_CLR(void) {
  register uint8_t old_sreg = SREG;
  cli();
  *_cmd_oreg &= ~_cmd_mask; // SET(*_cmd_oreg,_cmd_mask);
  SREG = old_sreg;
}

inline void  ATT_SET(void) {
  register uint8_t old_sreg = SREG;
  cli();
  *_att_oreg |= _att_mask ;
  SREG = old_sreg;
}

inline void ATT_CLR(void) {
  register uint8_t old_sreg = SREG;
  cli();
  *_att_oreg &= ~_att_mask;
  SREG = old_sreg;
}

inline bool DAT_CHK(void) {
  return (*_dat_ireg & _dat_mask) ? true : false;
}

#else
// On pic32, use the set/clr registers to make them atomic...
inline void  CLK_SET(void) {
  *_clk_lport_set |= _clk_mask;
}

inline void  CLK_CLR(void) {
  *_clk_lport_clr |= _clk_mask;
}

inline void  CMD_SET(void) {
  *_cmd_lport_set |= _cmd_mask;
}

inline void  CMD_CLR(void) {
  *_cmd_lport_clr |= _cmd_mask;
}

inline void  ATT_SET(void) {
  *_att_lport_set |= _att_mask;
}

inline void ATT_CLR(void) {
  *_att_lport_clr |= _att_mask;
}

inline bool DAT_CHK(void) {
  return (*_dat_lport & _dat_mask) ? true : false;
}

#endif

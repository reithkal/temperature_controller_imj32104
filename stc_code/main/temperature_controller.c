#include <reg52.h>
#include <stdio.h>
#include <string.h>
#include <intrins.h>

#define keyport P1          
sbit LCD_SDA = P2^7;        
sbit LCD_SCL = P2^6;        
sbit FAN_RELAY = P2^1;      
sbit TMP_SDA = P2^2;        
sbit TMP_SCL = P2^3;        


sbit RESET_BTN = P3^2;      
sbit SERVER_LED = P3^4;    
sbit RESET_LED = P3^5;      

#define LCD_ADDR 0x4E       
#define TMP102_ADDR_W 0x90  
#define TMP102_ADDR_R 0x91  

sfr IAP_DATA  = 0xE2;
sfr IAP_ADDRH = 0xE3;
sfr IAP_ADDRL = 0xE4;
sfr IAP_CMD   = 0xE5;
sfr IAP_TRIG  = 0xE6;
sfr IAP_CONTR = 0xE7;

#define IAP_CMD_IDLE  0
#define IAP_CMD_READ  1
#define IAP_CMD_WRITE 2
#define IAP_CMD_ERASE 3
#define IAP_WAIT      0x82   // Timing for 11.0592MHz/12MHz
#define EEPROM_ADDR   0x2000 // Sector 1 Start

unsigned char keypad[4][4] = {  {'1','2','3','A'},
                                {'4','5','6','B'},
                                {'7','8','9','C'},
                                {'*','0','#','D'} };

int current_temp_int = 0;       
int max_threshold = 30;         // Default max temp (it will be overwritten by EEPROM)


// ==========================================
// FUNCTION PROTOTYPES
// ==========================================
void delay_ms(unsigned int ms);
void IAP_Disable();
unsigned char EEPROM_Read(unsigned int addr);
void EEPROM_Erase(unsigned int addr);
void EEPROM_Write(unsigned int addr, unsigned char dat);
void LCD_I2C_Delay(void);
void LCD_I2C_Start(void);
void LCD_I2C_Stop(void);
void LCD_I2C_WriteByte(unsigned char dat);
void LCD_WriteNibble(unsigned char nibble, bit rs);
void LCD_Command(unsigned char cmd);
void LCD_Char(unsigned char dat);
void LCD_String_xy(unsigned char row, unsigned char pos, char *str);
void LCD_Init(void);
void TMP_Delay(void);
void TMP_Start(void);
void TMP_Stop(void);
void TMP_WriteByte(unsigned char dat);
unsigned char TMP_ReadByte(unsigned char ack);
void Update_Temperature(void);
unsigned char key_detect(void);
void External_Interrupt_0(void); 
// ==========================================


void delay_ms(unsigned int ms) {
    unsigned int i, j;
    for(i = 0; i < ms; i++)
        for(j = 0; j < 114; j++);
}

//EEPROM
void IAP_Disable() {
    IAP_CONTR = 0; IAP_CMD = 0; IAP_TRIG = 0;
    IAP_ADDRH = 0x80; IAP_ADDRL = 0;
}

unsigned char EEPROM_Read(unsigned int addr) {
    unsigned char dat;
    IAP_CONTR = IAP_WAIT; IAP_CMD = IAP_CMD_READ;
    IAP_ADDRH = addr >> 8; IAP_ADDRL = addr;
    EA = 0; 
    IAP_TRIG = 0x46; IAP_TRIG = 0xB9; 
    _nop_(); 
    EA = 1; 
    dat = IAP_DATA; IAP_Disable();
    return dat;
}

void EEPROM_Erase(unsigned int addr) {
    IAP_CONTR = IAP_WAIT; IAP_CMD = IAP_CMD_ERASE;
    IAP_ADDRH = addr >> 8; IAP_ADDRL = addr;
    EA = 0; 
    IAP_TRIG = 0x46; IAP_TRIG = 0xB9; 
    _nop_(); 
    EA = 1; 
    IAP_Disable();
}

void EEPROM_Write(unsigned int addr, unsigned char dat) {
    IAP_CONTR = IAP_WAIT; IAP_CMD = IAP_CMD_WRITE;
    IAP_ADDRH = addr >> 8; IAP_ADDRL = addr;
    IAP_DATA = dat;
    EA = 0; 
    IAP_TRIG = 0x46; IAP_TRIG = 0xB9; 
    _nop_(); 
    EA = 1; 
    IAP_Disable();
}

//lcd
void LCD_I2C_Delay(void) { unsigned char i; for(i=0; i<5; i++); }

void LCD_I2C_Start(void) {
    LCD_SDA = 1; LCD_SCL = 1; LCD_I2C_Delay();
    LCD_SDA = 0; LCD_I2C_Delay(); LCD_SCL = 0;
}

void LCD_I2C_Stop(void) {
    LCD_SDA = 0; LCD_SCL = 1; LCD_I2C_Delay();
    LCD_SDA = 1; LCD_I2C_Delay();
}

void LCD_I2C_WriteByte(unsigned char dat) {
    unsigned char i;
    for(i=0; i<8; i++) {
        LCD_SDA = (dat & 0x80) ? 1 : 0; dat <<= 1;
        LCD_SCL = 1; LCD_I2C_Delay(); LCD_SCL = 0; LCD_I2C_Delay();
    }
    LCD_SDA = 1; LCD_SCL = 1; LCD_I2C_Delay(); LCD_SCL = 0;
}

void LCD_WriteNibble(unsigned char nibble, bit rs) {
    unsigned char lcd_byte = (nibble & 0xF0) | 0x08 | (rs ? 0x01 : 0x00);
    LCD_I2C_Start();
    LCD_I2C_WriteByte(LCD_ADDR);
    LCD_I2C_WriteByte(lcd_byte | 0x04);     // EN=1
    LCD_I2C_WriteByte(lcd_byte & ~0x04);    // EN=0
    LCD_I2C_Stop();
}

void LCD_Command(unsigned char cmd) {
    LCD_WriteNibble(cmd & 0xF0, 0);
    LCD_WriteNibble((cmd << 4) & 0xF0, 0);
    if(cmd == 0x01 || cmd == 0x02) delay_ms(2);
}

void LCD_Char(unsigned char dat) {
    LCD_WriteNibble(dat & 0xF0, 1);
    LCD_WriteNibble((dat << 4) & 0xF0, 1);
}

void LCD_String_xy(unsigned char row, unsigned char pos, char *str) {
    if(row == 0) LCD_Command(0x80 + pos);
    else LCD_Command(0xC0 + pos);
    while(*str) LCD_Char(*str++);
}

void LCD_Init(void) {
    delay_ms(20);
    LCD_WriteNibble(0x30, 0); delay_ms(5);
    LCD_WriteNibble(0x30, 0); delay_ms(1);
    LCD_WriteNibble(0x30, 0); delay_ms(1);
    LCD_WriteNibble(0x20, 0); delay_ms(1); // 4-bit mode
    LCD_Command(0x28); LCD_Command(0x0C); 
    LCD_Command(0x06); LCD_Command(0x01); 
    delay_ms(2);
}

//tmp102
void TMP_Delay(void) { _nop_(); _nop_(); _nop_(); _nop_(); }

void TMP_Start(void) {
    TMP_SDA=1; TMP_SCL=1; TMP_Delay();
    TMP_SDA=0; TMP_Delay(); TMP_SCL=0; TMP_Delay();
}

void TMP_Stop(void) {
    TMP_SDA=0; TMP_SCL=1; TMP_Delay(); TMP_SDA=1; TMP_Delay();
}

void TMP_WriteByte(unsigned char dat) {
    unsigned char i;
    for(i=0; i<8; i++) {
        TMP_SDA = (dat & 0x80) ? 1 : 0; dat <<= 1; TMP_Delay();
        TMP_SCL = 1; TMP_Delay(); TMP_SCL = 0; TMP_Delay();
    }
    TMP_SDA = 1; TMP_Delay(); TMP_SCL = 1; TMP_Delay(); TMP_SCL = 0; TMP_Delay();
}

unsigned char TMP_ReadByte(unsigned char ack) {
    unsigned char i, dat = 0;
    TMP_SDA = 1; TMP_Delay();
    for(i=0; i<8; i++) {
        TMP_SCL = 1; TMP_Delay();
        dat <<= 1;
        if(TMP_SDA) dat |= 0x01;
        TMP_SCL = 0; TMP_Delay();
    }
    TMP_SDA = ack ? 0 : 1; TMP_Delay(); 
    TMP_SCL = 1; TMP_Delay(); TMP_SCL = 0; TMP_Delay();
    TMP_SDA = 1;
    return dat;
}

void Update_Temperature(void) {
    unsigned char msb, lsb;
    int temp_raw, frac_part;
    long temp_scaled;
    char temp_str[16];

    TMP_Start(); TMP_WriteByte(TMP102_ADDR_W); TMP_WriteByte(0x00); TMP_Stop();
    TMP_Start(); TMP_WriteByte(TMP102_ADDR_R);
    msb = TMP_ReadByte(1); // ACK
    lsb = TMP_ReadByte(0); // NACK
    TMP_Stop();

    temp_raw = (msb << 4) | (lsb >> 4);
    temp_scaled = (long)temp_raw * 625 / 100;
    current_temp_int = temp_scaled / 100;
    frac_part = temp_scaled % 100;

    sprintf(temp_str, "Temp: %d.%02d C  ", current_temp_int, frac_part);
    LCD_String_xy(0, 0, temp_str);
}

//keypad
unsigned char key_detect(void) {
    unsigned char colidx, colloc_temp, rowloc_temp;

    keyport = 0xF0;                     // Read columns
    colloc_temp = keyport & 0xF0;
    if(colloc_temp == 0xF0) return 0;   // If no key pressed, return instantly (non-blocking)

    delay_ms(20);                       // Debounce
    colloc_temp = keyport & 0xF0;
    if(colloc_temp == 0xF0) return 0;   // False trigger check

    // Check rows
    keyport = 0xFE; 
    if((keyport & 0xF0) != 0xF0) { rowloc_temp = 0; colloc_temp = keyport & 0xF0; }
    else { keyport = 0xFD; 
    if((keyport & 0xF0) != 0xF0) { rowloc_temp = 1; colloc_temp = keyport & 0xF0; }
    else { keyport = 0xFB; 
    if((keyport & 0xF0) != 0xF0) { rowloc_temp = 2; colloc_temp = keyport & 0xF0; }
    else { keyport = 0xF7; 
    if((keyport & 0xF0) != 0xF0) { rowloc_temp = 3; colloc_temp = keyport & 0xF0; }
    else return 0; }}}

    // Wait for key release before registering
    do { keyport = 0xF0; } while((keyport & 0xF0) != 0xF0);

    if(colloc_temp == 0xE0) colidx = 0;
    else if(colloc_temp == 0xD0) colidx = 1;
    else if(colloc_temp == 0xB0) colidx = 2;
    else colidx = 3;

    return keypad[3 - colidx][3 - rowloc_temp];
}

// ==========================================
// RESET INTERRUPT SERVICE ROUTINE (P3.2)
// ==========================================
void External_Interrupt_0(void) interrupt 0 {
    //command stc to reset
    IAP_CONTR = 0x20; 
}


//MAIN Code
void main(void) {
    unsigned char loaded_thresh;
    char key;
    char status_str[16];
    
    //Interrupt Setup 
    IT0 = 1; // Trigger INT0 on Falling Edge (When button is pressed down)
    EX0 = 1; // Enable External Interrupt 0 (P3.2)
    EA = 1;  // Enable Global Interrupts
    // ----------------------------------------
    
    // Boot Initialization
    delay_ms(100);
    FAN_RELAY = 0;  // Turn Fan OFF 
    SERVER_LED = 1; // Server LED ON by default
    
    // --- NEW: Turn on the Reset LED during the boot sequence ---
    RESET_LED = 1; 
    
    LCD_Init();
    
    LCD_String_xy(0, 0, "SYSTEM BOOTING  ");
    LCD_String_xy(1, 0, "Reading memory..");
    
    delay_ms(1500); // Wait 1.5 seconds so you can clearly see the Reset LED 
    
    RESET_LED = 0; // Turn off Reset LED now that booting is finished
    // -----------------------------------------------------------
    
    // Load Threshold from EEPROM
    loaded_thresh = EEPROM_Read(EEPROM_ADDR);
    if(loaded_thresh >= 1 && loaded_thresh <= 99) {
        max_threshold = loaded_thresh;
    } else {
        // First boot or corrupt data -> Default to 30C and save
        max_threshold = 30;
        EEPROM_Erase(EEPROM_ADDR);
        EEPROM_Write(EEPROM_ADDR, (unsigned char)max_threshold);
    }
    
    LCD_Command(0x01); // Clear screen
    
    while(1) {
        key = key_detect(); // Instant poll of keypad
        
        //setup logic
        if(key == '*') {
            char buf[3] = {0};
            int buf_idx = 0;
            FAN_RELAY = 0; // Disable fan during setup for safety
            
            LCD_Command(0x01);
            LCD_String_xy(0, 0, "Set Max Temp:   ");
            LCD_String_xy(1, 0, "New: _          ");
            
            while(1) {
                key = key_detect();
                if(key >= '0' && key <= '9') {
                    if(buf_idx < 2) {
                        buf[buf_idx] = key;
                        LCD_Command(0xC5 + buf_idx); // Move cursor to type position
                        LCD_Char(key);
                        buf_idx++;
                    }
                } 
                else if (key == '#') {
                    // Save new threshold
                    if(buf_idx > 0) {
                        int new_val;
                        if(buf_idx == 1) new_val = buf[0] - '0';
                        else new_val = (buf[0] - '0') * 10 + (buf[1] - '0');
                        
                        max_threshold = new_val;
                        EEPROM_Erase(EEPROM_ADDR);
                        EEPROM_Write(EEPROM_ADDR, (unsigned char)max_threshold);
                        
                        LCD_String_xy(1, 0, "Saved!          ");
                        delay_ms(1000);
                    }
                    break; // Exit setup
                }
                else if (key == '*') {
                    break; // Exit setup without saving
                }
            }
            LCD_Command(0x01); // Clear screen and return to normal loop
        }
        
        //normal setup
        Update_Temperature();
        
        // --- SERVER LED LOGIC ---
        if (current_temp_int >= 60) {
            SERVER_LED = 0; // Turn OFF LED (Server Offline due to high temp)
        } else {
            SERVER_LED = 1; // Turn ON LED (Server Online)
        }
        // -----------------------------
        
        if (current_temp_int >= max_threshold) {
            FAN_RELAY = 1; // Turn Fan ON
            sprintf(status_str, "Max:%02dC Fan:ON ", max_threshold);
        } else {
            FAN_RELAY = 0; // Turn Fan OFF
            sprintf(status_str, "Max:%02dC Fan:OFF", max_threshold);
        }
        LCD_String_xy(1, 0, status_str);
        
        delay_ms(150); // Small loop breather
    }
}
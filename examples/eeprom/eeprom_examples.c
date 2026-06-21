#include <reg52.h>
#include <intrins.h>
#include <stdio.h>
#include <stdlib.h> 

// ==========================================
// STC89C52 INTERNAL EEPROM REGISTERS (CORRECTED TO 0xE2)
// ==========================================
sfr IAP_DATA  = 0xE2;  // <--- FIXED: STC89 series uses E2, not C2
sfr IAP_ADDRH = 0xE3;
sfr IAP_ADDRL = 0xE4;
sfr IAP_CMD   = 0xE5;
sfr IAP_TRIG  = 0xE6;
sfr IAP_CONTR = 0xE7;

#define IAP_CMD_IDLE    0
#define IAP_CMD_READ    1
#define IAP_CMD_WRITE   2
#define IAP_CMD_ERASE   3
#define IAP_WAIT        0x82   // Matched to the ASM timing
#define EEPROM_ADDR     0x2000 // Starting address of Sector 1

// --- BUS 1: LCD PIN ASSIGNMENTS ---
sbit LCD_SDA = P2^0;   
sbit LCD_SCL = P2^1;   

// --- BUS 2: TMP102 PIN ASSIGNMENTS ---
sbit TMP_SDA = P2^2;   
sbit TMP_SCL = P2^3;   

// --- MOTOR MAPPING (PORT 3) ---
sbit MOTOR_IA = P3^4;  
sbit MOTOR_IB = P3^5;  

// --- EMERGENCY INTERRUPT PIN ---
sbit SHUTDOWN_BTN = P3^2; // Hardware INT0 pin

// --- KEYPAD 3x4 PIN ASSIGNMENTS (ALL ON PORT 1) ---
sbit ROW1 = P1^0;
sbit ROW2 = P1^1;
sbit ROW3 = P1^2;
sbit ROW4 = P1^3;
sbit COL1 = P1^4;
sbit COL2 = P1^5;
sbit COL3 = P1^6;

// --- CONSTANTS ---
#define I2C_LCD_ADDR   0x4E  
#define TMP102_ADDR_W  0x90  
#define TMP102_ADDR_R  0x91  
#define BACKLIGHT_ON   0x08  

// --- GLOBAL VARIABLES ---
int current_temp_int = 0;       
int threshold = 30;             
volatile bit system_halted = 0; 

// --- Function Prototypes ---
void delay_ms(unsigned int ms);
void lcd_cmd(unsigned char cmd);
void lcd_data(unsigned char dat);
void lcd_write_str(unsigned char row, unsigned char col, char *str);
void motor_stop(void) { MOTOR_IA = 0; MOTOR_IB = 0; }
void motor_blow_out(void) { MOTOR_IA = 1; MOTOR_IB = 0; }

// ==========================================
// 1. EEPROM (IAP) CONTROL FUNCTIONS 
// ==========================================
void IAP_Disable() {
    IAP_CONTR = 0;     
    IAP_CMD   = 0;     
    IAP_TRIG  = 0;     
    IAP_ADDRH = 0x80;  
    IAP_ADDRL = 0;
}

unsigned char EEPROM_Read(unsigned int addr) {
    unsigned char dat;
    
    IAP_CONTR = IAP_WAIT;
    IAP_CMD = IAP_CMD_READ;
    IAP_ADDRH = addr >> 8;
    IAP_ADDRL = addr;
    
    EA = 0; 
    IAP_TRIG = 0x46; // <--- Restored to ASM password
    IAP_TRIG = 0xB9; // <--- Restored to ASM password
    _nop_(); 
    EA = 1; 
    
    dat = IAP_DATA;
    IAP_Disable();
    return dat;
}

void EEPROM_Erase(unsigned int addr) {
    IAP_CONTR = IAP_WAIT;
    IAP_CMD = IAP_CMD_ERASE;
    IAP_ADDRH = addr >> 8;
    IAP_ADDRL = addr;
    
    EA = 0; 
    IAP_TRIG = 0x46; 
    IAP_TRIG = 0xB9; 
    _nop_(); 
    EA = 1; 
    
    IAP_Disable();
}

void EEPROM_Write(unsigned int addr, unsigned char dat) {
    IAP_CONTR = IAP_WAIT;
    IAP_CMD = IAP_CMD_WRITE;
    IAP_ADDRH = addr >> 8;
    IAP_ADDRL = addr;
    IAP_DATA = dat;
    
    EA = 0; 
    IAP_TRIG = 0x46; 
    IAP_TRIG = 0xB9; 
    _nop_(); 
    EA = 1; 
    
    IAP_Disable();
}

// ==========================================
// 2. HARDWARE INTERRUPT ROUTINE
// ==========================================
void external_interrupt_0() interrupt 0 {
    motor_stop();       
    system_halted = 1;  
}

void init_interrupts() {
    IT0 = 1; 
    EX0 = 1; 
    EA = 1;  
}

// ==========================================
// 3. I2C BUS 1: DEDICATED TO LCD
// ==========================================
void lcd_i2c_start(void) {
    LCD_SDA = 1; LCD_SCL = 1; _nop_(); _nop_();
    LCD_SDA = 0; _nop_(); _nop_(); LCD_SCL = 0;
}

void lcd_i2c_stop(void) {
    LCD_SDA = 0; LCD_SCL = 1; _nop_(); _nop_();
    LCD_SDA = 1; _nop_(); _nop_();
}

void lcd_i2c_write(unsigned char dat) {
    unsigned char i;
    for(i = 0; i < 8; i++) {
        LCD_SDA = (dat & 0x80) ? 1 : 0;
        dat <<= 1;
        LCD_SCL = 1; _nop_(); _nop_(); LCD_SCL = 0; _nop_(); _nop_();
    }
    LCD_SDA = 1; LCD_SCL = 1; _nop_(); _nop_(); LCD_SCL = 0;
}

void pcf8574_write(unsigned char val) {
    lcd_i2c_start();
    lcd_i2c_write(I2C_LCD_ADDR);
    lcd_i2c_write(val | BACKLIGHT_ON);
    lcd_i2c_stop();
}

void lcd_pulse(unsigned char val) {
    pcf8574_write(val | 0x04);  
    _nop_(); _nop_(); _nop_();
    pcf8574_write(val & ~0x04); 
    delay_ms(1);
}

void lcd_send(unsigned char val, unsigned char mode) {
    unsigned char high_nibble = val & 0xF0;
    unsigned char low_nibble = (val << 4) & 0xF0;
    pcf8574_write(high_nibble | mode); lcd_pulse(high_nibble | mode);
    pcf8574_write(low_nibble | mode); lcd_pulse(low_nibble | mode);
}

void lcd_cmd(unsigned char cmd) { lcd_send(cmd, 0x00); } 
void lcd_data(unsigned char dat) { lcd_send(dat, 0x01); } 

void lcd_init(void) {
    delay_ms(50); 
    pcf8574_write(0x30); lcd_pulse(0x30); delay_ms(5);
    pcf8574_write(0x30); lcd_pulse(0x30); delay_ms(1);
    pcf8574_write(0x30); lcd_pulse(0x30);
    pcf8574_write(0x20); lcd_pulse(0x20); 
    lcd_cmd(0x28); lcd_cmd(0x0C); lcd_cmd(0x06); lcd_cmd(0x01); delay_ms(2);
}

void lcd_write_str(unsigned char row, unsigned char col, char *str) {
    unsigned char addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
    lcd_cmd(addr); 
    while(*str) { lcd_data(*str++); }
}

// ==========================================
// 4. I2C BUS 2: DEDICATED TO TMP102
// ==========================================
void tmp_i2c_delay(void) { _nop_(); _nop_(); _nop_(); _nop_(); _nop_(); }

void tmp_i2c_start(void) {
    TMP_SDA = 1; TMP_SCL = 1; tmp_i2c_delay();
    TMP_SDA = 0; tmp_i2c_delay(); TMP_SCL = 0; tmp_i2c_delay();
}

void tmp_i2c_stop(void) {
    TMP_SDA = 0; TMP_SCL = 1; tmp_i2c_delay();
    TMP_SDA = 1; tmp_i2c_delay();
}

void tmp_i2c_write(unsigned char dat) {
    unsigned char i;
    for(i = 0; i < 8; i++) {
        TMP_SDA = (dat & 0x80) ? 1 : 0;
        dat <<= 1; tmp_i2c_delay();
        TMP_SCL = 1; tmp_i2c_delay(); TMP_SCL = 0; tmp_i2c_delay();
    }
    TMP_SDA = 1; tmp_i2c_delay();
    TMP_SCL = 1; tmp_i2c_delay(); TMP_SCL = 0; tmp_i2c_delay();
}

unsigned char tmp_i2c_read(unsigned char is_last_byte) {
    unsigned char i, dat = 0;
    TMP_SDA = 1; tmp_i2c_delay();
    for(i = 0; i < 8; i++) {
        TMP_SCL = 1; tmp_i2c_delay();
        dat <<= 1;
        if(TMP_SDA) dat |= 0x01;
        TMP_SCL = 0; tmp_i2c_delay();
    }
    TMP_SDA = is_last_byte ? 1 : 0; tmp_i2c_delay();
    TMP_SCL = 1; tmp_i2c_delay(); TMP_SCL = 0; tmp_i2c_delay();
    TMP_SDA = 1; 
    return dat;
}

void read_and_display_temp(void) {
    unsigned char msb, lsb;
    int temp_raw;
    int whole_part, frac_part;
    long temp_scaled;
    char temp_str[16];

    tmp_i2c_start();
    tmp_i2c_write(TMP102_ADDR_W);
    tmp_i2c_write(0x00);
    tmp_i2c_stop();

    tmp_i2c_start();
    tmp_i2c_write(TMP102_ADDR_R);
    msb = tmp_i2c_read(0);
    lsb = tmp_i2c_read(1); 
    tmp_i2c_stop();

    temp_raw = (msb << 4) | (lsb >> 4);
    temp_scaled = (long)temp_raw * 625 / 100;
    whole_part = temp_scaled / 100;
    frac_part = temp_scaled % 100;

    current_temp_int = whole_part;

    sprintf(temp_str, "Temp: %d.%02d C   ", whole_part, frac_part);
    lcd_write_str(0, 0, temp_str);
}

// ==========================================
// 5. KEYPAD SCANNER LOGIC
// ==========================================
char keypad_scan(void) {
    ROW1 = 0; ROW2 = 1; ROW3 = 1; ROW4 = 1;
    if (COL1 == 0) { delay_ms(20); while (COL1 == 0); return '1'; } 
    if (COL2 == 0) { delay_ms(20); while (COL2 == 0); return '2'; }
    if (COL3 == 0) { delay_ms(20); while (COL3 == 0); return '3'; }

    ROW1 = 1; ROW2 = 0; ROW3 = 1; ROW4 = 1;
    if (COL1 == 0) { delay_ms(20); while (COL1 == 0); return '4'; }
    if (COL2 == 0) { delay_ms(20); while (COL2 == 0); return '5'; }
    if (COL3 == 0) { delay_ms(20); while (COL3 == 0); return '6'; }

    ROW1 = 1; ROW2 = 1; ROW3 = 0; ROW4 = 1;
    if (COL1 == 0) { delay_ms(20); while (COL1 == 0); return '7'; }
    if (COL2 == 0) { delay_ms(20); while (COL2 == 0); return '8'; }
    if (COL3 == 0) { delay_ms(20); while (COL3 == 0); return '9'; }

    ROW1 = 1; ROW2 = 1; ROW3 = 1; ROW4 = 0;
    if (COL1 == 0) { delay_ms(20); while (COL1 == 0); return '*'; }
    if (COL2 == 0) { delay_ms(20); while (COL2 == 0); return '0'; }
    if (COL3 == 0) { delay_ms(20); while (COL3 == 0); return '#'; }

    return 0; 
}

void delay_ms(unsigned int ms) {
    unsigned int x, y;
    for(x = ms; x > 0; x--)
        for(y = 114; y > 0; y--);
}

// ==========================================
// 6. MAIN THERMOSTAT PIPELINE
// ==========================================
void main(void) {
    char key;
    char input_buffer[3];         
    unsigned char buf_idx = 0;    
    char display_str[17];         
    unsigned char loaded_thresh;
    
    // Allow power to fully stabilize
    delay_ms(100); 
    
    lcd_init();
    motor_stop();
    init_interrupts(); 
    
    // --- SPLASH SCREEN: READING MEMORY ---
    lcd_write_str(0, 0, "SYSTEM BOOTING  ");
    lcd_write_str(1, 0, "READING MEMORY..");
    delay_ms(1500); 
    
    // --- LOAD SAVED THRESHOLD FROM EEPROM ---
    loaded_thresh = EEPROM_Read(EEPROM_ADDR);
    
    // Validate EEPROM data (should be 1 to 99)
    if (loaded_thresh >= 1 && loaded_thresh <= 99) {
        threshold = loaded_thresh;
    } else {
        // If memory is blank or corrupt, set default to 30 and save it
        threshold = 30;
        EEPROM_Erase(EEPROM_ADDR);
        EEPROM_Write(EEPROM_ADDR, (unsigned char)threshold);
    }
    
    lcd_cmd(0x01); 
    delay_ms(2);

    while(1) {
        // --- EMERGENCY SHUTDOWN STATE ---
        if (system_halted == 1) {
            lcd_write_str(0, 0, "SYSTEM SHUTDOWN ");
            lcd_write_str(1, 0, "New Set: _      ");
            
            buf_idx = 0; 
            
            while (system_halted == 1) {
                key = keypad_scan();
                
                if (key != 0) {
                    if (key >= '0' && key <= '9') {
                        if (buf_idx < 2) {
                            input_buffer[buf_idx] = key;
                            buf_idx++;
                            input_buffer[buf_idx] = '\0'; 
                            sprintf(display_str, "New Set: %s_      ", input_buffer);
                            lcd_write_str(1, 0, display_str);
                        }
                    } 
                    else if (key == '#') { 
                        if (buf_idx > 0) {
                            int new_val = 0;
                            if (buf_idx == 1) new_val = input_buffer[0] - '0';
                            else if (buf_idx == 2) new_val = (input_buffer[0] - '0') * 10 + (input_buffer[1] - '0');
                            
                            threshold = new_val; 
                            
                            // SAVE NEW THRESHOLD TO EEPROM
                            EEPROM_Erase(EEPROM_ADDR);
                            EEPROM_Write(EEPROM_ADDR, (unsigned char)threshold);
                            
                            system_halted = 0; 
                        }
                    }
                    else if (key == '*') { 
                        buf_idx = 0;
                        lcd_write_str(1, 0, "New Set: _      ");
                    }
                }
            }
            
            lcd_cmd(0x01); 
            delay_ms(2);
        }

        // --- NORMAL OPERATION STATE ---
        read_and_display_temp();
        
        if (current_temp_int >= threshold) {
            motor_blow_out(); 
            sprintf(display_str, "Set:%02dC Mot: ON ", threshold);
        } else {
            motor_stop();     
            sprintf(display_str, "Set:%02dC Mot:OFF ", threshold);
        }
        lcd_write_str(1, 0, display_str);

        delay_ms(100); 
    }
}
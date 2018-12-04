#include <SoftwareSerial.h> 
#include "stk500.h"
#include <SPI.h>
#include <SD.h>

#define NODE_ADRESS 1
#define SS_PIN 10

//Select target board
#define TARGET_PRO_MINI
//#define TARGET_UNO

//Select software serial or default serial
#define SOFTWARE_SERIAL
//#define DEFAULT_SERIAL

#ifdef TARGET_UNO
  #define BOOT_BAUD 57600 //For Arduino uno and Seeeduino 57600, pro mini 168 19200
#endif
#ifdef TARGET_PRO_MINI
  #define BOOT_BAUD 19200 //For Arduino uno and Seeeduino 57600, pro mini 168 19200
#endif

//Turn on for debuggin timeout with erase op
//#define DEBUG_TIMEOUT

#define DEBUG_BAUD 19200
// different pins will be needed for I2SD, as 2/3 are leds
#define txPin 4
#define rxPin 5
#define rstPin 6
#define uploadTxPin 7
#define uploadRxPin 8
#define debugTimeoutPin 5

//indicator LEDs on I2SD
#define LED1 2
#define LED2 3
SoftwareSerial sSerial= SoftwareSerial(rxPin,txPin);
SoftwareSerial uploadSerial = SoftwareSerial(uploadRxPin,uploadTxPin);
// set up variables using the SD utility library functions:
SdFile root;
File myFile;
avrmem mybuf;
unsigned char mempage[128];

//chipselect for the wyolum i2sd is 10
const int chipSelect = 10;  
// STANDALONE_DEBUG sends error messages out the main 
// serial port. Not useful after you are actually trying to slave
// another arduino
//#define STANDALONE_DEBUG
#ifdef STANDALONE_DEBUG
#define DEBUGPLN Serial.println
#define DEBUGP Serial.print
#else
#define DEBUGPLN sSerial.println
#define DEBUGP sSerial.print
#endif


/****************
 *FOTA Constants*
 ****************/
 
//Message segments sizes
#define MSG_PAYLOAD_SIZE       34 //bytes  
#define MSG_HEADER_SIZE        1 //bytes 
#define MSG_ADDRESS_SIZE       1 //bytes 
#define MSG_SIZE               (MSG_PAYLOAD_SIZE + MSG_HEADER_SIZE + MSG_ADDRESS_SIZE) //bytes
#define ANS_SIZE               2 //bytes 
#define NBYTES_SIZE            1 //bytes 
#define FILE_MAX_NAME_SIZE     16 //bytes
#define FILE_CHECKSUM_SIZE     4 //bytes
#define N_STARTER_BYTES        (MSG_ADDRESS_SIZE + MSG_HEADER_SIZE + NBYTES_SIZE) //bytes 
#define CHECKSUM_TYPE          unsigned long

//Op codes
#define OP_CREATE          0b00000000
#define OP_ERASE           0b00000010
#define OP_VERIFY          0b00000100
#define OP_UPLOAD          0b00000110
#define OP_WRITE_DATA      0b00000001
#define OP_BEGIN           0b00001110
#define OP_SYNC            0b00001000

//Masks
#define OP_MASK                          0b00001111
#define ERROR_MASK                       0b01110000
#define FORMAT_1_MSG_DATA_SIZE_MASK      0b01111110
#define ALTERNATE_BIT_MASK               0b10000000

//Error codes
#define ERROR_SD_BEGIN                            0b01110000
#define OK                                        0b00000000
#define ERROR_CREATE_FILE_ALREADY_EXISTS          0b00010000
#define ERROR_CREATE_OPENING_NOT_POSSIBLE         0b00100000
#define ERROR_ERASE_FILE_DOESNT_EXIST             0b00010000
#define ERROR_ERASE_ERASING_NOT_POSSIBLE          0b00100000
#define ERROR_WRITE_DATA_INVALID_OPERATION        0b00010000
#define ERROR_WRITE_DATA_OPENING_NOT_POSSIBLE     0b00100000
#define ERROR_WRITE_DATA_ALL_DATA_NOT_WRITTEN     0b00110000
#define ERROR_VERIFY_FILE_DOESNT_EXIST            0b00010000
#define ERROR_VERIFY_OPENING_NOT_POSSIBLE         0b00100000
#define ERROR_VERIFY_INCORRECT_CHECKSUM           0b00110000
#define ERROR_UPLOAD_INVALID_OPERATION            0b00010000

enum Operations{
  data = 0,create,erase,verify,program,nop,invalid,sync
};

bool SdBeginOK;
int remainingBytes;
int byteIndex;
byte buffer[MSG_SIZE];
Operations op,lastOpExecuted;
char fileName[FILE_MAX_NAME_SIZE+1];
byte alternateBit;
bool booting;
byte rxAlternateBit;
byte lastOpAnswered;
byte lastErrorCodeAnswered;

void setup()
{
  Serial.begin(9600);
  SdBeginOK = 0;
  op = nop;
  lastOpExecuted = nop;
  remainingBytes = N_STARTER_BYTES;
  byteIndex = 0;
  pinMode(SS_PIN,OUTPUT);
  SdBeginOK = SD.begin(SS_PIN);
  alternateBit = 0;
  booting = true;
  lastOpAnswered = nop;
  lastErrorCodeAnswered = OK;

  //Upload procedure setup
  
  //digitalWrite(LED1,HIGH);
  //initialize serial port. Note that it's a good idea 
  // to use the softserial library here, so you can do debugging 
  // on USB. 
  mybuf.buf = &mempage[0];
  sSerial.begin(DEBUG_BAUD);
  // and the regular serial port for error messages, etc.
  uploadSerial.begin(BOOT_BAUD);
  pinMode(rxPin, INPUT);
  pinMode(txPin, OUTPUT);
  pinMode(rstPin,OUTPUT);
  pinMode(chipSelect,OUTPUT);
  pinMode(LED1,OUTPUT);
  #ifdef DEBUG_TIMEOUT
  pinMode(debugTimeoutPin,INPUT); // on = resend and answer on | off = no resend neither answer
  #endif
  //Set restPin high, otherwise the device that will be programmed will get stuck
  digitalWrite(rstPin,HIGH);
}

void loop()
{ 
   if(remainingBytes > 0)
   {
       if(Serial.available() > 0)
       {
         buffer[byteIndex] = Serial.read();
         remainingBytes--;
    
         if(byteIndex == MSG_ADDRESS_SIZE)
         {
            //Translate message header
            if(((buffer[byteIndex] & OP_WRITE_DATA) == OP_WRITE_DATA))
            {
              //Message format 1
              remainingBytes = ((buffer[byteIndex] & FORMAT_1_MSG_DATA_SIZE_MASK)>>1);
              op = data;
              rxAlternateBit = (buffer[byteIndex] & ALTERNATE_BIT_MASK)? 1 : 0;
            }
            else
            {
              //Message format 2,3 or 4
              if(((buffer[byteIndex] & OP_MASK) == OP_CREATE)) op = create; else
                if(((buffer[byteIndex] & OP_MASK) == OP_ERASE)) op = erase; else
                  if(((buffer[byteIndex] & OP_MASK) == OP_VERIFY)) op = verify; else
                    if(((buffer[byteIndex] & OP_MASK) == OP_UPLOAD)) op = program; else
                      if(((buffer[byteIndex] & OP_MASK) == OP_SYNC))
                      {
                        alternateBit = 0;
                        op = sync;
                        remainingBytes--;
                        
                      }
                      else
                      {
                        //READ ALL MSG
                        op = invalid;
                      }
                      
                 rxAlternateBit = (buffer[byteIndex] & ALTERNATE_BIT_MASK)? 1 : 0;
            }
         }
         else
         {
            if(byteIndex == MSG_ADDRESS_SIZE + MSG_HEADER_SIZE)
            {
              if(op != nop && op != invalid && op != data)
              {
                //Update message size 
                remainingBytes = buffer[byteIndex];
              }
            
            }
         }
         
         byteIndex++;
       
       }
   }
   else
   {
          //Sync alternate bit when booting
          if(booting)
          {
            alternateBit = rxAlternateBit;
            booting = false;
          }
  
          if(alternateBit == rxAlternateBit)
          {
                  //Operation execution
                  if(SdBeginOK)
                  {
                    if(op == data)
                    {
                       WriteOnFile();
                    }
                    else
                    {
                        GetFileName(buffer,op);
                        
                        if(op == create)
                        {
                          
                          CreateFile();
                          
                        }
                        else
                        {
                            if(op == erase)
                            {
                              
                              #ifdef DEBUG_TIMEOUT
                               if(digitalRead(debugTimeoutPin))
                              #endif
                              RemoveFile();
                              
                               
                              
                            }
                            else
                            {
                                if(op == verify)
                                {
                                  VerifyFile();                     
                                }
                                else
                                {
                                    if(op == program)
                                    {
                                      if(lastOpExecuted != verify)
                                      {
                                           //Recived invalid operation during data write
                                           AnswerMessage(OP_UPLOAD,ERROR_UPLOAD_INVALID_OPERATION);   
                                           
                                      }
                                      else
                                      {
                                          AnswerMessage(OP_UPLOAD,OK);
                                          #ifdef TARGET_PRO_MINI
                                          
                                             #ifdef SOFTWARE_SERIAL
                                                uploadSerial.begin(BOOT_BAUD);
                                             #endif
                                             #ifdef DEFAULT_SERIAL
                                                Serial.begin(BOOT_BAUD);
                                             #endif
                                   
                                          #endif 
                                          
                                          UploadFirmware(fileName);
                                          
                                          #ifdef TARGET_PRO_MINI
                                              #ifdef SOFTWARE_SERIAL
                                                uploadSerial.begin(BOOT_BAUD);
                                             #endif
                                             #ifdef DEFAULT_SERIAL
                                                Serial.begin(BOOT_BAUD);
                                             #endif
                                          #endif 
                                          
                                      }
                                        
                                    }
                                    else
                                    {
                                        if(op == sync)
                                        {
                                            AnswerMessage(OP_SYNC,OK);
                                        }
                                        else
                                        {
                                            if(op == nop || op == invalid)
                                            {
                                              //NOT IMPLEMENTED YET
                                            }
                                        }
                                    }                      
                                }
                            }
                        }
                    }
               }
               else
               {
                  //Error during SD begin
                  AnswerMessage(OP_BEGIN,ERROR_SD_BEGIN);
               }
               
               //Prepare to receive another msg
               alternateBit ^= 1;
               remainingBytes = N_STARTER_BYTES;
               byteIndex = 0;
               lastOpExecuted = op;
           }
           else
           {
               //Resend last answer
               #ifdef DEBUG_TIMEOUT
                if(digitalRead(debugTimeoutPin))
               #endif
               RepeatAnswerMessage();
    
               //Prepare to receive another msg
               remainingBytes = N_STARTER_BYTES;
               byteIndex = 0;
               lastOpExecuted = op;
           } 
   }
}

//FOTA operations procedures
void WriteOnFile()
{
   byte i;
   File f;
   if(lastOpExecuted != create && lastOpExecuted != data)
   {
       //Recived invalid operation during data write
       AnswerMessage(OP_WRITE_DATA,ERROR_WRITE_DATA_INVALID_OPERATION);   
   }
   else
   {
      if(!(f = SD.open(fileName,FILE_WRITE)))
       {
            //Failure to open file
            AnswerMessage(OP_WRITE_DATA,ERROR_WRITE_DATA_OPENING_NOT_POSSIBLE);
       }
       else
       {  
            //(*(buffer[MSG_ADDRESS_SIZE+MSG_HEADER_SIZE]))
            byte dataSize = ((buffer[MSG_ADDRESS_SIZE] & FORMAT_1_MSG_DATA_SIZE_MASK)>>1);
            if(f.write(buffer + (MSG_ADDRESS_SIZE + MSG_HEADER_SIZE),dataSize) != dataSize)
            {
                //Failure to write all data on file
                AnswerMessage(OP_WRITE_DATA,ERROR_WRITE_DATA_ALL_DATA_NOT_WRITTEN);
            }
            else
            {
                //Write successfully
                AnswerMessage(OP_WRITE_DATA,OK);
            }
            
            f.close();
       }   
   }
    
}

void CreateFile()
{
    File f;
    if (SD.exists(fileName)) 
    {
       //File already exists
       AnswerMessage(OP_CREATE,ERROR_CREATE_FILE_ALREADY_EXISTS);
    } 
    else
    {
        //File doesn't exist
        if(!(f = SD.open(fileName,FILE_WRITE)))
        {
            //Failure to create file
            AnswerMessage(OP_CREATE,ERROR_CREATE_OPENING_NOT_POSSIBLE);
        }
        else
        {
           //File created
           f.close(); 
           AnswerMessage(OP_CREATE,OK);
        }
    
     }
}

void RemoveFile()
{
    if (!SD.exists(fileName)) 
    {
       //File doesn't exist
       AnswerMessage(OP_ERASE,ERROR_ERASE_FILE_DOESNT_EXIST);
    } 
    else
    {
        //File exists
        if(!(SD.remove(fileName)))
        {
            //Failure to remove file
            AnswerMessage(OP_ERASE,ERROR_ERASE_ERASING_NOT_POSSIBLE);
        }
        else
        {
           //File removed
           AnswerMessage(OP_ERASE,OK);
        }
    
     }
}

void VerifyFile()
{
  File f;
  
    if(!SD.exists(fileName)) 
    {
       //File doesn't exist
       AnswerMessage(OP_VERIFY,ERROR_VERIFY_FILE_DOESNT_EXIST);
    }
    else
    {
        //File exists
        if(!(f = SD.open(fileName,FILE_READ)))
        {
              //Failure to open file
              AnswerMessage(OP_VERIFY,ERROR_VERIFY_OPENING_NOT_POSSIBLE);
        }
        else
        {
             unsigned long checksum = 0;
             char aux;
             
             //Calculate file Checksum
             while((aux = f.read()) != -1) 
             {
                checksum += aux;  
             }
             
             if(checksum != (*(CHECKSUM_TYPE*)(&buffer[MSG_ADDRESS_SIZE + MSG_HEADER_SIZE + NBYTES_SIZE + buffer[MSG_ADDRESS_SIZE + MSG_HEADER_SIZE] - FILE_CHECKSUM_SIZE])))
             {
                  //Incorrect Checksum
                  AnswerMessage(OP_VERIFY,ERROR_VERIFY_INCORRECT_CHECKSUM);
                  
             }
             else
             {    
                  //File verification performed successfully
                  AnswerMessage(OP_VERIFY,OK);
             }
             f.close();
        }
    }
}

void AnswerMessage(byte op, byte errorCode)
{
  byte buffer[ANS_SIZE];
  lastOpAnswered = op;
  lastErrorCodeAnswered = errorCode;
  buffer[0] = NODE_ADRESS;
  buffer[MSG_ADDRESS_SIZE] = ((OP_MASK & op) | (ERROR_MASK & errorCode) | (alternateBit ? ALTERNATE_BIT_MASK : 0));
  Serial.write(buffer,ANS_SIZE);
}

void RepeatAnswerMessage()
{
  byte buffer[ANS_SIZE];
  buffer[0] = NODE_ADRESS;
  buffer[MSG_ADDRESS_SIZE] = ((OP_MASK & lastOpAnswered) | (ERROR_MASK & lastErrorCodeAnswered) | (alternateBit ? 0 : ALTERNATE_BIT_MASK ));
  Serial.write(buffer,ANS_SIZE);
}

//FOTA auxiliar procedures
void GetFileName(byte* msg,Operations op)
{
    byte i;
    if(op == create || op == erase || op == program)
    {
        //Get file name from format 2 messages
        for(i=0; i<msg[MSG_ADDRESS_SIZE + MSG_HEADER_SIZE]; i++)
        {
            fileName[i] = (char)msg[MSG_ADDRESS_SIZE + MSG_HEADER_SIZE + NBYTES_SIZE+i];
        }
            fileName[i] = '\0';    
    }else
    {
        if(op == verify)
        {
            //Get file name from format 3 messages
            for(i=0; i<msg[MSG_ADDRESS_SIZE + MSG_HEADER_SIZE] - FILE_CHECKSUM_SIZE; i++)
            {
                fileName[i] = (char)msg[MSG_ADDRESS_SIZE + MSG_HEADER_SIZE + NBYTES_SIZE+i];
            }
                fileName[i] = '\0'; 
        }
    
    }
    
}

// Line Buffer is set up in global SRAM
#define LINELENGTH 50
unsigned char linebuffer[LINELENGTH];
unsigned char linemembuffer[16];
int readPage(File input, avrmem *buf)
{
  int len;
  int address;
  int total_len =0;
  // grab 128 bytes or less (one page)
  for (int i=0 ; i < 8; i++){
    len = readIntelHexLine(input, &address, &linemembuffer[0]);
    if (len < 0)
      break;
    else
      total_len=total_len+len;
    if (i==0)// first record determines the page address
      buf->pageaddress = address;
    memcpy((buf->buf)+(i*16), linemembuffer, len);
  }
  buf->size = total_len;
  return total_len;
  
}
// read one line of intel hex from file. Return the number of databytes
// Since the arduino code is always sequential, ignore the address for now.
// If you want to burn bootloaders, etc. we'll have to modify to 
// return the address.

// INTEL HEX FORMAT:
// :<8-bit record size><16bit address><8bit record type><data...><8bit checksum>
int readIntelHexLine(File input, int *address, unsigned char *buf){
  unsigned char c;
  int i=0;
  while (true){
    if (input.available()){
      c = input.read();
      // this should handle unix or ms-dos line endings.
      // break out when you reach either, then check
      // for lf in stream to discard
      if ((c == 0x0d)|| (c == 0x0a))
        break;
      else
        linebuffer[i++] =c;
    }
    else return -1; //end of file
  }
  linebuffer[i]= 0; // terminate the string
  //peek at the next byte and discard if line ending char.
  if (input.peek() == 0xa)
    input.read();
  int len = hex2byte(&linebuffer[1]);
  *address = (hex2byte(&linebuffer[3]) <<8) |
               (hex2byte(&linebuffer[5]));
  int j=0;
  for (int i = 9; i < ((len*2)+9); i +=2){
    buf[j] = hex2byte(&linebuffer[i]);
    j++;
  }

  
  
  return len;
}
unsigned char hex2byte(unsigned char *code){
  unsigned char result =0;

  if ((code[0] >= '0') && (code[0] <='9')){
    result = ((int)code[0] - '0') << 4;
  }
  else if ((code[0] >='A') && (code[0] <= 'F')) {
    result = ((int)code[0] - 'A'+10) << 4;
  }
  if ((code[1] >= '0') && (code[1] <='9')){
    result |= ((int)code[1] - '0');
  }
  else if ((code[1] >='A') && (code[1] <= 'F'))  
    result |= ((int)code[1] -'A'+10);

  
      
return result;
}

// Right now there is only one file.
void UploadFirmware(char * filename){

  digitalWrite(rstPin,HIGH);

  unsigned int major=0;
  unsigned int minor=0;
  delay(100);
   toggle_Reset();
   delay(10);
   stk500_getsync();
   stk500_getparm(Parm_STK_SW_MAJOR, &major);
  DEBUGP("software major: ");
  DEBUGPLN(major);
  stk500_getparm(Parm_STK_SW_MINOR, &minor);
  DEBUGP("software Minor: ");
  DEBUGPLN(minor);
if (SD.exists(filename)){
    myFile = SD.open(filename, FILE_READ);
  }
  else{
    DEBUGP(filename);
    DEBUGPLN(" doesn't exist");
    return;
  }
  //enter program mode
  stk500_program_enable();


  while (readPage(myFile,&mybuf) > 0){
    stk500_loadaddr(mybuf.pageaddress>>1);
    stk500_paged_write(&mybuf, mybuf.size, mybuf.size);
  }

  // could verify programming by reading back pages and comparing but for now, close out
  stk500_disable();
  delay(10);
  toggle_Reset();
  myFile.close();
  blinky(4,500);
  
  
}
void blinky(int times, long delaytime){
  for (int i = 0 ; i < times; i++){
    digitalWrite(LED1,HIGH);
    delay(delaytime);
    digitalWrite(LED1, LOW);
    delay (delaytime);
  }
  
}
void toggle_Reset()
{
  digitalWrite(rstPin, LOW);
  delayMicroseconds(1000);
  digitalWrite(rstPin,HIGH);
}
static int stk500_send(byte *buf, unsigned int len)
{
  #ifdef SOFTWARE_SERIAL
    uploadSerial.write(buf,len);
  #endif

  #ifdef DEFAULT_SERIAL
    Serial.write(buf,len);
  #endif
  
}
static int stk500_recv(byte * buf, unsigned int len)
{
  int rv;

  #ifdef SOFTWARE_SERIAL
    rv = uploadSerial.readBytes((char *)buf,len);
  #endif

  #ifdef DEFAULT_SERIAL
    rv = Serial.readBytes((char *)buf,len);
  #endif
  
  if (rv < 0) {
    error(ERRORNOPGMR);
    return -1;
  }
  return 0;
}
int stk500_drain()
{
  while (uploadSerial.available()> 0)
  {  
    DEBUGP("draining: ");
    DEBUGPLN(uploadSerial.read(),HEX);
  }
  return 1;
}
int stk500_getsync()
{
  byte buf[32], resp[32];

  /*
   * get in sync */
  buf[0] = Cmnd_STK_GET_SYNC;
  buf[1] = Sync_CRC_EOP;
  
  /*
   * First send and drain a few times to get rid of line noise 
   */
  
  stk500_send(buf, 2);
  stk500_drain();
  stk500_send(buf, 2);
  stk500_drain();
  
  stk500_send(buf, 2);
  if (stk500_recv(resp, 1) < 0)
    return -1;
  if (resp[0] != Resp_STK_INSYNC) {
        error1(ERRORPROTOSYNC,resp[0]);
    stk500_drain();
    return -1;
  }

  if (stk500_recv(resp, 1) < 0)
    return -1;
  if (resp[0] != Resp_STK_OK) {
    error1(ERRORNOTOK,resp[0]);
    return -1;
  }
  return 0;
}
static int stk500_getparm(unsigned parm, unsigned * value)
{
  byte buf[16];
  unsigned v;
  int tries = 0;

 retry:
  tries++;
  buf[0] = Cmnd_STK_GET_PARAMETER;
  buf[1] = parm;
  buf[2] = Sync_CRC_EOP;

  stk500_send(buf, 3);

  if (stk500_recv(buf, 1) < 0)
    exit(1);
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      error(ERRORNOSYNC);
      return -1;
    }
   if (stk500_getsync() < 0)
      return -1;
      
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    error1(ERRORPROTOSYNC,buf[0]);
    return -2;
  }

  if (stk500_recv(buf, 1) < 0)
    exit(1);
  v = buf[0];

  if (stk500_recv(buf, 1) < 0)
    exit(1);
  if (buf[0] == Resp_STK_FAILED) {
    error1(ERRORPARMFAILED,v);
    return -3;
  }
  else if (buf[0] != Resp_STK_OK) {
    error1(ERRORNOTOK,buf[0]);
    return -3;
  }

  *value = v;

  return 0;
}
/* read signature bytes - arduino version */
static int arduino_read_sig_bytes(AVRMEM * m)
{
  unsigned char buf[32];

  /* Signature byte reads are always 3 bytes. */

  if (m->size < 3) {
    DEBUGPLN("memsize too small for sig byte read");
    return -1;
  }

  buf[0] = Cmnd_STK_READ_SIGN;
  buf[1] = Sync_CRC_EOP;

  stk500_send(buf, 2);

  if (stk500_recv(buf, 5) < 0)
    return -1;
  if (buf[0] == Resp_STK_NOSYNC) {
    error(ERRORNOSYNC);
  return -1;
  } else if (buf[0] != Resp_STK_INSYNC) {
    error1(ERRORPROTOSYNC,buf[0]);
  return -2;
  }
  if (buf[4] != Resp_STK_OK) {
    error1(ERRORNOTOK,buf[4]);
    return -3;
  }

  m->buf[0] = buf[1];
  m->buf[1] = buf[2];
  m->buf[2] = buf[3];

  return 3;
}

static int stk500_loadaddr(unsigned int addr)
{
  unsigned char buf[16];
  int tries;

  tries = 0;
 retry:
  tries++;
  buf[0] = Cmnd_STK_LOAD_ADDRESS;
  buf[1] = addr & 0xff;
  buf[2] = (addr >> 8) & 0xff;
  buf[3] = Sync_CRC_EOP;


  stk500_send(buf, 4);

  if (stk500_recv(buf, 1) < 0)
    exit(1);
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      error(ERRORNOSYNC);
      return -1;
    }
    if (stk500_getsync() < 0)
      return -1;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    error1(ERRORPROTOSYNC, buf[0]);
    return -1;
  }

  if (stk500_recv(buf, 1) < 0)
    exit(1);
  if (buf[0] == Resp_STK_OK) {
    return 0;
  }

  error1(ERRORPROTOSYNC, buf[0]);
  return -1;
}
static int stk500_paged_write(AVRMEM * m, 
                              int page_size, int n_bytes)
{
  // This code from avrdude has the luxury of living on a PC and copying buffers around.
  // not for us...
 // unsigned char buf[page_size + 16];
 unsigned char cmd_buf[16]; //just the header
  int memtype;
 // unsigned int addr;
  int block_size;
  int tries;
  unsigned int n;
  unsigned int i;
  int flash;

  // Fix page size to 128 because that's what arduino expects
  page_size = 128;
  //EEPROM isn't supported
  memtype = 'F';
  flash = 1;


    /* build command block and send data separeately on arduino*/
    
    i = 0;
    cmd_buf[i++] = Cmnd_STK_PROG_PAGE;
    cmd_buf[i++] = (page_size >> 8) & 0xff;
    cmd_buf[i++] = page_size & 0xff;
    cmd_buf[i++] = memtype;
    stk500_send(cmd_buf,4);
    stk500_send(&m->buf[0], page_size);
    cmd_buf[0] = Sync_CRC_EOP;
    stk500_send( cmd_buf, 1);

    if (stk500_recv(cmd_buf, 1) < 0)
      exit(1); // errr need to fix this... 
    if (cmd_buf[0] == Resp_STK_NOSYNC) {
        error(ERRORNOSYNC);
        return -3;
     }
    else if (cmd_buf[0] != Resp_STK_INSYNC) {

     error1(ERRORPROTOSYNC, cmd_buf[0]);
      return -4;
    }
    
    if (stk500_recv(cmd_buf, 1) < 0)
      exit(1);
    if (cmd_buf[0] != Resp_STK_OK) {
    error1(ERRORNOTOK,cmd_buf[0]);

      return -5;
    }
  

  return n_bytes;
}
#ifdef LOADVERIFY //maybe sometime? note code needs to be re-written won't work as is
static int stk500_paged_load(AVRMEM * m, 
                             int page_size, int n_bytes)
{
  unsigned char buf[16];
  int memtype;
  unsigned int addr;
  int a_div;
  int tries;
  unsigned int n;
  int block_size;

  memtype = 'F';


  a_div = 1;

  if (n_bytes > m->size) {
    n_bytes = m->size;
    n = m->size;
  }
  else {
    if ((n_bytes % page_size) != 0) {
      n = n_bytes + page_size - (n_bytes % page_size);
    }
    else {
      n = n_bytes;
    }
  }

  for (addr = 0; addr < n; addr += page_size) {
//    report_progress (addr, n_bytes, NULL);

    if ((addr + page_size > n_bytes)) {
     block_size = n_bytes % page_size;
  }
  else {
     block_size = page_size;
  }
  
    tries = 0;
  retry:
    tries++;
    stk500_loadaddr(addr/a_div);
    buf[0] = Cmnd_STK_READ_PAGE;
    buf[1] = (block_size >> 8) & 0xff;
    buf[2] = block_size & 0xff;
    buf[3] = memtype;
    buf[4] = Sync_CRC_EOP;
    stk500_send(buf, 5);

    if (stk500_recv(buf, 1) < 0)
      exit(1);
    if (buf[0] == Resp_STK_NOSYNC) {
      if (tries > 33) {
        error(ERRORNOSYNC);
        return -3;
      }
      if (stk500_getsync() < 0)
  return -1;
      goto retry;
    }
    else if (buf[0] != Resp_STK_INSYNC) {
      error1(ERRORPROTOSYNC, buf[0]);
      return -4;
    }

    if (stk500_recv(&m->buf[addr], block_size) < 0)
      exit(1);

    if (stk500_recv(buf, 1) < 0)
      exit(1);

    if (buf[0] != Resp_STK_OK) {
        error1(ERRORPROTOSYNC, buf[0]);
        return -5;
      }
    }
  

  return n_bytes;
}
#endif

/*
 * issue the 'program enable' command to the AVR device
 */
static int stk500_program_enable()
{
  unsigned char buf[16];
  int tries=0;

 retry:
  
  tries++;

  buf[0] = Cmnd_STK_ENTER_PROGMODE;
  buf[1] = Sync_CRC_EOP;

  stk500_send( buf, 2);
  if (stk500_recv( buf, 1) < 0)
    exit(1);
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      error(ERRORNOSYNC);
      return -1;
    }
    if (stk500_getsync()< 0)
      return -1;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    error1(ERRORPROTOSYNC,buf[0]);
    return -1;
  }

  if (stk500_recv( buf, 1) < 0)
    exit(1);
  if (buf[0] == Resp_STK_OK) {
    return 0;
  }
  else if (buf[0] == Resp_STK_NODEVICE) {
    error(ERRORNODEVICE);
    return -1;
  }

  if(buf[0] == Resp_STK_FAILED)
  {
      error(ERRORNOPROGMODE);
    return -1;
  }


  error1(ERRORUNKNOWNRESP,buf[0]);

  return -1;
}

static void stk500_disable()
{
  unsigned char buf[16];
  int tries=0;

 retry:
  
  tries++;

  buf[0] = Cmnd_STK_LEAVE_PROGMODE;
  buf[1] = Sync_CRC_EOP;

  stk500_send( buf, 2);
  if (stk500_recv( buf, 1) < 0)
    exit(1);
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      error(ERRORNOSYNC);
      return;
    }
    if (stk500_getsync() < 0)
      return;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    error1(ERRORPROTOSYNC,buf[0]);
    return;
  }

  if (stk500_recv( buf, 1) < 0)
    exit(1);
  if (buf[0] == Resp_STK_OK) {
    return;
  }
  else if (buf[0] == Resp_STK_NODEVICE) {
    error(ERRORNODEVICE);
    return;
  }

  error1(ERRORUNKNOWNRESP,buf[0]);

  return;
}
//original avrdude error messages get copied to ram and overflow, wo use numeric codes.
void error1(int errno,unsigned char detail){
  DEBUGP("error: ");
  DEBUGP(errno);
  DEBUGP(" detail: 0x");
  DEBUGPLN(detail,HEX);
}


void error(int errno){
  DEBUGP("error" );
  DEBUGPLN(errno);
}
void dumphex(unsigned char *buf,int len)
{
  for (int i = 0; i < len; i++)
  {
    if (i%16 == 0)
      DEBUGPLN();
    DEBUGP(buf[i],HEX);DEBUGP(" ");
  }
  DEBUGPLN();
}


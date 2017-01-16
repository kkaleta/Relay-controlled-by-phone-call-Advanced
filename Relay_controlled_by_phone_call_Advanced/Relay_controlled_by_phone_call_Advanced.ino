/*
  For the purposes of this program I have modified GSM-GPRS-GPS-Shield library from
  https://github.com/open-electronics/GSM-GPRS-GPS-Shield.
  This code is runnig on Arduino Nano clone and SIM800L modem.
  For proper communication with SIM800L parameters need to be set:
  -GSM.cpp
  #define _GSM_TXPIN_
  #define _GSM_RXPIN_
  -GSM.h
  #define GSM_ON
  If your SIM card require PIN code, open GSM.cpp,
  find SendATCmdWaitResp(F("AT+CPIN=XXXX"), 500, 50, "READY", 5); uncomment and fill XXXX

  http://theveel.com/
  Kamil Kaleta
*/

#include "SIM900.h"
#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <EEPROM.h>
#include "sms.h"
#include "call.h"
CallGSM call;
SMSGSM sms;
const int RELAY1 = A0;
const int RELAY2 = A1;
const int callsAddr = 0;
const int openingAddr = 1;

//I`m counting calls and relay opening
//Liczę przychodzące rozmowy oraz uruchomienie przekaźnika
int calls = 1;
int opening = 1;
char spos;
char number[13];
char sim_number[13];
char reply[210];
char stat;
char message[159];

//Comment to disable debug with Serial.
//Zakomentowanie wyłączy konsolę Serial
#define LOCAL_DEBUG

//Comment line below to disable sms sent (when testing/debuging)
//Zakomentowanie wyłączy wysyłanie wiadomości SMS np podczas debugowania
#define SMS_ON

void setup()
{
  spos = -1;
  Serial.begin(9600);
  Serial.println(F("> GSM Shield testing"));
  if (gsm.begin(2400)) {
    Serial.println(F("> Status READY"));
  }
  else Serial.println(F("> Status IDLE"));
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  gsm.SimpleWriteln(F("AT+CMEE=2"));
  checkSMS();
};

void loop()
{
  byte b = call.CallStatus();
  if (b == CALL_INCOM_VOICE) {
#ifdef LOCAL_DEBUG
    Serial.print(F("> Incomming phone call from "));
#endif
    if (call.CallStatusWithAuth(number, 1, 100) == CALL_INCOM_VOICE_AUTH) {
#ifdef LOCAL_DEBUG
      Serial.println(F(" Allowed <"));
#endif

      //Pick up, wait a second, hang up and close/open relay, increase opening counter
      //Odbierz rozmowę, poczekaj chwilę, rozłącz rozmowę i zamknij/otwórz przekaźnik, inkrementuj licznik otwarć
      call.PickUp();
      delay(100);
      call.HangUp();
      digitalWrite(RELAY1, LOW);
      delay(1000);
      digitalWrite(RELAY1, HIGH);
      opening = EEPROM.read(openingAddr) + 1;
      EEPROM.update(openingAddr, opening);
    } else {
#ifdef LOCAL_DEBUG
      Serial.println(F(" Forbidden <"));
#endif
      call.PickUp();
      delay(100);
      call.HangUp();
    }
    calls = EEPROM.read(callsAddr) + 1;
    EEPROM.update(callsAddr, calls);
  } else {
    checkSMS();
  }
};

void pinInterrupt(void) {
  detachInterrupt(1);
}

void sleepNow(void)
{
  //Arduino is awake by SIM800L. Check data from modem, starts loop and go sleep again.
  //Modem RING pin is connected to D1 pin (interrupt1)
  //Arduino jest wybudzane przez modem SIM800L, uruchamia pętlę i ponownie zasypia
  //Pin RING modemu podłączony jest do pinu D1 Arduino (przerwanie1)

  //Without this modem will continue sending +creg reply which will wake up arduino for no reason
  //Bez tej komendy modem będzie cyklicznie wybudzał arduino
  gsm.SendATCmdWaitResp("AT+CREG=0", 2000, 50, "OK", 2, reply );
#ifdef LOCAL_DEBUG
  Serial.println(F("< Fell asleep >"));
#endif
  attachInterrupt(1, pinInterrupt, LOW);
  delay(100);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_mode();
  sleep_disable();
#ifdef LOCAL_DEBUG
  Serial.println(F("\n< Woke up >"));
#endif

  //Important delay
  //Ważne opóźnienie
  delay(2000);
}

void checkSMS() {
#ifdef LOCAL_DEBUG
  Serial.println(F("> Check text Messages"));
#endif
  gsm.SendATCmdWaitResp("AT+CREG=2", 2000, 50, "OK", 2, reply );
  delay(1000);
  spos = sms.IsSMSPresent(SMS_ALL);
  Serial.println(spos);
  message[0] = '\0';
  number[0] = '\0';
  sim_number[0] = '\0';
  reply[0] = '\0';
  byte admNrValid = 0;
  if ((int)spos > 0) {

    //Check if there is a contact on position 1 simcard, it`s where it keeps Administrator phone number
    //Sprawdź czy na 1 pozycji karty SIM znajduje się kontakt - na pierwszej pozycji przechowywany jest numed administratora
    if (1 == gsm.GetPhoneNumber(1, number)) {
#ifdef LOCAL_DEBUG
      Serial.print(F("\n@ Administrator phone number\n@ ")); Serial.println(number);
#endif
      admNrValid = 1;
    } else {
#ifdef LOCAL_DEBUG
      Serial.print(F("> No Administrator phone number!"));
#endif
    }

    //Ckeck if Authorized SMS (from Admin, whitch is positin 1 on sim card)
    //Sprawdź czy SMS autoryzacyjny - czy nadawcą jest Administrator
    stat = sms.GetAuthorizedSMS((int)spos, number, 13, message, 160, 1, 1);
#ifdef LOCAL_DEBUG
    Serial.print(F("\n########## SMS ############\n# Position on list:"));
    Serial.println((int)spos);
    Serial.print(F("# From:"));
    Serial.println((char *)number);
    Serial.print(F("# Message:"));
    Serial.println(message);
#endif
    if (stat == GETSMS_AUTH_SMS) {

      //Split sms body to keywords
      //Podziel wiadomość SMS na komendy
      char delimiters[] = ";";
      char* valPosition;
      valPosition = strtok(message, delimiters);
      char* smsWords[] = {0, 0, 0};
      int i;
      while (valPosition != NULL) {
        smsWords[i] = valPosition;
#ifdef LOCAL_DEBUG
        Serial.print(F("# smsWords["));
        Serial.print(i); Serial.print(F("] = "));
        Serial.println(smsWords[i]);
#endif
        valPosition = strtok(NULL, delimiters);
        i++;
      }
      /*
        SMS keywords
        Wyciągniete słowa kluczowe z SMSa
        smsWords[0]=[C]ontact, [A]dmin
        smsWords[1]=[A]add, [D]elete, [F]ind,[A]tt,[U]ssd
        smsWords[2]=phoneNumber,[A]ll,atCommand, ussdCode
        smsWords[3]=contatName
        smsWords[4]=[N]otify

        Available sms commands
        Dostępne komendy SMS
        C;A;xxxxxxxxx;John Doe;N  - will add (if not exist) John Doe contact and send him a message
        C;A;xxxxxxxxx;John Doe    - will add (if not exist) John Doe contact
        C;D;xxxxxxxxx             - will delete contact with number xxxxxxxxx (if exist)
        C;D;A                     - will delete ALL contacts (Administrator too)
        C;F;xxxxxxxxx             - will find if number xxxxxxxxx exist on phonebook
        A;A;AT+CBC                - will execute AT command (e.g. AT+CBC) and send modem answer
        A;U;someCode              - will execute USSD code
        A;I                       - will send replay message with some details
      */

      //Parse incomming message
      //Dekodowanie wiadomości SMS
      if (strcmp(smsWords[0], "C") == 0)
      {
#ifdef LOCAL_DEBUG
        Serial.println(F("[C]ontact"));
#endif
        if (strcmp(smsWords[1], "A") == 0)
          //[C]ontact-[A]dd
        {
#ifdef LOCAL_DEBUG
          Serial.println(F("[C]ontact-[A]dd"));
#endif
          byte p = isPhoneNumber(smsWords[2]);
          if (p > 0)
          {
            char smsBody[160] = {0};
            strcat(smsBody, "Phone number ");
            strcat(smsBody, smsWords[2]);
            strcat(smsBody, " ALREADY found on position ");
            char buffer [20];//iteger as char
            itoa ( p, buffer, 10);
            strcat(smsBody, buffer);
#ifdef SMS_ON

            //Send SMS with smsBody to Administrator (1 simcard contact position)
            //Wyślij SMS do Administratora (kontakt na 1 pozycji karty SIM)
            sms.SendSMS(1, smsBody);
#endif
#ifdef LOCAL_DEBUG
            Serial.print("\nSMS ");
            Serial.println(smsBody);
#endif
          } else {
#ifdef LOCAL_DEBUG
            Serial.print(F("Have NOT found given number.I will try to add "));
            Serial.println(smsWords[2]);
#endif
            if (1 == gsm.WritePhoneNumber(0, smsWords[2], smsWords[3]))
            {
              byte b = isPhoneNumber(smsWords[2]);
              if (strcmp(smsWords[4], "N") == 0)
                //NOTIFY USER
              {
#ifdef SMS_ON

                //Send SMS to b, which is new contact simcard index
                //Wyślij SMS do b, gdzie b to index dodawanego kontaktu
                sms.SendSMS(b, "Your phone number has been registered");
#endif
#ifdef LOCAL_DEBUG
                Serial.print(F("[SMS] "));
                Serial.println(F("Your phone number has been registered."));
#endif
              }
              char smsBody[160] = {0};
              strcat(smsBody, "Phone number ");
              strcat(smsBody, smsWords[2]);
              strcat(smsBody, " has been registered on position ");
              char buffer [20];//iteger as char
              itoa ( b, buffer, 10);
              strcat(smsBody, buffer);
#ifdef SMS_ON
              sms.SendSMS(1, smsBody);
#endif
#ifdef LOCAL_DEBUG
              Serial.print(F("\n[SMS]  "));
              Serial.println(smsBody);
#endif
            }
          }
        } else if (strcmp(smsWords[1], "D") == 0)
          //[C]ontact-[D]elete
        {
#ifdef LOCAL_DEBUG
          Serial.println(F("[C]ontact-[D]elete"));
#endif
          if (strcmp(smsWords[2], "A") == 0) {

            //[C]ontact-[D]elete-[A]ll
#ifdef LOCAL_DEBUG
            Serial.println(F("[C]ontact-[D]elete-[A]ll"));
#endif
            for (int i = 1; i < 251; i++) {
              gsm.DelPhoneNumber(i);
            }
            gsm.SendATCmdWaitResp("AT+CPBR=1,250", 20000, 50, "OK", 5, reply );
#ifdef SMS_ON
            sms.SendSMS(number, (char *)reply);
#endif
#ifdef LOCAL_DEBUG
            Serial.print(F("[SMS] "));
            Serial.println((char *)reply);
#endif
          } else {
            //Will delete single contact but is number on phonebook?
            //Usuwa kontakt jeżlei znajdzie na karcie SIM
            byte p = isPhoneNumber(smsWords[2]);
            if (p > 0)
            {
              if (1 == gsm.DelPhoneNumber(p)) {
                char smsBody[160] = {0};
                strcat(smsBody, "Number ");
                strcat(smsBody, sim_number);
                strcat(smsBody, " found on position ");
                char buffer[4];
                sprintf(buffer, "%d", p); //p to char
                strcat(smsBody, buffer);
                strcat(smsBody, ", and deleted");
#ifdef SMS_ON
                sms.SendSMS(1, smsBody);
#endif
#ifdef LOCAL_DEBUG
                Serial.print(F("[SMS] "));
                Serial.println(smsBody);
#endif
              } else {
                char smsBody[160] = {0};
                strcat(smsBody, "Found number ");
                strcat(smsBody, sim_number);
                strcat(smsBody, " on position ");
                strcat(smsBody, p);
                strcat(smsBody, ", but couldn't delete");
#ifdef SMS_ON
                sms.SendSMS(1, smsBody);
#endif
#ifdef LOCAL_DEBUG
                Serial.print(F("[SMS] "));
                Serial.println(smsBody);
#endif
              }
            }
          }
        } else if (strcmp(smsWords[1], "F") == 0)
        //[C]ontact-[F]ind
        {
#ifdef LOCAL_DEBUG
          Serial.println(F("[C]ontact-[F]ind"));
#endif
          byte p = isPhoneNumber(smsWords[2]);
          if (p > 0)
          {
            char smsBody[160] = {0};
            strcat(smsBody, "Phone number ");
            strcat(smsBody, smsWords[2]);
            strcat(smsBody, " found on position ");
            char buffer [20];
            itoa ( p, buffer, 10);
            strcat(smsBody, buffer);
#ifdef SMS_ON
            sms.SendSMS(1, smsBody);
#endif
#ifdef LOCAL_DEBUG
            Serial.print(F("[SMS] "));
            Serial.println(smsBody);
#endif
          } else {
            char smsBody[160] = {0};
            strcat(smsBody, "Phone number ");
            strcat(smsBody, smsWords[2]);
            strcat(smsBody, " has not been found");
#ifdef SMS_ON
            sms.SendSMS(1, smsBody);
#endif
#ifdef LOCAL_DEBUG
            Serial.print(F("[SMS] "));
            Serial.println(smsBody);
#endif
          }
        }
      } else if (strcmp(smsWords[0], "A") == 0) {
#ifdef LOCAL_DEBUG
        Serial.println(F("[A]dministrator"));
#endif
        if (strcmp(smsWords[1], "A") == 0)
        //[A]dministrator-[A]T
        {
#ifdef LOCAL_DEBUG
          Serial.println(F("[A]dministrator-[A]T"));
#endif
          gsm.SendATCmdWaitResp(smsWords[2], 5000, 50, "OK", 5, reply );
#ifdef SMS_ON
          if (strlen((char *)reply) > 159)
            sms.SendSMS(1, "AT Response too long for message");
          else
            sms.SendSMS(1, (char *)reply);
#endif
#ifdef LOCAL_DEBUG
          Serial.print(F("[SMS] "));
          Serial.println((char *)reply);
#endif
        } else if (strcmp(smsWords[1], "U") == 0) { 
          //[A]dministrator-[U]SSD
#ifdef LOCAL_DEBUG
          Serial.println(F("[A]dministrator-[U]SSD"));
#endif
          char atussd[20] = {0};
          strcat(atussd, "AT+CUSD=1,\"");
          strcat(atussd, smsWords[2]);
          strcat(atussd, "\"");
          char st = (gsm.SendATCmdWaitResp(atussd, 5000, 100, "CUSD", 3, reply));

          if (2 == gsm.WaitResp(10000, 100, "CUSD")) {
            char b[160];
            memcpy(b, gsm.comm_buf + 2, strlen(gsm.comm_buf));
            b[160] = '\0';
#ifdef SMS_ON
            if (strlen(gsm.comm_buf) < 160)
              sms.SendSMS(1, b);
            else
              sms.SendSMS(1, "USSD response to long for SMS message");
#endif
#ifdef LOCAL_DEBUG
            Serial.print(F("[SMS] "));
            Serial.println((char *)(gsm.comm_buf));
#endif
          } else {
            Serial.print(F("Something wrong with USSD command"));
            Serial.println(reply);
#ifdef SMS_ON
            sms.SendSMS(1, reply);
#endif
          }
        } else if (strcmp(smsWords[1], "I") == 0) { /////[A]dministrator-[I]nfo
          char st[1];
          st[0] = '\0';
          char str_perc[3];
          str_perc[0] = '\0';
          char str_vol[6]; //4
          str_vol[0] = '\0';

#ifdef LOCAL_DEBUG
          Serial.println(F("[A]dministrator-[I]nfo"));
#endif
          reply[0] = '\0';
          opening = EEPROM.read(openingAddr);
          calls = EEPROM.read(callsAddr);
          char calls_;
          char smsBody[160] = {0};
          strcat(smsBody, "Calls: ");
          char buffer[10];
          sprintf(buffer, "%d", calls);
          strcat(smsBody, buffer);
          strcat(smsBody, ", Opening: ");
          sprintf(buffer, "%d", opening);
          strcat(smsBody, buffer);
          if (1 ==   gsm.getBattInf(st, str_perc, str_vol)) {
            strcat(smsBody, "\n");
            strcat(smsBody, "Batt ");
            strcat(smsBody, str_perc);
            strcat(smsBody, ", ");
            strcat(smsBody, str_vol);
          }
          if (1 == (gsm.readCellTimeDate(reply))) {
            strcat(smsBody, "\n");
            strcat(smsBody, reply);
          }
#ifdef SMS_ON
          sms.SendSMS(1, smsBody);
#endif
#ifdef LOCAL_DEBUG
          Serial.print(F("[SMS] "));
          Serial.println(smsBody);
#endif
        }
      }
#ifdef LOCAL_DEBUG
      Serial.print(F("####### Authorized ########\n"));
#endif
    } else {
#ifdef LOCAL_DEBUG
      Serial.print(F("###### NOT Authorized #####\n"));
#endif
      if (!admNrValid) {
        //Very first SMS (1 simcard position was empty, so I will set sender as Administrator)
        //Pierwszy SMS (pozycja 1 na karcie SIM jest pusta, więc ustawiam nadawcę jako Administratora)
#ifdef LOCAL_DEBUG
        Serial.print(F(">> "));
        Serial.print(number);
        Serial.println(F(", set as Administrator number"));
        Serial.print(F("###########################\n"));
#endif
        gsm.WritePhoneNumber(1, number, "Administrator");
#ifdef SMS_ON
        sms.SendSMS(1, "You are Administrator now");
#endif
      } else {
        
        //This SMS is NOT Authorized SMS and the Administrator is set. I`m FFD it to the Administrator.
        //SMS nieautoryzacyjny i już wpisany numer Administratora. Przesyłam do administratora, niech czyta.
        char smsBody[160] = {0};
        strcat(smsBody, "From: ");
        strcat(smsBody, (char *)number);
        strcat(smsBody, ", ");
        strcat(smsBody, message);
#ifdef SMS_ON
        sms.SendSMS(1, smsBody);
#endif
#ifdef LOCAL_DEBUG
        Serial.print(F("[SMS] "));
        Serial.println(smsBody);
#endif
      }
    }
    
    //Now message must be removed
    //Na koniec usuwam wiadomość SMS
#ifdef LOCAL_DEBUG
    Serial.print(F("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!\n! deleting messages \n! on position "));
    Serial.println((int)spos);
    int d = 1;
    for (int i = 0; i < 5; i++) {
      delay(1000);
      if (sms.DeleteSMS((int)spos) == 1)
      {
        Serial.println(F("! successfully completed"));
        d = 0;
        break;
      } else
        Serial.println(F("try delete message"));
    }
    if (d)
    {
      Serial.println(F("! !failed \n! try kill em all"));
      
      //This should not happen, but just in case delate all messages
      //Nie powinno mieć miejsca, na wszelki wypadek usuwam wszystkie wiadomości SMS
      gsm.SimpleWriteln(F("AT+CMGDA = \"DEL ALL\""));
    }
    Serial.print(F("!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));
#endif
  } else
  {
#ifdef LOCAL_DEBUG
    Serial.println(F("> No SMS"));
#endif
    sleepNow();
  }
}

byte isPhoneNumber(char *what_nr) {
  int found = 0;
  //250 max amount contacts on my SIM card phonebook, this loop below costs you 40 seconds
  //250 to maksymalna ilość kontaktów na mojej karcie SMS, ta pętla poniżej kosztuje 40 sekund
  for (byte i = 1; i < 251; i++) {
    //    Serial.print("*");
    //    if ((i % 25) == 0)
    //      Serial.print("\n");
    if (1 == gsm.GetPhoneNumber(i, sim_number)) {
      if (strstr(sim_number, what_nr))
      {
        found = i;
        break;
      }
    }
  }
  return found;
}



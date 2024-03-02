//
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <array>
#include <cctype>
#include "RscpProtocol.h"
#include "RscpTags.h"
#include "SocketConnection.h"
#include "AES.h"
#include <time.h>
#include "E3DC_CONF.h"
#include "SunriseCalc.hpp"
#include "awattar.hpp"
#include "avl_array.h"
//#include "MQTTClient.h"
#include "cJSON.h"
#include <string>
#include <iostream>
#include <fcntl.h>

// for convenience test4
// using json = nlohmann::json;

#define AES_KEY_SIZE        32
#define AES_BLOCK_SIZE      32

// json j;
static int iSocket = -1;
static int iAuthenticated = 0;
static int iBattPowerStatus = 0; // Status, ob schon mal angefragt,
static int iWBStatus = 0; // Status, WB schon mal angefragt, 0 inaktiv, 1 aktiv, 2 regeln
static int iLMStatus = 0; // Status, Load Management  negativer Wert in Sekunden = Anforderung + Warten bis zur nächsten Anforderung, der angeforderte Wert steht in iE3DC_Req_Load
static int iLMStatus2 = 0; // Status, Load Management  Peakshaving > 0 ist aktiv

static float fAvBatterie,fAvBatterie900;
static int iAvBatt_Count = 0;
static int iAvBatt_Count900 = 0;
static uint8_t WBToggel;
static uint8_t WBchar[8];
static uint8_t WBchar6[6]; // Steuerstring zur Wallbox
const uint16_t iWBLen = 6;


static AES aesEncrypter;
static AES aesDecrypter;
static uint8_t ucEncryptionIV[AES_BLOCK_SIZE];
static uint8_t ucDecryptionIV[AES_BLOCK_SIZE];

// static int32_t iPower_Grid;
static uint8_t iCurrent_WB;
static uint8_t iCurrent_Set_WB;
static float fPower_Grid,fVoltage,fCurrent;
static float fAvPower_Grid,fAvPower_Grid3600,fAvPower_Grid600,fAvPower_Grid60; // Durchschnitt ungewichtete Netzleistung der letzten 10sec
static int iAvPower_GridCount = 0;
static float fPower_WB;
static float fDCDC = 0; // Strommenge mit rechnen
static int32_t iPower_PV, iPower_PV_E3DC;
static int32_t iAvalPower = 0;
static int32_t iMaxBattLade; // dynnamische maximale Ladeleistung der Batterie, abhängig vom SoC
static int32_t iPower_Bat;
static float fPower_Bat;
static int ireq_Heistab; // verfügbare Leistung
static uint8_t iPhases_WB;
static uint8_t iCyc_WB;
static int32_t iBattLoad;
static int iPowerBalance,iPowerHome;
static uint8_t iNotstrom = 0;
static time_t tE3DC, tWBtime;
static int hh,mm,ss;

static int32_t iFc, iMinLade,iMinLade2; // Mindestladeladeleistung des E3DC Speichers
static float_t fL1V=230,fL2V=230,fL3V=230;
static int iDischarge = -1;
static bool bDischarge = true,bDischargeDone;  // Wenn false, wird das Entladen blockiert, unabhängig von dem vom Portal gesetzen wert
char cWBALG;
static bool bWBLademodus; // Lademodus der Wallbox; z.B. Sonnenmodus
static bool bWBChanged; // Lademodus der Wallbox; wurde extern geändertz.B. Sonnenmodus
static bool bWBConnect; // True = Dose ist verriegelt x08
static bool bWBStart; // True Laden ist gestartet x10
static bool bWBCharge; // True Laden ist gestartet x20
static bool bWBSonne;  // Sonnenmodus x80
static bool bWBStopped;  // Laden angehalten x40
static bool bWBmaxLadestrom,bWBmaxLadestromSave; // Ladestrom der Wallbox per App eingestellt.; 32=ON 31 = OFF
static int  iWBSoll,iWBIst; // Soll = angeforderter Ladestrom, Ist = aktueller Ladestrom
static int32_t iE3DC_Req_Load,iE3DC_Req_Load_alt,iE3DC_Req_LoadMode=0; // Leistung, mit der der E3DC-Seicher geladen oder entladen werden soll
float_t WWTemp; // Warmwassertemperatur
static float fht; // Reserve aus ht berechnet
// in der Simulation wird der höchste Peakwert hochgerechnet
// 
int forecastpeaktime;  //
float forecastpeak;    //
static u_int8_t btasmota_ch1 = 0; // Anforderung LWWP 0 = aus, 1 = ein; 2 = Preis
static u_int8_t btasmota_ch2 = 0; // Anforderung LWWP/PV-Anhebung 1=ww, 2=preis, 4=überschuss

SunriseCalc * location;
std::vector<ch_s> ch;  //charge hour
// std::vector<watt_s> w1;

avl_array<uint16_t, uint16_t, std::uint16_t, 10, true> oek; // array mit 10 Einträgen


FILE * pFile;
e3dc_config_t e3dc_config;
char Log[300];

int WriteLog()
{
  static time_t t,t_alt = 0;
    int day,hour;
    char fname[256];
    time(&t);
    FILE *fp;
    struct tm * ptm;
    ptm = gmtime(&t);

    day = (t%(24*3600*4))/(24*3600);
    hour = (t%(24*3600))/(3600*4)*4;

    if (e3dc_config.debug) {

    if (hour!=t_alt) // neuer Tag
    {
//        int tt = (t%(24*3600)+12*3600);
        sprintf(fname,"%s.%i.%i.txt",e3dc_config.logfile,day,hour);
        fp = fopen(fname,"w");       // altes logfile löschen
        fclose(fp);
    }
        sprintf(fname,"%s.%i.%i.txt",e3dc_config.logfile,day,hour);
        fp = fopen(fname, "a");
    if(!fp)
        fp = fopen(fname, "w");
    if(fp)
    fprintf(fp,"%s\n",Log);
        fclose(fp);}
        t_alt = hour;
;
return(0);
}

int WriteSoC()
{
    char fname[256];
    sprintf(fname,"soc.txt");

    FILE *fp;
        fp = fopen(fname,"a");       // altes logfile löschen
    if(!fp)
        fp = fopen(fname, "w");
    if(fp)
    {
        fprintf(fp,"%s\n",Log);
        fclose(fp);
    }
return(0);
}

int MQTTsend(char host[20],char buffer[127])

{
    char cbuf[128];
    if (strcmp(host,"0.0.0.0")!=0) //gültige hostadresse
    {
        sprintf(cbuf, "mosquitto_pub -r -h %s -t %s", host,buffer);
        int ret = system(cbuf);
        return ret;
    } else
    return(-1);
}
/*
int ControlLoadData(SRscpFrameBuffer * frameBuffer,int32_t Power,int32_t Mode ) {
    RscpProtocol protocol;
    SRscpValue rootValue;
    // The root container is create with the TAG ID 0 which is not used by any device.
    protocol.createContainerValue(&rootValue, 0);
    
    // request Power Meter information
    SRscpValue PMContainer;
//    Power = Power*-1;
    protocol.createContainerValue(&PMContainer, TAG_EMS_REQ_SET_POWER);
    protocol.appendValue(&PMContainer, TAG_EMS_REQ_SET_POWER_MODE,Mode);
    if (Mode > 0)
    protocol.appendValue(&PMContainer, TAG_EMS_REQ_SET_POWER_VALUE,Power);
    // append sub-container to root container
    protocol.appendValue(&rootValue, PMContainer);
    // free memory of sub-container as it is now copied to rootValue
    protocol.destroyValueData(PMContainer);
    // create buffer frame to send data to the S10
    protocol.createFrameAsBuffer(frameBuffer, rootValue.data, rootValue.length, true); // true to calculate CRC on for transfer
    // the root value object should be destroyed after the data is copied into the frameBuffer and is not needed anymore
    protocol.destroyValueData(rootValue);
    
    return 0;
}
*/
/*int ControlLoadData2(SRscpFrameBuffer * frameBuffer,int32_t iPower) {
    RscpProtocol protocol;
    SRscpValue rootValue;
    uint32_t uPower;
    if (iPower < 0) uPower = 0; else if (iPower>e3dc_config.maximumLadeleistung) uPower = e3dc_config.maximumLadeleistung; else uPower = iPower;
    // The root container is create with the TAG ID 0 which is not used by any device.
    protocol.createContainerValue(&rootValue, 0);
    
    // request Power Meter information
    SRscpValue PMContainer;
    protocol.createContainerValue(&PMContainer, TAG_EMS_REQ_SET_POWER_SETTINGS);
    protocol.appendValue(&PMContainer, TAG_EMS_POWER_LIMITS_USED,true);
   protocol.appendValue(&PMContainer, TAG_EMS_MAX_CHARGE_POWER,uPower);
//    protocol.appendValue(&PMContainer, TAG_EMS_MAX_DISCHARGE_POWER,300);
//    protocol.appendValue(&PMContainer, TAG_EMS_DISCHARGE_START_POWER,70);
    // append sub-container to root container
    protocol.appendValue(&rootValue, PMContainer);
    // free memory of sub-container as it is now copied to rootValue
    protocol.destroyValueData(PMContainer);
    
    
    
    
    // create buffer frame to send data to the S10
    protocol.createFrameAsBuffer(frameBuffer, rootValue.data, rootValue.length, true); // true to calculate CRC on for transfer
    // the root value object should be destroyed after the data is copied into the frameBuffer and is not needed anymore
    protocol.destroyValueData(rootValue);
    
    return 0;
}
*/
/*
int Control_MAX_DISCHARGE(SRscpFrameBuffer * frameBuffer,int32_t iPower) {
    RscpProtocol protocol;
    SRscpValue rootValue;
    uint32_t uPower;
    if (iPower < 0) uPower = 0; else if (iPower>e3dc_config.maximumLadeleistung) uPower = e3dc_config.maximumLadeleistung; else uPower = iPower;
    // The root container is create with the TAG ID 0 which is not used by any device.
    protocol.createContainerValue(&rootValue, 0);
    
    // request Power Meter information
    SRscpValue PMContainer;
    protocol.createContainerValue(&PMContainer, TAG_EMS_REQ_SET_POWER_SETTINGS);
    protocol.appendValue(&PMContainer, TAG_EMS_POWER_LIMITS_USED,true);
    if (uPower < 65)
    protocol.appendValue(&PMContainer, TAG_EMS_DISCHARGE_START_POWER,uPower);
    else
    protocol.appendValue(&PMContainer, TAG_EMS_DISCHARGE_START_POWER,uint32_t(65));
    protocol.appendValue(&PMContainer, TAG_EMS_MAX_DISCHARGE_POWER,uPower);
    // append sub-container to root container
    protocol.appendValue(&rootValue, PMContainer);
    // free memory of sub-container as it is now copied to rootValue
    protocol.destroyValueData(PMContainer);
    
    
    
    
    // create buffer frame to send data to the S10
    protocol.createFrameAsBuffer(frameBuffer, rootValue.data, rootValue.length, true); // true to calculate CRC on for transfer
    // the root value object should be destroyed after the data is copied into the frameBuffer and is not needed anymore
    protocol.destroyValueData(rootValue);
    
    return 0;
}

*/
int createRequestWBData(SRscpFrameBuffer * frameBuffer) {
    RscpProtocol protocol;
    SRscpValue rootValue;
    if (iWBStatus<12)
    iWBStatus=12;
    
    // The root container is create with the TAG ID 0 which is not used by any device.
    protocol.createContainerValue(&rootValue, 0);
    
    // request Power Meter information
    SRscpValue WBContainer;
    SRscpValue WB2Container;

    // request Wallbox data

    protocol.createContainerValue(&WBContainer, TAG_WB_REQ_DATA) ;
    // add index 0 to select first wallbox
    protocol.appendValue(&WBContainer, TAG_WB_INDEX,(uint8_t)e3dc_config.wallbox);

    
    protocol.createContainerValue(&WB2Container, TAG_WB_REQ_SET_EXTERN);
    protocol.appendValue(&WB2Container, TAG_WB_EXTERN_DATA_LEN,6);
    protocol.appendValue(&WB2Container, TAG_WB_EXTERN_DATA,WBchar6,iWBLen);
    iWBSoll = WBchar6[1];   // angeforderte Ladestromstärke;
    WBToggel = WBchar6[4];


    protocol.appendValue(&WBContainer, WB2Container);
    // free memory of sub-container as it is now copied to rootValue
    protocol.destroyValueData(WB2Container);

    
// append sub-container to root container
    protocol.appendValue(&rootValue, WBContainer);
//    protocol.appendValue(&rootValue, WB2Container);
    // free memory of sub-container as it is now copied to rootValue
    protocol.destroyValueData(WBContainer);
    
    // create buffer frame to send data to the S10
    protocol.createFrameAsBuffer(frameBuffer, rootValue.data, rootValue.length, true); // true to calculate CRC on for transfer
    // the root value object should be destroyed after the data is copied into the frameBuffer and is not needed anymore
    protocol.destroyValueData(rootValue);
    
    return 0;
}

int createRequestWBData2(SRscpFrameBuffer * frameBuffer) {
    RscpProtocol protocol;
    SRscpValue rootValue;
    if (iWBStatus<12)
    iWBStatus=12;
    
    // The root container is create with the TAG ID 0 which is not used by any device.
    protocol.createContainerValue(&rootValue, 0);
    
    // request Power Meter information
    SRscpValue WBContainer;
    SRscpValue WB2Container;

    // request Wallbox data

    protocol.createContainerValue(&WBContainer, TAG_WB_REQ_DATA) ;
    // add index 0 to select first wallbox
    protocol.appendValue(&WBContainer, TAG_WB_INDEX,(uint8_t)e3dc_config.wallbox);

    
    protocol.createContainerValue(&WB2Container, TAG_WB_REQ_SET_PARAM_1);
    protocol.appendValue(&WB2Container, TAG_WB_EXTERN_DATA_LEN,8);
    protocol.appendValue(&WB2Container, TAG_WB_EXTERN_DATA,WBchar,8);
    iWBSoll = WBchar[2];   // angeforderte Ladestromstärke;


    protocol.appendValue(&WBContainer, WB2Container);
    // free memory of sub-container as it is now copied to rootValue
    protocol.destroyValueData(WB2Container);

    
// append sub-container to root container
    protocol.appendValue(&rootValue, WBContainer);
//    protocol.appendValue(&rootValue, WB2Container);
    // free memory of sub-container as it is now copied to rootValue
    protocol.destroyValueData(WBContainer);
    
    // create buffer frame to send data to the S10
    protocol.createFrameAsBuffer(frameBuffer, rootValue.data, rootValue.length, true); // true to calculate CRC on for transfer
    // the root value object should be destroyed after the data is copied into the frameBuffer and is not needed anymore
    protocol.destroyValueData(rootValue);
    
    return 0;
}


static float fBatt_SOC = -1;
static float fBatt_SOC_alt;
static float fSavedtoday, fSavedyesderday,fSavedtotal,fSavedWB; // Überschussleistung
static int32_t iDiffLadeleistung, iDiffLadeleistung2;
static time_t tLadezeit_alt,tLadezeitende_alt,tE3DC_alt;
static time_t t,t_alt;
static time_t tm_CONF_dt;
static bool bCheckConfig;
bool CheckConfig()
{
    struct stat stats;
    time_t  tm_dt;
     stat(e3dc_config.conffile,&stats);
     tm_dt = *(&stats.st_mtime);
    if (tm_dt==tm_CONF_dt)
        return false; else return true;
}
bool GetConfig()
{
// ermitteln location der conf-file
    
    // get conf parameters
    bool fpread=false;
    struct stat stats;
    FILE *fp;
        fp = fopen(e3dc_config.conffile, "r");
        if(!fp) {
            sprintf(e3dc_config.conffile,"%s",CONF_FILE);
            fp = fopen(CONF_FILE, "r");
            }
    if(fp) {

        FILE *sfp;
        char fbuf[255];
        bool bf;
        sprintf(fbuf,"%s.check",e3dc_config.conffile);
        sfp = fopen(fbuf, "w");


        stat(e3dc_config.conffile,&stats);
        tm_CONF_dt = *(&stats.st_mtime);
        char var[128], value[128], line[256];
        strcpy(e3dc_config.server_ip, "0.0.0.0");
        strcpy(e3dc_config.heizstab_ip, "0.0.0.0");
        e3dc_config.heizstab_port = 502;
        strcpy(e3dc_config.heizung_ip, "0.0.0.0");
        strcpy(e3dc_config.mqtt_ip, "0.0.0.0");
        strcpy(e3dc_config.mqtt2_ip, "0.0.0.0");
        strcpy(e3dc_config.openWB_ip, "0.0.0.0");
        memset(e3dc_config.openweathermap,0,sizeof(e3dc_config.openweathermap));
        e3dc_config.wrsteuerung = 1; // 0 = aus, 1= aktiv, 2=debug ausgaben
        e3dc_config.wallbox = -1;
        e3dc_config.openWB = false;
        e3dc_config.openmeteo = false;
        e3dc_config.WP = false;
        e3dc_config.WPWolf = false;
        e3dc_config.WPSperre = false;
        e3dc_config.ext1 = false;
        e3dc_config.ext2 = false;
        e3dc_config.ext3 = false;
        e3dc_config.ext4 = false;
        e3dc_config.ext7 = false;
        sprintf(e3dc_config.logfile,"logfile");
        e3dc_config.debug = false;
        e3dc_config.wurzelzaehler = 0;
        e3dc_config.untererLadekorridor = UNTERERLADEKORRIDOR;
        e3dc_config.obererLadekorridor = OBERERLADEKORRIDOR;
        e3dc_config.minimumLadeleistung = MINIMUMLADELEISTUNG;
        e3dc_config.maximumLadeleistung = MAXIMUMLADELEISTUNG;
        e3dc_config.powerfaktor = POWERFAKTOR;
        e3dc_config.wrleistung = WRLEISTUNG;
        e3dc_config.speichergroesse = SPEICHERGROESSE;
        e3dc_config.winterminimum = WINTERMINIMUM;
        e3dc_config.RB = -1;
        e3dc_config.sommermaximum = SOMMERMAXIMUM;
        e3dc_config.RE = -1;
        e3dc_config.sommerladeende = SOMMERLADEENDE;
        e3dc_config.LE = -1;
        e3dc_config.einspeiselimit = EINSPEISELIMIT;
        e3dc_config.ladeschwelle = LADESCHWELLE;
        e3dc_config.ladeende = LADEENDE;
        e3dc_config.ladeende2 = LADEENDE2;
        e3dc_config.wbmaxladestrom = WBMAXLADESTROM;
        e3dc_config.unload = 100;
        e3dc_config.ht = 0;
        e3dc_config.htsat = false;
        e3dc_config.htsun = false;
        e3dc_config.hton = 0;
        e3dc_config.htoff = 24*3600; // in Sekunden
        e3dc_config.htsockel = 0;
        e3dc_config.peakshave = 0;
        e3dc_config.wbmode = 4;
        e3dc_config.wbminlade = 1000;
        e3dc_config.wbminSoC = 10;
        e3dc_config.wbhour = -1;
        e3dc_config.wbvon = -1;
        e3dc_config.wbbis = -1;
        e3dc_config.hoehe = 50;
        e3dc_config.laenge = 10;
        e3dc_config.aWATTar = 0;
        e3dc_config.AWLand = 1;   // 1 = DE 2 = AT
        e3dc_config.AWMWSt = -1;   // 19 = DE 20 = AT
        e3dc_config.AWNebenkosten = -1;
        e3dc_config.Avhourly = 10;   // geschätzter stündlicher Verbrauch in %
        e3dc_config.AWDiff = -1;   // Differenzsockel in €/MWh
        e3dc_config.AWAufschlag = 1.2;
        e3dc_config.AWSimulation = 0;
        e3dc_config.AWtest = 0;
        e3dc_config.AWReserve = 0;
        e3dc_config.BWWP_Power = 0;
        e3dc_config.BWWP_port = 6722;
        e3dc_config.BWWPein = 0;
        e3dc_config.BWWPaus = 0;
        e3dc_config.BWWPSupport = -1;
        e3dc_config.BWWPTasmotaDauer = 0;
        memset(e3dc_config.BWWPTasmota,0,sizeof(e3dc_config.BWWPTasmota));
        e3dc_config.soc = -1;
        e3dc_config.WPHeizlast = -1;
        e3dc_config.WPLeistung = -1;
        e3dc_config.WPHeizgrenze = -1;
        e3dc_config.WPmin = -1;
        e3dc_config.WPmax = -1;
        e3dc_config.WPPVon = -1;
        e3dc_config.WPPVoff = -100;
        e3dc_config.WPHK2on = -1;
        e3dc_config.WPHK2off = -1;
        e3dc_config.WPEHZ = -1;
        e3dc_config.WPZWE = -99;
        e3dc_config.WPZWEPVon = -1;
        e3dc_config.WPOffset = 2;
        e3dc_config.MQTTavl = -1;
        e3dc_config.DCDC = true;



        bf = true;

            while (fgets(line, sizeof(line), fp)) {
                fpread = true;
                memset(var, 0, sizeof(var));
                memset(value, 0, sizeof(value));
                if(sscanf(line, "%[^ \t=]%*[\t ]=%*[\t ]%[^\n]", var, value) == 2) {
                    for (int i = 0; i < strlen(var); i++)
                    var[i] = tolower(var[i]);
                    if(strcmp(var, "server_ip") == 0)
                        strcpy(e3dc_config.server_ip, value);
                    else if(strcmp(var, "bwwp_ip") == 0)
                        strcpy(e3dc_config.BWWP_ip, value);
                    else if(strcmp(var, "heizung_ip") == 0)
                        strcpy(e3dc_config.heizung_ip, value);
                    else if(strcmp(var, "heizstab_ip") == 0)
                        strcpy(e3dc_config.heizstab_ip, value);
                    else if(strcmp(var, "heizstab_port") == 0)
                        e3dc_config.heizstab_port = atoi(value);
                    else if(strcmp(var, "server_port") == 0)
                        e3dc_config.server_port = atoi(value);
                    else if(strcmp(var, "e3dc_user") == 0)
                        strcpy(e3dc_config.e3dc_user, value);
                    else if(strcmp(var, "e3dc_password") == 0)
                        strcpy(e3dc_config.e3dc_password, value);
                    else if(strcmp(var, "aes_password") == 0)
                        strcpy(e3dc_config.aes_password, value);
                    else if(strcmp(var, "openwb_ip") == 0)
                        strcpy(e3dc_config.openWB_ip, value);
                    else if(strcmp(var, "mqtt_ip") == 0)
                        strcpy(e3dc_config.mqtt_ip, value);
                    else if(strcmp(var, "mqtt2_ip") == 0)
                        strcpy(e3dc_config.mqtt2_ip, value);
                    else if(strcmp(var, "forecast1") == 0)
                        strcpy(e3dc_config.Forecast[0], value);
                    else if(strcmp(var, "forecast2") == 0)
                        strcpy(e3dc_config.Forecast[1], value);
                    else if(strcmp(var, "forecast3") == 0)
                        strcpy(e3dc_config.Forecast[2], value);
                    else if(strcmp(var, "forecast4") == 0)
                        strcpy(e3dc_config.Forecast[3], value);
                    else if(strcmp(var, "openweathermap") == 0)
                        strcpy(e3dc_config.openweathermap, value);
                    else if(strcmp(var, "bwwptasmota") == 0)
                        strcpy(e3dc_config.BWWPTasmota, value);
                    else if(strcmp(var, "bwwptasmotadauer") == 0)
                        e3dc_config.BWWPTasmotaDauer = atoi(value);
                    else if(strcmp(var, "wurzelzaehler") == 0)
                        e3dc_config.wurzelzaehler = atoi(value);
                    else if(strcmp(var, "wrsteuerung") == 0)
                        e3dc_config.wrsteuerung = atoi(value);
                    else if((strcmp(var, "wallbox") == 0)){
                        if
                           (strcmp(value, "true") == 0)
                        e3dc_config.wallbox = 0;
                    else
                        if
                           (strcmp(value, "false") == 0)
                        e3dc_config.wallbox = -1;
                    else
                        e3dc_config.wallbox = atoi(value);}
                  else if((strcmp(var, "openwb") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.openWB = true;
                  else if((strcmp(var, "openmeteo") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.openmeteo = true;
                  else if((strcmp(var, "dcdc") == 0)&&
                          (strcmp(value, "false") == 0))
                      e3dc_config.DCDC = false;
                  else if((strcmp(var, "dcdc") == 0)&&
                          (strcmp(value, "true") == 0))
                      e3dc_config.DCDC = true;
                  else if((strcmp(var, "wp") == 0)&&
                          (strcmp(value, "true") == 0))
                      e3dc_config.WP = true;
                  else if((strcmp(var, "wpwolf") == 0)&&
                          (strcmp(value, "true") == 0))
                      e3dc_config.WPWolf = true;
                  else if((strcmp(var, "wpsperre") == 0)&&
                          (strcmp(value, "true") == 0))
                      e3dc_config.WPSperre = true;
                    else if((strcmp(var, "ext1") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.ext1 = true;
                    else if((strcmp(var, "ext2") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.ext2 = true;
                    else if((strcmp(var, "ext3") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.ext3 = true;
                    else if((strcmp(var, "ext4") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.ext4 = true;
                    else if((strcmp(var, "ext7") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.ext7 = true;
                    else if(strcmp(var, "logfile") == 0)
                        strcpy(e3dc_config.logfile, value);
                    else if((strcmp(var, "debug") == 0)&&
                            (strcmp(value, "true") == 0))
                    {e3dc_config.debug = true;

                        
                    }
                    else if(strcmp(var, "untererladekorridor") == 0)
                        e3dc_config.untererLadekorridor = atoi(value);
                    else if(strcmp(var, "obererladekorridor") == 0)
                        e3dc_config.obererLadekorridor = atoi(value);
                    else if(strcmp(var, "minimumladeleistung") == 0)
                        e3dc_config.minimumLadeleistung = atoi(value);
                    else if(strcmp(var, "powerfaktor") == 0)
                        e3dc_config.powerfaktor = atof(value);
                    else if(strcmp(var, "maximumladeleistung") == 0)
                        e3dc_config.maximumLadeleistung = atoi(value);
                    else if(strcmp(var, "wrleistung") == 0)
                        e3dc_config.wrleistung = atoi(value);
                    else if(strcmp(var, "speichergroesse") == 0)
                        e3dc_config.speichergroesse = atof(value);
                    else if(strcmp(var, "winterminimum") == 0)
                        e3dc_config.winterminimum = atof(value);
                    else if(strcmp(var, "wpheizlast") == 0)
                        e3dc_config.WPHeizlast = atof(value);
                    else if(strcmp(var, "wpheizgrenze") == 0)
                        e3dc_config.WPHeizgrenze = atof(value);
                    else if(strcmp(var, "wpleistung") == 0)
                        e3dc_config.WPLeistung = atof(value);
                    else if(strcmp(var, "wpmin") == 0)
                        e3dc_config.WPmin = atof(value);
                    else if(strcmp(var, "wpmax") == 0)
                        e3dc_config.WPmax = atof(value);
                    else if(strcmp(var, "wppvon") == 0)
                        e3dc_config.WPPVon = atof(value);
                    else if(strcmp(var, "wppvoff") == 0)
                        e3dc_config.WPPVoff = atof(value);
                    else if(strcmp(var, "wphk2on") == 0)
                        e3dc_config.WPHK2on = atof(value);
                    else if(strcmp(var, "wphk2off") == 0)
                        e3dc_config.WPHK2off = atof(value);
                    else if(strcmp(var, "wpehz") == 0)
                        e3dc_config.WPEHZ = atof(value);
                    else if(strcmp(var, "wpzwe") == 0)
                        e3dc_config.WPZWE = atof(value);
                    else if(strcmp(var, "wpzwepvon") == 0)
                        e3dc_config.WPZWEPVon = atof(value);
                    else if(strcmp(var, "wpoffset") == 0)
                        e3dc_config.WPOffset = atof(value);
                    else if(strcmp(var, "bwwpein") == 0)
                        e3dc_config.BWWPein = atof(value);
                    else if(strcmp(var, "bwwpaus") == 0)
                        e3dc_config.BWWPaus = atof(value);
                    else if(strcmp(var, "bwwpsupport") == 0)
                        e3dc_config.BWWPSupport = atof(value);
                    else if(strcmp(var, "rb") == 0)
                        e3dc_config.RB = atof(value);
                    else if(strcmp(var, "sommermaximum") == 0)
                        e3dc_config.sommermaximum = atof(value);
                    else if(strcmp(var, "re") == 0)
                        e3dc_config.RE = atof(value);
                    else if(strcmp(var, "sommerladeende") == 0)
                        e3dc_config.sommerladeende = atof(value);
                    else if(strcmp(var, "le") == 0)
                        e3dc_config.LE = atof(value);
                    else if(strcmp(var, "einspeiselimit") == 0)
                        e3dc_config.einspeiselimit = atof(value);
                    else if(strcmp(var, "ladeschwelle") == 0)
                        e3dc_config.ladeschwelle = atoi(value);
                    else if(strcmp(var, "ladeende") == 0)
                        e3dc_config.ladeende = atoi(value);
                    else if(strcmp(var, "ladeende2") == 0)
                        e3dc_config.ladeende2 = atoi(value);
                    else if(strcmp(var, "unload") == 0)
                        e3dc_config.unload = atoi(value);
                    else if(strcmp(var, "htmin") == 0)
                        e3dc_config.ht = atoi(value);
                    else if(strcmp(var, "htsockel") == 0)
                        e3dc_config.htsockel = atoi(value);
                    else if(strcmp(var, "wbmode") == 0)
                        e3dc_config.wbmode = atoi(value);
                    else if(strcmp(var, "wbminlade") == 0)
                        e3dc_config.wbminlade = atoi(value);
                    else if(strcmp(var, "wbmaxladestrom") == 0)
                        e3dc_config.wbmaxladestrom = atoi(value);
                    else if(strcmp(var, "wbhour") == 0)
                        e3dc_config.wbhour = atoi(value);
                    else if(strcmp(var, "wbvon") == 0)
                        e3dc_config.wbvon = atoi(value);
                    else if(strcmp(var, "wbbis") == 0)
                        e3dc_config.wbbis = atoi(value);
                    else if(strcmp(var, "wbminsoc") == 0)
                        e3dc_config.wbminSoC = atof(value);
                    else if(strcmp(var, "hoehe") == 0)
                        e3dc_config.hoehe = atof(value);
                    else if(strcmp(var, "laenge") == 0)
                        e3dc_config.laenge = atof(value);
                    else if(strcmp(var, "awsimulation") == 0)
                        e3dc_config.AWSimulation = atoi(value); // max länge eines Wintertages
                    else if(strcmp(var, "peakshave") == 0)
                        e3dc_config.peakshave = atoi(value); // in Watt
                    else if(strcmp(var, "soc") == 0)
                        e3dc_config.soc = atoi(value); // in Watt
                    else if(strcmp(var, "hton") == 0)
                        e3dc_config.hton = atof(value)*3600; // in Sekunden
                    else if(strcmp(var, "htoff") == 0)
                        e3dc_config.htoff = atof(value)*3600; // in Sekunden
                    else if((strcmp(var, "htsat") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.htsat = true;
                    else if((strcmp(var, "htsun") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.htsun = true;
                    else if(strcmp(var, "awattar") == 0)
                    {if (strcmp(value, "true") == 0)
                                 e3dc_config.aWATTar = 1;
                        else
                                 e3dc_config.aWATTar = atoi(value);}
                    else if((strcmp(var, "awland") == 0)&&
                            (strcmp(value, "at") == 0))   // AT = 2
                        e3dc_config.AWLand = 2;
                    else if((strcmp(var, "awland") == 0)&&
                            (strcmp(value, "AT") == 0))   // AT = 2
                        e3dc_config.AWLand = 2;
                    else if((strcmp(var, "awland") == 0)&&
                            (strcmp(value, "de") == 0))   // DE = 1
                        e3dc_config.AWLand = 1;
                    else if(strcmp(var, "avhourly") == 0)
                        e3dc_config.Avhourly = atof(value); // % der SoC
                    else if(strcmp(var, "awdiff") == 0)
                        e3dc_config.AWDiff = atof(value)*10; // % der SoC
                    else if(strcmp(var, "awaufschlag") == 0)
                        e3dc_config.AWAufschlag = 1 + atof(value)/100;
                    else if(strcmp(var, "awnebenkosten") == 0)
                        e3dc_config.AWNebenkosten = atof(value);
                    else if(strcmp(var, "awmwst") == 0)
                        e3dc_config.AWMWSt = atof(value);
                    else if(strcmp(var, "awreserve") == 0)
                        e3dc_config.AWReserve = atof(value);
                    else if(strcmp(var, "mqtt/aval") == 0)
                        e3dc_config.MQTTavl = atoi(value);
                    else if(strcmp(var, "awtest") == 0)
                        e3dc_config.AWtest = atoi(value); // Testmodus 0 = Idel, 1 = Entlade, 2 = Netzladen mit Begrenzung 3 = Netzladen ohne Begrenzung
                    else
                        bf = false;
                    
                    if (bf)
                        fprintf(sfp,"%s = %s\n",var,value);
                    else
                        bf = true;



                }
            }
    //        printf("e3dc_user %s\n",e3dc_config.e3dc_user);
    //        printf("e3dc_password %s\n",e3dc_config.e3dc_password);
    //        printf("aes_password %s\n",e3dc_config.aes_password);
            fclose(fp);
            fclose(sfp);
/*        if (e3dc_config.RB < 0)
            e3dc_config.RB = (e3dc_config.winterminimum-(e3dc_config.sommermaximum-e3dc_config.winterminimum)/2);
        if (e3dc_config.RE < 0)
            (e3dc_config.winterminimum+(e3dc_config.sommermaximum-e3dc_config.winterminimum)/2);
        if (e3dc_config.LE < 0)
            (e3dc_config.LE = e3dc_config.sommerladeende);
*/
        if (e3dc_config.AWMWSt<0)
        {
            if (e3dc_config.AWLand <= 1)
                e3dc_config.AWMWSt = 19;
            else
                e3dc_config.AWMWSt = 20;
            
        }
        if ((e3dc_config.AWNebenkosten > 0)&&(e3dc_config.AWDiff<0))
        e3dc_config.AWDiff = (e3dc_config.AWNebenkosten/(e3dc_config.AWMWSt+100) * (e3dc_config.AWAufschlag-1)*1000);
        if (e3dc_config.powerfaktor < 0)
            e3dc_config.powerfaktor = (float(e3dc_config.maximumLadeleistung)/(e3dc_config.obererLadekorridor-e3dc_config.untererLadekorridor));
        if (e3dc_config.aWATTar > 0) {
// wenn awattar dann hton/htoff deaktivieren
            e3dc_config.htoff = e3dc_config.hton+1;
            e3dc_config.htsat = false;
            e3dc_config.htsun = false;        }
    }


    if ((!fp)||not (fpread)) printf("Configurationsdatei %s nicht gefunden",CONF_FILE);
    return fpread;
}

int wphl,wppw,wpswk,wpkst,wpkt,wpzl;  //heizleistung und stromaufnahme wärmepumpe
time_t tLadezeitende,tLadezeitende1,tLadezeitende2,tLadezeitende3;  // dynamische Ladezeitberechnung aus dem Cosinus des lfd Tages. 23 Dez = Minimum, 23 Juni = Maximum
static int isocket = -1;
long iLength;
const char * relay_ip;
//const char * cmd;
static std::vector<uint8_t> send;
static std::vector<uint8_t> receive;
int iPort = e3dc_config.BWWP_port;
typedef struct {
    uint16_t Tid; //transaktion_ID
    uint16_t Pid; //Protokoll_ID
    uint16_t Mlen; //Msglength
    uint8_t Dev;  //Geräte_ID
    uint8_t Fcd;  //Function Code
    uint16_t Reg; //Register
    uint16_t Count; //Number Register
    uint16_t Value; //Wert
    char data[195];
}Modbus_send;
typedef struct {
    uint16_t Tid; //transaktion_ID
    uint16_t Pid; //Protokoll_ID
    uint16_t Mlen; //Msglength
    uint8_t Dev;  //Geräte_ID
    uint8_t Fcd;  //Function Code
    uint16_t Reg; // Content Register
    uint16_t Count; //anzahl Register
    char data[197];
}Modbus_receive;

std::array<int, 20> oekofen{2,11,12,13,21,22,23,31,32,33,41,42,43,60,61,73,78,101,102,105};
static int x1 = 0;
static int16_t temp[oekofen.size()];
static uint8_t tn = 1;

int iModbusTCP_Set(int reg,int val,int tac)
{
    send.resize(12);
    receive.resize(125);

    Modbus_send Msend;
    Msend.Tid = (tn*256+reg);
    tn++;
    Msend.Pid = 0;
    Msend.Mlen = 6*256;
    Msend.Dev = 1;  // Devadr. 1 oder 255
    Msend.Fcd = 6; // Funktioncode
    //            Msend.Fcd = 6; // Funktioncode
    Msend.Reg = reg*256;  // Adresse Register // Aussentemperatur
    Msend.Count = val*256; // Inhalt    int size = send.size();
    memcpy(&send[0],&Msend,send.size());
    iLength = SocketSendData(isocket,&send[0],send.size());
    iLength = SocketRecvData(isocket,&receive[0],receive.size());

    return iLength;
}
int iModbusTCP_Get(int reg,int val,int tac)
{
    send.resize(12);
    receive.resize(1024);

    Modbus_send Msend;
    Msend.Tid = (tn*256+tac);
    tn++;
    Msend.Pid = 0;
    Msend.Mlen = 6*256;
    Msend.Dev = 1;  // Devadr. 1 oder 255
    Msend.Fcd = 3; // Funktioncode
    //            Msend.Fcd = 6; // Funktioncode
    Msend.Reg = reg*256;  // Adresse Register // Aussentemperatur
    Msend.Count = val*256; // Inhalt    int size = send.size();
    memcpy(&send[0],&Msend,send.size());
    iLength = SocketSendData(isocket,&send[0],send.size());
    iLength = SocketRecvData(isocket,&receive[0],receive.size());

    return iLength;
}
static int bHK1off = 0; // wenn > 0 wird der HK ausgeschaltet
static int bHK2off = 0;
static int bWP = 0;

int iModbusTCP()
{
// jede Minute wird die Temperatur abgefragt, innerhalb 10sec muss die Antwort da sein, ansonsten wird die Verbindug geschlossen.
    static bool brequest = false;
    static time_t tlast = 0;
    time_t now;
    time(&now);
    static int  ret = 0;
    char server_ip[16];
    Modbus_send Msend;
    if (brequest||(not brequest&&(now-tlast)>10)) // 10 Sekunden auf die Antwoert warten
    {
        if (isocket < 0)
        {
            sprintf(server_ip,e3dc_config.heizung_ip);
            isocket = SocketConnect(server_ip, 502);
            ret = 0;
        }
        if (isocket > 0&&not brequest&&(now-tlast)>10) // Nur alle 20sec Anfrage starten
        {
            brequest = true;
            tlast = now;
            send.resize(12);
            receive.resize(1024);
  
// Heizkreise schalten in Abhängigkeit vom EVU und Status des jeweiligen heizkreis
// Wenn WP und oekofen aus sind, dann heizkreise ausschalten
// Wenn WP oder oekofen laufen Heizkreise einschalten
            
            if (temp[13] > 0) // Temperatur Puffer gesetzt?
            {
//  Kessel in Abhängigleit zu Aussentemperatur zu- und abschalten
//  Regelgröße ist die Zulufttemperatur der LWWP
//  daneben die aktuelle Temperatur vom Wetterportal
// und anstatt die Mitteltemperatur die aktuelle Temperatur zur Verifizierung
                float isttemp = (wolf[wpzl].wert + wetter[0].temp)/2;
                if ((now - wolf[wpzl].t > 300)||wolf[wpzl].wert<-90)
                   isttemp = wetter[0].temp;

                if (isttemp<(e3dc_config.WPZWE)&&temp[17]==0)
//                if (temp[0]<(e3dc_config.WPZWE)*10&&temp[17]==0)
                {
                    iLength  = iModbusTCP_Set(101,1,1); //Heizkessel register 101
                    iLength  = iModbusTCP_Get(101,1,1); //Heizkessel
                }
                if (isttemp>(e3dc_config.WPZWE+.5)&&temp[17]==1)
                {
                    iLength  = iModbusTCP_Set(101,0,7); //Heizkessel
                    iLength  = iModbusTCP_Get(101,1,7); //Heizkessel
                }


// Heizkreise schalten
                if ((tasmota_status[0]==0||temp[17]>0)&&temp[1]==0&&bHK1off==0)
                    // EVU Aus und Heizkreis Aus und WW Anforderung aus -> einschalten
                {
                    iLength  = iModbusTCP_Set(11,1,8); //FBH? register 11
                    iLength  = iModbusTCP_Get(11,1,8); //FBH?
                }
                if ((tasmota_status[0]==0||temp[17]>0)&&temp[7]==0&&bHK2off==0)
                    // EVU Aus und Heizkreis Aus und WW Anforderung aus -> einschalten
                {
                    iLength  = iModbusTCP_Set(31,1,7); //HZK? register 31
                    iLength  = iModbusTCP_Get(31,1,7); //HZK?
                }
                if (temp[1]==1&&((tasmota_status[0]==1&&temp[17]==0)
                    ||bHK1off>0))
// EVU aus und Kessel aus ODER WW Anforderung + Heizkreis aktiv -> HK ausschalten
                {
                    iLength  = iModbusTCP_Set(11,0,7); //FBH?
                    iLength  = iModbusTCP_Get(11,1,7); //FBH?
                }
                if (temp[7]==1&&((tasmota_status[0]==1&&temp[17]==0)
                    ||bHK2off>0))
// EVU aus und Kessel aus ODER WW Anforderung + Heizkreis aktiv -> HK ausschalten
                {
                    iLength  = iModbusTCP_Set(31,0,7); //HZK?
                    iLength  = iModbusTCP_Get(31,1,7); //HZK?
                }
            }
            {
                
                {
                    iLength = iModbusTCP_Get(2,105,0); // Alle Register auf einmal abfragen
                }
            }
        }
        else
            if (brequest)
            {
                if (iLength <= 0)
                    iLength = SocketRecvData(isocket,&receive[0],receive.size());
                if (iLength > 0)
                {
                    int x2 = 9;
                    int x3 = 0;
                    x1 = oekofen[receive[0]]; // Startregister
//                    x3 = x1;
                    if (receive[7]==3)
                        while (x2 < iLength)
                        {
                            if (oekofen[x3] == x1) // suchen der gewünschten register
                            {
                                temp[x3] = (receive[x2]*256+receive[x2+1]);
                                x3++;
                            }
                            WWTemp = float(receive[x2]*256+receive[x2+1])/10;
                            x1++;x2++;x2++;
                        }
                    else
                        WWTemp = float(receive[10]*256+receive[11])/10;
                    
                    //                SocketClose(isocket);
                    SocketClose(isocket);
                    isocket = -1;
                    brequest = false;
                } else
                    if (iLength == 0)
                    {
                        SocketClose(isocket);
                        isocket = -1;
                        brequest = false;
                    }
            }}
    return iLength;
}
int iModbusTCP_Heizstab(int ireq_power) // angeforderte Leistung
{
// Alle 10 sec wird der neue wert an den heizstab geschickt
    static int isocket = -1;
    static bool brequest = false;
    static time_t tlast = 0;
    static int maxpower = -1;
    time_t now;
    time(&now);
    char server_ip[16];
    Modbus_send Msend;
    static int iPower_Heizstab = 0;
        
    if ((now-tlast)>5)
    {
        if ((isocket < 0)&&(strcmp(e3dc_config.heizstab_ip, "0.0.0.0") != 0)&&ireq_power>0&&(now-tlast>10))
            
        {
            isocket = SocketConnect(e3dc_config.heizstab_ip, e3dc_config.heizstab_port);
            if (isocket < 0) // wenn der socket nicht verfügbar, eine Stunde warten
            tlast = now + 60;
        }
        if (isocket > 0)
        {
            //                brequest = true;
            tlast = now;
            send.resize(12);
            receive.resize(15);
            
            if (maxpower <= 0) // ermitteln maximale Leistung
            {
                Msend.Tid = 1*256;
                Msend.Pid = 0;
                Msend.Mlen = 6*256;
                Msend.Dev = 1;
                Msend.Fcd = 3; // Funktioncode auslesen
//                Msend.Fcd = 6; // Funktioncode
                Msend.Reg =  (1014%256)*256 + (1014/256);  // Adresse Register Leistung heizstab
                Msend.Count = 1*256; // Anzahl Register // 22.6° setzen
                memcpy(&send[0],&Msend,send.size());
                SocketSendData(isocket,&send[0],send.size());
                sleep(0.1);
// Auslesen der maximalen Leistung
            
                iLength = SocketRecvData(isocket,&receive[0],receive.size());
                if (iLength > 0)
                {
                    if (receive[7]==3)
                    {
                        maxpower = receive[9]*256+receive[10];
                    }
                }
            }

            
            iPower_Heizstab = iPower_Heizstab + ireq_power;
            if (iPower_Heizstab < 0) iPower_Heizstab = 0;
            if (maxpower < 3000) maxpower = 3000;
            if (iPower_Heizstab > maxpower) iPower_Heizstab = maxpower;
//            if (iPower_Heizstab > 10000) iPower_Heizstab = ireq_power;
            Msend.Tid = 1*256;
            Msend.Pid = 0;
            Msend.Mlen = 6*256;
            Msend.Dev = 1;
            Msend.Fcd = 6; // Funktioncode
            Msend.Reg =  (1000%256)*256 + (1000/256);  // Adresse Register Leistung heizstab
            //            Msend.Count = 1*256; // Anzahl Register // 22.6° setzen
            Msend.Count = (iPower_Heizstab%256)*256+ (iPower_Heizstab/256); // Leistung setzen
            memcpy(&send[0],&Msend,send.size());
            iLength = (SocketSendData(isocket,&send[0],send.size()));
            if (iLength <=0||(iPower_Heizstab==0))
            {
                SocketClose(isocket);
                if (isocket >= 0) isocket = -2;
                iPower_Heizstab = isocket;
            }
        }
        
        else
            iPower_Heizstab  = isocket;
    }
        return iPower_Heizstab;
}


int iRelayEin(const char * cmd)
{
//    if (isocket >= 0&&int32_t(e3dc_config.BWWP_ip)>0);
  {
    relay_ip = e3dc_config.BWWP_ip;
    int iPort = e3dc_config.BWWP_port;
    send.resize(2);
    receive.resize(8);
    memcpy(&send[0],&cmd[0],2);
    isocket = SocketConnect(relay_ip, iPort);
//      isocket = SocketConnect_noblock(relay_ip, iPort);
    if (isocket > 0){
        SocketSendData(isocket,&send[0],2);
    sleep(1);
       iLength = SocketRecvData(isocket,&receive[0],receive.size());
        SocketClose(isocket);
    }
  }
    return receive[0];
}
FILE *fp;
static char buf[127];
static int WP_status = -1;
int status,vdstatus;
std::string sverdichterstatus;
char path[4096];
wolf_s wo;

int wolfstatus()
{
    time_t now;
    time(&now);
    //           Test zur Abfrage des Tesmota Relais
    if (strcmp(e3dc_config.mqtt_ip,"0.0.0.0")!=0&&e3dc_config.WPWolf)
    {
        
 
        if (WP_status < 2)
        {
            if (wolf.size()==0)
            {
                wo.wert = 0;
                wo.t = 0;
                wo.status = "";
                wo.feld = "Vorlauftemperatur";
                wo.AK = "VL";
                wolf.push_back(wo);
/*                wo.feld = "Sammlertemperatur";
                wo.AK = "ST";
                wolf.push_back(wo);
*/                wo.feld = "Kesseltemperatur";
                wo.AK = "KT";
                wpkt = wolf.size();
                wolf.push_back(wo);
                wo.feld = "Kesselsolltemperatur";
                wo.AK = "KST";
                wpkst = wolf.size();
                wolf.push_back(wo);
                wo.feld = "Rücklauftemperatur";
                wo.AK = "RL";
                wolf.push_back(wo);
                wo.feld = "Heizleistung";
                wo.AK = "HL";
                wphl = wolf.size();
                wolf.push_back(wo);
                wo.feld = "Leistungsaufnahme (WP + EHZ)";
                wo.AK = "PW";
                wppw = wolf.size();
                wolf.push_back(wo);
                wo.feld = "Aktuelle Leistungsvorgabe Verdichter";
                wo.AK = "ALV";
                wolf.push_back(wo);
                wo.feld = "Heizkreisdurchfluss";
                wo.AK = "HD";
                wolf.push_back(wo);
                wo.feld = "Zulufttemperatur";
                wo.AK = "ZL";
                wo.wert = -99;
                wpzl = wolf.size();
                wolf.push_back(wo);
                wo.feld = "Sauggastemperatur";
                wo.AK = "SGT";
                wolf.push_back(wo);
                wo.feld = "Ablufttemperatur";
                wo.AK = "AL";
                wolf.push_back(wo);
                wo.feld = "BUSCONFIG_Sollwertkorrektur";
                wo.AK = "SWK";
                wo.wert = -5;
                wpswk = wolf.size();
                wolf.push_back(wo);
                wo.feld = "Betriebsart Heizgerät";
                wo.AK = "BHG";
                wolf.push_back(wo);
                wo.feld = "Verdichterstatus";
                wo.AK = "VS";
                wolf.push_back(wo);
                vdstatus = wolf.size();
            }
                
                
            fp = NULL;
           sprintf(buf,"mosquitto_sub -h %s -t Wolf/+/#",e3dc_config.mqtt_ip);
            fp = popen(buf, "r");
            int fd = fileno(fp);
            int flags = fcntl(fd, F_GETFL, 0);
            flags |= O_NONBLOCK;
            fcntl(fd, F_SETFL, flags);
            WP_status = 2;
        }
        if (fgets(path, 4096, fp) != NULL)
        {
            const cJSON *item = NULL;
            cJSON *wolf_json = cJSON_Parse(path);
        
            for(int x1=0;x1<wolf.size();x1++)
            {
                char * c = &wolf[x1].feld[0];
                item = cJSON_GetObjectItemCaseSensitive(wolf_json, c );
                if (item != NULL)
                {
                    wolf[x1].wert = item->valuedouble;
                    wolf[x1].t = now;
                    if (item->valuestring!= NULL)
                    wolf[x1].status = item->valuestring;
                    
                    if ((wolf[x1].feld=="Verdichterstatus")||
                        (wolf[x1].feld=="Betriebsart Heizgerät"))
                    {
                        item = item->child;
                        item = item->next;
                        wolf[x1].status = item->valuestring;
                    }
                }
            }
            
            cJSON_Delete(wolf_json);
                
        }
        if (WP_status < 2)
//            status = pclose(fp);
        return WP_status;
}
    return 0;
}

int mqtt()
{
if ((e3dc_config.MQTTavl > 0)&&(tE3DC % e3dc_config.MQTTavl) == 0)
{
    
    char buf[127];
//    sprintf(buf,"E3DC-Control/Avl -m %i",iAvalPower);
    sprintf(buf,"E3DC-Control/Aval -m '%i' ",ireq_Heistab);
    MQTTsend(e3dc_config.mqtt2_ip,buf);
    sprintf(buf,"E3DC-Control/BattL -m '%i' ",iBattLoad);
    MQTTsend(e3dc_config.mqtt2_ip,buf);
    if (e3dc_config.debug) printf("D4");

}
    return 0;
};


FILE *mfp;

int tasmotastatus(int ch)
{
    //           Test zur Abfrage des Tesmota Relais
    if (strcmp(e3dc_config.mqtt_ip,"0.0.0.0")!=0)
    {
        
        char buf[127];
        static int WP_status = -1;
        int status;
        char path[1024];
        if (WP_status < 2)
        {
            if (e3dc_config.debug) printf("W1");
            mfp == NULL;
            sprintf(buf,"mosquitto_sub -h %s -t stat/tasmota/POWER%i -W 1 -C 1",e3dc_config.mqtt_ip,ch);
            mfp = popen(buf, "r");
//            int fd = fileno(mfp);
//            int flags = fcntl(fd, F_GETFL, 0);
//            flags |= O_NONBLOCK;
//            fcntl(fd, F_SETFL, flags);
            WP_status = 2;
        }
        if (mfp != NULL)
        if (fgets(path, 1024, mfp) != NULL)
        {
            if (strcmp(path,"ON\n")==0)
                WP_status = 1;
            if (strcmp(path,"OFF\n")==0)
                WP_status = 0;
        }
//        if (WP_status < 2)
        if (mfp != NULL)
        status = pclose(mfp);
        if (e3dc_config.debug) printf("W2");
        return WP_status;
}
    return 0;
}
int tasmotaon(int ch)
{
    char buf[127];
    sprintf(buf,"cmnd/tasmota/POWER%i -m 1",ch);
    MQTTsend(e3dc_config.mqtt_ip,buf);
    tasmota_status[ch-1] = 1;
    return 0;
}
int tasmotaoff(int ch)
{
    char buf[127];
    sprintf(buf,"cmnd/tasmota/POWER%i -m 0",ch);
    MQTTsend(e3dc_config.mqtt_ip,buf);
    tasmota_status[ch-1] = 0;
    return 0;
}
typedef struct {
    time_t t;
    float fah, fsoc, fvoltage, fcurrent;
}soc_t;

int LoadDataProcess() {
    //    const int cLadezeitende1 = 12.5*3600;  // Sommerzeit -2h da GMT = MEZ - 2
    static int iLeistungHeizstab = 0;
    static int ich_Tasmota = 0;
    static time_t tasmotatime = 0;
    static soc_t high,low;
    static int itag = 24*3600;
    int x1,x2;
    static float fspreis;
    static time_t wpofftime = t;
    static time_t wpontime = t;  //
    
    
    // Speicher SoC selbst berechnen
    // Bei Sonnenuntergang wird je ein Datensatz mit den höchsten und niedrigsten SoC-Werten erstellt.
    if (e3dc_config.debug) printf("D1");
    if (e3dc_config.WPWolf)
        int ret = wolfstatus();
    time (&t);
    
    
    mqtt();
    
    if (e3dc_config.debug) printf("D2");
    
    
    fDCDC = fDCDC + fCurrent*(t-t_alt);
    if (fDCDC > high.fah) {
        high.fah = fDCDC;
        high.fsoc = fBatt_SOC;
        high.fvoltage = fVoltage;
        high.fcurrent = fCurrent;
        high.t = t;
    };
    if (fDCDC < low.fah) {
        low.fah = fDCDC;
        low.fsoc = fBatt_SOC;
        low.fvoltage = fVoltage;
        low.fcurrent = fCurrent;
        low.t = t;
    };
    // Bei Sonnenaufgang werden die Ausgangswerte neu gesetzt
    if (t % itag >= sunriseAt*60&&t_alt%itag < sunriseAt*60)
    {
        high.fah = fDCDC;
        high.fsoc = fBatt_SOC;
        high.fvoltage = fVoltage;
        high.fcurrent = fCurrent;
        high.t = t;
    }
    // Bei Sonnenuntergang werden High/Low in die Datei fortgeschrieben
    x1 = t % itag;
    x2 = t_alt % itag;
    
    if (e3dc_config.soc >=0)
        if ((t % itag >= sunsetAt*60&&t_alt%itag < sunsetAt*60)||
            (t % itag >= sunriseAt*60&&t_alt%itag < sunriseAt*60)||
            (e3dc_config.soc >0 &&t_alt % (60*e3dc_config.soc) > t % (60*e3dc_config.soc)))
        {
            tm *ts;
            soc_t *p;
            p=&high;
            ts = gmtime(&t);
            sprintf(Log,"Hoch %02i.%02i.%02i %02i:%02i Time %02i:%02i:%02i %0.04fAh SoC %0.04f%% %0.02fV %0.02fA",ts->tm_mday,ts->tm_mon,ts->tm_year-100,ts->tm_hour,ts->tm_min,
                    int((p->t%(24*3600))/3600),int((p->t%3600)/60),int(p->t%60),
                    p->fah/3600,p->fsoc,p->fvoltage,p->fcurrent);
            WriteSoC();
            p=&low;
            sprintf(Log,"Tief %02i.%02i.%02i %02i:%02i Time %02i:%02i:%02i %0.04fAh SoC %0.04f%% %0.02fV %0.02fA\n",ts->tm_mday,ts->tm_mon,ts->tm_year-100,ts->tm_hour,ts->tm_min,
                    int((p->t%(24*3600))/3600),int((p->t%3600)/60),int(p->t%60),
                    p->fah/3600,p->fsoc,p->fvoltage,p->fcurrent);
            WriteSoC();
            
            if (t % itag >= sunsetAt*60&&t_alt%itag < sunsetAt*60)
                low.fah = fDCDC;
            
        }
    t_alt = t;
    if (e3dc_config.debug) printf("D3");
    
    if (strcmp(e3dc_config.mqtt_ip,"0.0.0.0")!=0)
    {
        if (e3dc_config.debug) printf("D4");
        
        if (t-tasmotatime>10)
        {
            tasmota_status[ich_Tasmota] = tasmotastatus(ich_Tasmota+1);
            if (tasmota_status[ich_Tasmota]<2)
                ich_Tasmota++;
            if (ich_Tasmota > 3) ich_Tasmota = 0;
            tasmotatime = t;
            
        }
        if (tasmota_status[3] > 1)
            tasmota_status[3] = tasmotastatus(4);
        
        // Steuerung BWWP über Tasmota Kanal4
        if (tasmota_status[3]>=1&&temp[13]>e3dc_config.BWWPaus*10)
        {
            tasmotaoff(4);
            if (bHK1off & 2)
            bHK1off ^= 2;
            if (bHK2off & 2)
            bHK2off ^= 2;

        } else
        if (tasmota_status[3]==0&&temp[13]>0&&temp[13]<e3dc_config.BWWPein*10)
        {    
            tasmotaon(4);
            bHK1off |= 2;
            //            bHK2off |= 2;
        }
        // Steuerung LWWP über Tasmota Kanal2 Unterstützung WW Bereitung
        if (temp[2]>0)  // als indekation genutzt ob werte oekofen da
        {
            if (temp[17]==1&&(temp[19]==4||temp[18]>400)) // Kessel an + Leistungsbrand
            {
                // LWWP ausschalten wenn der Pelletskessel läuft
                // und keine Anforderungen anliegen
                if (btasmota_ch1 & 1)
                    btasmota_ch1 ^=1;
            } else
            {
                if (not e3dc_config.WPSperre&&bWP==0) //bWP > 0 LWWP ausschalten
                {
                    btasmota_ch1 |=1;
                    wpofftime = t;
                }
            }
        
            // LWWP bei günstigen Börsenpreisen laufen lassen WPZWEPVon
            //
            if (fcop>0)
                if (fspreis/fcop<e3dc_config.WPZWEPVon)
                {
                    if (btasmota_ch1 & 1)
                    {
                    } 
                    else
                        btasmota_ch1 |=2;

                } else
                {
                    if (btasmota_ch1 & 2)
                        btasmota_ch1 ^= 2;
                    if (btasmota_ch2 & 2)
                        btasmota_ch2 ^= 2;
                };
        }
        
        if  (e3dc_config.BWWPSupport>=0)
        {
            if  (temp[14]<(e3dc_config.BWWPein-e3dc_config.BWWPSupport)*10&&(tasmota_status[3]==1))
                btasmota_ch2 |= 1;
            if  (temp[14]>e3dc_config.BWWPein*10||(tasmota_status[3]==0))
                if (btasmota_ch2 & 1)
                    btasmota_ch2 ^= 1;
        }
// LWWP ausschalten
        if (e3dc_config.WPSperre||bWP>0||(bHK1off&&bHK2off))
            btasmota_ch1 = 0;
// FBH zwischen Sonnenuntergang und Sonnenaufgang+1h ausschalten
        int m1 = t%(24*3600)/60;
        if (m1 > (sunriseAt+60)&&m1 < sunsetAt&& bHK1off&1)
            bHK1off ^= 1;
        if (m1 < (sunriseAt+60)||m1 > sunsetAt)
            bHK1off |= 1;

// HK2 zwischen WPHK2off und WPHK2on ausschalten
        float f1 = t%(24*3600)/3600.0;
        if (f1>e3dc_config.WPHK2off)
            bHK2off |= 1;

        if (f1>(e3dc_config.WPHK2on)
            &&
            f1<e3dc_config.WPHK2off
            &&
            bHK2off&1)
                    
                bHK2off ^= 1;

        if (f1>(e3dc_config.WPHK2on)
            &&
            (e3dc_config.WPHK2on)>e3dc_config.WPHK2off
            &&
            bHK2off&1)
            
                bHK2off ^= 1;

// Wie lange reicht der SoC? wird nur außerhalb des Kernwinter genutzt
        f1 = 0;
        for (int x1=0; x1<w.size(); x1++) {
            f1 = f1 + w[x1].solar;
        }

        if  (
             ((sunsetAt-sunriseAt) > 10*60 && f1>300)  // 300% vom Soc = 60kWh
             &&
             (m1>sunsetAt||m1<(sunriseAt+120))
            &&
             fBatt_SOC>=0
            )
        {
            float f2 = 0;
            int x1;
            for (x1=0; x1<w.size()&&(w[x1].hourly+w[x1].wpbedarf)>w[x1].solar; x1++)
            {
                int hh1 = w[x1].hh%(24*3600)/3600;
                if ((hh1<e3dc_config.WPHK2off)||(hh1>e3dc_config.WPHK2on))
                f2 = f2 + w[x1].hourly;
            }
            if (fBatt_SOC>0&& fBatt_SOC< (f2+10)) 
                bWP  |= 1; // LWWP ausschalten
            if (x1 == 0&&bWP&1) 
                bWP ^=1;        // LWWP einschalten;
        }
// Auswertung Steuerung
        if (btasmota_ch1)
        {
            if (tasmota_status[0]==1)
            {
                tasmotaoff(1);   // EVU = OFF Keine Sperre
                wpontime = t;
                wpofftime = t;   //mindestlaufzeit
            }
        } else
            if (tasmota_status[0]==0)
            {
//                if (t-wpofftime > 60)   // 300sek. verzögerung vor der abschaltung
                tasmotaon(1);   // EVU = ON  Sperre
            }

        if (btasmota_ch2)
        {
            if (tasmota_status[1]==0)
            {
                if (btasmota_ch2<4||(t-wpontime>300&&btasmota_ch2&4))
                    tasmotaon(2);
            }
        } 
        else
        if (tasmota_status[1]==1)
        {
                tasmotaoff(2);
                wpofftime = t;
        }
    }


    
    
    
    

    printf("%c[K\n", 27 );
    tm *ts;
    ts = gmtime(&tE3DC);
    hh = t % (24*3600)/3600;
    mm = t % (3600)/60;
    ss = t % (60);
    static float fstrompreis=10000;
    if (((tE3DC % (24*3600))+12*3600)<t) {
// Erstellen Statistik, Eintrag Logfile
        GetConfig(); //Lesen Parameter aus e3dc.config.txt
        sprintf(Log,"Time %s U:%0.04f td:%0.04f yd:%0.04f WB%0.04f", strtok(asctime(ts),"\n"),fSavedtotal/3600000,fSavedtoday/3600000,fSavedyesderday/3600000,fSavedWB/3600000);
        WriteLog();
        if (fSavedtoday > 0)
        {
        FILE *fp;
        fp = fopen("savedtoday.txt", "a");
        if(!fp)
            fp = fopen("savedtoday.txt", "w");
            if(fp){
                fprintf(fp,"%s\n",Log);
                fclose(fp);}
        }
        fSavedyesderday=fSavedtoday; fSavedtoday=0;
        fSavedtotal=0; fSavedWB=0;
        
    }
    t = tE3DC % (24*3600);
    static int PVon;
    static time_t t_config = tE3DC;
    if ((tE3DC-t_config) > 10)
    {
        if (CheckConfig()) // Config-Datei hat sich geändert;
        {
//            printf("Config geändert");
            GetConfig();
            bCheckConfig = true;
//            printf("Config neu eingelesen");
        }
            t_config = tE3DC;
    }
   
#
    
    
//    iModbusTCP();
//    iModbusTCP();
//    iModbusTCP_Heizstab(300);
//    iModbusTCP_Heizstab(400);
//    iModbusTCP_Heizstab(500);

    
    float fLadeende = e3dc_config.ladeende;
    float fLadeende2 = e3dc_config.ladeende2;
    float fLadeende3 = e3dc_config.unload;

    if (cos((ts->tm_yday+9)*2*3.14/365) > 0) // im WinterHalbjahr bis auf 100% am 21.12.
    {
    fLadeende = (cos((ts->tm_yday+9)*2*3.14/365))*(95-fLadeende)+fLadeende;
//        fLadeende = (cos((ts->tm_yday+9)*2*3.14/365))*((100+e3dc_config.ladeende2)/2-fLadeende)+fLadeende;
    fLadeende2 = (cos((ts->tm_yday+9)*2*3.14/365))*(98-fLadeende2)+fLadeende2;
//        fLadeende2 = (cos((ts->tm_yday+9)*2*3.14/365))*(100-fLadeende2)+fLadeende2;
    fLadeende3 = (cos((ts->tm_yday+9)*2*3.14/365))*(100-fLadeende3)+fLadeende3;
    }
// Regelende
    int cLadezeitende1 = (e3dc_config.winterminimum+(e3dc_config.sommermaximum-e3dc_config.winterminimum)/2)*3600;
    tLadezeitende1 = cLadezeitende1+cos((ts->tm_yday+9)*2*3.14/365)*-((e3dc_config.sommermaximum-e3dc_config.winterminimum)/2)*3600;
    if (e3dc_config.RE > 0)
    {        cLadezeitende1 = (e3dc_config.RE*60+(sunsetAt+sunriseAt)/2)/2*60;
        // Regelende
        tLadezeitende1 =         cLadezeitende1+cos((ts->tm_yday+9)*2*3.14/365)*-((e3dc_config.RE*60-(sunsetAt+sunriseAt)/2)/2)*60;
    }
// Ladeende
    int cLadezeitende2 = (e3dc_config.winterminimum+0.5+(e3dc_config.sommerladeende-e3dc_config.winterminimum-0.5)/2)*3600; // eine halbe Stunde Später
    tLadezeitende2 = cLadezeitende2+cos((ts->tm_yday+9)*2*3.14/365)*-((e3dc_config.sommerladeende-e3dc_config.winterminimum-0.5)/2)*3600;
    if (e3dc_config.LE > 0)
    {    cLadezeitende2 = (e3dc_config.LE*60+60+(sunsetAt+sunriseAt)/2)/2*60; //Korrektur + 60min
        tLadezeitende2 = cLadezeitende2+cos((ts->tm_yday+9)*2*3.14/365)*-((e3dc_config.LE*60-60-(sunsetAt+sunriseAt)/2)/2)*60; //korrektur berücksichtigen
    }
// Regelbeginn
    int cLadezeitende3 = (e3dc_config.winterminimum-(e3dc_config.sommermaximum-e3dc_config.winterminimum)/2)*3600; //Unload
    tLadezeitende3 = cLadezeitende3-cos((ts->tm_yday+9)*2*3.14/365)*-((e3dc_config.sommermaximum-e3dc_config.winterminimum)/2)*3600;
    if (e3dc_config.RB > 0)
    {    cLadezeitende3 = (e3dc_config.RB*60+(sunsetAt+sunriseAt)/2)/2*60;
        tLadezeitende3 = cLadezeitende3+cos((ts->tm_yday+9)*2*3.14/365)*-((e3dc_config.RB*60-(sunsetAt+sunriseAt)/2)/2)*60;
    }
    int32_t tZeitgleichung;

   
//    fht = cos((ts->tm_yday+9)*2*3.14/365);
    fht = e3dc_config.htsockel + (e3dc_config.ht-e3dc_config.htsockel) * cos((ts->tm_yday+9)*2*3.14/365);

    // HT Endeladeleistung freigeben
    // Mo-Fr wird während der Hochtarif der Speicher zwischen hton und htoff endladen
    // Samstag und Sonntag nur wenn htsat und htsun auf true gesetzt sind.
    // Damit kann gleich eine intelligente Notstromreserve realisiert werden.
    // Die in ht eingestellte Reserve wird zwischen diesem Wert und 0% zur Tag-Nachtgleiche gesetzt
    // Die Notstromreserve im System ist davon unberührt
    if (iLMStatus == 1)
    {
//        if (sunsetAt-sunriseAt<600)
        location->date(1900+ts->tm_year, ts->tm_mon+1,ts->tm_mday,  0);
        sunriseAt = location->sunrise();
        sunsetAt = location->sunset();


        
        if (e3dc_config.debug) printf("D5");

    int ret; // Steuerung Netzladen = 2, Entladen = 1
        ret =  CheckaWATTar(w,sunriseAt,sunsetAt,sunriseWSW,fBatt_SOC,fht,e3dc_config.Avhourly,e3dc_config.AWDiff,e3dc_config.AWAufschlag,e3dc_config.maximumLadeleistung/e3dc_config.speichergroesse/10,0,fstrompreis,e3dc_config.AWReserve); // Ladeleistung in %

        if (e3dc_config.debug) printf("D6");

        
        switch (e3dc_config.AWtest) // Testfunktion
        {
            case 2:
            if (fBatt_SOC > fht-1) break;  // do nothink
            case 3: ret = 2; break;
            case 1: ret = 1;
        }

        if  ((ret == 2)&&(e3dc_config.aWATTar==1)&&
             (iPower_PV < e3dc_config.maximumLadeleistung||iPower_Bat<e3dc_config.maximumLadeleistung/2||fPower_Grid>e3dc_config.maximumLadeleistung/2))
        {
              iE3DC_Req_Load = e3dc_config.maximumLadeleistung*1.9;
//            printf("Netzladen an");
//            iE3DC_Req_Load = e3dc_config.maximumLadeleistung*0.8;
            iLMStatus = -7;
            bDischargeDone = false;
            fAvBatterie=0;
            fAvBatterie900=100;
            return 0;
        } else
        {
            if (ret == 2) ret = 0;
        }
       
        ts = gmtime(&tE3DC);

            
if (                             // Das Entladen aus dem Speicher
    (                            // wird freigegeben nach den ht/nt Regeln oder aWATTat
        (                        // ht Regel
          (ts->tm_wday>0&&ts->tm_wday<6) ||      // Montag - Freitag
          ((ts->tm_wday==0)&&e3dc_config.htsun)  // Sonntag ohne Sperre
            ||
          ((ts->tm_wday==6)&&e3dc_config.htsat)
        )&&  // Samstag ohne Sperre
        (
          ((e3dc_config.hton > e3dc_config.htoff) &&  // außerhalb der ht sperrzeiten
            ((e3dc_config.hton < t) ||
             (e3dc_config.htoff > t )))
          ||
         ((e3dc_config.hton < e3dc_config.htoff) &&
           (e3dc_config.hton < t && e3dc_config.htoff > t ))
        )      // Das Entladen wird durch hton/htoff zugelassen
    )  //
//    || (CheckaWATTar(sunriseAt,sunsetAt,fBatt_SOC,e3dc_config.Avhourly,e3dc_config.AWDiff)==1) // Rückgabewert aus CheckaWattar
    || (ret==1) // Rückgabewert aus CheckaWattar
   // Das Entladen wird zu den h mit den höchsten Börsenpreisen entladen
    ||
        (fht<fBatt_SOC&& not e3dc_config.aWATTar)        // Wenn der SoC > der berechneten Reserve liegt
    ||
//        (e3dc_config.aWATTar&&fPower_WB>1000&&(fAvBatterie>100||fAvBatterie900>100))       // Wenn der SoC > der berechneten Reserve liegt
    (e3dc_config.aWATTar&&fPower_WB>1000&&(fAvBatterie>100)&&fAvPower_Grid600<1000)       // Es wird über Wallbox geladen und der Speicher aus dem Netz nachgeladen daher anschließend den Speicher zum Entladen sperren
    ||
// Wenn der SoC > fht (Reserve) und (fAvPower_Grid600 < -100) und Batterie wird noch geladen ->Einspeisesitutaton dann darf entladen werden
// bei negativen Börsenpreisen nach Nebenkosten darf aus dem Netz bezogen werden
    (e3dc_config.aWATTar&& iPower_PV > 100 && (fstrompreis/10+fstrompreis*e3dc_config.AWMWSt/1000+e3dc_config.AWNebenkosten)<0
        &&((fAvPower_Grid60 < -100)||fAvBatterie>100||fAvBatterie900>100)) // Bei Solarertrag vorübergehend Entladen freigeben
// Entladen nach Laden kurzzeitig erlauben
    ||
    (e3dc_config.aWATTar&& iPower_PV > 100  &&((fAvPower_Grid60 < -100)||(fAvBatterie+fAvBatterie900)>100)) // Bei Solarertrag vorübergehend Entladen freigeben
// in Ausnahmefällen das Entladen zulassen obwohl Stop gesetzt wurde
    || (e3dc_config.aWATTar&&iPower_PV > 100 &&(iBattLoad<100||(e3dc_config.wallbox >= 0&&(iAvalPower>0&&fAvBatterie>100)))) // Es wird nicht mehr geladen
    ||(iNotstrom==1)  //Notstrom
    ||(iNotstrom==4)  //Inselbetrieb
   ){
            // ENdladen einschalten)
        if ((iPower_Bat == 0)&&(fPower_Grid>100)&&fBatt_SOC>0.5)
{            sprintf(Log,"BAT %s %0.02f %i %i% 0.02f",strtok(asctime(ts),"\n"),fBatt_SOC, iE3DC_Req_Load, iPower_Bat, fPower_Grid);
        WriteLog();
    iLMStatus = 10;
}
       bDischarge = true;
 
/*
       if (iDischarge < e3dc_config.maximumLadeleistung) {
            
        Control_MAX_DISCHARGE(frameBuffer,e3dc_config.maximumLadeleistung);
        iBattPowerStatus = 0;
        iLMStatus = 5;
            sprintf(Log,"Ein %s %0.02f %0.02f %i", strtok(asctime(ts),"\n"),fht,fBatt_SOC, iE3DC_Req_Load);
            WriteLog();

        }
*/
    } else {
        
bDischarge = false;

/*            // Endladen ausschalten
        if (iDischarge >1)
            // Ausschalten nur wenn nicht im Notstrom/Inselbetrieb
            { Control_MAX_DISCHARGE(frameBuffer,0);
            iBattPowerStatus = 0;
            iLMStatus = 5;
                sprintf(Log,"AUS %s %0.02f %0.02f %i", strtok(asctime(ts),"\n"),fht,fBatt_SOC, iE3DC_Req_Load);
            WriteLog();

;}
*/

    }
// printf("ret %i",ret);
//        if (ret<2)
        if (not bDischarge) // Entladen soll unterdrückt werden
        { if ((fPower_Grid < -100)&&(iPower_Bat>=-100)&&(iPower_Bat<=100))  // es wird eingespeist Entladesperre solange aufheben
                {
//                    iE3DC_Req_Load = fPower_Grid*-1;  // Es wird eingespeist
//                    iLMStatus = -7;
//                    iLMStatus = 7;
//                    printf("Batterie laden zulassen ");
//                    return 0;
                }   else
//                if (((iPower_Bat < -100)||((iPower_Bat==0)&&(fPower_Grid>100)))&&((fPower_WB==0)||(iPower_PV<100))) // Entladen zulassen wenn WB geladen wird
//                    if (((iPower_Bat < -100)||((iPower_Bat==0)&&(fPower_Grid>100)))) // Entladen zulassen wenn WB
                    if (fBatt_SOC>0&&((iPower_Bat < -100)||((iPower_Bat<=100)&&(fPower_Grid>100)))) // Entladen zulassen wenn WB
                    {
                    iE3DC_Req_Load = 0;  // Sperren
                    if (iPower_PV > 0)
                    iE3DC_Req_LoadMode = -2;       //Entlademodus  \n
    //                    printf("\nEntladen stoppen ");
                    iLMStatus = -7;
//                    return 0;
                }
        }
        else          // Entladen ok
        if ((fPower_Grid > 100)&&(iPower_Bat < 200)&&fBatt_SOC>0.5)  // es wird Strom bezogen Entladesperre solange aufheben
        {
                iE3DC_Req_Load = fPower_Grid*-1;  //Automatik anstossen
                   if (iE3DC_Req_Load < e3dc_config.maximumLadeleistung*-1)  //Auf maximumLadeleistung begrenzen
                    iE3DC_Req_Load = e3dc_config.maximumLadeleistung*-1;  //Automatik anstossen
//                 printf("Entladen starten ");
            if (e3dc_config.AWtest == 1||e3dc_config.AWtest == 4){
                iLMStatus = -7;
//                return 0;
            } else
                iLMStatus = 7;
        }


        
        
}

    
    // HT Endeladeleistung freigeben  ENDE
    
    
    
    // Berechnung freie Ladekapazität bis 90% bzw. Ladeende
    
    tZeitgleichung = (-0.171*sin(0.0337 * ts->tm_yday + 0.465) - 0.1299*sin(0.01787 * ts->tm_yday - 0.168))*3600;
    tLadezeitende1 = tLadezeitende1 - tZeitgleichung;
    tLadezeitende2 = tLadezeitende2 - tZeitgleichung;
    tLadezeitende3 = tLadezeitende3 - tZeitgleichung;
    tLadezeitende = tLadezeitende1;
    printf("RB %2ld:%2ld %0.1f%% ",tLadezeitende3/3600,tLadezeitende3%3600/60,fLadeende3);
    printf("RE %2ld:%2ld %0.1f%% ",tLadezeitende1/3600,tLadezeitende1%3600/60,fLadeende);
    printf("LE %2ld:%2ld %0.1f%% ",tLadezeitende2/3600,tLadezeitende2%3600/60,fLadeende2);
    fspreis = float((fstrompreis/10)+(fstrompreis*e3dc_config.AWMWSt/1000)+e3dc_config.AWNebenkosten);
    if (e3dc_config.aWATTar) printf("%.2f",fspreis);
    if (e3dc_config.WP&&fcop>0)
    {
        printf(" %.2f %.2f",fspreis/fcop,fcop);
        // LWWP  auf PV Anhebung schalten
        
        
        PVon = PVon*.9 + ((-sqrt(iMinLade*iBattLoad) + iPower_Bat - fPower_Grid))/10;
        //       Steuerung der LWWP nach Überschuss
        //       1. Stufe LWWP Ein Nach mind 15min Laufzeit
        //       2. Stufe PV-Anhebung
        if (PVon>e3dc_config.WPPVon)  // Überschuss PV oder
        {
            btasmota_ch1 |= 4;   // WP einschalten
            btasmota_ch2 |= 4;
        }
        // LWWP weiterlaufen lassen wenn noch überschuss da
        if (PVon>100&&t_alt-wpofftime<900) wpofftime = t_alt;
        if (PVon<(e3dc_config.WPPVoff))  // Überschuss PV
        {
            if (btasmota_ch1&4)
                btasmota_ch1  ^= 4;
            if (btasmota_ch2&4)
                btasmota_ch2  ^= 4;
        }


 }
// Überwachungszeitraum für das Überschussladen übschritten und Speicher > Ladeende
// Dann wird langsam bis Abends der Speicher bis 93% geladen und spätestens dann zum Vollladen freigegeben.
    if (t < tLadezeitende3) {
//            tLadezeitende = tLadezeitende3;
// Vor Regelbeginn. Ist der SoC > fLadeende3 wird entladen
// wenn die Abweichung vom SoC < 0.3% ist wird als Ziel der aktuelle SoC genommen
// damit wird ein Wechsel von Laden/Endladen am Ende der Periode verhindert
        if ((fBatt_SOC-fLadeende3) > 0){
          if ((fBatt_SOC-fLadeende3) < 0.6)
                fLadeende = fBatt_SOC; else
// Es wird bis tLadezeitende3 auf fLadeende3 entladen
                fLadeende = fLadeende3;
            tLadezeitende = tLadezeitende3;}
                }
 else
     if ((t >= tLadezeitende)&&(fBatt_SOC>=fLadeende)) {
         tLadezeitende = tLadezeitende2;
         if (fLadeende < fLadeende2)
         fLadeende = fLadeende2;

     }
    if (t < tLadezeitende2)
    // Berechnen der linearen Ladeleistung bis tLadezeitende2 = Sommerladeende
    {iMinLade2 = ((fLadeende2 - fBatt_SOC)*e3dc_config.speichergroesse*10*3600)/(tLadezeitende2-t);
    if (iMinLade2>e3dc_config.maximumLadeleistung)
        iMinLade2=e3dc_config.maximumLadeleistung;
    }
    else
        if (fLadeende2 <= fBatt_SOC) iMinLade2 = 0;
        else iMinLade2 = e3dc_config.maximumLadeleistung;
    
    if (t < tLadezeitende)
    {
         if ((fBatt_SOC!=fBatt_SOC_alt)||(t-tLadezeit_alt>300)||(tLadezeitende!=tLadezeitende_alt)||(iFc == 0)||bCheckConfig)
// Neuberechnung der Ladeleistung erfolgt, denn der SoC sich ändert oder
// tLadezeitende sich ändert oder nach Ablauf von höchstens 5 Minuten
      {
        fBatt_SOC_alt=fBatt_SOC; // bei Änderung SOC neu berechnen
          bCheckConfig=false;
          tLadezeitende_alt = tLadezeitende; // Auswertungsperiode
          tLadezeit_alt=t; // alle 300sec Berechnen

// Berechnen der Ladeleistung bis zum nächstliegenden Zeitpunkt
                
        iFc = (fLadeende - fBatt_SOC)*e3dc_config.speichergroesse*10*3600;
// Latenzbereich bis 0.5% des SoC berücksichtigen
         if ((fLadeende - fBatt_SOC) > -0.6 && (fLadeende - fBatt_SOC) < 0 && iFc < 0)
            iFc = 0;
          if ((tLadezeitende-t) > 300)
              iFc = iFc / (tLadezeitende-t); else
          iFc = iFc / (300);
// weniger als 2h vor Ladeende2 Angleichung der Ladeleistung an die nächste Ladeperiode
// Im Winter verringert sich der zeitliche Abstand zwischen RE und LE

// weniger als 2h vor Ladeende2 bzw. LE oder 1h vor RE
          if ((tLadezeitende-t) < 3600||(tLadezeitende2-t) < 7200)
          {
              if (iMinLade2 > iFc){
                  iFc = (iFc + iMinLade2)/2;
                  if (iFc < iMinLade2/2)
                      iFc = iMinLade2/2;
              }}
        if (iFc > e3dc_config.maximumLadeleistung)
        iMinLade = e3dc_config.maximumLadeleistung;
          else
          iMinLade = iFc;
//        iFc = (iFc-900)*5;
          if (iFc >= e3dc_config.untererLadekorridor)
              iFc = (iFc-e3dc_config.untererLadekorridor);
          else
            if (abs(iFc) >= e3dc_config.untererLadekorridor)
               iFc = (iFc+e3dc_config.untererLadekorridor);
            else
              iFc = 0;
          iFc = iFc*e3dc_config.powerfaktor;
          if (iFc > e3dc_config.maximumLadeleistung) iFc = e3dc_config.maximumLadeleistung;
          if (abs(iFc) > e3dc_config.maximumLadeleistung) iFc = e3dc_config.maximumLadeleistung*-1;
          if (abs(iFc) < e3dc_config.minimumLadeleistung) iFc = 0;
      }
     }
            else
 
            {       iFc = e3dc_config.maximumLadeleistung;
                    if (fBatt_SOC < fLadeende)
                        iMinLade = iFc;
                    else
                    iMinLade =  0;
            }

    printf("%c[K\n", 27 );

    //  Laden auf 100% nach 15:30
            if (iMinLade == iMinLade2)
                printf("ML1 %i RQ %i ",iMinLade,iFc);
            else
                printf("ML1 %i ML2 %i RQ %i ",iMinLade, iMinLade2,iFc);
            printf("GMT %2ld:%2ld ZG %d ",tLadezeitende/3600,tLadezeitende%3600/60,tZeitgleichung);

    printf("E3DC: %i:%i:%i %.2f ",hh,mm,ss,fatemp);
    printf("%c[K\n", 27 );
    
    int iPower = 0;
    if (iLMStatus == 0){
        iLMStatus = 5;
        tLadezeit_alt = 0;
//        iBattLoad = e3dc_config.maximumLadeleistung;
//        iBattLoad = 100;
//        fAvBatterie = e3dc_config.untererLadekorridor;

        
//        ControlLoadData2(frameBuffer,iBattLoad);
    }
    if (iAvBatt_Count < 120) iAvBatt_Count++;
    fAvBatterie = fAvBatterie*(iAvBatt_Count-1)/iAvBatt_Count;
    fAvBatterie = fAvBatterie + (float(iPower_Bat)/iAvBatt_Count);

    if (iAvBatt_Count900 < 900) iAvBatt_Count900++;
    fAvBatterie900 = fAvBatterie900*(iAvBatt_Count900-1)/iAvBatt_Count900;
    fAvBatterie900 = fAvBatterie900 + (float(iPower_Bat)/iAvBatt_Count900);

    // Überschussleistung=iPower ermitteln

    iPower = (-iPower_Bat + int32_t(fPower_Grid) - e3dc_config.einspeiselimit*-1000)*-1;
    
    // die PV-leistung kann die WR-Leistung überschreiten. Überschuss in den Speicher laden;
    
    if ((iPower_PV_E3DC - e3dc_config.wrleistung) > iPower)
        iPower = (iPower_PV_E3DC - e3dc_config.wrleistung);
    
//    if (iPower < 50) iPower = 0;
//    else
//    if (iPower > e3dc_config.maximumLadeleistung) iPower = e3dc_config.maximumLadeleistung;
//    else
//    if (iPower <100) iPower = 100;
    
 
// Ermitteln Überschuss/gesicherte Leistungen

    if (iPower > 0)
    {if (iPower >iPower_Bat)
          fSavedtoday = fSavedtoday + iPower_Bat;
        else
            fSavedtoday = fSavedtoday + iPower;}
    if (iPower_PV > e3dc_config.einspeiselimit*1000)
        {fSavedtotal = iPower_PV - e3dc_config.einspeiselimit*1000 + fSavedtotal;
            
    if (fPower_WB>0)
        if ((fPower_WB-fPower_Grid+iPower_Bat)>e3dc_config.einspeiselimit*1000)
            fSavedWB = fSavedWB+fPower_WB-fPower_Grid+iPower_Bat-e3dc_config.einspeiselimit*1000;
        }

        
        if (((fBatt_SOC > e3dc_config.ladeschwelle)&&(t<tLadezeitende))||(fBatt_SOC > e3dc_config.ladeende))
        {
            // Überprüfen ob vor RE der SoC > tLadeende2 ist, dann entladen was möglich

            if ((t>tLadezeitende3)&&(t<tLadezeitende1)&&(fBatt_SOC>fLadeende2))
                iFc = e3dc_config.maximumLadeleistung*-1;
          
            if (iPower<iFc)
            {   iPower = iFc;
                if ((iPower>0)&&(iPower > fAvBatterie900)) iPower = iPower + pow((iPower-fAvBatterie900),2)/20;
// Nur wenn positive Werte angefordert werden, wird dynamisiert
                if (iPower > e3dc_config.maximumLadeleistung) iPower = e3dc_config.maximumLadeleistung;
            }
            } else
//            if (fBatt_SOC < cLadeende) iPower = 3000;
//            else iPower = 0;
              iPower = e3dc_config.maximumLadeleistung;
        
    if (e3dc_config.wallbox>=0&&(bWBStart||bWBConnect)&&bWBStopped&&(e3dc_config.wbmode>1)
        &&(((t<tLadezeitende1)&&(e3dc_config.ladeende>fBatt_SOC))||
          ((t>tLadezeitende1)&&(e3dc_config.ladeende2>fBatt_SOC)))
        &&
        ((tE3DC-tWBtime)<7200)&&((tE3DC-tWBtime)>10))
// Wenn Wallbox vorhanden und das letzte Laden liegt nicht länger als 900sec zurück
// und wenn die Wallbox gestoppt wurde, dann wird für bis zu 2h weitergeladen
// oder bis der SoC ladeende2 erreicht hat
// oder solange iWBStatus > 1 ist
            iPower = e3dc_config.maximumLadeleistung; // mit voller Leistung E3DC Speicher laden

//        if (((abs( int(iPower - iPower_Bat)) > 30)||(t%3600==0))&&(iLMStatus == 1))
//            if (((abs( int(iPower - iBattLoad)) > 30)||(abs(t-tE3DC_alt)>3600*3))&&(iLMStatus == 1))
//    if (iPower > iBattLoad&&iLMStatus < 3)
//        iLMStatus = 1;

    // Überschussleistung an Heizstab, wenn vorhanden
    // iBattLoad-iPower_Bat die angeforderte Batterieladeleistung sollte angeforderten Batterieladeeistung entsprechen
    //
    // Ermitteln der tatsächlichen maximalen Speicherladeleistung
    
    if ((fAvPower_Grid < -100)&&(fPower_Grid<-150))
    { if ((iMaxBattLade*.02) > 50)
            iMaxBattLade = iMaxBattLade*.98;
        else iMaxBattLade = iMaxBattLade-50;}
    if (iPower_Bat > iMaxBattLade)
        iMaxBattLade = iPower_Bat;
    if (iMaxBattLade < iPower_Bat*-1&&fBatt_SOC<e3dc_config.ladeende) iMaxBattLade = iPower_Bat*-1;

//    if (iPower_PV > 100)
    {
//        ireq_Heistab = -(iMinLade*2)+ fAvPower_Grid60 + iPower_Bat - fPower_Grid;
        ireq_Heistab = -sqrt(iBattLoad*iMinLade+iBattLoad) + iPower_Bat - fPower_Grid;
        iLeistungHeizstab = iModbusTCP_Heizstab(ireq_Heistab);
    }
    
    if (iLMStatus == 1)
    {
        
            {
            if (iBattLoad > (iPower_Bat-iDiffLadeleistung))
            iDiffLadeleistung = iBattLoad-iPower_Bat+iDiffLadeleistung;
            if ((iDiffLadeleistung < 0 )||(abs(iBattLoad)<=100)) iDiffLadeleistung = 0;
            if (iDiffLadeleistung > 100 )iDiffLadeleistung = 100; //Maximal 100W vorhalten
            if (abs(iPower+iDiffLadeleistung) > e3dc_config.maximumLadeleistung) iDiffLadeleistung = 0;

// Steuerung direkt über vorgabe der Batterieladeleistung
// -iPower_Bat + int32_t(fPower_Grid)
                if (iLMStatus == 1) {
// Es wird nur Morgens bis zum Winterminimum auf ladeende entladen;
// Danach wird nur bis auf ladeende2 entladen.  #Funktion entfernt 28.5.23
//                     if ((iPower < 0)&&((t>e3dc_config.winterminimum*3600)&&(fBatt_SOC<e3dc_config.ladeende2)))
//                     iPower = 0;
// Wenn der SoC > >e3dc_config.ladeende2 wird mit der Speicher max verfügbaren Leistung entladen
//                    if ((iPower < 0)&&((t>tLadezeitende1)&&(fBatt_SOC>e3dc_config.ladeende2)))
//                 iPower = e3dc_config.maximumLadeleistung*-1;
                if (e3dc_config.wrsteuerung>0)
                    iBattLoad = iPower;
                else 
                    iBattLoad = 0;
                 tE3DC_alt = t;

                        {
                        if ((iPower<e3dc_config.maximumLadeleistung)&&
                            (
//                             (iPower > iPower_Bat)||
                            ((iPower > ((iPower_Bat - int32_t(fPower_Grid))/2))))
                            )
// Freilauf, solange die angeforderte Ladeleistung höher ist als die Batterieladeleistung abzüglich
// Wenn über der Wallbox geladen wird, Freilauf beenden
// die angeforderte Ladeleistung liegt über der verfügbaren Ladeleistung
                        {if ((fPower_Grid > 100)&&(iE3DC_Req_Load_alt<(e3dc_config.maximumLadeleistung-1)))
// es liegt Netzbezug vor und System war nicht im Freilauf
                            {iPower = iPower_Bat - int32_t(fPower_Grid);
// Einspeichern begrenzen oder Ausspeichern anfordern, begrenzt auf e3dc_config.maximumLadeleistung
                                if (iPower < e3dc_config.maximumLadeleistung*-1)
                                 iPower = e3dc_config.maximumLadeleistung*-1;
                            }
                            else
                                iPower = e3dc_config.maximumLadeleistung;}
// Wenn die angeforderte Leistung großer ist als die vorhandene Leistung
// wird auf Automatik umgeschaltet, d.h. Anforderung Maximalleistung;
//                        if (iPower >0)
//                        ControlLoadData(frameBuffer,(iPower+iDiffLadeleistung),3);
//                        Es wird nur die Variable mit dem Sollwert gefüllt
//                        die Variable wird im Mainloop überprüft und im E3DC gesetzt
//                        wenn iLMStatus einen negativen Wert hat
                            if (iPower > e3dc_config.maximumLadeleistung)
                            iE3DC_Req_Load = e3dc_config.maximumLadeleistung-1; else
                            iE3DC_Req_Load = iPower+iDiffLadeleistung;
                            if (iE3DC_Req_Load >e3dc_config.maximumLadeleistung)
                                iE3DC_Req_Load = e3dc_config.maximumLadeleistung;
// Testen Heizstab
//                            ireq_Heistab++;
//                            iLeistungHeizstab = iModbusTCP_Heizstab(ireq_Heistab);

                            
                            if (iPower_PV>0)  // Nur wenn die Sonne scheint
                            {
                                
                                static int iLastReq;
                                if (
                                    (
                                     ((iE3DC_Req_Load_alt) >=(e3dc_config.maximumLadeleistung-1))
                                     &&(iE3DC_Req_Load>=(e3dc_config.maximumLadeleistung-1))
                                     )
                                    ||
// Wenn ein negativer Wert angefordert wird und die Batterie stärker entladen wird sowie aus dem Netz > 100W Strom bezogen wird wird der Freilauf eingeschaltet
                                    (iE3DC_Req_Load<0&&
                                     (
                                      (
                                       ((iPower_Bat+100)<iE3DC_Req_Load&& fPower_Grid>-100)||
                                      fPower_Grid>100)
                                     )
                                    ))
// Wenn der aktuelle Wert >= e3dc_config.maximumLadeleistung-1 ist
// und der zuletzt angeforderte Werte auch >= e3dc_config.maximumLadeleistung-1
// war, bleibt der Freilauf erhalten

                                {   
//                                    if (bDischarge)  // Entladen ist zugelassen?
                                    iLMStatus = 3;
                                    if (iLastReq>0)
                                    {sprintf(Log,"CTL %s %0.02f %i %i %0.02f",strtok(asctime(ts),"\n"),fBatt_SOC, iE3DC_Req_Load, iPower_Bat, fPower_Grid);
                                        WriteLog();
                                        iLastReq--;}
                                        }
                                else
                                {
// testweise kein Freilauf
                                    if (iE3DC_Req_Load == e3dc_config.maximumLadeleistung)
                                    {
//                                        if (bDischarge)  // Entladen ist zugelassen?
                                        iLMStatus = 3;
                                        iE3DC_Req_Load_alt = iE3DC_Req_Load;
                                    }else
                                iLMStatus = -6;
                                iLastReq = 6;
                                sprintf(Log,"CTL %s %0.02f %i %i %0.02f",strtok(asctime(ts),"\n"),fBatt_SOC, iE3DC_Req_Load, iPower_Bat, fPower_Grid);
                                WriteLog();}
                            } else
                            iLMStatus = 11;
                            }
/*                    else if (fPower_Grid>50){
// Zurück in den Automatikmodus
                        ControlLoadData(frameBuffer,0,0);
                        iLMStatus = 7;}
*/
                }

          }
    }
// peakshaving erforderlich?
    if ((e3dc_config.peakshave != 0)&&(iLMStatus==2))
    {
        if ((fPower_Grid-iPower_Bat) != e3dc_config.peakshave)
        {
            iDiffLadeleistung2 = -iPower_Bat-iE3DC_Req_Load;
            if (iDiffLadeleistung2 > 100) iDiffLadeleistung2 = 100;
            if (iDiffLadeleistung2 < 0) iDiffLadeleistung2 = 0;
//            iBattLoad = e3dc_config.peakshave-iPowerHome;
//            iE3DC_Req_Load = e3dc_config.peakshave-iPowerHome+iDiffLadeleistung2;
//          Es soll die Netzeinspeisung auf einen Mindestwert gesetzt werden
//            iBattLoad = e3dc_config.peakshave-fPower_Grid+iPower_Bat;
// wenn iPower_Bat < 0 es wird ausgespeichert oder fPower_Grid > e3dc_config.peakshave
            if ((iPower_Bat < 0)||(e3dc_config.peakshave<(fPower_Grid)*.9))
            iE3DC_Req_Load = e3dc_config.peakshave-fPower_Grid+iPower_Bat+iDiffLadeleistung2;
            else
                iE3DC_Req_Load = 0;
                //            iE3DC_Req_Load = e3dc_config.peakshave-iPowerHome;
           if ((iE3DC_Req_Load) > e3dc_config.maximumLadeleistung)
               iE3DC_Req_Load = e3dc_config.maximumLadeleistung;
           else if (abs(iE3DC_Req_Load) > e3dc_config.maximumLadeleistung)
                iE3DC_Req_Load = e3dc_config.maximumLadeleistung*-1;
// Keine Laden aus dem Netz zulassen, nur von der PV
           if (iE3DC_Req_Load > iPower_PV) iE3DC_Req_Load = iPower_PV;
//            if (iE3DC_Req_Load > iPower_Bat)
           if (abs(iE3DC_Req_Load) > 100)
              iLMStatus = -7;
            sprintf(Log,"CPS %s %0.02f %i %i %0.02f %0.02ff", strtok(asctime(ts),"\n"),fBatt_SOC, iE3DC_Req_Load, iPower_Bat, fPower_Grid, fAvPower_Grid600);
            WriteLog();

}
    };
    if (iLMStatus>1) iLMStatus--;
    printf("AVB %0.1f %0.1f ",fAvBatterie,fAvBatterie900);
    printf("DisC %i ",iDischarge);
    if (not bDischarge) printf("halt ");
//    printf("ret %i ",ret);
    printf("BattL %i ",iBattLoad);
    printf("iLMSt %i ",iLMStatus);
    printf("Rsv %0.1f%%",fht);
    // Temperaturen der oekofen ausgeben
    if (strcmp(e3dc_config.heizung_ip,"0.0.0.0")!=0)
    {
        printf("%c[K\n", 27 );
        printf("oekofen AT %i HK1 %i %i %i %i %i %i ",int(temp[0]),temp[1],temp[2],temp[3],temp[4],temp[5],temp[6]);
        printf("HK2 %i %i %i %i %i %i"
               ,temp[7],temp[8],temp[9],temp[10],temp[11],temp[12]);
        printf("%c[K\n", 27 );
        printf("PU %i %i %i %i K: %i %i %i ",temp[13],temp[14],temp[15],temp[16],temp[17],temp[18],temp[19]);
        if (tasmota_status[3] == 0) printf("WW:OFF ");
        if (tasmota_status[3] == 1) printf("WW:ON ");
        if (tasmota_status[1] == 0) printf("PV:OFF ");
        if (tasmota_status[1] == 1) 
            printf("PV:ON%i ",btasmota_ch2);
        printf("%i %i %i",PVon,t_alt-wpontime,t_alt-wpofftime);
    }

    if (strcmp(e3dc_config.heizstab_ip, "0.0.0.0") != 0)
        printf("Heizstab %i %i",ireq_Heistab,iLeistungHeizstab);
        printf("%c[K\n", 27 );

    if (strcmp(e3dc_config.mqtt_ip,"0.0.0.0")!=0&&e3dc_config.WPWolf)
    {
        printf("CHA ");
        if (wolf.size()>0)
        for (int j=0;j<wolf.size();j++)
        {
            if (
//                (wolf[j].feld == "Betriebsart Heizgerät")
//                ||
                (wolf[j].feld == "Verdichterstatus")
                )
                printf("%s %s ",wolf[j].AK.c_str(),wolf[j].status.c_str());
            else
                if (
                    (wolf[j].feld != "Verdichterstatus")
&&
                    (wolf[j].feld != "Betriebsart Heizgerät")

                    )
                printf("%s %0.1f ",wolf[j].AK.c_str(),wolf[j].wert);
            if (j==6)
                printf("%c[K\n", 27 );

        }
        static float hl_alt;
        static float cop;
        if (wolf.size()>0)
            if (wolf[wppw].wert>0) //division durch 0 vermeiden
            {
                if (wolf[wphl].wert!=hl_alt)
                    cop = (wolf[wphl].wert/wolf[wppw].wert);
                printf("%0.2f %0.2f ",fspreis/(wolf[wphl].wert/wolf[wppw].wert),cop);
                hl_alt = wolf[wphl].wert;
            }
        printf("%c[K\n", 27 );

    }


    printf("U %0.0004fkWh td %0.0004fkWh", (fSavedtotal/3600000),(fSavedtoday/3600000));
    if (e3dc_config.wallbox>=0)
    printf(" WB %0.0004fkWh",(fSavedWB/3600000));
    printf(" yd %0.0004fkWh",(fSavedyesderday/3600000));
    printf("%c[J", 27 );

    char buffer [500];
//    sprintf(buffer,"echo $PATH");
//    system(buffer);

    if (e3dc_config.openWB)
    {
//        sprintf(buffer,"mosquitto_pub -r -h raspberrypi -t openWB/set/evu/VPhase1 -m %0.1f",float(223.4));
//        system(buffer);

        
    sprintf(buffer,"openWB/set/evu/W -m %0i",int(fPower_Grid));
    MQTTsend(e3dc_config.openWB_ip,buffer);

    sprintf(buffer,"openWB/set/pv/W -m %0i",iPower_PV*-1);
    MQTTsend(e3dc_config.openWB_ip,buffer);

    sprintf(buffer,"echo %0.1f > /var/www/html/openWB/ramdisk/llaktuell",fPower_WB);
//    system(buffer);

    sprintf(buffer,"openWB/set/Housebattery/W -m %0i",iPower_Bat);
    MQTTsend(e3dc_config.openWB_ip,buffer);

    sprintf(buffer,"openWB/set/Housebattery/%%Soc -m %0i",int(fBatt_SOC));
    MQTTsend(e3dc_config.openWB_ip,buffer);

    sprintf(buffer,"echo %i > /var/www/html/openWB/ramdisk/hausleistung",iPowerHome);
//    system(buffer);
    }
    return 0;
}
int WBProcess(SRscpFrameBuffer * frameBuffer) {
/*   Steuerung der Wallbox
*/
    const int cMinimumladestand = 15;
    static uint8_t WBChar_alt = 0;
    static int32_t iWBMinimumPower,idynPower; // MinimumPower bei 6A
    static bool bWBOn, bWBOff = false; // Wallbox eingeschaltet
    static bool bWBLademodusSave,bWBZeitsteuerung;

/*
 Die Ladeleistung der Wallbox wird nach verfügbaren PV-Überschussleistung gesteuert
 Der WBModus gibt die Priorität der Wallbox gegenüber dem E3DC Speicher vor.
 
 Der E3DC Speicher hat Priorität
 
 Der angeforderte Ladeleistung des E3DC wird durch iBattLoad angezeigt.
 der lineare Ladebedarf wird durch iMinLoad ermittelt
 die berechnete dynamische Ladeleistung wird in iFc ermittelt.
 ffAvBatterie und fAvBatterie900 zeigt die durchschnittliche Ladeleistung
 der letzen 120 bzw. 900 Sekunden an.
 
 WBModus = 0 KEINE STEUERUNG
 
 WBModus = 1 NUR Überschuss > einspeiselimit

 Der E3DC Speicher hat Priorität
 Die Wallbox kann nur die Überschussleistung zur Verfügung gestellt werden
 und speist sich aus der verfügbaren fPower_Grid > einspeiselimit-iWBMinimumPower.
 
 WBModus = 2 NUR Überschuss

 
 Der E3DC Speicher hat Priorität
 Die Wallbox kann nur die Überschussleistung zur Verfügung gestellt werden
 und speist sich aus der verfügbaren fPower_Grid > iWBMinimumPower.
 Entspricht weitgehend dem jetzigen Verfahren.
 
 der 1/4h Wert = fAvPower_Grid900 wird so geführt, dass er dem kleineren Wert von
 MinLoad und iFC entspricht.  MinLoad und iFC sollten im unteren Bereich
 des Ladekorridors (obererLadekorridor) geführt werden. Mit dem Ziel von MinLoad = iFc.
 
 Bei SoC >= ladeende2 wird fAvPower_Grid900 so geführt, dass diese um Null pendelt.
 Bei gridüberschuss von 100/200 oder iPower_Bat > 700 die Ladeleistung angeboben,
 Bei iPower_Bat < -700 oder fAvPower_Grid900 < 100 wird das Laden reduziert.
 
 Das Laden wird beendet bei fAvPower_Grid900 < -200.
 Das Laden wird neu gestartet bei fAvPower_Grid900 > 100 +
 verfügbare PV-Leistung (iPower_Bat-fPower_Grid)
 und verfügbare Speicherleistung (e3dc_config.maximumLadeleistung -700)
 großer als die iWBMinimumPower ist.
 
 WBModus = 3
 
 Der E3DC Speicher hat immer noch Priorität, untersützt aber die Wallbox stärker
 bei temporäre Ertragsschwankungen, der E3DC-Speicher wird so geführt,
 das immer die maximale Ladeleistung aufgenommen werden kann.
 Damit wird aber auch hingenommen, dass unter Umständen am Abend
 der Speicher nicht voll wird.
 der 1/4h Wert = fAvPower_Grid900 wird so geführt, dass er dem kleineren Wert von
 MinLoad und iFC nahe kommt dabei sollte iBattload immer dem
 e3dc_config.maximumLadeleistung entsprechen. MinLoad und iFC < WBminlade
 
 
 Zielparameter: fAvPower_Grid900 = 2*Minload - WBminlade und
                fAvPower_Grid    = 2*iFc - e3dc_config.maximumLadeleistung
 
 Dies wird wie folgt erreicht:
 
 Die Ladeleistung wird angehoben, wenn iBattload unter e3dc_config.maximumLadeleistung
 fällt oder fAvPower_Grid > 2*iFc - e3dc_config.maximumLadeleistung steigt
 
 dabei wird auch in Kauf genommen, dass die Batterie zeitweise entladen wird.
 
 WBModus = 4
 
 Hier bekommt die Wallbox die Priorität, der Speicher wird bis auf Ladeschwelle entladen, aber der Bezug aus dem Netz wird vermieden.
 
 Die Laden wird gestartet sobald verfügbare PV-Leistung (iPower_Bat-fPower_Grid)
 und verfügbare Speicherleistung (e3dc_config.maximumLadeleistung -700)
 großer als die iWBMinimumPower ist.
 Das Laden wird beendet bei Gridbezug > 100/200 und bei der Unterschreitung der Ladeschwelle.
 
 Unterstützung Teslatar
 
 Teslatar ist ein eigenständiges Python Program, dass auf dem Raspberry Pi läuft.
 
 Im Zusammenhang damit gibt es auch noch ein Problem mit der Wallbox, da Sie einfach
 auf den Zustand "gestoppt" springt, was ein späteres gesteuertes Laden verhindert.
 Deswegen wird zwischen 21:00 GMT und 5:00 GMT die Ladesperre der Wallbox überwacht
 Die Einstellung der Ladestromstärke auf 30A löst die Überwachung erst aus.
 
 Unterstützung aWATTar
 
 Schaltet die Wallbox fpr die in cw gespeicherten Interval ein.
 zu diesem zweck wird ein Ladeinterval zwischen Sonnenuntergang/-aufgang abgearbeitet
 bWBLademodus True = Sonne
 
 */

    if (iWBStatus == 0)  {

        iMaxBattLade = e3dc_config.maximumLadeleistung*.9;
        
        memcpy(WBchar6,"\x00\x06\x00\x00\x00\x00",6);
        WBchar6[1]=WBchar[2];

        if ((WBchar[2]==e3dc_config.wbmaxladestrom)||(WBchar[2]==30))
            bWBmaxLadestrom = true; else
            bWBmaxLadestrom = false;
            bWBLademodusSave = bWBSonne;    //Sonne = true
            bWBmaxLadestromSave =  bWBmaxLadestrom;
        
            if (WBchar[2] > 4)
            iWBStatus = 12;
            else {
//                iWBStatus= 1;
                return 0;}
        
//            createRequestWBData(frameBuffer);
    }
    
        int iRefload=0,iPower=0;
//        iMaxBattLade = e3dc_config.maximumLadeleistung*.9;


    if (iMinLade>iFc) iRefload = iFc;
        else iRefload = iMinLade;
// Morgens soll die volle Leistung zur Verfügung stehen
//        if (iMaxBattLade < iMinLade) iMaxBattLade = iMinLade*.9;
        
    if ((e3dc_config.wbmode>0)) // Dose verriegelt, bereit zum Laden
    {

//        if (iMaxBattLade < iRefload) // Führt zu Überreaktion
//            iRefload = iMaxBattLade;

        switch (e3dc_config.wbmode)
        {
            case 1:
//              iPower = -fPower_Grid-e3dc_config.einspeiselimit*1000+fPower_WB;
              iPower = -fPower_Grid-e3dc_config.einspeiselimit*1000+500; // Schon 500W früher einschalten
//                iPower = -fPower_Grid-e3dc_config.einspeiselimit*1000;
                if (fPower_WB > 1000)
//                    iPower = iPower+iPower_Bat-iRefload+iWBMinimumPower/6;
                    iPower = iPower+iWBMinimumPower/6;
                else
//                    iPower = iPower+iPower_Bat-iRefload+iWBMinimumPower;
                    iPower = iPower+iWBMinimumPower;

                if ((iPower <  (iWBMinimumPower)*-1)&&(WBchar[2] == 6)) // Erst bei Unterschreitung von Mindestladeleistung + 0W
                {//iPower = -20000;
// erst mit 30sec Verzögerung das Laden beenden
                    if (!bWBOff)
                    {iWBStatus = 29;
                    bWBOff = true;
                    }
                } else
                {
                bWBOff = false;
// Bei aktivem Ladevorgang nicht gleich wieder abbrechen
                if ((iPower < -2000)&&(fPower_WB>1000))
                    iPower= -2000;
                }
//            wenn nicht abgeregelt werden muss, abschalten
              break;
            case 2:
                iPower = -iFc + iPower_Bat + fPower_Grid;
                break;
                // Wenn Überschuss dann Laden starten
                iPower = 0;
                if (-fAvPower_Grid > iWBMinimumPower)
// Netzüberschuss  größer Startleistung WB
                    iPower = -fAvPower_Grid;
                else
                    if (fBatt_SOC > 10)
// Mindestladestand Erreicht
                    {
// Überschuss Netz
                        if ((fPower_Grid < -200) && (fAvPower_Grid < -100))
                        {
                            if ((-fPower_Grid > (iWBMinimumPower/6)))
                                iPower = -fPower_Grid;
                            else
                                iPower = iWBMinimumPower/6;
                        }
                        else
// Überschussleistung verfügbar?
                        {
                            if (abs(iPower_Bat-iBattLoad) > (iWBMinimumPower/6))
                            {
                                if (iBattLoad > iMaxBattLade)
                                    iPower = iPower_Bat-iMaxBattLade;
                                else
                                    iPower = iPower_Bat-iBattLoad;
                         
                            }
                        }
                    }
                    idynPower = (iRefload - (fAvBatterie900+fAvBatterie))*-2;
                    if (idynPower < 0)
                        iPower = idynPower;
                break;
            case 3:
                iPower = iPower_Bat-fPower_Grid*2-iRefload;
                idynPower = (iRefload - (fAvBatterie900+fAvBatterie)/2)*-2;
//                idynPower = idynPower- iRefload;
// Wenn das System im Gleichgewicht ist, gleichen iAvalPower und idynPower sich aus
                iPower = iPower + idynPower;
                if (iPower > iPower_Bat) iPower = iPower_Bat;
                break;
            case 4:
// Der Leitwert ist iMinLade2 und sollte der gewichteten Speicherladeleistung entsprechen
              if (iRefload > iMinLade2) iRefload = iMinLade2;
              iPower = iPower_Bat-fPower_Grid*3-iRefload;
              idynPower = (iRefload - (fAvBatterie900+fAvBatterie)/2)*-1;
                idynPower = idynPower + e3dc_config.maximumLadeleistung -iBattLoad;
              iPower = iPower + idynPower;
//                if (iPower > iPower_Bat+fPower_Grid*-3) iPower = iPower_Bat+fPower_Grid*-3;

              break;
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:

            // Der Leitwert ist iMinLade2 und sollte dem WBminlade
            // des Ladekorridors entprechen
                int x1,x2,x3,x4,x5;
               
                if ((iRefload > iMinLade2)) 
                    iRefload = (iRefload+iMinLade2)/2;
                if (iRefload > iMaxBattLade) 
                    iRefload = iMaxBattLade;
// iMaxBattLade ist die maximale tatsächliche mögliche Batterieladeleistung
                
                iPower = iPower_Bat-fPower_Grid*2-iRefload;
                
                x1 = iPower_Bat - iRefload;
                x2 = fAvBatterie900- iRefload;
                x3 = fAvBatterie- iRefload;

                idynPower = (iRefload*2 - e3dc_config.wbminlade - (fAvBatterie900+fAvBatterie)/2)*-2;
                iPower = iPower + idynPower;
//              Wenn iRefload < e3dc_config.wbminlade darf weiter entladen werden
//              bis iRefload 90% von e3dc_config.wbminlade erreicht sind
//              es wird mit 0/30/60/90% von e3dc_config.maximumLadeleistung
//              entladen
                
//                idynPower = iPower_Bat-fPower_Grid*2 + (iMaxBattLade - iWBMinimumPower/6) * (e3dc_config.wbmode-5)*1/3;
                idynPower = iPower_Bat-fPower_Grid*2 + (e3dc_config.maximumLadeleistung*.9 - iWBMinimumPower/6) * (e3dc_config.wbmode-5)*1/3;
// falls das Entladen gesperrt ist iPower_Bat==0 und Netzbezug Ladeleistung herabsetzen
                if (iPower_Bat==0&&fPower_Grid>100) idynPower = - fPower_Grid*2 - iWBMinimumPower/6;
// Berücksichtigung des SoC
                if (fBatt_SOC < (fht+15))
                    idynPower = idynPower*fBatt_SOC/100; // wenn fht < fBatt_Soc idynPower = 0
// Anhebung der Ladeleistung nur bis Ladezeitende1
                if ((t<tLadezeitende1)&&(iPower<idynPower)&&(iRefload<e3dc_config.wbminlade))
                {
//                    if (iRefload<iMaxBattLade)
//                    iPower = iPower + idynPower;
                    iPower = idynPower;
//                    else
                      if (iPower < (iPower_Bat-fPower_Grid))
                          iPower = iPower_Bat-fPower_Grid;
                          }
// Nur bei PV-Ertrag
                if  ((iPower > 0)&&(iPower_PV<100)) iPower = -20000;
// Bei wbmode 9 wird zusätzlich bis zum minimum SoC entladen, auch wenn keine PV verfügbar

               if ((e3dc_config.wbmode ==  9)&&(fBatt_SOC > (e3dc_config.wbminSoC-1.0)))
                {iPower = e3dc_config.maximumLadeleistung*(fBatt_SOC-e3dc_config.wbminSoC)*(fBatt_SOC-e3dc_config.wbminSoC)*
                    (fBatt_SOC-e3dc_config.wbminSoC)/4; // bis > 2% uber MinSoC volle Entladung
                 if (iPower > e3dc_config.maximumLadeleistung)
                     iPower = e3dc_config.maximumLadeleistung*.9;
                    iPower = iPower +(iPower_Bat-fPower_Grid*2);
                    
                }
                if (iPower > (e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid*2))
                    iPower = e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid*2;
                          break;
            case 10:
                    if (fPower_Grid > 0)
                        iPower = -fPower_Grid*3; else
                        iPower = -fPower_Grid;
                break;
// Auswertung
        }

// im Sonnenmodus nur bei PV-Produktion regeln
        if (iPower > e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid)
            iPower = e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid;
        
//        if (iPower < -e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid)
//            iPower = -e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid;


        if (iPower_PV_E3DC > e3dc_config.wrleistung){
            iPower = iPower - iPower_PV_E3DC + e3dc_config.wrleistung;
            if (fPower_Grid < 100 && iPower > fPower_Grid*-1) // Netzbezug, verfügbare Leistung reduzieren
                iPower = fPower_Grid*-1;
            if (fPower_Grid > 100 && iPower > 0) // Netzbezug, verfügbare Leistung reduzieren
            iPower = fPower_Grid*-3;
        }

        static float fPower_WB_alt = 0;

        if (fPower_WB!=fPower_WB_alt)
            iAvalPower = iAvalPower - fPower_WB + fPower_WB_alt;
        fPower_WB_alt = fPower_WB;        
        if  (
            (iAvalPower > 0&&iPower>0)
            ||
            (iAvalPower < 0&&iPower<0)
            )
        {
                iAvalPower = iAvalPower *.995 + iPower*.025;
// Über-/Unterschuss wird aufakkumuliert Gleichgewicht 1000W bei 5000W Anforderung


        } else
                iAvalPower = iAvalPower*.8  + iPower*.2;    // Über-/Unterschuss wird aufakkumuliert


        
        
        if ((iAvalPower>0)&&bWBLademodus&&iPower_PV<100&&e3dc_config.wbmode<9)
            iAvalPower = 0;

        
        if (iAvalPower > (e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid))
              iAvalPower = e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid;
        // Speicher nur bis 5-7% entladen
        if ((fBatt_SOC < 5)&&(iPower_Bat<0)) iAvalPower = iPower_Bat-fPower_Grid - iWBMinimumPower/6-fPower_WB;
//        else if (fBatt_SOC < 20) iAvalPower = iAvalPower + iPower_Bat-fPower_Grid;
        if (iAvalPower < (-iMaxBattLade+iPower_Bat-fPower_Grid-fPower_WB))
            iAvalPower = -iMaxBattLade+iPower_Bat-fPower_Grid-fPower_WB;

        if (e3dc_config.wbmode==1||e3dc_config.wbmode==10) 
            iAvalPower = iAvalPower *.9 + iPower*.3;    // Über-/Unterschuss wird aufakkumuliert
        
        
//        if ((iWBStatus == 1)&&(bWBConnect)) // Dose verriegelt
        if (iWBStatus == 1) // 
        {
// Wenn bWBZeitsteuerung erfolgt die Ladungsfreigabe nach ch = chargehours ermittelten Stunden
            struct tm * ptm;
            ptm = gmtime(&tE3DC);
            if (e3dc_config.aWATTar>0)
            if ((not(bWBZeitsteuerung))&&(bWBConnect)) // Zeitsteuerung nicht + aktiv + wenn Auto angesteckt
            {
// Überprüfen ob auf Sonne und Auto eingestellt ist,
// falls das der Fall sein sollte, Protokoll ausgeben und Sonne/Auto einstellen
                if ((not bWBLademodus||bWBmaxLadestrom)&&e3dc_config.aWATTar)
                {
                    sprintf(Log,"WB Error %s ", strtok(asctime(ptm),"\n"));
                    WriteLog();

                    bWBLademodus = true;
                    WBchar6[0] = 1;            // Sonnenmodus
                    WBchar6[1] = e3dc_config.wbmaxladestrom-1;       // fest auf Automatik einstellen
                    bWBZeitsteuerung = false; // Ausschalten, weil z.B. abgesteckt
//                    if (bWBCharge)
//                    WBchar6[4] = 1; // Laden stoppen
                    createRequestWBData(frameBuffer);  // Laden stoppen und/oeder Modi ändern
                    WBchar6[4] = 0; // Toggle aus
                    iWBStatus = 30;
                    return(0);
                }
                for (int j = 0; j < ch.size(); j++ ) // suchen nach dem Zeitfenster
                    if ((ch[j].hh <= tE3DC)&&(ch[j].hh+3600 >= tE3DC)){
                        bWBZeitsteuerung = true;
                    };
                if ((bWBZeitsteuerung)&&(bWBConnect)){  // Zeitfenster ist offen und Fahrzeug angesteckt
                    bWBmaxLadestromSave = bWBmaxLadestrom;
                    WBchar6[0] = 2;            // Netzmodus
//                    if (not(bWBmaxLadestrom))
                    {
                        bWBmaxLadestrom = true;
                        WBchar6[1] = e3dc_config.wbmaxladestrom;
                    }
                    if (bWBStopped)
                    WBchar6[4] = 1; // Laden starten
                    bWBLademodusSave=bWBLademodus;    //Sonne = true
                    bWBLademodus = false;  // Netz
                    createRequestWBData(frameBuffer);
                    WBchar6[4] = 0; // Toggle aus
                    iWBStatus = 30;
                    return(0);

                }
            }else   // Das Ladefenster ist offen, Überwachen, ob es sich wieder schließt
            {
                bWBZeitsteuerung = false;
                for (int j = 0; j < ch.size(); j++ )
                    if ((ch[j].hh <= tE3DC)&&(ch[j].hh+3600 >= tE3DC)){
                        bWBZeitsteuerung = true;
                    };
                if ((not(bWBZeitsteuerung))||not bWBConnect){    // Ausschalten
                    if ((bWBmaxLadestrom!=bWBmaxLadestromSave)||not (bWBLademodus))
                    {bWBmaxLadestrom=bWBmaxLadestromSave;  //vorherigen Zustand wiederherstellen
                    bWBLademodus = true;
//                    if (bWBLademodus)         // Sonnenmodus fest einstellen
                    WBchar6[0] = 1;            // Sonnenmodus
//                    if (not(bWBmaxLadestrom)){
                        WBchar6[1] = e3dc_config.wbmaxladestrom-1;       // fest auf Automatik einstellen
//                    } else WBchar6[1] = 32;
                    bWBZeitsteuerung = false; // Ausschalten, weil z.B. abgesteckt
// Laden wird bei Umschaltung auf Sonnen nicht mehr gleich gestoppt
//                    if (bWBCharge)
//                    WBchar6[4] = 1; // Laden stoppen
                    createRequestWBData(frameBuffer);  // Laden stoppen und/oeder Modi ändern
                    WBchar6[4] = 0; // Toggle aus
                    iWBStatus = 30;
                    return(0);
                    }
/*                    else
                    if (bWBCharge)                     // Laden stoppen
                    {
                    WBchar6[4] = 1; // Laden stoppen
                    createRequestWBData(frameBuffer);
                    WBchar6[4] = 0; // Toggle aus
                    iWBStatus = 30;
                    return(0);
                    }
*/                }

            };
            
            
            if (bWBmaxLadestrom)  {//Wenn der Ladestrom auf 32, dann erfolgt keine
            if ((fBatt_SOC>cMinimumladestand)&&(fAvPower_Grid<400)) {
//Wenn der Ladestrom auf 32, dann erfolgt keine Begrenzung des Ladestroms im Sonnenmodus
            if ((WBchar6[1]<e3dc_config.wbmaxladestrom)&&(fBatt_SOC>(cMinimumladestand+2))) {
                WBchar6[1]=e3dc_config.wbmaxladestrom;
                createRequestWBData(frameBuffer);
                WBChar_alt = WBchar6[1];
                }
        }
            }     else if ((WBchar6[1] > 6)&&(fPower_WB == 0)) WBchar6[1] = 6;

// Wenn der wbmodus neu eingestellt wurde, dann mit 7A starten

            if (bWBChanged) {
                bWBChanged = false;
                WBchar6[1] = 7;  // Laden von 6A aus
                WBchar6[4] = 0; // Toggle aus
                createRequestWBData(frameBuffer);
                WBChar_alt = WBchar6[1];


            }
            
// Immer von 6A aus starten
        
// Ermitteln Startbedingungen zum Ladestart der Wallbox
// Laden per Teslatar, Netzmodus, LADESTROM 30A
//            if ((WBchar[2] == 30) && not(bWBLademodus)&&cWBALG&64) {
            if ((WBchar[2] == 30) && not(bWBLademodus)&&bWBStopped) {
                WBchar6[1] = 30;
                iWBSoll = 30;
                WBchar6[4] = 1; // Laden starten
                createRequestWBData(frameBuffer);
                WBchar6[4] = 0; // Toggle aus
                WBChar_alt = WBchar6[1];
                iWBStatus = 30;
                sprintf(Log,"WB starten %s", strtok(asctime(ptm),"\n"));
                WriteLog();
            };
            
        if ( (fPower_WB == 0) &&bWBLademodus&&bWBConnect)
//            bWBLademodus = Sonne
             { // Wallbox lädt nicht
            if ((not bWBmaxLadestrom)&&(iWBStatus==1))
                {
                if ((bWBStopped)&& (iAvalPower>iWBMinimumPower))
                    {
                        WBchar6[1] = 6;  // Laden von 6A aus
                            WBchar6[4] = 1; // Laden starten
                            bWBOn = true;
                        createRequestWBData(frameBuffer);
                        WBchar6[4] = 0; // Toggle aus
                        WBChar_alt = WBchar6[1];
                        iWBStatus = 30;
                    } else
                    if (WBchar[2] != 6)
                        {
                            WBchar6[1] = 6;  // Laden von 6A aus
                            WBchar6[4] = 0; // Toggle aus
                            createRequestWBData(frameBuffer);
                            WBChar_alt = WBchar6[1];
                        }
                }
//                    else WBchar6[1] = 2;
        }
            if (fPower_WB > 0) {tWBtime = tE3DC;
               if (fPower_WB < 1000) iWBStatus = 30;
            } // WB Lädt, Zeitstempel updaten
            if ((fPower_WB > 1000) && not (bWBmaxLadestrom)) { // Wallbox lädt
            bWBOn = true; WBchar6[4] = 0;
            WBchar6[1] = WBchar[2];
            int icurrent = WBchar[2];  //Ausgangsstromstärke
                if (WBchar6[1]==6) iWBMinimumPower = fPower_WB;
            else
                if ((iWBMinimumPower == 0) ||
                    (iWBMinimumPower < (fPower_WB/WBchar6[1]*6) ))
                     iWBMinimumPower = (fPower_WB/WBchar6[1])*6;
            if  ((iAvalPower>=(iWBMinimumPower/6))&&
                (WBchar6[1]<e3dc_config.wbmaxladestrom-1)){
                WBchar6[1]++;
                for (int X1 = 3; X1 < 20; X1++)
                    
                if ((iAvalPower > (X1*iWBMinimumPower/6)) && (WBchar6[1]<e3dc_config.wbmaxladestrom-1)) WBchar6[1]++; else break;
//                WBchar[2] = WBchar6[1];
                if (icurrent == 6&&WBchar6[1]>16)
                    WBchar6[1] = 16;
                createRequestWBData(frameBuffer);
                WBChar_alt = WBchar6[1];
                if ((icurrent <=16)&&WBchar6[1]>16)
                iWBStatus = 30;
                // Länger warten bei Wechsel von <= 16A auf > 16A hohen Stömen

             } else

// Prüfen Herabsetzung Ladeleistung
            if ((WBchar6[1] > 6)&&(iAvalPower<=((iWBMinimumPower/6)*-1)))
                  { // Mind. 2000W Batterieladen
                WBchar6[1]--;
                for (int X1 = 2; X1 < 20; X1++)
                    if ((iAvalPower <= ((iWBMinimumPower/6)*-X1))&& (WBchar6[1]>7)) WBchar6[1]--; else break;
                WBchar[2] = WBchar6[1];
//                createRequestWBData2(frameBuffer);

                createRequestWBData(frameBuffer);
                if (WBchar6[1]==6)
                    iWBStatus = 30;
                WBChar_alt = WBchar6[1];

            } else
// Bedingung zum Wallbox abschalten ermitteln
//
                
                
            if ((fPower_WB>100)&&(
                                  ((iPower_Bat-fPower_Grid < (e3dc_config.maximumLadeleistung*-0.9))&&(fBatt_SOC < 94))
                || ((fPower_Grid > 3000)&&(iPower_Bat<1000))   //Speicher > 94%
                || (fAvPower_Grid>400)          // Hohem Netzbezug
                                                // Bei Speicher < 94%
//                || ((fAvBatterie900 < -1000)&&(fAvBatterie < -2000))
//                || (iAvalPower < (e3dc_config.maximumLadeleistung-fAvPower_Grid)*-1)
                || (iAvalPower < iWBMinimumPower*-1)
                ))  {
                if ((WBchar6[1] > 5)&&bWBLademodus)
                {WBchar6[1]--;

                        if (WBchar6[1]==5) {
                            WBchar6[1]=6;
                            WBchar6[4] = 1;
                            bWBOn = false;
                        } // Laden beenden
                        createRequestWBData(frameBuffer);
                    WBchar6[1]=5;
                    WBchar6[4] = 0;
                    WBChar_alt = WBchar6[1];
                    iWBStatus = 10;  // Warten bis Neustart
                }}
    }
        }}
    printf("%c[K\n", 27 );
    printf("AVal %0i/%01i/%01i Power %0i WBMode %0i ", iAvalPower,iPower,iMaxBattLade,iWBMinimumPower, e3dc_config.wbmode);
    printf(" iWBStatus %i %i %i %i",iWBStatus,WBToggel,WBchar6[1],WBchar[2]);
    if (iWBStatus > 1) iWBStatus--;
return 0;
}


int createRequestExample(SRscpFrameBuffer * frameBuffer) {
    RscpProtocol protocol;
    SRscpValue rootValue;
    // The root container is create with the TAG ID 0 which is not used by any device.
    protocol.createContainerValue(&rootValue, 0);

    //---------------------------------------------------------------------------------------------------------
    // Create a request frame
    //---------------------------------------------------------------------------------------------------------
    if(iAuthenticated == 0)
    
    {
        printf("\nRequest authentication\n");
        // authentication request
        SRscpValue authenContainer;
        protocol.createContainerValue(&authenContainer, TAG_RSCP_REQ_AUTHENTICATION);
        protocol.appendValue(&authenContainer, TAG_RSCP_AUTHENTICATION_USER, e3dc_config.e3dc_user);
        protocol.appendValue(&authenContainer, TAG_RSCP_AUTHENTICATION_PASSWORD, e3dc_config.e3dc_password);
        // append sub-container to root container
        protocol.appendValue(&rootValue, authenContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(authenContainer);
    }
    else
    {
//        printf("\nRequest cyclic example data\n");
        // request power data information
        protocol.appendValue(&rootValue, TAG_EMS_REQ_POWER_PV);
        protocol.appendValue(&rootValue, TAG_EMS_REQ_POWER_ADD);
        protocol.appendValue(&rootValue, TAG_EMS_REQ_POWER_BAT);
        protocol.appendValue(&rootValue, TAG_EMS_REQ_BAT_SOC);
        protocol.appendValue(&rootValue, TAG_EMS_REQ_POWER_HOME);
        protocol.appendValue(&rootValue, TAG_EMS_REQ_POWER_GRID);
        protocol.appendValue(&rootValue, TAG_EMS_REQ_EMERGENCY_POWER_STATUS);
//        protocol.appendValue(&rootValue, TAG_EMS_REQ_REMAINING_BAT_CHARGE_POWER);
        if(iBattPowerStatus == 0)
        {
            protocol.appendValue(&rootValue, TAG_EMS_REQ_GET_POWER_SETTINGS);
            iBattPowerStatus = 1;
        }
// request Power Meter information
        if (e3dc_config.wrsteuerung==0)
            printf("\n Achtung WR-Steuerung inaktiv %i Status %i\n",iE3DC_Req_Load,iLMStatus);
        if (e3dc_config.wrsteuerung==2) // Text ausgeben
            printf("\n WR-Steuerung aktiv %i Status %i\n",iE3DC_Req_Load,iLMStatus);

        if (iLMStatus < 0&&e3dc_config.wrsteuerung==0) iLMStatus=iLMStatus*-1;
        if (iLMStatus < 0&&e3dc_config.wrsteuerung>0)
        {
            int32_t Mode;
            if (iE3DC_Req_Load==0) Mode = 1; else
                if (iE3DC_Req_Load>e3dc_config.maximumLadeleistung)
                {
                    iE3DC_Req_Load = iE3DC_Req_Load - e3dc_config.maximumLadeleistung;
                    Mode = 4;  // Steuerung Netzbezug Anforderung durch den Betrag > e3dc_config.maximumLadeleistung
                }
                else
                if (iE3DC_Req_Load==e3dc_config.maximumLadeleistung) Mode = 0; else
                if (iE3DC_Req_Load>0) Mode = 3; else
            { iE3DC_Req_Load = iE3DC_Req_Load*-1;
                Mode = 2;}
            iLMStatus = iLMStatus*-1;
            iE3DC_Req_Load_alt = iE3DC_Req_Load;
            if (iE3DC_Req_Load > e3dc_config.maximumLadeleistung)
                iE3DC_Req_Load = e3dc_config.maximumLadeleistung;
                SRscpValue PMContainer;
        //    Power = Power*-1;
        protocol.createContainerValue(&PMContainer, TAG_EMS_REQ_SET_POWER);
        protocol.appendValue(&PMContainer, TAG_EMS_REQ_SET_POWER_MODE,Mode);
//        if (Mode > 0)
            protocol.appendValue(&PMContainer, TAG_EMS_REQ_SET_POWER_VALUE,iE3DC_Req_Load);
        // append sub-container to root container
        protocol.appendValue(&rootValue, PMContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(PMContainer);
        }

        // request battery information
        SRscpValue batteryContainer;
        protocol.createContainerValue(&batteryContainer, TAG_BAT_REQ_DATA);
        protocol.appendValue(&batteryContainer, TAG_BAT_INDEX, (uint8_t)0);
        protocol.appendValue(&batteryContainer, TAG_BAT_REQ_RSOC);
        protocol.appendValue(&batteryContainer, TAG_BAT_REQ_MODULE_VOLTAGE);
        protocol.appendValue(&batteryContainer, TAG_BAT_REQ_CURRENT);
        // append sub-container to root container
        protocol.appendValue(&rootValue, batteryContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(batteryContainer);
        
        // request Power Meter information
        uint8_t uindex = e3dc_config.wurzelzaehler;
        SRscpValue PMContainer;
        protocol.createContainerValue(&PMContainer, TAG_PM_REQ_DATA);
        protocol.appendValue(&PMContainer, TAG_PM_INDEX,uindex);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L1);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L2);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L3);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L1);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L2);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L3);
        // append sub-container to root container
        protocol.appendValue(&rootValue, PMContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(PMContainer);
// EXTERNER ZÄHLER 1
        protocol.createContainerValue(&PMContainer, TAG_PM_REQ_DATA);
        protocol.appendValue(&PMContainer, TAG_PM_INDEX, (uint8_t)1);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L1);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L2);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L3);
//        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L1);
//        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L2);
//        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L3);
        // append sub-container to root container
if (e3dc_config.ext1)
        protocol.appendValue(&rootValue, PMContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(PMContainer);
        // EXTERNER ZÄHLER 2
        protocol.createContainerValue(&PMContainer, TAG_PM_REQ_DATA);
        protocol.appendValue(&PMContainer, TAG_PM_INDEX, (uint8_t)2);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L1);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L2);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L3);
//        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L1);
//        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L2);
//        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L3);
        // append sub-container to root container
if (e3dc_config.ext2)
            protocol.appendValue(&rootValue, PMContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(PMContainer);
        // EXTERNER ZÄHLER 3
        protocol.createContainerValue(&PMContainer, TAG_PM_REQ_DATA);
        protocol.appendValue(&PMContainer, TAG_PM_INDEX, (uint8_t)3);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L1);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L2);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L3);
        //        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L1);
        //        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L2);
        //        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L3);
        // append sub-container to root container
if (e3dc_config.ext3)
            protocol.appendValue(&rootValue, PMContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(PMContainer);
        // EXTERNER ZÄHLER 4
        protocol.createContainerValue(&PMContainer, TAG_PM_REQ_DATA);
        protocol.appendValue(&PMContainer, TAG_PM_INDEX, (uint8_t)4);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L1);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L2);
        protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L3);
        //        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L1);
        //        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L2);
        //        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L3);
        // append sub-container to root container
if (e3dc_config.ext4)
                    protocol.appendValue(&rootValue, PMContainer);
                // free memory of sub-container as it is now copied to rootValue
                protocol.destroyValueData(PMContainer);
                // EXTERNER ZÄHLER 7
                protocol.createContainerValue(&PMContainer, TAG_PM_REQ_DATA);
                protocol.appendValue(&PMContainer, TAG_PM_INDEX, (uint8_t)7);
                protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L1);
                protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L2);
                protocol.appendValue(&PMContainer, TAG_PM_REQ_POWER_L3);
                //        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L1);
                //        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L2);
                //        protocol.appendValue(&PMContainer, TAG_PM_REQ_VOLTAGE_L3);
                // append sub-container to root container
if (e3dc_config.ext7)
        protocol.appendValue(&rootValue, PMContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(PMContainer);

        
        // request Power Inverter information DC
        SRscpValue PVIContainer;
        protocol.createContainerValue(&PVIContainer, TAG_PVI_REQ_DATA);
        protocol.appendValue(&PVIContainer, TAG_PVI_INDEX, (uint8_t)0);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_DC_POWER, (uint8_t)0);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_DC_VOLTAGE, (uint8_t)0);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_DC_CURRENT, (uint8_t)0);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_DC_POWER, (uint8_t)1);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_DC_VOLTAGE, (uint8_t)1);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_DC_CURRENT, (uint8_t)1);

        // append sub-container to root container
        protocol.appendValue(&rootValue, PVIContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(PVIContainer);

        // request Power Inverter information AC

        protocol.createContainerValue(&PVIContainer, TAG_PVI_REQ_DATA);
        protocol.appendValue(&PVIContainer, TAG_PVI_INDEX, (uint8_t)0);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_AC_POWER, (uint8_t)0);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_AC_VOLTAGE, (uint8_t)0);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_AC_CURRENT, (uint8_t)0);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_AC_POWER, (uint8_t)1);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_AC_VOLTAGE, (uint8_t)1);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_AC_CURRENT, (uint8_t)1);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_AC_POWER, (uint8_t)2);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_AC_VOLTAGE, (uint8_t)2);
        protocol.appendValue(&PVIContainer, TAG_PVI_REQ_AC_CURRENT, (uint8_t)2);

        // append sub-container to root container
        protocol.appendValue(&rootValue, PVIContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(PVIContainer);

        // request DCDC information
            SRscpValue DCDCContainer;
            
            protocol.createContainerValue(&DCDCContainer, TAG_DCDC_REQ_DATA);
            protocol.appendValue(&DCDCContainer, TAG_DCDC_INDEX, (uint8_t)0);
            protocol.appendValue(&DCDCContainer, TAG_DCDC_REQ_I_BAT);
            protocol.appendValue(&DCDCContainer, TAG_DCDC_REQ_U_BAT);
            protocol.appendValue(&DCDCContainer, TAG_DCDC_REQ_P_BAT);
            protocol.appendValue(&DCDCContainer, TAG_DCDC_REQ_I_DCL);
            protocol.appendValue(&DCDCContainer, TAG_DCDC_REQ_U_DCL);
            protocol.appendValue(&DCDCContainer, TAG_DCDC_REQ_P_DCL);
            
            // append sub-container to root container
            protocol.appendValue(&rootValue, DCDCContainer);
            // free memory of sub-container as it is now copied to rootValue
            protocol.destroyValueData(DCDCContainer);

        // request Wallbox information

        SRscpValue WBContainer;
  
/*
         protocol.createContainerValue(&WBContainer, TAG_WB_REQ_AVAILABLE_SOLAR_POWER);
         protocol.appendValue(&WBContainer, TAG_WB_INDEX, (uint8_t)0);

        // append sub-container to root container
         protocol.appendValue(&rootValue, WBContainer);
         // free memory of sub-container as it is now copied to rootValue
         protocol.destroyValueData(WBContainer);
*/
        
        protocol.createContainerValue(&WBContainer, TAG_WB_REQ_DATA);
        protocol.appendValue(&WBContainer, TAG_WB_INDEX, (uint8_t)e3dc_config.wallbox);
//        protocol.appendValue(&WBContainer, TAG_WB_REQ_PM_MODE);
//        protocol.appendValue(&WBContainer, TAG_WB_REQ_MODE);
//        protocol.appendValue(&WBContainer, TAG_WB_REQ_STATUS);

        protocol.appendValue(&WBContainer, TAG_WB_REQ_PARAM_1);
//        protocol.appendValue(&WBContainer, TAG_WB_REQ_PARAM_2);
        protocol.appendValue(&WBContainer, TAG_WB_REQ_EXTERN_DATA_ALG);

        protocol.appendValue(&WBContainer, TAG_WB_REQ_PM_POWER_L1);
        protocol.appendValue(&WBContainer, TAG_WB_REQ_PM_POWER_L2);
        protocol.appendValue(&WBContainer, TAG_WB_REQ_PM_POWER_L3);
//        protocol.appendValue(&WBContainer, TAG_WB_REQ_AVAILABLE_SOLAR_POWER);

        // append sub-container to root container
if (e3dc_config.wallbox>=0)
        protocol.appendValue(&rootValue, WBContainer);
        // free memory of sub-container as it is now copied to rootValue
        protocol.destroyValueData(WBContainer);

        
        if(iBattPowerStatus == 2)
            
        {
            // request RootPower Meter information
        }

    }

    
    // create buffer frame to send data to the S10
    protocol.createFrameAsBuffer(frameBuffer, rootValue.data, rootValue.length, true); // true to calculate CRC on for transfer
    // the root value object should be destroyed after the data is copied into the frameBuffer and is not needed anymore
    protocol.destroyValueData(rootValue);
//    printf("Request cyclic example data done %s %2ld:%2ld:%2ld",VERSION,tm_CONF_dt%(24*3600)/3600,tm_CONF_dt%3600/60,tm_CONF_dt%60);

    return 0;
}

int handleResponseValue(RscpProtocol *protocol, SRscpValue *response)
{
    char buffer[127];
    // check if any of the response has the error flag set and react accordingly
    if(response->dataType == RSCP::eTypeError) {
        // handle error for example access denied errors
        uint32_t uiErrorCode = protocol->getValueAsUInt32(response);
        sprintf(Log,"ERR Tag 0x%08X received error code %u.\n", response->tag, uiErrorCode);
        WriteLog();
        return -1;
    }

    

    // check the SRscpValue TAG to detect which response it is
    switch(response->tag){
    case TAG_RSCP_AUTHENTICATION: {
        // It is possible to check the response->dataType value to detect correct data type
        // and call the correct function. If data type is known,
        // the correct function can be called directly like in this case.
        uint8_t ucAccessLevel = protocol->getValueAsUChar8(response);
        if(ucAccessLevel > 0) {
            iAuthenticated = 1;
        }
        printf("RSCP authentitication level %i\n", ucAccessLevel);
        break;
    }
    case TAG_EMS_POWER_PV: {    // response for TAG_EMS_REQ_POWER_PV
        int32_t iPower = protocol->getValueAsInt32(response);
        printf("EMS PV %i", iPower);
        iPower_PV = iPower;
        iPower_PV_E3DC = iPower;
        break;
    }
    case TAG_EMS_POWER_BAT: {    // response for TAG_EMS_REQ_POWER_BAT
        iPower_Bat = protocol->getValueAsInt32(response);
        printf(" BAT %i", iPower_Bat);
        break;
    }
    case TAG_EMS_BAT_SOC: {              // response for TAG_BAT_REQ_RSOC
        fBatt_SOC = protocol->getValueAsUChar8(response);
//        printf("Battery SOC %0.1f %% ", fBatt_SOC);
        break;
    }
    case TAG_EMS_POWER_HOME: {    // response for TAG_EMS_REQ_POWER_HOME
        int32_t iPower2 = protocol->getValueAsInt32(response);
        printf(" home %i", iPower2);
        iPowerBalance = iPower2;
        iPowerHome = iPower2;

        break;
    }
    case TAG_EMS_POWER_GRID: {    // response for TAG_EMS_REQ_POWER_GRID
        int32_t iPower = protocol->getValueAsInt32(response);
        iPowerBalance = iPowerBalance- iPower_PV + iPower_Bat - iPower;
        printf(" grid %i", iPower);
        printf(" E3DC %i ", -iPowerBalance - int(fPower_WB));
        printf(" # %i", iPower_PV - iPower_Bat + iPower - int(fPower_WB));
        printf("%c[K\n", 27 );

        break;
    }
    case TAG_EMS_POWER_ADD: {    // response for TAG_EMS_REQ_POWER_ADD
        int32_t iPower = protocol->getValueAsInt32(response);

        printf(" add %i", - iPower);
        iPower_PV = iPower_PV - iPower;
        printf(" # %i", iPower_PV);
        break;
    }
        case TAG_EMS_SET_POWER: {    // response for TAG_EMS_SET_POWER
            int32_t iPower = protocol->getValueAsInt32(response);
            
//            printf(" SET %i\n", iPower);
            break;
        }
        case TAG_EMS_REMAINING_BAT_CHARGE_POWER: {    // response for TAG_EMS_SET_POWER
                    int32_t iPower = protocol->getValueAsInt32(response);
                    
        //            printf(" SET %i\n", iPower);
                    break;
                }
        case TAG_EMS_EMERGENCY_POWER_STATUS: {    // response for TAG_EMS_EMERGENCY_POWER_STATUS
            int8_t iPower = protocol->getValueAsUChar8(response);
            
//            printf(" EMERGENCY_POWER_STATUS: %i\n", iPower);
            iNotstrom = iPower;
            break;
        }
    case TAG_PM_POWER_L1: {    // response for TAG_EMS_REQ_POWER_ADD
            int32_t iPower = protocol->getValueAsInt32(response);
            printf("L1 is %i W\n", iPower);
            break;
    }
    case TAG_DCDC_DATA: {        // resposne for TAG_DCDC_REQ_DATA
        uint8_t ucBatteryIndex = 0;
        std::vector<SRscpValue> batteryData = protocol->getValueAsContainer(response);
        for(size_t i = 0; i < batteryData.size(); ++i) {
            if(batteryData[i].dataType == RSCP::eTypeError) {
                // handle error for example access denied errors
                uint32_t uiErrorCode = protocol->getValueAsUInt32(&batteryData[i]);
                printf("Tag 0x%08X received error code %u.\n", batteryData[i].tag, uiErrorCode);
                return -1;
            }
            // check each battery sub tag
            switch(batteryData[i].tag) {
                case TAG_DCDC_INDEX: {
                    ucBatteryIndex = protocol->getValueAsUChar8(&batteryData[i]);
                    printf("%c[K\n", 27 ); // neue Zeile
                    printf("DCDC ");
                    break;
                }
                case TAG_DCDC_U_BAT: {    // response for TAG_BAT_REQ_MODULE_VOLTAGE
                    float fVoltage = protocol->getValueAsFloat32(&batteryData[i]);
                    printf("%0.02fV ", fVoltage);
                    break;
                }
                case TAG_DCDC_I_BAT: {    // response for TAG_BAT_REQ_CURRENT
                   fCurrent = protocol->getValueAsFloat32(&batteryData[i]);
//                    fDCDC = fDCDC + fCurrent;
                    printf("%0.02fA %0.04fAh ", fCurrent, fDCDC/3600);

                    break;
                }
                case TAG_DCDC_P_BAT: {    // response for TAG_BAT_REQ_CURRENT
                    float fPower = protocol->getValueAsFloat32(&batteryData[i]);
                    printf("%0.02fW ", fPower);

                    break;
                }
                case TAG_DCDC_U_DCL: {    // response for TAG_BAT_REQ_MODULE_VOLTAGE
                    float fVoltage = protocol->getValueAsFloat32(&batteryData[i]);
                    printf("%0.02fV ", fVoltage);
                    break;
                }
                case TAG_DCDC_I_DCL: {    // response for TAG_BAT_REQ_CURRENT
                    float fCurrent = protocol->getValueAsFloat32(&batteryData[i]);
                    printf("%0.02fA ", fCurrent);

                    break;
                }
                case TAG_DCDC_P_DCL: {    // response for TAG_BAT_REQ_CURRENT
                    float fPower = protocol->getValueAsFloat32(&batteryData[i]);
                    printf("%0.02fW ", fPower);

                    break;
                }

                default:
                    // default behaviour
                    printf("Unknown DCDC tag %08X\n", response->tag);
                    break;
                }
            }
            protocol->destroyValueData(batteryData);
            break;
    }
   case TAG_BAT_DATA: {        // resposne for TAG_BAT_REQ_DATA
        uint8_t ucBatteryIndex = 0;
        std::vector<SRscpValue> batteryData = protocol->getValueAsContainer(response);
        for(size_t i = 0; i < batteryData.size(); ++i) {
            if(batteryData[i].dataType == RSCP::eTypeError) {
                // handle error for example access denied errors
                uint32_t uiErrorCode = protocol->getValueAsUInt32(&batteryData[i]);
                printf("Tag 0x%08X received error code %u.\n", batteryData[i].tag, uiErrorCode);
                return -1;
            }
            // check each battery sub tag
            switch(batteryData[i].tag) {
            case TAG_BAT_INDEX: {
                ucBatteryIndex = protocol->getValueAsUChar8(&batteryData[i]);
                break;
            }
            case TAG_BAT_RSOC: {              // response for TAG_BAT_REQ_RSOC
                if (abs(fBatt_SOC - protocol->getValueAsFloat32(&batteryData[i]))<1)
                fBatt_SOC = protocol->getValueAsFloat32(&batteryData[i]);
                printf("Battery SOC %0.02f%% ", fBatt_SOC);
                break;
            }
            case TAG_BAT_MODULE_VOLTAGE: {    // response for TAG_BAT_REQ_MODULE_VOLTAGE
                fVoltage = protocol->getValueAsFloat32(&batteryData[i]);
                printf(" %0.1f V ", fVoltage);
                break;
            }
            case TAG_BAT_CURRENT: {    // response for TAG_BAT_REQ_CURRENT
                float fCurrent = protocol->getValueAsFloat32(&batteryData[i]);
                fPower_Bat = fVoltage*fCurrent;
                printf(" %0.02f A %0.02f W", fCurrent,fPower_Bat);
                printf("%c[K\n", 27 );

                break;
            }
            case TAG_BAT_STATUS_CODE: {    // response for TAG_BAT_REQ_STATUS_CODE
                uint32_t uiErrorCode = protocol->getValueAsUInt32(&batteryData[i]);
                printf("Battery status code is 0x%08X\n", uiErrorCode);
                break;
            }
            case TAG_BAT_ERROR_CODE: {    // response for TAG_BAT_REQ_ERROR_CODE
                uint32_t uiErrorCode = protocol->getValueAsUInt32(&batteryData[i]);
                printf("Battery error code is 0x%08X\n", uiErrorCode);
                break;
            }
            // ...
            default:
                // default behaviour
                printf("Unknown battery tag %08X\n", response->tag);
                break;
            }
        }
        protocol->destroyValueData(batteryData);
        break;
    }
        case TAG_PM_DATA: {        // resposne for TAG_PM_REQ_DATA
            uint8_t ucPMIndex = 0;
            float fPower1 = 0;
            float fPower2 = 0;
            float fPower3 = 0;
            std::vector<SRscpValue> PMData = protocol->getValueAsContainer(response);
            for(size_t i = 0; i < PMData.size(); ++i) {
                if(PMData[i].dataType == RSCP::eTypeError) {
                    // handle error for example access denied errors
                    uint32_t uiErrorCode = protocol->getValueAsUInt32(&PMData[i]);
                    printf("Tag 0x%08X received error code %u.\n", PMData[i].tag, uiErrorCode);
                    return -1;
                }
                // check each PM sub tag
                switch(PMData[i].tag) {
                    case TAG_PM_INDEX: {
                        ucPMIndex = protocol->getValueAsUChar8(&PMData[i]);
                        break;
                    }
                    case TAG_PM_POWER_L1: {              // response for TAG_PM_REQ_L1
                        fPower1 = protocol->getValueAsDouble64(&PMData[i]);
                        printf("#%u is %0.1f W ", ucPMIndex,fPower1);
                        break;
                    }
                    case TAG_PM_POWER_L2: {              // response for TAG_PM_REQ_L2
                        fPower2 = protocol->getValueAsDouble64(&PMData[i]);
                        break;
                    }
                    case TAG_PM_POWER_L3: {              // response for TAG_PM_REQ_L3
                        fPower3 = protocol->getValueAsDouble64(&PMData[i]);
                        if ((fPower2+fPower3)||0){
                        printf("%0.1f W %0.1f W ", fPower2, fPower3);
                        printf(" # %0.1f W ", fPower1+fPower2+fPower3);
                        printf("%c[K\n", 27 );

                        }
                        if (ucPMIndex==e3dc_config.wurzelzaehler) {
                                sprintf(buffer,"openWB/set/evu/APhase1 -m %0.1f",float(fPower1/fL1V));
                                MQTTsend(e3dc_config.openWB_ip,buffer);
                            sprintf(buffer,"openWB/set/evu/APhase2 -m %0.1f",float(fPower2/fL2V));
                            MQTTsend(e3dc_config.openWB_ip,buffer);
                            sprintf(buffer,"openWB/set/evu/APhase3 -m %0.1f",float(fPower3/fL3V));
                            MQTTsend(e3dc_config.openWB_ip,buffer);

                            fPower_Grid = fPower1+fPower2+fPower3;
                            if (iAvPower_GridCount<3600)
                                iAvPower_GridCount++;
                            fAvPower_Grid3600 = fAvPower_Grid3600*(iAvPower_GridCount-1)/iAvPower_GridCount + fPower_Grid/iAvPower_GridCount;
                            if (iAvPower_GridCount<600)
                                fAvPower_Grid600 = fAvPower_Grid3600; else
                            fAvPower_Grid600 = fAvPower_Grid600*599/600 + fPower_Grid/600;
                            if (iAvPower_GridCount<60)
                                fAvPower_Grid60 = fAvPower_Grid3600; else
                                    fAvPower_Grid60 = fAvPower_Grid60*59/60 + fPower_Grid/60;
                            if (iAvPower_GridCount<20)
                                fAvPower_Grid = fAvPower_Grid3600; else
                            fAvPower_Grid = fAvPower_Grid*19/20 + fPower_Grid/20;
                            printf(" & %0.01f %0.01f %0.01f %0.01f W ", fAvPower_Grid3600, fAvPower_Grid600, fAvPower_Grid60, fAvPower_Grid);
                    }
                        break;
                    }
                    case TAG_PM_VOLTAGE_L1: {              // response for TAG_PM_REQ_L1
                        float fPower = protocol->getValueAsFloat32(&PMData[i]);
                        printf(" %0.1f V", fPower);
                        fL1V = fPower;
                        sprintf(buffer,"openWB/set/evu/VPhase1 -m %0.1f",float(fPower));
                        MQTTsend(e3dc_config.openWB_ip,buffer);

                        break;
                    }
                    case TAG_PM_VOLTAGE_L2: {              // response for TAG_PM_REQ_L2
                        float fPower = protocol->getValueAsFloat32(&PMData[i]);
                        printf(" %0.1f V", fPower);
                        fL2V = fPower;
                        sprintf(buffer,"openWB/set/evu/VPhase2 -m %0.1f",float(fPower));
                        MQTTsend(e3dc_config.openWB_ip,buffer);
                        break;
                    }
                    case TAG_PM_VOLTAGE_L3: {              // response for TAG_PM_REQ_L3
                        float fPower = protocol->getValueAsFloat32(&PMData[i]);
                        printf(" %0.1f V", fPower);
                        printf("%c[K\n", 27 );
                        fL3V = fPower;
                        sprintf(buffer,"openWB/set/evu/VPhase3 -m %0.1f",float(fPower));
                        MQTTsend(e3dc_config.openWB_ip,buffer);
                        break;
                    }
                        // ...
                    default:
                        // default behaviour
                        printf("Unknown Grid tag %08X\n", response->tag);
                        printf("Unknown Grid datatype %08X\n", response->dataType);
                        break;
                }
            }
            protocol->destroyValueData(PMData);
            break;
        }
        case TAG_PVI_DATA: {        // resposne for TAG_PVI_REQ_DATA
            uint8_t ucPVIIndex = 0;
            float fGesPower = 0;
            std::vector<SRscpValue> PVIData = protocol->getValueAsContainer(response);
            for(size_t i = 0; i < PVIData.size(); ++i) {
                if(PVIData[i].dataType == RSCP::eTypeError) {
                    // handle error for example access denied errors
                    uint32_t uiErrorCode = protocol->getValueAsUInt32(&PVIData[i]);
                    printf("Tag 0x%08X received error code %u.\n", PVIData[i].tag, uiErrorCode);
                    return -1;
                }
                // check each PVI sub tag
                switch(PVIData[i].tag) {
                    case TAG_PVI_INDEX: {
                        ucPVIIndex = protocol->getValueAsUChar8(&PVIData[i]);
                        break;
                    }
                    case TAG_PVI_DC_POWER:
                    {
                        int index = -1;
                        std::vector<SRscpValue> container = protocol->getValueAsContainer(&PVIData[i]);
                        for (size_t n = 0; n < container.size(); n++)
                        {
                            if (container[n].tag == TAG_PVI_INDEX)
                            {
                                index = protocol->getValueAsUInt16(&container[n]);
                            }
                            else if (container[n].tag == TAG_PVI_VALUE)
                            {
                                float fPower = protocol->getValueAsFloat32(&container[n]);
                                printf("DC%u %0.0f W ", index, fPower);

                            }
                        }
                        protocol->destroyValueData(container);
                        break;
                    }
                    case TAG_PVI_DC_VOLTAGE:
                    {
                        int index = -1;
                        std::vector<SRscpValue> container = protocol->getValueAsContainer(&PVIData[i]);
                        for (size_t n = 0; n < container.size(); n++)
                        {
                            if (container[n].tag == TAG_PVI_INDEX)
                            {
                                index = protocol->getValueAsUInt16(&container[n]);
                            }
                            else if (container[n].tag == TAG_PVI_VALUE)
                            {
                                float fPower = protocol->getValueAsFloat32(&container[n]);
                                printf(" %0.0f V", fPower);
                                
                            }
                        }
                        protocol->destroyValueData(container);
                        break;
                    }
                    case TAG_PVI_DC_CURRENT:
                    {
                        int index = -1;
                        std::vector<SRscpValue> container = protocol->getValueAsContainer(&PVIData[i]);
                        for (size_t n = 0; n < container.size(); n++)
                        {
                            if (container[n].tag == TAG_PVI_INDEX)
                            {
                                index = protocol->getValueAsUInt16(&container[n]);
                            }
                            else if (container[n].tag == TAG_PVI_VALUE)
                            {
                                float fPower = protocol->getValueAsFloat32(&container[n]);
//                                printf(" %0.2f A \n", fPower);
                                printf(" %0.2f A ", fPower);

                            }
                        }
                        protocol->destroyValueData(container);
                        break;
                    }
                                        case TAG_PVI_AC_POWER:
                                        {
                                            int index = -1;
                                            std::vector<SRscpValue> container = protocol->getValueAsContainer(&PVIData[i]);
                                            for (size_t n = 0; n < container.size(); n++)
                                            {
                                                if (container[n].tag == TAG_PVI_INDEX)
                                                {
                                                    index = protocol->getValueAsUInt16(&container[n]);
                                                }
                                                else if (container[n].tag == TAG_PVI_VALUE)
                                                {
                                                    float fPower = protocol->getValueAsFloat32(&container[n]);
                                                    if (index == 0)    printf("%c[K\n", 27 );
                                                    fGesPower = fGesPower + fPower;
                                                    printf("AC%u %0.0fW", index, fPower);

                                                }
                                            }
                                            protocol->destroyValueData(container);
                                            break;
                                        }
                                        case TAG_PVI_AC_VOLTAGE:
                                        {
                                            int index = -1;
                                            std::vector<SRscpValue> container = protocol->getValueAsContainer(&PVIData[i]);
                                            for (size_t n = 0; n < container.size(); n++)
                                            {
                                                if (container[n].tag == TAG_PVI_INDEX)
                                                {
                                                    index = protocol->getValueAsUInt16(&container[n]);
                                                }
                                                else if (container[n].tag == TAG_PVI_VALUE)
                                                {
                                                    float fPower = protocol->getValueAsFloat32(&container[n]);
                                                    printf(" %0.0fV", fPower);
                                                    
                                                }
                                            }
                                            protocol->destroyValueData(container);
                                            break;
                                        }
                                        case TAG_PVI_AC_CURRENT:
                                        {
                                            int index = -1;
                                            std::vector<SRscpValue> container = protocol->getValueAsContainer(&PVIData[i]);
                                            for (size_t n = 0; n < container.size(); n++)
                                            {
                                                if (container[n].tag == TAG_PVI_INDEX)
                                                {
                                                    index = protocol->getValueAsUInt16(&container[n]);
                                                }
                                                else if (container[n].tag == TAG_PVI_VALUE)
                                                {
                                                    float fPower = protocol->getValueAsFloat32(&container[n]);
                    //                                printf(" %0.2f A \n", fPower);
                                                    printf(" %0.2fA ", fPower);
                                                    if (index == 2) printf(" # %0.0fW",fGesPower);

                                                }
                                            }
                                            protocol->destroyValueData(container);
                                            break;
                                        }
                        // ...
                    default:
                        // default behaviour
                        printf("Unknown PVI tag %08X\n", response->tag);
                        printf("Unknown PVI datatype %08X\n", response->dataType);
                        break;
                }
            }
            protocol->destroyValueData(PVIData);
            break;
        }

            
        case TAG_WB_AVAILABLE_SOLAR_POWER: {              // response for TAG_WB_AVAILABLE_SOLAR_POWER
            uint8_t ucPMIndex = 0;
            std::vector<SRscpValue> PMData = protocol->getValueAsContainer(response);
            for(size_t i = 0; i < PMData.size(); ++i) {
                if(PMData[i].dataType == RSCP::eTypeError) {
                    // handle error for example access denied errors
                    uint32_t uiErrorCode = protocol->getValueAsUInt32(&PMData[i]);
                    printf("Tag 0x%08X received error code %u.\n", PMData[i].tag, uiErrorCode);
                    return -1;
                }
                printf(" datatype %08X", PMData[i].dataType);
                printf(" length %02X", PMData[i].length);
                printf(" data");
                for (size_t y = 0; y<PMData[i].length; y++) {
                    if (y%4 == 0)
                        printf(" ");
                    printf("%02X", PMData[i].data[y]);
                }
                printf("\n");

/*
                
            float fPower = protocol->getValueAsDouble64(&PMData[i]);
             printf(" %0.1f W", fPower);
             fPower_WB = fPower_WB + fPower;
             printf(" Solar %0.1f W\n", fPower_WB);
*/             break;
            }}

//        case TAG_WB_AVAILABLE_SOLAR_POWER:              // response for TAG_WB_AVAILABLE_SOLAR_POWER

        case TAG_WB_DATA: {        // resposne for TAG_WB_DATA
            uint8_t ucPMIndex = 0;
            std::vector<SRscpValue> PMData = protocol->getValueAsContainer(response);
            for(size_t i = 0; i < PMData.size(); ++i) {
                if(PMData[i].dataType == RSCP::eTypeError) {
                    // handle error for example access denied errors
                    uint32_t uiErrorCode = protocol->getValueAsUInt32(&PMData[i]);
                    printf("Tag 0x%08X received error code %u.\n", PMData[i].tag, uiErrorCode);
                    return -1;
                }
                // check each PM sub tag
                switch(PMData[i].tag) {
                    case TAG_WB_INDEX: {
                        ucPMIndex = protocol->getValueAsUChar8(&PMData[i]);
                        break;
                    }
                    case TAG_WB_PM_POWER_L1: {              // response for TAG_PM_REQ_L1
                        float fPower = protocol->getValueAsDouble64(&PMData[i]);
                        printf("\nWB is %0.1f W", fPower);
                        fPower_WB = fPower;
/*                        if (iCyc_WB>0) iCyc_WB--;
                        if (fPower > 1)
                        {   if (fPower_Grid < -800)
                            if ((WBchar[2] < 16) && (iCyc_WB == 0))
                            {WBchar[2]++;
                            iWBStatus = 2;
                            iCyc_WB = 3;
                            }
                        if (iPower_Bat < -300)
                        if (WBchar[2] > 6)
                        {   WBchar[2]--;
                            iWBStatus = 2;
                            iCyc_WB = 3;
                        }}
*/
                        break;
                    }
                    case TAG_WB_PM_POWER_L2: {              // response for TAG_PM_REQ_L2
                        float fPower = protocol->getValueAsDouble64(&PMData[i]);
                        printf(" %0.1f W", fPower);
                        fPower_WB = fPower_WB + fPower;
                        break;
                    }
                    case TAG_WB_PM_POWER_L3: {              // response for TAG_PM_REQ_L3
                        float fPower = protocol->getValueAsDouble64(&PMData[i]);
                        printf(" %0.1f W", fPower);
                        fPower_WB = fPower_WB + fPower;
                        printf(" Total %0.1f W", fPower_WB);
                        break;
                    }
/*                    case TAG_WB_AVAILABLE_SOLAR_POWER: {              // response for TAG_PM_REQ_L3
                        float fPower = protocol->getValueAsDouble64(&PMData[i]);
                        printf(" %0.1f W", fPower);
                        fPower_WB = fPower_WB + fPower;
                        printf(" Solar %0.1f W\n", fPower_WB);
                        break;
                    }
*/
 /*                    case TAG_WB_EXTERN_DATA: {              // response for TAG_RSP_PARAM_1
                        printf(" WB EXTERN_DATA\n");
                    }
                    case TAG_WB_EXTERN_DATA_LEN: {              // response for TAG_RSP_PARAM_1
                        printf(" WB EXTERN_DATA_LEN\n");
 
                    }
*/
                    case (TAG_WB_RSP_PARAM_1): {              // response for TAG_RSP_PARAM_1

                        
/*                       printf(" WB Param_1\n");
                        printf(" datatype %08X", PMData[i].dataType);
                        printf(" length %02X", PMData[i].length);
                        printf(" data");
                        for (size_t y = 0; y<PMData[i].length; y++) {
                            if (y%4 == 0)
                            printf(" ");
                        printf("%02X", PMData[i].data[y]);
                        }
                        printf("\n");
*/
                        std::vector<SRscpValue> WBData = protocol->getValueAsContainer(&PMData[i]);

                        for(size_t i = 0; i < WBData.size(); ++i) {
                            if(WBData[i].dataType == RSCP::eTypeError) {
                                // handle error for example access denied errors
                                uint32_t uiErrorCode = protocol->getValueAsUInt32(&WBData[i]);
                                printf("Tag 0x%08X received error code %u.\n", WBData[i].tag, uiErrorCode);
                                return -1;
                            }
                            // check each PM sub tag
                            switch(WBData[i].tag) {
                                case TAG_WB_EXTERN_DATA: {              // response for TAG_RSP_PARAM_1
//                                    printf(" WB EXTERN_DATA\n");
                                    memcpy(&WBchar,&WBData[i].data[0],sizeof(WBchar));
//                                    printf(" WB EXTERN_DATA\n");
/*                                    printf("\n");
                                    for(size_t x = 0; x < sizeof(WBchar); ++x)
                                        printf("%02X", WBchar[x]);
                                    printf("\n");
*/
                                    
//                                    bWBLademodus = (WBchar[0]&1);
                                    bWBLademodus = bWBSonne;
//                                    WBchar6[0]=WBchar[0];
                                    WBchar6[0]=2+bWBSonne;
                                    printf("%c[K\n", 27 );
                                    printf("WB: Modus %02X ",uint8_t(cWBALG));
//                                    for(size_t x = 0; x < sizeof(WBchar); ++x)
//                                        printf("%02X ", uint8_t(WBchar[x]));
                                    if (bWBLademodus) printf("Sonne "); else printf("Netz: ");
                                    if (bWBConnect) printf(" Dose verriegelt");
                                    if (bWBStart) printf(" gestartet");
                                    if (bWBCharge) printf(" lädt");
                                    if (bWBStopped ) printf(" gestoppt");

//                                    if ((WBchar[2]==32)&&(iWBSoll!=32)) {
                                    if (WBchar[2]==e3dc_config.wbmaxladestrom) {
                                        bWBmaxLadestrom=true;
                                    }
//                                    if ((WBchar[2]==30)&&(iWBSoll!=30)) {
//                                        bWBmaxLadestrom=true;
//                                    }
//                                if  ((WBchar[2]==31)&&(iWBSoll!=31)) {
                                    if  (WBchar[2]==e3dc_config.wbmaxladestrom-1) {
                                        bWBmaxLadestrom=false;
                                    }
                                    if ((int(WBchar[2])!=iWBIst)&&(iWBStatus==1))
                                    if ((not bWBmaxLadestrom)&&(int(WBchar[2])>iWBSoll))
                                    {
// ladeschwelle ändern 8..9

                                        
                                        if  (WBchar[2]==8)
                                        GetConfig();
/*                                        if  (WBchar[2]==9)
                                        {
                                            e3dc_config.ladeschwelle = 100;
                                            e3dc_config.ladeende = 100;
                                            e3dc_config.ladeende2 = 100;

                                        }
// lademodus ändern 10..19
                                        if  ((WBchar[2]>=11)&&(WBchar[2]<=19))
// lademodus 10 ausschließen, da gelegentlich vom System gesetzt
                                        e3dc_config.wbmode = WBchar[2]-10;
                                        
                                        bWBChanged = true;
                                        
*/
                                        }

                                    iWBIst = WBchar[2];
                                    if (bWBmaxLadestrom) printf(" Manu");
                                    else printf(" Auto");
                                    printf(" Ladestrom %u/%uA ",iWBSoll,WBchar[2]);
                                    printf("%c[K", 27 );
                                    break;
                                }
                                    
                                case TAG_WB_EXTERN_DATA_LEN: {              // response for TAG_RSP_PARAM_1
                                    uint8_t iLen = protocol->getValueAsUChar8(&WBData[i]);

//                                    printf(" WB EXTERN_DATA_LEN %u\n",iLen);
                                    break;
                                    
                                }

                                default:

                                    printf("Unknown WB tag %08X", WBData[i].tag);
                                    printf(" datatype %08X", WBData[i].dataType);
                            }
                            
/*                                    printf(" length %02X", WBData[i].length);
                                    printf(" data %02X", WBData[i].data[0]);
                                    printf("%02X", WBData[i].data[1]);
                                    printf("%02X", WBData[i].data[2]);
                                    printf("%02X\n", WBData[i].data[3]);
*/                        }
                       
                            protocol->destroyValueData(WBData);
                        break;

                    }
                    case (TAG_WB_EXTERN_DATA_ALG): {              // response for TAG_RSP_PARAM_1

                         std::vector<SRscpValue> WBData = protocol->getValueAsContainer(&PMData[i]);

                         for(size_t i = 0; i < WBData.size(); ++i) {
                             if(WBData[i].dataType == RSCP::eTypeError) {
                                 // handle error for example access denied errors
                                 uint32_t uiErrorCode = protocol->getValueAsUInt32(&WBData[i]);
                                 printf("Tag 0x%08X received error code %u.\n", WBData[i].tag, uiErrorCode);
                                 return -1;
                             }
                             // check each PM sub tag
                             switch(WBData[i].tag) {
                                 case TAG_WB_EXTERN_DATA: {              // response for TAG_RSP_PARAM_1
                                     char WBchar[8];
                                     memcpy(&WBchar,&WBData[i].data[0],sizeof(WBchar));
                                     cWBALG = WBchar[2];
                                     bWBConnect = (WBchar[2]&8);
                                     bWBCharge = (WBchar[2]&32);
                                     bWBStart = (WBchar[2]&16);
                                     bWBStopped = (WBchar[2]&64);
                                     bWBSonne = (WBchar[2]&128);
/*                                     printf(" WB ALG EXTERN_DATA\n");
                                     printf("\n");
                                     for(size_t x = 0; x < sizeof(WBchar); ++x)
                                     { uint8_t y;
                                         y=WBchar[x];
                                         printf(" %02X", y);
                                     } printf("\n");
 */                                    break;
                                 }
                                     
                                 case TAG_WB_EXTERN_DATA_LEN: {              // response for TAG_RSP_PARAM_1
                                     uint8_t iLen = protocol->getValueAsUChar8(&WBData[i]);

 //                                    printf(" WB EXTERN_DATA_LEN %u\n",iLen);
                                     break;
                                     
                                 }

                                 default:

                                     printf("Unknown WB tag %08X", WBData[i].tag);
                                     printf(" datatype %08X", WBData[i].dataType);
                             }
                             
 /*                                    printf(" length %02X", WBData[i].length);
                                     printf(" data %02X", WBData[i].data[0]);
                                     printf("%02X", WBData[i].data[1]);
                                     printf("%02X", WBData[i].data[2]);
                                     printf("%02X\n", WBData[i].data[3]);
 */                        }
                        
                             protocol->destroyValueData(WBData);
                         break;

                     }

                    // ...
/*                    default:
                        // default behaviour
                        printf("Unknown WB tag %08X", PMData[i].tag);
                        printf(" datatype %08X", PMData[i].dataType);
                        printf(" length %02X", PMData[i].length);
                        printf(" data ");
                        uint8_t PMchar[PMData[i].length];
                        memcpy(&PMchar,&PMData[i].data[0],(PMData[i].length));
                        for(size_t x = 0; x < PMData[i].length; ++x)
                            printf("%02X", PMchar[x]);
                        printf("\n");
                        sleep(1);
*/                    break;
                }
            }
            protocol->destroyValueData(PMData);
            break;
        }
        case TAG_EMS_GET_POWER_SETTINGS:         // resposne for TAG_PM_REQ_DATA
        case TAG_EMS_SET_POWER_SETTINGS: {        // resposne for TAG_PM_REQ_DATA
            uint8_t ucPMIndex = 0;
            std::vector<SRscpValue> PMData = protocol->getValueAsContainer(response);
            for(size_t i = 0; i < PMData.size(); ++i) {
                if(PMData[i].dataType == RSCP::eTypeError) {
                    // handle error for example access denied errors
                    uint32_t uiErrorCode = protocol->getValueAsUInt32(&PMData[i]);
                    printf("TAG_EMS_GET_POWER_SETTINGS 0x%08X received error code %u.\n", PMData[i].tag, uiErrorCode);
                    return -1;
                }
                // check each PM sub tag
                switch(PMData[i].tag) {
                    case TAG_PM_INDEX: {
                        ucPMIndex = protocol->getValueAsUChar8(&PMData[i]);
                        break;
                    }
                    case TAG_EMS_POWER_LIMITS_USED: {              // response for POWER_LIMITS_USED
                        if (protocol->getValueAsBool(&PMData[i])){
                            printf("POWER_LIMITS_USED\n");
                            }
                        break;
                    }
                    case TAG_EMS_MAX_CHARGE_POWER: {              // 101 response for TAG_EMS_MAX_CHARGE_POWER
                        uint32_t uPower = protocol->getValueAsUInt32(&PMData[i]);
                        if (uPower < e3dc_config.maximumLadeleistung)
                            {if (uPower < 1500)
                                  e3dc_config.maximumLadeleistung = 1500; else
                                   e3dc_config.maximumLadeleistung = uPower;
                            printf("MAX_CHARGE_POWER %i W\n", uPower);}
                        break;
                    }
                    case TAG_EMS_MAX_DISCHARGE_POWER: {              //102 response for TAG_EMS_MAX_DISCHARGE_POWER
                        uint32_t uPower = protocol->getValueAsUInt32(&PMData[i]);
                        printf("MAX_DISCHARGE_POWER %i W\n", uPower);
                        iDischarge = uPower;
                        break;
                    }
                    case TAG_EMS_DISCHARGE_START_POWER:{              //103 response for TAG_EMS_DISCHARGE_START_POWER
                        uint32_t uPower = protocol->getValueAsUInt32(&PMData[i]);
                        printf("DISCHARGE_START_POWER %i W\n", uPower);
                        break;
                    }
                    case TAG_EMS_POWERSAVE_ENABLED: {              //104 response for TAG_EMS_POWERSAVE_ENABLED
                        if (protocol->getValueAsBool(&PMData[i])){
                            printf("POWERSAVE_ENABLED\n");
                        }
                        break;
                    }
                    case TAG_EMS_WEATHER_REGULATED_CHARGE_ENABLED: {//105 resp WEATHER_REGULATED_CHARGE_ENABLED
                        if (protocol->getValueAsBool(&PMData[i])){
                            printf("WEATHER_REGULATED_CHARGE_ENABLED\n");
                        }
                        break;
                    }
                        // ...
                    default:
                        // default behaviour
/*                        printf("Unkonwn GET_POWER_SETTINGS tag %08X", PMData[i].tag);
                        printf(" len %08X", PMData[i].length);
                        printf(" datatype %08X\n", PMData[i].dataType);
                        uint32_t uPower = protocol->getValueAsUInt32(&PMData[i]);
                        printf(" Value  %i\n", uPower);
*/                        break;
                }
            }
            protocol->destroyValueData(PMData);
//            sleep(10);
            break;
        }
    // ...
    default:
        // default behavior
        printf("Unknown tag %08X\n", response->tag);
        break;
    }
    return 0;

}

static int processReceiveBuffer(const unsigned char * ucBuffer, int iLength)
{
    RscpProtocol protocol;
    SRscpFrame frame;

    int iResult = protocol.parseFrame(ucBuffer, iLength, &frame);
    if(iResult < 0) {
        // check if frame length error occured
        // in that case the full frame length was not received yet
        // and the receive function must get more data
        if(iResult == RSCP::ERR_INVALID_FRAME_LENGTH) {
            return 0;
        }
        // otherwise a not recoverable error occured and the connection can be closed
        else {
            return iResult;
        }
    }

    int iProcessedBytes = iResult;

// Auslesen Zeitstempel aus dem Frame von der E3DC und ausgeben
//    time_t t;
//    struct tm * ts;
    
      tE3DC = frame.header.timestamp.seconds;
//    ts = localtime(&tE3DC);
    
//    printf("E3DC Zeit: %s", asctime(ts));

    

    // process each SRscpValue struct seperately
    for(unsigned int i=0; i < frame.data.size(); i++) {
        handleResponseValue(&protocol, &frame.data[i]);
    }

    // destroy frame data and free memory
    protocol.destroyFrameData(frame);

    // returned processed amount of bytes
    return iProcessedBytes;
}

static void receiveLoop(bool & bStopExecution)
{
    //--------------------------------------------------------------------------------------------------------------
    // RSCP Receive Frame Block Data
    //--------------------------------------------------------------------------------------------------------------
    // setup a static dynamic buffer which is dynamically expanded (re-allocated) on demand
    // the data inside this buffer is not released when this function is left
    static int iReceivedBytes = 0;
    static std::vector<uint8_t> vecDynamicBuffer;

    // check how many RSCP frames are received, must be at least 1
    // multiple frames can only occur in this example if one or more frames are received with a big time delay
    // this should usually not occur but handling this is shown in this example
    int iReceivedRscpFrames = 0;
    while(!bStopExecution && ((iReceivedBytes > 0) || iReceivedRscpFrames == 0))
    {
        // check and expand buffer
        if((vecDynamicBuffer.size() - iReceivedBytes) < 4096) {
            // check maximum size
            if(vecDynamicBuffer.size() > RSCP_MAX_FRAME_LENGTH) {
                // something went wrong and the size is more than possible by the RSCP protocol
                printf("Maximum buffer size exceeded %lu\n", vecDynamicBuffer.size());
                bStopExecution = true;
                break;
            }
            // increase buffer size by 4096 bytes each time the remaining size is smaller than 4096
            vecDynamicBuffer.resize(vecDynamicBuffer.size() + 4096);
        }
        // receive data
        long iResult = SocketRecvData(iSocket, &vecDynamicBuffer[0] + iReceivedBytes, vecDynamicBuffer.size() - iReceivedBytes);
        if(iResult < 0)
        {
            // check errno for the error code to detect if this is a timeout or a socket error
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                // receive timed out -> continue with re-sending the initial block
                printf("Response receive timeout (retry)\n");
                break;
            }
            // socket error -> check errno for failure code if needed
            printf("Socket receive error. errno %i\n", errno);
            bStopExecution = true;
            break;
        }
        else if(iResult == 0)
        {
            // connection was closed regularly by peer
            // if this happens on startup each time the possible reason is
            // wrong AES password or wrong network subnet (adapt hosts.allow file required)
            printf("Connection closed by peer\n");
            bStopExecution = true;
            break;
        }
        // increment amount of received bytes
        iReceivedBytes += iResult;

        // process all received frames
        while (!bStopExecution)
        {
            // round down to a multiple of AES_BLOCK_SIZE
            int iLength = ROUNDDOWN(iReceivedBytes, AES_BLOCK_SIZE);
            // if not even 32 bytes were received then the frame is still incomplete
            if(iLength == 0) {
                break;
            }
            // resize temporary decryption buffer
            std::vector<uint8_t> decryptionBuffer;
            decryptionBuffer.resize(iLength);
            // initialize encryption sequence IV value with value of previous block
            aesDecrypter.SetIV(ucDecryptionIV, AES_BLOCK_SIZE);
            // decrypt data from vecDynamicBuffer to temporary decryptionBuffer
            aesDecrypter.Decrypt(&vecDynamicBuffer[0], &decryptionBuffer[0], iLength / AES_BLOCK_SIZE);

            // data was received, check if we received all data
            int iProcessedBytes = processReceiveBuffer(&decryptionBuffer[0], iLength);
            if(iProcessedBytes < 0) {
                // an error occured;
                printf("Error parsing RSCP frame: %i\n", iProcessedBytes);
                // stop execution as the data received is not RSCP data
                bStopExecution = true;
                break;

            }
            else if(iProcessedBytes > 0) {
                // round up the processed bytes as iProcessedBytes does not include the zero padding bytes
                iProcessedBytes = ROUNDUP(iProcessedBytes, AES_BLOCK_SIZE);
                // store the IV value from encrypted buffer for next block decryption
                memcpy(ucDecryptionIV, &vecDynamicBuffer[0] + iProcessedBytes - AES_BLOCK_SIZE, AES_BLOCK_SIZE);
                // move the encrypted data behind the current frame data (if any received) to the front
                memcpy(&vecDynamicBuffer[0], &vecDynamicBuffer[0] + iProcessedBytes, vecDynamicBuffer.size() - iProcessedBytes);
                // decrement the total received bytes by the amount of processed bytes
                iReceivedBytes -= iProcessedBytes;
                // increment a counter that a valid frame was received and
                // continue parsing process in case a 2nd valid frame is in the buffer as well
                iReceivedRscpFrames++;
            }
            else {
                // iProcessedBytes is 0
                // not enough data of the next frame received, go back to receive mode if iReceivedRscpFrames == 0
                // or transmit mode if iReceivedRscpFrames > 0
                break;
            }
        }
    }
}

static void mainLoop(void)
{
    RscpProtocol protocol;
    bool bStopExecution = false;
    bool bWBRequest = false;
    while(!bStopExecution)
    {
        
        //--------------------------------------------------------------------------------------------------------------
        // RSCP Transmit Frame Block Data
        //--------------------------------------------------------------------------------------------------------------
        SRscpFrameBuffer frameBuffer;
        memset(&frameBuffer, 0, sizeof(frameBuffer));

        // create an RSCP frame with requests to some example data
        if(iAuthenticated == 1) {
            int sunrise = sunriseAt;
            if (e3dc_config.debug) printf("M1");
            if (e3dc_config.aWATTar)
            aWATTar(ch,w,e3dc_config,fBatt_SOC, sunrise);
//            test;
            if (e3dc_config.debug) printf("M2");
            float zulufttemp = -99;
            if (e3dc_config.WPWolf)
                zulufttemp = wolf[wpzl].wert;
            if (fBatt_SOC >= 0)
            mewp(w,wetter,fatemp,fcop,sunriseAt,sunsetAt,e3dc_config,fBatt_SOC,ireq_Heistab,zulufttemp);       // Ermitteln Wetterdaten
            if (e3dc_config.debug) printf("M3");

            if (strcmp(e3dc_config.heizung_ip,"0.0.0.0") >  0)
              iModbusTCP();
            if((frameBuffer.dataLength == 0)&&(e3dc_config.wallbox>=0)&&(bWBRequest))
            WBProcess(&frameBuffer);
            if (e3dc_config.debug) printf("M4");

            if(frameBuffer.dataLength == 0)
                 bWBRequest = true;
            else
                 bWBRequest = false;
if (e3dc_config.debug) printf("M5");

        if(frameBuffer.dataLength == 0)
            LoadDataProcess();
//            sleep(1);
if (e3dc_config.debug) printf("M6");

        }
        // check that frame data was created
        
        if(frameBuffer.dataLength == 0)
        {
            createRequestExample(&frameBuffer);
//            sleep(1);
        }
        // check that frame data was created

        if(frameBuffer.dataLength > 0)
        {
            // resize temporary encryption buffer to a multiple of AES_BLOCK_SIZE
            std::vector<uint8_t> encryptionBuffer;
            encryptionBuffer.resize(ROUNDUP(frameBuffer.dataLength, AES_BLOCK_SIZE));
            // zero padding for data above the desired length
            memset(&encryptionBuffer[0] + frameBuffer.dataLength, 0, encryptionBuffer.size() - frameBuffer.dataLength);
            // copy desired data length
            memcpy(&encryptionBuffer[0], frameBuffer.data, frameBuffer.dataLength);
            // set continues encryption IV
            aesEncrypter.SetIV(ucEncryptionIV, AES_BLOCK_SIZE);
            // start encryption from encryptionBuffer to encryptionBuffer, blocks = encryptionBuffer.size() / AES_BLOCK_SIZE
            aesEncrypter.Encrypt(&encryptionBuffer[0], &encryptionBuffer[0], encryptionBuffer.size() / AES_BLOCK_SIZE);
            // save new IV for next encryption block
            memcpy(ucEncryptionIV, &encryptionBuffer[0] + encryptionBuffer.size() - AES_BLOCK_SIZE, AES_BLOCK_SIZE);

            // send data on socket
            int iResult = SocketSendData(iSocket, &encryptionBuffer[0], encryptionBuffer.size());
            if(iResult < 0) {
                printf("Socket send error %i. errno %i\n", iResult, errno);
                bStopExecution = true;
            }
            else {
                // go into receive loop and wait for response
                sleep(1);
//                printf("%c[2J", 27 );
                printf("%c[H", 27 );
                printf("Request cyclic example data done %s %2ld:%2ld:%2ld",VERSION,tm_CONF_dt%(24*3600)/3600,tm_CONF_dt%3600/60,tm_CONF_dt%60);
                printf("%c[K\n", 27 );


//                if (e3dc_config.debug) printf ("start receiveLoop");
                receiveLoop(bStopExecution);
//                if (e3dc_config.debug) printf ("end receiveLoop");
            }
        }
        // free frame buffer memory
        protocol.destroyFrameData(&frameBuffer);

        // main loop sleep / cycle time before next request
    }
    printf("mainloop beendet");
}
int main(int argc, char *argv[])
{
/*
    static int ret = -1;
    while (true)
    {
        ret = iModbusTCP();
        sleep(1);
    }
 */
 for (int i=1; i < argc; i++)
 {
     // Ausgabe aller Parameter
     printf(" %i %s",i,argv[i]);
     // Auf speziellen Parameter prüfen
     if((strcmp(argv[i], "-config") == 0)||(strcmp(argv[i], "-conf") == 0)||(strcmp(argv[i], "-c") == 0))
     strcpy(e3dc_config.conffile, argv[i+1]);
 }
    

static int iEC = 0;
 time(&t);
    t_alt = t;

 struct tm * ptm;

    // endless application which re-connections to server on connection lost
    int res = system("pwd");
    if (GetConfig())
        while(iEC < 10)
        {
            iEC++; // Schleifenzähler erhöhen
            ptm = gmtime(&t);
            //      Berechne Sonnenaufgang-/untergang
            location = new SunriseCalc(e3dc_config.hoehe, e3dc_config.laenge, 0);
            location->date(1900+ptm->tm_year, 12,21,  0); // Wintersonnewende
            sunriseWSW = location->sunrise();
            location->date(1900+ptm->tm_year, ptm->tm_mon+1,ptm->tm_mday,  0);
            sunriseAt = location->sunrise();
            sunsetAt = location->sunset();
            int hh1 = sunsetAt / 60;
            int mm1 = sunsetAt % 60;
            int hh = sunriseAt / 60;
            int mm = sunriseAt % 60;
            sprintf(Log,"Start %s %s", strtok(asctime(ptm),"\n"),VERSION);
            WriteLog();
            // connect to server
            printf("Program Start Version:%s\n",VERSION);
            printf("Sonnenaufgang %i:%i %i:%i\n", hh, mm, hh1, mm1);
            GetConfig();
            printf("GetConfig done");
            if (e3dc_config.aWATTar)
            {
                aWATTar(ch,w,e3dc_config,fBatt_SOC, sunriseAt); // im Master nicht aufrufen
//            mewp(w,wetter,fatemp,fcop,sunriseAt,sunsetAt,e3dc_config,55.5,ireq_Heistab,5);
        }
        LoadDataProcess();
        printf("Connecting to server %s:%i\n", e3dc_config.server_ip, e3dc_config.server_port);
        iSocket = SocketConnect(e3dc_config.server_ip, e3dc_config.server_port);
        if(iSocket < 0) {
            printf("Connection failed\n");
            sleep(1);
            continue;
        }
        printf("Connected successfully\n");

        // reset authentication flag
        iAuthenticated = 0;

        // create AES key and set AES parameters
        {
            // initialize AES encryptor and decryptor IV
            memset(ucDecryptionIV, 0xff, AES_BLOCK_SIZE);
            memset(ucEncryptionIV, 0xff, AES_BLOCK_SIZE);

            // limit password length to AES_KEY_SIZE
            int64_t iPasswordLength = strlen(e3dc_config.aes_password);
            if(iPasswordLength > AES_KEY_SIZE)
                iPasswordLength = AES_KEY_SIZE;

            // copy up to 32 bytes of AES key password
            uint8_t ucAesKey[AES_KEY_SIZE];
            memset(ucAesKey, 0xff, AES_KEY_SIZE);
            memcpy(ucAesKey, e3dc_config.aes_password, iPasswordLength);

            // set encryptor and decryptor parameters
            aesDecrypter.SetParameters(AES_KEY_SIZE * 8, AES_BLOCK_SIZE * 8);
            aesEncrypter.SetParameters(AES_KEY_SIZE * 8, AES_BLOCK_SIZE * 8);
            aesDecrypter.StartDecryption(ucAesKey);
            aesEncrypter.StartEncryption(ucAesKey);
        }

        // enter the main transmit / receive loop
        mainLoop();
        printf("mainloop beendet %i\n",iEC);
        // close socket connection
        SocketClose(iSocket);
        iSocket = -1;
        sleep(10);
    }
    printf("Programm wirklich beendet");
    
    return 0;
}

// Delta E3DC-V1

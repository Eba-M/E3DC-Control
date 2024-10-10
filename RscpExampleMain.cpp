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
static float fPVtoday=0; // Erzeugung heute
static float fPVdirect=0;  // Direktverbrauch
static float fPVSoll=0; // Mindestertrag zur Abdeckung Tagesverbrauch und Speicherladung bis ladeende2
static float fDCDC = 0; // Strommenge mit rechnen
static int32_t iPower_PV, iPower_PV_E3DC;
static int32_t iAvalPower = 0;
static int32_t iMaxBattLade; // dynnamische maximale Ladeleistung der Batterie, abhängig vom SoC
static int32_t iPower_Bat;
static int32_t iPower_WP = 0; // Leistungsaufnahme WP
static float fPower_Bat;
static float fPower_Ext[7];
static int ireq_Heistab; // verfügbare Leistung
static uint8_t iPhases_WB;
static uint8_t iCyc_WB;
static int32_t iBattLoad;
static int iPowerBalance = 0;
static int iPowerHome = 0;
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
float fpeakshaveminsoc; // Wenn peakshave dann wird hier der MindestSoc im Tagesverlauf berechnet
// am Morgen, d.h. sonnenaufgang + zeitversatz aus unload mit ziel 50% bei Sonnenuntergang - Zeitversatz unload. Wird der wert unterschritten, wird das entladen gestoppt, ist er um 10% unterschritten wird aus dem Netz geladen, soweit der Netzbezug unter peakshave liegt.
static u_int8_t btasmota_ch1 = 0; // Anforderung LWWP 0 = aus, 1 = ein; 2 = Preis
static u_int8_t btasmota_ch2 = 0; // Anforderung LWWP/PV-Anhebung 1=ww, 2=preis, 4=überschuss
#define sizeweekhour 24*7*4
int weekhour    =  sizeweekhour+1;
int dayhour     =   weekhour+1;
static u_int32_t iWeekhour[sizeweekhour+10]; // Wochenstatistik
static u_int32_t iWeekhourWP[sizeweekhour+10]; // Wochenstatistik Wärmepumpe
static u_int32_t iDayStat[25*4*2+1]; // Tagesertragstatisik SOLL/IST Vergleich
static int DayStat = sizeof(iDayStat)/sizeof(u_int32_t)-1;
static int iMQTTAval = 0;
static int32_t iGridStat[31*24*4]; //15min Gridbezug Monat
static char fnameGrid[100];

static int Gridstat;


SunriseCalc * location;
std::vector<ch_s> ch;  //charge hour
// std::vector<watt_s> w1;

avl_array<uint16_t, uint16_t, std::uint16_t, 10, true> oek; // array mit 10 Einträgen


e3dc_config_t e3dc_config;

float fLadeende = e3dc_config.ladeende;
float fLadeende2 = e3dc_config.ladeende2;
float fLadeende3 = e3dc_config.unload;


FILE * pFile;
char Log[3000];

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
struct tm * ptm;
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
        strcpy(e3dc_config.mqtt3_ip, "0.0.0.0");
        strcpy(e3dc_config.WB_ip, "0.0.0.0");
        strcpy(e3dc_config.openWB_ip, "0.0.0.0");
        strcpy(e3dc_config.shelly0V10V_ip, "0.0.0.0");
        memset(e3dc_config.openweathermap,0,sizeof(e3dc_config.openweathermap));
        e3dc_config.wrsteuerung = 1; // 0 = aus, 1= aktiv, 2=debug ausgaben
        e3dc_config.stop = 0; // 1 = Programm beenden
        e3dc_config.test = 0; // 1 = Programm beenden
        e3dc_config.wallbox = -1;
        e3dc_config.openWB = false;
        e3dc_config.openmeteo = false;
        e3dc_config.shelly0V10V = false;
        e3dc_config.shelly0V10Vmin = 12;
        e3dc_config.shelly0V10Vmax = 47;
        e3dc_config.tasmota = false;
        e3dc_config.statistik = false;
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
        e3dc_config.wbminladestrom = 6;
        e3dc_config.unload = 100;
        e3dc_config.ht = 0;
        e3dc_config.htsat = false;
        e3dc_config.htsun = false;
        e3dc_config.hton = 0;
        e3dc_config.htoff = 24*3600; // in Sekunden
        e3dc_config.htsockel = 0;
        e3dc_config.peakshave = 0;
        e3dc_config.peakshavesoc = 0;
        e3dc_config.peakshaveuppersoc = 50;
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
        e3dc_config.ForcecastSoc = 2; // Faktor zur Berechnung des Ladebedarf
        e3dc_config.ForcecastConsumption = 1; // Faktor zur Berechnung des Verbrauchsbedarf
        e3dc_config.ForcecastReserve = 1; // Hinzurechnung Reserve in % zum Strombedarf

        e3dc_config.WPHeizlast = -1;
        e3dc_config.WPLeistung = -1;
        e3dc_config.WPHeizgrenze = -1;
        e3dc_config.WPmin = -1;
        e3dc_config.WPmax = -1;
        e3dc_config.WPPVon = -1;
        e3dc_config.WPPVoff = -100;
        e3dc_config.WPHK1 = 25;
        e3dc_config.WPHK1max = 29;
        e3dc_config.WPHK2on = -1;
        e3dc_config.WPHK2off = -1;
        e3dc_config.WPEHZ = 0;
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
//                if(sscanf(line, "%[^ \t=]%*[\t ]=%*[\t ]%[^\n]", var, value) == 2) {
                    if(sscanf(line, "%[^ \t=]%*[\t ]=%*[\t ]%[^ \n]", var, value) == 2) {
                    for (int i = 0; i < strlen(var); i++)
                    var[i] = tolower(var[i]);
                    if(strcmp(var, "server_ip") == 0)
                        strcpy(e3dc_config.server_ip, value);
                    else if(strcmp(var, "bwwp_ip") == 0)
                        strcpy(e3dc_config.BWWP_ip, value);
                    else if(strcmp(var, "shelly0v10v_ip") == 0)
                        strcpy(e3dc_config.shelly0V10V_ip, value);
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
                    else if(strcmp(var, "mqtt3_ip") == 0)
                        strcpy(e3dc_config.mqtt3_ip, value);
                    else if(strcmp(var, "wb_ip") == 0)
                        strcpy(e3dc_config.WB_ip, value);
                    else if(strcmp(var, "wb_topic") == 0)
                        strcpy(e3dc_config.WB_topic, value);
                    else if(strcmp(var, "forecast1") == 0)
                        strcpy(e3dc_config.Forecast[0], value);
                    else if(strcmp(var, "forecast2") == 0)
                        strcpy(e3dc_config.Forecast[1], value);
                    else if(strcmp(var, "forecast3") == 0)
                        strcpy(e3dc_config.Forecast[2], value);
                    else if(strcmp(var, "forecast4") == 0)
                        strcpy(e3dc_config.Forecast[3], value);
                    else if(strcmp(var, "forecastsoc") == 0)
                        e3dc_config.ForcecastSoc = atof(value);
                    else if(strcmp(var, "forecastconsumption") == 0)
                        e3dc_config.ForcecastConsumption = atof(value);
                    else if(strcmp(var, "forecastreserve") == 0)
                        e3dc_config.ForcecastReserve = atof(value);
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
                    else if(strcmp(var, "stop") == 0)
                        e3dc_config.stop = atoi(value);
                    else if(strcmp(var, "test") == 0)
                        e3dc_config.test = atoi(value);
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
                  else if((strcmp(var, "shelly0v10v") == 0))
                  {
                      if (strcmp(value, "false") == 0)
                          e3dc_config.shelly0V10V = false;
                      else
                          if (strcmp(value, "true") == 0)
                              e3dc_config.shelly0V10V = true;
                  }
                  else if((strcmp(var, "tasmota") == 0))
                  {
                      if (strcmp(value, "false") == 0)
                          e3dc_config.tasmota = false;
                      else
                          if (strcmp(value, "true") == 0)
                              e3dc_config.tasmota = true;
                  }
                  else if((strcmp(var, "wp") == 0)&&
                            (strcmp(value, "true") == 0))
                            e3dc_config.WP = true;
                  
                  else if((strcmp(var, "wpwolf") == 0)&&
                          (strcmp(value, "true") == 0))
                      e3dc_config.WPWolf = true;
                  else if((strcmp(var, "wpsperre") == 0)&&
                          (strcmp(value, "true") == 0))
                      e3dc_config.WPSperre = true;
                  else if((strcmp(var, "statistik") == 0)&&
                          (strcmp(value, "true") == 0))
                      e3dc_config.statistik = true;
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
                    else if(strcmp(var, "shelly0v10vmin") == 0)
                        e3dc_config.shelly0V10Vmin = atoi(value);
                    else if(strcmp(var, "shelly0v10vmax") == 0)
                        e3dc_config.shelly0V10Vmax = atoi(value);
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
                    else if(strcmp(var, "wphk1") == 0)
                        e3dc_config.WPHK1 = atof(value);
                    else if(strcmp(var, "wphk1max") == 0)
                        e3dc_config.WPHK1max = atof(value);
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
                    else if(strcmp(var, "wbminladestrom") == 0)
                        e3dc_config.wbminladestrom = atoi(value);
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
                    else if(strcmp(var, "peakshavesoc") == 0)
                        e3dc_config.peakshavesoc = atof(value); // in Watt
                    else if(strcmp(var, "peakshaveuppersoc") == 0)
                        e3dc_config.peakshaveuppersoc = atoi(value); // in Watt
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
        if (e3dc_config.aWATTar) {
// wenn awattar dann hton/htoff deaktivieren
            e3dc_config.htoff = e3dc_config.hton+1;
            e3dc_config.htsat = false;
            e3dc_config.htsun = false;        }
    }
    
    ptm = gmtime(&t);
    //      Berechne Sonnenaufgang-/untergang
    location = new SunriseCalc(e3dc_config.hoehe, e3dc_config.laenge, 0);
    location->date(1900+ptm->tm_year, 12,21,  0); // Wintersonnewende
    sunriseWSW = location->sunrise();
    location->date(1900+ptm->tm_year, ptm->tm_mon+1,ptm->tm_mday,  0);
    sunriseAt = location->sunrise();
    sunsetAt = location->sunset();


    if ((!fp)||not (fpread)) printf("Configurationsdatei %s nicht gefunden",CONF_FILE);
    return fpread;
}

int wpvl,wphl,wppw,wpswk,wpkst,wpkt,wpkt2,wpzl,wpalv;  //heizleistung und stromaufnahme wärmepumpe
time_t tLadezeitende,tLadezeitende1,tLadezeitende2,tLadezeitende3;  // dynamische Ladezeitberechnung aus dem Cosinus des lfd Tages. 23 Dez = Minimum, 23 Juni = Maximum
static int isocket;
long iLength,myiLength;
int iRegister;
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
char server_ip[16];
static bool brequest = false;

long iModbusTCP_Set(int reg,int val,int tac)
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
    Msend.Count = (val%256)*256; // Inhalt    int size = send.size();
    Msend.Count = Msend.Count + val/256; // Inhalt    int size = send.size();
    memcpy(&send[0],&Msend,send.size());
    if (e3dc_config.debug)
        printf("BSE%i",isocket);
/*
    if (isocket > 0)
        iLength = SocketRecvData(isocket,&receive[0],receive.size());
    if (iLength < 0)
    {
        SocketClose(isocket);
        isocket = -1;
    }
*/
    if (brequest)
        return iLength;

    if (isocket <= 0)
        {
            sprintf(server_ip,e3dc_config.heizung_ip);
            if (e3dc_config.debug)
                printf("BSC");

            isocket = SocketConnect(server_ip, 502);
            if (e3dc_config.debug)
                printf("ASC");

        }

    if (isocket > 0)
    {
        iLength = SocketSendData(isocket,&send[0],send.size());
        if (e3dc_config.debug)
            printf("BRC");
        
        iLength = SocketRecvData(isocket,&receive[0],receive.size());
        if (e3dc_config.debug)
            printf("ARC");
    }
    return iLength;
}

long iModbusTCP_Get(int reg,int val,int tac) //val anzahl register lesen
{
    send.resize(12);
    receive.resize(2048);

    Modbus_send Msend;
    Modbus_receive Mreceive;

    Msend.Tid = (tn*256+tac);
    tn++;
    Msend.Pid = 0;
    Msend.Mlen = 6*256;
    Msend.Dev = 1;  // Devadr. 1 oder 255
    Msend.Fcd = 3; // Funktioncode
    //            Msend.Fcd = 6; // Funktioncode
    Msend.Reg = reg*256;  // Adresse Register // Aussentemperatur
    Msend.Count = (val%256)*256; // Inhalt    int size = send.size();
    Msend.Count = Msend.Count + val/256; // Inhalt    int size = send.size();

    memcpy(&send[0],&Msend,send.size());
    if (e3dc_config.debug)
        printf("BRQ");
/*
    if (isocket > 0)
    {
        iLength = SocketRecvData(isocket,&receive[0],receive.size());
        if (iLength < 0)
        {
            SocketClose(isocket);
            isocket = -1;
        }
    }

    if (isocket<0)
    {
        sprintf(server_ip,e3dc_config.heizung_ip);
        if (e3dc_config.debug)
            printf("BSC");

        isocket = SocketConnect(server_ip, 502);
        if (e3dc_config.debug)
            printf("ASC");

    }
*/
    if (brequest) 
        return iLength;
if (isocket > 0)
 {
     iLength = SocketSendData(isocket,&send[0],send.size());
     if (e3dc_config.debug)
         printf("BRCV");
     iLength = SocketRecvData(isocket,&receive[0],receive.size());
         brequest = true;
 }
        if (e3dc_config.debug)
        printf("ARCV");
    return iLength;
}

static int dummy[100];
static int bWP = 0;
static int bHK2off = 0; // wenn > 0 wird der HK ausgeschaltet
static int bHK1off = 0;

int iModbusTCP()
{
// jede Minute wird die Temperatur abgefragt, innerhalb 10sec muss die Antwort da sein, ansonsten wird die Verbindug geschlossen.
    static time_t tlast = 0;
    time_t now;
    time(&now);
    static int  ret = 0;
    static time_t t_OeK;

    Modbus_send Msend;
    printf("ÖK%i",now-t_OeK);
    if ((now-t_OeK)>40)
    {
        if (t_OeK>0&&(now-t_OeK)>300)
            e3dc_config.stop = 100;
        t_OeK = now;
        if (isocket>0)
        {
            SocketClose(isocket);
            isocket = -1;
        }
    }
 
    if (brequest||(not brequest&&(now-tlast)>10)) // 10 Sekunden auf die Antwoert warten
    {
        if (isocket <= 0)
        {
            sprintf(server_ip,e3dc_config.heizung_ip);
            if (e3dc_config.debug)
                printf("BSC");
            
            isocket = SocketConnect(server_ip, 502);
            if (e3dc_config.debug)
                printf("ASC");
            iLength = 0;

        }
        if (isocket > 0&&not brequest&&(now-tlast)>10) // Nur alle 10sec Anfrage starten
        {
            tlast = now;
            send.resize(12);
            if (e3dc_config.debug)
                printf("RV");
            receive.resize(1024);
            if (e3dc_config.debug)
                printf("ARV");

// Heizkreise schalten in Abhängigkeit vom EVU und Status des jeweiligen heizkreis
// Wenn WP und oekofen aus sind, dann heizkreise ausschalten
// Wenn WP oder oekofen laufen Heizkreise einschalten
            
            if (temp[13] > 0 && not brequest && (now-t_OeK<20))
// Temperatur Puffer gesetzt?
            {
//  Kessel in Abhängigleit zu Aussentemperatur zu- und abschalten
//  Regelgröße ist die Zulufttemperatur der LWWP
//  daneben die aktuelle Temperatur vom Wetterportal
// und anstatt die Mitteltemperatur die aktuelle Temperatur zur Verifizierung
                float isttemp = wolf[wpzl].wert;
                if (wetter.size() > 0)
                    isttemp  = (isttemp  + wetter[0].temp)/2;
                if ((now - wolf[wpzl].t > 300)||wolf[wpzl].wert<-90)
                   isttemp = wetter[0].temp;

                if (isttemp<(e3dc_config.WPZWE)&&temp[17]==0)
//                if (temp[0]<(e3dc_config.WPZWE)*10&&temp[17]==0)
                {
                    iLength  = iModbusTCP_Set(101,1,101); //Heizkessel register 101
                    iLength  = iModbusTCP_Get(101,0,101); //Heizkessel
//                    brequest = true;
                }
                if (isttemp>(e3dc_config.WPZWE+1)&&temp[17]==1)
                {
                    iLength  = iModbusTCP_Set(101,0,101); //Heizkessel
                    iLength  = iModbusTCP_Get(101,0,101); //Heizkessel
//                    brequest = true;
                }


// Heizkreise schalten
                if (temp[1]==0&&
                    (
                        ((tasmota_status[0]==0||temp[14]>300)&&bHK1off==0)
                     ||
                        temp[17]>0)
                    )
// EVU Aus und Heizkreis Aus und fbh Anforderung ein -> einschalten
                {
                    iLength  = iModbusTCP_Set(11,1,11); //FBH? register 11
                    iLength  = iModbusTCP_Get(11,1,11); //FBH?
  //                  brequest = true;
                }
                if (temp[7]==0&&
                    (
                     (
                      (tasmota_status[0]==0||temp[14]>300)&&bHK2off==0
                      )
                     ||temp[17]>0)
                    )
//                if ((tasmota_status[0]==0||temp[17]>0)&&temp[7]==0&&bHK2off==0)
                    // EVU Aus und Heizkreis Aus und WW Anforderung aus -> einschalten
                {
                    iLength  = iModbusTCP_Set(31,1,31); //HZK? register 31
                    iLength  = iModbusTCP_Get(31,1,31); //HZK?
//                    brequest = true;
                }
                if (temp[1]==1&&
                    (
                     (tasmota_status[0]==1&&temp[17]==0&&temp[14]<300)
// wenn der Puffer > 30° läuft die FBH nach
                    ||
                     (bHK1off>0)
                     )
                    )
// EVU aus und Kessel aus ODER fbh Anforderung aus aber  Heizkreis aktiv -> HK ausschalten
                {
                    iLength  = iModbusTCP_Set(11,0,11); //FBH?
                    iLength  = iModbusTCP_Get(11,1,11); //FBH?
//                    brequest = true;
                }
                if (temp[7]==1&&
                    (
                     (tasmota_status[0]==1&&temp[17]==0&&temp[14]<300)
                    ||
                     (bHK2off>0)
                     )
                    )
// wenn Puffer > 30° läuft die HKZ nach
// EVU aus und Kessel aus ODER WW Anforderung + Heizkreis aktiv -> HK ausschalten
                {
                    iLength  = iModbusTCP_Set(31,0,31); //HZK?
                    iLength  = iModbusTCP_Get(31,1,31); //HZK?
//                    brequest = true;

                }
            }
            {
                if (not brequest||t-t_OeK>30)
                {
                    brequest = false;
                    if (e3dc_config.debug)
                        printf("BGE");
                    iLength = iModbusTCP_Get(2,105,2); // Alle Register auf einmal abfragen
                    if (e3dc_config.debug)
                        printf("AGE");
                    myiLength = iLength;
                }
            }
        }
        else
            if (brequest)
            {
                if (isocket > 0&&iLength < 0)
                    iLength = SocketRecvData(isocket,&receive[0],receive.size());
                if (iLength > 0)
                {
                    if (iLength > 100)
                        t_OeK = now;
                    int x2 = 9;
                    int x3 = 0;
                    x1 = receive[0]; // Startregister tan
                    iRegister = receive[0]; // Startregister tan
//                    x3 = x1;
                    for (x3=0;oekofen[x3] != x1&&x3<oekofen.size();x3++);
                    
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
//                    if (iLength <= 0)
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
FILE *wolf_fp;
static char buf[127];
static int WP_status = -1;
int status,vdstatus;
std::string sverdichterstatus;
static char path[4096];
wolf_s wo;
static int ALV = -1;


int wolfstatus()
{
    time_t now;
    time(&now);
    static time_t wolf_t;
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
                wpvl = wolf.size();
                wolf.push_back(wo);
                /*                wo.feld = "Sammlertemperatur";
                 wo.AK = "ST";
                 wolf.push_back(wo);
                 */                wo.feld = "Kesseltemperatur";
                wo.AK = "KT";
                wpkt = wolf.size();
                wolf.push_back(wo);
                wo.feld = "Kesseltemperatur 2";
                wo.AK = "KT2";
                wpkt2 = wolf.size();
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
                wpalv = wolf.size();
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
                wo.feld = "Verdichterfrequenz";
                wo.AK = "VFQ";
                wo.wert = -5;
                wpswk = wolf.size();
                wolf.push_back(wo);
                wo.feld = "Betriebsart Heizgerät";
                wo.AK = "BHG";
                wolf.push_back(wo);
                wo.feld = "Verdichterstatus";
                wo.AK = "VS";
                vdstatus = wolf.size();
                wolf.push_back(wo);
            }
            
            
            wolf_fp = NULL;
            sprintf(buf,"mosquitto_sub -h %s -t Wolf/+/#",e3dc_config.mqtt_ip);
//            if (e3dc_config.debug)
            printf("Wo0a");
            wolf_fp = popen(buf, "r");
            if (wolf_fp != NULL)
            {
                //            if (e3dc_config.debug)
                printf("Wo0b");
                int fd = fileno(wolf_fp);
                int flags = fcntl(fd, F_GETFL, 0);
                flags |= O_NONBLOCK;
                fcntl(fd, F_SETFL, flags);
                //            if (e3dc_config.debug)
            }
            printf("Wo0c");
            
            WP_status = 2;
            wolf_t = now;
        }
        if (now-wolf_t > 300)
        {
//            if (e3dc_config.debug)
                printf("Wo1b\n");
//            if (wolf_fp != NULL)
//                status = pclose(wolf_fp);
//            if (e3dc_config.debug)
            printf("Wo1c");
            wolf_fp = NULL;
            WP_status = 1;
        }
        if (e3dc_config.debug) printf("Wo1\n");
        if (wolf_fp != NULL)
        {
//            if (e3dc_config.debug)
                printf("Wo%i",now-wolf_t);
            
            while (fgets(path, 4096, wolf_fp) != NULL)
            {
                if (e3dc_config.debug) printf("Wo2");
                
                const cJSON *item = NULL;
                cJSON *wolf_json = cJSON_Parse(path);
                wolf_t = now;
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
                iPower_WP = wolf[wppw].wert*1000;
                if (iPower_WP==0&&ALV>0&&tasmota_status[0]==0) // keine EVU sperre
                    iPower_WP = 700;
                else
                if (iPower_WP>0&&ALV==0)
                    iPower_WP = 0;
            }
        }
    }
    if (e3dc_config.debug) printf("Wo1d\n");
    if (wolf_fp == NULL) WP_status = 1;
//    else            status = pclose(wolf_fp);
    if (e3dc_config.debug) printf("Wo1e\n");

    return WP_status;
        

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
    sprintf(buf,"E3DC-Control/Grid -m '%0.2f %0.2f %0.2f'",fPower_Grid,fBatt_SOC,float(iPower_Bat));
    MQTTsend(e3dc_config.mqtt2_ip,buf);

    if (e3dc_config.debug) printf("D4b\n");

}
    return 0;
};


FILE *mfp;

int tasmotastatus(int ch)
{
    //           Test zur Abfrage des Tesmota Relais
    if (strcmp(e3dc_config.mqtt_ip,"0.0.0.0")!=0)
    {
        
        char buf[1024];
        static int WP_status = -1;
        int status;
        char path[1024];
        if (WP_status < 2)
        {
            if (e3dc_config.debug) printf("W1\n");
            mfp == NULL;
            sprintf(buf,"mosquitto_sub -h %s -t stat/tasmota/POWER%i -W 1 -C 1",e3dc_config.mqtt_ip,ch);
            mfp = popen(buf, "r");
//            int fd = fileno(mfp);
//            int flags = fcntl(fd, F_GETFL, 0);
//            flags |= O_NONBLOCK;
//            fcntl(fd, F_SETFL, flags);
            WP_status = 2;
        }
        if (e3dc_config.debug) printf("W2\n");
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
        {
            status = pclose(mfp);
            mfp = NULL;
        }
        if (e3dc_config.debug) printf("W2a\n");
        return WP_status;
}
    return 0;
}
int MQTTE3DC(float f[3])
{
    //           Test zur Abfrage eines zweiten E3DC
    if (strcmp(e3dc_config.mqtt3_ip,"0.0.0.0")!=0)
    {
        
        char buf[127];
        static int WP_status = -1;
        int status;
        char path[1024];
        char var [4] [20];
        memset(path, 0x00, sizeof(path));

        if (WP_status < 2)
        {
            if (e3dc_config.debug) printf("W1");
            mfp == NULL;
            sprintf(buf,"mosquitto_sub -h %s -t E3DC-Control/Grid -W 1 -C 1",e3dc_config.mqtt3_ip);
            mfp = popen(buf, "r");
            //            int fd = fileno(mfp);
            //            int flags = fcntl(fd, F_GETFL, 0);
            //            flags |= O_NONBLOCK;
            //            fcntl(fd, F_SETFL, flags);
            WP_status = 2;
        }
        if (mfp != NULL)
            while (fgets(path, 1024, mfp) != NULL)
            {
                status = sscanf(path, "%s %s %s", var[0], var[1], var[2]);
                for (int x1=0;x1<status;x1++)
                    f[x1]=atof(var[x1]);
            }
        //        if (WP_status < 2)
        if (mfp != NULL)
            pclose(mfp);
        if (e3dc_config.debug) printf("W2");
        WP_status = 0;
        return status;

    }
    return -1;
}
int MQTTWB(float f[3])
{
    //           Test zur Abfrage eines zweiten E3DC
    if (strcmp(e3dc_config.WB_ip,"0.0.0.0")!=0)
    {
        
        char buf[127];
        static int WP_status = -1;
        int status;
        char path[1024];
        char var [4] [20];
        memset(path, 0x00, sizeof(path));

        if (WP_status < 2)
        {
            if (e3dc_config.debug) printf("W1");
            mfp == NULL;
            sprintf(buf,"mosquitto_sub -h %s -t %s -W 1 -C 1",e3dc_config.WB_ip,e3dc_config.WB_topic);
            mfp = popen(buf, "r");
            //            int fd = fileno(mfp);
            //            int flags = fcntl(fd, F_GETFL, 0);
            //            flags |= O_NONBLOCK;
            //            fcntl(fd, F_SETFL, flags);
            WP_status = 2;
        }
        if (mfp != NULL)
            while (fgets(path, 1024, mfp) != NULL)
            {
                status = sscanf(path, "%s %s %s", var[0], var[1], var[2]);
                for (int x1=0;x1<status;x1++)
                    f[x1]=atof(var[x1]);
            }
        //        if (WP_status < 2)
        if (mfp != NULL)
            pclose(mfp);
        if (e3dc_config.debug) printf("W2");
        WP_status = 0;
        return status;

    }
    return -1;
}

int tasmotaon(int ch)
{
    char buf[127];
    if (e3dc_config.tasmota)
    {
        sprintf(buf,"cmnd/tasmota/POWER%i -m 1",ch);
        MQTTsend(e3dc_config.mqtt_ip,buf);
        tasmota_status[ch-1] = 1;
    }    return 0;
}
int tasmotaoff(int ch)
{
    char buf[127];
    if (e3dc_config.tasmota)
    {
        sprintf(buf,"cmnd/tasmota/POWER%i -m 0",ch);
        MQTTsend(e3dc_config.mqtt_ip,buf);
        tasmota_status[ch-1] = 0;
    }    return 0;
}
static time_t shellytimer = t;

int shelly_get(){
    FILE *fp;
    char line[256];
    int WP_status,status;
    char path[1024];
    
    fp = NULL;
    if (shellytimer < t) 
    {
        sprintf(line,"curl -s -X GET 'http://%s/rpc/Light.GetStatus?id=0'",e3dc_config.shelly0V10V_ip);
        fp = popen(line, "r");
        //    system(line);
        const cJSON *item = NULL;
        if (fp != NULL)
            if (fgets(path, 1024, fp) != NULL)
                
            {
                
                std::string feld;
                cJSON *wolf_json = cJSON_Parse(path);
                feld = "brightness";
                char * c = &feld[0];
                item = cJSON_GetObjectItemCaseSensitive(wolf_json, c );
                
            }
        status = pclose(fp);
        if (item!=NULL)
            return(item->valueint);
        else
        {
            shellytimer = t+600;
            return(-1);
        }
    }
    return(-2);
}


int shelly(int ALV)
{
    char path[1024];
    char buf[127];
    FILE *fp;
    fp==NULL;
    if (e3dc_config.shelly0V10V&&shellytimer < t)
    {
        if (ALV>0)
            sprintf(buf,"curl -s -X POST -d '{""id"":0, ""on"":true, ""brightness"":%i}' ""http://%s/rpc/Light.Set?",ALV,e3dc_config.shelly0V10V_ip);
        else
            sprintf(buf,"curl -s -X POST -d '{""id"":0, ""on"":false, ""brightness"":%i}' ""http://%s/rpc/Light.Set?",ALV,e3dc_config.shelly0V10V_ip);
        fp = popen(buf, "r");
        
        if (fp != NULL)
            if (fgets(path, 1024, fp) != NULL)
            {}
    if (fp != NULL)
        pclose(fp);
    }
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
    static int PVon;
    time_t t_sav;
    static int idauer = 0;

    
    // Speicher SoC selbst berechnen
    // Bei Sonnenuntergang wird je ein Datensatz mit den höchsten und niedrigsten SoC-Werten erstellt.
    if (e3dc_config.debug) printf("D1%i2",t%60);
    if (e3dc_config.WPWolf)
        int ret = wolfstatus();
    time (&t);
    t_sav = t;
    
    if (e3dc_config.debug) printf("D1a\n");
    if (not e3dc_config.test)
    mqtt();
    
    if (e3dc_config.debug) printf("D2\n");
    
    
    fDCDC = fDCDC + fCurrent*(t-t_alt);
    if (e3dc_config.statistik)
    {
// die laufende Stunde wird in iWeekhour[sizeweekhour+1]
// der Tageswert wird in iWeekhour[sizeweekhour+2]
        static time_t myt_alt;
        if (e3dc_config.WP&&not e3dc_config.WPWolf&&wetter.size()>0)
//            if (e3dc_config.WP&&wetter.size()>0) // zum Testen
            iPower_WP = wetter[0].wpbedarf*e3dc_config.speichergroesse*400;
        if (iPower_WP < iPowerHome&&e3dc_config.WP==true) // nur wenn WP kleiner als hausverbrauch sonst O Verbrauch
        {
            iWeekhour[weekhour] = iWeekhour[weekhour] + (iPowerHome-iPower_WP)*(t-myt_alt);
            iWeekhour[dayhour] = iWeekhour[dayhour] + (iPowerHome-iPower_WP)*(t-myt_alt);
        } else
        if (not e3dc_config.WP)
        {
//            printf("weekhour %i %i %i ",weekhour, iWeekhour[weekhour], iPowerHome);
            iWeekhour[weekhour] = iWeekhour[weekhour] + (iPowerHome)*(t-myt_alt);
            iWeekhour[dayhour] = iWeekhour[dayhour] + (iPowerHome)*(t-myt_alt);
        }

        iWeekhourWP[weekhour] = iWeekhourWP[weekhour] + (iPower_WP)*(t-myt_alt);
        iWeekhourWP[dayhour] = iWeekhourWP[dayhour] + (iPower_WP)*(t-myt_alt);
        if (e3dc_config.tasmota)
        {
            if (tasmota_status[3] == 1) // Brauchwasserwärmepumpe
            {
                iWeekhourWP[weekhour] = iWeekhourWP[weekhour] + (450)*(t-myt_alt);
                iWeekhourWP[dayhour] = iWeekhourWP[dayhour] + (450)*(t-myt_alt);
            }
        }
        
        Gridstat = (ptm->tm_mday-1)*24*4;
        Gridstat = Gridstat+t%(24*3600)/900;
        iGridStat[Gridstat] = iGridStat[Gridstat] + fPower_Grid*(t-myt_alt);

        int x2 = (t%(24*4*900))/900;
        int x3 = t%900;
        float f2,f3;
        static wetter_s w_alt;
//        if (w.size() > 0)
        {
            iDayStat[DayStat] = iDayStat[DayStat]+ iPower_PV*(t-myt_alt);
            iDayStat[DayStat-2] = iDayStat[DayStat-2]+ iPower_PV*(t-myt_alt);
            f2 = iDayStat[DayStat-1] * 2 / 10.0;
            f3 = iDayStat[DayStat-2]/(e3dc_config.speichergroesse*10*3600)*2/10.0;

        }
        int schalter900 = 0;
        int schalter3600 = 0;

        if (((myt_alt%900)>(t%900)||schalter900))
//            &&w.size()>0) // Verbrauchwerte alle 15min erfassen
        {
            int x1 = (myt_alt%(24*7*4*900))/900;
            if (iWeekhourWP[weekhour] == 0)
                iWeekhourWP[weekhour] = 1;
            if (iWeekhour[x1]>0)
                iWeekhour[x1] = iWeekhour[x1]*.9 + iWeekhour[weekhour]*.1;
            else
                iWeekhour[x1] = iWeekhour[weekhour];
//            iWeekhour[weekhour] = (iPowerHome-iPower_WP)*(t-t_alt);
            iWeekhour[weekhour] = 0;

            if (iWeekhourWP[x1]>0)
                iWeekhourWP[x1] = iWeekhourWP[x1]*.9 + iWeekhourWP[weekhour]*.1;
            else
                iWeekhourWP[x1] = iWeekhourWP[weekhour];
            iWeekhourWP[weekhour] = iPower_WP*(t-t_alt);

            char fname[100];
            struct tm * ptm;
            myt_alt=myt_alt+24*3600;
            ptm = gmtime(&myt_alt);
            int nextday = ptm->tm_mday;
            myt_alt=myt_alt-24*3600;
            ptm = gmtime(&myt_alt);
            int day = ptm->tm_mday;

            FILE * pFile;
            sprintf(fname,"%s.dat","PVStat");
            pFile = fopen(fname,"wb");       // altes logfile löschen
            
            if (pFile!=NULL)
            {
                x1 = fwrite (iDayStat , sizeof(uint32_t), sizeof(iDayStat)/sizeof(uint32_t), pFile);
                fclose (pFile);
            }

            if ((myt_alt%(4*3600))>(t%(4*3600)))
                //              if ((t_alt%(900))>(t%(900)))  // alle 15 min wegschreiben
            {
// Gridwerte speichern, wenn Monatswechsel, speichern, Werte löschen, neue Monatdatei anlegen
                
                pFile = fopen(fnameGrid,"wb");       // datei zurückschreiben
                if (pFile!=NULL)
                {
                    x1 = fwrite (iGridStat , sizeof(int32_t), sizeof(iGridStat)/sizeof(int32_t), pFile);
                    fclose (pFile);
                }
                ptm = gmtime(&t);
                sprintf(fname,"Grid.%i.%i.dat",ptm->tm_year%100,ptm->tm_mon+1);
                if (strcmp(fname,fnameGrid)!=0)
                {
                    for (int x1=0;x1<sizeof(iGridStat)/sizeof(int32_t);x1++)
                        iGridStat[x1]=0;
                    strcpy(fnameGrid,fname);
                }


                if ((myt_alt%(24*3600))>(t%(24*3600)))
                {
                    iWeekhour[dayhour] = 0;
                    iWeekhourWP[dayhour] = 0;

                }
                pFile = fopen ("Weekhour.dat","wb");
                if (pFile!=NULL)
                {
                    fwrite (iWeekhour , sizeof(uint32_t), sizeof(iWeekhour)/sizeof(uint32_t), pFile);
                    fclose (pFile);
                }
                pFile = fopen ("WeekhourWP.dat","wb");
                if (pFile!=NULL)
                {
                    fwrite (iWeekhourWP , sizeof(uint32_t), sizeof(iWeekhourWP)/sizeof(uint32_t), pFile);
                    fclose (pFile);
                }
            }

//            if (w.size()>0)
            {

            // alle 15min wird diese Routine durchlaufen
            if (iDayStat[x2]>0)  // war schon vorbelegt
            {
                iDayStat[x2] = iDayStat[x2]*.9 + (w_alt.solar+0.005)*10; // 10%
                iDayStat[x2+96] = iDayStat[x2+96]*.9 + iDayStat[DayStat]/10; // 10%
            }
            else
            {
                iDayStat[x2] = (w_alt.solar+0.005)*100;
                iDayStat[x2+96] = iDayStat[DayStat];
            }
            iDayStat[DayStat-1] = iDayStat[DayStat-1] + (wetter[0].solar+0.005)*100;
//            iDayStat[DayStat-2] = iDayStat[DayStat-2] + iDayStat[DayStat];
            float f2 = 0;
            float f3 = 0;
            float f4 = 0;
            float f5 = 0;
            FILE *fp;

            if (iDayStat[x2]+iDayStat[x2+96]>0)
            {
 
                {
                    // Ausgabe Soll/Ist/ %  -15min, akt Soll Ist
                    f2 = iDayStat[x2]/100.0;
                    f3 = iDayStat[x2+96]/(e3dc_config.speichergroesse*10*3600);
                    f4 = (w_alt.hh%(24*3600))/3600.0;
                    f5 = iDayStat[DayStat]/(e3dc_config.speichergroesse*10*3600);
                }
                sprintf(fname,"Ertrag.%i.txt",day);
                fp = fopen(fname, "a");
                if(!fp)
                    fp = fopen(fname, "w");
                if(fp)
                {
                    fprintf(fp,"%0.2f %0.2f%% %0.2f%% %0.2f %0.2f%% %0.2f%% %0.2f %0.2f %0.2f\n",f4,f2,f3,f3/f2,w_alt.solar,f5,f5/w_alt.solar,iWeekhour[dayhour]/3600000.0,iWeekhourWP[dayhour]/3600000.0);
                    fclose(fp);
                }
            }
            if (wetter.size()>0&&w_alt.hh<wetter[0].hh)
                w_alt = wetter[0];
            else
                if (wetter.size()>0&&w_alt.hh<wetter[1].hh)
                    w_alt = wetter[1];

            iDayStat[DayStat] = iPower_PV*(t-myt_alt);
            if ((myt_alt%(24*3600))>(t%(24*3600))||schalter3600) // Tageswechsel
            {
                // Ausgabe Soll/Ist/ %  -15min, akt Soll Ist
                f2 = iDayStat[DayStat-1] * e3dc_config.speichergroesse/10000.0;
                f3 = iDayStat[DayStat-2]/3600.0/1000.0;
                f4 = 0;
                f5 = 0;
                for(x1=0;x1<96;x1++)
                {
                    f4 = f4 + iDayStat[x1];
                    f5 = f5 + iDayStat[x1+96];
                }
                f4 = f4 * e3dc_config.speichergroesse/10000.0;
                f5 = f5 /3600.0/1000.0;
                sprintf(fname,"Ertrag.%i.txt",day);
                fp = fopen(fname, "a");
                if(!fp)
                    fp = fopen(fname, "w");
                if(fp)
                {
                    
                    fprintf(fp,"\nDay %0.2f%kWh %0.2fkWh %0.2f \n",f2,f3,f3/f2);
                    fprintf(fp,"\nSummary %0.2f%kWh %0.2fkWh %0.2f \n",f4,f5,f5/f4);
                    iDayStat[DayStat-1]=0;
                    iDayStat[DayStat-2]=0;
                    fclose(fp);
                }
            }
// folgende Tagesdatei löschen
                sprintf(fname,"Ertrag.%i.txt",nextday);
                fp = fopen(fname, "w");
                if (fp!=NULL)
                    fclose(fp);

            }

        }
        myt_alt = t;
//        nächsten Eintrag suchen
        if (wetter.size()>0&&w_alt.hh==0)
            w_alt = wetter[0];
    }
    
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
    if (e3dc_config.debug) printf("D3\n");
    
    if (strcmp(e3dc_config.mqtt_ip,"0.0.0.0")!=0)
    {
        if (e3dc_config.debug) printf("D4\n");
        
        if (t-tasmotatime>10)
        {
            tasmota_status[ich_Tasmota] = tasmotastatus(ich_Tasmota+1);
            if (tasmota_status[ich_Tasmota]<2)
                ich_Tasmota++;
            if (ich_Tasmota > 3) ich_Tasmota = 0;
            tasmotatime = t;
            
        }
        if (e3dc_config.debug) printf("D4a\n");

        if (tasmota_status[3] > 1)
            tasmota_status[3] = tasmotastatus(4);
        
        // Steuerung BWWP über Tasmota Kanal4
        if (tasmota_status[3]>=1&&temp[13]>e3dc_config.BWWPaus*10)
        {
            tasmotaoff(4);
            /*            if (bHK1off & 2)
             bHK1off ^= 2;
             if (bHK2off & 2)
             bHK2off ^= 2;
             */
        } else
            if (tasmota_status[3]==0&&temp[13]>0&&temp[13]<e3dc_config.BWWPein*10)
            {
                tasmotaon(4);
                //            bHK1off |= 2;
                //            bHK2off |= 2;
            }

        // Steuerung LWWP über Tasmota Kanal2 Unterstützung WW Bereitung
        if (temp[2]>0)  // als indekation genutzt ob werte oekofen da
        {
            if 
                (temp[17]==1&&
                    (temp[19]==4||
                        (temp[18]>400)
                )
            ) // Kessel an + Leistungsbrand
            {
                // LWWP ausschalten wenn der Pelletskessel läuft
                // und keine Anforderungen anliegen
                if (btasmota_ch1 & 1)
                    btasmota_ch1 ^=1;
            } else
            {
                if (not e3dc_config.WPSperre&&bWP<=0&&btasmota_ch1==0) //bWP > 0 LWWP ausschalten
                {
                    btasmota_ch1 |=1;
                    if (bWP == 0)
                        wpofftime = t;
                }
            }
            if (e3dc_config.debug) printf("D4c\n");

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
            
            // Wie lange reicht der SoC? wird nur außerhalb des Kernwinter genutzt
            if (e3dc_config.debug) printf("D4d\n");

            int iWPHK1max = e3dc_config.WPHK1max*10;
            if (fatemp>8)
                iWPHK1max = iWPHK1max - (fatemp-8)*(e3dc_config.WPHK1max-e3dc_config.WPHK1);
            if (iWPHK1max<e3dc_config.WPHK1) iWPHK1max = e3dc_config.WPHK1;
            int m1 = t%(24*3600)/60;
            // In der Übergangszeit wird versucht die WP möglichst tagsüber laufen zu lassen
            // Nach Sonnenunterang nur soweit der Speicher zur Verfügung steht.
            
            if   ((sunsetAt-sunriseAt) > 10*60)  // 300% vom Soc = 60kWh
            {
                // FBH zwischen Sonnenaufgang+1h und nach 12h Laufzeit ausschalten
//                    bHK1off = 0;

                if 
                (
                    (m1 > (sunriseAt+60)||PVon>0)
                    &&
                    m1 < sunriseAt+720 && bHK1off&1
                )
                {
                    if (temp[2]>e3dc_config.WPHK1*10)
                    {
                        // HK1 wird eingeschaltet, zuvor wird die Solltemperatur zurückgesetzt
                        iLength  = iModbusTCP_Set(12,e3dc_config.WPHK1*10,12); //FBH? Solltemperatur
                        iLength  = iModbusTCP_Get(12,1,12); //FBH?
                        if (iLength > 0)
                            bHK1off ^= 1;
                    } else bHK1off ^= 1;
                }
                if 
                (
                    temp[17]==0   // Pellets muss aus sein
                    &&
                    (m1 < (sunriseAt+60)
                    ||
                     (m1 > (sunriseAt+720)) //FBH 10h Laufzeit fest
// AT zu hoch und Soll unter 24°
                    || (fatemp > e3dc_config.WPHeizgrenze&&iWPHK1max<240)
                     )
                )
                {
                        bHK1off |= 1;

                }
                // Steuerung der Temperatur der FBH
                // Wenn WP an und PV Überschuss
                static time_t HK1_t = 0;
  
                if (fPVtoday>fPVSoll||bHK2off==0) // Steuerung, wenn ausreichend PV-Überschuss zu erwarten ist
                {
                    
                    if (not bHK1off && temp[1]>0 && temp[6]>0 && temp[4]<(iWPHK1max)&& temp[5]<(iWPHK1max) &&
                        (temp[4]-temp[5])<=10 && (t-HK1_t)>60 && btasmota_ch1&&PVon>200)
                    {
                        if (temp[4]<(iWPHK1max-5))
                            iLength  = iModbusTCP_Set(12,temp[2]+5,12); //FBH? Solltemperatur
                        else
                            if (temp[4]<(iWPHK1max))
                                iLength  = iModbusTCP_Set(12,temp[2]+1,12); //FBH? Solltemperatur
                        if (iLength>0 )
                        iLength  = iModbusTCP_Get(12,1,12); //FBH?
                        if (iLength>0 )
                            HK1_t = t;
                        else 
                            HK1_t++;
                        brequest = true;

                    
                    }
                    // Überprüfen ob die Solltemperatur der FBH bei PV-Ertragsmangel heruntergesetzt werden muss
                    
                    if ( t-HK1_t>60 &&
                        (
                         // Wenn die Puffertemperatur > 5K als die FBH ist muss bei mangelnder Sonne die FBH nicht heruntergeschaltet werden.
                         
                         ((bHK1off ||m1 > (sunsetAt+60) || (PVon<(-iMinLade/4)&&temp[14]<(temp[4]+50)))
                         &&
                         (
                          (((temp[4]+10)>=temp[5] && temp[2]>(e3dc_config.WPHK1*10)&&PVon<-200)
                           )
                          ))
                          ||
                          (
                           (temp[4]+2>iWPHK1max)&&(temp[5]>iWPHK1max)
                           )
                          
                         
                         )
                        )
                    {
                        if ((temp[2]-1)> e3dc_config.WPHK1*10)
                        {
                            if (-iWPHK1max+temp[4]>5&&((temp[2]-5)>= e3dc_config.WPHK1*10))
                                iLength  = iModbusTCP_Set(12,temp[2]-5,12); //FBH? Solltemperatur
                            else
                                iLength  = iModbusTCP_Set(12,temp[2]-1,12);
                        } //FBH? Solltemperatur
                        else
                            iLength  = iModbusTCP_Set(12,e3dc_config.WPHK1*10,12); //FBH? Solltemperatur
                        if (iLength>0)
                            iLength  = iModbusTCP_Get(12,1,12); //FBH?
                        if (iLength>0 ) HK1_t = t;
                        else HK1_t++;
                        brequest = true;
                    }
                }
                    // HK2 zwischen WPHK2off und WPHK2on ausschalten
                    if  (bHK2off&1)
                        bHK2off ^= 1;
                    
                    float f1 = t%(24*3600)/3600.0;
                    if (temp[17]==0&&               // Wenn Pelletskessel aus
                        (e3dc_config.WPHK2off>e3dc_config.WPHK2on)
                        &&
                        (f1>e3dc_config.WPHK2off||f1<e3dc_config.WPHK2on)
                        )
                        bHK2off |= 1;
                    if (temp[17]==0&&           // Wenn Pelletskessel aus
                        (e3dc_config.WPHK2off<e3dc_config.WPHK2on)
                        &&
                        (f1>e3dc_config.WPHK2off&&f1<e3dc_config.WPHK2on)
                        )
                        bHK2off |= 1;
                    if  (
                         (m1>sunsetAt||m1<(sunriseAt+60))
                         &&
                         fBatt_SOC>=0
                         )
                    {
                        
                        
                        float f3 = 0;
                        int x1;
                        for (x1=0; x1<wetter.size()&&(wetter[x1].hourly+wetter[x1].wpbedarf)>wetter[x1].solar; x1++)
                        {
                            int hh1 = w[x1].hh%(24*3600)/3600;
                            if ((hh1<e3dc_config.WPHK2off)||(hh1>e3dc_config.WPHK2on))
                                f3 = f3 + w[x1].hourly;
                        }
                        /*                    if (fBatt_SOC>0&& fBatt_SOC< (f3+10))
                         bWP  |= 1; // LWWP ausschalten
                         if (x1 == 0&&bWP&1)
                         bWP ^=1;        // LWWP einschalten;
                         */                }
                    
                    
                    
                
            }
            if ((strcmp(e3dc_config.heizung_ip, "0.0.0.0") != 0))
            {
                float f4 = 0;
                if (wetter.size()>0)
                    f4 = (t%900)*(wetter[0].solar+0.005)*100.0/900.0;
                float f2 = (iDayStat[DayStat-1]+f4) * e3dc_config.speichergroesse/10000.0;
                float f3 = iDayStat[DayStat-2]/3600.0/1000.0;

                printf("%c[K\n", 27 );
                printf("T%0.4f %0.2f %0.2f h%1i f%1i %1i %1i %1i %1i i%3li %2li %i %0.2f %0.2f %0.2f",t%(24*3600)/3600.0,e3dc_config.WPHK2on,e3dc_config.WPHK2off, bHK2off,bHK1off, btasmota_ch1, bWP,tasmota_status[0],isocket,myiLength,iLength,iRegister,f2,f3,f3/f2);
            }

// Steuerung LWWP über shelly 0-10V
            
            
            if (ALV < 0) ALV = shelly_get();
            
            static time_t wp_t;
            if (t - wp_t > 59&&ALV>=0&&tasmota_status[0]==0)
            {
                if (ALV!=0)
                if (ALV > e3dc_config.shelly0V10Vmax
                    ||
                    ALV < e3dc_config.shelly0V10Vmin)
                {
                    if (ALV > e3dc_config.shelly0V10Vmax)
                        ALV = e3dc_config.shelly0V10Vmax;
                    else
                        if (ALV > 0)
                        ALV = e3dc_config.shelly0V10Vmin;
                    shelly(ALV);
                    wp_t = t;
                }

                    
                 

                
/*                if (ALV>0&&wolf[wpvl].wert>0&&t-wolf[wpvl].t<100)
                {
                    if (wolf[wpvl].wert>42)
                        shelly((ALV--)-1);
                    wp_t = t;
                }
*/

                // muss Leistung angehoben werden?
                int mm=t%(24*3600)/60;
                int wwmax = e3dc_config.WPHK1max-fatemp+20.0;
                if (wwmax < e3dc_config.WPHK1max)
                    wwmax = e3dc_config.WPHK1max;
                if (wwmax > (e3dc_config.WPHK1max+5))
                    wwmax = e3dc_config.WPHK1max+5;

                // Leistung nur erhöhen, wenn der Bufferstpeicher unterhalb der Grenze liegt
                if (
                    (
                     temp[14]<(e3dc_config.WPHK1max+2)*10
                     ||
                     (temp[14]<(e3dc_config.WPHK1max+3)*10&&wolf[wpvl].wert<(e3dc_config.WPHK1max+3.0)&&
                      wolf[wpvl].wert>0&&wolf[wpkt2].wert<(e3dc_config.WPHK1max+3.0))
                    )
                    &&
                    (
//  FBH nur hochschalten, wenn die VL Temp aus dem Puffer weniger als 3° über der FBH liegt.
                    (temp[1]>0&&temp[6]>0&&temp[4]>(temp[5]+10)&&temp[14]<(temp[4]+30))
                    ||
//  HK2 nur hochschalten, wenn die VL Temp aus dem Puffer weniger als 1° über der HK2 liegt.

                     (temp[7]>0&&temp[10]>(temp[11]+10)&&temp[14]<(temp[10]+10))
//                    ||
//                    (PVon>500&&fPVtoday>fPVSoll&&temp[14]<=(e3dc_config.WPHK1max+2)*10)
                    )
                   )
                {
                    ALV = shelly_get();
                    if (PVon>0)
                    {
                        if  (
                             mm>sunriseAt&&mm<sunsetAt&&
                             temp[14]<(e3dc_config.WPHK1max+5)*10
// nur wenn der Puufer Wärme aufnehmen kann
                            )
                        {
                            if (PVon < 5000)
                                ALV = ALV + PVon / 1000;
                            else
                                ALV = ALV + 4;
                        }
                        if (ALV>= e3dc_config.shelly0V10Vmax)
                            ALV = e3dc_config.shelly0V10Vmax-1;
                    }
                        if (ALV>0&&ALV<e3dc_config.shelly0V10Vmin)
                        {
                            ALV = e3dc_config.shelly0V10Vmin-1;
                        }

                        if (ALV>0&&ALV<e3dc_config.shelly0V10Vmax)
                            shelly((ALV++)+1);
                        else
                            if (ALV==0)
                            {
                                ALV = e3dc_config.shelly0V10Vmin;
                                shelly(ALV);
                            }
                                
                        wp_t = t;
                    
                }   else
                
                if 
                    (
                     (
                      (PVon < 0 || fPVtoday<fPVSoll) &&                      (
                       (temp[1]>0&&temp[6]>0&&temp[4]<temp[5])
                     ||
                       (temp[7]>0&&temp[10]<temp[11])
                      )
                     )
                    ||
                     (temp[14]>(e3dc_config.WPHK1max+3)*10&&wolf[wpvl].wert>(e3dc_config.WPHK1max+3.0)&&
                      wolf[wpvl].wert>0)
                    ||
                     (temp[14]>(e3dc_config.WPHK1max+4)*10)
                    ||
                     (temp[1]>0&&temp[6]>0&&iWPHK1max<temp[5])
//                    ||
//                    (wolf[wpvl].wert>45)
                    )
                {
                    ALV = shelly_get();
                    
                        if (mm>sunriseAt&&mm<sunsetAt&&PVon<0)
                        {
                            if (PVon > -5000)
                                ALV = ALV + PVon / 1000;
                            else
                                ALV = ALV - 4;
                        }
                        if (ALV>0&&ALV<= e3dc_config.shelly0V10Vmin)
                            ALV = e3dc_config.shelly0V10Vmin+1;
                        if (ALV>0)
                            shelly((ALV--)-1);
                        wp_t = t;
                    
                }
//                if (temp[14]>(e3dc_config.WPHK1max+6)*10)
                ALV = shelly_get();
                float ALV_Calc = (e3dc_config.WPHK1max+4)*10-temp[14];
// Solltemp bis <1° überschritten mit shelly0V10Vmin weiterköcheln;
//                ALV_Calc = 33;
                if (ALV_Calc < -20)
                {
                        ALV_Calc = 0;
                }
                else
                {
                    if (ALV_Calc>50&&ALV==0)
                        ALV_Calc = e3dc_config.shelly0V10Vmin;
/*                    else
                    if (ALV_Calc>20&&ALV_Calc<30&&PVon<0&&ALV==e3dc_config.shelly0V10Vmin)
                        ALV_Calc = 0;
*/
                    else
                    if (PVon > 0)
                    {
//  ALV_Calc ist die Temperaturdifferenz Ist/Soll in 1/10 Kelvin Spreizung = 5K
// abhängig von der AT wird das ramp-up gesteuert
                        float ramp = 10+(fatemp-8)*2.5;  //20° = 40, 8° = 10
                        if (ramp > 40) ramp = 40;
                        if (ramp < 10) ramp = 10;

                        ALV_Calc =
                        (
                         ((ramp-ALV_Calc)/ramp)*
                        (e3dc_config.shelly0V10Vmax-e3dc_config.shelly0V10Vmin)
                         );
                        ALV_Calc = e3dc_config.shelly0V10Vmax- ALV_Calc;
                        if (ALV_Calc >= e3dc_config.shelly0V10Vmax)
                            ALV_Calc = e3dc_config.shelly0V10Vmax;
                        if (ALV_Calc <= e3dc_config.shelly0V10Vmin)
                            ALV_Calc = e3dc_config.shelly0V10Vmin;
                    } else
                        ALV_Calc = ALV;
//                    ALV_Calc = 0;  //WP ausschalten
                }
                if  (
                        (ALV_Calc > ALV&&PVon>500) //MEHR GAS NUR BEI PV
                        ||
                        ALV_Calc<ALV
                        ||
                        ((ALV_Calc==0)||ALV_Calc==e3dc_config.shelly0V10Vmin) // LANGEN TAKT
                    )
                    {
                        if (ALV_Calc <= e3dc_config.shelly0V10Vmin&&ALV_Calc>0)
                            ALV_Calc = e3dc_config.shelly0V10Vmin;
                        if (ALV_Calc > ALV) 
                        {
                            if (ALV==0)
                                ALV=ALV_Calc;
                            else
                                ALV++;
                        }
                        else
                            ALV = ALV_Calc;
                        shelly(ALV);
                        wp_t = t;
                    }
                if ((t%60)==0)
                    ALV = shelly_get();
            }
// Bei Übertemperatur > 450 WP ausschalten
// Bei Untertemperatur < 300 WP einschalten

            if ((temp[13]) > 450||(temp[14]) > 450||bWP<0)
            {
                btasmota_ch1 = 0;
                bWP = -1;
            }
// bWP -1 Abschaltung beibehalten
            if (not e3dc_config.WPSperre&&bWP<=0&&btasmota_ch1==0&&(temp[14])<400&&not(bHK1off&&bHK2off))
            {
                btasmota_ch1  |=8;
                bWP = 0;
            }
// LWWP wieder bei zu geringer Temperatur einschalten bWP wird nur hier geschaltet


            // Auswertung Steuerung
            if (btasmota_ch1)
            {
                if (tasmota_status[0]==1)
                {
                    tasmotaoff(1);   // EVU = OFF Keine Sperre
                    ALV = e3dc_config.shelly0V10Vmin;
                    wpontime = t;
                    wpofftime = t;   //mindestlaufzeit
                }
            } else
                if (tasmota_status[0]==0)
                {
                    //                if (t-wpofftime > 60)   // 300sek. verzögerung vor der abschaltung
                    tasmotaon(1);   // EVU = ON  Sperre
                    if (ALV > 0)
                    {
                        ALV = 0;
                        shelly(ALV);
                    }
                    // Leistung ALV auf 0 ausschalten
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
    }
    if (e3dc_config.debug) printf("D4c\n");

    float f2 = 0;
    float f3 = 0;
    fPVdirect = 0;
    fPVtoday = -1;

    //            for (int x1=0; x1<wetter.size(); x1++) {
        for (int x1=0; x1<wetter.size(); x1++) // nur die nächsten 24h
        {
            int hh = (wetter[x1].hh%(24*3600));
            int x2 = (wetter[x1].hh%(24*7*4*900))/900;
            int x5 = (wetter[x1].hh%(24*4*900))/900;

            {
                        
                if (e3dc_config.statistik)
                {
                    int x4 = 0;
                    int x6 = 0;
                    float f4 = 0;
                    float f6 = 0;

                    for (int y1=-1;y1<2;y1++)
                    {
                        int x3 = x5 + y1;
                        for (int y2=0;y2<7;y2++)
                        {
                            x3=x3+4*24;
                            if (x3 < 0) x3 = x3 + 24*4*7;
                            if (x3 > 24*4*7) x3 = x3 -24*4*7;
                            if (iWeekhour[x3] > 0) 
                                x4++;
                            f4 = f4 + iWeekhour[x3]/36000.0/e3dc_config.speichergroesse;
                            if (iWeekhourWP[x3] > 0) 
                                x6++;
                            f6 = f6 + iWeekhourWP[x3]/36000.0/e3dc_config.speichergroesse;

                        }
                    }
                    if (x4 > 0)
                    {
                        if (x1==0)  // die ersten 15min anteilig berechnen
                        {
                            f3 = (f4/x4)/900*(900-t%900);
                        }
                        else
                        {
                            if (wetter[x1].solar>0&&hh<tLadezeitende2) // Ziel  bis Ladezeitende 2
                                f3 = f3 + f4 / x4;
                        }
                        wetter[x1].hourly = f4/x4;

                    }
                    if (x6 > 0)
                    {
                        if (x1==0)  // die ersten 15min anteilig berechnen
                        {                            
                            f3 = f3 + (f6/x6)/900*(900-t%900);
                        }
                        else
                        {
                            if (wetter[x1].solar>0&&hh<tLadezeitende2) // Ziel  bis Ladezeitende 2
                                f3 = f3 + f6 / x6;

                        }
                        wetter[x1].wpbedarf = (f6/x6);

                    }

                } else
                    if (wetter[x1].solar>0&&hh<tLadezeitende2) // Ziel  bis Ladezeitende 2
                        f3 = f3 + wetter[x1].hourly+wetter[x1].wpbedarf;
                if (x1==0)  // die ersten 15min anteilig berechnen
                    f2 = wetter[x1].solar/900*(900-t%900);
                else
                    if (wetter[x1].solar>0&&hh<tLadezeitende2) // Ziel  bis Ladezeitende 2
                    {     
                        int ze = wetter[x1].solar*e3dc_config.speichergroesse*40;
                        if (ze>e3dc_config.maximumLadeleistung)
// nur die nutzbare Solarleistung berücksichtigen
                            f2 = f2 + e3dc_config.maximumLadeleistung/e3dc_config.speichergroesse/40;
                        else
                            f2 = f2 + wetter[x1].solar;
                    }
            }
            if (hh>(21*3600)&&fPVtoday<=0.0&&f2>0.0)
            {
                fPVtoday=f2;
                fPVdirect=f3;
                fPVSoll = fPVdirect*e3dc_config.ForcecastConsumption+(-fBatt_SOC+fLadeende2)*e3dc_config.ForcecastSoc+e3dc_config.ForcecastReserve;
//                break;
            }
        }

    
    
    

    printf("%c[K\n", 27 );
    tm *ts;
    ts = gmtime(&tE3DC);
    hh = t % (24*3600)/3600;
    mm = t % (3600)/60;
    ss = t % (60);
    static float fstrompreis=10000;
    if ((t % (24*3600))<(t_alt%(24*3600)))  // neuer tag
    {
// Erstellen Statistik, Eintrag Logfile
        CheckConfig(); //Lesen Parameter aus e3dc.config.txt
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
//    t_alt = t;
    t = t % (24*3600);
    
    static time_t t_config = tE3DC;
    if ((tE3DC-t_config) > 5)
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
    fLadeende = e3dc_config.ladeende;
    fLadeende2 = e3dc_config.ladeende2;
    if (e3dc_config.unload<0)
        fLadeende3 = 100;      // wenn über Nacht entladen wird, dann nicht am Tag
    else
        fLadeende3 = e3dc_config.unload;


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

// Austesten neue Modelle zur Ermittlung des Ladebedarfs
        
        
        
        
        if (e3dc_config.debug) printf("D5\n");

    int ret = 0; // Steuerung Netzladen = 2, Entladen = 1
    if (e3dc_config.aWATTar)
        ret =  CheckaWATTar(w,wetter,sunriseAt,sunsetAt,sunriseWSW,fBatt_SOC,fht,e3dc_config.Avhourly,e3dc_config.AWDiff,e3dc_config.AWAufschlag,e3dc_config.maximumLadeleistung/e3dc_config.speichergroesse/10,0,fstrompreis,e3dc_config.AWReserve); // Ladeleistung in %

        if (e3dc_config.debug) printf("D6 %i ",ret);

        
        switch (e3dc_config.AWtest) // Testfunktion
        {
            case 2:
            if (fBatt_SOC > fht-1) break;  // do nothink
            case 3: ret = 2; break;
            case 1: ret = 1;
            case 4: ret = 0;
        }
        if (e3dc_config.debug) printf("\nD7 %i ",ret);

        if  ((ret == 2)&&(e3dc_config.aWATTar==1)&&
             (iPower_PV < e3dc_config.maximumLadeleistung||iPower_Bat<e3dc_config.maximumLadeleistung/2||fPower_Grid>e3dc_config.maximumLadeleistung/2))
        {
              iE3DC_Req_Load = e3dc_config.maximumLadeleistung*1.9;
//            printf("Netzladen an");
//            iE3DC_Req_Load = e3dc_config.maximumLadeleistung*0.8;
            if (e3dc_config.debug) printf("RQ7 %i ",ret);
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
        if ((iPower_Bat == 0)&&(fPower_Grid>100)&&fBatt_SOC>0.5
            &&(e3dc_config.peakshave==0)  //peakshave?
            )
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
                    printf("\nEntladen stoppen ");
                    iLMStatus = -7;
//                    return 0;
                }
        }

        else          // Entladen ok
        if ((fPower_Grid > 100)&&(iPower_Bat < 200)&&fBatt_SOC>0.5&&idauer==0)  // es wird Strom bezogen Entladesperre solange aufheben
        {
                iE3DC_Req_Load = fPower_Grid*-1;  //Automatik anstossen
                   if (iE3DC_Req_Load < e3dc_config.maximumLadeleistung*-1)  //Auf maximumLadeleistung begrenzen
                    iE3DC_Req_Load = e3dc_config.maximumLadeleistung*-1;  //Automatik anstossen
//                 printf("Entladen starten ");
            if (e3dc_config.AWtest == 1||e3dc_config.AWtest == 4){
                printf("\nEntladen %i",iE3DC_Req_Load);
                iLMStatus = -7;
//                return 0;
            } else
                iLMStatus = 7;
        }


        
        
}

    
    // HT Endeladeleistung freigeben  ENDE
    
    if (e3dc_config.debug) printf("D8 %i ",iLMStatus);

    
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
    
// PVon dynamischer Berechnung unter Ausnutzung des Rest SoC am Morgen
        if (t<tLadezeitende3&&(t/60)>sunriseAt&&fPVtoday>fPVSoll&&fBatt_SOC>5&&iPower_PV>100)
        {
            float fdynPower = (fBatt_SOC-5)*e3dc_config.speichergroesse;
            PVon = PVon*.9 + fdynPower+((iPower_PV - iPowerHome - fPower_Grid))/10;
        }
        else
            PVon = PVon*.9 + ((-iMinLade +  iPower_PV - iPowerHome- fPower_Grid))/10;


    if (e3dc_config.WP&&fcop>0)
    {
        printf(" %.2f %.2f",fspreis/fcop,fcop);
        // LWWP  auf PV Anhebung schalten
        
        
        //        PVon = PVon*.9 + ((-sqrt(iMinLade*iBattLoad) + iPower_Bat - fPower_Grid))/10;
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
    static float soc_alt = 100;
    if (t < tLadezeitende3&&fBatt_SOC>fLadeende3&&e3dc_config.unload >= 0) 
    {
        //            tLadezeitende = tLadezeitende3;
        // Vor Regelbeginn. Ist der SoC > fLadeende3 wird entladen
        // wenn die Abweichung vom SoC < 0.3% ist wird als Ziel der aktuelle SoC genommen
        // damit wird ein Wechsel von Laden/Endladen am Ende der Periode verhindert
        if ((fBatt_SOC-fLadeende3) > 0)
        {    // Raum lassen zum atmen
            if ((fBatt_SOC-fLadeende3) < 0.6)
            {
                fLadeende = fBatt_SOC;
                
                // Es wird bis tLadezeitende3 auf fLadeende3 entladen
                if (soc_alt<fBatt_SOC&&(fBatt_SOC-fLadeende3)<0.3 )
                    fLadeende = fLadeende3+0.3;
                else
                    soc_alt = fBatt_SOC;
            }
            else fLadeende = fLadeende3;
            tLadezeitende = tLadezeitende3;
        }
            if (soc_alt > fBatt_SOC)
                soc_alt = fBatt_SOC;
        
    }
 else
     if ((t >= tLadezeitende)&&(fBatt_SOC>=fLadeende)) {
         tLadezeitende = tLadezeitende2;
         if (fLadeende < fLadeende2)
         fLadeende = fLadeende2;
     }

    if (t < tLadezeitende2)
    // Berechnen der linearen Ladeleistung bis tLadezeitende2 = Sommerladeende
    {
        iMinLade2 = ((fLadeende2 - fBatt_SOC)*e3dc_config.speichergroesse*10*3600)/(tLadezeitende2-t);
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
    // weniger als 2h vor Ladeende2 Angleichung der Ladeleistung an die nächste Ladeperiode
    // Im Winter verringert sich der zeitliche Abstand zwischen RE und LE

    // weniger als 15 vor Ladeende2 bzw. LE oder 1h vor RE
              if ((tLadezeitende-t) < 900||(tLadezeitende2-t) < 900) // statt 7200
              {
                  if (iMinLade2 > iFc)
                  {
                      iFc = (iFc + iMinLade2)/2;
                      if (iFc < iMinLade2/2)
                          iFc = iMinLade2/2;
                      if (iFc<0) iFc = 0;
                  }

                  if ((tLadezeitende1+tLadezeitende2)/2-t < 0 && iFc < 0)
                      iFc = 0;
              }
    time_t t_day;
    t_day = t_alt%(24*3600);
            if (
                  (t_alt%(24*3600) <=(tLadezeitende3-900)&&t>=(tLadezeitende3-900)) // Wechsel Ladezeitzone
                  ||
                  (t_alt%(24*3600) <=tLadezeitende3&&t>=tLadezeitende3) // Wechsel Ladezeitzone
                  ||
                  (t_alt%(24*3600) <=(tLadezeitende1-900)&&t>=(tLadezeitende1-900))
                  ||
                  (t_alt%(24*3600) <=tLadezeitende1&&t>=tLadezeitende1)
                  )
              {
                  fAvBatterie = iFc*e3dc_config.powerfaktor;
                  fAvBatterie900 = iFc*e3dc_config.powerfaktor;
                  if (fAvBatterie900 <= 0)
                      fAvBatterie900 = iMinLade2;
              }
              if (
                  (t_alt%(24*3600) <=tLadezeitende2&&t>=tLadezeitende2) // Wechsel Ladezeitzone
                  )
              {
                  fAvBatterie = 0;
                  fAvBatterie900 = 0;
              }

// Wenn der noch zu erwartende Solarertrag kleiner ist als der Speicherbedarf und der Stromverbrauch
// multipliziert mit einem Unsicherheitsfaktor von 2, dann wird das Laden freigegeben.
    if (fPVtoday>0&&(fPVtoday<fPVSoll)&&t<tLadezeitende)
    {
        if (fBatt_SOC<fLadeende-1||(t<tLadezeitende3&&fBatt_SOC<e3dc_config.ladeende-1))
        {
            if (iMinLade<iMinLade2) iMinLade = iMinLade2*2;
            if (iMinLade<(e3dc_config.maximumLadeleistung*.5))
            {
                iMinLade = e3dc_config.maximumLadeleistung*.5;
            }
//            if (iFc < iMinLade*e3dc_config.powerfaktor)
//                iFc = iMinLade*e3dc_config.powerfaktor;
            if (iFc < iMinLade*2)
                iFc = iMinLade*2;

        } else {
            if (fBatt_SOC<fLadeende||(t<tLadezeitende3&&fBatt_SOC<e3dc_config.ladeende))
            {
                if (iMinLade < 0)
                {
                    iMinLade = 0;
                    iFc = 0;
                }
            }
            //            iBattLoad = e3dc_config.maximumLadeleistung*.5;
        }
    }

    //  wenn unload < 0 dann wird ab sonnenuntergang bis sonnenaufgang - unload auf 0% entladem
    static float average = 0;
    float f[3];
    f[0] = 0;
    f[1] = 0;
    f[2] = 0;

    if (e3dc_config.unload<0)
    {
        int itime = (sunsetAt*60+e3dc_config.unload*60);  // Beginn verzögern min = 40sek
        idauer = 0;
        if (not e3dc_config.test)
        {
            if (MQTTE3DC(f)>0)
                iMQTTAval = f[0];
                //        iMQTTAval = iMQTTAval*.80 + MQTTE3DC()*.20;
                }
        else
            iMQTTAval = 4000;
        if (t>itime)
        {
            idauer = 24*3600-t + sunriseAt*60 - e3dc_config.unload*60;
        }
         int itime2 = (sunriseAt*60-e3dc_config.unload*60);  // Beginn verzögern min = 40sek
        if (t<itime2)
            idauer = sunriseAt*60 - t -e3dc_config.unload*60;
        if (idauer == 0) 
        {
// Tagesbetrieb
            fpeakshaveminsoc = (sunsetAt-sunriseAt)*60+2*e3dc_config.unload*60; //regeldauer
            fpeakshaveminsoc = (t-itime2-2*3600)/(fpeakshaveminsoc-2*3600);      //% restregeldauer
            // Beginn um 3h nach hinten verschieben
            fpeakshaveminsoc = (e3dc_config.peakshaveuppersoc-e3dc_config.peakshavesoc)*fpeakshaveminsoc+e3dc_config.peakshavesoc;
        } else // Nachtbetrieb
        {
            fpeakshaveminsoc = (24*60-sunsetAt+sunriseAt)*60-2*e3dc_config.unload*60; //regeldauer Nacht
            fpeakshaveminsoc = (idauer)/fpeakshaveminsoc;      //% restregeldauer
            fpeakshaveminsoc = (e3dc_config.peakshaveuppersoc-e3dc_config.peakshavesoc)*fpeakshaveminsoc+e3dc_config.peakshavesoc;

        }
        // muss noch geregelt werden, für Master/Slave unterschiedliche Ausgangssituation
// innerhalb des Nachtbetriebs und tagsüber wenn < minsoc und iPowerhome > peakshave
// dann wird nur auf e3dc_config.peakshave geregelt (Master)
        if (e3dc_config.test) // testparameter einstellen
        {
//            idauer = 100;
//            fpeakshaveminsoc = 10;
            iMQTTAval = 4000;
            fBatt_SOC = 29;
            iPowerHome = 1000;
            fPower_Grid = 0;
            
        }

        static int MQTTAval;
        float f4 = (t_alt%(900))+1; //(0..899) daher +1
        float fcurrentGrid = iGridStat[Gridstat]/f4;
        float fsollGrid = (e3dc_config.peakshave*900-iGridStat[Gridstat])/(900-f4+1);
        static int iFc0 = 0;
        int iFc1 = iFc;  // angefordertete Leistung
        if (
            (idauer > 0
             ||
             fBatt_SOC<e3dc_config.peakshavesoc
             ||
             fBatt_SOC<fpeakshaveminsoc
             ||
             (fcurrentGrid>e3dc_config.peakshave-200&&(strcmp(e3dc_config.mqtt3_ip,"0.0.0.0")==0)) // nur Master
             ||
             (
              ((iMinLade>fAvBatterie900&&iFc>fAvBatterie900*1.1)||fBatt_SOC<e3dc_config.ladeschwelle)
              &&strcmp(e3dc_config.mqtt3_ip,"0.0.0.0")!=0
              )
            )

//        if (((idauer > 0||fBatt_SOC<fpeakshaveminsoc)&&fPower_Grid>100)
            &&
            (
             (
              (strcmp(e3dc_config.mqtt2_ip,"0.0.0.0")!=0)
              )
            ||
             (
              (strcmp(e3dc_config.mqtt3_ip,"0.0.0.0")!=0)
              )
            )
        )
        {
            if (idauer>0&&fBatt_SOC-e3dc_config.peakshavesoc>0)
//                if (idauer>0&&fBatt_SOC-fpeakshaveminsoc>0)
            {
                iFc = (fBatt_SOC-e3dc_config.peakshavesoc)*e3dc_config.speichergroesse*10*3600;
                iFc = iFc / (idauer+600) *-1;
// 10 Minuten über Dauer hinaus berechnen um Extremwerte zu vermeiden
                if (fBatt_SOC-fpeakshaveminsoc<0) // unter dyn. peakshave soc? Leistung halbieren
                    iFc = iFc / 2;
//                iFc = iFc + iPower_PV_E3DC - fPower_Ext[2] - fPower_Ext[3];
            }
            else 
//                if( fPower_Grid < -100)
//                    iFc = fPower_Grid*-1;
//                else
                    iFc = 0;
            int iFc3 = iFc;
            if (e3dc_config.peakshave>0&&(strcmp(e3dc_config.mqtt2_ip,"0.0.0.0")!=0))
// Master E3DC sendet die grid-werte
            {
// Freilauf bei PV Ertrag + Durchschnitssverbrauch kleiner verfügbare Leistung
                if ((fAvBatterie900-200>iFc||fAvBatterie-100>iFc||fPower_Grid<-100||iPower_PV>iPowerHome)
                    &&iPower_PV_E3DC>100&&fpeakshaveminsoc-5 < fBatt_SOC&&fBatt_SOC>e3dc_config.peakshavesoc)
                {
//                    iFc = 0;
                    idauer = -1;
                }
                else
                {
// peakshaveing notwendig??
// Es wird auf das exakte 15min Intervall geregelt
                    if (fcurrentGrid>e3dc_config.peakshave)
                    {
// Peakshave Grenze erreich Entladeleistung erhöhen
//                        if (fsollGrid < e3dc_config.peakshave&&f4>800)
                        if (fsollGrid-100 < fPower_Grid&&f4>800)
                            iFc = iBattLoad - fcurrentGrid + fsollGrid - fPower_Grid + fsollGrid - 200;
                        else
                            if (fcurrentGrid>e3dc_config.peakshave-100)
//                                if (fcurrentGrid>e3dc_config.peakshave&&fsollGrid<fPower_Grid)
                                iFc = iBattLoad - fcurrentGrid + fsollGrid - fPower_Grid + fsollGrid;
                    }
                    else
                        // Besteht noch PV Überschuss?
                    {
                        // Nachladen aus dem Netz bis zur peakshaving grenze da fpeakshaveminsoc 5% unter Soll
                        if (fpeakshaveminsoc-5 > fBatt_SOC&&(fPower_Grid)<e3dc_config.peakshave-500)
                            //                        iFc = iBattLoad - fPower_Grid*3;
                            iFc =  iBattLoad -fPower_Grid+e3dc_config.peakshave-500;
                        else
                            if (fpeakshaveminsoc-2 > fBatt_SOC)
                                iFc = 0;

//                        iFc3 = iFc;

                        
                        if (iFc<0)
                        {
                            if (iPowerHome<iFc*-1)
                                iFc = iPowerHome*-1;
                            if (fPower_Grid < -100) // es wird eingspeist
                                iFc = 0;
                            else
                                if (fPower_Grid<500) // bis Netzbezug 500W runterregeln
                                    iFc = iBattLoad + fPower_Grid/2;
                                else
                                    if (iFc > iBattLoad)
                                        iFc = iBattLoad/2;
                        }

                        // Einspeisung
                        if (iFc == 0)
                        {
                            if (fPower_Grid<-500)
                            {
                                iFc = iBattLoad - fPower_Grid;
                                if (iFc < 0) iFc = 0;
                            }
                            else
                            {
                                if (fPower_Grid<-200)
                                    iFc = iBattLoad;
//                                else
//                                    iFc = iBattLoad*.7;
                                 // Strombezug aus dem Netz
//                                 iFc =  -fPower_Grid;
                            }
                        }
                    }
                    
                    if (iFc > e3dc_config.maximumLadeleistung-500)
                        iFc = e3dc_config.maximumLadeleistung-500;
                    
                }
            }
            if (e3dc_config.peakshave>0&&(strcmp(e3dc_config.mqtt3_ip,"0.0.0.0")!=0))
// Slave E3DC
            {
                iFc3 = iFc;
                if (f[2]>100) iFc = 0; //Master wird geladen
                
                if (iMQTTAval<600&&iMQTTAval>100)  // Leistung sanft zusteuern bei Netzbezug
                    iFc = (iFc/500.0)*iMQTTAval;
                else
                    if (iMQTTAval <= 100) iFc = 0;

                if (iFc3<0) // Es kann ausgespeichert werden
                {
                    if (f[2] < -1000&&f[2] >-2000)
// Batterieentladen des Masters zwischen -1000 und -2000W
                    {
                        if ((f[2]+1000)/1000.0*-iFc3 < iFc)
                            iFc = (f[2]+1000)/1000.0*-iFc3;
                    }
                    else
                        if ( f[2]<-2000)
                            iFc = iFc3;
                    
                    if (iMaxBattLade<iFc3)
                        iFc = iFc3;
                }
                
// Aus den Werten beider Systemen wird eine Leistungsbilanz gebildet
// Ist diese < 0 wird mehr Verbraucht, ist diese > 0 wird mehr erzeugt
// Enstsprechend wird der Slave geladen oder entladen
                if (iFc == 0)
// keine Anforderung über Gridbezug
                {
                    iFc3 = f[2];

                    int iBilanz = (f[0]*-1+f[2]+iPower_Bat);
                    if (abs(iBilanz)>1000)
                    {
                        if ((f[1]>fBatt_SOC+2&&iBilanz>1000)||(f[1]<fBatt_SOC&&iBilanz<-1000))
                            iFc = iBilanz *.7;
                        else
                            iFc = iBilanz *.6;
                        printf("%c[K\n", 27 );
                        printf("iBilanz %i %i %2i%% %2.0f%%",iBilanz,iFc, (iFc0*100/iBilanz),(f[2]*100/float(iFc0)));
                    }
                    if (fBatt_SOC-e3dc_config.peakshavesoc<0&&iFc<0)
                        iFc = 0;
                    if (f[0]<-500&&f[2]>=0)
                        iFc = iBilanz - f[2];
                    if ((f2) > 1000&&iFc < 0)  // Wenn der Master lädt, wird nicht endladen
                        iFc = 0;
                    
                    
                    if (f[1]<fBatt_SOC&&f[2]<-500) // Master entlädt
                    {
                        if (iFc < f[2])    // Grundleistung größer Leistung Master
                            iFc = f[2];
                    }
//angeforderde Ladeleistung
//                    if (iFc1<iFc)
//                        iFc = iMinLade*e3dc_config.powerfaktor;
                }

//                iFc3 = iFc;
                else
                {
                    if (iFc < 0)  // Angleichen Slave in der Ausspeicherungsleistung an den Master
                    {
                        iFc3 = iFc;
                        int iBilanz = iBattLoad + f[2];
                        if (f[2]<0) // nur wenn der Master auch ausspeichert
                        {
                            if (fBatt_SOC > f[1]&&iFc>(iBilanz*.7))
                                iFc = iBilanz*.7;
                            else
                                if (iFc>(iBilanz*.6))
                                    iFc = iBilanz*.6;
                        }
                        else
                            iFc = 0;
                        
                        printf("%c[K\n", 27 );
                        printf("iBilanz %i %i %i %2i%% %2.0f%% %2.0f%%",iFc3,iBilanz,iFc, (iFc0*100/iBilanz),(f[2]*100/float(iFc0)),f[1]);
                        
                    }
                }
                
                if (iMQTTAval>e3dc_config.peakshave-200)
// peakshave max. verdoppelung von iFc
                {
                        iFc = iBattLoad - (iMQTTAval - e3dc_config.peakshave+200)*2;
                };
// von der aktuellen Bezugsleistung starten
                
//                if (iBattLoad > iPower_Bat) iBattLoad = iPower_Bat;

                
                if (MQTTAval < e3dc_config.maximumLadeleistung*-1)
                    MQTTAval = e3dc_config.maximumLadeleistung*-1;

// Nachladen aus dem Netz bis zur peakshaving grenze da fpeakshaveminsoc 5% unter Soll
                    if (fpeakshaveminsoc-5 > fBatt_SOC)
                    {
                        if (iMQTTAval<e3dc_config.peakshave-500)
                            iFc =  iBattLoad -iMQTTAval+e3dc_config.peakshave-500;
                        else
                            iFc =  iBattLoad -iMQTTAval+e3dc_config.peakshave-1000;
                    }
                        
                if (iFc > e3dc_config.maximumLadeleistung-500)
                    iFc = e3dc_config.maximumLadeleistung-500;
                
            }
            int iFc2 = iFc;
            if (iFc > 0)
            {
                if (iFc >e3dc_config.maximumLadeleistung) iFc = e3dc_config.maximumLadeleistung-500;
                average = average * .95 + float(iFc)*0.05;
/*
                if (average > 0)
                {
                    iFc = e3dc_config.maximumLadeleistung; // noch kein shaving
                    average = 0;
                }
*/
                iFc = average;
            }
            else
            if (iFc < 0)
            {
                if (iFc < e3dc_config.maximumLadeleistung*-1) iFc = e3dc_config.maximumLadeleistung*-1;
                average = average * .95 + float(iFc)*0.05;
                iFc = average;
            }
            else
                if (iFc ==0)
                {
                    average = average * .9;
                    if (average < 100) average = 0;
                    iFc = average;

                }
//            if (iFc != 0 && idauer == 0)
            if (idauer == 0)
                idauer = 1;
            if (idauer == -1)
            {
                iFc = e3dc_config.maximumLadeleistung;
                idauer = 0;
            }

            if (abs(iFc)<100) iFc = 0;
            
            if (iFc < e3dc_config.maximumLadeleistung*-1) iFc = e3dc_config.maximumLadeleistung*-1;
//            if (iFc > 8000) iFc = 8000;
//            iMinLade = iFc;
            iBattLoad = iFc;
            printf("%c[K\n", 27 );
            printf("shavingA = %i %.2f %i %i %i %i %2.0f %2.0f",idauer,fpeakshaveminsoc,iFc3,iFc2,iFc,iMQTTAval,fPower_Ext[2],fPower_Ext[3]);

        } else
        {
//            iFc = e3dc_config.maximumLadeleistung; // noch kein shaving
            if (idauer>0&&average<-500)
            {
                average = average * .98;
                iFc = average;
            }
            else
            idauer = 0;
            MQTTAval = MQTTAval*.9;
            printf("%c[K\n", 27 );
            printf("shavingB = %i %.2f %i %i %2.0f %2.0f",t-itime,fpeakshaveminsoc,iFc,iMQTTAval,fPower_Ext[2],fPower_Ext[3]);
        }
        iFc0 = iFc;
    }


    if ((t_alt%(24*3600)) <=tLadezeitende1&&t>=tLadezeitende2) // Wechsel Ladezeitzone
    {
        fAvBatterie = iMinLade*e3dc_config.powerfaktor;
        fAvBatterie900 = iMinLade*e3dc_config.powerfaktor;
    }

    if (fAvBatterie900  > e3dc_config.maximumLadeleistung)
    {
        iMinLade = e3dc_config.maximumLadeleistung;
        fAvBatterie = e3dc_config.maximumLadeleistung;
        fAvBatterie900 = e3dc_config.maximumLadeleistung;
    }

    
    printf("%c[K\n", 27 );

    //  Laden auf 100% nach 15:30
            if (iMinLade == iMinLade2)
                printf("ML1 %i RQ %i ",iMinLade,iFc);
            else
                printf("ML1 %i ML2 %i RQ %i ",iMinLade, iMinLade2,iFc);
            printf("GMT %2ld:%2ld ZG %d ",tLadezeitende/3600,tLadezeitende%3600/60,tZeitgleichung);

    printf(" %i:%i:%i ",hh,mm,ss);
    if (wetter.size()>0)
    printf("%.2f° %.2f° ",wetter[0].temp,fatemp);
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
// wenn unload < 0 Power setzen
    if (iPower > iFc&&idauer > 0&&e3dc_config.unload<0) iPower = iFc;
        
    if (e3dc_config.wallbox>=0&&(bWBStart||bWBConnect)&&bWBStopped&&(e3dc_config.wbmode>1)
        &&
        (
           ((t<tLadezeitende1)&&(e3dc_config.ladeende>fBatt_SOC))
           ||
           ((t>tLadezeitende1)&&(e3dc_config.ladeende2>fBatt_SOC))
        )
        &&
        ((tE3DC-tWBtime)<7200)
        &&((tE3DC-tWBtime)>10)
        &&e3dc_config.wbmode<20
        )
// wbmade < 20
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
//            if (iBattLoad > iPower_Bat)
            iDiffLadeleistung = iBattLoad-iPower_Bat+iDiffLadeleistung;
//            else
//                iDiffLadeleistung = iBattLoad-iPower_Bat;
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
                    //                if (e3dc_config.wrsteuerung>0)
                    iBattLoad = iPower;
                    //                else
                    //                    iBattLoad = 0;
                    tE3DC_alt = t;
                    
                    {
                        if (iLMStatus == 1){
                            if
                                (
                                 (iPower<e3dc_config.maximumLadeleistung)
                                 &&
                                 (
                                  (
                                   (iPower > ((iPower_Bat - int32_t(fPower_Grid))/2))
                                   &&
                                   //                              (fPower_Grid>100) // Netzbezug
                                   //                              &&
                                   (iPower*2>iPower_Bat) // Netzbezug
                                   )
                                  //                             ||
                                  //                             (iPower<iPower_Bat/2)  // er lädt zuviel im Freilauf
                                  )
                                 )
                                // Freilauf, solange die angeforderte Ladeleistung höher ist als die Batterieladeleistung abzüglich
                                // Wenn über der Wallbox geladen wird, Freilauf beenden
                                // die angeforderte Ladeleistung liegt über der verfügbaren Ladeleistung
                            {
                                if (e3dc_config.debug) printf("RQ1 %i",iPower);
                                if (idauer>0) iE3DC_Req_Load = iPower;
                                else
                                if ((fPower_Grid > 100)&&(iE3DC_Req_Load_alt<(e3dc_config.maximumLadeleistung-1)))
                                    // es liegt Netzbezug vor und System war nicht im Freilauf
                                {   if (idauer==0)
                                    iPower = iPower_Bat - int32_t(fPower_Grid);
                                    // Einspeichern begrenzen oder Ausspeichern anfordern, begrenzt auf e3dc_config.maximumLadeleistung
                                    if (e3dc_config.debug) printf("RQ2 %i",iPower);
                                    
                                    if (iPower < e3dc_config.maximumLadeleistung*-1)
                                        iPower = e3dc_config.maximumLadeleistung*-1;
                                }
                                else
                                {
                                    if (e3dc_config.debug) printf("RQ3 %i",iPower);
                                    iPower = e3dc_config.maximumLadeleistung;
                                }
                            }
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
                            
                            
                            if (iPower_PV>0||e3dc_config.unload<0)  // Nur wenn die Sonne scheint
                            {
                                if (e3dc_config.debug) printf("RQ4 %i2",iPower);

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
                                       idauer==0&&
                                       (((iPower_Bat+100)<iE3DC_Req_Load&& fPower_Grid>-100)
                                       ||
                                       fPower_Grid>100)
                                      )
                                      
                                     ))
                                    // Wenn der aktuelle Wert >= e3dc_config.maximumLadeleistung-1 ist
                                    // und der zuletzt angeforderte Werte auch >= e3dc_config.maximumLadeleistung-1
                                    // war, bleibt der Freilauf erhalten
                                    
                                {
                                    //                                    if (bDischarge)  // Entladen ist zugelassen?
                                    if (e3dc_config.debug) printf("RQ5 %i2",iPower);
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
                                        if (e3dc_config.debug) printf("RQ6 %i",iPower);
                                        
                                        iLMStatus = 3;
                                        iE3DC_Req_Load_alt = iE3DC_Req_Load;
                                    }else
                                        iLMStatus = -6;
                                    iLastReq = 6;
                                    sprintf(Log,"CTL %s %0.02f %i %i %0.02f",strtok(asctime(ts),"\n"),fBatt_SOC, iE3DC_Req_Load, iPower_Bat, fPower_Grid);
                                    WriteLog();}
                            } else 
                            {
                                if (iLMStatus > 0)
                                iLMStatus = 11;
                            }
                        }
                    }
                }
          }
    }
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
                (wolf[j].feld == "Betriebsart Heizgerät")
//                ||
//                (wolf[j].feld == "Verdichterstatus")
                )
                if (wolf[vdstatus].t>wolf[j].t)  // mit verdichterstatus vergleichen ob aktueller
                    printf("%s %s ",wolf[vdstatus].AK.c_str(),wolf[vdstatus].status.c_str());
                else
                    printf("%s %s ",wolf[j].AK.c_str(),wolf[j].status.c_str());
            else
                if (
                    (wolf[j].feld != "Verdichterstatus")
&&
                    (wolf[j].feld != "Betriebsart Heizgerät")

                    )
                printf("%s %0.1f ",wolf[j].AK.c_str(),wolf[j].wert);
            if (j==6)
                printf("%i%c[K\n", ALV, 27 );

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
    t_alt = t_sav;
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
        if (e3dc_config.debug) printf("WB1");

        iMaxBattLade = e3dc_config.maximumLadeleistung*.9;
        
        memcpy(WBchar6,"\x00\x06\x00\x00\x00\x00",e3dc_config.wbminladestrom);
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
    float fwbminSoC = e3dc_config.wbminSoC;
        
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
                    iPower = iPower+iWBMinimumPower/6+iPower_Bat-iMinLade;
                else
//                    iPower = iPower+iPower_Bat-iRefload+iWBMinimumPower;
                {
                    iPower = iPower+iWBMinimumPower;
                    if (iPower > 0)
                    {
// PV-Erzeugung > Einspeiselimit, Auto lädt noch nicht Ladevorgang sofort starten
                        if (iAvalPower < iWBMinimumPower)
                            iAvalPower = iWBMinimumPower;
                    }
                }

                if ((iPower <  (iWBMinimumPower)*-1)&&(WBchar[2] == e3dc_config.wbminladestrom)) // Erst bei Unterschreitung von Mindestladeleistung + 0W
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

//                iPower = iPower_Bat-fPower_Grid-iRefload;
                iPower = -fPower_Grid*2;
                if (iFc>iRefload) iRefload = (iRefload+iFc)/2;
                idynPower = (iRefload - (fAvBatterie900+fAvBatterie)/2)*-2;

                // Wenn das System im Gleichgewicht ist, gleichen iAvalPower und idynPower sich aus
                iPower = iPower + idynPower;
                if (iMinLade > 100&&iPower > (iPower_Bat-fPower_Grid))
                    iPower = iPower_Bat-fPower_Grid;
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
                    fwbminSoC = fLadeende-1.5;
                case 9:
                case 10:

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
                    if (fBatt_SOC>fwbminSoC)
                    iPower = idynPower;
                    else
//                      if (iPower < (iPower_Bat-fPower_Grid))
                          iPower = iPower_Bat-fPower_Grid;
                          }
// Nur bei PV-Ertrag
                if  ((iPower > 0)&&(iPower_PV<100)) iPower = -20000;
// Bei wbmode 10 wird zusätzlich bis zum minimum SoC entladen, auch wenn keine PV verfügbar

               if (
                   (
                    (e3dc_config.wbmode ==  8 && (iPower_PV>100))
                   ||
                    (e3dc_config.wbmode ==  9 && (iPower_PV>100))
                   ||
                   (e3dc_config.wbmode ==  10))
                   &&
                   (fBatt_SOC > (fwbminSoC))
                   )
                {iPower = e3dc_config.maximumLadeleistung*(fBatt_SOC-fwbminSoC)*(fBatt_SOC-fwbminSoC)*
                    (fBatt_SOC-fwbminSoC)/4; // bis > 2% uber MinSoC volle Entladung
                 if (iPower > e3dc_config.maximumLadeleistung)
                     iPower = e3dc_config.maximumLadeleistung*.9;
                    iPower = iPower +(iPower_Bat-fPower_Grid*2);
                    
                }
                if (iPower > (e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid*2))
                    iPower = e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid*2;
                          break;
            case 11:
                    if (fPower_Grid > 0)
                        iPower = -fPower_Grid*3; else
                        iPower = -fPower_Grid;
                break;
            case 20:
                if (fPower_WB <= 0) //Start
                {
                    iPower = -fPower_Grid - iPower_PV_E3DC -iWBMinimumPower*.9 + iPower_Bat;
//                    if (iPower > iWBMinimumPower&&iPower>iAvalPower)
//                        iAvalPower = iPower + iWBMinimumPower*.9;
                    if (abs(iPower)>abs(iAvalPower))
                        iAvalPower = iPower;
                }
                else
                    iPower = -fPower_Grid - iPower_PV_E3DC + iPower_Bat;
                break;
            case 21:
                if (fPower_WB <= 0) 
                {   //Start
                    iPower = -fPower_Grid -iWBMinimumPower*.8 + iPower_Bat;
//                    if (iPower > iWBMinimumPower&&iPower>iAvalPower)
//                        iAvalPower = iPower + iWBMinimumPower*.9;
                    if (abs(iPower)>abs(iAvalPower))
                        iAvalPower = iPower;
                }
                else
                {
                    iPower = -fPower_Grid + iPower_Bat;
                    if ((-fPower_Grid - iPower_PV_E3DC + iPower_Bat) > 0||WBchar6[1]>e3dc_config.wbminladestrom)
                        iPower = -fPower_Grid - iPower_PV_E3DC + iPower_Bat;
                }
                break;

                // Auswertung
        }
        if (e3dc_config.debug) printf("WB2");

        if (abs(iPower)/2>abs(iAvalPower))
            iAvalPower = iPower/2;

// im Sonnenmodus nur bei PV-Produktion regeln
        if (iPower > e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid)
            iPower = e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid;
        
//        if (iPower < -e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid)
//            iPower = -e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid;

/*
        if (iPower_PV_E3DC > e3dc_config.wrleistung){
            iPower = iPower - iPower_PV_E3DC + e3dc_config.wrleistung;
            if (fPower_Grid < 100 && iPower > fPower_Grid*-1) // Netzbezug, verfügbare Leistung reduzieren
                iPower = fPower_Grid*-1;
            if (fPower_Grid > 100 && iPower > 0) // Netzbezug, verfügbare Leistung reduzieren
            iPower = fPower_Grid*-3;
        }
*/
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
/*
        if (iAvalPower < (-iMaxBattLade+iPower_Bat-fPower_Grid-fPower_WB))
            iAvalPower = -iMaxBattLade+iPower_Bat-fPower_Grid-fPower_WB;
*/
        if (e3dc_config.wbmode==11||e3dc_config.wbmode==10)
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
                    if (e3dc_config.debug) printf("WB4");
                    createRequestWBData(frameBuffer);
                    if (e3dc_config.debug) printf("WB5");

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
                if (e3dc_config.debug) printf("WB6");

                createRequestWBData(frameBuffer);
                if (e3dc_config.debug) printf("WB7");

                WBChar_alt = WBchar6[1];
                }
        }
            }     else if ((WBchar6[1] > e3dc_config.wbminladestrom)&&(fPower_WB == 0)) WBchar6[1] = e3dc_config.wbminladestrom;

// Wenn der wbmodus neu eingestellt wurde, dann mit 7A starten

            if (bWBChanged) {
                bWBChanged = false;
                WBchar6[1] = e3dc_config.wbminladestrom+1;  // Laden von 6A aus
                WBchar6[4] = 0; // Toggle aus
                if (e3dc_config.debug) printf("WB8");

                createRequestWBData(frameBuffer);
                if (e3dc_config.debug) printf("WB9");

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
                if (e3dc_config.debug) printf("WB10");

                createRequestWBData(frameBuffer);
                if (e3dc_config.debug) printf("WB11");

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
                        WBchar6[1] = e3dc_config.wbminladestrom;  // Laden von 6A aus
                            WBchar6[4] = 1; // Laden starten
                            bWBOn = true;
                        if (e3dc_config.debug) printf("WB12");

                        createRequestWBData(frameBuffer);
                        if (e3dc_config.debug) printf("WB13");

                        WBchar6[4] = 0; // Toggle aus
                        WBChar_alt = WBchar6[1];
                        iWBStatus = 30;
                    } else
                    if (WBchar[2] != e3dc_config.wbminladestrom)
                        {
                            WBchar6[1] = e3dc_config.wbminladestrom;  // Laden von 6A aus
                            WBchar6[4] = 0; // Toggle aus
                            if (e3dc_config.debug) printf("WB14");

                            createRequestWBData(frameBuffer);
                            if (e3dc_config.debug) printf("WB15");

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
                if (WBchar6[1]==e3dc_config.wbminladestrom) iWBMinimumPower = fPower_WB;
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
                if (icurrent == e3dc_config.wbminladestrom&&WBchar6[1]>16)
                    WBchar6[1] = 16;
                if (e3dc_config.debug) printf("WB16");

                createRequestWBData(frameBuffer);
                if (e3dc_config.debug) printf("WB17");

                WBChar_alt = WBchar6[1];
                if ((icurrent <=16)&&WBchar6[1]>16)
                iWBStatus = 30;
                // Länger warten bei Wechsel von <= 16A auf > 16A hohen Stömen

             } else

// Prüfen Herabsetzung Ladeleistung
            if ((WBchar6[1] > e3dc_config.wbminladestrom)&&(iAvalPower<=((iWBMinimumPower/6)*-1)))
                  { // Mind. 2000W Batterieladen
                WBchar6[1]--;
                for (int X1 = 2; X1 < 20; X1++)
                    if ((iAvalPower <= ((iWBMinimumPower/6)*-X1))&& (WBchar6[1]>e3dc_config.wbminladestrom+1)) WBchar6[1]--; else break;
                WBchar[2] = WBchar6[1];
//                createRequestWBData2(frameBuffer);
                      if (e3dc_config.debug) printf("WB18");

                createRequestWBData(frameBuffer);
                      if (e3dc_config.debug) printf("WB19");

                if (WBchar6[1]==e3dc_config.wbminladestrom)
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
                if ((WBchar6[1] >= e3dc_config.wbminladestrom)&&bWBLademodus)
                {WBchar6[1]--;

                        if (WBchar6[1]==e3dc_config.wbminladestrom-1) {
                            WBchar6[1]=e3dc_config.wbminladestrom;
                            WBchar6[4] = 1;
                            bWBOn = false;
                        } // Laden beenden
                    if (e3dc_config.debug) printf("WB20");

                        createRequestWBData(frameBuffer);
                    if (e3dc_config.debug) printf("WB21");

                    WBchar6[1]=e3dc_config.wbminladestrom-1;
                    WBchar6[4] = 0;
                    WBChar_alt = WBchar6[1];
                    iWBStatus = 10;  // Warten bis Neustart
                }}
    }
        }}
    if (e3dc_config.debug) printf("WB22");
    printf("%c[K\n", 27 );
    if (e3dc_config.debug) printf("WB23");
    printf("AVal %0i/%01i/%01i Power %0i WBMode %0i ", iAvalPower,iPower,iMaxBattLade,iWBMinimumPower, e3dc_config.wbmode);
    printf("iWBStatus %i %i %i %i",iWBStatus,WBToggel,WBchar6[1],WBchar[2]);
    if (iWBStatus > 1) iWBStatus--;
    if (e3dc_config.debug) printf("WBend");
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
        static int32_t Mode;

        if (e3dc_config.wrsteuerung==0)
            printf("\n Achtung WR-Steuerung inaktiv %i %i Status %i\n",iBattLoad,iE3DC_Req_Load,iLMStatus);
        if (e3dc_config.wrsteuerung==2) // Text ausgeben
            printf("\n WR-Steuerung aktiv %i %i %i Status %i\n",iBattLoad,iE3DC_Req_Load,Mode,iLMStatus);

        if (iLMStatus < 0&&e3dc_config.wrsteuerung==0) iLMStatus=iLMStatus*-1;
        if (iLMStatus < 0&&e3dc_config.wrsteuerung>0)
        {
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
        if (e3dc_config.wrleistung>=30000)  // S20Xnn
        {
            protocol.appendValue(&PVIContainer, TAG_PVI_REQ_DC_POWER, (uint8_t)2);
            protocol.appendValue(&PVIContainer, TAG_PVI_REQ_DC_VOLTAGE, (uint8_t)2);
            protocol.appendValue(&PVIContainer, TAG_PVI_REQ_DC_CURRENT, (uint8_t)2);
        }

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
        printf("PV %i", iPower);
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
//        printf(" E3DC %i ", -iPowerBalance - int(fPower_WB));
        printf(" # %i", iPower_PV - iPower_Bat + iPower - int(fPower_WB));
        printf("%c[K\n", 27 );

        break;
    }
    case TAG_EMS_POWER_ADD: {    // response for TAG_EMS_REQ_POWER_ADD
        int32_t iPower = protocol->getValueAsInt32(response);

        printf("+%i", - iPower);
        iPower_PV = iPower_PV - iPower;
        printf("=%i", iPower_PV);
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
//                printf("Battery SOC %0.02f%% ", fBatt_SOC);
                printf("SOC %0.02f%% ", fBatt_SOC);
                break;
            }
            case TAG_BAT_MODULE_VOLTAGE: {    // response for TAG_BAT_REQ_MODULE_VOLTAGE
                fVoltage = protocol->getValueAsFloat32(&batteryData[i]);
                printf(" %0.1fV ", fVoltage);
                break;
            }
            case TAG_BAT_CURRENT: {    // response for TAG_BAT_REQ_CURRENT
                float fCurrent = protocol->getValueAsFloat32(&batteryData[i]);
                fPower_Bat = fVoltage*fCurrent;
                printf(" %0.02fA %0.02fW", fCurrent,fPower_Bat);
                if (e3dc_config.statistik)
                {
                    int x1 = (t_alt%(24*7*4*900))/900;
                    int x2 = (t_alt%(24*7*4*900))/900;
                    int x3 = (t_alt%(24*7*4*900))/900;
                    float f4 = (t_alt%(900))+1; //(0..899) daher +1
//                    float fsollGrid = (e3dc_config.peakshave*900-iGridStat[Gridstat])/900000.0;
                    float fsollGrid = (e3dc_config.peakshave*900-iGridStat[Gridstat])/(900-f4)/1000.0;
                    if (fsollGrid > e3dc_config.peakshave) fsollGrid = e3dc_config.peakshave;
                    if (x1 == 0) x1 = weekhour; else x1--;
                    if (x3 == dayhour) x3 = 0; else x3++;
                    printf(" %0.04f/%0.04f/%0.04f %0.04f  %0.04fkWh",iWeekhour[x1]/900000.0,iWeekhour[x2]/900000.0,iWeekhour[x3]/900000.0,iWeekhour[weekhour]/f4/1000.0,iWeekhour[dayhour]/3600000.0); // Tages Hausverbrauch
                    {
                        printf("%c[K\n", 27 );
                        printf(" %0.04f/%0.04f/%0.04f %0.04f %0.04fW",iGridStat[Gridstat-2]/900000.0,iGridStat[Gridstat-1]/900000.0,iGridStat[Gridstat]/900000.0,iGridStat[Gridstat]/f4/1000.0,(fsollGrid)); // Tages Hausverbrauch
                    }
// Grid
                    if (e3dc_config.WP)
                    {
//                        printf("%c[K\n", 27 );
                        printf(" WP %0.04f/%0.04f/%0.04f %0.04f  %0.04fkWh",iWeekhourWP[x1]/900000.0,iWeekhourWP[x2]/900000.0,iWeekhourWP[x3]/900000.0,float(iWeekhourWP[weekhour])/f4/1000.0,iWeekhourWP[dayhour]/3600000.0); // Tages Hausverbrauch
                    }
                }
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
                        printf("#%u %0.1fW ", ucPMIndex,fPower1);
                        break;
                    }
                    case TAG_PM_POWER_L2: {              // response for TAG_PM_REQ_L2
                        fPower2 = protocol->getValueAsDouble64(&PMData[i]);
                        break;
                    }
                    case TAG_PM_POWER_L3: {              // response for TAG_PM_REQ_L3
                        fPower3 = protocol->getValueAsDouble64(&PMData[i]);
                        if ((fPower2+fPower3)||0){
                        printf("%0.1fW %0.1fW ", fPower2, fPower3);
                        printf("#%0.1fW ", fPower1+fPower2+fPower3);
                        printf("%c[K\n", 27 );
                            fPower_Ext[ucPMIndex] = fPower1+fPower2+fPower3;
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
                            printf("& %0.01f %0.01f %0.01f %0.01fW ", fAvPower_Grid3600, fAvPower_Grid600, fAvPower_Grid60, fAvPower_Grid);
                    }
                        break;
                    }
                    case TAG_PM_VOLTAGE_L1: {              // response for TAG_PM_REQ_L1
                        float fPower = protocol->getValueAsFloat32(&PMData[i]);
                        printf(" %0.1fV", fPower);
                        fL1V = fPower;
                        sprintf(buffer,"openWB/set/evu/VPhase1 -m %0.1f",float(fPower));
                        MQTTsend(e3dc_config.openWB_ip,buffer);

                        break;
                    }
                    case TAG_PM_VOLTAGE_L2: {              // response for TAG_PM_REQ_L2
                        float fPower = protocol->getValueAsFloat32(&PMData[i]);
                        printf(" %0.1fV", fPower);
                        fL2V = fPower;
                        sprintf(buffer,"openWB/set/evu/VPhase2 -m %0.1f",float(fPower));
                        MQTTsend(e3dc_config.openWB_ip,buffer);
                        break;
                    }
                    case TAG_PM_VOLTAGE_L3: {              // response for TAG_PM_REQ_L3
                        float fPower = protocol->getValueAsFloat32(&PMData[i]);
                        printf(" %0.1fV", fPower);
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
                                                    if (index == 2) printf("# %0.0fW",fGesPower);

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
                                    printf("WB: Mode %02X ",uint8_t(cWBALG));
//                                    for(size_t x = 0; x < sizeof(WBchar); ++x)
//                                        printf("%02X ", uint8_t(WBchar[x]));
                                    if (bWBLademodus) printf("SUN"); else printf("Grid");
                                    if (bWBConnect) printf(" lock");
                                    if (bWBStart) printf(" start");
                                    if (bWBCharge) printf(" charge");
                                    if (bWBStopped ) printf(" stop");

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
                                    printf(" %u/%uA ",iWBSoll,WBchar[2]);
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
    while(!bStopExecution&&!e3dc_config.stop)
    {
        
        //--------------------------------------------------------------------------------------------------------------
        // RSCP Transmit Frame Block Data
        //--------------------------------------------------------------------------------------------------------------
        SRscpFrameBuffer frameBuffer;
        memset(&frameBuffer, 0, sizeof(frameBuffer));

        // create an RSCP frame with requests to some example data
        if(iAuthenticated == 1) {
            int sunrise = sunriseAt;
            if (e3dc_config.debug) printf("M1\n");
            if (e3dc_config.aWATTar||e3dc_config.openmeteo)
            aWATTar(ch,w,wetter,e3dc_config,fBatt_SOC, sunrise);
//            test;
            if (e3dc_config.debug) printf("M2\n");
            float zulufttemp = -99;
            if (e3dc_config.WPWolf)
                zulufttemp = wolf[wpzl].wert;
            if (fBatt_SOC >= 0)
            mewp(w,wetter,fatemp,fcop,sunriseAt,sunsetAt,e3dc_config,fBatt_SOC,ireq_Heistab,zulufttemp);       // Ermitteln Wetterdaten
            if (e3dc_config.debug) printf("M3\n");

            if (strcmp(e3dc_config.heizung_ip,"0.0.0.0") >  0)
              iModbusTCP();
            if (e3dc_config.debug) printf("M3a\n");

            if((frameBuffer.dataLength == 0)&&(e3dc_config.wallbox>=0)&&(bWBRequest))
            {
                if (e3dc_config.debug) printf("M3b\n");
                WBProcess(&frameBuffer);
            }
            if (e3dc_config.debug) printf("M4\n");

            if(frameBuffer.dataLength == 0)
                 bWBRequest = true;
            else
                 bWBRequest = false;
if (e3dc_config.debug) printf("M5\n");

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
                    printf("%c[H", 27 );
                if (e3dc_config.debug)
                    printf("%c[2J", 27 );
//                printf("Request cyclic example data done %s
//                printf("Request data done %s %2ld:%2ld:%2ld",VERSION,tm_CONF_dt%(24*3600)/3600,tm_CONF_dt%3600/60,tm_CONF_dt%60);
                printf("%s %2ld:%2ld:%2ld  ",VERSION,tm_CONF_dt%(24*3600)/3600,tm_CONF_dt%3600/60,tm_CONF_dt%60);
                printf(" %0.02f %0.02f %0.02fkWh", fPVtoday*e3dc_config.speichergroesse/100,fPVSoll*e3dc_config.speichergroesse/100,fPVdirect*e3dc_config.speichergroesse/100); // erwartete PV Ertrag in % des Speichers
                int x2 = (t%(24*4*900))/900+1;
                if (x2 > 0&&e3dc_config.statistik)
                {
// Ausgabe Soll/Ist/ %  -15min, akt Soll Ist
                    float f2 = iDayStat[x2]/100.0;
                    float f3 = iDayStat[x2+96]/(e3dc_config.speichergroesse*10*3600);
                    float f4 = 0;
                    if (wetter.size()>0)
                        f4 = wetter[0].solar;
                    float f5 = iDayStat[DayStat]/(e3dc_config.speichergroesse*10*3600);

//                    if (f2>0)
//                    printf(" %0.02f%% %0.02f%% %0.02f %0.02f%% %0.04f%%", f2,f3,f3/f2,f4,f5);
                    printf(" %0.02f%% %0.02f%% %0.02f %0.02f %0.04fkWh", f2,f3,f3/f2,f4*e3dc_config.speichergroesse/100,iDayStat[DayStat]/3600000.0);
                    // erwartete PV Ertrag
                }
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
//    signgam(EPIPE, SIG_IGN);
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

 

    // endless application which re-connections to server on connection lost
    int res = system("pwd");
    GetConfig();
    e3dc_config.stop = 0; // Stop ausschalten
    
    if (e3dc_config.statistik)
    {
        FILE * pFile;
        pFile = NULL;
        pFile = fopen ("Weekhour.dat","rb");
        if (pFile!=NULL)
        {
            size_t x1 = sizeof(iWeekhour);
            x1 = fread (&iWeekhour , sizeof(uint32_t), sizeof(iWeekhour)/sizeof(uint32_t), pFile);
            fclose (pFile);
        }
        pFile = NULL;
        pFile = fopen ("WeekhourWP.dat","rb");
        if (pFile!=NULL)
        {
            size_t x1 = sizeof(iWeekhourWP);
            x1 = fread (&iWeekhourWP , sizeof(uint32_t), sizeof(iWeekhourWP)/sizeof(uint32_t), pFile);
            fclose (pFile);
        }

        pFile = NULL;
        char fname[100];
        time(&t);
        int day = (t%(24*3600*28))/(24*3600);
        sprintf(fname,"%s.dat","PVStat");
        pFile = fopen(fname,"rb");       // altes logfile löschen

        if (pFile!=NULL)
        {
            size_t x1 = sizeof(iDayStat);
            x1 = fread (&iDayStat , sizeof(uint32_t), sizeof(iDayStat)/sizeof(uint32_t), pFile);
            fclose (pFile);
        }
        pFile = NULL;
        sprintf(fnameGrid,"Grid.%i.%i.dat",ptm->tm_year%100,ptm->tm_mon+1);
        pFile = fopen(fnameGrid,"rb");       // altes logfile löschen

        if (pFile!=NULL)
        {
            size_t x1 = sizeof(iGridStat);
            x1 = fread (&iGridStat, sizeof(int32_t), sizeof(iGridStat)/sizeof(int32_t), pFile);
            fclose (pFile);
        }

    }
    
        while(iEC < 10&&e3dc_config.stop==0||e3dc_config.stop>99)
        {
            printf("Program stop Reason e3dc_config.stop=%i",e3dc_config.stop);
            e3dc_config.stop=0;
            iEC++; // Schleifenzähler erhöhen
            int hh1 = sunsetAt / 60;
            int mm1 = sunsetAt % 60;
            int hh = sunriseAt / 60;
            int mm = sunriseAt % 60;
            sprintf(Log,"Start %s %s", strtok(asctime(ptm),"\n"),VERSION);
            WriteLog();
            // connect to server
            printf("Program Start Version:%s\n",VERSION);
            printf("Sonnenaufgang %i:%i %i:%i\n", hh, mm, hh1, mm1);
//            CheckConfig();
//            printf("GetConfig done");
            if ((e3dc_config.aWATTar||e3dc_config.openmeteo))
            {
                mewp(w,wetter,fatemp,fcop,sunriseAt,sunsetAt,e3dc_config,55.5,ireq_Heistab,5);
                aWATTar(ch,w,wetter,e3dc_config,fBatt_SOC, sunriseAt); // im Master nicht aufrufen

            }
            while (e3dc_config.test)
                LoadDataProcess();

            LoadDataProcess();

        printf("Connecting to server %s:%i\n", e3dc_config.server_ip, e3dc_config.server_port);
        iSocket = SocketConnect(e3dc_config.server_ip, e3dc_config.server_port);
        if(iSocket < 0) 
        {
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
        if (!e3dc_config.stop)
            sleep(1);
            if (e3dc_config.statistik)
            {
                FILE * pFile;
                pFile = NULL;
                pFile = fopen ("Weekhour.dat","wb");
                if (pFile!=NULL)
                {
                    fwrite (iWeekhour , sizeof(uint32_t), sizeof(iWeekhour)/sizeof(uint32_t), pFile);
                    fclose (pFile);
                }
                pFile = fopen ("WeekhourWP.dat","wb");
                if (pFile!=NULL)
                {
                    fwrite (iWeekhourWP , sizeof(uint32_t), sizeof(iWeekhourWP)/sizeof(uint32_t), pFile);
                    fclose (pFile);
                }
                char fname[100];
                time(&t);
                int day = (t%(24*3600*28))/(24*3600);
                sprintf(fname,"%s.dat","PVStat");
                pFile = fopen(fname,"wb");       // altes logfile löschen

                if (pFile!=NULL)
                {
                    x1 = fwrite (iDayStat , sizeof(uint32_t), sizeof(iDayStat)/sizeof(uint32_t), pFile);
                    fclose (pFile);
                }
                pFile = fopen(fnameGrid,"wb");       // datei zurückschreiben
                if (pFile!=NULL)
                {
                    x1 = fwrite (iGridStat , sizeof(int32_t), sizeof(iGridStat)/sizeof(int32_t), pFile);
                    fclose (pFile);
                }

            }

    }

    printf("Programm wirklich beendet\n");
    
    return 0;
}

// Delta E3DC-V1

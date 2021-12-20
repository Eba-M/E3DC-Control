#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include "RscpProtocol.h"
#include "RscpTags.h"
#include "SocketConnection.h"
#include "AES.h"
#include <time.h>
#include "E3DC_CONF.h"
#include "SunriseCalc.hpp"
#include "awattar.hpp"
//#include "MQTTClient.h"
//#include "json.hpp"

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
static float fPower_Grid;
static float fAvPower_Grid,fAvPower_Grid3600,fAvPower_Grid600,fAvPower_Grid60; // Durchschnitt ungewichtete Netzleistung der letzten 10sec
static int iAvPower_GridCount = 0;
static float fPower_WB;
static int32_t iPower_PV, iPower_PV_E3DC;
static int32_t iPower_Bat;
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

int sunriseAt;  // Sonnenaufgang
int sunsetAt;   // Sonnenuntergang
std::vector<watt_s> ch;  //charge hour

FILE * pFile;
e3dc_config_t e3dc_config;
char Log[300];

int WriteLog()
{
  static time_t t,t_alt = 0;
    int day,hour;
    char fname[127];
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

int MQTTsend(char buffer[127])

{
    char cbuf[127];
    if (e3dc_config.openWB) {
        sprintf(cbuf, "mosquitto_pub -r -h %s -t %s", e3dc_config.openWBhost,buffer);
        system(cbuf);
    }
    return(0);
}
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
int ControlLoadData2(SRscpFrameBuffer * frameBuffer,int32_t iPower) {
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


int createRequestWBData(SRscpFrameBuffer * frameBuffer) {
    RscpProtocol protocol;
    SRscpValue rootValue;

    iWBStatus=12;
    
    // The root container is create with the TAG ID 0 which is not used by any device.
    protocol.createContainerValue(&rootValue, 0);
    
    // request Power Meter information
    SRscpValue WBContainer;
    SRscpValue WB2Container;

    // request Wallbox data

    protocol.createContainerValue(&WBContainer, TAG_WB_REQ_DATA) ;
    // add index 0 to select first wallbox
    protocol.appendValue(&WBContainer, TAG_WB_INDEX,0);

    
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

    iWBStatus=12;
    
    // The root container is create with the TAG ID 0 which is not used by any device.
    protocol.createContainerValue(&rootValue, 0);
    
    // request Power Meter information
    SRscpValue WBContainer;
    SRscpValue WB2Container;

    // request Wallbox data

    protocol.createContainerValue(&WBContainer, TAG_WB_REQ_DATA) ;
    // add index 0 to select first wallbox
    protocol.appendValue(&WBContainer, TAG_WB_INDEX,0);

    
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


static float fBatt_SOC, fBatt_SOC_alt;
static float fSavedtoday, fSavedyesderday,fSavedtotal,fSavedWB; // Überschussleistung
static int32_t iDiffLadeleistung, iDiffLadeleistung2;
static time_t tLadezeit_alt,tLadezeitende_alt,tE3DC_alt;
static time_t t = 0;
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
        stat(e3dc_config.conffile,&stats);
        tm_CONF_dt = *(&stats.st_mtime);
        char var[128], value[128], line[256];
        e3dc_config.wallbox = false;
        e3dc_config.openWB = false;
        e3dc_config.ext1 = false;
        e3dc_config.ext2 = false;
        e3dc_config.ext3 = false;
        e3dc_config.ext7 = false;
        sprintf(e3dc_config.logfile,"logfile");
        sprintf(e3dc_config.openWBhost,"%s",OPENWB);
        e3dc_config.debug = false;
        e3dc_config.wurzelzaehler = 0;
        e3dc_config.untererLadekorridor = UNTERERLADEKORRIDOR;
        e3dc_config.obererLadekorridor = OBERERLADEKORRIDOR;
        e3dc_config.minimumLadeleistung = MINIMUMLADELEISTUNG;
        e3dc_config.maximumLadeleistung = MAXIMUMLADELEISTUNG;
        e3dc_config.wrleistung = WRLEISTUNG;
        e3dc_config.speichergroesse = SPEICHERGROESSE;
        e3dc_config.winterminimum = WINTERMINIMUM;
        e3dc_config.sommermaximum = SOMMERMAXIMUM;
        e3dc_config.sommerladeende = SOMMERLADEENDE;
        e3dc_config.einspeiselimit = EINSPEISELIMIT;
        e3dc_config.ladeschwelle = LADESCHWELLE;
        e3dc_config.ladeende = LADEENDE;
        e3dc_config.ladeende2 = LADEENDE2;
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
        e3dc_config.hoehe = 50;
        e3dc_config.laenge = 10;
        e3dc_config.aWATTar = false;
        e3dc_config.Avhourly = 5;   // geschätzter Verbrauch in %
        e3dc_config.AWDiff = 100;   // geschätzter Verbrauch in %




            while (fgets(line, sizeof(line), fp)) {
                fpread = true;
                memset(var, 0, sizeof(var));
                memset(value, 0, sizeof(value));
                if(sscanf(line, "%[^ \t=]%*[\t ]=%*[\t ]%[^\n]", var, value) == 2) {
                    if(strcmp(var, "server_ip") == 0)
                        strcpy(e3dc_config.server_ip, value);
                    else if(strcmp(var, "server_port") == 0)
                        e3dc_config.server_port = atoi(value);
                    else if(strcmp(var, "e3dc_user") == 0)
                        strcpy(e3dc_config.e3dc_user, value);
                    else if(strcmp(var, "e3dc_password") == 0)
                        strcpy(e3dc_config.e3dc_password, value);
                    else if(strcmp(var, "aes_password") == 0)
                        strcpy(e3dc_config.aes_password, value);
                    else if(strcmp(var, "openWBhost") == 0)
                        strcpy(e3dc_config.openWBhost, value);
                    else if(strcmp(var, "wurzelzaehler") == 0)
                        e3dc_config.wurzelzaehler = atoi(value);
                    else if((strcmp(var, "wallbox") == 0)&&
                           (strcmp(value, "true") == 0))
                        e3dc_config.wallbox = true;
                    else if((strcmp(var, "openWB") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.openWB = true;
                    else if((strcmp(var, "ext1") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.ext1 = true;
                    else if((strcmp(var, "ext2") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.ext2 = true;
                    else if((strcmp(var, "ext3") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.ext3 = true;
                    else if((strcmp(var, "ext7") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.ext7 = true;
                    else if(strcmp(var, "logfile") == 0)
                        strcpy(e3dc_config.logfile, value);
                    else if((strcmp(var, "debug") == 0)&&
                            (strcmp(value, "true") == 0))
                    {e3dc_config.debug = true;

                        
                    }
                    else if(strcmp(var, "untererLadekorridor") == 0)
                        e3dc_config.untererLadekorridor = atoi(value);
                    else if(strcmp(var, "obererLadekorridor") == 0)
                        e3dc_config.obererLadekorridor = atoi(value);
                    else if(strcmp(var, "minimumLadeleistung") == 0)
                        e3dc_config.minimumLadeleistung = atoi(value);
                    else if(strcmp(var, "maximumLadeleistung") == 0)
                        e3dc_config.maximumLadeleistung = atoi(value);
                    else if(strcmp(var, "wrleistung") == 0)
                        e3dc_config.wrleistung = atoi(value);
                    else if(strcmp(var, "speichergroesse") == 0)
                        e3dc_config.speichergroesse = atof(value);
                    else if(strcmp(var, "winterminimum") == 0)
                        e3dc_config.winterminimum = atof(value);
                    else if(strcmp(var, "sommermaximum") == 0)
                        e3dc_config.sommermaximum = atof(value);
                    else if(strcmp(var, "sommerladeende") == 0)
                        e3dc_config.sommerladeende = atof(value);
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
                    else if(strcmp(var, "wbminSoC") == 0)
                        e3dc_config.wbminSoC = atof(value);
                    else if(strcmp(var, "hoehe") == 0)
                        e3dc_config.hoehe = atof(value);
                    else if(strcmp(var, "laenge") == 0)
                        e3dc_config.laenge = atof(value);
                    else if(strcmp(var, "peakshave") == 0)
                        e3dc_config.peakshave = atoi(value); // in Watt
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
                    else if((strcmp(var, "aWATTar") == 0)&&
                            (strcmp(value, "true") == 0))
                        e3dc_config.aWATTar = true;
                    else if(strcmp(var, "Avhourly") == 0)
                        e3dc_config.Avhourly = atof(value); // % der SoC
                    else if(strcmp(var, "AWDiff") == 0)
                        e3dc_config.AWDiff = atof(value)*10; // % der SoC


                }
            }
    //        printf("e3dc_user %s\n",e3dc_config.e3dc_user);
    //        printf("e3dc_password %s\n",e3dc_config.e3dc_password);
    //        printf("aes_password %s\n",e3dc_config.aes_password);
            fclose(fp);
        }

    if ((!fp)||not (fpread)) printf("Configurationsdatei %s nicht gefunden",CONF_FILE);
    return fpread;
}

time_t tLadezeitende,tLadezeitende1,tLadezeitende2,tLadezeitende3;  // dynamische Ladezeitberechnung aus dem Cosinus des lfd Tages. 23 Dez = Minimum, 23 Juni = Maximum


        


int LoadDataProcess(SRscpFrameBuffer * frameBuffer) {
//    const int cLadezeitende1 = 12.5*3600;  // Sommerzeit -2h da GMT = MEZ - 2
    printf("\n");
    tm *ts;
    ts = gmtime(&tE3DC);
    hh = t % (24*3600)/3600;
    mm = t % (3600)/60;
    ss = t % (60);

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
    
    
    
    
    float fLadeende = e3dc_config.ladeende;
    float fLadeende2 = e3dc_config.ladeende2;
    float fLadeende3 = e3dc_config.unload;

    if (cos((ts->tm_yday+9)*2*3.14/365) > 0)
    {
    fLadeende = (cos((ts->tm_yday+9)*2*3.14/365))*((100+e3dc_config.ladeende2)/2-fLadeende)+fLadeende;
    fLadeende2 = (cos((ts->tm_yday+9)*2*3.14/365))*(100-fLadeende2)+fLadeende2;
    fLadeende3 = (cos((ts->tm_yday+9)*2*3.14/365))*(100-fLadeende3)+fLadeende3;
    }
    int cLadezeitende1 = (e3dc_config.winterminimum+(e3dc_config.sommermaximum-e3dc_config.winterminimum)/2)*3600;
    int cLadezeitende2 = (e3dc_config.winterminimum+0.5+(e3dc_config.sommerladeende-e3dc_config.winterminimum-0.5)/2)*3600; // eine halbe Stunde Später
    int cLadezeitende3 = (e3dc_config.winterminimum-(e3dc_config.sommermaximum-e3dc_config.winterminimum)/2)*3600; //Unload

    int32_t tZeitgleichung;
    tLadezeitende1 = cLadezeitende1+cos((ts->tm_yday+9)*2*3.14/365)*-((e3dc_config.sommermaximum-e3dc_config.winterminimum)/2)*3600;
    tLadezeitende2 = cLadezeitende2+cos((ts->tm_yday+9)*2*3.14/365)*-((e3dc_config.sommerladeende-e3dc_config.winterminimum-0.5)/2)*3600;
    tLadezeitende3 = cLadezeitende3-cos((ts->tm_yday+9)*2*3.14/365)*-((e3dc_config.sommermaximum-e3dc_config.winterminimum)/2)*3600;

//    float fht = e3dc_config.ht * cos((ts->tm_yday+9)*2*3.14/365);
    float fht = e3dc_config.htsockel + (e3dc_config.ht-e3dc_config.htsockel) * cos((ts->tm_yday+9)*2*3.14/365);

    // HT Endeladeleistung freigeben
    // Mo-Fr wird während der Hochtarif der Speicher zwischen hton und htoff endladen
    // Samstag und Sonntag nur wenn htsat und htsun auf true gesetzt sind.
    // Damit kann gleich eine intelligente Notstromreserve realisiert werden.
    // Die in ht eingestellte Reserve wird zwischen diesem Wert und 0% zur Tag-Nachtgleiche gesetzt
    // Die Notstromreserve im System ist davon unberührt
    if (iLMStatus == 1)
    {
        int ret;
        ret =  CheckaWATTar(sunriseAt,sunsetAt,fBatt_SOC,fht,e3dc_config.Avhourly,e3dc_config.AWDiff);
        if  (ret == 2)
        {
              iE3DC_Req_Load = e3dc_config.maximumLadeleistung*1.9;
//            printf("Netzladen an");
//            iE3DC_Req_Load = e3dc_config.maximumLadeleistung*0.8;
            iLMStatus = -7;
            bDischargeDone = false;
            return 0;
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
        (fht<fBatt_SOC)        // Wenn der SoC > der berechneten Reserve liegt
    ||(iNotstrom==1)  //Notstrom
    ||(iNotstrom==4)  //Inselbetrieb
   ){
            // ENdladen einschalten)
        if ((iPower_Bat == 0)&&(fPower_Grid>100))
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
        if (not bDischarge) // Entladen soll unterdrückt werden
        { if ((fPower_Grid < -100)&&(iPower_Bat==0))  // es wird eingespeist Entladesperre solange aufheben
                {
                    iE3DC_Req_Load = fPower_Grid*-1;  // Es wird eingespeist
                    iLMStatus = -7;
                    printf("Batterie laden zulassen ");
//                    return 0;
                }   else
                if (((iPower_Bat < -100)||(fPower_Grid>100))&&(fPower_WB==0)) // Entladen zulassen wenn WB geladen wird
                {  // Entladen Stoppen wenn
                    iE3DC_Req_Load = 0;  // Sperren
                    if (iPower_PV > 0)
                    iE3DC_Req_LoadMode = -2;       //Entlademodus  \n
    //                    printf("\nEntladen stoppen ");
                    iLMStatus = -7;
//                    return 0;
                }
        }
        else          // Entladen ok
        if ((fPower_Grid > 100)&&(iPower_Bat ==0))  // es wird Strom bezogen Entladesperre solange aufheben
        {
                iE3DC_Req_Load = fPower_Grid*-1;  //Automatik anstossen
    //                if (iE3DC_Req_Load < e3dc_config.maximumLadeleistung*-1)  //Auf maximumLadeleistung begrenzen
    //                iE3DC_Req_Load = e3dc_config.maximumLadeleistung*-1;  //Automatik anstossen
                 printf("Entladen starten ");
                 iLMStatus = -7;
//                return 0;
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
    printf("LE %2ld:%2ld %0.1f%%\n",tLadezeitende2/3600,tLadezeitende2%3600/60,fLadeende2);

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
          iFc = iFc*(float(e3dc_config.maximumLadeleistung)/(e3dc_config.obererLadekorridor-e3dc_config.untererLadekorridor));
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
        //  Laden auf 100% nach 15:30
            if (iMinLade == iMinLade2)
                printf("ML1 %i RQ %i ",iMinLade,iFc);
            else
                printf("ML1 %i ML2 %i RQ %i ",iMinLade, iMinLade2,iFc);
            printf("GMT %2ld:%2ld ZG %d ",tLadezeitende/3600,tLadezeitende%3600/60,tZeitgleichung);
        
    printf("E3DC: %s", asctime(ts));

    
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
        
    if (e3dc_config.wallbox&&(bWBStart||bWBConnect)&&(bWBStopped||(iWBStatus>1))&&(e3dc_config.wbmode>1)
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
    if (iLMStatus == 1)
    {
        
            {
            if (iBattLoad > (iPower_Bat-iDiffLadeleistung))
            iDiffLadeleistung = iBattLoad-iPower_Bat+iDiffLadeleistung;
            if ((iDiffLadeleistung < 0 )||(abs(iBattLoad)<=100)) iDiffLadeleistung = 0;
            if (iDiffLadeleistung > 100 )iDiffLadeleistung = 100; //Maximal 100W vorhalten
            if (abs(iPower+iDiffLadeleistung) > e3dc_config.maximumLadeleistung) iDiffLadeleistung = 0;
/*            if (iLMStatus == 1) {
                iBattLoad = iPower;
                tE3DC_alt = t;
            ControlLoadData2(frameBuffer,(iBattLoad+iDiffLadeleistung));
            iLMStatus = 7;
            }
*/
// Steuerung direkt über vorgabe der Batterieladeleistung
// -iPower_Bat + int32_t(fPower_Grid)
                if (iLMStatus == 1) {
// Es wird nur Morgens bis zum Winterminimum auf ladeende entladen;
// Danach wird nur bis auf ladeende2 entladen.
                     if ((iPower < 0)&&((t>e3dc_config.winterminimum*3600)&&(fBatt_SOC<e3dc_config.ladeende2)))
                     iPower = 0;
// Wenn der SoC > >e3dc_config.ladeende2 wird mit der Speicher max verfügbaren Leistung entladen
//                    if ((iPower < 0)&&((t>tLadezeitende1)&&(fBatt_SOC>e3dc_config.ladeende2)))
//                 iPower = e3dc_config.maximumLadeleistung*-1;
                 iBattLoad = iPower;
                 tE3DC_alt = t;

                        {
                        if ((iPower<e3dc_config.maximumLadeleistung)&&(iPower > ((iPower_Bat - int32_t(fPower_Grid))/2)))
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
                            if (iPower_PV>0)  // Nur wenn die Sonne scheint
                            {
                                static int iLastReq;
                                if (((iE3DC_Req_Load_alt) >=  (e3dc_config.maximumLadeleistung-1))&&(iE3DC_Req_Load>=(e3dc_config.maximumLadeleistung-1)))
// Wenn der aktuelle Wert >= e3dc_config.maximumLadeleistung-1 ist
// und der zuletzt angeforderte Werte auch >= e3dc_config.maximumLadeleistung-1
// war, bleibt der Freilauf erhalten

                                {   iLMStatus = 3;
                                    if (iLastReq>0)
                                    {sprintf(Log,"CTL %s %0.02f %i %i %0.02f",strtok(asctime(ts),"\n"),fBatt_SOC, iE3DC_Req_Load, iPower_Bat, fPower_Grid);
                                        WriteLog();
                                        iLastReq--;}
                                        }
                                else
                                {
// testweise kein Freilauf
                                    if (iE3DC_Req_Load == e3dc_config.maximumLadeleistung)
                                    {iLMStatus = 3;
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
            sprintf(Log,"CPS %s %0.02f %i %i% 0.02f 0.02f 0.02f", strtok(asctime(ts),"\n"),fBatt_SOC, iE3DC_Req_Load, iPower_Bat, fPower_Grid, fAvPower_Grid600);
            WriteLog();

}
    };
    if (iLMStatus>1) iLMStatus--;
    printf("AVB %0.1f %0.1f ",fAvBatterie,fAvBatterie900);
    printf("DisC %i ",iDischarge);
    if (not bDischarge) printf("halt ");
    printf("BattL %i ",iBattLoad);
    printf("iLMSt %i ",iLMStatus);
    printf("Rsv %0.1f%%\n",fht);
    printf("U %0.0004fkWh td %0.0004fkWh", (fSavedtotal/3600000),(fSavedtoday/3600000));
    if (e3dc_config.wallbox)
    printf(" WB %0.0004fkWh",(fSavedWB/3600000));
    printf(" yd %0.0004fkWh\n",(fSavedyesderday/3600000));

    char buffer [500];
//    sprintf(buffer,"echo $PATH");
//    system(buffer);

    if (e3dc_config.openWB)
    {
//        sprintf(buffer,"mosquitto_pub -r -h raspberrypi -t openWB/set/evu/VPhase1 -m %0.1f",float(223.4));
//        system(buffer);

        
    sprintf(buffer,"openWB/set/evu/W -m %0i",int(fPower_Grid));
    MQTTsend(buffer);

    sprintf(buffer,"openWB/set/pv/W -m %0i",iPower_PV*-1);
    MQTTsend(buffer);

    sprintf(buffer,"echo %0.1f > /var/www/html/openWB/ramdisk/llaktuell",fPower_WB);
//    system(buffer);

    sprintf(buffer,"openWB/set/Housebattery/W -m %0i",iPower_Bat);
    MQTTsend(buffer);

    sprintf(buffer,"openWB/set/Housebattery/%%Soc -m %0i",int(fBatt_SOC));
    MQTTsend(buffer);

    sprintf(buffer,"echo %i > /var/www/html/openWB/ramdisk/hausleistung",iPowerHome);
//    system(buffer);
    }
    return 0;
}
int WBProcess(SRscpFrameBuffer * frameBuffer) {
/*   Steuerung der Wallbox
*/
    const int cMinimumladestand = 15;
    const int iMaxcurrent=31;
    static uint8_t WBChar_alt = 0;
    static int32_t iWBMinimumPower,iAvalPower,iAvalPowerCount,idynPower; // MinimumPower bei 6A
    static bool bWBOn, bWBOff = false; // Wallbox eingeschaltet
    static int32_t iMaxBattLade; // dynnamische maximale Ladeleistung der Batterie, abhängig vom SoC
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

        if ((WBchar[2]==32)||(WBchar[2]==30))
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
    
    if ((e3dc_config.wbmode>0)) // Dose verriegelt, bereit zum Laden
    {
        int iRefload,iPower=0;
// Ermitteln der tatsächlichen maximalen Speicherladeleistung
        if ((fAvPower_Grid < -100)&&(fPower_Grid<-150))
        { if ((iMaxBattLade*.02) > 50)
                iMaxBattLade = iMaxBattLade*.98;
            else iMaxBattLade = iMaxBattLade-50;}
        if (iPower_Bat > iMaxBattLade)
            iMaxBattLade = iPower_Bat;
        if (iMinLade>iFc) iRefload = iFc;
        else iRefload = iMinLade;

//        if (iMaxBattLade < iRefload) // Führt zu Überreaktion
//            iRefload = iMaxBattLade;

        switch (e3dc_config.wbmode)
        {
            case 1:
//              iPower = -fPower_Grid-e3dc_config.einspeiselimit*1000+fPower_WB;
              iPower = -fPower_Grid-e3dc_config.einspeiselimit*1000;
              if (fPower_WB > 1000)
                iPower = iPower+iPower_Bat-iRefload+iWBMinimumPower/6;
              else
                iPower = iPower+iPower_Bat-iRefload+iWBMinimumPower;
 
              if ((iPower <  iWBMinimumPower*-1)&&(WBchar[2] == 6))
                {iPower = -20000;
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
                  {if ((-fPower_Grid > (iWBMinimumPower/6)))
                         iPower = -fPower_Grid;
                     else
                        (iPower = iWBMinimumPower/6);
                  }
                 else
// Überschussleistung verfügbar?
                 { if (abs(iPower_Bat-iBattLoad) > (iWBMinimumPower/6))
                 {if (iBattLoad > iMaxBattLade)
                    iPower = iPower_Bat-iMaxBattLade;
                    else
                    iPower = iPower_Bat-iBattLoad;
                 }}
                }
              break;
            case 3:
                iPower = iPower_Bat-fPower_Grid*2-iRefload;
                idynPower = (iRefload - (fAvBatterie900+fAvBatterie)/2)*-2;
//                idynPower = idynPower- iRefload;
// Wenn das System im Gleichgewicht ist, gleichen iAvalPower und idynPower sich aus
                iPower = iPower + idynPower;
                break;
            case 4:
// Der Leitwert ist iMinLade2 und sollte der gewichteten Speicherladeleistung entsprechen
              if (iRefload > iMinLade2) iRefload = iMinLade2;
              iPower = iPower_Bat-fPower_Grid*3-iRefload;
              idynPower = (iRefload - (fAvBatterie900+fAvBatterie)/2)*-1;
                idynPower = idynPower + e3dc_config.maximumLadeleistung -iBattLoad;
              iPower = iPower + idynPower;
                
              break;
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:

            // Der Leitwert ist iMinLade2 und sollte dem WBminlade
            // des Ladekorridors entprechen

                if ((iRefload > iMinLade2)) iRefload = (iRefload+iMinLade2)/2;
                if (iRefload > iMaxBattLade) iRefload = iMaxBattLade;
// iMaxBattLade ist die maximale tatsächliche mögliche Batterieladeleistung
                iPower = iPower_Bat-fPower_Grid*2-iRefload;
                
                idynPower = (iRefload - (fAvBatterie900+fAvBatterie)/2)*-2;
                iPower = iPower + idynPower;
//              Wenn iRefload > e3dc_config.wbminlade darf weiter entladen werden
//              bis iRefload 90% von e3dc_config.wbminlade erreicht sind
//              es wird mit 0/30/60/90% von e3dc_config.maximumLadeleistung
//              entladen
                
                idynPower = iPower_Bat-fPower_Grid*2 + (iMaxBattLade - iWBMinimumPower/6) * (e3dc_config.wbmode-5)*1/3;
// Berücksichtigung des SoC
                if (fBatt_SOC < 15)
                    idynPower = idynPower*(fBatt_SOC - 5)/10;
// Anhebung der Ladeleistung nur bis Ladezeitende1
                if ((t<tLadezeitende1)&&(iPower<idynPower)&&(iRefload<e3dc_config.wbminlade))
                {
                    if (iRefload<iMaxBattLade)
                     iPower = idynPower;
                    else
                      if (iPower < (iPower_Bat-fPower_Grid))
                          iPower = iPower_Bat-fPower_Grid;
                          }
// Nur bei PV-Ertrag
                if  ((iPower > 0)&&(iPower_PV<100)) iPower = -20000;
// Bei wbmode 9 wird zusätzlich bis zum minimum SoC entladen, auch wenn keine PV verfügbar

                if ((e3dc_config.wbmode ==  9)&&(fBatt_SOC > e3dc_config.wbminSoC))
                {iPower = e3dc_config.maximumLadeleistung*(fBatt_SOC-e3dc_config.wbminSoC)/2;
                 iPower = iPower +(iPower_Bat-fPower_Grid*2);
                 iPower = iPower - e3dc_config.maximumLadeleistung/4;
                    
                }
                if (iPower > (e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid*2))
                    iPower = e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid*2;

                          break;
        }

// im Sonnenmodus nur bei PV-Produktion regeln
        
        
        if (iAvalPowerCount < 3) iAvalPowerCount++;
        iAvalPower = iAvalPower*(iAvalPowerCount-1)/iAvalPowerCount;
        iAvalPower = iAvalPower + iPower/iAvalPowerCount;

        if ((iAvalPower>0)&&bWBLademodus&&iPower_PV<100)
            iAvalPower = 0;

        
        if (iAvalPower > (e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid))
              iAvalPower = e3dc_config.maximumLadeleistung*.9+iPower_Bat-fPower_Grid;
        // Speicher nur bis 5-7% entladen
        if ((fBatt_SOC < 5)&&(iPower_Bat<0)) iAvalPower = iPower_Bat-fPower_Grid - iWBMinimumPower/6-fPower_WB;
//        else if (fBatt_SOC < 20) iAvalPower = iAvalPower + iPower_Bat-fPower_Grid;
        if (iAvalPower < (-iMaxBattLade+iPower_Bat-fPower_Grid-fPower_WB))
            iAvalPower = -iMaxBattLade+iPower_Bat-fPower_Grid-fPower_WB;

        if (e3dc_config.wbmode==1) iAvalPower = iPower;
        
        
//        if ((iWBStatus == 1)&&(bWBConnect)) // Dose verriegelt
        if (iWBStatus == 1) // 
        {
// Wenn bWBZeitsteuerung erfolgt die Ladungsfreigabe nach ch = chargehours ermittelten Stunden
            struct tm * ptm;
            ptm = gmtime(&tE3DC);

            if ((not(bWBZeitsteuerung))&&(bWBConnect)) // Zeitsteuerung nicht + aktiv + wenn Auto angesteckt
            {
                for (int j = 0; j < ch.size(); j++ )
                    if ((ch[j].hh% (24*3600)/3600)==hh){
                        bWBZeitsteuerung = true;
                    };
                if ((bWBZeitsteuerung)&&(bWBConnect)){
                    bWBmaxLadestromSave = bWBmaxLadestrom;
                    WBchar6[0] = 2;            // Netzmodus
                    if (not(bWBmaxLadestrom))
                    {
                        bWBmaxLadestrom = true;
                        WBchar6[1] = 32;
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
            }else   // Das Ladefenster ist offen Überwachen, wann es sich wieder schließt
            {
                bWBZeitsteuerung = false;
                for (int j = 0; j < ch.size(); j++ )
                    if ((ch[j].hh% (24*3600)/3600)==hh){
                        bWBZeitsteuerung = true;
                    };
                if ((not(bWBZeitsteuerung))||not bWBConnect){    // Ausschalten
                    if ((bWBmaxLadestrom!=bWBmaxLadestromSave)||(bWBLademodus != bWBLademodusSave))
                    {bWBmaxLadestrom=bWBmaxLadestromSave;  //vorherigen Zustand wiederherstellen
                    bWBLademodus = bWBLademodusSave;
                    if (bWBLademodus)
                    WBchar6[0] = 1;            // Sonnenmodus
                    if (not(bWBmaxLadestrom)){
                        WBchar6[1] = 31;
                    } else WBchar6[1] = 32;

                    if (bWBCharge)
                    WBchar6[4] = 1; // Laden stoppen
                    createRequestWBData(frameBuffer);  // Laden stoppen und/oeder Modi ändern
                    WBchar6[4] = 0; // Toggle aus
                    iWBStatus = 30;
                    return(0);
                    } else
                    if (bWBCharge)                     // Laden stoppen
                    {WBchar6[4] = 1; // Laden stoppen
                    createRequestWBData(frameBuffer);
                    WBchar6[4] = 0; // Toggle aus
                    iWBStatus = 30;
                    return(0);
                    }
                }

            };
            
            
            if (bWBmaxLadestrom)  {//Wenn der Ladestrom auf 32, dann erfolgt keine
            if ((fBatt_SOC>cMinimumladestand)&&(fAvPower_Grid<400)) {
//Wenn der Ladestrom auf 32, dann erfolgt keine Begrenzung des Ladestroms im Sonnenmodus
            if ((WBchar6[1]<32)&&(fBatt_SOC>(cMinimumladestand+2))) {
                WBchar6[1]=32;
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
            
        if ( (fPower_WB == 0) &&bWBLademodus)
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
            if (WBchar6[1]==6) iWBMinimumPower = fPower_WB;
            else
                if ((iWBMinimumPower == 0) ||
                    (iWBMinimumPower < (fPower_WB/WBchar6[1]*6) ))
                     iWBMinimumPower = (fPower_WB/WBchar6[1])*6;
            if  ((iAvalPower>=(iWBMinimumPower/6))&&
                (WBchar6[1]<iMaxcurrent)){
                WBchar6[1]++;
                for (int X1 = 3; X1 < 20; X1++)
                    
                if ((iAvalPower > (X1*iWBMinimumPower/6)) && (WBchar6[1]<iMaxcurrent)) WBchar6[1]++; else break;
                WBchar[2] = WBchar6[1];
                createRequestWBData(frameBuffer);
                WBChar_alt = WBchar6[1];

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
                || (iAvalPower < (e3dc_config.maximumLadeleistung-fAvPower_Grid)*-1)
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
                    iWBStatus = 20;  // Warten bis Neustart
                }}
    }
        }}
    printf("\nAVal %0i/%01i Power %0i WBMode %0i ", iAvalPower,iMaxBattLade,iWBMinimumPower, e3dc_config.wbmode);
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
        if (iLMStatus < 0)
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
        protocol.appendValue(&WBContainer, TAG_WB_INDEX, (uint8_t)0);
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
if (e3dc_config.wallbox)
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
    printf("\nRequest cyclic example data done %s %2ld:%2ld:%2ld",VERSION,tm_CONF_dt%(24*3600)/3600,tm_CONF_dt%3600/60,tm_CONF_dt%60);

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
        printf("\nEMS PV %i", iPower);
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
        printf(" # %i\n", iPower_PV - iPower_Bat + iPower - int(fPower_WB));
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
                float fVoltage = protocol->getValueAsFloat32(&batteryData[i]);
                printf(" %0.1f V ", fVoltage);
                break;
            }
            case TAG_BAT_CURRENT: {    // response for TAG_BAT_REQ_CURRENT
                float fVoltage = protocol->getValueAsFloat32(&batteryData[i]);
                printf(" %0.1f A\n", fVoltage);
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
                        printf(" # %0.1f W \n", fPower1+fPower2+fPower3);
                        }
                        if (ucPMIndex==e3dc_config.wurzelzaehler) {
                                sprintf(buffer,"openWB/set/evu/APhase1 -m %0.1f",float(fPower1/fL1V));
                                MQTTsend(buffer);
                            sprintf(buffer,"openWB/set/evu/APhase2 -m %0.1f",float(fPower2/fL2V));
                            MQTTsend(buffer);
                            sprintf(buffer,"openWB/set/evu/APhase3 -m %0.1f",float(fPower3/fL3V));
                            MQTTsend(buffer);

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
                        MQTTsend(buffer);

                        break;
                    }
                    case TAG_PM_VOLTAGE_L2: {              // response for TAG_PM_REQ_L2
                        float fPower = protocol->getValueAsFloat32(&PMData[i]);
                        printf(" %0.1f V", fPower);
                        fL2V = fPower;
                        sprintf(buffer,"openWB/set/evu/VPhase2 -m %0.1f",float(fPower));
                        MQTTsend(buffer);
                        break;
                    }
                    case TAG_PM_VOLTAGE_L3: {              // response for TAG_PM_REQ_L3
                        float fPower = protocol->getValueAsFloat32(&PMData[i]);
                        printf(" %0.1f V\n", fPower);
                        fL3V = fPower;
                        sprintf(buffer,"openWB/set/evu/VPhase3 -m %0.1f",float(fPower));
                        MQTTsend(buffer);
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
                                                    if (index == 0) printf("\n");
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
                                    printf(" \nWB: Modus %02X ",uint8_t(cWBALG));
//                                    for(size_t x = 0; x < sizeof(WBchar); ++x)
//                                        printf("%02X ", uint8_t(WBchar[x]));
                                    if (bWBLademodus) printf("Sonne "); else printf("Netz: ");
                                    if (bWBConnect) printf(" Dose verriegelt");
                                    if (bWBStart) printf(" gestartet");
                                    if (bWBCharge) printf(" lädt");
                                    if (bWBStopped ) printf(" gestoppt");

//                                    if ((WBchar[2]==32)&&(iWBSoll!=32)) {
                                    if (WBchar[2]==32) {
                                        bWBmaxLadestrom=true;
                                    }
//                                    if ((WBchar[2]==30)&&(iWBSoll!=30)) {
//                                        bWBmaxLadestrom=true;
//                                    }
//                                if  ((WBchar[2]==31)&&(iWBSoll!=31)) {
                                    if  (WBchar[2]==31) {
                                        bWBmaxLadestrom=false;
                                    }
                                    if ((int(WBchar[2])!=iWBIst)&&(iWBStatus==1))
                                    if ((not bWBmaxLadestrom)&&(int(WBchar[2])>iWBSoll))
                                    {
// ladeschwelle ändern 8..9

                                        
                                        if  (WBchar[2]==8)
                                        GetConfig();
                                        if  (WBchar[2]==9)
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
                                        

                                        }
                                
                                    iWBIst = WBchar[2];
                                    if (bWBmaxLadestrom) printf(" Manu");
                                    else printf(" Auto");
                                    printf(" Ladestrom %u/%uA ",iWBSoll,WBchar[2]);
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
            if (e3dc_config.aWATTar)
            aWATTar(ch); // im Master nicht aufrufen
            if((frameBuffer.dataLength == 0)&&(e3dc_config.wallbox)&&(bWBRequest))
            WBProcess(&frameBuffer);
            
            if(frameBuffer.dataLength == 0)
                 bWBRequest = true;
            else
                 bWBRequest = false;
            
        if(frameBuffer.dataLength == 0)
            LoadDataProcess(&frameBuffer);
//            sleep(1);
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
 struct tm * ptm;

    // endless application which re-connections to server on connection lost
    system("pwd");
    if (GetConfig())
        while(iEC < 10)
    {
        iEC++; // Schleifenzähler erhöhen
        ptm = gmtime(&t);
//      Berechne Sonnenaufgang-/untergang
        SunriseCalc *location = new SunriseCalc(e3dc_config.hoehe, e3dc_config.laenge, 0);
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

//
//  aWATTar.hpp
//  RscpExcample
//
//  Created by Eberhard Mayer on 26.11.21.
//  Copyright © 2021 Eberhard Mayer. All rights reserved.
// 

#ifndef awattar_hpp
#define awattar_hpp

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <iostream>
#include <string>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <array>
#include <cctype>
#include <time.h>
#include <array>
#include <algorithm>
#include <vector>


// #include "Waermepumpe.hpp"


#endif /* aWATTar_hpp */
typedef struct {
    char server_ip[20];
    char heizstab_ip[20]; // Heizstab
    char heizung_ip[20];  // oekofen
    char mqtt_ip[20],mqtt2_ip[20],mqtt3_ip[20],mqtt4_ip[20],mqtt4_topic[20];  // MQTT
    char BWWP_ip[20],WB_ip[20],WB_topic[30];
    char shelly0V10V_ip[20];
    char shellyEM_ip[20];
    uint32_t  server_port,heizstab_port,BWWP_port;
    char e3dc_user[128];
    char e3dc_password[128];
    char aes_password[128];
    char logfile[128],conffile[128];
    char openWB_ip[20];
    char openWB_topic[20];
    char openweathermap[50];
    char BWWPTasmota[50];
    char e3dcwallboxtxt[128];
    char analyse[128];
    bool ext1,ext2,ext3,ext4,ext7,debug,htsat,htsun,openWB,shelly0V10V,tasmota,WP,WPWolf,WPSperre,DCDC,openmeteo,statistik;
    uint8_t wurzelzaehler,ladeschwelle, ladeende,ladeende2,  AWtest,aWATTar,wbmaxladestrom,wbminladestrom,wrsteuerung,stop,test;
    int32_t ht, unload,untererLadekorridor, obererLadekorridor, minimumLadeleistung, maximumLadeleistung, wrleistung,peakshave,peakshaveuppersoc,peakshavepvcharge,wbtest,wbmode,wbminlade,wbhour,wbvon,wbbis;
    int32_t wallbox,BWWP_Power,AWLand,AWSimulation,soc,MQTTavl,shelly0V10Vmin,shelly0V10Vmax;
    float_t RB,RE,LE,speichergroesse,winterminimum, sommermaximum,sommerladeende, einspeiselimit,powerfaktor,peakshavesoc,ladeende2rampe,
    hton, htoff, htsockel, wbminSoC, hoehe, laenge, Avhourly, AWDiff, AWAufschlag,AWNebenkosten, AWMWSt,AWReserve,
    WPHeizlast,WPHeizgrenze,WPLeistung,WPmin,WPmax,WPPVon,WPPVoff,WPEHZ,WPZWE,WPZWEPVon,WPHK1,WPHK1max,WPHK2on,WPHK2off,WPOffset,BWWPein,BWWPaus,BWWPon,BWWPoff,BWWPmax, BWWPSupport,BWWPTasmotaDauer,ForcecastSoc,ForcecastConsumption,ForcecastReserve;
    char Forecast[4][20]; // 4 Elemente

    
}e3dc_config_t;

// central information for dyn. price, consumption and solar production
// update when new priceinformation is avaiable (once a day) or consumption/production (hourly)
typedef struct {time_t hh; float pp; float hourly; float pn;}watt_s;
// weather information for the next 48h
// temp: prog. Tagestemperatur
// hourly: Grundverbrauch aus Statistik in % der Speichergröße
// kosten: elektrische Leistung in kW
// solar: hochgerechnete Solarleistung in % der Speichergröße
// progsolar: Prognostizierte Solarleistung aus openmeteo in % der Speichergröße
// wpbedarf: Wärmebedarf in % der Speichergröße
// waerme: erforderliche Wärmeleistung in kW
// cop: Effizienz WP
typedef struct {time_t hh; float temp; int sky; float uvi;float hourly;float kosten;float solar;float progsolar;
    float wpbedarf;float wwwpbedarf;float heizstabbedarf;float waerme;float waermepreis;float cop;}wetter_s;
// information for the wolf heatpump
typedef struct {time_t t; std::string feld; std::string AK; std::string status; float wert;}wolf_s;
// central information for automation depending on price and for various channels
// 0 = wallbox, 1 = bwwptasmota
// hh is starttime for one full hour = 3600sec
// update when new priceinformation is avaiable (once a day) or on request
typedef struct {time_t hh; int ch; float pp;}ch_s;
typedef struct {int x1; float waermepreis;float cop;}wetter1_s;
typedef struct {uint32_t verbrauch; uint32_t wp;}stat_s;
typedef struct {float fgrid; float fsoc; float fbat;}farm_s;
typedef struct {int stunde; float strompreis;}strompreis_s;  //variable strompreistarife

static float fatemp,fcop;
static int heizbegin;
static int heizende;
static int sunriseAt,sunriseWSW;  // Sonnenaufgang, Wintersonnenwende
static int sunsetAt;   // Sonnenuntergang

static int tasmota_status[4]={2,2,2,2};

static std::vector<watt_s> w; // Stundenwerte der Börsenstrompreise
static std::vector<wetter_s>wetter; // Stundenwerte der Wetterprognose
static std::vector<wolf_s>wolf; // Werte der Wolf WP
static std::vector<strompreis_s>strompreis; // Werte der variable Strompreistarife

static int32_t iHeatStat[24*4+2]; //15min WP Heizleistung der letzten 24h

void mewp(std::vector<watt_s> &w,std::vector<wetter_s>&wetter,float &fatemp,float &cop,int sunrise, int sunset,e3dc_config_t &e3dc, float soc, int ireq_Heistab, float zuluft, float notromreserve,int32_t HeatStat);
void aWATTar(std::vector<ch_s> &ch,std::vector<watt_s> &w,std::vector<wetter_s> &wetter, e3dc_config_t &e3dc,float soc,float notstromreserve, int sunriseAt,u_int32_t iDayStat[25*4*2+1]);
int SimuWATTar(std::vector<watt_s> &w, std::vector<wetter_s> &wetter,int h, float &fSoC,float &anforderung, float Diff,float aufschlag, float reserve, float notstromreserve, float ladeleistung);
int CheckaWATTar(std::vector<watt_s> &w,std::vector<wetter_s> &wetter, float fSoC,float fmaxSoC,float fConsumption,float Diff,float aufschlag, float ladeleistung,int mode,float &fstrompreis, float reserve, float notstromreserve);
bool GetWallbox(std::vector<ch_s> &ch);
bool PutWallbox(std::vector<ch_s> &ch);

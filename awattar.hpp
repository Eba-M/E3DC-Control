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
    char mqtt_ip[20];  // MQTT
    char BWWP_ip[20];
    uint32_t  server_port,heizstab_port,BWWP_port;
    char e3dc_user[128];
    char e3dc_password[128];
    char aes_password[128];
    char logfile[128],conffile[128];
    char openWB_ip[20];
    char openweathermap[50];
    bool ext1,ext2,ext3,ext4,ext7,debug,htsat,htsun,openWB,WP;
    uint8_t wurzelzaehler,ladeschwelle, ladeende,ladeende2, unload, AWtest,aWATTar,wbmaxladestrom;
    int32_t ht, untererLadekorridor, obererLadekorridor, minimumLadeleistung, maximumLadeleistung, wrleistung,peakshave,peakshsoc,wbmode,wbminlade;
    int32_t wallbox,BWWP_Power,AWLand,AWTagoffset,soc;
    float_t RB,RE,LE,speichergroesse,winterminimum, sommermaximum,sommerladeende, einspeiselimit,powerfaktor,
    hton, htoff, htsockel, wbminSoC, hoehe, laenge, Avhourly, AWDiff, AWAufschlag,AWNebenkosten, AWMWSt,
    WPHeizlast,WPHeizgrenze,WPLeistung,WPmin,WPmax,WPZWE,BWWPein,BWWPaus;
    char Forecast[4][20]; // 4 Elemente

    
}e3dc_config_t;


typedef struct {time_t hh; float pp; float hourly; float wpbedarf;float solar;}watt_s;
typedef struct {time_t hh; float temp; int sky; float uvi;float kosten;}wetter_s;
typedef struct {time_t t; std::string feld; std::string AK; float wert;}wolf_s;
static float fatemp,fcop;
static int heizbegin;
static int heizende;
static int tasmota_status[4]={2,2,2,2};

static std::vector<watt_s> w; // Stundenwerte der Börsenstrompreise
static std::vector<wetter_s>wetter; // Stundenwerte der Börsenstrompreise
static std::vector<wolf_s>wolf; // Stundenwerte der Börsenstrompreise

void mewp(std::vector<watt_s> &w,std::vector<wetter_s>&wetter,float &fatemp,float &cop,int sunrise, e3dc_config_t &e3dc, float soc);
void aWATTar(std::vector<watt_s> &ch,std::vector<watt_s> &w, e3dc_config_t &e3dc,float soc);
int SimuWATTar(std::vector<watt_s> &w, int h, float &fSoC,float anforderung, float Diff,float aufschlag, float ladeleistung);
int CheckaWATTar(std::vector<watt_s> &w,int sunrise,int sunset,int sunriseWSW, float fSoC,float fmaxSoC,float fConsumption,float Diff,float aufschlag, float ladeleistung,int mode,float &fstrompreis, int Wintertag);


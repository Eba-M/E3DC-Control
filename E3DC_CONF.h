//
//  E3DC_CONF.h
//  RscpExcample
//
//  Created by Eberhard Mayer on 16.08.18.
//  Copyright © 2018 Eberhard Mayer. All rights reserved.
//
#define VERSION "C2023.11.05.2" //aktuelle Version für dynamische Tarife und WP-Betrieb
#ifndef E3DC_CONF_h
#define E3DC_CONF_h


#endif /* E3DC_CONF_h */
// Konfigurationsdatei
#define CONF_FILE "e3dc.config.txt"
#define WURZELZAEHLER 0;     // 0 = interner Zähler 6 = externer Zähler

#define LADESCHWELLE 50;     // bis zur dieser Schwelle wird geladen bevor die Regelung beginnt
#define LADEENDE 80;         // Zielwert bis Ende Regelung, dannach wird Ladung auf Landeende2 weiter geregelt und dann ab SOMMERLADEENDE freigegeben
#define LADEENDE2 93;
#define UNTERERLADEKORRIDOR  100 // die Ladeleistung soll zwischen dem unteren und
#define OBERERLADEKORRIDOR  2000 // oberere Ladeleistung liegen, jedoch
#define MINIMUMLADELEISTUNG  100 // immer > MINIMUMLADELEISTUNG
#define POWERFAKTOR          -1  // Verstärkungsfaktor 
#define MAXIMUMLADELEISTUNG 3000 // maximale Ladeleistung
#define WRLEISTUNG 12000 // maximale Ladeleistung
#define WBMAXLADESTROM 32 // maximale Ladestrom der Wallbox

#define SPEICHERGROESSE 13.8 // nutzbare Kapazität des S10 Speichers
#define WINTERMINIMUM   11.5 // Uhrzeit (als Dezimalwert) bis zu dieser Uhrzeit wird das Laden überwacht
#define SOMMERMAXIMUM   14.5 // alle Zeiten in GMT = MEZ Winterzeit - 1
#define SOMMERLADEENDE  18.5 // alle Zeiten in GMT = MEZ Winterzeit - 1
#define EINSPEISELIMIT   30  // maximal erlaubte Einspeiseleistung in kW


//const int cLadeschwelle = LADESCHWELLE; // Minimum Lade-Schwelle wird bevorzugt der E3DC-Speicher geladen
//const int cLadeende = LADEENDE;     // Lade-Schwelle des überwachten Ladens
/*
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
    std::string Forcast[3]; // 4 Elemente
    bool ext1,ext2,ext3,ext4,ext7,debug,htsat,htsun,openWB,WP;
    uint8_t wurzelzaehler,ladeschwelle, ladeende,ladeende2, unload, AWtest,aWATTar,wbmaxladestrom;
    int32_t ht, untererLadekorridor, obererLadekorridor, minimumLadeleistung, maximumLadeleistung, wrleistung,peakshave,peakshsoc,wbmode,wbminlade;
    int32_t wallbox,BWWP_Power,AWLand,AWTagoffset,soc;
    float_t RB,RE,LE,speichergroesse,winterminimum, sommermaximum,sommerladeende, einspeiselimit,powerfaktor,
    hton, htoff, htsockel, wbminSoC, hoehe, laenge, Avhourly, AWDiff, AWAufschlag,AWNebenkosten, AWMWSt,
    WPHeizlast,WPHeizgrenze,WPLeistung;
    
    
}e3dc_config_t;
*/

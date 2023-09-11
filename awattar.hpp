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
#include <errno.h>
#include <vector>

#endif /* aWATTar_hpp */

typedef struct {time_t hh; float pp;}watt_s;
// std::vector<watt_s> ch;    // charge hour
static std::vector<watt_s> w; // Stundenwerte der Börsenstrompreise


void aWATTar(std::vector<watt_s> &ch, int32_t Land, int MWSt, float Nebenkosten);
int CheckaWATTar(int sunrise,int sunset,int sunriseWSW, float fSoC,float fmaxSoC,float fConsumption,float Diff,float aufschlag, float ladeleistung,int mode,float &fstrompreis, int Wintertag);
// fConsumption Verbrauch in % SoC Differenz Laden/Endladen


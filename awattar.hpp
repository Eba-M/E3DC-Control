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
// std::vector<watt_s> ch;  //charge hour


void aWATTar(std::vector<watt_s> &ch);
int CheckaWATTar(int sunrise,int sunset,float fSoC,float fConsumption,float Diff);
// fConsumption Verbrauch in % SoC Differenz Laden/Endladen


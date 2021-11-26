//
//  SunriseCalc.cpp
//  RscpExcample
//
//  Created by Eberhard Mayer on 20.11.21.
//  Copyright © 2021 Eberhard Mayer. All rights reserved.
//

#include "SunriseCalc.hpp"

#ifndef deg2rad
#define deg2rad(x) (3.14159 * (x) / 180.0)
#endif
#ifndef rad2deg
#define rad2deg(x) (180.0 * (x) / 3.14159)
#endif

SunriseCalc::SunriseCalc(float lat, float lon, float tz) : lat(lat), lon(lon), tz(tz), calculated(false)
{
}

void SunriseCalc::date(int y, int m, int d, bool dst)
{
  year = y;
  month = m;
  day = d;
  isDST = dst;

  calculated = false;
}

int SunriseCalc::sunrise()
{
  if (!calculated)
    updateCalc();
  return sunriseLST * 1440 + (isDST ? 60 : 0);
}

int SunriseCalc::sunset()
{
  if (!calculated)
    updateCalc();
  return sunsetLST * 1440 + (isDST ? 60 : 0);
}

float SunriseCalc::toJulian(int y, int m, int d)
{
  float baseDay = (((1461 * (y + 4800 + (m - 14)/12))/4 + (367 * (m - 2 - 12 * ((m - 14)/12)))/12 - (3 * ((y + 4900 + (m - 14)/12)/100))/4 + d - 32075));
  // Julian days start at noon. We want midnight, so offset back to
  // midnight; and then adjust for local time zone.
  return baseDay - 0.5 - ((tz)/24.0);
}

void SunriseCalc::updateCalc()
{
  float julian = toJulian(year, month, day);

  float julianCentury = (julian - 2451545.0) / 36525;

  float geomMeanLongSun = fmod(280.46646 + julianCentury * (36000.76983 + julianCentury * 0.0003032), 360.0);
  float geomMeanAnomSun = 357.52911 + julianCentury * (35999.05029 - 0.0001537 * julianCentury);
  float eccentEarthOrbit = 0.016708634-julianCentury*(0.000042037+0.0000001267*julianCentury);
  float meanObliqCorr = 23+(26+((21.448-julianCentury*(46.815+julianCentury*(0.00059-julianCentury*0.001813))))/60)/60;
  float obliqCorr = meanObliqCorr+0.00256*cos(deg2rad(125.04-1934.136*julianCentury));
  float varY = tan(deg2rad(obliqCorr/2))*tan(deg2rad(obliqCorr/2));
  float eqOfTime = 4*rad2deg(varY*sin(2*deg2rad(geomMeanLongSun))-2*eccentEarthOrbit*sin(deg2rad(geomMeanAnomSun))+4*eccentEarthOrbit*varY*sin(deg2rad(geomMeanAnomSun))*cos(2*deg2rad(geomMeanLongSun))-0.5*varY*varY*sin(4*deg2rad(geomMeanLongSun))-1.25*eccentEarthOrbit*eccentEarthOrbit*sin(2*deg2rad(geomMeanAnomSun)));

  solarNoonLST = ((720-4*lon-eqOfTime+(tz)*60)/1440);

  float sunEqOfCtr = sin(deg2rad(geomMeanAnomSun))*(1.914602-julianCentury*(0.004817+0.000014*julianCentury))+sin(deg2rad(2*geomMeanAnomSun))*(0.019993-0.000101*julianCentury)+sin(deg2rad(3*geomMeanAnomSun))*0.000289;
  float sunTrueLon = geomMeanLongSun + sunEqOfCtr;
  float sunAppLon = sunTrueLon-0.00569-0.00478*sin(deg2rad(125.04-1934.136*julianCentury));
  float sunDecl = rad2deg(asin(sin(deg2rad(obliqCorr))*sin(deg2rad(sunAppLon))));
  float haSunrise = rad2deg(acos(cos(deg2rad(90.833))/(cos(deg2rad(lat))*cos(deg2rad(sunDecl)))-tan(deg2rad(lat))*tan(deg2rad(sunDecl))));

  sunriseLST = (solarNoonLST * 1440 - haSunrise*4)/1440;
  sunsetLST = (solarNoonLST * 1440 + haSunrise*4) / 1440;

  calculated = true;
}

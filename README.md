# E3DC-Control

Viele haben bemängelt, das der Speicher von E3DC über keine ausreichende und funktionsfähige Steuerung zur prognosebasierende Laden verfügt.
Ich habe nun schon seit mehr als ein Jahr, meine Software auf einem headless Raspberry Pi Zero laufen.
nachdem ich nun mehrfach um meine Software gebeten wurde, habe beschlossen, diese über Github zu veröffentlichen.

Ich kann natürlich keinerlei Haftung für Funktion der Software, Wartung etc. übernehmen. Jeder ist natürlich eingeladen, die Software zu erweitern und zu verbessern und diese auf eigene Gefahr und Risiko einzusetzen.

Hier findet Ihr meine erste Version.

git clone https://github.com/Eba-M/E3DC-Control.git

Ich bin auch gerade erst dabei mich in Github einzuarbeiten, erwartet bitte keine perfekte Dokumentation und Anleitung.
Diese wird sicherlich im Lauf des Projektes noch verbessert und erweitern werden müssen.



viel Spass beim Ausprobieren

Eberhard

Diese Programm ist gedacht, auf einem Raspberry Pi ständig laufen zu lassen.
Bei mir läuft das Programm auf einen headless Raspberry Pi Zero W.
Das bedeutet, ich habe keinen Monitor oder Tastatur angeschlossen.
Die gesamte Steuerung/Überwachung und Bedienung erfolgt remote per ssh von meinen MAC aus.
Ich lasse das Programm in einer Session innerhalb von Screen laufen.

Als Basis dient das von E3DC veröffentliche RSCP-Beispielprogramm. Der Speicher wird morgens mit maximaler Ladeleistung auf ein mittleres SoC geladen. Dann wird die Batterie innerhalb eines Ladekorridor bis zum geplanten Ladeende auf 90% geladen. Das Ladeende wird für Winterminimum und Sommermaximum definiert und das Programm ermittelt für jeden Tag dazwischen das entsprechende Ende der Überwachungsladung. Daneben wird auch die Überschussleistung ermittelt und nötigenfalls die Ladeleistung hochgeregelt um unter Einspeisegrenze zu bleiben.

HT = Hochtarifoptimierung. Mit den Parametern z.B. "hton = 5" und "htoff = 21" wird der Beginn und die Endzeit des Hochtarif von den Wochentagen Mo.-Fr. in GMT festgelegt, hier im Beispiel von 6Uhr bis 22 Uhr. Über "htmin = 50" wird festgelegt, dass diese Regelung erst bei einem Soc des Speichers von < 50% wirksam wird.
So wird sichergestellt, das bei HT/NT Tarifen der Speicher möglichst im Hochtarif ausspeichert. Sonst wird der Speicher über Nacht bei NT entleert und morgens bei Beginn des HT ist dann keine Ladung mehr im Speicher verfügbar.
Die Speichergröße fortlaufend wird zwischen dem kürzesten Tag (ht=50) und Tag-/Nachtgleiche 0% mittels einer Cosinusfunktion verändert.  

Nun noch einige Hinweise um das Programm auf den Raspberry Pi zu instllieren und dort zu nutzen

Wenn man einen Raspberry Pi Zero W headless nutzen möchte, findet man hier hinweise zu ssh over usb

https://desertbot.io/blog/ssh-into-pi-zero-over-usb

Den Raspberry PI einrichten und Betrieb


# WLAN SCHON VOR DER INBETRIEBNAHME KONFIGURIEREN

Mitunter ist es praktisch, wenn Sie einen Raspberry Pi auf Anhieb über das WLAN via SSH bedienen können. Das gibt Ihnen die Möglichkeit, ohne angeschlossene Maus und Tastatur mit der Konfiguration zu beginnen — zumindest soweit, wie Sie dies via SSH im Textmodus durchführen können.
Die leere Datei ssh bewirkt, dass der SSH-Dienst sofort aktiviert wird. (Bei aktuellen Raspbian-Versionen ist dies ja nicht mehr der Fall.) 
Und die Datei wpa_supplicant.conf enthält die WLAN-Konfiguration. Sie wird beim ersten Start des Raspberry Pi in das Verzeichnis /etc/wpa_supplicant kopiert. Die Datei muss die Bezeichnung des WLANs (SSID) und dessen Passwort enthalten. Dabei gilt dieser Aufbau. 

Datei wpa_supplicant.conf in der Boot-Partition (Raspbian Stretch)

country=DE
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1
network={
       ssid="wlan-bezeichnung"
       psk="passwort"
       key_mgmt=WPA-PSK
}

Damit kann der Raspberry auf das Hausnetz zugreifen

Sobald der Raspberry Pi hochgefahren ist, können Sie sich mit ssh pi@raspberrypi und dem Default-Passwort raspberry einloggen. Anschließend müssen Sie sofort mit sudo passwd pi ein neues Passwort für den Benutzer pi einrichten! Ein aktiver SSH-Server in Kombination mit dem Default-Passwort ist ein großes Sicherheitsrisiko!
// Installieren git

`sudo apt install git`

`git clone  https://github.com/Eba-M/E3DC-Control.git`

// updates runterladen (nur erforderlich, wenn ein neues Updat installiert werden soll

cd E3DC-Control  // Nach E3DC wechseln und mit pull runterladen

git pull  https://github.com/Eba-M/E3DC-Control.git`

// nach clone oder pull  mit make kompilieren

cd E3DC-Control

make

// mit nano die Datei

nano e3dc.config.txt.template

// editieren und die Parameter

server_ip = xxx.xxx.xxx.xxx
server_port = 5033
e3dc_user = xxxxxxxxxx
e3dc_password = xxxxxxxx

// und die möglichen Parameter

wallbox = false         
// true, wenn man die E3DC-Wallbox mit erweiterter Funktion nutzen möchte
ext1 = false				
// true, wenn ein externer Zähler genutzt wird
ext2 = false
wurzelzaehler = 0		
// 6 = externer Wurzelzähler
einspeiselimit = 7.0 
// 70% Einspeisegrenze bei z.B. 10kWp
untererLadekorridor = 500  
obererLadekorridor = 1500  
// bei der PRO wird 4500 empfohlen
minimumLadeleistung = 300
maximumLadeleistung = 3000  
// 1500 bei mini, 3000 E12 und 9000/1200 PRO
wrleistung = 12000          
// AC-Leistung des WR, 4600 bei mini
ladeschwelle = 15           
// Unter 15% SoC wird immer geladen 
ladeende = 85               
// Ziel SoC 85% zwischen
winterminimum = 11		   
// winterminimum wintersonnenwende
sommermaximum = 14           
// sommermaximum sommersonnenwende
sommerladeende = 18.5     
// im Sommer wird das Laden auf 100% verzögert
// Im Winterhalbjahr wird versucht den Speicher zum Hochtarif zu nutzen
htmin = 30                
// Speicherreserve 30% bei winterminimum
htsockel = 10             
// sockelwert bei Tag-Nachtgleiche
hton = 5                  
// Begin Hochtarif
htoff = 14                
// Ende Hochtarif 
htsat = true              
// Hochtarif Samstag
htsun = true              
// Hochtourig Sonntag
debug = false
logfile = logfile
// zusätzliche debug informationenin eine logfile ausgeben
// Alle Steuerungsanweisungen an E3DC

// anpassen und als 
//`e3dc.config.txt`
// abspeichern.


// Installieren von Screen

`sudo apt-get install screen`


// Skriptdatei erstellen

nano E3DC.sh

// folgende kopieren

#!/bin/bash

cd /home/pi/E3DC-Control
while true;
 do
./E3DC-Control
sleep 30
done
```
// Skriptdatei ausführbar machen

`chmod +x E3DC.sh`

// Ausführen

`./E3DC.sh`

// Eintrag in die autostart datei /etc/rc.local


sudo nano /etc/rc.local

wird folgende Zeile vor dem „exit 0“ eingetragen

``` 
su  pi -c "screen -dmS E3DC /home/pi/E3DC-Control/E3DC.sh"

exit 0
```


Richtig herunterfahren

`sudo shutdown -h 0`

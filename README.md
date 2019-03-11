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

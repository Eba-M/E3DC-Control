<?php
$datei = "/home/pi/E3DC-Control/e3dc.wallbox.txt";
$zeile = "1";

if (file_exists($datei))
{
   $myfile = fopen($datei,"r") ;
   $zeile = fgets($myfile);
   if  (! feof($myfile))     $zeile2 = fgets($myfile);
     fclose($myfile);
}


#if (empty($zeile))
#echo "is empty ";

#if ($zeile<"1")
#echo "is < 1";;
//if ((empty($zeile))||(($zeile)=="0"))
if ($zeile<"1")
{
        echo "es waren keine Ladezeiten geplant<br>";
}
else
{
        if ($zeile=="1")
                echo "Die aktuelle  Ladedauer beträgt eine  Stunde<br>";
        if ($zeile > "1")
                echo "Die aktuelle  Ladedauer beträgt " . $zeile .  " Stunden<br>";
        if (! empty($zeile2))
                echo "die bisher geplante Ladezeiten waren um " . $zeile2 ." Börsenpreis;<br>";
}
?>
<html>
 <head>
  <title>PHP-Test</title>
 </head>
 <body>
<form action="e3dc.php"  method="post">
    <p><input type="text" name="zwei" value=<?=$zeile?> minlength="1" maxlength="2" size="2">  Stunden Ladedauer </p>
    <p><input type="submit"></p>
</form>

 </body>
</html>


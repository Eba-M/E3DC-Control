
<?php
$datei1 = "/home/pi/E3DC-Control/e3dc.wallbox.txt";
$datei2 = "e3dc.wallbox.txt";
if (file_exists($datei2))
$datei = $datei2;
elseif (file_exists($datei1))
$datei = $datei1;

$zeile = "1";

if (file_exists($datei))
{
   $myfile = fopen($datei,"r") ;
   $zeile = fgets($myfile);
   if  (! feof($myfile))     $zeile2 = fgets($myfile);
#     fclose($myfile);
}



//if ((empty($zeile))||(($zeile)=="0"))
if ($zeile<"1")
{
        echo "es waren keine Ladezeiten geplant<br>";
}
else
{
        if ($zeile<"2")
                echo "Die aktuelle  Ladedauer beträgt eine  Stunde<br>";
        if ($zeile >= "2")
                echo "Die aktuelle  Ladedauer beträgt " . $zeile .  " Stunden<br>";
        if (! empty($zeile2))
                echo "die bisher geplante Ladezeiten waren:<br><br> " . $zeile2 ."<br>";
}
        while  (! feof($myfile))
                {$zeile2 = fgets($myfile);
                echo $zeile2 ."<br>";}
        fclose($myfile);



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

